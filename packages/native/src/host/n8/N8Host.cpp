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

}  // namespace

N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir)
    : factory_(std::move(factory)), link_(factory_), lister_(std::move(lister)), configDir_(std::move(configDir)) {}

N8ConfigDto N8Host::getConfig() {
    N8ConfigDto c;
    c.ports        = lister_ ? lister_() : std::vector<N8PortDto>{};
    c.selectedPort = port_;
    c.connected    = link_.isConnected();
    c.enabled      = enabled_;
    c.lookaheadMs  = link_.lookaheadMs();
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

void N8Host::restore() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "r")) {
        char        line[512];
        std::string port;
        int         la = 10, en = 0;  // defaults: lookahead 10ms, disabled
        if (std::fgets(line, sizeof line, f)) {
            port = line;
            while (!port.empty() && (port.back() == '\n' || port.back() == '\r')) port.pop_back();
        }
        if (std::fgets(line, sizeof line, f)) la = std::atoi(line);
        if (std::fgets(line, sizeof line, f)) en = std::atoi(line);
        std::fclose(f);
        port_ = port;
        link_.setLookaheadMs(la < 0 ? 0 : la);
        enabled_ = (en != 0);
    }
    if (enabled_) connect(true);  // reconnect the persisted link (auto-picks if the saved port is empty)
}

void N8Host::save() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%d\n%d\n", port_.c_str(), link_.lookaheadMs(), enabled_ ? 1 : 0);
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
