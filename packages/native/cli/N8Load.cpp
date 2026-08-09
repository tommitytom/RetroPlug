#include "N8Load.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "host/n8/Edio.hpp"
#include "host/n8/N8Menu.hpp"
#include "host/n8/WjwwoodSerialPort.hpp"

namespace retroplug {

namespace {

const char* kDefaultRom = "resources/roms/n8-midi.nes";

void printUsage() {
    std::puts(
        "usage: retroplug-cli n8-load [options] [<rom.nes>]\n"
        "\n"
        "  Load + boot a ROM on a physical Everdrive N8 Pro over USB by driving its on-device menu.\n"
        "  The N8 firmware parses the ROM and sources the mapper core from its own SD card, so no\n"
        "  low-level FPGA/mapper work happens here. The N8 must be on its file-browser menu.\n"
        "\n"
        "  With <rom.nes>, the local ROM is uploaded to the N8 SD (usb-games/) and booted.\n"
        "  Default ROM (no args): resources/roms/n8-midi.nes (relative to the current directory).\n"
        "\n"
        "options:\n"
        "  <rom.nes>          upload this local ROM to the N8 SD and boot it\n"
        "  --sd-path <path>   instead, boot a ROM already on the N8 SD card, by its SD path\n"
        "  --srm <save.srm>   restore this battery save: write it to the menu's per-game save slot\n"
        "                     (EDN8/gamedata/<rom>/bram.srm) so the menu loads it on boot (e.g. a risa song)\n"
        "  --sram-only        with --srm: write the save STRAIGHT to cart SRAM over USB (no ROM/menu/reboot),\n"
        "                     for a game already running. WARNING: corrupts the menu if run on the menu\n"
        "  --dump-sram <file> read the cart SRAM out to <file> (no ROM, no menu, no reboot) - captures a\n"
        "                     game's native on-cart save\n"
        "  --ls <path>        list an SD-card directory (use \"/\" for root) and exit\n"
        "  --serial <port>    use this serial port (default: auto-detect the N8, VID:PID 38df:0017)\n"
        "  -h, --help         show this help\n"
        "\n"
        "  Run this from the N8 file-browser menu. If the install fails with 'out of memory' (a dirty\n"
        "  menu heap after a prior failed load), power-cycle the console to a fresh menu and retry.");
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Read a whole local file into memory. Returns false (+ message) if it can't be read.
bool slurp(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize n = in.tellg();
    if (n < 0) return false;
    in.seekg(0);
    out.resize(static_cast<std::size_t>(n));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), n));
}

}  // namespace

int runN8Load(int argc, char** argv) {
    std::string romPath;   // local ROM to upload (positional)
    std::string sdPath;    // existing SD path to boot (--sd-path)
    std::string srmPath;   // optional battery save (.srm) to upload alongside (--srm)
    std::string serialPortName;
    std::string dumpSramPath;      // --dump-sram <file>: read cart SRAM to a file (no ROM, no reboot)
    std::string lsPath;            // --ls <path>: list an SD directory (no ROM, no reboot)
    bool        doLs = false;
    bool        sramOnly = false;  // --sram-only: just write --srm to cart SRAM (no ROM, no reboot)

    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(a, "--sd-path") == 0) {
            sdPath = next("--sd-path");
        } else if (std::strcmp(a, "--srm") == 0) {
            srmPath = next("--srm");
        } else if (std::strcmp(a, "--sram-only") == 0) {
            sramOnly = true;
        } else if (std::strcmp(a, "--dump-sram") == 0) {
            dumpSramPath = next("--dump-sram");
        } else if (std::strcmp(a, "--ls") == 0) {
            lsPath = next("--ls");
            doLs = true;
        } else if (std::strcmp(a, "--serial") == 0) {
            serialPortName = next("--serial");
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            printUsage();
            return 0;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s' (try --help)\n", a);
            return 2;
        } else {
            romPath = a;  // positional = local ROM to upload
        }
    }

    if (sramOnly && srmPath.empty()) {
        std::fprintf(stderr, "error: --sram-only requires --srm <save.srm>\n");
        return 2;
    }
    const bool sramReadWriteOnly = sramOnly || !dumpSramPath.empty() || doLs;  // no ROM / no menu / no reboot
    if (sdPath.empty() && romPath.empty() && !sramReadWriteOnly) romPath = kDefaultRom;  // convenience default

    // Read the local ROM up front (before touching hardware) so a bad path fails fast.
    std::vector<std::uint8_t> rom;
    if (sdPath.empty() && !sramReadWriteOnly) {
        if (!slurp(romPath, rom)) {
            std::fprintf(stderr, "error: cannot read ROM '%s'\n", romPath.c_str());
            return 1;
        }
    }

    // Read the optional battery save (.srm) up front too.
    std::vector<std::uint8_t> srm;
    if (!srmPath.empty()) {
        if (!slurp(srmPath, srm)) {
            std::fprintf(stderr, "error: cannot read save '%s'\n", srmPath.c_str());
            return 1;
        }
    }

    // Resolve the serial port (explicit --serial, else auto-detect the N8) and connect.
    std::string portName = serialPortName.empty() ? findN8Port() : serialPortName;
    if (portName.empty()) {
        std::fprintf(stderr,
                     "error: no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass\n"
                     "       --serial <port>. Run 'retroplug-cli n8-bridge --list' to see ports.\n");
        return 1;
    }

    std::unique_ptr<WjwwoodSerialPort> port;
    try {
        port = std::make_unique<WjwwoodSerialPort>(portName);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: cannot open serial port %s: %s\n", portName.c_str(), e.what());
        return 1;
    }

    Edio edio(*port);
    try {
        edio.connect();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: N8 handshake failed on %s: %s\n", portName.c_str(), e.what());
        return 1;
    }
    std::printf("Connected to Everdrive N8 Pro on %s\n", portName.c_str());

    // --ls: list an SD directory (no ROM, no menu, no reboot). For discovering exact SD paths + save layout.
    if (doLs) {
        const std::vector<N8DirEntry> entries = edio.listDir(lsPath);
        std::printf("%s (%zu entries):\n", lsPath.empty() ? "/" : lsPath.c_str(), entries.size());
        for (const N8DirEntry& e : entries) {
            if (e.isDir) std::printf("  [DIR]  %s\n", e.name.c_str());
            else         std::printf("  %8u  %s\n", e.size, e.name.c_str());
        }
        return 0;
    }

    // --dump-sram: read the cart SRAM out to a file (no ROM, no menu, no reboot). Works on a running game;
    // the tool to capture a game's NATIVE on-cart save format for comparison / conversion.
    if (!dumpSramPath.empty()) {
        std::vector<std::uint8_t> sram(Edio::SIZE_SRM_GAME);
        std::printf("Reading cart SRAM (%zu bytes)...\n", sram.size());
        edio.memRD(Edio::ADDR_SRM, sram.data(), sram.size());
        std::ofstream out(dumpSramPath, std::ios::binary);
        if (!out || !out.write(reinterpret_cast<const char*>(sram.data()),
                               static_cast<std::streamsize>(sram.size()))) {
            std::fprintf(stderr, "error: cannot write '%s'\n", dumpSramPath.c_str());
            return 1;
        }
        std::printf("Wrote %zu bytes -> %s\n", sram.size(), dumpSramPath.c_str());
        return 0;
    }

    // --sram-only: write the save straight to cart SRAM (no ROM, no menu, no reboot) - for a game that is
    // ALREADY running (the menu won't answer then). A tracker reads its song from SRAM during playback, so
    // this loads a song into a live risa WITHOUT the boot-time SRAM reload a menu start does (that reload
    // path crashes risa when the SRAM is externally written).
    if (sramOnly) {
        const std::size_t n = std::min(srm.size(), Edio::SIZE_SRM_GAME);
        std::printf("Writing save '%s' -> cart SRAM (%zu bytes, no reboot)...\n", srmPath.c_str(), n);
        edio.memWR(Edio::ADDR_SRM, srm.data(), n);
        std::vector<std::uint8_t> check(n);
        edio.memRD(Edio::ADDR_SRM, check.data(), n);
        if (!std::equal(check.begin(), check.end(), srm.begin())) {
            std::fprintf(stderr, "error: cart SRAM verify failed (readback != save)\n");
            return 1;
        }
        std::printf("SRAM written + verified.\n");
        return 0;
    }

    N8Menu menu(edio);
    try {
        // Confirm the menu is actually running before anything else (a running game won't answer '*t').
        std::printf("Handshaking with the N8 menu...\n");
        try {
            menu.test();
        } catch (const std::exception& e) {
            std::fprintf(stderr,
                         "error: the N8 menu isn't responding (%s).\n"
                         "       Is a game already running? Power-cycle the console to the menu, then retry.\n",
                         e.what());
            return 1;
        }

        std::string bootPath = sdPath;
        if (bootPath.empty()) {
            // Upload the local ROM to usb-games/<name> on the SD, then boot that path.
            bootPath = "usb-games/" + baseName(romPath);
            std::printf("Uploading '%s' -> %s (%zu bytes)...\n", romPath.c_str(), bootPath.c_str(), rom.size());
            edio.fileOpen(bootPath, Edio::FA_WRITE | Edio::FA_CREATE_ALWAYS | Edio::FS_MAKEPATH);
            edio.fileWrite(rom);
            edio.fileClose();
        }

        // Restore the battery save the NATIVE way: write it into the menu's per-game save slot
        // (EDN8/gamedata/<rom>/bram.srm) BEFORE the menu loads the game, so the MENU itself copies it into
        // the cart SRAM at hand-off. Writing the cart SRAM directly over USB corrupts the running menu (which
        // uses that region), so we let the menu do the SRAM write at the one safe moment.
        if (!srm.empty()) {
            const std::string gd = "EDN8/gamedata/" + baseName(bootPath) + "/bram.srm";
            std::printf("Writing save '%s' -> %s (%zu bytes)...\n", srmPath.c_str(), gd.c_str(), srm.size());
            edio.fileOpen(gd, Edio::FA_WRITE | Edio::FA_CREATE_ALWAYS | Edio::FS_MAKEPATH);
            edio.fileWrite(srm);
            edio.fileClose();
        }

        std::printf("Installing '%s'...\n", bootPath.c_str());
        const int mapIdx = menu.appInstall(bootPath);
        std::printf("Installed (map index %d). Booting...\n", mapIdx);
        menu.appStart();
        std::printf("Booted. The N8 is now running '%s'.\n", bootPath.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

}  // namespace retroplug
