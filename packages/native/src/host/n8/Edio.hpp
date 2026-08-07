#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace retroplug {

// The only serial surface Edio touches - one virtual per used serial::Serial call. The concrete impl
// (WjwwoodSerialPort) wraps deps/serial; the framing unit test injects a capturing fake. read() returns
// the number of bytes actually read (may be < size on a timeout); timeoutMs is per-call, matching
// deps/serial (whose read() returns short when its total-timeout expires).
struct ISerialPort {
    virtual ~ISerialPort() = default;
    virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;
    virtual std::size_t read(std::uint8_t* buffer, std::size_t size, int timeoutMs) = 0;
    virtual void        flushInput() = 0;
};

// krikzz Everdrive N8 Pro USB client - the FIFO subset needed to stream MIDI to the cart. The N8 speaks
// krikzz's "Edio" command protocol over a CDC serial port (NOT raw passthrough): every command is a 4-byte
// framed header ('+', '+'^0xFF, cmd, cmd^0xFF), args are little-endian, and MIDI is delivered by writing
// raw bytes to the cart FIFO address via CMD_MEM_WR. Ported from the proven ecs-linux client. Exposes the
// FIFO subset (handshake + fifoWR) plus the small read/string helpers the on-device menu command layer
// (N8Menu) rides on; the flash/RTC/FPGA commands are intentionally omitted.
class Edio {
public:
    // Protocol constants (krikzz Edio). Public so the framing test can assert against them.
    static constexpr std::uint8_t CMD_STATUS    = 0x10;   // connect handshake / status poll
    static constexpr std::uint8_t CMD_MEM_WR    = 0x1A;   // write bytes to a device address
    static constexpr std::uint8_t CMD_F_FOPN    = 0xC9;   // open a file on the SD card
    static constexpr std::uint8_t CMD_F_FWR     = 0xCC;   // write bytes to the open file
    static constexpr std::uint8_t CMD_F_FCLOSE  = 0xCE;   // close the open file
    static constexpr std::int32_t ADDR_FIFO     = 0x1810000; // cart FIFO (NES side reads $40F0/$40F1)
    static constexpr int          ACK_BLOCK_SIZE = 1024;  // fileWrite ack granularity
    // File-open mode flags (FatFs).
    static constexpr std::uint8_t FA_WRITE        = 0x02;
    static constexpr std::uint8_t FA_CREATE_ALWAYS = 0x08;
    static constexpr std::uint8_t FS_MAKEPATH     = 0x80; // create parent dirs if missing

    explicit Edio(ISerialPort& port) : port_(port) {}

    // The connect handshake: flushInput + CMD_STATUS -> rx16, requiring a 0xA5xx reply (low byte = status,
    // 0 = OK). Uses a short read timeout while probing, then raises it. Returns the status code; throws
    // std::runtime_error on a bad or absent reply (no device / wrong port).
    int connect(int handshakeTimeoutMs = 300);

    // The connect status read on its own (CMD_STATUS -> rx16 -> 0xA5 check). Throws on a bad reply.
    int getStatus();

    // Write raw MIDI bytes to the cart FIFO (fire-and-forget: no status read). The one op the bridge needs.
    void fifoWR(const std::uint8_t* data, std::size_t size);
    void fifoWR(const std::vector<std::uint8_t>& bytes) { fifoWR(bytes.data(), bytes.size()); }

    // Write `size` bytes to a device address via CMD_MEM_WR (fire-and-forget).
    void memWR(std::int32_t addr, const std::uint8_t* data, std::size_t size);

    // Write a length-prefixed string to the cart FIFO: a 2-byte little-endian length, then the bytes.
    // How the on-device menu receives a path argument (edlink DeviceIO.FifoTxString).
    void fifoTxString(const std::string& s);

    // SD-card file API (subset, for uploading a ROM). fileOpen(path, FA_WRITE|FA_CREATE_ALWAYS|FS_MAKEPATH)
    // -> fileWrite(bytes) -> fileClose(). Each throws std::runtime_error on a non-zero device status.
    void fileOpen(const std::string& path, std::uint8_t mode);
    void fileWrite(const std::uint8_t* data, std::size_t size);
    void fileWrite(const std::vector<std::uint8_t>& bytes) { fileWrite(bytes.data(), bytes.size()); }
    void fileClose();

    // Blocking reads from the serial port - the N8 menu's replies come back this way (its TX FIFO drains to
    // USB). Throw std::runtime_error on timeout. Used by the menu command layer (N8Menu).
    std::uint8_t  rx8();
    std::uint16_t rx16();

    // Read timeout (ms) threaded into ISerialPort::read (handshake + menu replies). fifoWR never reads.
    void setReadTimeout(int ms) { timeoutMs_ = ms; }

private:
    void txCMD(std::uint8_t cmd);
    void tx8(std::uint8_t v);
    void tx16(std::uint32_t v);
    void tx32(std::uint32_t v);
    void txData(const std::uint8_t* data, std::size_t size);    // chunked at 8192
    void txString(const std::string& s);                        // tx16(len) + bytes
    void txDataACK(const std::uint8_t* data, std::size_t size); // ack byte (0=ok) per ACK_BLOCK_SIZE block
    void rxData(std::uint8_t* data, std::size_t size);          // blocks; throws on timeout
    void checkStatus();                                         // poll CMD_STATUS; throw if low byte != 0

    ISerialPort& port_;
    int          timeoutMs_ = 2000; // per-call read timeout, threaded into ISerialPort::read
};

}  // namespace retroplug
