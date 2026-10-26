# Phase B3a: real ports and the script-node runtime

**Date:** 2026-10-25 · **Branch:** `feature/modularize` · **Status:** built (5002c4b4 to 1ed9a283, then the branch
review's cleanup, §8); B3b not yet built, and the LSDj master-sync fix comes before it (§10).

Phase B of [modular-graph-design.md](modular-graph-design.md) moves the engine onto the graph in steps,
each held bit for bit to the B0 parity baseline ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)).
B1 ran native on the executor, B2 ([modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)) gave
the graph's structure to TS. B3, "the DSP kernel dissolves", is split in two: **B3a** (this document) makes the
kernel's input and output real wires and runs the kernel, unchanged, as one TS node in a new script-node
runtime; **B3b** splits that node into one protocol node per role and deletes the kernel. §10 records what
building B3a found.

## 0. Where B3a sits

Before B3a, `main` was merged into this branch (c2245f1b). Its one feature written against the old engine,
the physical-N8 cart mirror (`Engine::cartSystemId`), was ported onto the graph engine, and parity did not move:
the reliable link stays off for every parity ROM (each advertises caps `0xFF`), and no case reaches the empty
N8 FIFO read main changed. B3a starts from that merged tree, so its bar is the behaviour that ships.

Before B3a the kernel was a sealed box. `rp.dsp.kernel` was a native node with no ports, first in every plan
([B1 §3.3](modular-graph-phase-b1-design.md)). Its `process()` was `Engine::runKernelStage`: it handed the
staged host MIDI, the control-surface MIDI and last block's MI.OUT bytes to one `processBlock` call in a bare
QuickJS context (`DspRuntime`), then fanned four system-addressed sink kinds (serial in, core MIDI, core bytes,
buttons) into the cores by id, in a fixed order, and left its host MIDI out and control-surface out for the host to
drain. Its structure (each system's role pipeline, the project stages) arrived separately, through
`dspSetSystems`, and the bundle through `compileScript` + `dspLoadKernel` at boot.

After B3a that I/O is wiring the planner can see: host I/O nodes, input ports on every system node, a
`serialOut` port on the Game Boy, and a kernel node whose settings are its structure, run by a script host every
Engine has. The sealed kernel, its fan-out and its three RPCs are gone (§5.4).

## 1. Decisions taken

| Question | Decision |
|---|---|
| Merge `main` before B3 or after? | **Before** (done: c2245f1b). B3 rewrites the code where NES MIDI reaches the N8 FIFO, which is where main had moved on. |
| How is B3 cut? | **B3a then B3b**, each with its own spec, plan and bit-identical check. B3a: the runtime and the ports, with the kernel as one script node. B3b: one protocol node per role, `kernelProjection` folded into `buildGraph`, zod out of the bundle, the kernel deleted. |
| Frame order on the new wires | **Sort at the source.** `rp.host.midi-in` stable-sorts the staged host MIDI by frame, so every queue keeps the ABI's frame-ordered promise (§3.4). |
| Kernel state across a structure change | **Reshape.** A script node may keep its instance across a settings change that alters its layout; the planner rebinds its ports by id and commits the new settings at the swap (§2.4). |
| Does an edit wait for the last edit's commit? | **Only when that commit's node reads its current settings to prepare.** A type that declares `RP_TYPE_STATELESS_PREPARE`, as every script type does, never holds `setGraph` back; its commits queue on the ring in push order (§2.4, §10). |
| How does one set of functions serve several types? | **The type-level calls take their type.** `descriptor`, `layout_for`, `query_memory` and `create` are handed the `rp_node_type` they are called through, which gains a `ctx` (§2.2, §10). |
| How does the bundle reach the engine? | **Embedded at build time**, as bytecode, and loaded when an Engine is constructed. No boot call (§2.5). |
| Host MIDI out's system tag | **Kept**, with its emission order, by a private 4-byte prefix on the kernel's one `midiOut` port that only `rp.host.midi-out` reads (§4.4). Transitional: B3b's nodes each have their own port. |
| The N8 cart mirror | **A node**, `rp.n8.cart-mirror`, wired by `buildGraph` from the first NES's kernel outputs (§3.5). TS's choice of cart system is the only one. |
| A throw in the kernel's JS | **The old rule**: the block's output up to the throw stands, the next block runs again; counted and logged once (§4.5). |
| Host block sizes | **Any, from 1 frame.** New parity groups pinned 1-frame blocks and FL-style ragged splits before B3a changed anything, and a bench measured per-call cost at tiny blocks while the old kernel still ran (§6, §7.4). |
| LSDj master sync in tiny blocks | **Pinned as it is, and fixed right after B3a**, as its own change, before B3b ports the decoder (§7.1, §10). |

## 2. The script-node runtime

### 2.1 One audio-side runtime per engine

`DspRuntime` became the **script host** ([ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp)): one
bare QuickJS runtime per `Engine` holding every script node instance, entered only by the thread that runs blocks
(the audio thread, or the caller's thread while quiescent), as the kernel's context was. Each Engine has its own: the
plugin's instances, the standalone, the SDL host, the CLI, the UI's `RenderHost` jobs and the Node addon never share
one.

A script node is reached through an ordinary `rp_node`: a native **shim** per instance that the planner creates,
activates and destroys like any node. The shim's `create` and `activate` run on the control thread and only allocate
the shim and its record. The JS instance behind it lives in the script host:

- **created** there by the first call the shim gets on the block thread, from the settings the shim was created
  with. In practice that is the `ports_changed` the planner sends every fresh instance when its plan is adopted, so
  the instance exists before that plan's first `process`; a `commit_settings` or `process` arriving first would
  create it too; a `create` that throws faults the node at its first block. A live instance is re-activated (a new
  rate or block size) only while quiescent, since the GraphHost re-activates only then, and its JS `activate` hook
  runs at once.
- **released** there once the plan that last held it is reclaimed: the shim's `destroy` (control thread) hands the
  record to the script host, which releases the JS instance at once while quiescent, and otherwise queues it on a
  256-slot SPSC ring (with a control-side backlog behind it, threaded through the records so a destroy allocates
  nothing) that `beginBlock` drains at the top of later blocks, 16 releases a block. The released record comes back on
  a second ring of 512 slots and `reclaim` deletes it on the control thread, so the audio thread frees nothing native.
  What a released instance leaves in cycles is collected at points where no block runs: the reclaim that ends a
  quiescent operation which released some (the Engine's after every `setGraph`, one collection however many went),
  and the audio thread handing the runtime back. While an audio thread owns the runtime, those cycles wait for
  QuickJS's own threshold collection, inside whichever block's JS allocates past it. The first reclaim after the
  bundle's load collects what the load left in cycles too (about 144 KiB of the production bundle's).

So the audio runtime runs on one thread at a time, as it did (the control-side runtime runs on the planning thread,
§2.2). Creating an instance allocates in QuickJS on the block thread, as the old `SetSystems` command, applied on the
audio thread, did; so do parsing its settings there, the mask array `portsChanged` receives and the frees of a
release. They happen only at swaps and releases, which is accepted, though a heavy `create` is an xrun source.

### 2.2 One control-side runtime for validation

The ABI's `layout_for` and `prepare_settings` run on the control thread, and a TS node's ports may be a function
of its settings (the kernel's are per system). The planner must keep validating a whole document before it
creates anything ([B1 §6](modular-graph-phase-b1-design.md)). So a second bare QuickJS runtime, created with
the Engine and loaded with the same bundle, answers those two calls for every script type: the definition's
`parseSettings` checks a settings object and its `layout` gives the ports, from the settings alone. It never holds an
instance and never runs `process`; the two runtimes share no JS values, only the bundle.

A bundle registers its definitions by appending them to the global `__rp_scriptNodes`
([register.ts](../packages/retroplug/src/script/register.ts)). Once it has run in both runtimes the host reads both
lists, requires each definition's id and descriptor to match by index (so a bundle that branches on which runtime it
runs in is refused), and registers one `rp_node_type` per definition in one package, `rp.script`, each declaring
`RP_TYPE_STATELESS_PREPARE` (§2.4) and taking `reshapes` and `faultOnThrow` from its definition. One definition that
cannot be read refuses the whole package, and a second `load` is refused, leaving the first one's types registered.
Every type is served by the same functions, which the ABI could not tell apart: its type-level calls had no context
(§10). So `descriptor`, `layout_for`, `query_memory` and `create` now take the `const rp_node_type*`
they are called through, and `rp_node_type` gains a trailing `ctx`, null for a C++ node and never read by the host,
which for a script type points at its definition's record ([rp_node.h](../packages/native/src/graph/rp_node.h)).

### 2.3 Marshalling

A node's `process` gets one `Block` object, the same every block, and each input becomes a JS value the first time the
node reads it in a block:

- **an events or bytes input** (`messages(port)`) is a reused `Message[]` of `{ frame, data, flags }`. The payloads
  are copied into one JS-owned buffer per port (grown, never shrunk), since plan memory may be freed after a swap,
  and a message's `data` view is made again only when its place in that buffer moves;
- **a control input or a param** (`control(id)`) is a reused `{ start, changes }`;
- **an audio port** (`audio(port)`) is a reused `Float32Array[]` over the instance's own JS-owned buffer per channel,
  sized to the block maximum, made the first time the port runs and again only when a larger block is activated or
  the node gives one away (`buffer.transfer()`). An input's samples are copied into it before `process` and an
  output's copied out after it, one `memcpy` per channel per block, and the views are made again only when the block's
  length moves, so a steady block still allocates nothing.

Plan memory never reaches JS: a view a node keeps across a plan swap, a buffer it transfers away, an input it writes in
place or an array it edits reaches only its own copy, never a buffer another node reads or a plan the control thread
has freed. Handing JS views of the plan's buffers and detaching them before a buffer is freed cannot be made sound: a
kept array, `transfer()` and an edited array each outran the tracking (§10). What a block hands out is good for that
call only.

What a block refills is the node's to hold, so a write onto it can fail: a setter the node hung there throws (or runs
past the deadline), or it froze the object. That is the node's throw. `messages()` and `control()` throw it rather than
hand back a half-written value with the exception left pending, and a failed write onto its audio views (written again
when the block's length moves, a channel is added or a copy is made again, and all of them the block after a refused
one), the `Block` or its transport, made before `process` is called, is a throw in `process` whose block does not run
(§10). Every node shares one realm, so the host keeps its writes, and its reads of a node's methods, off the shared
prototypes. Each field it fills is the object's own data property from when it makes the object (a define, which never
walks the prototype chain), and each array slot is set when it is the array's own at that write and defined when it is
not (never made, or taken back by the node with a shift, a splice or a shorter length), so only the node's own setters
run there, never an accessor another node (or the bundle) hung on `Object.prototype` or `Array.prototype`. One gap is
left: a field the node deletes is set again, which walks the chain, as does the read of a message's `data` it deleted;
asking at every field write as well would cost each script call about 12 ns a block, 14% of an empty pass-through's on
§7.4's bench. `process` and the hooks are read from what `create` returned and the prototypes it inherits them through,
short of `Object.prototype`, so a getter there runs for no node and a function there is no node's hook. A Proxy on the
way answers as JS reads one (its `get` trap, or its target's chain), so a wrapper or a test double works; a chain longer
than 1024 plain objects (`ScriptHost::kMethodHops`), or one that cycles back through a Proxy, fails the `create` rather
than holding the block thread, since that read runs no JS for the deadline to stop (§10). The `Block` prototype every
node reaches is frozen with its methods (a profile build binds the trace globals read-only and frozen too), so no node
replaces one for another: assigning one on a node's own `Block` (`block.send = f`) throws in the strict bundle, where
`Object.defineProperty` on it still works. That is the host's part only: what nodes can do to each other through the
shared realm (an accessor on a prototype another's own code reads through, a global it calls) is left to one context per
node ([spec/07](../spec/07-remaining-work.md)).

**Outputs** go out through `send` and `emit`, which write the plan's queue or control stream for the port directly,
with no intermediate JS array; a `send` that does not fit returns false, and the queue counts the drop (§2.6). Ports
are found by id through atoms made when the instance, or the layout it reshapes to, is bound, in a linear walk (a
layout has a handful of ports), so a warm block allocates nothing native. Measured, a node written with indexed loops
allocates nothing in JS either, and one written with `for ... of` and an array literal per message makes 7
allocations (360 bytes) a block ([ScriptNodes.test.cpp](../packages/native/test/engine-graph/ScriptNodes.test.cpp)).

**Promise jobs** a block's JS queues run at the end of that block, at most 256 of them, so a reaction that queues
another every time cannot hold the audio thread; the rest wait for the next block's end, and a block that leaves some
is counted and said once. The control context drains its own after each call into it.

**The block's transport** (frames, sample rate, tempo, ppq at block start, playing) comes from `rp_process`. The
Engine builds it from the fields it built the old `DspRuntime::BlockInfo` from (all but `controllerConnected`, which
now arrives on its own port, §3.3), so the kernel sees the same numbers.

### 2.4 Reshape

Before B3a an instance's layout was fixed for its life: a settings change that would alter it was a rebuild
(`rp_node_type::layout_for`'s contract). The kernel cannot work that way. Its ports are per system, and its
state must survive a system being added: `setSystems` keeps every surviving id's tick counter and role
scratch, and every project stage's state (the Launchpad session), and prunes only removed ids. A rebuild
cannot carry that state by copying it either, since the new instance is created on the control thread while
the old one is still advancing on the audio thread.

So, while ABI v1 is unshipped, `prepare_settings` gained a fourth result, **`RP_PREPARE_RESHAPE`**: keep this
instance, with the layout `layout_for` gives for the new settings.

- The planner asks `prepare_settings` on **every** settings change, whether or not the layout moved: only the
  instance knows whether it can keep up with a new layout. `RP_PREPARE_SWAP` for a changed layout is a
  `node-contract` error, and `RP_PREPARE_RESHAPE` with the layout unchanged is a swap. That is a behaviour change for
  an ABI node that answers a swap for every change: it used to be rebuilt without a word when its layout moved, and is
  now refused (no in-tree node did so, and ABI v1 has not shipped).
- A reshape keeps the instance (its node id and type are unchanged) and binds the new plan's ports to it **by
  stable port id**. What is bound to a layout lives in a per-layout `PortState`, which each plan holds
  ([PlanStorage.hpp](../packages/native/src/graph/PlanStorage.hpp)): the layout, the param, control-output and
  resource-output stores, and the control side's records (the document values sent, the wired params, the last
  mask). A reshape builds a new one on the control thread while the running plan still uses the old, sharing into it,
  never copying, each store whose port or param persists: a param whose id, type, range and enum labels match, a
  control or resource output whose id, kind and resource type match. The rest start afresh: a param at its default,
  its document value (if the document gives one) sent again, a control output at 0, a resource slot empty.
- Feedback wires keep their last block, as they already do across a plan swap when a wire's key (its two
  endpoints) and shape are unchanged ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). A queue
  front whose source's capacity changed carries its block too while no audio thread runs, counting what the new
  capacity cannot hold as the source's drops, and starts empty while one does (the control thread may not read a
  front the audio thread is filling); a front whose channel count changed starts silent.
- The prepared settings commit **at the swap, on the block thread**, through the existing settings-commit path,
  in push order with every other command, so the first block of the new plan runs with them. A reshape that changes
  the layout always brings a `ports_changed`, after its `commit_settings`, so the node has the settings the mask
  belongs to. A script node receives the settings in a JS `onSettings(settings)` hook.
- A node that cannot reshape returns `RP_PREPARE_REBUILD`, as before. A C++ node opts in with `static constexpr bool
  reshapes`. A script type whose definition says `reshapes: true` answers `RP_PREPARE_RESHAPE` for every change its
  definition accepts; one without answers `RP_PREPARE_SWAP` while the layout stays the one it was created with and
  `RP_PREPARE_REBUILD` once it moves.

**The commit wait.** `setGraph` waited, up to its adoption timeout (2 s), for every published settings commit to be
adopted, because a C++ node's prepare may read its current settings and must not race the audio thread's commit. B1
never produced a commit; in B3a every edit that changes the kernel's settings (a system added or removed, a DSP role's
config, the MIDI routing, the controller) carries one to the kernel. So the wait now applies only to a
commit whose node reads its current settings to prepare. A type promises the opposite with
`RP_TYPE_STATELESS_PREPARE` on `rp_node_type.flags`, a plan records whether any commit it carries is to a type
without it (`PlanStorage::awaitsAdoption`), and `GraphHost::waitForCommits` waits only for such plans
([GraphHost.cpp](../packages/native/src/graph/GraphHost.cpp)). Every script type is stateless (the control-side
runtime checks the new settings alone, and the planner already knows whether the layout moved); a C++ node says so
with `static constexpr bool stateless_prepare`, which a `static_assert` refuses wherever its prepare would call
`needs_rebuild` (that reads the current settings). Consecutive kernel commits then queue on the ring and apply in
push order (§10 says what that bounds). Two things about the wait as built: it is global, so one commit to a stateful
type holds back an edit that touches only stateless nodes (a gate per instance is possible later); and every
`node.hpp` node without `needs_rebuild` already prepares statelessly (its prepare compares only what `create` fixed),
so the flag could be derived for C++ nodes rather than declared.

### 2.5 The bundle, embedded

The script-node bundle ([bundle.ts](../packages/retroplug/src/script/bundle.ts), whose one production type is the
kernel node) is esbuilt to a global-code IIFE and compiled to QuickJS bytecode at build time with the in-tree `tjsc`,
without `-m`, as the render worker is: the target `retroplug-script-bundle`
([bundles.cmake](../packages/native/cmake/bundles.cmake)), a derived C array under `build/native/` compiled into
`retroplug-backend` and never committed. Each Engine loads it into both runtimes at construction and registers the
script node types it defines; `Engine(sampleRate, chunks)` loads another bundle instead, which is how the native tests
run their toy nodes (the test-only twin, `retroplug-script-test-bundle`). Every host therefore has the same node types
with no boot call, and the boot-time `dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__))` in
`pluginControlPlane.ts` and `bootSession.ts` is gone. If the bundle fails to load, no script type is registered, so
every document naming one is refused (`unknown-type`), and the Engine logs why.

The bundle is about 660 KB and still carries zod (about 567 KB of it, by esbuild's metafile) through the kernel
node's settings schema and `dspRoles.ts`; B3b removes it. Both runtimes load it, which costs memory and construction
time, not per-block time: making a script host and loading the bundle into it takes about 11 ms in a Release build
(the engine-graph case that does just that).

### 2.6 Queue capacity

A plan queue holds 256 messages and 4 KB unless its port says otherwise (`kDefaultQueueMessages` /
`kDefaultQueueBytes`), and drops what does not fit. The old staging vectors were unbounded, so a large SysEx or a
burst of tracker sync bytes that passed them would be dropped on a wire. So an events or bytes **output**'s
descriptor may declare its capacity, `"capacity": { "messages": n, "bytes": n }`, either part or both, each at most
1 Mi messages or 16 MiB (in C++ `rp::bytes_out::sized<rp::capacity{...}>`, in TS `events.out(capacity)` /
`bytes.out(capacity)`). A wire carries its **source's** queue, and a feedback wire's front is sized from the source
too, so an input declares none: both parsers refuse a capacity on an input, and the C++ layer gives an input no
`sized`. The outputs that declare one are `rp.host.midi-in`'s and `rp.host.controller-in`'s (§3.3), every kernel
output (§4.1) and the Game Boy's `serialOut` (§3.2), which also sizes the MI.OUT feedback front and so stays fixed.
The kernel's two outputs that pass host MIDI on, each system's `serialIn` and `midi`, hold what `rp.host.midi-in`'s
output holds and room for a role's own bytes besides, so the one place host MIDI is dropped is that edge; the rest are
sized from the most the tests send through them in one block, with margin.

Queues are allocated per plan, two plans' worth during a swap (more while published plans wait unadopted, §10), and
B3a replans on every structure edit. A plan's events and bytes queues (each wired output's, each feedback front) are
held to a budget of 64 MiB of message items and bytes (`kPlanQueueBudget`): a document past it is refused
(`queue-budget`) before any instance is made, and an allocation the planner still cannot make refuses the plan
(`out-of-memory`). The kernel's queues take 278 KiB a system (§4.1), 9.0 MiB at 33 systems. An output with no wire
writes one shared scratch queue of the default size, whatever its port declares, and its drops go uncounted, since
nothing reads it. Control streams keep 64 changes a block, and the overwrite a full one makes is counted only on the
stream itself, never in the drops below. Drops are
counted per node instance: the executor folds each block's queue counts into the producing instance's count and the
host's (`GraphHost::droppedMessages()`, and `droppedMessages(id)` for one node), `reclaim` names a node the first time
it drops, and `Engine::droppedMessages` (which adds what `rp.host.midi-out` drops, §4.4) reaches TS through the
harness facet (`audio.droppedMessages()`). The parity rig and the native goldens run fail on a dropped message.

### 2.7 The call deadline

A script node's JS runs on the audio thread, so one that never returns would hang the host. Every call into the
audio runtime (`process`, the hooks, `create`, the block's promise jobs) is held to a deadline, 2 s by default, of
the **calling thread's CPU time** (`CLOCK_THREAD_CPUTIME_ID`, `GetThreadTimes` on Windows), never wall time: a
stall that is not the node's own (the thread preempted under load, Ctrl-Z on `retroplug-cli render`, a debugger or
a VM pause, the machine asleep) costs it nothing. QuickJS's interrupt handler takes the baseline at its first poll
inside the call and compares at the later ones (it is asked every 10000 polls), so arming the deadline reads no
clock. A call that runs past it is stopped, which JS cannot catch, and counted as a throw. A node with
`faultOnThrow` then faults. One without follows section 4.5 (the next block runs again) and faults only when its
next call is stopped too, so a real infinite loop is bypassed within about two deadlines while one long call costs
a block; a call that runs to its end starts the count again. A deadline fault is a fault like any other: it latches
on the instance and outlives reshapes, so only a rebuild, or the node removed and added again, clears it. Both
runtimes' JS stacks are held to 256 KiB, under a 512 KiB macOS secondary thread (a render job's, or a DAW's).

## 3. Ports and host I/O

### 3.1 System node inputs

Every system node gains four inputs, mapped onto core calls `SystemBase` already has (so no `dynamic_cast`;
[SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)):

| Port | Kind | Core call |
|---|---|---|
| `serialIn` | bytes | `pushSerialIn(frame, byte)`, one call per byte, at its message's frame |
| `midi` | events | `onMidi`, with the old rule: a message longer than a `MidiEvent`'s inline data goes down `pushCoreBytes`, unframed |
| `bytes` | bytes | `pushCoreBytes(frame, data, size, flush)`, the flush flag riding `RP_MESSAGE_FLUSH` |
| `buttons` | events | `pressButton(button, down)`, a 2-byte payload `[button, down]` |

A node applies them once, at the top of its `prepare`, where `process` starts too, so a linked Game Boy's coupled
group (which steps through `rp.step/1` and never calls `process`) has them all before any member steps. They go in
the old fan-out's order, `serialIn`, then `midi`, then `bytes`, then `buttons`, each in queue order. Only MIDI before
bytes is observable, on the NES, where both land in the same FIFO: a flushed bytes message drops what the FIFO still
holds, the same block's MIDI included, as it did. A Game Boy's `midi` input has no effect, as before (SameBoy's
`onMidi` queues into a list nothing reads); mGB and LSDj hear MIDI through `serialIn`.

### 3.2 The Game Boy's `serialOut`

The Game Boy node has a `serialOut` output (bytes): the bytes its core captured on the serial port this block while
MI.OUT capture is armed (the build-time `serialOutCapture` flag, or `setSerialOutCapture`). It replaces the engine's
per-block gather into `pendingSerialOut_`. That gather dropped the bytes' frames, and the kernel read them without
any; the port carries each byte at frame 0, so the kernel reads exactly what it read before. The node copies the
core's log in `finish`, only while the port is connected, and the core clears it at its next `prepareForBlock`.

Capture clocks a bit every sample frame while the Game Boy holds an external-clock transfer (SC = 0x80), so an armed
port carries about one byte every 8 frames, idle bytes included, not only MI.OUT data. Its capacity, 8256 messages and
8256 bytes, is a byte every 8 frames over the largest block a host can run (65536 frames, the GraphHost's
`kMaxBlockFrames`, to which the plugin grows its plan with its buffer size), with 64 to spare for the bits a block
carries in and the frames a core runs past its target
([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) `kSerialOutCapacity`). A Game Boy that clocks
the port itself shifts at its serial rate instead, and a CGB on its fast clock can outrun the capacity in a block past
about 12000 frames (about 6000 in double speed); what does not fit is dropped and counted. The capacity is fixed, so
the feedback wire from it keeps its front across every plan swap (§2.6).

### 3.3 Host I/O nodes

| Node | Ports | Replaces |
|---|---|---|
| `rp.host.midi-in` | out `midi` (events; 2048 messages, 64 KiB) | `pendingMidi_` handed to the kernel |
| `rp.host.midi-out` | in `midi` (events) | `DspRuntime::midiOut_`; feeds `Engine::midiOut()` |
| `rp.host.controller-in` | out `midi` (events; 1024 messages, 16 KiB), out `connected` (control) | `pendingControllerMidi_` and the per-block `controllerConnected` flag handed to the kernel |
| `rp.host.controller-out` | in `midi` (events) | `DspRuntime::controllerOut_`; feeds `Engine::controllerOut()` |

They are the package `rp.host.io` ([HostIoNodes.hpp](../packages/native/src/host/graph/HostIoNodes.hpp)), each over
the engine's `rp.engine.host-io/1` service, without which none activates. The engine keeps staging what the hosts
hand it (`stageMidi`, `stageControllerMidi`, the controller-connected state): the plugin and the SDL host stage on
the audio thread, the harness and the CLI through the command ring, as before. Each staging is a flat arena, every
message's frame and place in one byte buffer, reserved at construction for what its input node carries in a block,
so staging allocates nothing until a block stages more. The input nodes take what is staged at the top of their
block; what no node takes, in a plan with no input node, is dropped at the block's end rather than carried, as is what
was staged for a block too long to run. An input node takes everything staged whether or not its output is wired, so
`Engine::setGraph` refuses a document with a second `rp.host.midi-in` or `rp.host.controller-in`. The output nodes
append to the buffers the plugin, the SDL host and `drainMidiOut` read, which the engine clears at the top of each
block; `rp.host.midi-out` drops a message too short to carry its system prefix (§4.4), counted in
`Engine::droppedMessages`.

`controllerConnected` is a control value. `rp.host.controller-in` writes a change at frame 0 on the block the flag
flips, since a node cannot set an output's start (the start is the last block's end), so a reader takes the start and
any change at frame 0: `startValue` in [defineNode.ts](../packages/retroplug/src/graph/defineNode.ts). That value is
the old per-block flag.

### 3.4 Frame order

ABI queues are frame-ordered: Phase A's `pushMessage` clamps a message whose frame is earlier than the last one
up to it. Host MIDI is not always staged in frame order. The plugin stages the DAW's MIDI events first
([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), `stageMidi(e.frame, ...)`) and then each moved
automation slot's CC at frame 0, so a block can stage `[100, 300, 0]`. Before B3a the NES FIFO and the SMS sync role
stable-sorted by frame, so that CC landed at frame 0, while SameBoy's serial queue, which releases only the byte at
its front, took it after the frame-300 byte.

`rp.host.midi-in` **stable-sorts** the staged MIDI by frame as it pushes, without allocating. What the plugin stages
is two runs, each in frame order (the DAW's events, then the CCs at frame 0), so it merges the two straight onto its
queue, a tie going to the earlier run, in linear time; a staging of more than two runs is sorted in place first, by
insertion ([Engine.cpp](../packages/native/src/host/engine/Engine.cpp) `Staging::takeSortedInto`). So:

- the NES and SMS get exactly what they got (a stable sort of what they stable-sort);
- a Game Boy now receives a frame-0 automation CC at block start, before the block's notes. That is a
  **deliberate behaviour change**: frame 0 is when the automation happens, and a host that splits blocks at
  automation points (FL Studio) already puts it there. Parity cannot see it (`renderBlocks` refuses unsorted
  MIDI), so it has its own tests (§7.2).

The kernel's own output needs no sort. It sends out of frame order in two places, both LSDj sync modes: the
Arduinoboy protocol (a row byte at its note's frame, then START / STOP and the clocks at frame 0) and MI.MAP (a row
byte or the 0xFE note-off at its note's frame, then the 0xFF clocks at frame 0; §10). Both go only to SameBoy's
front-release serial queue, where the clamp raises the late bytes to the row byte's frame and the core takes them in
the same order as before. The kernel node pushes each sink call in emission order, and the plan delivers it to the
core in that order.

### 3.5 The N8 cart mirror

The merge ported the cart mirror as native logic in `runKernelStage`: for the first NES in slot order, its
core-MIDI and core-bytes went to the host's core-byte sink (`N8Link`) before the core's dispatch. B3a makes it
`rp.n8.cart-mirror`, a native node in the package `rp.n8` with inputs `midi` and `bytes`, which `buildGraph` wires
from the **first NES's** kernel `sys-<id>.midi` and `sys-<id>.bytes` outputs (an output fans out to any number of
inputs for free). It calls the sink for every MIDI message, then every byte message (the old order, flush flag
included), skipping an empty one, and does nothing while no sink is set. Its settings, `{ "system": <id> }`, name the
system it mirrors and change nothing it does, so its prepare is stateless. `Engine::setGraph` reads them with the
node's own reader (`cartMirrorSystem`), so the two accept the same settings, refuses (`cart-mirror`) a second mirror
and one naming a system that is not one of the slots' NES systems, and records the one it names with the plan:
`Engine::cartMirrorSystemId` reads the running plan's, for the hosts' "is a cart mirrored" test (no raw DAW MIDI to
the cart while one is) and for the `stageSystemMidi` mirror. `Engine::cartSystemId`'s walk is gone.

The two small items the merge left are done: the comments that cited a "BlipToaster host LFO" consuming CC76-79 now
name the routing and risa's sync, and `EngineCartMirror.test.cpp` judges a message staged at the cart against the
plan adopted ahead of it in the same drain.

## 4. The kernel node

### 4.1 Settings and ports

`rp.dsp.kernel` is a TS node ([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts)), defined with
`defineNode` in the script-node bundle around the unchanged `DspKernel`, with a registry of the DSP roles alone. Its
settings are the kernel's `KernelStructure` plus one fact per system:

```ts
{ project: RoleInstance[], systems: { id: number, pipeline: RoleInstance[], serialOut: boolean }[] }
```

`serialOut` is true for a Game Boy, the one system that has the port. `project` and `systems` default to empty, so a
document that names the type with no settings plans as an empty kernel; a role's `config` defaults to `{}` (its `kind`
and a system's `id` are required), and the ids are integers from 1 to 2^32 - 1 and must be distinct, which the old
kernel did not check (§10). Its ports, from those settings:

| Direction | Ports |
|---|---|
| in | `midiIn` (events), `controllerIn` (events), `controllerConnected` (control), `sys-<id>.serialOut` (bytes) for each system with `serialOut` |
| out | `sys-<id>.serialIn`, `sys-<id>.midi`, `sys-<id>.bytes`, `sys-<id>.buttons` for each system; `midiOut` (events); `controllerOut` (events) |

Ports are named by system id, never position, so a reshape (§2.4) binds a surviving system's ports to the
same wires. The two outputs that pass host MIDI on, `serialIn` (mGB, `lsdj-sync`'s passthrough) and `midi`
(`nes-n8-midi`), hold at least what `rp.host.midi-in`'s output holds (2048 messages, 64 KiB): twice its messages (a
forwarded message is one message on `midi`, and at most one on `serialIn`, which sends a run of same-frame bytes as
one, besides the sync roles' own bytes) and its bytes with 8 KiB more, for a role's own bytes (a sync role's clocks)
in a block whose forwarded MIDI fills `rp.host.midi-in`, so a SysEx the host stages whole reaches the system whole and
the one place host MIDI is dropped is that edge. The rest come from what the whole `test:native` suite sends
through them in one block, measured with the fixtures (parity included, 2.2 million kernel blocks up to 8192 frames):
each has room for 16 times that or more, messages and bytes, and the MIDI out is sized instead for a decoded MI.OUT
stream at its densest (a captured byte every 8 frames: 1024 in 8192). That sizing stops at 8192-frame blocks, where
§3.2 sizes `serialOut` for 65536: in a larger block a decoded MI.OUT stream can pass 1024 messages, and what does not
fit is dropped and counted:

| Output | Messages | Bytes |
|---|---|---|
| `sys-<id>.serialIn` | 4096 | 72 KiB, `rp.host.midi-in`'s and 8 KiB |
| `sys-<id>.midi` | 4096 | 72 KiB, `rp.host.midi-in`'s and 8 KiB |
| `sys-<id>.bytes` | 256 | 1 KiB |
| `sys-<id>.buttons` | 64 | 128 |
| `midiOut` | 1024 | 8 KiB |
| `controllerOut` | 256 | 4 KiB |

At 16 bytes a message plus its bytes, that is 278 KiB of queues per system per plan, nearly all of it the two that
pass host MIDI on (9.0 MiB at 33 systems, of the plan's 64 MiB budget, §2.6), with 32 KiB more for the kernel's own
two outputs and 96 KiB for `rp.host.midi-in`'s.

### 4.2 Per block

`process` builds the same `BlockInput` `DspRuntime::processBlock` built, afresh each block: the block info from the
transport, `controllerConnected` (only when true, read with `startValue`, §3.3), `midiIn` from its port, `serialOut`
by concatenating each system's `sys-<id>.serialOut` input in the order the settings list the systems (slot order, as
`buildGraph` writes them), the still-unfed `buttons` / `keys` as empty arrays, and `controllerIn` from its port when a
surface sent something. It calls `DspKernel.processBlock` with a `SinkTarget` whose methods push into the output
ports: `pushSerialIn` on `sys-<id>.serialIn`, a run of consecutive bytes at one frame to a message (sent when a byte at
another frame comes, and when `processBlock` returns or throws), which the system node applies a byte at a time at
that frame, as the old fan-out applied them; `emitCoreMidi` on `sys-<id>.midi`, `pushCoreBytes` on `sys-<id>.bytes`
(flush as the flag), `pressButton` on `sys-<id>.buttons` at frame 0, `emitMidiOut` on `midiOut` (§4.4),
`emitControllerOut` on `controllerOut` at frame 0. A system sink call for an id with no ports (an unknown id) is
dropped, as the old fan-out skipped an id it could not find; `emitMidiOut` sends for any id, tagged with it. A count
of the runs it holds lets a block that holds none skip the run bookkeeping, which the bench caught costing every block
(§10).

### 4.3 Structure changes

`onSettings(settings)` calls `setSystems(settings)`, so a structure change keeps the old rules exactly:
surviving ids keep their tick counter and role scratch, removed ids are pruned, project stage state survives.
Every structure change is a reshape (the definition says `reshapes: true`), so the kernel's instance is never
rebuilt. The new ports' serial-in runs start empty, so no byte crosses a reshape onto another port.

### 4.4 Host MIDI out

`drainMidiOut` reports which system emitted each message, and its emission order matters: a project stage (the
Launchpad controller addressing a real Game Boy) emits before every system stage. Per-system output ports would
lose that order. So `midiOut` is one port in emission order, and each message's payload is prefixed with the
emitting system's id (4 bytes, little-endian). Only `rp.host.midi-out` reads that port; it strips the prefix
into `Engine::midiOut()`'s `system` field, and drops a message too short to hold one, counted in
`Engine::droppedMessages`. One port in emission order relies on every role sending its host MIDI out at frame 0, as
all do (the Launchpad controller and LSDj's MI.OUT and master-sync decoders); a message at a later frame ahead of an
earlier one would have the earlier raised. The prefix is private to B3a: in B3b each protocol node has its own
`midiOut` port and `rp.host.midi-out` takes the id from its wiring.

### 4.5 A throw

Before B3a a JS exception in `processBlock` was dropped: the sink calls made before it stood, and the next block
called `processBlock` again. The kernel node keeps that rule: the script host catches the throw, keeps what was
pushed, counts it on the block thread (`Engine::scriptThrows`), keeps the first one's text in a fixed 512-byte buffer
for `reclaim` to log once on the control thread, and runs the next block. It does not return `RP_FAULT`, whose latch
would turn a transient error into MIDI that stays dead until a rebuild. B3b's protocol nodes may opt into the latch:
a definition with `faultOnThrow: true` faults on a throw, and a `process` that returns `"fault"` faults too.

The call deadline (section 2.7) keeps the rule, but for the serial-in runs the kernel still held: a call the
deadline stops runs no `finally`, so those bytes are cleared at the next `process` instead of sent (what it had
already sent stands). Its expiry is a throw like this one, so a kernel without
`faultOnThrow` runs again after one, and faults only on a second in a row, which a node that has hung for good
reaches within two deadlines. It counts the thread's CPU time, so a stall that is not the kernel's own never trips
it. This matters more for the kernel than for any node: it reshapes on every system added or removed, and a fault
outlives reshapes (only a rebuild or a remove-and-add clears it), so a spurious one would drop MIDI routing for the
rest of the session.

## 5. The TS side

### 5.1 `buildGraph`

The model gains each system's `pipeline` and the project's stages; `kernelProjection` still computes them from
the store (B3b moves that logic into `buildGraph` itself). The document
([buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts)) gains:

- `rp.host.midi-in`, `rp.host.controller-in`, `rp.host.midi-out`, `rp.host.controller-out`;
- the kernel node (`kernel`) with its settings (§4.1), the systems in slot order (positional MIDI routing reads it),
  and its wires: `midi-in.midi -> kernel.midiIn`, the controller pair, each `kernel.sys-<id>.*` output to
  `sys-<id>`'s input of the same name, each Game Boy's `sys-<id>.serialOut -> kernel.sys-<id>.serialOut` as a
  **feedback** wire (MI.OUT's one block of latency), `kernel.midiOut -> midi-out.midi`,
  `kernel.controllerOut -> controller-out.midi`;
- `rp.n8.cart-mirror` (`cart`) wired from the first NES in slot order, when there is one.

The kernel runs before the systems because its wires say so, not because it is listed first. The 840 goldens changed
on purpose and were re-recorded from `buildGraph` (every model gains an empty structure; the audio part of every
document is unchanged); the native goldens test plans every distinct document among them on an Engine with the
embedded kernel and runs a block of each, but for the 33-system `perSystem` ones, refused as they must be (more buses
than `rp.host.audio-out` takes). B2's remark that two slot orders can build one document no longer holds, since the
kernel lists its systems in slot order; `EngineGraph`'s dedupe key keeps the slots all the same.

### 5.2 The stores

`ProjectStore.publishGraph` runs `projectKernelStructure` (with the controller projection, which moved in from
`appHost`) and publishes the systems with their pipelines, the routing and the project stages as one model,
`EngineGraph.publish({ systems, routing, project })`. A system's pipeline carries only the roles the kernel runs
(`SystemsStore.graphSystems`, by the kernel's own test, `isSystemStage`: a DSP behaviour and a scope that is not
`project`), so a model, palette or asset edit leaves the document as it was; a store with no registry hands the
kernel every role, and publishes an edit to any of them. An edit to a role the kernel runs (an LSDj mode change), a
MIDI-routing change, a controller edit and a polled song or start-edge change publish, like any structural edit;
`EngineGraph`'s dedupe drops a publish that changes nothing. A project load holds the publishes back while it adopts,
then publishes once; one that throws partway publishes what it adopted, with their pipelines, and every exit of a load
tells the structure's observers once. `syncDspFromStore`, its push and the kernel half of the `onSystemsChange`
wiring are gone; the hook stays for the structure's other observers, the UI and the plugin's watched ROMs.

### 5.3 `DirectGraph`

`DirectGraph` gains `setPipeline(id, roles)` and `setProjectStages(stages)`, each a publish without a tap re-arm
([directGraph.ts](../packages/retroplug/src/graph/directGraph.ts)). A system it adds or replaces starts with no
pipeline, and until `setProjectStages` there are no project stages, so no MIDI is routed to any system; a refused edit
puts the structure back. The callers moved mechanically, as B2 moved the build calls: 20 direct `test-native` files
moved their 32 `dsp.setSystems` pushes onto `setPipeline` / `setProjectStages`, 30 store-built ones dropped the
kernel load and the sync hook, and `render.ts` (the CLI `render` and the UI's `RenderHost`) and the two NES export
sessions dropped their manual sync, since the stores they build through now publish the pipelines themselves. The
harness's `stageMidiIn(bytes, frame?)` takes a frame: the TS driver refuses one that is not an integer from 0 to 1023
before it sends (rpcpp would truncate it), and the RPC refuses one past `renderAudio`'s 1024-frame block. And
`MockBackend.kernelSettings()` reads the kernel's settings out of a published document.

### 5.4 What went

The `dspSetSystems`, `dspLoadKernel` and `compileScript` RPCs (the facets' union went from 89 methods to 87: those
three gone, and the harness's `droppedMessages` added, §2.6);
`DspRuntime` whole (the kernel's context, its sinks and thunks), `dspRuntime.ts`, `dspKernelBundle.ts` and
`__DSP_KERNEL_BUNDLE__`; the CLI session's `dsp` client and its SDK declaration; native's `KernelNode` and
`rp.engine.kernel/1`, `runKernelStage` and its fan-out, `Engine::loadKernel` / `setSystems`, the `SetSystems` and
`LoadKernel` commands, `pendingSerialOut_` and its gather (with `SystemEntry::sameboy`, its last reader, and the
per-slot `dynamic_cast` that filled it), both `Engine::cartSystemId` overloads, and `SystemTable`'s transitional
host-MIDI flags (the engine now drops unclaimed staging at the end of every block).

What stayed: `ScriptCompiler` (the native tests compile toy nodes with it), `DspCommand`, the `Dsp*` profiling structs
(now in `ScriptHost.hpp`, which TS mirrors), `DspKernel.setSystems` in TS until B3b, and the `dsp*` profiling and
trace RPC names `dsp-bench`, `nes-bench` and `tools/run-profile.sh` use. The script host took over the runtime's
profiling: a profile build's allocation window per Engine block, auto-GC pinning, a timed collection, and the trace
window, in which every script node's `process` is a `dsp-kernel` span; resetting either window and the GC pass refuse
while an audio thread owns the runtime, and the trace reads return nothing then (spec/08 has the detail).

## 6. Block size

The engine takes any block from 1 frame up to its plan's largest (8192 frames unless the host grows it with
`setMaxBlockFrames`, to at most 65536), and a frame stamp is an offset inside its block, so a host that splits a
block (FL Studio splits at automation and tempo points, down to 1 frame) is giving it two blocks. Three properties
are worth stating:

- **Frame 0 is the block's start.** Under splitting hosts it is exactly when automation happens, which is what
  the sort at the source (§3.4) relies on.
- **MI.OUT's latency is one block, not a fixed time.** In samples it is the previous block's length, so it
  varies with a splitting host. That was true before B3a, and the feedback wire keeps it bit for bit.
- **A script node costs one JS call per block.** Measured (§7.4): about 0.09 us with nothing to pass and 0.18 us with
  a message in and out, flat in node count and block size, against a 1-frame block's 22.7 us at 44.1 kHz. B3b's
  protocol nodes multiply calls by node count, so a 1-frame block means several JS calls per sample, and that is
  affordable: the main design's "batch consecutive script nodes into one call, not built until measured"
  ([§4.9](modular-graph-design.md)) is measured and not needed (§9).

Parity rendered the same scenes in 64, 199, 512, 1024 and 8192-frame blocks, a mixed cycle, and an edge segment with
a 7-frame block, but never a 1-frame block or a ragged FL-style split, until B3a's first commit recorded both (§7.1).

## 7. Testing

### 7.1 Parity, extended first

Before any B3a code existed, the tiny-block parity groups were recorded on the merged engine (5002c4b4, and master
sync's cases in 955299e2), in three splits:
`1` (1-frame blocks), `fl` (the repeating ragged list `[33, 1, 222, 1, 7, 4096, 2]`) and `fl256` (a 256-frame buffer
split inside at automation points, `[256, 33, 1, 222, 255, 1, 1, 255, 7, 249]`), with MIDI, automation CCs and
transport points on block edges, on the first and last frame of a block and in 1-frame blocks
([blocks-tiny.ts](../packages/retroplug/test-native/parity/blocks-tiny.ts)):

- `blocks-tiny-mgb` and `blocks-tiny-nes` (mGB and BlipToaster at 44.1 and 48 kHz, fixture-free, so CI runs them);
- `blocks-tiny-lsdj` (LSDj MIDI sync, Arduinoboy and MI.OUT), `blocks-tiny-lsdj-master` (LSDj master sync, in a
  file of its own so the two run in parallel, 59e4b183) and `blocks-tiny-risa` (risa host sync), all fixture-only.

The Arduinoboy cases also show the Arduinoboy defect ([B0 design §2.5](modular-graph-phase-b0-design.md)) is not "every
block carries a tick": it stays silent in `1` and `fl256` and plays in `fl`, where six blocks of seven carry almost
none. Its play and stop notes sit on a block's first frame, so a one-block delay of either is caught in `fl` (59e4b183,
22ae5cc4). LSDj master sync was recorded in the same splits, at 44.1 and 48 kHz, with its defect pinned (955299e2, B0's
eighth): in small blocks its decoder reads a stopped LSDj's idle flood as clocks (§10). The pinned cases are
`blocks-tiny-lsdj-master`'s `masterSync-44100-{1,fl,fl256}`, which play and stop, and `masterSync-48000-{1,fl,fl256}`,
silent (B0's sixth defect), so their messages are the flood alone. The fix, right after B3a and before B3b, re-records
these cases, and possibly `lsdj-modes`' `masterSync-44100` and `masterSync-48000`, whose boot noise the same threshold
decides.

Segments are short, since 1-frame blocks multiply the block count. B3a is held to strict parity: every
group, with the fixtures present and `RP_FAIL_ON_SKIP=1`, and every commit of B3a held it bit for bit.

### 7.2 The deliberate change

Catch2 cases ([HostIo.test.cpp](../packages/native/test/engine-graph/HostIo.test.cpp)) hold `rp.host.midi-in`'s sort
(frame order, ties in staging order, `std::stable_sort`'s order over 3000 random stagings, nothing allocated from
staging to the queue, the merge path included). `parity/host-midi-order` stages
plugin-style MIDI (notes, then a frame-0 CC) through `stageMidiIn` on mGB and on BlipToaster: each renders exactly what
the same messages staged in frame order render, and the Game Boy differs from the CC placed after the notes, its old
behaviour. `nes-core-bytes` sends a 60 KiB SysEx and requires it to reach the NES FIFO whole, with no drop.

### 7.3 Reshape and the script host

- Native tests with a toy reshaping node (`retroplug-graph-test`): the instance is kept, its ports are rebound by id,
  a feedback wire keeps its last block, the settings commit at the swap, a node that returns `RP_PREPARE_REBUILD` is
  rebuilt; and the invariants a reshape relies on (its ports switch only with a published plan, what carries over and
  what starts afresh, a plan committing to both a stateless and a stateful prepare awaits adoption).
- Toy TS nodes ([ScriptNodes.test.cpp](../packages/native/test/engine-graph/ScriptNodes.test.cpp)): input and output
  marshalling, sends past capacity refused and counted, layouts from the control-side runtime, a reshaping node kept and
  a non-reshaping one swapped or rebuilt, creation at adoption and release at reclaim on the block thread (the release
  ring, its backlog and the 16 a block), a throw's count and the latch, a failed write onto what a node holds (a setter
  it hung there, or an object it froze, its audio views among them) counted as its throw while the setters one node
  hangs on the shared prototypes charge no other (one that shifts, splices and empties every array it is handed
  included), a method only `Object.prototype` holds is adopted by none, a getter on a base class or a Proxy's `get` trap
  answers a node's methods, a prototype cycle through a Proxy or a chain past 1024 objects fails its `create`, and no
  node replaces a method of the frozen `Block` prototype, the warm block path allocating nothing native; the JS-owned
  audio copies against a node that keeps its array across swaps, transfers its buffer, writes an input in place or edits
  the array; promise jobs run and bounded per block, a queueMicrotask callback's throw counted; the call deadline on CPU
  time, once counted and twice faulted; a second load and a bundle that differs between the runtimes refused; two
  Engines on two threads; and ThreadSanitizer runs a plan's script nodes on an audio thread while the control thread
  plans, reshapes, rebuilds and reclaims them. A profile build adds five `[profile]` cases on the trace window and the
  calls it refuses while an audio thread runs (spec/08).
- The kernel node ([KernelNode.test.cpp](../packages/native/test/engine-graph/KernelNode.test.cpp)), in a plan with the
  host I/O nodes: host MIDI under every routing, every LSDj sync mode, risa and smsggdj host sync, a controller
  session, state across structure changes (survivors go on, a removed id is pruned), a throw, a block that fills
  `rp.host.midi-in` reaching an mGB and a NES whole, and a reshape under a running audio thread. Each case is held to
  digests of everything the node sent, which until the old kernel stage was deleted ran beside it and was found
  equal to it block by block (c323c7a8 to e3de2e46). A Game Boy's serial input is counted and hashed byte by byte, so
  sending a run of same-frame bytes as one message moved none of the recorded values.

### 7.4 The bench

[ScriptBench.test.cpp](../packages/native/test/engine-graph/ScriptBench.test.cpp), hidden from the normal run
(`build/bin/retroplug-engine-graph-test "[bench]"`), Release, 44.1 kHz on a Ryzen 9 9950X, medians of 7 runs. It was
written while both kernels existed (d22bdb20, 9c3c10f9), so it timed the kernel node against the old kernel stage over
the same blocks; the figures below are three runs of that version, as ranges. Since the deletion it times the node
alone.

**A script node's call.** Chains of 1, 4, 16 and 64 pass-through TS nodes (`rp.bench.pass`, indexed loops) between a
native source and sink: about 0.09 us a node with nothing to pass (0.084 to 0.102, about 1/260 of a 1-frame block)
and 0.18 us with one message in and out (0.171 to 0.187, about 1/125), flat in node count and block size, with no JS
or native allocation per block. 64 nodes take about 5.6 us, a quarter of a 1-frame block.

**The kernel node against the old kernel stage**, over mGB, an LSDj in MIDI sync and a NES on `nes-n8-midi`,
`sendToAll`, playing at 120 bpm, a host message every 441 frames and each Game Boy's MI.OUT at its maximum, a byte
every 8 frames; no core on either side:

| Block | Old kernel stage | Kernel node | Ratio | The node's JS allocations a block |
|---|---|---|---|---|
| 1 frame | 1.95 to 2.02 us | 2.68 to 2.74 us | about 1.4 | 25 |
| 64 frames | 5.01 to 5.08 us | 7.69 to 7.92 us | about 1.55 | 81 |
| 512 frames | 24.5 to 25.5 us | 39.5 to 40.7 us | about 1.6 | 431 |

So the node takes about 1.35 to 1.6 times the stage's time (a fourth run reached 1.65), rising with the block size: over
the three runs the ratio was 1.36 to 1.37 at 1 frame, 1.53 to 1.57 at 64 and 1.59 to 1.62 at 512, ranges narrower than
the run-to-run spread (the fourth gave 1.39, 1.53 and 1.65), and the 512-frame rows move 2 to 3% from run to run. The
plan's native nodes take 0.033, 0.050 and 0.16 us of the node's figures, and allocate nothing native; the stage made 0,
0.15 and 1.16 native allocations a block, and its JS allocations went unmeasured (its runtime counted them only in a
profile build). The gap is per message: the `Message` arrays refilled, then the kernel's own `BlockInput` copy of them,
and one send per output. Idle MI.OUT bytes are nearly all of what the node reads in the larger blocks: at 512 frames the
two Game Boys send it 128 a block against about one host message, and the node's JS allocations grow with them. Capture
is armed only for an LSDj in a MIDI-out mode, so the table is the worst case. The bench also caught the kernel node's
serial-in runs costing every block, held or not: a count of the runs it holds (9c3c10f9) took its 1-frame block from
2.91 to 2.92 us down to the table's 2.68 to 2.74, in an A/B on the same tree. The numbers decide B3b's granularity (§9).

### 7.5 Acceptance

Strict parity (all 30 files, the new groups among them, 197 cases in 26 groups); `pnpm test`, `test:native`,
`test:plugin`, `test:ui`, `test:node`, `typecheck`, `check:docs`; the thread and address sanitizers; the Reaper checks
(including the LSDj sync and MI.OUT renders and the editor checks); `sdl:smoke` and `sdl:pipewire`; the
BlipToaster and NESVJ consumer suites against the built CLI.

## 8. How it lands

As B2 did: a working prototype built and verified on a scratch branch first, a plan written from it, then
executed task by task with review, each task's review fixes following its commit. In order, as built:

1. The new parity groups, recorded on the merged engine (5002c4b4), and master sync's tiny-block defect pinned in
   them (955299e2; 59e4b183, 93b3515f and 22ae5cc4 split the LSDj group in two and sharpened what the cases pin).
2. Reshape: the ABI result, the planner's keep-and-rebind, the settings commit, the stateless-prepare flag (7f8c7252;
   7e8454a6).
3. Port capacities in descriptors, and drops reaching the host (decea323; 2a391e69 and 71427499: inputs refuse a
   capacity, the plan's queue budget, drops named reliably).
4. System node inputs, the Game Boy's `serialOut`, the host I/O nodes and `rp.n8.cart-mirror`, beside the old path
   (eec48b20; d12bbc1f and 9c0c271c).
5. The script host: the two runtimes, the shim, marshalling, the embedded bundle, the ABI's type context (4d669338;
   b5a149ab, 4defa782 and 5e259bd5: JS-owned audio copies, promise jobs, the call deadline).
6. The kernel TS node (c323c7a8; 7254188c and ddbc5a14: its serial-in runs and capacities), then the flip:
   `buildGraph`'s wiring, the stores publishing the structure, `DirectGraph`'s calls and their callers moved
   (bf3481fa; 7df64212 and f385f542).
7. The bench (d22bdb20; 9c3c10f9). It compares the kernel node with the kernel stage it replaces, so it moved ahead of
   the deletion, while both existed.
8. Delete the fan-out, the RPCs and the native kernel node, after moving the runtime's profiling onto the script host
   (e3de2e46, b7963b0d; 45bb10c6 and 822ff3eb).
9. The docs (33a49a91; 4f74b62d: the comments the deletion left behind).
10. The marshalling and realm fixes a review of the script host found (2150de4c, d014d99c, 78d1cc02 and 1ed9a283): a
    failed write in a marshal fill is the node's throw, the host defines what it writes onto and asks an array whether
    a slot is its own, it reads a node's methods short of `Object.prototype` through a bounded walk, and the `Block`
    prototype is frozen (§2.3, §10).
11. The branch review's cleanup: the host contract a node author needs, written into `defineNode.ts`; `activate`
    faulting under `faultOnThrow`, as the other hooks do; byte headroom on the kernel's two outputs that pass host
    MIDI on (§4.1); and the docs it found stale.
12. The acceptance run, on the final tree: every suite, strict parity with the fixtures present, the Reaper leg, the
    sanitizers and the consumer suites.

## 9. Left for B3b

The bench (§7.4) settles how B3b is cut: one node per role is affordable at 1-frame blocks, and batching is not
needed.

- **One protocol node per role** (`mgb.midi`, `lsdj.sync`, `risa.sync`, `sms.sync`, `n8.midi-to-fifo`, MIDI routing,
  the Launchpad controller), each with its own ports; the kernel node and `DspKernel` deleted. Split per role, the
  bench's structure (four roles) makes three more script calls a block, about 0.25 to 0.55 us, 1 to 2.5% of a 1-frame
  block. A port per protocol also ends the 4-byte system prefix on host MIDI out (§4.4), and with it the one port's
  reliance on every role sending its host MIDI out at frame 0.
- **No batching** of consecutive script nodes into one call: the main design's §4.9 "not built until measured" is
  measured and not needed. It would matter only at hundreds of script nodes on 1-frame blocks, the Reaktor-scale case
  (1000 empty nodes take about 90 us a block).
- **Write protocol nodes as `rp.bench.pass` is written**: indexed loops over the `Message` views, nothing allocated
  per block. A node's cost is what its JS does, not the call.
- **Wire a Game Boy's `serialOut` only into a node that decodes MI.OUT**, so a Game Boy without one pays nothing for
  an armed capture's idle bytes.
- `kernelProjection`'s special cases (the synthesized `midi-routing` and Launchpad stages, the forced
  `lsdj-sync` mode for MI.MAP) to move into `buildGraph`.
- A per-product script-node bundle with no zod in it.
- How several stages' outputs merge into one system input: each core must still get all its MIDI before all its
  bytes (the NES FIFO, §3.1), and `rp.core.merge`'s output is default-sized, so merging the kernel's large wires needs
  a merge whose output capacity its settings give.
- The `apu-render` span starts where the last script node's `process` ended, which is right while every script node
  runs before the systems, as in B3a; a protocol node placed after a system would need it revisited.

Before B3b starts, the LSDj master-sync fix lands on its own (§10).

## 10. Found while building

- **A commit made the next edit wait, and then the ring bounded edits.** `GraphHost::setGraph` waited up to its
  2 s adoption timeout for every published settings commit, because a C++ node's `prepare_settings` may read its
  current settings and must not race the audio thread's commit. B1 never produced a commit, so the wait never ran; in
  B3a every edit that changes the kernel's settings carries one. With audio running that costs at most a block, but a
  host that stops calling `process` while the plugin stays active (DPF's CLAP `stop_processing` does nothing) would
  have frozen the UI 2 s per edit and then refused it `busy`. The wait now applies only to commits to a type without
  `RP_TYPE_STATELESS_PREPARE` (§2.4). So consecutive kernel commits queue on the command ring instead (256 slots, 255
  usable, shared with params and the engine's commands), and while no block runs (a burst of edits, or that CLAP host)
  every queued plan holds its buffers until it is adopted, and once the ring is full `setGraph` answers `busy`. That
  was kept for B3a: it is bounded, and a refused edit is not lost, since `EngineGraph` keeps the model it was asked for
  and sends the next document even when it matches (B2's refusal contract), so the latest model lands with the next
  publish. Coalescing queued, unadopted plans is the alternative, should it bite.
- **The ABI's type-level calls had no context.** `descriptor`, `layout_for`, `query_memory` and `create` received
  no hint of the type they were called through, so one host could not serve several types through one set of
  functions, which is what the script host's one type per definition needs. They now take the `rp_node_type` first,
  and it gains a trailing `ctx`, null for a C++ node (§2.2).
- **`SystemNode::finish` wrote silence into every connected output** through its audio view, whatever the port's
  kind. Nothing but audio outputs was ever connected until the Game Boy's `serialOut`, whose queue it would have
  overwritten; it now silences audio outputs only.
- **An unbounded MI.OUT gather.** With the old kernel loaded but the kernel node in its place, the engine went on
  gathering MI.OUT into `pendingSerialOut_`, which only the old kernel stage cleared, so it grew without bound once
  capture was armed. The flip deleted the gather in the same change, and a plan with no `rp.host.midi-in` (or
  `controller-in`) now drops what was staged at the block's end rather than letting it grow.
- **LSDj master sync's stopped-LSDj detector depends on the block size.** `arduinoboyMasterSyncBlock`
  ([lsdjArduinoboy.ts](../packages/retroplug/src/lsdjArduinoboy.ts)) calls a block holding more than 16 serial-out
  bytes (`MASTER_SYNC_IDLE_FLOOD`) the stopped LSDj's idle flood. A stopped LSDj sends a few thousand bytes a second,
  so no block of fewer than about 150 frames at 44.1 kHz ever holds 16, and the handshake decodes as clocks: 10497
  MIDI messages in 1-frame blocks against 101 in 1024-frame ones; and at a fast tempo an 8192-frame block can pass 16
  while LSDj plays and read as stopped. It is pinned as it is (§7.1). By the user's decision it is fixed right after
  B3a, before B3b ports the decoder, as its own change re-recording exactly the cases it moves (`lsdj-modes`'
  `masterSync-44100` and `masterSync-48000` may move too, since the same threshold decides their boot noise). The fix
  needs a window across blocks, trading spurious clocks at the stop edge against a few milliseconds of clock latency.
- **The NES path cannot see one block of delay at 1-frame blocks.** BlipToaster's output does not change when every
  event of the edge segment moves by one frame (a lone note-on moved by a frame changes it about once every 67 frames
  at 48 kHz), and in 1-frame blocks a frame is exactly one block of latency, so `blocks-tiny-nes`' `1` cases are blind
  to it. Its ragged splits, whose blocks are mostly far longer than a frame, pin it; mGB hears a one-frame move in
  every split. `blocks-tiny-risa`'s `transport-1` has the same blind spot (a transport point one block late is one
  frame late there), and its `fl` and `fl256` cases catch it.
- **MI.MAP sends out of frame order too.** §3.4 as designed named only the Arduinoboy protocol; MI.MAP also sends a row
  byte at its note's frame and then its 0xFF clocks at frame 0. Both reach only SameBoy's front-release serial queue,
  so a frame-ordered queue's clamp changes nothing the core sees: the equivalence test applied the clamp to the old
  stage's output, counted the frames it raised, and parity held bit for bit.
- **Duplicate ids are refused.** The old kernel took a structure naming one system id twice without a word; the kernel
  node's settings refuse it ("two systems have the same id"), as they refuse an id outside 1 to 2^32 - 1.
- **The bench's numbers** (§7.4): a script call costs about 0.09 to 0.18 us whatever the node count or block size, the
  kernel node about 1.35 to 1.6 times the old kernel stage (a fourth run reached 1.65), the gap per message, and idle
  MI.OUT bytes most of what the larger blocks carry. One node per role is affordable; batching is not needed (§9).
- **The bench caught a cost every block paid.** Sending serial-in as runs (below) put a clear over every system at the
  top of the kernel node's `process` and a send of every run in its `finally`, though nearly every small block holds
  none. A count of the runs held gates both (9c3c10f9), which took about 0.2 us off a 1-frame block, and sends exactly
  what it sent.
- **Staging is allocation-free.** Host and control-surface MIDI are staged into a flat arena each (every message's
  frame and place in one byte buffer), reserved at construction for what the input node carries, and no-allocation
  cases hold it: from a direct `stageMidi`, through the command ring, at everything `rp.host.midi-in` carries in a
  block, for the control surface, and for a block too long to run, which drops what was staged for it. The output side
  still allocates: `rp.host.midi-out` and `controller-out` append one vector per message to the engine's buffers, the
  next candidate.
- **Audio views over plan memory could not be made safe** (b5a149ab). The prototype handed JS `Float32Array` views
  straight over the plan's buffers and detached them before a buffer was freed, and JS outran it: an array kept across
  a plan swap or a re-activation wrote into the freed plan, `transfer()` moved or reallocated plan memory, and an
  edited array kept a view the detach never saw, each a heap-use-after-free under ASan; an input view written in place
  changed its source's buffer, or the plan's shared silence, for every later reader. Audio now reaches JS as the
  instance's own copies (§2.3). Promise jobs had never run either, so a reaction queued in `process` never fired and
  was kept without bound; they now run at each block's end, bounded.
- **A script node could hang the host** (4defa782, 5e259bd5). A call that never returned held the audio thread for
  good; every call is now held to a deadline and both runtimes' JS stacks to 256 KiB (§2.7). The first deadline
  measured wall time, so a stall that was not the node's (preemption, Ctrl-Z on a render, a debugger) could expire it,
  and a fault outlives reshapes, which for the kernel would drop MIDI routing for the session: it now counts the
  thread's CPU time, and a node without `faultOnThrow` faults only on a second expiry in a row.
- **The kernel node dropped mid-graph what the host edge had accepted** (7254188c). Its `serialIn` and `midi` outputs
  were sized from what the suites sent, below what `rp.host.midi-in` passes, so a dense block of host MIDI lost its
  tail on the kernel's wire (stuck notes on an mGB). Both now hold at least `rp.host.midi-in`'s output, and `serialIn`
  sends a run of same-frame bytes as one message (§4.1, §4.2); a case fills `rp.host.midi-in` and requires all of it at
  an mGB and a NES.
- **The Game Boy's `serialOut` was too small for the largest block** (d12bbc1f). At 4096 messages and bytes an mGB in
  65536-frame blocks dropped about 2700 bytes a block; it holds 8256 now (§3.2). The same review found that a second
  `rp.host.midi-in` would take the staging from the first, since each takes everything staged, wired or not, so a
  document may hold one (§3.3).
- **Capacities on inputs bounded nothing, and nothing bounded a document** (2a391e69). A wire carries its source's
  queue, so a capacity on an input only read as if it did something; both parsers and the C++ layer refuse one now.
  Each port may declare about 32 MiB, so a plan's queues are held to a 64 MiB budget (§2.6).
- **The bundle's load left about 144 KiB of cycles** in the audio runtime, waiting for QuickJS's threshold collection
  inside a block (45bb10c6); the first quiescent reclaim collects them now (§2.1).
- **A failed write in a marshal fill was swallowed.** `fillMessages` and `fillControl` ignored what each
  `JS_SetProperty` returned, so a setter a node hung on a message (or a message it froze) left an exception pending
  while `messages()` returned: the throw went uncounted, and stayed pending for whatever asked QuickJS next. A failed
  write now ends the fill as running out of memory did, and the host's writes onto the `Block` and its transport
  before `process` go the same way (§2.3). Its review found the audio views still swallowing one: a refused write onto
  a node's views array was cleared and the block run, uncounted, even when the refusal was the deadline stopping a
  setter there. It now fails the block as the others do, and every view is made again the next block.
- **One node's prototype setters were charged to another.** The host's first write of each property onto an object it
  had just made (the transport and `frames`, a message's `frame` / `data` / `flags`, a change's `frame` / `value`, the
  control stream's `start` / `changes`, a new slot of an array, `create`'s context, `portsChanged`'s mask), and its
  read of the element pool past its end, walked the prototype chain, so an accessor one node hung on `Object.prototype`
  or `Array.prototype` ran there for every node beside it, and with the fix above its throw was theirs: on the `Block`,
  its transport or their audio views it skipped their blocks, the kernel's included, and in a `messages()` or
  `control()` fill it was thrown inside their `process`, which might catch it; a getter there that returned an object
  of its own would have had another node's messages written into it. The host now defines what it will write as the
  object's own data property when it makes the object, and reads the pool only where it defined the slot (§2.3). Its
  review found three more ways in. Whether a slot was set or defined was decided by what the host had written there
  before, which a node's ordinary `shift()`, `splice` or shorter `length` makes wrong, sending the next fill's write up
  the chain; it now asks the array at each write. `process` and the hooks were read through `Object.prototype`, so a
  getter there that threw failed every later node's `create`, and a function there became the hook of every node
  without one; they are read short of it now. And the `Block` prototype every node reaches was open, so one node could
  replace `messages` for all; it is frozen. The walk that read the methods short of `Object.prototype` had no bound of
  its own: a `create` whose object's prototype was a Proxy over the object itself, a cycle `setPrototypeOf` cannot see
  through, held the block thread for ever, since the walk ran no JS for the deadline to stop (the plain read it replaced
  had recursed into the Proxy until QuickJS's stack check threw); and a wrapper whose `get` trap answers the methods,
  a trap the walk never asked, had stopped working. At a Proxy the read is a plain one again, and the walk stops at 1024
  objects. A field a node deletes is left as the one gap (§2.3), and the realm itself,
  whose prototypes and globals one node's own code can still change under another's, is what one context per node
  would close (spec/07).
- **The TS stripper does not build outside the repo.** `retroplug-ts-stripper-bundle` fails in a build directory
  outside the source tree: [build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) sets no `absWorkingDir`, unlike
  `build-ui.js`, so esbuild resolves its `typescript-5` alias against the build's working directory. Pre-existing, and
  worth a fix of its own.
