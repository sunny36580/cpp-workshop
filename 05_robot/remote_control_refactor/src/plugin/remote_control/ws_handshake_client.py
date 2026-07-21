"""
WebSocket 握手交互客户端

通过 WebSocket 向握手服务发送 auto / on / off 指令。
"""

import json
import threading
import time

import websockets.sync.client

from .config import WS_HANDSHAKE_URI, WS_CONNECT_TIMEOUT, WS_CMD_TIMEOUT, HandshakeCmdType


class WsHandshakeClient:
    """握手交互 WebSocket 客户端

    后台自动维持连接（断线重连），提供 send_cmd() 同步等待 ACK 的接口。
    """

    def __init__(self, on_log=None):
        self._ws = None
        self._connected = False
        self._running = True
        self._lock = threading.Lock()
        self._ack_event = threading.Event()

        # 日志回调
        self._on_log = on_log

        # 启动后台连接线程
        threading.Thread(target=self._connect_loop, daemon=True).start()

    # ---- 属性 ----
    @property
    def connected(self) -> bool:
        return self._connected

    # ---- 生命周期 ----
    def shutdown(self):
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
        """后台线程：保持握手服务 WebSocket 连接"""
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
                    WS_HANDSHAKE_URI, timeout=WS_CONNECT_TIMEOUT,
                )
                print(f"✅ 握手 WebSocket 已连接: {WS_HANDSHAKE_URI}")

                with self._lock:
                    self._ws = ws
                    self._connected = True

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
                            self._log("握手动作完成", "success")
                            print("[HS] 握手动作完成")
                        elif event == "error":
                            self._log(f"握手错误: {msg.get('msg', '')}", "error")
                    except json.JSONDecodeError:
                        continue
                    except (websockets.ConnectionClosed, OSError):
                        break

            except (
                websockets.InvalidURI, websockets.InvalidHandshake,
                OSError, TimeoutError,
            ):
                if self._running:
                    print("⚠️ 握手 WebSocket 连接失败，5s 后重试...")

            except Exception as e:
                print(f"❌ 握手 WebSocket 异常: {e}")

            with self._lock:
                self._connected = False
                self._ws = None

            if self._running:
                time.sleep(5.0)

    # ---- 指令发送 ----
    def send_cmd(self, cmd: HandshakeCmdType) -> bool:
        """发送握手指令，等待 ACK"""
        msg = {"cmd": cmd.value}

        with self._lock:
            if not self._ws or not self._connected:
                self._log("握手服务未连接", "warning")
                return False
            ws = self._ws

        try:
            self._ack_event.clear()
            ws.send(json.dumps(msg))
            print(f"📤 HS -> {WS_HANDSHAKE_URI}  {msg}")

            if self._ack_event.wait(timeout=WS_CMD_TIMEOUT):
                self._log(f"✅ 握手指令确认: {cmd.value}", "success")
                return True
            else:
                self._log(f"⚠️ 握手指令超时: {cmd.value}", "warning")
                return False

        except (websockets.ConnectionClosed, OSError, AttributeError) as e:
            self._log(f"握手 WebSocket 发送失败: {e}", "error")
            with self._lock:
                self._connected = False
            return False
