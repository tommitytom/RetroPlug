#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId

class SystemBase;

// Backend-agnostic description of a system to build. No emulator-specific types here: which core,
// which platform (so a multi-platform core like Mesen knows which system to build), which ROM (a path
// or an embedded marker), seed bytes, and an opaque per-backend settings blob whose schema is owned by
// TS and decoded only by the matching backend.
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

// One builder per emulator backend. Runs on the control thread (heavy, non-RT); returns an
// already-onActivate'd system, or nullptr on an unreadable / rejected ROM.
class SystemBackend {
public:
    virtual ~SystemBackend() = default;
    virtual std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                              double sampleRate) = 0;
};

// The one build path: a registry keyed by core. Dispatches to the matching backend;
// nullptr on an unknown core.
class SystemFactory {
public:
    void registerBackend(std::string core, std::unique_ptr<SystemBackend> backend);
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) const;

private:
    std::unordered_map<std::string, std::unique_ptr<SystemBackend>> backends_;
};
