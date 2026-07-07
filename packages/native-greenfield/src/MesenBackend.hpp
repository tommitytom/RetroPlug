#pragma once

#include <memory>

#include "SystemFactory.hpp"

class SystemBase;

// The Mesen core — one backend spanning multiple platforms. Dispatches on `spec.platform` to build the
// right Mesen system (NES or GBA): slurp the ROM, gate on the platform's magic, seed SRAM/savestate,
// and onActivate (which boots the core, or fails gracefully on a bad ROM). GBA runs on Mesen's HLE
// boot ROM (biosPath left empty). Mesen exposes no backend "system"-role knobs yet, so the opaque
// settings blob is unused. An unknown platform or a ROM that doesn't match it is rejected (nullptr).
class MesenBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;
};
