#include "runtime/service_access/service_registry.h"

namespace robot_runtime {

void ServiceRegistry::register_service(const ServiceEntry& entry) {
    entries_[entry.service_name] = entry;
    if (on_registered_) on_registered_(entry);
}

void ServiceRegistry::register_service(ServiceEntry&& entry) {
    const auto& name = entry.service_name;
    if (on_registered_) on_registered_(entry);
    entries_[name] = std::move(entry);
}

void ServiceRegistry::unregister_service(const std::string& service_name) {
    auto it = entries_.find(service_name);
    if (it != entries_.end()) {
        if (on_unregistered_) on_unregistered_(it->second);
        entries_.erase(it);
    }
}

const ServiceEntry* ServiceRegistry::find(const std::string& service_name) const {
    auto it = entries_.find(service_name);
    return (it != entries_.end()) ? &it->second : nullptr;
}

std::vector<const ServiceEntry*> ServiceRegistry::find_by_capability(const std::string& cap_name) const {
    std::vector<const ServiceEntry*> result;
    for (const auto& [_, entry] : entries_) {
        for (const auto& cap : entry.capabilities) {
            if (cap.name == cap_name) {
                result.push_back(&entry);
                break;
            }
        }
    }
    return result;
}

std::vector<const ServiceEntry*> ServiceRegistry::find_by_protocol(const std::string& protocol) const {
    std::vector<const ServiceEntry*> result;
    for (const auto& [_, entry] : entries_) {
        for (const auto& cap : entry.capabilities) {
            if (cap.protocol == protocol) {
                result.push_back(&entry);
                break;
            }
        }
    }
    return result;
}

std::vector<const ServiceEntry*> ServiceRegistry::all() const {
    std::vector<const ServiceEntry*> result;
    for (const auto& [_, entry] : entries_) {
        result.push_back(&entry);
    }
    return result;
}

void ServiceRegistry::update_alive(const std::string& service_name, bool alive) {
    auto it = entries_.find(service_name);
    if (it != entries_.end()) {
        it->second.alive = alive;
    }
}

void ServiceRegistry::clear() {
    entries_.clear();
}

} // namespace robot_runtime
