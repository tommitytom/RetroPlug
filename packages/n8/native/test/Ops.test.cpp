// Guards the on-device menu's commands (n8/Menu.hpp) and the operations over them (n8/Ops.hpp) against FakeN8.hpp's
// model of the cart, which answers the menu's '*' commands and keeps an SD card, a battery and the FPGA config block:
// the menu's replies and refusals (worded as the TS client threw them, which the CLI prints), a ROM booted through the
// menu with and without its upload and its battery, a cancel at each of bootRom's stop points and the step a failure
// names, a file read and written whole, the cart-SRAM paths the CLI's direct writes take, free space, mkdir and delete.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "FakeN8.hpp"
#include "n8/Edio.hpp"
#include "n8/Menu.hpp"
#include "n8/Ops.hpp"

using retroplug::test::Bytes;
using retroplug::test::FakeN8;
using retroplug::test::FakeN8Port;
using rp::n8::Code;
using rp::n8::Edio;
using rp::n8::Menu;
using rp::n8::Status;
using rp::n8::Step;
using rp::n8::Where;

namespace {

std::string text(const Status& s, std::string_view path = {}) {
    char buf[512];
    rp::n8::describe(s, buf, sizeof buf, path);
    return buf;
}

// The FIFO writes the cart saw, in order ("fifo:2A 74" is '*t').
std::vector<std::string> fifoLog(FakeN8& dev) {
    std::lock_guard<std::mutex> l(dev.mu);
    std::vector<std::string>    out;
    for (const std::string& e : dev.log)
        if (e.rfind("fifo:", 0) == 0) out.push_back(e);
    return out;
}

Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(seed + i * 7);
    return b;
}

}  // namespace

// --- the menu's commands -----------------------------------------------------------------------------------------

TEST_CASE("the menu's test takes 'k', and anything else or nothing is a failure in the menu's words", "[n8][menu]") {
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    CHECK(Menu(edio).test().ok());
    CHECK(fifoLog(dev) == std::vector<std::string>{"fifo:2A 74"});

    dev.testReply = 'x';
    const Status other = Menu(edio).test();
    CHECK(other.code == Code::BadReply);
    CHECK(other.where == Where::MenuTest);
    CHECK(text(other) == "N8 menu: unexpected test response 0x78 (is the menu running?)");

    dev.menu = false;  // a running game reads no '*' command and answers nothing
    const Status none = Menu(edio).test();
    CHECK(none.code == Code::Timeout);
    CHECK(text(none) == "Edio: serial read timeout (no N8 response)");
}

TEST_CASE("install sends '*n' and the path, answers the map index, and words the menu's refusals", "[n8][menu]") {
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    dev.mapIndex        = 0x123;
    std::uint16_t index = 0;
    REQUIRE(Menu(edio).install("usb-games/x.nes", &index).ok());
    CHECK(index == 0x123);
    // '*n', then the path as fifoTxString sends one: its length, then its bytes, each a FIFO write of its own.
    CHECK(fifoLog(dev) == std::vector<std::string>{"fifo:2A 6E", "fifo:0F 00",
                                                   "fifo:75 73 62 2D 67 61 6D 65 73 2F 78 2E 6E 65 73"});

    dev.installStatus = 0x44;
    const Status heap = Menu(edio).install("usb-games/x.nes");
    CHECK(heap.code == Code::Device);
    CHECK(heap.device == 0x44);
    CHECK(text(heap, "usb-games/x.nes") ==
          "N8 menu out of memory (0x44) - the menu heap is dirty (e.g. after a prior failed load). Power-cycle the "
          "console to a fresh menu and retry.");

    dev.installStatus = 0x12;
    const Status refused = Menu(edio).install("usb-games/x.nes");
    CHECK(refused.where == Where::Install);
    CHECK(text(refused, "usb-games/x.nes") == "N8 menu: app install error 0x12 (path 'usb-games/x.nes')");
}

TEST_CASE("start boots what the menu installed; vramDump and reset", "[n8][menu]") {
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    for (std::size_t i = 0; i < dev.vram.size(); ++i) dev.vram[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0; i < dev.palette.size(); ++i) dev.palette[i] = static_cast<std::uint8_t>(0xA0 + i);
    std::uint8_t vram[Menu::kVramBytes], palette[Menu::kPaletteBytes];
    REQUIRE(Menu(edio).vramDump(vram, palette).ok());
    CHECK(vram[300] == static_cast<std::uint8_t>(300));
    CHECK(palette[15] == 0xAF);

    REQUIRE(Menu(edio).reset().ok());  // no ack comes, which is fine
    CHECK(dev.resets == 1);

    REQUIRE(Menu(edio).install("usb-games/x.nes").ok());
    REQUIRE(Menu(edio).start().ok());
    CHECK(dev.booted == "usb-games/x.nes");
    CHECK_FALSE(dev.menu);
}

// --- a boot ------------------------------------------------------------------------------------------------------

TEST_CASE("bootRom tests the menu, uploads the ROM and its battery, installs and starts, in that order", "[n8][ops]") {
    FakeN8        dev;
    FakeN8Port    port(dev);
    Edio          edio(port);
    const Bytes   rom = pattern(3000, 1), srm = pattern(8192, 2);
    std::uint16_t index = 0;
    std::vector<std::uint32_t> done;
    std::uint32_t              total = 0;
    auto step = [&](std::uint32_t d, std::uint32_t t) {
        done.push_back(d);
        total = t;
        return true;
    };
    REQUIRE(rp::n8::bootRom(edio, "usb-games/song.nes", {rom.data(), rom.size()}, {srm.data(), srm.size()},
                            rp::n8::Upload::Always, &index, rp::n8::progressOf(step))
                .ok());
    CHECK(index == 69);
    CHECK(dev.files["usb-games/song.nes"] == rom);
    CHECK(dev.files["EDN8/gamedata/song.nes/bram.srm"] == srm);
    CHECK(dev.booted == "usb-games/song.nes");
    CHECK(dev.filesOpen == 0);
    // '*t' first and '*s' last, the files between them (each one write command, not one per block).
    const auto fifo = fifoLog(dev);
    REQUIRE(fifo.size() >= 3);
    CHECK(fifo.front() == "fifo:2A 74");
    CHECK(fifo.back() == "fifo:2A 73");
    CHECK(dev.count("file-wr") == 2);
    // The bytes, of both files, counted together and never backwards, ending at the whole.
    CHECK(total == rom.size() + srm.size());
    CHECK(done.front() == 0);
    CHECK(done.back() == total);
    for (std::size_t i = 1; i < done.size(); ++i) CHECK(done[i] >= done[i - 1]);
}

TEST_CASE("bootRom with no upload boots what the card holds, and an empty battery writes no slot", "[n8][ops]") {
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    REQUIRE(rp::n8::bootRom(edio, "games/held.nes", {}, {}, rp::n8::Upload::Never, nullptr).ok());
    CHECK(dev.booted == "games/held.nes");
    CHECK(dev.count("file-wr") == 0);
    CHECK(dev.files.empty());
}

TEST_CASE("a cancel at each of bootRom's stop points stops it there, and the status names the step", "[n8][ops]") {
    // The calls a 1 KB ROM and a 1 KB battery make: before the test (a stop point), before the ROM's open (one), its
    // block (a report: the cart takes the command's payload whole), before the battery's open (one), its block (a
    // report), before the install (one). Cancelling from call k on stops at the first stop point at or after it.
    struct Expect {
        Step step;
        bool rom, srm;
    };
    const Expect expected[] = {
        {Step::Test, false, false}, {Step::Upload, false, false}, {Step::Save, true, false},
        {Step::Save, true, false},  {Step::Install, true, true},  {Step::Install, true, true},
    };
    const Bytes rom = pattern(1024, 3), srm = pattern(1024, 4);
    for (int k = 1; k <= 7; ++k) {
        INFO("cancel from call " << k);
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        int        calls = 0;
        auto       step  = [&](std::uint32_t, std::uint32_t) { return ++calls < k; };
        const Status s = rp::n8::bootRom(edio, "usb-games/c.nes", {rom.data(), rom.size()},
                                         {srm.data(), srm.size()}, rp::n8::Upload::Always, nullptr,
                                         rp::n8::progressOf(step));
        if (k == 7) {
            CHECK(s.ok());
            CHECK(dev.booted == "usb-games/c.nes");
            continue;
        }
        const Expect& e = expected[k - 1];
        CHECK(s.code == Code::Cancelled);
        CHECK(s.step == e.step);
        CHECK(dev.files.count("usb-games/c.nes") == (e.rom ? 1u : 0u));
        CHECK(dev.files.count("EDN8/gamedata/c.nes/bram.srm") == (e.srm ? 1u : 0u));
        CHECK(dev.booted.empty());
        CHECK(dev.filesOpen == 0);  // a cancel comes between commands: nothing is left open
        CHECK(fifoLog(dev).size() == (e.step == Step::Test ? 0u : 1u));  // '*t' at most: no install, no start
    }
}

TEST_CASE("bootRom names the step that failed, so a caller keeps its policy around it", "[n8][ops]") {
    const Bytes rom = pattern(100, 5);
    {
        FakeN8 dev;
        dev.menu = false;  // a running game: '*t' goes unanswered
        FakeN8Port   port(dev);
        Edio         edio(port);
        const Status s =
            rp::n8::bootRom(edio, "usb-games/g.nes", {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr);
        CHECK(s.code == Code::Timeout);
        CHECK(s.step == Step::Test);
        CHECK(dev.files.empty());  // nothing reached the card
    }
    {
        FakeN8 dev;
        dev.installStatus = 0x44;  // the box re-uploads and retries once on this
        FakeN8Port   port(dev);
        Edio         edio(port);
        const Status s =
            rp::n8::bootRom(edio, "usb-games/g.nes", {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr);
        CHECK(s.code == Code::Device);
        CHECK(s.step == Step::Install);
        CHECK(s.device == 0x44);
        CHECK(dev.files["usb-games/g.nes"] == rom);
        CHECK(dev.booted.empty());
    }
    {
        FakeN8 dev;
        dev.failNextWrite = true;  // the upload's first block refused
        FakeN8Port   port(dev);
        Edio         edio(port);
        const Status s =
            rp::n8::bootRom(edio, "usb-games/g.nes", {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr);
        CHECK(s.code == Code::Device);
        CHECK(s.where == Where::WriteAck);
        CHECK(s.step == Step::Upload);
        CHECK(text(s) == "Edio: tx ack error 0x01");
    }
}

TEST_CASE("bootRom's options: no handshake, a step hook that may use the Edio, and one that stops the boot",
          "[n8][ops]") {
    const Bytes rom = pattern(1500, 6);
    struct Hook {
        Edio*             edio = nullptr;
        std::vector<Step> steps;
        Step              stopAt = Step::None;
    };
    const auto before = [](void* ctx, Step step) {
        Hook& h = *static_cast<Hook*>(ctx);
        h.steps.push_back(step);
        if (step == h.stopAt) return false;
        // Between whole commands the Edio is idle: a file written here lands between the upload and the install, as
        // the Pico box's box.cfg does.
        if (step == Step::Install) {
            const std::uint8_t cfg[4] = {'N', '8', 'P', 'B'};
            REQUIRE(rp::n8::writeFile(*h.edio, "usb-games/pico/box.cfg", {cfg, sizeof cfg}).ok());
        }
        return true;
    };
    {
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        Hook       hook{&edio};
        REQUIRE(rp::n8::bootRom(edio, "usb-games/b.nes", {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr,
                                {}, {false, before, &hook})
                    .ok());
        CHECK(hook.steps == std::vector<Step>{Step::Upload, Step::Install, Step::Start});
        CHECK(dev.booted == "usb-games/b.nes");
        CHECK(dev.files.count("usb-games/pico/box.cfg") == 1);
        // No '*t': the install's '*n' is the first FIFO write, after both files.
        const auto fifo = fifoLog(dev);
        REQUIRE_FALSE(fifo.empty());
        CHECK(fifo.front() == "fifo:2A 6E");
        CHECK(fifo.back() == "fifo:2A 73");
        std::lock_guard<std::mutex> l(dev.mu);
        const auto written = std::count(dev.log.begin(), dev.log.end(), std::string("file-wr"));
        const auto install = std::find(dev.log.begin(), dev.log.end(), std::string("fifo:2A 6E"));
        CHECK(written == 2);
        CHECK(std::count(dev.log.begin(), install, std::string("file-wr")) == 2);
    }
    {
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        Hook       hook{&edio, {}, Step::Install};
        const Status s = rp::n8::bootRom(edio, "usb-games/b.nes", {rom.data(), rom.size()}, {},
                                         rp::n8::Upload::Always, nullptr, {}, {true, before, &hook});
        CHECK(s.code == Code::Cancelled);
        CHECK(s.step == Step::Install);
        CHECK(hook.steps == std::vector<Step>{Step::Test, Step::Upload, Step::Install});
        CHECK(dev.files["usb-games/b.nes"] == rom);
        CHECK(fifoLog(dev) == std::vector<std::string>{"fifo:2A 74"});  // the test alone: no install, no start
    }
    {
        // The box reads a stop at Start as "the N8 went": the install is done, and no '*s' went out after it.
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        Hook       hook{&edio, {}, Step::Start};
        const Bytes  path  = {'u', 's', 'b', '-', 'g', 'a', 'm', 'e', 's', '/', 'b', '.', 'n', 'e', 's'};
        const Status s = rp::n8::bootRom(edio, "usb-games/b.nes", {rom.data(), rom.size()}, {},
                                         rp::n8::Upload::Always, nullptr, {}, {false, before, &hook});
        CHECK(s.code == Code::Cancelled);
        CHECK(s.step == Step::Start);
        CHECK(hook.steps == std::vector<Step>{Step::Upload, Step::Install, Step::Start});
        CHECK(dev.booted.empty());
        const auto fifo = fifoLog(dev);
        REQUIRE(fifo.size() >= 2);
        CHECK(fifo.front() == "fifo:2A 6E");
        CHECK(std::count(fifo.begin(), fifo.end(), std::string("fifo:2A 73")) == 0);
        std::string installed = "fifo:";
        for (std::size_t i = 0; i < path.size(); ++i) {
            char hex[4];
            std::snprintf(hex, sizeof hex, i == 0 ? "%02X" : " %02X", path[i]);
            installed += hex;
        }
        CHECK(fifo.back() == installed);  // the install's path is the last thing sent
    }
    {
        // A battery adds the Save stop point between the upload and the install.
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        Hook       hook{&edio};
        const Bytes  srm = pattern(256, 9);
        REQUIRE(rp::n8::bootRom(edio, "usb-games/b.nes", {rom.data(), rom.size()}, {srm.data(), srm.size()},
                                rp::n8::Upload::Always, nullptr, {}, {false, before, &hook})
                    .ok());
        CHECK(hook.steps == std::vector<Step>{Step::Upload, Step::Save, Step::Install, Step::Start});
        CHECK(dev.files["EDN8/gamedata/b.nes/bram.srm"] == srm);
        CHECK(dev.booted == "usb-games/b.nes");
    }
    {
        // A stop at Save leaves the ROM uploaded and the battery, the install and the start unsent.
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        Hook       hook{&edio, {}, Step::Save};
        const Bytes  srm = pattern(256, 9);
        const Status s = rp::n8::bootRom(edio, "usb-games/b.nes", {rom.data(), rom.size()}, {srm.data(), srm.size()},
                                         rp::n8::Upload::Always, nullptr, {}, {false, before, &hook});
        CHECK(s.code == Code::Cancelled);
        CHECK(s.step == Step::Save);
        CHECK(hook.steps == std::vector<Step>{Step::Upload, Step::Save});
        CHECK(dev.files["usb-games/b.nes"] == rom);
        CHECK(dev.files.count("EDN8/gamedata/b.nes/bram.srm") == 0);
        CHECK(fifoLog(dev).empty());
    }
}

TEST_CASE("bootRom's battery path fits any FatFs name: 255 UTF-16 units of three-byte UTF-8", "[n8][ops]") {
    std::string longest;
    for (int i = 0; i < 255; ++i) longest += "\xE3\x81\x82";  // U+3042: one UTF-16 unit, three bytes
    const Bytes srm = pattern(16, 7);
    FakeN8      dev;
    FakeN8Port  port(dev);
    Edio        edio(port);
    REQUIRE(rp::n8::bootRom(edio, "usb-games/" + longest, {}, {srm.data(), srm.size()}, rp::n8::Upload::Never, nullptr)
                .ok());
    CHECK(dev.files["EDN8/gamedata/" + longest + "/bram.srm"] == srm);

    // One unit more is no FatFs name, and does not fit: refused before anything of the battery is sent (the menu,
    // having started the first boot's game, is past answering '*t', so this one skips it).
    dev.log.clear();
    const Status s = rp::n8::bootRom(edio, "usb-games/" + longest + "\xE3\x81\x82", {}, {srm.data(), srm.size()},
                                     rp::n8::Upload::Never, nullptr, {}, {false});
    CHECK(s.code == Code::TooLarge);
    CHECK(s.step == Step::Save);
    CHECK(dev.log.empty());
}

TEST_CASE("a refusal is decided before the first byte: nothing is written, and the next command starts clean",
          "[n8][ops][refusal]") {
    const std::string huge(0x10000, 'x');  // past the 16-bit length that prefixes a string
    const std::string longName(256, 'n');  // a FatFs name is at most 255 units
    const Bytes       rom = pattern(32, 3), srm = pattern(16, 9);
    // What a refused call must never ask of its caller: a stop point on its Progress, or a step on its before().
    int  asks = 0, steps = 0;
    auto counting = [&](std::uint32_t, std::uint32_t) {
        ++asks;
        return true;
    };
    const rp::n8::Progress asked = rp::n8::progressOf(counting);
    const auto             stepped = [](void* ctx, Step) {
        ++*static_cast<int*>(ctx);
        return true;
    };
    struct Row {
        const char*                  name;
        std::function<Status(Edio&)> call;
        Step                         step;
        Where                        where;
    };
    const Row rows[] = {
        {"Menu::install", [&](Edio& e) { return Menu(e).install(huge); }, Step::None, Where::Length},
        {"readFile",
         [&](Edio& e) { return rp::n8::readFile(e, huge, static_cast<std::uint8_t*>(nullptr), 0, nullptr, asked); },
         Step::Open, Where::Length},
        {"writeFile", [&](Edio& e) { return rp::n8::writeFile(e, huge, {rom.data(), rom.size()}, asked); }, Step::Open,
         Where::Length},
        {"bootRom, a path past the length, with an upload",
         [&](Edio& e) {
             return rp::n8::bootRom(e, huge, {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr, asked);
         },
         Step::Upload, Where::Length},
        {"bootRom, a path past the length, without one",
         [&](Edio& e) { return rp::n8::bootRom(e, huge, {}, {}, rp::n8::Upload::Never, nullptr, asked); },
         Step::Install, Where::Length},
        {"bootRom, a battery whose slot does not fit, which the upload would have preceded",
         [&](Edio& e) {
             return rp::n8::bootRom(e, "usb-games/" + longName + longName + longName, {rom.data(), rom.size()},
                                    {srm.data(), srm.size()}, rp::n8::Upload::Always, nullptr, asked);
         },
         Step::Save, Where::BatteryPath},
        // With the handshake on and a step hook, which the pre-checks must come before: no '*t', no before().
        {"bootRom, a path past the length, with the handshake and a step hook",
         [&](Edio& e) {
             return rp::n8::bootRom(e, huge, {rom.data(), rom.size()}, {}, rp::n8::Upload::Always, nullptr, asked,
                                    {true, stepped, &steps});
         },
         Step::Upload, Where::Length},
        {"bootRom, a battery whose slot does not fit, with the handshake and a step hook",
         [&](Edio& e) {
             return rp::n8::bootRom(e, "usb-games/" + longName + longName + longName, {rom.data(), rom.size()},
                                    {srm.data(), srm.size()}, rp::n8::Upload::Always, nullptr, asked,
                                    {true, stepped, &steps});
         },
         Step::Save, Where::BatteryPath},
    };
    // A count past 32 bits: the pointer is never read, the refusal comes first. Where size_t is 32 bits no such size
    // exists, and the call is simply not made.
    if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
        const std::size_t big = (std::size_t{1} << 32);  // one past what the wire's count holds
        std::uint8_t      scratch[4] = {};
        const Row         sized[] = {
            {"memWR", [&](Edio& e) { return e.memWR(0x1800000, scratch, big); }, Step::None, Where::Size},
            {"memRD", [&](Edio& e) { return e.memRD(0x1800000, scratch, big); }, Step::None, Where::Size},
            {"fifoWR", [&](Edio& e) { return e.fifoWR(scratch, big); }, Step::None, Where::Size},
            {"fileWrite", [&](Edio& e) { return e.fileWrite(scratch, big); }, Step::None, Where::Size},
            {"fileRead", [&](Edio& e) { return e.fileRead(scratch, big); }, Step::None, Where::Size},
            {"writeFile", [&](Edio& e) { return rp::n8::writeFile(e, "a/b.bin", {scratch, big}); }, Step::Open,
             Where::Size},
            {"bootRom, a ROM past the count",
             [&](Edio& e) { return rp::n8::bootRom(e, "usb-games/g.nes", {scratch, big}, {}, rp::n8::Upload::Always, nullptr); },
             Step::Upload, Where::Size},
            {"bootRom, a battery past the count",
             [&](Edio& e) {
                 return rp::n8::bootRom(e, "usb-games/g.nes", {}, {scratch, big}, rp::n8::Upload::Never, nullptr);
             },
             Step::Save, Where::Size},
            {"bootRom, a ROM and a battery that together pass it",
             [&](Edio& e) {
                 const std::size_t half = big / 2 + 1;
                 return rp::n8::bootRom(e, "usb-games/g.nes", {scratch, half}, {scratch, half}, rp::n8::Upload::Always,
                                        nullptr);
             },
             Step::Upload, Where::Size},
        };
        for (const Row& r : sized) {
            INFO(r.name);
            FakeN8     dev;
            FakeN8Port port(dev);
            Edio       edio(port);
            const Status s = r.call(edio);
            CHECK(s.code == Code::TooLarge);
            CHECK(s.where == r.where);
            CHECK(s.step == r.step);
            CHECK(dev.writes == 0);
            CHECK(dev.log.empty());
            CHECK(text(s) == "Edio: more than 4294967295 bytes in one command");
            CHECK(Menu(edio).test().ok());
            CHECK(fifoLog(dev) == std::vector<std::string>{"fifo:2A 74"});
        }
        CHECK(Edio::countable(0xFFFFFFFFu));
        CHECK_FALSE(Edio::countable(big));
    }
    for (const Row& r : rows) {
        INFO(r.name);
        FakeN8     dev;
        FakeN8Port port(dev);
        Edio       edio(port);
        asks = steps = 0;
        const Status s = r.call(edio);
        CHECK(s.code == Code::TooLarge);
        CHECK(s.where == r.where);
        CHECK(s.step == r.step);
        CHECK(dev.writes == 0);
        CHECK(dev.log.empty());
        CHECK(asks == 0);
        CHECK(steps == 0);
        // The next command starts clean: nothing of the refused one precedes its frame.
        CHECK(Menu(edio).test().ok());
        CHECK(fifoLog(dev) == std::vector<std::string>{"fifo:2A 74"});
    }
}

TEST_CASE("a battery path that does not fit is worded as the ROM's name, not as a long string", "[n8][ops]") {
    Status s;
    s.code  = Code::TooLarge;
    s.where = Where::BatteryPath;
    s.step  = Step::Save;
    CHECK(text(s, "usb-games/x.nes") == "Edio: the ROM's file name is too long for its battery slot (usb-games/x.nes)");
}

// --- files -------------------------------------------------------------------------------------------------------

TEST_CASE("writeFile and readFile round-trip a file; NotFound, TooLarge with the size, and a cancel that closes",
          "[n8][ops]") {
    FakeN8      dev;
    FakeN8Port  port(dev);
    Edio        edio(port);
    const Bytes data = pattern(1300, 6);
    REQUIRE(rp::n8::writeFile(edio, "d/f.bin", {data.data(), data.size()}).ok());
    CHECK(dev.files["d/f.bin"] == data);

    Bytes       buf(4096);
    std::size_t size = 0;
    REQUIRE(rp::n8::readFile(edio, "d/f.bin", buf.data(), buf.size(), &size).ok());
    CHECK(size == data.size());
    CHECK(Bytes(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(size)) == data);
    CHECK(dev.count("file-rd") == 3);  // 512, 512, 276: one CMD_F_FRD a block
    CHECK(dev.filesOpen == 0);

    const Status missing = rp::n8::readFile(edio, "d/none.bin", buf.data(), buf.size(), &size);
    CHECK(missing.code == Code::NotFound);
    CHECK(text(missing, "d/none.bin") == "Edio: file not found on SD: d/none.bin");

    const Status small = rp::n8::readFile(edio, "d/f.bin", buf.data(), 100, &size);
    CHECK(small.code == Code::TooLarge);
    CHECK(size == data.size());  // what a caller needs to know to try again
    CHECK(dev.filesOpen == 0);   // nothing was opened

    int  blocks = 0;
    auto step   = [&](std::uint32_t done, std::uint32_t) { return done == 0 || ++blocks < 1; };
    const Status cancelled =
        rp::n8::readFile(edio, "d/f.bin", buf.data(), buf.size(), &size, rp::n8::progressOf(step));
    CHECK(cancelled.code == Code::Cancelled);
    CHECK(cancelled.step == Step::Data);
    CHECK(dev.filesOpen == 0);  // the cancel came between two blocks, and closed the file
}

TEST_CASE("the cart-SRAM paths: a battery written straight to SRAM reads back, and a dump reads all of it",
          "[n8][ops]") {
    FakeN8      dev;
    FakeN8Port  port(dev);
    Edio        edio(port);
    const Bytes save = pattern(8192, 7);
    REQUIRE(edio.memWR(static_cast<std::uint32_t>(Edio::ADDR_SRM), save.data(), save.size()).ok());
    Bytes back(save.size());
    REQUIRE(edio.memRD(static_cast<std::uint32_t>(Edio::ADDR_SRM), back.data(), back.size()).ok());
    CHECK(back == save);
    Bytes whole(Edio::SIZE_SRM_GAME);
    REQUIRE(edio.memRD(static_cast<std::uint32_t>(Edio::ADDR_SRM), whole.data(), whole.size()).ok());
    CHECK(Bytes(whole.begin(), whole.begin() + 8192) == save);
    CHECK(dev.count("srm-wr") == 1);
    CHECK(dev.count("srm-rd") == 2);
}

TEST_CASE("free space, mkdir (an existing directory included) and delete over the cart", "[n8][ops]") {
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    dev.freeBytes       = (std::uint64_t{3} << 32) + 5;
    std::uint64_t bytes = 0;
    REQUIRE(edio.freeSpace(bytes).ok());
    CHECK(bytes == (std::uint64_t{3} << 32) + 5);

    REQUIRE(edio.dirMake("songs").ok());
    REQUIRE(edio.dirMake("songs").ok());  // "exists" is done
    REQUIRE(edio.dirMake("songs/new").ok());
    std::vector<std::string> dirs;
    REQUIRE(edio.listDir("songs", [&](const rp::n8::DirEntry& e) {
                    if (e.isDir) dirs.push_back(std::string(e.name));
                }).ok());
    CHECK(dirs == std::vector<std::string>{"new"});

    const Bytes one = {1};
    REQUIRE(rp::n8::writeFile(edio, "songs/a.sav", {one.data(), one.size()}).ok());
    REQUIRE(edio.fileDelete("songs/a.sav").ok());
    CHECK(dev.files.count("songs/a.sav") == 0);
    const Status gone = edio.fileDelete("songs/a.sav");
    CHECK(gone.code == Code::Device);
    CHECK(gone.device == 0x04);  // FatFs FR_NO_FILE
    CHECK(text(gone) == "Edio: operation error 0x04");
}
