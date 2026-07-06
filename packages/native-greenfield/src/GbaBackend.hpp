#pragma once

#include <memory>

#include "SystemFactory.hpp"

class SystemBase;

// Builds a real MesenGbaSystem (GBA) from a file-backed .gba ROM. Mirrors SameBoyBackend: slurp the
// ROM, gate on the GBA Nintendo logo, seed SRAM/savestate, and onActivate. biosPath is left empty →
// Mesen boots on its HLE boot ROM (no gba_bios.bin required); skipBootScreen defaults true. GBA's
// backend "system"-role config (via the settings blob) is deferred, so it isn't decoded here. A
// non-GBA ROM is rejected (nullptr).
class GbaBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildSpec& spec,
                                      double sampleRate) override;
};
