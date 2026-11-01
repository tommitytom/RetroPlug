#include "host/rpc/EngineRpcService.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include "graph/CanonicalJson.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"

#include "EmbeddedRoms.hpp"
#include "system/SystemBase.hpp"

#include "kit/KitCompiler.hpp"       // rp::kit::KitCompiler (generic, compileKit) - heavy, so only in this TU
#include "lsdj/LsdjKitCodec.hpp"     // rp::lsdj::LsdjKitCodec (the LSDJ nibble/16KB-bank codec)
#include "risa/RisaDmcCodec.hpp"     // rp::risa::RisaDmcCodec (the risa NES-DPCM/8KB-bank codec)
#include "util/SlurpFile.hpp"

namespace {

rfl::Bytestring toBytestring(const std::vector<std::uint8_t>& v) {
    if (v.empty()) return {};
    const auto* p = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(p, p + v.size());
}

// The inverse for a wire byte block (rfl::Bytestring, i.e. a Uint8Array) copied into a std::vector<std::uint8_t>:
// std::byte isn't implicitly convertible to std::uint8_t, so reinterpret + copy. A zero-length block answers an empty
// vector rather than forming a range from its data pointer, which may be null.
std::vector<std::uint8_t> toBytes(const rfl::Bytestring& b) {
    if (b.size() == 0) return {};
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data());
    return std::vector<std::uint8_t>(p, p + b.size());
}

} // namespace

EngineRpcService::EngineRpcService(Engine& engine, QueuedInvoker& invoker) : engine_(engine), invoker_(invoker) {}

// Whether the planned graph writes exactly `channels` host channels: TS sets the shape a render call needs
// before making it, and a render into buffers of another width would fill them from the wrong wiring.
bool EngineRpcService::rendersInto(std::size_t channels) const {
    if (engine_.hostWidth() == channels) return true;
    std::fprintf(stderr, "[engine] a render into %zu channels was refused: the planned graph writes %u\n", channels,
                 static_cast<unsigned>(engine_.hostWidth()));
    return false;
}

// Out-of-line so unique_ptr<KitCompiler> can destroy a complete type (fwd-declared in the header).
EngineRpcService::~EngineRpcService() = default;

RpcPutResourceResult EngineRpcService::putResource(BackendResourceSpec spec) {
    RpcPutResourceResult out{std::nullopt, std::nullopt};
    // A node reads a resource as one of three things (a system's ROM, its battery, a savestate), so the wire takes
    // no other type: a resource of a type no node wants could only be a mistake in the caller.
    if (spec.type != "rom" && spec.type != "sram" && spec.type != "state") {
        out.error = "a resource type is \"rom\", \"sram\" or \"state\", not " + rp::graph::jsonString(spec.type);
        return out;
    }
    const int sources = (spec.bytes ? 1 : 0) + (spec.path ? 1 : 0) + (spec.embedded ? 1 : 0);
    if (sources != 1) {
        out.error = "a resource carries exactly one of bytes, path or embedded (" + std::to_string(sources) + " given)";
        return out;
    }
    std::vector<std::uint8_t> bytes;
    if (spec.bytes) {
        bytes = toBytes(*spec.bytes);
    } else if (spec.path) {
        // A file that cannot be opened or read is refused; one that opens empty is not, since an empty payload is a
        // resource (a zero-byte `.sav` is a blank battery, as a zero-byte byte block is). An empty rom is refused
        // below, for the same reason a byte block is: it has nothing to boot.
        std::optional<std::vector<std::uint8_t>> file = slurpAll(*spec.path);
        if (!file) {
            out.error = "could not read " + rp::graph::jsonString(*spec.path) + ": the file is absent or unreadable";
            return out;
        }
        bytes = std::move(*file);
    } else {
        const std::span<const std::uint8_t> rom = rp::embeddedRom(*spec.embedded);
        if (rom.empty()) {
            out.error = rp::graph::jsonString(*spec.embedded) + " is not an embedded ROM";
            return out;
        }
        bytes.assign(rom.begin(), rom.end());
    }
    // An empty byte block is a legitimate seed (a blank battery), but a ROM with no bytes has nothing to boot.
    if (bytes.empty() && spec.type == "rom") {
        out.error = "a rom resource cannot be empty: a zero-length ROM cannot boot";
        return out;
    }
    std::string why;
    const std::uint32_t id = engine_.putResource(spec.type, std::move(bytes), &why);
    if (id == 0) {
        out.error = why.empty() ? "the engine created no resource" : why;
        return out;
    }
    out.id = id;
    return out;
}

bool EngineRpcService::releaseResource(std::uint32_t id) { return engine_.releaseResource(id); }

RpcSetGraphResult EngineRpcService::setGraph(RpcSetGraphRequest request) {
    Engine::GraphRequest r;
    r.document = std::move(request.document);
    r.slots.assign(request.slots.begin(), request.slots.end());
    if (request.tap) {
        r.rearmTap = true;
        if (request.tap->arm) {
            r.arm = true;
            r.armId = request.tap->arm->id;
            r.armMode = request.tap->arm->mode;
        }
    }
    const Engine::GraphResult result = engine_.setGraph(r);
    // The errors travel back in the result, and EngineGraph (every TS publisher's path) logs them.
    RpcSetGraphResult out{result.ok, {}};
    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
    return out;
}

RpcLoadScriptPackResult EngineRpcService::loadScriptPack(RpcLoadScriptPackRequest request) {
    RpcLoadScriptPackResult out{false, {}, {}, {}};
    // The limit arrives as the JS number it was (BackendTypes.hpp says why): a whole number of bytes a double holds
    // exactly, or refused.
    std::size_t limit = 0;
    if (request.memoryLimitBytes) {
        constexpr double kLargest = 9007199254740991.0;  // 2^53 - 1, JS's Number.MAX_SAFE_INTEGER
        const double bytes = *request.memoryLimitBytes;
        if (!(bytes >= 0.0 && bytes <= kLargest && std::floor(bytes) == bytes)) {
            char text[64];
            std::snprintf(text, sizeof text, "%g", bytes);
            out.error = std::string("memoryLimitBytes must be a whole number of bytes, 0 for the default: not ") + text;
            return out;
        }
        limit = static_cast<std::size_t>(bytes);
    }
    const Engine::ScriptPackResult result = engine_.loadScriptPack(request.id, request.source, limit);
    out.ok = result.ok;
    out.error = result.error;
    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
    for (const auto& t : result.types) out.types.push_back({t.id, t.descriptor});
    return out;
}

bool EngineRpcService::setParam(std::string nodeId, std::string paramId, double value) {
    return engine_.setParam(nodeId, paramId, value);
}

// duplicate, reload and remove are TS orchestration (SystemsStore over the resource calls, the registry reads and
// the documents it publishes): duplicate pulls the source savestate and boots a seeded system; reload pulls SRAM
// and cold-boots the ROM under a new id; remove publishes a document without it. Native has no bespoke method for
// any of them.

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

bool EngineRpcService::writeRam(std::uint32_t id, std::uint32_t offset, rfl::Bytestring bytes) {
    // Queued through the invoker (flushed inline when quiescent, applied on the audio thread while it
    // runs), so a host can poke a PLAYING core.
    //
    // But NOT accepted optimistically the way pressButton is, and the difference matters. A dropped
    // key edge is a lost keypress; a dropped 6,912-byte write is a caller that believes it loaded a
    // song. So the answerable failures are answered HERE, synchronously, from the published snapshot
    // (race-free, no live-core read): unknown id, no writable RAM, out of bounds. The queued apply
    // re-checks bounds anyway, because the region could in principle change between the two.
    if (bytes.empty()) return false;
    const std::size_t ram = engine_.ramSize(id);
    if (ram == 0 || static_cast<std::size_t>(offset) + bytes.size() > ram) return false;
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return invoker_.writeRam(id, offset, std::vector<std::uint8_t>(p, p + bytes.size()));
}

RpcJobSubmitResult EngineRpcService::submitJob(RpcJobRequest request) {
    rp::host::JobRequest r;
    r.nodeId = std::move(request.nodeId);
    r.capability = std::move(request.capability);
    r.op = std::move(request.op);
    r.region = request.region.value_or("");
    r.offset = request.offset.value_or(0);
    r.length = request.length.value_or(0);
    r.name = request.name.value_or("");
    if (request.bytes) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(request.bytes->data());
        r.bytes.assign(p, p + request.bytes->size());
    }
    const rp::host::JobSubmit submitted = engine_.submitJob(std::move(r));
    RpcJobSubmitResult out;
    if (submitted.id != 0) out.id = submitted.id;
    else out.error = submitted.error;
    return out;
}

std::vector<RpcJobCompletion> EngineRpcService::drainJobs() {
    std::vector<RpcJobCompletion> out;
    for (auto& c : engine_.drainJobs())
        out.push_back({c.id, c.done, c.ok, std::move(c.error), toBytestring(c.bytes), c.progress, c.total});
    return out;
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

bool EngineRpcService::pressButton(std::uint32_t id, std::uint32_t button, bool down) {
    // A joypad transition → the focused core, pushed through the invoker (flushed inline when quiescent,
    // applied on the audio thread while it runs) — so it reaches a live core in both. The store owns
    // existence; accept optimistically (the queued apply can't report back).
    invoker_.pressButton(id, static_cast<std::uint8_t>(button), down);
    return true;
}

bool EngineRpcService::stageNodeMidi(std::string nodeId, std::vector<std::uint8_t> bytes) {
    if (bytes.empty()) return false;
    invoker_.stageNodeMidi(std::move(nodeId), std::move(bytes));
    return true;
}

rfl::Bytestring EngineRpcService::renderAudio(double ms) {
    // This thread calls processBlock itself, which only the Engine's owning thread may do.
    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};
    if (!rendersInto(2)) return {};
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
        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
        for (std::uint32_t f = 0; f < frames; ++f) {
            out.push_back(scratchL_[f]);  // interleave L,R,L,R…
            out.push_back(scratchR_[f]);
        }
    }
    const auto* p = reinterpret_cast<const std::byte*>(out.data());
    return rfl::Bytestring(p, p + out.size() * sizeof(float));
}

std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
    std::vector<rfl::Bytestring> result;
    if (ms <= 0.0 || invoker_.audioThreadOwns()) return result;  // as renderAudio
    const std::size_t n = engine_.systemCount();
    if (n == 0) return result;
    if (!rendersInto(2 * n)) return result;

    // One persistent L/R block buffer per system; the PerSystem shape routes each core into its own pair,
    // so linked systems interleave exactly as they do in the mixed path.
    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
    std::vector<float*> ls(n), rs(n), lanes;  // lanes: the pairs as one channel list (L0, R0, L1, R1, …)
    std::vector<std::vector<float>> out(n);  // per system: interleaved L,R,L,R…
    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    for (std::size_t i = 0; i < n; ++i) {
        ls[i] = bl[i].data();
        rs[i] = br[i].data();
        lanes.insert(lanes.end(), {ls[i], rs[i]});
        out[i].reserve(total * 2);
    }

    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlock(frames, lanes.data(), lanes.size());
        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
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
    if (invoker_.audioThreadOwns()) return result;  // as renderAudio
    // Single-system only: the PerChannel shape fans one system's streams, so a second system's would collide
    // into these buffers. The mixed/per-system paths are the multi-system routes.
    if (ms <= 0.0 || engine_.systemCount() != 1) return result;
    SystemBase* sys = engine_.findSystem(id);
    if (sys == nullptr) return result;

    // The target's output streams (Game Boy = 4 stereo: Pulse 1/Pulse 2/Wave/Noise; default = 1 "Mix").
    const std::size_t n = sys->channelLayout().size();
    if (n == 0) return result;
    if (!rendersInto(2 * n)) return result;

    // One persistent L/R block buffer per stream; the PerChannel shape routes each stream into its own pair.
    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
    std::vector<float*> ls(n), rs(n), lanes;  // lanes: the pairs as one channel list (L0, R0, L1, R1, …)
    std::vector<std::vector<float>> out(n);  // per stream: interleaved L,R,L,R…
    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
    for (std::size_t i = 0; i < n; ++i) {
        ls[i] = bl[i].data();
        rs[i] = br[i].data();
        lanes.insert(lanes.end(), {ls[i], rs[i]});
        out[i].reserve(total * 2);
    }

    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
        engine_.processBlock(frames, lanes.data(), lanes.size());
        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
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
    // Harness use is to pick the render rate BEFORE building a system, so require an empty engine with no
    // audio thread running (the CLI's driver runs one with no systems): the graph re-activates on THIS
    // thread, and the render rate is unambiguous.
    // (Engine::setSampleRate itself does re-rate live cores — that path is exercised by the plugin.)
    if (sr <= 0.0 || invoker_.audioThreadOwns() || engine_.systemCount() != 0) return false;
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

bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes, std::uint32_t frame) {
    // Any non-empty length: a channel message, a SysEx, or several messages staged as one run. The
    // invoker carries the long ones as an owning payload; the routing node broadcasts a run longer
    // than one message unchanged, and a system node's midi input hands it to the core's raw byte path.
    if (bytes.empty() || frame >= kBlockSize) return false;
    invoker_.stageMidi(std::move(bytes), frame);
    return true;
}

bool EngineRpcService::stageControllerIn(std::vector<std::uint8_t> bytes) {
    // A pad press waits in the harness's surface link, as a device's would, and the document's surface-in node drains
    // it at the next block. No link is no control surface: the host answers so rather than staging into nothing.
    if (!harnessSurface_ || bytes.empty()) return false;
    return harnessSurface_->queueInput(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
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

namespace {

// The plan's own block size: a render never grows it (Engine::setMaxBlockFrames), so a longer block would
// render silence.
constexpr std::uint32_t kMaxRenderBlockFrames = Engine::kDefaultMaxBlockFrames;

// Why renderBlocks refuses `req`, or empty. Checked whole before anything renders, so a refused request
// leaves the Engine exactly as it was.
std::string renderBlocksRefusal(const RpcRenderBlocksRequest& req, bool hasSurface) {
    if (req.outputs < 2 || req.outputs > 8 || req.outputs % 2 != 0) return "outputs must be 2, 4, 6 or 8";
    for (std::size_t b = 0; b < req.blocks.size(); ++b) {
        const RpcRenderBlock& blk = req.blocks[b];
        const std::string at = "block " + std::to_string(b) + ": ";
        if (blk.frames == 0 || blk.frames > kMaxRenderBlockFrames)
            return at + "frames must be 1.." + std::to_string(kMaxRenderBlockFrames);
        std::uint32_t last = 0;
        for (const RpcBlockMidi& m : blk.midi) {
            if (m.bytes.empty()) return at + "a MIDI message is empty";
            if (m.frame >= blk.frames)
                return at + "MIDI at frame " + std::to_string(m.frame) + " is outside a " + std::to_string(blk.frames) + "-frame block";
            // A DAW delivers events frame-sorted, so unsorted input is left out of the baseline. (The plugin
            // itself appends automation CCs at frame 0 after later host events, which rp.host.midi-in sorts
            // into place, so unsorted input IS a real plugin path, held by parity/host-midi-order; it is
            // deliberately not recorded.) Ties keep their list order.
            if (m.frame < last)
                return at + "MIDI frames go backwards (" + std::to_string(m.frame) + " after " + std::to_string(last) + ")";
            last = m.frame;
        }
        for (const auto& c : blk.controller) {
            if (c.empty() || c.size() > 4) return at + "a controller message must be 1..4 bytes";
            // It goes where a device's own input would wait (the harness's surface link), so a host with none has
            // nowhere to stage it: the whole run is refused rather than one message silently dropped.
            if (!hasSurface) return at + "this host has no control surface to stage a controller message into";
        }
        if (blk.bpm && !(std::isfinite(*blk.bpm) && *blk.bpm > 0.0)) return at + "bpm must be a positive finite number";
        if (blk.ppq && !std::isfinite(*blk.ppq)) return at + "ppq must be finite";
    }
    return {};
}

} // namespace

RpcRenderBlocksResult EngineRpcService::renderBlocks(RpcRenderBlocksRequest req) {
    RpcRenderBlocksResult result;
    // This thread calls processBlock itself, which only the Engine's owning thread may do.
    if (invoker_.audioThreadOwns()) {
        result.error = "the background audio thread is running";
        return result;
    }
    result.error = renderBlocksRefusal(req, harnessSurface_ != nullptr);
    if (!result.error.empty()) return result;

    // The graph TS published must be built for these lanes (docs/modular-graph-phase-b2-design.md section 4).
    if (engine_.hostWidth() != req.outputs) {
        result.error = "the planned graph writes " + std::to_string(engine_.hostWidth()) + " host channels, not " +
                       std::to_string(req.outputs);
        return result;
    }
    // A run is a session of its own, and nothing is cleared here to make it one: a press an earlier run staged while
    // no plan placed the surface waits in the link, and the surface-in node of a plan that does place it drains what the
    // link held on its first block after it bound the link, forwarding none of it. A press staged while the surface is
    // bound is delivered at the next block - renderBlocks' first included - as stageControllerIn promises.
    std::uint64_t total = 0;
    for (const RpcRenderBlock& b : req.blocks) total += b.frames;
    // Whole-run planar lanes, zero-filled; each block renders straight into its slice of the result.
    // The vector's allocation comes from operator new, so it is suitably aligned for float.
    result.lanes.assign(req.outputs, rfl::Bytestring(total * sizeof(float), std::byte{0}));
    std::vector<float*> outs(req.outputs);

    std::uint64_t at = 0;
    for (std::uint32_t b = 0; b < req.blocks.size(); ++b) {
        RpcRenderBlock& blk = req.blocks[b];
        // PluginDSP::run's order: host MIDI staged at its frames, then the control plane's queued edits,
        // then transport, then the block itself. Not modelled: the plugin's CC-automation stage and its
        // MIDI-out size filter. ppq (a locate) and controller have no PluginDSP::run counterpart (the
        // plugin never calls setPpq, which would double-count; controller traffic is the SDL standalone's).
        for (const RpcBlockMidi& m : blk.midi) engine_.stageMidi(m.frame, m.bytes);  // copied into the staging
        // The controller's own stream, into the harness's surface link (a pad press is staged as a device's arrival
        // would be: frame 0 of this block). A host with no harness surface has no control surface to stage it into.
        for (const auto& c : blk.controller)
            harnessSurface_->queueInput(std::span<const std::uint8_t>(c.data(), c.size()));
        invoker_.drainInto(engine_);
        if (blk.bpm) engine_.setBpm(*blk.bpm);
        if (blk.playing) engine_.setTransport(*blk.playing);
        if (blk.ppq) engine_.setPpq(*blk.ppq);
        for (std::uint32_t c = 0; c < req.outputs; ++c)
            outs[c] = reinterpret_cast<float*>(result.lanes[c].data()) + at;
        engine_.processBlock(blk.frames, outs.data(), req.outputs);
        for (const auto& mo : engine_.midiOut()) {
            RpcBlockMidiOut o;
            o.block  = b;
            o.system = mo.system;
            o.frame  = mo.frame;
            const auto* p = reinterpret_cast<const std::byte*>(mo.data.data());
            o.data.assign(p, p + mo.data.size());
            result.midiOut.push_back(std::move(o));
        }
        engine_.clearMidiOut();
        at += blk.frames;
    }
    return result;
}

std::uint32_t EngineRpcService::droppedMessages() const { return engine_.droppedMessages(); }

// "<os>-<arch>-<compiler><major>", from the compiler's own macros so each slice of a universal binary
// names itself (a CMake variable would name the configure host).
#define RP_STR2(x) #x
#define RP_STR(x) RP_STR2(x)
#if defined(__linux__)
#define RP_TARGET_OS "linux"
#elif defined(__APPLE__)
#define RP_TARGET_OS "darwin"
#elif defined(_WIN32)
#define RP_TARGET_OS "windows"
#else
#define RP_TARGET_OS "unknown"
#endif
#if defined(__x86_64__) || defined(_M_X64)
#define RP_TARGET_ARCH "x64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RP_TARGET_ARCH "arm64"
#else
#define RP_TARGET_ARCH "unknown"
#endif
#if defined(__clang__) && defined(__apple_build_version__)
#define RP_TARGET_CC "appleclang" RP_STR(__clang_major__)
#elif defined(__clang__)
#define RP_TARGET_CC "clang" RP_STR(__clang_major__)
#elif defined(_MSC_VER)
#define RP_TARGET_CC "msvc" RP_STR(_MSC_VER)  // the full number: MSVC 14.44 and 14.51 differ
#elif defined(__GNUC__)
#define RP_TARGET_CC "gnu" RP_STR(__GNUC__)
#else
#define RP_TARGET_CC "unknown"
#endif

std::string EngineRpcService::buildTarget() const {
    return RP_TARGET_OS "-" RP_TARGET_ARCH "-" RP_TARGET_CC;
}

// On Windows the SameBoy core is compiled by clang-cl (cmake/sameboy.cmake), whose version this key does not
// carry; that would matter only if a Windows baseline were ever recorded.
#undef RP_STR2
#undef RP_STR
#undef RP_TARGET_OS
#undef RP_TARGET_ARCH
#undef RP_TARGET_CC

// Direct engine_ access (not the invoker): the benchmark drives the synchronous renderAudio pull path
// and never startAudio, so the control thread owns the Engine here — same regime as readState.
rp::host::DspAllocStats EngineRpcService::dspAllocStats() { return engine_.dspAllocStats(); }

bool EngineRpcService::dspResetAllocStats(bool disableAutoGc) {
    return engine_.resetDspAllocStats(disableAutoGc);  // false: refused (an audio thread owns the runtime), or not a profile build
}

rp::host::DspGcResult EngineRpcService::dspRunGc() { return engine_.dspRunGc(); }

bool EngineRpcService::dspTraceReset(bool arm) {
    return engine_.dspTraceReset(arm);
}

std::vector<rp::host::DspTraceSpan> EngineRpcService::dspTrace() { return engine_.dspTrace(); }
std::vector<std::string>            EngineRpcService::dspTraceNames() { return engine_.dspTraceNames(); }
