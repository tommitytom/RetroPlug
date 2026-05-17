#include "PluginRpcService.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "config/UserConfig.hpp"
#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/InputTypes.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/GbaConfig.hpp"
#include "system/mesen/GbaSystem.hpp"
#include "system/mesen/MesenConfig.hpp"
#include "system/mesen/MesenSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/FrameBufferTriple.hpp"

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

} // namespace

PluginRpcService::PluginRpcService(Project* project,
                                   CommandQueue* commands,
                                   EventQueue* events,
                                   std::atomic<double>* sampleRate,
                                   std::atomic<SystemId>* focusedSystemId,
                                   UserConfig* userConfig)
    : project_(project),
      commands_(commands),
      events_(events),
      sampleRate_(sampleRate),
      focusedSystemId_(focusedSystemId),
      userConfig_(userConfig) {}

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
            cfg.sram = Base64Bytes(std::move(sramBytes));
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
    emit("rom-loaded", path);
    return true;
}

bool PluginRpcService::saveProjectToPath(const std::string& path) {
    if (!project_) {
        emit("project-error", path);
        return false;
    }
    std::string json;
    try {
        json = projectConfigToJson(project_->snapshotConfig());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "saveProjectToPath: serialize failed: %s\n", e.what());
        emit("project-error", path);
        return false;
    }
    if (!spillString(path, json)) {
        std::fprintf(stderr, "saveProjectToPath: write failed for '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    emit("project-saved", path);
    return true;
}

bool PluginRpcService::loadProjectFromPath(const std::string& path) {
    if (!commands_) {
        emit("project-error", path);
        return false;
    }
    std::string json = slurpString(path);
    if (json.empty()) {
        std::fprintf(stderr, "loadProjectFromPath: empty / unreadable '%s'\n", path.c_str());
        emit("project-error", path);
        return false;
    }
    // Heap-allocate the JSON; DSP frees after parsing.
    auto* heap = new std::string(std::move(json));
    if (!commands_->tryPush(Command::makeLoadProject(heap))) {
        std::fprintf(stderr, "loadProjectFromPath: command queue full\n");
        delete heap;
        emit("project-error", path);
        return false;
    }
    emit("project-loaded", path);
    return true;
}

void PluginRpcService::onFileBrowserSelected(const char* path) {
    if (!path || !*path) return;
    switch (pendingFileMode_) {
        case PendingFileMode::AddRom:      addRomFromPath(path);      break;
        case PendingFileMode::LoadProject: loadProjectFromPath(path); break;
        case PendingFileMode::SaveProject: saveProjectToPath(path);   break;
        case PendingFileMode::LoadRom:
        default:                           loadRomFromPath(path);     break;
    }
    pendingFileMode_ = PendingFileMode::LoadRom;
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
    openFileBrowser_("Save RetroPlug project", true, "project.rplg");
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
            for (const auto& rc : sb->config_.roles) {
                if (const auto* lsdj = rfl::get_if<LsdjSyncConfig>(&rc.variant())) {
                    entry.lsdjSyncMode     = static_cast<std::uint32_t>(lsdj->mode);
                    entry.lsdjTempoDivisor = lsdj->tempoDivisor;
                    break;
                }
            }
        } else if (sys->kind() == SystemKind::Mesen) {
            entry.kind = "mesen";
        } else {
            entry.kind = "gba";
        }
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

UserConfigDto PluginRpcService::getUserConfig() {
    if (!userConfig_) {
        // No watcher attached (LV2-UI, rpc-schema-dump). Hand back the
        // hardcoded defaults so the JS side still has a working binding
        // map to install.
        UserConfigDto out;
        out.activeBindings = "default";
        out.bindings       = defaultBindingMap();
        return out;
    }
    return userConfig_->snapshot();
}

bool PluginRpcService::setActiveBindings(std::string name) {
    if (!userConfig_) return false;
    return userConfig_->setActiveBindings(std::move(name));
}
