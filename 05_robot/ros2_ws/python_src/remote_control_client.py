import json
import pygame
import sys
import time
import serial
import socket
import threading
import numpy as np
import io
from enum import Enum
import websockets.sync.client

# ====================== 基础配置 ======================
LINEAR_SPEED_MAX = 0.6   # 运控节点线速度上限 (m/s)
ANGULAR_SPEED_MAX = 1.0  # 运控节点角速度上限 (rad/s)
CMD_SEND_RATE = 7       # 运动指令发送频率(Hz)

# 相机推流配置
CAMERA_IP = "192.168.9.253"   # 机端IP地址
CAMERA_PORT = 8888             # TCP 推流端口

# 串口配置（与 C++ 模块管理器一致）
# Windows 下 CH340 通常是 COM3，改为: SERIAL_PORT = "COM3"
# Linux  下 CH340 通常是 /dev/ttyUSB0 或 /dev/ttyCH340USB0
SERIAL_PORT = "/dev/ttyUSB1"
SERIAL_BAUD = 115200

# 二进制协议常量（32 字节固定帧，与 C++ 端保持一致）
SERIAL_SOF0 = 0xAA
SERIAL_SOF1 = 0x55
SERIAL_HEADER_LEN = 4   # SOF0 + SOF1 + CmdType + PayLen
SERIAL_DATA_LEN = 16    # 数据域固定 16 字节
SERIAL_RESERVED = 10    # 保留 10 字节
SERIAL_CRC_LEN = 2      # CRC16 2 字节
SERIAL_FRAME_LEN = SERIAL_HEADER_LEN + SERIAL_DATA_LEN + SERIAL_RESERVED + SERIAL_CRC_LEN  # = 32

# 指令类型
CMD_MOVE = 0x01      # 速度指令
CMD_TASK = 0x02      # 任务指令
CMD_HEARTBEAT = 0x03 # 心跳包
CMD_STATUS = 0x04    # 状态反馈

# ====================== 通信状态 + 模块ID映射 ======================
# 模块ID与C++端 bit 位一一对应
MODULE_IDS = ["lower_body", "upper_body", "imu_driver", "remote_interface", "usb_camera"]

# ====================== 指令类型枚举 ======================
class CmdType(Enum):
    MOVE = 1    # 0x01 运动控制: payload = linear_f32 + angular_f32
    TASK = 2    # 0x02 预设任务: payload = task_id_u8
    STOP = 3    # 0x03 紧急停止（与机端心跳复用同一数值，但由控制端主动发送）

# ====================== WebSocket 动作组协议 ======================
# 通过 WebSocket 向局域网内的动作组服务发送 play/reset 指令
# 协议为 JSON，支持双向通信：发指令、收 ACK + 事件推送
WS_ACTION_URI = "ws://192.168.9.253:9998/action"  # 动作组 WebSocket 服务
WS_CONNECT_TIMEOUT = 3.0   # 连接超时 (秒)
WS_CMD_TIMEOUT = 3.0       # 指令等待 ACK 超时 (秒)

class ActionCmdType(Enum):
    """WebSocket 动作指令类型"""
    PLAY = "play"   # 播放动作组
    RESET = "reset" # 动作归位

# ====================== WebSocket 握手协议 ======================
WS_HANDSHAKE_URI = "ws://192.168.9.253:9999/handshake"

class HandshakeCmdType(Enum):
    """握手交互指令"""
    AUTO = "auto"       # 自动感知模式
    FORCE_ON = "on"     # 强制握手开启
    FORCE_OFF = "off"   # 强制握手关闭

# ====================== 10个预设任务定义 ======================
TASK_LIST = {
    1: "语音动作组",
    2: "握手交互",
    3: "语音交互",
    4: "待机模式",
    5: "手指动作能力展示",
    6: "挥手动作",
    7: "表情头能力展示",
    8: "回到待机模式",
    9: "预留任务9",
    10: "预留任务10"
}

# 颜色（保留基本颜色供其他模块使用）
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREEN = (0, 255, 0)
RED = (255, 0, 0)
# ======================================================

# ====================== 跨平台中文字体查找 ======================
# Linux 上 pygame.font.match_font() 对中文字体名经常返回 None，
# 即使字体已安装。Windows 则直接使用 SysFont("microsoftyahei") 即可。
# 以下根据平台分流：
#   Windows → SysFont("microsoftyahei" / "simhei")
#   Linux   → 多级查找：fc-list → 常见路径硬编码 → match_font → SysFont

import subprocess
import os

_IS_WINDOWS = sys.platform == "win32" or sys.platform == "cygwin"

# ---------- Windows：直接用 SysFont ----------
_WIN_CN_FONT_NAME = "microsoftyahei"
_WIN_CN_MONO_NAME = "microsoftyahei"


def _load_font_windows(size, bold=False, mono=False):
    """Windows 上用 SysFont 直接指定中文字体名，效果可靠"""
    name = _WIN_CN_MONO_NAME if mono else _WIN_CN_FONT_NAME
    font = pygame.font.SysFont(name, size, bold=bold)
    # 验证字体是否真的包含中文（随便渲染一个中文字看宽高是否正常）
    test_surf = font.render("中", True, (0, 0, 0))
    if test_surf.get_width() > 4:
        return font
    # 万一没找到，用 None（pygame 默认字体），但概率极低
    return pygame.font.SysFont(None, size, bold=bold)


# ---------- Linux：多级查找策略 ----------
# 常见 Linux 中文字体文件路径（按优先级排列）
_LINUX_CN_FONT_PATHS = [
    # Noto Sans CJK
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    # Noto Sans CJK SC
    "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansCJKsc-Regular.ttf",
    # WQY
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
    # Source Han Sans
    "/usr/share/fonts/opentype/source-han-sans-sc/SourceHanSansSC-Regular.otf",
    "/usr/share/fonts/truetype/source-han-sans-sc/SourceHanSansSC-Regular.ttf",
    # Droid Sans Fallback
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
]

_LINUX_CN_MONO_PATHS = [
    # Noto Sans Mono CJK
    "/usr/share/fonts/opentype/noto/NotoSansMonoCJKsc-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansMonoCJK-Regular.ttf",
    # WQY (等宽)
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
]

_LINUX_FONT_CANDIDATES = [
    "notosanscjksc", "notosanscjk", "sourcehansanssc",
    "wenquanyimicrohei", "wqyzenhei",
    "noto sans cjk sc", "noto sans cjk",
    "wqy-microhei", "wqy-zenhei",
    "microsoftyahei", "simhei",
]

_LINUX_MONO_CANDIDATES = [
    "notosansmonocjksc", "notosansmonocjk", "notosanscjksc",
    "noto sans mono cjk sc",
    "wenquanyimicrohei", "wqyzenhei", "wqy-zenhei", "wqy-microhei",
]


def _linux_find_by_fc_list(mono=False):
    """使用 fc-list 查询系统已安装的中文字体文件路径"""
    try:
        result = subprocess.run(
            ["fc-list", ":lang=zh", "-f", "%{file}\n"],
            capture_output=True, text=True, timeout=3.0
        )
        if result.returncode != 0 or not result.stdout.strip():
            return None
        paths = [p.strip() for p in result.stdout.strip().split("\n") if p.strip()]
        if not paths:
            return None
        seen = set()
        unique = []
        for p in paths:
            key = os.path.basename(p)
            if key not in seen:
                seen.add(key)
                unique.append(p)
        if mono:
            for p in unique:
                if "mono" in os.path.basename(p).lower() and os.path.exists(p):
                    return p
        for p in unique:
            if os.path.exists(p):
                return p
    except Exception:
        pass
    return None


def _linux_find_by_common_paths(mono=False):
    """按常见字体文件路径查找"""
    paths = _LINUX_CN_MONO_PATHS if mono else _LINUX_CN_FONT_PATHS
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def _linux_find_by_match_font(mono=False, bold=False):
    """按字体名使用 pygame.match_font 查找"""
    candidates = _LINUX_MONO_CANDIDATES if mono else _LINUX_FONT_CANDIDATES
    for name in candidates:
        font_path = pygame.font.match_font(name, bold=bold)
        if font_path and os.path.exists(font_path):
            return font_path
    return None


def _load_font_linux(size, bold=False, mono=False):
    """Linux 多级策略加载中文字体"""
    font_path = _linux_find_by_fc_list(mono=mono)
    if not font_path:
        font_path = _linux_find_by_common_paths(mono=mono)
    if not font_path:
        font_path = _linux_find_by_match_font(mono=mono, bold=bold)
    if font_path:
        try:
            font = pygame.font.Font(font_path, size)
            font.set_bold(bold)
            return font
        except Exception:
            pass
    print("⚠️ 未找到中文字体，请安装 fonts-noto-cjk 或 fonts-wqy-microhei")
    return pygame.font.SysFont(None, size, bold=bold)


# ====================== 统一字体加载入口 ======================
def load_ui_font(size, bold=False, mono=False):
    """根据当前操作系统选择合适的中文字体加载方式"""
    if _IS_WINDOWS:
        return _load_font_windows(size, bold=bold, mono=mono)
    return _load_font_linux(size, bold=bold, mono=mono)


def calc_crc16(data: bytes) -> int:
    """CRC16-Modbus 校验，与 C++ 端 calcCRC16 一致"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def build_frame(cmd_type: int, payload: bytes = b"") -> bytes:
    """构建 32 字节固定帧:
       帧头(2) + CmdType(1) + PayLen(1) + 数据域(16) + 保留(10) + CRC16(2)
    """
    frame = bytes([SERIAL_SOF0, SERIAL_SOF1, cmd_type & 0xFF, len(payload) & 0xFF])

    # 数据域 16 字节（填充 0）
    data_field = bytearray(SERIAL_DATA_LEN)
    copy_len = min(len(payload), SERIAL_DATA_LEN)
    data_field[:copy_len] = payload[:copy_len]
    frame += bytes(data_field)

    # 保留 10 字节（全 0）
    frame += bytes(SERIAL_RESERVED)

    # CRC16（从 CmdType 到保留末尾）
    crc = calc_crc16(frame[2:])  # 从 CmdType 开始算
    frame += bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    assert len(frame) == SERIAL_FRAME_LEN, f"帧长度错误: {len(frame)} != {SERIAL_FRAME_LEN}"
    return frame


class RobotRemote:
    def __init__(self):
        pygame.init()
        self.win_w = 1200
        self.win_h = 760
        self.screen = pygame.display.set_mode((self.win_w, self.win_h))
        pygame.display.set_caption("三代人形机器人远程控制系统")

        # 字体
        self.font_title = load_ui_font(28, bold=True)
        self.font_md = load_ui_font(18)
        self.font_sm = load_ui_font(14)
        self.font_mono = load_ui_font(12, mono=True)

        # 摇杆透传配置
        self.joystick = None
        self.joystick_connected = False
        self.joystick_name = ""
        self.joystick_num_axes = 0
        self.joystick_num_buttons = 0
        self.joystick_num_hats = 0
        self.last_joystick_buf = b""  # 上一帧缓存，无变化不发送
        self.last_tip = "等待操作"
        self.last_joystick_send_time = 0

        # 相机画面（C键播放/停止，画面常驻）
        self.camera_playing = False     # 是否正在播放
        self.camera_active = False      # 线程是否运行中
        self.camera_frame = None
        self.camera_thread = None
        self.camera_running = False
        self.camera_frame_lock = threading.Lock()  # 保护 camera_frame
        self.camera_last_frame_time = 0  # 上次收到新帧的时间戳
        self.camera_frame_count = 0      # 收到的总帧数
        self.camera_fps_log_time = 0     # 上次fps日志时间
        self.camera_fps_frame_count = 0  # fps统计帧数

        # ========== 界面布局 ==========
        # 主任务配置
        self.main_tasks = [
            {"id": 1, "name": "1. 语音动作组", "subs": [f"语音段落 {i}" for i in range(1, 16)]},
            {"id": 2, "name": "2. 握手交互",   "subs": ["自动感知模式", "强制握手开启", "强制握手关闭"]},
            {"id": 3, "name": "3. 语音交互",   "subs": ["语音问答交互模式"]},
            {"id": 4, "name": "4. 待机模式",   "subs": ["待机模式（关闭非运控节点）"]},
        ]
        self.current_main_task = 0  # 当前选中的主任务索引
        self.sub_task_states = {}   # sub_id -> bool
        for i, main in enumerate(self.main_tasks):
            for j, _ in enumerate(main["subs"]):
                self.sub_task_states[f"{i}-{j}"] = False

        # 初始化摇杆
        self._init_joystick()

        # 日志
        self.logs = []
        self.add_log("远程控制系统已启动")
        self.add_log("433MHz控制链路已连接")
        self.add_log("915MHz图传链路已连接")
        if self.joystick_connected:
            self.add_log(f"摇杆已连接: {self.joystick_name}", "success")
        else:
            self.add_log("⚠️ 未检测到手柄，请连接后重启", "warning")

        # ========== WebSocket 动作组（Play / Reset）==========
        self.ws = None
        self.ws_connected = False
        self.ws_running = True
        self.ws_lock = threading.Lock()
        self.ws_ack_event = threading.Event()  # 等待 ACK 信号
        self.action_playing = False   # 是否正在播放动作组
        self.action_resetting = False # 是否正在归位
        # 后台连接 + 接收线程
        self.ws_connect_retry = True
        threading.Thread(target=self._ws_connect_loop, daemon=True).start()

        # ========== WebSocket 握手交互（AUTO / ON / OFF）==========
        self.hs_ws = None
        self.hs_connected = False
        self.hs_running = True
        self.hs_lock = threading.Lock()
        self.hs_ack_event = threading.Event()
        threading.Thread(target=self._hs_connect_loop, daemon=True).start()

        # 串口初始化
        try:
            self.ser = serial.Serial(
                port=SERIAL_PORT,
                baudrate=SERIAL_BAUD,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1
            )
            print(f"✅ 串口已打开: {SERIAL_PORT} @ {SERIAL_BAUD} baud")
        except Exception as e:
            print(f"❌ 串口打开失败: {e}")
            print(f"   请检查 CH340 设备是否已连接，路径是否为 {SERIAL_PORT}")
            self.ser = None

        self.module_statuses = {m: False for m in MODULE_IDS}
        self.dtu_connected = self.ser is not None
        self.lan_connected = False
        if self.ser:
            threading.Thread(target=self.serial_rx_loop, daemon=True).start()
        threading.Thread(target=self.lan_check_loop, daemon=True).start()

        print(f"✅ 运动指令发送频率: {CMD_SEND_RATE}Hz")

        # 串口仅用于摇杆透传，不再发送协议帧

    # -------------------------- 日志 --------------------------
    def add_log(self, message, level="info"):
        ts = time.strftime("%H:%M:%S")
        self.logs.append((ts, message, level))
        if len(self.logs) > 100:
            self.logs.pop(0)

    # -------------------------- LAN 链路探测 (SSH 22) --------------------------
    def lan_check_loop(self):
        """每3秒尝试TCP连接SSH端口判断局域网通断"""
        while True:
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(2.0)
                s.connect((CAMERA_IP, 22))
                s.close()
                self.lan_connected = True
            except Exception:
                self.lan_connected = False
            time.sleep(3.0)

    # -------------------------- 串口接收（解析C++回发的状态） --------------------------
    def serial_rx_loop(self):
        rx_count = 0
        last_rx = time.time()
        rx_buf = b""
        while self.ser and self.ser.is_open:
            try:
                # 读取原始数据
                chunk = self.ser.read(SERIAL_FRAME_LEN)
                if not chunk:
                    # 超时检测
                    if last_rx > 0 and time.time() - last_rx > 2.5:
                        print("[TIMEOUT] C++ 心跳超时，标记离线")
                        self.dtu_connected = False
                        for name in self.module_statuses:4
                        last_rx = 0
                    continue

                rx_buf += chunk

                # 尝试从缓存中解析帧（32 字节固定帧长）
                while len(rx_buf) >= SERIAL_FRAME_LEN:
                    # 找帧头 0xAA 0x55
                    sof_idx = rx_buf.find(bytes([SERIAL_SOF0, SERIAL_SOF1]))
                    if sof_idx < 0:
                        rx_buf = b""
                        break

                    if sof_idx > 0:
                        rx_buf = rx_buf[sof_idx:]
                        continue

                    if len(rx_buf) < SERIAL_FRAME_LEN:
                        break

                    frame = rx_buf[:SERIAL_FRAME_LEN]

                    # CRC16 校验（从 CmdType 到保留末尾）
                    recv_crc = frame[-2] | (frame[-1] << 8)
                    calc_crc = calc_crc16(frame[2:-2])  # 从 CmdType 到保留末尾
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
                        if rx_count % 10 == 0:
                            pass

                    rx_buf = rx_buf[SERIAL_FRAME_LEN:]

            except serial.SerialException:
                self.dtu_connected = False; time.sleep(0.5)
            except Exception:
                pass
        self.dtu_connected = False

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
            self.joystick_connected = True
            self.joystick_name = js.get_name()
            self.joystick_num_axes = js.get_numaxes()
            self.joystick_num_buttons = js.get_numbuttons()
            self.joystick_num_hats = js.get_numhats()
            print(f"\n=== 手柄 0 ===")
            print(f"名称: {self.joystick_name}")
            print(f"轴数: {self.joystick_num_axes}")
            print(f"按钮数: {self.joystick_num_buttons}")
            print(f"帽子数: {self.joystick_num_hats}")
        except Exception as e:
            print(f"手柄初始化失败: {e}")

    # -------------------------- 指令封装（二进制协议）--------------------------
    def send_frame(self, frame: bytes):
        """通过串口发送二进制帧"""
        if self.ser is None:
            return
        try:
            self.ser.write(frame)
        except Exception as e:
            print(f"❌ 串口发送失败: {e}")

    def _read_joystick_raw(self) -> bytes:
        """读取摇杆当前所有轴、按钮、十字帽原始值，序列化为字节流

        格式（参考遥控器透传协议）：
          - 每个轴: 2字节小端 uint16, 范围 0~2047 (将 -1~1 映射到 0~2047)
          - 每个按钮: 1字节, 0/1
          - 每个十字帽: 2字节, (hx+2, hy+2)
        """
        if not self.joystick_connected or self.joystick is None:
            return b""

        pygame.event.pump()  # 刷新手柄状态
        buf = bytearray()

        # 摇杆轴：浮点转2字节整型（-1~1 → 0~2047）
        for idx in range(self.joystick_num_axes):
            val = int((self.joystick.get_axis(idx) + 1) * 1023.5)
            buf.append(val & 0xFF)
            buf.append((val >> 8) & 0xFF)

        # 按钮：按下1 / 松开0，单字节
        for idx in range(self.joystick_num_buttons):
            buf.append(self.joystick.get_button(idx))

        # 十字帽方向
        for idx in range(self.joystick_num_hats):
            hx, hy = self.joystick.get_hat(idx)
            buf.append(hx + 2)
            buf.append(hy + 2)

        return bytes(buf)

    def send_task_cmd(self, task_num):
        """发送预设任务指令：
           帧: 0xAA | 0x02 | 0x01 | task_id_u8 | checksum
        """
        payload = bytes([task_num & 0xFF])
        frame = build_frame(CmdType.TASK.value, payload)
        self.send_frame(frame)

    # -------------------------- 相机画面 --------------------------
    def toggle_camera_play(self):
        """C键切换播放/停止（画面常驻，C键只控制数据流）"""
        if self.camera_playing:
            self.stop_camera()
        else:
            self.start_camera()

    def start_camera(self):
        """启动相机接收线程"""
        self.camera_playing = True
        self.camera_active = True
        self.camera_running = True
        self.last_tip = "相机播放中..."
        self.camera_thread = threading.Thread(target=self.camera_loop, daemon=True)
        self.camera_thread.start()

    def stop_camera(self):
        """停止相机接收（保留最后一帧画面）"""
        self.camera_running = False
        self.camera_active = False
        self.camera_playing = False
        self.last_tip = "相机已暂停"

    def camera_loop(self):
        """相机接收线程：TCP → H.264 → pygame 解码（带自动重连）"""
        while self.camera_running and self.camera_playing:
            sock = None
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2.0)  # 2 秒超时，更快检测断开
                sock.connect((CAMERA_IP, CAMERA_PORT))
                self.last_tip = "相机已连接"
                self.add_log("TCP 相机已连接", "success")
                print("✅ TCP 相机已连接")

                # 用 Python 的 av 库来解码
                try:
                    import av
                    codec = av.CodecContext.create('h264', 'r')
                except ImportError:
                    self.add_log("需要安装 av 库: pip install av", "error")
                    print("⚠ 需要安装 av 库: pip install av")
                    raise

                while self.camera_running and self.camera_playing:
                    # 读4字节数据长度
                    try:
                        len_data = sock.recv(4)
                    except socket.timeout:
                        self.add_log("相机接收超时，主动断开重连", "warning")
                        break
                    if not len_data:
                        print("[CAM] recv(4) returned empty, connection closed")
                        break
                    length = int.from_bytes(len_data, 'big')
                    if length > 500000:
                        print(f"[CAM] length too large: {length}, 帧边界错位，断开重连")
                        break

                    # 读完整 H.264 数据
                    data = b''
                    while len(data) < length:
                        packet = sock.recv(length - len(data))
                        if not packet:
                            print(f"[CAM] recv payload failed at {len(data)}/{length}")
                            break
                        data += packet

                    if len(data) != length:
                        print(f"[CAM] payload incomplete: got {len(data)}/{length}")
                        break

                    # 用 PyAV 解码 H.264 ES
                    frame_received = False
                    try:
                        frames = codec.decode(av.Packet(data))
                        for frame in frames:
                            frame_received = True
                            img = frame.to_ndarray(format='bgr24')
                            raw = pygame.image.frombuffer(img.tobytes(), (img.shape[1], img.shape[0]), "BGR")
                            with self.camera_frame_lock:
                                self.camera_frame = raw
                            # 每帧耗时统计
                            now = time.time()
                            frame_interval = now - self.camera_last_frame_time if self.camera_last_frame_time > 0 else 0
                            self.camera_last_frame_time = now
                            self.camera_frame_count += 1
                            self.camera_fps_frame_count += 1
                            # 记录最大帧间隔
                            if not hasattr(self, 'camera_max_interval'):
                                self.camera_max_interval = 0
                                self.camera_interval_log_time = now
                                self.camera_interval_frame_count = 0
                            if frame_interval > self.camera_max_interval:
                                self.camera_max_interval = frame_interval
                            self.camera_interval_frame_count += 1
                            if self.camera_interval_frame_count >= 300:
                                print(f"[CAM] 最近300帧: 最大帧间隔={self.camera_max_interval*1000:.0f}ms")
                                self.camera_max_interval = 0
                                self.camera_interval_frame_count = 0
                                self.camera_interval_log_time = now
                            if now - self.camera_fps_log_time >= 10.0:
                                actual_fps = self.camera_fps_frame_count / (now - self.camera_fps_log_time)
                                print(f"[CAM] 实际接收帧率: {actual_fps:.1f} fps, 总帧数: {self.camera_frame_count}")
                                self.camera_fps_log_time = now
                                self.camera_fps_frame_count = 0
                    except Exception as e:
                        print(f"[CAM] decode error: {e}")
                        pass

                    if not frame_received:
                        # 连续收不到帧说明解码器卡死了，主动断开让外层重连
                        print(f"[CAM] no frame decoded from {length} bytes, 解码器可能卡死，断开重连")
                        break
                    else:
                        pass  # 正常收到帧

            except socket.timeout:
                self.last_tip = "相机连接超时"
                print("❌ 相机连接超时")
            except ConnectionRefusedError:
                self.last_tip = "相机服务未启动"
                print("❌ 相机服务未启动")
            except (OSError, BrokenPipeError) as e:
                self.last_tip = f"相机断开: {str(e)[:20]}"
                print(f"❌ 相机断开: {e}")
            except Exception as e:
                self.last_tip = f"相机错误: {str(e)[:30]}"
                print(f"❌ 相机错误: {e}")
            finally:
                if sock:
                    try:
                        sock.close()
                    except:
                        pass

            # 立即重连，不等待
            if self.camera_running and self.camera_playing:
                print("📷 相机断开，立即重连...")
                # 不等待，直接进入外层循环重试
            else:
                break

    def send_stop_cmd(self):
        """发送紧急停止指令：
           帧: 0xAA | 0x03 | 0x00 | checksum
        """
        frame = build_frame(CmdType.STOP.value)
        self.send_frame(frame)

    # -------------------------- WebSocket 连接 & 接收 ----------
    def _ws_connect_loop(self):
        """后台线程：保持 WebSocket 连接，断线自动重连，收取服务端推送"""
        while self.ws_running:
            try:
                with self.ws_lock:
                    if self.ws:
                        try:
                            self.ws.close()
                        except:
                            pass
                        self.ws = None
                    self.ws_connected = False

                ws = websockets.sync.client.connect(
                    WS_ACTION_URI, timeout=WS_CONNECT_TIMEOUT)
                print(f"✅ WebSocket 已连接: {WS_ACTION_URI}")

                with self.ws_lock:
                    self.ws = ws
                    self.ws_connected = True

                # 接收循环
                while self.ws_running:
                    try:
                        raw = ws.recv()
                        if raw is None:
                            break
                        msg = json.loads(raw)
                        event = msg.get("event", "")
                        # ACK 响应（播放/重置指令确认）
                        if event == "ack":
                            self.ws_ack_event.set()
                        # 播放完成通知
                        elif event == "completed":
                            self.action_playing = False
                            self.add_log("动作组播放完成", "success")
                            print("[WS] 动作组播放完成")
                        # 错误通知
                        elif event == "error":
                            self.add_log(f"动作组错误: {msg.get('msg', '')}", "error")
                            print(f"[WS] 动作组错误: {msg}")
                    except json.JSONDecodeError:
                        continue
                    except (websockets.ConnectionClosed, OSError):
                        break

            except (websockets.InvalidURI, websockets.InvalidHandshake,
                    OSError, TimeoutError) as e:
                if self.ws_running:
                    print(f"⚠️ WebSocket 连接失败 ({e}), 5s 后重试...")
                    self.add_log("动作组服务未连接，等待重试...", "warning")

            except Exception as e:
                print(f"❌ WebSocket 异常: {e}")

            with self.ws_lock:
                self.ws_connected = False
                self.ws = None

            if self.ws_running:
                time.sleep(5.0)  # 重连间隔

    # -------------------------- WebSocket 动作指令发送 ----------
    def send_action_cmd(self, cmd_type: ActionCmdType, para: int = 0) -> bool:
        """通过 WebSocket 发送动作指令，等待 ACK 确认。
           para: 语音段落号（0=播放整组）
           返回 True 表示收到服务端 ACK。
        """
        msg = {"cmd": cmd_type.value}
        if cmd_type == ActionCmdType.PLAY and para > 0:
            msg["para"] = para

        with self.ws_lock:
            if not self.ws or not self.ws_connected:
                self.add_log(f"动作组服务未连接，无法发送 {cmd_type.value}", "warning")
                return False
            ws = self.ws

        try:
            self.ws_ack_event.clear()
            ws.send(json.dumps(msg))
            print(f"📤 WS -> {WS_ACTION_URI}  {msg}")

            # 等待服务端 ACK
            if self.ws_ack_event.wait(timeout=WS_CMD_TIMEOUT):
                self.add_log(f"✅ 动作指令确认: {cmd_type.value}", "success")
                return True
            else:
                self.add_log(f"⚠️ 动作指令超时: {cmd_type.value}", "warning")
                return False

        except (websockets.ConnectionClosed, OSError, AttributeError) as e:
            self.add_log(f"WebSocket 发送失败: {e}", "error")
            with self.ws_lock:
                self.ws_connected = False
            return False

    # -------------------------- 握手 WebSocket 连接 & 接收 ----------
    def _hs_connect_loop(self):
        """后台线程：保持握手服务 WebSocket 连接"""
        while self.hs_running:
            try:
                with self.hs_lock:
                    if self.hs_ws:
                        try:
                            self.hs_ws.close()
                        except:
                            pass
                        self.hs_ws = None
                    self.hs_connected = False

                ws = websockets.sync.client.connect(
                    WS_HANDSHAKE_URI, timeout=WS_CONNECT_TIMEOUT)
                print(f"✅ 握手 WebSocket 已连接: {WS_HANDSHAKE_URI}")

                with self.hs_lock:
                    self.hs_ws = ws
                    self.hs_connected = True

                while self.hs_running:
                    try:
                        raw = ws.recv()
                        if raw is None:
                            break
                        msg = json.loads(raw)
                        event = msg.get("event", "")
                        if event == "ack":
                            self.hs_ack_event.set()
                        elif event == "completed":
                            self.add_log("握手动作完成", "success")
                            print("[HS] 握手动作完成")
                        elif event == "error":
                            self.add_log(f"握手错误: {msg.get('msg', '')}", "error")
                    except json.JSONDecodeError:
                        continue
                    except (websockets.ConnectionClosed, OSError):
                        break

            except (websockets.InvalidURI, websockets.InvalidHandshake,
                    OSError, TimeoutError):
                if self.hs_running:
                    print("⚠️ 握手 WebSocket 连接失败，5s 后重试...")

            except Exception as e:
                print(f"❌ 握手 WebSocket 异常: {e}")

            with self.hs_lock:
                self.hs_connected = False
                self.hs_ws = None

            if self.hs_running:
                time.sleep(5.0)

    # -------------------------- 握手指令发送 ----------
    def send_handshake_cmd(self, cmd: HandshakeCmdType) -> bool:
        """通过 WebSocket 发送握手指令，等待 ACK"""
        msg = {"cmd": cmd.value}

        with self.hs_lock:
            if not self.hs_ws or not self.hs_connected:
                self.add_log("握手服务未连接", "warning")
                return False
            ws = self.hs_ws

        try:
            self.hs_ack_event.clear()
            ws.send(json.dumps(msg))
            print(f"📤 HS -> {WS_HANDSHAKE_URI}  {msg}")

            if self.hs_ack_event.wait(timeout=WS_CMD_TIMEOUT):
                self.add_log(f"✅ 握手指令确认: {cmd.value}", "success")
                return True
            else:
                self.add_log(f"⚠️ 握手指令超时: {cmd.value}", "warning")
                return False

        except (websockets.ConnectionClosed, OSError, AttributeError) as e:
            self.add_log(f"握手 WebSocket 发送失败: {e}", "error")
            with self.hs_lock:
                self.hs_connected = False
            return False

    # -------------------------- 左侧任务面板鼠标点击 ----------
    def _handle_task_click(self, mx, my):
        panel_y = 72
        panel_h = self.win_h - panel_y - 100
        left_w = int(self.win_w * 0.23)
        left_x = 12

        # 检查是否在左侧面板范围内
        if not (left_x <= mx <= left_x + left_w and panel_y <= my <= panel_y + panel_h):
            return

        content_y = panel_y + 42
        main_w = int(left_w * 0.4) - 6
        sub_w = left_w - main_w - 16
        sub_x = left_x + 10 + main_w + 6   # 子任务列左边界
        btn_w = (sub_w - 10) // 2           # 两按钮平分子任务列宽度
        btn_h = 32
        btn_gap = 10
        voice_group_idx = 0  # "1. 语音动作组" 对应 index 0

        # 主任务点击
        main_y = content_y
        for i, main in enumerate(self.main_tasks):
            rect = pygame.Rect(left_x + 10, main_y, main_w, 28)
            if rect.collidepoint(mx, my):
                self.current_main_task = i
                self.add_log(f"切换到主任务: {main['name']}", "info")
                return
            main_y += 32

        # 子任务点击（含语音动作组专属按钮和 UDP 发送）
        sub_y = content_y
        current_main = self.main_tasks[self.current_main_task]
        is_voice_group = (self.current_main_task == voice_group_idx)

        # 语音动作组：子任务区域顶部先放 Play/Reset 按钮
        if is_voice_group:
            sub_y += 44  # 按钮区域高度偏移

        for j, sub_name in enumerate(current_main["subs"]):
            rect = pygame.Rect(left_x + 10 + main_w + 6, sub_y, sub_w, 28)
            if rect.collidepoint(mx, my):
                # ---- 语音动作组子任务 → WebSocket 发送 ----
                if is_voice_group:
                    paragraph_num = j + 1  # 语音段落 1-15
                    success = self.send_action_cmd(
                        ActionCmdType.PLAY, para=paragraph_num)
                    if success:
                        self.action_playing = True
                        self.action_resetting = False
                        self.last_tip = f"语音段落 {paragraph_num}"
                    else:
                        self.last_tip = f"语音段落 {paragraph_num} 发送失败"
                else:
                    # ---- 握手交互子任务 → WebSocket 发送 ----
                    if self.current_main_task == 1:  # "2. 握手交互"
                        handshake_cmds = [
                            HandshakeCmdType.AUTO,      # j=0: 自动感知模式
                            HandshakeCmdType.FORCE_ON,  # j=1: 强制握手开启
                            HandshakeCmdType.FORCE_OFF, # j=2: 强制握手关闭
                        ]
                        if j < len(handshake_cmds):
                            success = self.send_handshake_cmd(handshake_cmds[j])
                            if success:
                                self.last_tip = sub_name
                            else:
                                self.last_tip = f"{sub_name} 发送失败"
                    else:
                        # ---- 其他子任务 → 保持原有点击切换逻辑 ----
                        sid = f"{self.current_main_task}-{j}"
                        new_state = not self.sub_task_states.get(sid, False)
                        self.sub_task_states[sid] = new_state
                        if new_state:
                            self.add_log(f"已开启子任务: {sub_name}", "success")
                        else:
                            self.add_log(f"已关闭子任务: {sub_name}", "info")
                return
            sub_y += 32

        # ---- 语音动作组按钮点击（在子任务区域顶部）----
        if is_voice_group:
            btn_y = content_y + 4
            play_x = sub_x
            reset_x = sub_x + btn_w + btn_gap

            if pygame.Rect(play_x, btn_y, btn_w, btn_h).collidepoint(mx, my):
                if not self.action_playing:
                    self.action_playing = True
                    self.action_resetting = False
                    self.send_action_cmd(ActionCmdType.PLAY)
                    self.add_log("▶️ 动作组播放中...", "success")
                    self.last_tip = "动作组播放中"
                else:
                    self.add_log("动作组已在播放中", "info")
                return

            if pygame.Rect(reset_x, btn_y, btn_w, btn_h).collidepoint(mx, my):
                self.action_playing = False
                self.action_resetting = True
                self.send_action_cmd(ActionCmdType.RESET)
                self.add_log("⏹ 动作组已归位", "warning")
                self.last_tip = "动作归位完成"
                return

    # -------------------------- UI 绘制 --------------------------
    def draw_ui(self):
        self.screen.fill((26, 32, 44))  # bg-gray-900

        # ==================== 标题栏 ====================
        title = self.font_title.render("三代人形机器人远程控制系统", True, (96, 165, 250))
        self.screen.blit(title, (self.win_w//2 - title.get_width()//2, 10))
        sub = self.font_sm.render("433MHz控制链路 | 915MHz图传链路", True, (156, 163, 175))
        self.screen.blit(sub, (self.win_w//2 - sub.get_width()//2, 44))

        # ==================== 三列布局 ====================
        panel_y = 72
        panel_h = self.win_h - panel_y - 100  # 留底部给WASD

        # ---- 左侧面板：任务控制 (col-span-3) ----
        left_w = int(self.win_w * 0.23)
        left_x = 12
        self._draw_panel(left_x, panel_y, left_w, panel_h, "任务指令控制", (96, 165, 250, 255))
        self._draw_task_panel(left_x, panel_y, left_w, panel_h)

        # ---- 中间面板：视频 (col-span-6) ----
        mid_x = left_x + left_w + 10
        mid_w = int(self.win_w * 0.5)
        self._draw_panel(mid_x, panel_y, mid_w, panel_h, "机器人胸口摄像头", (96, 165, 250, 255))
        self._draw_video_panel(mid_x, panel_y, mid_w, panel_h)

        # ---- 右侧面板：操作日志 (col-span-3) ----
        right_x = mid_x + mid_w + 10
        right_w = self.win_w - right_x - 12
        self._draw_panel(right_x, panel_y, right_w, panel_h, "操作日志", (96, 165, 250, 255))
        self._draw_log_panel(right_x, panel_y, right_w, panel_h)

        # ==================== 底部摇杆状态区 ====================
        self._draw_joystick_panel()

        pygame.display.flip()

    # ---------- 辅助：画面板背景 ----------
    def _draw_panel(self, x, y, w, h, title, color):
        pygame.draw.rect(self.screen, (31, 41, 55), (x, y, w, h), border_radius=8)
        title_surf = self.font_md.render(title, True, (color[0], color[1], color[2]))
        self.screen.blit(title_surf, (x + 12, y + 8))
        pygame.draw.line(self.screen, (75, 85, 99), (x + 12, y + 34), (x + w - 12, y + 34), 1)

    # ---------- 左侧：任务面板 ----------
    def _draw_task_panel(self, px, py, pw, ph):
        content_y = py + 42
        content_h = ph - 50
        main_w = int(pw * 0.4) - 6
        sub_w = pw - main_w - 16
        sub_x = px + 10 + main_w + 6     # 子任务列左边界
        is_voice_group = (self.current_main_task == 0)  # "1. 语音动作组"
        voice_group_action_h = 44  # Play/Reset 按钮区域高度
        btn_w = (sub_w - 10) // 2   # 两按钮平分子任务列宽度
        btn_h = 32
        btn_gap = 10

        # ========== 主任务列表（左半）==========
        main_y = content_y
        for i, main in enumerate(self.main_tasks):
            active = (i == self.current_main_task)
            bg = (30, 64, 175) if active else (55, 65, 81)
            rect = pygame.Rect(px + 10, main_y, main_w, 28)
            pygame.draw.rect(self.screen, bg, rect, border_radius=4)
            if active:
                pygame.draw.rect(self.screen, (96, 165, 250), rect, 2, border_radius=4)
            txt = self.font_sm.render(main["name"], True, (255, 255, 255))
            self.screen.blit(txt, (rect.x + 6, rect.y + 5))
            main_y += 32

        # ========== 子任务列表（右半）==========
        sub_y = content_y
        current_main = self.main_tasks[self.current_main_task]

        # 语音动作组：在子任务区域顶部绘制 Play/Reset 按钮
        if is_voice_group:
            btn_y = content_y + 4
            play_x = sub_x
            reset_x = sub_x + btn_w + btn_gap

            # Play 按钮（与子任务项对齐）
            play_color = (21, 128, 61) if self.action_playing else (22, 163, 74)
            play_rect = pygame.Rect(play_x, btn_y, btn_w, btn_h)
            pygame.draw.rect(self.screen, play_color, play_rect, border_radius=6)
            if self.action_playing:
                pygame.draw.rect(self.screen, (74, 222, 128), play_rect, 2, border_radius=6)
            play_txt = self.font_sm.render("▶ Play", True, (255, 255, 255))
            self.screen.blit(play_txt, (play_rect.x + 6, play_rect.y + 6))

            # Reset 按钮
            reset_color = (185, 28, 28) if self.action_resetting else (220, 38, 38)
            reset_rect = pygame.Rect(reset_x, btn_y, btn_w, btn_h)
            pygame.draw.rect(self.screen, reset_color, reset_rect, border_radius=6)
            if self.action_resetting:
                pygame.draw.rect(self.screen, (252, 165, 165), reset_rect, 2, border_radius=6)
            reset_txt = self.font_sm.render("⟳ Reset", True, (255, 255, 255))
            self.screen.blit(reset_txt, (reset_rect.x + 6, reset_rect.y + 6))

            # 按钮下分隔线
            sep_y = btn_y + btn_h + 4
            pygame.draw.line(self.screen, (75, 85, 99), (sub_x, sep_y),
                             (px + pw - 10, sep_y), 1)

            sub_y += voice_group_action_h

        for j, sub_name in enumerate(current_main["subs"]):
            sid = f"{self.current_main_task}-{j}"
            on = self.sub_task_states.get(sid, False) if not is_voice_group else False
            bg = (55, 65, 81)
            rect = pygame.Rect(px + 10 + main_w + 6, sub_y, sub_w, 28)
            pygame.draw.rect(self.screen, bg, rect, border_radius=4)
            txt = self.font_sm.render(sub_name, True, (255, 255, 255))
            self.screen.blit(txt, (rect.x + 6, rect.y + 5))
            # 语音组段落点击即发 UDP，不显示状态灯；其他任务保持状态灯
            if not is_voice_group:
                dot_color = (34, 197, 94) if on else (107, 114, 128)
                pygame.draw.circle(self.screen, dot_color, (rect.right - 10, rect.y + 14), 5)
            sub_y += 32

        # 底部：系统状态
        status_y = py + ph - 70
        pygame.draw.line(self.screen, (75, 85, 99), (px + 12, status_y - 6), (px + pw - 12, status_y - 6), 1)
        st = self.font_sm.render("系统状态", True, (134, 239, 172))
        self.screen.blit(st, (px + 12, status_y))
        ctrl = "已连接" if self.dtu_connected else "已断开"
        ctrl_c = (34, 197, 94) if self.dtu_connected else (239, 68, 68)
        video = "已连接" if self.lan_connected else "已断开"
        video_c = (34, 197, 94) if self.lan_connected else (239, 68, 68)
        self.screen.blit(self.font_sm.render(f"控制链路:", True, (255,255,255)), (px + 12, status_y + 22))
        self.screen.blit(self.font_sm.render(ctrl, True, ctrl_c), (px + pw - 90, status_y + 22))
        self.screen.blit(self.font_sm.render(f"图传链路:", True, (255,255,255)), (px + 12, status_y + 42))
        self.screen.blit(self.font_sm.render(video, True, video_c), (px + pw - 90, status_y + 42))

    # ---------- 中间：视频面板 ----------
    def _draw_video_panel(self, px, py, pw, ph):
        content_y = py + 42
        vw = pw - 24
        vh = ph - 80
        # 视频背景
        pygame.draw.rect(self.screen, (0, 0, 0), (px + 12, content_y, vw, vh), border_radius=6)
        if self.camera_frame is not None and self.camera_playing:
            # 缩放视频到合适大小
            frame_scaled = pygame.transform.scale(self.camera_frame, (vw, vh))
            self.screen.blit(frame_scaled, (px + 12, content_y))
        elif not self.camera_playing:
            # 暂停覆盖
            overlay = pygame.Surface((vw, vh), pygame.SRCALPHA)
            overlay.fill((0, 0, 0, 180))
            self.screen.blit(overlay, (px + 12, content_y))
            pause = self.font_md.render("⏸ 已暂停 [C键播放]", True, (255, 255, 255))
            self.screen.blit(pause, (px + 12 + vw//2 - pause.get_width()//2,
                                     content_y + vh//2 - pause.get_height()//2))
        else:
            # 等待连接
            wait = self.font_md.render("等待视频流连接...", True, (107, 114, 128))
            self.screen.blit(wait, (px + 12 + vw//2 - wait.get_width()//2,
                                    content_y + vh//2 - wait.get_height()//2))

        # 视频信息
        info_y = content_y + vh + 8
        self.screen.blit(self.font_sm.render("分辨率: 480P@20fps", True, (156, 163, 175)), (px + 12, info_y))
        cw = int(self.font_sm.size("分辨率: 480P@20fps")[0])
        self.screen.blit(self.font_sm.render("编码: H.264", True, (156, 163, 175)), (px + 12 + cw + 30, info_y))
        self.screen.blit(self.font_sm.render("延迟: <200ms", True, (156, 163, 175)), (px + pw - 90, info_y))

    # ---------- 右侧：日志面板 ----------
    def _draw_log_panel(self, px, py, pw, ph):
        content_y = py + 42
        log_h = ph - 50
        # 画日志背景
        log_rect = pygame.Rect(px + 10, content_y, pw - 20, log_h)
        pygame.draw.rect(self.screen, (17, 24, 39), log_rect, border_radius=4)

        # 显示最近日志
        vis_count = min(len(self.logs), int(log_h / 18))
        start = len(self.logs) - vis_count
        for i in range(vis_count):
            ts, msg, level = self.logs[start + i]
            if level == "success":
                c = (34, 197, 94)
            elif level == "warning":
                c = (250, 204, 21)
            elif level == "error":
                c = (239, 68, 68)
            else:
                c = (156, 163, 175)
            txt = f"[{ts}] {msg}"
            surf = self.font_mono.render(txt, True, c)
            self.screen.blit(surf, (log_rect.x + 6, log_rect.y + 6 + i * 18))

        # 操作说明
        help_y = content_y + log_h + 10
        help_title = self.font_md.render("操作说明", True, (250, 204, 21))
        self.screen.blit(help_title, (px + 12, help_y))
        helps = [
            "• 摇杆: 控制机器人移动",
            "• 数字键1-4: 切换主任务分类",
            "• C键: 相机播放/暂停",
        ]
        for i, h in enumerate(helps):
            self.screen.blit(self.font_sm.render(h, True, (209, 213, 219)), (px + 12, help_y + 24 + i * 20))

    # ---------- 底部：摇杆状态 ----------
    def _draw_joystick_panel(self):
        panel_y = self.win_h - 90
        panel_h = 82
        pygame.draw.rect(self.screen, (31, 41, 55), (12, panel_y, self.win_w - 24, panel_h), border_radius=8)
        title = self.font_md.render("遥控器数据透传", True, (96, 165, 250))
        self.screen.blit(title, (24, panel_y + 6))
        pygame.draw.line(self.screen, (75, 85, 99), (24, panel_y + 30), (self.win_w - 24, panel_y + 30), 1)

        # 摇杆状态信息
        info_x = 30
        info_y = panel_y + 40

        if self.joystick_connected:
            # 显示手柄连接信息
            conn_color = (34, 197, 94)
            conn_text = f"✅ {self.joystick_name} | 轴:{self.joystick_num_axes} 按钮:{self.joystick_num_buttons} 十字帽:{self.joystick_num_hats}"
        else:
            conn_color = (239, 68, 68)
            conn_text = "❌ 未检测到手柄"

        self.screen.blit(self.font_sm.render(conn_text, True, conn_color), (info_x, info_y))

        # 显示当前各轴数值（仅当有手柄时）
        if self.joystick_connected:
            axis_strs = []
            for i in range(min(self.joystick_num_axes, 6)):  # 最多显示6个轴
                try:
                    v = self.joystick.get_axis(i)
                    axis_strs.append(f"A{i}:{v:+.2f}")
                except:
                    pass
            axis_text = "  ".join(axis_strs)
            self.screen.blit(self.font_sm.render(axis_text, True, (156, 163, 175)), (info_x + 12, info_y + 22))

            # 按钮快速预览
            btn_on = sum(1 for i in range(self.joystick_num_buttons) if self.joystick.get_button(i))
            btn_text = f"按钮按下: {btn_on}/{self.joystick_num_buttons}"
            cw = int(self.font_sm.size(axis_text)[0]) if axis_text else 0
            self.screen.blit(self.font_sm.render(btn_text, True, (250, 204, 21)), (info_x + 12 + cw + 40, info_y + 22))
        else:
            self.screen.blit(self.font_sm.render("请在启动前连接USB手柄", True, (107, 114, 128)), (info_x + 12, info_y + 22))

        # 底部提示
        hint = self.font_sm.render("摇杆=运动控制 | 数字键1-4=切换主任务 | C=相机 | ESC=退出", True, (156, 163, 175))
        self.screen.blit(hint, (self.win_w//2 - hint.get_width()//2, panel_y + panel_h - 18))

    def run(self):
        clock = pygame.time.Clock()
        running = True
        # 摇杆透传降频配置（从配置常量读取 FPS）
        JOYSTICK_FPS = 15                     # 透传发送帧率
        joystick_interval = 1.0 / JOYSTICK_FPS
        print("========== 三代人形机器人远程控制系统 ==========")
        print(f"✅ 摇杆透传降频 {JOYSTICK_FPS}Hz | 数字键 1-4 切换主任务")
        print(f"✅ 摇杆: {self.joystick_name if self.joystick_connected else '未连接'}")
        print("================================================\n")

        while running:
            current_time = time.time()

            # 事件循环
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False

                if event.type == pygame.KEYDOWN:
                    # 数字键1-4：仅切换左侧任务面板，不再发送串口指令
                    if pygame.K_1 <= event.key <= pygame.K_4:
                        idx = event.key - pygame.K_1
                        if idx < len(self.main_tasks):
                            self.current_main_task = idx
                            self.add_log(f"切换到主任务: {self.main_tasks[idx]['name']}", "info")
                            self.last_tip = f"{self.main_tasks[idx]['name']}"

                    # C 键播放/暂停相机
                    elif event.key == pygame.K_c:
                        self.toggle_camera_play()

                    # ESC 退出
                    elif event.key == pygame.K_ESCAPE:
                        running = False

                # ---- 鼠标点击：左侧任务面板 ----

                # ---- 鼠标点击：左侧任务面板 ----
                if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    mx, my = event.pos
                    self._handle_task_click(mx, my)

            # ===================== 【摇杆透传降频发送】 =====================
            if self.joystick_connected and self.ser and self.ser.is_open:
                # 按降频间隔发送
                if current_time - self.last_joystick_send_time >= joystick_interval:
                    buf = self._read_joystick_raw()
                    if buf:
                        # 数据无变化则跳过发送（进一步节流）
                        if buf != self.last_joystick_buf:
                            try:
                                self.ser.write(buf)
                                self.last_joystick_buf = buf
                            except Exception as e:
                                print(f"❌ 透传发送失败: {e}")
                        self.last_joystick_send_time = current_time
            # ===============================================================

            self.draw_ui()
            clock.tick(60)

        # 退出前关闭相机、串口和 WebSocket
        self.ws_running = False
        self.hs_running = False
        self.camera_running = False
        self.camera_playing = False
        self.camera_active = False
        self.camera_frame = None
        if self.ser:
            self.ser.close()
        pygame.quit()
        print("程序已安全退出")

if __name__ == "__main__":
    app = RobotRemote()
    app.run()