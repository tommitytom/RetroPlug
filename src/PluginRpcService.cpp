#include "PluginRpcService.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "config/RecentFiles.hpp"
#include "config/UserConfig.hpp"
#include "lsdj/KitCompiler.hpp"
#include "lsdj/SampleCache.hpp"
#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/InputTypes.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/GbaConfig.hpp"
#include "system/mesen/GbaSystem.hpp"
#include "system/mesen/MesenConfig.hpp"
#include "system/mesen/MesenSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/Hash.hpp"

namespace {

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

SystemBase* PluginRpcService::buildSystemFromPath(const std::string& path) {
    if (!project_ || !sampleRate_) {
        std::fprintf(stderr, "buildSystemFromPath: shared DSP state unavailable (LV2-UI?)\n");
        return nullptr;
    }
    std::vector<std::uint8_t> bytes = slurp(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "buildSystemFromPath: failed to read '%s'\n", path.c_str());
        emit("rom-error", path);
        return nullptr;
    }

    // Content-based dispatch: iNES magic → MesenSystem, GBA Nintendo logo
    // at $0004 → GbaSystem, Game Boy Nintendo logo at $0104 → SameBoySystem,
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

    const SystemId id = project_->nextSystemId();
    const double sr = sampleRate_->load(std::memory_order_acquire);

    if (fmt == RomFormat::Mesen) {
        MesenConfig cfg;
        cfg.romPath = path;
        auto sys = std::make_unique<MesenSystem>(id, cfg, std::move(bytes));
        sys->onActivate(sr);
        return sys.release();
    }

    if (fmt == RomFormat::Gba) {
        GbaSystemConfig cfg;
        cfg.romPath = path;
        cfg.biosPath = "build/firmware/gba_bios.bin";
        auto sys = std::make_unique<GbaSystem>(id, cfg, std::move(bytes));
        sys->onActivate(sr);
        return sys.release();
    }

    SameBoyConfig cfg;
    cfg.romPath  = path;
    cfg.model    = SameBoyModel::CgbC;
    cfg.fastBoot = true;

    // Optional sibling .sav (cartridge battery RAM). Missing file is fine.
    {
        std::filesystem::path sav = std::filesystem::path(path);
        sav.replace_extension(".sav");
        std::vector<std::uint8_t> sramBytes = slurp(sav.string());
        if (!sramBytes.empty())
            cfg.sram = std::move(sramBytes);
    }

    auto sys = std::make_unique<SameBoySystem>(id, cfg, std::move(bytes));
    sys->onActivate(sr);
    return sys.release();
}

bool PluginRpcService::loadRomFromPath(std::string path) {
    if (!commands_) {
        emit("rom-error", path);
        return false;
    }
    SystemBase* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeLoadRom(sys))) {
        std::fprintf(stderr, "loadRomFromPath: command queue full\n");
        delete sys;
        emit("rom-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path, "rom");
    emit("rom-loaded", path);
    return true;
}

bool PluginRpcService::addRomFromPath(std::string path) {
    if (!commands_) {
        emit("rom-error", path);
        return false;
    }
    SystemBase* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeAddSystem(sys))) {
        std::fprintf(stderr, "addRomFromPath: command queue full\n");
        delete sys;
        emit("rom-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path, "rom");
    emit("rom-loaded", path);
    return true;
}

bool PluginRpcService::replaceRomFromPath(std::uint32_t id, std::string path) {
    if (!commands_) {
        emit("rom-error", path);
        return false;
    }
    SystemBase* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeReplaceSystem(static_cast<SystemId>(id), sys))) {
        std::fprintf(stderr, "replaceRomFromPath: command queue full\n");
        delete sys;
        emit("rom-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path, "rom");
    emit("rom-loaded", path);
    return true;
}

bool PluginRpcService::saveProjectToPath(const std::string& path) {
    if (!project_) {
        emit("project-error", path);
        return false;
    }
    std::vector<std::uint8_t> zip;
    try {
        zip = projectConfigToZip(project_->snapshotConfig());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "saveProjectToPath: serialize failed: %s\n", e.what());
        emit("project-error", path);
        return false;
    }
    if (zip.empty()) {
        std::fprintf(stderr, "saveProjectToPath: zip serialization produced empty buffer\n");
        emit("project-error", path);
        return false;
    }
    if (!spillBytes(path, zip)) {
        std::fprintf(stderr, "saveProjectToPath: write failed for '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path, "project");
    currentProjectPath_ = path;
    emit("project-saved", path);
    return true;
}

bool PluginRpcService::loadProjectFromPath(const std::string& path) {
    if (!commands_) {
        emit("project-error", path);
        return false;
    }
    const auto bytes = slurp(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "loadProjectFromPath: empty / unreadable '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    auto parsed = projectConfigFromZip(bytes);
    if (!parsed) {
        std::fprintf(stderr, "loadProjectFromPath: failed to parse zip '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    // Heap-allocate the parsed config; DSP frees after applying.
    auto* heap = new ProjectConfig(std::move(*parsed));
    if (!commands_->tryPush(Command::makeLoadProject(heap))) {
        std::fprintf(stderr, "loadProjectFromPath: command queue full\n");
        delete heap;
        emit("project-error", path);
        return false;
    }
    if (recentFiles_) recentFiles_->add(path, "project");
    currentProjectPath_ = path;
    emit("project-loaded", path);
    return true;
}

void PluginRpcService::onFileBrowserSelected(const char* path) {
    if (!path || !*path) {
        pendingFileMode_ = PendingFileMode::LoadRom;
        pendingFileSystemId_ = 0;
        return;
    }
    switch (pendingFileMode_) {
        case PendingFileMode::AddRom:      addRomFromPath(path);             break;
        case PendingFileMode::LoadProject: loadProjectFromPath(path);        break;
        case PendingFileMode::SaveProject: saveProjectToPath(path);          break;
        case PendingFileMode::LoadSample:  emit("sample-path-selected", path); break;
        case PendingFileMode::SaveSram:    saveSramToPath(pendingFileSystemId_, path);  break;
        case PendingFileMode::SaveState:   saveStateToPath(pendingFileSystemId_, path); break;
        case PendingFileMode::LoadState:   loadStateFromPath(pendingFileSystemId_, path); break;
        case PendingFileMode::LoadRom:
        default:                           loadRomFromPath(path);            break;
    }
    pendingFileMode_ = PendingFileMode::LoadRom;
    pendingFileSystemId_ = 0;
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
    pendingFileMode_ = (opts.mode && *opts.mode == "add")
        ? PendingFileMode::AddRom
        : PendingFileMode::LoadRom;
    openFileBrowser_("Open ROM (Game Boy or NES)", false, nullptr);
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
    openFileBrowser_("Save RetroPlug project", true, defaultName.c_str());
    return true;
}

bool PluginRpcService::openLoadProjectBrowser() {
    if (!openFileBrowser_) return false;
    pendingFileMode_ = PendingFileMode::LoadProject;
    openFileBrowser_("Load RetroPlug project", false, nullptr);
    return true;
}

bool PluginRpcService::removeSystem(std::uint32_t id) {
    if (!commands_) return false;
    return commands_->tryPush(Command::makeRemoveSystem(static_cast<SystemId>(id)));
}

bool PluginRpcService::duplicateSystem(std::uint32_t id) {
    if (!project_ || !commands_ || !sampleRate_) return false;
    SystemBase* src = project_->findSystem(static_cast<SystemId>(id));
    if (!src) return false;

    const SystemId newId = project_->nextSystemId();
    const double   sr    = sampleRate_->load(std::memory_order_acquire);
    auto clone = src->clone(newId, sr);
    if (!clone) return false;

    SystemBase* released = clone.release();
    if (!commands_->tryPush(Command::makeAddSystem(released))) {
        std::fprintf(stderr, "duplicateSystem: command queue full\n");
        delete released;
        return false;
    }
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

        // Per-kind fields. NES (Mesen) currently surfaces only the kind tag
        // — there's no per-system Mesen config exposed to the UI yet.
        if (auto* sb = dynamic_cast<const SameBoySystem*>(sys.get())) {
            entry.kind        = "sameboy";
            entry.gainDb      = sb->config_.gainDb;
            entry.linkGroupId = sb->config_.linkGroupId;
            entry.model       = static_cast<std::uint32_t>(sb->config_.model);
            for (const auto& rc : sb->config_.roles) {
                if (const auto* lsdj = rfl::get_if<LsdjSyncConfig>(&rc.variant())) {
                    entry.lsdjSyncMode     = static_cast<std::uint32_t>(lsdj->mode);
                    entry.lsdjTempoDivisor = lsdj->tempoDivisor;
                } else if (rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
                    entry.hasLsdjKitRole = true;
                }
            }
        } else if (sys->kind() == SystemKind::Mesen) {
            entry.kind = "mesen";
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
    return commands_->tryPush(
        Command::makeSetMidiRouting(static_cast<MidiRouting>(routing)));
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

bool PluginRpcService::setZoom(std::uint32_t zoom) {
    if (!commands_) return false;
    if (zoom < 1u || zoom > 6u) return false;
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
    return commands_->tryPush(
        Command::makeSetFastBoot(static_cast<SystemId>(id), enabled));
}

bool PluginRpcService::setModel(std::uint32_t id, std::uint32_t model) {
    if (!commands_) return false;
    if (model > static_cast<std::uint32_t>(SameBoyModel::Agb)) return false;
    return commands_->tryPush(
        Command::makeSetModel(static_cast<SystemId>(id),
                              static_cast<SameBoyModel>(model)));
}

bool PluginRpcService::setReloadOnRomChange(std::uint32_t id, bool enabled) {
    if (!commands_) return false;
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

        // Rebuild from disk via the format-agnostic loader.
        auto liveSram = sys->saveSramBytes();
        SystemBase* rebuilt = buildSystemFromPath(romPath);
        if (!rebuilt) {
            it->second.mtime = mtime;
            continue;
        }
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

bool PluginRpcService::setLsdjSyncConfig(std::uint32_t id,
                                         std::uint32_t mode,
                                         std::uint32_t divisor) {
    if (!commands_) return false;
    // Same lockstep with LsdjSyncMode value range (Off..ArduinoboyMaster).
    if (mode > static_cast<std::uint32_t>(LsdjSyncMode::ArduinoboyMaster))
        return false;
    if (divisor < 1u || divisor > 8u) return false;
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
            // Effects don't currently round-trip with the per-sample
            // metadata — they're applied at compile time and not stored
            // on the role config. UI re-edit recompiles whatever the
            // current effect picker shows.
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
    std::vector<rp::lsdj::CompileSampleSpec> compileSpecs;
    compileSpecs.reserve(samples.size());
    for (auto& s : samples) {
        rp::lsdj::CompileSampleSpec c;
        c.path    = std::move(s.path);
        c.name    = std::move(s.name);
        c.offset  = s.offset.value_or(0);
        c.length  = s.length.value_or(0);
        c.effects = std::move(s.effects);
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
            out.path   = s.path;   // moved-from above on the copy used by compileSpecs
            out.name   = s.name;
            out.pitch  = s.pitch.value_or(0x7F);
            out.volume = s.volume.value_or(0xFF);
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
    openFileBrowser_("Load sample (WAV / MP3 / FLAC)", false, nullptr);
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

namespace {
std::string defaultSavePath(const SystemBase& sys, const char* ext) {
    const std::string& romPath = sys.romPath();
    if (romPath.empty()) return {};
    std::filesystem::path p(romPath);
    p.replace_extension(ext);
    return p.string();
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

bool PluginRpcService::saveSramToPath(std::uint32_t systemId, const std::string& path) {
    if (!project_ || path.empty()) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    auto bytes = sys->saveSramBytes();
    if (bytes.empty()) {
        emit("sram-error", path);
        return false;
    }
    if (!spillBytes(path, bytes)) {
        emit("sram-error", path);
        return false;
    }
    emit("sram-saved", path);
    return true;
}

bool PluginRpcService::saveStateToPath(std::uint32_t systemId, const std::string& path) {
    if (!project_ || path.empty()) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    auto bytes = sys->saveStateBytes();
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
    if (!project_ || path.empty()) return false;
    SystemBase* sys = project_->findSystem(static_cast<SystemId>(systemId));
    if (!sys) return false;
    auto bytes = slurp(path);
    if (bytes.empty() || !sys->loadStateBytes(bytes)) {
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
    const std::string path = defaultSavePath(*sys, ".sav");
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
    openFileBrowser_("Save SRAM", true, defaultName.c_str());
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
    openFileBrowser_("Save State", true, defaultName.c_str());
    return true;
}

bool PluginRpcService::openLoadStateBrowser(std::uint32_t systemId) {
    if (!openFileBrowser_) return false;
    pendingFileMode_     = PendingFileMode::LoadState;
    pendingFileSystemId_ = systemId;
    openFileBrowser_("Load State", false, nullptr);
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
        out.push_back(RecentFileDto{std::move(e.path), std::move(e.kind)});
    }
    return out;
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

    rp::MemoryAccessor accessor = sys->getMemory(static_cast<rp::MemoryType>(type),
                                                 rp::AccessType::Read);
    if (!accessor.valid()) return std::nullopt;

    if (offset > accessor.size()) return std::nullopt;
    const std::size_t available = accessor.size() - offset;
    const std::size_t count = (length == 0 || length > available) ? available : length;

    MemorySnapshotResponse out;
    out.regionSize = static_cast<std::uint32_t>(accessor.size());
    out.bytes.resize(count);
    if (count > 0) {
        std::memcpy(out.bytes.data(), accessor.data() + offset, count);
        out.hash = rp::hash::fnv1a64(accessor.data() + offset, count);
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
