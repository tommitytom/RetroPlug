// n8-sd-hwtest - a manual hardware harness for the N8 SD / menu ops (Settings > N8 Pro). It constructs the
// SAME native N8Host + N8SdWorker + connection manager the standalone/plugin menu drives, but against a real
// /dev/ttyACM0, so a hardware pass exercises exactly the code path the UI does (not the CLI's TS stack).
// EXCLUDE_FROM_ALL, built by name (retroplug-n8-hwtest); NOT a CI target. Needs a real N8 plugged in.
//
//   retroplug-n8-hwtest dump    <dest.srm>   [port]   # read the 64 KB battery to a file (non-destructive)
//   retroplug-n8-hwtest load    <rom.nes>    [port]   # upload + boot a ROM (cart must be at its menu)
//   retroplug-n8-hwtest restore <save.srm>   [port]   # write a .srm to the running game's SRAM + verify
//   retroplug-n8-hwtest peek    <addr-hex> <len> [port]   # CMD_MEM_RD: read bytes from a device address
//   retroplug-n8-hwtest poke    <addr-hex> <byte>[port]   # CMD_MEM_WR: write one byte to a device address
//   retroplug-n8-hwtest read    <sd-path> <local-dest> [port]  # CMD_F_FRD: read an SD file over USB
//   retroplug-n8-hwtest vramdump <out.bin> [port]              # menu '*v': dump VRAM+palette+CHR (screenshot)
//
// peek/poke/read/vramdump drive a bare Edio (no N8Host / streaming thread). peek/poke reach FPGA config regs like
// the expansion-audio master volume at 0x1800023 (MapConfig.master_vol = scfg[3]; see krikzz edn8-pro-pub
// edio/everdrive.h + fpga/base_sv/sys_cfg.sv). 0x1800023 <- 0..255, where 128 = unity gain. read pulls a whole
// SD file (e.g. EDN8/sysdata/registry.bin) to a local file.
//
// Exit 0 on success, 1 on the op's error, 2 on a usage / no-device problem.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "host/n8/Edio.hpp"
#include "host/n8/N8Host.hpp"
#include "host/n8/N8Menu.hpp"
#include "host/n8/WjwwoodSerialPort.hpp"

using namespace retroplug;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <dump|load|restore|peek|poke|read|vramdump> <path|addr> [len|byte|dest] [port]\n", argv[0]);
        return 2;
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
            WjwwoodSerialPort               sp(pport);
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

    // Raw device-memory peek/poke: a bare Edio over the port (no N8Host, no streaming thread). Used to read /
    // write FPGA config registers directly - e.g. the expansion-audio master volume at 0x1800003.
    if (op == "peek" || op == "poke") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s %s <addr-hex> <%s> [port]\n", argv[0], op.c_str(),
                         op == "peek" ? "len" : "byte");
            return 2;
        }
        const std::int32_t addr = static_cast<std::int32_t>(std::strtol(argv[2], nullptr, 0));
        const std::string  pport = argc > 4 ? argv[4] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            WjwwoodSerialPort sp(pport);
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
            WjwwoodSerialPort sp(pport);
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

    const std::string path = argv[2];
    std::string       port = argc > 3 ? argv[3] : findN8Port();
    if (port.empty()) {
        std::fprintf(stderr, "no Everdrive N8 found (VID:PID 38df:0017); pass a port explicitly\n");
        return 2;
    }
    std::printf("N8 SD hw-test: op=%s path=%s port=%s\n", op.c_str(), path.c_str(), port.c_str());

    N8Host host(
        [](const std::string& p) -> std::unique_ptr<ISerialPort> { return std::make_unique<WjwwoodSerialPort>(p); },
        [] {
            std::vector<N8PortDto> v;
            for (const auto& p : listSerialPorts()) v.push_back({p.port, p.isN8});
            return v;
        },
        "/tmp");
    host.setPort(port);

    if (op == "dump")         host.startDumpSram(path);
    else if (op == "load")    host.startLoadRom(path);
    else if (op == "restore") host.startRestoreSram(path);
    else {
        std::fprintf(stderr, "unknown op '%s'\n", op.c_str());
        return 2;
    }

    // Poll the same status snapshot the UI polls, printing progress until the job finishes.
    std::uint64_t lastVersion = ~0ull;
    for (;;) {
        const N8SdStatusDto s = host.sdStatus();
        if (s.version != lastVersion) {
            lastVersion = s.version;
            const int pct = s.bytesTotal ? static_cast<int>(100 * s.bytesDone / s.bytesTotal) : 0;
            std::printf("\r  [%-8s] %-14s %6llu/%-6llu %3d%%   ", s.op.c_str(), s.phase.c_str(),
                        static_cast<unsigned long long>(s.bytesDone),
                        static_cast<unsigned long long>(s.bytesTotal), pct);
            std::fflush(stdout);
        }
        if (s.done) {
            std::printf("\n");
            if (!s.error.empty()) { std::printf("ERROR: %s\n", s.error.c_str()); return 1; }
            std::printf("OK: %s\n", s.result.c_str());
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
