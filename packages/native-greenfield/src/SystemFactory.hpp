#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId

class SystemBase;

// Backend-agnostic description of a system to build. No emulator-specific types here: which
// backend, which ROM (a path or an embedded marker), seed bytes, and an opaque per-backend
// settings blob whose schema is owned by TS and decoded only by the matching backend.
struct SystemBuildSpec {
    std::string               backendKind;   // "sameboy" | "mesen-nes" | …
    std::string               romPath;       // "" when embedded
    std::string               embeddedRom;   // marker, e.g. "mgb" ("" when file-backed)
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

// The one build path: a registry keyed by backendKind. Dispatches to the matching backend;
// nullptr on an unknown kind.
class SystemFactory {
public:
    void registerBackend(std::string kind, std::unique_ptr<SystemBackend> backend);
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) const;

private:
    std::unordered_map<std::string, std::unique_ptr<SystemBackend>> backends_;
};
