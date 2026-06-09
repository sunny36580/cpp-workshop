"""
动作组 WebSocket 服务端（含 UDP 发现）
- 监听 UDP 9999 端口响应客户端发现请求
- 监听 WebSocket 9998 端口处理动作指令

客户端发送 JSON:
  {"cmd": "play", "para": 3}   # 播放语音段落 3
  {"cmd": "play"}              # 播放整组
  {"cmd": "reset"}             # 归位

服务端回复 JSON:
  {"event": "ack"}                     # 确认收到指令
  {"event": "completed"}               # 播放完成
  {"event": "error", "msg": "..."}     # 错误
"""
import asyncio
import json
import socket
import threading
import websockets

# ==== UDP 发现常量（与客户端保持一致）====
DISCOVERY_PORT = 9999
DISCOVERY_MSG  = b"DISCOVER_ACTION_SERVER"
DISCOVERY_RESP = b"ACTION_SERVER_HERE"

# ========== 在这里实现你的实际动作执行逻辑 ==========
# async def execute_play(para: int = 0):
#     """播放语音动作组，para=0播放整组"""
#     ...
#
# async def execute_reset():
#     """动作归位"""
#     ...


async def handle_client(websocket):
    """处理单个 WebSocket 客户端"""
    print(f"[WS] 客户端已连接: {websocket.remote_address}")

    try:
        async for raw in websocket:
            try:
                msg = json.loads(raw)
                cmd = msg.get("cmd", "")

                if cmd == "play":
                    para = msg.get("para", 0)
                    print(f"[WS] 指令: play para={para}")

                    # 回复 ACK
                    await websocket.send(json.dumps({"event": "ack"}))

                    # === 在这里执行播放逻辑 ===
                    # await execute_play(para)

                    # 模拟播放耗时
                    await asyncio.sleep(1.0)

                    # 通知完成
                    await websocket.send(json.dumps({"event": "completed"}))

                elif cmd == "reset":
                    print("[WS] 指令: reset")

                    await websocket.send(json.dumps({"event": "ack"}))

                    # === 在这里执行归位逻辑 ===
                    # await execute_reset()

                    await asyncio.sleep(0.5)
                    await websocket.send(json.dumps({"event": "completed"}))

                else:
                    await websocket.send(json.dumps({
                        "event": "error", "msg": f"未知指令: {cmd}"
                    }))

            except json.JSONDecodeError:
                await websocket.send(json.dumps({
                    "event": "error", "msg": "无效 JSON"
                }))

    except websockets.ConnectionClosed:
        print(f"[WS] 客户端断开: {websocket.remote_address}")
    except Exception as e:
        print(f"[WS] 错误: {e}")


def udp_discovery():
    """UDP 发现监听（线程）：收到客户端广播后回复本机 IP"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", DISCOVERY_PORT))
    sock.settimeout(1.0)
    print(f"[UDP] 发现服务监听: 0.0.0.0:{DISCOVERY_PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(256)
            if data == DISCOVERY_MSG:
                print(f"[UDP] 发现请求来自 {addr}")
                sock.sendto(DISCOVERY_RESP, addr)
        except socket.timeout:
            continue
        except Exception:
            break
    sock.close()


async def main():
    host = "0.0.0.0"
    ws_port = 9998
    print(f"[WS] 动作组服务启动: ws://{host}:{ws_port}/action")

    # 线程运行 UDP 发现
    udp_thread = threading.Thread(target=udp_discovery, daemon=True)
    udp_thread.start()

    async with websockets.serve(
        handle_client, host, ws_port,
        ping_interval=10, ping_timeout=5,
    ):
        await asyncio.Future()  # 永久运行


if __name__ == "__main__":
    asyncio.run(main())
