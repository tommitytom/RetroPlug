#include "host/n8/N8Host.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "host/n8/N8Menu.hpp"

namespace retroplug {

namespace {

// Read a whole local file into bytes (the ROM / .srm to upload). Throws if it can't be opened.
std::vector<std::uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Write bytes to a local file (the dumped SRAM). Throws on failure.
void writeFileBytes(const std::string& path, const std::uint8_t* data, std::size_t n) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write file: " + path);
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    if (!f) throw std::runtime_error("write failed: " + path);
}

// The final path component (basename) of a local or SD path.
std::string baseNameOf(const std::string& path) {
    const auto i = path.find_last_of("/\\");
    return i == std::string::npos ? path : path.substr(i + 1);
}

// iNES mappers whose cartridge carries its own sound chip - the audio the N8's FPGA mixes in through
// `master_vol`. The TS twin is EXPANSION_MAPPERS in cli/sessions/n8-play.ts (the same policy behind
// `n8-play --rom`); keep the two in step.
bool mapperHasExpansionAudio(int mapper) {
    switch (mapper) {
        case 5:   // MMC5
        case 19:  // Namco 163
        case 24:  // VRC6
        case 26:  // VRC6 (alternate pinout)
        case 69:  // Sunsoft 5B
        case 85:  // VRC7
            return true;
        default:
            return false;
    }
}

// The iNES mapper number from a ROM image's header, or -1 when the bytes are not an iNES image. Twin of
// inesMapper in cli/sessions/n8-play.ts, down to ignoring NES 2.0's extra nibble (no expansion-audio mapper
// needs it).
int inesMapper(const std::vector<std::uint8_t>& rom) {
    if (rom.size() < 16 || rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) return -1;
    return (rom[7] & 0xF0) | (rom[6] >> 4);
}

// The mapper of the RUNNING cart, read off the N8's FPGA config block - what the OS told the cart it is, so
// it answers for a game the user booted from the cart's own file browser (the usual state when the menu says
// Connect). map_idx = scfg[0] | scfg[2]'s high nibble << 8, as decodeMapConfig does TS-side. Returns -1 when
// the block can't be read, which lands on "leave the register alone" - the safe answer.
int runningMapper(Edio& edio) {
    std::uint8_t cfg[Edio::SIZE_CFG] = {0};
    try {
        edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg);
    } catch (const std::exception&) {
        return -1;
    }
    return ((cfg[2] >> 4) << 8) | cfg[0];
}

}  // namespace

N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir)
    : factory_(std::move(factory)), link_(factory_), lister_(std::move(lister)), configDir_(std::move(configDir)) {
    // Every time the streaming link comes up - a Connect, a live port switch, the resume after an SD op, the
    // reconnect restore() does - set the expansion-audio master volume. Connect is the moment that matters:
    // a ROM load deliberately leaves streaming stopped, so the user comes back through here either way.
    link_.setOnConnected([this](Edio& edio) { applyExpVol(edio, -1); });
}

N8ConfigDto N8Host::getConfig() {
    N8ConfigDto c;
    c.ports        = lister_ ? lister_() : std::vector<N8PortDto>{};
    c.selectedPort = port_;
    c.connected    = link_.isConnected();
    c.enabled      = enabled_;
    c.lookaheadMs  = link_.lookaheadMs();
    c.expVol       = expVol_.load(std::memory_order_relaxed);
    c.bytes        = link_.bytesForwarded();
    c.error        = link_.lastError();
    return c;
}

void N8Host::setPort(const std::string& port) {
    if (sdWorker_.busy()) return;  // an SD op owns the port; don't race link_ connect/disconnect
    const bool wasStreaming = link_.isConnected();
    port_ = port;
    if (wasStreaming) {
        link_.disconnect();
        if (!port.empty()) link_.connect(port);
    }
    save();
}

void N8Host::connect(bool enable) {
    if (sdWorker_.busy()) return;  // an SD op owns the port; the menu also disables this row while busy
    if (enable) {
        if (port_.empty()) {  // auto-pick the first attached N8 (USB VID:PID 38df:0017)
            for (const N8PortDto& p : (lister_ ? lister_() : std::vector<N8PortDto>{}))
                if (p.isN8) { port_ = p.port; break; }
        }
        enabled_ = true;
        if (!port_.empty()) link_.connect(port_);
    } else {
        enabled_ = false;
        link_.disconnect();
    }
    save();
}

void N8Host::setLookahead(int ms) {
    if (sdWorker_.busy()) return;
    link_.setLookaheadMs(ms < 0 ? 0 : ms);
    save();
}

void N8Host::setExpVol(int v) {
    if (sdWorker_.busy()) return;  // an SD op owns the port; the reconnect below would fight it
    const int clamped = v < 0 ? EXP_VOL_AUTO : (v > 255 ? 255 : v);
    expVol_.store(clamped, std::memory_order_relaxed);
    // Bounce a live link so the pick is audible now rather than at the next Connect: the volume is written by
    // the connect path (the serial thread owns the Edio once it's up), which is the same reason setPort
    // reconnects to switch ports.
    if (link_.isConnected() && !port_.empty()) {
        link_.disconnect();
        link_.connect(port_);
    }
    save();
}

void N8Host::applyExpVol(Edio& edio, int mapperHint) {
    int value = expVol_.load(std::memory_order_relaxed);
    if (value == EXP_VOL_AUTO) {
        const int mapper = mapperHint >= 0 ? mapperHint : runningMapper(edio);
        // Not an expansion-audio cart (or nothing readable to say it is): leave the register as the N8 OS set
        // it. Auto only rescues the case it can recognise; it never overrides a deliberate device setting.
        if (mapper < 0 || !mapperHasExpansionAudio(mapper)) return;
        value = EXP_VOL_UNITY;
    }
    const std::uint8_t byte = static_cast<std::uint8_t>(value);
    edio.memWR(Edio::ADDR_EXP_VOL, &byte, 1);
}

void N8Host::restore() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "r")) {
        char        line[512];
        std::string port;
        int         la = 10, en = 0;        // defaults: lookahead 10ms, disabled
        int         ev = EXP_VOL_AUTO;      // a file written before the setting existed has no 4th line
        if (std::fgets(line, sizeof line, f)) {
            port = line;
            while (!port.empty() && (port.back() == '\n' || port.back() == '\r')) port.pop_back();
        }
        if (std::fgets(line, sizeof line, f)) la = std::atoi(line);
        if (std::fgets(line, sizeof line, f)) en = std::atoi(line);
        if (std::fgets(line, sizeof line, f)) ev = std::atoi(line);
        std::fclose(f);
        port_ = port;
        link_.setLookaheadMs(la < 0 ? 0 : la);
        enabled_ = (en != 0);
        expVol_.store(ev < 0 ? EXP_VOL_AUTO : (ev > 255 ? 255 : ev), std::memory_order_relaxed);
    }
    // Read before this line, so the reconnect below applies the restored volume rather than the default.
    if (enabled_) connect(true);  // reconnect the persisted link (auto-picks if the saved port is empty)
}

void N8Host::save() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%d\n%d\n%d\n", port_.c_str(), link_.lookaheadMs(), enabled_ ? 1 : 0,
                     expVol_.load(std::memory_order_relaxed));
        std::fclose(f);
    }
}

N8SdWorker::Job N8Host::controlJob(bool reconnectAfter, std::function<void(Edio&, N8SdWorker::Progress&)> op) {
    N8Link::PortFactory factory = factory_;  // copy for the worker thread
    const std::string   port    = port_;      // stable while busy (config edits are rejected)
    N8Link*             link    = &link_;      // sdWorker_ joins before link_ is destroyed, so this stays valid
    return [factory, port, link, reconnectAfter, op = std::move(op)](N8SdWorker::Progress& p) {
        if (port.empty()) throw std::runtime_error("no N8 port selected");
        const bool wasStreaming = link->isConnected();
        link->disconnect();  // release the exclusive serial port so the control Edio can open it
        std::unique_ptr<ISerialPort> sp;
        try {
            sp = factory(port);  // exclusive OS open; throws if busy / absent
        } catch (const std::exception& e) {
            if (wasStreaming) link->connect(port);
            throw std::runtime_error("cannot open " + port + ": " + e.what());
        }
        Edio edio(*sp);
        try {
            edio.connect();  // N8 hardware handshake (works whether the menu or a game is running)
            op(edio, p);
        } catch (...) {
            if (wasStreaming) link->connect(port);  // a failed op leaves the previously-running state streaming
            throw;
        }
        if (reconnectAfter && wasStreaming) link->connect(port);
    };
}

void N8Host::startLoadRom(const std::string& romPath) {
    if (sdWorker_.busy()) return;
    // reconnectAfter=false: a successful load boots a NEW ROM, so the old stream is stale - leave it stopped.
    sdWorker_.start("load", controlJob(false, [this, romPath](Edio& edio, N8SdWorker::Progress& p) {
        p.phase("Reading ROM");
        const std::vector<std::uint8_t> rom = readFileBytes(romPath);
        if (rom.empty()) throw std::runtime_error("ROM file is empty: " + romPath);
        N8Menu menu(edio);
        p.phase("Checking menu");
        menu.test();  // clear error if the cart isn't at its file browser
        const std::string name     = baseNameOf(romPath);
        const std::string bootPath = "usb-games/" + name;
        p.phase("Uploading");
        p.total(rom.size());
        edio.fileOpen(bootPath, Edio::FA_WRITE | Edio::FA_CREATE_ALWAYS | Edio::FS_MAKEPATH);
        constexpr std::size_t CHUNK = 8192;
        for (std::size_t off = 0; off < rom.size(); off += CHUNK) {
            const std::size_t n = std::min(CHUNK, rom.size() - off);
            edio.fileWrite(rom.data() + off, n);
            p.advance(n);
        }
        edio.fileClose();
        p.phase("Booting");
        const int mapIndex = menu.appInstall(bootPath);  // menu parses iNES + sources the core from SD
        menu.appStart();
        // The mapload just reloaded the OS's stored master_vol, so set ours on the ROM we hold - its header
        // names the mapper outright, no need to ask the cart. The next Connect applies it again (streaming is
        // stopped below), which is what covers a cart booted from the N8's own file browser.
        applyExpVol(edio, inesMapper(rom));
        p.result("Booted " + name + " (map " + std::to_string(mapIndex) + ") - streaming stopped");
        // The old stream died with the previous ROM (controlJob left link_ disconnected + doesn't reconnect a
        // load), so commit that: clear the enabled toggle + persist. Without this, enabled_ stays true with the
        // link down, so the Status row reads a forever "Connecting..." and a restart would try to resume a dead
        // stream. Race-free here: config writers (connect/setPort/setLookahead) are gated on the worker being
        // busy, which we still are; enabled_ is atomic; and this only runs on success (a throw skips it).
        enabled_.store(false, std::memory_order_relaxed);
        save();
    }));
}

void N8Host::startDumpSram(const std::string& destPath) {
    if (sdWorker_.busy()) return;
    sdWorker_.start("dump", controlJob(true, [destPath](Edio& edio, N8SdWorker::Progress& p) {
        p.phase("Reading SRAM");
        p.total(Edio::SIZE_SRM_GAME);
        std::vector<std::uint8_t> buf(Edio::SIZE_SRM_GAME);
        constexpr std::size_t CHUNK = 4096;
        for (std::size_t off = 0; off < buf.size(); off += CHUNK) {
            const std::size_t n = std::min(CHUNK, buf.size() - off);
            edio.memRD(Edio::ADDR_SRM + static_cast<std::int32_t>(off), buf.data() + off, n);
            p.advance(n);
        }
        p.phase("Saving");
        writeFileBytes(destPath, buf.data(), buf.size());
        p.result("Dumped 64 KB to " + baseNameOf(destPath));
    }));
}

void N8Host::startRestoreSram(const std::string& srmPath) {
    if (sdWorker_.busy()) return;
    sdWorker_.start("restore", controlJob(true, [srmPath](Edio& edio, N8SdWorker::Progress& p) {
        p.phase("Reading file");
        const std::vector<std::uint8_t> srm = readFileBytes(srmPath);
        const std::size_t n = std::min(srm.size(), Edio::SIZE_SRM_GAME);
        if (n == 0) throw std::runtime_error("save file is empty: " + srmPath);
        p.phase("Writing SRAM");
        edio.memWR(Edio::ADDR_SRM, srm.data(), n);  // straight to cart SRAM (a running game); menu would corrupt
        p.phase("Verifying");
        p.total(n);
        std::vector<std::uint8_t> check(n);
        constexpr std::size_t CHUNK = 4096;
        for (std::size_t off = 0; off < n; off += CHUNK) {
            const std::size_t c = std::min(CHUNK, n - off);
            edio.memRD(Edio::ADDR_SRM + static_cast<std::int32_t>(off), check.data() + off, c);
            p.advance(c);
        }
        for (std::size_t i = 0; i < n; i++)
            if (check[i] != srm[i]) throw std::runtime_error("cart SRAM verify failed (readback != save)");
        p.result("Restored " + std::to_string(n) + " bytes");
    }));
}

}  // namespace retroplug
