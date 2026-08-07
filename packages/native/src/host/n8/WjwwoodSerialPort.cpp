#include "host/n8/WjwwoodSerialPort.hpp"

#include <serial/serial.h>

#include <algorithm>
#include <cctype>

namespace retroplug {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The N8 Pro presents krikzz's own USB id 38df:0017 - Linux/macOS as "USB VID:PID=38df:0017 ...",
// Windows as "USB\VID_38DF&PID_0017&REV_0200". Match either shape, case-insensitively (a substring
// match, so it works across physical units - the reference did a brittle exact match that baked in one
// unit's serial number).
bool hardwareIdIsN8(const std::string& hwid) {
    const std::string h = toLower(hwid);
    return h.find("38df:0017") != std::string::npos          // Linux / macOS shape
        || h.find("vid_38df&pid_0017") != std::string::npos;  // Windows shape
}

}  // namespace

WjwwoodSerialPort::WjwwoodSerialPort(const std::string& portName) : portName_(portName) {
    // 9600 / 8N1, no flow control (all serial::Serial defaults except baud). The initial 2000 ms timeout
    // is a safety net; Edio overrides it per read via read()/setReadTimeout.
    port_ = std::make_unique<serial::Serial>(portName, 9600, serial::Timeout::simpleTimeout(2000));
}

WjwwoodSerialPort::~WjwwoodSerialPort() = default;

bool WjwwoodSerialPort::isOpen() const { return port_ && port_->isOpen(); }

std::size_t WjwwoodSerialPort::write(const std::uint8_t* data, std::size_t size) {
    return port_->write(data, size);
}

std::size_t WjwwoodSerialPort::read(std::uint8_t* buffer, std::size_t size, int timeoutMs) {
    serial::Timeout t = serial::Timeout::simpleTimeout(static_cast<std::uint32_t>(timeoutMs));
    port_->setTimeout(t);
    return port_->read(buffer, size);
}

void WjwwoodSerialPort::flushInput() { port_->flushInput(); }

std::vector<N8PortInfo> listSerialPorts() {
    std::vector<N8PortInfo> out;
    try {
        for (const serial::PortInfo& p : serial::list_ports()) {
            N8PortInfo info;
            info.port        = p.port;
            info.description = p.description;
            info.hardwareId  = p.hardware_id;
            info.isN8        = hardwareIdIsN8(p.hardware_id);
            out.push_back(std::move(info));
        }
    } catch (...) {
        // Enumeration failed (no serial subsystem) - return whatever we gathered.
    }
    return out;
}

std::string findN8Port() {
    for (const N8PortInfo& p : listSerialPorts())
        if (p.isN8) return p.port;
    return "";
}

}  // namespace retroplug
