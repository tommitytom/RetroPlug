#include "EngineRpcService.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>

#include "Engine.hpp"
#include "EngineInvoker.hpp"
#include "MesenBackend.hpp"
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

// The inverse for a wire seed (rfl::Bytestring, i.e. a Uint8Array) copied into the build spec's
// byte vector — std::byte isn't implicitly convertible to std::uint8_t, so reinterpret + copy.
std::vector<std::uint8_t> toBytes(const rfl::Bytestring& b) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data());
    return std::vector<std::uint8_t>(p, p + b.size());
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
    if (spec.sramBytes) out.sram = toBytes(*spec.sramBytes);
    else if (spec.savPath) out.sram = slurpAll(*spec.savPath);
    if (spec.stateBytes) out.savestate = toBytes(*spec.stateBytes);
    else if (spec.statePath) out.savestate = slurpAll(*spec.statePath);
    if (spec.settings) out.settings.assign(spec.settings->begin(), spec.settings->end());
    return out;
}

} // namespace

EngineRpcService::EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker)
    : engine_(engine), factory_(factory), invoker_(invoker) {}

bool EngineRpcService::constructSystem(BackendConstructSpec spec) {
    // TS owns the id counter and passes it in; the build (control thread) can fail (bad ROM), the
    // queued adopt can't — so this returns "did it build", never an id.
    const SystemId id = spec.id;
    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
    if (!sys) return false;  // unknown backend / unreadable or non-SameBoy ROM

    // Seed the snapshot slot from the just-built core (control thread) BEFORE handoff, so a read
    // right after construct works with no block rendered.
    if (!engine_.registry().claim(id, *sys)) return false;  // pool full

    if (spec.replaceId) invoker_.replaceSystem(*spec.replaceId, std::move(sys));
    else                invoker_.adoptSystem(std::move(sys));
    return true;
}

// duplicate + reload are TS orchestration now (SystemsStore over the registry reads + constructSystem-
// with-state): duplicate pulls the source savestate and builds a seeded core; reload pulls SRAM and
// cold-boots the ROM with replaceId. Native has no bespoke method for either — that deletes the
// findSystem walks + the last live-core clone/cloneFromState reliance from this layer.

bool EngineRpcService::removeSystem(std::uint32_t id) {
    // No threading branch: quiescent → direct erase; running → enqueue for the audio thread, which
    // hands the core back through the release ring for the control thread to delete (drainReleased).
    invoker_.removeSystem(id);
    return true;
}

bool EngineRpcService::applySystemSetting(std::uint32_t id, std::string key, double value) {
    // A universal per-system setting → the live core, pushed through the invoker (flushed inline when
    // quiescent, applied on the audio thread while it runs). The store gates existence on its own list.
    if (key == "gainDb")
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Gain), value);
    else if (key == "reloadOnRomChange")
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ReloadOnRomChange), value);
    else
        return false;  // unknown key
    return true;
}

bool EngineRpcService::applyRoleConfig(std::uint32_t id, std::string kind, std::string config) {
    // A backend "system" role carries live emulator config, keyed by core ("sameboy" | "mesen"). The
    // store re-sends the WHOLE role config, so each field applies guarded (Engine no-ops an unchanged
    // one — no spurious model restart / region reset when only a sibling knob moved).
    if (kind == "sameboy") {
        const SameBoyRoleConfig c = SameBoyBackend::decodeSameBoyRoleConfig(config);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Model), static_cast<double>(c.model));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Highpass), static_cast<double>(c.highpass));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::LinkGroup), static_cast<double>(c.linkGroupId));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::FastBoot), c.fastBoot ? 1.0 : 0.0);
        return true;
    }
    if (kind == "mesen") {
        // Attaches to any Mesen system; a GBA system casts to null in Engine::applyConfigField → no-op.
        const MesenNesRoleConfig c = MesenBackend::decodeMesenNesRoleConfig(config);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRegion), static_cast<double>(c.region));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRemoveSpriteLimit), c.removeSpriteLimit ? 1.0 : 0.0);
        return true;
    }
    return false;
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
    // Encodes the owned registry frame (a published copy) — safe while the audio thread runs, no guard.
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
    invoker_.loadKernel(std::move(bytecode));
    return true;  // applied now (direct) or on the audio thread; the DSP stage goes active there
}

bool EngineRpcService::dspSetSystems(std::string json) {
    invoker_.setSystems(std::move(json));
    return true;
}

bool EngineRpcService::pressButton(std::uint32_t id, std::uint32_t button, bool down) {
    // A joypad transition → the focused core, pushed through the invoker (flushed inline when quiescent,
    // applied on the audio thread while it runs) — so it reaches a live core in both. The store owns
    // existence; accept optimistically (the queued apply can't report back).
    invoker_.pressButton(id, static_cast<std::uint8_t>(button), down);
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
    invoker_.setTransport(running);  // transport is a queued op — applied now (direct) or on the audio thread
    return true;
}

bool EngineRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) return false;
    invoker_.setBpm(bpm);
    return true;
}

bool EngineRpcService::setAudioRouting(std::uint32_t mode) {
    if (mode > 2) return false;  // Stereo / TwoPerInstance / OnePerInstance
    invoker_.setAudioRouting(static_cast<std::uint8_t>(mode));  // → the block runner's MultiOutRouter
    return true;
}

bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 4) return false;  // a MIDI message fits in 4 bytes
    invoker_.stageMidi(std::move(bytes));
    return true;
}

// Direct engine_ access (not the invoker): the benchmark drives the synchronous renderAudio pull path
// and never startAudio, so the control thread owns the Engine here — same regime as readState.
DspAllocStats EngineRpcService::dspAllocStats() { return engine_.dspAllocStats(); }

bool EngineRpcService::dspResetAllocStats(bool disableAutoGc) {
    engine_.resetDspAllocStats(disableAutoGc);
    return true;
}

DspGcResult EngineRpcService::dspRunGc() { return engine_.dspRunGc(); }

bool EngineRpcService::dspTraceReset(bool arm) {
    engine_.dspTraceReset(arm);
    return true;
}

std::vector<DspTraceSpan> EngineRpcService::dspTrace() { return engine_.dspTrace(); }
std::vector<std::string>  EngineRpcService::dspTraceNames() { return engine_.dspTraceNames(); }
