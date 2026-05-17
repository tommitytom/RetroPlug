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

#include "lsdj/KitCompiler.hpp"
#include "lsdj/SampleCache.hpp"
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
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
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
                                   std::atomic<SystemId>* focusedSystemId)
    : project_(project),
      commands_(commands),
      events_(events),
      sampleRate_(sampleRate),
      focusedSystemId_(focusedSystemId) {}

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
        case PendingFileMode::AddRom:      addRomFromPath(path);             break;
        case PendingFileMode::LoadProject: loadProjectFromPath(path);        break;
        case PendingFileMode::SaveProject: saveProjectToPath(path);          break;
        case PendingFileMode::LoadSample:  emit("sample-path-selected", path); break;
        case PendingFileMode::LoadRom:
        default:                           loadRomFromPath(path);            break;
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
                } else if (rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
                    entry.hasLsdjKitRole = true;
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
