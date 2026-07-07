"""
动作组 WebSocket 服务端
点对点连接，监听 WebSocket 9998 端口处理动作指令

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
import websockets

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

                    await asyncio.sleep(1.0)       # 模拟耗时
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


async def main():
    host = "0.0.0.0"
    ws_port = 9998
    print(f"[WS] 动作组服务启动: ws://{host}:{ws_port}/action")

    async with websockets.serve(
        handle_client, host, ws_port,
        ping_interval=10, ping_timeout=5,
    ):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
