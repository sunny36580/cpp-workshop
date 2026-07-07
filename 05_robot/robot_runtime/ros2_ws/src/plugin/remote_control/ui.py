"""
Pygame UI 面板绘制

包含任务面板、视频面板、日志面板、摇杆状态面板的绘制逻辑。
纯展示层，不包含业务逻辑。
"""

import pygame

from .font_utils import load_ui_font

# ====================== 颜色 ======================
BG_DARK = (26, 32, 44)
PANEL_BG = (31, 41, 55)
PANEL_BORDER = (75, 85, 99)
TEXT_WHITE = (255, 255, 255)
TEXT_GRAY = (156, 163, 175)
TEXT_DIM = (107, 114, 128)
ACCENT_BLUE = (96, 165, 250)
ACCENT_GREEN = (34, 197, 94)
ACCENT_RED = (239, 68, 68)
ACCENT_YELLOW = (250, 204, 21)
TASK_BG = (55, 65, 81)
TASK_ACTIVE_BG = (30, 64, 175)
BTN_PLAY = (22, 163, 74)
BTN_PLAY_ACTIVE = (21, 128, 61)
BTN_RESET = (220, 38, 38)
BTN_RESET_ACTIVE = (185, 28, 28)
VIDEO_BG = (0, 0, 0)
LOG_BG = (17, 24, 39)


class UIRenderer:
    """UI 渲染器

    负责所有 Pygame 界面的绘制，不包含业务逻辑。
    所有需要展示的数据通过属性或参数注入。
    """

    def __init__(self, win_w: int, win_h: int, screen):
        self.win_w = win_w
        self.win_h = win_h
        self.screen = screen

        # 字体
        self.font_title = load_ui_font(28, bold=True)
        self.font_md = load_ui_font(18)
        self.font_sm = load_ui_font(14)
        self.font_mono = load_ui_font(12, mono=True)

    # ==================== 主绘制入口 ====================
    def draw(
        self,
        # 任务面板
        main_tasks,
        current_main_task,
        sub_task_states,
        voice_scroll_offset,
        action_playing,
        action_resetting,
        dtu_connected,
        lan_connected,
        # 视频面板
        camera_frame,
        camera_playing,
        # 日志面板
        logs,
        # 摇杆面板
        joystick_connected,
        joystick_name,
        joystick_num_axes,
        joystick_num_buttons,
        joystick_num_hats,
        joystick_axis_str,
        joystick_btn_count,
        # 系统提示
        last_tip,
    ):
        self.screen.fill(BG_DARK)

        # 标题栏
        self._draw_title()

        panel_y = 72
        panel_h = self.win_h - panel_y - 100

        # 左：任务面板
        left_w = int(self.win_w * 0.23)
        left_x = 12
        self._draw_panel(left_x, panel_y, left_w, panel_h, "任务指令控制")
        self._draw_task_panel(
            left_x, panel_y, left_w, panel_h,
            main_tasks, current_main_task, sub_task_states,
            voice_scroll_offset, action_playing, action_resetting,
            dtu_connected, lan_connected,
        )

        # 中：视频面板
        mid_x = left_x + left_w + 10
        mid_w = int(self.win_w * 0.5)
        self._draw_panel(mid_x, panel_y, mid_w, panel_h, "机器人胸口摄像头")
        self._draw_video_panel(
            mid_x, panel_y, mid_w, panel_h,
            camera_frame, camera_playing,
        )

        # 右：日志面板
        right_x = mid_x + mid_w + 10
        right_w = self.win_w - right_x - 12
        self._draw_panel(right_x, panel_y, right_w, panel_h, "操作日志")
        self._draw_log_panel(right_x, panel_y, right_w, panel_h, logs)

        # 底部：摇杆状态
        self._draw_joystick_panel(
            joystick_connected, joystick_name,
            joystick_num_axes, joystick_num_buttons, joystick_num_hats,
            joystick_axis_str, joystick_btn_count,
        )

        pygame.display.flip()

    # ==================== 标题栏 ====================
    def _draw_title(self):
        title = self.font_title.render(
            "三代人形机器人远程控制系统", True, ACCENT_BLUE,
        )
        self.screen.blit(title, (
            self.win_w // 2 - title.get_width() // 2, 10,
        ))
        sub = self.font_sm.render(
            "433MHz控制链路 | 915MHz图传链路", True, TEXT_GRAY,
        )
        self.screen.blit(sub, (
            self.win_w // 2 - sub.get_width() // 2, 44,
        ))

    # ==================== 面板背景 ====================
    def _draw_panel(self, x, y, w, h, title):
        pygame.draw.rect(self.screen, PANEL_BG, (x, y, w, h), border_radius=8)
        title_surf = self.font_md.render(title, True, ACCENT_BLUE)
        self.screen.blit(title_surf, (x + 12, y + 8))
        pygame.draw.line(
            self.screen, PANEL_BORDER,
            (x + 12, y + 34), (x + w - 12, y + 34), 1,
        )

    # ==================== 左侧任务面板 ====================
    def _draw_task_panel(
        self, px, py, pw, ph,
        main_tasks, current_main_task, sub_task_states,
        voice_scroll_offset, action_playing, action_resetting,
        dtu_connected, lan_connected,
    ):
        content_y = py + 42
        main_w = int(pw * 0.4) - 6
        sub_w = pw - main_w - 16
        sub_x = px + 10 + main_w + 6
        is_voice_group = (current_main_task == 0)
        voice_action_h = 44
        btn_w = (sub_w - 10) // 2
        btn_h = 32
        btn_gap = 10

        # ---- 主任务列表 ----
        main_y = content_y
        for i, main in enumerate(main_tasks):
            active = (i == current_main_task)
            bg = TASK_ACTIVE_BG if active else TASK_BG
            rect = pygame.Rect(px + 10, main_y, main_w, 28)
            pygame.draw.rect(self.screen, bg, rect, border_radius=4)
            if active:
                pygame.draw.rect(self.screen, ACCENT_BLUE, rect, 2, border_radius=4)
            txt = self.font_sm.render(main["name"], True, TEXT_WHITE)
            self.screen.blit(txt, (rect.x + 6, rect.y + 5))
            main_y += 32

        # ---- 子任务列表 ----
        sub_y = content_y
        current_main = main_tasks[current_main_task]

        # 语音动作组：顶部 Play/Reset 按钮
        if is_voice_group:
            btn_y = content_y + 4
            play_x = sub_x
            reset_x = sub_x + btn_w + btn_gap

            play_color = BTN_PLAY_ACTIVE if action_playing else BTN_PLAY
            play_rect = pygame.Rect(play_x, btn_y, btn_w, btn_h)
            pygame.draw.rect(self.screen, play_color, play_rect, border_radius=6)
            if action_playing:
                pygame.draw.rect(self.screen, (74, 222, 128), play_rect, 2, border_radius=6)
            play_txt = self.font_sm.render("▶ Play", True, TEXT_WHITE)
            self.screen.blit(play_txt, (play_rect.x + 6, play_rect.y + 6))

            reset_color = BTN_RESET_ACTIVE if action_resetting else BTN_RESET
            reset_rect = pygame.Rect(reset_x, btn_y, btn_w, btn_h)
            pygame.draw.rect(self.screen, reset_color, reset_rect, border_radius=6)
            if action_resetting:
                pygame.draw.rect(self.screen, (252, 165, 165), reset_rect, 2, border_radius=6)
            reset_txt = self.font_sm.render("⟳ Reset", True, TEXT_WHITE)
            self.screen.blit(reset_txt, (reset_rect.x + 6, reset_rect.y + 6))

            sep_y = btn_y + btn_h + 4
            pygame.draw.line(
                self.screen, PANEL_BORDER,
                (sub_x, sep_y), (px + pw - 10, sep_y), 1,
            )
            sub_y += voice_action_h

        for j, sub_name in enumerate(current_main["subs"]):
            if is_voice_group:
                vis_idx = j - voice_scroll_offset
                if vis_idx < 0:
                    continue
                max_visible = (ph - 50 - voice_action_h - 10) // 32
                if vis_idx >= max_visible:
                    continue
                actual_y = sub_y + vis_idx * 32
            else:
                actual_y = sub_y + j * 32

            sid = f"{current_main_task}-{j}"
            on = sub_task_states.get(sid, False) if not is_voice_group else False
            rect = pygame.Rect(px + 10 + main_w + 6, actual_y, sub_w, 28)
            pygame.draw.rect(self.screen, TASK_BG, rect, border_radius=4)
            txt = self.font_sm.render(sub_name, True, TEXT_WHITE)
            self.screen.blit(txt, (rect.x + 6, rect.y + 5))
            if not is_voice_group:
                dot_color = ACCENT_GREEN if on else TEXT_DIM
                pygame.draw.circle(self.screen, dot_color, (rect.right - 10, rect.y + 14), 5)

        # 滚动提示
        if is_voice_group:
            total = len(current_main["subs"])
            max_visible = (ph - 50 - voice_action_h - 10) // 32
            if total > max_visible:
                scroll_hint = self.font_sm.render(
                    f"↑↓ 滚动  {voice_scroll_offset + 1}-"
                    f"{min(voice_scroll_offset + max_visible, total)}/{total}",
                    True, TEXT_GRAY,
                )

        # ---- 系统状态 ----
        status_y = py + ph - 70
        pygame.draw.line(
            self.screen, PANEL_BORDER,
            (px + 12, status_y - 6), (px + pw - 12, status_y - 6), 1,
        )
        st = self.font_sm.render("系统状态", True, ACCENT_GREEN)
        self.screen.blit(st, (px + 12, status_y))

        ctrl = "已连接" if dtu_connected else "已断开"
        ctrl_c = ACCENT_GREEN if dtu_connected else ACCENT_RED
        video = "已连接" if lan_connected else "已断开"
        video_c = ACCENT_GREEN if lan_connected else ACCENT_RED

        self.screen.blit(self.font_sm.render("控制链路:", True, TEXT_WHITE), (px + 12, status_y + 22))
        self.screen.blit(self.font_sm.render(ctrl, True, ctrl_c), (px + pw - 90, status_y + 22))
        self.screen.blit(self.font_sm.render("图传链路:", True, TEXT_WHITE), (px + 12, status_y + 42))
        self.screen.blit(self.font_sm.render(video, True, video_c), (px + pw - 90, status_y + 42))

    # ==================== 视频面板 ====================
    def _draw_video_panel(self, px, py, pw, ph, camera_frame, camera_playing):
        content_y = py + 42
        vw = pw - 24
        vh = ph - 80
        pygame.draw.rect(self.screen, VIDEO_BG, (px + 12, content_y, vw, vh), border_radius=6)

        if camera_frame is not None and camera_playing:
            frame_scaled = pygame.transform.scale(camera_frame, (vw, vh))
            self.screen.blit(frame_scaled, (px + 12, content_y))
        elif not camera_playing:
            overlay = pygame.Surface((vw, vh), pygame.SRCALPHA)
            overlay.fill((0, 0, 0, 180))
            self.screen.blit(overlay, (px + 12, content_y))
            pause = self.font_md.render("⏸ 已暂停 [C键播放]", True, TEXT_WHITE)
            self.screen.blit(
                pause,
                (px + 12 + vw // 2 - pause.get_width() // 2,
                 content_y + vh // 2 - pause.get_height() // 2),
            )
        else:
            wait = self.font_md.render("等待视频流连接...", True, TEXT_DIM)
            self.screen.blit(
                wait,
                (px + 12 + vw // 2 - wait.get_width() // 2,
                 content_y + vh // 2 - wait.get_height() // 2),
            )

        info_y = content_y + vh + 8
        self.screen.blit(
            self.font_sm.render("分辨率: 480P@20fps", True, TEXT_GRAY),
            (px + 12, info_y),
        )
        cw = int(self.font_sm.size("分辨率: 480P@20fps")[0])
        self.screen.blit(
            self.font_sm.render("编码: H.264", True, TEXT_GRAY),
            (px + 12 + cw + 30, info_y),
        )
        self.screen.blit(
            self.font_sm.render("延迟: <200ms", True, TEXT_GRAY),
            (px + pw - 90, info_y),
        )

    # ==================== 日志面板 ====================
    def _draw_log_panel(self, px, py, pw, ph, logs):
        content_y = py + 42
        log_h = ph - 50
        log_rect = pygame.Rect(px + 10, content_y, pw - 20, log_h)
        pygame.draw.rect(self.screen, LOG_BG, log_rect, border_radius=4)

        vis_count = min(len(logs), int(log_h / 18))
        start = len(logs) - vis_count
        for i in range(vis_count):
            ts, msg, level = logs[start + i]
            if level == "success":
                c = ACCENT_GREEN
            elif level == "warning":
                c = ACCENT_YELLOW
            elif level == "error":
                c = ACCENT_RED
            else:
                c = TEXT_GRAY
            txt = f"[{ts}] {msg}"
            surf = self.font_mono.render(txt, True, c)
            self.screen.blit(surf, (log_rect.x + 6, log_rect.y + 6 + i * 18))

        help_y = content_y + log_h + 10
        help_title = self.font_md.render("操作说明", True, ACCENT_YELLOW)
        self.screen.blit(help_title, (px + 12, help_y))
        helps = [
            "• 摇杆: 控制机器人移动",
            "• 数字键1-4: 切换主任务分类",
            "• C键: 相机播放/暂停",
        ]
        for i, h in enumerate(helps):
            self.screen.blit(
                self.font_sm.render(h, True, (209, 213, 219)),
                (px + 12, help_y + 24 + i * 20),
            )

    # ==================== 摇杆状态面板 ====================
    def _draw_joystick_panel(
        self,
        joystick_connected, joystick_name,
        joystick_num_axes, joystick_num_buttons, joystick_num_hats,
        joystick_axis_str, joystick_btn_count,
    ):
        panel_y = self.win_h - 90
        panel_h = 82
        pygame.draw.rect(
            self.screen, PANEL_BG,
            (12, panel_y, self.win_w - 24, panel_h), border_radius=8,
        )
        title = self.font_md.render("遥控器数据透传", True, ACCENT_BLUE)
        self.screen.blit(title, (24, panel_y + 6))
        pygame.draw.line(
            self.screen, PANEL_BORDER,
            (24, panel_y + 30), (self.win_w - 24, panel_y + 30), 1,
        )

        info_x = 30
        info_y = panel_y + 40

        if joystick_connected:
            conn_color = ACCENT_GREEN
            conn_text = (
                f"✅ {joystick_name} | 轴:{joystick_num_axes} "
                f"按钮:{joystick_num_buttons} 十字帽:{joystick_num_hats}"
            )
        else:
            conn_color = ACCENT_RED
            conn_text = "❌ 未检测到手柄"

        self.screen.blit(self.font_sm.render(conn_text, True, conn_color), (info_x, info_y))

        if joystick_connected and joystick_axis_str:
            axis_text = joystick_axis_str
            self.screen.blit(
                self.font_sm.render(axis_text, True, TEXT_GRAY),
                (info_x + 12, info_y + 22),
            )
            btn_text = f"按钮按下: {joystick_btn_count}/{joystick_num_buttons}"
            cw = int(self.font_sm.size(axis_text)[0]) if axis_text else 0
            self.screen.blit(
                self.font_sm.render(btn_text, True, ACCENT_YELLOW),
                (info_x + 12 + cw + 40, info_y + 22),
            )
        else:
            self.screen.blit(
                self.font_sm.render("请在启动前连接USB手柄", True, TEXT_DIM),
                (info_x + 12, info_y + 22),
            )

        hint = self.font_sm.render(
            "摇杆=运动控制 | 数字键1-4=切换主任务 | C=相机 | ESC=退出",
            True, TEXT_GRAY,
        )
        self.screen.blit(
            hint,
            (self.win_w // 2 - hint.get_width() // 2, panel_y + panel_h - 18),
        )
