#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "graph/rp_node.h"
#include "transport/SpscRing.hpp"

// Forward-declared so quickjs.h stays out of this header; these match quickjs.h's tags.
struct JSRuntime;
struct JSContext;

namespace rp::host {

// What the audio-side runtime's heap has done since the host was made, cumulative: every block, adoption and
// release that entered JS. A test reads it either side of a stretch of blocks. Written only by the thread that runs
// blocks; any thread may read it, a read on another thread lagging a running audio thread.
struct ScriptAllocations {
    std::uint64_t allocs = 0;    // malloc and calloc calls
    std::uint64_t reallocs = 0;
    std::uint64_t frees = 0;
    std::uint64_t bytes = 0;     // asked for, by allocs and growing reallocs
};

// ---- profiling the audio runtime (spec/08-profiling.md) ---------------------------------------------------------
// Real only in a RETROPLUG_PROFILE build; in any other every field is 0 and `enabled` false. They cross the harness
// RPCs (dspAllocStats, dspRunGc, dspTrace) as they are, so their field names are what packages/retroplug/src/
// audioDriver.ts reads. "dsp" is the DSP (audio) thread, whose JS the script host's audio runtime runs.

// The allocation window: what the audio runtime did since the last resetAllocStats(). `liveBytesDelta` is the net
// change of its live heap (flat ~0: refcount churn; rising: a leak). The block counts are per Engine block, each
// measured from its beginBlock to its endBlock, so a benchmark reads them once per window.
struct DspAllocStats {
    bool          enabled            = false;
    std::uint64_t allocCalls         = 0;
    std::uint64_t reallocCalls       = 0;
    std::uint64_t freeCalls          = 0;
    std::uint64_t allocBytes         = 0;   // asked for, by allocs and growing reallocs, in the window
    std::int64_t  liveBytesDelta     = 0;   // usable bytes
    std::uint64_t peakBytes          = 0;   // the live heap's all-time peak, usable bytes (informational)
    std::uint64_t blockCount         = 0;
    std::uint64_t maxBlockAllocCalls = 0;   // the worst single block (the tail)
    std::uint64_t maxBlockAllocBytes = 0;
};

// One self-driven collection pass (JS_RunGC): `freedBytes` ~0 proves the runtime accumulates no reference cycles.
struct DspGcResult {
    bool         enabled    = false;
    double       ms         = 0.0;
    std::int64_t freedBytes = 0;
};

// One span of the trace window: `t0` and `t1` in microseconds from the window's start (traceReset), `label` an index
// into traceNames().
struct DspTraceSpan {
    std::uint32_t label = 0;
    double        t0    = 0.0;
    double        t1    = 0.0;
};

// The fixed span labels; the kernel node interns its role kinds from DSP_SPAN_ROLE_BASE up.
enum DspSpanLabel : std::uint32_t {
    DSP_SPAN_KERNEL    = 0,   // ScriptHost: a script node's process call (the DSP kernel's, in every document TS builds)
    DSP_SPAN_MARSHAL   = 1,   // ScriptHost: its inputs made JS values (the audio copies in and the transport, then
                              //   each input it reads)
    DSP_SPAN_JSCALL    = 2,   // ScriptHost: the JS_Call into its process
    DSP_SPAN_APU       = 3,   // Engine: the plan after its script nodes (the cores, the mixing, the host outputs)
    DSP_SPAN_PUBLISH   = 4,   // Engine: the state pump to the control plane (the per-block frame copy, and the timed
                              //   savestate and SRAM republish, so spiky)
    DSP_SPAN_ROLE_BASE = 16,
};

// Every label a trace names is below this: the fixed ones, then the kernel node's, one per DSP role kind it registers
// (a handful), well short of it. traceName refuses a label past it, so no label JS hands it can size the name table.
inline constexpr std::uint32_t kDspSpanLabels = 1024;

// The script host (docs/modular-graph-phase-b3a-design.md section 2): every TS node one Engine runs, behind the
// same C ABI as a native node. A bundle registers its node definitions (defineNode results, appended to the global
// __rp_scriptNodes: packages/retroplug/src/script/register.ts), and the host registers an rp_node_type for each in
// one package, "rp.script", whose instances are native shims.
//
// Two bare QuickJS runtimes, each loaded with the same bundle and sharing no JS value:
//   - the CONTROL side, entered only by the control thread, answers layout_for and prepare_settings: the
//     definition's own parseSettings checks a settings object, and its layout gives the ports, from the settings
//     alone. It never holds an instance.
//   - the AUDIO side holds every instance and is entered only by the thread that runs blocks (the audio thread, or
//     the control thread while setAudioThreadOwns(false)). A shim's create and activate run on the control thread
//     and only allocate the shim; the JS instance is created on the block thread at the first call the shim gets
//     there, which is the ports_changed the planner sends every fresh instance when its plan is adopted, so before
//     that plan's first process. A shim's destroy (control thread) queues the instance's release, which beginBlock
//     performs at the start of a later block (at most kReleasesPerBlock a block), or does it there and then while no
//     audio thread runs; the record it frees then goes back to the control thread to be deleted (reclaim), so the
//     audio thread frees nothing native. What a released instance leaves in cycles is collected by a GC at two
//     points no block is running: the reclaim that ends a quiescent operation which released some (the Engine's
//     after every setGraph: one GC however many went), and the audio thread handing the runtime back
//     (setAudioThreadOwns(false)). The first reclaim after the load (the top of the first setGraph) takes what the bundle's
//     load left in cycles too, whether or not an instance released anything. Never by a
//     block, so while an audio thread owns the runtime (a live session) those cycles wait for QuickJS's own threshold
//     GC, which runs inside whichever block's JS allocates past it, or for the audio thread to hand the runtime back.
//
// Both runtimes' JS stacks are held to kStackBytes, under the smallest thread that may enter one: 512 KiB is a macOS
// secondary thread's default, which a DAW's audio or control thread may have and a render job's std::thread does
// (no host here sizes its threads), so 256 KiB leaves the caller's frames half. Loading the production bundle needs
// 4 KiB of it; the test bundle with its toys, planned (zod's parse, refusal and JSON schema), 40 KiB, 64 under
// TSan and 96 under ASan.
//
// Every call into the audio runtime from the block thread is held to a deadline (setCallDeadline) of the calling
// thread's CPU time, so a stall that is not the node's (preemption, a stopped process, a debugger or a VM pause) never
// trips it. A call that runs past it is stopped (QuickJS's interrupt, which JS cannot catch) and counted as a throw.
// A node with faultOnThrow then faults; one without runs again next block (design section 4.5), and faults only if
// its next call is stopped too, so a node that hangs is bypassed within two deadlines and one that ran long once is
// not. A call that runs to its end starts the count again. A deadline fault is a fault like any other: it latches on
// the instance and outlives reshapes, and only a rebuild (or the node removed and added again) clears it.
//
// Every type declares RP_TYPE_STATELESS_PREPARE: its prepare_settings reads the settings it is handed, the
// definition and what the instance was created with, never anything a commit writes. A type that reshapes answers
// RP_PREPARE_RESHAPE for every change its definition accepts; one that does not answers RP_PREPARE_SWAP while the
// layout stays the one it was created with and RP_PREPARE_REBUILD when it moves.
//
// A node's process gets one Block object, the same every block (Phase A's Block, packages/retroplug/src/graph/
// defineNode.ts): messages(port) is a reused Message[] whose payloads are copied into one JS-owned buffer per port,
// control(id) a reused { start, changes }, audio(port) a reused Float32Array[] over the instance's own JS-owned copy
// of each channel, which an input's samples are copied into before process and an output's copied out of after it
// (made when the port first runs and again only when a larger block or JS outgrows them, each view again only when the
// block's length moves), and send and emit write the plan's output queues directly. No plan memory ever reaches JS,
// so nothing a node keeps, transfers or writes can reach a buffer another node reads or a plan the control thread
// frees. Ports are found by id through atoms made when the instance, or the layout it reshapes to, is bound, so a warm
// block allocates nothing native, and a node written with indexed loops nothing in JS either. What a block hands out
// is good for that process call: keep copies.
//
// The promise jobs a block's JS queues run at the end of the block (endBlock), at most kJobsPerBlock of them, so a
// reaction that queues another every time cannot hold the block: the rest wait for the next block's end.
//
// A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
// (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch); a
// faultOnThrow node whose onSettings or portsChanged threw faults at its next block, since it may hold half of what
// the hook set. Returning "fault" faults it too. The first throw's message is kept, and logged by reclaim.
//
// Settings reach the audio side as their JSON form: the control side's parseSettings checks them, and what it
// returns crosses as JSON (the two runtimes share no value), so a transform's result that JSON cannot hold (a Map, a
// Date, undefined) arrives as JSON spells it. The audio side never runs the schema: it needs no zod (which B3b takes
// out of the bundle) and does no validation work at a swap.
//
// A profile build (RETROPLUG_PROFILE, spec/08-profiling.md) profiles the audio runtime: its allocator keeps the live
// and peak heap, an allocation window counts per block, and a trace window records nested wall-time spans. Every
// script node's process is a dsp-kernel span, holding a marshal span for the audio copies in and the transport and a
// js-call span for its JS, inside which each input it reads is a marshal span too; the audio context's globals
// spanBegin(label), spanEnd(), traceName(label, name) and traceArmed() let the JS add its own (the kernel node's
// roles). JS's spans nest inside the js-call span of the process call running, and record nothing outside one: its
// spanEnd never closes a span the host opened, and what it leaves open (a throw, or the deadline, which skips every
// finally) the host closes as the call returns, so js-call and dsp-kernel always close their own.
class ScriptHost {
public:
    using Chunk = std::span<const std::uint8_t>;
    // Globals a host binds into one of the contexts before any bundle runs in it (the native tests' probes),
    // `audioSide` saying which; called once for each, on the constructing thread.
    using Binder = void (*)(void* user, JSContext* ctx, bool audioSide);

    explicit ScriptHost(Binder binder = nullptr, void* user = nullptr);
    // Every instance released first: no audio thread may own it (setAudioThreadOwns(false)), and every shim must
    // be gone (the graph that held them destroyed before this host).
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // The production bundle every Engine loads (packages/retroplug/src/script/bundle.ts, embedded at build time).
    static Chunk embeddedBundle();

    // ---- control thread ------------------------------------------------------------------------------
    // Run a bundle (global-code bytecode, in chunks run in order) in both contexts, then read the definitions it
    // registered. Once, before anything plans with the package: a second call is refused, and leaves what the first
    // registered as it is. False, with a reason, when a chunk fails to load or a definition cannot be read or is not
    // the same in both contexts (its id or its descriptor differs: a bundle that branches on which context it runs
    // in). One bad definition refuses the whole package: it then has no types, so every document naming any of the
    // bundle's is refused (unknown-type).
    bool load(std::span<const Chunk> chunks, std::string* error = nullptr);
    // The script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a load).
    const rp_package* package() const { return &package_; }
    // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
    // queued instance there and then, and collects what they left.
    void setAudioThreadOwns(bool owns);
    // How much of the calling thread's CPU time one call into the audio runtime may use before it is stopped
    // (kDefaultCallDeadline until set). Set it before an audio thread runs blocks.
    void setCallDeadline(std::chrono::milliseconds limit);
    static constexpr std::chrono::milliseconds kDefaultCallDeadline{2000};
    // Delete the records the block thread has released, and log the first throw once. While quiescent, also collect
    // what instances released since the last left in cycles, and, the first time after the load, what the bundle's load
    // left (whether or not anything was released): call it after an operation that may release some.
    void reclaim();
    // The first throw's type and message ("" until one), once the block thread has kept it.
    std::string firstThrow() const;

    // ---- the thread that runs blocks ---------------------------------------------------------------------
    // At the top of every block, before the graph runs: release what destroy queued, at most kReleasesPerBlock (a
    // release frees an instance's whole JS state, which for a large node is a great many objects: a plan of hundreds
    // going at once is spread over blocks, and a full ring still drains in 16).
    void beginBlock();
    static constexpr std::uint32_t kReleasesPerBlock = 16;
    // At the end of every block that ran the graph: the promise jobs its JS queued, at most kJobsPerBlock of them,
    // then the allocation window's per-block counts (a profile build).
    void endBlock();
    static constexpr int kJobsPerBlock = 256;
    static constexpr std::size_t kStackBytes = 256 * 1024;  // each runtime's JS stack (the class comment)

    // ---- any thread ------------------------------------------------------------------------------------------
    // Throws caught in a script node (process, and its create, onSettings, portsChanged and activate) and in the
    // promise jobs their blocks queued, so far.
    std::uint32_t     throwCount() const { return throws_.load(std::memory_order_relaxed); }
    ScriptAllocations audioAllocations() const;
    // JS instances created and not yet released.
    std::uint32_t     liveInstances() const { return live_.load(std::memory_order_relaxed); }
    // Blocks whose promise jobs outran kJobsPerBlock, leaving some for the next block; reclaim says so once.
    std::uint32_t     jobsCutShort() const { return jobsCutShort_.load(std::memory_order_relaxed); }

    // ---- profiling (spec/08-profiling.md): real only in a RETROPLUG_PROFILE build --------------------------------
    // The allocation window, opened by resetAllocStats, which can pin the audio runtime's auto-GC off from then on (so
    // a window collects nothing); runGc runs and times one collection there. Quiescent: no audio thread runs blocks.
    DspAllocStats allocStats() const;
    // Each refuses, doing nothing, while an audio thread owns the runtime (setAudioThreadOwns): resetAllocStats and
    // traceReset return false, runGc a result with enabled false, and the trace reads nothing; said once on stderr.
    bool          resetAllocStats(bool disableAutoGc);
    DspGcResult   runGc();
    // The trace window. traceReset clears it and arms (or disarms) it, quiescent, as are the reads; the block thread
    // records, and only while it is armed. spanBegin and spanEnd bracket a span; spanSince records one from `t0`, a
    // traceNow(), to now. lastProcessEnd() is when the last script node's process span ended, on traceNow's clock.
    // traceName names a label below kDspSpanLabels, and ignores one past it.
    void                      spanBegin(std::uint32_t label);
    void                      spanEnd();
    void                      spanSince(std::uint32_t label, double t0);
    double                    traceNow() const;
    double                    lastProcessEnd() const { return lastProcessEnd_; }
    void                      traceName(std::uint32_t label, const std::string& name);
    bool                      traceReset(bool arm);
    bool                      traceArmed() const { return traceArmed_; }
    std::vector<DspTraceSpan> traceSpans() const;
    std::vector<std::string>  traceNames() const;

    // Defined in ScriptHost.cpp: a script type, and one instance's record.
    struct Type;
    struct Instance;

private:
    struct Glue;  // the ABI tables, the Block object's methods and the audio runtime's allocator
    struct Counters {
        std::atomic<std::uint64_t> allocs{0}, reallocs{0}, frees{0}, bytes{0};
        // The live heap and its peak, in usable bytes: kept by a profile build only.
        std::atomic<std::int64_t>  liveBytes{0};
        std::atomic<std::uint64_t> peakBytes{0};
    };

    void releaseJs(Instance& in);   // the block thread, or quiescent
    void retire(Instance* in);      // control thread: a destroyed shim's record
    void releaseQueued();           // quiescent: everything queued, released and deleted
    void drainReleased();           // control thread: the records handed back, deleted, and the backlog moved on
    void collect();                 // quiescent: a GC of the audio runtime, when the load or a release has left
                                    //   garbage since the last
    void drainControlJobs();        // control thread: the promise jobs a call into the control context queued
    void runAudioJobs();            // the block thread: endBlock's promise jobs
    void freeTypes();

    // counters_ is the audio runtime's allocator opaque, so it is declared (and made) before it.
    Counters                     counters_;
    JSRuntime*                   controlRt_ = nullptr;
    JSContext*                   control_ = nullptr;
    JSRuntime*                   audioRt_ = nullptr;
    JSContext*                   audio_ = nullptr;
    std::uint32_t                blockClass_ = 0;  // the JSClassID of the Block object, in the audio runtime
    std::array<std::uint32_t, 16> atoms_{};        // JSAtoms the block path names properties by, in the audio runtime

    std::vector<std::unique_ptr<Type>> types_;
    std::vector<const rp_node_type*>   typeList_;
    rp_package                         package_{};
    bool                               loaded_ = false;  // load has been called

    std::atomic<bool>          audioOwns_{false};
    // destroy -> the block thread, and the released records back. A full release ring spills into the backlog (the
    // control thread's: a list threaded through the records themselves, so a destroy, which runs in a destructor,
    // allocates nothing), pushed on at the next destroy or reclaim; the way back, twice as deep, is drained by every
    // destroy and reclaim, and a record it cannot take is counted and leaked rather than freed on the audio thread.
    SpscRing<Instance*, 256>   releases_;
    SpscRing<Instance*, 512>   released_;
    Instance*                  backlogHead_ = nullptr;
    Instance*                  backlogTail_ = nullptr;
    std::atomic<std::uint32_t> leakedRecords_{0};
    std::uint32_t              leaksReported_ = 0;
    bool                       garbage_ = false;  // the load, or a release, since the last collect (whoever releases)

    // The call deadline: its length in CPU nanoseconds (set on the control thread), and while a call runs (armed_), the
    // thread's CPU time at the first poll in it (-1 until then) and whether the interrupt stopped it (the block thread's).
    std::atomic<std::int64_t>  callDeadlineNs_{std::chrono::nanoseconds(kDefaultCallDeadline).count()};
    bool                       armed_ = false;
    std::int64_t               cpuStart_ = -1;
    bool                       interrupted_ = false;

    std::atomic<std::uint32_t> throws_{0};
    std::atomic<std::uint32_t> live_{0};
    std::atomic<std::uint32_t> jobsCutShort_{0};
    bool                       jobsLogged_ = false;
    bool                       ownedRefusalLogged_ = false;
    bool                       refuseWhileOwned(const char* what);
    // The first throw, kept by the block thread in a fixed buffer (it allocates nothing native there): claimed
    // 0 -> 1, written, then published 1 -> 2.
    std::atomic<int>           firstThrowState_{0};
    char                       firstThrow_[512] = {};
    bool                       firstThrowLogged_ = false;

    // Profiling (kept by a profile build only; a handful of zeros otherwise). The allocation window: the counts and
    // live heap it opened at, the counts the current block began at, and the blocks it has run, with the worst one's
    // allocations.
    ScriptAllocations windowStart_{};
    std::int64_t      windowLiveBytes_ = 0;
    ScriptAllocations blockStart_{};
    std::uint64_t     windowBlocks_ = 0, maxBlockAllocs_ = 0, maxBlockBytes_ = 0;
    // The trace window: its clock's start (steady_clock nanoseconds), the spans open as {label, t0} and those closed.
    // While a process call's JS runs, jsSpanFloor_ is the open spans' count with its js-call span the last of them:
    // JS's own spans stand above it, and none is JS's while it is kNoJsSpans.
    static constexpr std::size_t                  kNoJsSpans = static_cast<std::size_t>(-1);
    bool                                          traceArmed_ = false;
    std::uint64_t                                 traceStartNs_ = 0;
    double                                        lastProcessEnd_ = 0.0;
    std::size_t                                   jsSpanFloor_ = kNoJsSpans;
    std::vector<std::pair<std::uint32_t, double>> traceStack_;
    std::vector<DspTraceSpan>                     traceSpans_;
    std::vector<std::string>                      traceNames_;
};

}  // namespace rp::host
