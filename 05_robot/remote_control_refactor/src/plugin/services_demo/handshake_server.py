"""
握手交互 WebSocket 服务端
监听 9999 端口处理握手指令

客户端发送 JSON:
  {"cmd": "auto"}     # 自动感知模式
  {"cmd": "on"}       # 强制握手开启
  {"cmd": "off"}      # 强制握手关闭

服务端回复 JSON:
  {"event": "ack"}              # 确认收到指令
  {"event": "completed"}        # 执行完成
  {"event": "error", "msg": "..."}
"""
import asyncio
import json
import websockets


async def handle_client(websocket):
    print(f"[HS] 客户端已连接: {websocket.remote_address}")

    try:
        async for raw in websocket:
            try:
                msg = json.loads(raw)
                cmd = msg.get("cmd", "")

                if cmd in ("auto", "on", "off"):
                    print(f"[HS] 指令: {cmd}")
                    await websocket.send(json.dumps({"event": "ack"}))

                    # === 在这里执行握手逻辑 ===
                    # ...

                    await asyncio.sleep(0.3)
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
        print(f"[HS] 客户端断开: {websocket.remote_address}")
    except Exception as e:
        print(f"[HS] 错误: {e}")


async def main():
    host = "0.0.0.0"
    port = 9999
    print(f"[HS] 握手服务启动: ws://{host}:{port}/handshake")

    async with websockets.serve(
        handle_client, host, port,
        ping_interval=10, ping_timeout=5,
    ):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
