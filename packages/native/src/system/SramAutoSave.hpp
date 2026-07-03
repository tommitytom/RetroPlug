#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "lsdj/SampleCache.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemBase.hpp"

// Auto-save a system's cartridge battery RAM to its sibling `<rom>.sav`, the way
// most Game Boy emulators do: write when the SRAM has changed since the last
// check, and create the file if it doesn't exist. Shared by
// PluginRpcService::pumpSramAutoSave (the live UI-idle timer) and the test
// harness, so the write/dedup logic has one home.

namespace rp {

namespace sram_autosave {

// Resolve a system's sibling file path next to its ROM. `suffix` disambiguates
// systems that share one ROM file: 0 (or 1) => the plain `<rom><ext>`; N>=2 =>
// `<rom>-N<ext>`. Keeps Duplicate Instance / repeat loads from clobbering one
// shared `<rom>.sav`. Returns empty when there's no ROM path.
inline std::string siblingPath(const std::string& romPath, std::uint32_t suffix,
                               const char* ext) {
    if (romPath.empty()) return {};
    std::filesystem::path p(romPath);
    if (suffix >= 2) {
        p.replace_filename(p.stem().string() + "-" + std::to_string(suffix) + ext);
        return p.string();
    }
    p.replace_extension(ext);
    return p.string();
}

inline std::string siblingSavPath(const std::string& romPath, std::uint32_t suffix = 0) {
    return siblingPath(romPath, suffix, ".sav");
}

// Resolve where a system's battery RAM is read/written: the explicit user-paired
// `savPath` override when set, else the suffix-derived sibling. Single source of
// truth for auto-save, Save SRAM, dirty tracking, and project-load restore.
inline std::string resolveSavPath(const std::string& romPath, std::uint32_t suffix,
                                  const std::string& savPathOverride) {
    return savPathOverride.empty() ? siblingSavPath(romPath, suffix) : savPathOverride;
}
inline std::string resolveSavPath(const SystemBase& sys) {
    return resolveSavPath(sys.romPath(), sys.savSuffix(), sys.savPath());
}

// Current battery RAM, read tear-free from the DSP-published state snapshot when
// available (the same race-free path Save SRAM uses), else a direct live read.
inline std::vector<std::uint8_t> readSram(SystemBase& sys) {
    const auto& region = sys.stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
    if (region.size > 0) {
        std::vector<std::uint8_t> state;
        if (sys.readStateSnapshot(state) &&
            static_cast<std::size_t>(region.offset) + region.size <= state.size()) {
            return std::vector<std::uint8_t>(
                state.begin() + region.offset,
                state.begin() + region.offset + region.size);
        }
    }
    return sys.saveSramBytes();
}

inline std::uint64_t hashFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return 0;
    const std::streamsize size = in.tellg();
    if (size <= 0) return 0;
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size)) return 0;
    return rp::lsdj::SampleCache::hashBytes(buf.data(), buf.size());
}

inline bool spill(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

} // namespace sram_autosave

// Flush `sys`'s battery RAM to its resolved sav path (the suffix-derived sibling,
// or the user-paired `savPath` override) if it changed since the last call.
// `lastHash` is per-system caller-owned state (nullopt = never checked):
//   - unchanged since last write  -> no-op, returns false
//   - first check + identical file already on disk -> seed hash, returns false
//   - changed (or no/needs file)  -> write, update hash, returns true
// Returns false (no-op) for systems with no romPath or no battery.
inline bool autoSaveSramToSibling(SystemBase& sys,
                                  std::optional<std::uint64_t>& lastHash) {
    const std::string path = sram_autosave::resolveSavPath(sys);
    if (path.empty()) return false;

    const std::vector<std::uint8_t> bytes = sram_autosave::readSram(sys);
    if (bytes.empty()) return false;

    const std::uint64_t h = rp::lsdj::SampleCache::hashBytes(bytes.data(), bytes.size());
    if (lastHash && *lastHash == h) return false; // unchanged since last write

    if (!lastHash) {
        // First observation: don't rewrite an identical sibling that was just
        // loaded — only adopt its hash. (A missing/different file falls through.)
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && sram_autosave::hashFile(path) == h) {
            lastHash = h;
            return false;
        }
    }

    if (!sram_autosave::spill(path, bytes)) return false;
    lastHash = h;
    return true;
}

} // namespace rp
