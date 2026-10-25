#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "system/SystemTypes.hpp"

#include "host/dsp/DspRuntime.hpp"
#include "host/engine/HostSyncTrace.hpp"
#include "host/engine/SnapshotRegistry.hpp"

class SystemBase;
class MesenNesSystem;
struct DspCommand;
namespace rp::graph { struct HostCommand; }
namespace rp::host { struct SystemTable; }

// A per-system config field a control-plane edit applies to the live core (SameBoy today). Carried
// as a double across the command ring: gain in dB; a bool as 0/1; an enum/int as its integer value.
enum class ConfigField : std::uint8_t {
    Gain = 0, ReloadOnRomChange = 1,
    Model = 2, Highpass = 3, LinkGroup = 4, FastBoot = 5,     // SameBoy
    NesRegion = 6, NesRemoveSpriteLimit = 7,                  // Mesen (NES)
    SerialOutCapture = 8,                                     // SameBoy (LSDj MI.OUT)
    NesApuLatencyMs = 9,                                      // Mesen (NES) — APU flush window as latency (ms)
    // SameBoy display group — all live (they land on the next rendered frame), all routed through
    // SameBoySystem::applyDisplayConfig.
    ColorCorrection = 10, DmgPalette = 11, LightTemperature = 12,
    // Mesen (NES) cartridge-accuracy switches — 0 = chip, 1 = Everdrive N8. Both live.
    NesS5bNoise = 13, NesMmc5PhaseReset = 14,
    // Mesen (NES) — the cartridge sound chip's level, percent of unity. Live.
    NesExpansionVolume = 15,
};

// One system's video frame, read from its lock-free FrameBufferTriple. `data` is raw XRGB8888
// (little-endian B,G,R,X), width*height*4 bytes — the LVGL Canvas's native format, so no transcode.
// `published` is false (and `data` empty) until the core has rendered its first frame.
struct EngineFrame {
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    bool                      published = false;
    std::vector<std::uint8_t> data;
};

// The core of the host: the graph the systems run on (docs/modular-graph-phase-b1-design.md), the DSP role
// kernel, the staged MIDI and the snapshot registry. Structure reaches it as graph documents TS builds
// (docs/modular-graph-phase-b2-design.md), planned on the control thread; every other edit is a command on
// the GraphHost's one ring, applied at the next drain. A removed core is freed on the control thread, with
// the plan that last ran it. Transport and ppq are plain members, set per block by whoever drives it; the
// only atomics count the blocks too long to run, which the audio thread may not log.
class Engine {
public:
    // The block size the plan starts at, the matrix's largest (B0 section 4). A render never grows it, so
    // its blocks must fit; a host grows it to its own maximum with setMaxBlockFrames.
    static constexpr std::uint32_t kDefaultMaxBlockFrames = 8192;

    explicit Engine(double sampleRate = 44100.0);
    ~Engine();

    double sampleRate() const { return sampleRate_; }

    // --- the running plan's systems: the block thread's view, or the control thread's while no audio
    // thread runs (structure itself arrives through setGraph) ---
    std::size_t systemCount() const;
    SystemBase* findSystem(SystemId id);   // borrowed (non-owning)

    // --- DSP kernel ---
    bool loadKernel(const std::vector<std::uint8_t>& bytecode);   // sets the per-block DSP stage active
    bool setSystems(const std::vector<std::uint8_t>& json);
    void stageMidi(std::uint32_t frame, std::vector<std::uint8_t> bytes);  // delivered on the next processBlock, at intra-block `frame`
    void stageMidi(std::vector<std::uint8_t> bytes) { stageMidi(0, std::move(bytes)); }  // frame-0 convenience (RPC/harness path)

    // Stage traffic from a CONTROL SURFACE (a Launchpad), delivered on the next processBlock as the kernel's
    // `controllerIn` - a separate stream from stageMidi, and separate for a reason: a pad press is a NoteOn,
    // and a translator like LSDj's MidiMap reads a NoteOn as a row launch. A surface sharing the musical
    // stream would fire every launch twice, once through the controller app and once raw.
    void stageControllerMidi(std::vector<std::uint8_t> bytes);

    // Whether a control surface is attached. Plain member, like transport: the host sets it each block from
    // its device link, and the kernel's controller role takes the device on the false->true edge.
    void setControllerConnected(bool connected) { controllerConnected_ = connected; }

    // --- transport (plain members; mutated only by the Engine's owning thread) ---
    void setBpm(double bpm);
    void setTransport(bool playing);
    // Move the host playhead. A DAW does this on every locate: stop-and-rewind, a loop wrap,
    // or a click in the timeline. Roles see it as a ppqStart discontinuity in the next block.
    void setPpq(double ppq);

    // Host sample rate. Used at construct for new cores (findable via sampleRate()) AND pushed to every
    // already-live core, the running plan's and the parked ones, so a host sample-rate change re-rates them
    // in place (SameBoy GB_set_sample_rate, Mesen resampler). Safe to call with cores live only when the
    // audio thread isn't draining: the plugin calls it from DPF's sampleRateChanged, which fires only while
    // deactivated; the CLI, before any build.
    void setSampleRate(double sr);

    // The kernel's host-MIDI-out sink for this block (filled by processBlock's DSP stage, cleared at
    // the top of the next). The plugin drains this to the DAW after processBlock; nothing else reads it.
    const std::vector<DspRuntime::MidiOut>& midiOut() const { return dsp_.midiOut_; }
    void clearMidiOut() { dsp_.midiOut_.clear(); }

    // The kernel's CONTROL-SURFACE sink for this block (LED traffic). Not system-addressed - the device is a
    // project-scope peripheral, not something a system owns. The standalone drains this to the Launchpad
    // link; every other host leaves it empty because nothing binds a controller.
    const std::vector<std::vector<std::uint8_t>>& controllerOut() const { return dsp_.controllerOut_; }
    void clearControllerOut() { dsp_.controllerOut_.clear(); }

    // An optional observer of the raw core-bytes sink (a tracker's host-sync protocol), invoked once
    // per message on the audio thread at the point those bytes fan to the addressed core. The standalone
    // sets it to MIRROR the generated risa sync stream to a physical Everdrive N8 (via N8Link), so the
    // real cart stays in lock-step with the emulated core. Null everywhere else (plugin / CLI / tests),
    // where it is a no-op. `frame` is the intra-block sample offset; `flush` is the arm-barrier flag.
    void setCoreByteSink(std::function<void(std::uint32_t frame, const std::uint8_t* data,
                                            std::size_t size, bool flush)> sink) {
        coreByteSink_ = std::move(sink);
    }

    // --- DSP-runtime allocation/GC profiling (spec/08-profiling.md) ---
    // Forward to the bare JS runtime's counters. Valid only on the Engine's owning thread (the
    // renderAudio pull path); no-op / enabled=false in a non-RETROPLUG_PROFILE build.
    DspAllocStats dspAllocStats() const { return dsp_.allocStats(); }
    void          resetDspAllocStats(bool disableAutoGc) { dsp_.resetAllocStats(disableAutoGc); }
    DspGcResult   dspRunGc() { return dsp_.runGc(); }

    // --- per-role runtime tracing (spec/08-profiling.md Tier B) ---
    void                      dspTraceReset(bool arm) { dsp_.traceReset(arm); }
    std::vector<DspTraceSpan> dspTrace() const { return dsp_.traceSpans(); }
    std::vector<std::string>  dspTraceNames() const { return dsp_.traceNames(); }

    // --- per block: the plan (the kernel stage, every system, the mixing, the host outputs), then the
    // MI.OUT collection, the snapshot publish and the ppq advance. `outputs` is a flat array of
    // `numOutputs` planar channels laid out as the graph's host shape says (the plugin's 8, a render's
    // pairs); the caller has drained the ring first. ---
    void processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs);
    void processBlock(std::uint32_t frames, float* outL, float* outR);

    // --- live-state reads (valid only on the Engine's owning thread; a mutation arrives as a command) ---
    std::optional<std::vector<std::uint8_t>> readState(SystemId id);
    std::optional<std::vector<std::uint8_t>> readSram(SystemId id);
    // Work RAM (WRAM), from the registry's per-block published copy — safe while the audio thread runs,
    // and fresh EVERY block (unlike readState/readSram), so a runtime overlay tracks per-frame state.
    std::optional<std::vector<std::uint8_t>> readRam(SystemId id);
    /** The published WRAM length, for a bounds check that costs no copy. 0 = no writable RAM. */
    std::size_t ramSize(SystemId id);
    bool screenshot(SystemId id, const std::string& path);
    // The system's latest video frame (raw XRGB8888). Reads the concurrent FrameBufferTriple, so it is
    // safe while the audio thread writes; width/height are 0 (published false) for an unknown system.
    EngineFrame getFrame(SystemId id);

    // The owned snapshot store the control plane reads through. Claim a slot at build, release it
    // when the core is deleted; processBlock publishes into it.
    SnapshotRegistry& registry() { return registry_; }

    // --- the ring: QueuedInvoker pushes every command onto it ---
    bool pushCommand(const DspCommand& c);          // control thread: false when the ring is full
    void drainCommands();                           // the block thread (or the control thread, quiescent)
    void setAudioThreadOwns(bool owns);
    // Control thread. Also where the blocks processBlock refused as too long are reported, once per new one.
    std::size_t reclaim();
    void discardPending();                          // teardown, the audio thread stopped
    // Grow the plan's block size to a host's maximum (never below kDefaultMaxBlockFrames). Quiescent.
    void setMaxBlockFrames(std::uint32_t frames);

    // --- graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5), control thread ---
    // Park a built core under its id until the system node a document names it in takes it, claiming its
    // snapshot slot (seeded from the core, so a read right after the build works with no block run). False,
    // with the core freed and the registry untouched, when the id is 0 or a core under it is still live
    // (parked, or held by a node of any plan); false, with the core freed, when the snapshot pool is full.
    bool parkCore(std::unique_ptr<SystemBase> core);
    bool coreLive(SystemId id) const { return liveCores_.count(id) != 0; }
    struct GraphRequest {
        std::string           document;  // a graph document, as toGraphJson writes it
        std::vector<SystemId> slots;     // the document's systems, in slot order
        bool                  rearmTap = false;  // carry a SyncTap in the swap: restore, then arm if `arm`
        bool                  arm = false;
        SystemId              armId = 0;
        std::uint32_t         armMode = 0;
    };
    struct GraphError {
        std::string nodeId, port, code, message;
    };
    struct GraphResult {
        bool                    ok = false;
        std::vector<GraphError> errors;
    };
    // Plan `request` and publish it with its system table (built from the slots and the live cores) and its
    // SyncTap. Refused before planning when a system node's type is not its core's, or the tap arms a mode
    // other than 1 to 3 or a system not in the slots. Refused at all, every core stays where it was: one a
    // node of the refused plan took goes back to parked. Once it has published, a core still parked is one
    // no document placed, and is freed.
    GraphResult setGraph(const GraphRequest& request);
    // The host channels the last published plan writes (the sum of its rp.host.audio-out buses). Control thread.
    std::uint32_t hostWidth() const { return hostWidth_; }

private:
    struct Graph;
    // The running plan's systems (an empty table before the first plan), for everything but the host-command
    // handler: the kernel stage and the tail on the block thread, and the control thread's readers. The
    // handler is handed its plan's table instead, and threads that through.
    const rp::host::SystemTable& currentTable() const;
    void runKernelStage(std::uint32_t frames);     // rp.dsp.kernel's body
    void applyCommand(DspCommand& c, const rp::host::SystemTable& table);
    void syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table);
    SystemBase* takeCore(std::uint64_t id);
    static const void* graphInterface(void* ctx, const char* id);
    static void onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const void* hostData, bool discard);

    // --- the per-system commands, applied by applyCommand to the core its table names (false, or nothing,
    // when it names none) ---
    // Hand `len` MIDI bytes straight to ONE system's ingress, skipping the routing kernel. For CONTROL
    // traffic the UI aims at a specific cart (a settings knob), which must not depend on how the project
    // routes musical MIDI. See DspCommand::stageSystemMidi.
    bool stageSystemMidi(SystemBase* sys, const std::uint8_t* bytes, std::size_t len);
    // Poke bytes into a system's work RAM. `offset` indexes the SAME region readRam returns, so
    // read-modify-write round-trips without a coordinate conversion. Applied between blocks via the
    // invoker, never from the calling thread - which is what separates it from the debug facet's writeCpu
    // (a live-core write, valid only when the audio thread is stopped).
    //
    // Deliberately unguarded beyond bounds: writing a running core's RAM CAN confuse it, because the
    // emulated program has its own invariants over those bytes. That is the point of the feature.
    bool writeRam(SystemBase* sys, std::uint32_t offset, const std::vector<std::uint8_t>& bytes);
    // Apply a live config edit to a system. Value-guarded per field so a whole-config re-send only acts on
    // what changed (no spurious model restart).
    void applyConfigField(SystemBase* sys, std::uint8_t field, double value);

    // The NES tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before
    // every re-resolve so leaving a split mode (or gaining a second system) puts the core back exactly as
    // found. Scoped to what WE armed on purpose: the CLI / RenderHost set channelExportMode at construct
    // through the mesen role blob and never select a split routing, so they are never disturbed. `core`
    // is compared, never followed: a fresh core replaced in under the same id is not the one armed.
    struct ArmedTap {
        bool                  active   = false;
        SystemId              id       = 0;
        const MesenNesSystem* core     = nullptr;
        std::uint32_t         prevMode = 0;
    };
    ArmedTap armedTap_;

    SnapshotRegistry registry_;
    DspRuntime dsp_;
    double     sampleRate_;
    bool       dspActive_ = false;                 // a kernel is loaded → run the per-block DSP stage
    std::vector<DspRuntime::MidiIn> pendingMidi_;  // staged host MIDI, consumed on the next block
    std::vector<DspRuntime::MidiIn> pendingControllerMidi_;  // staged control-surface traffic, likewise
    // Raw serial-out bytes each core emitted LAST block (LSDj MI.OUT), gathered after the plan runs and fed
    // to the kernel on the next block (one-block latency: the kernel runs first, and each SameBoy clears
    // its serialOutLog_ in prepareForBlock).
    std::vector<DspRuntime::SerialOut> pendingSerialOut_;

    // Simulated host transport, driven per block. Continuous ppq clock (the pull path and the audio
    // loop share it; they never run concurrently). Plain — mutated only by the owning thread.
    double bpm_       = 120.0;
    bool   transport_ = false;
    double ppq_       = 0.0;
    bool   controllerConnected_ = false;  // a control surface is bound (set per block by the host)
    bool   blockTracing_ = false;         // this block's syncTrace_.beginBlock answer, for the kernel stage
    HostSyncTrace syncTrace_;  // inert unless RETROPLUG_SYNC_TRACE is set
    // Optional external mirror of the core-bytes stream (set by the standalone to reach a physical N8);
    // null in every other host. Called on the audio thread, so the target must be RT-safe (N8Link is).
    std::function<void(std::uint32_t, const std::uint8_t*, std::size_t, bool)> coreByteSink_;

    // Blocks processBlock refused as longer than the plan's buffers, and the length of the last one: counted
    // on the block thread, reported by reclaim() on the control thread (oversizedReported_ is its side).
    std::atomic<std::uint32_t> oversizedBlocks_{0};
    std::atomic<std::uint32_t> oversizedFrames_{0};
    std::uint32_t              oversizedReported_ = 0;

    // Cores buildCore built, waiting for the node a document names them in (control thread), and every core
    // alive anywhere by id: parked, or owned by a system node, until that node frees it.
    std::map<SystemId, std::unique_ptr<SystemBase>> parked_;
    std::map<SystemId, SystemBase*>                 liveCores_;
    std::uint32_t                                   hostWidth_ = 2;
    // While setGraph plans (inSetGraph_), the ids takeCore hands out of parked_: a node of that plan which
    // gives its core up again, the plan refused, puts it back in parked_ rather than freeing it.
    bool                                            inSetGraph_ = false;
    std::set<SystemId>                              takenInSetGraph_;

    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
    // liveCores_) as they go.
    std::unique_ptr<Graph> graph_;
};
