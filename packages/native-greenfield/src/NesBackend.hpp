#pragma once

#include <memory>

#include "SystemFactory.hpp"

class SystemBase;

// Builds a real MesenNesSystem (NES) from a file-backed .nes ROM. Mirrors SameBoyBackend: slurp the
// ROM, gate on the iNES magic, seed SRAM/savestate, and onActivate (which boots the Mesen core, or
// fails gracefully on a bad ROM). NES has no backend "system" role, so the opaque settings blob is
// unused. A non-NES ROM is rejected (nullptr).
class NesBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;
};
