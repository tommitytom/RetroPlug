#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "host/n8/Edio.hpp"

namespace serial { class Serial; }

namespace retroplug {

// An ISerialPort backed by wjwwood/serial (deps/serial). Opens the given port at 9600 / 8N1 (baud is
// irrelevant on the N8's FT245-class USB device). The read timeout is applied per call (Edio only reads
// during the handshake). The ctor throws serial::IOException if the port can't be opened.
class WjwwoodSerialPort : public ISerialPort {
public:
    explicit WjwwoodSerialPort(const std::string& portName);
    ~WjwwoodSerialPort() override;

    bool               isOpen() const;
    const std::string& portName() const { return portName_; }

    std::size_t write(const std::uint8_t* data, std::size_t size) override;
    std::size_t read(std::uint8_t* buffer, std::size_t size, int timeoutMs) override;
    void        flushInput() override;

private:
    std::string                     portName_;
    std::unique_ptr<serial::Serial> port_;
};

// A discovered serial port + whether it looks like an Everdrive N8 Pro (USB VID:PID 38df:0017).
struct N8PortInfo {
    std::string port;         // OS port name (/dev/ttyACM0, COM3, ...)
    std::string description;
    std::string hardwareId;   // e.g. "USB VID:PID=38df:0017 ..."
    bool        isN8 = false;
};

// Enumerate all serial ports, flagging N8 units. Never throws (returns {} if enumeration fails).
std::vector<N8PortInfo> listSerialPorts();

// The OS port name of the first attached N8 Pro, or "" if none. Substring-matches VID:PID 38df:0017 on the
// hardware id (robust across units, unlike the reference's exact-serial-number match).
std::string findN8Port();

}  // namespace retroplug
