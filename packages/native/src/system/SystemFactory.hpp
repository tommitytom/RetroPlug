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

// Backend-agnostic description of a system to build. No emulator-specific types here: which core,
// which platform (so a multi-platform core like Mesen knows which system to build), which ROM (a path
// or an embedded marker), seed bytes, and an opaque per-backend settings blob whose schema is owned by
// TS and decoded only by the matching backend.
//
// The blob path is buildCore's, until C3's last commit deletes it; a system node builds through
// SystemBuildRequest below, and both reach each backend's one build function per console.
struct SystemBuildSpec {
    std::string               core;          // "sameboy" | "mesen" — the factory registry key
    std::string               platform;      // "gb" | "nes" | "gba" — what the ROM is
    std::string               romPath;       // "" when embedded
    std::string               embeddedRom;   // marker, e.g. "mgb" ("" when file-backed)
    std::vector<std::uint8_t> romBytes;      // effective ROM to load instead of slurping romPath (may be empty)
    std::vector<std::uint8_t> sram;          // battery RAM seed (may be empty)
    std::vector<std::uint8_t> savestate;     // savestate seed (may be empty)
    std::vector<std::uint8_t> settings;      // opaque per-backend config; the backend decodes it
};

// The config a console's backend builds a core from, every construct-time field already in it. Which alternative
// is set names the console, so a request cannot carry a NES config under a Game Boy node.
using SystemConfig = std::variant<SameBoyConfig, MesenNesConfig, MesenSmsConfig, MesenGbaConfig>;

// A system to build from typed inputs (docs/modular-graph-phase-c3-design.md section 5): the bytes a system node's
// resources hold, the ROM's name, and the config its settings make. A core built this way is the core buildCore
// builds from the same values: the blob path decodes its role configs into these same structs and calls the same
// backend function, so there is one boot path per console, not two.
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
    // buildCore's path: the opaque settings blob, decoded here into the console's config.
    virtual std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                              double sampleRate) = 0;
    // A system node's path: the request names the console by the config alternative it carries, and this backend
    // builds only the consoles it serves (nullptr, with a reason, for another's).
    virtual std::unique_ptr<SystemBase> buildTyped(SystemId id, const SystemBuildRequest& request,
                                                   double sampleRate, std::string* why) = 0;
};

// The one build path: a registry keyed by core. Dispatches to the matching backend;
// nullptr on an unknown core.
class SystemFactory {
public:
    void registerBackend(std::string core, std::unique_ptr<SystemBackend> backend);
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) const;
    // The typed path: the core is the one the request's config alternative names.
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                      std::string* why) const;

private:
    std::unordered_map<std::string, std::unique_ptr<SystemBackend>> backends_;
};
