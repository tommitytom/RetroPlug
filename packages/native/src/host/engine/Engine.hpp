#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "system/SystemTypes.hpp"

#include "graph/rp_node.h"  // rp_queue
#include "host/engine/HostSyncTrace.hpp"
#include "host/engine/SnapshotRegistry.hpp"
#include "host/script/ScriptHost.hpp"

class SystemBase;
class MesenNesSystem;
struct DspCommand;
namespace rp::graph { struct HostCommand; }
namespace rp::host { struct SystemTable; }

// One message of the host MIDI out (Engine::midiOut), tagged with the system that emitted it.
struct EngineMidiOut {
    std::uint32_t             system = 0;
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
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

// The core of the host: the graph the systems run on (docs/modular-graph-phase-b1-design.md), the script host its TS
// nodes run in, the MIDI routing, the controller and each role's protocol node among them
// (docs/modular-graph-phase-b3b-design.md section 2), and the types of any script node pack loaded beside them
// (docs/modular-graph-phase-c1-design.md), the staged MIDI and the snapshot registry. Structure reaches it
// as graph documents TS builds (docs/modular-graph-phase-b2-design.md), planned on the control thread; every other
// edit is a command on the GraphHost's one ring, applied at the next drain. A removed core is freed on the control
// thread, with the plan that last ran it. Transport and ppq are plain members, set per block by whoever drives it; its
// own atomics count what the audio thread may not log: the blocks too long to run.
class Engine {
public:
    // The block size the plan starts at, the matrix's largest (B0 section 4). A render never grows it, so
    // its blocks must fit; a host grows it to its own maximum with setMaxBlockFrames.
    static constexpr std::uint32_t kDefaultMaxBlockFrames = 8192;

    explicit Engine(double sampleRate = 44100.0);
    // With the script nodes of `scriptBundle` (its chunks, run in order) in place of the embedded bundle's: a test's
    // toy nodes.
    Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle);
    ~Engine();

    double sampleRate() const { return sampleRate_; }

    // --- the running plan's systems: the block thread's view, or the control thread's while no audio
    // thread runs (structure itself arrives through setGraph) ---
    std::size_t systemCount() const;
    SystemBase* findSystem(SystemId id);   // borrowed (non-owning)

    // --- host input, taken by the plan's rp.host.midi-in / controller-in at the top of the next block (dropped at
    // its end by a plan with neither). The bytes are copied into the engine's staging, which allocates nothing while a
    // block stages no more than those nodes carry (HostIoNodes.hpp) ---
    // Delivered on the next processBlock, at intra-block `frame`.
    void stageMidi(std::uint32_t frame, std::span<const std::uint8_t> bytes);
    void stageMidi(std::uint32_t frame, const std::vector<std::uint8_t>& bytes) {
        stageMidi(frame, std::span<const std::uint8_t>(bytes));
    }

    // Stage traffic from a CONTROL SURFACE (a Launchpad), delivered on the next processBlock by the plan's
    // rp.host.controller-in to the controller node's `controllerIn` - a separate stream from stageMidi, and separate
    // for a reason: a pad press is a NoteOn, and a translator like LSDj's MidiMap reads a NoteOn as a row launch. A
    // surface sharing the musical stream would fire every launch twice, once through the controller app and once raw.
    void stageControllerMidi(std::span<const std::uint8_t> bytes);
    void stageControllerMidi(const std::vector<std::uint8_t>& bytes) {
        stageControllerMidi(std::span<const std::uint8_t>(bytes));
    }

    // Whether a control surface is attached. Plain member, like transport: the host sets it each block from
    // its device link, and the controller node takes the device on the false->true edge.
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

    // The host MIDI out of this block: what the plan's rp.host.midi-out appended, each tagged with the system that
    // emitted it. Frame-sorted, each frame inside the block: a plan holds one rp.host.midi-out at most (setGraph
    // refuses a second), which merges its inputs by frame, sends nothing earlier than the last frame it handed out and
    // nothing past the block's last frame, and the plugin relies on that to write the events to a DAW that requires
    // them in order (CLAP, LV2) and takes each frame for a place in the block. Cleared at the top of each block that
    // runs: a 0-frame block, or one longer than the plan's buffers, runs nothing and keeps the previous contents. The
    // plugin drains this to the DAW after processBlock, and the SDL standalone to its MIDI out; the RPC drains
    // (drainMidiOut, the harness's) read it too.
    const std::vector<EngineMidiOut>& midiOut() const { return midiOut_; }
    void clearMidiOut() { midiOut_.clear(); }

    // The CONTROL-SURFACE out of this block (LED traffic), from the plan's rp.host.controller-out, likewise. Not
    // system-addressed - the device is a project-scope peripheral, not something a system owns. The standalone
    // drains this to the Launchpad link; every other host leaves it empty because nothing binds a controller.
    const std::vector<std::vector<std::uint8_t>>& controllerOut() const { return controllerOut_; }
    void clearControllerOut() { controllerOut_.clear(); }

    // The physical-N8 mirror: invoked on the audio thread, once per message, with what the cart system
    // (cartMirrorSystemId, below) is fed - its protocol nodes' MIDI and bytes, through the plan's rp.n8.cart-mirror,
    // and any message staged at it - so the real cart stays in lock-step with the emulated core. The plugin and the
    // standalone set it (to N8Link); null in the CLI / tests, where there is no cart. `frame` is the intra-block
    // sample offset; `flush` is the arm-barrier flag.
    void setCoreByteSink(std::function<void(std::uint32_t frame, const std::uint8_t* data,
                                            std::size_t size, bool flush)> sink) {
        coreByteSink_ = std::move(sink);
    }

    // The emulated system a connected physical N8 MIRRORS (0 = none): the one the running plan's rp.n8.cart-mirror
    // names, which TS wires from the first NES in slot order (docs/modular-graph-phase-b3a-design.md section 3.5).
    // The cart then gets exactly what that system's emulated FIFO gets from its protocol nodes - its MIDI after the
    // project's routing (which picks the messages it gets and may rewrite their channel) and its generated bytes
    // (risa's host sync) - and nothing from any other system. The hosts stop forwarding raw DAW MIDI to the cart while
    // this is non-zero (it would arrive twice, the second copy unrouted); with no NES system they fall back to that raw
    // forward. The block thread, or the control thread while no audio thread runs (it reads the running plan's system
    // table, as findSystem does).
    SystemId cartMirrorSystemId() const;

    // --- profiling the audio runtimes (spec/08-profiling.md): the script host's allocation window (every pack's audio
    // runtime, summed), its GC pass (a collection of each) and its trace window, which holds the engine's own stages
    // too (the plan's whole run, its script nodes' spans inside it; the state publish). Quiescent (the renderAudio pull
    // path): while the audio thread runs, resetDspAllocStats and dspTraceReset return false, dspRunGc a result with
    // enabled false, and dspTrace and dspTraceNames nothing, as they do in any build but a RETROPLUG_PROFILE one;
    // dspAllocStats never refuses, and is enabled false only off-profile ---
    rp::host::DspAllocStats             dspAllocStats() const;
    bool                                resetDspAllocStats(bool disableAutoGc);
    rp::host::DspGcResult               dspRunGc();
    bool                                dspTraceReset(bool arm);
    std::vector<rp::host::DspTraceSpan> dspTrace() const;
    std::vector<std::string>            dspTraceNames() const;

    // --- per block: the plan (the host inputs, the script nodes, every system, the mixing, the host outputs), then the
    // snapshot publish and the ppq advance. `outputs` is a flat array of
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

    // --- resources (docs/modular-graph-phase-c3-design.md section 4), control thread ---
    // An immutable byte resource in the engine's ResourceTable, which a node's settings name as "res:<id>" and a
    // document binds to a resource input. Returns 0, with the reason in *error, for a type the table refuses (it takes
    // any plain id; the RPC admits "rom", "sram" and "state", the ones a system is built from). Ids count up from 1
    // and are never reused.
    std::uint32_t putResource(const std::string& type, std::vector<std::uint8_t> bytes, std::string* error = nullptr);
    // The owner's drop: idempotent, and it never frees a resource a plan still pins (that happens when the plan is
    // reclaimed). False for an unknown or already dropped id.
    bool releaseResource(std::uint32_t id);
    // The resource a node may name while its owner still holds it, or null. Control thread: a node pins what it
    // reads, so nothing on the audio side needs this.
    const rp_resource* resource(std::uint32_t id) const;
    // How many pins it holds (one per instance whose settings name it, and one per plan binding), or 0 for an
    // unknown or dropped id that no plan holds.
    std::uint32_t resourcePinCount(std::uint32_t id) const;

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
    // SyncTap. Refused before planning when a system node's type is not its core's, the tap arms a mode other
    // than 1 to 3 or a system not in the slots, the document holds a second rp.host.midi-in, rp.host.controller-in,
    // rp.host.midi-out or rp.n8.cart-mirror, or its mirror names anything but one of its NES systems. Refused at all,
    // every core stays where it was: one a node of the refused plan took goes back to parked. Once it has published, a
    // core still parked is one no document placed, and is freed, and the request is the one a script pack's reload
    // replans (loadScriptPack).
    GraphResult setGraph(const GraphRequest& request);
    // A live change to a param of a node the last published plan places (GraphHost::setParam): clamped to the param's
    // range (and rounded for an int, enum or bool), and applied at the start of the next block, flushed inline while
    // no audio thread runs, on the command ring like every other edit. False for a node or a param that plan does not
    // have (a node the next publish adds takes its value from the document), a NaN value, a param a wire drives, or a
    // full ring. Control thread.
    bool setParam(std::string_view nodeId, std::string_view paramId, double value);
    // The host channels the last published plan writes (the sum of its rp.host.audio-out buses). Control thread.
    std::uint32_t hostWidth() const { return hostWidth_; }
    // Nodes faulted so far, each counted once and bypassed from then on (GraphHost::faultCount). An atomic the
    // block thread bumps relaxed: any thread may read it, a control-thread read may lag a running audio thread,
    // and on a quiescent engine, whose blocks run on the caller's thread, it is exact once processBlock returns.
    std::uint32_t faultCount() const;
    // Messages dropped so far by the plan's full output queues (GraphHost::droppedMessages), each counted by the block
    // thread at the end of its block. Any thread may read it, as faultCount. Wraps, as the graph's does.
    std::uint32_t droppedMessages() const;
    // Throws caught in the script nodes so far (ScriptHost::throwCount): any thread, as faultCount.
    std::uint32_t scriptThrows() const;

    // --- script node packs (docs/modular-graph-phase-c1-design.md section 7), control thread ---
    struct ScriptPackType {
        std::string id;
        std::string descriptor;  // as the registry holds it: canonical JSON, every field spelled
    };
    struct ScriptPackResult {
        bool                        ok = false;
        std::string                 error;   // why not, when not
        std::vector<GraphError>     errors;  // the planner's, when a replan refused (a load makes no plan: none)
        std::vector<ScriptPackType> types;   // the pack's, in registration order
    };
    // Load a pack of script node types named `id`: `source` is global code (the IIFE a bundler makes, appending its
    // defineNode results to __rp_scriptNodes as the built-in bundle does: packages/retroplug/src/script/register.ts),
    // compiled here and run in runtimes of the pack's own (ScriptHost::loadPack), each held to `memoryLimit` bytes
    // (0: ScriptHost::kDefaultMemoryLimit); then its types join the registry, so a document can place them from the
    // next setGraph on. Callable while an audio thread runs blocks: the pack is built here, and the block thread
    // meets it only in a plan that places one of its nodes. Refused with the reason in `error`, the types a document
    // can name and the running plan as they were (the reclaim it makes first may still free retired generations, their
    // retired entries dropped, as a refused setGraph's can): an id ScriptHost::canLoadPack refuses (checked first,
    // before the source is compiled: one of the app's, "rp" and under, among them), a compile error (its line), a pack
    // ScriptHost::loadPack refuses (a definition it cannot read, a type id outside the pack's namespace, a load past
    // the call deadline or the memory limit), or one the registry refuses (a descriptor it cannot use, a type id
    // another pack holds already). A refusal once the source compiles (ScriptHost::loadPack's own included) still uses
    // up a generation number (the id's count of loads: ScriptHost::loadPack).
    // An id the script host holds a current generation of is reloaded (docs/modular-graph-phase-c1-design.md section
    // 5), all or nothing: its next generation is built as a load builds a pack, the registry swaps the pack's entries
    // for its, and the request setGraph last published is planned again without re-arming the tap (the running plan's
    // stays as it is), so every node of the pack is rebuilt on the new code, its JS state not carried over, while every
    // other node keeps its instance; a core parked since that request stays parked. When the replan is refused (a
    // node's settings the new code refuses, a type the document places gone, the ring busy) the registry is swapped
    // back and the planner's errors are returned in `errors`, summed up in `error`: the old code runs on, its instances
    // untouched. The new generation is discarded: freed within the call when no audio thread runs. Under one, if the
    // refused plan made fresh instances of it (the ring busy, or a later node of the pack whose create refuses, its
    // settings check run again: at its control runtime's memory limit, say), their releases go to the block thread, so
    // it stays in scriptPacks(), discarded, until those records come back, and the reclaim that finds them back frees
    // it; else (a settings refusal or a type gone is refused before anything is created) it too is freed within the
    // call. On success `types` are the new generation's, and the old one is retired, freed with its runtimes, its
    // registry entries dropped with it, once its nodes' records come back: within the call when no audio thread runs.
    // Under one, they come back once a reclaim has freed the plan that held those nodes, queuing their releases to the
    // block thread, and a block has released them; the generation goes at the host's next reclaim after that, which is
    // reclaim() or the one setGraph and every load begin with (buildCore's RPC calls reclaim() first too). The plugin
    // and SDL reclaim only in those calls, and as they stop the audio thread, so the generation the last reload
    // replaced stays until later ones have freed that plan and, once a block has released its nodes, found their
    // records back. A document that places none of the pack's nodes is not replanned: it would plan as it runs.
    ScriptPackResult loadScriptPack(const std::string& id, const std::string& source, std::size_t memoryLimit = 0);
    // How much of the calling thread's CPU time one call into a script pack's JS may use, on either side
    // (ScriptHost::setCallDeadline): set it before an audio thread runs blocks.
    void setScriptCallDeadline(std::chrono::milliseconds limit);
    // The script host's packs, the built-in one first (ScriptHost::packs).
    std::vector<rp::host::ScriptHost::PackInfo> scriptPacks() const;
    // The node types a document can name: the registry's, native and script.
    std::size_t nodeTypeCount() const;
    // The registry's entries a reload swapped out whose generation the script host has not yet freed.
    std::size_t retiredNodeTypeCount() const;
    // Script node instances whose JS is made and not yet released (ScriptHost::liveInstances): any thread, as
    // faultCount.
    std::uint32_t scriptInstances() const;

private:
    struct Graph;
    // Host MIDI staged for the next block: each message's frame and its place in one flat arena of bytes. Both keep
    // their capacity, reserved at construction for what the input node taking them carries in a block, so staging
    // allocates nothing until a block stages more.
    struct Staging {
        struct Message {
            std::uint32_t frame = 0, offset = 0, size = 0;
        };
        std::vector<Message>      messages;
        std::vector<std::uint8_t> bytes;

        Staging(std::size_t messageRoom, std::size_t byteRoom);
        void add(std::uint32_t frame, std::span<const std::uint8_t> data);
        // In frame order, keeping the staging order of messages on the same frame (design section 3.4).
        void sortByFrame();
        // Onto `out`, in order, then cleared.
        void takeInto(rp_queue& out);
        // Onto `out` in sortByFrame's order, then cleared, without moving the plugin's two runs (Engine.cpp).
        void takeSortedInto(rp_queue& out);
        void clear();

    private:
        void push(rp_queue& out, const Message& m) const;
    };

    // The running plan's systems (an empty table before the first plan), for everything but the host-command
    // handler: the tail on the block thread, and the control thread's readers. The handler is handed its plan's
    // table instead, and threads that through.
    const rp::host::SystemTable& currentTable() const;
    // setGraph, or with `replan` a script pack's reload planning the request last published again, which leaves the
    // parked cores and the request kept as they are.
    GraphResult publishGraph(const GraphRequest& request, bool replan);
    void applyCommand(DspCommand& c, const rp::host::SystemTable& table);
    void syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table);
    SystemBase* takeCore(std::uint64_t id);
    static const void* graphInterface(void* ctx, const char* id);
    static void onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const void* hostData, bool discard);

    // --- the per-system commands, applied by applyCommand to the core its table names (false, or nothing,
    // when it names none) ---
    // Hand `len` MIDI bytes straight to ONE system's ingress, skipping the project's MIDI routing. For CONTROL
    // traffic the UI aims at a specific cart (a settings knob), which must not depend on how the project
    // routes musical MIDI. See DspCommand::stageSystemMidi. `toCart`: `sys` is the system a physical N8 mirrors
    // (the plan's rp.n8.cart-mirror), so the cart gets the message too.
    bool stageSystemMidi(SystemBase* sys, const std::uint8_t* bytes, std::size_t len, bool toCart);
    // Poke bytes into a system's work RAM. `offset` indexes the SAME region readRam returns, so
    // read-modify-write round-trips without a coordinate conversion. Applied between blocks via the
    // invoker, never from the calling thread - which is what separates it from the debug facet's writeCpu
    // (a live-core write, valid only when the audio thread is stopped).
    //
    // Deliberately unguarded beyond bounds: writing a running core's RAM CAN confuse it, because the
    // emulated program has its own invariants over those bytes. That is the point of the feature.
    bool writeRam(SystemBase* sys, std::uint32_t offset, const std::vector<std::uint8_t>& bytes);

    // The NES tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before
    // every re-resolve so leaving a split mode (or gaining a second system) puts the core back exactly as
    // found. Scoped to what WE armed on purpose: the CLI / RenderHost set channelExportMode at construct
    // through the mesen role blob and never select a split routing, so they are never disturbed. `core`
    // is compared, never followed: a fresh core replaced in under the same id is not the one armed. It is
    // compared by address, so a core allocated where the armed one was freed defeats it; TS never gets there,
    // since buildCore refuses a live id, the stores' id counter only goes up, and every TS edit that drops a
    // core re-arms, which drops this record with it.
    struct ArmedTap {
        bool                  active   = false;
        SystemId              id       = 0;
        const MesenNesSystem* core     = nullptr;
        std::uint32_t         prevMode = 0;
    };
    ArmedTap armedTap_;

    SnapshotRegistry registry_;
    double           sampleRate_;
    // Staged host MIDI, taken on the next block by the plan's rp.host.midi-in (its one, which takes it whether or not
    // its output is wired), and the staged control-surface traffic, by its rp.host.controller-in; what neither takes
    // is dropped at the block's end.
    Staging pendingMidi_;
    Staging pendingControllerMidi_;
    // This block's host MIDI out and control-surface out, which the plan's rp.host.midi-out and controller-out append
    // to; cleared at the top of each block.
    std::vector<EngineMidiOut>             midiOut_;
    std::vector<std::vector<std::uint8_t>> controllerOut_;

    // Simulated host transport, driven per block. Continuous ppq clock (the pull path and the audio
    // loop share it; they never run concurrently). Plain — mutated only by the owning thread.
    double bpm_       = 120.0;
    bool   transport_ = false;
    double ppq_       = 0.0;
    bool   controllerConnected_ = false;  // a control surface is bound (set per block by the host)
    bool   blockTracing_ = false;         // this block's syncTrace_.beginBlock answer, for the system nodes
    HostSyncTrace syncTrace_;  // inert unless RETROPLUG_SYNC_TRACE is set
    // Optional external mirror of what the cart system is fed (set by the plugin and the standalone to reach a
    // physical N8); null in every other host. Called on the audio thread, so the target must be RT-safe (N8Link is).
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
    // The request setGraph last published (the empty document's, from construction), which a reload replans.
    std::optional<GraphRequest>                     lastRequest_;

    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
    // liveCores_) as they go.
    std::unique_ptr<Graph> graph_;
};
