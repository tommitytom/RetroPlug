#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"
#include "host/script/ScriptHost.hpp"  // DspAllocStats / DspGcResult / DspTraceSpan (no quickjs types)

class Engine;
class QueuedInvoker;

// The emulator surface (lifecycle / reads / MIDI / transport / profiling) as a THIN RPC layer over
// (Engine + the one Invoker). No threading branches: every mutation just pushes onto
// the invoker (which flushes inline when quiescent, or hands to the audio thread when it owns the
// Engine); reads come from the snapshot registry, so they need no guard.
class EngineRpcService {
public:
    EngineRpcService(Engine& engine, QueuedInvoker& invoker);

    // --- emulator lifecycle / reads ---
    // (duplicate, reload and remove live in the TS SystemsStore over the resource calls and setGraph.)
    // Put an immutable byte resource in the engine's ResourceTable (docs/modular-graph-phase-c3-design.md section 4)
    // and answer its id, which a node's settings name as "res:<id>"; or, creating none, why. Its type is "rom",
    // "sram" or "state"; a file named by `path` is read here, so one that is absent or cannot be read is answered
    // before any document naming it is written, and an empty one only when its type is a rom: an empty seed file is a
    // blank battery, which is what a zero-byte `.sav` beside a ROM is.
    RpcPutResourceResult putResource(BackendResourceSpec spec);
    // The owner's drop of a resource putResource made: idempotent (false for an unknown or already dropped id), and it
    // never frees one a plan or a node instance still pins, which lasts until the plan that ran the instance is
    // reclaimed.
    bool                releaseResource(std::uint32_t id);
    // Plan a document TS built (docs/modular-graph-phase-b2-design.md section 2.5).
    RpcSetGraphResult           setGraph(RpcSetGraphRequest request);
    // Load a pack of script node types, or reload one loaded already (Engine::loadScriptPack,
    // docs/modular-graph-phase-c1-design.md section 7), while the audio thread runs blocks or not. Refused, changing
    // nothing, for a memory limit that is no whole number of bytes from 0 to 2^53 - 1 (BackendTypes.hpp).
    RpcLoadScriptPackResult     loadScriptPack(RpcLoadScriptPackRequest request);
    // A live change to a param of a node the last published plan places (Engine::setParam), at the start of the next
    // block: a system node's gain or a NES's expansionVolume, as the store sends an edit of either, with no publish.
    // False for a node or a param that plan does not have, a NaN, a param a wire drives, or a full command ring.
    bool setParam(std::string nodeId, std::string paramId, double value);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    std::optional<rfl::Bytestring> readRam(std::uint32_t id);   // work RAM (WRAM), per-block published copy
    // The WRITE counterpart of readRam, in the same coordinates. Queued like pressButton, so it lands
    // between blocks and is safe while the audio thread runs - unlike the debug facet's writeCpu.
    bool            writeRam(std::uint32_t id, std::uint32_t offset, rfl::Bytestring bytes);
    // Its twin aimed at the battery, at readSram's offsets (Engine::writeSram). A non-empty `expect` makes it a
    // compare-and-swap, judged only as it applies: a refusal there shows up as a republished battery, not as false.
    bool            writeSram(std::uint32_t id, std::uint32_t offset, rfl::Bytestring bytes, rfl::Bytestring expect);
    // A capability job for a node of the running graph (Engine::submitJob, docs/modular-graph-phase-c5-design.md
    // section 6): its id, whose completion a later drainJobs hands back, or why it was refused. readSram, readRam and
    // writeRam above are the system-id forms of three of these, on the same paths.
    RpcJobSubmitResult            submitJob(RpcJobRequest request);
    // The completions since the last drain (and the progress of jobs still running), which the control plane polls
    // from its tick (Engine::drainJobs).
    std::vector<RpcJobCompletion> drainJobs();
    bool screenshot(std::uint32_t id, std::string path);
    RpcFrame getFrame(std::uint32_t id);
    // An offline job (Engine::submitOfflineJob, docs/modular-graph-phase-d2-design.md section 3): its id, whose
    // completion a later drainOfflineJobs hands back, or why it was refused. Refused here too for an input that is not
    // exactly one kind (samples, channels and rate; or a path, with its window), samples that are not whole float32
    // values, and a cap arithmetic that is neither "float-ratio" nor "double-scale".
    RpcOfflineSubmitResult            submitOfflineJob(RpcOfflineRequest request);
    // An engine job (Engine::submitEngineJob, docs/modular-graph-phase-d3-design.md section 3) on the same pool: its
    // id, whose completion and progress later drainOfflineJobs calls hand back, or why it was refused.
    RpcOfflineSubmitResult            submitEngineJob(RpcEngineJobRequest request);
    // The completions since the last drain, oldest first, of both kinds, every running engine job's progress, and the
    // jobs still queued or running after them. `waitMs` above 0 waits up to that long (at most a second) for a
    // completion while a job is in flight: what a host with no UI tick does between pumps (src/offline.ts). A host that
    // pumps from its UI tick passes 0, and never blocks the thread its UI runs on.
    RpcOfflineDrain                   drainOfflineJobs(double waitMs);
    // Cancel a queued or running offline job of either kind; it completes with the reason "cancelled". False for one
    // that is neither.
    bool                              cancelOfflineJob(std::uint32_t id);

    // (Live-core debug/inspection — getApuState/readCpu/breakpoints/trace/profiler/… — moved to
    // DebugRpcService; only the CLI binds that facet.)

    // --- audio render / input drive / transport ---
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    // A control message aimed at ONE NODE of the running graph, by the document id it gives it: a settings SysEx to a
    // system's core ("sys-<id>") or to a cart's FIFO (its rp.n8.nes node). Queued through the invoker like every
    // per-system command, so it reaches the node whether an audio thread drains or the flush is inline, and the node
    // id resolves against the plan running when it drains. The contract is optimistic: refuse only what can never be
    // delivered, an empty message (a node the plan does not place takes nothing, silently).
    bool            stageNodeMidi(std::string nodeId, std::vector<std::uint8_t> bytes);
    // Continuous blocks (docs/modular-graph-phase-d3-design.md section 4), which only an engine job's render turns on
    // (RenderHost): renderAudio, renderAudioPerSystem and renderAudioPerChannel then run whole kBlockSize blocks and
    // nothing else. A call returns the frames it asks for; the rest of its last block waits for the next call, so the
    // engine's blocks fall where they would whatever lengths the calls ask for. Those carried frames belong to the
    // shape that rendered them (the mix, the per-system lanes, or one system's channels, with its stream count) and to
    // the rate: a call of another shape, or after the rate changed, drops them rather than hand one shape's lanes out
    // as another's or one rate's frames out at another, and so does renderBlocks, whose blocks come after them in
    // time. The emulation behind dropped frames is not undone: the next call's audio starts where the last block
    // ended. A block's host MIDI out is gathered whole (drainMidiOut), so out of a block whose tail is carried it can
    // be drained before that tail's audio is handed out; its frames stay block-relative. So can every other read of a
    // core between two calls (a render's end detection reads NR52 or risa's sequencer flag, and an smsggdj render reads
    // and writes its work RAM): it sees the state up to kBlockSize - 1 frames past the audio handed back. A render
    // assumes nothing else advances the engine between its calls: the audio driver's thread
    // (AudioDriverRpcService::startAudio) and the debug facet's stepping (stepInstruction, runUntilPc, runUntilBreak)
    // run the cores outside these calls and keep the carry, which the next call would then hand out ahead of audio from
    // after it; no render does either. Off, as every other host runs it, a call renders whole blocks and then one of
    // the remainder: the splits parity records. Not an RPC: the host that builds this service sets it before any script
    // runs.
    void            setContinuousBlocks(bool on);
    // The frames the last render call in continuous blocks left for the next one (none in exact blocks): for tests.
    std::size_t     carriedFrames() const { return carry_.frames; }
    rfl::Bytestring renderAudio(double ms);
    // Per-system audio: each live system's interleaved-stereo PCM, in slot order (marshals to
    // Uint8Array[]). Isolates each core so LSDj link-cable sync is provable (a follower's own RMS).
    std::vector<rfl::Bytestring> renderAudioPerSystem(double ms);
    // Per-channel audio for ONE system: each of its channelLayout() streams as its own interleaved-stereo
    // PCM (Game Boy = 4: Pulse 1/Pulse 2/Wave/Noise). Single-system only (empty if systemCount() != 1 or
    // `id` is unknown) - the PerChannel shape keys off the stream, not the slot. Marshals to Uint8Array[].
    std::vector<rfl::Bytestring> renderAudioPerChannel(std::uint32_t id, double ms);
    // The engine's audio sample rate (Hz), so callers can label WAV output correctly.
    double          sampleRate() const;
    // Set the host sample rate (Hz). Baked into each core at construct, so it only takes effect BEFORE any
    // system is built — rejected (false) once a system exists, since there's no resample-on-change today.
    bool            setSampleRate(double sr);
    bool            setTransport(bool running);
    bool            setPpq(double ppq);
    bool            setBpm(double bpm);
    // Host MIDI for the next block rendered, at `frame` inside it (any order, as a plugin stages a block: its events,
    // then each moved automation slot's CC at frame 0). False, nothing staged, for an empty message or a frame of
    // kBlockSize or more. The frame lands in renderAudio's next block when it renders at least kBlockSize frames (its
    // first block is the lesser of kBlockSize and the whole render); a block of `frame` frames or fewer, a renderBlocks
    // one included, is the caller's to avoid. The frame arrives as rpcpp reads any integer, truncated to its low 32 bits
    // (1.5 as 1, NaN as 0), so only the TS driver (audioDriver.ts) can refuse a frame that is no integer in range.
    bool            stageMidiIn(std::vector<std::uint8_t> bytes, std::uint32_t frame);
    // The control-surface twin: staged as `controllerIn` rather than musical MIDI, so a headless test
    // can drive a Launchpad app without a Launchpad. Harness-facet only, like stageMidiIn. It waits in the staging hook
    // a host set (setControllerStaging), whose link the document's control-surface input node drains at its next block
    // as it would a real device's input; false, staging nothing, when this host set no hook. A plan places that node by
    // its controller setting, and the node discards what the link held when it bound it (a press staged while no plan
    // placed the surface, including one staged for block 0 of the run that follows the publish), so a message staged
    // against no surface node is dropped rather than delivered.
    bool            stageControllerIn(std::vector<std::uint8_t> bytes);
    // TEST / HARNESS: where a headless host stages control-surface messages (the test host's HarnessSurfaceLink's
    // queueInput, from the launchpad pack). The backend names no link type: a host that owns a real surface installs
    // its link on the Engine (installLink) and leaves this unset, and then has no surface to stage into: the hook
    // answers false and a block carrying controller messages is refused.
    void            setControllerStaging(std::function<bool(std::span<const std::uint8_t>)> staging) {
        controllerStaging_ = std::move(staging);
    }
    // TEST / HARNESS: what the product's rp_device_host was told (rp::host::ProviderHost::productCalls), drained so a
    // headless test can hold the facet's claim, its scan's held edge and the startup rendezvous to what reached the
    // product: "reserve_midi:<input>|<output>", "suspend_hardware_inputs:1" or "restored", in order. Empty on a product
    // that implements the calls itself (the standalone does, over its shared MIDI stream).
    void                          setProductCalls(std::vector<std::string>* calls) { productCalls_ = calls; }
    std::vector<std::string>      drainProductCalls();
    // Drain the host MIDI out (LSDj's serial-out decoders, a controller's launches), accumulated across renderAudio
    // blocks since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
    std::vector<RpcMidiOut> drainMidiOut();
    // Render a scripted run of host blocks the way PluginDSP::run renders each one: stage the block's MIDI
    // at its frames, drain the command ring, apply its transport, processBlock(frames, lanes, outputs),
    // then take and clear the host MIDI out. Quiescent only. The parity suite's capture call.
    // ppq (a locate) and controller have no PluginDSP::run counterpart (the plugin never calls setPpq, which
    // would double-count; controller traffic is the SDL standalone's path), and the plugin's CC-automation
    // stage and MIDI-out size filter are not modelled.
    RpcRenderBlocksResult   renderBlocks(RpcRenderBlocksRequest req);
    // The target this binary was built for, "<os>-<arch>-<compiler><major>" (linux-x64-gnu13): which
    // parity baseline its float output can be compared against. MSVC reports the full _MSC_VER (msvc1951).
    std::string             buildTarget() const;
    // Messages the graph's full output queues have dropped so far (Engine::droppedMessages), so a test can hold a
    // run to none. Never reset: compare two reads.
    std::uint32_t           droppedMessages() const;

    // --- profiling the audio runtimes (spec/08-profiling.md): the script host's allocation window (every pack's audio
    // runtime, summed), GC pass (a collection of each) and trace window, reached directly through the Engine (the
    // quiescent renderAudio pull path owns it: the benchmark never starts the audio thread). dspResetAllocStats and
    // dspTraceReset return false, dspRunGc enabled false and the trace reads empty when refused (the audio thread
    // running) or on a host that is not a profile build; dspAllocStats never refuses, and is enabled false only
    // off-profile ---
    rp::host::DspAllocStats             dspAllocStats();
    bool                                dspResetAllocStats(bool disableAutoGc);
    rp::host::DspGcResult               dspRunGc();
    bool                                dspTraceReset(bool arm);
    std::vector<rp::host::DspTraceSpan> dspTrace();
    std::vector<std::string>            dspTraceNames();

private:
    // Which render call a carried block came from (setContinuousBlocks): a carry is handed only to a call of its shape.
    enum class Shape : std::uint8_t { None, Mix, PerSystem, PerChannel };
    // One block of `frames` into the call's lanes, each stream's L then R (L0, R0, L1, R1, ...), of kBlockSize floats.
    using BlockFn = std::function<void(std::uint32_t frames, float* const* lanes)>;
    // The frames `ms` asks for at the engine's rate, in `streams` interleaved-stereo outputs (L,R,L,R...), rendered by
    // `block`: in exact splits, or in whole blocks with the carry setContinuousBlocks describes. `id` is the system a
    // PerChannel call fans, 0 for the others.
    std::vector<std::vector<float>> pull(Shape shape, std::uint32_t id, std::size_t streams, double ms,
                                         const BlockFn& block);
    void dropCarry();
    bool rendersInto(std::size_t channels) const;
    // Append the block's host MIDI out (Engine::midiOut(), cleared each block) to accumMidiOut_.
    // Called after each processBlock in the render loops so drainMidiOut() sees the whole window.
    void accumulateMidiOut();

    Engine&        engine_;
    QueuedInvoker& invoker_;   // the one mutation path (push; flushes inline when quiescent)

    static constexpr std::uint32_t kBlockSize = 1024;
    // The frames past the last call's in its last block, in continuous blocks; empty otherwise.
    struct Carry {
        Shape                           shape = Shape::None;
        std::uint32_t                   id = 0;
        double                          rate = 0.0;
        std::size_t                     frames = 0;
        std::vector<std::vector<float>> streams;  // per stream: interleaved L,R... not yet handed out
    };
    bool                     continuous_ = false;
    Carry                    carry_;
    std::vector<RpcMidiOut> accumMidiOut_;  // host MIDI out gathered across a render window (drainMidiOut)
    std::function<bool(std::span<const std::uint8_t>)> controllerStaging_;  // a headless host's surface, if it set one
    std::vector<std::string>*     productCalls_ = nullptr;    // the product host's record, on a host that keeps one
};
