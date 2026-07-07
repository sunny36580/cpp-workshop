"""
WebSocket 动作组客户端

通过 WebSocket 向局域网内的动作组服务发送 play/reset 指令。
协议为 JSON，支持双向通信：发指令、收 ACK + 事件推送。
"""

import json
import threading
import time

import websockets.sync.client

from .config import WS_ACTION_URI, WS_CONNECT_TIMEOUT, WS_CMD_TIMEOUT, ActionCmdType


class WsActionClient:
    """动作组 WebSocket 客户端

    后台自动维持连接（断线重连），提供 send_cmd() 同步等待 ACK 的接口。
    """

    def __init__(self, on_log=None):
        self._ws = None
        self._connected = False
        self._running = True
        self._lock = threading.Lock()
        self._ack_event = threading.Event()

        self.playing = False
        self.resetting = False

        # 日志回调
        self._on_log = on_log

        # 启动后台连接线程
        self._connect_retry = True
        threading.Thread(target=self._connect_loop, daemon=True).start()

    # ---- 属性 ----
    @property
    def connected(self) -> bool:
        return self._connected

    # ---- 生命周期 ----
    def shutdown(self):
        """关闭连接，停止重连"""
        self._running = False
        with self._lock:
            if self._ws:
                try:
                    self._ws.close()
                except Exception:
                    pass
                self._ws = None
            self._connected = False

    # ---- 日志 ----
    def _log(self, msg, level="info"):
        if self._on_log:
            self._on_log(msg, level)

    # ---- 连接管理 ----
    def _connect_loop(self):
        """后台线程：保持 WebSocket 连接，断线自动重连"""
        while self._running:
            try:
                with self._lock:
                    if self._ws:
                        try:
                            self._ws.close()
                        except Exception:
                            pass
                        self._ws = None
                    self._connected = False

                ws = websockets.sync.client.connect(
                    WS_ACTION_URI, timeout=WS_CONNECT_TIMEOUT,
                )
                print(f"✅ WebSocket 已连接: {WS_ACTION_URI}")

                with self._lock:
                    self._ws = ws
                    self._connected = True

                # 接收循环
                while self._running:
                    try:
                        raw = ws.recv()
                        if raw is None:
                            break
                        msg = json.loads(raw)
                        event = msg.get("event", "")
                        if event == "ack":
                            self._ack_event.set()
                        elif event == "completed":
                            self.playing = False
                            self._log("动作组播放完成", "success")
                            print("[WS] 动作组播放完成")
                        elif event == "error":
                            self._log(f"动作组错误: {msg.get('msg', '')}", "error")
                            print(f"[WS] 动作组错误: {msg}")
                    except json.JSONDecodeError:
                        continue
                    except (websockets.ConnectionClosed, OSError):
                        break

            except (
                websockets.InvalidURI, websockets.InvalidHandshake,
                OSError, TimeoutError,
            ) as e:
                if self._running:
                    print(f"⚠️ WebSocket 连接失败 ({e}), 5s 后重试...")
                    self._log("动作组服务未连接，等待重试...", "warning")
            except Exception as e:
                print(f"❌ WebSocket 异常: {e}")

            with self._lock:
                self._connected = False
                self._ws = None

            if self._running:
                time.sleep(5.0)

    # ---- 指令发送 ----
    def send_cmd(self, cmd_type: ActionCmdType, para: int = 0) -> bool:
        """发送动作指令，等待 ACK 确认。

        Args:
            cmd_type: PLAY 或 RESET
            para: 语音段落号（0=播放整组）

        Returns:
            True 表示收到服务端 ACK。
        """
        msg: dict = {"cmd": cmd_type.value}
        if cmd_type == ActionCmdType.PLAY and para > 0:
            msg["para"] = para

        with self._lock:
            if not self._ws or not self._connected:
                self._log(f"动作组服务未连接，无法发送 {cmd_type.value}", "warning")
                return False
            ws = self._ws

        try:
            self._ack_event.clear()
            ws.send(json.dumps(msg))
            print(f"📤 WS -> {WS_ACTION_URI}  {msg}")

            if self._ack_event.wait(timeout=WS_CMD_TIMEOUT):
                self._log(f"✅ 动作指令确认: {cmd_type.value}", "success")
                return True
            else:
                self._log(f"⚠️ 动作指令超时: {cmd_type.value}", "warning")
                return False

        except (websockets.ConnectionClosed, OSError, AttributeError) as e:
            self._log(f"WebSocket 发送失败: {e}", "error")
            with self._lock:
                self._connected = False
            return False
