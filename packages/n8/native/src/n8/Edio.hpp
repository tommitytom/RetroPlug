#pragma once

// krikzz's Edio, the Everdrive N8 Pro's USB protocol (docs/modular-graph-phase-e2-design.md section 4), written once
// for the desktop and the RP2350: the CLI's sessions reach it through the pack's facet (n8/desktop/N8Rpc.hpp), the
// standalone's and the plugin's streaming link and cart jobs run it on their serial thread (n8/desktop/N8Link.hpp,
// N8Host.hpp), and the Pico box runs it over TinyUSB. The N8 speaks it over a CDC serial port, NOT raw passthrough:
// every command is a 4-byte framed header ('+', '+'^0xFF, cmd, cmd^0xFF), arguments are little-endian, and MIDI or a
// save reaches the cart by writing bytes to a device address (CMD_MEM_WR). The flash, RTC and FPGA commands are left
// out. The on-device menu's commands ride on it (n8/Menu.hpp), and the operations of several commands (a file read
// or written whole, a ROM booted) are n8/Ops.hpp's.
//
// It allocates nothing, throws nothing and uses no RTTI: every call answers a Status, values come back through
// out-parameters and caller buffers, and describe() words a failure. Nor is it thread-safe: on the desktop N8Link and
// N8Host serialise every use, and the Pico has one thread. Nor is it re-entrant: the frame being written and the name
// being listed are the object's, so nothing a call runs inside it (a Progress, a listDir visitor, a transport's idle
// hook pumping USB inside a wait) may call the same Edio; a MIDI forward from inside a wait waits for the call. The
// same bytes go out in the same order as the TS and C++ clients it replaced, which edio-golden.json holds
// (packages/n8/native/test).
//
// One write per frame: a command's header, its arguments and as much of its payload as fits a 256-byte scratch in the
// object go out in one Transport::write, and a larger payload follows in further writes. That is the Pico's rule
// (Pico-PIO-USB sends one packet per endpoint per 1 ms frame, so each separate write costs a frame), and on the desktop
// it changes the number of calls and nothing else.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "n8/Transport.hpp"

namespace rp::n8 {

// What a call came to. Device carries the N8's own status byte (a FatFs error, a menu refusal).
enum class Code : std::uint8_t {
    Ok,
    Timeout,    // a reply did not arrive in its time, or the port did not take a command's bytes
    LinkDown,   // the transport says the link is down (Transport::linkUp)
    BadReply,   // a reply that is not what the protocol answers there
    Device,     // the N8 answered with a non-zero status
    NotFound,   // an operation's file is not on the card (Ops.hpp's readFile)
    Cancelled,  // the progress callback answered false (Ops.hpp)
    TooLarge,   // a string longer than its length prefix, a name longer than asked for, a file larger than its buffer
};

// Where a call failed: what describe() words it by.
enum class Where : std::uint8_t {
    None,
    Write,        // a command's bytes
    Read,         // a reply's bytes
    StatusReply,  // CMD_STATUS's reply, which is <status> A5
    Operation,    // a command's trailing status
    MkDir,        // dirMake's status (8, "exists", is not a failure)
    DirEntry,     // a directory record's own status byte
    FileBlock,    // a file read block's status byte
    WriteAck,     // the ack that releases a file write block
    Name,         // a directory record's name, longer than the 255 bytes the listing asks for
    Length,       // a string longer than the 16-bit length that prefixes it
    Buffer,       // a file larger than the buffer it is read into
    MenuTest,     // the menu's answer to '*t', which is 'k'
    Install,      // the menu's install status
    MapIndex,     // the map index the menu sends after a 0 install status (a timeout there followed an install)
};

// The step of an operation (Ops.hpp) a status comes from; None for a single call.
enum class Step : std::uint8_t { None, Test, Upload, Save, Install, Start, List, Open, Data, Close };

struct Status {
    Code          code   = Code::Ok;
    Where         where  = Where::None;
    Step          step   = Step::None;
    std::uint8_t  device = 0;  // Device: the N8's status byte
    std::uint16_t reply  = 0;  // BadReply: what it answered (the status word, the menu's test byte)

    constexpr bool ok() const { return code == Code::Ok; }
};

// A Status's code as the facet names it ("ok", "timeout", "linkDown", ...), and its step ("test", "upload", ...; ""
// for None).
const char* codeName(Code code);
const char* stepName(Step step);

// The text of a failure, as the TS client this replaced threw it (the CLI prints it, and BlipToaster and the hardware
// lab read it): into `out` (NUL-terminated, cut to `capacity`), its length answered. `path` is the call's path, which
// a few texts name (a mkdir's, an install's, a file not found). "" for Ok.
std::size_t describe(const Status& status, char* out, std::size_t capacity, std::string_view path = {});

// How long each kind of wait lasts, in ms. One table of defaults on every Edio, which a caller may change; a call that
// takes a timeout of its own (0: the table's) overrides it for that call. Each read the transport makes waits this long
// for its bytes, so a long reply that keeps arriving never times out.
struct Timeouts {
    std::uint32_t readMs      = 2000;   // a reply
    std::uint32_t handshakeMs = 300;    // connect's status reply: short, as it is how a port is found to be no N8
    std::uint32_t sdBlockMs   = 5000;   // a file block: a write block's ack, a read block's status and its data
    std::uint32_t installMs   = 12000;  // the menu's install reply, after it loads the ROM and its mapper core off SD
};

// Progress through a transfer, and the one way to cancel it: `step(ctx, done, total)` in bytes, false to cancel. An
// operation takes the answer at its stop points, which are between whole commands (before a file is opened, before
// each block of a file read, before an install); a call made inside a command, as each acknowledged block of a file
// write is, only reports, since the cart takes a command's whole payload once it has asked for it.
struct Progress {
    bool (*step)(void* ctx, std::uint32_t done, std::uint32_t total) = nullptr;
    void* ctx = nullptr;

    bool operator()(std::uint32_t done, std::uint32_t total) const { return !step || step(ctx, done, total); }
};

// A Progress over a callable `f(done, total) -> bool`, which must outlive it.
template <class F>
Progress progressOf(F& f) {
    return Progress{
        [](void* ctx, std::uint32_t done, std::uint32_t total) { return (*static_cast<F*>(ctx))(done, total); },
        const_cast<void*>(static_cast<const void*>(&f))};
}

// One SD-card directory entry, as listDir hands it to its visitor: the name is the Edio's own buffer, valid for the
// call.
struct DirEntry {
    std::string_view name;
    std::uint32_t    size  = 0;
    bool             isDir = false;
};

class Edio {
public:
    // Protocol constants (krikzz Edio).
    static constexpr std::uint8_t CMD_STATUS     = 0x10;  // connect handshake / status poll
    static constexpr std::uint8_t CMD_GET_VDC    = 0x13;  // read board voltages (4x u16: v50, v25, v12, bat)
    static constexpr std::uint8_t CMD_MEM_RD     = 0x19;  // read bytes from a device address
    static constexpr std::uint8_t CMD_SYS_INF    = 0x26;  // read the 64-byte device info (serial, versions, ...)
    static constexpr std::uint8_t CMD_MEM_WR     = 0x1A;  // write bytes to a device address
    static constexpr std::uint8_t CMD_F_DIR_LD   = 0xC5;  // load a directory into the N8 buffer (sorted)
    static constexpr std::uint8_t CMD_F_DIR_SIZE = 0xC6;  // number of records in the loaded directory
    static constexpr std::uint8_t CMD_F_DIR_GET  = 0xC8;  // pull a range of directory records
    static constexpr std::uint8_t CMD_F_FOPN     = 0xC9;  // open a file on the SD card
    static constexpr std::uint8_t CMD_F_FRD      = 0xCA;  // read bytes from the open file
    static constexpr std::uint8_t CMD_F_FWR      = 0xCC;  // write bytes to the open file
    static constexpr std::uint8_t CMD_F_FCLOSE   = 0xCE;  // close the open file
    static constexpr std::uint8_t CMD_F_DIR_MK   = 0xD2;  // make a directory on the SD card
    static constexpr std::uint8_t CMD_F_DEL      = 0xD3;  // delete a file or empty directory
    static constexpr std::uint8_t CMD_F_AVB      = 0xD5;  // free space available on the SD card (u64)
    static constexpr std::int32_t ADDR_PRG      = 0x0000000;  // PRG-ROM PSRAM (8 MB): the same chip the CPU fetches
    static constexpr std::int32_t ADDR_CHR      = 0x0800000;  // CHR-ROM PSRAM (8 MB): the same chip the PPU fetches
    static constexpr std::int32_t ADDR_SRM      = 0x1000000;  // cart battery RAM (SRAM/PRG-NVRAM): a game's .srm
    static constexpr std::int32_t ADDR_MENU_CHR = 0x0FE0000;  // menu CHR (ADDR_CHR 0x800000 + 0x7E0000): screenshot
    static constexpr std::int32_t N8_OS_REGION  = 0x07E0000;  // top of PRG/CHR (0x7E0000..0x800000): the N8 OS's
    static constexpr std::int32_t ADDR_SSR      = 0x1802000;  // save-state sniffer: a running game's live APU/PPU/OAM
    static constexpr std::int32_t ADDR_FIFO     = 0x1810000;  // cart FIFO (NES side reads $40F0/$40F1)
    // The 16-byte FPGA mapper-config block (edn8-pro-pub fpga/base_sv/sys_cfg.sv `scfg`): what the OS told the cart the
    // RUNNING game is - mapper index, PRG/CHR/SRM size masks, mirroring, CHR-RAM-vs-ROM, master volume. Read AND write
    // over USB (HW-verified). Decoded TS-side by @retroplug/n8's mapConfig.ts.
    static constexpr std::int32_t ADDR_CFG     = 0x1800020;
    static constexpr std::size_t  SIZE_CFG     = 16;
    static constexpr std::int32_t ADDR_EXP_VOL = ADDR_CFG + 3;  // expansion-audio master volume (128 = unity)
    // A CHR-RAM cart's RAM is NOT at ADDR_CHR: everdrive.sv forces CHR address bit 22 for a chr_ram game, so the PPU
    // sees the UPPER 4 MB of the CHR chip. That bit is applied only to the console's own fetches (`!dma.req_chr`), so
    // a host read/write over USB has to add this offset itself.
    static constexpr std::int32_t CHR_RAM_OFFSET = 0x400000;
    static constexpr std::size_t  SIZE_CHR_BANK  = 0x2000;   // 8 KB: one CHR bank, the PPU pattern-table window
    static constexpr std::size_t  SIZE_SRM_GAME  = 0x10000;  // 64 KB: the most battery RAM a game uses (risa: 64 KB)
    static constexpr std::size_t  SIZE_SYS_INFO  = 64;
    static constexpr std::size_t  SIZE_VDC       = 8;
    static constexpr int          ACK_BLOCK_SIZE = 1024;  // a file write's ack granularity
    // A file read's block: one CMD_F_FRD per block, as the device's ed_cmd_file_read loops (edn8-pro-pub); at most
    // 512 avoids the cart-FIFO overload the device warns about, which desyncs a multi-block read at 4096.
    static constexpr int          RD_BLOCK_SIZE  = 512;
    // File-open mode flags (FatFs).
    static constexpr std::uint8_t FA_READ          = 0x01;
    static constexpr std::uint8_t FA_WRITE         = 0x02;
    static constexpr std::uint8_t FA_CREATE_ALWAYS = 0x08;
    static constexpr std::uint8_t FS_MAKEPATH      = 0x80;  // create parent dirs if missing
    // The longest name a listing asks for, and the buffer its visitor's name is read into.
    static constexpr std::size_t kNameMax = 255;
    // The frame a command's header, arguments and first payload bytes are written from in one write.
    static constexpr std::size_t kFrameBytes = 256;

    // Not constexpr on purpose: a constant-initialised static Edio (the Pico box's) would put the whole object, its
    // 512 bytes of zeroed buffers included, in .data, copied from flash at boot, where a dynamic one is a bss zero
    // fill and one store.
    explicit Edio(Transport& transport) : transport_(transport) {}
    Edio(const Edio&)            = delete;
    Edio& operator=(const Edio&) = delete;

    Transport&      transport() { return transport_; }
    Timeouts&       timeouts() { return timeouts_; }
    const Timeouts& timeouts() const { return timeouts_; }

    // The connect handshake: flushInput, then CMD_STATUS, whose reply must be <status> A5 (0 = OK), within
    // `handshakeMs` (0: the table's). The status (which need not be 0) lands in `deviceStatus` when given.
    Status connect(std::uint8_t* deviceStatus = nullptr, std::uint32_t handshakeMs = 0);
    // CMD_STATUS on its own: the reply checked as connect checks it, its status byte into `deviceStatus`.
    Status status(std::uint8_t& deviceStatus, std::uint32_t timeoutMs = 0);

    // The raw 64-byte device-info block (serial, versions, form factor, flash; decoded TS-side by sysInfo.ts), and the
    // 8-byte board-voltage block (four u16: v50, v25, v12, bat). Either works at the menu or in a game.
    Status sysInfo(std::uint8_t (&out)[SIZE_SYS_INFO]);
    Status vdc(std::uint8_t (&out)[SIZE_VDC]);

    // SD file management. freeSpace: bytes free (CMD_F_AVB: two u32, high then low). dirMake: mkdir, an "exists"
    // (status 8) taken as done. fileDelete: remove a file or an empty directory.
    Status freeSpace(std::uint64_t& bytes);
    Status dirMake(std::string_view path);
    Status fileDelete(std::string_view path);

    // Device memory. memWR is fire-and-forget (no status: a failed write is silent, by design); memRD blocks until
    // `size` bytes have arrived. Neither sends anything for a size of 0.
    Status memWR(std::uint32_t addr, const std::uint8_t* data, std::size_t size);
    Status memRD(std::uint32_t addr, std::uint8_t* out, std::size_t size);
    // MIDI (or any bytes) to the cart FIFO: memWR to ADDR_FIFO.
    Status fifoWR(const std::uint8_t* data, std::size_t size);
    // A length-prefixed string to the cart FIFO, as two FIFO writes: a 2-byte little-endian length, then the bytes
    // (how the on-device menu receives a path, edlink DeviceIO.FifoTxString).
    Status fifoTxString(std::string_view s);

    // List an SD-card directory, sorted: loaded into the N8's buffer, then every record pulled. `visit(ctx, entry)` is
    // called once an entry, its name in this object's own buffer, so a listing has no capacity of its own; `count`,
    // when given, gets the number of records the N8 said it holds.
    Status listDir(std::string_view path, void (*visit)(void* ctx, const DirEntry& entry), void* ctx,
                   std::uint16_t* count = nullptr);
    template <class F>
    Status listDir(std::string_view path, F&& visit, std::uint16_t* count = nullptr) {
        using V = std::remove_reference_t<F>;
        return listDir(
            path, [](void* ctx, const DirEntry& e) { (*static_cast<V*>(ctx))(e); },
            const_cast<void*>(static_cast<const void*>(&visit)), count);
    }

    // The SD file API. fileOpen(path, FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH), fileWrite, fileClose to write one;
    // fileOpen(path, FA_READ), fileRead, fileClose to read one. fileWrite sends `size` bytes in ACK_BLOCK_SIZE blocks,
    // each released by the cart's 0 ack, then reads the command's status; `progress` reports each block, the command
    // having been asked for (it cannot stop one). fileRead reads `size` bytes as one CMD_F_FRD a RD_BLOCK_SIZE block,
    // each gated by a status byte, with no trailing status; `progress` is asked before each block (false cancels before
    // it: Cancelled, the file still open) and told at the end.
    Status fileOpen(std::string_view path, std::uint8_t mode);
    Status fileWrite(const std::uint8_t* data, std::size_t size, Progress progress = {});
    Status fileRead(std::uint8_t* out, std::size_t size, Progress progress = {});
    Status fileClose();

    // Blocking raw reads, with no command sent: a menu's reply (Menu.hpp), which the firmware streams after a FIFO
    // command. `timeoutMs` 0 is the table's readMs.
    Status rx8(std::uint8_t& v, std::uint32_t timeoutMs = 0);
    Status rx16(std::uint16_t& v, std::uint32_t timeoutMs = 0);
    Status rx32(std::uint32_t& v, std::uint32_t timeoutMs = 0);
    Status readData(std::uint8_t* out, std::size_t size, std::uint32_t timeoutMs = 0);

    // Drop any buffered input (reboot garbage before polling the menu, a late ack before a job's reply).
    void flushInput() { transport_.flushInput(); }

private:
    // The frame being built in scratch_: begin() starts a command, the put*s append its arguments, and send() writes it
    // with as much of `payload` as fits, then the rest of `payload` in further writes.
    void   begin(std::uint8_t cmd);
    void   put8(std::uint8_t v);
    void   put16(std::uint16_t v);
    void   put32(std::uint32_t v);
    Status send(const std::uint8_t* payload = nullptr, std::size_t size = 0);
    Status write(const std::uint8_t* data, std::size_t size);
    Status read(std::uint8_t* out, std::size_t size, std::uint32_t timeoutMs);
    // A command that names a path: begin(cmd), the put*s `args` already made, the length, then the path as payload.
    Status sendPath(std::string_view path);
    // CMD_STATUS, and the status a non-zero one is: Device, at `where`.
    Status checkStatus(Where where = Where::Operation);
    std::uint32_t timeout(std::uint32_t ms) const { return ms ? ms : timeouts_.readMs; }

    Transport&   transport_;
    Timeouts     timeouts_;
    std::uint8_t scratch_[kFrameBytes] = {};
    std::size_t  frame_                = 0;
    char         name_[kNameMax + 1]   = {};
};

}  // namespace rp::n8
