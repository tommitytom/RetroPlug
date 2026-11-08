#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "system/SystemTypes.hpp"

#include "graph/rp_node.h"  // rp_queue
#include "host/devices/DeviceRegistry.hpp"  // linkHooks, for installLink
#include "host/engine/CapabilityJobs.hpp"
#include "host/engine/HostSyncTrace.hpp"
#include "host/engine/OfflineTypes.hpp"
#include "host/engine/SnapshotRegistry.hpp"
#include "host/script/ScriptHost.hpp"

class SystemBase;
class ChannelTap;
struct DspCommand;
struct SystemBuildRequest;
namespace rp::graph { struct HostCommand; }
namespace rp::host { class OfflineJobs; struct SystemEntry; struct SystemTable; }

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

    // --- host input, taken by the plan's rp.host.midi-in at the top of the next block (dropped at its end by a plan
    // without one). The bytes are copied into the engine's staging, which allocates nothing while a block stages no
    // more than that node carries (HostIoNodes.hpp) ---
    // Delivered on the next processBlock, at intra-block `frame`.
    void stageMidi(std::uint32_t frame, std::span<const std::uint8_t> bytes);
    void stageMidi(std::uint32_t frame, const std::vector<std::uint8_t>& bytes) {
        stageMidi(frame, std::span<const std::uint8_t>(bytes));
    }

    // --- transport (plain members; mutated only by the Engine's owning thread) ---
    void setBpm(double bpm);
    void setTransport(bool playing);
    // Move the host playhead. A DAW does this on every locate: stop-and-rewind, a loop wrap,
    // or a click in the timeline. Roles see it as a ppqStart discontinuity in the next block.
    void setPpq(double ppq);

    // Host sample rate. Used at construct for new cores (findable via sampleRate()) AND, when quiescent, pushed to the
    // running plan's cores, so a host sample-rate change re-rates them in place (SameBoy GB_set_sample_rate, Mesen
    // resampler). Safe to call with cores live only when the audio thread isn't draining: the plugin calls it from DPF's
    // sampleRateChanged, which fires only while deactivated; the CLI, before any build.
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
    // and fresh EVERY block (unlike readState, and readSram on a savestate-sliced battery), so a runtime overlay
    // tracks per-frame state.
    std::optional<std::vector<std::uint8_t>> readRam(SystemId id);
    /** The published WRAM length, for a bounds check that costs no copy. 0 = no writable RAM. */
    std::size_t ramSize(SystemId id);
    /** The published battery length, likewise. 0 = no battery. */
    std::size_t sramSize(SystemId id);
    bool screenshot(SystemId id, const std::string& path);
    // The system's latest video frame (raw XRGB8888). Reads the concurrent FrameBufferTriple, so it is
    // safe while the audio thread writes; width/height are 0 (published false) for an unknown system.
    EngineFrame getFrame(SystemId id);

    // --- capability jobs (host/engine/CapabilityJobs.hpp, docs/modular-graph-phase-c5-design.md section 6), control
    // thread ---
    // How many jobs may be outstanding at once: taken and not yet finished, or finished and not yet drained. A
    // submission past it is refused (busy), so every job taken has room for its completion and none is ever dropped.
    static constexpr std::size_t kMaxJobs = 64;
    // The room a device's node has for a refusal's reason (rp_device_jobs::submit's error buffer), its NUL included: a
    // reason past it is cut short. The fixed reasons in tree are all under half of it; only one that echoes back an
    // argument the caller made long (an operation's name) can reach it.
    static constexpr std::size_t kDeviceJobErrorBytes = 256;
    // Take a job for a node of the last published plan, or refuse it (id 0, the reason in `error`): a node the plan
    // does not have, one that does not present the capability (a node that is not a system node, or whose core returns
    // null), an operation the capability does not have or does not perform here (an emulated battery's restore, which
    // is a rebuild), arguments refusable from the published copy (a region the core does not list or the registry does
    // not publish, a range past its end, an empty write), a full job count, and a full command ring. Every read of an
    // emulated system completes before this returns, its completion queued; a write completes once the block after the
    // drain that applies it has published the region, or, while no audio thread runs, its state is final before this
    // returns and its completion is queued by the next drain. A system node is named sys-<id>
    // (publishGraph refuses any other id for one), so the plan's table maps the node to its core. Any other node is
    // asked itself (submitDeviceJob): a device's node runs the job on its device's thread, or refuses it busy or not
    // connected.
    rp::host::JobSubmit submitJob(rp::host::JobRequest request);
    // The record of a job a device runs on its own thread (CapabilityJobs.hpp: progress, then the result or the error,
    // then the final state, stored last with release): counted against kMaxJobs, given the next id and kept until a
    // drain collects it. A TEST / HARNESS seam, which plays a device: a device node's job comes through its
    // node instead (submitDeviceJob). Refused, the record dropped, when kMaxJobs are outstanding.
    rp::host::JobSubmit adoptJob(std::unique_ptr<rp::host::Job> job);
    // The completions since the last drain, oldest first, and a progress entry for each running job that reported
    // progress since then. A write still in the ring is not here: it is collected by a later drain.
    std::vector<rp::host::JobCompletion> drainJobs();
    // Jobs outstanding: taken and not yet drained. The old writes below are not among them: nobody collects one.
    std::size_t jobsOutstanding() const;
    // A write into system `id`'s work RAM at `offset`, the region readRam returns, on the job path with no completion:
    // the old system-id write (EngineRpcService::writeRam, QueuedInvoker::writeRam). False, queuing nothing, for an
    // empty write, a system the last published plan does not place, and a full command ring; a write past the region is
    // dropped where it applies, as the RPC refuses one first from the published size.
    bool writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);
    // writeRam's twin aimed at the battery, at the offsets readSram returns (EngineRpcService::writeSram,
    // QueuedInvoker::writeSram): a detached job applied through SramCapability::write. A non-empty `expect` (as long
    // as `bytes`) makes it a compare-and-swap: applied only if the battery still holds `expect` there, the snapshot the
    // caller built its bytes from. Applied or refused, the apply republishes the battery (SnapshotRegistry::
    // republishSram), so the caller's next readSram shows what it holds now: the write, or the cart's change to
    // rebuild from. False, queuing nothing, for an empty write, an `expect` of another length, a system the last
    // published plan does not place or that presents no battery, and a full command ring.
    bool writeSram(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes, std::vector<std::uint8_t> expect);

    // --- offline jobs (host/engine/OfflineJobs.hpp, docs/modular-graph-phase-d2-design.md section 3), control
    // thread ---
    // A graph document run to completion on a host of its own, on a worker thread of the engine's offline pool, apart
    // from everything above: its own node registry (the built-in C++ packages only), its own resource table, no audio
    // thread, and nothing of this engine's plan, ring or resources. The pool is made at the first submission, so a host
    // that never submits starts no thread, and joined first as the engine goes. Taken or refused at once
    // (OfflineJobs::submit); its completion comes through a later drain.
    rp::host::OfflineSubmit submitOfflineJob(rp::host::OfflineRequest request);
    // An engine job (docs/modular-graph-phase-d3-design.md section 3) on the same pool: a script bundle of `kind` (the
    // one is "render") run with a QuickJS runtime and an Engine of its own, which is not this one. It belongs to this
    // engine all the same: a render survives its editor closing, and this engine's teardown cancels it and joins its
    // worker. Taken or refused at once (OfflineJobs::submitEngine).
    rp::host::OfflineSubmit submitEngineJob(rp::host::EngineJobRequest request);
    // The graph jobs' completions since the last drain, oldest first; with `engine`, the engine jobs' completions and
    // every running one's progress too (OfflineJobs::drain). With `wait`, waits up to it for one while a job is in
    // flight, which a host with no UI tick does between pumps. `inFlight`, when given, is set to the jobs still queued
    // or running after it.
    std::vector<rp::host::OfflineCompletion> drainOfflineJobs(
        std::chrono::milliseconds wait = std::chrono::milliseconds(0), std::size_t* inFlight = nullptr,
        rp::host::EngineJobsDrain* engine = nullptr);
    // Cancel a queued or running job of either kind: it completes with the reason "cancelled". False for one neither
    // queued nor running.
    bool        cancelOfflineJob(std::uint32_t id);
    // Offline jobs queued, running, or finished and not yet drained.
    std::size_t offlineJobsOutstanding() const;

    // The host service the live graph's nodes get for `id` from this engine (what rp_host.get_interface answers past
    // GraphHost's own ids), or null for one it does not offer. A guard report (rp.host.guards/1) is one it does not:
    // live nodes recover from their guards with nobody told.
    const void* graphService(const char* id) { return graphInterface(this, id); }

    // The owned snapshot store the control plane reads through. Claim a slot at build, release it
    // when the core is deleted; processBlock publishes into it.
    SnapshotRegistry& registry() { return registry_; }

    // --- the ring: QueuedInvoker pushes every command onto it ---
    bool pushCommand(const DspCommand& c);          // control thread: false when the ring is full
    void drainCommands();                           // the block thread (or the control thread, quiescent)
    void setAudioThreadOwns(bool owns);
    // Control thread: free the plans the block thread has handed back (each only once the block thread has moved past
    // it), the nodes only they held with them, and the script records it has released. Also where the blocks
    // processBlock refused as too long are reported, once per new one. A host with an idle tick calls it every frame
    // while its audio runs (the plugin's editor idle, the SDL standalone's frame loop), so with nothing handed back it
    // allocates nothing and takes no lock the block thread takes.
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
    // The owner's drop: idempotent, and it never frees a resource a plan or a node instance still pins (the plan's
    // binding or the instance goes at the reclaim that frees it). False for an unknown or already dropped id.
    bool releaseResource(std::uint32_t id);
    // The resource a node may name while its owner still holds it, or null. Control thread: a node pins what it
    // reads, so nothing on the audio side needs this.
    const rp_resource* resource(std::uint32_t id) const;
    // How many pins it holds (one per instance whose settings name it, and one per plan binding), or 0 for an
    // unknown or dropped id that no plan holds.
    std::uint32_t resourcePinCount(std::uint32_t id) const;

    // --- graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5), control thread ---
    // A system node that builds its own core (docs/modular-graph-phase-c3-design.md section 5), inside the setGraph
    // that activates it: the core is built from `request` at the engine's rate, its snapshot slot is claimed under
    // `system` and it joins the live cores. Null, with the reason in *why, for a live id (one a node of any plan not
    // yet reclaimed holds), a ROM the console's gate or the core refuses, a full snapshot pool, and an id the plan
    // being built has no slot for (which destroys the core it built and gives back the slot and the live-core entry
    // that claim took); the node throws
    // that reason, which the planner reports as activate-failed. The core is the calling node's until destroySystem,
    // which is the reclaim (or refused-plan unwind) that frees the node:
    SystemBase* buildSystem(std::uint64_t system, SystemBuildRequest request, std::string* why);
    bool        coreLive(SystemId id) const { return liveCores_.count(id) != 0; }
    // Every core alive: owned by a system node of a plan not yet reclaimed. Control thread.
    std::size_t liveSystemCount() const { return liveCores_.size(); }
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
    // SyncTap. Refused before planning when a system node's `system` field is present but is not a system id, its node
    // id is not `sys-<its id>`, two of its nodes name one system, its slots and its system nodes name different
    // systems, the tap arms a mode other than 1 to 3 or a system not in the slots, or the document holds a second
    // rp.host.midi-in or rp.host.midi-out. A node that names no system is refused by its own create
    // (settings-invalid) with the reason naming what it must name, and a node refused that way holds the slots and
    // duplicate checks back rather than being lined up by an id it lacks. A node's failure to build (the ROM gate, a
    // Mesen boot, a live id, a full snapshot pool, no builder) is activate-failed on that node. A refused plan destroys
    // the cores its nodes built and leaves the running plan, its cores, its slots and every resource pin as they were.
    // The request is the one a script pack's reload replans (loadScriptPack).
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
    // defineNode results to __rp_scriptNodes as the built-in bundle does: packages/core/ts/register.ts),
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
    // other node keeps its instance, its core included. When the replan is refused (a
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
    // reclaim() or the one setGraph and every load begin with. The plugin (while its editor is open) and the SDL
    // standalone call reclaim() from their idle tick every frame, so there the generation goes within a few frames; a
    // host with no tick reclaims only in those calls and as it stops the audio thread, so the generation the last
    // reload replaced stays until later ones have freed that plan and, once a block has released its nodes, found
    // their records back. A document that places none of the pack's nodes is not replanned: it would plan as it runs.
    ScriptPackResult loadScriptPack(const std::string& id, const std::string& source, std::size_t memoryLimit = 0);
    // How much of the calling thread's CPU time one call into a script pack's JS may use, on either side
    // (ScriptHost::setCallDeadline): set it before an audio thread runs blocks.
    void setScriptCallDeadline(std::chrono::milliseconds limit);
    // The script host's packs, the built-in one first (ScriptHost::packs).
    std::vector<rp::host::ScriptHost::PackInfo> scriptPacks() const;
    // TEST / HARNESS: one more native node package beside the built-in ones, which is how the engine-graph suite puts
    // its probe nodes on the real engine. Control thread; a document may place its types from the next setGraph on.
    // `package` must outlive the engine, as the built-in packages do: the registry keeps the pointer as the identity
    // of the version it registers, so a package with a shorter life would be compared against freed memory. Every
    // caller today is a function-local `static`. False, with the reason in *why (or on stderr when `why` is null),
    // for a package the registry refuses.
    bool addNodePackage(const rp_package* package, std::string* why = nullptr);
    // The node types a document can name: the registry's, native and script.
    std::size_t nodeTypeCount() const;
    // The registry's entries a reload swapped out whose generation the script host has not yet freed.
    std::size_t retiredNodeTypeCount() const;
    // Script node instances whose JS is made and not yet released (ScriptHost::liveInstances): any thread, as
    // faultCount.
    std::uint32_t scriptInstances() const;

    // --- device links (docs/modular-graph-phase-e2-design.md section 5), control thread ---
    // The host's device links, for the nodes that bind one by name and interface id through rp.host.devices/2
    // (graph/rp_device.h, host/devices/DeviceRegistry.hpp). Install them before the first graph: the engine keeps these
    // pointers for the life of its graph, and a node borrows a link for the life of the plan it runs in. A name a host
    // installs nothing under answers null, and the node that asks runs on a stub of its own, never connected, which is
    // what a host that owns no such device leaves it as by never calling this (the Node addon, the render host and the
    // UI harness; the CLI installs the N8's, through the provider it starts, and never connects it). A host that owns
    // its links as members of its own state tears its plans down with clearGraph first, then clears them here, before
    // those members go: its engine is destroyed after them, and with it the plans whose nodes still hold a link.
    //
    // `link` is the pointer to the interface `interfaceId` names, which a node casts back to that interface; `acquire`
    // and `release` (null: nothing) are told of each node's acquire and release. False, installing nothing, for a full
    // table, a name and id or a link already installed, or a name or id the registry has no room for
    // (DeviceRegistry::install).
    [[nodiscard]] bool installDevice(const char* name, const char* interfaceId, void* link,
                                     void (*acquire)(void* link) = nullptr, void (*release)(void* link) = nullptr);
    // The host taking `link` back (the pointer it installed): a lookup finds nothing and a node's release of it reaches
    // nothing from here on. True when it was installed; false, clearing nothing, for a pointer the registry does not
    // hold. It frees nothing: the link outlives every plan that could hold it (graph/rp_device.h).
    bool clearDevice(void* link);
    // installDevice for a C++ link implementing `Interface`: installed as an `Interface*`, the pointer a node casts
    // back, with the link's own acquire() and release() as the hooks when it has them (rp::host::linkHooks). clearLink
    // takes it back.
    template <class Interface, class Link>
    [[nodiscard]] bool installLink(const char* name, const char* interfaceId, Link& link) {
        const rp::host::LinkHooks hooks = rp::host::linkHooks<Interface, Link>();
        return installDevice(name, interfaceId, static_cast<Interface*>(&link), hooks.acquire, hooks.release);
    }
    template <class Interface, class Link>
    bool clearLink(Link& link) {
        return clearDevice(static_cast<Interface*>(&link));
    }
    // Control thread, audio thread stopped: publish the construction-time empty document and reclaim the plan it
    // replaced, so every node of the host's last graph is destroyed here, while the links it may use in its destroy are
    // still alive. A host that owns its device links as members and cannot declare them before its engine runs this
    // from its destructor body, before it clears them (clearDevice).
    void clearGraph();

private:
    // The node giving its core up, or a refused plan unwinding one it built. Only rp.host.systems/2's destroy lambda
    // calls it, so the core a live node holds cannot be deleted by anyone else.
    void destroySystem(SystemBase* core);

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
    // request kept as it is.
    GraphResult publishGraph(const GraphRequest& request, bool replan);
    void applyCommand(DspCommand& c, const rp::host::SystemTable& table);
    void syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table);
    static const void* graphInterface(void* ctx, const char* id);
    static void onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const void* hostData, bool discard);

    // --- the staged and per-system commands: stageNodeMidi by the id a document gives a node, the rest applied by
    // applyCommand to the core its table names (false, or nothing, when it names none) ---
    // Hand `len` staged bytes to the node the plan running at this point of the drain places under `nodeId`, through
    // the rp.stage/1 interface the node answers (a system node hands them to its core's ingress, a device node to its
    // device). See DspCommand::stageNodeMidi: the id is the document's, so the engine reads no node's type. [audio]
    bool stageNodeMidi(const std::string& nodeId, const std::uint8_t* bytes, std::size_t len);
    // Apply a capability write to the core `table` names (the plan current at this point of the drain), at a block
    // boundary and never mid-block - which is what separates it from the debug facet's writeCpu (a live-core write,
    // valid only when the audio thread is stopped). It runs on the block thread at the drain, or on the control
    // thread when the push flushes inline while no audio thread owns the cores. Deliberately unguarded beyond bounds:
    // writing a running core's RAM CAN confuse it, because the emulated program has its own invariants over those
    // bytes. That is the point of the feature. Stores the job's state and touches it no more once that state is final;
    // allocates and frees nothing.
    void applyJob(rp::host::Job& job, const rp::host::SystemTable& table);
    // The block's end, after the publish: each write applied since the last block that ran is done if `ran` (the table
    // of the plan that ran it) still has its system, so its region is published, and failed (removed) if not.
    void confirmApplied(const rp::host::SystemTable& ran);
    // While no audio thread runs, the control thread owns the cores: republish what the inline flush applied and
    // finish those writes there and then, as confirmApplied would at a block's end. Nothing while one runs.
    void settleQuiescent();
    // Move every finished job to the completions (an old write to none) and free its record.
    void sweepJobs();
    // A job for a node of the plan that is not a system node: a device's node, asked for the capability through its
    // rp_node_vtable::get_interface (rp_device_jobs, graph/rp_jobs.h), takes it into a record kept here or refuses it
    // (busy, not connected); a node that presents none of the interfaces refuses every job.
    rp::host::JobSubmit submitDeviceJob(const rp::host::JobRequest& request);
    // The system node `nodeId` names in the last published plan, or null for a node id that is not sys-<id> of one.
    const rp::host::SystemEntry* publishedSystem(const std::string& nodeId) const;

    // The tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before every
    // re-resolve so leaving a split mode (or gaining a second system) puts the core back exactly as found. Scoped
    // to what WE armed on purpose: the CLI / RenderHost set channelExportMode in the system node's settings and
    // never select a split routing, so they are never disturbed. `tap` is compared, never followed: a fresh core
    // replaced in under the same id is not the one armed, and a pointer taken from this record is never dereferenced
    // (the restore follows the plan's tap, and only when the two addresses agree). A NES's tap is the core object, so
    // its address differs from the core pointer's by a per-class constant and within one class tap equality is core
    // equality. The record itself survives a drop and reclaim, so a core whose tap landed where the armed tap was
    // freed defeats it; TS never gets there, since buildSystem refuses a live id, the stores' id counter only goes up,
    // and every TS edit that drops a core re-arms, which drops this record with it.
    struct ArmedTap {
        bool              active   = false;
        SystemId          id       = 0;
        const ChannelTap* tap      = nullptr;
        std::uint32_t     prevMode = 0;
    };
    ArmedTap armedTap_;

    SnapshotRegistry registry_;
    double           sampleRate_;
    // Staged host MIDI, taken on the next block by the plan's rp.host.midi-in (its one, which takes it whether or not
    // its output is wired); what it does not take is dropped at the block's end.
    Staging pendingMidi_;
    // This block's host MIDI out, which the plan's rp.host.midi-out appends to; cleared at the top of each block.
    std::vector<EngineMidiOut> midiOut_;

    // Simulated host transport, driven per block. Continuous ppq clock (the pull path and the audio
    // loop share it; they never run concurrently). Plain — mutated only by the owning thread.
    double bpm_       = 120.0;
    bool   transport_ = false;
    double ppq_       = 0.0;
    bool   blockTracing_ = false;         // this block's syncTrace_.beginBlock answer, for the system nodes
    HostSyncTrace syncTrace_;  // inert unless RETROPLUG_SYNC_TRACE is set

    // Blocks processBlock refused as longer than the plan's buffers, and the length of the last one: counted
    // on the block thread, reported by reclaim() on the control thread (oversizedReported_ is its side).
    std::atomic<std::uint32_t> oversizedBlocks_{0};
    std::atomic<std::uint32_t> oversizedFrames_{0};
    std::uint32_t              oversizedReported_ = 0;

    // Every core alive by id, owned by a system node until that node frees it (control thread).
    std::map<SystemId, SystemBase*> liveCores_;
    // The system table publishGraph is filling while it plans (points into the one the plan is published with): a
    // system node's core does not exist before its activation, so buildSystem adds it to the slots here. Null outside
    // setGraph; only system nodes, on the control thread, call buildSystem.
    rp::host::SystemTable*                          building_ = nullptr;
    std::uint32_t                                   hostWidth_ = 2;
    // The request setGraph last published (the empty document's, from construction), which a reload replans.
    std::optional<GraphRequest>                     lastRequest_;

    // The table of the last plan publishGraph published, which a job's node id is resolved against on the control
    // thread while the audio thread may still run the one before it.
    std::shared_ptr<const rp::host::SystemTable>    publishedTable_;
    // The capability jobs (CapabilityJobs.hpp): the records not yet collected, the control thread's, and the
    // completions not yet drained. Before graph_, so a queued write the graph discards as it goes still has its record.
    std::vector<std::unique_ptr<rp::host::Job>>     jobs_;
    std::deque<rp::host::JobCompletion>             completions_;
    std::uint32_t                                   nextJobId_ = 1;
    // The writes applied and not yet published: the block thread's, or the control thread's while no audio thread
    // runs. Fixed, never grown: every job in it is one of at most kMaxJobs outstanding (an old write, which nobody
    // collects, finishes as it applies and never enters it).
    std::array<rp::host::Job*, kMaxJobs>            applied_{};
    std::size_t                                     appliedCount_ = 0;

    // The offline job pool, made at the first submission. It uses nothing of the engine's (its registry, its graph
    // jobs' tables and hosts, and its engine jobs' runtimes and Engines are its own), so where it sits among the
    // members does not matter for what it touches; the destructor still resets it first, so every worker is joined,
    // and every node, core and runtime a job built destroyed, before any other member goes.
    std::unique_ptr<rp::host::OfflineJobs>          offline_;

    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
    // liveCores_) as they go.
    std::unique_ptr<Graph> graph_;
};
