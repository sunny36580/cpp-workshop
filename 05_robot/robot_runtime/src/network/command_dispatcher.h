#pragma once

#include "network/protocol_parser.h"
#include "manager/service_manager/service_manager.h"

#include <cstdio>
#include <string>

namespace robot_runtime {
class Runtime;
} // namespace robot_runtime

namespace robot_runtime::net {

// ============================================================================
// 工具：打包服务状态列表为二进制格式
// ============================================================================
inline std::vector<uint8_t> pack_service_status_list(
    const std::vector<ServiceStatus>& statuses) {
    std::vector<uint8_t> data;
    for (const auto& s : statuses) {
        auto name_bytes = to_bytes(s.name);
        auto type_bytes = to_bytes(s.type);
        uint8_t state_val = static_cast<uint8_t>(s.state);
        uint8_t alive_val = s.alive ? 1 : 0;

        uint16_t name_len = static_cast<uint16_t>(name_bytes.size());
        uint16_t type_len = static_cast<uint16_t>(type_bytes.size());

        auto push16 = [&](uint16_t v) {
            data.push_back(static_cast<uint8_t>(v >> 8));
            data.push_back(static_cast<uint8_t>(v & 0xFF));
        };

        push16(name_len);
        data.insert(data.end(), name_bytes.begin(), name_bytes.end());
        push16(type_len);
        data.insert(data.end(), type_bytes.begin(), type_bytes.end());
        data.push_back(state_val);
        data.push_back(alive_val);
        uint32_t pid32 = static_cast<uint32_t>(s.pid);
        data.push_back(static_cast<uint8_t>((pid32 >> 24) & 0xFF));
        data.push_back(static_cast<uint8_t>((pid32 >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((pid32 >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>(pid32 & 0xFF));
    }
    return data;
}

// ============================================================================
// CommandDispatcher — 指令分发层
// ============================================================================
// 将网络协议指令映射到 Runtime 管理器接口，完全复用现有管控逻辑。
// 和本地 CLI 属于同一层级的入口，无逻辑割裂。
// ============================================================================
class CommandDispatcher {
public:
    explicit CommandDispatcher(Runtime& runtime)
        : runtime_(runtime) {}

    // 处理请求，返回响应
    ResponsePacket dispatch(const RequestPacket& req) {
        ResponsePacket resp;
        resp.version = req.version;
        resp.cmd     = req.cmd;
        resp.code    = RESP_OK;

        switch (req.cmd) {
            // ---- 服务管控 ----
            case CMD_SERVICE_START: {
                std::string name = to_string(req.data);
                if (!runtime_.start_service(name)) {
                    resp.code = RESP_ERROR;
                    resp.data = to_bytes("start failed");
                }
                break;
            }
            case CMD_SERVICE_STOP: {
                std::string name = to_string(req.data);
                if (!runtime_.stop_service(name)) {
                    resp.code = RESP_ERROR;
                    resp.data = to_bytes("stop failed");
                }
                break;
            }
            case CMD_SERVICE_RESTART: {
                std::string name = to_string(req.data);
                if (!runtime_.restart_service(name)) {
                    resp.code = RESP_ERROR;
                    resp.data = to_bytes("restart failed");
                }
                break;
            }
            case CMD_SERVICE_STATUS: {
                std::string name = to_string(req.data);
                auto svc = runtime_.get_service(name);
                if (!svc) {
                    resp.code = RESP_NOT_FOUND;
                    resp.data = to_bytes("service not found");
                } else {
                    auto st = svc->status();
                    std::vector<ServiceStatus> list = {st};
                    resp.data = pack_service_status_list(list);
                }
                break;
            }
            case CMD_SERVICE_LIST: {
                auto all = runtime_.all_status();
                resp.data = pack_service_status_list(all);
                break;
            }

            // ---- 模式管控 ----
            case CMD_MODE_SWITCH: {
                std::string mode = to_string(req.data);
                if (!runtime_.switch_mode(mode)) {
                    resp.code = RESP_ERROR;
                    resp.data = to_bytes("switch mode failed");
                }
                break;
            }
            case CMD_MODE_CURRENT: {
                auto& mm = runtime_.mode_manager();
                resp.data = to_bytes(mm.current_mode());
                break;
            }
            case CMD_MODE_LIST: {
                auto& mm = runtime_.mode_manager();
                std::string result;
                for (const auto& [name, _] : mm.modes()) {
                    if (!result.empty()) result += "\n";
                    result += name;
                    if (name == mm.current_mode()) result += " (current)";
                }
                resp.data = to_bytes(result);
                break;
            }

            // ---- 监控 ----
            case CMD_MONITOR_STATUS: {
                auto all = runtime_.all_status();
                size_t running_count = 0, failed_count = 0;
                for (const auto& s : all) {
                    if (s.state == ServiceState::RUNNING) running_count++;
                    if (s.state == ServiceState::FAILED)  failed_count++;
                }
                std::string info = "total=" + std::to_string(all.size()) +
                                   " running=" + std::to_string(running_count) +
                                   " failed=" + std::to_string(failed_count);
                resp.data = to_bytes(info);
                break;
            }
            case CMD_MONITOR_FAILURES: {
                auto failures = runtime_.monitor_manager().failures();
                std::string info;
                for (const auto& f : failures) {
                    if (!info.empty()) info += "\n";
                    info += f.name + " time=" + std::to_string(f.time);
                }
                if (info.empty()) info = "(none)";
                resp.data = to_bytes(info);
                break;
            }

            // ---- 系统 ----
            case CMD_HEARTBEAT:
                resp.data = to_bytes("pong");
                break;

            case CMD_AUTH:
                // 简单鉴权：暂不实现，预留
                resp.data = to_bytes("auth ok");
                break;

            default:
                resp.code = RESP_INVALID;
                resp.data = to_bytes("unknown command");
                break;
        }

        return resp;
    }

private:
    Runtime& runtime_;
};

} // namespace robot_runtime::net
