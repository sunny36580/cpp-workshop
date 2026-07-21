"""
游戏摇杆/手柄数据读取模块

提供摇杆原始数据的读取、轴值映射和二进制序列化。
"""

import struct

import pygame

from .config import USE_LINUX_AXIS_MAP
from .protocol import calc_crc16


class JoystickHandler:
    """游戏摇杆处理器

    封装 pygame.joystick 的初始化、状态轮询和原始数据序列化。
    """

    def __init__(self):
        self.joystick = None
        self.connected = False
        self.name = ""
        self.num_axes = 0
        self.num_buttons = 0
        self.num_hats = 0

        self._init_joystick()

    def _init_joystick(self):
        """初始化游戏手柄"""
        count = pygame.joystick.get_count()
        print("检测到手柄数量：", count)
        if count == 0:
            print("未找到手柄，仅保留键盘/鼠标操作")
            return

        try:
            js = pygame.joystick.Joystick(0)
            js.init()
            self.joystick = js
            self.connected = True
            self.name = js.get_name()
            self.num_axes = js.get_numaxes()
            self.num_buttons = js.get_numbuttons()
            self.num_hats = js.get_numhats()
            print(f"\n=== 手柄 0 ===")
            print(f"名称: {self.name}")
            print(f"轴数: {self.num_axes}")
            print(f"按钮数: {self.num_buttons}")
            print(f"帽子数: {self.num_hats}")
        except Exception as e:
            print(f"手柄初始化失败: {e}")

    def read_raw(self) -> bytes:
        """读取摇杆当前所有轴、按钮原始值，序列化为精简二进制格式

        格式：
          [0xAA 0x55 帧头:2B][轴数:1B][轴0~N: int16小端 × N]
          [按钮数:1B][按钮0~M: uint8 × M][CRC16:2B 小端]
        """
        if not self.connected or self.joystick is None:
            return b""

        pygame.event.pump()
        buf = bytearray()

        # 帧头
        buf.extend([0xAA, 0x55])

        # 轴数据
        num_axes = self.num_axes
        buf.append(num_axes & 0xFF)
        for idx in range(num_axes):
            raw = self.joystick.get_axis(idx)
            if USE_LINUX_AXIS_MAP:
                center = -0.5 if idx % 2 == 0 else 0.5
                norm = -((raw - center) / 0.5)
            else:
                norm = -raw
            norm = max(-1.0, min(1.0, norm))
            val = int(norm * 32767)
            val = max(-32767, min(32767, val))
            buf.extend(struct.pack('<h', val))

        # 按钮数据
        num_btns = self.num_buttons
        buf.append(num_btns & 0xFF)
        for idx in range(num_btns):
            buf.append(self.joystick.get_button(idx))

        # CRC16
        crc = calc_crc16(bytes(buf[2:]))
        buf.extend(struct.pack('<H', crc))

        return bytes(buf)

    def get_axis_debug_str(self) -> str:
        """返回各轴当前值的调试字符串"""
        if not self.connected:
            return ""
        parts = []
        for i in range(min(self.num_axes, 6)):
            try:
                v = self.joystick.get_axis(i)
                if USE_LINUX_AXIS_MAP:
                    center = -0.5 if i % 2 == 0 else 0.5
                    v = -((v - center) / 0.5)
                else:
                    v = -v
                parts.append(f"A{i}:{v:+.3f}")
            except Exception:
                pass
        return "  ".join(parts)

    def get_button_count(self) -> int:
        """返回当前按下的按钮数量"""
        if not self.connected:
            return 0
        return sum(
            1 for i in range(self.num_buttons) if self.joystick.get_button(i)
        )
