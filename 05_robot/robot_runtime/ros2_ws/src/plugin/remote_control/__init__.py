"""
remote_control - 三代人形机器人远程控制系统

包结构：
    config.py              配置常量与枚举
    protocol.py            CRC16 校验与二进制帧构建
    font_utils.py          跨平台中文字体加载
    camera_streamer.py     相机 TCP/H.264 解码与渲染
    ws_action_client.py    动作组 WebSocket 客户端
    ws_handshake_client.py 握手交互 WebSocket 客户端
    serial_io.py           串口通信（收发二进制帧）
    joystick_handler.py    游戏摇杆原始数据读取
    ui.py                  Pygame UI 面板绘制
    robot_remote.py        主应用类 RobotRemote（胶水层）
    main.py                程序入口
"""
