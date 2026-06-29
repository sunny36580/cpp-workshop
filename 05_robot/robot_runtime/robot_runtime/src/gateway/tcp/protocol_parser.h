#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>

namespace robot_runtime::net {

// ============================================================================
// 指令类型
// ============================================================================
enum CommandType : uint16_t {
    // 服务管控
    CMD_SERVICE_START   = 0x0001,
    CMD_SERVICE_STOP    = 0x0002,
    CMD_SERVICE_RESTART = 0x0003,
    CMD_SERVICE_STATUS  = 0x0004,
    CMD_SERVICE_LIST    = 0x0005,

    // 模式管控
    CMD_MODE_SWITCH  = 0x0101,
    CMD_MODE_CURRENT = 0x0102,
    CMD_MODE_LIST    = 0x0103,

    // 监控查询
    CMD_MONITOR_STATUS = 0x0201,
    CMD_MONITOR_FAILURES = 0x0202,

    // 能力调用（预留）
    CMD_CAPABILITY_EXEC = 0x0301,
    CMD_CAPABILITY_LIST = 0x0302,

    // 系统
    CMD_HEARTBEAT = 0xFF01,
    CMD_AUTH      = 0xFF02,
};

// ============================================================================
// 二进制帧结构
// ============================================================================
//  帧头(2) | 版本(1) | 指令(2) | 长度(2) | 数据体(N) | 校验(1) | 帧尾(2)
//   AA55   |   01    |  XXXX   |  NNNN   |  ......   |  CRC    |  0D0A
// ============================================================================
static constexpr uint8_t  FRAME_HEADER1    = 0xAA;
static constexpr uint8_t  FRAME_HEADER2    = 0x55;
static constexpr uint8_t  FRAME_FOOTER1    = 0x0D;
static constexpr uint8_t  FRAME_FOOTER2    = 0x0A;
static constexpr uint8_t  PROTOCOL_VERSION = 0x01;
static constexpr size_t   FRAME_MIN_SIZE   = 8;   // 2+1+2+2+0+1+2
static constexpr size_t   FRAME_HEADER_SIZE = 5;  // 2+1+2
static constexpr size_t   FRAME_FOOTER_SIZE = 3;  // 1+2
static constexpr size_t   MAX_DATA_SIZE    = 4096;

// 响应码
enum ResponseCode : uint8_t {
    RESP_OK       = 0x00,
    RESP_ERROR    = 0x01,
    RESP_NOT_FOUND= 0x02,
    RESP_DENIED   = 0x03,
    RESP_TIMEOUT  = 0x04,
    RESP_INVALID  = 0x05,
};

// ============================================================================
// 请求包
// ============================================================================
struct RequestPacket {
    uint8_t  version = PROTOCOL_VERSION;
    uint16_t cmd     = 0;
    std::vector<uint8_t> data;
};

// ============================================================================
// 响应包
// ============================================================================
struct ResponsePacket {
    uint8_t  version = PROTOCOL_VERSION;
    uint16_t cmd     = 0;       // 回显请求指令
    uint8_t  code    = RESP_OK; // 响应码
    std::vector<uint8_t> data;  // 返回数据
};

// ============================================================================
// CRC8 校验（多项式 x^8 + x^2 + x + 1）
// ============================================================================
inline uint8_t crc8(const uint8_t* buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

// ============================================================================
// 打包请求帧
// ============================================================================
inline std::vector<uint8_t> pack_request(const RequestPacket& req) {
    size_t data_len = req.data.size();
    std::vector<uint8_t> frame(FRAME_HEADER_SIZE + 2 + data_len + FRAME_FOOTER_SIZE);

    size_t pos = 0;
    frame[pos++] = FRAME_HEADER1;
    frame[pos++] = FRAME_HEADER2;
    frame[pos++] = req.version;
    frame[pos++] = static_cast<uint8_t>(req.cmd >> 8);
    frame[pos++] = static_cast<uint8_t>(req.cmd & 0xFF);
    frame[pos++] = static_cast<uint8_t>((data_len >> 8) & 0xFF);
    frame[pos++] = static_cast<uint8_t>(data_len & 0xFF);

    if (data_len > 0) {
        memcpy(&frame[pos], req.data.data(), data_len);
        pos += data_len;
    }

    // CRC8 of [version .. data]
    uint8_t crc = crc8(frame.data() + 2, pos - 2);  // from version byte
    frame[pos++] = crc;
    frame[pos++] = FRAME_FOOTER1;
    frame[pos++] = FRAME_FOOTER2;

    return frame;
}

// ============================================================================
// 打包响应帧
// ============================================================================
inline std::vector<uint8_t> pack_response(const ResponsePacket& resp) {
    size_t data_len = resp.data.size();
    std::vector<uint8_t> frame(FRAME_HEADER_SIZE + 2 + 1 + data_len + FRAME_FOOTER_SIZE);

    size_t pos = 0;
    frame[pos++] = FRAME_HEADER1;
    frame[pos++] = FRAME_HEADER2;
    frame[pos++] = resp.version;
    frame[pos++] = static_cast<uint8_t>(resp.cmd >> 8);
    frame[pos++] = static_cast<uint8_t>(resp.cmd & 0xFF);
    // 响应帧：长度域包含 code(1) + data(N)
    uint16_t body_len = 1 + data_len;
    frame[pos++] = static_cast<uint8_t>((body_len >> 8) & 0xFF);
    frame[pos++] = static_cast<uint8_t>(body_len & 0xFF);
    frame[pos++] = resp.code;

    if (data_len > 0) {
        memcpy(&frame[pos], resp.data.data(), data_len);
        pos += data_len;
    }

    uint8_t crc = crc8(frame.data() + 2, pos - 2);
    frame[pos++] = crc;
    frame[pos++] = FRAME_FOOTER1;
    frame[pos++] = FRAME_FOOTER2;

    return frame;
}

// ============================================================================
// 解析一帧（从字节流中提取完整帧）
// 返回值：>0 表示解析成功，帧长度；0 表示需要更多数据；<0 表示格式错误
// ============================================================================
inline int parse_frame(const uint8_t* buf, size_t buf_len,
                       RequestPacket& out_req) {
    if (buf_len < FRAME_MIN_SIZE) return 0;

    // 搜索帧头
    size_t pos = 0;
    while (pos + 1 < buf_len) {
        if (buf[pos] == FRAME_HEADER1 && buf[pos + 1] == FRAME_HEADER2) break;
        pos++;
    }
    if (pos + 1 >= buf_len) return 0;
    if (pos > 0) return -static_cast<int>(pos);  // 跳过了无效字节

    // 解析固定头
    size_t hdr_end = pos + FRAME_HEADER_SIZE;  // after cmd bytes
    if (buf_len < hdr_end + 2) return 0;  // need length field

    out_req.version = buf[pos + 2];
    out_req.cmd     = (static_cast<uint16_t>(buf[pos + 3]) << 8) |
                      static_cast<uint16_t>(buf[pos + 4]);

    uint16_t data_len = (static_cast<uint16_t>(buf[pos + 5]) << 8) |
                         static_cast<uint16_t>(buf[pos + 6]);

    if (data_len > MAX_DATA_SIZE) return -2;  // 数据过长

    size_t total_len = FRAME_HEADER_SIZE + 2 + data_len + FRAME_FOOTER_SIZE;
    if (buf_len < total_len) return 0;  // 还不完整

    // 校验 CRC8（从 version 到 data 末尾）
    size_t crc_pos = pos + FRAME_HEADER_SIZE + 2 + data_len;
    uint8_t expected_crc = crc8(buf + pos + 2, crc_pos - pos - 2);
    if (buf[crc_pos] != expected_crc) return -3;  // CRC 错误

    // 校验帧尾
    if (buf[crc_pos + 1] != FRAME_FOOTER1 ||
        buf[crc_pos + 2] != FRAME_FOOTER2) return -4;

    // 提取数据
    size_t data_start = pos + FRAME_HEADER_SIZE + 2;
    if (data_len > 0) {
        out_req.data.assign(buf + data_start, buf + data_start + data_len);
    }

    return static_cast<int>(total_len);
}

// ============================================================================
// 工具：字符串 ↔ 字节向量
// ============================================================================
inline std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

inline std::string to_string(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

} // namespace robot_runtime::net
