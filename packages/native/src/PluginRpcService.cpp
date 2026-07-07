#include "PluginRpcService.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "EmbeddedRoms.hpp"
#include "Version.hpp"
#include "config/RecentFiles.hpp"
#include "config/SchemaVersions.hpp"
#include "config/UserConfig.hpp"
#include "lsdj/KitCompiler.hpp"
#include "lsdj/ProjectKitRecompile.hpp"
#include "lsdj/SampleCache.hpp"
#include "project/Project.hpp"
#include "project/ProjectPaths.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/InputTypes.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/RomFormat.hpp"
#include "system/SramAutoSave.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/Hash.hpp"

namespace {

// File-browser glob filters (whitespace-separated patterns + human label),
// passed straight through to DPF's FileBrowserOptions.
constexpr const char* kRomPatterns     = "*.gb *.gbc *.gba *.nes";
constexpr const char* kRomFilterName   = "ROM files";
// The Open browser also accepts a `.sav`, which we pair with a ROM (see
// handleOpenRomSelection). The 2nd (ROM-for-sav) browser uses kRomPatterns.
constexpr const char* kRomOrSavPatterns   = "*.gb *.gbc *.gba *.nes *.sav";
constexpr const char* kRomOrSavFilterName = "ROM or save (.sav)";
constexpr const char* kAudioPatterns   = "*.wav *.mp3 *.flac";
constexpr const char* kAudioFilterName = "Audio files";
constexpr const char* kProjPatterns    = "*.rplg";
constexpr const char* kProjFilterName  = "RetroPlug project";
constexpr const char* kZipPatterns     = "*.zip";
constexpr const char* kZipFilterName   = "Zip archive";
constexpr const char* kSramPatterns    = "*.sav";
constexpr const char* kSramFilterName  = "Save RAM";
constexpr const char* kStatePatterns   = "*.ss?";
constexpr const char* kStateFilterName = "Savestate";

// File slurper. Runs on the UI thread. Empty vector on any failure.
std::vector<std::uint8_t> slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size))
        return {};
    return buf;
}

std::string slurpString(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!in.read(out.data(), size))
        return {};
    return out;
}

bool spillString(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool spillBytes(const std::string& path, std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

} // namespace

PluginRpcService::PluginRpcService(Project* project,
                                   CommandQueue* commands,
                                   EventQueue* events,
                                   std::atomic<double>* sampleRate,
                                   std::atomic<SystemId>* focusedSystemId,
                                   UserConfig* userConfig,
                                   RecentFiles* recentFiles)
    : project_(project),
      commands_(commands),
      events_(events),
      sampleRate_(sampleRate),
      focusedSystemId_(focusedSystemId),
      userConfig_(userConfig),
      recentFiles_(recentFiles) {}

// Defined here (not in the header) so the std::unique_ptr<KitCompiler>
// destructor can see the complete KitCompiler type.
PluginRpcService::~PluginRpcService() = default;

void PluginRpcService::emit(const std::string& channel, const std::string& payload) const {
    if (emitEvent_) emitEvent_(channel, payload);
}

std::uint32_t PluginRpcService::assignSavSuffix(const std::string& romPath) const {
    if (!project_ || romPath.empty()) return 0;
    const auto ownedByLive = [&](std::uint32_t cand) {
        for (const auto& sys : project_->systems())
            if (sys && sys->romPath() == romPath && sys->savSuffix() == cand)
                return true;
        return false;
    };
    const auto fileOnDisk = [&](std::uint32_t cand) {
        std::error_code ec;
        return std::filesystem::exists(
            rp::sram_autosave::siblingSavPath(romPath, cand), ec);
    };
    // Prefer the plain `<rom>.sav` when no live system owns it — reclaiming it is
    // the normal single-instance "resume my save" case (the loader reads it back).
    if (!ownedByLive(0)) return 0;
    // For a disambiguated slot, skip any suffix whose `<rom>-N.sav` already exists
    // on disk, even if no live system owns it: that file belongs to a since-removed
    // instance, and a duplicate (which carries the source's SRAM, not the file's)
    // would otherwise auto-save straight over it. Grow to the next free slot so no
    // orphaned battery file is ever clobbered.
    std::uint32_t n = 2;           // skip 1 — duplicates read naturally as -2, -3, ...
    while (ownedByLive(n) || fileOnDisk(n)) ++n;
    return n;
}

SystemBase* PluginRpcService::buildSystemFromPath(const std::string& path, bool disambiguate,
                                                  const std::string& explicitSav) {
    if (!project_ || !sampleRate_) {
        std::fprintf(stderr, "buildSystemFromPath: shared DSP state unavailable (LV2-UI?)\n");
        return nullptr;
    }
    // When adding an instance, claim a non-colliding loose-battery suffix so two
    // systems backed by the same ROM file don't auto-save over one `<rom>.sav`.
    const std::uint32_t suffix = disambiguate ? assignSavSuffix(path) : 0;

    // Optional explicit battery save the caller paired with this ROM. Seeds the
    // instance's SRAM, and — unless it's already this system's natural sibling —
    // becomes a persisted override so future saves target that exact file
    // (rather than the suffix-derived sibling). Keeping the common "picked the
    // sibling" case override-free preserves relink-following.
    std::vector<std::uint8_t> explicitSram;
    std::string savPathOverride;
    if (!explicitSav.empty()) {
        explicitSram = slurp(explicitSav);
        if (explicitSram.empty()) {
            std::fprintf(stderr,
                "buildSystemFromPath: explicit sav '%s' unreadable; using sibling\n",
                explicitSav.c_str());
        } else {
            std::error_code ec;
            const auto picked   = std::filesystem::weakly_canonical(explicitSav, ec);
            const auto siblingN = std::filesystem::weakly_canonical(
                rp::sram_autosave::siblingSavPath(path, suffix), ec);
            const auto sibling0 = std::filesystem::weakly_canonical(
                rp::sram_autosave::siblingSavPath(path, 0), ec);
            // Only pin an override when the pick is a genuinely different file from
            // this ROM's managed loose-battery siblings. If it's the plain <rom>.sav
            // or this instance's own <rom>-N.sav, leave the override empty so the
            // suffix mechanism owns the file — otherwise an *added* instance that
            // picked <rom>.sav would auto-save over the suffix-0 instance.
            if (picked != siblingN && picked != sibling0) savPathOverride = explicitSav;
        }
    }

    std::vector<std::uint8_t> bytes = slurp(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "buildSystemFromPath: failed to read '%s'\n", path.c_str());
        emit("rom-error", path);
        return nullptr;
    }

    // Content-based dispatch: iNES magic → MesenNesSystem, GBA Nintendo logo
    // at $0004 → MesenGbaSystem, Game Boy Nintendo logo at $0104 → SameBoySystem,
    // anything else → reject. Mislabelled extensions still route correctly;
    // unrelated files surface as "rom-error" instead of being executed.
    const RomFormat fmt = detectRomFormat(bytes);
    if (fmt == RomFormat::Unknown) {
        std::fprintf(stderr,
            "buildSystemFromPath: '%s' is not a recognised Game Boy, NES, or GBA ROM\n",
            path.c_str());
        emit("rom-error", path);
        return nullptr;
    }

    return constructInstanceCore(fmt, path, /*embeddedRom*/ "", suffix, std::move(bytes),
                                 std::move(explicitSram), savPathOverride);
}

SystemBase* PluginRpcService::constructInstanceCore(
        RomFormat fmt, const std::string& romPath, const std::string& embeddedRom,
        std::uint32_t suffix, std::vector<std::uint8_t> romBytes,
        std::vector<std::uint8_t> explicitSram, const std::string& savPathOverride) {
    if (!project_ || !sampleRate_) {
        std::fprintf(stderr, "constructInstanceCore: shared DSP state unavailable (LV2-UI?)\n");
        return nullptr;
    }
    const SystemId id = project_->nextSystemId();
    const double sr = sampleRate_->load(std::memory_order_acquire);

    if (fmt == RomFormat::Nes) {
        MesenNesConfig cfg;
        cfg.romPath   = romPath;
        cfg.savSuffix = suffix;
        cfg.savPath   = savPathOverride;
        if (!explicitSram.empty()) cfg.sram = std::move(explicitSram);
        auto sys = std::make_unique<MesenNesSystem>(id, cfg, std::move(romBytes));
        sys->onActivate(sr);
        return sys.release();
    }

    if (fmt == RomFormat::Gba) {
        MesenGbaConfig cfg;
        cfg.romPath   = romPath;
        cfg.savSuffix = suffix;
        cfg.savPath   = savPathOverride;
        if (!explicitSram.empty()) cfg.sram = std::move(explicitSram);
        cfg.biosPath = "build/firmware/gba_bios.bin";
        auto sys = std::make_unique<MesenGbaSystem>(id, cfg, std::move(romBytes));
        sys->onActivate(sr);
        return sys.release();
    }

    SameBoyConfig cfg;
    cfg.romPath   = romPath;
    cfg.savSuffix = suffix;
    cfg.savPath   = savPathOverride;
    cfg.model     = SameBoyModel::CgbC;
    cfg.fastBoot  = true;
    // A binary-baked ROM (e.g. mGB) has no file: mark it embedded so saves
    // re-supply the bytes from the binary and stay small (embedRom=false).
    if (!embeddedRom.empty()) {
        cfg.embeddedRom = embeddedRom;
        cfg.embedRom    = false;
    }

    // Battery RAM: an explicit paired save wins; otherwise the sibling .sav for
    // this instance's suffix (only for a real file — an embedded/pathless ROM
    // has no sibling). Missing file is fine — the instance starts empty.
    if (!explicitSram.empty()) {
        cfg.sram = std::move(explicitSram);
    } else if (!romPath.empty()) {
        std::vector<std::uint8_t> sramBytes =
            slurp(rp::sram_autosave::siblingSavPath(romPath, suffix));
        if (!sramBytes.empty())
            cfg.sram = std::move(sramBytes);
    }

    auto sys = std::make_unique<SameBoySystem>(id, cfg, std::move(romBytes));
    sys->onActivate(sr);
    return sys.release();
}

std::string PluginRpcService::writeSiblingProject(const SystemConfig& sysCfg,
                                                  const std::string& romPath) {
    std::filesystem::path proj = std::filesystem::path(romPath);
    proj.replace_extension(".rplg");
    const std::string projPath = proj.string();

    std::error_code ec;
    if (std::filesystem::exists(proj, ec) && !ec) {
        return projPath;   // keep the existing project beside the ROM
    }

    ProjectConfig cfg;                 // default schemaVersion / settings
    cfg.systems.push_back(sysCfg);     // single freshly-built system
    // Store paths relative to the .rplg's dir when the asset is under it, so the
    // sibling records the ROM's bare basename and the folder stays relocatable.
    rp::project_paths::toRelative(cfg, proj.parent_path().string());
    std::string json;
    try {
        json = projectConfigToJsonFile(cfg);   // thin, strips embedded binaries
    } catch (const std::exception& e) {
        std::fprintf(stderr, "writeSiblingProject: serialize failed: %s\n", e.what());
        return {};
    }
    if (json.empty()) return {};
    const std::vector<std::uint8_t> bytes(json.begin(), json.end());
    if (!spillBytes(projPath, bytes)) {
        std::fprintf(stderr, "writeSiblingProject: write failed for '%s'\n", projPath.c_str());
        return {};
    }
    return projPath;
}

bool PluginRpcService::constructSystem(std::string romPath, std::string embeddedRom,
                                       std::string mode) {
    if (!commands_ || !project_ || !sampleRate_) return false;

    // Source the ROM bytes + pick the backend. A non-empty embedded marker (e.g.
    // "mgb") supplies binary-baked bytes and is always Game Boy; otherwise the
    // file is slurped and its format auto-detected (a mislabelled extension
    // still routes correctly, a non-ROM is rejected). No bytes cross the bridge
    // — the UI hands us the path; the slurp stays native.
    std::vector<std::uint8_t> romBytes;
    RomFormat fmt;
    if (!embeddedRom.empty()) {
        const std::span<const std::uint8_t> rom = rp::embeddedRom(embeddedRom);
        romBytes.assign(rom.begin(), rom.end());
        fmt = RomFormat::Gb;
    } else {
        romBytes = slurp(romPath);
        fmt = detectRomFormat(romBytes);
    }
    if (romBytes.empty() || fmt == RomFormat::Unknown) return false;

    const bool add = (mode == "add");
    // Adding disambiguates the loose-battery suffix so a second copy of the same
    // ROM gets its own `<rom>-N.sav`; a "load" owns the plain `<rom>.sav`.
    const std::uint32_t suffix = (add && !romPath.empty()) ? assignSavSuffix(romPath) : 0;

    SystemBase* sys = constructInstanceCore(fmt, romPath, embeddedRom, suffix,
                                            std::move(romBytes), /*explicitSram*/ {},
                                            /*savPathOverride*/ "");
    if (!sys) return false;

    // A "load" of a real file writes a thin sibling `.rplg` and tracks it in the
    // recent list (mirrors the old loadRomFromPath, minus the sibling-`.rplg`
    // deferral which now runs in the UI before we're called); an "add", or a
    // pathless embedded mGB, does neither. Capture the config before tryPush
    // transfers ownership to the DSP.
    const bool writeSibling = (!add && !romPath.empty());
    SystemConfig sysCfg;
    if (writeSibling) sysCfg = sys->snapshotConfig();

    const Command cmd = add ? Command::makeAddSystem(sys) : Command::makeLoadRom(sys);
    if (!commands_->tryPush(cmd)) {
        std::fprintf(stderr, "constructSystem: command queue full\n");
        delete sys;
        return false;
    }
    markProjectDirty();

    if (writeSibling) {
        const std::string projPath = writeSiblingProject(sysCfg, romPath);
        if (!projPath.empty()) {
            if (recentFiles_) recentFiles_->add(projPath);
            currentProjectPath_ = projPath;   // subsequent saves are silent
            projectDirty_       = false;      // the on-disk project matches the load
        }
    }
    // No emit: the UI ignores rom-loaded/rom-error, and the DSP's ConfigChanged
    // (from adopt/swap) drives the "config-changed" re-seed. Errors are our
    // bool return.
    return true;
}

std::string PluginRpcService::findSiblingRom(const std::string& savPath) const {
    std::filesystem::path p(savPath);
    const std::filesystem::path dir = p.parent_path();
    const std::string stem = p.stem().string();

    // Candidate stems: the save's own stem, plus (when it's a `<base>-N.sav`
    // duplicate slot) the base stem — so `game-2.sav` can pair with `game.gb`.
    // Exact stem is tried first so `song-2.sav` prefers `song-2.gb` over `song.gb`.
    std::vector<std::string> stems{ stem };
    const auto dash = stem.rfind('-');
    if (dash != std::string::npos && dash + 1 < stem.size() &&
        stem.find_first_not_of("0123456789", dash + 1) == std::string::npos)
        stems.push_back(stem.substr(0, dash));

    // Probes lowercase extensions only; a mixed-case ROM filename on a
    // case-sensitive filesystem is reached via the fallback 2nd browser instead.
    static constexpr const char* kExts[] = { ".gb", ".gbc", ".gba", ".nes" };
    for (const auto& s : stems) {
        for (const char* ext : kExts) {
            const std::filesystem::path cand = dir / (s + ext);
            std::error_code ec;
            if (!std::filesystem::exists(cand, ec)) continue;
            const auto bytes = slurp(cand.string());
            if (!bytes.empty() && detectRomFormat(bytes) != RomFormat::Unknown)
                return cand.string();   // content-validated ROM
        }
    }
    return {};
}

bool PluginRpcService::loadRomPaired(const std::string& romPath,
                                     const std::string& savPath, bool add) {
    if (!commands_) {
        emit("rom-error", romPath);
        return false;
    }
    SystemBase* sys = buildSystemFromPath(romPath, /*disambiguate*/ add, savPath);
    if (!sys) return false;   // buildSystemFromPath already emitted rom-error

    // A pinned paired-save override is a deliberate edit that must reach disk.
    const bool hasOverride = !sys->savPath().empty();

    // Capture the config before tryPush transfers ownership to the DSP.
    SystemConfig sysCfg = sys->snapshotConfig();

    Command cmd = add ? Command::makeAddSystem(sys) : Command::makeLoadRom(sys);
    if (!commands_->tryPush(cmd)) {
        std::fprintf(stderr, "loadRomPaired: command queue full\n");
        delete sys;
        emit("rom-error", romPath);
        return false;
    }
    markProjectDirty();

    // Replace mode mirrors loadRomFromPath's recent/currentProjectPath bookkeeping,
    // but deliberately does NOT defer to a sibling `<rom>.rplg` — the user picked a
    // specific save, so the pairing wins over the project's own SRAM.
    if (!add) {
        std::error_code ec;
        std::filesystem::path siblingRplgPath(romPath);
        siblingRplgPath.replace_extension(".rplg");
        const bool siblingExisted = std::filesystem::exists(siblingRplgPath, ec);
        const std::string projPath = writeSiblingProject(sysCfg, romPath);
        if (!projPath.empty()) {
            if (recentFiles_) recentFiles_->add(projPath);
            currentProjectPath_ = projPath;
            // writeSiblingProject leaves a pre-existing sibling untouched, so it
            // won't carry a freshly-pinned override. Keep the project dirty in
            // that case so the pairing survives a save/close (saveProjectToPath
            // serializes the live config, override included) instead of being
            // silently discarded when the stale sibling reloads.
            projectDirty_ = siblingExisted && hasOverride;
        }
    }
    emit("rom-loaded", romPath);
    return true;
}

bool PluginRpcService::handleOpenRomSelection(const std::string& path, bool add) {
    // Content decides: a real ROM loads/adds exactly as before. Reading the file
    // also lets a `.sav` (which detectRomFormat rejects) route into pairing.
    const auto bytes = slurp(path);
    if (!bytes.empty() && detectRomFormat(bytes) != RomFormat::Unknown)
        // ROM construction orchestration lives in the UI (TS) now; hand it the
        // path. TS decides load-vs-add (its own pending latch), does the
        // sibling-`.rplg` deferral, then drives constructSystem. Only the `.sav`
        // pairing below stays native (deferred).
        { emit("rom-path-selected", path); return true; }

    // Not a ROM. Only a `.sav` is treated as a save to pair; anything else falls
    // through to the normal loader so its existing "rom-error" fires.
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".sav")
        // ROM construction orchestration lives in the UI (TS) now; hand it the
        // path. TS decides load-vs-add (its own pending latch), does the
        // sibling-`.rplg` deferral, then drives constructSystem. Only the `.sav`
        // pairing below stays native (deferred).
        { emit("rom-path-selected", path); return true; }

    // A picked save: pair with its sibling ROM if there is one, else arm the 2nd
    // (ROM) browser to let the user point at the ROM.
    const std::string rom = findSiblingRom(path);
    if (!rom.empty())
        return loadRomPaired(rom, path, add);

    if (!openFileBrowser_) {   // no browser available (e.g. LV2-UI) — give up
        emit("rom-error", path);
        return false;
    }
    pendingSavPath_  = path;
    pendingFileMode_ = add ? PendingFileMode::PairRomForSavAdd
                           : PendingFileMode::PairRomForSav;
    openFileBrowser_("Select the ROM for this save", false, nullptr,
                     kRomPatterns, kRomFilterName);
    return true;
}

// --- Project save/export byte-mover primitives -----------------------------
//
// The .rplg save + zip-export orchestration moved to shared TS
// (@retroplug/retroplug projectSerialization.ts, the same module the CLI harness
// drives). These primitives move the bytes it can't; the UI runs them on
// "save-path-selected" / "export-path-selected" (emitted from
// onFileBrowserSelected) and finishes with notifyProjectSaved.

rfl::Bytestring PluginRpcService::readFile(std::string path) {
    const auto bytes = slurp(path);
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

bool PluginRpcService::writeFile(std::string path, std::vector<std::uint8_t> bytes) {
    return spillBytes(path, bytes);
}

rfl::Bytestring PluginRpcService::zipEntries(std::vector<ZipInput> entries) {
    MinizWriter zip;
    for (const auto& e : entries)
        if (!zip.add(e.name, e.bytes))
            throw std::runtime_error("zipEntries: failed to add entry " + e.name);
    const auto bytes = zip.finish();
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

std::vector<PluginRpcService::ZipEntry> PluginRpcService::unzipEntries(std::vector<std::uint8_t> bytes) {
    std::vector<ZipEntry> out;
    MinizReader zip(bytes);
    if (!zip.valid()) return out;
    for (const auto& name : zip.names()) {
        const auto data = zip.read(name);
        const auto* p = reinterpret_cast<const std::byte*>(data.data());
        out.push_back({ name, rfl::Bytestring(p, p + data.size()) });
    }
    return out;
}

PluginRpcService::ProjectSnapshot PluginRpcService::snapshotProjectConfig(std::string baseDir) {
    ProjectSnapshot out;
    if (!project_) return out;
    ProjectConfig cfg = project_->snapshotConfig();
    // baseDir non-empty => rebase a *copy* to project-relative form (the thin
    // path-only save); the live project keeps its absolute paths. Empty leaves
    // paths absolute (the self-contained zip export embeds every blob).
    if (!baseDir.empty())
        rp::project_paths::toRelative(cfg, baseDir);
    // Stamp the running build's schema, not whatever was loaded.
    cfg.schemaVersion = std::to_string(rp::schema::kProject);
    // Collect the stripped blobs (key + bytes) instead of writing them into a
    // zip — the templated project_binaries walk drives this sink, so TS owns the
    // zip framing while the codec stays native (see ProjectBinaries.hpp).
    struct Collector {
        std::vector<ZipEntry> blobs;
        bool add(std::string_view name, std::span<const std::uint8_t> bytes) {
            const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
            blobs.push_back({ std::string(name), rfl::Bytestring(p, p + bytes.size()) });
            return true;
        }
    } coll;
    project_binaries::strip(coll, cfg); // empties cfg's blobs into coll
    out.config = projectConfigToJson(cfg);
    out.blobs  = std::move(coll.blobs);
    return out;
}

bool PluginRpcService::notifyProjectSaved(std::string path, bool exported) {
    projectDirty_ = false;
    if (exported) {
        emit("project-exported", path);
    } else {
        if (recentFiles_) recentFiles_->add(path);
        currentProjectPath_ = path;
        emit("project-saved", path);
    }
    return true;
}

bool PluginRpcService::fileExists(std::string path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool PluginRpcService::commitProject(std::string config,
                                     std::vector<ZipInput> blobs,
                                     std::string path) {
    if (!commands_) { emit("project-error", path); return false; }

    auto parsed = projectConfigFromJson(config);
    if (!parsed) {
        std::fprintf(stderr, "commitProject: failed to parse config for '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    // Restore the keyed blob entries (zip export) back into the thin config so
    // the DSP's loadFromConfig sees the bytes. A thin JSON load carries no blobs;
    // addSystem re-reads ROM/SRAM from (now-relinked) paths + the sibling .sav.
    if (!blobs.empty()) {
        struct MapSource {
            const std::vector<ZipInput>* entries;
            bool has(std::string_view name) const {
                for (const auto& e : *entries) if (e.name == name) return true;
                return false;
            }
            std::vector<std::uint8_t> read(std::string_view name) const {
                for (const auto& e : *entries) if (e.name == name) return e.bytes;
                return {};
            }
        } src{&blobs};
        project_binaries::restore(src, *parsed);
    }
    // Path-only saves carry kit metadata but no compiled bytes — rebuild each kit
    // from its (now-relinked) source WAVs. Zip exports carry the bytes (no-op).
    if (rp::lsdj::projectHasKitsNeedingRecompile(*parsed)) {
        if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::lsdj::KitCompiler>();
        rp::lsdj::recompileMissingKits(*parsed, *kitCompiler_);
    }
    // Heap-allocate the parsed config; DSP frees after applying.
    auto* heap = new ProjectConfig(std::move(*parsed));
    if (!commands_->tryPush(Command::makeLoadProject(heap))) {
        std::fprintf(stderr, "commitProject: command queue full\n");
        delete heap;
        emit("project-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path);
    currentProjectPath_ = path;
    projectDirty_ = false;          // freshly loaded project is clean
    // A fresh project replaces all systems; drop stale per-system SRAM state so
    // the new systems re-seed their load baselines.
    sramLoadBaseline_.clear();
    sramSavedHashes_.clear();
    emit("project-loaded", path);
    return true;
}

bool PluginRpcService::openRelinkBrowser(std::string kind) {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::Relink;
    const char* title;
    const char* patterns;
    const char* filterName;
    if (kind == "sram") {
        title = "Locate save (.sav)";  patterns = kSramPatterns;  filterName = kSramFilterName;
    } else if (kind == "sample") {
        title = "Locate sample (WAV / MP3 / FLAC)"; patterns = kAudioPatterns; filterName = kAudioFilterName;
    } else {   // "rom" (default)
        title = "Locate ROM";          patterns = kRomPatterns;   filterName = kRomFilterName;
    }
    openFileBrowser_(title, false, nullptr, patterns, filterName);
    return true;
}

void PluginRpcService::onFileBrowserSelected(const char* path) {
    if (!path || !*path) {
        pendingFileMode_ = PendingFileMode::LoadRom;
        pendingFileSystemId_ = 0;
        pendingRelinkRecentPath_.clear();
        pendingSavPath_.clear();
        return;
    }
    // Capture the mode at entry: handleOpenRomSelection may *arm* a pairing mode
    // (opening a 2nd browser), which must survive the reset below.
    const PendingFileMode entryMode = pendingFileMode_;
    switch (entryMode) {
        case PendingFileMode::AddRom:      handleOpenRomSelection(path, /*add*/ true);  break;
        // Project load orchestration lives in the UI (TS) now; hand it the path.
        case PendingFileMode::LoadProject: emit("load-path-selected", path);  break;
        // Save / export orchestration runs in shared TS: hand the chosen path
        // to the UI, which drives saveRplg/saveProjectFile over the byte-mover
        // primitives (same event-style flow as LoadSample / Relink below).
        case PendingFileMode::SaveProject: emit("save-path-selected", path);   break;
        case PendingFileMode::ExportZip:   emit("export-path-selected", path); break;
        case PendingFileMode::LoadSample:  emit("sample-path-selected", path); break;
        case PendingFileMode::Relink:      emit("relink-path-selected", path); break;
        case PendingFileMode::RelinkRecent:
            if (recentFiles_ && !pendingRelinkRecentPath_.empty())
                recentFiles_->relink(pendingRelinkRecentPath_, path);   // fires onChange
            break;
        case PendingFileMode::SaveSram:    saveSramToPath(pendingFileSystemId_, path);  break;
        case PendingFileMode::LoadSram:    loadSramFromPath(pendingFileSystemId_, path); break;
        case PendingFileMode::SaveState:   saveStateToPath(pendingFileSystemId_, path); break;
        case PendingFileMode::LoadState:   loadStateFromPath(pendingFileSystemId_, path); break;
        case PendingFileMode::PairRomForSav:
        case PendingFileMode::PairRomForSavAdd: {
            // 2nd dialog returned the ROM for the stashed save. Guard against the
            // user picking another non-ROM.
            const bool add = (entryMode == PendingFileMode::PairRomForSavAdd);
            const auto bytes = slurp(path);
            if (!bytes.empty() && detectRomFormat(bytes) != RomFormat::Unknown)
                loadRomPaired(path, pendingSavPath_, add);
            else
                emit("rom-error", path);
            break;
        }
        case PendingFileMode::LoadRom:
        default:                           handleOpenRomSelection(path, /*add*/ false); break;
    }
    // If handling armed a 2nd (pairing) browser, keep the pending state alive.
    if (pendingFileMode_ != entryMode &&
        (pendingFileMode_ == PendingFileMode::PairRomForSav ||
         pendingFileMode_ == PendingFileMode::PairRomForSavAdd))
        return;
    pendingFileMode_ = PendingFileMode::LoadRom;
    pendingFileSystemId_ = 0;
    pendingRelinkRecentPath_.clear();
    pendingSavPath_.clear();
}

std::optional<PluginRpcService::FrameResponse>
PluginRpcService::getFrame(std::uint32_t systemId) {
    if (!project_) return std::nullopt;

    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return std::nullopt;

    FrameBufferTriple* fb = sys->framebuffer();
    if (!fb) return std::nullopt;

    const std::uint32_t w      = fb->width();
    const std::uint32_t h      = fb->height();
    const std::size_t   pixels = static_cast<std::size_t>(w) * h;

    std::vector<std::uint32_t> staging(pixels, 0u);
    if (!fb->readInto(staging.data(), w * h))
        return std::nullopt;

    FrameResponse out;
    out.width  = w;
    out.height = h;
    out.buffer.resize(pixels * sizeof(std::uint32_t));
    std::memcpy(out.buffer.data(), staging.data(), out.buffer.size());
    return out;
}

bool PluginRpcService::openRomBrowser(OpenRomOpts opts) {
    if (!openFileBrowser_) {
        std::fprintf(stderr, "plugin.openRomBrowser: no open-browser callback registered\n");
        return false;
    }
    pendingSavPath_.clear();   // defensive: drop any save stranded by a prior flow
    pendingFileMode_ = (opts.mode && *opts.mode == "add")
        ? PendingFileMode::AddRom
        : PendingFileMode::LoadRom;
    // Also accept a `.sav` here; onFileBrowserSelected pairs it with a ROM.
    openFileBrowser_("Open ROM or .sav", false, nullptr,
                     kRomOrSavPatterns, kRomOrSavFilterName);
    return true;
}

bool PluginRpcService::openSaveProjectBrowser() {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::SaveProject;
    std::string defaultName = "project.rplg";
    if (!currentProjectPath_.empty()) {
        auto name = std::filesystem::path(currentProjectPath_).filename();
        if (!name.empty()) defaultName = name.string();
    }
    openFileBrowser_("Save RetroPlug project", true, defaultName.c_str(), kProjPatterns, kProjFilterName);
    return true;
}

bool PluginRpcService::openExportZipBrowser() {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::ExportZip;
    openFileBrowser_("Export RetroPlug zip", true, "project.zip", kZipPatterns, kZipFilterName);
    return true;
}

bool PluginRpcService::openLoadProjectBrowser() {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::LoadProject;
    openFileBrowser_("Load RetroPlug project", false, nullptr, kProjPatterns, kProjFilterName);
    return true;
}

bool PluginRpcService::removeSystem(std::uint32_t id) {
    if (!commands_) return false;
    markProjectDirty();
    return commands_->tryPush(Command::makeRemoveSystem(static_cast<SystemId>(id)));
}

bool PluginRpcService::duplicateSystem(std::uint32_t id) {
    if (!project_ || !commands_ || !sampleRate_) return false;
    SystemBase* src = project_->findSystem(static_cast<SystemId>(id));
    if (!src) return false;

    const SystemId newId = project_->nextSystemId();
    const double   sr    = sampleRate_->load(std::memory_order_acquire);
    // Prefer cloning from the DSP-published state snapshot (race-free); fall
    // back to clone() (live read) when no snapshot exists yet or the backend
    // doesn't support snapshot-based cloning.
    std::unique_ptr<SystemBase> clone;
    std::vector<std::uint8_t> state;
    if (src->readStateSnapshot(state) && !state.empty())
        clone = src->cloneFromState(newId, sr, state);
    if (!clone) clone = src->clone(newId, sr);
    if (!clone) return false;

    // The clone copied the source's ROM path, so give it its own loose-battery
    // suffix (`<rom>-N.sav`) — otherwise both would auto-save over one `<rom>.sav`.
    clone->setSavSuffix(assignSavSuffix(clone->romPath()));

    SystemBase* released = clone.release();
    if (!commands_->tryPush(Command::makeAddSystem(released))) {
        std::fprintf(stderr, "duplicateSystem: command queue full\n");
        delete released;
        return false;
    }
    markProjectDirty();
    return true;
}

bool PluginRpcService::clearCurrentProjectPath() {
    currentProjectPath_.clear();
    return true;
}

std::vector<PluginRpcService::SystemEntry> PluginRpcService::listSystems() {
    std::vector<SystemEntry> out;
    if (!project_) return out;

    for (const auto& sys : project_->systems()) {
        if (!sys) continue;
        SystemEntry entry;
        entry.id = sys->id();

        // Per-kind fields. The Mesen-backed kinds (NES/GBA) currently surface
        // only the kind tag — there's no per-system config exposed to the UI yet.
        if (auto* sb = dynamic_cast<const SameBoySystem*>(sys.get())) {
            entry.kind        = "sameboy";
            entry.gainDb      = sb->config_.gainDb;
            entry.linkGroupId = sb->config_.linkGroupId;
            entry.model       = static_cast<std::uint32_t>(sb->config_.model);
            entry.highpass    = static_cast<std::uint32_t>(sb->config_.highpass);
            for (const auto& rc : sb->config_.roles) {
                if (const auto* lsdj = rfl::get_if<LsdjSyncConfig>(&rc.variant())) {
                    entry.lsdjSyncMode     = static_cast<std::uint32_t>(lsdj->mode);
                    entry.lsdjTempoDivisor = lsdj->tempoDivisor;
                } else if (rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
                    entry.hasLsdjKitRole = true;
                }
            }
        } else if (sys->kind() == SystemKind::MesenNes) {
            entry.kind = "nes";
        } else {
            entry.kind = "gba";
        }
        // Kind-agnostic fields. fastBoot is nullopt on Mesen (returns no
        // value); reloadOnRomChange defaults to false on backends that
        // don't override.
        if (auto fb = sys->fastBoot()) entry.fastBoot = *fb;
        entry.reloadOnRomChange = sys->wantsRomReload();
        out.push_back(std::move(entry));
    }
    return out;
}

PluginRpcService::ProjectView PluginRpcService::getProjectView() {
    // Compose the individual getters so the semantics stay identical (defaults,
    // zoom-raw handling) — this is purely a fan-in that hands the UI one atomic
    // snapshot instead of six separate round-trips.
    ProjectView v;
    v.systems      = listSystems();
    v.focus        = getFocus();
    v.midiRouting  = getMidiRouting();
    v.audioRouting = getAudioRouting();
    v.layout       = getLayout();
    v.projectZoom  = getProjectZoom();
    return v;
}

bool PluginRpcService::setFocus(std::uint32_t id) {
    if (!focusedSystemId_) return false;
    focusedSystemId_->store(static_cast<SystemId>(id), std::memory_order_release);
    return true;
}

std::uint32_t PluginRpcService::getFocus() {
    if (!focusedSystemId_) return 0u;
    return focusedSystemId_->load(std::memory_order_acquire);
}

bool PluginRpcService::pressButton(std::int32_t button,
                                   bool down,
                                   std::optional<std::uint32_t> systemId) {
    if (!commands_ || !project_) return false;

    // Resolve target: explicit > focused > first.
    SystemId target = 0;
    if (systemId) target = static_cast<SystemId>(*systemId);
    if (target == 0 && focusedSystemId_)
        target = focusedSystemId_->load(std::memory_order_acquire);
    if (target == 0) {
        const auto& systems = project_->systems();
        if (systems.empty() || !systems.front()) return false;
        target = systems.front()->id();
    }

    // The byte is reinterpreted by the target system. SameBoy → GameboyButton,
    // Mesen → NesButton. Both enums are position-aligned for the eight
    // standard buttons so callers pass a single int regardless of target.
    Command cmd = Command::makeButtonPress(target,
                                           static_cast<std::uint8_t>(button),
                                           down);
    return commands_->tryPush(cmd);
}

bool PluginRpcService::setLinkGroupId(std::uint32_t id, std::uint32_t groupId) {
    if (!commands_) return false;
    if (groupId > 255u) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetLinkGroup(static_cast<SystemId>(id),
                                  static_cast<std::uint8_t>(groupId)));
}

std::uint32_t PluginRpcService::getMidiRouting() {
    if (!project_) return static_cast<std::uint32_t>(MidiRouting::SendToAll);
    return static_cast<std::uint32_t>(project_->config().settings.midiRouting);
}

bool PluginRpcService::setMidiRouting(std::uint32_t routing) {
    if (!commands_) return false;
    // Reject out-of-range rather than narrowing into a meaningful enum by
    // accident — keep the JS side in sync with the C++ enum.
    if (routing > static_cast<std::uint32_t>(MidiRouting::MidiChannelToInstance))
        return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetMidiRouting(static_cast<MidiRouting>(routing)));
}

std::uint32_t PluginRpcService::getAudioRouting() {
    if (!project_) return static_cast<std::uint32_t>(AudioRouting::Stereo);
    return static_cast<std::uint32_t>(project_->config().settings.audioRouting);
}

bool PluginRpcService::setAudioRouting(std::uint32_t routing) {
    if (!commands_) return false;
    if (routing > static_cast<std::uint32_t>(AudioRouting::OnePerInstance))
        return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetAudioRouting(static_cast<AudioRouting>(routing)));
}

std::string PluginRpcService::getVersion() {
    return RETROPLUG_VERSION_STRING;
}

std::uint32_t PluginRpcService::getZoom() {
    // 0 in ProjectSettings means "inherit from UserConfig::defaultZoom".
    std::uint8_t z = 0;
    if (project_) z = project_->config().settings.zoom;
    if (z == 0) {
        std::uint8_t def = 3;
        if (userConfig_) def = userConfig_->snapshot().defaultZoom;
        z = def;
    }
    if (z < 1) z = 1;
    if (z > 6) z = 6;
    return z;
}

std::uint32_t PluginRpcService::getProjectZoom() {
    // Raw per-project value: 0 = inherit the user default, 1..6 = explicit.
    if (!project_) return 0;
    std::uint8_t z = project_->config().settings.zoom;
    return z > 6 ? 0 : z;
}

bool PluginRpcService::setZoom(std::uint32_t zoom) {
    if (!commands_) return false;
    if (zoom > 6u) return false;   // 0 = inherit default, 1..6 = explicit
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetZoom(static_cast<std::uint8_t>(zoom)));
}

std::uint32_t PluginRpcService::getLayout() {
    if (!project_) return static_cast<std::uint32_t>(SystemLayout::Auto);
    return static_cast<std::uint32_t>(project_->config().settings.layout);
}

bool PluginRpcService::setLayout(std::uint32_t layout) {
    if (!commands_) return false;
    if (layout > static_cast<std::uint32_t>(SystemLayout::Grid)) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetLayout(static_cast<SystemLayout>(layout)));
}

bool PluginRpcService::resetSystem(std::uint32_t id) {
    if (!commands_) return false;
    return commands_->tryPush(Command::makeResetSystem(static_cast<SystemId>(id)));
}

bool PluginRpcService::newSram(std::uint32_t id) {
    if (!commands_) return false;
    return commands_->tryPush(Command::makeNewSram(static_cast<SystemId>(id)));
}

bool PluginRpcService::setFastBoot(std::uint32_t id, bool enabled) {
    if (!commands_) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetFastBoot(static_cast<SystemId>(id), enabled));
}

bool PluginRpcService::setModel(std::uint32_t id, std::uint32_t model) {
    if (!commands_) return false;
    if (model > static_cast<std::uint32_t>(SameBoyModel::Gbp)) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetModel(static_cast<SystemId>(id),
                              static_cast<SameBoyModel>(model)));
}

bool PluginRpcService::setHighpass(std::uint32_t id, std::uint32_t mode) {
    if (!commands_) return false;
    if (mode > static_cast<std::uint32_t>(SameBoyHighpass::RemoveDcOffset)) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetHighpass(static_cast<SystemId>(id),
                                 static_cast<SameBoyHighpass>(mode)));
}

bool PluginRpcService::setReloadOnRomChange(std::uint32_t id, bool enabled) {
    if (!commands_) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetReloadOnRomChange(static_cast<SystemId>(id), enabled));
}

void PluginRpcService::pumpRomWatchers() {
    if (!project_ || !commands_ || !sampleRate_) return;

    // First pass: prune entries whose system no longer exists or whose flag
    // is now off. Two passes so we don't mutate while iterating.
    std::vector<SystemId> toDrop;
    for (const auto& [sysId, _] : romWatchers_) {
        SystemBase* sys = project_->findSystem(sysId);
        if (!sys || !sys->wantsRomReload()) toDrop.push_back(sysId);
    }
    for (SystemId id : toDrop) romWatchers_.erase(id);

    // Second pass: walk live systems with the flag set. New entries record
    // current mtime without triggering a reload; existing entries diff and
    // trigger via buildSystemFromPath (already format-agnostic — picks
    // SameBoy / Mesen / GBA based on file content).
    for (const auto& sys : project_->systems()) {
        if (!sys || !sys->wantsRomReload()) continue;
        const std::string& romPath = sys->romPath();
        if (romPath.empty()) continue;

        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(romPath, ec);
        if (ec) continue;

        auto it = romWatchers_.find(sys->id());
        if (it == romWatchers_.end()) {
            romWatchers_.emplace(sys->id(), RomWatchEntry{romPath, mtime});
            continue;
        }
        if (it->second.path != romPath) {
            it->second.path  = romPath;
            it->second.mtime = mtime;
            continue;
        }
        if (mtime == it->second.mtime) continue;

        // Rebuild from disk via the format-agnostic loader. Carry the live
        // SRAM forward — prefer the race-free state snapshot, fall back direct.
        std::vector<std::uint8_t> liveSram;
        if (!sliceFromStateSnapshot(sys.get(), rp::MemoryType::Sram, liveSram))
            liveSram = sys->saveSramBytes();
        SystemBase* rebuilt = buildSystemFromPath(romPath, /*disambiguate*/ false);
        if (!rebuilt) {
            it->second.mtime = mtime;
            continue;
        }
        // Same ROM, reloaded from disk: keep this instance's loose-battery suffix
        // (and any user-paired sav override) so it keeps writing the same file.
        rebuilt->setSavSuffix(sys->savSuffix());
        rebuilt->setSavPath(sys->savPath());
        // Fold the live SRAM forward into the rebuilt system so in-game
        // progress survives the reload. Savestate intentionally drops.
        if (!liveSram.empty()) {
            auto acc = rebuilt->getMemory(rp::MemoryType::Sram,
                                          rp::AccessType::ReadWrite);
            if (acc.valid() && acc.size() > 0) {
                const std::size_t n = std::min(liveSram.size(), acc.size());
                std::memcpy(acc.data(), liveSram.data(), n);
            }
        }
        // ReplaceSystem swaps the slot keyed by the old id; the rebuilt
        // system has its own id from nextSystemId(). The DSP handler
        // updates focusedSystemAtomic if the focused id was the swapped
        // one.
        if (!commands_->tryPush(Command::makeReplaceSystem(sys->id(), rebuilt))) {
            std::fprintf(stderr, "pumpRomWatchers: command queue full\n");
            delete rebuilt;
            continue;
        }
        it->second.mtime = mtime;
    }
}

bool PluginRpcService::setSramMirror(std::string mode) {
    if (!userConfig_) return false;
    const rp::SramMirror parsed = rp::sramMirrorFromString(mode);
    if (!userConfig_->setSramMirror(parsed)) return false;
    // Push the new mode to the DSP immediately so getState/deactivate flush with
    // it right away; the pump also reconciles, but don't wait a tick.
    if (commands_ && commands_->tryPush(Command::makeSetSramMirror(parsed)))
        lastPushedSramMirror_ = static_cast<int>(parsed);
    return true;
}

bool PluginRpcService::setDefaultZoom(std::uint32_t zoom) {
    if (!userConfig_) return false;
    if (zoom < 1u || zoom > 6u) return false;
    return userConfig_->setDefaultZoom(static_cast<std::uint8_t>(zoom));
}

void PluginRpcService::pumpSramAutoSave() {
    if (!project_) return;

    // Always (regardless of the Auto Save preference): prune dead systems and
    // seed the per-system load-baseline used by the unsaved-SRAM check, so a
    // change since load can be detected even with auto-save off.
    auto prune = [&](auto& map) {
        std::vector<SystemId> dead;
        for (const auto& kv : map)
            if (!project_->findSystem(kv.first)) dead.push_back(kv.first);
        for (SystemId id : dead) map.erase(id);
    };
    prune(sramSavedHashes_);
    prune(sramLoadBaseline_);

    for (const auto& sys : project_->systems()) {
        if (!sys || sys->romPath().empty()) continue;
        if (sramLoadBaseline_.count(sys->id())) continue;          // seeded once
        const auto bytes = rp::sram_autosave::readSram(*sys);
        if (!bytes.empty())
            sramLoadBaseline_[sys->id()] =
                rp::lsdj::SampleCache::hashBytes(bytes.data(), bytes.size());
    }

    // Keep the DSP's mirror mode (used by its getState/deactivate flush hooks)
    // in sync with UserConfig. Re-push whenever it drifts — a config.json edit
    // picked up by efsw, or the initial value the DSP hasn't heard yet.
    if (userConfig_ && commands_) {
        const int mode = static_cast<int>(userConfig_->sramMirror());
        if (mode != lastPushedSramMirror_ &&
            commands_->tryPush(Command::makeSetSramMirror(
                static_cast<rp::SramMirror>(mode))))
            lastPushedSramMirror_ = mode;
    }

    // Idle-tick writes only in Continuous mode; OnProjectSave/Off leave the
    // loose `.sav` to the DSP flush hooks (host save / quit). Throttled.
    if (!userConfig_ || userConfig_->sramMirror() != rp::SramMirror::Continuous) return;
    const auto now = std::chrono::steady_clock::now();
    if (lastSramAutoSave_.time_since_epoch().count() != 0) {
        const std::chrono::duration<double> elapsed = now - lastSramAutoSave_;
        if (elapsed.count() < sramAutoSaveIntervalSec_) return;
    }
    lastSramAutoSave_ = now;

    for (const auto& sys : project_->systems()) {
        if (!sys || sys->romPath().empty()) continue;
        rp::autoSaveSramToSibling(*sys, sramSavedHashes_[sys->id()]);
    }
}

bool PluginRpcService::hasUnsavedChanges() {
    return projectDirty_ || sramDirtyCount() > 0;
}

std::uint32_t PluginRpcService::sramDirtyCount() {
    if (!project_) return 0;
    std::uint32_t count = 0;
    for (const auto& sys : project_->systems()) {
        if (!sys || sys->romPath().empty()) continue;
        const auto bytes = rp::sram_autosave::readSram(*sys);
        if (bytes.empty()) continue;                           // no battery
        const std::uint64_t cur =
            rp::lsdj::SampleCache::hashBytes(bytes.data(), bytes.size());
        auto it = sramLoadBaseline_.find(sys->id());
        if (it == sramLoadBaseline_.end()) {
            // Not seeded yet (just appeared): adopt as baseline, no evidence of change.
            sramLoadBaseline_[sys->id()] = cur;
            continue;
        }
        if (cur == it->second) continue;                       // unchanged since load
        // Changed since load — unsaved unless the sibling already holds it.
        const std::string sav = rp::sram_autosave::resolveSavPath(*sys);
        std::error_code ec;
        if (std::filesystem::exists(sav, ec) &&
            rp::sram_autosave::hashFile(sav) == cur) continue; // already persisted
        ++count;
    }
    return count;
}

PluginRpcService::UnsavedSummary PluginRpcService::getUnsavedSummary() {
    UnsavedSummary s;
    s.project     = projectDirty_;
    s.sramSystems = sramDirtyCount();
    return s;
}

bool PluginRpcService::saveDirtySram() {
    if (!project_) return false;
    bool ok = true;
    for (const auto& sys : project_->systems()) {
        if (!sys || sys->romPath().empty()) continue;
        const auto bytes = rp::sram_autosave::readSram(*sys);
        if (bytes.empty()) continue;
        const std::uint64_t cur =
            rp::lsdj::SampleCache::hashBytes(bytes.data(), bytes.size());
        const std::string sav = rp::sram_autosave::resolveSavPath(*sys);
        std::error_code ec;
        if (std::filesystem::exists(sav, ec) &&
            rp::sram_autosave::hashFile(sav) == cur) continue;  // already saved
        if (saveSramToPath(sys->id(), sav)) sramSavedHashes_[sys->id()] = cur;
        else ok = false;
    }
    return ok;
}

bool PluginRpcService::quitStandalone() {
    if (!quit_) return false;
    quit_();
    return true;
}

bool PluginRpcService::newProject() {
    if (!commands_) return false;
    // Empty default config: zero systems, default settings. The DSP's
    // LoadProject handler tears down the current systems via loadFromConfig and
    // adopts these defaults, then pushes ConfigChanged so the UI drops back to
    // the start screen.
    auto* heap = new ProjectConfig();
    if (!commands_->tryPush(Command::makeLoadProject(heap))) {
        std::fprintf(stderr, "newProject: command queue full\n");
        delete heap;
        return false;
    }
    // A fresh project has no path and nothing unsaved; the new (zero) systems
    // re-seed their own SRAM baselines on next sight.
    currentProjectPath_.clear();
    projectDirty_ = false;
    sramLoadBaseline_.clear();
    sramSavedHashes_.clear();
    return true;
}

bool PluginRpcService::setLsdjSyncConfig(std::uint32_t id,
                                         std::uint32_t mode,
                                         std::uint32_t divisor) {
    if (!commands_) return false;
    // Same lockstep with LsdjSyncMode value range (Off..ArduinoboyMaster).
    if (mode > static_cast<std::uint32_t>(LsdjSyncMode::ArduinoboyMaster))
        return false;
    if (divisor < 1u || divisor > 8u) return false;
    markProjectDirty();
    return commands_->tryPush(
        Command::makeSetLsdjSyncConfig(static_cast<SystemId>(id),
                                       mode,
                                       static_cast<std::uint8_t>(divisor)));
}

bool PluginRpcService::setWindowSize(std::uint32_t w, std::uint32_t h) {
    if (!setWindowSize_) return false;
    if (w == 0u || h == 0u) return false;
    setWindowSize_(static_cast<unsigned>(w), static_cast<unsigned>(h));
    return true;
}

bool PluginRpcService::isWindowSizeControlled() {
    if (!isWindowSizeControlled_) return false;
    return isWindowSizeControlled_();
}

// ----- LSDJ kit patching ----------------------------------------------------

namespace {

const rp::lsdj::LsdjKitPatchConfig*
findKitConfig(const SameBoySystem& sb) {
    for (const auto& rc : sb.config_.roles) {
        if (const auto* k = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant()))
            return k;
    }
    return nullptr;
}

} // namespace

PluginRpcService::KitsResponse
PluginRpcService::getKitsConfig(std::uint32_t systemId) {
    KitsResponse out;
    if (!project_) return out;
    auto* sys = project_->findSystem(static_cast<SystemId>(systemId));
    auto* sb  = dynamic_cast<const SameBoySystem*>(sys);
    if (!sb) return out;
    const auto* cfg = findKitConfig(*sb);
    if (!cfg) return out;

    out.kits.reserve(cfg->kits.size());
    for (const auto& k : cfg->kits) {
        KitEntry entry;
        entry.slot         = k.slot;
        entry.name         = k.name;
        entry.compiledHash = k.compiledHash;
        entry.compiledSize = k.compiledBytes.size();
        entry.samples.reserve(k.samples.size());
        for (const auto& s : k.samples) {
            KitSampleEntry e;
            e.path       = s.path;
            e.name       = s.name;
            e.pitch      = s.pitch;
            e.volume     = s.volume;
            e.sourceHash = s.sourceHash;
            e.offset     = s.offset;
            e.length     = s.length;
            e.effects    = s.effects;
            entry.samples.push_back(std::move(e));
        }
        out.kits.push_back(std::move(entry));
    }
    return out;
}

PluginRpcService::CompileKitResult
PluginRpcService::compileAndPatchKit(std::uint32_t systemId,
                                     std::uint8_t  kitIndex,
                                     std::string   kitName,
                                     std::vector<KitSampleSpec> samples) {
    CompileKitResult result;
    if (!project_ || !commands_) {
        result.error = "service not wired up";
        return result;
    }
    if (kitIndex >= LsdjKitPatchRole::kSlotCount) {
        result.error = "kitIndex out of range (0..15)";
        return result;
    }
    auto* sys = project_->findSystem(static_cast<SystemId>(systemId));
    auto* sb  = dynamic_cast<SameBoySystem*>(sys);
    if (!sb) {
        result.error = "system not found / not SameBoy";
        return result;
    }
    if (!findKitConfig(*sb)) {
        result.error = "system has no lsdj-kit-patch role";
        return result;
    }

    // Translate the rpc spec into the compile-pipeline's input type. The
    // two are deliberately structurally identical — they differ only in
    // how optional fields are surfaced (rpcpp uses std::optional; the
    // compiler uses defaulted plain values).
    // Copy (don't move) the per-sample fields: they're stored on the project
    // config below so the kit can be recompiled from source on the next load.
    std::vector<rp::lsdj::CompileSampleSpec> compileSpecs;
    compileSpecs.reserve(samples.size());
    for (const auto& s : samples) {
        rp::lsdj::CompileSampleSpec c;
        c.path    = s.path;
        c.name    = s.name;
        c.offset  = s.offset.value_or(0);
        c.length  = s.length.value_or(0);
        c.effects = s.effects;
        compileSpecs.push_back(std::move(c));
    }

    if (!kitCompiler_) {
        kitCompiler_ = std::make_unique<rp::lsdj::KitCompiler>();
    }

    auto compiled = kitCompiler_->compileKit(kitName, compileSpecs);
    if (!compiled.ok || compiled.bytes.size() != rp::lsdj::Kit::kSize) {
        result.error = compiled.error.empty() ? "kit compile failed" : compiled.error;
        return result;
    }

    // Stash per-sample metadata on the project config now so it survives
    // a save before the DSP processes the patch command. (The DSP writes
    // the *bytes* there too; this UI write doesn't race because both
    // happen on different fields and Project mutations are serialised
    // by the DSP's command drain — but rfl's vector ops aren't atomic,
    // so keep the writes confined to UI here.)
    for (auto& rc : sb->config_.roles) {
        auto* cfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
        if (!cfg) continue;
        rp::lsdj::LsdjKitConfig* slot = nullptr;
        for (auto& k : cfg->kits) {
            if (k.slot == kitIndex) { slot = &k; break; }
        }
        if (!slot) {
            rp::lsdj::LsdjKitConfig fresh;
            fresh.slot = kitIndex;
            cfg->kits.push_back(std::move(fresh));
            slot = &cfg->kits.back();
        }
        slot->name = kitName;
        slot->samples.clear();
        slot->samples.reserve(samples.size());
        for (const auto& s : samples) {
            rp::lsdj::LsdjSampleConfig out;
            out.path    = s.path;
            out.name    = s.name;
            out.pitch   = s.pitch.value_or(0x7F);
            out.volume  = s.volume.value_or(0xFF);
            out.offset  = s.offset.value_or(0);
            out.length  = s.length.value_or(0);
            out.effects = s.effects;
            slot->samples.push_back(std::move(out));
        }
        break;
    }

    // Heap-allocate the bytes for transfer to DSP. The DSP-side handler
    // takes ownership (via std::unique_ptr) and frees after applying.
    auto* heapBytes = new std::vector<std::uint8_t>(std::move(compiled.bytes));
    if (!commands_->tryPush(Command::makePatchKit(static_cast<SystemId>(systemId),
                                                   kitIndex, heapBytes))) {
        delete heapBytes;
        result.error = "command queue full";
        return result;
    }

    markProjectDirty();
    result.ok           = true;
    result.compiledHash = compiled.hash;
    // Send a copy back so the UI can hash for dirty tracking + preview the
    // freshly-patched bytes. Bytestring is std::byte; reinterpret cast is
    // safe (same width, both POD).
    result.compiledBytes.resize(heapBytes->size());
    std::memcpy(result.compiledBytes.data(), heapBytes->data(), heapBytes->size());
    return result;
}

PluginRpcService::AuditionResponse
PluginRpcService::auditionSample(std::string path) {
    AuditionResponse out;
    if (!kitCompiler_) {
        kitCompiler_ = std::make_unique<rp::lsdj::KitCompiler>();
    }
    const auto* data = kitCompiler_->cache().getOrLoad(path);
    if (!data || data->buffer.empty()) {
        return out;
    }
    out.ok         = true;
    out.sampleRate = data->sampleRate;
    out.pcmF32.resize(data->buffer.size() * sizeof(float));
    std::memcpy(out.pcmF32.data(),
                data->buffer.data(),
                data->buffer.size() * sizeof(float));
    return out;
}

bool PluginRpcService::openSampleBrowser() {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::LoadSample;
    openFileBrowser_("Load sample (WAV / MP3 / FLAC)", false, nullptr, kAudioPatterns, kAudioFilterName);
    return true;
}

bool PluginRpcService::eraseKit(std::uint32_t systemId, std::uint8_t kitIndex) {
    if (!commands_ || !project_) return false;
    if (kitIndex >= LsdjKitPatchRole::kSlotCount) return false;

    // Erase semantics: patch the slot with a freshly-zeroed kit bank. The
    // role applies it the same way as any other patch, and the kit slot
    // ends up looking "empty" in LSDJ (offset table cleared, no samples).
    auto* heapBytes = new std::vector<std::uint8_t>(rp::lsdj::Kit::kSize, 0);
    if (!commands_->tryPush(Command::makePatchKit(static_cast<SystemId>(systemId),
                                                   kitIndex, heapBytes))) {
        delete heapBytes;
        return false;
    }
    markProjectDirty();
    return true;
}

UserConfigDto PluginRpcService::getUserConfig() {
    if (!userConfig_) {
        // No watcher attached (LV2-UI, rpc-schema-dump). Hand back the
        // hardcoded defaults so the JS side still has a working binding
        // map to install.
        UserConfigDto out;
        out.activeKeyboardBindings = "default";
        out.activeGamepadBindings  = "default";
        out.bindings               = defaultBindingMap();
        return out;
    }
    return userConfig_->snapshot();
}

bool PluginRpcService::setActiveKeyboardBindings(std::string name) {
    if (!userConfig_) return false;
    return userConfig_->setActiveKeyboardBindings(std::move(name));
}

bool PluginRpcService::setActiveGamepadBindings(std::string name) {
    if (!userConfig_) return false;
    return userConfig_->setActiveGamepadBindings(std::move(name));
}

std::optional<BindingMapJson> PluginRpcService::getBindingProfile(std::string name) {
    if (!userConfig_) return std::nullopt;
    return userConfig_->loadProfile(name);
}

bool PluginRpcService::saveBindingProfile(std::string name, BindingMapJson bindings) {
    if (!userConfig_) return false;
    return userConfig_->saveProfile(std::move(name), std::move(bindings));
}

bool PluginRpcService::renameBindingProfile(std::string oldName, std::string newName) {
    if (!userConfig_) return false;
    return userConfig_->renameProfile(std::move(oldName), std::move(newName));
}

bool PluginRpcService::deleteBindingProfile(std::string name) {
    if (!userConfig_) return false;
    return userConfig_->deleteProfile(std::move(name));
}

namespace {
std::string defaultSavePath(const SystemBase& sys, const char* ext) {
    // Honour the instance's loose-battery suffix so Save SRAM / Save State for a
    // duplicated system targets `<rom>-N.<ext>` rather than the shared sibling.
    return rp::sram_autosave::siblingPath(sys.romPath(), sys.savSuffix(), ext);
}

bool spillBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!bytes.empty())
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    return out.good();
}
} // namespace

bool PluginRpcService::sliceFromStateSnapshot(SystemBase* sys, rp::MemoryType type,
                                              std::vector<std::uint8_t>& out) {
    if (!sys) return false;
    const auto& region = sys->stateRegions()[static_cast<std::size_t>(type)];
    if (region.size == 0) return false;            // region not present (non-GB / no battery)
    std::vector<std::uint8_t> state;
    if (!sys->readStateSnapshot(state)) return false;
    if (static_cast<std::size_t>(region.offset) + region.size > state.size()) return false;
    out.assign(state.begin() + region.offset,
               state.begin() + region.offset + region.size);
    return true;
}

bool PluginRpcService::saveSramToPath(std::uint32_t systemId, const std::string& path) {
    if (!project_ || path.empty()) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    // Prefer slicing SRAM out of the DSP-published state snapshot (race-free);
    // fall back to a direct read when no snapshot exists yet.
    std::vector<std::uint8_t> bytes;
    if (!sliceFromStateSnapshot(sys, rp::MemoryType::Sram, bytes))
        bytes = sys->saveSramBytes();
    if (bytes.empty()) {
        emit("sram-error", path);
        return false;
    }
    if (!spillBytes(path, bytes)) {
        emit("sram-error", path);
        return false;
    }
    // Keep the auto-save dedup baseline in sync when writing the sibling, so a
    // manual Save SRAM doesn't immediately trigger a redundant auto-save write.
    if (path == rp::sram_autosave::resolveSavPath(*sys))
        sramSavedHashes_[static_cast<SystemId>(systemId)] =
            rp::lsdj::SampleCache::hashBytes(bytes.data(), bytes.size());
    emit("sram-saved", path);
    return true;
}

bool PluginRpcService::loadSramFromPath(std::uint32_t systemId, const std::string& path) {
    if (!commands_ || path.empty()) return false;
    auto bytes = slurp(path);
    if (bytes.empty()) {
        emit("sram-error", path);
        return false;
    }
    // The DSP thread owns the live emulator, so the actual battery-RAM load +
    // reset happens there. Hand the bytes over on the heap (ownership transfers
    // to the command consumer, which frees them).
    auto* owned = new std::vector<std::uint8_t>(std::move(bytes));
    if (!commands_->tryPush(Command::makeLoadSram(static_cast<SystemId>(systemId), owned))) {
        delete owned;
        emit("sram-error", path);
        return false;
    }
    emit("sram-loaded", path);
    return true;
}

bool PluginRpcService::saveStateToPath(std::uint32_t systemId, const std::string& path) {
    if (!project_ || path.empty()) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    // Prefer the DSP-published state snapshot (race-free). Fall back to a
    // direct read only when no snapshot exists (non-plugin / single-threaded
    // contexts, or the brief cold-start window before the first publish).
    std::vector<std::uint8_t> bytes;
    if (!sys->readStateSnapshot(bytes)) bytes = sys->saveStateBytes();
    if (bytes.empty()) {
        emit("state-error", path);
        return false;
    }
    if (!spillBytes(path, bytes)) {
        emit("state-error", path);
        return false;
    }
    emit("state-saved", path);
    return true;
}

bool PluginRpcService::loadStateFromPath(std::uint32_t systemId, const std::string& path) {
    if (!commands_ || path.empty()) return false;
    auto bytes = slurp(path);
    if (bytes.empty()) {
        emit("state-error", path);
        return false;
    }
    // The DSP thread owns the live emulator, so the actual savestate load
    // happens there. Hand the bytes over on the heap (ownership transfers to
    // the command consumer, which frees them).
    auto* owned = new std::vector<std::uint8_t>(std::move(bytes));
    if (!commands_->tryPush(Command::makeLoadState(static_cast<SystemId>(systemId), owned))) {
        delete owned;
        emit("state-error", path);
        return false;
    }
    emit("state-loaded", path);
    return true;
}

bool PluginRpcService::saveSram(std::uint32_t systemId) {
    if (!project_) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    // Battery target honours a user-paired `savPath` override, else the
    // suffix-derived sibling. (Save State keeps using defaultSavePath — the
    // override is sav-only.)
    const std::string path = rp::sram_autosave::resolveSavPath(*sys);
    if (path.empty()) {
        // No romPath — fall back to the file dialog so the user picks a target.
        return openSaveSramBrowser(systemId);
    }
    return saveSramToPath(systemId, path);
}

bool PluginRpcService::openSaveSramBrowser(std::uint32_t systemId) {
    if (!openFileBrowser_) return false;
    pendingFileMode_     = PendingFileMode::SaveSram;
    pendingFileSystemId_ = systemId;
    std::string defaultName = "sram.sav";
    if (SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId))) {
        if (!sys->romPath().empty()) {
            auto name = std::filesystem::path(sys->romPath()).filename();
            name.replace_extension(".sav");
            defaultName = name.string();
        }
    }
    openFileBrowser_("Save SRAM", true, defaultName.c_str(), kSramPatterns, kSramFilterName);
    return true;
}

bool PluginRpcService::openLoadSramBrowser(std::uint32_t systemId) {
    if (!openFileBrowser_) return false;
    pendingFileMode_     = PendingFileMode::LoadSram;
    pendingFileSystemId_ = systemId;
    openFileBrowser_("Load SRAM", false, nullptr, kSramPatterns, kSramFilterName);
    return true;
}

bool PluginRpcService::saveState(std::uint32_t systemId) {
    if (!project_) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    const std::string path = defaultSavePath(*sys, ".ss0");
    if (path.empty()) {
        return openSaveStateBrowser(systemId);
    }
    return saveStateToPath(systemId, path);
}

bool PluginRpcService::openSaveStateBrowser(std::uint32_t systemId) {
    if (!openFileBrowser_) return false;
    pendingFileMode_     = PendingFileMode::SaveState;
    pendingFileSystemId_ = systemId;
    std::string defaultName = "savestate.ss0";
    if (SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId))) {
        if (!sys->romPath().empty()) {
            auto name = std::filesystem::path(sys->romPath()).filename();
            name.replace_extension(".ss0");
            defaultName = name.string();
        }
    }
    openFileBrowser_("Save State", true, defaultName.c_str(), kStatePatterns, kStateFilterName);
    return true;
}

bool PluginRpcService::openLoadStateBrowser(std::uint32_t systemId) {
    if (!openFileBrowser_) return false;
    pendingFileMode_     = PendingFileMode::LoadState;
    pendingFileSystemId_ = systemId;
    openFileBrowser_("Load State", false, nullptr, kStatePatterns, kStateFilterName);
    return true;
}

bool PluginRpcService::openSettingsFolder() {
    if (!userConfig_) return false;
    const std::filesystem::path root = userConfig_->rootDir();
    if (root.empty()) return false;
#if defined(__APPLE__)
    const std::string cmd = "open " + std::string("\"") + root.string() + "\" &";
#elif defined(_WIN32)
    const std::string cmd = "start \"\" \"" + root.string() + "\"";
#else
    const std::string cmd = "xdg-open \"" + root.string() + "\" &";
#endif
    return std::system(cmd.c_str()) == 0;
}

std::vector<PluginRpcService::RecentFileDto> PluginRpcService::getRecentFiles() {
    std::vector<RecentFileDto> out;
    if (!recentFiles_) return out;
    auto snap = recentFiles_->snapshot();
    out.reserve(snap.size());
    for (auto& e : snap) {
        // Existence is a view concern, recomputed each fetch (the UI re-queries
        // on "recent-files-changed") — kept out of RecentFiles to keep it pure.
        std::error_code ec;
        const bool missing =
            !std::filesystem::exists(std::filesystem::path(e.path), ec) || ec;
        out.push_back(RecentFileDto{std::move(e.path), std::move(e.name), missing});
    }
    return out;
}

bool PluginRpcService::removeRecentFile(std::string path) {
    if (!recentFiles_) return false;
    return recentFiles_->remove(path);   // fires onChange -> "recent-files-changed"
}

bool PluginRpcService::renameRecentFile(std::string path, std::string newName) {
    if (!recentFiles_) return false;
    return recentFiles_->rename(path, newName);   // display alias only; file untouched
}

bool PluginRpcService::openRecentRelinkBrowser(std::string path) {
    if (!openFileBrowser_ || path.empty()) return false;
    pendingFileMode_         = PendingFileMode::RelinkRecent;
    pendingRelinkRecentPath_ = std::move(path);
    openFileBrowser_("Locate project (.rplg)", /*saving=*/false, nullptr, kProjPatterns, kProjFilterName);
    return true;
}

// ----- Memory snapshot API --------------------------------------------------

std::optional<PluginRpcService::MemorySnapshotResponse>
PluginRpcService::getMemory(std::uint32_t systemId,
                            std::uint32_t type,
                            std::uint32_t offset,
                            std::uint32_t length) {
    if (!project_) return std::nullopt;
    if (type >= rp::kMemoryTypeCount) return std::nullopt;

    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return std::nullopt;

    // Prefer slicing the region out of the DSP-published state snapshot
    // (race-free, covers GB SRAM/RAM/VRAM). Fall back to a direct read of the
    // live region for regions not in the snapshot or before the first publish.
    // (High-frequency reads should use subscribeMemory, which is already safe.)
    std::vector<std::uint8_t> region;
    std::size_t regionSize = 0;
    const std::uint8_t* data = nullptr;
    rp::MemoryAccessor accessor;
    if (sliceFromStateSnapshot(sys, static_cast<rp::MemoryType>(type), region)) {
        regionSize = region.size();
        data       = region.data();
    } else {
        accessor = sys->getMemory(static_cast<rp::MemoryType>(type), rp::AccessType::Read);
        if (!accessor.valid()) return std::nullopt;
        regionSize = accessor.size();
        data       = accessor.data();
    }

    if (offset > regionSize) return std::nullopt;
    const std::size_t available = regionSize - offset;
    const std::size_t count = (length == 0 || length > available) ? available : length;

    MemorySnapshotResponse out;
    out.regionSize = static_cast<std::uint32_t>(regionSize);
    out.bytes.resize(count);
    if (count > 0) {
        std::memcpy(out.bytes.data(), data + offset, count);
        out.hash = rp::hash::fnv1a64(data + offset, count);
    }
    return out;
}

bool PluginRpcService::subscribeMemory(std::uint32_t systemId,
                                       std::uint32_t type,
                                       std::uint32_t hz) {
    if (!project_ || !commands_) return false;
    if (type >= rp::kMemoryTypeCount) return false;

    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;

    const auto memType = static_cast<rp::MemoryType>(type);

    // Reject unsupported types and oversized regions up front so the JS
    // caller gets a synchronous false instead of a silent dropped sub.
    rp::MemoryAccessor probe = sys->getMemory(memType, rp::AccessType::Read);
    if (!probe.valid()) return false;
    if (probe.size() > SystemBase::kMaxStreamableBytes) return false;

    MemorySubKey key{static_cast<SystemId>(systemId), memType};
    auto it = memorySubs_.find(key);
    if (it != memorySubs_.end()) {
        // Already streaming — re-subscribe just updates the cadence cap.
        it->second.hz = hz;
        return true;
    }

    if (!commands_->tryPush(Command::makeSubscribeMemory(key.systemId, memType)))
        return false;

    MemorySubState state;
    state.hz = hz;
    memorySubs_.emplace(key, state);
    return true;
}

bool PluginRpcService::unsubscribeMemory(std::uint32_t systemId,
                                         std::uint32_t type) {
    if (!commands_) return false;
    if (type >= rp::kMemoryTypeCount) return false;

    const auto memType = static_cast<rp::MemoryType>(type);
    MemorySubKey key{static_cast<SystemId>(systemId), memType};
    auto it = memorySubs_.find(key);
    if (it == memorySubs_.end()) return false;

    memorySubs_.erase(it);
    commands_->tryPush(Command::makeUnsubscribeMemory(key.systemId, memType));
    return true;
}
