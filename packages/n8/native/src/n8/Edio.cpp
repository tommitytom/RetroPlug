#include "n8/Edio.hpp"

#include <algorithm>
#include <cstring>

namespace rp::n8 {

namespace {

// A larger payload than the frame holds goes out in writes of this many bytes, as the clients this replaced chunked it.
constexpr std::size_t kTxBlock = 8192;

constexpr Status fail(Code code, Where where) {
    Status s;
    s.code  = code;
    s.where = where;
    return s;
}

// A value as `digits` lowercase hex digits, as the TS client printed them (toString(16), padStart).
struct Hex {
    unsigned value;
    int      digits;
};

// describe()'s text, built without a formatting library (newlib's printf family can reach the heap): appended into a
// caller's buffer, cut at its capacity and always terminated.
class Text {
public:
    Text(char* out, std::size_t capacity) : out_(out), capacity_(capacity) {
        if (capacity_) out_[0] = '\0';
    }
    Text& operator<<(std::string_view s) {
        for (const char c : s) push(c);
        return *this;
    }
    Text& operator<<(Hex h) {
        static constexpr char kDigits[] = "0123456789abcdef";
        for (int i = h.digits - 1; i >= 0; --i) push(kDigits[(h.value >> (4 * i)) & 0xF]);
        return *this;
    }
    std::size_t size() const { return size_; }

private:
    void push(char c) {
        if (size_ + 1 >= capacity_) return;
        out_[size_++] = c;
        out_[size_]   = '\0';
    }

    char*       out_;
    std::size_t capacity_;
    std::size_t size_ = 0;
};

// How describe() names a step a cancel came before.
std::string_view stepText(Step step) {
    switch (step) {
        case Step::Test: return "the menu test";
        case Step::Upload: return "the upload";
        case Step::Save: return "the save's upload";
        case Step::Install: return "the install";
        case Step::Start: return "the start";
        case Step::List: return "the listing";
        case Step::Open: return "the open";
        case Step::Data: return "the data";
        case Step::Close: return "the close";
        case Step::None: break;
    }
    return {};
}

}  // namespace

const char* codeName(Code code) {
    switch (code) {
        case Code::Ok: return "ok";
        case Code::Timeout: return "timeout";
        case Code::LinkDown: return "linkDown";
        case Code::BadReply: return "badReply";
        case Code::Device: return "device";
        case Code::NotFound: return "notFound";
        case Code::Cancelled: return "cancelled";
        case Code::TooLarge: return "tooLarge";
    }
    return "unknown";
}

const char* stepName(Step step) {
    switch (step) {
        case Step::None: return "";
        case Step::Test: return "test";
        case Step::Upload: return "upload";
        case Step::Save: return "save";
        case Step::Install: return "install";
        case Step::Start: return "start";
        case Step::List: return "list";
        case Step::Open: return "open";
        case Step::Data: return "data";
        case Step::Close: return "close";
    }
    return "";
}

std::size_t describe(const Status& s, char* out, std::size_t capacity, std::string_view path) {
    Text t(out, capacity);
    switch (s.code) {
        case Code::Ok: break;
        case Code::Timeout:
            if (s.where == Where::Write) t << "Edio: serial write timeout (the N8 did not take the command)";
            else t << "Edio: serial read timeout (no N8 response)";
            break;
        case Code::LinkDown: t << "Edio: the N8's USB link is down"; break;
        case Code::BadReply:
            if (s.where == Where::MenuTest)
                t << "N8 menu: unexpected test response 0x" << Hex{s.reply, 2} << " (is the menu running?)";
            else t << "Edio: unexpected status response (" << Hex{s.reply, 4} << ")";
            break;
        case Code::Device:
            switch (s.where) {
                case Where::MkDir: t << "Edio: mkdir error 0x" << Hex{s.device, 2} << " (" << path << ")"; break;
                case Where::DirEntry: t << "Edio: dir read error 0x" << Hex{s.device, 2}; break;
                case Where::FileBlock: t << "Edio: file read error 0x" << Hex{s.device, 2}; break;
                case Where::WriteAck: t << "Edio: tx ack error 0x" << Hex{s.device, 2}; break;
                case Where::Install:
                    // ERR_OUT_OF_MEMORY: the menu heap is dirty, usually after a prior failed load.
                    if (s.device == 0x44)
                        t << "N8 menu out of memory (0x44) - the menu heap is dirty (e.g. after a prior failed load). "
                             "Power-cycle the console to a fresh menu and retry.";
                    else t << "N8 menu: app install error 0x" << Hex{s.device, 2} << " (path '" << path << "')";
                    break;
                default: t << "Edio: operation error 0x" << Hex{s.device, 2}; break;
            }
            break;
        case Code::NotFound: t << "Edio: file not found on SD: " << path; break;
        case Code::Cancelled:
            t << "Edio: cancelled";
            if (!stepText(s.step).empty()) t << " before " << stepText(s.step);
            break;
        case Code::TooLarge:
            switch (s.where) {
                case Where::Name: t << "Edio: a directory entry's name is longer than the 255 bytes asked for"; break;
                case Where::Buffer: t << "Edio: the file is larger than the buffer it is read into (" << path << ")";
                    break;
                case Where::BatteryPath:
                    t << "Edio: the ROM's file name is too long for its battery slot (" << path << ")";
                    break;
                default: t << "Edio: a string longer than 65535 bytes (" << path << ")"; break;
            }
            break;
    }
    return t.size();
}

// --- the frame ---------------------------------------------------------------------------------------------------

void Edio::begin(std::uint8_t cmd) {
    scratch_[0] = static_cast<std::uint8_t>('+');
    scratch_[1] = static_cast<std::uint8_t>('+' ^ 0xFF);
    scratch_[2] = cmd;
    scratch_[3] = static_cast<std::uint8_t>(cmd ^ 0xFF);
    frame_      = 4;
}

// Every command's arguments fit the frame (the longest, CMD_F_DIR_GET's, is 10 bytes), so these never overflow it.
void Edio::put8(std::uint8_t v) { scratch_[frame_++] = v; }

void Edio::put16(std::uint16_t v) {
    put8(static_cast<std::uint8_t>(v));
    put8(static_cast<std::uint8_t>(v >> 8));
}

void Edio::put32(std::uint32_t v) {
    put16(static_cast<std::uint16_t>(v));
    put16(static_cast<std::uint16_t>(v >> 16));
}

Status Edio::write(const std::uint8_t* data, std::size_t size) {
    if (!transport_.linkUp()) return fail(Code::LinkDown, Where::Write);
    if (transport_.write(data, size) == size) return {};
    return fail(transport_.linkUp() ? Code::Timeout : Code::LinkDown, Where::Write);
}

Status Edio::send(const std::uint8_t* payload, std::size_t size) {
    const std::size_t first = std::min(size, kFrameBytes - frame_);
    if (first) std::memcpy(scratch_ + frame_, payload, first);
    const std::size_t frame = frame_ + first;
    frame_                  = 0;
    if (Status s = write(scratch_, frame); !s.ok()) return s;
    for (std::size_t off = first; off < size; off += kTxBlock)
        if (Status s = write(payload + off, std::min(kTxBlock, size - off)); !s.ok()) return s;
    return {};
}

Status Edio::sendPath(std::string_view path) {
    if (path.size() > kStringMax) {
        frame_ = 0;
        return fail(Code::TooLarge, Where::Length);
    }
    put16(static_cast<std::uint16_t>(path.size()));
    return send(reinterpret_cast<const std::uint8_t*>(path.data()), path.size());
}

Status Edio::read(std::uint8_t* out, std::size_t size, std::uint32_t timeoutMs) {
    std::size_t got = 0;
    while (got < size) {
        if (!transport_.linkUp()) return fail(Code::LinkDown, Where::Read);
        const std::size_t n = transport_.read(out + got, size - got, timeoutMs);
        if (n == 0) return fail(transport_.linkUp() ? Code::Timeout : Code::LinkDown, Where::Read);
        got += n;
    }
    return {};
}

Status Edio::checkStatus(Where where) {
    std::uint8_t code = 0;
    if (Status s = status(code); !s.ok()) return s;
    if (code == 0) return {};
    Status s = fail(Code::Device, where);
    s.device = code;
    return s;
}

// --- raw reads ---------------------------------------------------------------------------------------------------

Status Edio::rx8(std::uint8_t& v, std::uint32_t timeoutMs) { return read(&v, 1, timeout(timeoutMs)); }

Status Edio::rx16(std::uint16_t& v, std::uint32_t timeoutMs) {
    std::uint8_t b[2];
    if (Status s = read(b, 2, timeout(timeoutMs)); !s.ok()) return s;
    v = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    return {};
}

Status Edio::rx32(std::uint32_t& v, std::uint32_t timeoutMs) {
    std::uint8_t b[4];
    if (Status s = read(b, 4, timeout(timeoutMs)); !s.ok()) return s;
    v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    return {};
}

Status Edio::readData(std::uint8_t* out, std::size_t size, std::uint32_t timeoutMs) {
    return read(out, size, timeout(timeoutMs));
}

// --- status and device info ----------------------------------------------------------------------------------------

Status Edio::status(std::uint8_t& deviceStatus, std::uint32_t timeoutMs) {
    begin(CMD_STATUS);
    if (Status s = send(); !s.ok()) return s;
    std::uint16_t word = 0;
    if (Status s = rx16(word, timeoutMs); !s.ok()) return s;
    if ((word & 0xFF00) != 0xA500) {
        Status s = fail(Code::BadReply, Where::StatusReply);
        s.reply  = word;
        return s;
    }
    deviceStatus = static_cast<std::uint8_t>(word & 0xFF);
    return {};
}

Status Edio::connect(std::uint8_t* deviceStatus, std::uint32_t handshakeMs) {
    transport_.flushInput();
    std::uint8_t code = 0;
    if (Status s = status(code, handshakeMs ? handshakeMs : timeouts_.handshakeMs); !s.ok()) return s;
    if (deviceStatus) *deviceStatus = code;
    return {};
}

Status Edio::sysInfo(std::uint8_t (&out)[SIZE_SYS_INFO]) {
    begin(CMD_SYS_INF);
    if (Status s = send(); !s.ok()) return s;
    return readData(out, sizeof out);
}

Status Edio::vdc(std::uint8_t (&out)[SIZE_VDC]) {
    begin(CMD_GET_VDC);
    if (Status s = send(); !s.ok()) return s;
    return readData(out, sizeof out);
}

Status Edio::freeSpace(std::uint64_t& bytes) {
    begin(CMD_F_AVB);
    if (Status s = send(); !s.ok()) return s;
    std::uint8_t b[8];
    if (Status s = readData(b, sizeof b); !s.ok()) return s;
    const auto u32 = [&](int at) {
        return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
               (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    };
    bytes = (static_cast<std::uint64_t>(u32(0)) << 32) | u32(4);
    return {};
}

Status Edio::dirMake(std::string_view path) {
    begin(CMD_F_DIR_MK);
    if (Status s = sendPath(path); !s.ok()) return s;
    std::uint8_t code = 0;
    if (Status s = status(code); !s.ok()) return s;
    if (code == 0 || code == 8) return {};  // 8: FR_EXIST, the directory is already there
    Status s = fail(Code::Device, Where::MkDir);
    s.device = code;
    return s;
}

Status Edio::fileDelete(std::string_view path) {
    begin(CMD_F_DEL);
    if (Status s = sendPath(path); !s.ok()) return s;
    return checkStatus();
}

// --- device memory -----------------------------------------------------------------------------------------------

Status Edio::memWR(std::uint32_t addr, const std::uint8_t* data, std::size_t size) {
    if (size == 0) return {};
    begin(CMD_MEM_WR);
    put32(addr);
    put32(static_cast<std::uint32_t>(size));
    put8(0);  // exec flag
    return send(data, size);  // fire-and-forget: no status read
}

Status Edio::memRD(std::uint32_t addr, std::uint8_t* out, std::size_t size) {
    if (size == 0) return {};
    begin(CMD_MEM_RD);
    put32(addr);
    put32(static_cast<std::uint32_t>(size));
    put8(0);  // exec flag
    if (Status s = send(); !s.ok()) return s;
    return readData(out, size);
}

Status Edio::fifoWR(const std::uint8_t* data, std::size_t size) {
    return memWR(static_cast<std::uint32_t>(ADDR_FIFO), data, size);
}

Status Edio::fifoTxString(std::string_view str) {
    if (str.size() > kStringMax) return fail(Code::TooLarge, Where::Length);
    const std::uint8_t len[2] = {static_cast<std::uint8_t>(str.size()), static_cast<std::uint8_t>(str.size() >> 8)};
    if (Status s = fifoWR(len, 2); !s.ok()) return s;
    return fifoWR(reinterpret_cast<const std::uint8_t*>(str.data()), str.size());
}

// --- the SD card -------------------------------------------------------------------------------------------------

Status Edio::listDir(std::string_view path, void (*visit)(void* ctx, const DirEntry& entry), void* ctx,
                     std::uint16_t* count) {
    if (count) *count = 0;
    // Load the directory into the N8's buffer (sorted), then pull every record in one range read.
    begin(CMD_F_DIR_LD);
    put8(1);  // sorted
    if (Status s = sendPath(path); !s.ok()) return s;
    if (Status s = checkStatus(); !s.ok()) return s;

    begin(CMD_F_DIR_SIZE);
    if (Status s = send(); !s.ok()) return s;
    std::uint16_t records = 0;
    if (Status s = rx16(records); !s.ok()) return s;
    if (count) *count = records;
    if (records == 0) return {};

    begin(CMD_F_DIR_GET);
    put16(0);  // start index
    put16(records);
    put16(static_cast<std::uint16_t>(kNameMax));  // max name length
    if (Status s = send(); !s.ok()) return s;
    for (std::uint16_t i = 0; i < records; ++i) {
        std::uint8_t resp = 0;
        if (Status s = rx8(resp); !s.ok()) return s;
        if (resp != 0) {
            Status s = fail(Code::Device, Where::DirEntry);
            s.device = resp;
            return s;
        }
        DirEntry      e;
        std::uint16_t dateWord = 0, timeWord = 0, nameLen = 0;
        std::uint8_t  attrib = 0;
        if (Status s = rx32(e.size); !s.ok()) return s;
        if (Status s = rx16(dateWord); !s.ok()) return s;  // unused
        if (Status s = rx16(timeWord); !s.ok()) return s;  // unused
        if (Status s = rx8(attrib); !s.ok()) return s;
        if (Status s = rx16(nameLen); !s.ok()) return s;
        if (nameLen > kNameMax) return fail(Code::TooLarge, Where::Name);
        if (Status s = readData(reinterpret_cast<std::uint8_t*>(name_), nameLen); !s.ok()) return s;
        name_[nameLen] = '\0';
        e.name         = std::string_view(name_, nameLen);
        e.isDir        = (attrib & 0x10) != 0;  // FatFs AM_DIR
        if (visit) visit(ctx, e);
    }
    return {};
}

Status Edio::fileOpen(std::string_view path, std::uint8_t mode) {
    begin(CMD_F_FOPN);
    put8(mode);
    if (Status s = sendPath(path); !s.ok()) return s;
    return checkStatus();
}

Status Edio::fileWrite(const std::uint8_t* data, std::size_t size, Progress progress) {
    begin(CMD_F_FWR);
    put32(static_cast<std::uint32_t>(size));
    if (Status s = send(); !s.ok()) return s;
    const auto total = static_cast<std::uint32_t>(size);
    for (std::size_t off = 0; off < size;) {
        std::uint8_t ack = 0;  // the device releases each block with a 0 ack byte
        if (Status s = rx8(ack, timeouts_.sdBlockMs); !s.ok()) return s;
        if (ack != 0) {
            Status s = fail(Code::Device, Where::WriteAck);
            s.device = ack;
            return s;
        }
        const std::size_t block = std::min<std::size_t>(size - off, ACK_BLOCK_SIZE);
        if (Status s = write(data + off, block); !s.ok()) return s;
        off += block;
        progress(static_cast<std::uint32_t>(off), total);  // inside the command: reported, not a stop point
    }
    return checkStatus();
}

Status Edio::fileRead(std::uint8_t* out, std::size_t size, Progress progress) {
    const auto total = static_cast<std::uint32_t>(size);
    for (std::size_t off = 0; off < size;) {
        if (!progress(static_cast<std::uint32_t>(off), total)) return fail(Code::Cancelled, Where::FileBlock);
        const std::size_t block = std::min<std::size_t>(RD_BLOCK_SIZE, size - off);
        begin(CMD_F_FRD);
        put32(static_cast<std::uint32_t>(block));
        if (Status s = send(); !s.ok()) return s;
        std::uint8_t resp = 0;
        if (Status s = rx8(resp, timeouts_.sdBlockMs); !s.ok()) return s;
        if (resp != 0) {
            Status s = fail(Code::Device, Where::FileBlock);
            s.device = resp;
            return s;
        }
        if (Status s = readData(out + off, block, timeouts_.sdBlockMs); !s.ok()) return s;
        off += block;
    }
    progress(total, total);
    return {};
}

Status Edio::fileClose() {
    begin(CMD_F_FCLOSE);
    if (Status s = send(); !s.ok()) return s;
    return checkStatus();
}

}  // namespace rp::n8
