"""
二进制串口协议层

提供 CRC16-Modbus 校验和 32 字节固定帧构建功能，
与 C++ 端模块管理器的串口协议完全对齐。
"""

from .config import (
    SERIAL_SOF0, SERIAL_SOF1, SERIAL_HEADER_LEN,
    SERIAL_DATA_LEN, SERIAL_RESERVED, SERIAL_CRC_LEN, SERIAL_FRAME_LEN,
)


def calc_crc16(data: bytes) -> int:
    """CRC16-Modbus 校验，与 C++ 端 calcCRC16 一致"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_frame(cmd_type: int, payload: bytes = b"") -> bytes:
    """构建 32 字节固定帧:
       帧头(2) + CmdType(1) + PayLen(1) + 数据域(16) + 保留(10) + CRC16(2)
    """
    frame = bytes([SERIAL_SOF0, SERIAL_SOF1, cmd_type & 0xFF, len(payload) & 0xFF])

    # 数据域 16 字节（填充 0）
    data_field = bytearray(SERIAL_DATA_LEN)
    copy_len = min(len(payload), SERIAL_DATA_LEN)
    data_field[:copy_len] = payload[:copy_len]
    frame += bytes(data_field)

    # 保留 10 字节（全 0）
    frame += bytes(SERIAL_RESERVED)

    # CRC16（从 CmdType 到保留末尾）
    crc = calc_crc16(frame[2:])  # 从 CmdType 开始算
    frame += bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    assert len(frame) == SERIAL_FRAME_LEN, f"帧长度错误: {len(frame)} != {SERIAL_FRAME_LEN}"
    return frame


def parse_status_frame(frame: bytes) -> int | None:
    """解析状态帧 (CMD_STATUS)，返回模块状态 mask，解析失败返回 None"""
    if len(frame) < SERIAL_FRAME_LEN:
        return None
    cmd_type = frame[2]
    pay_len = frame[3]
    if cmd_type != 0x04 or pay_len < 2:
        return None
    # CRC16 校验
    recv_crc = frame[-2] | (frame[-1] << 8)
    calc_crc = calc_crc16(frame[2:-2])
    if recv_crc != calc_crc:
        return None
    data = frame[SERIAL_HEADER_LEN:SERIAL_HEADER_LEN + pay_len]
    return (data[0] << 8) | data[1]
