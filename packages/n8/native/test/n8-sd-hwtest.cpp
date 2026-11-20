// n8-sd-hwtest - a manual hardware harness for the cart's capability jobs (the UI drives the same jobs from its cart
// menu, under Settings > N8 Pro). It constructs the SAME native N8Host the cart node hands a job to, but against a real
// /dev/ttyACM0, and brings its link up as Settings > N8's Connect row would (a job is refused while the link is down),
// so a hardware pass exercises the same N8Host job path the cart node reaches (not the CLI's TS stack).
// EXCLUDE_FROM_ALL, built by name (retroplug-n8-hwtest); NOT a CI target. Needs a real N8 plugged in and at its menu
// for the battery jobs and the upload.
//
//   retroplug-n8-hwtest dump    <rom-name> <dest.srm>  [port]  # the battery file to a local file (non-destructive)
//   retroplug-n8-hwtest load    <rom.nes>              [port]  # upload + boot a ROM (the cart must be at its menu)
//   retroplug-n8-hwtest restore <rom-name> <save.srm>  [port]  # write a .srm as the battery file the game loads next
//
// A battery job's <rom-name> is a bare file name, not a path: it names the ROM whose folder under EDN8/gamedata/ holds
// the battery file, which is why `dump /roms/mGB.nes out.srm` is refused.
//
// The rest take the port directly, with no N8Host and no capability job. link and timing bring up N8Link's own
// streaming thread; the others, linkdbg included, drive a bare Edio:
//   retroplug-n8-hwtest peek     <addr-hex> <len> [port]       # CMD_MEM_RD: read bytes from a device address
//   retroplug-n8-hwtest poke     <addr-hex> <byte> [port]      # CMD_MEM_WR: write one byte to a device address
//   retroplug-n8-hwtest read     <sd-path> <local-dest> [port] # CMD_F_FRD: read an SD file over USB
//   retroplug-n8-hwtest write    <local-src> <sd-path> [port]  # CMD_F_FWR: write a local file to the SD card
//   retroplug-n8-hwtest fiford   <count> [timeout-ms] [port]   # read the cart's USB back-channel (CMD_USB_WR)
//   retroplug-n8-hwtest link     <steps> [loss-permille] [port] # BlipToaster's reliable link, over N8Link
//   retroplug-n8-hwtest menuguard <seconds> [port] [raw] # the menu guard, streaming while a person presses RESET
//   retroplug-n8-hwtest menuprobe <test|sysex|note> [port] # what the N8 OS menu does with bytes it did not ask for
//   retroplug-n8-hwtest bootwindow <delay-ms> [stream-ms] [port]  # raw notes from <delay-ms> after a RESET
//   retroplug-n8-hwtest menuwatch <seconds> [quiet|probe|stream] [port] # the menu probed as the guard does: answers, misses
//   retroplug-n8-hwtest pausereset <gap-ms> [after-s] [loss-permille] [port] # a RESET between a framed stream's notes
//   retroplug-n8-hwtest cfgwatch <seconds> [every-ms] [port]   # the FPGA config block, each change timed (no FIFO write)
//   retroplug-n8-hwtest linkdbg  [port]                        # the link step by hand: sync, frames, acks, APU mirror
//   retroplug-n8-hwtest timing   <idle|busy> [steps] [--raw] [--midi <port>]  # latency / jitter / load, real console
//   retroplug-n8-hwtest vramdump <out.bin> [port]              # menu '*v': dump VRAM+palette+CHR (screenshot)
//   retroplug-n8-hwtest sniff    [port]                        # memRD ADDR_SSR: a running game's live APU/PPU/OAM
//   retroplug-n8-hwtest memwr    <addr-hex> <file> [port]      # block memWR + verify (live-patch CHR/PRG)
//   retroplug-n8-hwtest info     [port]                        # CMD_SYS_INF + CMD_GET_VDC: serial/versions/form/volts
//   retroplug-n8-hwtest fstest   [port]                        # CMD_F_AVB/DIR_MK/DEL: free space + scratch mkdir/rm
//
// fiford is the read half of fifowr, and the only op that does NOT handshake first: Edio::connect starts with
// flushInput, which would throw away bytes a running ROM had already queued. So it opens the port and reads,
// and a wrong/absent port simply times out rather than reporting "no device".
//
// peek/poke reach FPGA config regs like the expansion-audio master volume at 0x1800023 (MapConfig.master_vol = scfg[3];
// see krikzz edn8-pro-pub edio/everdrive.h + fpga/base_sv/sys_cfg.sv). 0x1800023 <- 0..255, where 128 = unity gain.
// read pulls a whole SD file (e.g. EDN8/sysdata/registry.bin) to a local file.
//
// Exit 0 on success, 1 on the op's error, 2 on a usage / no-device problem.
#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cstdint>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "FakeJobs.hpp"  // a job record and request of the pack's own, as an engine would hand the cart
#include "n8/Edio.hpp"
#include "n8/Menu.hpp"
#include "n8/Ops.hpp"
#include "n8/desktop/N8Host.hpp"
#include "n8/desktop/N8Link.hpp"
#include "n8/desktop/SerialTransport.hpp"

using namespace retroplug;

namespace {

// The bare-Edio ops below are each written around a throw (an op reports its failure and exits): the pack's Edio, Menu
// and Ops behind the calls they make, each throwing a failed Status's text (failureText, the port's own words or
// describe()'s), so an op reads as a short script.
struct Edio : rp::n8::Edio {
    using rp::n8::Edio::Edio;

    void connect() { ok(rp::n8::Edio::connect()); }
    void setReadTimeout(int ms) { timeouts().readMs = static_cast<std::uint32_t>(ms); }
    void readData(std::uint8_t* out, std::size_t n) { ok(rp::n8::Edio::readData(out, n)); }
    void memRD(std::uint32_t addr, std::uint8_t* out, std::size_t n) { ok(rp::n8::Edio::memRD(addr, out, n)); }
    void memWR(std::uint32_t addr, const std::uint8_t* d, std::size_t n) { ok(rp::n8::Edio::memWR(addr, d, n)); }
    void fifoWR(const std::uint8_t* d, std::size_t n) { ok(rp::n8::Edio::fifoWR(d, n)); }
    void dirMake(const std::string& path) { ok(rp::n8::Edio::dirMake(path), path); }
    void fileDelete(const std::string& path) { ok(rp::n8::Edio::fileDelete(path), path); }
    std::uint64_t freeSpace() {
        std::uint64_t bytes = 0;
        ok(rp::n8::Edio::freeSpace(bytes));
        return bytes;
    }
    std::vector<std::uint8_t> sysInfo() {
        std::uint8_t b[SIZE_SYS_INFO];
        ok(rp::n8::Edio::sysInfo(b));
        return {b, b + sizeof b};
    }
    std::vector<std::uint8_t> vdc() {
        std::uint8_t b[SIZE_VDC];
        ok(rp::n8::Edio::vdc(b));
        return {b, b + sizeof b};
    }
    std::vector<std::uint8_t> readFile(const std::string& path) {
        std::vector<std::uint8_t> out;
        const rp::n8::FileSink    sink{[](void* ctx, std::size_t size) {
                                        auto& v = *static_cast<std::vector<std::uint8_t>*>(ctx);
                                        v.resize(size);
                                        return v.data();
                                    },
                                    &out};
        std::size_t size = 0;
        ok(rp::n8::readFile(*this, path, sink, &size), path);
        out.resize(size);
        return out;
    }
    void writeFile(const std::string& path, const std::vector<std::uint8_t>& data) {
        ok(rp::n8::writeFile(*this, path, {data.data(), data.size()}), path);
    }

    void ok(const rp::n8::Status& s, std::string_view path = {}) {
        if (!s.ok()) throw std::runtime_error(failureText(s, *this, path));
    }
};

struct N8VramDump {
    std::vector<std::uint8_t> vram, palette;
};

struct N8Menu {
    explicit N8Menu(Edio& edio) : edio_(edio), menu_(edio) {}
    void       test() { edio_.ok(menu_.test()); }
    N8VramDump vramDump() {
        std::uint8_t vram[rp::n8::Menu::kVramBytes], palette[rp::n8::Menu::kPaletteBytes];
        edio_.ok(menu_.vramDump(vram, palette));
        return {{vram, vram + sizeof vram}, {palette, palette + sizeof palette}};
    }

    Edio&        edio_;
    rp::n8::Menu menu_;
};

// One line of a watch's log: the time on its clock, then the event.
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void sayAt(double ms, const char* fmt, ...) {
    std::printf("%10.1f ms  ", ms);
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    va_end(a);
    std::printf("\n");
    std::fflush(stdout);
}

// A whole local file into a byte vector. Empty on a missing/unreadable/empty file — every caller here
// treats "nothing to send" as an error, so the two cases don't need telling apart.
std::vector<std::uint8_t> slurpLocal(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::vector<std::uint8_t> data;
    std::uint8_t              chunk[4096];
    for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), f)) > 0;)
        data.insert(data.end(), chunk, chunk + got);
    std::fclose(f);
    return data;
}

}  // namespace

#if defined(RP_N8_TIMING)
int runTiming(int argc, char** argv, const std::string& defaultPort);   // n8-timing.cpp
#endif
int main(int argc, char** argv) {
    if (argc < 3) {
        // `sniff`/`info`/`fstest` need no path/addr (they auto-detect the port), so allow a bare argc==2.
        const std::string a1 = argc >= 2 ? argv[1] : "";
        if (!(argc == 2 && (a1 == "sniff" || a1 == "info" || a1 == "fstest" || a1 == "linkdbg"))) {
            std::fprintf(stderr,
                         "usage: %s dump <rom-name> <dest.srm> [port]\n"
                         "       %s load <rom.nes> [port]\n"
                         "       %s restore <rom-name> <save.srm> [port]\n"
                         "       %s <peek|poke|read|write|vramdump|sniff|memwr|fifowr|fiford|link|menuguard|menuwatch|pausereset|cfgwatch|linkdbg|"
                         "timing|info|fstest> <path|addr|count> [len|byte|dest] [port]\n",
                         argv[0], argv[0], argv[0], argv[0]);
            return 2;
        }
    }
    const std::string op = argv[1];

    // Bare-Edio SD file read (CMD_F_FRD): pull a whole SD file over USB to a local file. Read-only, so safe on
    // a running game (exercises the C++ Edio::readFile twin of the CLI's TS readFile).
    if (op == "read") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s read <sd-path> <local-dest> [port]\n", argv[0]);
            return 2;
        }
        const std::string sdPath = argv[2];
        const std::string dest   = argv[3];
        const std::string pport  = argc > 4 ? argv[4] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport                 sp(pport);
            Edio                            edio(sp);
            edio.connect();  // handshake (works whether the menu or a game is running)
            const std::vector<std::uint8_t> bytes = edio.readFile(sdPath);
            std::FILE*                      f     = std::fopen(dest.c_str(), "wb");
            if (!f) {
                std::fprintf(stderr, "cannot write %s\n", dest.c_str());
                return 1;
            }
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            std::printf("read %zu bytes of %s -> %s\n", bytes.size(), sdPath.c_str(), dest.c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "read failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio SD file write (CMD_F_FWR): push a local file onto the card, creating parent dirs. The
    // supported way to put a DATA file on the SD over USB — the alternative was smuggling it through the
    // per-game save slot (--srm), which is a fixed path capped at 64 KB.
    if (op == "write") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s write <local-src> <sd-path> [port]\n", argv[0]);
            return 2;
        }
        const std::string src    = argv[2];
        const std::string sdPath = argv[3];
        const std::string pport  = argc > 4 ? argv[4] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        const std::vector<std::uint8_t> data = slurpLocal(src);
        if (data.empty()) {
            std::fprintf(stderr, "cannot read %s (or it is empty)\n", src.c_str());
            return 1;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            edio.writeFile(sdPath, data);
            std::printf("wrote %zu bytes of %s -> %s\n", data.size(), src.c_str(), sdPath.c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "write failed: %s\n", e.what());
            return 1;
        }
    }

    // The read half of fifowr: bytes the RUNNING ROM sent host-ward with CMD_USB_WR, which the MCU
    // forwards straight out of the USB port (edn8-pro-pub edio/everdrive.c `ed_cmd_usb_wr`; fifo_b in
    // fpga/base_sv/base_io.sv, whose empty flag the ROM reads as $40F1 bit 6). This is the back-channel
    // that lets a host streaming data IN pace itself to what the ROM has actually consumed.
    //
    // Deliberately NO connect() handshake: it begins with flushInput, which would discard bytes already
    // queued by the ROM. The cost is that a wrong port just times out instead of saying "no device".
    // BlipToaster's MIDI latency / jitter / latency-under-load on the real console, timed against a timestamped
    // capture of its audio (n8-timing.cpp).
    if (op == "timing") {
#if defined(RP_N8_TIMING)
        return runTiming(argc, argv, findN8Port());
#else
        std::fprintf(stderr, "timing needs ALSA (this build has none)\n");
        return 2;
#endif
    }

    // Diagnostic for the reliable link on hardware: one port, every step by hand - sync, read the back-channel,
    // frames, the APU mirror after each - to see where a frame stops landing. It polls the back-channel the way
    // N8Link does (0 ms reads), which is what found the read timeout leaking into writes (SerialTransport::read):
    // a healthy run acks F5 50, F5 51, F5 52 and every memRD answers.
    if (op == "linkdbg") {
        const std::string pport = argc > 2 ? argv[2] : findN8Port();
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            auto apu = [&](const char* tag) {
                std::vector<std::uint8_t> ssr(0x200);
                edio.memRD(Edio::ADDR_SSR, ssr.data(), ssr.size());
                std::printf("%-28s $4000=%02X $400C=%02X\n", tag, ssr[0x080], ssr[0x08C]);
            };
            auto drain = [&](const char* tag, int ms) {
                std::uint8_t b[64];
                std::printf("%-28s rx:", tag);
                const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                while (std::chrono::steady_clock::now() < until) {
                    const std::size_t n = sp.read(b, sizeof b, 0);   // N8Link's own polling: 0 ms reads
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    for (std::size_t i = 0; i < n; ++i) std::printf(" %02X", b[i]);
                }
                std::printf("\n");
            };
            auto frame = [](std::uint8_t seq, std::initializer_list<std::uint8_t> msg) {
                std::vector<std::uint8_t> f = { 0xF5, seq };
                unsigned sum = seq;
                for (auto m : msg) { f.push_back(m); sum += m; }
                f.push_back(static_cast<std::uint8_t>(sum & 0x7F));
                return f;
            };
            apu("start");
            const std::uint8_t sync[2] = { 0xF4, 0x50 };
            edio.fifoWR(sync, 2);
            drain("after sync", 100);
            auto a = frame(0x50, { 0x90, 0x48, 0x78 });   // pulse1 C5, vol 15
            edio.fifoWR(a.data(), a.size());
            drain("after frame 50 (note-on)", 100);
            apu("after frame 50");
            auto b = frame(0x51, { 0x80, 0x48, 0x00 });   // pulse1 off
            edio.fifoWR(b.data(), b.size());
            apu("after frame 51, no read");
            drain("then read", 100);
            apu("end");
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "linkdbg failed: %s\n", e.what());
            return 1;
        }
    }

    // The reliable link on real hardware, through N8Link - the code the plugin streams to a cart with. Plays
    // <steps> 16th-note hats (ch4 noise) at 170 bpm, each released half a step later, dropping
    // <loss-permille> of the framed bytes on the way out in 1-3 byte bursts (what an NTSC DMC-DMA halt does at the
    // cart; a PAL console loses nothing, so the recovery has to be provoked). Reports whether the ROM answered the
    // probe, the re-sends, the fastest round trip, and whether every frame was acknowledged. Record the audio
    // alongside to count the hats that actually sounded (retroplug-cli/repro/capture-onsets.ts in BlipToaster).
    if (op == "link") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: %s link <steps> [loss-permille] [port]\n", argv[0]);
            return 2;
        }
        const int         steps = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        const int         loss  = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 0;
        const std::string lport = argc > 4 ? argv[4] : findN8Port();
        if (lport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        retroplug::N8Link link([](const std::string& p) -> std::unique_ptr<PortTransport> {
            return std::make_unique<SerialTransport>(p);
        });
        if (!link.connect(lport)) {
            std::fprintf(stderr, "link: connect failed: %s\n", link.lastError().c_str());
            return 1;
        }
        link.setLookaheadMs(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));   // the probe (~100 ms)
        std::printf("link: %s\n", link.reliable() ? "reliable (the ROM answered the probe)" : "raw (no answer)");
        link.setTestLossPermille(loss);
        const auto stepDur = std::chrono::microseconds(static_cast<long>(60e6 / 170 / 4));
        const auto t0      = std::chrono::steady_clock::now();
        const std::uint8_t on[]  = {0x93, 60, 120};
        const std::uint8_t off[] = {0x83, 60, 0};
        for (int i = 0; i < steps; ++i) {   // a half-step hat, so a capture can count the hits
            std::this_thread::sleep_until(t0 + stepDur * i);
            link.push(0, on, sizeof on, 48000.0);
            std::this_thread::sleep_until(t0 + stepDur * i + stepDur / 2);
            link.push(0, off, sizeof off, 48000.0);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (link.linkOutstanding() > 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const unsigned long long outstanding = link.linkOutstanding();
        std::printf("link: %d hats, loss %d/1000, re-sends %llu, outstanding %llu, fastest round trip %.2f ms, %llu bytes\n",
                    steps, loss, static_cast<unsigned long long>(link.linkResends()), outstanding,
                    static_cast<double>(link.linkRttMinNs()) / 1e6,
                    static_cast<unsigned long long>(link.bytesForwarded()));
        const std::string err = link.lastError();
        link.disconnect();
        if (!err.empty()) {
            std::fprintf(stderr, "link: %s\n", err.c_str());
            return 1;
        }
        return outstanding == 0 ? 0 : 1;
    }

    // The menu guard on real hardware (spec/07 "The N8 menu's residual window"): N8Host's own link, so its MenuCheck is
    // the production one, streaming a short 2A03 note every 60 ms for <seconds> while a person presses the console's
    // RESET button (it keeps USB and the link up and drops the cart to its menu: the case a power-cycle cannot make).
    // Logs every change of the link's state with its time, then what was written after the guard tripped (expected: 0
    // bytes) and how long the trip took after the cart stopped acknowledging. Boot a game with the reliable link
    // (BlipToaster) first.
    // `raw` holds the link raw (no link probe, so BlipToaster gets plain MIDI): the raw path, whose only reset signal
    // is the guard's own probe cadence.
    // How long the N8 OS is fragile after a console RESET (the menu-guard design's raw-link limit): streams framed
    // through N8Host's link to a game with the reliable link (BlipToaster) until a person presses RESET, which shows as
    // the window standing still; then disconnects at once, so nothing more goes out, waits until <delay-ms> after the
    // window stopped (when the console went away, near enough; the stall is seen 100 ms later, so a delay under that
    // cannot be taken), and writes one raw note-on and its note-off straight to the FIFO. Read the screen after it: an
    // ERROR:40 means a raw note at that delay breaks the OS's boot.
    if (op == "bootwindow") {
        const int         delayMs = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        // [stream-ms]: keep writing the note every 60 ms for that long, the raw stream's shape, rather than once.
        const int         streamMs = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 0;
        // [clock]: the stream is MIDI clock, one F8 every 20 ms (24 a quarter at 120 bpm, risa's sync), not notes.
        const bool        clock    = std::string(argv[argc - 1]) == "clock";
        const std::string bport    = argc > 4 && std::string(argv[4]) != "clock" ? argv[4] : findN8Port();
        if (delayMs <= 0 || bport.empty()) {
            std::fprintf(stderr, "usage: %s bootwindow <delay-ms> [stream-ms] [port] (no Everdrive N8 found?)\n",
                         argv[0]);
            return 2;
        }
        N8Host host(
            [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); },
            [] { return std::vector<N8PortDto>{}; }, "/tmp");
        host.setPort(bport);
        host.connect(true);
        N8Link& link = host.link();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the connect seed and the probe
        if (!link.isConnected() || !link.reliable()) {
            std::fprintf(stderr, "bootwindow: needs a game with the reliable link (connected=%d reliable=%d)\n",
                         link.isConnected(), link.reliable());
            return 1;
        }
        using Clock = std::chrono::steady_clock;
        const auto         t0 = Clock::now();
        const auto         ms = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
        const std::uint8_t on[]  = {0x90, 69, 100};
        const std::uint8_t off[] = {0x80, 69, 0};
        std::printf("bootwindow: streaming framed; press RESET\n");
        std::fflush(stdout);
        double outstandingSince = -1, resetMs = -1;
        for (int step = 0; resetMs < 0 && ms() < 1800000.0; ++step) {
            if (step % 6 == 0) link.push(0, on, sizeof on, 48000.0);
            if (step % 6 == 3) link.push(0, off, sizeof off, 48000.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (link.linkOutstanding() == 0) outstandingSince = -1;
            else if (outstandingSince < 0) outstandingSince = ms();
            else if (ms() - outstandingSince >= 100.0) resetMs = outstandingSince;
        }
        const double seenMs = ms();
        host.connect(false);   // nothing more of the stream, and no guard probe, from here
        if (resetMs < 0) {
            std::printf("bootwindow: no reset seen\n");
            return 1;
        }
        std::this_thread::sleep_until(
            t0 + std::chrono::microseconds(static_cast<long long>((resetMs + delayMs) * 1000)));
        const double noteMs = ms();
        try {
            SerialTransport sp(bport);
            rp::n8::Edio    edio(sp);
            const auto streamEnd = Clock::now() + std::chrono::milliseconds(streamMs);
            const std::uint8_t tick[] = {0xF8};
            do {
                if (clock) {
                    edio.fifoWR(tick, sizeof tick);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                edio.fifoWR(on, sizeof on);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                edio.fifoWR(off, sizeof off);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            } while (Clock::now() < streamEnd);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "bootwindow: the note could not be written: %s\n", e.what());
            return 1;
        }
        std::printf("bootwindow: reset at %.1f ms (seen %.1f ms later); the raw note went out %.1f ms after it\n",
                    resetMs, seenMs - resetMs, noteMs - resetMs);
        return 0;
    }

    // The menu guard's question put to a cart for a while, on a bare Edio with the guard's own decisions
    // (rp::N8MenuGuard, as a raw link runs it): what the menu answers, how fast, what it says that nobody asked for, and,
    // with `stream`, what a raw stream writes into a menu the guard left. spec/07's back-channel and residual-window
    // checks: run it at the menu while a person browses, opens folders and loads ROMs from the cart's own browser.
    //   quiet:  no probe at all, nothing written; every byte the back-channel says is logged (an idle menu: none)
    //   probe:  the guard's probes, every 250 ms whatever it decides, nothing else written (the default)
    //   stream: the guard as a raw link runs it, an A4 every 60 ms (no 0x2A in it) while it says the cart is no menu
    if (op == "menuwatch") {
        const int         seconds = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        const std::string mode    = argc > 3 ? argv[3] : "probe";
        const std::string wport   = argc > 4 ? argv[4] : findN8Port();
        if (seconds <= 0 || wport.empty() || (mode != "quiet" && mode != "probe" && mode != "stream")) {
            std::fprintf(stderr, "usage: %s menuwatch <seconds> [quiet|probe|stream] [port] (no Everdrive N8 found?)\n",
                         argv[0]);
            return 2;
        }
        using Clock = std::chrono::steady_clock;
        try {
            SerialTransport sp(wport);
            Edio            edio(sp);
            edio.connect();
            sp.flushInput();
            const auto   t0  = Clock::now();
            const auto   now = [&] { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count() + 1; };
            const auto   ms  = [](std::int64_t ns) { return static_cast<double>(ns) / 1e6; };
            rp::N8MenuGuard        guard;
            rp::N8MenuGuard::Scan  scan;   // the guard's own scan, run again to time each 'k' it hears
            const bool   quiet  = mode == "quiet";
            const bool   stream = mode == "stream";
            const std::uint8_t on[]  = {0x90, 69, 100};
            const std::uint8_t off[] = {0x80, 69, 0};

            // Each probe waits in `pending` for its 'k'. A 'k' is the newest probe's while that one is unanswered (a
            // menu that swallowed an earlier probe answers the next at once, and must not be read as answering late),
            // else the oldest waiting one's (a busy menu answering its backlog). A 'k' with none waiting is one nobody
            // asked for; a probe still waiting after 2 s was swallowed. The longest gap between two 'k's (250 ms on a
            // healthy menu, which answers each probe) is the menu's longest silence, whatever the matching.
            std::deque<std::int64_t> pending;
            std::int64_t  nextNoteNs = 150'000'000, offNs = 0, leaveNs = 0, lastKNs = 0, worstKGapAtNs = 0;
            bool          noteHeld = false;
            std::int64_t  lastProbeNs = 0;
            std::uint64_t probes = 0, answers = 0, slow = 0, lost = 0, acks = 0, unasked = 0, otherBytes = 0, leaves = 0, enters = 0,
                          streamBytes = 0, bytesAtLeave = 0, probesAtLeave = 0, worstLeakBytes = 0;
            std::vector<double> latencies;
            double        longestKGapMs = 0;   // the longest between two 'k's
            sayAt(ms(now()), "menuwatch: %s, %d s on %s", mode.c_str(), seconds, wport.c_str());
            std::int64_t nextTickNs = 5'000'000'000;
            while (now() < std::int64_t(seconds) * 1'000'000'000) {
                std::uint8_t      buf[64];
                const std::size_t n = sp.read(buf, sizeof buf, 0);
                const std::int64_t t = now();
                while (!pending.empty() && t - pending.front() >= 2'000'000'000) {
                    ++lost;
                    sayAt(ms(t), "SWALLOWED: the probe at %.1f ms was never answered", ms(pending.front()));
                    pending.pop_front();
                }
                for (std::size_t i = 0; i < n; ++i) {
                    const bool inAck = buf[i] == rp::N8ReliableSender::FRAME || scan.state != 0;
                    const auto heard = scan.feed(buf[i]);
                    if (heard == rp::N8MenuGuard::Scan::Heard::Ack) ++acks;   // a game with the link: it drops the probe
                    if (heard != rp::N8MenuGuard::Scan::Heard::MenuAnswer) {
                        if (!inAck) {
                            ++otherBytes;
                            sayAt(ms(t), "heard 0x%02X (neither a 'k' nor an ack)", buf[i]);
                        }
                        continue;
                    }
                    if (pending.empty()) {
                        ++unasked;
                        sayAt(ms(t), "UNASKED 'k': no probe waiting");
                        continue;
                    }
                    std::int64_t asked;
                    if (pending.back() == lastProbeNs) {
                        asked = pending.back();
                        pending.pop_back();
                    } else {
                        asked = pending.front();
                        pending.pop_front();
                    }
                    const double lat = ms(t - asked);
                    ++answers;
                    latencies.push_back(lat);
                    if (lastKNs && ms(t - lastKNs) > longestKGapMs) {
                        longestKGapMs = ms(t - lastKNs);
                        worstKGapAtNs = lastKNs;
                    }
                    lastKNs = t;
                    if (lat >= ms(rp::N8MenuGuard::kAnswerNs)) {
                        ++slow;
                        sayAt(ms(t), "slow 'k': %.1f ms after its probe (%zu more waiting; the guard counts a miss past "
                                     "100 ms)", lat, pending.size());
                    }
                }
                if (quiet) {
                    if (n == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                guard.hear(buf, n);
                const rp::N8MenuGuard::Turn turn = guard.turn(t, nullptr, true);
                if (turn.enteredMenu) {
                    ++enters;
                    noteHeld = false;   // as N8Link drops a pending release at the menu
                    if (leaveNs) {
                        const std::uint64_t leaked = streamBytes - bytesAtLeave;
                        worstLeakBytes = std::max(worstLeakBytes, leaked);
                        sayAt(ms(t), "AT THE MENU again, %.1f ms after the leave: %llu stream byte(s) and %llu probe(s) "
                                     "went into it", ms(t - leaveNs), static_cast<unsigned long long>(leaked),
                              static_cast<unsigned long long>(probes - probesAtLeave));
                        leaveNs = 0;
                    } else {
                        sayAt(ms(t), "AT THE MENU (the guard's state)");
                    }
                }
                if (turn.leftMenu) {
                    ++leaves;
                    leaveNs       = t;
                    bytesAtLeave  = streamBytes;
                    probesAtLeave = probes;
                    sayAt(ms(t), "LEFT THE MENU: three probes unanswered (a game, or a busy menu: a false leave if the "
                                 "menu is still up)");
                    if (stream) {   // N8Link probes the link again: the input flushed, then the sync
                        sp.flushInput();
                        const std::uint8_t sync[2] = {rp::N8ReliableSender::SYNC, 0};
                        edio.fifoWR(sync, sizeof sync);
                        streamBytes += sizeof sync;
                    }
                }
                if (turn.writeProbe) {
                    edio.fifoWR(rp::N8MenuGuard::kProbe, sizeof rp::N8MenuGuard::kProbe);
                    pending.push_back(t);
                    lastProbeNs = t;
                    ++probes;
                } else if (stream && !turn.hold && noteHeld && t >= offNs) {
                    edio.fifoWR(off, sizeof off);
                    streamBytes += sizeof off;
                    noteHeld = false;
                } else if (stream && !turn.hold && !noteHeld && t >= nextNoteNs) {
                    edio.fifoWR(on, sizeof on);   // a 30 ms A4 every 60 ms, as menuguard streams
                    streamBytes += sizeof on;
                    noteHeld   = true;
                    offNs      = t + 30'000'000;
                    nextNoteNs = t + 60'000'000;
                }
                if (t >= nextTickNs) {
                    nextTickNs += 5'000'000'000;
                    sayAt(ms(t), "tick: %s, probes %llu, answered %llu (slow %llu), unasked 'k' %llu",
                          guard.atMenu() ? "menu" : "no menu", static_cast<unsigned long long>(probes),
                          static_cast<unsigned long long>(answers), static_cast<unsigned long long>(slow),
                          static_cast<unsigned long long>(unasked));
                }
                if (n == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (noteHeld) edio.fifoWR(off, sizeof off);   // never leave the last note held
            if (!pending.empty())
                sayAt(ms(now()), "still waiting at the end: %zu probe(s), the oldest from %.1f ms", pending.size(),
                      ms(pending.front()));
            std::sort(latencies.begin(), latencies.end());
            const auto pct = [&](double p) {
                return latencies.empty() ? -1.0 : latencies[std::min(latencies.size() - 1,
                                                                      static_cast<std::size_t>(p * latencies.size()))];
            };
            std::printf("menuwatch: %s %d s: probes %llu, answered %llu (slow %llu), swallowed %llu, unasked 'k' "
                        "%llu, acks %llu, other bytes %llu\n",
                        mode.c_str(), seconds, static_cast<unsigned long long>(probes),
                        static_cast<unsigned long long>(answers), static_cast<unsigned long long>(slow),
                        static_cast<unsigned long long>(lost), static_cast<unsigned long long>(unasked),
                        static_cast<unsigned long long>(acks), static_cast<unsigned long long>(otherBytes));
            std::printf("menuwatch: answer latency p50 %.1f / p99 %.1f / max %.1f ms; longest gap between two 'k's "
                        "%.1f ms, from %.1f ms (250 healthy; the guard leaves after 3 misses, about 600 ms); leaves %llu, "
                        "re-entries %llu; "
                        "stream bytes %llu, worst into a left menu %llu\n",
                        pct(0.5), pct(0.99), latencies.empty() ? -1.0 : latencies.back(), longestKGapMs, ms(worstKGapAtNs),
                        static_cast<unsigned long long>(leaves), static_cast<unsigned long long>(enters),
                        static_cast<unsigned long long>(streamBytes), static_cast<unsigned long long>(worstLeakBytes));
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "menuwatch failed: %s\n", e.what());
            return 1;
        }
    }

    // A console RESET while a framed stream is between notes (spec/07's residual window): a sparse stream, one 30 ms A4
    // every <gap-ms>, through N8Link with the guard on, as the cart node streams it. A press in a gap goes unseen until
    // the next note's frame meets no ack, so that frame, and whatever the guard writes after it (the stall's probes, a
    // sync, a raw stream once the link is given up), reaches the booting OS. The reset lies between the last moment the
    // window drained and the first write after it; every write from there is listed with its offset from both ends, and
    // the stream runs on for <after-s> seconds as a song would. Then look at the screen: a clean menu or an ERROR.
    // [loss-permille] drops framed bytes on the way out (setTestLossPermille), so stalls answered by an ack open the
    // trusted second, the other case spec/07 names.
    if (op == "pausereset") {
        const int         gapMs   = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        const int         afterS  = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 6;
        const int         loss    = argc > 4 ? static_cast<int>(std::strtol(argv[4], nullptr, 0)) : 0;
        const std::string pport   = argc > 5 ? argv[5] : findN8Port();
        if (gapMs < 60 || afterS <= 0 || pport.empty()) {
            std::fprintf(stderr, "usage: %s pausereset <gap-ms (>=60)> [after-s] [loss-permille] [port]\n", argv[0]);
            return 2;
        }
        using Clock = std::chrono::steady_clock;
        const auto steadyNs = [] {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        };
        struct Write {
            std::int64_t              ns;
            std::vector<std::uint8_t> bytes;
            bool                      resend;
        };
        std::mutex         wm;
        std::vector<Write> writes;
        N8Host host(
            [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); },
            [] { return std::vector<N8PortDto>{}; }, "/tmp");
        N8Link& link = host.link();
        link.setWriteLog([&](const std::uint8_t* d, std::size_t n, std::int64_t ns, bool resend) {
            std::lock_guard<std::mutex> l(wm);
            writes.push_back({ns, {d, d + n}, resend});
        });
        host.setPort(pport);
        host.connect(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the connect seed and the probe
        if (!link.isConnected() || !link.reliable() || link.atMenu()) {
            std::fprintf(stderr, "pausereset: needs a game with the reliable link (connected=%d reliable=%d menu=%d)\n",
                         link.isConnected(), link.reliable(), link.atMenu());
            return 1;
        }
        link.setTestLossPermille(loss);
        const std::int64_t t0 = steadyNs();
        const auto ms = [&](std::int64_t ns) { return static_cast<double>(ns - t0) / 1e6; };
        std::printf("pausereset: a note every %d ms, framed%s; press RESET\n", gapMs,
                    loss ? " with test loss" : "");
        std::fflush(stdout);
        const std::uint8_t on[]  = {0x90, 69, 100};
        const std::uint8_t off[] = {0x80, 69, 0};
        std::uint64_t prevOutstanding = 0;
        std::int64_t nextOn = t0, nextOff = -1, lastDrained = t0, outstandingSince = -1, stallNs = -1, endNs = -1;
        bool         wasMenu = false, wasReliable = true, wasUp = true;
        const auto   say = [&](std::int64_t t, const char* what) {
            std::printf("%10.1f ms  %s (connected=%d atMenu=%d reliable=%d outstanding=%llu resends=%llu)\n", ms(t), what,
                        link.isConnected(), link.atMenu(), link.reliable(),
                        static_cast<unsigned long long>(link.linkOutstanding()),
                        static_cast<unsigned long long>(link.linkResends()));
            std::fflush(stdout);
        };
        while (endNs < 0 || steadyNs() < endNs) {
            const std::int64_t t = steadyNs();
            if (t - t0 > 1'800'000'000'000LL) {   // half an hour without a press
                std::printf("pausereset: no reset seen\n");
                return 1;
            }
            if (t >= nextOn) {
                link.push(0, on, sizeof on, 48000.0);
                nextOff = t + 30'000'000;
                nextOn  = t + std::int64_t(gapMs) * 1'000'000;
            }
            if (nextOff >= 0 && t >= nextOff) {
                link.push(0, off, sizeof off, 48000.0);
                nextOff = -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const std::int64_t now = steadyNs();
            if (stallNs < 0) {
                const std::uint64_t outstanding = link.linkOutstanding();
                if (outstanding == 0) {
                    if (prevOutstanding > 0) lastDrained = now;   // an ack emptied the window: the cart was up then
                    outstandingSince = -1;
                } else if (outstandingSince < 0) {
                    outstandingSince = now;
                } else if (now - outstandingSince >= 100'000'000) {
                    stallNs = outstandingSince;
                    endNs   = now + std::int64_t(afterS) * 1'000'000'000;
                    say(now, "acks stopped: the reset");
                }
                prevOutstanding = outstanding;
            }
            if (link.atMenu() != wasMenu) {
                wasMenu = link.atMenu();
                say(now, wasMenu ? "GUARD: at the menu" : "GUARD: left the menu");
            }
            if (link.reliable() != wasReliable) {
                wasReliable = link.reliable();
                say(now, wasReliable ? "link: framed" : "link: RAW (the window given up, or the link re-probed)");
            }
            if (link.isConnected() != wasUp) {
                wasUp = link.isConnected();
                say(now, wasUp ? "link up" : "link down");
            }
        }
        link.push(0, off, sizeof off, 48000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        host.connect(false);

        // The reset lies between the last frame the cart acknowledged and the first it did not: the newest frame written
        // before the window was seen to stop (the first unacknowledged; acks come within the 2 ms poll, so the poll
        // itself rarely sees the window fill), and the frame before it. Every write from the last acknowledged frame on.
        std::lock_guard<std::mutex> l(wm);
        std::int64_t firstAfter = -1;
        lastDrained             = t0;
        for (const Write& w : writes) {
            if (w.bytes.empty() || w.bytes[0] != rp::N8ReliableSender::FRAME || w.ns > stallNs) continue;
            lastDrained = firstAfter < 0 ? lastDrained : firstAfter;
            firstAfter  = w.ns;
        }
        std::printf("pausereset: the last acknowledged frame went at %.1f ms, the first unacknowledged at %.1f ms (%.1f ms "
                    "later): the reset is in between, so that frame reached the cart at most that long after it\n",
                    ms(lastDrained), ms(firstAfter), firstAfter >= 0 ? (firstAfter - lastDrained) / 1e6 : -1.0);
        std::printf("   at (ms)   after drain  after 1st   bytes\n");
        std::size_t total = 0, frames = 0, probes = 0, syncs = 0, raw = 0;
        for (const Write& w : writes) {
            if (w.ns < lastDrained) continue;
            const char* kind = "raw";
            if (w.bytes.size() == 4 && std::equal(w.bytes.begin(), w.bytes.end(), std::begin(rp::N8MenuGuard::kProbe)))
                kind = "probe", ++probes;
            else if (!w.bytes.empty() && w.bytes[0] == rp::N8ReliableSender::SYNC)
                kind = "sync", ++syncs;
            else if (!w.bytes.empty() && w.bytes[0] == rp::N8ReliableSender::FRAME)
                kind = w.resend ? "re-send" : "frame", ++frames;
            else
                ++raw;
            total += w.bytes.size();
            std::printf("%10.1f %12.1f %10.1f   %-7s", ms(w.ns), (w.ns - lastDrained) / 1e6, (w.ns - firstAfter) / 1e6,
                        kind);
            for (auto b : w.bytes) std::printf(" %02X", b);
            std::printf("\n");
        }
        std::printf("pausereset: %zu byte(s) written from the drain on: %zu frame write(s), %zu probe(s), %zu sync(s), "
                    "%zu raw write(s); guard at the menu: %s\n",
                    total, frames, probes, syncs, raw, wasMenu ? "yes" : "NO");
        return 0;
    }

    // The FPGA config block (0x1800020, 16 bytes) read every <ms> for <seconds>, each change printed with its time: what
    // a cart shows over USB, with nothing written to its FIFO, as the menu starts a game (map_idx and the rest). A read of
    // the system region is the MCU's, not a DMA, and goes nowhere near the console's FIFO.
    if (op == "cfgwatch") {
        const int         seconds = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        const int         everyMs = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 50;
        const std::string cport   = argc > 4 ? argv[4] : findN8Port();
        if (seconds <= 0 || everyMs <= 0 || cport.empty()) {
            std::fprintf(stderr, "usage: %s cfgwatch <seconds> [every-ms] [port]\n", argv[0]);
            return 2;
        }
        try {
            SerialTransport sp(cport);
            Edio            edio(sp);
            edio.connect();
            using Clock = std::chrono::steady_clock;
            const auto t0 = Clock::now();
            const auto ms = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
            std::uint8_t last[16] = {}, cur[16];
            bool         first = true;
            unsigned     reads = 0, fails = 0;
            while (ms() < seconds * 1000.0) {
                try {
                    edio.memRD(0x1800020, cur, sizeof cur);
                    ++reads;
                    if (first || std::memcmp(cur, last, sizeof cur) != 0) {
                        std::printf("%10.1f ms ", ms());
                        for (int i = 0; i < 16; ++i) std::printf(" %02X%s", cur[i], cur[i] != last[i] && !first ? "*" : "");
                        std::printf("\n");
                        std::fflush(stdout);
                        std::memcpy(last, cur, sizeof cur);
                        first = false;
                    }
                } catch (const std::exception& e) {
                    ++fails;
                    std::printf("%10.1f ms  read failed: %s\n", ms(), e.what());
                    std::fflush(stdout);
                    sp.flushInput();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(everyMs));
            }
            std::printf("cfgwatch: %u reads, %u failed\n", reads, fails);
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "cfgwatch failed: %s\n", e.what());
            return 1;
        }
    }

    if (op == "menuguard") {
        const int         seconds = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        const bool        raw     = std::string(argv[argc - 1]) == "raw";
        const std::string gport   = argc > 3 && std::string(argv[3]) != "raw" ? argv[3] : findN8Port();
        if (seconds <= 0 || gport.empty()) {
            std::fprintf(stderr, "usage: %s menuguard <seconds> [port] [raw] (no Everdrive N8 found?)\n", argv[0]);
            return 2;
        }
        N8Host host(
            [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); },
            [] { return std::vector<N8PortDto>{}; }, "/tmp");
        if (raw) host.link().setLinkAllowed(false);
        host.setPort(gport);
        host.connect(true);
        N8Link& link = host.link();
        if (!link.isConnected()) {
            std::fprintf(stderr, "menuguard: the link did not come up on %s: %s\n", gport.c_str(),
                         link.lastError().c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the connect seed and the probe
        using Clock = std::chrono::steady_clock;
        const auto t0  = Clock::now();
        const auto ms  = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
        const auto say = [&](const char* what) {
            std::printf("%9.1f ms  %-26s connected=%d atMenu=%d reliable=%d bytes=%llu dropped=%llu resends=%llu "
                        "outstanding=%llu\n",
                        ms(), what, link.isConnected(), link.atMenu(), link.reliable(),
                        static_cast<unsigned long long>(link.bytesForwarded()),
                        static_cast<unsigned long long>(link.pushesDropped()),
                        static_cast<unsigned long long>(link.linkResends()),
                        static_cast<unsigned long long>(link.linkOutstanding()));
            std::fflush(stdout);
        };
        say(link.atMenu() ? "start: AT THE MENU" : "start: streaming");
        const std::uint8_t on[]  = {0x90, 69, 100};
        const std::uint8_t off[] = {0x80, 69, 0};
        bool          wasMenu = link.atMenu(), wasUp = link.isConnected(), stalled = false;
        double        stallMs = -1, tripMs = -1, lastWriteMs = 0, outstandingSince = -1;
        std::uint64_t lastBytes = link.bytesForwarded(), bytesAtTrip = 0;
        int           step = 0;
        // Up to <seconds>, or 5 s past a trip: enough to see that nothing more is written once the guard has the menu.
        while (ms() < seconds * 1000.0 && (tripMs < 0 || ms() < tripMs + 5000.0)) {
            if (step % 6 == 0) link.push(0, on, sizeof on, 48000.0);    // a 30 ms A4 every 60 ms
            if (step % 6 == 3) link.push(0, off, sizeof off, 48000.0);
            ++step;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const std::uint64_t bytes = link.bytesForwarded();
            if (bytes != lastBytes) { lastBytes = bytes; lastWriteMs = ms(); }
            // The cart stopped acknowledging: frames outstanding past the link's 100 ms timeout (the reset, on a
            // framed link). The guard holds the re-sends in that stall, so it shows as the outstanding count staying
            // up, not as a re-send; when it went up is when the console went away, near enough.
            if (link.reliable() && link.linkOutstanding() > 0) {
                if (outstandingSince < 0) outstandingSince = ms();
            } else {
                outstandingSince = -1;
            }
            if (!stalled && outstandingSince >= 0 && ms() - outstandingSince >= 100.0) {
                stalled = true;
                stallMs = outstandingSince;
                say("acks stopped (reset?)");
            }
            if (link.atMenu() != wasMenu) {
                wasMenu = link.atMenu();
                if (wasMenu && tripMs < 0) { tripMs = ms(); bytesAtTrip = bytes; }
                say(wasMenu ? "GUARD TRIPPED: at the menu" : "left the menu");
            }
            if (link.isConnected() != wasUp) {
                wasUp = link.isConnected();
                say(wasUp ? "link up" : "link down");
            }
            if (step % 500 == 0) say("tick");
        }
        link.push(0, off, sizeof off, 48000.0);   // never leave the last note held behind the disconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        say("end");
        const std::uint64_t after = tripMs >= 0 ? link.bytesForwarded() - bytesAtTrip : 0;
        if (tripMs < 0) std::printf("menuguard: the guard never tripped (no reset seen)\n");
        else
            std::printf("menuguard: tripped at %.1f ms (%.1f ms after the acks stopped); last write at %.1f ms; %llu "
                        "byte(s) written after the trip\n",
                        tripMs, stallMs >= 0 ? tripMs - stallMs : -1.0, lastWriteMs,
                        static_cast<unsigned long long>(after));
        link.disconnect();
        return tripMs >= 0 && after == 0 ? 0 : 1;
    }

    // What the N8 OS menu does with bytes it did not ask for (docs/n8-menu-guard-reset-design.md section 1): 'test'
    // sends '*t' and reports the answer ('k' is the menu); 'sysex' sends '*t' wrapped as F0 2A 74 F7, a SysEx a MIDI
    // game ignores, then reports its answer and a plain '*t' after it; 'note' sends one note-on (90 3C 40), a stray
    // byte run of the residual window's kind, then a plain '*t'. Run at a menu; check the screen after each.
    if (op == "menuprobe") {
        const std::string mode  = argv[2];
        const std::string mport = argc > 3 ? argv[3] : findN8Port();
        if (mport.empty() || (mode != "test" && mode != "sysex" && mode != "note")) {
            std::fprintf(stderr, "usage: %s menuprobe <test|sysex|note> [port]\n", argv[0]);
            return 2;
        }
        const auto show = [](const char* what, const rp::n8::Status& s, int got) {
            if (s.ok()) std::printf("%s: answered 0x%02X%s\n", what, got, got == 0x6b ? " ('k': the menu)" : "");
            else std::printf("%s: %s (reply 0x%02X)\n", what, rp::n8::codeName(s.code), s.reply);
        };
        try {
            SerialTransport sp(mport);
            rp::n8::Edio    edio(sp);   // the pack's, whose calls answer a Status rather than throw
            if (const rp::n8::Status c = edio.connect(); !c.ok()) {
                std::fprintf(stderr, "menuprobe: connect: %s\n", rp::n8::codeName(c.code));
                return 1;
            }
            rp::n8::Menu menu(edio);
            if (mode != "test") {
                const std::uint8_t sysex[] = {0xF0, 0x2A, 0x74, 0xF7};
                const std::uint8_t note[]  = {0x90, 0x3C, 0x40};
                const bool         isSx    = mode == "sysex";
                rp::n8::Status     w = isSx ? edio.fifoWR(sysex, sizeof sysex) : edio.fifoWR(note, sizeof note);
                std::printf("%s written: %s\n", isSx ? "F0 2A 74 F7" : "90 3C 40", rp::n8::codeName(w.code));
                if (isSx) {
                    std::uint8_t   got = 0;
                    rp::n8::Status r   = edio.rx8(got, 1000);
                    show("the wrapped *t", r, got);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            const rp::n8::Status s = menu.test();
            show("a plain *t", s, s.ok() ? 0x6b : 0);
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "menuprobe failed: %s\n", e.what());
            return 1;
        }
    }

    if (op == "fiford") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: %s fiford <count> [timeout-ms] [port]\n", argv[0]);
            return 2;
        }
        const std::size_t count     = static_cast<std::size_t>(std::strtol(argv[2], nullptr, 0));
        const int         timeoutMs = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 2000;
        const std::string pport     = argc > 4 ? argv[4] : findN8Port();
        if (count == 0) {
            std::fprintf(stderr, "fiford: count must be > 0\n");
            return 2;
        }
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport           sp(pport);
            Edio                      edio(sp);
            edio.setReadTimeout(timeoutMs);
            std::vector<std::uint8_t> buf(count);
            edio.readData(buf.data(), buf.size());  // throws on timeout (the ROM sent nothing)
            std::printf("fiford %zu:", buf.size());
            for (std::uint8_t b : buf) std::printf(" %02X", b);
            std::printf("\n  ascii: ");
            for (std::uint8_t b : buf) std::printf("%c", (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.');
            std::printf("\n");
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fiford failed: %s (is a ROM running and sending CMD_USB_WR?)\n", e.what());
            return 1;
        }
    }

    // Raw device-memory peek/poke: a bare Edio over the port (no N8Host, no streaming thread). Used to read /
    // write FPGA config registers directly - e.g. the expansion-audio master volume at Edio::ADDR_EXP_VOL.
    if (op == "peek" || op == "poke") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s %s <addr-hex> <%s> [port]\n", argv[0], op.c_str(),
                         op == "peek" ? "len" : "byte");
            return 2;
        }
        // Base 16, as the usage says: with strtol's base 0 a bare "1800020" silently read as DECIMAL
        // (0x1B7754) and returned bus noise, which is how the config block was once written off as
        // write-only. An explicit "0x" prefix still parses.
        const std::int32_t addr = static_cast<std::int32_t>(std::strtol(argv[2], nullptr, 16));
        const std::string  pport = argc > 4 ? argv[4] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();  // handshake (works whether the menu or a game is running)
            if (op == "poke") {
                const std::uint8_t v = static_cast<std::uint8_t>(std::strtol(argv[3], nullptr, 0));
                edio.memWR(addr, &v, 1);
                std::printf("poke [0x%X] <- 0x%02X (%u)\n", addr, v, v);
            } else {
                const std::size_t         len = static_cast<std::size_t>(std::strtol(argv[3], nullptr, 0));
                std::vector<std::uint8_t> buf(len ? len : 1);
                edio.memRD(addr, buf.data(), buf.size());
                std::printf("peek [0x%X] (%zu):", addr, buf.size());
                for (std::uint8_t b : buf) std::printf(" %02X", b);
                std::printf("\n");
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s failed: %s\n", op.c_str(), e.what());
            return 1;
        }
    }

    // Bare-Edio SD file-management round-trip (CMD_F_AVB + CMD_F_DIR_MK + CMD_F_DEL): free space, then make +
    // remove a scratch dir (self-cleaning, safe). Exercises the native freeSpace/dirMake/fileDelete twins.
    if (op == "fstest") {
        const std::string pport = argc > 2 ? argv[2] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            const std::uint64_t free = edio.freeSpace();
            std::printf("SD free space: %.2f GB (%llu bytes)\n", free / static_cast<double>(1ull << 30),
                        static_cast<unsigned long long>(free));
            const std::string scratch = "EDN8/rp-scratch";
            edio.dirMake(scratch);
            std::printf("made %s\n", scratch.c_str());
            edio.fileDelete(scratch);
            std::printf("removed %s\n", scratch.c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fstest failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio device info (CMD_SYS_INF + CMD_GET_VDC): the native twin of the TS decodeSysInfo/decodeVdc.
    // Decodes the key fields inline (offsets from edlink getSysInf; little-endian) to prove the C++ Edio::sysInfo.
    if (op == "info") {
        const std::string pport = argc > 2 ? argv[2] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            const std::vector<std::uint8_t> b = edio.sysInfo();
            const std::vector<std::uint8_t> v = edio.vdc();
            const auto u16 = [](const std::vector<std::uint8_t>& d, int o) { return d[o] | (d[o + 1] << 8); };
            const auto u32 = [](const std::vector<std::uint8_t>& d, int o) {
                return static_cast<std::uint32_t>(d[o]) | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
            };
            std::printf("device_id  : 0x%02X %s\n", b[46], b[46] == 0x17 ? "(EverDrive-N8 PRO)" : "");
            std::printf("serial     : %08X.%08X\n", u32(b, 20), u32(b, 24));
            std::printf("form factor: %s\n", b[52] == 0 ? "NES" : b[52] == 1 ? "Famicom" : "unknown");
            std::printf("bootloader : 0x%04X   flash: %u MB\n", u16(b, 44), (1u << b[55]) / 0x100000u);
            std::printf("voltages   : 5.0=%02X.%02X 2.5=%02X.%02X 1.2=%02X.%02X bat=%02X.%02X\n",
                        v[1], v[0], v[3], v[2], v[5], v[4], v[7], v[6]);
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "info failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio block memWR from a file + readback verify: the block-write twin of poke. Live-patch a running
    // game's CHR/PRG (memWR to ADDR_CHR/ADDR_PRG + offset writes the same PSRAM the console fetches). Exercises
    // the native memWR on hardware at an arbitrary address.
    if (op == "memwr") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s memwr <addr-hex> <file> [port]\n", argv[0]);
            return 2;
        }
        const std::int32_t addr  = static_cast<std::int32_t>(std::strtol(argv[2], nullptr, 0));
        const std::string  src   = argv[3];
        const std::string  pport = argc > 4 ? argv[4] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        const std::vector<std::uint8_t> data = slurpLocal(src);
        if (data.empty()) {
            std::fprintf(stderr, "cannot read %s (or it is empty)\n", src.c_str());
            return 1;
        }
        try {
            SerialTransport           sp(pport);
            Edio                      edio(sp);
            edio.connect();
            edio.memWR(addr, data.data(), data.size());
            std::vector<std::uint8_t> check(data.size());
            edio.memRD(addr, check.data(), check.size());
            if (check != data) {
                std::fprintf(stderr, "verify failed: readback != written\n");
                return 1;
            }
            std::printf("wrote + verified %zu bytes -> [0x%X]\n", data.size(), addr);
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "memwr failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio cart-FIFO write from a file: memWR to ADDR_FIFO, which a running ROM drains at $40F0/$40F1
    // (BlipToaster reads raw MIDI bytes from it). Unlike memwr there is NO readback verify - a FIFO is consumed
    // by the NES side, so reading it back can never return what was written.
    if (op == "fifowr") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: %s fifowr <file> [port]\n", argv[0]);
            return 2;
        }
        const std::string src   = argv[2];
        const std::string pport = argc > 3 ? argv[3] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        const std::vector<std::uint8_t> data = slurpLocal(src);
        if (data.empty()) {
            std::fprintf(stderr, "cannot read %s (or it is empty)\n", src.c_str());
            return 1;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            edio.fifoWR(data.data(), data.size());
            std::printf("fifoWR %zu bytes\n", data.size());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fifowr failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio + N8Menu screen dump ('*v'): pull the menu VRAM(2048)+palette(16)+CHR(8192) over USB to one
    // raw file (10256 bytes). Menu-only (a running game won't answer '*v'). Exercises the C++ readData /
    // vramDump / memRD twins; render the raw dump to PNG via the TS assembler or use the CLI --screenshot.
    if (op == "vramdump") {
        const std::string dest  = argv[2];
        const std::string pport = argc > 3 ? argv[3] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport   sp(pport);
            Edio              edio(sp);
            edio.connect();
            N8Menu menu(edio);
            menu.test();  // clean "not at its menu" error before the big raw read
            const N8VramDump          vd = menu.vramDump();
            std::vector<std::uint8_t> chr(8192);
            edio.memRD(Edio::ADDR_MENU_CHR, chr.data(), chr.size());
            std::FILE* f = std::fopen(dest.c_str(), "wb");
            if (!f) {
                std::fprintf(stderr, "cannot write %s\n", dest.c_str());
                return 1;
            }
            std::fwrite(vd.vram.data(), 1, vd.vram.size(), f);
            std::fwrite(vd.palette.data(), 1, vd.palette.size(), f);
            std::fwrite(chr.data(), 1, chr.size(), f);
            std::fclose(f);
            std::printf("wrote vram(%zu)+palette(%zu)+chr(%zu) = %zu bytes -> %s\n", vd.vram.size(),
                        vd.palette.size(), chr.size(), vd.vram.size() + vd.palette.size() + chr.size(), dest.c_str());
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "vramdump failed: %s\n", e.what());
            return 1;
        }
    }

    // Bare-Edio live save-state sniffer read: memRD(ADDR_SSR, 512) mirrors a RUNNING game's APU/PPU/OAM
    // writes (edn8-pro-pub sst.sv). Read-only, safe on a running game. Menu-only-OFF: at the file browser the
    // mirror is disabled, so the magic byte at +0xCF won't be 0x53. Exercises the C++ memRD twin on hardware.
    if (op == "sniff") {
        const std::string pport = argc > 2 ? argv[2] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            SerialTransport           sp(pport);
            Edio                      edio(sp);
            edio.connect();
            std::vector<std::uint8_t> ssr(0x200);
            edio.memRD(Edio::ADDR_SSR, ssr.data(), ssr.size());
            const bool magicOk = ssr[0xCF] == 0x53;
            std::printf("magic [+0xCF] = 0x%02X %s\n", ssr[0xCF], magicOk ? "('S' - sniffer live)" : "(no running game?)");
            std::printf("APU  [+0x080..0x09F]:");
            for (int i = 0; i < 0x20; ++i) std::printf(" %02X", ssr[0x080 + i]);
            std::printf("\nPPU  [+0x0C0..0x0C3]: %02X %02X %02X %02X   CPU [+0x0C8]: %02X %02X %02X %02X\n",
                        ssr[0x0C0], ssr[0x0C1], ssr[0x0C2], ssr[0x0C3], ssr[0x0C8], ssr[0x0C9], ssr[0x0CA], ssr[0x0CB]);
            return magicOk ? 0 : 1;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "sniff failed: %s\n", e.what());
            return 1;
        }
    }

    if (op != "dump" && op != "load" && op != "restore") {
        std::fprintf(stderr, "unknown op '%s'\n", op.c_str());
        return 2;
    }
    // The card's operations are the cart node's capability jobs, which is what this drives: a battery job names the ROM
    // whose file it reaches, and is run at the cart's menu (see N8Host::submitCartJob).
    const bool        battery = op != "load";
    const std::string romName = battery ? argv[2] : "";
    const std::string path    = battery ? (argc > 3 ? argv[3] : "") : argv[2];
    if (battery && path.empty()) {
        std::fprintf(stderr, "usage: %s %s <rom-name> <file> [port]\n", argv[0], op.c_str());
        return 2;
    }
    std::string port = argc > (battery ? 4 : 3) ? argv[battery ? 4 : 3] : findN8Port();
    if (port.empty()) {
        std::fprintf(stderr, "no Everdrive N8 found (VID:PID 38df:0017); pass a port explicitly\n");
        return 2;
    }
    std::printf("N8 SD hw-test: op=%s file=%s rom=%s port=%s\n", op.c_str(), path.c_str(),
                battery ? romName.c_str() : "-", port.c_str());

    N8Host host(
        [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); },
        [] {
            std::vector<N8PortDto> v;
            for (const auto& p : listSerialPorts()) v.push_back({p.port, p.isN8});
            return v;
        },
        "/tmp");
    // setPort only reconnects a link that is already streaming, and a job is refused while the link is down, so bring it
    // up the way Settings > N8 would: the cart node's jobs run on that link.
    host.setPort(port);
    host.connect(true);
    if (!host.link().isConnected()) {
        std::fprintf(stderr, "link did not come up on %s: %s\n", port.c_str(), host.link().lastError().c_str());
        return 2;
    }

    rp::n8::test::JobRequest request;
    if (op == "load") {
        request.capability = RP_SYSTEM_ROM_INTERFACE;
        request.op         = "upload";
        request.bytes      = slurpLocal(path);
        if (request.bytes.empty()) {
            std::fprintf(stderr, "cannot read %s (or it is empty)\n", path.c_str());
            return 1;
        }
        const auto slash   = path.find_last_of("/\\");
        request.name       = slash == std::string::npos ? path : path.substr(slash + 1);
    } else {
        request.capability = RP_SYSTEM_SRAM_INTERFACE;
        request.op         = op;
        request.name       = romName;
        if (op == "restore") {
            request.bytes = slurpLocal(path);
            if (request.bytes.empty()) {
                std::fprintf(stderr, "cannot read %s (or it is empty)\n", path.c_str());
                return 1;
            }
        }
    }

    rp::n8::test::Job job;
    std::string       error;
    if (!host.submitCartJob(rp::n8::test::requestView(request), rp::n8::test::handle(&job),
                            rp::n8::test::jobReports(), &host, &error)) {
        std::fprintf(stderr, "%s: %s\n", op.c_str(), error.c_str());
        return 1;
    }
    // Poll the record, as an engine's drainJobs would, printing progress until the job is terminal.
    std::uint32_t lastProgress = ~0u, lastTotal = ~0u;
    while (!job.finished()) {
        const std::uint32_t done = job.progress.load(std::memory_order_acquire);
        const std::uint32_t total = job.total.load(std::memory_order_acquire);
        if (done != lastProgress || total != lastTotal) {
            lastProgress = done;
            lastTotal    = total;
            const int pct = total ? static_cast<int>(100.0 * done / total) : 0;
            std::printf("\r  [%-8s] %6u/%-6u %3d%%   ", op.c_str(), done, total, pct);
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::printf("\n");
    if (job.state.load(std::memory_order_acquire) != rp::n8::test::Job::Done) {
        std::printf("ERROR: %s\n", job.error.c_str());
        return 1;
    }
    if (op == "dump") {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 1;
        }
        std::fwrite(job.result.data(), 1, job.result.size(), f);
        std::fclose(f);
        std::printf("OK: %zu bytes -> %s\n", job.result.size(), path.c_str());
    } else {
        std::printf("OK: %.*s\n", static_cast<int>(job.result.size()), reinterpret_cast<const char*>(job.result.data()));
    }
    return 0;
}
