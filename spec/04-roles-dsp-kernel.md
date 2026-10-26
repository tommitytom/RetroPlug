# 04 — Roles & the DSP kernel

Roles are how RetroPlug attaches *meaning* to a system without teaching the C++ cores anything.
A role is a `{ kind, config }` pair keyed by a string; the core treats `config` as an opaque
per-kind blob. Everything specific to a system — a backend's emulator knobs, or an optional
feature like LSDj sync or mGB passthrough — is a role, so the cores stay free of backend/LSDj/mGB
knowledge. Cores are built **bare** (`setSniffDefaultRoles(false)`); all feature behaviour lives in
TypeScript and runs inside the **DSP role kernel**.

This is the sharpest expression of the thesis (*"native owns bytes and cores; TypeScript owns
meaning"*): the identical TS kernel code runs under the mock (a `CollectingSink`) in unit tests and
on the audio thread (the script node `rp.dsp.kernel`, its sinks sending on the node's output ports) in
the real hosts, and native carries the kernel's bundle and its structure (the node's settings) without
interpreting either.

This doc covers the role model and the DSP kernel end to end. For the runtime plumbing the kernel
rides on (the command ring, the QuickJS runtimes, the audio-thread ownership model), see
[01-architecture.md](01-architecture.md); this doc references those concepts rather than
re-explaining them. For the C++ script host the kernel node runs in, see
[02-native-host.md](02-native-host.md).

## The two role kinds

| | **system-role** | **feature-role** |
|---|---|---|
| Carries | a backend's live emulator knobs | a DSP-thread behaviour |
| Attached by | backend/core kind | ROM identity (a `RomProvider`) |
| `category` | `"system"` | `"feature"` |
| Examples today | `sameboy` | `mgb`, `lsdj-sync`, `midi-routing` |
| Config reaches the live core? | **yes** — the only role config that crosses, via `applyRoleConfig` | **no** — its config never gets a C++ struct |
| Where behaviour lives | inside the core (native applies the fields) | the TS DSP kernel (`dsp` behaviour) |

The dividing line is load-bearing: **only a system-role's config crosses the RPC boundary into the
live emulator**. A feature-role's config is pure TS — it only ever travels as part of the kernel
structure JSON the DSP kernel parses (below). There is exactly one system-role today (`sameboy`);
Mesen exposes no natively-consumed knobs yet, so NES/GBA systems carry no system-role.

## The role model

Five pieces make up the generic model, in [systemRoles.ts](../packages/retroplug/src/systemRoles.ts).
It ports the shape of the native `RoleConfig` tagged union into a runtime registry, minus the
hardcoded if/else chains.

### `RoleInstance` — what serializes

```ts
interface RoleInstance { kind: string; config: Record<string, unknown>; }
```

A system carries an ordered `RoleInstance[]`. This is the persisted form (part of `SystemEntry`);
config is an opaque per-kind blob whose schema the role owns.

### `RoleType` — a registry entry

Contributed by the built-ins (or, eventually, an extension). Fields
([systemRoles.ts:56](../packages/retroplug/src/systemRoles.ts#L56)):

| Field | Meaning |
|---|---|
| `kind` | the string key (for a system-role, `kind` IS the core, e.g. `"sameboy"`) |
| `category` | `"system"` or `"feature"` |
| `scope?` | where the `dsp` behaviour runs: `"system"` (per-system, default) or `"project"` (once over all systems, e.g. routing) |
| `schema` | a zod schema whose `.parse` fills defaults + clamps a partial/invalid config into a full one; `.parse({})` yields the default config |
| `dsp?` | the DSP-thread behaviour — a `SystemBehavior` (system scope) or `ProjectBehavior` (project scope), run per block by the kernel |
| `onConstruct?` | a load-time hook that transforms the resolved `ConstructSpec` just before the core is built (ADDITIVE by contract — seed only what's otherwise absent) |
| `ui?` | **deferred** — a UI-thread behaviour + settings render descriptor (see [Not yet built](#not-yet-built--deferred)) |

### `RoleRegistry` — the kind-keyed store

[`RoleRegistry`](../packages/retroplug/src/systemRoles.ts#L111) holds a `Map<kind, RoleType>`
plus a list of `RomProvider`s:

| Method | Behaviour |
|---|---|
| `registerRole(t)` / `registerRomProvider(fn)` | populate the registry |
| `roleType(kind)` | look up a `RoleType` |
| `systemRoleFor(core)` | the `"system"` role whose kind === the core, or none |
| `defaultRoles(core, platform, header, embeddedRom)` | the default `RoleInstance[]` for a fresh system: the core's config role (if any) followed by every provider's feature-role suggestions, each `schema.parse`d |

### `RomProvider` + `RomContext` — feature-role attachment

A `RomProvider` inspects a freshly-classified ROM and returns the feature roles to attach. It is the
TS twin of the C++ `RomSniffer`'s default-role step:

```ts
type RomProvider = (rom: RomContext) => RoleInstance[];
// RomContext = { platform, core, header (cartridge title @ 0x134), embeddedRom }
```

The built-ins ([romProviders.ts](../packages/retroplug/src/romProviders.ts)) match on ROM
identity:

| Provider | Match | Attaches |
|---|---|---|
| mGB | embedded marker `"mgb"`, or cartridge title starting `MGB` | `{ kind: "mgb", config: {} }` |
| LSDj | cartridge title starting `LSDJ` (stock or arduinoboy build) | `{ kind: "lsdj-sync", config: { mode: 1 } }` |

The embedded-ROM **marker** is the only signal for a baked-in synth, whose bytes never reach TS — so
its header is empty and the provider matches on the marker instead. ROM providers are registered into
the **control-plane** registry only; the script-node bundle the kernel runs in never sees them.

### `ConstructCaps` + `onConstruct` hooks

`onConstruct` runs at construct time for each role actually attached to a system (so it is inherently
ROM-gated), transforming the resolved `ConstructSpec`. It sees only a narrow `ConstructCaps` slice —
`{ savFromJson, fileExists }` — which the full `Backend` satisfies structurally, so the store passes
itself.

The one hook today is LSDj's empty-sav seed
([dspRoles.ts:311](../packages/retroplug/src/dspRoles.ts#L311)): a fresh LSDj cart with no
save data runs a 12–15 s cartridge self-test on boot. When nothing else will seed the battery (no
savestate, no SRAM blob, no on-disk `.sav` for native to load) the hook hands it a valid empty sav
via `savFromJson("{}")` so LSDj boots straight to the song screen. It is additive — the spec is
returned untouched when real save data is present.

### Where derivation happens in the store

[SystemsStore](../packages/retroplug/src/systemsStore.ts) drives the registry:
`defaultRoles(...)` reads a `ROLE_HEADER_LEN = 0x150` header prefix and calls
`registry.defaultRoles(...)`
([systemsStore.ts:759](../packages/retroplug/src/systemsStore.ts#L759)); `applyConstructHooks`
folds each attached role's `onConstruct` over the spec before the build call
([systemsStore.ts:863](../packages/retroplug/src/systemsStore.ts#L863)). See
[03-ts-layer.md](03-ts-layer.md) for the surrounding store idioms.

## The built-in roles

Three registration functions populate the control-plane registry
([appHost.ts](../packages/retroplug/src/appHost.ts) `buildAppRegistry`):
`registerCoreRoles` + `registerDspRoles` + `registerRomProviders`.

**System roles** ([coreRoles.ts](../packages/retroplug/src/coreRoles.ts)) — keyed by core
value, ranges mirroring the native enums:

| Role | Config (zod, defaults) |
|---|---|
| `sameboy` | `model` 0–13 (CgbC=9), `highpass` 0–2, `linkGroupId` 0–255, `fastBoot` bool (true), plus the display group (`colorCorrection`, `dmgPalette`, `lightTemperature`) |
| `mesen` | one role for every Mesen platform, so the knobs are per-platform and the menu gates each group. NES: `region`, `removeSpriteLimit`, `apuLatencyMs`, the cartridge-accuracy pair (`s5bNoise` / `mmc5PhaseReset`, both defaulting to `"n8"`), `expansionVolume` (the cartridge sound chip's level, percent of unity — the same value the UI rescales into a connected N8's `master_vol`), `channelExportMode`, `sdRoot`. SMS/GG: `enableFm` |

Not every system-role field is live. `region`, `channelExportMode` and `sdRoot` are read when the core
is **constructed**, so an edit takes effect only after a rebuild: `setRoleConfig(id, …)` then
`reset(id)`, which re-sends the stored role config as the construct `settings` blob and hands back a
new id. `sdRoot` is the host directory the emulated EverDrive N8 SD card maps to (`"/"` on the card),
for a NES ROM that pulls its data off the cartridge; empty means a per-process scratch directory, not
the working directory. Reading back what such a ROM sends is `drainCoreBytes` ([09](09-cli-debugging.md)).
A ROM can also skip the CPU entirely: `CMD_F_FRD_MEM` has the emulated MCU read the open file straight
into cartridge memory over the PI bus, which `MesenNesSystem` backs for the CHR-RAM window
(PI `0xC00000`) only. That transfer's duration is modelled from the rate measured on real hardware,
so the ROM's wait loop spins about as long here as it does on a console.

`fifo` (plus the `fifoDepth` / `fifoBytesPerSecond` overrides) says how faithfully that FIFO behaves
like the cartridge's. It defaults to `"instant"` - unbounded and delivered immediately, which is what
every existing test assumes - and `"hardware"` opts in to 2048 bytes deep, dropping when full, fed at
the wire's ~150 KB/s. Both of those absences hide a bug class rather than being bugs: a ROM that
cannot lose a byte never has its recovery path exercised, and one handed a whole chunk atomically is
never starved mid-structure. `getCoreTransportStats` reports both queue depths and the cumulative
drop count ([09](09-cli-debugging.md)).

**DSP-thread roles** ([dspRoles.ts](../packages/retroplug/src/dspRoles.ts)) — feature
behaviours over the per-system context:

| Role | Scope | Behaviour |
|---|---|---|
| `mgb` | system | forward every host-MIDI byte verbatim into the system's serial input |
| `lsdj-sync` | system | dispatches on `config.mode` (LsdjSyncMode): MidiSync (24-PPQN `0xF8` clock, `tempoDivisor`-subdivided), MidiSyncArduinoboy (note-driven play/divisor + `0xFA`/`0xFC` transport bookends), MidiMap (row bytes + `0xFE`), KeyboardMidi (PS/2 scancodes), MidiPassthrough (raw bytes → serial), MidiOut (ArduinoboyMaster: emulator serial-out → decode → `emitMidiOut`), MasterSync (LSDj self-clock → host MIDI); Off/Keyboard emit nothing. Keeps cross-block scratch via `ctx.state`. Has the `lsdjSeedSav` `onConstruct` hook |
| `midi-routing` | project | fans the block's global `midiIn` into per-system inboxes, reusing the pure `routeBlock` decision ([midiRouting.ts](../packages/retroplug/src/midiRouting.ts)) |

## Which config reaches the live core

Two channels cross the RPC boundary to a running emulator, and no feature-role config uses either:

| Change | RPC | Reaches |
|---|---|---|
| the two universal settings (`gainDb`, `reloadOnRomChange`) | `applySystemSetting` | the live core (see [03-ts-layer.md](03-ts-layer.md)) |
| a **system-role** config edit | `applyRoleConfig(id, kind, config)` | the live core (`sameboy` + `mesen`), minus the construct-time fields noted above |
| a **feature-role** config edit | *(none to the core)* | only the DSP kernel, as its node's settings in the next document the project publishes |

[`SystemsStore.setRoleConfig`](../packages/retroplug/src/systemsStore.ts#L536) merges the
partial, re-parses through the role schema, and calls `backend.applyRoleConfig` **only when the role's
`category === "system"`**. On the native side
[`EngineRpcService::applyRoleConfig`](../packages/native/src/host/rpc/EngineRpcService.cpp#L154)
accepts `kind == "sameboy"` and `"mesen"`, decodes the whole role config, and pushes each field as a
guarded config field (an unchanged field is a no-op, so moving `highpass` never triggers a spurious
model restart).

A **feature**-role config edit takes a different path: `setRoleConfig` updates TS state and, for a
role the kernel runs, publishes the graph (`setOnGraphChange` → `ProjectStore.publishGraph`), whose
kernel node's settings hold the whole `KernelStructure` (the role's config rides inside them); then
`markDirty` fires the systems `onChange` → the project's `onSystemsChange`
([projectStore.ts:210](../packages/retroplug/src/projectStore.ts#L210)) for the structure's
observers. The core is never told, with one exception: an `lsdj-sync` mode change arms or disarms the
Game Boy's serial-out capture (`setSerialOutCapture`), which the two MIDI-out modes decode.

## The DSP kernel

[dspKernel.ts](../packages/retroplug/src/dspKernel.ts) is pure TS with no `Backend`
dependency. The kernel node hands `processBlock` what its inputs carried for one audio block;
project-scope behaviours fan the input to systems, then each system's ordered pipeline of behaviours
reads its inputs and writes frame-tagged sinks.

### Structure vs per-block input

| Type | Arrives | Contents |
|---|---|---|
| `KernelStructure` | at each structure change, via `setSystems`: the kernel node's settings (below) | `{ project?: RoleInstance[], systems: { id, pipeline: RoleInstance[] }[] }` |
| `BlockInput` | every block, via `processBlock`, built by the kernel node from its inputs | `BlockInfo` (`frames`, `sampleRate`, `tempo`, `ppqStart`, `transport`, `controllerConnected`) + `midiIn` (global), `serialOut` (each Game Boy's MI.OUT bytes of last block), `controllerIn`, `buttons`, `keys` |

**System order is authoritative**: positional MIDI routing maps a channel to a system by its index in
`systems`, so the list defines both the set and the order. `DspKernel` keeps ONE persistent `Block`
view and overwrites its dynamic fields each block — no per-block allocation.

### Execution order

[`DspKernel.processBlock`](../packages/retroplug/src/dspKernel.ts#L417):

1. **The per-system inputs**: this block's `keys`, `buttons` and `serialOut` are fanned to their
   systems, one pass each, into per-system lists kept across blocks and cleared in place.
2. **Project scope**: each project-scope stage (routing) runs, filling the per-system inboxes (kept
   the same way) from the global `midiIn`.
3. **System scope**: for each system in order, run each stage of its pipeline. A stage whose roleType
   is missing, project-scope, or has no `dsp` is **skipped** when `setSystems` builds the pipeline, so a
   pipeline needs no filtering upstream. The stores filter it all the same (`SystemsStore.graphSystems`
   hands it, with a registry, only the roles the kernel runs; a store without one hands every role), so
   a config edit to a role it does not run (a model, a palette, an asset override) changes no graph
   document.

`setSystems` also **prunes per-system tick state for ids no longer present**, so a removed-then-readded
id starts a fresh clock instead of resuming mid-count. A surviving id keeps its tick counter and its
stages' scratch, and each project stage keeps its own by position (a controller session survives a
system being added); a project stage past the new count is dropped.

### The sinks

A behaviour never carries a `sys` argument; it writes through a per-system `SystemCtx`
([dspKernel.ts:153](../packages/retroplug/src/dspKernel.ts#L153)) whose sinks are scoped to
the system id. The kernel forwards each call to an injected `SinkTarget`:

| Sink | Signature (on `SystemCtx`) | Direction |
|---|---|---|
| `pushSerialIn` | `(frame, byte)` | → the system's serial input (LSDj clock, mGB passthrough) |
| `emitCoreMidi` | `(frame, data)` | → the system's MIDI input (a NES's N8 FIFO; a Game Boy ignores it) |
| `pushCoreBytes` | `(frame, data, flush?)` | → the core's raw byte input (a tracker's host-sync stream); `flush` first drops what the FIFO still holds |
| `emitMidiOut` | `(frame, data)` | → host MIDI out |
| `pressButton` | `(button, down)` | → role-generated joypad transition (distinct from a UI tap) |
| `eachTick` | `(resolution, cb)` | walk the PPQ ticks in this block (see below) |
| `setNextTick` | `(tick)` | force the tick this system's next `eachTick` starts from |

`SinkTarget` ([dspKernel.ts:102](../packages/retroplug/src/dspKernel.ts#L102)) is the
system-addressed form: `pushSerialIn(system, frame, byte)`, `emitCoreMidi(system, frame, data)`,
`pushCoreBytes(system, frame, data, flush?)`, `emitMidiOut(system, frame, data)`,
`pressButton(system, frame, button, down)`, optional `emitControllerOut(data)` and `reset()`. A
project stage reaches the same target through its `ProjectCtx` (`emitMidiOut` for any system,
`emitControllerOut` for a control surface). Under the mock a `CollectingSink` gathers calls into a
`Sinks` object the assertions read; in the hosts the kernel node's target sends each call onto one of
its output ports ([Running as a script node](#running-as-a-script-node)), which the graph wires to
the system nodes' inputs and the host's outputs. (Role-generated `pressButton` is emitted at
`frame 0`.)

### The drift-exact PPQ clock — in JS

The tick clock lives **entirely in the kernel**; native owns no `nextTick`/`eachTick` primitive.
[`walkTicks`](../packages/retroplug/src/dspKernel.ts#L235) is a faithful TS twin of native
`PpqUtil::eachTick`: it walks the ticks at `resolution` ticks/quarter that fall in this block, calling
`cb(tick, offsetSamples)` for each. The caller-owned `nextTick` persists across blocks (the kernel
stores it per system id), so the clock is drift-free at block edges — each tick fires exactly once,
never double or missed, and a >1-tick transport jump (seek/loop/start) resyncs. `lsdj-sync` uses it at
24 PPQN.

## `kernelProjection` — store → kernel

[projectKernelStructure(views, midiRouting, controller?)](../packages/retroplug/src/kernelProjection.ts#L72)
is the single seam that turns "what the app has" into "what the DSP runs". Its `views` are
`ProjectedSystem`s (`{ id, roles }`) in slot order:

- each system's `roles` map straight into its `pipeline`, order preserved;
- the project-scope `{ kind: "midi-routing", config: { mode: midiRouting } }` role is **synthesized**
  from the project's MIDI-routing setting (it lives in project settings, not on any system);
- with a controller enabled, the project-scope `launchpad` role is synthesized from the controller
  settings the same way (its config carries the derived song-timing table, so it never reaches the
  `.rplg`), and the `lsdj-sync` stage of the cart the MI.MAP app drives is forced to `midiMap`, or to
  `off` when the cart's own SYNC setting is not MI.MAP (`controllerSyncOverride`).

It is pure and **registry-free**: the kernel self-guards scope. `ProjectStore.publishGraph` calls it
with each system's pipeline (`SystemsStore.graphSystems`: the roles the kernel runs, or every role for a
store without a registry) and
publishes the result with the graph, where `buildGraph` makes it the kernel node's settings, so every
structural edit, and every edit to a role or setting the structure reads, reaches the kernel.

## Running as a script node

The kernel runs as the script node `rp.dsp.kernel`
([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts), docs/modular-graph-phase-b3a-design.md
section 4) inside the script host's **bare QuickJS audio context** (no txiki), on whichever thread runs
blocks, distinct from the control-plane runtime; see [01-architecture.md](01-architecture.md) for the
runtimes and [02-native-host.md](02-native-host.md) for the script host.

**The bundle** ([script/bundle.ts](../packages/retroplug/src/script/bundle.ts)) holds the kernel
node's definition, appended to the global `__rp_scriptNodes` by `registerScriptNodes`
([script/register.ts](../packages/retroplug/src/script/register.ts)). It is esbuilt to one IIFE,
compiled to QuickJS bytecode at build time and embedded in the backend library, and every Engine runs
it in both of its script contexts at construction, so every host has the kernel node with no compile
or load call. The node builds its **own** registry (**`dspRoles` only**: no core roles, no ROM
providers), shared by every instance, since a behaviour keeps its state in the kernel's scratch bags.

**Its settings** are the kernel's structure plus one fact per system:
`{ project: RoleInstance[], systems: { id, pipeline: RoleInstance[], serialOut: boolean }[] }`. Every
field but a system's `id` and a role's `kind` has a default (a document naming `rp.dsp.kernel` with no
settings plans an empty kernel, and a role's `config` defaults to `{}`), and each id is an integer from 1
to 2^32 - 1, distinct from the others: two systems under one id are refused. `serialOut` is true for a Game Boy,
the one system node with that output.

**Its ports** follow from the settings and are named by system id, never position:

| Direction | Ports |
|---|---|
| in | `midiIn` (events), `controllerIn` (events), `controllerConnected` (control), and `sys-<id>.serialOut` (bytes) for each system with `serialOut` |
| out | `sys-<id>.serialIn` (bytes), `sys-<id>.midi` (events), `sys-<id>.bytes` (bytes) and `sys-<id>.buttons` (events) for each system; `midiOut` (events); `controllerOut` (events) |

`buildGraph` wires them ([03-ts-layer.md](03-ts-layer.md)): the inputs from `rp.host.midi-in`,
`rp.host.controller-in` and each Game Boy's `serialOut` (a feedback wire, so the kernel reads last
block's MI.OUT), each `sys-<id>.*` output to that system node's input of the same name, and `midiOut`
and `controllerOut` to the host's output nodes. Each output declares its queue's capacity: `serialIn`
and `midi` 4096 messages / 64 KiB, at least what `rp.host.midi-in`'s output holds, so a SysEx the host
stages whole reaches the system whole and host MIDI is dropped only at that edge; the rest sized from
the most the suites send through them in one block, with room to spare: `bytes` 256 / 1 KiB, `buttons`
64 / 128 bytes, `midiOut` 1024 / 8 KiB, `controllerOut` 256 / 4 KiB. A send past one is dropped and
counted (`Engine::droppedMessages`).

**A structure change reshapes it.** The definition declares `reshapes`, so a settings change keeps
the instance even when it changes the ports: the planner binds the new plan's ports to it by id (a
surviving system's wires stay on its ports) and commits the settings at the plan swap, where
`onSettings` hands them to `DspKernel.setSystems`. A surviving id keeps its clock and scratch, the
project stages keep theirs, and a removed id is pruned ([Execution order](#execution-order)).

**Each block** it builds the `BlockInput` afresh, field for field: the transport (`frames`,
`sampleRate`, `tempo`, `ppqStart` at the block-start ppq, `transport`); `controllerConnected` when the
control's value from frame 0 is non-zero (`startValue`: its start, or a change at frame 0, which is how
`rp.host.controller-in` signals a flip); `midiIn`, and `controllerIn` when a surface sent something,
copied from their ports as `{ frame, data }` with `data` a plain array (a role may keep one across
blocks); each Game Boy's `serialOut` bytes, concatenated in the order the settings list the systems
(slot order) and tagged with its id; and empty `buttons` and `keys` (no host feeds them yet). Its
`SinkTarget` sends each sink call onto a port, in emission order:

| Sink call | Port |
|---|---|
| `pushSerialIn(system, frame, byte)` | `sys-<id>.serialIn`, a run of consecutive bytes at one frame as one message (sent when a byte at another frame comes, and when `processBlock` returns or throws), which the system node applies a byte at a time at that frame |
| `emitCoreMidi(system, frame, data)` | `sys-<id>.midi` |
| `pushCoreBytes(system, frame, data, flush)` | `sys-<id>.bytes`, the flush flag as `RP_MESSAGE_FLUSH` |
| `pressButton(system, frame, button, down)` | `[button, down]` on `sys-<id>.buttons`, at frame 0 |
| `emitMidiOut(system, frame, data)` | `midiOut`, behind the system's id as 4 little-endian bytes, which `rp.host.midi-out` strips into the message's system tag |
| `emitControllerOut(data)` | `controllerOut`, at frame 0 |

A system sink call (`pushSerialIn`, `emitCoreMidi`, `pushCoreBytes`, `pressButton`) naming a system with no
ports (an id not in the settings) is dropped; `emitMidiOut` sends for any id, tagged with it. Host MIDI out is one port
rather than one per system so that its emission order holds: a project stage (the Launchpad
controller addressing a real Game Boy) emits before every system stage.

**Frame order.** A queue is frame-ordered: a message pushed with an earlier frame than the one before
it is raised to that frame. Two roles emit out of order onto a system's `serialIn`: Arduinoboy MIDI
sync writes a row byte at its note's frame, then START / STOP and its clocks at frame 0, and MI.MAP
writes row bytes at their note's frame, then its `0xFF` clocks at frame 0. Both drive only a Game
Boy, whose serial queue begins a byte only once every byte ahead of it has gone, so raising a byte's
frame to the one before it changes nothing the core sees.

**A throw** in the kernel's JS is caught by the script host and counted (`Engine::scriptThrows`; the
first one is logged): what the kernel sent before it stands, its held serial-in runs included, and the
next block runs again. A call that runs past the script host's CPU-time deadline is stopped the same
way, except that it skips the `finally` that sends the runs, so those are cleared at the next `process`
instead; a second expiry in a row faults the node, and a fault outlives reshapes (only a rebuild, or the
node removed and added again, clears it).

**Two registries, one kernel.** The control-plane registry (core + DSP + ROM providers) drives the
stores and derives roles; the bundle's registry holds only the DSP behaviours. This is safe because
`kernelProjection` is registry-free and the kernel self-guards scope, so a pipeline (one a
`DirectGraph` caller wrote included) is interpreted identically whether or not the bundle knows every
kind in it.

**Plugin wiring.** [pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts)
loads no kernel: the graph the project publishes carries the kernel node and its structure.

## Not yet built / deferred

Only the pieces below are open; the [07-remaining-work.md](07-remaining-work.md) doc is the authoritative
inventory of remaining work.

- **Raw Keyboard mode is the one unbuilt LSDj sync mode.** `lsdj-sync` authors every other mode —
  MidiSync, MidiSyncArduinoboy, MidiMap, KeyboardMidi, MidiPassthrough, and the two host-out
  directions **MidiOut** (ArduinoboyMaster MI.OUT, `emulator serial-out → decode → emitMidiOut`) and
  **MasterSync** — backed by the `ctx.state` scratch bag and a live-apply menu. Raw **Keyboard**
  (mode 4) still emits nothing: it needs the per-block `keys` feed (below).
- **Per-block buttons/keys aren't fed yet.** The kernel carries `buttons`/`keys` inputs and per-system
  filtering, but the kernel node has no port for them and hands it empty arrays every block, so the
  input is present but nothing in the running host feeds it, which is what blocks raw Keyboard mode.
- **kit-patch UI-thread behaviour.** LSDj kit-patch runs entirely on the UI / control thread: it
  compiles the kit (over the native `KitCompiler` primitives) and writes the already-patched memory
  regions **into the core**. It has **no DSP-kernel component** and does not use the kernel's sinks
  or ports: the DSP kernel never sees kit data. It needs the deferred `ui?` render seam and a
  control-plane "write a memory region into a core" capability, neither of which exists yet. Not
  yet built.
- **`ui?` render descriptor + extension model.** The `ui?` field on `RoleType` is a labelled-deferred
  seam (a role's own settings UI), and the third-party extension model (registering `RoleType`s, ROM
  providers, and behaviours from outside the built-ins) is future work.

## Key files

TypeScript ([packages/retroplug/src/](../packages/retroplug/src/)):

- [systemRoles.ts](../packages/retroplug/src/systemRoles.ts) — `RoleInstance` / `RoleType` /
  `RoleRegistry` / `RomProvider` / `ConstructCaps`.
- [coreRoles.ts](../packages/retroplug/src/coreRoles.ts) — the `sameboy` system-role.
- [dspRoles.ts](../packages/retroplug/src/dspRoles.ts) — `mgb` / `lsdj-sync` /
  `midi-routing` behaviours + the LSDj `onConstruct` seed.
- [romProviders.ts](../packages/retroplug/src/romProviders.ts) — feature-role attachment by
  ROM identity.
- [dspKernel.ts](../packages/retroplug/src/dspKernel.ts): `DspKernel`, `SystemCtx`, the
  sinks, `walkTicks`.
- [kernelProjection.ts](../packages/retroplug/src/kernelProjection.ts) — store →
  `KernelStructure`.
- [script/kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts) /
  [script/bundle.ts](../packages/retroplug/src/script/bundle.ts) /
  [script/register.ts](../packages/retroplug/src/script/register.ts): the kernel as a script node,
  the bundle every Engine embeds, and how a bundle registers its node types.
- [appHost.ts](../packages/retroplug/src/appHost.ts): `buildAppRegistry`.

Native ([packages/native/src/](../packages/native/src/)):

- [ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp) /
  [ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp): the script-node runtime the
  kernel node runs in.
- [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp): each system node's inputs,
  applied before its core runs.
- [HostIoNodes.cpp](../packages/native/src/host/graph/HostIoNodes.cpp): the host I/O nodes on the
  kernel node's inputs and outputs, and the N8 cart mirror.
- [EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp#L154): `applyRoleConfig`
  (system-role only).
