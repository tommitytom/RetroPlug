#include "GbaBackend.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"

namespace {

// Whole file into a byte vector (empty if unreadable).
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

} // namespace

std::unique_ptr<SystemBase> GbaBackend::build(SystemId id, const SystemBuildSpec& spec,
                                              double sampleRate) {
    std::vector<std::uint8_t> romBytes = slurpAll(spec.romPath);
    if (romBytes.empty() || detectRomFormat(romBytes) != RomFormat::MesenGba) return nullptr;

    MesenGbaConfig cfg;
    cfg.romPath = spec.romPath;
    cfg.sram = spec.sram;
    cfg.savestate = spec.savestate;
    // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).

    auto sys = std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes));
    sys->onActivate(sampleRate);  // boots the Mesen GBA core on HLE (graceful on a bad ROM)
    return sys;
}
