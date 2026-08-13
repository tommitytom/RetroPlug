#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace retroplug {

// One SD-card directory entry (from listDir).
struct N8DirEntry {
    std::string   name;
    std::uint32_t size = 0;
    bool          isDir = false;
};

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
    static constexpr std::uint8_t CMD_GET_VDC   = 0x13;   // read board voltages (4x u16: v50, v25, v12, bat)
    static constexpr std::uint8_t CMD_MEM_RD    = 0x19;   // read bytes from a device address
    static constexpr std::uint8_t CMD_SYS_INF   = 0x26;   // read the 64-byte device info (serial, versions, form factor)
    static constexpr std::uint8_t CMD_MEM_WR    = 0x1A;   // write bytes to a device address
    static constexpr std::uint8_t CMD_F_DIR_LD  = 0xC5;   // load a directory into the N8 buffer (sorted)
    static constexpr std::uint8_t CMD_F_DIR_SIZE = 0xC6;  // number of records in the loaded directory
    static constexpr std::uint8_t CMD_F_DIR_GET = 0xC8;   // pull a range of directory records
    static constexpr std::uint8_t CMD_F_FOPN    = 0xC9;   // open a file on the SD card
    static constexpr std::uint8_t CMD_F_FRD     = 0xCA;   // read bytes from the open file
    static constexpr std::uint8_t CMD_F_FWR     = 0xCC;   // write bytes to the open file
    static constexpr std::uint8_t CMD_F_FCLOSE  = 0xCE;   // close the open file
    static constexpr std::uint8_t CMD_F_DIR_MK  = 0xD2;   // make a directory on the SD card
    static constexpr std::uint8_t CMD_F_DEL     = 0xD3;   // delete a file or empty directory
    static constexpr std::uint8_t CMD_F_AVB     = 0xD5;   // free space available on the SD card (u64)
    static constexpr std::int32_t ADDR_PRG      = 0x0000000; // PRG-ROM PSRAM (8 MB) - the same chip the CPU fetches
    static constexpr std::int32_t ADDR_CHR      = 0x0800000; // CHR-ROM PSRAM (8 MB) - the same chip the PPU fetches
    static constexpr std::int32_t ADDR_SRM      = 0x1000000; // cart battery RAM (SRAM/PRG-NVRAM); a game's .srm
    static constexpr std::int32_t ADDR_MENU_CHR = 0x0FE0000; // menu CHR (ADDR_CHR 0x800000 + 0x7E0000); screenshot
    static constexpr std::int32_t ADDR_SSR      = 0x1802000; // save-state sniffer: a running game's live APU/PPU/OAM mirror
    static constexpr std::int32_t ADDR_FIFO     = 0x1810000; // cart FIFO (NES side reads $40F0/$40F1)
    static constexpr std::size_t  SIZE_SRM_GAME = 0x10000;   // 64 KB — max battery RAM a game uses (risa: 64 KB)
    static constexpr int          ACK_BLOCK_SIZE = 1024;  // fileWrite ack granularity
    static constexpr int          RD_BLOCK_SIZE  = 512;   // fileRead block size: one CMD_F_FRD/block; <=512 avoids FIFO overload
    // File-open mode flags (FatFs).
    static constexpr std::uint8_t FA_READ         = 0x01;
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

    // Read the raw 64-byte device-info block (serial, versions, form factor, flash); decode with decodeSysInfo
    // (TS src/n8/sysInfo.ts). Works whether the menu or a game is running.
    std::vector<std::uint8_t> sysInfo();

    // Read the raw 8-byte board-voltage block (four u16: v50, v25, v12, bat).
    std::vector<std::uint8_t> vdc();

    // SD file management. freeSpace: bytes free (CMD_F_AVB). dirMake: mkdir (tolerates "exists"). fileDelete:
    // remove a file or empty dir. dirMake/fileDelete throw std::runtime_error on a device error.
    std::uint64_t freeSpace();
    void          dirMake(const std::string& path);
    void          fileDelete(const std::string& path);

    // Write raw MIDI bytes to the cart FIFO (fire-and-forget: no status read). The one op the bridge needs.
    void fifoWR(const std::uint8_t* data, std::size_t size);
    void fifoWR(const std::vector<std::uint8_t>& bytes) { fifoWR(bytes.data(), bytes.size()); }

    // Write `size` bytes to a device address via CMD_MEM_WR (fire-and-forget).
    void memWR(std::int32_t addr, const std::uint8_t* data, std::size_t size);

    // Read `size` bytes from a device address via CMD_MEM_RD (blocks; throws on timeout). Used to write a
    // game's battery save into ADDR_SRM and read it back to verify.
    void memRD(std::int32_t addr, std::uint8_t* data, std::size_t size);

    // Write a length-prefixed string to the cart FIFO: a 2-byte little-endian length, then the bytes.
    // How the on-device menu receives a path argument (edlink DeviceIO.FifoTxString).
    void fifoTxString(const std::string& s);

    // List an SD-card directory (sorted). Loads the dir into the N8 buffer then pulls every record. Throws
    // std::runtime_error on a bad path / device status. For discovering exact SD paths + reading saves.
    std::vector<N8DirEntry> listDir(const std::string& path);

    // SD-card file API (subset, for uploading a ROM). fileOpen(path, FA_WRITE|FA_CREATE_ALWAYS|FS_MAKEPATH)
    // -> fileWrite(bytes) -> fileClose(). Each throws std::runtime_error on a non-zero device status.
    void fileOpen(const std::string& path, std::uint8_t mode);
    void fileWrite(const std::uint8_t* data, std::size_t size);
    void fileWrite(const std::vector<std::uint8_t>& bytes) { fileWrite(bytes.data(), bytes.size()); }
    void fileClose();

    // Read `size` bytes from the open file via CMD_F_FRD (resp-gated blocks; the inverse of fileWrite - no
    // trailing status). Pairs with fileOpen(path, FA_READ). Throws on a non-zero per-block device status.
    void fileRead(std::uint8_t* data, std::size_t size);
    // Read a whole SD file by path: find its size via listDir, then fileOpen(FA_READ) -> fileRead -> fileClose.
    std::vector<std::uint8_t> readFile(const std::string& path);

    // Blocking reads from the serial port - the N8 menu's replies come back this way (its TX FIFO drains to
    // USB). Throw std::runtime_error on timeout. Used by the menu command layer (N8Menu).
    std::uint8_t  rx8();
    std::uint16_t rx16();
    std::uint32_t rx32();

    // Read `size` raw bytes off the serial port (no command frame) - a menu reply the firmware streams after a
    // FIFO command, e.g. N8Menu::vramDump's 2048+16 bytes. (memRD/fileRead can't serve it: they send their own
    // command first.) Blocks; throws on timeout.
    void readData(std::uint8_t* data, std::size_t size);

    // Read timeout (ms) threaded into ISerialPort::read (handshake + menu replies). fifoWR never reads.
    void setReadTimeout(int ms) { timeoutMs_ = ms; }

    // Drop any buffered input (e.g. reboot garbage before polling the menu). Best-effort.
    void flushInput();

private:
    void txCMD(std::uint8_t cmd);
    void tx8(std::uint8_t v);
    void tx16(std::uint32_t v);
    void tx32(std::uint32_t v);
    void txData(const std::uint8_t* data, std::size_t size);    // chunked at 8192
    void txString(const std::string& s);                        // tx16(len) + bytes
    void txDataACK(const std::uint8_t* data, std::size_t size); // ack byte (0=ok) per ACK_BLOCK_SIZE block
    void rxData(std::uint8_t* data, std::size_t size);          // blocks; throws on timeout
    std::string rxString();                                     // rx16(len) + bytes
    void checkStatus();                                         // poll CMD_STATUS; throw if low byte != 0

    ISerialPort& port_;
    int          timeoutMs_ = 2000; // per-call read timeout, threaded into ISerialPort::read
};

}  // namespace retroplug
