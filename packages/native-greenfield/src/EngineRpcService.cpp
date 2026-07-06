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

// Map the wire construct spec to the backend-agnostic build spec: resolve the SRAM/savestate seeds
// (zip-import bytes win, else the on-disk file, else empty). The opaque `settings` blob stays empty
// for now — cores construct bare (feature roles are TS kernel behaviours, not native config).
SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
    SystemBuildSpec out;
    out.backendKind = "sameboy";  // greenfield host is SameBoy-only for now
    out.romPath = spec.romPath;
    out.embeddedRom = spec.embeddedRom;
    if (spec.sramBytes) out.sram = *spec.sramBytes;
    else if (spec.savPath) out.sram = slurpAll(*spec.savPath);
    if (spec.stateBytes) out.savestate = *spec.stateBytes;
    else if (spec.statePath) out.savestate = slurpAll(*spec.statePath);
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

bool EngineRpcService::applySystemSetting(std::uint32_t id, std::string /*key*/, double /*value*/) {
    return engine_.findSystem(id) != nullptr;
}

bool EngineRpcService::applyRoleConfig(std::uint32_t id, std::string /*kind*/, std::string /*config*/) {
    return engine_.findSystem(id) != nullptr;
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
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // direct core mutation; quiescent only
    return engine_.pressButton(id, static_cast<std::uint8_t>(button), down);
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
