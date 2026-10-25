# Phase B3a: real ports and the script-node runtime

**Date:** 2026-10-25 · **Branch:** `feature/modularize` · **Status:** designed, not built.

Phase B of [modular-graph-design.md](modular-graph-design.md) moves the engine onto the graph in steps,
each held bit for bit to the B0 parity baseline ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)).
B1 ran native on the executor, B2 ([modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)) gave
the graph's structure to TS. B3, "the DSP kernel dissolves", is split in two: **B3a** (this document) makes the
kernel's input and output real wires and runs today's kernel, unchanged, as one TS node in a new script-node
runtime; **B3b** splits that node into one protocol node per role and deletes the kernel.

## 0. Where B3a sits

Before B3a, `main` was merged into this branch (c2245f1b). Its one feature written against the old engine,
the physical-N8 cart mirror (`Engine::cartSystemId`), was ported onto the graph engine, and parity did not move:
the reliable link stays off for every parity ROM (each advertises caps `0xFF`), and no case reaches the empty
N8 FIFO read main changed. B3a starts from that merged tree, so its bar is the behaviour that ships.

Today the kernel is a sealed box. `rp.dsp.kernel` is a native node with no ports, first in every plan
([B1 §3.3](modular-graph-phase-b1-design.md)). Its `process()` is `Engine::runKernelStage`: it hands the
staged host MIDI, the control-surface MIDI and last block's MI.OUT bytes to one `processBlock` call in a bare
QuickJS context (`DspRuntime`), then fans six system-addressed sink kinds into the cores by id, in a fixed
order. Its structure (each system's role pipeline, the project stages) arrives separately, through
`dspSetSystems`, and the bundle through `compileScript` + `dspLoadKernel` at boot.

After B3a, that I/O is wiring the planner can see: host I/O nodes, input ports on every system node, a
`serialOut` port on the Game Boy, and a kernel node whose settings are its structure.

## 1. Decisions taken

| Question | Decision |
|---|---|
| Merge `main` before B3 or after? | **Before** (done: c2245f1b). B3 rewrites the code where NES MIDI reaches the N8 FIFO, which is where main had moved on. |
| How is B3 cut? | **B3a then B3b**, each with its own spec, plan and bit-identical check. B3a: the runtime and the ports, with today's kernel as one script node. B3b: one protocol node per role, `kernelProjection` folded into `buildGraph`, zod out of the bundle, the kernel deleted. |
| Frame order on the new wires | **Sort at the source.** `rp.host.midi-in` stable-sorts the staged host MIDI by frame, so every queue keeps the ABI's frame-ordered promise (§3.4). |
| Kernel state across a structure change | **Reshape.** A script node may keep its instance across a settings change that alters its layout; the planner rebinds its ports and commits the new settings at the swap (§2.4). |
| How does the bundle reach the engine? | **Embedded at build time**, as bytecode, and loaded when an Engine is constructed. No boot call (§2.5). |
| Host MIDI out's system tag | **Kept**, with today's emission order, by a private 4-byte prefix on the kernel's one `midiOut` port that only `rp.host.midi-out` reads (§4.4). Transitional: B3b's nodes each have their own port. |
| The N8 cart mirror | **A node**, `rp.n8.cart-mirror`, wired by `buildGraph` from the first NES's kernel outputs (§3.5). TS's choice of cart system is the only one. |
| A throw in the kernel's JS | **Today's rule**: the block's output up to the throw stands, the next block runs again; counted and logged once (§4.5). |
| Host block sizes | **Any, from 1 frame.** New parity groups pin 1-frame blocks and FL-style ragged splits before B3a changes anything, and a bench measures per-call cost at tiny blocks (§6). |

## 2. The script-node runtime

### 2.1 One audio-side runtime per engine

`DspRuntime` becomes the **script host**: one bare QuickJS runtime per `Engine`, holding every script node
instance, run only by the thread that runs blocks (the audio thread, or the caller's thread while quiescent),
exactly as the kernel's context is today. Each Engine has its own: the plugin's instances, the standalone, the
SDL host, the CLI, the UI's `RenderHost` jobs and the Node addon never share one.

A script node is reached through an ordinary `rp_node`: a native **shim** per instance that the planner
creates, activates and destroys like any node. The JS instance behind it lives in the script host:

- **created** there when the plan holding the shim is adopted (on the block thread, before that plan's first
  `process`), from the settings the shim was created with;
- **released** there when the plan that last held it is reclaimed: the shim's `destroy` (control thread) queues
  the release, and the script host performs it at the start of its next block, or inline while quiescent.

So JS runs on one thread at a time, as today. Creating an instance allocates in QuickJS on the block thread,
which is no worse than today's `SetSystems` command, applied on the audio thread.

### 2.2 One control-side context for validation

The ABI's `layout_for` and `prepare_settings` run on the control thread, and a TS node's ports may be a function
of its settings (the kernel's are per system). The planner must keep validating a whole document before it
creates anything ([B1 §6](modular-graph-phase-b1-design.md)). So a second, small bare context, created with
the Engine and loaded with the same bundle, answers those two calls for every script type. It never holds an
instance and never runs `process`; the two contexts share no JS values, only the bundle.

### 2.3 Marshalling

- **Inputs** reach JS as persistent arrays the shim reuses: an events or bytes port as an array of
  `{ frame, data, flags }` objects refilled each block (grown, never shrunk, so a steady state allocates
  nothing), a control port as `{ start, changes }`.
- **Outputs** go out through push functions bound once per instance, which write straight into the plan's
  `rp_queue` for that port, as today's sink thunks append to vectors. No intermediate JS array per block.
- **The block's transport** (frames, sample rate, tempo, ppq at block start, playing) comes from `rp_process`.
  The Engine builds it from the same fields it builds today's `DspBlockInfo` from, so the kernel sees the same
  numbers.

### 2.4 Reshape

Today an instance's layout is fixed for its life: a settings change that would alter it is a rebuild
(`rp_node_type::layout_for`'s contract). The kernel cannot work that way. Its ports are per system, and its
state must survive a system being added: today `setSystems` keeps every surviving id's tick counter and role
scratch, and every project stage's state (the Launchpad session), and prunes only removed ids. A rebuild
cannot carry that state by copying it either, since the new instance is created on the control thread while
the old one is still advancing on the audio thread.

So, while ABI v1 is unshipped, `prepare_settings` gains a fourth result, **`RP_PREPARE_RESHAPE`**: keep this
instance, with the layout `layout_for` gives for the new settings.

- The planner keeps the instance (its node id and type are unchanged), resolves the new layout, and binds the
  new plan's ports to it **by name**.
- Feedback wires keep their last block, as they already do across a plan swap when a wire's key (its two
  endpoints) and shape are unchanged ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`).
- The prepared settings commit **at the swap, on the block thread**, through the existing settings-commit path,
  in push order with every other command, so the first block of the new plan runs with them. A script node
  receives them in a JS `onSettings(settings)` hook.
- A node that cannot reshape returns `RP_PREPARE_REBUILD` as today; native nodes are unaffected.

### 2.5 The bundle, embedded

The script-node bundle (today's kernel bundle, `dspKernelBundle.ts`, extended with the node definitions) is
compiled to QuickJS bytecode at build time, with the in-tree QuickJS, and embedded in the native binaries as
the UI bundles are (a derived C array under `build/native/`, never committed). Each Engine loads it into both
contexts at construction and registers the script node types it defines. Every host therefore has the same
node types with no boot call, and the boot-time `dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__))` in
`pluginControlPlane.ts` and `bootSession.ts` goes. If the embedded bundle fails to load, every document naming
a script type is refused (`unknown-type`) and logged, as the Engine constructor's empty graph already is.

The bundle still carries zod (about 568 KB of today's 638 KB) through `dspRoles.ts`; B3b removes it. Two
contexts per Engine load it in the meantime, which costs memory, not per-block time.

### 2.6 Queue capacity

A plan queue holds 256 messages and 4 KB today (`kQueueItems` / `kQueueBytes`), and drops what does not fit.
Today's staging vectors are unbounded, so a large SysEx or a burst of tracker sync bytes that passes today
would be dropped on a wire. So a port's descriptor may declare its capacity (items and bytes), with today's
numbers as the default; the host I/O nodes and the kernel's ports declare large ones (sized in the plan from
the largest message the tests send, with margin). Drops are counted per queue and exposed to tests; parity and
the suites require zero.

## 3. Ports and host I/O

### 3.1 System node inputs

Every system node gains four inputs, mapped onto core calls `SystemBase` already has (so no `dynamic_cast`):

| Port | Kind | Core call |
|---|---|---|
| `serialIn` | bytes | `pushSerialIn(frame, byte)`, one call per byte |
| `midi` | events | `onMidi`, with today's rule: a message longer than a `MidiEvent`'s inline data goes down `pushCoreBytes` |
| `bytes` | bytes | `pushCoreBytes(frame, data, size, flush)`, the flush flag riding `RP_MESSAGE_FLUSH` |
| `buttons` | events | `pressButton(button, down)`, a 2-byte payload `[button, down]` |

A node applies them before it runs the core (at the top of `process`, or of `prepare` for a linked Game Boy,
whose coupled group steps through `rp.step/1`), in today's fan-out order (`serialIn`, then `midi`, then `bytes`,
then `buttons`), each in queue order. The order matters on the NES, where MIDI and bytes land in the same FIFO.

### 3.2 The Game Boy's `serialOut`

The Game Boy node gains a `serialOut` output (bytes): the bytes its core captured on the serial port this block
while MI.OUT capture is armed (the build-time `serialOutCapture` flag, or `setSerialOutCapture`). It replaces the
engine's per-block gather into `pendingSerialOut_`. Today the gather drops the bytes' frames, and the kernel
reads them without any; the port carries each byte at frame 0, so the kernel reads exactly what it reads today.

### 3.3 Host I/O nodes

| Node | Ports | Replaces |
|---|---|---|
| `rp.host.midi-in` | out `midi` (events) | `pendingMidi_` handed to the kernel |
| `rp.host.midi-out` | in `midi` (events) | `DspRuntime::midiOut_`; feeds `Engine::midiOut()` |
| `rp.host.controller-in` | out `midi` (events), out `connected` (control) | `pendingControllerMidi_` and the per-block `controllerConnected` flag |
| `rp.host.controller-out` | in `midi` (events) | `DspRuntime::controllerOut_`; feeds `Engine::controllerOut()` |

The engine keeps staging what the hosts hand it (`stageMidi`, `stageControllerMidi`, the controller-connected
state) through the command ring, as today; the input nodes read the staged data at the top of their block and
clear it. The output nodes append to the buffers the plugin, the SDL host and `drainMidiOut` read, cleared at
the top of each block as today. `controllerConnected` becomes a control value whose block-start value is
today's per-block flag.

### 3.4 Frame order

ABI queues are frame-ordered: Phase A's `pushMessage` clamps a message whose frame is earlier than the last one
up to it. Today's host MIDI is not always in frame order. The plugin stages the DAW's MIDI events first
([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), `stageMidi(e.frame, ...)`) and then each moved
automation slot's CC at frame 0, so a block can stage `[100, 300, 0]`. Today the NES FIFO and the SMS sync role
stable-sort by frame, so that CC lands at frame 0. SameBoy's serial queue releases only the byte at its front,
so on a Game Boy the CC goes in after the frame-300 byte.

`rp.host.midi-in` **stable-sorts** the staged MIDI by frame (in place, without allocating) before it pushes:

- the NES and SMS get exactly what they get today (a stable sort of what they stable-sort);
- a Game Boy now receives a frame-0 automation CC at block start, before the block's notes. That is a
  **deliberate behaviour change**: frame 0 is when the automation happens, and a host that splits blocks at
  automation points (FL Studio) already puts it there. Parity cannot see it (`renderBlocks` refuses unsorted
  MIDI), so it gets its own tests (§7).

The kernel's own output needs no sort. Its one out-of-order case, LSDj Arduinoboy emitting row bytes at their
note's frame and then START / STOP at frame 0, goes only to SameBoy's front-release serial queue, where a frame
earlier than the queue's last one changes nothing. The kernel node pushes each sink call in emission order, and
the plan delivers it to the core in that order.

### 3.5 The N8 cart mirror

The merge ported the cart mirror as native logic in `runKernelStage`: for the first NES in slot order, its
core-MIDI and core-bytes go to the host's core-byte sink (`N8Link`) before the core's dispatch. B3a makes it
`rp.n8.cart-mirror`, a native node with inputs `midi` and `bytes`, which `buildGraph` wires from the **first
NES's** kernel `sys-<id>.midi` and `sys-<id>.bytes` outputs (an output fans out to any number of inputs for free).
It calls the sink for every MIDI message, then every byte message (today's order, flush flag included), and
does nothing while no sink is set. Its settings name the system it mirrors, and the hosts' "is a cart mirrored"
test (no raw DAW MIDI to the cart while one is) and the `stageSystemMidi` mirror both read that from the
published plan. `Engine::cartSystemId`'s walk goes.

The merge left two small items that belong here: comments in Engine.hpp, PluginDSP.cpp, sdl/main.cpp and
EngineCartMirror.test.cpp that cite a "BlipToaster host LFO" consuming CC76-79, which does not exist; and a test
that a message staged at the cart judges the cart against the post-swap plan.

## 4. The kernel node

### 4.1 Settings and ports

`rp.dsp.kernel` becomes a TS node, defined with `defineNode` in the script-node bundle, around today's
`DspKernel`. Its settings are today's `KernelStructure` plus one fact per system:

```ts
{ project: RoleInstance[], systems: { id: number, pipeline: RoleInstance[], serialOut: boolean }[] }
```

`serialOut` is true for a Game Boy, the one system that has the port. Its ports, from those settings:

| Direction | Ports |
|---|---|
| in | `midiIn` (events), `controllerIn` (events), `controllerConnected` (control), `sys-<id>.serialOut` (bytes) for each system with `serialOut` |
| out | `sys-<id>.serialIn`, `sys-<id>.midi`, `sys-<id>.bytes`, `sys-<id>.buttons` for each system; `midiOut` (events); `controllerOut` (events) |

Ports are named by system id, never position, so a reshape (§2.4) binds a surviving system's ports to the
same wires.

### 4.2 Per block

`process` builds the same `BlockInput` today's `DspRuntime::processBlock` builds: the block info from the
transport and `controllerConnected`, `midiIn` from its port, `controllerIn` from its port, `serialOut` by
concatenating each system's `sys-<id>.serialOut` input in slot order (today's array), and the still-unfed
`buttons` / `keys` as today's empty arrays. It calls `DspKernel.processBlock` with a `SinkTarget` whose methods
push into the output ports: `pushSerialIn` one byte per message on `sys-<id>.serialIn`, `emitCoreMidi` on
`sys-<id>.midi`, `pushCoreBytes` on `sys-<id>.bytes` (flush as the flag), `pressButton` on `sys-<id>.buttons`
at frame 0, `emitMidiOut` on `midiOut` (§4.4), `emitControllerOut` on `controllerOut` at frame 0. A sink call for
an id with no port (an unknown id) is dropped, as today's fan-out skips an id it cannot find.

### 4.3 Structure changes

`onSettings(settings)` calls `setSystems(settings)`, so a structure change keeps today's rules exactly:
surviving ids keep their tick counter and role scratch, removed ids are pruned, project stage state survives.
Every structure change is a reshape, so the kernel's instance is never rebuilt.

### 4.4 Host MIDI out

`drainMidiOut` reports which system emitted each message, and its emission order matters: a project stage (the
Launchpad controller addressing a real Game Boy) emits before every system stage. Per-system output ports would
lose that order. So `midiOut` is one port in emission order, and each message's payload is prefixed with the
emitting system's id (4 bytes, little-endian). Only `rp.host.midi-out` reads that port; it strips the prefix
into `Engine::midiOut()`'s `system` field. The prefix is private to B3a: in B3b each protocol node has its own
`midiOut` port and `rp.host.midi-out` takes the id from its wiring.

### 4.5 A throw

Today a JS exception in `processBlock` is dropped: the sink calls made before it stand, and the next block calls
`processBlock` again. The kernel node keeps that rule: it catches per block, keeps what was pushed, counts the
throw and logs the first one, and runs the next block. It does not return `RP_FAULT`, whose latch would turn a
transient error into MIDI that stays dead until a rebuild. B3b's protocol nodes may opt into the latch.

## 5. The TS side

### 5.1 `buildGraph`

The model gains each system's `pipeline` and the project's stages; `kernelProjection` still computes them from
the store (B3b moves that logic into `buildGraph` itself). The document gains:

- `rp.host.midi-in`, `rp.host.midi-out`, `rp.host.controller-in`, `rp.host.controller-out`;
- the kernel node's settings (§4.1) and its wires: `midi-in.midi -> kernel.midiIn`, the controller pair, each
  `kernel.sys-<id>.*` output to `sys-<id>`'s input of the same name, each Game Boy's
  `sys-<id>.serialOut -> kernel.sys-<id>.serialOut` as a **feedback** wire (MI.OUT's one block of latency),
  `kernel.midiOut -> midi-out.midi`, `kernel.controllerOut -> controller-out.midi`;
- `rp.n8.cart-mirror` wired from the first NES in slot order, when there is one.

The kernel now runs before the systems because of its wires, not because it is listed first, so B1's hidden
dependency and the test that pinned it go. The 840 goldens change on purpose and are re-recorded from
`buildGraph`; the native goldens test keeps planning and running every one of them.

### 5.2 The stores

`ProjectStore.publishGraph` passes the kernel structure with the systems. A feature-role edit (an LSDj mode
change) and a MIDI-routing change now publish, like any structural edit, and `syncDspFromStore`'s push and its
`onSystemsChange` hook go. EngineGraph's dedupe drops an edit that changes nothing.

### 5.3 `DirectGraph`

`DirectGraph` gains `setPipeline(id, roles)` and `setProjectStages(stages)`, each a publish. Today about 50
test-native files, `render.ts` (the CLI `render` and the UI's `RenderHost`) and two CLI sessions push the
structure through `dsp.setSystems`; they move onto these calls, mechanically, as B2 moved the build calls.

### 5.4 What goes

The `dspSetSystems` and `dspLoadKernel` RPCs and the kernel half of `dspRuntime.ts`; `compileScript` too unless a
caller other than the kernel's boot remains; native's `KernelNode`, `runKernelStage`'s fan-out, the staging
vectors' kernel path, `pendingSerialOut_` and its gather, `Engine::cartSystemId`. The script host keeps the
runtime's profiling calls (allocation stats, GC, tracing).

## 6. Block size

The engine takes any block from 1 to 8192 frames, and a frame stamp is an offset inside its block, so a host
that splits a block (FL Studio splits at automation and tempo points, down to 1 frame) is giving it two blocks.
Three properties are worth stating:

- **Frame 0 is the block's start.** Under splitting hosts it is exactly when automation happens, which is what
  the sort at the source (§3.4) relies on.
- **MI.OUT's latency is one block, not a fixed time.** In samples it is the previous block's length, so it
  varies with a splitting host. That is true today, and the feedback wire keeps it bit for bit.
- **A script node costs one JS call per block.** The kernel node costs what today's kernel stage costs (one
  call), plus marshalling. B3b's protocol nodes multiply calls by node count, so a 1-frame block means several
  JS calls per sample. The main design's "batch consecutive script nodes into one call, not built until
  measured" ([§4.9](modular-graph-design.md)) becomes a measurement B3a must make (§7.4).

Parity already renders the same scenes in 64, 199, 512, 1024 and 8192-frame blocks, a mixed cycle, and an
edge segment with a 7-frame block. It has never run a 1-frame block or a ragged FL-style split.

## 7. Testing

### 7.1 Parity, extended first

Before any B3a code exists, new parity groups are recorded on today's merged engine:

- **1-frame blocks** and **FL-style ragged splits** (a repeating list such as `[33, 1, 222, 1, 7, 4096, 2]`),
  with MIDI and automation CCs landing on block edges, on the first and last frame of a 1-frame block;
- the scenes whose timing is protocol-sensitive: mGB and BlipToaster NES (fixture-free, so CI runs them),
  LSDj MIDI sync, Arduinoboy and MI.OUT, and risa host sync (fixture-only).

Segments are kept short, since 1-frame blocks multiply the block count. B3a is accepted on strict parity: every
group, with the fixtures present and `RP_FAIL_ON_SKIP=1`.

### 7.2 The deliberate change

A Catch2 case holds `rp.host.midi-in`'s sort (stable, in place, no allocation). A test-native case stages
plugin-style unsorted MIDI (notes, then a frame-0 CC) through `stageMidiIn` and checks where the CC lands on a
Game Boy (before the notes now) and on a NES (at frame 0, as before).

### 7.3 Reshape and the script host

- Native tests with a toy reshaping node: the instance is kept, its ports are rebound by name, a feedback wire
  keeps its last block, the settings commit at the swap, a node that returns `RP_PREPARE_REBUILD` is rebuilt.
- Toy TS nodes: input and output marshalling, the push thunks, layouts from the control-side context, creation
  at adoption and release at reclaim on the block thread, a throw's count. ThreadSanitizer runs over both
  contexts and a running audio thread.
- The kernel node: state survives a system add and a feature-role edit (a playing LSDj keeps its clock), a
  removed id is pruned, host MIDI out keeps its system tags and order with a controller emitting.

### 7.4 The bench

Per-call cost of a script node at 1-frame and 64-frame blocks: the kernel node against today's kernel stage, and
a chain of trivial TS nodes. Its numbers decide B3b's node granularity and whether the batching of §6 is needed.

### 7.5 Acceptance

Strict parity (the existing 24 files and the new groups); `pnpm test`, `test:native`, `test:plugin`,
`test:ui`, `test:node`, `typecheck`, `check:docs`; the thread and address sanitizers; the Reaper checks
(including the LSDj sync and MI.OUT renders and the editor checks); `sdl:smoke` and `sdl:pipewire`; the
BlipToaster and NESVJ consumer suites against the built CLI.

## 8. How it lands

As B2 did: a working prototype built and verified on a scratch branch first, a plan written from it, then
executed task by task with review. In order:

1. The new parity groups, recorded on today's engine.
2. Reshape: the ABI result, the planner's keep-and-rebind, the settings commit.
3. Port capacities in descriptors.
4. System node inputs, the Game Boy's `serialOut`, the host I/O nodes and `rp.n8.cart-mirror` (native, beside
   today's path).
5. The script host: the two contexts, the shim, marshalling, the embedded bundle.
6. The kernel TS node, `buildGraph`'s wiring, the stores publishing the structure, `DirectGraph`'s calls and
   their callers moved.
7. Delete the fan-out, the staging vectors' kernel path, the RPCs and the native kernel node.
8. The bench and the docs.
9. The acceptance run.

## 9. Left for B3b

- One protocol node per role (`mgb.midi`, `lsdj.sync`, `risa.sync`, `sms.sync`, `n8.midi-to-fifo`, MIDI
  routing, the Launchpad controller), each with its own ports; the kernel node and `DspKernel` deleted.
- `kernelProjection`'s special cases (the synthesized `midi-routing` and Launchpad stages, the forced
  `lsdj-sync` mode for MI.MAP) moved into `buildGraph`.
- A per-product script-node bundle with no zod in it.
- How several stages' outputs merge into one system input, and whether consecutive script nodes are batched
  into one call (decided by §7.4's numbers).
