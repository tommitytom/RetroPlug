#include "N8Protocol.hpp"

#include <cstdint>

#include "n8/Edio.hpp"
#include "n8/Menu.hpp"
#include "n8/Ops.hpp"

namespace rp::device_check {

namespace {

// What the cart answers the script's calls, in order.
constexpr std::uint8_t kReplies[] = {
    0x00, 0xA5,                    // connect: status OK
    0x6B,                          // '*t': 'k'
    0x00, 0xA5, 0x00, 0x00, 0xA5, 0x00, 0xA5,  // the ROM: open, its one block's ack, its status, close
    0x00, 0xA5, 0x00, 0x00, 0xA5, 0x00, 0xA5,  // the battery: the same
    0x00, 0x45, 0x00,              // '*n': installed, map index 0x45
    0x00, 0xA5, 0x01, 0x00,        // readFile's listing: loaded, one record
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 'f',  // "f", 4 bytes
    0x00, 0xA5,                    // its open
    0x00, 0xDE, 0xAD, 0xBE, 0xEF,  // its block: status, then the data
    0x00, 0xA5,                    // its close
    0x00, 0xA5,                    // dirMake: done
    0x00, 0xA5,                    // fileDelete: done
    0x00,                          // '*r''s ack
    0x00, 0xA5,                    // status: OK
};

// The cart, as far as the script goes: writes taken whole and hashed, reads served from kReplies.
class Script final : public n8::Transport {
public:
    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        for (std::size_t i = 0; i < size; ++i) hash_ = (hash_ ^ data[i]) * 16777619u;
        written_ += size;
        return size;
    }
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t) override {
        std::size_t n = 0;
        for (; n < size && zeros_ > 0; --zeros_) buffer[n++] = 0;
        while (n < size && at_ < sizeof kReplies) buffer[n++] = kReplies[at_++];
        return n;
    }
    // The next `n` bytes read are zeros, ahead of the table: a reply whose content the script does not check (the
    // device info, the voltages, a memory read, the menu's screen), kept out of the image's flash.
    void zeros(std::size_t n) { zeros_ = n; }
    void flushInput() override {}

    void reset() {
        zeros_   = 0;
        at_      = 0;
        written_ = 0;
        hash_    = 2166136261u;
    }
    bool          drained() const { return at_ == sizeof kReplies && zeros_ == 0; }
    std::size_t   written() const { return written_; }
    std::uint32_t hash() const { return hash_; }

private:
    std::size_t   zeros_   = 0;
    std::size_t   at_      = 0;
    std::size_t   written_ = 0;
    std::uint32_t hash_    = 2166136261u;
};

bool same(const char* a, const char* b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

// Static, as an image's long-lived objects are: the Edio's frame and name buffers are 512 bytes, which a device keeps
// off its stack.
Script   gScript;
n8::Edio gEdio(gScript);

std::uint8_t gRom[1024];
std::uint8_t gSrm[16];
std::uint8_t gVram[n8::Menu::kVramBytes];  // 2 KB: the menu's screen, off a 2 KB stack
std::uint8_t gPalette[n8::Menu::kPaletteBytes];

}  // namespace

N8ProtocolResult n8Protocol() {
    N8ProtocolResult r;
    const auto fail = [&r](const char* what) {
        r.failed = what;
        r.written = gScript.written();
        r.hash    = gScript.hash();
        return r;
    };
    gScript.reset();  // so a second run meets the script from its start
    for (std::size_t i = 0; i < sizeof gRom; ++i) gRom[i] = static_cast<std::uint8_t>(i * 7);
    for (std::size_t i = 0; i < sizeof gSrm; ++i) gSrm[i] = static_cast<std::uint8_t>(0xA0 + i);

    std::uint8_t status = 0xFF;
    if (!gEdio.connect(&status).ok() || status != 0) return fail("connect");
    std::uint16_t mapIndex = 0;
    if (!n8::bootRom(gEdio, "usb-games/check.nes", {gRom, sizeof gRom}, {gSrm, sizeof gSrm}, n8::Upload::Always,
                     &mapIndex)
             .ok() ||
        mapIndex != 0x45)
        return fail("bootRom");
    std::uint8_t file[8] = {};
    std::size_t  size    = 0;
    if (!n8::readFile(gEdio, "d/f", file, sizeof file, &size).ok() || size != 4 || file[0] != 0xDE || file[3] != 0xEF)
        return fail("readFile");
    // The primitives a boot and a file read do not reach, each called once so the image links them under the trap (it
    // is linked with section GC, which drops a function nothing calls before the trap could see what it references).
    std::uint8_t info[n8::Edio::SIZE_SYS_INFO];
    gScript.zeros(sizeof info);
    if (!gEdio.sysInfo(info).ok()) return fail("sysInfo");
    std::uint8_t vdc[n8::Edio::SIZE_VDC];
    gScript.zeros(sizeof vdc);
    if (!gEdio.vdc(vdc).ok()) return fail("vdc");
    std::uint64_t bytesFree = 1;
    gScript.zeros(8);
    if (!gEdio.freeSpace(bytesFree).ok() || bytesFree != 0) return fail("freeSpace");
    if (!gEdio.dirMake("chk").ok()) return fail("dirMake");
    if (!gEdio.fileDelete("chk").ok()) return fail("fileDelete");
    std::uint8_t cfg[n8::Edio::SIZE_CFG];
    gScript.zeros(sizeof cfg);
    if (!gEdio.memRD(static_cast<std::uint32_t>(n8::Edio::ADDR_CFG), cfg, sizeof cfg).ok()) return fail("memRD");
    n8::Menu menu(gEdio);
    gScript.zeros(sizeof gVram + sizeof gPalette);
    if (!menu.vramDump(gVram, gPalette).ok()) return fail("vramDump");
    if (!menu.reset().ok()) return fail("reset");
    if (!gEdio.status(status).ok() || status != 0) return fail("status");
    // A boot cancelled at its first stop point sends and reads nothing.
    const n8::Progress cancel{[](void*, std::uint32_t, std::uint32_t) { return false; }, nullptr};
    const n8::Status   cancelled =
        n8::bootRom(gEdio, "usb-games/check.nes", {gRom, sizeof gRom}, {}, n8::Upload::Always, nullptr, cancel);
    if (cancelled.code != n8::Code::Cancelled || cancelled.step != n8::Step::Test) return fail("cancel");
    if (!same(n8::codeName(cancelled.code), "cancelled") || !same(n8::stepName(cancelled.step), "test"))
        return fail("codeName");
    if (!gScript.drained()) return fail("replies left unread");
    char text[96];
    if (n8::describe(cancelled, text, sizeof text) == 0) return fail("describe");

    r.written = gScript.written();
    r.hash    = gScript.hash();
    if (r.written != kN8ProtocolBytes || r.hash != kN8ProtocolHash) return fail("the bytes written");
    r.ok = true;
    return r;
}

}  // namespace rp::device_check
