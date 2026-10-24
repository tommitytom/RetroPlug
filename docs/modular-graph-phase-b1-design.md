# Design: Phase B1, native runs on the executor

**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** built
([modular-graph-phase-b1-plan.md](modular-graph-phase-b1-plan.md)); B2 and B3 not yet built.

Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph. B0
([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)) recorded today's engine bit for
bit; B1 makes `Engine::processBlock` run `GraphHost` with no TS change at all, so any parity break it
causes can only be native. It settles the two decisions §8 left open and deletes the routers,
`BlockRunner`, `LinkGroup`, `AudioRouting` and `Project`.

---

## 0. Where B1 sits

B0 §0 has the four-step table. B1 as designed here differs from §8 of the main design in one place:
**the kernel stays sealed.** §8 lists the host I/O nodes with the engine move, but `rp.host.midi-in` /
`rp.host.midi-out`, the system nodes' input ports (`serialIn`, `fifo`, `buttons`, `port`) and the Game
Boy's `serialOut` port all move to B3, with the protocol nodes they would connect to (§2.3 says why). B1
moves the audio side: emulators, the link cable, mixing and the host outputs.

## 1. Decisions taken

| Question | Decision |
|---|---|
| One command ring or two (§8 open decision)? | **One.** `GraphHost`'s ring carries plan swaps, params and today's engine commands, in push order (§5). |
| Validate before creating (§8 open decision)? | **A type-level `layout_for(settings)`** on `rp_node_type`, replacing the instance's `layout()` (§6). |
| How does B1 land? | **Beside today's engine**, chosen once per process by `RETROPLUG_GRAPH_ENGINE=1`; the last commit flips the default and deletes the old path and the switch (§9). |
| How does the kernel reach the host and the cores? | **As today.** The kernel node runs today's kernel stage line for line: it reads the engine's staged MIDI and fans its output into the cores by id. MIDI nodes and system input ports come in B3 (§2.3). |
| How is the NES channel tap armed? | **By today's code**, run as a `SyncTap` trailer that rides in each structural plan's swap command; the wiring only sets the lane count (§3.2). §6.5 of the main design maps it to `ports_changed`, which cannot copy today's re-arming, so that move is left for later. |
| Does `setGraph` wait for the previous plan? | **Only while a published plan still carries a settings commit**, which B1 never produces, so in B1 it never blocks (§5.2). |

## 2. The block

### 2.1 Per block

`Engine::processBlock(frames, outputs, n)` in graph mode:

1. The host has already called `invoker.drainInto(engine)` where it does today (§5.3): plan swaps, params
   and engine commands applied in push order.
2. `syncTrace_.beginBlock(...)`.
3. `GraphHost::run(frames, transport, outputs, n)`: silence the host outputs, then the plan:
   1. `rp.dsp.kernel` (§3.3);
   2. the system nodes: an unlinked system as one `process()` call, linked Game Boys as one coupled group;
   3. the mixing nodes (`rp.core.split`, `rp.core.add`);
   4. `rp.host.audio-out`.
4. Today's tail, unchanged: the MI.OUT collection into `pendingSerialOut_` (only while a kernel is loaded,
   systems in slot order, frames dropped), `registry_.publishSystem` for each system in slot order,
   `syncTrace_.endBlock()`, and the ppq advance `if (transport_) ppq_ += (bpm_ / 60.0) * (double(frames) /
   sampleRate_)`.

The MI.OUT collection and the registry read the system table (§4.5) rather than `Project`; the collection
reads its Game Boy pointers rather than `dynamic_cast` each system every block.

### 2.2 Who owns what

- **Engine** keeps `DspRuntime`, the staging vectors (`pendingMidi_`, `pendingControllerMidi_`,
  `pendingSerialOut_`), the snapshot registry, the transport fields, `controllerConnected_`,
  `coreByteSink_` and `syncTrace_`. It owns the `NodeRegistry`, the `ResourceTable` and the `GraphHost`.
- **Each system node owns its core** (`std::unique_ptr<SystemBase>`). A core is destroyed when the last
  plan holding its node is reclaimed, on the control thread (§4.6).
- **The translator** (§4) is control-side: the ordered system list (`Project`'s job today), each Game Boy's
  link group, the routing mode and the host shape. It turns every structural call into a document.

### 2.3 Why the kernel stays sealed

ABI queues are frame-ordered: `pushMessage` clamps a message whose frame is earlier than the last one up to
it. Today's kernel I/O is not always in frame order. The plugin stages the DAW's MIDI first and its CC
automation at frame 0 after it, so a block can stage `[100, 300, 0]`. The NES FIFO and the SMS sync role
stable-sort by frame, so today that CC lands at frame 0; through a clamping queue it would land at 300.
(SameBoy's serial queue only releases the byte at its front, so clamping changes nothing for it.) Within
the kernel, Arduinoboy mode emits row bytes at their note's frame, then START/STOP and clocks at frame 0.
`renderBlocks` refuses unsorted MIDI (B0 §2.2), so parity could not see any of this.

In B1 a MIDI-in node would have only one thing to connect to: the kernel itself, a sealed box. The
protocol nodes that make the wiring meaningful (`mgb.midi`, `n8.midi-to-fifo`, `lsdj.sync`) arrive in B3,
and so does the decision on how several stages' outputs merge into one input. B3 settles the queue-order
question there, where it is real.

## 3. The nodes

### 3.1 `SystemNode<T>`

One adapter template over today's `SystemBase` subclasses, giving four types: `rp.sameboy.gb`
(`SameBoySystem`), `rp.mesen.nes`, `rp.mesen.gba` and `rp.mesen.sms` (the three `Mesen*System`s).

- **Unlinked:** `process()` builds today's `AudioBlockInfo` from the block (frames, the transport's rate,
  tempo, ppq and playing flag; backends read only `frames`), then runs `prepareForBlock`,
  `while (stepIfBelowTarget(frames)) {}` and `finishBlock`: `runUnit` for one member.
- **Linked Game Boys:** `rp.step/1` maps one to one: `prepare` to `prepareForBlock`, `step(target)` to
  `stepIfBelowTarget(target)` (true is `RP_STEP_BELOW`, false `RP_STEP_DONE`), `finish` to `finishBlock`.
  The executor's group loop is `runUnit`'s: prepare every member in order, then passes of one step per
  member in order until a pass reports nothing below, then finish each in order. Calling a member that is
  already done is a no-op, as `stepIfBelowTarget` returns false at once. The executor adds Phase A's bound
  of 4096 passes per frame; today's loop is unbounded, so the two differ only where today's would spin
  forever.
- **`rp.link/1`:** `set_peers` sets `linkPeers_` to the other members' cores in member order, which is
  what `rebuildLinkGroups` does today. `linkPeers_` already reserves 8, so it no longer allocates on the
  audio thread. A system leaving a group gets an empty list (the planner sends one to every node of a
  one-member group whose peers changed), so `isLinked()` goes false at the same block boundary as today.
- **Outputs:** the node zeroes the output buffers it hands to `finishBlock`, which adds into them, so every
  lane is `0 + v·g` exactly as in today's zeroed bus. The gain smoother stays inside the core: one value
  per frame shared by every lane.
- **Lane count** (`finishBlock`'s `laneCount`, worked out at finish) follows which outputs are connected
  (`ports_changed`):
  - GB: 8 when any of `ch1`-`ch4` is connected, otherwise 2;
  - NES: 2 when `mix` is connected; otherwise, when tap ports are connected, twice the stream count of the
    armed mode (6 for pins, 8 for pins plus reference, 10 for channels), with stream k's buffer as lane
    `2k` and lane `2k + 1` pointing at the same buffer, as `ChannelSplitRouter` gives a mono stream
    today (the NES writes only lane `2k`);
  - GBA and SMS: 2.
- **Ports in B1:**

  | Node | Outputs | Other |
  |---|---|---|
  | GB | `mix`, `ch1`-`ch4` (stereo) | `link` |
  | NES | `mix` (stereo); `pins.pulse`, `pins.tnd`, `pins.expansion`, `pins.mixref` (mono); `channels.sq1`, `.sq2`, `.tri`, `.noise`, `.dmc` (mono); exclusive `["pins.*", "channels.*"]` | |
  | GBA, SMS | `mix` (stereo) | |

- **Settings:** `{ "core": <token> }` only (§4.6). The core's own build config (ROM, model, region, roles)
  stays where it is today: built on the control thread by the backend factory before the node exists.
- **Config fields** (gain, model, highpass, fast boot, serial-out capture, the display knobs, the NES knobs)
  stay today's `SetConfigField` command, applied at drain to the core the running plan's table names.
  `applyConfigField` keeps its per-kind switch until B2 deletes `ConfigField` with the role-config commands;
  the work that cast every block (the MI.OUT collection, the tap) reads the table's typed pointers instead.
  **LinkGroup** is
  the exception: the translator intercepts it and rewires (§4.1); the value still reaches the core's
  `config_` too, so anything reading it there is unchanged. A **Model** change still restarts the emulator
  in place on the audio thread; `restartEmulator` leaves `linkPeers_` alone, so no rewire is needed.
- **Sample rate:** a node's first `activate` takes its core (§4.6); later ones do nothing to it (the factory
  activated it at build, at the engine's rate). `Engine::setSampleRate` calls each core's `onSampleRateChanged` in slot order, as
  `Project::onSampleRateChanged` does today, then `GraphHost::reactivate` for the graph's buffers, so the
  cores see exactly today's calls.

### 3.2 The NES tap

Arming stays today's code. `syncSplitPlan`'s arming half (restore whatever `armedTap_` armed, then arm the
mode the routing wants, recording what it replaced) moves into a `SyncTap` that rides in the plan's
own swap command: `GraphHost::setGraph(document, hostData, trailer)` carries it as a trailer in the same
ring slot, and the drain dispatches it right after the plan is adopted, in the same pop, against the new
plan's system table. No block can run between the two, and a full ring refuses both halves together.
`Translator::Publish` carries a `rearmsTap` flag: true for add, replace, remove and set-routing (a same-mode
set included), false for the link-group field, a host-shape change and the constructor's empty plan. A remove
or replace of an id that is already gone publishes nothing, so it pushes a standalone `SyncTap`, as
`syncSplitPlan` ran for those too. `discardPending` still applies a trailer with its plan, since the plan is
adopted; only the GraphHost's destructor hands queued trailers back as discarded.

The armed-tap record (`armedTap_`) holds the core pointer as well as the id, so a NES replaced under its
SAME id is treated as gone: the old core's saved export mode is not restored onto the fresh core, which is
what the engine before B1 did (its `replaceSystem` cleared the record explicitly for that case). A target
that has gone is not found and its record is dropped, as today.

`ports_changed` would have been the cast-free form (§6.5 of the main design), but it cannot copy today:
`syncSplitPlan` restores and re-arms on every structural edit even when the wanted mode is unchanged
(set-routing to the same split, PinSplit to StereoPinSplit), and each call resets the capture streams and
their resamplers (`NesSoundMixer::SetChannelCapture`), while `ports_changed` fires only when the wiring
changes. The record (`armedTap_`) therefore stays on the Engine, and the table carries each NES's
`MesenNesSystem*` so the command needs no cast.

The wiring only sets the lane count (§3.1). Which mode a connected tap port means:

| Connected | Lanes | The mode it needs |
|---|---|---|
| `mix` | 2 | any (the tap may be armed: the narrow-host defect) |
| `pins.pulse` / `.tnd` / `.expansion` | 6 | 1 (pins) |
| those three and `pins.mixref` | 8 | 2 (pins plus reference) |
| `channels.*` | 10 | 3 (five mono channels) |

The translator only wires tap ports when the mode it computes for the core (the routing's arm, else the
build mode) is the one the table needs, so the two always agree by the time the NES runs.

**The narrow-host defect stays as it is** (B0 §2.5): a split that does not fit the host falls back to
Stereo wiring, the `SyncTap` arms the tap regardless, as today, and the NES outputs its mix with the
capture armed, so the mix drifts. Fixing the defect is its own commit with its re-record.
`setChannelExportMode` still allocates on the audio thread (`blip_new`), as today.

### 3.3 `rp.dsp.kernel`

A B1-only node with no ports, first in every document (§4.2). Its `process()` is today's kernel stage
(`Engine::runKernelStage`), reached through a host interface onto the Engine:

- skip everything while no kernel is loaded (`dspActive_`), leaving the staging vectors as they are (today
  they keep growing until a kernel loads; copied, not fixed);
- `dsp_.processBlock(pendingMidi_, …, pendingSerialOut_, pendingControllerMidi_, info)`, then clear the
  three staging vectors;
- fan the kernel's sinks out in today's order, all of `serialIn`, then `coreMidi`, then `coreBytes`, then
  buttons, each in emission order, finding each target by id in the system table (a linear scan in slot
  order, as `project_.findSystem` does; an unknown id is skipped). `coreByteSink_` and the sync trace fire
  where they do today: before the core's dispatch, even for an unknown id.

`midiOut_` and `controllerOut_` stay in `DspRuntime` for the hosts to drain, as today. The kernel's
position is a hidden dependency (it writes into cores the planner cannot see it touch), so a translator
test pins that it is the plan's first step. B3 replaces the node.

### 3.4 Host and core nodes

- **`rp.host.audio-out`** gets the host's buffers through `rp_host.get_interface("rp.host.audio-buffers/1")`
  rather than casting `ctx` to `GraphHost`, and the planner refuses a document with a second one (Phase A
  lets the last one win). Both are items §8 lists as owed.

## 4. The translator

Control-side. It receives the structural calls the RPC layer makes today (add, replace, remove, set
routing, the LinkGroup field, the host shape), keeps the model, and calls `setGraph` with the document and
its system table.

### 4.1 The model

It mirrors `Project` exactly:

- **add** appends a system;
- **replace** keeps the slot and takes the new id TS hands it (`constructSystem` with `replaceId` builds the
  new core under a NEW id, so a replace is a new node at the old position);
- **remove** erases the system, so later slots shift down (which moves systems between TwoPerInstance /
  OnePerInstance outputs, as today).

Link groups are formed as `rebuildLinkGroups` forms them: Game Boys only, bucketed by nonzero link-group id
in slot order, groups of fewer than two dissolved.

### 4.2 Document order

1. `rp.dsp.kernel`;
2. the unlinked systems, by slot;
3. the link groups, in order of first appearance, each group's members by slot (one `link` wire chains
   each member to the next);
4. the mixing nodes;
5. `rp.host.audio-out`.

Kahn's algorithm breaks ties by document order, and every system is ready at the start (the kernel's
writes are not wires), so the plan runs the kernel, then the systems in this order, then the mixing:
today's `runBlock` order, where a linked system runs after every unlinked one whatever its slot. A
translator test pins the order. Node ids are `kernel`, `sys-<id>` and names derived from them, never from
position, so a reused system keeps its instance across edits.

### 4.3 Routing as wiring

For each host channel the translator lists the contributions today's router adds into it, in today's
order: systems in §4.2's order, and within one system L before R (`finishBlock` adds L then R per frame,
so `OnePerInstance`'s aliased lane is `(x + L·g) + R·g`).

- A channel with **one** contribution is wired straight to `audio-out`.
- A channel with **several** goes through one mono `rp.core.add` whose inputs are in that order. `add` sums
  from zero in input order, which is today's accumulation exactly. Past `add`'s 16 inputs the adds chain
  (the first add's output as the next one's first input), which continues the same left-to-right sum.
- A system's stereo output reaches mono lanes through `rp.core.split`; `audio-out` has one mono bus per
  host channel.

The mapping per mode is `BlockRunner.hpp`'s, evaluated when the document is built:

| Mode | System at slot i |
|---|---|
| Stereo, and every split that falls back | L to channel 0, R to channel 1 |
| TwoPerInstance | `p = (2i) mod N`: L to `p`, R to `(p + 1) mod N` |
| OnePerInstance | L and R both to `i mod N` |
| ChannelSplit / PinSplit / StereoPinSplit, valid | stream k to lane `(stride·k) mod N` and, for a stereo stream, the next; StereoPinSplit's mirror is the mono stream wired to both lanes of its pair |

A split is valid exactly when `syncSplitPlan` makes it valid today: one system, the pin splits only on a
NES, and `stride × streams ≤ N`, with today's lane plans (GB stems: 4 stereo streams, stride 2; NES
channels: 5 mono, stride 1; NES pins: 3 mono, stride 1, or stride 2 for StereoPinSplit; GBA and SMS
ChannelSplit: one stereo `Mix` stream). An invalid split falls back to Stereo wiring.

**For B2:** the OnePerInstance graph of the main design's §7.6 (`downmix`, then `join`) is not bit-exact
once systems outnumber outputs: it adds `(L0 + R0) + (L4 + R4)` where today adds `((L0 + R0) + L4) + R4`,
and float addition is not associative. B2's `buildGraph` must keep the flat form.

### 4.4 Host shapes

Today the router is chosen per call; in B1 the output shape is part of the document, set on the control
thread before the blocks that use it:

| Shape | Who | Channels |
|---|---|---|
| `Multi(n)` | the plugin (8), SDL (its device count), `AudioDriverRpcService` and `renderAudio` (2), `renderBlocks` (its `outputs`) | the routing of §4.3 |
| `PerSystem` | `renderAudioPerSystem` | the system at slot i on channels `2i`, `2i + 1`, as `PerSystemRouter`: its mix, never its stems |
| `PerChannel` | `renderAudioPerChannel` (one system) | stream s on channels `2s`, `2s + 1`, as `PerChannelRouter`; a mono stream fills only `2s`. The streams are the ones `channelLayout()` reports today: a NES's armed or build mode, a GB's four stems |

A shape change is a rewire: every instance is reused, so no core notices. The render RPCs set their shape
before rendering and refuse while an audio thread owns the engine (the guard today's `renderAudio*` lacks,
fixed before B1 starts, §9). `GraphHost`'s `maxFrames` is the larger of 8192 (the largest block the matrix
holds, B0 §4) and the host's own maximum, re-activated quiescently if a host reports a larger one.

### 4.5 The system table

A slot-ordered list of `{ id, SystemBase*, SameBoySystem*, MesenNesSystem* }` (each of the last two null
for other kinds), built with each document and handed to `setGraph` with it. `GraphHost` stores it with
the plan (opaque host data, freed with the plan on reclaim) and exposes the current plan's table on the
audio thread. The kernel's fan-out, the command handler (§5.1, `SyncTap` included), the MI.OUT collection
and the registry read it, so a lookup always matches the plan that is running, including between two swaps
adopted in one drain.

### 4.6 Core handoff and lifetime

- The RPC layer builds and activates a core as today (`factory.build`, control thread) and claims its
  snapshot slot as today, then hands it to the translator.
- The translator parks it under a fresh token and puts `{ "core": <token> }` in the node's settings;
  the node takes the core at its first `activate`, through a host interface (`rp.host.cores/1`). A token
  never repeats, so even a
  reused system id with a new core plans as a new instance.
- The node owns the core. Its destroy, which runs at reclaim on the control thread once no running plan
  holds it, releases the snapshot slot and then deletes the core: today's `reclaimReleased` order.

## 5. Commands

### 5.1 One ring

`GraphHost`'s `Command` gains a third kind, `Host`, carrying today's `DspCommand` unchanged (to
`GraphHost` an opaque fixed-size payload). `drain` hands each one to the Engine's handler together with the
host data of the plan current at that point, so a command pushed after a swap sees the new systems and one
pushed before sees the old.

| Today's command | In B1 |
|---|---|
| AddSystem, ReplaceSystem, RemoveSystem, SetAudioRouting | `setGraph` (the translator), with the `SyncTap` as the swap's trailer (§3.2) |
| SetConfigField (LinkGroup) | `setGraph`, plus the field to the core |
| SetConfigField (the rest), PressButton, WriteRam, StageSystemMidi | `Host`, applied to the core by id |
| StageMidi, StageControllerMidi, SetBpm, SetTransport, SetPpq, SetSystems, LoadKernel | `Host`, applied to the Engine |

`EngineInvoker`'s two rings go: a displaced core leaves with its plan rather than on a release ring of its
own. Payloads freed on the audio thread today (the `SetSystems` JSON, kernel bytecode, a RAM write's bytes)
are still freed there; that is today's behaviour, left for later. At teardown the queued commands are
discarded, not applied, as `freePending` does.

### 5.2 `setGraph` never blocks in B1

Phase A's `setGraph` waits until the audio thread has adopted the previous plan before planning, so that
no node is asked to prepare settings while its previous settings commit may still be running on the audio
thread. B1 narrows the wait to that hazard: `setGraph` waits only while a published plan still carries a
settings commit. B1's nodes never commit settings on a live node (a system's settings never change, since
a new core is a new instance; the tap is a command; the mixing nodes' settings fix their layouts, so any
change rebuilds them), so in B1 it never waits.

Several plans can therefore sit in the ring at once. The audio thread adopts them in order in one drain,
interleaved with the commands exactly as they were pushed, which is today's semantics: one first-in,
first-out ring with any number of structural commands per drain. The wait would have hurt: DPF leaves a
CLAP plugin active when the host stops processing it (Bitwig's plugin sleep does), so every edit after the
first would have stalled the control thread for the 2 s timeout and then been dropped.

Capacity: 256 commands, now shared by plans and commands, and a release ring of twice that (512). Because
`setGraph` reclaims before every publish, a drain that adopts every queued plan can release at most the
ring's usable depth plus one between two reclaims; a 64-slot ring leaked the overflow, cores included, when
a host stopped processing while edits continued. An overflow
is counted, where today's full ring drops silently.

### 5.3 Drain points and reclaim

- Hosts keep calling `invoker.drainInto(engine)` where they do today, which now drains the GraphHost's
  ring (`Engine::drainCommands`): the plugin after staging and before setting bpm and transport, SDL first,
  `renderBlocks` after staging. A host's bpm and transport set after the drain therefore still win over
  queued ones. `GraphHost` splits `processBlock` into `drain()` and `run()` (its `processBlock` keeps doing
  both for Phase A's tests).
- Clearing the invoker's ownership bit now hands the GraphHost back, which applies whatever is queued, so
  it is cleared only once the audio thread has stopped: the plugin's `deactivate` and SDL's stream stop
  already did so, and `AudioDriverRpcService`'s loop, which ran on that bit, gets a run flag of its own.
- With no audio thread, every push still flushes inline (`setGraph` adopts at once, a command applies at
  once). This is how the parity rig runs.
- `reclaim()` runs at every `setGraph` and at the hosts' existing stop and deactivate points. Today's
  release ring is drained only while quiescent, so a removed core is now freed sooner. A reclaim from an
  idle tick stays owed (§8 of the main design); nothing in B1 depends on it.

## 6. `layout_for`

- `rp_node_type` gains `layout_for(settings)`: the layout JSON a node of this type has with those settings.
  It replaces the per-instance `layout()`. A node's layout then depends on its settings alone, which both
  authoring layers already guarantee (`adjust_layout` is static in C++; TS computes it from settings). ABI
  v1 has never shipped, so `RP_ABI_VERSION` stays 1.
- The planner computes every layout from the document first and runs every check that needs one (ports,
  port kinds, one wire per input, channel counts, exclusive groups, one `audio-out`) before it creates
  anything. Phase A's late refusal, which built instances and tore them down, goes. A failing `create` or
  `activate` (a bad core token, a core that will not activate) still rolls back.
- The C++/TS conformance goldens are unchanged: a descriptor is the layout for default settings, which
  `layout_for` gives as well. A golden for a settings-dependent layout waits for the first TS node whose
  layout depends on its settings.

## 7. Why the output stays bit-identical

- **Same cores, same calls, same order.** Each core sees today's `prepareForBlock` / `stepIfBelowTarget` /
  `finishBlock` sequence, with the same `frames`, in today's system order (§4.2); a linked group steps in
  today's member order; the kernel runs the same code first.
- **Same sums.** A lane is `0 + v·g` in the node's zeroed buffer, as in today's zeroed bus. Adding that
  value into a running sum gives the same float as adding `v·g`: `0 + v·g` differs from `v·g` only when
  `v·g` is `-0.0`, and adding `±0` to a sum that started from `+0` never changes it. `add` then sums in
  today's order, from `+0`.
- **Same timing.** Plans and commands keep their push order in one ring and land at the same drain
  positions; the tap arms as a trailer in its plan's swap, with today's restore and re-arm; host
  blocks are never split; staged MIDI and the kernel's I/O do not pass through a queue at all.
- **What is not identical: arm64.** Today `lane += v·g` writes straight into the shared bus, which GCC and
  Apple clang fuse into one multiply-add with a single rounding on arm64. B1 multiplies into the node's
  buffer, then `add` adds: two roundings. Our x64 builds have no fused multiply-add (no `-march`), so the
  baseline target is unaffected, but arm64 output can move by one unit in the last place per summed sample.
  Off-target audio is only held to itself (B0 §5.2), so nothing fails; this paragraph is the record.

## 8. Verification

- **Graph layer** (`retroplug-graph-test`): `layout_for` and validate-first planning; the narrowed wait;
  `drain` / `run`; the `Host` command kind in push order with swaps, including two swaps in one drain;
  per-plan host data; `audio-out` through `get_interface`; a second `audio-out` refused.
- **`SystemNode<T>`** (a Catch2 binary linking the backend, the tenth `test:plugin` binary): for every
  backend, a node's output equals a direct prepare / step / finish run bit for bit, for a lone system and
  a linked pair of mGBs, across the B0 block partitions; lane counts per connected mask.
- **The translator**: documents for every routing mode and host shape against explicit expected wiring
  tables (not against `BlockRunner`, which is deleted), plan order with the kernel first, chained adds past
  16 inputs, slot shifts on remove, and the `SyncTap` riding the swap of exactly the four calls that run
  `syncSplitPlan` today, with today's arm and restore sequence (including a redundant re-arm).
- **Parity** under `RETROPLUG_GRAPH_ENGINE=1` is the progress meter while the steps land.
- **Acceptance** (B0 §4): parity with the fixtures present and `RP_FAIL_ON_SKIP=1`
  (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`) passes with the switch
  off and on before the flip, and once more after it. Then all of `pnpm test:native`, `pnpm test`,
  `pnpm test:plugin` and the node addon; a ThreadSanitizer pass with edits made while the audio thread
  renders; and `reaper:editor-reopen`, `reaper:two-instances-load` and `reaper:params`, the only checks
  where a live host drives the ring.

## 9. Work order

Before anything moves, each its own commit with parity unchanged:

1. The three latent bugs: `QueuedInvoker::stageControllerMidi` copies at most 4 bytes but sets the full
   length, so `drainInto` reads past the inline buffer for a longer message; `freePending` leaks a
   `StageSystemMidi` payload; `renderAudio*` have no ownership guard (`renderBlocks` has one).
2. Parity cases for the two shapes the matrix lacks (B0 §9): `renderAudioPerSystem` (two and three systems,
   mixed kinds) and `renderAudioPerChannel` (GB stems; a NES built in modes 0, 1, 2 and 3), recorded on
   today's engine.

Then, behind the switch:

3. The graph layer: `layout_for`, the narrowed wait, `drain` / `run`, the `Host` command kind, per-plan host
   data, the two `audio-out` fixes.
4. `SystemNode<T>` and its tests.
5. `rp.dsp.kernel`.
6. The translator and its tests.
7. The Engine in graph mode, the shapes in the render RPCs and hosts, and the audio driver's run flag.
8. The flip: graph mode becomes the only path. Deleted with it: the routers, `BlockRunner`, `LinkGroup`,
   native `AudioRouting`, `Project` (the translator's model replaces its list, the system table its
   lookups), the dead `OfflineRender`, `syncSplitPlan`'s lane plan (`splitPlan_`; the arming half lives
   on in `SyncTap`), the structural command kinds,
   `EngineInvoker`'s rings, `processBlockPerSystem` / `processBlockPerChannel`, and the switch.
9. Docs: `spec/01-architecture.md` (the engine section), `spec/06-build-test.md`, AGENTS.md (the binary
   count), §8 of the main design.

## 10. Not in B1

- `rp.host.midi-in`, `rp.host.midi-out`, the system nodes' input ports and the GB `serialOut` port: B3, with
  the protocol nodes and the queue-order decision (§2.3).
- `rp.host.audio-in`, and so reading host inputs before the outputs are silenced (owed by §8): nothing reads
  an input yet.
- Deleting `ConfigField` / `applyConfigField` and the kernel commands: B2, when TS sends documents.
- Fixing any of B0's four recorded defects, the audio-thread allocations B1 carries over
  (`setChannelExportMode`, SameBoy's restart and lazy buffer growth, the payload frees), or the growth of a
  linked Game Boy's unread serial queue.
- How B2 hands cores to TS-built documents: B1's core token is a translator detail, and B2 decides whether
  it survives.

## 11. Found while building

- **Release ring too shallow.** It had 64 slots against a 256-slot command ring that the narrowed wait lets
  fill. It is now twice the command ring.
- **reactivate dropped the plan's host data.** After any sample-rate or block-size change the system table
  was null, so kernel fan-out and publishing silently stopped. It now keeps it.
- **SyncTap was a separate push from its plan.** With the audio thread owning, one block could run new wiring
  with the old tap mode, and a full ring could drop it. It now rides the swap. A replace of an absent id also
  re-arms, and a same-id NES replace no longer inherits the old core's export mode (a B1 regression against
  pre-B1 `replaceSystem`).
- **Destruction order.** `Engine::Graph` and the test rig destroyed the GraphHost after the core handoff its
  nodes call into from their destructors. The host is now declared after them.
- **SmsPsg had no destructor.** About 48 KB leaked per SMS core freed. Fixed in the in-tree `deps/mesen`.
- **The graph tests' allocator replacement missed the nothrow new/delete forms**, so ASan reported a
  mismatch. They are replaced too.
- **A block larger than the plan's maxFrames ran the engine tail** (duplicate MI.OUT, publish, ppq advance)
  for cores that never ran. It now runs nothing.
- **Several tests passed whatever the code did:** the linked-pair node test, the SMS node case, and the
  rollback test after validate-first planning. They now fail under the defects they name.
