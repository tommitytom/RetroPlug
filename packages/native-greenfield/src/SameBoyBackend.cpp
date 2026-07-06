#include "SameBoyBackend.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "EmbeddedRoms.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"

namespace {

// Whole file into a byte vector (empty if unreadable).
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// Parse an LSDJ sync-mode name into the role enum. Unknown names throw so a typo surfaces
// rather than silently defaulting (mirrors the CLI harness).
LsdjSyncMode parseLsdjSyncMode(const std::string& s) {
    if (s == "Off")                return LsdjSyncMode::Off;
    if (s == "MidiSync")           return LsdjSyncMode::MidiSync;
    if (s == "MidiSyncArduinoboy") return LsdjSyncMode::MidiSyncArduinoboy;
    if (s == "MidiMap")            return LsdjSyncMode::MidiMap;
    if (s == "Keyboard")           return LsdjSyncMode::Keyboard;
    if (s == "KeyboardMidi")       return LsdjSyncMode::KeyboardMidi;
    if (s == "MidiPassthrough")    return LsdjSyncMode::MidiPassthrough;
    if (s == "ArduinoboyMaster")   return LsdjSyncMode::ArduinoboyMaster;
    throw std::runtime_error("constructSystem: unknown lsdjSyncMode: " + s);
}

} // namespace

std::unique_ptr<SameBoySystem> SameBoyBackend::buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                           std::vector<std::uint8_t> romBytes,
                                                           double sampleRate) {
    auto sys = std::make_unique<SameBoySystem>(id, std::move(cfg), std::move(romBytes));
    sys->onActivate(sampleRate);  // boots gb_ + restores cfg.sram then cfg.savestate
    return sys;
}

std::unique_ptr<SystemBase> SameBoyBackend::build(SystemId id, const SystemBuildSpec& spec,
                                                  double sampleRate) {
    std::vector<std::uint8_t> romBytes;
    if (!spec.embeddedRom.empty()) {
        // Embedded ROM: resolve the marker to baked bytes; the format is SameBoy by fiat.
        const auto rom = rp::embeddedRom(spec.embeddedRom);
        if (rom.empty()) return nullptr;  // unknown marker
        romBytes.assign(rom.begin(), rom.end());
    } else {
        // File-backed: slurp the full ROM and sniff. SameBoy-only gate here.
        romBytes = slurpAll(spec.romPath);
        if (romBytes.empty() || detectRomFormat(romBytes) != RomFormat::SameBoy) return nullptr;
    }

    SameBoyConfig cfg;
    cfg.romPath = spec.romPath;
    cfg.model = SameBoyModel::CgbC;
    cfg.fastBoot = true;
    if (!spec.embeddedRom.empty()) {
        cfg.embeddedRom = spec.embeddedRom;
        cfg.embedRom = false;  // re-supplied from the marker on load; keeps saves small
    }
    cfg.sram = spec.sram;
    cfg.savestate = spec.savestate;
    // Opaque per-backend settings: today just the optional LSDJ sync-role seed (a mode name),
    // decoded only here. Empty = no seed, so onActivate's sniffer default applies. "Off" makes
    // the role passive (no host clock) so a DSP script can be the sole clock.
    if (!spec.settings.empty()) {
        LsdjSyncConfig lsdj;
        lsdj.mode = parseLsdjSyncMode(std::string(spec.settings.begin(), spec.settings.end()));
        cfg.roles.emplace_back(lsdj);
    }

    return buildSameBoy(id, std::move(cfg), std::move(romBytes), sampleRate);
}
