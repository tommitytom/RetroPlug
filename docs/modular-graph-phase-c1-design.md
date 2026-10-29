# Phase C1: script node packs, each in its own runtimes

**Date:** 2026-10-29 · **Branch:** `feature/modularize` · **Status:** designed

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
heap's GC grows with every node in it. A runtime per pack costs about 300 KB a pack, made off the block thread, and
each pack's GC covers its own heap.

## 4. Packs in the script host

`ScriptHost` stops being one pair of runtimes and becomes a holder of packs.

- **A pack** is an id, a generation (its load's count: 1, then 1 more each reload), a control runtime and context, an
  audio runtime and context, its types, and its own `rp_package` (manifest `{"id": "<pack id>"}`). Everything the
  audio side holds per runtime today is the pack's: the `Block` class and its frozen prototype, the atoms the block
  thread names properties by, the address of `Object.prototype`, the interrupt handler (with the host as its opaque).
  A `Type` names its pack, and an `Instance` reaches its runtime through its type.
- **The built-in pack** is today's bundle, as pack `rp.script`: the same type ids, the same code, loaded at the Engine's
  construction as now (`ScriptHost::load` keeps its signature and loads this pack). The binder a test host passes binds
  into every pack's two contexts.
- **A loaded pack** (`loadScriptPack`, section 7) is checked before anything is built:
  1. Its id is a plain id (the registry's `isPlainId`) and not `rp.script`, which is not reloadable.
  2. Each type id starts with the pack id and a dot (`user.mysynth.osc` in `user.mysynth`), so a pack can never
     shadow a built-in type or another pack's, and a reload can never collide with anything.
  3. Its source is global code, the IIFE a bundler makes, appending `defineNode` results to `__rp_scriptNodes`
     ([register.ts](../packages/retroplug/src/script/register.ts)), as the built-in bundle does. It is compiled to
     bytecode on the control thread by `dsp::compileToBytecode`
     ([ScriptCompiler.hpp](../packages/native/src/host/dsp/ScriptCompiler.hpp)), which now returns the compile
     error's text and position rather than nothing, and run in both of the pack's runtimes by today's load path, with
     its checks: the same ids and descriptors in both, and one definition that cannot be read refusing the whole pack.
     Bytecode is locked to one qjs build, so a download or a reload cannot ship it; the built-in pack keeps its
     embedded bytecode.
- **Shared across packs:** the call deadline's state (one call runs at a time on the block thread); the allocation
  counters, summed over every pack's audio runtime, so `audioAllocations`, the profile's allocation window and the
  zero-allocation benches keep their meaning, "the audio side"; the release rings (a record knows its pack); the
  throw count and first throw; and the profile's span labels, one per type id, given at a type id's first
  registration and kept by its reloads, so the built-in types' labels stay 16 up in bundle order
  ([spec/08-profiling.md](../spec/08-profiling.md)) and a dev loop's reloads do not use up the name table.
- **Per pack:** its memory limit (section 6), its promise-job queue, its cycle GC, and its quiescent collection
  (`collect` runs per pack that has garbage).

The built-in pack's memory and per-call cost are today's; each loaded pack adds about 300 KB; each call into a node
gains one job-pending check (a list-empty test, section 6).

## 5. Loading, reloading and freeing

All of it runs on the control thread. The block thread enters a pack only through instances of it in a plan it has
adopted, so a pack is the control thread's until a plan placing one of its nodes is published, and the plan swap's
ring (release on push, acquire on adopt) carries the whole built pack with it.

- **Load** (a new id): the pack's two runtimes are made, its source compiled and run in both, and its definitions
  read; then its package is added to the Engine's node registry. A document can place its nodes from then on.
- **Reload** (an id already loaded), all or nothing, as `setGraph` is:
  1. The new generation is built as a load builds, beside the old one.
  2. The registry swaps the package's entries for the new generation's. The old entries stay valid: planned instances
     point at them (`NodeRegistry` keeps them until told to drop them, rather than never removing an entry).
  3. The Engine replans its current document (the last request it published, its tap carried as it is). The planner
     already rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/native/src/graph/Planner.cpp)
     `reconcile`), so every node of the pack gets a fresh instance of the new code under the same node id, its JS state
     not carried over, while every other node keeps its instance.
  4. A replan that is refused (the new generation rejects a node's settings, or no longer has a type the document
     places) swaps the registry back to the old entries, frees the new generation, and returns the planner's errors.
     Nothing has changed: the old plan's instances still match the old entries, so none is rebuilt.

  With no document published yet, a reload is steps 1 and 2. A reload may add or remove types; removing one the
  document still places is refused by step 4.
- **Freeing a retired generation.** A replaced generation is retired, and its instances go the way every released
  instance goes today: each shim is destroyed when the plan that held it is reclaimed, its JS state freed on the block
  thread at the top of a later block, and its record handed back to the control thread to be deleted. A pack counts its
  records (the control thread makes and deletes them: a plain count). Once a retired pack's count reaches 0 the block
  thread holds nothing of it, and the next `reclaim` frees its runtimes and has the registry drop its entries, on the
  control thread: a whole generation's heap freed off the audio thread, where today's releases free JS state on it.
  While no audio thread runs, releases happen there and then, so a reload frees the old generation within the call.
- **Not in C1:** unloading a pack (a dev loop reloads, and a pack nothing places costs only its runtimes), and loading a
  pack into more than one Engine: a render job's Engine (or a second one) has the built-in pack only. Loading a project's
  packs into every Engine that runs it belongs with whatever first places user nodes in projects; until then none does.

## 6. A node pays for its own jobs and memory

**Promise jobs** run after the call that queued them, not at the block's end. After every call the block thread makes
into a node (`create`, `onSettings`, `portsChanged`, `activate`, `process`), the host runs its pack's pending jobs, at
most 256 (today's `kJobsPerBlock`, now a bound a call), under the same deadline as the call: one deadline spans the call and its jobs. A pack's
queue is empty between two calls into its nodes, so every job that runs is charged to the node whose call queued it
(or whose jobs did). `endBlock` no longer runs jobs. No built-in node uses a promise, so parity cannot move.

| | Today | C1 |
|---|---|---|
| A job that fails (a `queueMicrotask` callback that threw, or ran out of memory) | a throw charged to no node, never faulting one | the node's throw: counted, and a `faultOnThrow` node faults |
| The deadline stops a job | stops the whole block's job run, charged to no node | counts as the node's call being stopped: it faults on the second in a row, or at once under `faultOnThrow` |
| A chain that does not settle in 256 jobs | the rest wait for the next block's end (`jobsCutShort`) | the node's throw (and `jobsCutShort`), and twice in a row faults it, as two expiries do |
| A promise reaction's own throw | only rejects its promise | unchanged |

QuickJS cannot drop pending jobs, so a chain that is cut short stays queued. Its pack remembers whose jobs they are
until its queue empties: they run at the pack's later checkpoints (the next calls into any of its nodes) still charged
to the node that left them, so a pack-mate is never charged for them (while they are pending, a pack-mate's own job
failures are charged to that node too, which errs towards the innocent). A faulted node's chain can still run up to
256 jobs a checkpoint, since rebuilding or removing the node frees nothing in the runtime its closures live in; a
reload of its pack, which frees that runtime, ends it. The cost stays bounded and stays in its pack.

Two more queue a job outside any one node's call, and are charged as follows: a release (`beginBlock` freeing a
destroyed node's JS) runs its pack's jobs there, charged to no node, since the node is gone; and a
`FinalizationRegistry` callback is queued by whichever call's GC found its target, so it runs as that call's job. No
built-in node uses either.

**Memory.** Each pack's audio runtime is held to its memory limit (64 MiB unless the load says otherwise): an
allocation that would pass it is refused, and QuickJS throws out of memory in the call that made it. The host learns
that its pack refused one (section 11 leaves to the prototype how), and a call (or its jobs) during which its pack
refused one faults the node at once, whether or not the node caught the error: a pack at its limit is one none of its
nodes can rely on. Its heap is
freed when it is rebuilt or removed, or its pack reloaded. Other packs, the built-in one among them, are untouched. The
control side's runtimes keep no limit: they hold no instance.

Faults latch per instance as now, and a reload clears them, every instance of the pack being fresh. The first throw's
logged text already names the node's type, so a dev loop sees which of its nodes broke.

## 7. The surface

- **The Engine** (control thread): `loadScriptPack(id, source, memoryLimit)` returns `ok`, `error` with the planner's
  structured errors when it was a reload's replan that refused, and on success `types`: each type's id and descriptor
  JSON, so whatever builds documents for the pack knows its ports. `memoryLimit` 0 is the default.
- **RPC:** `EngineRpcService::loadScriptPack` on the `emulator` facet beside `setGraph`, so every host has it (the
  plugin, SDL, the CLI, the Node addon, the test host): a user developing nodes in a DAW needs a reload there.
- **TS:** `Backend.loadScriptPack({ id, source, memoryLimitBytes? })` with that result type. The mock backend refuses
  (`"the mock backend runs no script packs"`): it has no JS engine to run one in.
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
  - the refusals: a non-plain id, `rp.script`, a type outside the pack's namespace, a compile error (its text and
    position), a definition that cannot be read;
  - reload: the pack's nodes run the new code, a node of another pack (and a built-in one) keeps its instance and its
    JS state across it, a type added by the reload places, and a refused reload (a type the document places removed,
    settings the new code rejects) returns the planner's errors with the old code still running;
  - freeing: a retired generation's runtimes are freed once its records come back, quiescent and under an audio thread
    driving blocks, and a reload loop frees every generation it replaces;
  - jobs: a node's failing `queueMicrotask` callback is its throw and faults it under `faultOnThrow`; a job the deadline
    stops counts as its call stopped; a never-settling chain faults its node after two cut-short checkpoints while a
    pack-mate is never charged; a built-in node's counts stay 0 throughout;
  - memory: a node that allocates past its pack's limit faults, and nodes of other packs keep running.
- **The ThreadSanitizer and AddressSanitizer passes** (`tools/run-sanitizer.sh`) cover reloading under a running audio
  thread.
- **`test:native`**, a `script-packs` case through the real host: `loadScriptPack`, a document placing a pack node beside
  the built-in ones, a reload whose node's output changes, and the error shapes.
- **Unchanged and re-run:** strict parity (all 197 cases, the fixtures present, `RP_FAIL_ON_SKIP=1`), with no
  re-record; the four suites; `test:plugin`; the zero-allocation benches.
- **Measured** and recorded here: a pack's memory and load time in the Engine, and the script node call cost before and
  after (B3a's `[bench]`, `build/bin/retroplug-engine-graph-test "[bench]"`), expected to differ by about one
  job-pending check a call.

## 10. Docs

[spec/01-architecture.md](../spec/01-architecture.md) section 6 (the runtimes table: a pair per pack),
[spec/02-native-host.md](../spec/02-native-host.md) (the script host: packs, load and reload, jobs and memory charged to
the node), [spec/04-roles-protocol-nodes.md](../spec/04-roles-protocol-nodes.md) where it says what the script runtime
is, [spec/07-remaining-work.md](../spec/07-remaining-work.md) section 5 (the item closed, and what section 5 and section
11 here leave), [defineNode.ts](../packages/retroplug/src/graph/defineNode.ts)'s comment on when promise jobs run, and
AGENTS.md's description of `retroplug-engine-graph-test`.

## 11. Landing

Five commits, each green on its own (the engine-graph tests, `test:plugin`, strict parity):

1. **Packs inside the script host**, the built-in bundle the only one: the per-runtime state moves into a pack. No
   behaviour change.
2. **Jobs charged to their node, and the memory limit.**
3. **Loading a pack**: `Engine::loadScriptPack` for a new id, the namespacing, the compiler's error text.
4. **Reload and freeing**: the registry's swap and drop, the replan and its rollback, retired generations freed.
5. **The RPC and TS surface, the `test:native` case, and the docs.**

As for B3b and LSDj's serial-out node, a prototype is built first in these five commits on a local branch, and the plan
is written from it; the prototype is where the questions below are answered.

- Whether QuickJS-ng's per-runtime class ids, atoms and interrupt handler need anything beyond moving today's setup
  into a pack (`JS_NewClassID` takes the runtime).
- What the replan's request carries for the SyncTap, so a reload neither drops nor re-arms a tap the running plan has.
- Where the memory limit is enforced: by the host's own allocator, which sees every allocation the pack's runtime
  makes and so knows when it refused one (at the cost of keeping each pack's live bytes in every build, where only a
  profile build keeps the live heap today), or by QuickJS's `JS_SetMemoryLimit`, which checks before the allocator is
  called, so the refusal has to be read from QuickJS after the call.
- That the 64 MiB default holds every test bundle's toys.

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
