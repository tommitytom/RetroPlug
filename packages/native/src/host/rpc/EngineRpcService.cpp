#include "host/rpc/EngineRpcService.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <span>
#include <string>
#include <utility>

#include "graph/CanonicalJson.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"

#include "system/SystemBase.hpp"
#include "util/BuildTarget.hpp"      // rp::buildTarget (shared with the kit baseline's golden)
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
        // A built-in ROM, which the product's table holds because it lists the package that embeds it: an id it does
        // not list is not in that table (docs/modular-graph-phase-f2-design.md section 5).
        const std::span<const std::uint8_t> rom = rp::graph::embeddedResource(*spec.embedded);
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

bool EngineRpcService::writeSram(std::uint32_t id, std::uint32_t offset, rfl::Bytestring bytes, rfl::Bytestring expect) {
    // writeRam's twin, aimed at the battery, with the same synchronous refusals. The apply republishes the SRAM
    // snapshot (Engine::writeSram), so a readSram after the write lands sees it. A non-empty `expect` makes the write
    // conditional on the battery still holding it; that can only be judged at apply time, so a refusal there shows up
    // as a republished battery, not as false here.
    if (bytes.empty()) return false;
    if (!expect.empty() && expect.size() != bytes.size()) return false;
    const std::size_t sram = engine_.sramSize(id);
    if (sram == 0 || static_cast<std::size_t>(offset) + bytes.size() > sram) return false;
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const auto* e = reinterpret_cast<const std::uint8_t*>(expect.data());
    return invoker_.writeSram(id, offset, std::vector<std::uint8_t>(p, p + bytes.size()),
                              std::vector<std::uint8_t>(e, e + expect.size()));
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

RpcOfflineSubmitResult EngineRpcService::submitOfflineJob(RpcOfflineRequest request) {
    RpcOfflineSubmitResult out{std::nullopt, std::nullopt};
    rp::host::OfflineRequest r;
    r.document = request.document.value_or("");
    r.frames = request.frames;
    r.rate = request.rate;
    r.returns = std::move(request.returns);
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        RpcOfflineInput& in = request.inputs[i];
        const std::string name = "input " + std::to_string(i);
        const bool samples = in.samples || in.channels || in.rate;
        const bool file = in.path || in.offset || in.length || in.cap;
        if (samples == file) {
            out.error = name + " must be samples (samples, channels and rate) or a file (a path and its window)";
            return out;
        }
        if (samples) {
            if (!in.samples || !in.channels || !in.rate) {
                out.error = name + " needs samples, channels and a rate";
                return out;
            }
            if (in.samples->size() % sizeof(float) != 0) {
                out.error = name + "'s samples are " + std::to_string(in.samples->size()) +
                            " bytes, not a whole number of float32 values";
                return out;
            }
            rp::host::OfflineSamples s;
            s.samples.resize(in.samples->size() / sizeof(float));
            if (!s.samples.empty()) std::memcpy(s.samples.data(), in.samples->data(), in.samples->size());
            s.channels = *in.channels;
            s.rate = *in.rate;
            r.inputs.emplace_back(std::move(s));
            continue;
        }
        if (!in.path) {
            out.error = name + " needs a path";
            return out;
        }
        rp::host::OfflineFile f;
        f.path = std::move(*in.path);
        f.offset = in.offset.value_or(0);
        f.length = in.length.value_or(0);
        if (in.cap) {
            rp::host::OfflineCap cap;
            cap.frames = in.cap->frames;
            cap.rate = in.cap->rate;
            if (in.cap->arithmetic == "float-ratio") cap.arithmetic = rp::host::OfflineCapArithmetic::FloatRatio;
            else if (in.cap->arithmetic == "double-scale") cap.arithmetic = rp::host::OfflineCapArithmetic::DoubleScale;
            else {
                out.error = name + "'s cap arithmetic \"" + in.cap->arithmetic +
                            "\" is neither \"float-ratio\" nor \"double-scale\"";
                return out;
            }
            f.cap = cap;
        }
        r.inputs.emplace_back(std::move(f));
    }
    for (const RpcOfflineOutput& o : request.outputs) r.outputs.push_back({o.channels, o.frames, o.rate});
    rp::host::OfflineSubmit submitted = engine_.submitOfflineJob(std::move(r));
    if (submitted.id != 0) out.id = submitted.id;
    else out.error = std::move(submitted.error);
    return out;
}

RpcOfflineSubmitResult EngineRpcService::submitEngineJob(RpcEngineJobRequest request) {
    RpcOfflineSubmitResult  out{std::nullopt, std::nullopt};
    rp::host::OfflineSubmit submitted =
        engine_.submitEngineJob({std::move(request.kind), std::move(request.spec)});
    if (submitted.id != 0) out.id = submitted.id;
    else out.error = std::move(submitted.error);
    return out;
}

namespace {

std::vector<RpcEngineJobLine> toRpcLines(std::vector<rp::host::EngineJobLine>& lines) {
    std::vector<RpcEngineJobLine> out;
    out.reserve(lines.size());
    for (auto& l : lines)
        out.push_back({l.stream == rp::host::EngineJobStream::Err ? "stderr" : "stdout", std::move(l.text)});
    return out;
}

}  // namespace

RpcOfflineDrain EngineRpcService::drainOfflineJobs(double waitMs) {
    // A NaN or a negative wait waits not at all; the engine bounds a long one.
    const double ms = std::isfinite(waitMs) ? std::clamp(waitMs, 0.0, 1000.0) : (waitMs > 0 ? 1000.0 : 0.0);
    RpcOfflineDrain           out{{}, 0, {}, {}};
    std::size_t               inFlight = 0;
    rp::host::EngineJobsDrain engine;
    for (auto& c : engine_.drainOfflineJobs(std::chrono::milliseconds(static_cast<std::int64_t>(ms)), &inFlight,
                                            &engine)) {
        RpcOfflineCompletion rc{c.id, c.ok, std::move(c.error), {}, {}, c.guardsFired, std::move(c.guardReason)};
        for (auto& b : c.buffers) {
            rfl::Bytestring bytes;
            if (!b.samples.empty()) {
                const auto* p = reinterpret_cast<const std::byte*>(b.samples.data());
                bytes = rfl::Bytestring(p, p + b.samples.size() * sizeof(float));
            }
            rc.buffers.push_back({b.handle, b.channels, b.frames, b.rate, std::move(bytes), b.nonFinite});
        }
        for (const auto& i : c.inputs)
            rc.inputs.push_back({i.handle, i.decodedRate, i.decodedFrames, i.frames, i.nonFinite});
        out.completions.push_back(std::move(rc));
    }
    for (auto& c : engine.completions)
        out.engineCompletions.push_back({c.id, std::move(c.status), std::move(c.message), std::move(c.outputs),
                                         c.renderedMs, toRpcLines(c.lines), c.droppedLines});
    for (auto& p : engine.progress)
        out.engineProgress.push_back({p.id, p.renderedMs, toRpcLines(p.lines), p.droppedLines});
    out.inFlight = static_cast<std::uint32_t>(inFlight);
    return out;
}

bool EngineRpcService::cancelOfflineJob(std::uint32_t id) { return engine_.cancelOfflineJob(id); }

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

void EngineRpcService::setContinuousBlocks(bool on) {
    continuous_ = on;
    dropCarry();
}

void EngineRpcService::dropCarry() {
    carry_.shape = Shape::None;
    carry_.frames = 0;
    carry_.streams.clear();
}

std::vector<std::vector<float>> EngineRpcService::pull(Shape shape, std::uint32_t id, std::size_t streams, double ms,
                                                       const BlockFn& block) {
    const double                    rate = engine_.sampleRate();
    const std::uint64_t             total = static_cast<std::uint64_t>(ms * rate / 1000.0);
    std::vector<std::vector<float>> out(streams);  // per stream: interleaved L,R,L,R...
    for (auto& o : out) o.reserve(total * 2);

    std::uint64_t done = 0;
    if (continuous_) {
        // A carry of another shape, stream count or rate is not this call's audio (setContinuousBlocks).
        if (carry_.shape != shape || carry_.id != id || carry_.rate != rate || carry_.streams.size() != streams)
            dropCarry();
        const std::uint64_t take = std::min<std::uint64_t>(carry_.frames, total);
        for (std::size_t i = 0; i < carry_.streams.size() && take > 0; ++i) {
            auto&      c = carry_.streams[i];
            const auto end = c.begin() + static_cast<std::ptrdiff_t>(take * 2);
            out[i].insert(out[i].end(), c.begin(), end);
            c.erase(c.begin(), end);
        }
        carry_.frames -= take;
        done = take;
    }
    if (done == total) return out;

    // One persistent L/R block buffer per stream, listed as the lanes a block renders into (L0, R0, L1, R1, ...).
    std::vector<std::vector<float>> lanes(streams * 2, std::vector<float>(kBlockSize));
    std::vector<float*>             ptrs(streams * 2);
    for (std::size_t k = 0; k < ptrs.size(); ++k) ptrs[k] = lanes[k].data();

    // The Engine owns transport + the ppq clock and consumes any host MIDI staged via stageMidiIn on the first block
    // it renders. Every carried frame was handed out before this point, so a block's frames follow the call's in time.
    while (done < total) {
        const std::uint64_t want = total - done;
        const auto frames =
            continuous_ ? kBlockSize : static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, want));
        block(frames, ptrs.data());
        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
        const auto give = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, want));
        for (std::size_t i = 0; i < streams; ++i) {
            const float* l = ptrs[i * 2];
            const float* r = ptrs[i * 2 + 1];
            for (std::uint32_t f = 0; f < give; ++f) {
                out[i].push_back(l[f]);  // interleave L,R,L,R…
                out[i].push_back(r[f]);
            }
        }
        if (give < frames) {
            // Continuous blocks only: the rest of the block is the next call's, if that call is of this shape.
            carry_.shape = shape;
            carry_.id = id;
            carry_.rate = rate;
            carry_.frames = frames - give;
            carry_.streams.assign(streams, {});
            for (std::size_t i = 0; i < streams; ++i) {
                carry_.streams[i].reserve(carry_.frames * 2);
                for (std::uint32_t f = give; f < frames; ++f) {
                    carry_.streams[i].push_back(ptrs[i * 2][f]);
                    carry_.streams[i].push_back(ptrs[i * 2 + 1][f]);
                }
            }
        }
        done += give;
    }
    return out;
}

namespace {

std::vector<rfl::Bytestring> toBytestrings(std::vector<std::vector<float>>&& streams) {
    std::vector<rfl::Bytestring> result;
    result.reserve(streams.size());
    for (const auto& s : streams) {
        const auto* p = reinterpret_cast<const std::byte*>(s.data());
        result.emplace_back(p, p + s.size() * sizeof(float));
    }
    return result;
}

} // namespace

rfl::Bytestring EngineRpcService::renderAudio(double ms) {
    // This thread calls processBlock itself, which only the Engine's owning thread may do.
    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};
    if (!rendersInto(2)) return {};
    auto out = pull(Shape::Mix, 0, 1, ms, [this](std::uint32_t frames, float* const* lanes) {
        engine_.processBlock(frames, lanes[0], lanes[1]);
    });
    const auto* p = reinterpret_cast<const std::byte*>(out[0].data());
    return rfl::Bytestring(p, p + out[0].size() * sizeof(float));
}

std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};  // as renderAudio
    const std::size_t n = engine_.systemCount();
    if (n == 0) return {};
    if (!rendersInto(2 * n)) return {};
    // The PerSystem shape routes each core into its own pair, so linked systems interleave exactly as they do in the
    // mixed path.
    return toBytestrings(pull(Shape::PerSystem, 0, n, ms, [this, n](std::uint32_t frames, float* const* lanes) {
        engine_.processBlock(frames, lanes, 2 * n);
    }));
}

std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerChannel(std::uint32_t id, double ms) {
    if (invoker_.audioThreadOwns()) return {};  // as renderAudio
    // Single-system only: the PerChannel shape fans one system's streams, so a second system's would collide
    // into these buffers. The mixed/per-system paths are the multi-system routes.
    if (ms <= 0.0 || engine_.systemCount() != 1) return {};
    SystemBase* sys = engine_.findSystem(id);
    if (sys == nullptr) return {};

    // The target's output streams (Game Boy = 4 stereo: Pulse 1/Pulse 2/Wave/Noise; default = 1 "Mix").
    const std::size_t n = sys->channelLayout().size();
    if (n == 0) return {};
    if (!rendersInto(2 * n)) return {};
    // The PerChannel shape routes each stream into its own pair.
    return toBytestrings(pull(Shape::PerChannel, id, n, ms, [this, n](std::uint32_t frames, float* const* lanes) {
        engine_.processBlock(frames, lanes, 2 * n);
    }));
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
    // A pad press waits in the staging hook's link, as a device's would, and the document's surface-in node drains it
    // at the next block. No hook is no control surface: the host answers so rather than staging into nothing.
    if (!controllerStaging_ || bytes.empty()) return false;
    return controllerStaging_(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

std::vector<std::string> EngineRpcService::drainProductCalls() {
    std::vector<std::string> out;
    if (productCalls_) out.swap(*productCalls_);  // a host with no record answers nothing, rather than keeping one
    return out;
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
    result.error = renderBlocksRefusal(req, controllerStaging_ != nullptr);
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
    // Blocks of the caller's own lengths: frames a continuous render carried would come after them, out of order.
    dropCarry();
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
        // The controller's own stream, into the staging hook (a pad press is staged as a device's arrival would be:
        // frame 0 of this block). A host with no hook has no control surface to stage it into.
        for (const auto& c : blk.controller)
            controllerStaging_(std::span<const std::uint8_t>(c.data(), c.size()));
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

std::string EngineRpcService::buildTarget() const { return rp::buildTarget(); }

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
