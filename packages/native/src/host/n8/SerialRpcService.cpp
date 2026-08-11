#include "host/n8/SerialRpcService.hpp"

#include <exception>

namespace retroplug {

WjwwoodSerialPort* SerialRpcService::find(std::int32_t handle) {
    const auto it = ports_.find(handle);
    return it == ports_.end() ? nullptr : it->second.get();
}

std::vector<SerialPortInfo> SerialRpcService::serialListPorts() {
    std::vector<SerialPortInfo> out;
    for (const N8PortInfo& p : listSerialPorts()) out.push_back({p.port, p.isN8});
    return out;
}

std::int32_t SerialRpcService::serialOpen(std::string port) {
    try {
        auto sp = std::make_unique<WjwwoodSerialPort>(port);
        const std::int32_t handle = nextHandle_++;
        ports_.emplace(handle, std::move(sp));
        return handle;
    } catch (const std::exception&) {
        return -1;  // absent / busy / permission - TS turns this into a typed error
    }
}

std::int32_t SerialRpcService::serialWrite(std::int32_t handle, rfl::Bytestring data) {
    WjwwoodSerialPort* p = find(handle);
    if (!p) return -1;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    return static_cast<std::int32_t>(p->write(bytes, data.size()));
}

rfl::Bytestring SerialRpcService::serialRead(std::int32_t handle, std::uint32_t size, std::int32_t timeoutMs) {
    WjwwoodSerialPort* p = find(handle);
    if (!p || size == 0) return {};
    std::vector<std::uint8_t> buf(size);
    const std::size_t n = p->read(buf.data(), size, timeoutMs);
    const auto* b = reinterpret_cast<const std::byte*>(buf.data());
    return rfl::Bytestring(b, b + n);
}

bool SerialRpcService::serialFlush(std::int32_t handle) {
    WjwwoodSerialPort* p = find(handle);
    if (!p) return false;
    p->flushInput();
    return true;
}

bool SerialRpcService::serialClose(std::int32_t handle) {
    return ports_.erase(handle) > 0;
}

}  // namespace retroplug
