#include "N8Load.hpp"

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
    std::string serialPortName;

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

    if (sdPath.empty() && romPath.empty()) romPath = kDefaultRom;  // convenience default

    // Read the local ROM up front (before touching hardware) so a bad path fails fast.
    std::vector<std::uint8_t> rom;
    if (sdPath.empty()) {
        if (!slurp(romPath, rom)) {
            std::fprintf(stderr, "error: cannot read ROM '%s'\n", romPath.c_str());
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
