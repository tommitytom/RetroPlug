#include "n8/desktop/N8Host.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "n8/Menu.hpp"
#include "n8/desktop/PortTransport.hpp"

namespace retroplug {

using rp::n8::Edio;

namespace {

// An Edio call's failure as the throw the jobs' steps are written around, in the words the port or describe() gives it
// (failureText).
void check(const rp::n8::Status& status, Edio& edio, std::string_view path = {}) {
    if (!status.ok()) throw std::runtime_error(failureText(status, edio, path));
}

// iNES mappers whose cartridge carries its own sound chip - the audio the N8's FPGA mixes in through
// `master_vol`. The TS twin is EXPANSION_MAPPERS in packages/n8/ts/cli/n8Play.ts (the same policy behind
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
// inesMapper in packages/n8/ts/cli/n8Play.ts, down to ignoring NES 2.0's extra nibble (no expansion-audio mapper
// needs it).
int inesMapper(std::span<const std::uint8_t> rom) {
    if (rom.size() < 16 || rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) return -1;
    return (rom[7] & 0xF0) | (rom[6] >> 4);
}

// The mapper of the RUNNING cart, read off the N8's FPGA config block - what the OS told the cart it is, so
// it answers for a game the user booted from the cart's own file browser (the usual state when the menu says
// Connect). map_idx = scfg[0] | scfg[2]'s high nibble << 8, as decodeMapConfig does TS-side. Returns -1 when
// the block can't be read, which lands on "leave the register alone" - the safe answer.
int runningMapper(Edio& edio) {
    std::uint8_t cfg[Edio::SIZE_CFG] = {0};
    if (!edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok()) return -1;
    return ((cfg[2] >> 4) << 8) | cfg[0];
}

// The mapper the N8 OS runs its own file browser on: the config block's map_idx while the menu shows (read on an N8
// Pro; mapConfig.ts: "the OS clears it to force map 255"). A system-region read, so telling the menu from a game this
// way touches neither the cart's bus nor a running game's FIFO.
constexpr int kMenuMapper = 255;

// What N8Host::probeMenu answers when the link went down while the probe was on its way to the serial thread, and when
// its deadline passed with the reliable link still busy (a cart acknowledging the stream, so no turn was quiet for the
// read).
constexpr int kProbeLinkDown = -2;
constexpr int kProbeLinkBusy = -3;

// How long a submission waits for the serial thread's answer to its menu probe: a config-block read is one USB round
// trip, taken between two MIDI chunks. A cart that has stopped answering gets its refusal either way, and this keeps
// a control thread (the DAW's) from waiting out the read's own 2 s timeout to learn it.
constexpr int kMenuProbeMs = 1000;

// Why a battery job refuses a running game: the battery file lives on the SD card, which only the menu's file API
// reaches (a running game has no menu), and the cart's own battery is never touched over USB (on the real cart one
// read of it wedged a running BlipToaster, and one at the menu left the menu deaf to its commands).
constexpr const char* kBatteryAtGame =
    "the battery file can only be reached at the N8 menu: power-cycle to the menu first";

// Why an upload refuses one: the menu's own commands would go into a running game's MIDI stream, and it has no menu to
// install into.
constexpr const char* kUploadAtGame =
    "the cart is running a game, which has no menu to load a ROM: power-cycle the console to its menu first";

// The largest battery file a restore takes: far past any the N8 writes (a 64 KB battery is a 66,560-byte file), so
// only a mistake is refused.
constexpr std::size_t kMaxBatteryFile = 1u << 20;

// What the card holds for `rom`'s battery: whether its folder exists, and the size of its bram.srm when there is one. A
// folder that is not there (FatFs FR_NO_FILE / FR_NO_PATH, the directory load's status 0x04 / 0x05) is the one failure
// taken as an answer; any other is the card's or the link's, and is reported as that.
struct BatteryFolder {
    bool                         exists = false;
    std::optional<std::uint32_t> size;
};
BatteryFolder listBatteryFolder(Edio& edio, const std::string& rom) {
    BatteryFolder                folder;
    std::optional<std::uint32_t> size;
    const rp::n8::Status         listed = edio.listDir("EDN8/gamedata/" + rom, [&](const rp::n8::DirEntry& e) {
        if (!e.isDir && e.name == "bram.srm") size = e.size;
    });
    if (listed.ok()) {
        folder.exists = true;
        folder.size   = size;
        return folder;
    }
    const bool absent = listed.code == rp::n8::Code::Device && listed.where == rp::n8::Where::Operation &&
                        (listed.device == 0x04 || listed.device == 0x05);
    if (!absent)
        throw std::runtime_error("the battery folder EDN8/gamedata/" + rom + " could not be listed: " +
                                 failureText(listed, edio));
    return folder;
}

// Where the N8 OS keeps a game's battery between boots: written back from the cart's SRAM when the console returns to
// the menu, and loaded into it when the game next boots.
std::string batteryFilePath(const std::string& rom) { return "EDN8/gamedata/" + rom + "/bram.srm"; }

// A name a job may put in an SD path: a plain file name, at most FatFs's 255 units, with no separator and no control
// byte (a NUL from the RPC would cut the path short at the device, so "a\0b" would reach ROM a's folder).
bool plainFileName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name == "." || name == "..") return false;
    for (const char c : name)
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) <= 0x1F) return false;
    return true;
}

// `s` as the body of a JSON string. A job's `name` is the caller's, and a card that accepts it does not make it a JSON
// string: a quote (which plainFileName allows) would end the string early and break the result TS parses.
std::string jsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char esc[7];
            std::snprintf(esc, sizeof esc, "\\u%04X", static_cast<unsigned char>(c));
            out += esc;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir)
    : link_(std::move(factory)), lister_(std::move(lister)), configDir_(std::move(configDir)) {
    // Every time the streaming link comes up - a Connect, a live port switch, the reconnect restore() does - set the
    // expansion-audio master volume. Connect is the moment that matters: a ROM load deliberately leaves streaming
    // stopped, so the user comes back through here either way. A job's pause is not a connect: the port and the
    // register stay as they were, and only a boot (which the job applies itself, on the ROM's own mapper) changes them.
    link_.setOnConnected([this](Edio& edio) {
        // Read the generation first: a change made after this read is applied again on the serial thread's next turn,
        // never marked applied without having been written.
        const unsigned generation = expVolGeneration_.load(std::memory_order_acquire);
        // What the cart runs now, which rom.header / rom.title answer for: the image this host booted is forgotten
        // unless the cart still runs its mapper (a power-cycle leaves the menu's, a game booted from the cart's own
        // browser its own; one of the same mapper is the case this cannot tell), and an unreadable block forgets it
        // too.
        const int mapper = runningMapper(edio);
        {
            std::lock_guard<std::mutex> l(loadedMutex_);
            if (!loadedRom_.empty() && mapper != loadedMapIndex_) {
                loadedRom_.clear();
                loadedName_.clear();
                loadedMapIndex_ = -1;
            }
        }
        applyExpVol(edio, expVol_.load(std::memory_order_relaxed), mapper);
        expVolApplied_.store(generation, std::memory_order_relaxed);
    });
    // A value a param changed while the link is up: the serial thread owns the port, so it writes the standing value
    // on its own turn, between MIDI chunks, and marks the generation it wrote. A throw leaves the generation behind,
    // so the next turn tries again (N8Link catches it: a config write must not take the MIDI link down).
    link_.setOnControl([this](Edio& edio, bool backChannelQuiet) {
        // The config block a submission's refusal reads (probeMenu). Taken here, between chunks, for the reason the
        // volume is: a system-region read on the thread that owns the port, putting no byte in a running game's FIFO.
        // Ahead of the volume, and a submission asks the link for a quiet turn of its own (N8Link::wakeControl), so a
        // config write that keeps failing does not hold it up.
        //
        // Both reads below wait for a quiet turn (N8Link::OnControl): an ack for an outstanding frame can arrive
        // inside a read, and would be taken for the reply's first bytes while the reply's tail was left in the input
        // to be parsed as link traffic. N8Link services the link before this turn and passes whether it is quiet,
        // which a cart that has acknowledged nothing for a while (one at its menu) is too; the probe's own deadline
        // covers a cart that keeps acknowledging.
        const unsigned probe = menuProbeGeneration_.load(std::memory_order_acquire);
        if (backChannelQuiet && probe != menuProbeApplied_.load(std::memory_order_relaxed)) {
            menuProbeIndex_.store(runningMapper(edio), std::memory_order_relaxed);
            menuProbeApplied_.store(probe, std::memory_order_release);
        }
        const unsigned generation = expVolGeneration_.load(std::memory_order_acquire);
        if (generation == expVolApplied_.load(std::memory_order_relaxed)) return;
        const int value = expVol_.load(std::memory_order_relaxed);
        // A standing value needs no read; Auto asks the cart which mapper runs, so it has the probe's exposure. The
        // value is the one read here, not a second read inside applyExpVol: a param moving it to Auto between the two
        // would take the read past this turn's guard.
        if (value == EXP_VOL_AUTO && !backChannelQuiet) return;
        applyExpVol(edio, value, -1);
        expVolApplied_.store(generation, std::memory_order_relaxed);
    });
}

N8Host::~N8Host() { link_.disconnect(); }

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
    if (cartJobBusy()) return;  // a cart job owns the port; don't race link_ connect
    const bool wasStreaming = link_.isConnected();
    if (port != port_) {
        // Another port may be another cart, with another SD card: what this host booted on the last one names nothing
        // here.
        std::lock_guard<std::mutex> l(loadedMutex_);
        lastBootedName_.clear();
    }
    port_ = port;
    if (wasStreaming) {
        link_.disconnect();
        if (!port.empty()) connectLink(port);
    }
    save();
}

void N8Host::connect(bool enable) {
    if (cartJobBusy()) return;  // a job owns the port; the menu also disables this row while busy
    if (enable) {
        if (port_.empty()) {  // auto-pick the first attached N8 (USB VID:PID 38df:0017)
            for (const N8PortDto& p : (lister_ ? lister_() : std::vector<N8PortDto>{}))
                if (p.isN8) { port_ = p.port; break; }
        }
        enabled_ = true;
        if (!port_.empty()) connectLink(port_);
    } else {
        enabled_ = false;
        link_.disconnect();
    }
    save();
}

void N8Host::connectLink(const std::string& port) {
    if (std::string why = portGuard_ ? portGuard_(port) : std::string(); !why.empty()) {
        link_.refuse(why);
        return;
    }
    link_.connect(port);
}

void N8Host::setLookahead(int ms) {
    if (cartJobBusy()) return;
    link_.setLookaheadMs(ms < 0 ? 0 : ms);
    save();
}

void N8Host::setExpVol(int v) {
    const int clamped = v < 0 ? EXP_VOL_AUTO : (v > 255 ? 255 : v);
    if (expVol_.exchange(clamped, std::memory_order_release) == clamped) return;
    // Publish the change; a live link's serial thread picks it up at its next turn, so turning the knob is audible on
    // the console straight away without interrupting the MIDI stream. Nothing is written while a job holds the link
    // paused, but the generation survives the pause, so the value goes out on the serial thread's first turn after the
    // resume; while disconnected the next connect applies it.
    expVolGeneration_.fetch_add(1, std::memory_order_release);
}

int N8Host::probeMenu() {
    const unsigned generation = menuProbeGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    // A turn of its own, so a volume write that keeps failing (whose retry interval is a second) is not what this
    // waits behind; the deadline below covers a cart that never answers, whatever the link is doing.
    link_.wakeControl();
    const auto     deadline   = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMenuProbeMs);
    while (menuProbeApplied_.load(std::memory_order_acquire) < generation) {
        if (!link_.isConnected()) return kProbeLinkDown;
        if (std::chrono::steady_clock::now() >= deadline) return link_.backChannelQuiet() ? -1 : kProbeLinkBusy;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return menuProbeIndex_.load(std::memory_order_relaxed);
}

void N8Host::applyExpVol(Edio& edio, int value, int mapperHint) {
    if (value == EXP_VOL_AUTO) {
        const int mapper = mapperHint >= 0 ? mapperHint : runningMapper(edio);
        // Not an expansion-audio cart (or nothing readable to say it is): leave the register as the N8 OS set
        // it. Auto only rescues the case it can recognise; it never overrides a deliberate device setting.
        if (mapper < 0 || !mapperHasExpansionAudio(mapper)) return;
        value = EXP_VOL_UNITY;
    }
    // A failed write throws: OnControl's caller (N8Link's serial thread) retries it a second later, and every other
    // caller takes it as best-effort.
    const std::uint8_t byte = static_cast<std::uint8_t>(value);
    check(edio.memWR(Edio::ADDR_EXP_VOL, &byte, 1), edio);
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
    // Note there is no expansion volume here: it belongs to the project, not the link (see EXP_VOL_AUTO), so this
    // reconnect comes up on AUTO and the cart node pushes the loaded project's value when it has one.
    if (enabled_) connect(true);  // reconnect the persisted link (auto-picks if the saved port is empty)
}

void N8Host::save() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%d\n%d\n", port_.c_str(), link_.lookaheadMs(), enabled_ ? 1 : 0);
        std::fclose(f);
    }
}

bool N8Host::submitCartJob(const rp_job_request& request, rp_job* job, const rp_host_jobs* reports, const void* owner,
                           std::string* error) {
    const auto refuse = [error](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!job || !reports) return refuse("the job has no record to report into");
    // What the job reads of the request: the strings copied, the bytes viewed. The C request is the caller's for this
    // call only and the job's steps run later on the serial thread, so the one job that keeps the bytes (a restore's
    // save, an upload's image) copies them into its step, once, after every refusal.
    const auto text = [](const char* s) { return std::string(s ? s : ""); };
    const struct {
        std::string                    capability, op, name;
        std::uint32_t                  length;
        std::span<const std::uint8_t> bytes;
    } r{text(request.capability), text(request.op), text(request.name), request.length,
        request.bytes && request.size ? std::span<const std::uint8_t>(request.bytes, request.size)
                                      : std::span<const std::uint8_t>{}};
    const bool sram = r.capability == RP_SYSTEM_SRAM_INTERFACE;
    const bool rom  = r.capability == RP_SYSTEM_ROM_INTERFACE;
    if (!sram && !rom) return refuse("the cart presents no " + r.capability);

    // What this host booted on the cart: kept here, so answered at once, with no transfer, whatever the link is doing.
    // Every connect checks it against the mapper the cart says it runs, and forgets it on a mismatch (a power-cycle,
    // a game booted from the cart's own browser), so an answer is never a stale image.
    if (rom && (r.op == "header" || r.op == "title")) {
        std::lock_guard<std::mutex> l(loadedMutex_);
        if (loadedRom_.empty()) return refuse("nothing this host booted is running");
        if (r.op == "header") {
            const std::size_t n =
                r.length != 0 ? std::min<std::size_t>(r.length, loadedRom_.size()) : loadedRom_.size();
            reports->succeed(job, loadedRom_.data(), n);
        } else {
            reports->succeed(job, reinterpret_cast<const std::uint8_t*>(loadedName_.data()), loadedName_.size());
        }
        return true;
    }

    const bool dump    = sram && r.op == "dump";
    const bool restore = sram && r.op == "restore";
    const bool upload  = rom && r.op == "upload";
    if (!dump && !restore && !upload) return refuse(r.capability + " has no operation '" + r.op + "'");
    if (restore && (r.bytes.empty() || r.bytes.size() > kMaxBatteryFile))
        return refuse("a battery restore takes 1 to " + std::to_string(kMaxBatteryFile) + " bytes");
    if (upload && inesMapper(r.bytes) < 0) return refuse("the upload is not an iNES image");
    if (upload && !plainFileName(r.name))
        return refuse("an upload names the file it is stored under, a plain file name");
    // The ROM whose battery file a battery job reaches: the one it names, else the one this host last booted.
    std::string batteryRom;
    if (dump || restore) {
        if (!r.name.empty() && !plainFileName(r.name))
            return refuse("a battery job names the ROM's file, a plain file name");
        if (!r.name.empty()) {
            batteryRom = r.name;
        } else {
            std::lock_guard<std::mutex> l(loadedMutex_);
            batteryRom = lastBootedName_;
        }
        if (batteryRom.empty())
            return refuse("a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
    }
    if (cartJobBusy()) return refuse("busy: the cart is running another job");
    if (!link_.isConnected()) return refuse("not connected");
    // Where the cart is, before any pause: the transfers run at the menu only, so one a running game will refuse is
    // refused here, and the stream never pauses for it (every note-on pushed during such a pause was lost; note-offs
    // are owed, note-ons are not). The job checks again at its first step, for a cart that changed in between.
    const int mapper = probeMenu();
    if (mapper == kProbeLinkDown) return refuse("not connected");
    if (mapper == kProbeLinkBusy)
        return refuse("the reliable link never went quiet for the cart's config read: the cart kept acknowledging the "
                      "stream");
    if (mapper < 0) return refuse("the cart's config block did not answer");
    if (mapper != kMenuMapper) return refuse(dump || restore ? kBatteryAtGame : kUploadAtGame);
    if (!cartJob_.claim(reports, job, owner)) return refuse("busy: the cart is running another job");

    // The job's outcome, which `run` fills and `done` posts once streaming is back: both on the serial thread, one
    // after the other, so it needs no lock of its own.
    struct Outcome {
        bool                      ok = false;
        std::vector<std::uint8_t> bytes;
        std::string               error;
    };
    auto outcome = std::make_shared<Outcome>();
    // Between steps: the job's node left the plan (its record already failed), or a disconnect was asked for.
    const auto stopReason = [this]() -> const char* {
        if (cartJob_.abandoned()) return "the job's node left the plan";
        if (link_.stopRequested()) return "the link was disconnected during the job";
        return nullptr;
    };
    const auto checkStop = [stopReason] {
        if (const char* why = stopReason()) throw std::runtime_error(why);
    };
    // The cart's state, read off its FPGA config block (a system-region read, which never touches the cart's bus and
    // puts no byte in a running game's FIFO): the OS runs on its own mapper, 255, so that is the menu.
    const auto atMenu = [](Edio& edio) {
        const int mapper = runningMapper(edio);
        if (mapper < 0) throw std::runtime_error("the cart's config block did not answer");
        return mapper == kMenuMapper;
    };
    using AfterJob = N8Link::AfterJob;
    N8Link::PausedJob paused;
    if (dump) {
        paused.run = [this, outcome, stopReason, checkStop, atMenu, rom = batteryRom](Edio* edio) {
            if (!edio) {
                outcome->error = "the link went down before the job ran";
                return AfterJob::Resume;
            }
            bool open = false;
            try {
                checkStop();
                if (!atMenu(*edio)) throw std::runtime_error(kBatteryAtGame);
                const std::string   path   = batteryFilePath(rom);
                const BatteryFolder folder = listBatteryFolder(*edio, rom);
                if (!folder.size) throw std::runtime_error("no battery file for " + rom + " (" + path + ")");
                // The card's word for the size is checked against the one bound both directions keep, so a corrupt
                // directory entry neither allocates gigabytes nor pauses the stream for days.
                const std::uint32_t size = *folder.size;
                if (size == 0 || size > kMaxBatteryFile)
                    throw std::runtime_error("the battery file " + path + " holds " + std::to_string(size) +
                                             " bytes: a battery file is 1 to " + std::to_string(kMaxBatteryFile));
                // The menu's file read moves about a kilobyte a second whatever the block (measured on an N8 Pro: 512
                // bytes in 476 ms, 256 in 209 ms), so a 64 KB file takes about a minute: a block a step, each one a
                // progress report and a point the job may stop at (reading changes nothing on the card). fileRead asks
                // before each block and tells the end, which is the job's progress in bytes.
                std::vector<std::uint8_t> buf(size);
                check(edio->fileOpen(path, Edio::FA_READ), *edio);
                open = true;
                const char* stopped = nullptr;
                auto        step    = [&](std::uint32_t done, std::uint32_t total) {
                    cartJob_.progress(done, total);
                    stopped = stopReason();
                    return stopped == nullptr;
                };
                const rp::n8::Status readResult = edio->fileRead(buf.data(), size, rp::n8::progressOf(step));
                if (readResult.code == rp::n8::Code::Cancelled) throw std::runtime_error(stopped);
                check(readResult, *edio);
                check(edio->fileClose(), *edio);
                open = false;
                outcome->bytes = std::move(buf);
                outcome->ok    = true;
            } catch (const std::exception& e) {
                outcome->error = e.what();
                if (open) (void)edio->fileClose();  // best-effort
            }
            return AfterJob::Resume;
        };
    } else if (restore) {
        std::vector<std::uint8_t> restored(r.bytes.begin(), r.bytes.end());
        paused.run = [this, outcome, checkStop, atMenu, rom = batteryRom, srm = std::move(restored)](Edio* edio) {
            if (!edio) {
                outcome->error = "the link went down before the job ran";
                return AfterJob::Resume;
            }
            const std::string path = batteryFilePath(rom);
            bool open = false;
            try {
                checkStop();
                if (!atMenu(*edio)) throw std::runtime_error(kBatteryAtGame);
                // Whether the ROM's folder is there yet: a game that never saved on this card has none (the N8 OS makes
                // it at the first write-back), and a restore ahead of the first boot is the way to give it a save, so
                // the folder is made rather than refused; the result says it was, which is how a caller tells a new
                // ROM from a mistyped name.
                const bool created = !listBatteryFolder(*edio, rom).exists;
                checkStop();  // the last point it may stop at: the open below empties the file there was
                // The Edio subset has no rename, so there is no writing to a temporary name: the file is replaced in
                // place, and a write once started runs to the end (the menu writes about 100 KB a second: an upload of
                // 288 KB took 2.8 s on the real cart, so a battery takes well under a second).
                const std::uint32_t n = static_cast<std::uint32_t>(srm.size());
                check(edio->fileOpen(path, Edio::FA_REPLACE), *edio);
                open = true;
                constexpr std::size_t CHUNK = 4096;
                cartJob_.progress(0, n);
                for (std::size_t off = 0; off < n; off += CHUNK) {
                    const std::size_t c = std::min<std::size_t>(CHUNK, n - off);
                    // Each 1 KB acknowledged by the cart, then the command's status.
                    check(edio->fileWrite(srm.data() + off, c), *edio);
                    cartJob_.progress(static_cast<std::uint32_t>(off + c), n);
                }
                check(edio->fileClose(), *edio);
                open = false;
                const std::string json =
                    "{\"path\":\"" + jsonString(path) + "\",\"created\":" + (created ? "true" : "false") + "}";
                outcome->bytes.assign(json.begin(), json.end());
                outcome->ok = true;
            } catch (const std::exception& e) {
                outcome->error = e.what();
                if (open) {
                    // The open emptied the file there was: what is on the card now is neither save. Closed best-effort,
                    // so the cart does not keep the handle.
                    outcome->error = "the battery file " + path + " was replaced and is incomplete: restore again (" +
                                     e.what() + ")";
                    (void)edio->fileClose();
                }
            }
            return AfterJob::Resume;
        };
    } else {
        std::vector<std::uint8_t> uploaded(r.bytes.begin(), r.bytes.end());
        paused.run = [this, outcome, checkStop, atMenu, image = std::move(uploaded), name = r.name](Edio* edio) {
            if (!edio) {
                outcome->error = "the link went down before the job ran";
                return AfterJob::Resume;
            }
            const std::string bootPath = "usb-games/" + name;
            int  mapIndex = -1;
            bool created = false, opened = false;
            try {
                checkStop();
                // A running game has no menu, and the menu's own probe would put '*t' into its MIDI stream.
                if (!atMenu(*edio)) throw std::runtime_error(kUploadAtGame);
                rp::n8::Menu menu(*edio);
                // A clear error if the menu answers something else, in the job's own words (a job's caller shows them),
                // as an install's refusal is below; the CLI prints the protocol's (describe()) for both.
                const rp::n8::Status tested = menu.test();
                if (tested.code == rp::n8::Code::BadReply)
                    throw std::runtime_error(
                        "N8 is not at its menu (no '*t' answer) - reset the cart to the file browser and retry");
                check(tested, *edio);
                check(edio->fileOpen(bootPath, Edio::FA_REPLACE), *edio);
                created = opened = true;
                // Each chunk its own write command (its 1 KB blocks acknowledged, then its status), so the job may stop
                // between two.
                constexpr std::size_t CHUNK = 8192;
                for (std::size_t off = 0; off < image.size(); off += CHUNK) {
                    checkStop();
                    const std::size_t n = std::min(CHUNK, image.size() - off);
                    check(edio->fileWrite(image.data() + off, n), *edio);
                    cartJob_.progress(static_cast<std::uint32_t>(off + n), static_cast<std::uint32_t>(image.size()));
                }
                checkStop();  // the last point that leaves the cart as it was: past it, the cart boots the image
                check(edio->fileClose(), *edio);
                opened = false;
                // The menu parses iNES and sources the mapper core from SD.
                std::uint16_t        index     = 0;
                const rp::n8::Status installed = menu.install(bootPath, &index);
                if (installed.code == rp::n8::Code::Device && installed.device != 0x44)
                    throw std::runtime_error("N8 menu: app install error (path '" + bootPath + "')");
                check(installed, *edio, bootPath);
                mapIndex = index;
                check(menu.start(), *edio);
            } catch (const std::exception& e) {
                // Nothing booted: the cart is as it was, and streaming resumes to it, as a job that failed before its
                // first chunk leaves it. The file the open made (or emptied) goes with it, so the cart's browser never
                // lists a partial image (best-effort).
                outcome->error = e.what();
                if (opened) (void)edio->fileClose();
                if (created) (void)edio->fileDelete(bootPath);
                return AfterJob::Resume;
            }
            // The game runs. The mapload reloaded the OS's stored master_vol, so set ours on the ROM we hold, whose
            // header names the mapper outright; best-effort, as every expansion-volume write is.
            try {
                applyExpVol(*edio, expVol_.load(std::memory_order_relaxed), inesMapper(image));
            } catch (const std::exception&) {
            }
            {
                std::lock_guard<std::mutex> l(loadedMutex_);
                loadedRom_      = image;
                loadedName_     = name;
                loadedMapIndex_ = mapIndex;
                lastBootedName_ = name;
            }
            // The old stream died with the previous ROM, so streaming stops here and the enabled toggle clears and is
            // persisted. save() reads port_ on this thread: no config writer runs while a job holds the cart (setPort,
            // connect and setLookahead refuse while cartJobBusy()), so it cannot move here.
            enabled_.store(false, std::memory_order_relaxed);
            save();
            const std::string json = "{\"path\":\"" + jsonString(bootPath) + "\",\"mapIndex\":" +
                                     std::to_string(mapIndex) + ",\"streaming\":\"stopped\"}";
            outcome->bytes.assign(json.begin(), json.end());
            outcome->ok = true;
            return AfterJob::Stop;
        };
    }
    paused.done = [this, outcome] {
        if (outcome->ok) cartJob_.succeed(outcome->bytes);
        else cartJob_.fail(outcome->error);
    };
    if (!link_.runPaused(std::move(paused))) {
        cartJob_.unclaim();  // the link went down between the check and the hand-over
        return refuse("not connected");
    }
    return true;
}

}  // namespace retroplug
