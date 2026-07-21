"""
主应用类 RobotRemote

作为胶水层，将各独立模块（串口、摄像头、WebSocket、摇杆、UI）组合在一起。
负责：
  - 初始化各模块
  - 事件循环（键盘、鼠标、摇杆轮询）
  - 协调模块之间的交互
  - 程序退出清理
"""

import sys
import threading
import time
import socket

import pygame

from .config import (
    CMD_SEND_RATE,
    MODULE_IDS,
    CmdType, ActionCmdType, HandshakeCmdType,
    JOYSTICK_FPS,
)
from .protocol import build_frame
from .serial_io import SerialIO
from .joystick_handler import JoystickHandler
from .camera_streamer import CameraStreamer
from .ws_action_client import WsActionClient
from .ws_handshake_client import WsHandshakeClient
from .ui import UIRenderer


class RobotRemote:
    """远程控制系统主类"""

    def __init__(self):
        pygame.init()
        self.win_w = 1200
        self.win_h = 760
        self.screen = pygame.display.set_mode((self.win_w, self.win_h))
        pygame.display.set_caption("三代人形机器人远程控制系统")

        # ---- UI 渲染器 ----
        self.ui = UIRenderer(self.win_w, self.win_h, self.screen)

        # ---- 日志 ----
        self.logs: list[tuple[str, str, str]] = []

        # ---- 摇杆 ----
        self.joystick_handler = JoystickHandler()
        self.last_tip = "等待操作"
        self.last_joystick_send_time = 0

        # ---- 串口 ----
        self.serial_io = SerialIO(on_log=self.add_log)
        self.dtu_connected = self.serial_io.dtu_connected

        # ---- LAN 探测 ----
        self.lan_connected = False
        threading.Thread(target=self._lan_check_loop, daemon=True).start()

        # ---- 相机 ----
        self.camera = CameraStreamer(on_log=self.add_log)

        # ---- WebSocket 动作组 ----
        self.ws_action = WsActionClient(on_log=self.add_log)

        # ---- WebSocket 握手 ----
        self.ws_handshake = WsHandshakeClient(on_log=self.add_log)

        # ---- 任务状态 ----
        self.main_tasks = [
            {"id": 1, "name": "1. 语音动作组", "subs": [f"语音段落 {i}" for i in range(1, 31)]},
            {"id": 2, "name": "2. 握手交互",   "subs": ["自动感知模式", "强制握手开启", "强制握手关闭"]},
            {"id": 3, "name": "3. 语音交互",   "subs": ["语音问答交互模式"]},
            {"id": 4, "name": "4. 待机模式",   "subs": ["待机模式（关闭非运控节点）"]},
        ]
        self.current_main_task = 0
        self.sub_task_states: dict[str, bool] = {}
        for i, main in enumerate(self.main_tasks):
            for j, _ in enumerate(main["subs"]):
                self.sub_task_states[f"{i}-{j}"] = False
        self.voice_scroll_offset = 0

        # ---- 初始化日志 ----
        self.add_log("远程控制系统已启动")
        self.add_log("433MHz控制链路已连接")
        self.add_log("915MHz图传链路已连接")
        if self.joystick_handler.connected:
            self.add_log(f"摇杆已连接: {self.joystick_handler.name}", "success")
        else:
            self.add_log("⚠️ 未检测到手柄，请连接后重启", "warning")

        print(f"✅ 运动指令发送频率: {CMD_SEND_RATE}Hz")
        print(f"✅ 摇杆透传降频 {JOYSTICK_FPS}Hz | 数字键 1-4 切换主任务")
        print(f"✅ 摇杆: {self.joystick_handler.name if self.joystick_handler.connected else '未连接'}")
        print("================================================")

    # -------------------------- 日志 --------------------------
    def add_log(self, message, level="info"):
        ts = time.strftime("%H:%M:%S")
        self.logs.append((ts, message, level))
        if len(self.logs) > 100:
            self.logs.pop(0)

    # -------------------------- LAN 链路探测 --------------------------
    def _lan_check_loop(self):
        """每3秒尝试TCP连接SSH端口判断局域网通断"""
        while True:
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(2.0)
                s.connect(("192.168.9.253", 22))
                s.close()
                self.lan_connected = True
            except Exception:
                self.lan_connected = False
            time.sleep(3.0)

    # -------------------------- 任务指令发送 --------------------------
    def send_task_cmd(self, task_num):
        """发送预设任务指令"""
        payload = bytes([task_num & 0xFF])
        frame = build_frame(CmdType.TASK.value, payload)
        self.serial_io.send_frame(frame)

    def send_stop_cmd(self):
        """发送紧急停止指令"""
        frame = build_frame(CmdType.STOP.value)
        self.serial_io.send_frame(frame)

    # -------------------------- 主循环 --------------------------
    def run(self):
        clock = pygame.time.Clock()
        running = True
        joystick_interval = 1.0 / JOYSTICK_FPS

        while running:
            current_time = time.time()

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False

                if event.type == pygame.KEYDOWN:
                    if pygame.K_1 <= event.key <= pygame.K_4:
                        idx = event.key - pygame.K_1
                        if idx < len(self.main_tasks):
                            self.current_main_task = idx
                            self.add_log(
                                f"切换到主任务: {self.main_tasks[idx]['name']}", "info",
                            )
                            self.last_tip = self.main_tasks[idx]["name"]

                    elif event.key == pygame.K_c:
                        playing = self.camera.toggle()
                        if playing:
                            self.last_tip = "相机播放中..."
                        else:
                            self.last_tip = "相机已暂停"

                    elif event.key == pygame.K_ESCAPE:
                        running = False

                if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    mx, my = event.pos
                    self._handle_task_click(mx, my)

                if event.type == pygame.MOUSEWHEEL:
                    self._handle_mouse_wheel(event.y)

            # ---- 摇杆透传降频发送 ----
            self._send_joystick_data(current_time, joystick_interval)

            # ---- 刷新 DTU 连接状态 ----
            self.dtu_connected = self.serial_io.dtu_connected

            # ---- 绘制 UI ----
            self.ui.draw(
                main_tasks=self.main_tasks,
                current_main_task=self.current_main_task,
                sub_task_states=self.sub_task_states,
                voice_scroll_offset=self.voice_scroll_offset,
                action_playing=self.ws_action.playing,
                action_resetting=self.ws_action.resetting,
                dtu_connected=self.dtu_connected,
                lan_connected=self.lan_connected,
                camera_frame=self.camera.safe_frame,
                camera_playing=self.camera.is_playing,
                logs=self.logs,
                joystick_connected=self.joystick_handler.connected,
                joystick_name=self.joystick_handler.name,
                joystick_num_axes=self.joystick_handler.num_axes,
                joystick_num_buttons=self.joystick_handler.num_buttons,
                joystick_num_hats=self.joystick_handler.num_hats,
                joystick_axis_str=self.joystick_handler.get_axis_debug_str(),
                joystick_btn_count=self.joystick_handler.get_button_count(),
                last_tip=self.last_tip,
            )
            clock.tick(60)

        self._cleanup()

    # -------------------------- 摇杆发送 --------------------------
    def _send_joystick_data(self, current_time, interval):
        if not self.joystick_handler.connected:
            return
        ser = self.serial_io.ser
        if ser is None or not ser.is_open:
            return
        if current_time - self.last_joystick_send_time < interval:
            return

        buf = self.joystick_handler.read_raw()
        if buf:
            if int(current_time) % 5 == 0 and int(current_time) != getattr(
                self, '_last_debug_ts', 0,
            ):
                self._last_debug_ts = int(current_time)
                print(f"[JOY] {self.joystick_handler.get_axis_debug_str()}")
            self.serial_io.send_raw(buf)
        self.last_joystick_send_time = current_time

    # -------------------------- 鼠标点击处理 --------------------------
    def _handle_task_click(self, mx, my):
        panel_y = 72
        panel_h = self.win_h - panel_y - 100
        left_w = int(self.win_w * 0.23)
        left_x = 12

        if not (left_x <= mx <= left_x + left_w and panel_y <= my <= panel_y + panel_h):
            return

        content_y = panel_y + 42
        main_w = int(left_w * 0.4) - 6
        sub_w = left_w - main_w - 16
        sub_x = left_x + 10 + main_w + 6
        btn_w = (sub_w - 10) // 2
        btn_h = 32
        btn_gap = 10
        voice_group_idx = 0

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
        is_voice_group = (self.current_main_task == voice_group_idx)

        if is_voice_group:
            sub_y += 44

        for j, sub_name in enumerate(current_main["subs"]):
            if is_voice_group:
                vis_idx = j - self.voice_scroll_offset
                if vis_idx < 0:
                    continue
                max_visible = (panel_h - 50 - 44) // 32
                if vis_idx >= max_visible:
                    continue
                actual_row = sub_y + vis_idx * 32
            else:
                actual_row = sub_y + j * 32

            rect = pygame.Rect(left_x + 10 + main_w + 6, actual_row, sub_w, 28)
            if rect.collidepoint(mx, my):
                if is_voice_group:
                    paragraph_num = j + 1
                    success = self.ws_action.send_cmd(
                        ActionCmdType.PLAY, para=paragraph_num,
                    )
                    if success:
                        self.ws_action.playing = True
                        self.ws_action.resetting = False
                        self.last_tip = f"语音段落 {paragraph_num}"
                    else:
                        self.last_tip = f"语音段落 {paragraph_num} 发送失败"
                elif self.current_main_task == 1:  # 握手交互
                    handshake_cmds = [
                        HandshakeCmdType.AUTO,
                        HandshakeCmdType.FORCE_ON,
                        HandshakeCmdType.FORCE_OFF,
                    ]
                    if j < len(handshake_cmds):
                        success = self.ws_handshake.send_cmd(handshake_cmds[j])
                        if success:
                            self.last_tip = sub_name
                        else:
                            self.last_tip = f"{sub_name} 发送失败"
                else:
                    sid = f"{self.current_main_task}-{j}"
                    new_state = not self.sub_task_states.get(sid, False)
                    self.sub_task_states[sid] = new_state
                    if new_state:
                        self.add_log(f"已开启子任务: {sub_name}", "success")
                    else:
                        self.add_log(f"已关闭子任务: {sub_name}", "info")
                return
            sub_y += 32

        # 语音动作组按钮点击
        if is_voice_group:
            btn_y = content_y + 4
            play_x = sub_x
            reset_x = sub_x + btn_w + btn_gap
            if pygame.Rect(play_x, btn_y, btn_w, btn_h).collidepoint(mx, my):
                if not self.ws_action.playing:
                    self.ws_action.playing = True
                    self.ws_action.resetting = False
                    self.ws_action.send_cmd(ActionCmdType.PLAY)
                    self.add_log("▶️ 动作组播放中...", "success")
                    self.last_tip = "动作组播放中"
                else:
                    self.add_log("动作组已在播放中", "info")
                return
            if pygame.Rect(reset_x, btn_y, btn_w, btn_h).collidepoint(mx, my):
                self.ws_action.playing = False
                self.ws_action.resetting = True
                self.ws_action.send_cmd(ActionCmdType.RESET)
                self.add_log("⏹ 动作组已归位", "warning")
                self.last_tip = "动作归位完成"
                return

    # -------------------------- 滚轮事件 --------------------------
    def _handle_mouse_wheel(self, scroll_y):
        if self.current_main_task != 0:
            return
        panel_y = 72
        panel_h = self.win_h - panel_y - 100
        left_w = int(self.win_w * 0.23)
        left_x = 12
        sub_x = left_x + 10 + int(left_w * 0.4) + 6
        sub_w = left_w - int(left_w * 0.4) - 16
        mx, my = pygame.mouse.get_pos()
        if sub_x <= mx <= sub_x + sub_w and panel_y <= my <= panel_y + panel_h:
            max_visible = (panel_h - 50 - 44) // 32
            max_offset = max(0, len(self.main_tasks[0]["subs"]) - max_visible)
            self.voice_scroll_offset = max(
                0, min(max_offset, self.voice_scroll_offset - scroll_y),
            )

    # -------------------------- 清理 --------------------------
    def _cleanup(self):
        self.ws_action.shutdown()
        self.ws_handshake.shutdown()
        self.camera.stop()
        self.serial_io.close()
        pygame.quit()
        print("程序已安全退出")
