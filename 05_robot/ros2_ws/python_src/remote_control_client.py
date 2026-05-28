import pygame
import sys
import time
import socket

# ====================== 基础配置 ======================
LINEAR_SPEED_MAX = 1.0
ANGULAR_SPEED_MAX = 1.5
SPEED_STEP = 0.1

# UDP 配置
UDP_TARGET_IP = "192.168.1.100"
UDP_TARGET_PORT = 8888

# 自定义指令类型（统一协议）
CMD_MOVE = "MOVE"    # 运动控制: MOVE,线速度,角速度
CMD_TASK = "TASK"    # 预设任务: TASK,任务编号
CMD_STOP = "STOP"    # 紧急停止: STOP

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

class RobotRemote:
    def __init__(self):
        pygame.init()
        self.win_w = 800
        self.win_h = 500
        self.screen = pygame.display.set_mode((self.win_w, self.win_h))
        pygame.display.set_caption("遥控前端 - WASD 键盘直控")

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

        # 虚拟按键位置尺寸
        self.k_size = 60
        self.k_gap = 10
        self.k_cx = 200
        self.k_cy = 420

        # UDP 初始化
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"✅ UDP 已初始化，目标: {UDP_TARGET_IP}:{UDP_TARGET_PORT}")

        # 防抖标记：防止单按键重复发包
        self.task_sent = False
        self.stop_sent = False

    def update_speed(self):
        """根据按键状态更新速度并发送运动指令"""
        self.linear_vel = 0.0
        self.angular_vel = 0.0

        if self.key_w:
            self.linear_vel += self.linear_cfg
        if self.key_s:
            self.linear_vel -= self.linear_cfg
        if self.key_a:
            self.angular_vel += self.angular_cfg
        if self.key_d:
            self.angular_vel -= self.angular_cfg

        # 持续发送运动指令（60帧/秒，正常控速）
        self.send_move_cmd()

    # -------------------------- 指令封装 --------------------------
    def send_udp_msg(self, msg):
        """通用UDP发送接口"""
        try:
            self.udp_socket.sendto(msg.encode(), (UDP_TARGET_IP, UDP_TARGET_PORT))
            # print(f"📤 发送: {msg.strip()}")
        except Exception as e:
            print(f"❌ UDP 发送失败: {e}")

    def send_move_cmd(self):
        """发送运动控制指令 MOVE,线性速度,角速度"""
        linear = round(self.linear_vel, 2)
        angular = round(self.angular_vel, 2)
        msg = f"{CMD_MOVE},{linear},{angular}\n"
        self.send_udp_msg(msg)

    def send_task_cmd(self, task_num):
        """发送预设任务指令 TASK,任务编号"""
        msg = f"{CMD_TASK},{task_num}\n"
        self.send_udp_msg(msg)

    def send_stop_cmd(self):
        """发送急停指令 STOP"""
        msg = f"{CMD_STOP}\n"
        self.send_udp_msg(msg)

    # -------------------------- UI 绘制（完全保留原有逻辑） --------------------------
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
        title = self.font_lg.render("机器人遥控前端", True, WHITE)
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
        desc = [
            "W / S ：前进 / 后退",
            "A / D ：左转 / 右转",
            "数字 1~10 ：执行预设任务",
            "空格 ：紧急停止",
            "ESC ：退出程序"
        ]
        y = 80
        for line in desc:
            t = self.font_sm.render(line, True, GRAY)
            self.screen.blit(t, (500, y))
            y += 28
        pygame.display.flip()

    def run(self):
        clock = pygame.time.Clock()
        running = True
        print("========== 遥控前端启动 ==========")
        print("W 前进  S 后退  A 左转  D 右转")
        print("数字键 1-0 执行预设任务 | 空格 急停 | ESC 退出")
        print("==================================")

        old_w, old_a, old_s, old_d = False, False, False, False

        while running:
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
                    # 数字键：预设任务（按下一次发一次，防抖）
                    if pygame.K_1 <= event.key <= pygame.K_9:
                        if not self.task_sent:
                            num = event.key - pygame.K_0
                            print(f"按下 数字 {num}，执行任务{num}")
                            self.last_tip = f"执行任务 {num}"
                            self.send_task_cmd(num)
                            self.task_sent = True
                    elif event.key == pygame.K_0:
                        if not self.task_sent:
                            print("按下 数字 0，执行任务10")
                            self.last_tip = "执行任务 10"
                            self.send_task_cmd(10)
                            self.task_sent = True

                    # 空格：急停指令
                    elif event.key == pygame.K_SPACE:
                        if not self.stop_sent:
                            print("按下 空格 — 紧急停止")
                            self.last_tip = "紧急停止"
                            self.send_stop_cmd()
                            self.stop_sent = True
                            # 清空速度
                            self.linear_vel = 0.0
                            self.angular_vel = 0.0

                    # ESC 退出
                    elif event.key == pygame.K_ESCAPE:
                        running = False

                # 按键抬起，解除防抖
                if event.type == pygame.KEYUP:
                    if pygame.K_1 <= event.key <= pygame.K_9 or event.key == pygame.K_0:
                        self.task_sent = False
                    if event.key == pygame.K_SPACE:
                        self.stop_sent = False

            self.update_speed()
            self.draw_ui()
            clock.tick(60)

        # 释放资源
        self.udp_socket.close()
        pygame.quit()
        print("程序已退出")

if __name__ == "__main__":
    app = RobotRemote()