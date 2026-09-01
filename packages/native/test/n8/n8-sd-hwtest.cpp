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
//   retroplug-n8-hwtest sniff    [port]                        # memRD ADDR_SSR: a running game's live APU/PPU/OAM
//   retroplug-n8-hwtest memwr    <addr-hex> <file> [port]      # block memWR + verify (live-patch CHR/PRG)
//   retroplug-n8-hwtest info     [port]                        # CMD_SYS_INF + CMD_GET_VDC: serial/versions/form/volts
//   retroplug-n8-hwtest fstest   [port]                        # CMD_F_AVB/DIR_MK/DEL: free space + scratch mkdir/rm
//
// peek/poke/read/vramdump/sniff/memwr/info/fstest drive a bare Edio (no N8Host / streaming thread). peek/poke reach FPGA config regs like
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
        // `sniff`/`info`/`fstest` need no path/addr (they auto-detect the port), so allow a bare argc==2.
        const std::string a1 = argc >= 2 ? argv[1] : "";
        if (!(argc == 2 && (a1 == "sniff" || a1 == "info" || a1 == "fstest"))) {
            std::fprintf(stderr,
                         "usage: %s <dump|load|restore|peek|poke|read|vramdump|sniff|memwr|fifowr|info|fstest> <path|addr> [len|byte|dest] [port]\n",
                         argv[0]);
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

    // Bare-Edio SD file-management round-trip (CMD_F_AVB + CMD_F_DIR_MK + CMD_F_DEL): free space, then make +
    // remove a scratch dir (self-cleaning, safe). Exercises the native freeSpace/dirMake/fileDelete twins.
    if (op == "fstest") {
        const std::string pport = argc > 2 ? argv[2] : findN8Port();
        if (pport.empty()) {
            std::fprintf(stderr, "no Everdrive N8 found; pass a port explicitly\n");
            return 2;
        }
        try {
            WjwwoodSerialPort sp(pport);
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
            WjwwoodSerialPort sp(pport);
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
        std::FILE* f = std::fopen(src.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", src.c_str());
            return 1;
        }
        std::vector<std::uint8_t> data;
        std::uint8_t              chunk[4096];
        for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), f)) > 0;)
            data.insert(data.end(), chunk, chunk + got);
        std::fclose(f);
        if (data.empty()) {
            std::fprintf(stderr, "%s is empty\n", src.c_str());
            return 1;
        }
        try {
            WjwwoodSerialPort         sp(pport);
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
        std::FILE* f = std::fopen(src.c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", src.c_str());
            return 1;
        }
        std::vector<std::uint8_t> data;
        std::uint8_t              chunk[4096];
        for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), f)) > 0;)
            data.insert(data.end(), chunk, chunk + got);
        std::fclose(f);
        if (data.empty()) {
            std::fprintf(stderr, "%s is empty\n", src.c_str());
            return 1;
        }
        try {
            WjwwoodSerialPort sp(pport);
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
            WjwwoodSerialPort         sp(pport);
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
