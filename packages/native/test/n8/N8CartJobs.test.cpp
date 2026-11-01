// Guards the cart's capability jobs (N8Host::submitCartJob, docs/modular-graph-phase-c5-design.md section 6) and the
// pause they run in (N8Link::PausedJob): a job runs on the link's serial thread over the port the link already holds,
// what was pushed before the pause is delivered first, a push during it is dropped and counted, and the job completes
// once streaming has resumed, on the same port and with the reliable link's sequence state intact. A ROM upload boots
// through the cart's menu and stops streaming; the battery jobs read and write the battery file the N8 OS keeps on the
// SD card, at the menu only, and never the cart's own battery.
//
// The fake device below is a model of the cart's USB side, enough for these: it parses Edio's command frames as they
// are written (status, memory reads and writes, the SD directory and file API, with a failure it can inject into a
// directory load, a file write or a file read), counts the files open on the card, keeps a 64 KB battery (logged if
// anything touches it) and the FPGA config block (map_idx 255 while the menu shows), answers the menu's '*' commands
// while the menu is showing, and plays BlipToaster's end of the reliable link while a game that speaks it runs. Every
// event (a FIFO write, a battery read, a file block) lands in one ordered log.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "host/engine/CapabilityJobs.hpp"
#include "host/n8/Edio.hpp"
#include "host/n8/N8CartLink.hpp"
#include "host/n8/N8Host.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8CartLink;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;
using rp::host::Job;
using rp::host::JobRequest;
using Bytes = std::vector<std::uint8_t>;

namespace {

// The cart: what every port opened on it shares, under one lock (the serial thread drives it while the test reads).
struct FakeN8 {
    std::mutex               mu;
    Bytes                    sram = Bytes(Edio::SIZE_SRM_GAME, 0);
    Bytes                    cfg  = Bytes(Edio::SIZE_CFG, 0);
    std::map<std::string, Bytes> files;
    bool                     menu = true;           // the file browser is showing (it answers '*' commands)
    bool                     linkRom = false;       // the running game answers the reliable link (BlipToaster)
    bool                     linkDeaf = false;      // ... until it stops answering (it read no frame and acks nothing)
    int                      linkAckDelayMs = 0;    // how long the game takes to write a frame's ack back
    int                      fileDelayMs = 0;       // likewise each SD file write
    int                      fileReadDelayMs = 0;   // likewise each SD file read block
    bool                     failNextWrite = false; // the next SD file write answers its first block with an error
    int                      throwOnFlushInput = 0; // the Nth flushInput from now throws, as a port that just died does
    bool                     failNextRead = false;  // the next SD file read block answers an error
    bool                     failNextList = false;  // the next directory load answers a disk error
    int                      filesOpen = 0;         // files opened on the card and not yet closed
    std::uint16_t            mapIndex = 69;         // what '*n' answers
    std::string              booted;                // the path '*s' started
    // "fifo:90 3C 7F", "srm-rd", "srm-wr", "file-wr", "file-rd", ..., in order
    std::vector<std::string> log;
    std::vector<Bytes>       applied;               // messages the reliable link delivered to the game
    int                      syncs = 0;             // F4 syncs the game saw
    std::vector<int>         frameSeqs;             // the seq of every intact frame it read
    int                      opens = 0, openNow = 0;

    std::size_t count(const std::string& prefix) {
        std::lock_guard<std::mutex> l(mu);
        return static_cast<std::size_t>(
            std::count_if(log.begin(), log.end(), [&](const std::string& e) { return e.rfind(prefix, 0) == 0; }));
    }
};

std::string hex(const std::uint8_t* d, std::size_t n) {
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += digits[d[i] >> 4];
        s += digits[d[i] & 15];
    }
    return s;
}

// One open port on the cart: Edio's frames parsed as they arrive, the replies queued for the host's reads.
struct FakeN8Port : ISerialPort {
    explicit FakeN8Port(FakeN8& dev) : dev_(dev) {
        std::lock_guard<std::mutex> l(dev_.mu);
        ++dev_.opens;
        ++dev_.openNow;
    }
    ~FakeN8Port() override {
        std::lock_guard<std::mutex> l(dev_.mu);
        --dev_.openNow;
    }
    std::size_t write(const std::uint8_t* data, std::size_t n) override {
        int delayMs = 0;
        {
            std::lock_guard<std::mutex> l(dev_.mu);
            in_.insert(in_.end(), data, data + n);
            delayMs = parse();
        }
        if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::lock_guard<std::mutex> l(dev_.mu);
        releaseAcks(true);  // whatever the game has had time to write since the last read
        std::size_t i = 0;
        while (i < n && !out_.empty()) {
            b[i++] = out_.front();
            out_.pop_front();
        }
        return i;
    }
    void flushInput() override {
        std::lock_guard<std::mutex> l(dev_.mu);
        out_.clear();
        if (dev_.throwOnFlushInput > 0 && --dev_.throwOnFlushInput == 0)
            throw std::runtime_error("fake port: flushInput failed");
    }

private:
    static std::uint32_t le32(const Bytes& b, std::size_t at) {
        return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    }
    void reply(std::initializer_list<std::uint8_t> bytes) { out_.insert(out_.end(), bytes); }

    // An ack the game still owes the link: it writes one when its FIFO runs empty after a frame, so the ack can be
    // in its own queue when the host's next command arrives, and the MCU puts it ahead of that command's reply.
    struct PendingAck {
        std::chrono::steady_clock::time_point due;
        std::deque<std::uint8_t>              bytes;
    };
    void replyAck(std::initializer_list<std::uint8_t> bytes) {
        if (dev_.linkAckDelayMs <= 0) {
            reply(bytes);
            return;
        }
        acks_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(dev_.linkAckDelayMs),
                         std::deque<std::uint8_t>(bytes)});
    }
    // Hand the host the acks the game has written: only those now due, or everything it still owes - what a command
    // arriving first sees, since the cart answers it behind what its queue holds.
    void releaseAcks(bool dueOnly) {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = acks_.begin(); it != acks_.end();) {
            if (!dueOnly || now >= it->due) {
                out_.insert(out_.end(), it->bytes.begin(), it->bytes.end());
                it = acks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Consume every complete command in in_; the delay a battery read asks for, applied outside the lock.
    int parse() {
        releaseAcks(false);  // ahead of any reply below: what the game owes a frame is written first
        int delay = 0;
        for (;;) {
            if (fileLeft_ > 0) {  // the data of a file write, a 1 KB block after each ack
                const std::size_t take = std::min<std::size_t>({in_.size(), fileLeft_, blockLeft_});
                if (take == 0) return delay;
                file_.insert(file_.end(), in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(take));
                in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(take));
                fileLeft_ -= take;
                blockLeft_ -= take;
                if (blockLeft_ == 0 && fileLeft_ > 0) {
                    blockLeft_ = Edio::ACK_BLOCK_SIZE;
                    reply({0});
                }
                if (fileLeft_ == 0) {
                    Bytes& f = dev_.files[filePath_];
                    f.insert(f.end(), file_.begin(), file_.end());
                }
                continue;
            }
            if (in_.size() < 4) return delay;
            const std::uint8_t cmd = in_[2];
            std::size_t need = 4;
            if (cmd == Edio::CMD_MEM_RD) need = 13;
            if (cmd == Edio::CMD_MEM_WR) need = in_.size() >= 13 ? 13 + le32(in_, 8) : 13;
            if (cmd == Edio::CMD_F_FOPN) need = in_.size() >= 7 ? 7 + (in_[5] | (in_[6] << 8)) : 7;
            if (cmd == Edio::CMD_F_FWR) need = 8;
            if (cmd == Edio::CMD_F_DEL) need = in_.size() >= 6 ? 6 + (in_[4] | (in_[5] << 8)) : 6;
            if (cmd == Edio::CMD_F_DIR_LD) need = in_.size() >= 7 ? 7 + (in_[5] | (in_[6] << 8)) : 7;
            if (cmd == Edio::CMD_F_DIR_GET) need = 10;
            if (cmd == Edio::CMD_F_FRD) need = 8;
            if (in_.size() < need) return delay;
            const Bytes c(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(need));
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(need));
            if (cmd == Edio::CMD_STATUS) {
                reply({status_, 0xA5});
                status_ = 0;
            } else if (cmd == Edio::CMD_F_DIR_LD) {
                const std::string dir = std::string(c.begin() + 7, c.end()) + "/";
                dirList_.clear();
                for (const auto& [path, bytes] : dev_.files)
                    if (path.rfind(dir, 0) == 0 && path.find('/', dir.size()) == std::string::npos)
                        dirList_.push_back({path.substr(dir.size()), bytes.size()});
                if (dirList_.empty()) status_ = 0x05;  // FR_NO_PATH
                if (dev_.failNextList) {
                    dev_.failNextList = false;
                    dirList_.clear();
                    status_ = 0x01;  // FR_DISK_ERR
                }
            } else if (cmd == Edio::CMD_F_DIR_SIZE) {
                const auto n = static_cast<std::uint16_t>(dirList_.size());
                reply({static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8)});
            } else if (cmd == Edio::CMD_F_DIR_GET) {
                for (const auto& [name, size] : dirList_) {
                    const auto s = static_cast<std::uint32_t>(size);
                    reply({0, static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(s >> 8),
                           static_cast<std::uint8_t>(s >> 16), static_cast<std::uint8_t>(s >> 24), 0, 0, 0, 0, 0,
                           static_cast<std::uint8_t>(name.size()), static_cast<std::uint8_t>(name.size() >> 8)});
                    out_.insert(out_.end(), name.begin(), name.end());
                }
            } else if (cmd == Edio::CMD_F_FRD && dev_.failNextRead) {
                dev_.failNextRead = false;
                reply({0x01});  // FR_DISK_ERR: no data follows
                dev_.log.push_back("file-rd-fail");
            } else if (cmd == Edio::CMD_F_FRD) {
                const std::uint32_t block = le32(c, 4);
                const Bytes& file = dev_.files[filePath_];
                reply({0});
                for (std::uint32_t i = 0; i < block; ++i)
                    out_.push_back(readPos_ + i < file.size() ? file[readPos_ + i] : 0);
                readPos_ += block;
                dev_.log.push_back("file-rd");
                delay = dev_.fileReadDelayMs;
            } else if (cmd == Edio::CMD_MEM_RD) {
                const std::uint32_t addr = le32(c, 4), size = le32(c, 8);
                for (std::uint32_t i = 0; i < size; ++i) out_.push_back(peek(addr + i));
                if (addr >= Edio::ADDR_SRM && addr < Edio::ADDR_SRM + Edio::SIZE_SRM_GAME) dev_.log.push_back("srm-rd");
            } else if (cmd == Edio::CMD_MEM_WR) {
                const std::uint32_t addr = le32(c, 4), size = le32(c, 8);
                const std::uint8_t* data = c.data() + 13;
                if (addr == static_cast<std::uint32_t>(Edio::ADDR_FIFO)) {
                    dev_.log.push_back("fifo:" + hex(data, size));
                    for (std::uint32_t i = 0; i < size; ++i) fifoByte(data[i]);
                } else {
                    for (std::uint32_t i = 0; i < size; ++i) poke(addr + i, data[i]);
                    if (addr >= Edio::ADDR_SRM && addr < Edio::ADDR_SRM + Edio::SIZE_SRM_GAME)
                        dev_.log.push_back("srm-wr");
                }
            } else if (cmd == Edio::CMD_F_DEL) {
                const std::string path(c.begin() + 6, c.end());
                dev_.files.erase(path);
                dev_.log.push_back("file-del:" + path);
            } else if (cmd == Edio::CMD_F_FOPN) {
                filePath_ = std::string(c.begin() + 7, c.end());
                readPos_ = 0;
                if (c[4] & Edio::FA_WRITE) dev_.files[filePath_].clear();
                else if (!dev_.files.count(filePath_)) status_ = 0x04;  // FR_NO_FILE
                if (status_ == 0) ++dev_.filesOpen;
            } else if (cmd == Edio::CMD_F_FCLOSE) {
                if (dev_.filesOpen > 0) --dev_.filesOpen;
            } else if (cmd == Edio::CMD_F_FWR) {
                dev_.log.push_back("file-wr");
                delay = dev_.fileDelayMs;
                fileLeft_ = le32(c, 4);
                blockLeft_ = Edio::ACK_BLOCK_SIZE;
                file_.clear();
                if (dev_.failNextWrite && fileLeft_ > 0) {  // the block is refused: no data follows
                    dev_.failNextWrite = false;
                    fileLeft_ = 0;
                    reply({0x01});
                } else if (fileLeft_ > 0) {
                    reply({0});
                }
            }
        }
    }
    std::uint8_t peek(std::uint32_t addr) {
        if (addr >= Edio::ADDR_SRM && addr < Edio::ADDR_SRM + Edio::SIZE_SRM_GAME)
            return static_cast<std::uint8_t>(dev_.sram[addr - Edio::ADDR_SRM]);
        // The menu runs on the OS's own mapper, 255.
        if (dev_.menu && addr == static_cast<std::uint32_t>(Edio::ADDR_CFG)) return 0xFF;
        if (dev_.menu && addr == static_cast<std::uint32_t>(Edio::ADDR_CFG) + 2) return 0;
        if (addr >= Edio::ADDR_CFG && addr < Edio::ADDR_CFG + Edio::SIZE_CFG) return dev_.cfg[addr - Edio::ADDR_CFG];
        return 0;
    }
    void poke(std::uint32_t addr, std::uint8_t v) {
        if (addr >= Edio::ADDR_SRM && addr < Edio::ADDR_SRM + Edio::SIZE_SRM_GAME) dev_.sram[addr - Edio::ADDR_SRM] = v;
        if (addr >= Edio::ADDR_CFG && addr < Edio::ADDR_CFG + Edio::SIZE_CFG) dev_.cfg[addr - Edio::ADDR_CFG] = v;
    }

    // What the cart's side of the FIFO makes of a byte: the menu's commands, or the game's reliable link.
    void fifoByte(std::uint8_t b) {
        if (dev_.menu) {
            menu_.push_back(b);
            if (menu_[0] != '*') { menu_.clear(); return; }      // not a command: the menu skips it
            if (menu_.size() < 2) return;
            if (menu_[1] == 't') { reply({'k'}); menu_.clear(); return; }
            if (menu_[1] == 's') {  // the game boots, and the FPGA's config block names its mapper
                dev_.menu = false;
                dev_.booted = menuPath_;
                dev_.cfg[0] = static_cast<std::uint8_t>(dev_.mapIndex);
                dev_.cfg[2] = static_cast<std::uint8_t>((dev_.mapIndex >> 8) << 4);
                menu_.clear();
                return;
            }
            if (menu_[1] == 'n') {                                // '*n', then a length-prefixed path
                if (menu_.size() < 4) return;
                const std::size_t len = menu_[2] | (menu_[3] << 8);
                if (menu_.size() < 4 + len) return;
                menuPath_ = std::string(menu_.begin() + 4, menu_.end());
                reply({0x00, static_cast<std::uint8_t>(dev_.mapIndex), static_cast<std::uint8_t>(dev_.mapIndex >> 8)});
                menu_.clear();
                return;
            }
            menu_.clear();
            return;
        }
        if (!dev_.linkRom || dev_.linkDeaf) return;
        // BlipToaster's end of the link, for intact traffic: a sync sets the next seq, a frame carrying it is applied,
        // and each is acknowledged with F5 <next expected> <last header read>.
        switch (link_) {
            case Idle:
                if (b == 0xF4) link_ = WantSync;
                else if (b == 0xF5) link_ = WantSeq;
                return;
            case WantSync:
                next_ = b;
                ++dev_.syncs;
                replyAck({0xF5, next_, static_cast<std::uint8_t>((next_ - 1) & 0x7F)});
                link_ = Idle;
                return;
            case WantSeq:
                seq_ = b;
                msg_.clear();
                link_ = InMsg;
                return;
            case InMsg: {
                msg_.push_back(b);
                const std::size_t len = (msg_[0] & 0xE0) == 0xC0 ? 2 : 3;
                if (msg_.size() == len) link_ = WantSum;
                return;
            }
            case WantSum: {
                unsigned sum = seq_;
                for (auto m : msg_) sum += m;
                link_ = Idle;
                if (b != (sum & 0x7F)) return;
                dev_.frameSeqs.push_back(seq_);
                if (seq_ == next_) {
                    dev_.applied.push_back(msg_);
                    next_ = static_cast<std::uint8_t>((next_ + 1) & 0x7F);
                }
                replyAck({0xF5, next_, seq_});
                return;
            }
        }
    }

    FakeN8&                  dev_;
    Bytes                    in_;
    std::deque<std::uint8_t> out_;
    std::string              filePath_;
    std::size_t              readPos_ = 0;
    std::uint8_t             status_ = 0;  // what the next CMD_STATUS answers (a FatFs error, or 0)
    std::vector<std::pair<std::string, std::size_t>> dirList_;
    Bytes                    file_;
    std::size_t              fileLeft_ = 0, blockLeft_ = 0;
    Bytes                    menu_;
    std::deque<PendingAck>   acks_;  // what the game owes the link, in the order it will write it
    std::string              menuPath_;
    enum { Idle, WantSync, WantSeq, InMsg, WantSum } link_ = Idle;
    std::uint8_t             next_ = 0, seq_ = 0;
    Bytes                    msg_;
};

N8Link::PortFactory factoryFor(FakeN8& dev) {
    return [&dev](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakeN8Port>(dev); };
}

std::string tempDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-n8-cartjobs-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");
    return dir.string();
}

template <class Pred>
bool waitUntil(Pred pred, int ms = 3000) {
    for (int i = 0; i < ms && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return pred();
}

JobRequest request(const char* capability, const char* op) {
    JobRequest r;
    r.nodeId     = "cart";
    r.capability = capability;
    r.op         = op;
    return r;
}

// A connected host on the fake cart, streaming with no lookahead. `link` false skips the reliable link's probe.
struct Rig {
    FakeN8     dev;
    N8Host     host{factoryFor(dev), [] { return std::vector<N8PortDto>{{"/dev/fake", true}}; }, tempDir()};
    N8CartLink cart{host};
    int        owner = 0;  // the address that stands for the node submitting
    // Every case ends with no file left open on the card, whatever its jobs did.
    ~Rig() {
        std::lock_guard<std::mutex> l(dev.mu);
        CHECK(dev.filesOpen == 0);
    }

    void connect() {
        host.setPort("/dev/fake");
        host.connect(true);
        REQUIRE(host.link().isConnected());
        host.link().setLookaheadMs(0);
    }
    bool submit(const JobRequest& r, Job& job, std::string* error = nullptr) {
        std::string why;
        const bool ok = cart.submitJob(r, job, &owner, &why);
        if (error) *error = why;
        return ok;
    }
    void push(std::initializer_list<std::uint8_t> bytes) {
        const Bytes b(bytes);
        cart.push(0, b.data(), b.size(), false, 48000.0);
    }
};

}  // namespace

namespace {

// The battery file the N8 OS keeps for game.nes, and a save to put there.
const char* const kGameBattery = "EDN8/gamedata/game.nes/bram.srm";
Bytes saveOf(std::size_t n, unsigned seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(i * seed + 1);
    return b;
}
JobRequest battery(const char* op, const char* rom = "game.nes") {
    auto r = request(RP_SYSTEM_SRAM_INTERFACE, op);
    r.name = rom;
    return r;
}

// An iNES image of mapper 69 (Sunsoft 5B, a cart with its own sound chip), `kb` KB of PRG behind its header.
Bytes inesImage(std::size_t kb) {
    Bytes rom = {'N', 'E', 'S', 0x1A, 16, 4, 0x50, 0x40};
    rom.resize(16 + kb * 1024, 0xEA);
    return rom;
}

JobRequest uploadOf(const Bytes& rom) {
    auto r = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    r.bytes = rom;
    r.name = "game.nes";
    return r;
}

}  // namespace

TEST_CASE("a dump job pauses streaming on the port it has, delivers what was pushed first, reads the battery file "
          "with progress, and resumes before it completes", "[n8][cart-jobs]") {
    Rig rig;
    const Bytes save = saveOf(3000, 7);
    rig.dev.files[kGameBattery] = save;
    rig.dev.fileReadDelayMs = 20;
    rig.connect();

    // A note pushed 20 ms ahead and then a job at once: the note goes out at its own time, before the first read.
    rig.host.link().setLookaheadMs(20);
    rig.push({0x90, 0x3C, 0x7F});
    rig.push({0x80, 0x3C, 0x00});
    rig.host.link().setLookaheadMs(0);
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(rig.host.cartJobBusy());

    // While it runs: the link reads disconnected, and a push goes nowhere and is counted.
    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
    CHECK_FALSE(rig.cart.connected());
    rig.push({0x90, 0x40, 0x7F});
    CHECK(rig.cart.dropped() == 1);

    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == save);
    CHECK(job.total.load() == save.size());     // a block a step, in bytes
    CHECK(job.progress.load() == save.size());
    // Completed after streaming resumed, on the one port the link opened.
    CHECK(rig.cart.connected());
    CHECK_FALSE(rig.host.cartJobBusy());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.opens == 1);
        const auto at = [&](const std::string& e) {
            return std::find(rig.dev.log.begin(), rig.dev.log.end(), e) - rig.dev.log.begin();
        };
        const auto end = static_cast<std::ptrdiff_t>(rig.dev.log.size());
        CHECK(at("fifo:90 3C 7F") < end);
        CHECK(at("fifo:90 3C 7F") < at("fifo:80 3C 00"));
        CHECK(at("fifo:80 3C 00") < at("file-rd"));
    }
    CHECK(rig.dev.count("file-rd") == (save.size() + 511) / 512);  // one 512-byte block a step
    CHECK(rig.dev.count("srm-") == 0);                              // the cart's own battery: never
    CHECK(rig.dev.count("fifo:90 40") == 0);  // the push the pause dropped never reached the cart

    rig.push({0x91, 0x41, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:91 41 7F") == 1; }));
}

TEST_CASE("a restore job writes the battery file, creating the folder, for the game's next boot", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    const Bytes save = saveOf(8192 + 100, 13);
    auto r = battery("restore", "fresh.nes");
    r.bytes = save;
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"EDN8/gamedata/fresh.nes/bram.srm","created":true})");
    CHECK(job.total.load() == save.size());
    CHECK(job.progress.load() == save.size());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files["EDN8/gamedata/fresh.nes/bram.srm"] == save);
    }
    CHECK(rig.dev.count("srm-") == 0);
    CHECK(rig.cart.connected());

    // A dump reads back what the restore wrote.
    Job back;
    REQUIRE(rig.submit(battery("dump", "fresh.nes"), back));
    REQUIRE(waitUntil([&] { return back.finished(); }));
    CHECK(back.result == save);

    // Arguments refused at submission, the record untouched.
    Job none;
    std::string why;
    auto empty = battery("restore");
    CHECK_FALSE(rig.submit(empty, none, &why));
    CHECK(why == "a battery restore takes 1 to 1048576 bytes");
    auto bad = battery("dump", "../x.nes");
    CHECK_FALSE(rig.submit(bad, none, &why));
    CHECK(why == "a battery job names the ROM's file, a plain file name");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), none, &why));
    CHECK(why == "a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
    CHECK(none.state.load() == Job::Queued);
}

TEST_CASE("battery jobs refuse a running game before the stream pauses, so the game keeps its notes, and a dump "
          "finds no file a game never saved", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.menu = false;  // a game runs
    rig.dev.files[kGameBattery] = saveOf(512, 3);
    rig.connect();
    // Refused at submission, from the config block read between chunks: the stream never pauses, so a note pushed
    // afterwards reaches the cart instead of being dropped for a job that was going to refuse anyway.
    for (const char* op : {"dump", "restore"}) {
        auto r = battery(op);
        r.bytes = Bytes(16, 1);
        Job job;
        std::string why;
        CHECK_FALSE(rig.submit(r, job, &why));
        CHECK(why == "the battery file can only be reached at the N8 menu: power-cycle to the menu first");
        CHECK(job.state.load() == Job::Queued);  // the record is untouched
        CHECK_FALSE(rig.host.cartJobBusy());
        CHECK(rig.cart.connected());
        rig.push({static_cast<std::uint8_t>(op[0] == 'd' ? 0x90 : 0x91), 0x3C, 0x7F});
    }
    CHECK(rig.cart.dropped() == 0);
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1 && rig.dev.count("fifo:91 3C 7F") == 1; }));
    CHECK(rig.dev.count("file-") == 0);
    CHECK(rig.dev.count("srm-") == 0);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files[kGameBattery] == saveOf(512, 3));  // untouched
        rig.dev.menu = true;
    }
    Job missing;
    REQUIRE(rig.submit(battery("dump", "never.nes"), missing));
    REQUIRE(waitUntil([&] { return missing.finished(); }));
    CHECK(missing.state.load() == Job::Failed);
    CHECK(missing.error == "no battery file for never.nes (EDN8/gamedata/never.nes/bram.srm)");
    CHECK(rig.cart.connected());
}

TEST_CASE("one cart job at a time, none while disconnected, and an operation the cart does not have is refused",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(2048, 5);
    rig.dev.fileReadDelayMs = 50;
    std::string why;
    Job early;
    CHECK_FALSE(rig.submit(battery("dump"), early, &why));
    CHECK(why == "not connected");

    rig.connect();
    Job job, second;
    REQUIRE(rig.submit(battery("dump"), job));
    CHECK_FALSE(rig.submit(battery("dump"), second, &why));
    CHECK(why == "busy: the cart is running another job");
    // The host's own config writes wait for the job too: a port switch or a disconnect would pull the port from under
    // it.
    rig.host.connect(false);
    CHECK(rig.host.getConfig().enabled);
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));

    CHECK_FALSE(rig.submit(request(RP_SYSTEM_MEMORY_INTERFACE, "read"), second, &why));
    CHECK(why == "the cart presents no rp.system.memory/1");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "erase"), second, &why));
    CHECK(why == "rp.system.sram/1 has no operation 'erase'");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "header"), second, &why));
    CHECK(why == "nothing this host booted is running");
}

TEST_CASE("a ROM upload boots through the menu, stops streaming and clears the enabled toggle, and says so",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();  // at the menu: the link streams to it, which skips what is not a command
    REQUIRE(rig.host.getConfig().enabled);
    Bytes rom = {'N', 'E', 'S', 0x1A, 16, 4, 0x50, 0x40};  // mapper 69 (Sunsoft 5B): a cart with its own sound chip
    rom.resize(16 + 20000, 0xEA);

    auto r = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    r.bytes = rom;
    r.name  = "game.nes";
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"usb-games/game.nes","mapIndex":69,"streaming":"stopped"})");
    CHECK(job.progress.load() == rom.size());

    // The game runs, streaming stopped (the port given back), the toggle cleared, the expansion volume at unity for a
    // mapper with expansion audio (the host's default is AUTO).
    CHECK_FALSE(rig.cart.connected());
    CHECK_FALSE(rig.host.getConfig().enabled);
    CHECK_FALSE(rig.host.cartJobBusy());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files["usb-games/game.nes"] == rom);
        CHECK(rig.dev.booted == "usb-games/game.nes");
        CHECK_FALSE(rig.dev.menu);
        CHECK(rig.dev.cfg[3] == N8Host::EXP_VOL_UNITY);
        CHECK(rig.dev.openNow == 0);
    }

    // What it booted answers at once, with no transfer and the link down.
    auto header = request(RP_SYSTEM_ROM_INTERFACE, "header");
    header.length = 16;
    Job h, t;
    REQUIRE(rig.submit(header, h));
    CHECK(h.state.load() == Job::Done);
    CHECK(h.result == Bytes(rom.begin(), rom.begin() + 16));
    REQUIRE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "title"), t));
    CHECK(std::string(t.result.begin(), t.result.end()) == "game.nes");

    // Reconnecting reaches the new game.
    rig.host.connect(true);
    REQUIRE(rig.cart.connected());
    // Still the game this host booted: what it booted still answers after the reconnect.
    Job kept;
    REQUIRE(rig.submit(header, kept));
    CHECK(kept.result == Bytes(rom.begin(), rom.begin() + 16));

    // An upload with no menu to load it is refused before it pauses anything, so no byte (the menu's own commands
    // included) reaches the game's FIFO, and the cart keeps streaming to it.
    const std::size_t menuCommands = rig.dev.count("fifo:2A");  // the first upload's *t, *n and *s
    Job again;
    std::string why;
    CHECK_FALSE(rig.submit(r, again, &why));
    CHECK(why ==
          "the cart is running a game, which has no menu to load a ROM: power-cycle the console to its menu first");
    CHECK(again.state.load() == Job::Queued);
    CHECK(rig.dev.count("fifo:2A") == menuCommands);
    CHECK(rig.cart.connected());
    CHECK(rig.host.getConfig().enabled);
    CHECK_FALSE(rig.host.cartJobBusy());

    auto unnamed = r;
    unnamed.name = "../x.nes";
    CHECK_FALSE(rig.submit(unnamed, again, &why));
    CHECK(why == "an upload names the file it is stored under, a plain file name");
}

TEST_CASE("the reliable link keeps its sequence across a pause: no new sync, the next frame carries the next seq",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;  // BlipToaster, which answers the probe
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    // The fake answers the config block as the menu does while a game runs. That is the one combination which takes a
    // job with the reliable link up (a real cart's link belongs to a game, and a running game is refused before the
    // pause), and it is what this case pins: the pause keeps the sequence state.
    rig.dev.cfg[0] = 0xFF;  // map_idx 255: the menu, which the job's own first step checks again
    rig.dev.cfg[2] = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F4") + rig.dev.count("fifo:F5") >= 2; }));
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 3));
    REQUIRE(rig.cart.connected());
    rig.push({0x80, 0x3C, 0x00});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 2;
    }));
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.applied[0] == Bytes{0x90, 0x3C, 0x7F});
    CHECK(rig.dev.applied[1] == Bytes{0x80, 0x3C, 0x00});
    // The probe's sync and the first frame's: none after the pause, whose frame simply carries seq 1.
    CHECK(rig.dev.syncs == 2);
    CHECK(rig.dev.frameSeqs == std::vector<int>{0, 1});
    CHECK(rig.dev.opens == 1);
    CHECK(rig.host.link().reliable());
}

TEST_CASE("the menu probe waits out an ack the game still owes, so its config read sees only its reply",
          "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    // A game runs, the link is up, and the game writes a frame's ack a little after the host writes the frame, so the
    // ack is still in the cart's queue when a submission's config read arrives: the cart's MCU puts what it owes ahead
    // of the reply. Reading across that is what a probe must not do - the ack becomes the read's first bytes, and the
    // reply's own tail is left in the input to be parsed as link traffic.
    //
    // The last three bytes of the config block are an ack that names a frame two ahead, so a tail parsed as link
    // traffic would point the sequence past the frame the game is about to acknowledge.
    const auto cfgTail = [](FakeN8& dev) {
        dev.cfg[13] = 0xF5;
        dev.cfg[14] = 0x7F;
        dev.cfg[15] = 0x00;
    };

    SECTION("the probe waits, so the config read answers the menu") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
        rig.dev.cfg[0] = 0xFF;   // the config block says the menu, so the job is taken (the fake's combination)
        rig.dev.cfg[2] = 0;
        cfgTail(rig.dev);
        rig.dev.linkAckDelayMs = 60;
        rig.dev.files[kGameBattery] = saveOf(1024, 3);
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        Job job;
        const auto t0 = Clock::now();
        REQUIRE(rig.submit(battery("dump"), job));
        CHECK(ms(t0) >= 30);  // it waited for the ack the game still owed, then read the block
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        CHECK(job.result == saveOf(1024, 3));
        CHECK(rig.host.link().linkOutstanding() == 0);
        CHECK(rig.host.link().linkRttMinNs() >= 0);  // the link measured the round trip: the ack was its own
        std::lock_guard<std::mutex> l(rig.dev.mu);
        // The frame went once. An ack the read had swallowed would leave it outstanding, and a tail parsed as an ack
        // would point the link past it: either way the frame is sent again and the game reads its seq a second time.
        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
        CHECK(rig.dev.syncs == 2);
        CHECK(rig.dev.applied == std::vector<Bytes>{Bytes{0x90, 0x3C, 0x7F}});
    }

    SECTION("the volume's Auto read waits for it too") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0] = 69;  // Sunsoft 5B: a cart with its own sound chip, so Auto writes unity
        rig.dev.cfg[2] = 0;
        cfgTail(rig.dev);
        rig.dev.linkAckDelayMs = 60;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

        rig.cart.setExpansionVolume(64);
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.cfg[3] == 64;
        }));
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        rig.host.setExpVol(N8Host::EXP_VOL_AUTO);
        // Auto asks the cart which mapper runs, so that read has the probe's exposure: it waits for the ack, then
        // reads the block and writes unity for this mapper.
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.cfg[3] == N8Host::EXP_VOL_UNITY;
        }));
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
        CHECK(rig.dev.syncs == 2);
    }
}

TEST_CASE("a port that throws as a job starts stops the link with the reason, and the job says the link went down",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    rig.connect();
    // The run's own pre-flush throws, as a USB unplug during a job does: the job never runs, so nothing addresses the
    // card, and its record must still carry a reason rather than the generic failure a job with no outcome gets.
    rig.dev.throwOnFlushInput = 1;  // connect() flushed once already; the job's own flush is the next
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error == "the link went down before the job ran");
    // The failure is the link's, and lastError is where Settings > N8 reads it: the thread ended, so a connect can
    // start it afresh.
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    CHECK_FALSE(rig.host.cartJobBusy());
    CHECK(rig.dev.count("file-") == 0);
    Job again;
    std::string why;
    CHECK_FALSE(rig.submit(battery("dump"), again, &why));
    CHECK(why == "not connected");
}

TEST_CASE("a port that throws after a job ran still posts its outcome, and the link stops with the reason",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    rig.connect();
    // The flush after the job throws: the transfer itself completed, so its completion is the one that was decided.
    rig.dev.throwOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after it ran
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 3));
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    CHECK(rig.dev.count("file-") > 0);
}

TEST_CASE("an upload whose trailing flush throws still closes the port it booted from", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    rig.dev.throwOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after the boot
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), job));
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));
    // The boot was decided before the throw, so its completion stands, and the port an upload frees for the CLI's
    // tools is closed as the boot would have closed it rather than held until a disconnect.
    CHECK(job.state.load() == Job::Done);
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.openNow == 0);
    CHECK_FALSE(rig.dev.menu);
    CHECK(rig.dev.booted == "usb-games/game.nes");
}

TEST_CASE("a job its node abandons fails at once, stops at its next chunk, and the cart resumes streaming",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 20;
    rig.dev.files[kGameBattery] = saveOf(4096, 9);
    rig.connect();
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(64)), job));  // eight 8 KB chunks
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 2; }));
    rig.cart.abandonJobs(&rig.owner);
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.failure.load() == Job::Removed);
    const std::uint32_t progressAtAbandon = job.progress.load();

    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(rig.cart.connected());                      // nothing booted: streaming resumed to the menu, as it was
    CHECK(rig.dev.count("file-wr") < 8);              // it stopped early
    CHECK(job.progress.load() == progressAtAbandon);  // and reported nothing more into the record
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.menu);
        CHECK(rig.dev.booted.empty());
        CHECK(rig.dev.files.count("usb-games/game.nes") == 0);  // the partial image went with the job
    }

    // A dump abandoned between its blocks stops there, having changed nothing.
    rig.dev.fileReadDelayMs = 20;
    Job dump;
    REQUIRE(rig.submit(battery("dump"), dump));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") >= 2; }));
    rig.cart.abandonJobs(&rig.owner);
    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(dump.failure.load() == Job::Removed);
    CHECK(rig.dev.count("file-rd") < 8);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files[kGameBattery] == saveOf(4096, 9));
    }

    // Another node's abandon is not this job's: a second job runs to its end.
    Job next;
    REQUIRE(rig.submit(battery("dump"), next));
    int other = 0;
    rig.cart.abandonJobs(&other);
    REQUIRE(waitUntil([&] { return next.finished(); }));
    CHECK(next.state.load() == Job::Done);
}

TEST_CASE("an upload abandoned after its last chunk still boots nothing, and its file goes", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 150;
    rig.connect();
    Job job;
    // One chunk: the abandon lands during its write, after the loop's last check.
    REQUIRE(rig.submit(uploadOf(inesImage(4)), job));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 1; }));
    rig.cart.abandonJobs(&rig.owner);
    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(job.failure.load() == Job::Removed);
    CHECK(rig.cart.connected());
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.menu);
    CHECK(rig.dev.booted.empty());
    CHECK(rig.dev.files.count("usb-games/game.nes") == 0);
}

TEST_CASE("a disconnect during a job ends it with the reason, and a job taken just before one still completes",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 20;
    rig.connect();
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(64)), job));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 1; }));
    rig.host.link().disconnect();  // joins: the job stops at its next chunk
    REQUIRE(job.finished());
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error == "the link was disconnected during the job");
    CHECK_FALSE(rig.host.cartJobBusy());

    // Taken and then the link torn down before the serial thread could run it: it fails, never hangs.
    rig.connect();
    Job raced;
    REQUIRE(rig.submit(battery("dump"), raced));
    rig.host.link().disconnect();
    REQUIRE(raced.finished());
    CHECK_FALSE(rig.host.cartJobBusy());
}

TEST_CASE("a connect forgets what this host booted once the cart runs something else, and the battery jobs default "
          "to the ROM it last booted", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    Job up;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), up));
    REQUIRE(waitUntil([&] { return up.finished(); }));
    REQUIRE(up.state.load() == Job::Done);
    Job before;
    REQUIRE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "title"), before));

    // A power-cycle: the cart is back at its menu, whose map_idx the next connect reads; the OS wrote the game's
    // battery back to its file on the way.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
        rig.dev.files[kGameBattery] = saveOf(1024, 11);
    }
    rig.host.connect(true);
    REQUIRE(rig.cart.connected());
    Job after;
    std::string why;
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "header"), after, &why));
    CHECK(why == "nothing this host booted is running");

    // The battery file of what it booted, with no name given.
    Job dump;
    REQUIRE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), dump));
    REQUIRE(waitUntil([&] { return dump.finished(); }));
    CHECK(dump.state.load() == Job::Done);
    CHECK(dump.result == saveOf(1024, 11));
}

TEST_CASE("the pause waits a bounded time for acks a silent cart never sends, and a disconnect inside that wait "
          "returns promptly", "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    SECTION("the bound") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        // The config block says the menu shows, so the job is taken and the pause's ack wait runs (see the sequence
        // case for why that combination is the fake's, not a real cart's).
        rig.dev.cfg[0] = 0xFF;
        rig.dev.cfg[2] = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkDeaf = true;
        }
        // The probe reads a quiet link, so the frame the drain has to wait for is one the drain itself writes: pushed
        // ahead of its release time, it is still in the ring when the submission's read comes.
        rig.host.link().setLookaheadMs(20);
        rig.push({0x90, 0x3C, 0x7F});
        rig.host.link().setLookaheadMs(0);
        Job job;
        const auto t0 = Clock::now();
        REQUIRE(rig.submit(battery("dump"), job));
        REQUIRE(waitUntil([&] { return job.finished(); }));
        const auto took = ms(t0);
        CHECK(took >= 250);  // it waited for the ack...
        CHECK(took < 2000);  // ...a bounded time, then ran the job (which found no battery file)
        CHECK(job.error == "no battery file for game.nes (EDN8/gamedata/game.nes/bram.srm)");
    }
    SECTION("a disconnect inside the wait") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0] = 0xFF;
        rig.dev.cfg[2] = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkDeaf = true;
        }
        rig.host.link().setLookaheadMs(20);
        rig.push({0x90, 0x3C, 0x7F});
        rig.host.link().setLookaheadMs(0);
        Job job;
        REQUIRE(rig.submit(battery("dump"), job));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));  // inside the 250 ms the drain waits for acks
        const auto t0 = Clock::now();
        rig.host.link().disconnect();
        CHECK(ms(t0) < 200);
        REQUIRE(job.finished());
        CHECK(job.error == "the link went down before the job ran");
        CHECK_FALSE(rig.host.cartJobBusy());
    }
}

TEST_CASE("a paused job whose run throws is run once, its completion is still posted, and the link stops with the "
          "reason", "[n8][cart-jobs]") {
    FakeN8 dev;
    N8Link link(factoryFor(dev));
    link.setLinkAllowed(false);
    REQUIRE(link.connect("/dev/fake"));
    std::atomic<int> runs{0}, unrun{0}, dones{0};
    N8Link::PausedJob job;
    job.run = [&](Edio* edio) -> N8Link::AfterJob {
        if (!edio) {
            ++unrun;  // what a job that never ran is told
            return N8Link::AfterJob::Resume;
        }
        ++runs;
        throw std::runtime_error("the job's own throw");
    };
    job.done = [&] { ++dones; };
    REQUIRE(link.runPaused(std::move(job)));
    REQUIRE(waitUntil([&] { return dones.load() == 1; }));
    // It ran: being told afterwards that the link went down before it could would overwrite what it recorded.
    CHECK(runs.load() == 1);
    CHECK(unrun.load() == 0);
    CHECK_FALSE(link.jobPending());
    CHECK_FALSE(link.isConnected());
    CHECK(link.lastError() == "serial write failed (N8 unplugged?): the job's own throw");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // the thread is gone: nothing runs it again
    CHECK(runs.load() == 1);
    CHECK(unrun.load() == 0);
    CHECK(dones.load() == 1);
}

TEST_CASE("a restore whose write fails says the battery file is replaced and incomplete, and closes it; a failed read "
          "closes too", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(2048, 3);
    rig.connect();
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.failNextWrite = true;
    }
    auto r = battery("restore");
    r.bytes = saveOf(4096, 5);
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error.rfind("the battery file EDN8/gamedata/game.nes/bram.srm was replaced and is incomplete: restore "
                          "again (", 0) == 0);
    CHECK(rig.cart.connected());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.filesOpen == 0);                 // closed on the failure path
        CHECK(rig.dev.files[kGameBattery].empty());    // the open emptied the old save: neither save is there
        rig.dev.files[kGameBattery] = saveOf(2048, 3);
        rig.dev.failNextRead = true;
    }
    Job dump;
    REQUIRE(rig.submit(battery("dump"), dump));
    REQUIRE(waitUntil([&] { return dump.finished() && !rig.host.cartJobBusy(); }));
    CHECK(dump.state.load() == Job::Failed);
    CHECK(dump.error == "Edio: file read error 0x01");
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.filesOpen == 0);
}

TEST_CASE("a restore replaces a longer file whole and says whether it made the ROM's folder", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(8192, 3);
    rig.connect();
    auto r = battery("restore");
    r.bytes = saveOf(1000, 5);
    Job over;
    REQUIRE(rig.submit(r, over));
    REQUIRE(waitUntil([&] { return over.finished() && !rig.host.cartJobBusy(); }));
    REQUIRE(over.state.load() == Job::Done);
    CHECK(std::string(over.result.begin(), over.result.end()) ==
          R"({"path":"EDN8/gamedata/game.nes/bram.srm","created":false})");
    auto fresh = battery("restore", "fresh.nes");
    fresh.bytes = saveOf(16, 1);
    Job made;
    REQUIRE(rig.submit(fresh, made));
    REQUIRE(waitUntil([&] { return made.finished() && !rig.host.cartJobBusy(); }));
    CHECK(std::string(made.result.begin(), made.result.end()) ==
          R"({"path":"EDN8/gamedata/fresh.nes/bram.srm","created":true})");
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.files[kGameBattery] == saveOf(1000, 5));  // truncated by the open, not overlaid
    CHECK(rig.dev.filesOpen == 0);
}

TEST_CASE("a result's paths are JSON strings, so a name the card accepts cannot break the result", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    // plainFileName allows a quote (it refuses a separator and a control byte), so the result must escape it: TS parses
    // these results, and an unescaped quote would end the string early.
    auto r = battery("restore", "quo\"te.nes");
    r.bytes = saveOf(16, 1);
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"EDN8/gamedata/quo\"te.nes/bram.srm","created":true})");

    auto up = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    up.bytes = inesImage(8);
    up.name  = "quo\"te.nes";
    Job uploaded;
    REQUIRE(rig.submit(up, uploaded));
    REQUIRE(waitUntil([&] { return uploaded.finished(); }));
    REQUIRE(uploaded.state.load() == Job::Done);
    CHECK(std::string(uploaded.result.begin(), uploaded.result.end()) ==
          R"({"path":"usb-games/quo\"te.nes","mapIndex":69,"streaming":"stopped"})");
}

TEST_CASE("a reconnect re-applies the standing expansion volume, which is the path a cart node relies on",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    rig.cart.setExpansionVolume(64);
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.cfg[3] == 64;
    }));
    // A power-cycle clears the register (master_vol is 0 on a fresh cart). A cart node's own re-send on the connected
    // edge asks for a value N8Host already holds, which setExpVol does not rewrite; the connect path is what writes it
    // again, and this pins that it does.
    rig.host.connect(false);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.cfg[3] = 0;
    }
    rig.host.connect(true);
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.cfg[3] == 64;
    }));
}

TEST_CASE("a dump keeps the restore's bound and tells a missing folder from a card that will not list",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files["EDN8/gamedata/empty.nes/bram.srm"] = {};
    rig.dev.files["EDN8/gamedata/huge.nes/bram.srm"] = Bytes((1u << 20) + 1, 0);
    rig.connect();
    const auto run = [&](JobRequest req) {
        Job j;
        REQUIRE(rig.submit(req, j));
        REQUIRE(waitUntil([&] { return j.finished() && !rig.host.cartJobBusy(); }));
        CHECK(j.state.load() == Job::Failed);
        return j.error;
    };
    CHECK(run(battery("dump", "empty.nes")) ==
          "the battery file EDN8/gamedata/empty.nes/bram.srm holds 0 bytes: a battery file is 1 to 1048576");
    CHECK(run(battery("dump", "huge.nes")) ==
          "the battery file EDN8/gamedata/huge.nes/bram.srm holds 1048577 bytes: a battery file is 1 to 1048576");
    CHECK(rig.dev.count("file-rd") == 0);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.failNextList = true;
    }
    CHECK(run(battery("dump", "empty.nes")) ==
          "the battery folder EDN8/gamedata/empty.nes could not be listed: Edio: operation error 0x01");
}

TEST_CASE("a battery job's name wins over the ROM this host booted, names are plain, and a port change forgets the "
          "booted ROM", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files["EDN8/gamedata/other.nes/bram.srm"] = saveOf(700, 9);
    rig.connect();
    Job up;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), up));  // boots game.nes
    REQUIRE(waitUntil([&] { return up.finished(); }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;  // power-cycled to the menu
    }
    rig.host.connect(true);
    Job named;
    REQUIRE(rig.submit(battery("dump", "other.nes"), named));
    REQUIRE(waitUntil([&] { return named.finished() && !rig.host.cartJobBusy(); }));
    CHECK(named.result == saveOf(700, 9));

    for (const std::string bad : {std::string("."), std::string(".."), std::string("a\\b"), std::string("a\0b", 3),
                                  std::string(256, 'x')}) {
        Job none;
        std::string why;
        auto r = request(RP_SYSTEM_SRAM_INTERFACE, "dump");
        r.name = bad;
        CHECK_FALSE(rig.submit(r, none, &why));
        CHECK(why == "a battery job names the ROM's file, a plain file name");
    }

    rig.host.connect(false);
    rig.host.setPort("/dev/another-cart");
    Job none;
    std::string why;
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), none, &why));
    CHECK(why == "a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
}
