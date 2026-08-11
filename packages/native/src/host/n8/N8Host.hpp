#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "host/n8/Edio.hpp"        // Edio (control-op protocol), ISerialPort
#include "host/n8/N8Link.hpp"
#include "host/n8/N8SdWorker.hpp"

namespace retroplug {

// One serial port for the config UI (mirrors N8PortInfo, trimmed to what the UI needs).
struct N8PortDto {
    std::string port;
    bool        isN8 = false;
};

// The N8 config snapshot the UI reads - matches the __rp_getN8Config object shape (n8Devices.ts N8Config).
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
// connect/setPort/setLookahead (through the __rp_* hooks bindN8Hooks installs) and reads getConfig().
// The serial-port factory + enumeration are injected, so this is testable hardware-free (a fake ISerialPort,
// no serial lib). Factors what used to be inline SDL-only logic (sdl/main.cpp connectN8/setN8Port/save/load).
class N8Host {
public:
    using PortLister = std::function<std::vector<N8PortDto>()>;

    N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir);

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

    // Load n8.cfg (port / lookahead / enabled) and reconnect if it was enabled. Call once at host startup.
    void restore();

    // --- SD-card / menu control ops (Settings > N8 Pro). Each runs on the N8SdWorker's background thread:
    // it pauses streaming (link_.disconnect()), borrows the one serial port, drives a C++ Edio, then (for
    // Dump/Restore) resumes streaming. Fire-and-forget; the UI polls sdStatus(). A no-op if a job is already
    // in flight. All paths are absolute local files (from the OS file dialog). ---

    // Upload a local ROM to usb-games/<name> and boot it via the on-device menu (needs the cart at its menu).
    // Leaves streaming stopped (a new ROM is now running).
    void startLoadRom(const std::string& romPath);
    // Read the 64 KB cart battery and write it to destPath. Works on a running game; resumes streaming after.
    void startDumpSram(const std::string& destPath);
    // Write a local .srm straight to cart SRAM (running game) + verify. Resumes streaming after.
    void startRestoreSram(const std::string& srmPath);

    N8SdStatusDto sdStatus() { return sdWorker_.status(); }

private:
    void save();

    // Build a worker job that borrows the port from link_ (pausing streaming), opens a control Edio, runs
    // `op(edio, progress)`, then resumes streaming iff reconnectAfter (a thrown op always resumes streaming).
    N8SdWorker::Job controlJob(bool reconnectAfter, std::function<void(Edio&, N8SdWorker::Progress&)> op);

    N8Link::PortFactory factory_;    // FIRST: kept so the SD worker can open a control port (link_ has its own copy)
    N8Link              link_;
    PortLister          lister_;
    std::string         configDir_;
    std::string         port_;
    std::atomic<bool>   enabled_{false};  // atomic: a successful ROM load clears it from the worker thread (below)
    N8SdWorker          sdWorker_;   // LAST: destroyed first, so its thread joins while link_ + factory_ are alive
};

}  // namespace retroplug
