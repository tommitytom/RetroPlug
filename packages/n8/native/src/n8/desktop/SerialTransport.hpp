#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "n8/desktop/PortTransport.hpp"

namespace serial { class Serial; }

namespace retroplug {

// The desktop's transport, over wjwwood/serial (deps/serial). Opens the given port at 9600 / 8N1 (baud is irrelevant on
// the N8's FT245-class USB device). The constructor throws if the port can't be opened (serial::IOException, or
// PortBusyError when another process holds it), before any Edio call (its owner catches it). On POSIX the open port is
// exclusive (TIOCEXCL): a second open of it anywhere, by another instance, a CLI session or minicom, fails busy until
// this one closes (root is exempt, as the kernel has it); a Windows COM port is exclusive already. The read timeout is
// applied per call. Every call catches what deps/serial throws and answers short (a write that took nothing, a read
// with nothing), keeping the exception's text as failure().
class SerialTransport final : public PortTransport {
public:
    explicit SerialTransport(const std::string& portName);
    ~SerialTransport() override;

    // Every write gets this long, whatever the last read asked for (see read()).
    static constexpr std::uint32_t kWriteTimeoutMs = 2000;

    bool               isOpen() const;
    const std::string& portName() const { return portName_; }

    std::size_t write(const std::uint8_t* data, std::size_t size) override;
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) override;
    void        flushInput() override;
    std::string failure() const override { return failure_; }

private:
    std::string                     portName_;
    std::string                     failure_;
    int                             exclusiveFd_ = -1;  // POSIX: the descriptor holding TIOCEXCL, -1 when none
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
