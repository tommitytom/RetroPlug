#include "MesenBackend.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesSystem.hpp"

namespace {

// Whole file into a byte vector (empty if unreadable).
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

} // namespace

std::unique_ptr<SystemBase> MesenBackend::build(SystemId id, const SystemBuildSpec& spec,
                                                double sampleRate) {
    std::vector<std::uint8_t> romBytes = slurpAll(spec.romPath);
    if (romBytes.empty()) return nullptr;

    // Dispatch on the platform: one Mesen core, two systems. The RomFormat gate confirms the bytes
    // actually match the requested platform (a mislabelled ROM is rejected).
    if (spec.platform == "nes") {
        if (detectRomFormat(romBytes) != RomFormat::Nes) return nullptr;
        MesenNesConfig cfg;
        cfg.romPath = spec.romPath;
        cfg.sram = spec.sram;
        cfg.savestate = spec.savestate;
        auto sys = std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes));
        sys->onActivate(sampleRate);  // boots the Mesen NES core (graceful on a bad ROM)
        return sys;
    }

    if (spec.platform == "gba") {
        if (detectRomFormat(romBytes) != RomFormat::Gba) return nullptr;
        MesenGbaConfig cfg;
        cfg.romPath = spec.romPath;
        cfg.sram = spec.sram;
        cfg.savestate = spec.savestate;
        // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).
        auto sys = std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes));
        sys->onActivate(sampleRate);  // boots the Mesen GBA core on HLE (graceful on a bad ROM)
        return sys;
    }

    return nullptr;  // Mesen doesn't serve this platform
}
