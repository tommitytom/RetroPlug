#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "n8/Edio.hpp"             // the control ops' protocol
#include "n8/desktop/N8Link.hpp"
#include "graph/desktop/DeviceJobSlot.hpp"  // the one cart job at a time
#include "graph/rp_jobs.h"                  // the capability jobs rp.n8.nes hands the cart (N8CartLink)

namespace retroplug {

// One serial port for the config UI (mirrors N8PortInfo, trimmed to what the UI needs).
struct N8PortDto {
    std::string port;
    bool        isN8 = false;
};

// The N8 config snapshot the UI reads: the N8 facet's n8LinkConfig answers it (N8Rpc.hpp), as @retroplug/n8's
// N8LinkConfig.
struct N8ConfigDto {
    std::vector<N8PortDto> ports;
    std::string            selectedPort;
    bool                   connected = false;
    bool                   enabled = false;
    int                    lookaheadMs = 0;
    std::uint64_t          bytes = 0;
    std::string            error;
};

// Shared owner of the physical-N8 streaming link + its config, used by BOTH the SDL standalone and the DAW
// plugin. Owns an N8Link (the serial thread), the selected port + enabled toggle, and persistence to
// <configDir>/n8.cfg. The audio thread pushes bytes via link().push(...); the UI thread drives
// connect/setPort/setLookahead (through the N8 facet, N8Rpc.hpp) and reads getConfig().
// The serial-port factory + enumeration are injected, so this is testable hardware-free (a fake port, no serial
// lib). Factors what used to be inline SDL-only logic (sdl/main.cpp connectN8/setN8Port/save/load).
class N8Host {
public:
    using PortLister = std::function<std::vector<N8PortDto>()>;

    // The expansion-audio master volume written to the device (Edio::ADDR_EXP_VOL - `master_vol`, which
    // scales ONLY the cartridge's own sound chip, never the console's 2A03). It is 0 after a power-cycle,
    // and a 0 there silences a VRC6 / VRC7 / N163 / S5B / MMC5 cart's extra voices no matter what the ROM
    // does - a symptom that reads exactly like a dead chip.
    //
    // NOT a setting of its own: the UI owns the level as the NES system's `expansionVolume` knob (percent, in the
    // project), which `buildGraph` hands to the cart node as that node's param; the node pushes the rescaled byte here,
    // so the cart sounds the same emulated and on the console. Hence no persistence in n8.cfg - the project is where it
    // lives. AUTO is the standing value until something pushes one: it writes unity for a cart whose mapper carries
    // expansion audio, and leaves anything else as the N8's own menu had it. That is what a project with no NES system
    // in it (or a link that comes up before one loads) gets, and what the last cart node releasing the link puts back.
    static constexpr int EXP_VOL_AUTO  = -1;
    static constexpr int EXP_VOL_UNITY = 128;

    N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir);
    // Stops the serial thread before any member it reads goes away: the control turn (OnControl, which applies
    // setExpVol's value) captures `this` and runs on that thread, and link_ is declared BEFORE the fields such a
    // callback touches, so its own destructor would join too late.
    ~N8Host();

    N8Link&       link() { return link_; }
    const N8Link& link() const { return link_; }

    // The live config snapshot (ports enumerated fresh, link state read live).
    N8ConfigDto getConfig();

    // Choose the serial port; live-switches if currently streaming; persists.
    void setPort(const std::string& port);

    // Toggle streaming: on enable, auto-pick the first attached N8 if none chosen, then (dis)connect. Persists.
    void connect(bool enable);

    // Timed-release lookahead (ms, clamped >= 0). Persists.
    void setLookahead(int ms);

    // Expansion-audio master volume: EXP_VOL_AUTO or a 0..255 value (clamped). Written on every connect, and on a
    // live link at the serial thread's next turn after it changes: the value is an atomic and a generation, so any
    // thread may call this (a node's param does, from the block) without a lock or an allocation. Not persisted - see
    // EXP_VOL_AUTO.
    void setExpVol(int v);

    // Load n8.cfg (port / lookahead / enabled) and reconnect if it was enabled. Call once at host startup.
    void restore();

    // [control] What connects the link asks first: the reason `port` is not the link's to open, or "" when it is. The
    // facet (N8Rpc.hpp) answers for the ports its sessions hold, as a session refuses the link's: two writers on one
    // N8 would interleave their commands. A refused connect leaves the link down with the reason as its lastError, as
    // a failed one does. Cleared (null) by the facet before it goes. One facet per host: a second's guard would
    // replace the first's, and whichever went first would clear the other's.
    using PortGuard = std::function<std::string(const std::string& port)>;
    void setPortGuard(PortGuard guard) { portGuard_ = std::move(guard); }

    // --- The cart's capability jobs (docs/modular-graph-phase-c5-design.md section 6), which rp.n8.nes hands over
    // through its cart link (N8CartLink). The transfers run on the link's serial thread with streaming PAUSED rather
    // than disconnected (N8Link::PausedJob): the port, the reliable link's sequence state and the expansion volume are
    // kept, what was pushed before the pause is delivered first, and the job completes once streaming has resumed.
    //
    //   The battery jobs go through the SD card's save file, EDN8/gamedata/<rom>/bram.srm, which the N8 OS writes
    //   back from the cart's SRAM when the console returns to the menu and loads into it when the game next boots. The
    //   cart's own battery is never touched over USB: on the real cart one read of it wedged a running BlipToaster,
    //   and one at the menu left the menu deaf to its commands until a power-cycle. So both run at the menu only (a
    //   running game is refused: "the battery file can only be reached at the N8 menu"), and <rom> is the job's `name`
    //   (the ROM's file name on the card), else the file name this host last booted through rom.upload. That fallback
    //   is the host's own memory, kept across connects (at the menu the game no longer runs) and cleared by a port
    //   change: a game booted from the cart's own browser since then makes it the wrong ROM, so a caller that knows
    //   the ROM names it. With neither the job is refused. Both directions keep one bound: 1 byte to 1 MB.
    //
    //   rp.system.sram/1 dump      the file, read in 512-byte blocks (the menu's file read moves about a kilobyte a
    //                              second, whatever the block: a 64 KB battery takes about a minute), each a progress
    //                              report in bytes and a point the job may stop at. Fails when the file is missing,
    //                              empty or past the bound, and names the card's problem when the folder will not list.
    //   rp.system.sram/1 restore   `bytes` written as the file for the game to load at its next boot (progress in
    //                              bytes; the menu writes about 100 KB a second). The ROM's folder is made when it is
    //                              not there (a game that never saved has none), and the result, JSON, says which file
    //                              it wrote and whether it made the folder: {"path":..., "created":true|false}, so a
    //                              mistyped name shows as a folder made. Success means the cart acknowledged every
    //                              block; nothing is read back. The Edio subset has no rename, so the file is replaced
    //                              in place: the job may stop until the file is opened, and from then on the write runs
    //                              to the end, so a Removed (an abandon) after the open does not mean nothing was
    //                              written. A write that FAILS after the open leaves the file replaced and incomplete,
    //                              the old save already gone: the job says so ("restore again") and closes the file.
    //
    //   rp.system.rom/1 upload     `bytes` (an iNES image) to usb-games/<name> in 8 KB chunks (progress in bytes), then
    //                              installed and booted by the cart's menu, which must be showing (a running game is
    //                              refused before any byte reaches its FIFO). A boot STOPS streaming and clears the
    //                              enabled toggle (a running game has no menu, so the old stream is stale; the user
    //                              reconnects to the new ROM), and the result, JSON, says so:
    //                              {"path":..., "mapIndex":N, "streaming":"stopped"}. Abandoned or failed before the
    //                              boot, it boots nothing, deletes the file it opened and resumes streaming.
    //   rp.system.rom/1 header     the first `length` bytes (0: all) of the image this host last booted on the cart,
    //   rp.system.rom/1 title      and the name it booted it under: answered at once from what the host kept, with no
    //                              transfer, whatever the link is doing. Every connect reads the running map_idx and
    //                              forgets the image when it differs from the one the menu answered for it (a
    //                              power-cycle, a game booted from the cart's own browser), so both refuse "nothing
    //                              this host booted is running" then; another game of the same mapper is what it
    //                              cannot tell.
    //
    // One job at a time ("busy: ..."), and none while the link is down ("not connected"). Every transfer first reads
    // the cart's FPGA config block - the map_idx that tells the menu (255) from a running game - on the serial thread
    // between MIDI chunks, the expansion volume's path, and refuses there: a job a running game will not have pauses
    // nothing, so the notes the block pushes meanwhile reach the cart. The read waits for a turn quiet for it
    // (N8Link::OnControl: nothing outstanding on the reliable link, or a cart silent for a while, as one at its menu
    // is), so an ack the cart owes for a frame it is answering now cannot land inside it (a cart silent past that
    // bound is taken not to be answering at all, the risk the pause's drain already takes). Within a second: a link
    // that keeps acknowledging the whole time is refused for that ("the reliable link never went quiet ..."), and a
    // read that does not answer as such ("the cart's config block did not answer"); the job checks again at its first
    // step, for a cart that changed in between.
    // [control], like abandonCartJobs: the node submitting is the job's owner, and a node leaving the plan abandons its
    // job, which fails at once (Removed) and stops at its next safe point.
    // The job is the host's record, finished through `reports` (rp.host.jobs/1); the request is borrowed for the call.
    bool submitCartJob(const rp_job_request& request, rp_job* job, const rp_host_jobs* reports, const void* owner,
                       std::string* error);
    void abandonCartJobs(const void* owner) { cartJob_.abandon(owner); }
    // A capability job holds the cart: taken, and not yet done (streaming not yet resumed, or its completion not
    // posted).
    bool cartJobBusy() const { return cartJob_.busy() || link_.jobPending(); }

private:
    void save();
    // link_.connect(port) unless the guard refuses it.
    void connectLink(const std::string& port);

    // The cart's running map_idx, read on the serial thread that owns the port (a config-block read, between two MIDI
    // chunks) and answered here: the map_idx, or -1 when the block did not answer, -2 when the link went down while
    // the probe was asked, or -3 when its deadline passed with the link never quiet for the read. Each call asks
    // afresh, since a game can boot between two jobs.
    int probeMenu();

    // Write the expansion-audio master volume `value` through `edio` (see EXP_VOL_AUTO). `mapperHint` is the iNES
    // mapper when the caller knows it (a ROM we just uploaded), or -1 to ask the running cart.
    // Runs on whichever thread owns the port: the UI thread inside connect(), or the serial thread a job's transfer
    // runs on.
    void applyExpVol(rp::n8::Edio& edio, int value, int mapperHint);

    N8Link              link_;
    PortLister          lister_;
    PortGuard           portGuard_;
    std::string         configDir_;
    std::string         port_;
    std::atomic<bool>   enabled_{false};  // atomic: a successful ROM load clears it from the serial thread
    std::atomic<int>      expVol_{EXP_VOL_AUTO};  // read by the caller, which passes it to applyExpVol
    // A change counter, so the serial thread applies the standing value at its next turn without a mutex or a
    // std::function built on the caller's side: setExpVol only bumps the generation, and OnControl applies the value
    // while the applied generation lags. Bumping after a failed write is what retries it, so no failure needs a
    // re-arm.
    std::atomic<unsigned> expVolGeneration_{0};
    std::atomic<unsigned> expVolApplied_{0};  // serial thread writes, after a successful write
    // The menu probe (probeMenu): a submission asks the serial thread for the cart's config block, and the answer lands
    // here with the generation it answered. The serial thread stores the index, then the generation, so a reader that
    // sees the generation sees the value.
    std::atomic<unsigned> menuProbeGeneration_{0};
    std::atomic<unsigned> menuProbeApplied_{0};
    std::atomic<int>      menuProbeIndex_{-1};
    // The one capability job running on the cart, and what this host last booted there (rom.header / rom.title), which
    // an upload writes on the serial thread and those read on the control thread.
    rp::DeviceJobSlot         cartJob_;
    mutable std::mutex        loadedMutex_;
    std::vector<std::uint8_t> loadedRom_;
    std::string               loadedName_;
    int                       loadedMapIndex_ = -1;  // what the menu answered for it: the map_idx a connect checks
    // The file name this host last booted on the cart, which a connect does not forget (the battery jobs' default ROM,
    // reached at the menu, where the game is no longer running) and a port change does (another cart, another card).
    std::string               lastBootedName_;
};

}  // namespace retroplug
