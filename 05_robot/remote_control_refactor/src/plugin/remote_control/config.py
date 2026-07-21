"""
配置常量与枚举

所有与硬件、网络、协议相关的常量集中管理。
修改配置只需改此文件，无需深入业务逻辑。
"""

from enum import Enum

# ====================== 运动控制 ======================
LINEAR_SPEED_MAX = 0.6   # 运控节点线速度上限 (m/s)
ANGULAR_SPEED_MAX = 1.0  # 运控节点角速度上限 (rad/s)
CMD_SEND_RATE = 7        # 运动指令发送频率(Hz)

# ====================== 相机推流 ======================
CAMERA_IP = "192.168.9.253"   # 机端IP地址
CAMERA_PORT = 8888             # TCP 推流端口

# ====================== 串口配置 ======================
# Windows 下 CH340 通常是 COM3，改为: SERIAL_PORT = "COM3"
# Linux  下 CH340 通常是 /dev/ttyUSB0 或 /dev/ttyCH340USB0
SERIAL_PORT = "/dev/ttyUSB0"
SERIAL_BAUD = 115200

# ====================== 二进制协议常量（32 字节固定帧）======================
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

# ====================== 模块ID映射 ======================
# 与 C++ 端 bit 位一一对应
MODULE_IDS = ["lower_body", "upper_body", "imu_driver", "remote_interface", "usb_camera"]

# ====================== 指令类型枚举 ======================
class CmdType(Enum):
    MOVE = 1    # 0x01 运动控制: payload = linear_f32 + angular_f32
    TASK = 2    # 0x02 预设任务: payload = task_id_u8
    STOP = 3    # 0x03 紧急停止（与机端心跳复用同一数值，但由控制端主动发送）

# ====================== WebSocket 动作组协议 ======================
WS_ACTION_URI = "ws://192.168.9.253:9998/action"
WS_CONNECT_TIMEOUT = 3.0   # 连接超时 (秒)
WS_CMD_TIMEOUT = 3.0       # 指令等待 ACK 超时 (秒)

class ActionCmdType(Enum):
    """WebSocket 动作指令类型"""
    PLAY = "play"
    RESET = "reset"

# ====================== WebSocket 握手协议 ======================
WS_HANDSHAKE_URI = "ws://192.168.9.253:9999/handshake"

class HandshakeCmdType(Enum):
    """握手交互指令"""
    AUTO = "auto"
    FORCE_ON = "on"
    FORCE_OFF = "off"

# ====================== 预设任务 ======================
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
    10: "预留任务10",
}

# ====================== UI颜色 ======================
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREEN = (0, 255, 0)
RED = (255, 0, 0)

# ====================== 摇杆配置 ======================
# Linux 下手柄轴值范围可能跟 Windows 不同，需要做映射。
# Linux: A0/A2 中心 -0.5 (范围 -1~0), A1/A3 中心 +0.5 (范围 0~1)
# 设 True 启用 Linux 轴映射，False 用原始值（Windows 默认）
USE_LINUX_AXIS_MAP = False

JOYSTICK_FPS = 15  # 摇杆透传发送帧率
