"""
串口通信模块

提供串口打开/关闭、发送二进制帧、接收并解析 C++ 端回发的状态帧。
"""

import threading
import time

import serial

from .config import (
    SERIAL_PORT, SERIAL_BAUD,
    SERIAL_FRAME_LEN, SERIAL_HEADER_LEN,
    CMD_STATUS, MODULE_IDS,
)
from .protocol import calc_crc16


class SerialIO:
    """串口通信处理器

    维护串口连接，提供 send_frame() 发送，
    在后台线程中持续接收并解析状态帧，更新 module_statuses。
    """

    def __init__(self, on_log=None):
        self.ser: serial.Serial | None = None
        self.dtu_connected = False
        self.module_statuses: dict[str, bool] = {m: False for m in MODULE_IDS}
        self._on_log = on_log

        # 尝试打开串口
        try:
            self.ser = serial.Serial(
                port=SERIAL_PORT,
                baudrate=SERIAL_BAUD,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
            )
            print(f"✅ 串口已打开: {SERIAL_PORT} @ {SERIAL_BAUD} baud")
        except Exception as e:
            print(f"❌ 串口打开失败: {e}")
            print(f"   请检查 CH340 设备是否已连接，路径是否为 {SERIAL_PORT}")
            self.ser = None

        self.dtu_connected = self.ser is not None

        if self.ser:
            threading.Thread(target=self._rx_loop, daemon=True).start()

    # ---- 日志 ----
    def _log(self, msg, level="info"):
        if self._on_log:
            self._on_log(msg, level)

    # ---- 发送 ----
    def send_frame(self, frame: bytes):
        """通过串口发送二进制帧"""
        if self.ser is None:
            return
        try:
            self.ser.write(frame)
        except Exception as e:
            print(f"❌ 串口发送失败: {e}")

    def send_raw(self, data: bytes):
        """发送原始字节（用于摇杆透传等非标帧）"""
        if self.ser is None:
            return
        try:
            self.ser.write(data)
        except Exception as e:
            print(f"❌ 透传发送失败: {e}")

    # ---- 接收 ----
    def _rx_loop(self):
        """串口接收循环：解析 C++ 回发的状态帧"""
        rx_count = 0
        last_rx = time.time()
        rx_buf = b""
        while self.ser and self.ser.is_open:
            try:
                chunk = self.ser.read(SERIAL_FRAME_LEN)
                if not chunk:
                    if last_rx > 0 and time.time() - last_rx > 2.5:
                        print("[TIMEOUT] C++ 心跳超时，标记离线")
                        self.dtu_connected = False
                        for name in self.module_statuses:
                            self.module_statuses[name] = False
                        last_rx = 0
                    continue

                rx_buf += chunk

                while len(rx_buf) >= SERIAL_FRAME_LEN:
                    sof_idx = rx_buf.find(bytes([0xAA, 0x55]))
                    if sof_idx < 0:
                        rx_buf = b""
                        break
                    if sof_idx > 0:
                        rx_buf = rx_buf[sof_idx:]
                        continue
                    if len(rx_buf) < SERIAL_FRAME_LEN:
                        break

                    frame = rx_buf[:SERIAL_FRAME_LEN]

                    # CRC16 校验
                    recv_crc = frame[-2] | (frame[-1] << 8)
                    calc_crc = calc_crc16(frame[2:-2])
                    if recv_crc != calc_crc:
                        print("[CRC] CRC16 校验错误，丢弃")
                        rx_buf = rx_buf[1:]
                        continue

                    cmd_type = frame[2]
                    pay_len = frame[3]
                    data = frame[SERIAL_HEADER_LEN:SERIAL_HEADER_LEN + pay_len]

                    # 任何有效帧都刷新心跳计时
                    self.dtu_connected = True
                    last_rx = time.time()

                    if cmd_type == CMD_STATUS and len(data) >= 2:
                        mask = (data[0] << 8) | data[1]
                        for i, name in enumerate(MODULE_IDS):
                            self.module_statuses[name] = bool(mask & (1 << i))
                        rx_count += 1

                    rx_buf = rx_buf[SERIAL_FRAME_LEN:]

            except serial.SerialException:
                self.dtu_connected = False
                time.sleep(0.5)
            except Exception:
                pass
        self.dtu_connected = False

    # ---- 关闭 ----
    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.dtu_connected = False
