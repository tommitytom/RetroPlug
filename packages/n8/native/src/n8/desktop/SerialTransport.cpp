#include "n8/desktop/SerialTransport.hpp"

#include <serial/serial.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <exception>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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

std::string busyWords(const std::string& portName) {
    return "the serial port " + portName + " is busy: another program or RetroPlug instance has it open";
}

#if !defined(_WIN32)
// Makes the port this process has open exclusive: every later open of it, by another RetroPlug instance, a CLI session
// or minicom, fails with EBUSY (except for root, which the kernel exempts). deps/serial opens a POSIX port without
// TIOCEXCL or a lock and does not hand out its descriptor, so a second descriptor on the same path sets it. The flag
// belongs to the device, not the descriptor, and a descriptor cannot be opened once it is set, so this one is kept
// to clear the flag again (releaseExclusive) when the transport closes; the kernel clears it at the last close of a
// real port, and a pty keeps it for as long as its master lives. The descriptor is close-on-exec, so a child process
// does not keep the port exclusive after this one lets go. Answers -1, the port left shared as it was, for a device
// that does not take the ioctl. If another instance set the flag between deps/serial's open and this one, the open
// answers EBUSY and the constructor throws PortBusyError. Two opens that both land before either flag are not told
// apart, and the window cannot be closed: the flag cannot be set before deps/serial's own open without refusing it.
int holdExclusive(const std::string& portName) {
    const int fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == EBUSY) throw PortBusyError(busyWords(portName));
        return -1;
    }
    if (::ioctl(fd, TIOCEXCL) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void releaseExclusive(int fd) {
    if (fd < 0) return;
    (void)::ioctl(fd, TIOCNXCL);
    ::close(fd);
}
#else
// A COM port another process holds is refused by the open itself; deps/serial reports it as an unknown error 5
// (ERROR_ACCESS_DENIED), not as a busy port, so Windows has no PortBusyError.
int holdExclusive(const std::string&) { return -1; }

void releaseExclusive(int) {}
#endif

}  // namespace

SerialTransport::SerialTransport(const std::string& portName) : portName_(portName) {
    // 9600 / 8N1, no flow control (all serial::Serial defaults except baud). The initial 2000 ms timeout is a safety
    // net; every read sets its own (read()).
    try {
        port_ = std::make_unique<serial::Serial>(portName, 9600, serial::Timeout::simpleTimeout(2000));
    } catch ([[maybe_unused]] const serial::IOException& e) {
#if !defined(_WIN32)
        if (e.getErrorNumber() == EBUSY) throw PortBusyError(busyWords(portName));
#endif
        throw;
    }
    try {
        exclusiveFd_ = holdExclusive(portName);
    } catch (...) {
        port_.reset();  // the port is not ours to hold: close what was opened
        throw;
    }
}

SerialTransport::~SerialTransport() {
    // The exclusive flag first, then the port closes with port_ (a member, so after this body).
    releaseExclusive(exclusiveFd_);
}

bool SerialTransport::isOpen() const { return port_ && port_->isOpen(); }

std::size_t SerialTransport::write(const std::uint8_t* data, std::size_t size) {
    failure_.clear();
    try {
        return port_->write(data, size);
    } catch (const std::exception& e) {
        failure_ = e.what();
        return 0;
    }
}

std::size_t SerialTransport::read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) {
    failure_.clear();
    try {
        // The READ timeout only. serial::Timeout::simpleTimeout(t) sets the write timeout to t as well, and the port
        // has one Timeout for both - so every write inherited the last read's: after a short poll of the back-channel
        // (the reliable link reads with 0 ms) a ~20-byte CMD_MEM_WR timed out part-written, the client then ignored a
        // short write, and the N8's command parser was left mid-command, swallowing everything after it - frames that
        // never landed, then a device that answered nothing (2026-10-22, measured: a 0-1 ms read before a fifoWR, every
        // time).
        serial::Timeout t(serial::Timeout::max(), timeoutMs, 0, kWriteTimeoutMs, 0);
        port_->setTimeout(t);
        return port_->read(buffer, size);
    } catch (const std::exception& e) {
        failure_ = e.what();
        return 0;
    }
}

void SerialTransport::flushInput() {
    failure_.clear();
    try {
        port_->flushInput();
    } catch (const std::exception& e) {
        failure_ = e.what();
    }
}

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
