import pygame
import sys
import time
import struct
import serial
from enum import Enum

# ====================== 基础配置 ======================
LINEAR_SPEED_MAX = 1.0
ANGULAR_SPEED_MAX = 1.5
CMD_SEND_RATE = 20  # 运动指令发送频率(Hz)，建议10-20Hz

# 串口配置（与 C++ 模块管理器一致）
# Windows 下 CH340 通常是 COM3，改为: SERIAL_PORT = "COM3"
# Linux  下 CH340 通常是 /dev/ttyUSB0 或 /dev/ttyCH340USB0
SERIAL_PORT = "COM3"
SERIAL_BAUD = 115200

# 二进制协议常量（与 C++ 端保持一致）
SERIAL_SOF = 0xAA

# ====================== 指令类型枚举 ======================
class CmdType(Enum):
    MOVE = 1    # 运动控制: payload = linear_f32 + angular_f32
    TASK = 2    # 预设任务: payload = task_id_u8
    STOP = 3    # 紧急停止: payload 空

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

def calc_checksum(data: bytes) -> int:
    """XOR 校验和，与 C++ 端 calcChecksum 一致"""
    cs = 0
    for b in data:
        cs ^= b
    return cs

def build_frame(cmd_type: int, payload: bytes = b"") -> bytes:
    """构建二进制帧: SOF(1) + CmdType(1) + PayLen(1) + Payload(N) + Checksum(1)"""
    frame = bytes([SERIAL_SOF, cmd_type & 0xFF, len(payload) & 0xFF])
    frame += payload
    # 校验和从 CmdType 算到 Payload 末尾
    cs = calc_checksum(frame[1:])  # 从 CmdType 开始
    frame += bytes([cs])
    return frame


class RobotRemote:
    def __init__(self):
        pygame.init()
        self.win_w = 800
        self.win_h = 500
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
        self.linear_accel = 8.0     # 线加速度 (m/s²)，1.0m/s → 约 0.125s 到满速
        self.angular_accel = 12.0   # 角加速度 (rad/s²)，1.5rad/s → 约 0.125s 到满速
        self.last_update_time = 0.0  # 上次 update_speed 的时间

        # 【核心】运动状态控制
        self.is_moving = False
        self.last_cmd_time = 0

        # 任务指令重发（绕过 CH340 latency timer + 增加可靠性）
        self.task_retry_count = 0        # 当前剩余重发次数
        self.task_retry_max = 5          # 总共发 5 次
        self.task_retry_num = 0          # 当前重发的任务编号
        self.last_task_time = 0          # 上次任务发送时间

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
                timeout=0.01  # 非阻塞读
            )
            print(f"✅ 串口已打开: {SERIAL_PORT} @ {SERIAL_BAUD} baud")
        except Exception as e:
            print(f"❌ 串口打开失败: {e}")
            print(f"   请检查 CH340 设备是否已连接，路径是否为 {SERIAL_PORT}")
            self.ser = None

        print(f"✅ 运动指令发送频率: {CMD_SEND_RATE}Hz")

        # 防抖标记
        self.task_sent = False
        self.stop_sent = False

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
        state = self.font_md.render("UDP 遥控模式", True, YELLOW)
        self.screen.blit(state, (40, 80))
        speed_cfg = self.font_md.render(
            f"线速度上限: {self.linear_cfg:.1f} m/s  角速度上限: {self.angular_cfg:.1f} rad/s",
            True, WHITE
        )
        self.screen.blit(speed_cfg, (40, 110))
        self.draw_speed_bar(40, 160, 300, 20, self.linear_vel, LINEAR_SPEED_MAX, "线速度")
        self.draw_speed_bar(40, 220, 300, 20, self.angular_vel, ANGULAR_SPEED_MAX, "角速度")
        tip = self.font_md.render(f"当前操作: {self.last_tip}", True, WHITE)
        self.screen.blit(tip, (40, 270))
        self.draw_key("W", self.k_cx, self.k_cy - self.k_size - self.k_gap, self.key_w)
        self.draw_key("A", self.k_cx - self.k_size - self.k_gap, self.k_cy, self.key_a)
        self.draw_key("S", self.k_cx, self.k_cy, self.key_s)
        self.draw_key("D", self.k_cx + self.k_size + self.k_gap, self.k_cy, self.key_d)
        
        # 右侧任务列表
        task_title = self.font_md.render("预设任务列表", True, WHITE)
        self.screen.blit(task_title, (500, 80))
        y = 120
        for task_id, task_name in TASK_LIST.items():
            task_text = f"{task_id}: {task_name}"
            t = self.font_sm.render(task_text, True, GRAY)
            self.screen.blit(t, (500, y))
            y += 25

        # 操作说明
        help_title = self.font_md.render("操作说明", True, WHITE)
        self.screen.blit(help_title, (500, 400))
        help_texts = [
            "W / S ：前进 / 后退",
            "A / D ：左转 / 右转",
            "数字 1~10 ：执行预设任务",
            "空格 ：紧急停止",
            "ESC ：退出程序"
        ]
        y = 430
        for text in help_texts:
            t = self.font_sm.render(text, True, GRAY)
            self.screen.blit(t, (500, y))
            y += 22

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

        # 退出前发送一次停止指令
        self.send_move_cmd()
        if self.ser:
            self.ser.close()
        pygame.quit()
        print("程序已安全退出")

if __name__ == "__main__":
    app = RobotRemote()
    app.run()