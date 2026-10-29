#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graph/rp_node.h"
#include "transport/SpscRing.hpp"

// Forward-declared so quickjs.h stays out of this header; these match quickjs.h's tags.
struct JSRuntime;
struct JSContext;

namespace rp::host {

// What the audio-side runtimes' heaps have done since the host was made, summed over every pack, cumulative: every
// block, adoption and release that entered JS. A test reads it either side of a stretch of blocks. Written only by the
// thread that runs blocks; any thread may read it, a read on another thread lagging a running audio thread.
struct ScriptAllocations {
    std::uint64_t allocs = 0;    // malloc and calloc calls
    std::uint64_t reallocs = 0;
    std::uint64_t frees = 0;
    std::uint64_t bytes = 0;     // asked for, by allocs and growing reallocs
};

// ---- profiling the audio runtimes (spec/08-profiling.md) --------------------------------------------------------
// Real only in a RETROPLUG_PROFILE build; in any other every field is 0 and `enabled` false. They cross the harness
// RPCs (dspAllocStats, dspRunGc, dspTrace) as they are, so their field names are what packages/retroplug/src/
// audioDriver.ts reads. "dsp" is the DSP (audio) thread, whose JS the script host's audio runtimes run.

// The allocation window: what the audio runtimes did since the last resetAllocStats(), every pack's summed.
// `liveBytesDelta` is the net change of their live heap (flat ~0: refcount churn; rising: a leak). The block counts are
// per Engine block, each measured from its beginBlock to its endBlock, so a benchmark reads them once per window.
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

// One self-driven collection pass (JS_RunGC of every pack's audio runtime): `freedBytes` ~0 proves they accumulate no
// reference cycles.
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

// The span labels. A script node's process call is labelled by its type, so a profile shows each type's cost, as the
// DSP kernel's per-role spans did inside its one node until B3b made each role a node of its own: the packs' types
// take DSP_SPAN_TYPE_BASE up, in the order they were registered, each named by its id. The label is the type's, not
// the node's, as the kernel's role spans were per role kind; two systems' nodes of one type still show as two spans a
// block (each its own process call, in plan order). A type id's label is given at its first registration and kept for
// the host's life, so one registered again has the label it had, where one per node id would come and go with every
// document, and a node's create is never told its id.
enum DspSpanLabel : std::uint32_t {
    DSP_SPAN_SCRIPT_NODE = 0,   // ScriptHost: the process call of a script node whose type has no label of its own (a
                                //   bundle registering more types than kDspSpanLabels leaves room for)
    DSP_SPAN_MARSHAL     = 1,   // ScriptHost: its inputs made JS values (the audio copies in and the transport, then
                                //   each input it reads)
    DSP_SPAN_JSCALL      = 2,   // ScriptHost: the JS_Call into its process
    DSP_SPAN_APU         = 3,   // Engine: the plan's whole run (the host inputs, the script nodes, every system, the
                                //   mixing, the host outputs), holding its script nodes' process spans
    DSP_SPAN_PUBLISH     = 4,   // Engine: the state pump to the control plane (the per-block frame copy, and the timed
                                //   savestate and SRAM republish, so spiky)
    DSP_SPAN_TYPE_BASE   = 16,  // ScriptHost: the process call of a script node of the first type registered, and up
};

// Every label a trace names is below this: the fixed ones, then one per script node type (the production bundle's are
// in packages/retroplug/src/script/bundle.ts), so the name table is bounded whatever the packs register.
inline constexpr std::uint32_t kDspSpanLabels = 1024;

// The script host (docs/modular-graph-phase-b3a-design.md section 2): every TS node one Engine runs, behind the
// same C ABI as a native node. It holds them in packs (docs/modular-graph-phase-c1-design.md section 4), the unit it
// isolates and loads: a pack has an id, a bundle that registers its node definitions (defineNode results, appended to
// the global __rp_scriptNodes: packages/retroplug/src/script/register.ts), and a package of its own, named by its id,
// in which the host registers an rp_node_type for each definition, whose instances are native shims. The first pack is
// the built-in one, "rp.script", which load fills with a bundle (an Engine's is embeddedBundle()); loadPack loads
// more beside it, each under an id of its own that names its types: every type id a loaded pack registers is the
// pack's id, a dot and more, so no pack takes another's type ids (where two namespaces meet, one id running on into
// another's, "a" and "a.b", the registry refuses the type the second would take).
//
// A pack is two bare QuickJS runtimes of its own, each loaded with its bundle, sharing no JS value with each other or
// with another pack's:
//   - the CONTROL side, entered only by the control thread, answers layout_for and prepare_settings: the
//     definition's own parseSettings checks a settings object, and its layout gives the ports, from the settings
//     alone. It never holds an instance.
//   - the AUDIO side holds every instance of the pack's types and, once the pack is loaded (below), is entered only by
//     the thread that runs blocks (the audio thread, or the control thread while setAudioThreadOwns(false)), which
//     reaches it only through an instance.
//     A shim's create and activate run on the control thread and only allocate the shim; the JS instance is created on
//     the block thread at the first call the shim gets there, which is the ports_changed the planner sends every fresh
//     instance when its plan is adopted, so before that plan's first process. A shim's destroy (control thread) queues
//     the instance's release, which beginBlock performs at the start of a later block (at most kReleasesPerBlock a
//     block), or does it there and then while no audio thread runs; the record it frees then goes back to the control
//     thread to be deleted (reclaim), so the audio thread frees nothing native. What a released instance leaves in
//     cycles is collected by a GC of its pack's audio runtime at two points no block is running: the reclaim that ends
//     a quiescent operation which released some (the Engine's after every setGraph: one GC a pack however many went),
//     and the audio thread handing the runtimes back (setAudioThreadOwns(false)). The first reclaim after the built-in
//     pack's load (an Engine's first setGraph, at its top) takes what its bundle's load left in cycles too, whether or
//     not an instance released anything; a loaded pack's load collects its own before loadPack returns. Never by a
//     block, so while an audio thread owns the runtimes (a live session) those cycles wait for QuickJS's own threshold
//     GC, which runs inside whichever block's JS allocates past it in that runtime, or for the audio thread to hand the
//     runtimes back.
// A pack is made and loaded on the control thread, its audio side included, so loadPack runs while an audio thread
// runs blocks in the other packs: the block thread first reaches a pack through an instance of one of its types, in a
// plan published after its load, and the plan swap's ring (release on push, acquire on adopt) carries the whole built
// pack with it. Until its load is done, a pack's audio runtime touches nothing the block thread writes: it joins what
// every pack shares (the call deadline's interrupt handler, the allocation counters) as its load ends (join). So what
// a loaded pack's load allocates is left out of the allocation counters, which count the audio side's blocks,
// adoptions and releases; the built-in pack's, at the host's construction, comes before any and is in them.
// A pack loaded again is reloaded (docs/modular-graph-phase-c1-design.md section 5): loadPack on an id loaded builds
// its next generation beside the current one, as a load builds a pack, under the same id and namespace, and the caller
// settles which stands once it knows whether a plan placing the new one's nodes was published: retirePack retires the
// old generation, discardPack the new one. Either way that generation's instances go the way every released instance
// goes, and a pack counts the records of its types' instances alive (create makes them, and drainReleased,
// releaseQueued and retire's quiescent path delete them, all on the control thread, so a plain count). A retired or
// discarded generation whose count is 0 is freed by the call that finds it so (retirePack, discardPack, reclaim,
// setAudioThreadOwns(false)), on the control thread: its runtimes and contexts, its types' definitions and its atoms,
// with the pack-freed callback told a retired one's package first (an Engine's registry drops its retired entries
// then). That holds while an audio thread runs blocks in other packs: the last record came back through the release
// ring after the block thread released its JS (or was released on this thread while none ran), so the block thread
// holds nothing of the pack, no plan it may yet adopt places one of its nodes (that plan would hold a record), and no
// JS value crosses packs, so no other heap points into its. Its audio runtime first leaves what every pack shares
// (leave, join's reverse): what freeing it frees is kept out of the allocation counters, as a loaded pack's load is,
// since the block thread may be writing them. So a whole generation's heap is freed off the audio thread, where an
// instance's release frees its JS on it. A record the release ring back could not take, leaked rather than freed on the
// audio thread, leaks its pack with it. The built-in pack is never retired.
// What every pack shares stays the host's: the call deadline (one call runs at a time on the block thread), the
// allocation counters (summed over every pack's audio runtime, so audioAllocations and the profile's allocation window
// mean the whole audio side), the release rings, the throw count and the first throw, and the profile's span labels.
// What is a pack's own: its promise-job queue, its memory limit, its cycle GC and its quiescent collection.
//
// Every runtime's JS stack is held to kStackBytes, under the smallest thread that may enter one: 512 KiB is a macOS
// secondary thread's default, which a DAW's audio or control thread may have and a render job's std::thread does
// (no host here sizes its threads), so 256 KiB leaves the caller's frames half. Loading the production bundle needs
// 4 KiB of it; the test bundle with its toys, planned (their settings' parse, refusal and JSON schema, measured while
// those were zod's, before B3b), 40 KiB, 64 under TSan and 96 under ASan.
//
// Every call into an audio runtime from the block thread is held to a deadline (setCallDeadline) of the calling
// thread's CPU time, so a stall that is not the node's (preemption, a stopped process, a debugger or a VM pause) never
// trips it. One deadline spans a call into a node, the create a fresh instance makes first, and the promise jobs they
// queue (below). A call that runs past it is stopped (QuickJS's interrupt, which JS cannot catch) and counted as a
// throw. A node with faultOnThrow then faults; one without runs again next block (design section 4.5), and faults only
// if its next call is stopped too, so a node that hangs is bypassed within two deadlines and one that ran long once is
// not. A call that runs to its end, its jobs with it, starts the count again. A deadline fault is a fault like any
// other: it latches on the instance and outlives reshapes, and only a rebuild (or the node removed and added again)
// clears it.
// The control thread's calls into a pack are held to the same length of its own CPU time, on a clock of its own (the
// block thread may be inside a call into another pack meanwhile, so the two share no deadline state): a load, one call
// (its chunks in both runtimes, the jobs they queue, its definitions read), and a settings check (parseSettings and
// layout, for layout_for, prepare_settings and create, and the jobs they queue). A pack's control runtime has the
// control thread's handler for its life, its audio runtime until join gives it the block thread's. A load the deadline
// stops refuses the pack; a settings check it stops refuses the settings, saying so, so the planner refuses the
// document and the running plan stands. A settings check runs at most kJobsPerCheckpoint of the promise jobs pending
// in its pack's control runtime, as a load does each stretch of it, and one that leaves any refuses the settings too,
// saying so. QuickJS cannot drop a pending job, so what is left stays there for the pack's next settings check, which
// runs it with its own: a chain that never settles refuses every settings check of the pack, and so every document
// placing one of its nodes, until a reload frees the runtime it lives in.
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
// A node pays for its own promise jobs and memory (docs/modular-graph-phase-c1-design.md section 6). The jobs a call
// into a node's JS queues (its onSettings, portsChanged, activate or process, and the create a fresh instance's first
// call makes before it) run at a checkpoint right after the call, inside its deadline, at most kJobsPerCheckpoint of
// them: a create's with those of the call that made it, or at a checkpoint of its own when that call has no hook to
// run. They are charged to that node: a job that fails (a queueMicrotask callback that threw, or the platform's
// allocator out of memory) is its throw, and faults it under faultOnThrow; one the deadline stops counts as its call
// stopped (the rule above); and a chain still pending after the bound is cut short, its throw, which faults it at
// once under faultOnThrow and otherwise on the second cut short in a row, as two expiries do. QuickJS cannot drop a
// pending job, so a chain cut short stays queued and its pack remembers whose it is until the queue empties: its
// later checkpoints (the next calls into any of its nodes) run the rest charged to that node, never to a pack-mate,
// and charge it every other job they run meanwhile too, a pack-mate's own included. So a pack-mate that starts a
// chain of its own that never settles while another node's leftovers are pending has its cut shorts charged to that
// node, which faults for them while the pack-mate runs on; and once that node is released the chain is no node's, run
// kJobsPerCheckpoint at a time at every checkpoint of the pack until the pack is reloaded, the node that started it
// never charged or bypassed. What is queued where no call into a node ran, by a release freeing a node's JS or by a
// quiescent collection finding a FinalizationRegistry callback's target, runs there and then, charged to the
// remembered owner when the pack has one, else to no node, as is what a released owner leaves (a throw counted,
// faulting no one); what a reshape's freeing queues runs at a checkpoint after its commit, charged to its node. A
// fault charged to the node whose process this is returns RP_FAULT; one after a hook, or charged to another node of
// the pack whose leftovers ran, faults that node at its next block. A promise reaction's own throw only rejects its
// promise, which is the node's to handle. A load's own jobs run as it loads, at most kJobsPerCheckpoint each stretch
// of it (its top level in each runtime, each descriptor, what reading the definitions after the last one queued, what
// a loaded pack's collection queues), and a load that leaves any is refused: they would run at the pack's first
// checkpoint, charged to whichever of its nodes' calls came first, or, in the control runtime, at its first settings
// check.
//
// Each of a pack's two runtimes is held to its memory limit (kDefaultMemoryLimit, unless its load says otherwise), on
// its own, by the host's allocator, which refuses an allocation past it, so QuickJS throws out of memory in the call
// that asked, and notes the refusal on the pack. The limit holds each runtime whole, its intrinsics (about 90 KB) and
// the pack's loaded code with them, so one too small for those refuses the load, which says so. Once the pack is
// loaded, a refusal in its audio runtime during a call, or its jobs, faults the node they are charged to, whatever its
// faultOnThrow and whether or not JS caught the error (a refusal JS caught is counted as its throw), since a pack at
// its limit is one none of its nodes can rely on: at once in its process (RP_FAULT), at its next block after a hook or
// for its leftovers run at a pack-mate's checkpoint. Only an out-of-memory error the limit did not cause, the
// platform's allocator running out, is a throw like any other, faulting under faultOnThrow. A call is charged only
// what its pack refuses after it begins: the refusal note is cleared as each call arms its deadline, so a refusal made
// where no checkpoint looks (a release freeing a node's JS, a quiescent collection, the text the host makes of a
// checkpoint's last job's throw) faults no node. Another pack is untouched. In the control runtime the limit holds
// the pack's top level, parseSettings and layout, which hold no instance: a load or a settings check that fails on an
// allocation it refused is refused, saying so (a settings check's refusing the document, so the running plan stands),
// and one that catches the error runs on, there being no node to fault.
//
// A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
// (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch); a
// faultOnThrow node whose onSettings, portsChanged or activate threw faults next block, since it may hold half of what
// the hook set. Returning "fault" faults it too. The first throw's message is kept, and logged by reclaim. A write the
// block path makes onto what the node holds that fails (a setter the node hung there threw or ran past the deadline, or
// it froze the object) is the node's throw too: messages() and control() throw it, and on its audio views, the Block
// object or its transport, written before process is called, it is a throw in process whose block does not run (its
// audio outputs silent). Every node of a pack shares its realm, so the host keeps its writes, and its reads of a node's
// methods, off the prototypes they all share. Each field it fills it makes the object's own data property when it makes
// the object (a define, which never walks the prototype chain), and each slot of an array it writes as the slot is at
// that write: set when it is the array's own, defined when it is not (never made, or taken back by the node with a
// shift, a splice or a shorter length). So only the node's own setters run there, never an accessor another node (or
// the bundle) hung on Object.prototype or Array.prototype, with one gap: a field the node deletes is set again, which
// walks the chain, as does the host's read of a message's data the node deleted (asking at every field write would cost
// each script call about 12 ns a block, 14% of an empty pass-through's). It reads process and the hooks from the object
// create returned and the prototypes it inherits them through, short of Object.prototype, so it neither runs nor adopts
// a getter or a function another put there. A Proxy on the way answers as JS reads one (its get trap, or its target's
// chain), so a wrapper or a test double works; a chain longer than kMethodHops, or one that cycles back through a
// Proxy, fails the create rather than holding the block thread, where the deadline has no JS to stop. And it freezes
// the Block prototype every node reaches, with its methods, so no node replaces one for another: assigning one on a
// node's own Block (`block.send = f`) throws in the strict bundle, where defining it there still works. That is the
// host's part only: what the nodes of a pack can do to each other through the realm they share (an accessor on a
// prototype another's own code reads through, a global it calls) it does not cover. Only nodes of different packs are
// kept apart, each pack's realm in runtimes of its own (docs/modular-graph-phase-c1-design.md section 12).
//
// Settings reach the audio side as their JSON form: the control side's parseSettings checks them, and what it
// returns crosses as JSON (a pack's two runtimes share no value), so a transform's result that JSON cannot hold (a
// Map, a Date, undefined) arrives as JSON spells it. The audio side never runs the schema, so it does no validation work at
// a swap (and no node's schema is zod's: since B3b the bundle holds none, graph/settingsSchema.ts's instead).
//
// A profile build (RETROPLUG_PROFILE, spec/08-profiling.md) profiles the audio runtimes: their allocator keeps the
// live and peak heap, every pack's summed, an allocation window counts per block, and a trace window records nested
// wall-time spans. Every script node's process is a span labelled by its type (DspSpanLabel), holding a marshal span
// for the audio copies in and the transport and a js-call span for its JS, inside which each input it reads is a
// marshal span too. Only the host opens spans, and it closes each on every way out of the call (a throw, the deadline,
// a write onto what the node holds refused before its JS runs). JS adds none: the DSP kernel's roles were the only JS
// that did, through globals a profile build bound into the audio context, and with each role a node of its own the
// host's spans attribute per role without them.
class ScriptHost {
public:
    using Chunk = std::span<const std::uint8_t>;
    // Globals a host binds into one of a pack's contexts before any bundle runs in it (the native tests' probes),
    // `audioSide` saying which; called once for each of every pack's two, on the thread that makes the pack (the
    // constructing thread, for the built-in one).
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
    // Load the built-in pack: run a bundle (global-code bytecode, in chunks run in order) in both of its contexts, then
    // read the definitions it registered, each of its runtimes held to `memoryLimit` bytes (0: kDefaultMemoryLimit)
    // from then on, the load included. Once, before anything plans with the package: a second call is refused, and
    // leaves what the first registered as it is. False, with a reason, when a chunk fails to load (or runs out of
    // memory), the load runs past the call deadline, the promise jobs it queues do not settle in kJobsPerCheckpoint
    // (the class comment), or a definition cannot be read or is not the same in both contexts (its id or its
    // descriptor differs: a bundle that branches on which context it runs in). One bad definition refuses the whole
    // package: it then has no types, so every document naming any of the bundle's is refused (unknown-type).
    bool load(std::span<const Chunk> chunks, std::string* error = nullptr, std::size_t memoryLimit = kDefaultMemoryLimit);
    // What each of a pack's runtimes may hold, in usable bytes, unless its load says otherwise (the class comment).
    static constexpr std::size_t kDefaultMemoryLimit = std::size_t{64} << 20;
    // The built-in pack's script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a
    // load).
    const rp_package* package() const;
    // Load a pack beside the built-in one (docs/modular-graph-phase-c1-design.md sections 4 and 5): a pack named `id`,
    // its two runtimes made for it, runs a bundle (global-code bytecode, in chunks run in order) in both, each held to
    // `memoryLimit` bytes (0: kDefaultMemoryLimit), the load included, and its definitions are read and checked as
    // load checks the built-in pack's. Callable while an audio thread runs blocks (the class comment). On an
    // id loaded already it builds the pack's next generation (one more than the newest of the id held), beside the
    // current one, until retirePack or discardPack settles which goes. Its package, named by `id`, for the registry
    // (NodeRegistry::addPackage, or replacePackage for a new generation), valid until the pack is freed; null, with a
    // reason, when canLoadPack refuses the id (nothing is built), or, the pack freed there and then, when a chunk fails
    // to load (or runs out of memory), the load runs past the call deadline, the promise jobs it queues do not settle
    // in kJobsPerCheckpoint, a definition cannot be read or is not the same in both contexts, or a type id is not the
    // pack's id, a dot and more ("user.synth.osc" in "user.synth").
    const rp_package* loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
                               std::string* error = nullptr);
    // Whether loadPack takes a pack named `id`: a plain id (graph::isPlainId), not the built-in pack's ("rp.script",
    // which is not reloadable), and not otherwise the app's ("rp" and every id under "rp.", where its native packages
    // are named too). False, with the reason.
    bool canLoadPack(const std::string& id, std::string* error = nullptr) const;
    // Retire the generation `package` names, which a reload replaced: a plan placing the new generation's nodes in
    // place of its own is published (or none placed its own). Freed at once when no record of its types' instances is
    // alive, else by the call that finds the last one back (the class comment), the pack-freed callback told its
    // package first either way. Never the built-in pack.
    void retirePack(const rp_package* package);
    // Free a pack loadPack returned that never stood: a registry refused its package, or the replan of the reload that
    // built it was refused. There and then when no record of its types' instances is alive; else (instances a refused
    // plan made, whose releases the block thread has yet to hand back) by the call that finds the last one back. No
    // registry holds its entries, so the pack-freed callback is not told. Never the built-in pack.
    void discardPack(const rp_package* package);
    // Told, on the control thread, of each retired generation as it is freed, before its types go: `user` as given, and
    // its package, whose retired entries whatever holds them drops then (an Engine's registry:
    // NodeRegistry::dropRetired). Never by the destructor, which frees every pack it holds.
    using PackFreed = void (*)(void* user, const rp_package* package);
    void setPackFreed(PackFreed freed, void* user);
    // A pack held: its id, its generation (its load's count: 1, then one more each reload), whether it is a retired or
    // discarded one waiting for its records, and how many records of its types' instances are alive.
    struct PackInfo {
        std::string   id;
        std::uint32_t generation = 0;
        bool          retired = false;
        std::uint32_t records = 0;
    };
    // Every pack held, the built-in one first, then in load order: a retired or discarded generation among them until
    // it is freed.
    std::vector<PackInfo> packs() const;
    // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
    // queued instance there and then, frees each retired or discarded pack whose last records that brings back, and
    // collects what they left.
    void setAudioThreadOwns(bool owns);
    // How much of the calling thread's CPU time one call into a pack's JS may use before it is stopped, on either side
    // (kDefaultCallDeadline until set: the class comment). Set it before an audio thread runs blocks.
    void setCallDeadline(std::chrono::milliseconds limit);
    static constexpr std::chrono::milliseconds kDefaultCallDeadline{2000};
    // Delete the records the block thread has released, free each retired or discarded pack whose last records that
    // brings back (the class comment), and log the first throw once. While quiescent, also collect what instances
    // released since the last left in cycles, and, the first time after a pack's load, what its bundle's load left
    // (whether or not anything was released), in each pack that has some: call it after an operation that may release
    // some.
    void reclaim();
    // The first throw's type and message ("" until one), once the block thread has kept it.
    std::string firstThrow() const;

    // ---- the thread that runs blocks ---------------------------------------------------------------------
    // At the top of every block, before the graph runs: release what destroy queued, at most kReleasesPerBlock (a
    // release frees an instance's whole JS state, which for a large node is a great many objects: a plan of hundreds
    // going at once is spread over blocks, and a full ring still drains in 16).
    void beginBlock();
    static constexpr std::uint32_t kReleasesPerBlock = 16;
    // At the end of every block that ran the graph: the allocation window's per-block counts (a profile build). Its
    // promise jobs ran at its calls' checkpoints.
    void endBlock();
    // The most of a pack's pending promise jobs one run takes (the class comment): a checkpoint after a call into a
    // node, a run outside any (a release's), each stretch of a load (one that leaves any refuses the load), and a
    // settings check's drain of the control side (one that leaves any refuses the settings).
    static constexpr int kJobsPerCheckpoint = 256;
    static constexpr std::size_t kStackBytes = 256 * 1024;  // each runtime's JS stack (the class comment)
    // How many plain objects reading a node's process or a hook walks (what its create returned, then the prototypes
    // it inherits from, short of Object.prototype) before it refuses, failing the create (the class comment).
    static constexpr int kMethodHops = 1024;

    // ---- any thread ------------------------------------------------------------------------------------------
    // Throws caught in a script node (process, and its create, onSettings, portsChanged and activate) so far; the jobs
    // that failed: a queueMicrotask callback that threw, or a job the deadline stopped or JS ran out of memory in, each
    // charged to the node the class comment says, or to no node (one run where no call ran with no owner remembered,
    // or a released owner's leftovers); the runs that cut a node's jobs short; and the allocations a pack refused that
    // JS caught. A promise reaction's own throw only rejects its promise, which is the node's to handle, so it is not
    // counted.
    std::uint32_t     throwCount() const { return throws_.load(std::memory_order_relaxed); }
    ScriptAllocations audioAllocations() const;
    // JS instances created and not yet released.
    std::uint32_t     liveInstances() const { return live_.load(std::memory_order_relaxed); }
    // Runs of a pack's promise jobs (a checkpoint's, or one where no call ran: a release's, a quiescent collection's)
    // that outran kJobsPerCheckpoint, leaving some for its next one; reclaim says so once.
    std::uint32_t     jobsCutShort() const { return jobsCutShort_.load(std::memory_order_relaxed); }

    // ---- profiling (spec/08-profiling.md): real only in a RETROPLUG_PROFILE build --------------------------------
    // The allocation window, opened by resetAllocStats, which can pin every pack's audio runtime's auto-GC off from
    // then on, a pack loaded later's too (so a window collects nothing); runGc runs and times one collection of each.
    // allocStats never refuses (a benchmark reads its `enabled` to know a profile host), and read on another thread
    // while blocks run lags them.
    DspAllocStats allocStats() const;
    // These refuse, doing nothing, while an audio thread owns the runtimes (setAudioThreadOwns): resetAllocStats and
    // traceReset return false and runGc a result with enabled false (the first refusal said once on stderr), and the
    // trace reads, traceSpans and traceNames, return nothing, silently. The check is sound because these and
    // setAudioThreadOwns are called on the same control thread: an audio thread starts only after that thread has set
    // it true, which it cannot do while inside one of these.
    bool          resetAllocStats(bool disableAutoGc);
    DspGcResult   runGc();
    // The trace window. traceReset clears it and arms (or disarms) it, quiescent, as are the reads; the block thread
    // records, and only while it is armed, so a window armed before an audio thread starts records (and grows) until
    // that thread hands the runtimes back. spanBegin and spanEnd bracket a span; spanSince records one from `t0`, a
    // traceNow(), to now. traceName names a label below kDspSpanLabels, and ignores one past it.
    void                      spanBegin(std::uint32_t label);
    void                      spanEnd();
    void                      spanSince(std::uint32_t label, double t0);
    double                    traceNow() const;
    void                      traceName(std::uint32_t label, const std::string& name);
    bool                      traceReset(bool arm);
    bool                      traceArmed() const { return traceArmed_; }
    std::vector<DspTraceSpan> traceSpans() const;
    std::vector<std::string>  traceNames() const;

    // Defined in ScriptHost.cpp: a pack, a script type, and one instance's record.
    struct Pack;
    struct Type;
    struct Instance;

private:
    struct Glue;  // the ABI tables, the Block object's methods and the audio runtimes' allocator
    struct Counters {
        std::atomic<std::uint64_t> allocs{0}, reallocs{0}, frees{0}, bytes{0};
        // The live heap and its peak, in usable bytes: kept by a profile build only.
        std::atomic<std::int64_t>  liveBytes{0};
        std::atomic<std::uint64_t> peakBytes{0};
    };

    // control thread: a pack named `id` with its two runtimes made (makeRuntimes), loaded with nothing yet, its audio
    // runtime's allocations counted in `counters` until it joins
    std::unique_ptr<Pack> makePack(const std::string& id, Counters& counters);
    void makeRuntimes(Pack& pack);  // control thread: its runtimes and contexts, set up as the class comment says
    void freeRuntimes(Pack& pack);  // control thread or quiescent: its types first (freeTypes), then its runtimes
    // control thread: run a bundle in both of the pack's contexts, read and check its definitions (a loaded pack's
    // type ids in its namespace), and label its types
    bool loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error);
    // control thread, its load done and before any plan can place its types: the pack's audio runtime joins what every
    // pack shares (the class comment)
    void join(Pack& pack);
    // control thread, no record of its types alive: the pack's audio runtime leaves what every pack shares, its frees
    // counted in `counters` from then on
    void leave(Pack& pack, Counters& counters);
    void freeTypes(Pack& pack);
    using Packs = std::vector<std::unique_ptr<Pack>>;
    Packs::iterator findPack(const rp_package* package);  // a pack loadPack returned, by its package, or end
    void freeIdle();                    // control thread: every retired or discarded pack with no record alive, freed
    Packs::iterator freePack(Packs::iterator at);  // control thread: one of those, freed; the pack after it
    void settleFreedLive();             // control thread: a profile build's live heap, past freed packs, once quiescent
    void deleteRecord(Instance* in);    // control thread: a record its release is done with, off its pack's count
    std::uint32_t spanLabelFor(const std::string& typeId);  // control thread: a type id's label (DspSpanLabel)
    void releaseJs(Instance& in);   // the block thread, or quiescent
    void retire(Instance* in);      // control thread: a destroyed shim's record
    void releaseQueued();           // quiescent: everything queued, released and deleted
    void drainReleased();           // control thread: the records handed back, deleted, and the backlog moved on
    void collect();                 // quiescent: a GC of each pack's audio runtime whose load or releases have left
                                    //   garbage since its last
    bool collectAudio(Pack& pack);  // quiescent: one GC of its audio runtime; whether it queued jobs (collect)
    // control thread: the jobs a call into its control context queued, at most kJobsPerCheckpoint; whether they settled
    static bool drainControlJobs(Pack& pack);

    // Every audio runtime's allocator opaque (its pack's Heap) points at counters_, so it is declared (and made) first.
    Counters                     counters_;
    Binder                       binder_ = nullptr;
    void*                        binderUser_ = nullptr;
    // Every pack, the built-in one first. Made, loaded and read on the control thread (and quiescent, by releaseQueued,
    // collect and the profile calls), so it grows (loadPack) and shrinks (freePack) there, while an audio thread runs
    // blocks; the block thread reaches a pack only through an instance of one of its types, never through this list.
    Packs                              packs_;
    bool                               loaded_ = false;  // load has been called
    bool                               gcPinned_ = false;  // resetAllocStats pinned the audio runtimes' auto-GC off
    PackFreed                          packFreed_ = nullptr;
    void*                              packFreedUser_ = nullptr;
#ifdef RETROPLUG_PROFILE
    // What freed packs had added to the live heap in counters_ since they joined, taken off it once no audio thread
    // runs (settleFreedLive): the control thread's.
    std::int64_t                       freedLive_ = 0;
#endif
    // Each type id's span label (DspSpanLabel), given at its first registration in any pack and kept, and the next one
    // to give (control thread).
    std::unordered_map<std::string, std::uint32_t> spanLabels_;
    std::uint32_t                                  nextSpanLabel_ = DSP_SPAN_TYPE_BASE;

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

    // The call deadline: its length in CPU nanoseconds (set on the control thread), and while a call runs (armed_), the
    // thread's CPU time at the first poll in it (-1 until then) and whether the interrupt stopped it (the block thread's).
    std::atomic<std::int64_t>  callDeadlineNs_{std::chrono::nanoseconds(kDefaultCallDeadline).count()};
    bool                       armed_ = false;
    std::int64_t               cpuStart_ = -1;
    bool                       interrupted_ = false;
    // The control thread's own: its calls into a pack's JS (a load, a settings check) run while the block thread may be
    // inside one into another pack, so the two clocks share nothing but the length.
    bool                       controlArmed_ = false;
    std::int64_t               controlCpuStart_ = -1;
    bool                       controlInterrupted_ = false;

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
    // allocations. Those three are written by endBlock on the block thread, or zeroed by resetAllocStats while no
    // audio thread runs, so one writer at a time; atomic for allocStats, which does not refuse and may read them from
    // another thread while blocks run.
    ScriptAllocations          windowStart_{};
    std::int64_t               windowLiveBytes_ = 0;
    ScriptAllocations          blockStart_{};
    std::atomic<std::uint64_t> windowBlocks_{0}, maxBlockAllocs_{0}, maxBlockBytes_{0};
    // The trace window: its clock's start (steady_clock nanoseconds), the spans open as {label, t0} and those closed.
    bool                                          traceArmed_ = false;
    std::uint64_t                                 traceStartNs_ = 0;
    std::vector<std::pair<std::uint32_t, double>> traceStack_;
    std::vector<DspTraceSpan>                     traceSpans_;
    std::vector<std::string>                      traceNames_;
};

}  // namespace rp::host
