#include "DeviceHost.hpp"

#include <cstring>

namespace rp::box {

void DeviceHost::init(const DeviceLink* links, std::size_t count, const rp_device_midi_in* midi, Log log) {
    links_ = links;
    count_ = links ? count : 0;
    midi_ = midi;
    log_ = log;
}

void* DeviceHost::acquireFn(void* ctx, const char* name, const char* interfaceId) {
    const auto& self = *static_cast<const DeviceHost*>(ctx);
    if (!name || !interfaceId) return nullptr;
    for (std::size_t i = 0; i < self.count_; ++i) {
        const DeviceLink& l = self.links_[i];
        if (std::strcmp(l.name, name) != 0 || std::strcmp(l.interfaceId, interfaceId) != 0) continue;
        if (l.acquired) l.acquired(l.hookCtx);
        return l.link;
    }
    return nullptr;
}

// Told only for a link this host installed (rp.host.devices/2's identity check): a stray pointer is ignored.
void DeviceHost::releaseFn(void* ctx, void* link) {
    const auto& self = *static_cast<const DeviceHost*>(ctx);
    if (!link) return;
    for (std::size_t i = 0; i < self.count_; ++i) {
        const DeviceLink& l = self.links_[i];
        if (l.link != link) continue;
        if (l.released) l.released(l.hookCtx);
        return;
    }
}

void DeviceHost::logFn(void* ctx, int level, const char* message) {
    const auto& self = *static_cast<const DeviceHost*>(ctx);
    if (self.log_ && message) self.log_(level, message);
}

const void* DeviceHost::interfaceFn(const rp_host* host, const char* id) {
    const auto& self = *static_cast<const DeviceHost*>(host->ctx);
    if (!id) return nullptr;
    if (std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0) return &self.devices_;
    if (std::strcmp(id, RP_DEVICE_MIDI_IN_INTERFACE) == 0) return self.midi_;
    return nullptr;
}

}  // namespace rp::box
