"""
相机 TCP/H.264 解码流

从机端 TCP 端口读取 H.264 编码数据，用 PyAV 解码为 pygame 可渲染的 surface。
支持自动重连、帧率统计、解码异常检测。
"""

import socket
import threading
import time

import pygame

from .config import CAMERA_IP, CAMERA_PORT


class CameraStreamer:
    """相机 TCP 流接收 + H.264 解码

    在后台线程中运行，将最新帧写入 self.frame（线程安全）。
    通过 start() / stop() 控制生命周期。
    """

    def __init__(self, on_log=None):
        self.frame = None
        self.frame_lock = threading.Lock()

        self._playing = False
        self._running = False
        self._thread = None

        # 统计
        self.last_frame_time = 0
        self.frame_count = 0
        self.fps_log_time = 0
        self.fps_frame_count = 0
        self.max_interval = 0
        self.interval_frame_count = 0
        self.interval_log_time = 0

        # 日志回调（可选，用于向 UI 日志面板输出消息）
        self._on_log = on_log

    # ---- 属性 ----
    @property
    def is_playing(self) -> bool:
        return self._playing

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def safe_frame(self):
        """线程安全地获取当前帧"""
        with self.frame_lock:
            return self.frame

    # ---- 生命周期 ----
    def start(self):
        """启动相机接收线程"""
        if self._running:
            return
        self._playing = True
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        """停止相机接收（保留最后一帧画面）"""
        self._running = False
        self._playing = False

    def toggle(self):
        """切换播放/停止"""
        if self._playing:
            self.stop()
        else:
            self.start()
        return self._playing

    # ---- 内部循环 ----
    def _log(self, msg, level="info"):
        if self._on_log:
            self._on_log(msg, level)

    def _loop(self):
        """相机接收线程：TCP → H.264 → pygame 解码（带自动重连）"""
        while self._running and self._playing:
            sock = None
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2.0)
                sock.connect((CAMERA_IP, CAMERA_PORT))
                print("✅ TCP 相机已连接")

                import av
                codec = av.CodecContext.create('h264', 'r')

                while self._running and self._playing:
                    # 读4字节数据长度
                    try:
                        len_data = sock.recv(4)
                    except socket.timeout:
                        print("[CAM] 接收超时，主动断开重连")
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

                    # 解码 H.264 ES
                    frame_received = False
                    try:
                        frames = codec.decode(av.Packet(data))
                        for frame in frames:
                            frame_received = True
                            img = frame.to_ndarray(format='bgr24')
                            raw = pygame.image.frombuffer(
                                img.tobytes(), (img.shape[1], img.shape[0]), "BGR"
                            )
                            with self.frame_lock:
                                self.frame = raw

                            # 帧间隔统计
                            now = time.time()
                            if self.last_frame_time > 0:
                                interval = now - self.last_frame_time
                                if interval > self.max_interval:
                                    self.max_interval = interval
                            self.last_frame_time = now
                            self.frame_count += 1
                            self.fps_frame_count += 1
                            self.interval_frame_count += 1

                            if self.interval_frame_count >= 300:
                                print(
                                    f"[CAM] 最近300帧: 最大帧间隔="
                                    f"{self.max_interval * 1000:.0f}ms"
                                )
                                self.max_interval = 0
                                self.interval_frame_count = 0

                            if now - self.fps_log_time >= 10.0:
                                actual_fps = self.fps_frame_count / (
                                    now - self.fps_log_time
                                )
                                print(
                                    f"[CAM] 实际接收帧率: {actual_fps:.1f} fps, "
                                    f"总帧数: {self.frame_count}"
                                )
                                self.fps_log_time = now
                                self.fps_frame_count = 0
                    except Exception as e:
                        print(f"[CAM] decode error: {e}")

                    if not frame_received:
                        print(
                            f"[CAM] no frame decoded from {length} bytes, "
                            "解码器可能卡死，断开重连"
                        )
                        break

            except socket.timeout:
                print("[CAM] 相机连接超时")
            except ConnectionRefusedError:
                print("[CAM] 相机服务未启动")
            except (OSError, BrokenPipeError) as e:
                print(f"[CAM] 相机断开: {e}")
            except ImportError:
                print("[CAM] 需要安装 av 库: pip install av")
                self._playing = False
                break
            except Exception as e:
                print(f"[CAM] 相机错误: {e}")
            finally:
                if sock:
                    try:
                        sock.close()
                    except Exception:
                        pass

            if self._running and self._playing:
                print("[CAM] 相机断开，立即重连...")
            else:
                break
