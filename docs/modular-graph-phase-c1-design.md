# Phase C1: script node packs, each in its own runtimes

**Date:** 2026-10-29 · **Branch:** `feature/modularize` · **Status:** built: section 11's five commits; what
building them changed is in the sections it changed, and what it found is section 13

Every script node an Engine runs shares one QuickJS realm and one promise-job queue, so one node's bug can reach
another: a getter it hangs on `Object.prototype`, a global it replaces, a promise chain that never settles, a heap it
fills. Before anyone writes nodes of their own (Phase C, the Reaktor-scale aim), the script host is to contain that.
This phase makes the **pack** the unit the script host isolates, loads and replaces: each pack runs in a QuickJS
runtime pair of its own, the built-in bundle becomes the first pack, a pack can be loaded or reloaded while audio
runs, and what a node's promise jobs and allocations do is charged to that node. It is the first of Phase C's pieces
([modular-graph-design.md](modular-graph-design.md) section 8, [spec/07-remaining-work.md](../spec/07-remaining-work.md)
section 5: "One QuickJS context per script node, decided before user-written nodes"), the others being the live knobs
as node settings (C2), system nodes building their own cores (C3), console code into the nodes and the capability
interfaces (C4), and hardware as nodes (C5).

## 1. What happens today

`ScriptHost` ([ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp)) owns two bare QuickJS runtimes per
Engine, loaded at construction with the embedded script-node bundle (B3a,
[modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md) section 2): a control side that runs each
type's `parseSettings` and `layout` for the planner, and an audio side that holds every instance and runs `process`,
entered only by the thread that runs blocks. Neither is txiki: the control plane's txiki runtime, the stores and the
editor, is a third, and no JS value passes between any two ([spec/01-architecture.md](../spec/01-architecture.md)
section 6). A second `load` is refused.

So every script node shares the audio side's one context:

- **One realm.** The host keeps its own writes and its reads of a node's methods off the shared prototypes, but a
  node's own code can still change what another's reads through: an accessor on `Object.prototype` or
  `Array.prototype`, a global, an entry in `__rp_scriptNodes`. A field a node deletes puts the host's next write of it
  back on the chain ([spec/02-native-host.md](../spec/02-native-host.md), the script host).
- **One job queue, charged to no one.** `ScriptHost::runAudioJobs` runs every node's promise jobs at the block's end
  under one deadline, and counts a failed one as nobody's throw, so a node whose reaction queues another every block,
  or runs long every block, is never bypassed: it can burn a full deadline (2 s of CPU) a block.
- **One heap, one GC.** A node that allocates without bound takes the heap from every other, and QuickJS's cycle GC,
  which runs inside whichever block's JS allocates past its threshold, sweeps every node's objects at once.
- **Loaded once, at construction, by the block thread's runtime.** Anything loaded into that runtime while audio
  runs would have to be loaded on the block thread.

## 2. Decisions taken

The user's, on 2026-10-29:

- **The isolation is for bugs, not security.** A buggy node (a patched prototype, a leaked global, a runaway promise
  job, a heap blow-up) should fail alone and pay for its own work. A hostile node is out of scope: downloaded native
  packs already run unsandboxed ([modular-graph-design.md](modular-graph-design.md) section 9), so a sandbox for TS
  ones alone would buy nothing.
- **Nodes will arrive as downloaded packs, and users will build their own with hot reloading.** So the unit is the
  pack: what an author writes, what is downloaded, and what a reload replaces.
- **A runtime pair per pack** (approach A), rather than a context per pack in today's one audio runtime (B), which
  would build every pack loaded or reloaded during playback on the block thread and share one heap, GC and memory
  limit between packs, or a context per instance (C), 140 to 300 KB and 0.2 to 0.4 ms per node made on the block thread,
  for isolation only between nodes of one author's code.
- **C1's scope** (sections 4 to 7): packs in the script host, loading and reloading one while the engine runs,
  each node charged for its jobs and its pack's memory limit, and a minimal `loadScriptPack` call. Left to later work:
  the authoring loop, the pack format and downloads, a node's state across a reload, isolation inside a pack, and
  everything about a pack's UI half (section 8).
- **A pack's type ids are namespaced** under its id, the built-in pack is not reloadable, and a pack's memory limit
  is 64 MiB unless its load says otherwise (section 4).
- **A reload is all or nothing**, as `setGraph` is; there is no unload; a pack is loaded into one Engine (section 5).
- **A pack's promise jobs run after each call into one of its nodes**, charged to that node, and hitting the pack's
  memory limit faults the node whose call hit it (section 6).

Two more, the controller's, taken while building to keep to the user's intent:

- **`rp` and every id under `rp.` are the app's to give** (its built-in pack's and its native packages': `rp.core`,
  `rp.host` and the rest), so no loaded pack may take one (section 4). Namespacing alone keeps a pack off another's
  type ids, but a pack named `rp.midi` would hold the namespace of the built-in `rp.midi.route`, and one named after
  a native package, its types': a type the app adds there later could collide with a user's.
- **The control thread's calls into a pack are held to the call deadline too**, on a clock of their own (section 4).
  The design held only the block thread's, but a pack's top level, `parseSettings`, `layout` or descriptor that never
  returns would hang the control thread that loads the pack or plans a document placing it, which is a buggy node
  failing everything rather than alone.

## 3. What a context costs

Measured with a throwaway program (`/tmp/c1meas/meas.cpp`) linked against the build's own `libqjs.a` (Release,
`-O3`) and the production script bundle (51.5 KB of bytecode), on the Ryzen 9 9950X; memory is the allocator's live
usable bytes, averaged over 200:

| What | Memory | Time to make |
|---|---|---|
| A runtime | 21 KB | |
| Its first context (every intrinsic) | 69 KB | 103 us |
| The bundle loaded into it | 205 KB | 374 us |
| Each further context in that runtime, the bundle loaded | 214 KB | 403 us (44 + 359) |
| A context with only base objects, JSON, typed arrays, promises, maps and proxies, bare | 45 KB | 34 us |
| The same with the bundle | 139 KB | 183 us |
| A runtime of its own (runtime, context, bundle, a GC) | 296 KB | 322 us |
| A cycle GC of one runtime holding 200 contexts with the bundle (41 MB) | | 12.2 ms |
| A cycle GC of one runtime holding one | | 0.06 ms |

So a context per node costs 140 MB or more at a thousand script nodes, each made on the block thread, and a shared
heap's GC grows with every node in it. A pack is two runtimes, a control one and an audio one, each with its first
context and its code loaded: about 180 KB (twice 90: 21 for a runtime, 69 for its first context) and twice what its
code loads to, so about 590 KB for a pack the size of the production bundle (twice 296), made off the block thread,
and each pack's GC covers its own heap. The built-in pack's two runtimes are the two the script host had, so it costs
nothing new.

Measured in the Engine once built (Release, through `test:native`, the same machine): a toy pack of two nodes holds
about 101 KB in its audio runtime (the allocator's usable bytes) and 107 KB in its control runtime (QuickJS's own
count), about 210 KB the pair, and `loadScriptPack` takes 0.3 to 0.45 ms from source, its compile included (a reload
that replans, 0.5 to 0.6 ms). The production bundle's source loaded as a pack, its type ids renamed into the pack's
namespace, holds about 366 KB in its audio runtime and 391 KB in its control one, 760 KB the pair, loaded in 4.4 to
5.3 ms: more than twice 296 KB, because a pack compiled from source keeps its debug information (its lines, which a
thrown error's stack names), where the built-in bundle is compiled stripped (`tjsc -s`).

## 4. Packs in the script host

`ScriptHost` stops being one pair of runtimes and becomes a holder of packs.

- **A pack** is an id, a generation (which of its id's loads made it: 1 for the first, every load of the id counted,
  refused ones included, section 5), a control runtime and context, an audio runtime and context, its types, and its own
  `rp_package` (manifest `{"id": "<pack id>"}`, the id written as a JSON string, since a plain id may hold `"` or
  `\`). Everything the audio side holds per runtime today is the pack's: the `Block` class and its frozen prototype,
  the atoms the block thread names properties by, the address of `Object.prototype`, the interrupt handler (with the
  host as its opaque). The audio context's opaque is its pack, through which a `Block` method finds its pack's class
  and the host. A `Type` names its pack, and an `Instance` reaches its runtime through its type.
- **The built-in pack** is today's bundle, as pack `rp.script`: the same type ids, the same code, loaded at the Engine's
  construction as now (`ScriptHost::load` keeps its signature, a defaulted memory limit added, and loads this pack).
  The binder a test host passes binds into every pack's two contexts.
- **A loaded pack** (`loadScriptPack`, section 7) is checked before anything is built:
  1. Its id is a plain id (the registry's `isPlainId`), not `rp.script`, which is not reloadable, and not otherwise
     the app's: `rp` and every id under `rp.` (section 2). The id is checked first, before the source is compiled.
  2. Each type id starts with the pack id and a dot (`user.mysynth.osc` in `user.mysynth`), so a pack can never
     shadow a built-in type or another pack's. Where one pack's id runs on into another's (`a` and `a.b` can both
     name `a.b.c`), the registry refuses the type the second would take, and the load or reload that brings it.
  3. Its source is global code, the IIFE a bundler makes, appending `defineNode` results to `__rp_scriptNodes`
     ([register.ts](../packages/retroplug/src/script/register.ts)), as the built-in bundle does. It is compiled to
     bytecode on the control thread by `dsp::compileToBytecode`
     ([ScriptCompiler.hpp](../packages/native/src/host/dsp/ScriptCompiler.hpp)), which returns the compile error's
     text and its line (`line 3: SyntaxError: ...`; section 13 says why only the line) rather than nothing, its parser
     held to the script host's 256 KiB stack rather than QuickJS's 1 MiB, and run in both of the pack's runtimes by
     today's load path, with its checks: the same ids and descriptors in both, and one definition that cannot be read
     refusing the whole pack. Bytecode is locked to one qjs build, so a download or a reload cannot ship it; the
     built-in pack keeps its embedded bytecode.
- **Shared across packs:** the call deadline's state (one call runs at a time on the block thread); the allocation
  counters, summed over every pack's audio runtime, so `audioAllocations`, the profile's allocation window and the
  zero-allocation benches keep their meaning, "the audio side"; the release rings (a record knows its pack); the live
  instance count, the throw count and first throw, and `jobsCutShort`; and the profile's span labels, one per type id,
  given as the first pack defining it loads and kept by its reloads, so the built-in types' labels stay 16 up in
  bundle order ([spec/08-profiling.md](../spec/08-profiling.md)) and a dev loop's reloads do not use up the name
  table. A label is given before the registry is asked, so a pack it refuses, or a refused reload's new type, keeps a
  label and a trace name for its ids; bounded, the same ids loaded again reusing theirs. A loaded pack's audio runtime
  joins the deadline's handler and the allocation counters only once its load is done: the load runs on the control
  thread while the block thread may be inside a call into another pack, so a handler polled in the load would touch
  that call's deadline state, and the load's allocations would be a second writer of counters that have one (section
  13). So what a loaded pack's load allocates, and what freeing a generation frees (section 5), are counted apart,
  never in `audioAllocations`; the built-in pack's load, at construction, comes before any block and is counted. A
  pack loaded after the profile's allocation window pinned the auto-GC off joins with its own pinned too.
- **Per pack:** its memory limit (section 6), its promise-job queue, its cycle GC, and its quiescent collection
  (`collect` runs per pack that has garbage).
- **The control thread's calls into a pack** are held to the call deadline's length of its own CPU time, on a clock of
  its own (section 2), since the block thread may be inside a call into another pack meanwhile: a load is one call
  (both runtimes' chunks, the jobs they queue, its definitions read), and so is a settings check (`parseSettings` and
  `layout` with their jobs, for `layout_for`, `prepare_settings` and `create`). A load it stops refuses the pack; a
  settings check it stops refuses the settings, saying so, so the planner refuses the document and the running plan
  stands. A settings check runs at most 256 of the promise jobs pending in the pack's control runtime, and one that
  leaves any refuses the settings the same way: QuickJS cannot drop them, so they wait there for the pack's next
  settings check, which runs them with its own, and a chain that never settles refuses every settings check of the
  pack, and so every document placing one of its nodes, until a reload frees that runtime. A pack's control runtime
  keeps that handler for its life, its audio runtime until it joins. A load runs at most 256 of the promise jobs each
  stretch of it queues (its top level in each runtime, each descriptor, what reading the definitions after the last
  one queues, what a loaded pack's collection of its load's cycles queues), and one that leaves any is refused (section
  6).

The built-in pack's memory is today's; each loaded pack adds two runtimes, about 180 KB and twice what its code loads
to (section 3). Each call into a node gains its checkpoint (a job-pending check when nothing is queued), and each
allocation and free in an audio runtime a `malloc_usable_size` (section 11's third question): B3a's `[bench]` moved by
about 4 ns an empty call and 12 one that passes a message on (section 9).

## 5. Loading, reloading and freeing

All of it runs on the control thread. The block thread enters a pack only through instances of it in a plan it has
adopted, so a pack is the control thread's until a plan placing one of its nodes is published, and the plan swap's
ring (release on push, acquire on adopt) carries the whole built pack with it.

- **Load** (a new id): the id is checked, the source compiled, the pack's two runtimes made and its code run in both,
  its definitions read, what the load left in cycles collected, and its audio runtime joined to what packs share
  (section 4); then its package is added to the Engine's node registry. A document can place its nodes from then on.
  A package the registry refuses (a descriptor the planner could not use, a type id another pack holds) frees the
  pack there and then: nothing placed it. Its audio runtime leaves what packs share first, as every freed pack's does
  (the destructor's included), so nothing freeing it frees is in the shared counters.
- **Reload** (an id already loaded: one the script host holds a current generation of, asked of it rather than of the
  registry, where a package of another kind, a downloaded native one, could one day share the id), all or nothing, as
  `setGraph` is:
  1. The new generation is built as a load builds, beside the old one. Its number is the id's count of loads, which
     every load of it advances, refused or not, so one number never names two sources, whenever a refused one is
     freed.
  2. The registry swaps the package's entries for the new generation's (`NodeRegistry::replacePackage`, keyed by each
     generation's `rp_package`, which the generation owns for its life). The old entries stay valid: planned instances
     point at them. The registry retires them, each keeping its address, until told to drop them (`dropRetired`),
     rather than never removing an entry, and can put the same ones back (`restorePackage`). The swap is checked as an
     addition is, and refused, changing nothing, for a new version naming another package or a type id another
     package holds.
  3. The Engine replans its current document: the last request `setGraph` published, with its tap carried as it is
     (the request re-arms no tap, so its swap carries none: section 11's second question). The planner already
     rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/native/src/graph/Planner.cpp)
     `reconcile`), so every node of the pack gets a fresh instance of the new code under the same node id, its JS state
     not carried over, while every other node keeps its instance. A document that places none of the old
     generation's types would plan as it runs, so it is not replanned. The replan leaves a core parked since that
     request parked (TS may have built it for the next document), and the request stays the one `setGraph` published.
  4. A replan that is refused (the new generation rejects a node's settings, or no longer has a type the document
     places) swaps the registry back to the old entries, frees the new generation, and returns the planner's errors.
     Nothing has changed: the old plan's instances still match the old entries, so none is rebuilt. The new
     generation is freed there and then while no audio thread runs; under one, the refused plan's fresh instances have
     releases queued to the block thread, so the generation is marked discarded and freed once their records come
     back. A replan refused as `busy` (the command ring full, or a settings commit not yet adopted) says nothing
     against the pack: the caller may try again.

  With no document placing the pack's nodes (the empty one the Engine publishes at construction among them), a
  reload is steps 1 and 2. A reload may add or remove types; removing one the document still places is refused by
  step 4.
- **Freeing a retired generation.** A replaced generation is retired, and its instances go the way every released
  instance goes today: each shim is destroyed when the plan that held it is reclaimed, its JS state freed on the block
  thread at the top of a later block, and its record handed back to the control thread to be deleted. A pack counts its
  records (the control thread makes and deletes them: a plain count). Once a retired or discarded pack's count is 0
  the block thread holds nothing of it, and the call that finds it so (`retirePack` itself, `discardPack`, `reclaim`,
  or `setAudioThreadOwns(false)`) frees it on the control thread: the pack-freed callback is told a retired
  generation's package first (the Engine's has the registry drop that package's retired entries), then its audio
  runtime leaves what packs share (the join's reverse, so its frees are counted apart), then its runtimes go. A whole
  generation's heap is freed off the audio thread, where today's releases free JS state on it. While no audio thread
  runs, releases happen there and then, so a reload frees the old generation within the call. A record the release
  ring back could not take, leaked rather than freed on the audio thread, keeps its pack for the host's life. A
  profile build takes what a freed generation added to the live heap off the shared count once no audio thread runs.
- **Not in C1:** unloading a pack (a dev loop reloads, and a pack nothing places costs only its runtimes), and loading a
  pack into more than one Engine: a render job's Engine (or a second one) has the built-in pack only. Loading a project's
  packs into every Engine that runs it belongs with whatever first places user nodes in projects; until then none does.

## 6. A node pays for its own jobs and memory

**Promise jobs** run after the call that queued them, not at the block's end. After every call the block thread makes
into a node (`create`, `onSettings`, `portsChanged`, `activate`, `process`), the host runs its pack's pending jobs at
a checkpoint, at most 256 (`kJobsPerCheckpoint`, which was `kJobsPerBlock`), under the same deadline as the call: one
deadline spans the call, the `create` a fresh instance's first call makes before it, and the checkpoint after them (a
`create` whose call has no hook to run gets a checkpoint of its own). Unless a chain was cut short (below), a pack's
queue is empty between two calls into its nodes, so every job that runs is charged to the node whose call queued it
(or whose jobs did). `endBlock` no longer runs jobs, and in `process` the checkpoint runs after the node's audio
outputs are copied out. No built-in node uses a promise, so parity cannot move.

| | Today | C1 |
|---|---|---|
| A job that fails (a `queueMicrotask` callback that threw, or the platform's allocator ran out of memory) | a throw charged to no node, never faulting one | the node's throw: counted, and a `faultOnThrow` node faults (an allocation the pack's limit refused faults the node whatever its `faultOnThrow`: Memory, below) |
| The deadline stops a job | stops the whole block's job run, charged to no node | counts as the node's call being stopped: it faults on the second in a row, or at once under `faultOnThrow` |
| A chain that does not settle in 256 jobs | the rest wait for the next block's end (`jobsCutShort`) | the node's throw (and `jobsCutShort`): a `faultOnThrow` node faults at once, any other on the second in a row, as two expiries do |
| A promise reaction's own throw | only rejects its promise | unchanged |

QuickJS cannot drop pending jobs, so a chain that is cut short stays queued. Its pack remembers whose jobs they are
until its queue empties: they run at the pack's later checkpoints (the next calls into any of its nodes) still charged
to the node that left them, so a pack-mate is never charged for them. While they are pending, every job those
checkpoints run is charged to that node, a pack-mate's own among them. So a pack-mate that starts a chain of its own
that never settles then has its cut shorts charged to the node that left the leftovers, which faults for them while
the pack-mate runs on; and once that node is released the chain is no node's, run 256 jobs at every checkpoint of the
pack until the pack is reloaded, the pack-mate that started it never charged or bypassed. A faulted node's chain can
still run up to 256 jobs a checkpoint, since rebuilding or removing the node frees nothing in the runtime its closures
live in; a reload of its pack, which frees that runtime, ends it. The cost stays bounded and stays in its pack. Each
of those checkpoints that cuts the chain short again still counts as the faulted node's throw. Leftovers that a
pack-mate's checkpoint ran and the deadline stopped count against the node that left them as its own call stopped
would: it faults at its next block on the second in a row, or the first under `faultOnThrow`; a run not stopped leaves
its count alone. Once the node that left them is released, they are no node's until the queue empties: a later
checkpoint runs them charged to no node, never to the pack-mate whose call it follows, and one that cuts them short
counts in `jobsCutShort` only.

Two more queue a job outside any one node's call, and are charged as follows: a release (`beginBlock` freeing a
destroyed node's JS) runs its pack's jobs there, charged to the node that left them when that is another node, else to
no node, since the node is gone; and a `FinalizationRegistry` callback is queued by whichever GC or free found its
target: one inside a call (QuickJS's threshold GC, or a commit freeing a reshaped node's old layout) runs as that
call's job, a reshape with no `onSettings` having a checkpoint of its own for it, and a quiescent collection (`collect`,
the profile's `runGc`) runs what it queued there and then, as a release does, when the queue was empty before the
collection (behind another node's leftovers those jobs wait with them, charged to that node). No built-in node uses either. A load's own
jobs run as it loads, at most 256 each stretch of it (its top level in each runtime, each descriptor, what reading the
definitions after the last one queues, what a loaded pack's collection queues), and a load that leaves any is refused,
as every other load failure is: QuickJS cannot drop them, and they would run at the pack's first checkpoint, charged to
whichever of its nodes' calls came first, or, in the control runtime, at its first settings check.

**Memory.** Each of a pack's two runtimes is held to its memory limit (64 MiB unless the load says otherwise), each on
its own: an allocation that would pass it is refused, and QuickJS throws out of memory in the call that made it. The
host's own allocator enforces it (section 11's third question): each runtime allocates through an opaque of its pack's
holding its live bytes and the limit, which refuses an allocation past the limit and notes the refusal on the pack. In
the audio runtime, a refusal during a call (or its jobs) faults the node they are charged to, whether or not it caught
the error and whatever its `faultOnThrow`: a pack at its limit is one none of its nodes can rely on. It faults at once
in `process`, and at its next block after a hook or for its leftovers run at a pack-mate's checkpoint or where no call
ran (a release's run). A refusal JS
caught is counted as the node's throw, saying the node "reached its pack's N MiB memory limit"; an uncaught one is the
out-of-memory throw already counted. Only an out-of-memory error the limit did not cause, the platform's allocator
running out, is a throw like any other, faulting a `faultOnThrow` node. A call is charged only what its pack refused
since it began: the note is cleared as each call arms its deadline, so a refusal made where no checkpoint looks (a
release freeing a node's JS, a quiescent collection, the text the host makes of a checkpoint's last job's throw), or
while the pack loads, faults no node. The limit holds each runtime whole, its intrinsics and its loaded code included,
so a limit too small for those refuses the load, which says so. A node's heap is freed when it is rebuilt or removed,
or its pack reloaded. Other packs, the built-in one among them, are untouched. In the control runtime, which holds no
instance, the limit holds the pack's top level, `parseSettings` and `layout`: a load or a settings check that fails on
an allocation it refused is refused, saying so ("reached its pack's N MiB memory limit"), a settings check's refusing
the document while the running plan stands, and one that catches the error runs on, there being no node to fault.
"Past its limit" means past it in reachable and uncollected bytes together: the limit counts everything a runtime
holds, garbage included. An allocation past it is refused at once, with no collection first, and QuickJS's own cycle
GC runs only as an object is made, once the heap has grown half again since the last one. So a pack whose reachable
heap is above about two thirds of its limit can be refused, and a node faulted, for garbage not yet collected, and a
profile window whose auto-GC is pinned off (`resetAllocStats(true)`, [spec/08-profiling.md](../spec/08-profiling.md))
gathers garbage until the limit faults a node. Left as it is (section 12).

Faults latch per instance as now, and a reload clears them, every instance of the pack being fresh. The first throw's
logged text already names the node's type, so a dev loop sees which of its nodes broke.

## 7. The surface

- **The Engine** (control thread): `loadScriptPack(id, source, memoryLimit)` returns `ok`; `error`, why not, as text
  (for a reload's refused replan, "the running document does not plan with the reloaded pack: " and the planner's
  first error, `<code> at <node>: <message>`, or `<code>: <message>` for one at no node such as `busy`, with how many
  more); `errors`, the planner's structured errors when it was a reload's replan that refused, empty for a refusal
  before any replan (the id, the compile, the load, the registry); and on success `types`: each type's id and its
  descriptor as the registry holds it (canonical JSON, every field spelled), in the order the source registered them,
  so whatever builds documents for the pack knows its ports. `memoryLimit` 0 is the default.
- **RPC:** `EngineRpcService::loadScriptPack(RpcLoadScriptPackRequest) -> RpcLoadScriptPackResult`
  ([BackendTypes.hpp](../packages/native/src/host/rpc/BackendTypes.hpp)) on the `emulator` facet beside `setGraph`,
  so every host has it (the plugin, SDL, the CLI, the Node addon, the test host): a user developing nodes in a DAW
  needs a reload there. The limit crosses as a double, `memoryLimitBytes`, refused unless it is a whole number of
  bytes from 0 to 2^53 - 1: an integer field would read whatever number came, 1.5 as 1 and -1 as a limit past any
  heap.
- **TS:** `Backend.loadScriptPack({ id, source, memoryLimitBytes? })` returns `LoadScriptPackResult`, `{ ok, error,
  errors, types }`, field for field the RPC's (`error` empty when `ok`). The mock backend refuses (`"the mock backend
  runs no script packs"`): it has no JS engine to run one in.
- Nothing in the app calls it yet; the authoring project will.

## 8. A pack's UI half

A pack may also carry UI: views (React components drawn by LVGL), menu entries, and the extension logic that decides
where its nodes go in a graph (`matches` and `contribute`, [modular-graph-design.md](modular-graph-design.md) sections
2.6, 2.7 and 7.1). None of it can run in the script host, which has no LVGL, no React and no txiki, and whose audio
side must never wait on the UI. So a pack has two halves, each loaded where it can run:

| Half | Holds | Loaded into | Isolated |
|---|---|---|---|
| Nodes | its `defineNode` types | the script host: a runtime pair of its own (this phase) | per pack |
| UI and extension | views, menu entries, `matches` / `contribute` | the control plane's txiki runtime, beside the app's own UI | no: it shares the app's realm and its one React tree |

They talk the way the app's own UI and nodes do: the UI sends settings and params into the document and reads
snapshots back, and no JS value passes between them. The pack's id names both halves, so a UI half finds its nodes by
their namespaced type ids. Loading and hot-reloading a UI half (re-importing a module in txiki and remounting its
views), and containing one that throws (an error boundary around each pack's views, since its isolation, too, is for
bugs), belong with the authoring and package-split work.

## 9. Tests

- **`retroplug-engine-graph-test`**, a new `test/engine-graph/ScriptPacks.test.cpp` over the Engine, its toy packs inline
  JS source:
  - a second pack's nodes run beside the built-in ones', placed in one document;
  - isolation: one pack hangs a getter on `Object.prototype`, replaces `Array.prototype.push` and a global, and a probe
    node in another pack, reading through each, sees nothing changed;
  - the refusals: a non-plain id, `rp.script`, one of the app's ids (`rp`, `rp.mine`, `rp.core`, `rp.midi`), a type
    outside the pack's namespace, a compile error (its text and line), a definition that cannot be read, a type
    another pack's namespace holds;
  - reload: the pack's nodes run the new code, a node of another pack (and a built-in one) keeps its instance and its
    JS state across it, a type added by the reload places, and a refused reload (a type the document places removed,
    settings the new code rejects) returns the planner's errors with the old code still running; generation numbers
    only increase, refused loads counted, however soon a refused generation is freed; which generation of an id the
    script host holds as current;
  - freeing: a retired generation's runtimes are freed once its records come back, quiescent and under an audio thread
    driving blocks, and a reload loop frees every generation it replaces; a pack the registry refuses is discarded with
    nothing in an audio-side count; a reload refused `busy` under an audio thread, after its replan made a fresh
    instance, leaves its generation listed, discarded, until the block thread releases that instance and a reclaim
    finds its record back;
  - jobs: a node's failing `queueMicrotask` callback is its throw and faults it under `faultOnThrow`; a job the
    deadline stops counts as its call stopped; a never-settling chain faults its node after two cut-short checkpoints
    while a pack-mate is never charged; a built-in node's counts stay 0 throughout; a load whose promise jobs do not
    settle in 256 (its top level's in either runtime, a descriptor's, a flag's getter's read after the last
    descriptor, its collection's, the built-in pack's load too) refused; a settings check whose promise jobs do not
    settle in 256 refusing its document while the running plan runs on, and a chain that never settles refusing every
    later check of its pack until the pack is reloaded;
  - memory: a node that allocates past its pack's limit faults, and nodes of other packs keep running; a top level, or
    a `parseSettings`, allocating past it in the control runtime refuses the load, or the document placing its node
    while the running plan runs on, saying so;
  - the control thread's deadline: a load that never returns refused, a settings check that never returns refusing its
    document while the running plan runs on, its clock its own while an audio thread makes calls into another pack;
  - a load under an audio thread driving blocks, and a reload under one, its old generation freed by a later reclaim;
  - a reload's replan carrying the running plan's tap as it is, and leaving a core parked since the last request
    parked; a type's span label kept across fifty reloads; and, in a profile build, a freed generation's share of the
    live heap taken off within the call while quiescent, and under an audio thread once it hands the runtimes back.
- **The ThreadSanitizer and AddressSanitizer passes** (`tools/run-sanitizer.sh`) cover reloading under a running audio
  thread.
- **`test:native`**, a `script-packs` case through the real host
  ([script-packs.test.ts](../packages/retroplug/test-native/script-packs.test.ts)): `loadScriptPack`, a document
  placing a pack node beside the built-in ones, a reload whose node's output changes, and the error shapes (a compile
  error at its line, a type outside the namespace, `rp.script`, an id of the app's, a memory limit that is no whole
  number or too small for the load, a refused reload with the planner's errors). No fixtures, so CI runs it.
- **Unchanged and re-run:** strict parity (all 197 cases, the fixtures present, `RP_FAIL_ON_SKIP=1`), with no
  re-record; the four suites; `test:plugin`; the zero-allocation benches.
- **Measured**: a pack's memory and load time in the Engine (section 3), and the script node call cost before and after
  (B3a's `[bench]`, `build/bin/retroplug-engine-graph-test "[bench]"`, two runs each, before and after the second
  commit): an empty pass-through 0.083 to 0.092 us a call before, 0.088 to 0.096 after; one passing a message on 0.178
  to 0.188 before, 0.193 to 0.202 after; the protocol nodes' plan at 1 frame 0.84 to 0.855 before, 0.872 to 0.898
  after. The profile's `allocsPerBlock` and `maxBlockAllocs` stay 0 in all three of its windows.

## 10. Docs

[spec/01-architecture.md](../spec/01-architecture.md) section 6 (the runtimes table: a pair per pack),
[spec/02-native-host.md](../spec/02-native-host.md) (the script host: packs, load and reload, jobs and memory charged to
the node), [spec/04-roles-protocol-nodes.md](../spec/04-roles-protocol-nodes.md) where it says what the script runtime
is, [spec/07-remaining-work.md](../spec/07-remaining-work.md) section 5 (the item closed, and what section 5 and section
11 here leave), [defineNode.ts](../packages/retroplug/src/graph/defineNode.ts)'s comment on when promise jobs run, and
AGENTS.md's description of `retroplug-engine-graph-test`. Writing them found three more: the `test:plugin` row of
[spec/06-build-test.md](../spec/06-build-test.md), [spec/08-profiling.md](../spec/08-profiling.md) (the audio runtimes
summed, a type's label across reloads, where the jobs run in the trace), and
[modular-graph-design.md](modular-graph-design.md) sections 4.9 and 8.

## 11. Landing

Five commits, each green on its own (the engine-graph tests, `test:plugin`, strict parity):

1. **Packs inside the script host**, the built-in bundle the only one: the per-runtime state moves into a pack. No
   behaviour change.
2. **Jobs charged to their node, and the memory limit.**
3. **Loading a pack**: `Engine::loadScriptPack` for a new id, the namespacing, the compiler's error text.
4. **Reload and freeing**: the registry's swap and drop, the replan and its rollback, retired generations freed.
5. **The RPC and TS surface, the `test:native` case, and the docs.**

As for B3b and LSDj's serial-out node, a prototype is built first in these five commits on a local branch, and the plan
is written from it; the prototype is where the questions below are answered (each answer follows its question).

- Whether QuickJS-ng's per-runtime class ids, atoms and interrupt handler need anything beyond moving today's setup
  into a pack (`JS_NewClassID` takes the runtime). **Nothing more.** `JS_NewClassID(rt, &id)` takes the next id from the
  runtime's own counter when `id` is 0, so each pack's `Block` class starts at 0 in its own runtime; atoms and the
  interrupt handler are each runtime's already. (A test binder that keeps one class id for every pack's probe works
  only because every pack allocates its classes in the same order, `Block` first.)
- What the replan's request carries for the SyncTap, so a reload neither drops nor re-arms a tap the running plan has.
  **No tap at all.** The replan is the request `setGraph` last published with its re-arm cleared, so its swap carries
  no trailer and the tap code never runs: the armed tap stays as it is. A case arms a bliptoaster NES to mode 1, moves
  its mode to 2 behind the engine's back, and finds it still 2 after a reload (neither restored nor armed again),
  then 0 after a later restore request, so the armed record survived.
- Where the memory limit is enforced: by the host's own allocator, which sees every allocation the pack's runtime
  makes and so knows when it refused one (at the cost of keeping each pack's live bytes in every build, where only a
  profile build keeps the live heap today), or by QuickJS's `JS_SetMemoryLimit`, which checks before the allocator is
  called, so the refusal has to be read from QuickJS after the call. **The host's allocator.** `JS_SetMemoryLimit`
  refuses inside `js_malloc_rt` before the allocator is called, and QuickJS keeps no record of a refusal JS caught, so
  there is nothing to read after the call. The cost is one `malloc_usable_size` an allocation and one a free in every
  build (QuickJS already calls it once each), and the call cost section 9 records.
- That the 64 MiB default holds every test bundle's toys. **It does**, with room: the largest any engine-graph test's
  pack reached was 2.55 MB, across 546 packs, and the production bundle's in a whole `test:native` run 0.43 MB (a
  temporary probe of each pack's peak). Only the toy built to pass a 16 MiB limit came near one.

## 12. Left as it is

- **Isolation inside a pack.** Nodes of one pack share its realm, one author having written all of them. Freezing a
  pack's intrinsics after its load (an SES-style lockdown) would contain a bug within a pack too, at the price of code
  that writes over an inherited property, which a frozen prototype refuses.
- **A node's state across a reload.** C1 restarts a reloaded pack's nodes clean; a `snapshot` / `restore` pair would
  carry it.
- **The authoring loop, the pack format and downloads, unloading, more than one Engine, the UI half** (sections 5 and
  8).
- **The script nodes' own call cost** (about 0.08 us a node a block empty, B3a) and MicroQuickJS readiness
  (`defineNode` keeps to ES2015's library), both unchanged.
- **The memory limit counts garbage** (section 6): nothing collects before a refusal, so a pack whose reachable heap is
  above about two thirds of its limit can be faulted for garbage. The default leaves room (64 MiB, where the largest
  test pack peaked at 2.55 MB, section 11).

## 13. Found while building

- **A pack joined what packs share too early.** The prototype first gave a pack's audio runtime the block thread's
  interrupt handler and the shared allocation counters as its runtimes were made, before its load. ThreadSanitizer
  found a data race in `Glue::interrupt` called from `loadPack` (three runs of three) once a toy's top level ran long
  enough to reach QuickJS's interrupt poll (300000 iterations), the block thread inside a call into another pack, so
  the join moved to the load's end (section 4). What the load allocates goes to a counter of its own, so a profile
  build's absolute live heap leaves out what a live-loaded pack's load left: a constant offset, every delta exact.
- **A compile error has a line and nothing finer.** QuickJS puts no position on the error object, and the first frame
  of its stack (`at <script>:L:C`) gives a column that is often 1 whatever the column was, so `compileToBytecode`
  says `line 3: SyntaxError: ...` (section 4). Its compiled code's name is `<script>`.
- **Where namespaces meet, the registry is the backstop.** `a` and `a.b` can both name a type `a.b.c`, a pack named
  `rp.midi` could define the built-in `rp.midi.route`, and one named `rp.core` takes a native package's name. The
  registry refuses each type already registered, and the pack goes with it; the `rp.` reservation (section 2) then
  put the app's namespace out of reach before anything is built.
- **The control thread had no deadline**, so a pack whose top level, `parseSettings`, `layout` or descriptor never
  returns hung whichever thread loaded it or planned it (section 2). A call the control deadline stops leaves the
  pack's control runtime usable. A settings check then ran at most 256 of its pack's control-side jobs (fewer, if its
  deadline stopped one) and accepted the settings whatever it left, the rest waiting in that runtime for the pack's
  next check, so a `parseSettings` or `layout` that started a chain that never settles taxed every later check of its
  pack 256 of its jobs. A settings check whose jobs do not settle in the 256 it runs is refused now (section 4), as a
  load is.
- **A refused replan's generation cannot always be freed at once.** Under an audio thread the refused plan's fresh
  instances have their releases queued to the block thread, so the new generation is marked discarded and freed by the
  call that finds its last record back, with no pack-freed callback: the registry dropped its entries when it put the
  old ones back (section 5).
- **A replan must leave the parked cores alone.** A reload between a `buildCore` and the `setGraph` that places its
  core would otherwise free the core TS had just parked, as a published document frees the parked cores it does not
  place; so a replan leaves them parked and the request it replans stays the last one `setGraph` published.
- **A record the release ring back cannot take keeps its pack for the host's life.** It is leaked rather than freed on
  the audio thread, as before C1, and its pack's count never reaches 0. Left as it is: the ring back is twice the
  release ring's depth, and nothing has filled it.
- **The records' count is what makes freeing safe.** Freeing a retired pack with records out (a mutation) is an
  AddressSanitizer heap-use-after-free in `~Instance`, reading its dropped registry entry. Skipping the counters'
  detachment when a generation is freed leaves ThreadSanitizer silent, since relaxed atomics lose updates rather than
  race. And a profile build has to take a freed generation's share off the live heap: fifty reloads leave it within
  16 bytes of where it was, where without that it rose by 1.67 MB.
- **A memory limit too small for the runtime itself refuses every load, and QuickJS said `null`.** The limit holds the
  audio runtime's intrinsics (about 90 KB) and its loaded code, so 64 KiB refuses a two-node toy's load (256 KiB loads
  it), and once QuickJS cannot make its out-of-memory error it throws `null`: the load said only `chunk 0: null`. The
  fifth commit has a load the limit refused say so, `(the load reached the pack's 65536-byte memory limit)` (in the
  words a node's refusal uses, "reached its pack's N MiB memory limit"), and `test:native` holds it.
- **A pack compiled from source keeps its debug information.** The built-in bundle is compiled stripped (`tjsc -s`);
  `compileToBytecode` keeps the lines a thrown error's stack names, which a dev loop wants, so the production bundle's
  source loaded as a pack holds about 366 KB in its audio runtime, where the stripped bundle loaded to 296 (section
  3). Whether a downloaded pack's code is compiled stripped is left to the pack-format work.
- **Running out of memory faults sooner than a throw did.** An allocation refused at the limit in `process`, or while
  its inputs are made JS values, faults the node at once, where it had been a throw the node ran on from; a refusal
  while a release frees a node's JS, or while a pack loads, faults no node (section 6).
- **A plain id can hold `"` or `\`,** so a pack's manifest writes its id as a JSON string; an id `test"q` broke the
  manifest built by concatenation.
- **A generation's number could name two sources.** It was one more than the newest of its id the host still held,
  so a refused reload freed at once left its number to the next load, and one waiting for its records did not: the
  number a reload got hung on when a block ran. Each id counts its loads now (section 5).
- **What a load left queued was charged to its pack's first node.** A load ran at most 256 of the jobs each stretch
  queued and let the rest wait, so the first of the pack's nodes a checkpoint followed was charged for them, and
  faulted after two cut shorts, for a top level's chain; a loaded pack's collection of its load's cycles, after the
  load, queued its `FinalizationRegistry` callbacks the same way. A load that leaves any is refused (section 6),
  and the collection runs inside the load. What the load's reads of a definition after the last descriptor's drain
  queued in the control runtime (a getter on a flag) was then still left for the pack's first settings check, until a
  drain of that runtime after the definitions took it in.
- **What ran between two calls into a pack was charged to a node.** A quiescent collection's `FinalizationRegistry`
  callbacks, and a reshape's with no `onSettings` to run them after, waited for the pack's next checkpoint and were
  charged to whichever node's call it followed; and an allocation refused where no checkpoint looks faulted a node: one
  in a quiescent collection the next node called, one as a release freed a node's JS the node whose leftovers that
  release ran. A collection runs what it queued, a reshape has a checkpoint of its own, and each call clears its pack's
  refusal note as it arms its deadline (section 6).
- **A pack's control runtime had no memory limit.** Its top level, `parseSettings` and `layout` run there, held only by
  the control thread's deadline, so a top level allocating without bound could take the host's memory before 2 s of
  CPU stopped it, the design having reasoned that the control side holds no instance. It is held to the pack's limit
  (section 6), through the same allocator as the audio runtime rather than QuickJS's `JS_SetMemoryLimit`, which
  refuses before the allocator is called and keeps no record of it, so the load or the settings check it fails could
  not say the limit was why.
