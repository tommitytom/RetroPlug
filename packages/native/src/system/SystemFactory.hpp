#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

class SystemBase;

// The config a console's backend builds a core from, every construct-time field already in it. Which alternative
// is set names the console, so a request cannot carry a NES config under a Game Boy node.
using SystemConfig = std::variant<SameBoyConfig, MesenNesConfig, MesenSmsConfig, MesenGbaConfig>;

// A system to build (docs/modular-graph-phase-c3-design.md section 5): the bytes a system node's resources hold, the
// ROM's name, and the config its settings make. Every core is built this way, so there is one boot path per console.
struct SystemBuildRequest {
    std::vector<std::uint8_t> rom;
    std::vector<std::uint8_t> sram;       // battery RAM seed (may be empty)
    std::vector<std::uint8_t> savestate;  // savestate seed (may be empty)
    // The ROM's file stem. A Master System or Game Gear core stages its ROM as a file and takes its battery stem from
    // that name, and now that its ROM is bytes it has nothing else to name the file by; empty keeps the core's "rom".
    std::string  romName;
    SystemConfig config;
};

// One builder per emulator backend. Runs on the control thread (heavy, non-RT); returns an
// already-onActivate'd system, or nullptr with the reason in *why (when it is given) for an unreadable,
// rejected or refused ROM.
class SystemBackend {
public:
    virtual ~SystemBackend() = default;
    // The request names the console by the config alternative it carries, and this backend builds only the consoles it
    // serves (nullptr, with a reason, for another's). It is taken by value and consumed: the ROM is moved into the core
    // rather than copied a second time. It is taken by value and consumed: the ROM is moved into the core
    // rather than copied a second time.
    virtual std::unique_ptr<SystemBase> build(SystemId id, SystemBuildRequest request,
                                              double sampleRate, std::string* why) = 0;
};

// The one build path: a registry keyed by core, the core the request's config alternative names. nullptr on an
// unknown core.
class SystemFactory {
public:
    void registerBackend(std::string core, std::unique_ptr<SystemBackend> backend);
    std::unique_ptr<SystemBase> build(SystemId id, SystemBuildRequest request, double sampleRate,
                                      std::string* why) const;

private:
    std::unordered_map<std::string, std::unique_ptr<SystemBackend>> backends_;
};
