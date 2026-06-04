import pygame
import sys
import time
import struct
import serial
import socket
import threading
import cv2
import numpy as np
import io
from enum import Enum

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
SERIAL_PORT = "COM3"
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

# ====================== 10个预设任务定义 ======================
TASK_LIST = {
    1: "握手程序",
    2: "语音交互模式",
    3: "黄梅戏剧本",
    4: "原地旋转180度",
    5: "手指动作能力展示",
    6: "挥手动作",
    7: "表情头能力展示",
    8: "回到待机模式",
    9: "预留任务9",
    10: "预留任务10"
}

# 颜色
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREEN = (0, 255, 0)
RED = (255, 0, 0)
GRAY = (128, 128, 128)
YELLOW = (255, 255, 0)
BLUE = (0, 150, 255)
DARK_GRAY = (50, 50, 50)
# ======================================================

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
        self.win_h = 720
        self.screen = pygame.display.set_mode((self.win_w, self.win_h))
        pygame.display.set_caption("人形机器人遥控系统")

        # 字体
        try:
            self.font_lg = pygame.font.SysFont("Microsoft YaHei", 32, bold=True)
            self.font_md = pygame.font.SysFont("Microsoft YaHei", 20)
            self.font_sm = pygame.font.SysFont("Microsoft YaHei", 16)
        except:
            self.font_lg = pygame.font.SysFont("SimHei", 32, bold=True)
            self.font_md = pygame.font.SysFont("SimHei", 20)
            self.font_sm = pygame.font.SysFont("SimHei", 16)

        # 按键状态
        self.key_w = False
        self.key_a = False
        self.key_s = False
        self.key_d = False

        # 速度变量
        self.linear_vel = 0.0
        self.angular_vel = 0.0
        self.linear_cfg = LINEAR_SPEED_MAX
        self.angular_cfg = ANGULAR_SPEED_MAX
        self.last_tip = "等待操作"

        # 加减速参数（预加速 / 预减速）
        self.linear_accel = 0.2     # 线加速度 (m/s²)，与运控节点一致
        self.angular_accel = 0.3    # 角加速度 (rad/s²)，与运控节点一致
        self.last_update_time = 0.0  # 上次 update_speed 的时间

        # 【核心】运动状态控制
        self.is_moving = False
        self.last_cmd_time = 0

        # 任务指令重发（绕过 CH340 latency timer + 增加可靠性）
        self.task_retry_count = 0        # 当前剩余重发次数
        self.task_retry_max = 5          # 总共发 5 次
        self.task_retry_num = 0          # 当前重发的任务编号
        self.last_task_time = 0          # 上次任务发送时间

        # 相机画面（C键播放/停止，画面常驻）
        self.camera_playing = False     # 是否正在播放
        self.camera_active = False      # 线程是否运行中
        self.camera_frame = None
        self.camera_thread = None
        self.camera_running = False
        self.camera_frame_lock = threading.Lock()  # 保护 camera_frame

        # 虚拟按键位置尺寸
        self.k_size = 60
        self.k_gap = 10
        self.k_cx = 200
        self.k_cy = 420

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

        # 防抖标记
        self.task_sent = False
        self.stop_sent = False

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
                        for name in self.module_statuses:
                            self.module_statuses[name] = False
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
                            print(f"[STATUS] 第{rx_count}次状态, 模块在线: {sum(1 for v in self.module_statuses.values() if v)}")

                    rx_buf = rx_buf[SERIAL_FRAME_LEN:]

            except serial.SerialException:
                self.dtu_connected = False; time.sleep(0.5)
            except Exception:
                pass
        self.dtu_connected = False

    def update_speed(self):
        """带加减速的平滑速度更新"""
        now = time.time()
        dt = now - self.last_update_time if self.last_update_time > 0 else 0.02
        self.last_update_time = now
        # 限幅 dt，防止卡顿时速度突变
        dt = min(dt, 0.1)

        # 1. 计算目标速度（基于按键）
        target_linear = 0.0
        target_angular = 0.0
        if self.key_w:
            target_linear += self.linear_cfg
        if self.key_s:
            target_linear -= self.linear_cfg
        if self.key_a:
            target_angular += self.angular_cfg
        if self.key_d:
            target_angular -= self.angular_cfg

        # 2. 线性加速/减速逼近目标
        diff_linear = target_linear - self.linear_vel
        diff_angular = target_angular - self.angular_vel

        if abs(diff_linear) > 0.001:
            step = self.linear_accel * dt
            self.linear_vel += step if diff_linear > 0 else -step
            # 防止过冲
            if abs(diff_linear) < step:
                self.linear_vel = target_linear
        else:
            self.linear_vel = target_linear

        if abs(diff_angular) > 0.001:
            step = self.angular_accel * dt
            self.angular_vel += step if diff_angular > 0 else -step
            if abs(diff_angular) < step:
                self.angular_vel = target_angular
        else:
            self.angular_vel = target_angular

        self.is_moving = (abs(self.linear_vel) > 0.001 or abs(self.angular_vel) > 0.001)

    # -------------------------- 指令封装（二进制协议）--------------------------
    def send_frame(self, frame: bytes):
        """通过串口发送二进制帧"""
        if self.ser is None:
            return
        try:
            self.ser.write(frame)
        except Exception as e:
            print(f"❌ 串口发送失败: {e}")

    def send_move_cmd(self):
        """发送运动控制指令：
           帧: 0xAA | 0x01 | 0x08 | linear_f32(LE,4B) | angular_f32(LE,4B) | checksum
        """
        linear = round(self.linear_vel, 2)
        angular = round(self.angular_vel, 2)
        payload = struct.pack("<ff", linear, angular)  # 小端 float32 x 2
        frame = build_frame(CmdType.MOVE.value, payload)
        self.send_frame(frame)

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
        """相机接收线程：TCP → H.264 → OpenCV 解码"""
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5.0)
            sock.connect((CAMERA_IP, CAMERA_PORT))
            self.last_tip = "相机已连接"
            print("✅ TCP 相机已连接")

            # 创建 H.264 解码器
            h264_decoder = cv2.VideoWriter_fourcc(*'H264')
            # OpenCV 的 VideoCapture 不能直接解 raw H.264 ES
            # 用 Python 的 av 库来解码
            try:
                import av
                codec = av.CodecContext.create('h264', 'r')
            except ImportError:
                print("⚠ 需要安装 av 库: pip install av")
                raise

            while self.camera_running:
                # 读4字节数据长度
                len_data = sock.recv(4)
                if not len_data:
                    break
                length = int.from_bytes(len_data, 'big')
                if length > 500000:
                    continue

                # 读完整 H.264 数据
                data = b''
                while len(data) < length:
                    packet = sock.recv(length - len(data))
                    if not packet:
                        break
                    data += packet

                if len(data) != length:
                    break

                # 用 PyAV 解码 H.264 ES
                try:
                    frames = codec.decode(av.Packet(data))
                    for frame in frames:
                        img = frame.to_ndarray(format='bgr24')
                        # 直接转 pygame surface，640x480 无需额外缩放
                        raw = pygame.image.frombuffer(img.tobytes(), (img.shape[1], img.shape[0]), "BGR")
                        with self.camera_frame_lock:
                            self.camera_frame = raw
                except Exception:
                    pass  # EAGAIN: need more data

        except socket.timeout:
            self.last_tip = "相机连接超时"
            print("❌ 相机连接超时")
        except ConnectionRefusedError:
            self.last_tip = "相机服务未启动"
            print("❌ 相机服务未启动")
        except Exception as e:
            self.last_tip = f"相机错误: {str(e)[:30]}"
            print(f"❌ 相机错误: {e}")
        finally:
            if sock:
                try:
                    sock.close()
                except:
                    pass
            self.camera_running = False
            self.camera_active = False
            self.camera_playing = False
            print("📷 相机已断开")

    def send_stop_cmd(self):
        """发送紧急停止指令：
           帧: 0xAA | 0x03 | 0x00 | checksum
        """
        frame = build_frame(CmdType.STOP.value)
        self.send_frame(frame)
        # 急停后重置运动状态
        self.is_moving = False
        self.linear_vel = 0.0
        self.angular_vel = 0.0

    # -------------------------- UI 绘制 --------------------------
    def draw_key(self, text, x, y, pressed):
        color = BLUE if pressed else DARK_GRAY
        border = WHITE if pressed else GRAY
        rect = (x - self.k_size//2, y - self.k_size//2, self.k_size, self.k_size)
        pygame.draw.rect(self.screen, color, rect, border_radius=8)
        pygame.draw.rect(self.screen, border, rect, 2, border_radius=8)
        txt = self.font_md.render(text, True, WHITE)
        self.screen.blit(txt, (x - txt.get_width()//2, y - txt.get_height()//2))

    def draw_speed_bar(self, x, y, w, h, val, max_val, label):
        pygame.draw.rect(self.screen, DARK_GRAY, (x, y, w, h), border_radius=4)
        pygame.draw.rect(self.screen, GRAY, (x, y, w, h), 1, border_radius=4)
        mid = x + w // 2
        if val >= 0:
            bw = int((val / max_val) * (w/2))
            pygame.draw.rect(self.screen, GREEN, (mid, y, bw, h), border_radius=4)
        else:
            bw = int((abs(val) / max_val) * (w/2))
            pygame.draw.rect(self.screen, RED, (mid - bw, y, bw, h), border_radius=4)
        pygame.draw.line(self.screen, WHITE, (mid, y), (mid, y+h), 2)
        lab = self.font_sm.render(label, True, WHITE)
        val_txt = self.font_sm.render(f"{val:.2f}", True, WHITE)
        self.screen.blit(lab, (x, y-20))
        self.screen.blit(val_txt, (x + w - val_txt.get_width(), y-20))

    def draw_ui(self):
        self.screen.fill(BLACK)
        title = self.font_lg.render("人形机器人遥控系统", True, WHITE)
        self.screen.blit(title, (self.win_w//2 - title.get_width()//2, 20))
        # 通信状态: DTU 无线串口 + LAN 局域网
        dtu = "DTU OK" if self.dtu_connected else "DTU LOST"
        lan = "LAN OK" if self.lan_connected else "LAN LOST"
        self.screen.blit(self.font_md.render(dtu, True, GREEN if self.dtu_connected else RED), (40, 80))
        self.screen.blit(self.font_md.render(lan, True, GREEN if self.lan_connected else RED), (200, 80))
        # 模块状态
        mod_y = 110
        online_n = sum(1 for v in self.module_statuses.values() if v)
        total_n = len(self.module_statuses)
        self.screen.blit(self.font_sm.render(f"模块: {online_n}/{total_n}", True, WHITE), (40, mod_y))
        mod_y += 18
        for name, on in self.module_statuses.items():
            color = GREEN if on else RED
            txt = f"  {'[ON]' if on else '[OFF]'} {name}"
            self.screen.blit(self.font_sm.render(txt, True, color), (40, mod_y))
            mod_y += 18
        if mod_y < 200: mod_y = 200
        speed_cfg = self.font_md.render(
            f"线速度上限: {self.linear_cfg:.1f} m/s  角速度上限: {self.angular_cfg:.1f} rad/s", True, WHITE)
        self.screen.blit(speed_cfg, (40, mod_y))
        self.draw_speed_bar(40, mod_y + 30, 300, 20, self.linear_vel, LINEAR_SPEED_MAX, "线速度")
        self.draw_speed_bar(40, mod_y + 80, 300, 20, self.angular_vel, ANGULAR_SPEED_MAX, "角速度")
        self.screen.blit(self.font_md.render(f"当前操作: {self.last_tip}", True, WHITE), (40, mod_y + 130))
        self.draw_key("W", self.k_cx, self.k_cy - self.k_size - self.k_gap, self.key_w)
        self.draw_key("A", self.k_cx - self.k_size - self.k_gap, self.k_cy, self.key_a)
        self.draw_key("S", self.k_cx, self.k_cy, self.key_s)
        self.draw_key("D", self.k_cx + self.k_size + self.k_gap, self.k_cy, self.key_d)

        # ========== 右侧区域（视频画面 640x480）==========
        cam_w, cam_h = 640, 480
        cam_x, cam_y = 520, 60
        # 画背景区域
        pygame.draw.rect(self.screen, DARK_GRAY, (cam_x-5, cam_y-5, cam_w+10, cam_h+45), border_radius=4)
        if self.camera_frame is not None:
            self.screen.blit(self.camera_frame, (cam_x, cam_y))
        if not self.camera_playing:
            # 暂停覆盖层
            overlay = pygame.Surface((cam_w, cam_h), pygame.SRCALPHA)
            overlay.fill((0, 0, 0, 160))
            self.screen.blit(overlay, (cam_x, cam_y))
            pause_text = self.font_md.render("⏸ 已暂停 [C键播放]", True, WHITE)
            self.screen.blit(pause_text, (cam_x + cam_w//2 - pause_text.get_width()//2,
                                          cam_y + cam_h//2 - pause_text.get_height()//2))
        # 状态指示
        if self.camera_playing:
            cam_label = self.font_sm.render("▶ 播放中 [C键暂停]", True, GREEN)
        else:
            cam_label = self.font_sm.render("⏸ 已暂停 [C键播放]", True, GRAY)
        self.screen.blit(cam_label, (cam_x, cam_y + cam_h + 5))

        # 操作说明
        help_title = self.font_md.render("操作说明", True, WHITE)
        self.screen.blit(help_title, (cam_x, cam_y + cam_h + 25))
        help_texts = [
            "W / S ：前进 / 后退",
            "A / D ：左转 / 右转",
            "数字 1~10 ：执行预设任务",
            "空格 ：紧急停止",
            "C   ：相机播放 / 暂停",
            "ESC ：退出程序"
        ]
        y = cam_y + cam_h + 50
        for text in help_texts:
            t = self.font_sm.render(text, True, GRAY)
            self.screen.blit(t, (cam_x, y))
            y += 20

        pygame.display.flip()

    def run(self):
        clock = pygame.time.Clock()
        running = True
        print("========== 人形机器人遥控系统启动 ==========")
        print("✅ 按住WASD持续发送运动指令，松开自动停止")
        print("W 前进  S 后退  A 左转  D 右转")
        print("数字键 1-0 执行预设任务 | 空格 急停 | ESC 退出")
        print("==========================================\n")

        old_w, old_a, old_s, old_d = False, False, False, False
        was_moving = False  # 记录上一帧是否在运动

        while running:
            current_time = time.time()

            # 轮询WASD按键
            keys = pygame.key.get_pressed()
            self.key_w = keys[pygame.K_w]
            self.key_a = keys[pygame.K_a]
            self.key_s = keys[pygame.K_s]
            self.key_d = keys[pygame.K_d]

            # WASD 按键打印提示
            if self.key_w and not old_w:
                print("按下 W")
                self.last_tip = "W - 前进"
            if not self.key_w and old_w:
                print("松开 W")
            if self.key_a and not old_a:
                print("按下 A")
                self.last_tip = "A - 左转"
            if not self.key_a and old_a:
                print("松开 A")
            if self.key_s and not old_s:
                print("按下 S")
                self.last_tip = "S - 后退"
            if not self.key_s and old_s:
                print("松开 S")
            if self.key_d and not old_d:
                print("按下 D")
                self.last_tip = "D - 右转"
            if not self.key_d and old_d:
                print("松开 D")

            old_w, old_a, old_s, old_d = self.key_w, self.key_a, self.key_s, self.key_d

            # 事件循环：数字键/空格/ESC 单次触发指令
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False

                if event.type == pygame.KEYDOWN:
                    # 数字键：预设任务
                    if pygame.K_1 <= event.key <= pygame.K_9:
                        if not self.task_sent:
                            num = event.key - pygame.K_0
                            print(f"按下 数字 {num}，执行任务: {TASK_LIST[num]}")
                            self.last_tip = f"执行任务: {TASK_LIST[num]}"
                            # 设置重发（立即发第一次 + 后续重复）
                            self.task_retry_count = self.task_retry_max
                            self.task_retry_num = num
                            self.task_sent = True
                    elif event.key == pygame.K_0:
                        if not self.task_sent:
                            print(f"按下 数字 0，执行任务: {TASK_LIST[10]}")
                            self.last_tip = f"执行任务: {TASK_LIST[10]}"
                            self.task_retry_count = self.task_retry_max
                            self.task_retry_num = 10
                            self.task_sent = True

                    # 空格：急停指令
                    elif event.key == pygame.K_SPACE:
                        if not self.stop_sent:
                            print("按下 空格 — 紧急停止")
                            self.last_tip = "紧急停止"
                            self.send_stop_cmd()
                            self.stop_sent = True

                    # C 键播放/暂停相机
                    elif event.key == pygame.K_c:
                        self.toggle_camera_play()

                    # ESC 退出
                    elif event.key == pygame.K_ESCAPE:
                        running = False

                # 按键抬起，解除防抖 + 立即停止重发
                if event.type == pygame.KEYUP:
                    if pygame.K_1 <= event.key <= pygame.K_9 or event.key == pygame.K_0:
                        self.task_sent = False
                        self.task_retry_count = 0  # 松开立即停止重发
                    if event.key == pygame.K_SPACE:
                        self.stop_sent = False

            # 更新速度和运动状态
            self.update_speed()

            # ===================== 【核心发送逻辑】 =====================
            # 1. 正在运动：按固定频率持续发送
            if self.is_moving:
                if current_time - self.last_cmd_time >= 1/CMD_SEND_RATE:
                    self.send_move_cmd()
                    self.last_cmd_time = current_time
                was_moving = True

            # 2. 刚停止运动：只发送一次停止指令
            elif was_moving:
                self.send_move_cmd()  # 发送最后一次0速度指令
                print("所有按键松开，发送停止指令")
                was_moving = False
            # ==========================================================

            # ===================== 【任务指令重发逻辑】 =====================
            # 按同样 20Hz 频率重发，绕过 CH340 latency timer
            if self.task_retry_count > 0:
                if current_time - self.last_task_time >= 1/CMD_SEND_RATE:
                    self.send_task_cmd(self.task_retry_num)
                    self.last_task_time = current_time
                    self.task_retry_count -= 1
                    if self.task_retry_count == 0:
                        self.task_retry_num = 0
            # =============================================================

            self.draw_ui()
            clock.tick(60)

        # 退出前关闭相机和串口
        self.camera_running = False
        self.camera_playing = False
        self.camera_active = False
        self.camera_frame = None
        self.send_move_cmd()
        if self.ser:
            self.ser.close()
        pygame.quit()
        print("程序已安全退出")

if __name__ == "__main__":
    app = RobotRemote()
    app.run()