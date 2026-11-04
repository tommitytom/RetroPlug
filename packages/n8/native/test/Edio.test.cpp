// Guards the Everdrive N8 Pro protocol (n8/Edio.hpp, Menu.hpp, Ops.hpp) without hardware. The golden (edio-golden.json,
// beside this file) is the protocol's oracle: every case's exact byte stream, recorded from the TS and C++ clients the
// pack's one Edio replaced (the primitives, and the listings, files, menu commands and boots their TS client sent), so
// it holds the wire they spoke. The rest covers what the golden does not: what connect answers, every
// failure each primitive can meet (and how describe() words it, as the TS client threw it), one write per frame, the
// listing's visitor, a link that goes down inside a wait, and the timeout each wait asks for; then the N8Link forward
// path (the standalone's and the plugin's realtime serial thread).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "n8/Edio.hpp"
#include "n8/Menu.hpp"
#include "n8/Ops.hpp"
#include "n8/desktop/N8Link.hpp"
#include "n8/desktop/PortTransport.hpp"

using retroplug::N8Link;
using retroplug::PortTransport;
using rp::n8::Code;
using rp::n8::Edio;
using rp::n8::Status;
using rp::n8::Where;

namespace {

// An in-memory transport: writes append to `written` (each call counted); reads pop from a scripted `toRead` queue (an
// empty queue answers 0 bytes, which Edio takes for a timeout); each read's timeout is recorded. `takeWrites` writes
// are taken whole and every one after takes nothing, as a port that stopped taking bytes; `up` is linkUp(), and
// `downAfterReads` reads take the link down once that many have been served.
struct FakeSerialPort : PortTransport {
    std::vector<std::uint8_t>  written;
    std::vector<std::size_t>   writeSizes;
    std::deque<std::uint8_t>   toRead;
    std::vector<std::uint32_t> timeouts;
    bool                       flushed        = false;
    bool                       up             = true;
    int                        takeWrites     = 1'000'000;
    int                        downAfterReads = -1;

    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        if (takeWrites <= 0) return 0;
        --takeWrites;
        written.insert(written.end(), data, data + size);
        writeSizes.push_back(size);
        return size;
    }
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) override {
        timeouts.push_back(timeoutMs);
        std::size_t n = 0;
        while (n < size && !toRead.empty()) {
            buffer[n++] = toRead.front();
            toRead.pop_front();
        }
        if (downAfterReads > 0 && --downAfterReads == 0) up = false;
        return n;  // 0 => timeout
    }
    void flushInput() override { flushed = true; }
    bool linkUp() const override { return up; }

    // Queue a little-endian 16-bit status word (what the N8 returns for CMD_STATUS).
    void queueStatus(std::uint16_t v) {
        toRead.push_back(static_cast<std::uint8_t>(v & 0xFF));
        toRead.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    void queue(std::initializer_list<std::uint8_t> bytes) { toRead.insert(toRead.end(), bytes); }
};

std::vector<std::uint8_t> fromHex(const std::string& h) {
    std::vector<std::uint8_t> out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return out;
}

std::string text(const Status& s, std::string_view path = {}) {
    char buf[512];
    rp::n8::describe(s, buf, sizeof buf, path);
    return buf;
}

// Every primitive, by name, as a call on an Edio: the table the failure cases run.
struct Primitive {
    const char*                  name;
    std::function<Status(Edio&)> call;
    bool                         reads = true;  // waits on a reply at all (memWR and the FIFO writes do not)
};

std::vector<Primitive> primitives() {
    static std::uint8_t sink[Edio::SIZE_SYS_INFO];
    static const std::uint8_t two[2] = {0xDE, 0xAD};
    return {
        {"connect", [](Edio& e) { return e.connect(); }},
        {"status", [](Edio& e) { std::uint8_t c; return e.status(c); }},
        {"sysInfo", [](Edio& e) { return e.sysInfo(sink); }},
        {"vdc", [](Edio& e) { std::uint8_t v[Edio::SIZE_VDC]; return e.vdc(v); }},
        {"freeSpace", [](Edio& e) { std::uint64_t b; return e.freeSpace(b); }},
        {"dirMake", [](Edio& e) { return e.dirMake("d"); }},
        {"fileDelete", [](Edio& e) { return e.fileDelete("d"); }},
        {"memRD", [](Edio& e) { return e.memRD(Edio::ADDR_SRM, sink, 4); }},
        {"listDir", [](Edio& e) { return e.listDir("d", [](const rp::n8::DirEntry&) {}); }},
        {"fileOpen", [](Edio& e) { return e.fileOpen("f", Edio::FA_READ); }},
        {"fileWrite", [](Edio& e) { return e.fileWrite(two, 2); }},
        {"fileRead", [](Edio& e) { return e.fileRead(sink, 2); }},
        {"fileClose", [](Edio& e) { return e.fileClose(); }},
        {"rx8", [](Edio& e) { std::uint8_t v; return e.rx8(v); }},
        {"readData", [](Edio& e) { return e.readData(sink, 4); }},
        {"memWR", [](Edio& e) { return e.memWR(Edio::ADDR_SRM, two, 2); }, false},
        {"fifoWR", [](Edio& e) { return e.fifoWR(two, 2); }, false},
        {"fifoTxString", [](Edio& e) { return e.fifoTxString("ab"); }, false},
    };
}

}  // namespace

// The golden's schema (edio-golden.json). Field names match the JSON keys; every field the golden omits per op is
// std::optional. These live in a NAMED namespace deliberately: reflect-cpp reflects a struct through a fake-object
// conversion operator, which clang refuses to define for a type with no linkage - an anonymous-namespace DTO fails to
// compile.
namespace edio_golden {

struct EdioArgs {
    std::optional<std::string>  bytes;
    std::optional<std::string>  str;
    std::optional<std::string>  path;
    std::optional<int>          mode;
    std::optional<std::int64_t> addr;
    std::optional<int>          size;
    std::optional<std::string>  srm;  // loadRom: the battery (bytes is its ROM, str its romName, path its sdPath)
};
struct EdioCase {
    std::string                id;
    std::string                op;
    std::optional<EdioArgs>    args;
    std::optional<std::string> reads;
    std::string                writes;
};
struct EdioGolden {
    std::vector<EdioCase> cases;
};

}  // namespace edio_golden

using edio_golden::EdioArgs;
using edio_golden::EdioCase;
using edio_golden::EdioGolden;

TEST_CASE("Edio framing matches the golden byte for byte", "[n8]") {
    std::ifstream f(EDIO_GOLDEN_PATH);
    REQUIRE(f.good());
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const auto parsed = rfl::json::read<EdioGolden>(text);
    REQUIRE(parsed);
    const EdioGolden g = parsed.value();
    // The 15 primitives' cases, then the 13 recorded from the TS client at ad3a1d4ae, the commit before the one Edio
    // (listings, files read whole, the menu's commands, boots).
    REQUIRE(g.cases.size() == 28);

    for (const EdioCase& c : g.cases) {
        FakeSerialPort port;
        if (c.reads)
            for (std::uint8_t b : fromHex(*c.reads)) port.toRead.push_back(b);
        Edio           edio(port);
        const EdioArgs a     = c.args.value_or(EdioArgs{});
        const auto     bytes = fromHex(a.bytes.value_or(""));
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(std::max(a.size.value_or(0), 64)));
        Status s;

        if (c.op == "fifoWR") {
            s = edio.fifoWR(bytes.data(), bytes.size());
        } else if (c.op == "fifoTxString") {
            s = edio.fifoTxString(a.str.value_or(""));
        } else if (c.op == "connect") {
            s = edio.connect();
        } else if (c.op == "fileOpen") {
            s = edio.fileOpen(a.path.value_or(""), static_cast<std::uint8_t>(a.mode.value_or(0)));
        } else if (c.op == "fileWrite") {
            s = edio.fileWrite(bytes.data(), bytes.size());
        } else if (c.op == "writeFile") {
            s = rp::n8::writeFile(edio, a.path.value_or(""), {bytes.data(), bytes.size()});
        } else if (c.op == "fileClose") {
            s = edio.fileClose();
        } else if (c.op == "memRD") {
            s = edio.memRD(static_cast<std::uint32_t>(a.addr.value_or(0)), buf.data(),
                           static_cast<std::size_t>(a.size.value_or(0)));
        } else if (c.op == "fileRead") {
            s = edio.fileRead(buf.data(), static_cast<std::size_t>(a.size.value_or(0)));
        } else if (c.op == "sysInfo") {
            std::uint8_t info[Edio::SIZE_SYS_INFO];
            s = edio.sysInfo(info);
        } else if (c.op == "vdc") {
            std::uint8_t vdc[Edio::SIZE_VDC];
            s = edio.vdc(vdc);
        } else if (c.op == "freeSpace") {
            std::uint64_t bytesFree = 0;
            s = edio.freeSpace(bytesFree);
        } else if (c.op == "dirMake") {
            s = edio.dirMake(a.path.value_or(""));
        } else if (c.op == "fileDelete") {
            s = edio.fileDelete(a.path.value_or(""));
        } else if (c.op == "listDir") {
            std::vector<std::string> entries;  // "<name> <size> <file|dir>", as the old client parsed each record
            s = edio.listDir(a.path.value_or(""), [&](const rp::n8::DirEntry& e) {
                entries.push_back(std::string(e.name) + " " + std::to_string(e.size) + (e.isDir ? " dir" : " file"));
            });
            if (c.id == "listDir.two") CHECK(entries == std::vector<std::string>{"a.nes 10 file", "sub 0 dir"});
            if (c.id == "listDir.empty") CHECK(entries.empty());
        } else if (c.op == "readFile") {
            std::vector<std::uint8_t> file(4096);
            std::size_t               size = 0;
            s = rp::n8::readFile(edio, a.path.value_or(""), file.data(), file.size(), &size);
        } else if (c.op == "menu.test") {
            s = rp::n8::Menu(edio).test();
        } else if (c.op == "menu.install") {
            std::uint16_t index = 0;
            s = rp::n8::Menu(edio).install(a.path.value_or(""), &index);
            CHECK(index == 7);  // the map index the old client parsed from the case's reply
        } else if (c.op == "menu.start") {
            s = rp::n8::Menu(edio).start();
        } else if (c.op == "menu.reset") {
            s = rp::n8::Menu(edio).reset();
        } else if (c.op == "menu.vramDump") {
            std::uint8_t vram[rp::n8::Menu::kVramBytes], palette[rp::n8::Menu::kPaletteBytes];
            s = rp::n8::Menu(edio).vramDump(vram, palette);
        } else if (c.op == "loadRom") {
            // As the old TS client's loadRom (n8Load.ts at ad3a1d4ae, which recorded these cases): an sdPath boots what
            // the card holds, else the ROM goes to usb-games/<romName>.
            const auto        srm    = fromHex(a.srm.value_or(""));
            const bool        upload = !a.path;
            const std::string path   = upload ? "usb-games/" + a.str.value_or("") : *a.path;
            std::uint16_t     index  = 0;
            s = rp::n8::bootRom(edio, path, {bytes.data(), bytes.size()}, {srm.data(), srm.size()},
                                upload ? rp::n8::Upload::Always : rp::n8::Upload::Never, &index);
            CHECK(index == 0x45);  // the map index that client parsed from the case's reply
        } else {
            FAIL("unknown golden op: " << c.op);
        }

        INFO("golden case: " << c.id);
        CHECK(s.ok());
        REQUIRE(port.written == fromHex(c.writes));
        CHECK(port.toRead.empty());  // every reply byte the case queued was read, and no more were asked for
    }
}

// --- what a call answers -----------------------------------------------------------------------------------------

TEST_CASE("connect flushes and answers Ok with a 0 status on a 0xA500 reply", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);
    std::uint8_t code = 0xFF;
    REQUIRE(Edio(port).connect(&code).ok());
    CHECK(code == 0);
    CHECK(port.flushed);
}

TEST_CASE("connect surfaces the low status byte", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA5C3);  // high byte OK, status code 0xC3
    std::uint8_t code = 0;
    REQUIRE(Edio(port).connect(&code).ok());
    CHECK(code == 0xC3);
}

TEST_CASE("connect answers BadReply on a non-0xA5 status word, worded with the word", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0x12AB);  // wrong high byte
    const Status s = Edio(port).connect();
    CHECK(s.code == Code::BadReply);
    CHECK(s.where == Where::StatusReply);
    CHECK(s.reply == 0x12AB);
    CHECK(text(s) == "Edio: unexpected status response (12ab)");
}

TEST_CASE("connect answers Timeout when the device does not answer", "[n8]") {
    FakeSerialPort port;  // no queued reply => read returns 0 => timeout
    const Status   s = Edio(port).connect();
    CHECK(s.code == Code::Timeout);
    CHECK(s.where == Where::Read);
    CHECK(text(s) == "Edio: serial read timeout (no N8 response)");
}

TEST_CASE("fileRead loops resp-gated blocks over RD_BLOCK_SIZE", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back(0x00);  // block 1 resp
    for (int i = 0; i < Edio::RD_BLOCK_SIZE; ++i) port.toRead.push_back(0xAA);
    port.toRead.push_back(0x00);  // block 2 resp
    for (int i = 0; i < 4; ++i) port.toRead.push_back(0xBB);
    std::vector<std::uint8_t> buf(Edio::RD_BLOCK_SIZE + 4);
    REQUIRE(Edio(port).fileRead(buf.data(), buf.size()).ok());
    REQUIRE(buf.front() == 0xAA);
    REQUIRE(buf[Edio::RD_BLOCK_SIZE - 1] == 0xAA);
    REQUIRE(buf.back() == 0xBB);
}

TEST_CASE("readFile finds the size via listDir, then reads the whole file", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);                                                            // listDir DIR_LD status
    port.queue({0x01, 0x00});                                                             // DIR_SIZE = 1 record
    port.queue({0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x66});  // file "f", size 4
    port.queueStatus(0xA500);                                                            // fileOpen(FA_READ) status
    port.queue({0x00, 0xDE, 0xAD, 0xBE, 0xEF});                                          // fileRead resp + 4 bytes
    port.queueStatus(0xA500);                                                            // fileClose status
    Edio         edio(port);
    std::uint8_t buf[8] = {};
    std::size_t  size   = 0;
    REQUIRE(rp::n8::readFile(edio, "d/f", buf, sizeof buf, &size).ok());
    REQUIRE(size == 4);
    REQUIRE(std::vector<std::uint8_t>(buf, buf + 4) == std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("Menu.vramDump sends '*v' and splits the 2048+16 reply", "[n8]") {
    FakeSerialPort port;
    for (int i = 0; i < 2048; ++i) port.toRead.push_back(static_cast<std::uint8_t>(i & 0xFF));
    for (int i = 0; i < 16; ++i) port.toRead.push_back(static_cast<std::uint8_t>(0xA0 + i));
    Edio         edio(port);
    std::uint8_t vram[rp::n8::Menu::kVramBytes];
    std::uint8_t palette[rp::n8::Menu::kPaletteBytes];
    REQUIRE(rp::n8::Menu(edio).vramDump(vram, palette).ok());
    REQUIRE(vram[2047] == (2047 & 0xFF));
    REQUIRE(palette[0] == 0xA0);
    REQUIRE(palette[15] == 0xAF);
    // The only write is the '*v' FIFO command (memWR to ADDR_FIFO 0x1810000).
    REQUIRE(port.written == fromHex("2bd41ae50000810102000000002a76"));
}

TEST_CASE("Menu.install tells a missing map index from a failed install", "[n8]") {
    // A 0 status and then no index: the ROM is installed, and the failure says where it came, so a caller that boots
    // regardless (the Pico box always has) can tell it from the install's own timeout.
    FakeSerialPort port;
    port.queue({0x00});
    Edio           edio(port);
    const Status   noIndex = rp::n8::Menu(edio).install("usb-games/x.nes");
    CHECK(noIndex.code == Code::Timeout);
    CHECK(noIndex.where == Where::MapIndex);
    CHECK(text(noIndex) == "Edio: serial read timeout (no N8 response)");

    const Status noStatus = rp::n8::Menu(edio).install("usb-games/x.nes");
    CHECK(noStatus.code == Code::Timeout);
    CHECK(noStatus.where == Where::Read);
}

// --- every failure each primitive can meet -----------------------------------------------------------------------

TEST_CASE("every primitive that waits answers Timeout when its reply does not come, and sent its command first",
          "[n8][edio-status]") {
    for (const Primitive& p : primitives()) {
        INFO(p.name);
        FakeSerialPort port;
        Edio           edio(port);
        const Status   s = p.call(edio);
        if (!p.reads) {
            CHECK(s.ok());  // fire-and-forget: nothing to wait for
            continue;
        }
        CHECK(s.code == Code::Timeout);
        CHECK(s.where == Where::Read);
        CHECK(text(s) == "Edio: serial read timeout (no N8 response)");
        // rx8 and readData send nothing; every other one asked before it waited.
        const bool raw = std::string_view(p.name) == "rx8" || std::string_view(p.name) == "readData";
        CHECK(port.written.empty() == raw);
    }
}

TEST_CASE("every primitive answers LinkDown at once when the link is down, writing nothing", "[n8][edio-status]") {
    for (const Primitive& p : primitives()) {
        INFO(p.name);
        FakeSerialPort port;
        port.up = false;
        port.queueStatus(0xA500);  // a reply that is there, so only the link's state can fail the call
        Edio         edio(port);
        const Status s = p.call(edio);
        CHECK(s.code == Code::LinkDown);
        CHECK(port.written.empty());
        CHECK(port.timeouts.empty());  // nothing waited
        CHECK(text(s) == "Edio: the N8's USB link is down");
    }
}

TEST_CASE("every primitive stops at a write the port did not take, sending nothing after it", "[n8][edio-status]") {
    for (const Primitive& p : primitives()) {
        const bool raw = std::string_view(p.name) == "rx8" || std::string_view(p.name) == "readData";
        if (raw) continue;  // sends nothing
        INFO(p.name);
        FakeSerialPort port;
        port.takeWrites = 0;
        port.queueStatus(0xA500);
        Edio         edio(port);
        const Status s = p.call(edio);
        CHECK(s.code == Code::Timeout);
        CHECK(s.where == Where::Write);
        CHECK(port.timeouts.empty());  // it read no reply to a command that never went out
        CHECK(text(s) == "Edio: serial write timeout (the N8 did not take the command)");
    }
    // And a write past the first: fifoTxString's string, after its length went out.
    FakeSerialPort port;
    port.takeWrites = 1;
    Edio edio(port);
    CHECK(edio.fifoTxString("ab").where == Where::Write);
    CHECK(port.writeSizes.size() == 1);
}

TEST_CASE("a status word that is not <status> A5 is BadReply wherever a status is read", "[n8][edio-status]") {
    const std::uint8_t two[2] = {1, 2};
    const std::vector<Primitive> statusReaders = {
        {"connect", [](Edio& e) { return e.connect(); }},
        {"status", [](Edio& e) { std::uint8_t c; return e.status(c); }},
        {"dirMake", [](Edio& e) { return e.dirMake("d"); }},
        {"fileDelete", [](Edio& e) { return e.fileDelete("d"); }},
        {"listDir", [](Edio& e) { return e.listDir("d", [](const rp::n8::DirEntry&) {}); }},
        {"fileOpen", [](Edio& e) { return e.fileOpen("f", Edio::FA_READ); }},
        {"fileClose", [](Edio& e) { return e.fileClose(); }},
        {"fileWrite", [&](Edio& e) { return e.fileWrite(two, 2); }},  // its trailing status, after the block's ack
    };
    for (const Primitive& p : statusReaders) {
        INFO(p.name);
        FakeSerialPort port;
        if (std::string_view(p.name) == "fileWrite") port.queue({0x00});
        port.queueStatus(0x0000);  // a 0 high byte: no A5
        Edio         edio(port);
        const Status s = p.call(edio);
        CHECK(s.code == Code::BadReply);
        CHECK(s.where == Where::StatusReply);
        CHECK(text(s) == "Edio: unexpected status response (0000)");
    }
}

TEST_CASE("a non-zero device status is Device, worded as the TS client threw it", "[n8][edio-status]") {
    const std::uint8_t two[2] = {1, 2};
    std::uint8_t       buf[8];
    struct Row {
        const char*                        name;
        std::function<Status(Edio&)>       call;
        std::vector<std::uint8_t>          reads;
        Where                              where;
        std::string                        said;
    };
    const std::vector<Row> rows = {
        {"fileDelete", [](Edio& e) { return e.fileDelete("d"); }, {0x0A, 0xA5}, Where::Operation,
         "Edio: operation error 0x0a"},
        {"fileOpen", [](Edio& e) { return e.fileOpen("f", Edio::FA_READ); }, {0x04, 0xA5}, Where::Operation,
         "Edio: operation error 0x04"},
        {"fileClose", [](Edio& e) { return e.fileClose(); }, {0x01, 0xA5}, Where::Operation,
         "Edio: operation error 0x01"},
        {"listDir (load)", [](Edio& e) { return e.listDir("d", [](const rp::n8::DirEntry&) {}); }, {0x05, 0xA5},
         Where::Operation, "Edio: operation error 0x05"},
        {"listDir (entry)", [](Edio& e) { return e.listDir("d", [](const rp::n8::DirEntry&) {}); },
         {0x00, 0xA5, 0x01, 0x00, 0x0B}, Where::DirEntry, "Edio: dir read error 0x0b"},
        {"dirMake", [](Edio& e) { return e.dirMake("d"); }, {0x07, 0xA5}, Where::MkDir, "Edio: mkdir error 0x07 (d)"},
        {"fileRead", [&](Edio& e) { return e.fileRead(buf, 2); }, {0x0C}, Where::FileBlock,
         "Edio: file read error 0x0c"},
        {"fileWrite (ack)", [&](Edio& e) { return e.fileWrite(two, 2); }, {0x0D}, Where::WriteAck,
         "Edio: tx ack error 0x0d"},
        {"fileWrite (status)", [&](Edio& e) { return e.fileWrite(two, 2); }, {0x00, 0x11, 0xA5}, Where::Operation,
         "Edio: operation error 0x11"},
    };
    for (const Row& r : rows) {
        INFO(r.name);
        FakeSerialPort port;
        port.toRead.insert(port.toRead.end(), r.reads.begin(), r.reads.end());
        Edio         edio(port);
        const Status s = r.call(edio);
        CHECK(s.code == Code::Device);
        CHECK(s.where == r.where);
        CHECK(text(s, "d") == r.said);
    }
    // An "exists" is no failure of dirMake's.
    FakeSerialPort port;
    port.queue({0x08, 0xA5});
    CHECK(Edio(port).dirMake("d").ok());
}

TEST_CASE("TooLarge: a string past its 16-bit length is sent as nothing, a listed name past 255 stops the listing",
          "[n8][edio-status]") {
    const std::string huge(0x10000, 'x');
    for (const auto& call : std::vector<std::function<Status(Edio&)>>{
             [&](Edio& e) { return e.fifoTxString(huge); }, [&](Edio& e) { return e.dirMake(huge); },
             [&](Edio& e) { return e.fileDelete(huge); }, [&](Edio& e) { return e.fileOpen(huge, Edio::FA_READ); },
             [&](Edio& e) { return e.listDir(huge, [](const rp::n8::DirEntry&) {}); }}) {
        FakeSerialPort port;
        Edio           edio(port);
        const Status   s = call(edio);
        CHECK(s.code == Code::TooLarge);
        CHECK(s.where == Where::Length);
        CHECK(port.written.empty());
        // The next command starts clean: nothing of the refused one's frame is left in the scratch.
        port.queueStatus(0xA500);
        CHECK(edio.fileClose().ok());
        CHECK(port.written == fromHex("2bd4ce312bd410ef"));
    }

    FakeSerialPort port;
    port.queueStatus(0xA500);
    port.queue({0x01, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x00, 0x01});  // one record, its name 256 bytes long
    int        visited = 0;
    const Status s     = Edio(port).listDir("d", [&](const rp::n8::DirEntry&) { ++visited; });
    CHECK(s.code == Code::TooLarge);
    CHECK(s.where == Where::Name);
    CHECK(visited == 0);
    CHECK(text(s) == "Edio: a directory entry's name is longer than the 255 bytes asked for");
}

// --- the frame, the listing, the link, the timeouts ----------------------------------------------------------------

TEST_CASE("one write per frame: a command, its arguments and its payload's first bytes go out in one write",
          "[n8][edio-frame]") {
    FakeSerialPort port;
    Edio           edio(port);
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    REQUIRE(edio.fifoWR(note, sizeof note).ok());
    CHECK(port.writeSizes == std::vector<std::size_t>{16});  // 13 bytes of command, the 3 of the note

    // A payload past the 256-byte frame: the frame full, then the rest in a write of its own.
    port.writeSizes.clear();
    std::vector<std::uint8_t> big(1000, 0x55);
    REQUIRE(edio.memWR(static_cast<std::uint32_t>(Edio::ADDR_PRG), big.data(), big.size()).ok());
    CHECK(port.writeSizes == std::vector<std::size_t>{256, 1013 - 256});

    // And past 8 KB, in 8 KB writes after the frame, as the clients this replaced chunked it.
    port.writeSizes.clear();
    std::vector<std::uint8_t> huge(20000, 0x66);
    REQUIRE(edio.memWR(static_cast<std::uint32_t>(Edio::ADDR_PRG), huge.data(), huge.size()).ok());
    CHECK(port.writeSizes == std::vector<std::size_t>{256, 8192, 8192, 20013 - 256 - 2 * 8192});

    // A path is its command's payload: fileOpen's header, mode, length and path in one write, the status poll another.
    port.writeSizes.clear();
    port.queueStatus(0xA500);
    REQUIRE(edio.fileOpen("usb-games/a.nes", Edio::FA_READ).ok());
    CHECK(port.writeSizes == std::vector<std::size_t>{4 + 1 + 2 + 15, 4});

    // A file write's blocks go out as they are released, one write a block.
    port.writeSizes.clear();
    port.queue({0x00, 0x00});
    port.queueStatus(0xA500);
    std::vector<std::uint8_t> file(1500, 0x77);
    REQUIRE(edio.fileWrite(file.data(), file.size()).ok());
    CHECK(port.writeSizes == std::vector<std::size_t>{8, 1024, 476, 4});
}

TEST_CASE("listDir hands its visitor each entry, a 255-byte name whole, and the count", "[n8][edio-frame]") {
    FakeSerialPort    port;
    const std::string longName(255, 'n');
    port.queueStatus(0xA500);
    port.queue({0x02, 0x00});  // two records
    // A file of 0x01020304 bytes with the longest name, then a directory "d".
    port.queue({0x00, 0x04, 0x03, 0x02, 0x01, 0, 0, 0, 0, 0x20, 0xFF, 0x00});
    for (char c : longName) port.toRead.push_back(static_cast<std::uint8_t>(c));
    port.queue({0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0x01, 0x00, 'd'});
    struct Seen {
        std::string   name;
        std::uint32_t size;
        bool          isDir;
    };
    std::vector<Seen> seen;
    std::uint16_t     count = 0;
    const auto visit = [&](const rp::n8::DirEntry& e) { seen.push_back({std::string(e.name), e.size, e.isDir}); };
    REQUIRE(Edio(port).listDir("x", visit, &count).ok());
    CHECK(count == 2);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].name == longName);
    CHECK(seen[0].size == 0x01020304u);
    CHECK_FALSE(seen[0].isDir);
    CHECK(seen[1].name == "d");
    CHECK(seen[1].isDir);
    // The request asked for names of at most 255 bytes, and an empty directory answers no record pull.
    CHECK(std::vector<std::uint8_t>(port.written.end() - 10, port.written.end()) ==
          fromHex("2bd4c837" "0000" "0200" "ff00"));
    FakeSerialPort empty;
    empty.queueStatus(0xA500);
    empty.queue({0x00, 0x00});
    CHECK(Edio(empty).listDir("x", [&](const rp::n8::DirEntry&) { FAIL("no entry"); }).ok());
    CHECK(empty.written == fromHex("2bd4c53a01010078" "2bd410ef" "2bd4c639"));
}

TEST_CASE("a link that goes down inside a wait fails it at once, as LinkDown, not a timeout", "[n8][edio-frame]") {
    FakeSerialPort port;
    port.downAfterReads = 1;   // the first read serves half the reply, then the link goes
    port.queue({0x00});         // CMD_STATUS's reply, cut short
    const Status s = Edio(port).connect();
    CHECK(s.code == Code::LinkDown);
    CHECK(s.where == Where::Read);
    CHECK(port.timeouts.size() == 1);  // no second wait on a link known to be down
}

TEST_CASE("each wait asks the transport for its own timeout from the table, and a call may override it",
          "[n8][edio-frame]") {
    FakeSerialPort port;
    Edio           edio(port);
    port.queueStatus(0xA500);
    REQUIRE(edio.connect().ok());
    CHECK(port.timeouts.back() == 300);  // the handshake
    port.queueStatus(0xA500);
    std::uint8_t code = 0;
    REQUIRE(edio.status(code).ok());
    CHECK(port.timeouts.back() == 2000);  // a reply
    port.queue({0x00, 0xAA, 0xBB});
    std::uint8_t two[2];
    REQUIRE(edio.fileRead(two, 2).ok());
    CHECK(port.timeouts.back() == 5000);  // a file block: its status and its data
    port.timeouts.clear();
    port.queue({0x00});
    port.queueStatus(0xA500);
    REQUIRE(edio.fileWrite(two, 2).ok());
    CHECK(port.timeouts == std::vector<std::uint32_t>{5000, 2000});  // the block's ack, then the command's status
    port.queue({0x00, 0x01, 0x00});
    std::uint16_t index = 0;
    REQUIRE(rp::n8::Menu(edio).install("p", &index).ok());
    CHECK(index == 1);
    CHECK(port.timeouts[port.timeouts.size() - 2] == 12000);  // the install's status, then the map index at 2 s
    CHECK(port.timeouts.back() == 2000);
    port.queueStatus(0xA500);
    REQUIRE(edio.connect(nullptr, 50).ok());
    CHECK(port.timeouts.back() == 50);
    edio.timeouts().readMs = 700;
    port.queue({0x01});
    std::uint8_t v = 0;
    REQUIRE(edio.rx8(v).ok());
    CHECK(port.timeouts.back() == 700);
}

// --- N8Link: the host serial thread + ring + timed scheduler (standalone/plugin forward) ---------------------------

namespace {
// A serial port that captures writes into a TEST-OWNED buffer, so it outlives N8Link's port (disconnect()
// destroys the port; the buffer survives + is safely readable after the join happens-before).
struct N8FakePort : PortTransport {
    std::vector<std::uint8_t>& written;
    std::deque<std::uint8_t>   toRead;
    explicit N8FakePort(std::vector<std::uint8_t>& w) : written(w) {}
    std::size_t write(const std::uint8_t* d, std::size_t n) override {
        written.insert(written.end(), d, d + n);
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t) override {
        std::size_t i = 0;
        while (i < n && !toRead.empty()) { b[i++] = toRead.front(); toRead.pop_front(); }
        return i;
    }
    void flushInput() override {}
};

template <class Pred>
bool waitUntil(Pred pred, int ms = 1000) {
    for (int i = 0; i < ms && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return pred();
}

// Factory: capture writes into `out` (test-owned), priming the handshake reply on each opened port.
N8Link::PortFactory captureFactory(std::vector<std::uint8_t>& out, std::uint16_t status = 0xA500) {
    return [&out, status](const std::string&) -> std::unique_ptr<PortTransport> {
        auto f = std::make_unique<N8FakePort>(out);
        f->toRead.push_back(static_cast<std::uint8_t>(status & 0xFF));
        f->toRead.push_back(static_cast<std::uint8_t>(status >> 8));
        return f;
    };
}
}  // namespace

TEST_CASE("N8Link forwards a pushed MIDI message to the serial port via fifoWR", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    REQUIRE(link.connect("fake"));
    REQUIRE(link.isConnected());
    link.setLookaheadMs(0);

    const std::uint8_t midi[] = {0x90, 0x3C, 0x7F};
    link.push(0, midi, sizeof(midi), 48000.0);
    REQUIRE(waitUntil([&] { return link.bytesForwarded() >= 3; }));
    link.disconnect();  // joins the serial thread -> `captured` is complete + safe to read

    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x10, 0xEF,  // connect(): CMD_STATUS handshake
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0xF4, 0x00,  // the link probe (sync)
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x90, 0x3C, 0x7F,  // fifoWR
    };
    REQUIRE(captured == expected);
}

TEST_CASE("N8Link forwards multiple messages in FIFO order", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    REQUIRE(link.connect("fake"));
    link.setLookaheadMs(0);

    const std::uint8_t on[]  = {0x90, 0x3C, 0x7F};
    const std::uint8_t off[] = {0x80, 0x3C, 0x00};
    link.push(0, on, sizeof(on), 48000.0);
    link.push(0, off, sizeof(off), 48000.0);
    REQUIRE(waitUntil([&] { return link.bytesForwarded() >= 6; }));
    link.disconnect();

    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x10, 0xEF,  // handshake
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0xF4, 0x00,  // the link probe
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x90, 0x3C, 0x7F,  // note-on
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3C, 0x00,  // note-off
    };
    REQUIRE(captured == expected);
}

TEST_CASE("N8Link frames MIDI for a ROM that answers the link probe (BlipToaster's reliable link)", "[n8]") {
    // The handshake reply, then the ROM's ack to the probe (F5 00 on the back-channel): the link comes up, and a
    // note goes out as the sender's sync + one checksummed frame (transport/N8ReliableLink.hpp).
    std::vector<std::uint8_t> captured;
    N8Link link([&captured](const std::string&) -> std::unique_ptr<PortTransport> {
        auto f = std::make_unique<N8FakePort>(captured);
        for (std::uint8_t b : { 0x00, 0xA5, 0xF5, 0x00, 0x7F }) f->toRead.push_back(b);
        return f;
    });
    REQUIRE(link.connect("fake"));
    link.setLookaheadMs(0);
    REQUIRE(waitUntil([&] { return link.reliable(); }));

    const std::uint8_t midi[] = {0x90, 0x3C, 0x7F};
    link.push(0, midi, sizeof(midi), 48000.0);
    REQUIRE(waitUntil([&] { return link.bytesForwarded() >= 8; }));
    link.disconnect();

    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x10, 0xEF,
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0xF4, 0x00,   // probe
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00,               // fifoWR, 8 bytes:
        0xF4, 0x00, 0xF5, 0x00, 0x90, 0x3C, 0x7F, 0x4B,                                             // sync + frame
    };
    REQUIRE(captured == expected);
    CHECK(link.linkOutstanding() <= 1);
}

TEST_CASE("N8Link connect fails cleanly on a bad handshake, with the status's words", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured, 0x1234));  // wrong high byte
    REQUIRE_FALSE(link.connect("fake"));
    REQUIRE_FALSE(link.isConnected());
    CHECK(link.lastError() == "Edio: unexpected status response (1234)");
}

TEST_CASE("N8Link push is a no-op when not connected", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    const std::uint8_t        m[] = {0x90, 0x3C, 0x7F};
    link.push(0, m, sizeof(m), 48000.0);  // not connected -> dropped
    REQUIRE(link.bytesForwarded() == 0);
}
