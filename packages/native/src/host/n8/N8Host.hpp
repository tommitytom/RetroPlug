#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "host/n8/N8Link.hpp"

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

private:
    void save();

    N8Link      link_;
    PortLister  lister_;
    std::string configDir_;
    std::string port_;
    bool        enabled_ = false;
};

}  // namespace retroplug
