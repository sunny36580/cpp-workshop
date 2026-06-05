import pygame
import sys
import time
import struct
import serial
import socket
import threading
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
        try:
            self.font_title = pygame.font.SysFont("Microsoft YaHei", 28, bold=True)
            self.font_md = pygame.font.SysFont("Microsoft YaHei", 18)
            self.font_sm = pygame.font.SysFont("Microsoft YaHei", 14)
            self.font_mono = pygame.font.SysFont("Microsoft YaHei", 12)
        except:
            self.font_title = pygame.font.SysFont("SimHei", 28, bold=True)
            self.font_md = pygame.font.SysFont("SimHei", 18)
            self.font_sm = pygame.font.SysFont("SimHei", 14)
            self.font_mono = pygame.font.SysFont("SimHei", 12)

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
        self.camera_last_frame_time = 0  # 上次收到新帧的时间戳
        self.camera_frame_count = 0      # 收到的总帧数

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

        # 日志
        self.logs = []
        self.add_log("远程控制系统已启动")
        self.add_log("433MHz控制链路已连接")
        self.add_log("915MHz图传链路已连接")
        self.add_log("机器人处于基础运控就绪状态")

        # 虚拟按键尺寸（WASD 控制区在底部中央）
        self.k_size = 56
        self.k_gap = 8

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
                            pass

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
        # 急停后重置运动状态
        self.is_moving = False
        self.linear_vel = 0.0
        self.angular_vel = 0.0

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

        # 主任务点击
        main_y = content_y
        for i, main in enumerate(self.main_tasks):
            rect = pygame.Rect(left_x + 10, main_y, main_w, 28)
            if rect.collidepoint(mx, my):
                self.current_main_task = i
                self.add_log(f"切换到主任务: {main['name']}", "info")
                return
            main_y += 32

        # 子任务点击
        sub_y = content_y
        current_main = self.main_tasks[self.current_main_task]
        for j, sub_name in enumerate(current_main["subs"]):
            rect = pygame.Rect(left_x + 10 + main_w + 6, sub_y, sub_w, 28)
            if rect.collidepoint(mx, my):
                sid = f"{self.current_main_task}-{j}"
                new_state = not self.sub_task_states.get(sid, False)
                self.sub_task_states[sid] = new_state
                if new_state:
                    self.add_log(f"已开启子任务: {sub_name}", "success")
                    # # 子任务开启时发送对应预设任务指令
                    # task_id = self.current_main_task + 1  # 1-4 对应 TASK_LIST
                    # if not self.task_sent:
                    #     self.last_tip = f"执行任务: {TASK_LIST[task_id]}"
                    #     self.task_retry_count = self.task_retry_max
                    #     self.task_retry_num = task_id
                    #     self.task_sent = True
                else:
                    self.add_log(f"已关闭子任务: {sub_name}", "info")
                return
            sub_y += 32

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

        # ==================== 底部WASD控制区 ====================
        self._draw_wasd_panel()

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

        # 主任务列表（左半）
        main_w = int(pw * 0.4) - 6
        sub_w = pw - main_w - 16

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

        # 子任务列表（右半）
        sub_y = content_y
        current_main = self.main_tasks[self.current_main_task]
        for j, sub_name in enumerate(current_main["subs"]):
            sid = f"{self.current_main_task}-{j}"
            on = self.sub_task_states.get(sid, False)
            bg = (55, 65, 81)
            rect = pygame.Rect(px + 10 + main_w + 6, sub_y, sub_w, 28)
            pygame.draw.rect(self.screen, bg, rect, border_radius=4)
            txt = self.font_sm.render(sub_name, True, (255, 255, 255))
            self.screen.blit(txt, (rect.x + 6, rect.y + 5))
            # 状态灯
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
        self.screen.blit(self.font_sm.render("分辨率: 480P@30fps", True, (156, 163, 175)), (px + 12, info_y))
        cw = int(self.font_sm.size("分辨率: 480P@30fps")[0])
        self.screen.blit(self.font_sm.render("编码: H.264", True, (156, 163, 175)), (px + 12 + cw + 30, info_y))
        self.screen.blit(self.font_sm.render("延迟: <150ms", True, (156, 163, 175)), (px + pw - 90, info_y))

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
            "• WASD键: 控制机器人移动",
            "• 数字键1-4: 切换主任务分类",
            "• 空格键: 紧急停止所有运动",
            "• C键: 相机播放/暂停",
        ]
        for i, h in enumerate(helps):
            self.screen.blit(self.font_sm.render(h, True, (209, 213, 219)), (px + 12, help_y + 24 + i * 20))

    # ---------- 底部：WASD 控制 ----------
    def _draw_wasd_panel(self):
        panel_y = self.win_h - 90
        panel_h = 82
        pygame.draw.rect(self.screen, (31, 41, 55), (12, panel_y, self.win_w - 24, panel_h), border_radius=8)
        title = self.font_md.render("运动控制", True, (96, 165, 250))
        self.screen.blit(title, (24, panel_y + 6))
        pygame.draw.line(self.screen, (75, 85, 99), (24, panel_y + 30), (self.win_w - 24, panel_y + 30), 1)

        # WASD 按键在底部居中
        cx = self.win_w // 2
        cy = panel_y + panel_h // 2
        s = self.k_size
        g = self.k_gap

        # 绘制4个按键
        keys_state = {"W": self.key_w, "A": self.key_a, "S": self.key_s, "D": self.key_d}
        positions = {"W": (cx, cy - s - g), "A": (cx - s - g, cy),
                     "S": (cx, cy), "D": (cx + s + g, cy)}

        for k, (kx, ky) in positions.items():
            pressed = keys_state[k]
            color = (59, 130, 246) if pressed else (55, 65, 81)
            border = (255, 255, 255) if pressed else (107, 114, 128)
            rect = (kx - s//2, ky - s//2, s, s)
            pygame.draw.rect(self.screen, color, rect, border_radius=6)
            pygame.draw.rect(self.screen, border, rect, 2, border_radius=6)
            txt = self.font_md.render(k, True, (255, 255, 255))
            self.screen.blit(txt, (kx - txt.get_width()//2, ky - txt.get_height()//2))

        # 底部提示
        hint = self.font_sm.render("W=前进 | S=后退 | A=左转 | D=右转 | 空格=紧急停止", True, (156, 163, 175))
        self.screen.blit(hint, (self.win_w//2 - hint.get_width()//2, panel_y + panel_h - 18))

    def run(self):
        clock = pygame.time.Clock()
        running = True
        print("========== 三代人形机器人远程控制系统 ==========")
        print("✅ WASD 运动控制 | 数字键 1-4 主任务 | 空格 急停")
        print("================================================\n")

        old_w, old_a, old_s, old_d = False, False, False, False
        was_moving = False  # 记录上一帧是否在运动

        # 任务点击防抖
        main_task_clicked = False
        sub_task_clicked = False

        while running:
            current_time = time.time()

            # 轮询WASD按键
            keys = pygame.key.get_pressed()
            self.key_w = keys[pygame.K_w]
            self.key_a = keys[pygame.K_a]
            self.key_s = keys[pygame.K_s]
            self.key_d = keys[pygame.K_d]

            # WASD 按键日志
            if self.key_w and not old_w:
                self.last_tip = "W - 前进"
                self.add_log("运动指令: W 前进", "info")
            if not self.key_w and old_w:
                self.add_log("停止指令: W 松开", "info")
            if self.key_a and not old_a:
                self.last_tip = "A - 左转"
                self.add_log("运动指令: A 左转", "info")
            if not self.key_a and old_a:
                self.add_log("停止指令: A 松开", "info")
            if self.key_s and not old_s:
                self.last_tip = "S - 后退"
                self.add_log("运动指令: S 后退", "info")
            if not self.key_s and old_s:
                self.add_log("停止指令: S 松开", "info")
            if self.key_d and not old_d:
                self.last_tip = "D - 右转"
                self.add_log("运动指令: D 右转", "info")
            if not self.key_d and old_d:
                self.add_log("停止指令: D 松开", "info")

            old_w, old_a, old_s, old_d = self.key_w, self.key_a, self.key_s, self.key_d

            # 事件循环
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False

                if event.type == pygame.KEYDOWN:
                    # 数字键1-4：切换主任务 + 同时发送预设任务指令
                    if pygame.K_1 <= event.key <= pygame.K_4:
                        idx = event.key - pygame.K_1
                        if idx < len(self.main_tasks):
                            self.current_main_task = idx
                            self.add_log(f"切换到主任务: {self.main_tasks[idx]['name']}", "info")
                        # 数字键1-4同时作为预设任务发送
                        num = event.key - pygame.K_0
                        if not self.task_sent:
                            self.last_tip = f"执行任务: {TASK_LIST[num]}"
                            self.add_log(f"执行预设任务 {num}: {TASK_LIST[num]}", "info")
                            self.task_retry_count = self.task_retry_max
                            self.task_retry_num = num
                            self.task_sent = True

                    # 数字键5-9：预设任务
                    elif pygame.K_5 <= event.key <= pygame.K_9:
                        num = event.key - pygame.K_0
                        if not self.task_sent:
                            self.last_tip = f"执行任务: {TASK_LIST[num]}"
                            self.add_log(f"执行预设任务 {num}: {TASK_LIST[num]}", "info")
                            self.task_retry_count = self.task_retry_max
                            self.task_retry_num = num
                            self.task_sent = True
                    elif event.key == pygame.K_0:
                        if not self.task_sent:
                            self.last_tip = f"执行任务: {TASK_LIST[10]}"
                            self.add_log(f"执行预设任务 10: {TASK_LIST[10]}", "info")
                            self.task_retry_count = self.task_retry_max
                            self.task_retry_num = 10
                            self.task_sent = True

                    # 空格：急停指令
                    elif event.key == pygame.K_SPACE:
                        if not self.stop_sent:
                            self.last_tip = "紧急停止"
                            self.send_stop_cmd()
                            self.add_log("紧急停止已触发", "warning")
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
                        self.task_retry_count = 0
                    if event.key == pygame.K_SPACE:
                        self.stop_sent = False

                # ---- 鼠标点击：左侧任务面板 ----
                if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    mx, my = event.pos
                    self._handle_task_click(mx, my)

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