#pragma once

// A model of the Everdrive N8 Pro's USB side, enough for the cart's link, its jobs and the node that drives them: it
// parses Edio's command frames as they are written (status, memory reads and writes, the SD directory and file API,
// with a failure it can inject into a directory load, a file write, a file read or an expansion-volume write), counts
// the files open on the card, keeps a 64 KB battery (logged if anything touches it) and the FPGA config block (map_idx
// 255 while the menu shows), answers the menu's '*' commands while the menu is showing, and plays BlipToaster's end of
// the reliable link while a game that speaks it runs (its acks late, never, or reporting every frame damaged, when a
// case asks). Every event (a FIFO write, a battery read, a file block) lands in one ordered log.
//
// Shared by retroplug-n8-test (N8CartJobs.test.cpp: the jobs and the pause, against N8Host with no engine) and
// retroplug-engine-graph-test (N8Adapters.test.cpp: the production adapters behind a real Engine), so both drive one
// model of the cart rather than two that could drift. No `serial` lib, no hardware.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "n8/desktop/Edio.hpp"
#include "n8/desktop/N8Link.hpp"

namespace retroplug::test {

using Bytes = std::vector<std::uint8_t>;

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
    bool                     linkDamaged = false;   // every frame reaches the game damaged, and it says so each time
    int                      refuseExpVolWrites = 0; // the next N expansion-volume writes throw (a refusing port)
    int                      expVolAttempts = 0;    // every expansion-volume write the host made
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

inline std::string hex(const std::uint8_t* d, std::size_t n) {
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
    // arriving first sees, since the cart answers it behind what its queue holds. Any write releases them all, a FIFO
    // write included, which a real cart does not do (its ack waits for the game, not for the host): harmless where an
    // owed ack is all a case is after, but a case that measures a round trip across a write would measure a short one.
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
                if (addr == static_cast<std::uint32_t>(Edio::ADDR_EXP_VOL)) {
                    ++dev_.expVolAttempts;
                    if (dev_.refuseExpVolWrites > 0) {
                        --dev_.refuseExpVolWrites;
                        throw std::runtime_error("fake port: the config write was refused");
                    }
                }
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
                if (dev_.linkDamaged) {  // a damaged frame is reported as the one the game expected, which it was not
                    replyAck({0xF5, next_, next_});
                    return;
                }
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

inline N8Link::PortFactory factoryFor(FakeN8& dev) {
    return [&dev](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakeN8Port>(dev); };
}

}  // namespace retroplug::test
