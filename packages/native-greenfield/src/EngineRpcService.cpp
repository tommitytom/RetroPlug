#include "EngineRpcService.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>

#include "Engine.hpp"
#include "EngineInvoker.hpp"
#include "SameBoyBackend.hpp"
#include "ScriptCompiler.hpp"
#include "SystemFactory.hpp"

#include "EmbeddedRoms.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

rfl::Bytestring toBytestring(const std::vector<std::uint8_t>& v) {
    const auto* p = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(p, p + v.size());
}

// Whole file into a byte vector (empty if unreadable). Used for a seed .sav/state + the reload ROM.
std::vector<std::uint8_t> slurpAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The default core for a platform — the fallback when the wire spec omits `core`. TS always sends
// both (derived via defaultCoreFor), so this only backstops a caller that sends platform alone.
std::string defaultCoreFor(const std::string& platform) {
    if (platform == "nes" || platform == "gba") return "mesen";
    return "sameboy";  // "gb" (and unknown/absent) → SameBoy
}

// Map the wire construct spec to the backend-agnostic build spec: carry `core` (the factory-registry
// key, defaulted from `platform` if absent) and `platform` (so a multi-platform core picks the right
// system), resolve the SRAM/savestate seeds (zip-import bytes win, else the on-disk file, else empty),
// and carry the opaque `settings` blob (decoded only by the matching backend) through unchanged.
SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
    SystemBuildSpec out;
    out.platform = spec.platform.value_or("");
    out.core = spec.core.value_or(defaultCoreFor(out.platform));
    out.romPath = spec.romPath;
    out.embeddedRom = spec.embeddedRom;
    if (spec.sramBytes) out.sram = *spec.sramBytes;
    else if (spec.savPath) out.sram = slurpAll(*spec.savPath);
    if (spec.stateBytes) out.savestate = *spec.stateBytes;
    else if (spec.statePath) out.savestate = slurpAll(*spec.statePath);
    if (spec.settings) out.settings.assign(spec.settings->begin(), spec.settings->end());
    return out;
}

} // namespace

EngineRpcService::EngineRpcService(Engine& engine, SystemFactory& factory, EngineInvoker*& active,
                                   const std::atomic<bool>& audioRunning)
    : engine_(engine), factory_(factory), active_(active), audioRunning_(audioRunning) {}

std::optional<std::uint32_t> EngineRpcService::constructSystem(BackendConstructSpec spec) {
    // Id is allocated up front (nextSystemId only bumps a control-thread counter) so we return it
    // synchronously whether the invoker applies now or enqueues for the audio thread.
    const SystemId id = engine_.nextSystemId();
    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
    if (!sys) return std::nullopt;  // unknown backend / unreadable or non-SameBoy ROM

    if (spec.replaceId) active_->replaceSystem(*spec.replaceId, std::move(sys));
    else                active_->adoptSystem(std::move(sys));
    return id;
}

std::optional<std::uint32_t> EngineRpcService::duplicateSystem(std::uint32_t srcId,
                                                               std::optional<std::string> savPath) {
    // Reads the SOURCE core's live state (clone) — unsafe while the audio thread owns it. Needs the
    // deferred per-system snapshot triple-buffers; until then, quiescent only.
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* src = engine_.findSystem(srcId);
    if (!src) return std::nullopt;

    // clone() boots an independent copy of the live state (SRAM + savestate).
    const SystemId id = engine_.nextSystemId();
    auto sys = src->clone(id, engine_.sampleRate());
    if (!sys) return std::nullopt;
    if (savPath) sys->setSavPath(*savPath);  // the duplicate auto-saves to its own file
    engine_.adoptSystem(std::move(sys));
    return id;
}

std::optional<std::uint32_t> EngineRpcService::reloadSystem(std::uint32_t id) {
    // Reads the existing core's live SRAM/config — unsafe mid-run (see duplicateSystem).
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    SystemBase* old = engine_.findSystem(id);
    if (!old) return std::nullopt;
    // SameBoy-only: the rebuild below reads SameBoyConfig. NES/GBA reload needs a backend-agnostic
    // rebuild (deferred) — refuse rather than downcast a Mesen core (UB). The store treats null as
    // "not reloaded".
    if (old->kind() != SystemKind::SameBoy) return std::nullopt;

    // Rebuild the ROM from disk (or the embedded marker), carrying the live SRAM forward and
    // dropping the savestate — a genuine reload, swapped in place with a fresh id.
    const std::string romPath = old->romPath();
    SameBoyConfig cfg = static_cast<const SameBoySystem*>(old)->config_;  // carry paths/roles/model
    cfg.sram = old->saveSramBytes();
    cfg.savestate.clear();

    std::vector<std::uint8_t> romBytes;
    if (!cfg.embeddedRom.empty()) {
        const auto rom = rp::embeddedRom(cfg.embeddedRom);
        romBytes.assign(rom.begin(), rom.end());
    } else {
        romBytes = slurpAll(romPath);
    }
    if (romBytes.empty()) return std::nullopt;

    // Quiescent only (guarded above) — swap in place through the active (direct) invoker so the
    // reload shares the one Engine::replaceSystem path.
    const SystemId newId = engine_.nextSystemId();
    auto sys = SameBoyBackend::buildSameBoy(newId, std::move(cfg), std::move(romBytes), engine_.sampleRate());
    active_->replaceSystem(id, std::move(sys));
    return newId;
}

bool EngineRpcService::removeSystem(std::uint32_t id) {
    // No threading branch: quiescent → direct erase; running → enqueue for the audio thread, which
    // hands the core back through the release ring for the control thread to delete (drainReleased).
    active_->removeSystem(id);
    return true;
}

bool EngineRpcService::applySystemSetting(std::uint32_t id, std::string key, double value) {
    // A universal per-system setting → the live core, through `active_` (direct when quiescent,
    // queued when the audio thread runs). The store gates existence on its own list, so accept.
    if (key == "gainDb")
        active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Gain), value);
    else if (key == "reloadOnRomChange")
        active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ReloadOnRomChange), value);
    else
        return false;  // unknown key
    return true;
}

bool EngineRpcService::applyRoleConfig(std::uint32_t id, std::string kind, std::string config) {
    // Only a backend "system" role carries live emulator config; SameBoy is the only one today. The
    // store re-sends the WHOLE role config, so each field applies guarded (Engine no-ops an unchanged
    // one — no spurious model restart when only highpass moved).
    if (kind != "sameboy") return false;
    const SameBoyRoleConfig c = SameBoyBackend::decodeSameBoyRoleConfig(config);
    active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Model), static_cast<double>(c.model));
    active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Highpass), static_cast<double>(c.highpass));
    active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::LinkGroup), static_cast<double>(c.linkGroupId));
    active_->applyConfigField(id, static_cast<std::uint8_t>(ConfigField::FastBoot), c.fastBoot ? 1.0 : 0.0);
    return true;
}

// Core reads below touch live core state, unsafe while the audio thread owns the Engine. Until the
// per-system snapshot triple-buffers land (deferred), they fail safe during a run.
std::optional<rfl::Bytestring> EngineRpcService::readState(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    auto bytes = engine_.readState(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

std::optional<rfl::Bytestring> EngineRpcService::readSram(std::uint32_t id) {
    if (audioRunning_.load(std::memory_order_acquire)) return std::nullopt;
    auto bytes = engine_.readSram(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool EngineRpcService::screenshot(std::uint32_t id, std::string path) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;
    return engine_.screenshot(id, path);
}

// Unlike the reads above, getFrame is NOT audioRunning_-guarded: the UI displays frames WHILE audio
// plays, and the FrameBufferTriple is a concurrent one-writer/one-reader design. (The findSystem walk
// shares the same deferred structural-snapshot concern, but a live editor over a running audio thread
// doesn't exist yet.)
GreenfieldFrame EngineRpcService::getFrame(std::uint32_t id) {
    const EngineFrame f = engine_.getFrame(id);
    GreenfieldFrame out;
    out.width = f.width;
    out.height = f.height;
    out.published = f.published;
    if (f.published) {
        const auto* p = reinterpret_cast<const std::byte*>(f.data.data());
        out.data.assign(p, p + f.data.size());
    }
    return out;
}

std::optional<rfl::Bytestring> EngineRpcService::compileScript(std::string source) {
    auto bytecode = dsp::compileToBytecode(source);
    if (!bytecode) return std::nullopt;
    return toBytestring(*bytecode);
}

bool EngineRpcService::dspLoadKernel(std::vector<std::uint8_t> bytecode) {
    active_->loadKernel(std::move(bytecode));
    return true;  // applied now (direct) or on the audio thread; the DSP stage goes active there
}

bool EngineRpcService::dspSetSystems(std::string json) {
    active_->setSystems(std::move(json));
    return true;
}

bool EngineRpcService::pressButton(std::uint32_t id, std::uint32_t button, bool down) {
    // A joypad transition → the focused core, through `active_` (direct when quiescent, queued onto the
    // audio thread when it runs). SystemBase::pressButton is the audio-thread entry point, so — unlike the
    // old direct-only path that returned false the moment audio started — this reaches a live core. The
    // store owns existence; accept optimistically (the queued apply can't report back).
    active_->pressButton(id, static_cast<std::uint8_t>(button), down);
    return true;
}

rfl::Bytestring EngineRpcService::renderAudio(double ms) {
    if (ms <= 0.0) return {};
    scratchL_.resize(kBlockSize);  // idempotent — no per-call realloc after the first
    scratchR_.resize(kBlockSize);

    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    std::vector<float> out;
    out.reserve(total * 2);
    // The Engine owns transport + the ppq clock and consumes any host MIDI staged via stageMidiIn on
    // the first block it renders.
    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlock(frames, scratchL_.data(), scratchR_.data());
        for (std::uint32_t f = 0; f < frames; ++f) {
            out.push_back(scratchL_[f]);  // interleave L,R,L,R…
            out.push_back(scratchR_[f]);
        }
    }
    const auto* p = reinterpret_cast<const std::byte*>(out.data());
    return rfl::Bytestring(p, p + out.size() * sizeof(float));
}

bool EngineRpcService::setTransport(bool running) {
    active_->setTransport(running);  // transport is a queued op — applied now (direct) or on the audio thread
    return true;
}

bool EngineRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) return false;
    active_->setBpm(bpm);
    return true;
}

bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 4) return false;  // a MIDI message fits in 4 bytes
    active_->stageMidi(std::move(bytes));
    return true;
}
