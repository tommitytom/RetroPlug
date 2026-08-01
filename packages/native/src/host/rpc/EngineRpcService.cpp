#include "host/rpc/EngineRpcService.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "system/mesen/MesenBackend.hpp"
#include "system/sameboy/SameBoyBackend.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "system/SystemFactory.hpp"

#include "EmbeddedRoms.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#include "kit/KitCompiler.hpp"       // rp::kit::KitCompiler (generic, compileKit) — heavy, so only in this TU
#include "lsdj/LsdjKitCodec.hpp"     // rp::lsdj::LsdjKitCodec (the LSDJ nibble/16KB-bank codec)
#include "risa/RisaDmcCodec.hpp"     // rp::risa::RisaDmcCodec (the risa NES-DPCM/8KB-bank codec)

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
    if (spec.romBytes) out.romBytes = toBytes(*spec.romBytes);
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

// Out-of-line so unique_ptr<KitCompiler> can destroy a complete type (fwd-declared in the header).
EngineRpcService::~EngineRpcService() = default;

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
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ColorCorrection), static_cast<double>(c.colorCorrection));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::DmgPalette), static_cast<double>(c.dmgPalette));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::LightTemperature), c.lightTemperature);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::BackgroundEnabled), c.backgroundEnabled ? 1.0 : 0.0);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ObjectsEnabled), c.objectsEnabled ? 1.0 : 0.0);
        return true;
    }
    if (kind == "mesen") {
        // Attaches to any Mesen system; a GBA system casts to null in Engine::applyConfigField → no-op.
        const MesenNesRoleConfig c = MesenBackend::decodeMesenNesRoleConfig(config);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRegion), static_cast<double>(c.region));
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRemoveSpriteLimit), c.removeSpriteLimit ? 1.0 : 0.0);
        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesApuLatencyMs), c.apuLatencyMs);
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

std::optional<rfl::Bytestring> EngineRpcService::readRam(std::uint32_t id) {
    auto bytes = engine_.readRam(id);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool EngineRpcService::screenshot(std::uint32_t id, std::string path) {
    // Encodes the owned registry frame (a published copy) — safe while the audio thread runs, no guard.
    return engine_.screenshot(id, path);
}

// Like the reads above, getFrame reads the owned registry by id (the UI displays frames while audio
// plays) — no guard, no findSystem walk.
RpcFrame EngineRpcService::getFrame(std::uint32_t id) {
    const EngineFrame f = engine_.getFrame(id);
    RpcFrame out;
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
        accumulateMidiOut();  // gather this block's kernel MIDI-out before the next block clears it
        for (std::uint32_t f = 0; f < frames; ++f) {
            out.push_back(scratchL_[f]);  // interleave L,R,L,R…
            out.push_back(scratchR_[f]);
        }
    }
    const auto* p = reinterpret_cast<const std::byte*>(out.data());
    return rfl::Bytestring(p, p + out.size() * sizeof(float));
}

std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
    const std::size_t n = engine_.systemCount();
    std::vector<rfl::Bytestring> result;
    if (ms <= 0.0 || n == 0) return result;

    // One persistent L/R block buffer per system; the Engine routes each core into its own pair
    // (PerSystemRouter) so linked systems interleave exactly as they do in the mixed path.
    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
    std::vector<float*> ls(n), rs(n);
    std::vector<std::vector<float>> out(n);  // per system: interleaved L,R,L,R…
    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    for (std::size_t i = 0; i < n; ++i) {
        ls[i] = bl[i].data();
        rs[i] = br[i].data();
        out[i].reserve(total * 2);
    }

    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlockPerSystem(frames, ls.data(), rs.data(), n);
        accumulateMidiOut();  // gather this block's kernel MIDI-out before the next block clears it
        for (std::size_t i = 0; i < n; ++i)
            for (std::uint32_t f = 0; f < frames; ++f) {
                out[i].push_back(ls[i][f]);  // interleave L,R,L,R…
                out[i].push_back(rs[i][f]);
            }
    }

    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto* p = reinterpret_cast<const std::byte*>(out[i].data());
        result.emplace_back(p, p + out[i].size() * sizeof(float));
    }
    return result;
}

std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerChannel(std::uint32_t id, double ms) {
    std::vector<rfl::Bytestring> result;
    // Single-system only: PerChannelRouter fans by stream index, not slot, so a second system's streams
    // would collide into these buffers. The mixed/per-system paths are the multi-system routes.
    if (ms <= 0.0 || engine_.systemCount() != 1) return result;
    SystemBase* sys = engine_.findSystem(id);
    if (sys == nullptr) return result;

    // The target's output streams (Game Boy = 4 stereo: Pulse 1/Pulse 2/Wave/Noise; default = 1 "Mix").
    const std::size_t n = sys->channelLayout().size();
    if (n == 0) return result;

    // One persistent L/R block buffer per stream; the Engine routes each stream into its own pair.
    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
    std::vector<float*> ls(n), rs(n);
    std::vector<std::vector<float>> out(n);  // per stream: interleaved L,R,L,R…
    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    for (std::size_t i = 0; i < n; ++i) {
        ls[i] = bl[i].data();
        rs[i] = br[i].data();
        out[i].reserve(total * 2);
    }

    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlockPerChannel(frames, ls.data(), rs.data(), n);
        accumulateMidiOut();  // gather this block's kernel MIDI-out before the next block clears it
        for (std::size_t i = 0; i < n; ++i)
            for (std::uint32_t f = 0; f < frames; ++f) {
                out[i].push_back(ls[i][f]);  // interleave L,R,L,R…
                out[i].push_back(rs[i][f]);
            }
    }

    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto* p = reinterpret_cast<const std::byte*>(out[i].data());
        result.emplace_back(p, p + out[i].size() * sizeof(float));
    }
    return result;
}

rfl::Bytestring EngineRpcService::compileKit(KitCompileSpec spec) {
    // Lazily build the compiler on first use — it spins up an enkiTS pool + a SampleCache, which the
    // plugin (which also constructs this service) must never pay for.
    if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::kit::KitCompiler>();

    const bool rotate = spec.rotate.value_or(true);   // per the target ROM's LSDj version (9.2.0+ rotates)
    std::vector<rp::lsdj::CompileSampleSpec> specs;
    specs.reserve(spec.samples.size());
    for (auto& s : spec.samples) {
        rp::lsdj::CompileSampleSpec cs;
        cs.path    = s.path;
        cs.name    = s.name;
        cs.offset  = s.offset.value_or(0);
        cs.length  = s.length.value_or(0);
        cs.effects = s.effects;
        cs.rotate  = rotate;
        specs.push_back(std::move(cs));
    }

    // The generic compiler drives the LSDJ codec (nibble pack + 16 KB bank). Always returns a bank; a
    // per-sample load failure just leaves that slot empty. The caller (CLI) validates by reading the
    // bank back, so no exception crosses the RPC boundary here.
    rp::lsdj::LsdjKitCodec codec(spec.name, std::move(specs));
    rp::kit::CompiledKit kit = kitCompiler_->compile(codec);
    return toBytestring(kit.bytes);
}

rfl::Bytestring EngineRpcService::compileDmc(RisaKitCompileSpec spec) {
    // Reuse the same lazy compiler as compileKit — it's codec-agnostic (enkiTS pool + SampleCache).
    if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::kit::KitCompiler>();

    std::vector<rp::risa::CompileDmcSampleSpec> specs;
    specs.reserve(spec.samples.size());
    for (auto& s : spec.samples) {
        rp::risa::CompileDmcSampleSpec cs;
        cs.path      = s.path;
        cs.name      = s.name;
        cs.offset    = s.offset.value_or(0);
        cs.length    = s.length.value_or(0);
        cs.effects   = s.effects;
        cs.rate      = static_cast<std::uint8_t>(s.rate.value_or(12) & 0x0F);
        cs.loop      = s.loop.value_or(false);
        cs.normalize = s.normalize.value_or(true);
        specs.push_back(std::move(cs));
    }

    rp::risa::RisaDmcCodec codec(spec.name, std::move(specs));
    rp::kit::CompiledKit kit = kitCompiler_->compile(codec);
    return toBytestring(kit.bytes);
}

double EngineRpcService::sampleRate() const {
    return engine_.sampleRate();
}

bool EngineRpcService::setSampleRate(double sr) {
    // Harness use is to pick the render rate BEFORE building a system, so require an empty engine: it keeps
    // this off the audio thread (a system-free engine has none) and makes the render rate unambiguous.
    // (Engine::setSampleRate itself does re-rate live cores — that path is exercised by the plugin.)
    if (sr <= 0.0 || engine_.systemCount() != 0) return false;
    engine_.setSampleRate(sr);
    return true;
}

bool EngineRpcService::setTransport(bool running) {
    invoker_.setTransport(running);  // transport is a queued op — applied now (direct) or on the audio thread
    return true;
}

bool EngineRpcService::setPpq(double ppq) {
    invoker_.setPpq(ppq);  // queued like transport, so a locate lands between blocks not mid-block
    return true;
}

bool EngineRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) return false;
    invoker_.setBpm(bpm);
    return true;
}

bool EngineRpcService::setAudioRouting(std::uint32_t mode) {
    if (mode > 3) return false;  // Stereo / TwoPerInstance / OnePerInstance / ChannelSplit
    invoker_.setAudioRouting(static_cast<std::uint8_t>(mode));  // → the Engine's router (Multi-out / channel-split)
    return true;
}

bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 4) return false;  // a MIDI message fits in 4 bytes
    invoker_.stageMidi(std::move(bytes));
    return true;
}

bool EngineRpcService::setSerialOutCapture(std::uint32_t id, bool on) {
    // Control-plane arm/disarm through the invoker's config path (SameBoy-only in Engine::applyConfigField;
    // a no-op on a Mesen/GBA system). The store gates existence on its own list — accept optimistically.
    invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::SerialOutCapture), on ? 1.0 : 0.0);
    return true;
}

void EngineRpcService::accumulateMidiOut() {
    // Engine::midiOut() holds only the block just rendered (cleared at the top of the next processBlock),
    // so copy it out now. Usually empty (only an armed LSDj MI.OUT system emits) — cheap when so.
    for (const auto& mo : engine_.midiOut()) {
        RpcMidiOut g;
        g.system = mo.system;
        g.frame  = mo.frame;
        const auto* p = reinterpret_cast<const std::byte*>(mo.data.data());
        g.data.assign(p, p + mo.data.size());
        accumMidiOut_.push_back(std::move(g));
    }
}

std::vector<RpcMidiOut> EngineRpcService::drainMidiOut() {
    return std::exchange(accumMidiOut_, {});  // return the window's accumulation and reset it
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
