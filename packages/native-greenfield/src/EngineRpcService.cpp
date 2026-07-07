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

bool EngineRpcService::constructSystem(BackendConstructSpec spec) {
    // TS owns the id counter and passes it in; the build (control thread) can fail (bad ROM), the
    // queued adopt can't — so this returns "did it build", never an id.
    const SystemId id = spec.id;
    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
    if (!sys) return false;  // unknown backend / unreadable or non-SameBoy ROM

    // Seed the snapshot slot from the just-built core (control thread) BEFORE handoff, so a read
    // right after construct works with no block rendered.
    if (!engine_.registry().claim(id, *sys)) return false;  // pool full

    if (spec.replaceId) active_->replaceSystem(*spec.replaceId, std::move(sys));
    else                active_->adoptSystem(std::move(sys));
    return true;
}

// duplicate + reload are TS orchestration now (SystemsStore over the registry reads + constructSystem-
// with-state): duplicate pulls the source savestate and builds a seeded core; reload pulls SRAM and
// cold-boots the ROM with replaceId. Native has no bespoke method for either — that deletes the
// findSystem walks + the last live-core clone/cloneFromState reliance from this layer.

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

// Reads of the owned snapshot registry — the control plane reads published copies by id, never the
// live core, so they're safe while the audio thread runs (no audioRunning_ guard, no findSystem walk).
std::optional<rfl::Bytestring> EngineRpcService::readState(std::uint32_t id) {
    auto bytes = engine_.readState(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

std::optional<rfl::Bytestring> EngineRpcService::readSram(std::uint32_t id) {
    auto bytes = engine_.readSram(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool EngineRpcService::screenshot(std::uint32_t id, std::string path) {
    if (audioRunning_.load(std::memory_order_acquire)) return false;
    return engine_.screenshot(id, path);
}

// Like the reads above, getFrame reads the owned registry by id (the UI displays frames while audio
// plays) — no guard, no findSystem walk.
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

bool EngineRpcService::setAudioRouting(std::uint32_t mode) {
    if (mode > 2) return false;  // Stereo / TwoPerInstance / OnePerInstance
    active_->setAudioRouting(static_cast<std::uint8_t>(mode));  // → the block runner's MultiOutRouter
    return true;
}

bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 4) return false;  // a MIDI message fits in 4 bytes
    active_->stageMidi(std::move(bytes));
    return true;
}
