# 04 - Roles & the protocol nodes

Roles are how RetroPlug attaches *meaning* to a system without teaching the C++ cores anything.
A role is a `{ kind, config }` pair keyed by a string; the core treats `config` as an opaque
per-kind blob. Everything specific to a system (a backend's emulator knobs, or an optional
feature like LSDj sync or mGB passthrough) is a role, so the cores stay free of backend/LSDj/mGB
knowledge. Cores are built **bare** (the legacy build said so with a `setSniffDefaultRoles(false)` call; there is none now); every feature behaviour lives in
TypeScript, and a role whose work is the audio thread's runs as a **protocol node**: a TS script node
of its own in the engine's graph, which `buildGraph` places for each system that has the role.

This is the sharpest expression of the thesis (*"native owns bytes and cores; TypeScript owns
meaning"*): the same node code runs over a `Block` stand-in in the unit tests and on the audio thread
in the script host, and native carries the bundle and each node's settings without interpreting
either. Until B3b ([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)) one
TS DSP kernel ran every role inside one script node; each role is its own node now, and the kernel is
gone.

This doc covers the role model and the protocol nodes end to end. For the runtime plumbing the nodes
ride on (the command ring, the QuickJS runtimes, the audio-thread ownership model), see
[01-architecture.md](01-architecture.md); this doc references those concepts rather than
re-explaining them. For the C++ script host the nodes run in, see [02-native-host.md](02-native-host.md).

## The two role kinds

| | **system-role** | **feature-role** |
|---|---|---|
| Carries | a backend's live emulator knobs | a feature's settings (a sync mode, an asset override) |
| Attached by | backend/core kind | ROM identity (a `RomProvider`) |
| `category` | `"system"` | `"feature"` |
| Examples today | `sameboy`, `mesen` | `mgb`, `lsdj-sync`, `risa-sync`, `lsdj-assets` |
| Config reaches the live core? | **yes**: the only role config that crosses, via `applyRoleConfig` | **no**: its config never gets a C++ struct |
| Where behaviour lives | inside the core (native applies the fields) | its protocol node, when it has one; otherwise the control plane (an asset override is folded into the ROM there) |

The dividing line is load-bearing: **only a system-role's config crosses the RPC boundary into the
live emulator**. A feature-role's config is pure TS: for a role with a protocol node it travels only as
that node's settings in the graph document the project publishes (below).

## The role model

Five pieces make up the generic model, in [systemRoles.ts](../packages/retroplug/src/systemRoles.ts).
It is a runtime registry (the legacy build had a native `RoleConfig` tagged union and hardcoded if/else
chains in its place; neither exists now).

### `RoleInstance`: what serializes

```ts
interface RoleInstance { kind: string; config: Record<string, unknown>; }
```

A system carries an ordered `RoleInstance[]`. This is the persisted form (part of `SystemEntry`);
config is an opaque per-kind blob whose schema the role owns.

### `RoleType`: a registry entry

Contributed by the built-ins (or, eventually, an extension). Fields
([systemRoles.ts:52](../packages/retroplug/src/systemRoles.ts#L52)):

| Field | Meaning |
|---|---|
| `kind` | the string key (for a system-role, `kind` IS the core, e.g. `"sameboy"`) |
| `category` | `"system"` or `"feature"` |
| `schema` | a zod schema whose `.parse` fills defaults + clamps a partial/invalid config into a full one; `.parse({})` yields the default config |
| `onConstruct?` | a load-time hook that transforms the resolved `ConstructSpec` just before the core is built (ADDITIVE by contract: seed only what's otherwise absent) |
| `ui?` | **deferred**: a UI-thread behaviour + settings render descriptor (see [Not yet built](#not-yet-built--deferred)) |

A role type says nothing about the audio thread. Which role kinds have a protocol node, and which node
each is placed as, is one table beside `buildGraph` ([Store → graph](#store--graph)), not a field here:
the node types are code in the bundle every Engine embeds, so that is a fact of the bundle, and two of
the three publishers that need it hold no registry.

### `RoleRegistry`: the kind-keyed store

[`RoleRegistry`](../packages/retroplug/src/systemRoles.ts#L94) holds a `Map<kind, RoleType>`
plus a list of `RomProvider`s:

| Method | Behaviour |
|---|---|
| `registerRole(t)` / `registerRomProvider(fn)` | populate the registry |
| `roleType(kind)` | look up a `RoleType` |
| `systemRoleFor(core)` | the `"system"` role whose kind === the core, or none |
| `defaultRoles(core, platform, header, embeddedRom)` | the default `RoleInstance[]` for a fresh system: the core's config role (if any) followed by every provider's feature-role suggestions, each `schema.parse`d |

### `RomProvider` + `RomContext`: feature-role attachment

A `RomProvider` inspects a freshly-classified ROM and returns the feature roles to attach. It is the
TS replacement for the legacy build's C++ `RomSniffer` default-role step (gone):

```ts
type RomProvider = (rom: RomContext) => RoleInstance[];
// RomContext = { platform, core, header (cartridge title @ 0x134), embeddedRom }
```

The built-ins ([romProviders.ts](../packages/retroplug/src/romProviders.ts)) match on ROM
identity:

| Provider | Match | Attaches |
|---|---|---|
| mGB | embedded marker `"mgb"`, or cartridge title starting `MGB` | `mgb` |
| LSDj | cartridge title starting `LSDJ` (stock or arduinoboy build) | `lsdj-sync` (`midiSync`) and `lsdj-assets` |
| NES | every NES ROM | `nes-n8-midi` |
| smsggdj | the SMSGGDJ build marker, on a Master System or Game Gear | `sms-sync` (`machine` the platform) |
| risa | its iNES 2.0 header fingerprint (MMC5 + a 64 KB battery) | `risa` and `risa-assets`, and `risa-sync` for a build with the `RISA-SYNC` marker |
| BlipToaster | the `bliptoaster` marker in the ROM head | `bliptoaster` and `bliptoaster-assets` |

The embedded-ROM **marker** is the only signal for a baked-in synth, whose bytes never reach TS, so
its header is empty and the provider matches on the marker instead. ROM providers are registered into
the **control-plane** registry only; the script-node bundle never sees a role, a schema or a provider.

### `ConstructCaps` + `onConstruct` hooks

`onConstruct` runs at construct time for each role actually attached to a system (so it is inherently
ROM-gated), transforming the resolved `ConstructSpec`. It sees only a narrow `ConstructCaps` slice
(`savFromJson`, `fileExists`, `readFile`, `pngDecode`), which the full `Backend` satisfies structurally,
so the store passes itself.

LSDj's empty-sav seed ([dspRoles.ts:16](../packages/retroplug/src/dspRoles.ts#L16)) is the plainest: a
fresh LSDj cart with no save data runs a 12-15 s cartridge self-test on boot. When nothing else will seed
the battery (no savestate, no SRAM blob, no on-disk `.sav` for native to load) the hook hands it a valid
empty sav via `savFromJson("{}")` so LSDj boots straight to the song screen. It is additive: the spec is
returned untouched when real save data is present. The asset roles' hooks fold their overrides into the
ROM in memory the same way.

### Where derivation happens in the store

[SystemsStore](../packages/retroplug/src/systemsStore.ts) drives the registry:
`defaultRoles(...)` reads a `ROLE_HEADER_LEN = 0x150` header prefix and calls
`registry.defaultRoles(...)`
([systemsStore.ts:749](../packages/retroplug/src/systemsStore.ts#L749)); `applyConstructHooks`
folds each attached role's `onConstruct` over the spec before the build call
([systemsStore.ts:853](../packages/retroplug/src/systemsStore.ts#L853)). See
[03-ts-layer.md](03-ts-layer.md) for the surrounding store idioms.

## The built-in roles

The control-plane registry ([appHost.ts](../packages/retroplug/src/appHost.ts) `buildAppRegistry`)
takes `registerCoreRoles`, `registerDspRoles`, the asset and marker roles (`lsdj-assets`, `risa`,
`risa-assets`, `bliptoaster`, `bliptoaster-assets`) and `registerRomProviders`.

**System roles** ([coreRoles.ts](../packages/retroplug/src/coreRoles.ts)), keyed by core
value, ranges mirroring the native enums:

| Role | Config (zod, defaults) |
|---|---|
| `sameboy` | `model` 0-13 (CgbC=9), `highpass` 0-2, `linkGroupId` 0-255, `fastBoot` bool (true), plus the display group (`colorCorrection`, `dmgPalette`, `lightTemperature`) |
| `mesen` | one role for every Mesen platform, so the knobs are per-platform and the menu gates each group. NES: `region`, `removeSpriteLimit`, `apuLatencyMs`, the cartridge-accuracy pair (`s5bNoise` / `mmc5PhaseReset`, both defaulting to `"n8"`), `expansionVolume` (the cartridge sound chip's level, percent of unity: the same value the UI rescales into a connected N8's `master_vol`), `channelExportMode`, `sdRoot`. SMS/GG: `enableFm` |

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
like the cartridge's. It defaults to `"instant"` (unbounded and delivered immediately, which is what
every existing test assumes), and `"hardware"` opts in to 2048 bytes deep, dropping when full, fed at
the wire's ~150 KB/s. Both of those absences hide a bug class rather than being bugs: a ROM that
cannot lose a byte never has its recovery path exercised, and one handed a whole chunk atomically is
never starved mid-structure. `getCoreTransportStats` reports both queue depths and the cumulative
drop count ([09](09-cli-debugging.md)).

**Roles with a protocol node** ([dspRoles.ts](../packages/retroplug/src/dspRoles.ts)): each registers its
kind, its zod config schema (the shape the stores parse, persist and hand the node as its settings) and,
for LSDj, the seed hook. What each does on the audio thread is its node's
([The protocol nodes](#the-protocol-nodes)):

| Role | Node | Config |
|---|---|---|
| `mgb` | `rp.mgb.midi` | none |
| `nes-n8-midi` | `rp.n8.midi-to-fifo` | none |
| `risa-sync` | `rp.risa.sync` | none |
| `sms-sync` | `rp.sms.sync` | `machine`: `"sms"` or `"gg"` |
| `lsdj-sync` | `rp.lsdj.sync` | `mode` (an `LsdjSyncMode`, default `midiSync`), `tempoDivisor` 1-8, `autoStart` |

The MIDI routing and the control-surface controller are **project settings**, not roles
([05-data-persistence.md](05-data-persistence.md)): `buildGraph` places a node for each from the
project's settings, as it places a protocol node for a role.

## Which config reaches the live core

Two channels cross the RPC boundary to a running emulator, and no feature-role config uses either:

| Change | RPC | Reaches |
|---|---|---|
| the two universal settings (`gainDb`, `reloadOnRomChange`) | `applySystemSetting` | the live core (see [03-ts-layer.md](03-ts-layer.md)) |
| a **system-role** config edit | `applyRoleConfig(id, kind, config)` | the live core (`sameboy` + `mesen`), minus the construct-time fields noted above |
| a **feature-role** config edit | *(none to the core)* | only its protocol node, as its settings in the next document the project publishes |

[`SystemsStore.setRoleConfig`](../packages/retroplug/src/systemsStore.ts#L525) merges the
partial, re-parses through the role schema, and calls `backend.applyRoleConfig` **only when the role's
`category === "system"`**. On the native side
[`EngineRpcService::applyRoleConfig`](../packages/native/src/host/rpc/EngineRpcService.cpp#L165)
accepts `kind == "sameboy"` and `"mesen"`, decodes the whole role config, and pushes each field as a
guarded config field (an unchanged field is a no-op, so moving `highpass` never triggers a spurious
model restart).

A **feature**-role config edit takes a different path: `setRoleConfig` updates TS state and, for a
role that has a protocol node, publishes the graph (`setOnGraphChange` → `ProjectStore.publishGraph`),
in which that node's settings are the role's parsed config; the planner hands the node its new settings
at the plan swap (`onSettings`), keeping the instance and its state, since every protocol node reshapes.
An edit to a role with no node (an asset override) changes no document. Then `markDirty` fires the
systems `onChange` → the project's `onSystemsChange`
([projectStore.ts:215](../packages/retroplug/src/projectStore.ts#L215)) for the structure's
observers. The core is never told, with one exception: an `lsdj-sync` mode change sets the Game Boy's
serial-out capture (`setSerialOutCapture`), which the two MIDI-out modes decode: `drive` for MI.OUT, where
the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where LSDj clocks itself.

## The protocol nodes

Seven TS node types ([src/script/nodes/](../packages/retroplug/src/script/nodes)), each a `defineNode`
in the script-node bundle ([script/bundle.ts](../packages/retroplug/src/script/bundle.ts)): the MIDI
routing, a protocol node for each role that has one, and the control-surface session. Each is its
protocol's glue around pure functions that stay where they were (the Arduinoboy and master-sync
decoders in `lsdjArduinoboy.ts`, `routeTarget` in `midiRouting.ts`, `risaLocate` / `risaArmPacket` in
`risaSync.ts`, `syncLevelsFor` in `smsSync.ts`, `walkTicks` in `ticks.ts`, the LSDj keyboard map and
link bytes), and each is held to the kernel it replaced by the recorded digests of
[ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp).

| Type | Placed for | Inputs | Outputs | Settings |
|---|---|---|---|---|
| `rp.midi.route` | the project (`routing`) | `midiIn` | `sys-<id>` for each system, in slot order | `mode` (a `MidiRouting`, default `sendToAll`), `systems` (the ids in slot order) |
| `rp.mgb.midi` | `mgb` | `midi` | `serialIn` | none |
| `rp.n8.midi-to-fifo` | `nes-n8-midi` | `midiIn` | `midi` | none |
| `rp.risa.sync` | `risa-sync` | none (the transport) | `bytes` | none |
| `rp.sms.sync` | `sms-sync` | none (the transport) | `bytes` | `machine` |
| `rp.lsdj.sync` | `lsdj-sync` | `midi`, `launches`, `serialOut` | `serialIn`, `buttons`, `midiOut` | `mode`, `tempoDivisor`, `autoStart` |
| `rp.controller.session` | the project's controller (`controller`) | `controllerIn` (events), `controllerConnected` (control) | `controllerOut`, `launches`, `midiOut` | `app`, `target`, `systemId` (resolved: 0 is none), `appConfig` (parsed), `songRowTicks`, `anchorRows`, `anchorSeq` |

**How they are written.** Against the `Block` API, as the bench's pass-through is: indexed loops over
the input `Message` views, `send` on the node's own ports, state and the tick counter on the instance
(made in `create`), nothing allocated per block. Measured, the routing, mGB, N8 and LSDj MIDI-sync nodes
allocate nothing in a warm block, JS or native ([08-profiling.md](08-profiling.md)); what still allocates is
work that makes something: risa's arm packet on a start or a seek, the controller session's surface diff and
launches, and the LSDj node's two MIDI-out decoders' emitted messages. Every type declares `reshapes`, so a
settings change keeps the instance and its state, even one that changes its ports (the routing node's
outputs follow the systems); a system removed and added again gets fresh nodes.

**What each does per block:**

- **`rp.midi.route`** fans the host's MIDI out to the systems: each message goes, at its own frame and
  in the order it came, to every system (`sendToAll`, and always for a SysEx or a system message), or to
  one by its channel's position among them (`fourChannelsPerInstance`: channel / 4, `oneChannelPerInstance`
  and `midiChannelToInstance`: the channel, each modulo the system count), by `routeTarget`'s arithmetic.
  `midiChannelToInstance` hands its target a copy with the channel rewritten to 0, from a buffer made once.
  System order is authoritative: the positional modes route by a system's index in `systems`. Its layout
  refuses two systems under one id.
- **`rp.mgb.midi`** sends each routed message's bytes as one `serialIn` message at its frame, which is all
  mGB listens for (the system node applies a message's bytes in order at its frame).
- **`rp.n8.midi-to-fifo`** sends each routed message whole to the NES core's `midi` input, which feeds the
  always-attached N8 FIFO. Its input is `midiIn` because its output is named for the system input it
  feeds, as every protocol node's is.
- **`rp.risa.sync`** drives risa's N8-FIFO host-sync receive path from the transport alone: on a rise, or a
  ppq discontinuity while playing (a seek or a loop), a 5-byte arm+locate computed from the block-start
  ppq, flushing the FIFO (`RP_MESSAGE_FLUSH`), then START; on a fall, STOP; while playing, a 24-PPQN clock
  at each tick's frame.
- **`rp.sms.sync`** clocks smsggdj: while playing, a 2-bit counter advanced each 24-PPQN tick, each step a
  level word at the tick's frame that the system holds on the sync lines (controller port 2's TR + TH on a
  Master System, the EXT port on a Game Gear, by `machine`).
- **`rp.lsdj.sync`** runs its `mode`, reading its routed MIDI and then the controller's launches as one
  inbox, in that order: MidiSync (a 24-PPQN `0xF8` clock at each tick's frame, `tempoDivisor`-subdivided,
  `autoStart` tapping START on the transport's rise), MidiSyncArduinoboy (notes 24/25 play and stop,
  26-29 set the divisor, 30 and up a row byte, `0xFA`/`0xFC` on transport edges, its clocks at frame 0),
  MidiMap (a NoteOn a row byte, the matching NoteOff `0xFE`, `0xFF` clocks at frame 0), KeyboardMidi (PS/2
  scancodes, sliding the octave cursor), MidiPassthrough (every byte verbatim), MidiOut (ArduinoboyMaster:
  last block's serial-out bytes decoded to host MIDI), and MasterSync (LSDj clocks itself: a `0xF8` a byte,
  the first of a run after the song-row NoteOn and `0xFA`, and `0xFC` once 200 ms pass with no byte, timed
  on the bytes' frames); Off and Keyboard send nothing. Its serial-in bytes go as runs of same-frame bytes,
  a run a message; a button press as `[button, down]` on `buttons` at frame 0; its host MIDI at frame 0. Its
  scratch is one bag kept across mode changes, master sync's sub-object dropped outside that mode so coming
  back starts clean. `serialOut` is a feedback wire from its Game Boy: last block's captured bytes, each at
  the frame its core completed it.
- **`rp.controller.session`** runs a `ControllerSession` ([controller/session.ts](../packages/retroplug/src/controller/session.ts))
  for the app its settings name (`controller/apps/index.ts`, the apps' runtime, zod-free): the surface's
  traffic and the block's clock in (its tick derived from ppq at 24 PPQN), its LED bytes out on
  `controllerOut`, and its row launches at frame 0 on `launches` (into the target cart's `rp.lsdj.sync`) or
  `midiOut` (out of the host, to a real Game Boy's Arduinoboy). It takes a device on the block a link first
  reports one. Its settings are what the control plane resolved: it parses nothing. A settings change that
  keeps the app, target, system and app config adopts a new timing table in place, so the playhead does not
  move; any other rebuilds the session around the same predictor, so the song does not restart; an anchor
  is applied once per `anchorSeq`.

**The drift-exact PPQ clock, in JS.** Native owns no `nextTick`/`eachTick` primitive.
[`walkTicks`](../packages/retroplug/src/ticks.ts#L23) is a faithful TS twin of native `PpqUtil::eachTick`:
it walks the ticks at `resolution` ticks a quarter that fall in this block, calling `cb(tick,
offsetSamples)` for each. A node that clocks keeps its `nextTick` on its instance
([nodes/clock.ts](../packages/retroplug/src/script/nodes/clock.ts) `TickClock`), and no system has two
clocking protocols, so it is the counter its system has; across blocks each tick fires exactly once, never
doubled or missed, and a jump of more than a tick (a seek, a loop, a start) resyncs. The transport it reads
is the block's, at the block-start ppq.

**Capacities.** Each output declares its queue
([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts)): `serialIn` and `midi` 4096 messages /
72 KiB: the bytes are what `rp.host.midi-in`'s output holds (64 KiB) with 8 KiB more for a role's own, the
message count twice its 2048, so a SysEx the host
stages whole reaches the system whole and host MIDI is dropped only at that edge; a routing output what
`rp.host.midi-in`'s holds (2048 / 64 KiB), since under send-to-all it carries everything; `bytes` 256 / 1
KiB, `buttons` 64 / 128 bytes, `midiOut` 1024 / 8 KiB, `controllerOut` 256 / 4 KiB, `launches` 64 / 256
bytes. A send past one is dropped and counted (`Engine::droppedMessages`).

**Frame order.** A queue is frame-ordered: a message pushed with an earlier frame than the one before it is
raised to that frame. `rp.lsdj.sync` sends out of order onto `serialIn` in two modes: Arduinoboy MIDI sync
writes a row byte at its note's frame, then START / STOP and its clocks at frame 0, and MI.MAP writes row
bytes at their note's frame, then its `0xFF` clocks at frame 0. Both drive only a Game Boy, whose serial
queue begins a byte only once every byte ahead of it has gone, so raising a byte's frame to the one before
it changes nothing the core sees.

**A throw** in a node's JS is caught by the script host and counted (`Engine::scriptThrows`; the first one
is logged): what the node sent before it stands, a held serial-in run included, and the next block runs
again; the other nodes run as usual. A call that runs past the script host's CPU-time deadline is stopped
the same way, except that it skips the `finally` that sends the run, so that is cleared at the next
`process` instead; a second expiry in a row faults the node, and a fault outlives reshapes (only a rebuild,
or the node removed and added again, clears it).

**Settings without zod.** A node's settings are a zod-free schema
([graph/settingsSchema.ts](../packages/retroplug/src/graph/settingsSchema.ts): `s.object`, `s.enum`,
`s.int`, `s.bool`, `s.number`, `s.string`, `s.array`, `s.json`), each builder parsing and writing its JSON
Schema as its zod twin did, so the bundle every Engine loads into both of its script runtimes holds no zod
(78 KB, where the kernel's was 660 KB, most of it zod). `defineNode` takes any schema with `safeParse` and
`toJSONSchema`, so a zod schema still serves on the control plane. The role's own zod schema stays on the
control plane: the store parses a role's config with it, and that parsed config is the node's settings.
[bundle-no-zod.test.ts](../packages/retroplug/test/script/bundle-no-zod.test.ts), a `pnpm test` case (not a
build gate), fails on a bundle that pulls a zod module, or a role or config module, back in, or that passes 200 KB.

## Store → graph

[`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) places, in document order and ahead of the
systems (the planner breaks ties by document order, so they run first, and a profile build's `apu-render`
span relies on it):

1. `routing`, the `rp.midi.route` node, when the model has a MIDI routing mode (a project always has one;
   a `DirectGraph`, or a `SystemsStore` used without a project, routes none until a mode is set), fed from
   `midi-in.midi`;
2. `controller`, the `rp.controller.session` node, when a controller is enabled, fed from
   `controller-in.midi` and `.connected`, its `controllerOut` to `controller-out`;
3. each system's protocol nodes, in slot order and then the order of its roles, each `sys-<id>/<kind>`
   with the role's parsed config as its settings: what reads MIDI is fed from `routing.sys-<id>`, each
   output wired to the system node's input of the same name, a Game Boy's `serialOut` back into its
   `rp.lsdj.sync` as a feedback wire (one block late: what MI.OUT and master sync decode), and the
   controller's `launches` into the target system's `rp.lsdj.sync` when the target is `"system"`.

Which role kind is placed as which node, and how each type is wired, is one pure, zod-free table,
[graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) (`protocolNodeOf`,
`protocolPipeline`): both stores and a `DirectGraph` read it, with or without a role registry, and a role
with no node is left out, so an edit to it (a model, a palette, an asset override) leaves the document as
it was. `test/system-settings/registry.test.ts` holds it to what `registerDspRoles` registers, and
`test/graph/protocol-nodes.test.ts` to the ports each node module declares and to the bundle's node types;
`golden/build-graph-roles.json` records the documents it makes `buildGraph` build.

**One writer per system input.** Two roles whose nodes write the same system input (a project stored with `mgb`
whose ROM path now holds LSDj gains `lsdj-sync`, both writing `serialIn`; or a hand-edited file naming a kind twice)
would make the planner refuse the whole document (`input-taken`). `protocolPipeline` places the first in role order
and leaves out a later one, and a kind already placed, whole: the role stays on the system, so a save keeps it. On a
load, `adopt` first moves each role the ROM's providers suggest ahead of the earliest role they do not suggest whose
node writes an input its own does, so the role today's ROM calls for is the one placed; the left-out one is logged
once, after the core has built. The serial-out capture and the reported sync latency follow the `lsdj-sync` the
pipeline places (`placedRole`), so a left-out one arms nothing. `DirectGraph.setPipeline` sends the document
without the later role and can report it through a callback.

**Loaded role configs are parsed.** A loaded protocol role's config is parsed by its `RoleType` schema in `adopt`
(so the node's settings, the capture the core is built with, the menus and a save all see the parsed one), and a
repair is logged once with the ROM and each repaired value. A config the schema refuses outright falls back to
what the ROM's providers suggest for the role, then to the schema's defaults. The parse drops a key the schema does
not read, as an edit already did, and names it in the log.

The rest of the document follows from those nodes:

- **Host MIDI out**: `rp.host.midi-out` in its systems form, its inputs in the order the kernel emitted:
  the controller's `midiOut` first (under its resolved target), then each `rp.lsdj.sync`'s in slot order,
  each under its system; with no producer, an empty list and no inputs. The node keeps the host's list
  frame-sorted across its inputs (each message at the later of its frame and the last handed out in the block, as the
  kernel's one queue clamped), which `Engine::midiOut` promises and CLAP and LV2 require.
- **The N8 cart mirror** (`cart`, `rp.n8.cart-mirror`) for the first NES in slot order, fed what that
  system's protocol nodes send its `midi` and `bytes` inputs.
- **The controller** ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts)): its target
  "0 is the first system" is resolved here (`controllerTarget`), so it follows a change of first system;
  with no system its host MIDI is tagged `NO_SYSTEM` (4294967295) and the node takes 0, its none. The
  publisher hands it the app config already parsed by the app's own schema (`parseAppConfig`,
  [controller/registry.ts](../packages/retroplug/src/controller/registry.ts)) and the timing table it read
  from the cart's battery, on the control plane. The `rp.lsdj.sync` node of the cart the MI.MAP app drives
  is forced to `midiMap`, or to `off` when the cart's own SYNC setting is not MI.MAP
  (`controllerSyncOverride`): that node's settings alone, so nothing reaches the `.rplg`.

Where nothing produces for a system input, or no controller is enabled, the input or the host I/O node is
left unwired. The plugin loads nothing for any of this
([pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts)): the graph the project
publishes carries every node and its settings.

## Not yet built / deferred

Only the pieces below are open; the [07-remaining-work.md](07-remaining-work.md) doc is the authoritative
inventory of remaining work.

- **Raw Keyboard mode is the one unbuilt LSDj sync mode.** `rp.lsdj.sync` runs every other mode:
  MidiSync, MidiSyncArduinoboy, MidiMap, KeyboardMidi, MidiPassthrough, and the two host-out directions
  **MidiOut** (ArduinoboyMaster MI.OUT, serial out decoded to host MIDI) and **MasterSync**, with a
  live-apply menu. Raw **Keyboard** (mode 4) still sends nothing: it needs a per-block `keys` feed, an
  input on the node that nothing in the running host would feed yet, and per-block host `buttons` the same.
- **The MI.OUT and master-sync decoders allocate.** Their emitted arrays cost about 3 JS allocations for
  every host MIDI message they decode, the one per-block JS allocation left on a production path; it is
  the decoders' own change, in `lsdjArduinoboy.ts`.
- **kit-patch UI-thread behaviour.** LSDj kit-patch runs entirely on the UI / control thread: it
  compiles the kit (over the native `KitCompiler` primitives) and the `lsdj-assets` role folds it into the
  ROM in memory. It has **no protocol node** and never reaches the audio thread. A role-owned settings UI
  would need the deferred `ui?` render seam.
- **`ui?` render descriptor + extension model.** The `ui?` field on `RoleType` is a labelled-deferred
  seam (a role's own settings UI), and the third-party extension model (registering `RoleType`s, ROM
  providers, and node types from outside the built-ins) is future work.

## Key files

TypeScript ([packages/retroplug/src/](../packages/retroplug/src/)):

- [systemRoles.ts](../packages/retroplug/src/systemRoles.ts): `RoleInstance` / `RoleType` /
  `RoleRegistry` / `RomProvider` / `ConstructCaps`.
- [coreRoles.ts](../packages/retroplug/src/coreRoles.ts): the `sameboy` and `mesen` system-roles.
- [dspRoles.ts](../packages/retroplug/src/dspRoles.ts): the roles that have a protocol node (kind, schema)
  + the LSDj `onConstruct` seed.
- [romProviders.ts](../packages/retroplug/src/romProviders.ts): feature-role attachment by ROM identity.
- [script/nodes/](../packages/retroplug/src/script/nodes): the seven node types, `clock.ts` (the tick
  counter) and `ports.ts` (the capacities); [ticks.ts](../packages/retroplug/src/ticks.ts): `walkTicks`.
- [graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) /
  [graph/controller.ts](../packages/retroplug/src/graph/controller.ts) /
  [graph/buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts): store → graph, the protocol node per
  role, the controller's target and its cart's sync override.
- [graph/settingsSchema.ts](../packages/retroplug/src/graph/settingsSchema.ts) /
  [graph/defineNode.ts](../packages/retroplug/src/graph/defineNode.ts): the zod-free settings schema and the
  node contract.
- [script/bundle.ts](../packages/retroplug/src/script/bundle.ts) /
  [script/register.ts](../packages/retroplug/src/script/register.ts): the bundle every Engine embeds, and how
  a bundle registers its node types.
- [controller/apps/index.ts](../packages/retroplug/src/controller/apps/index.ts) /
  [controller/registry.ts](../packages/retroplug/src/controller/registry.ts): the controller apps' runtime,
  and their labels and schemas for the control plane.
- [appHost.ts](../packages/retroplug/src/appHost.ts): `buildAppRegistry`.

Native ([packages/native/src/](../packages/native/src/)):

- [ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp) /
  [ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp): the script-node runtime the
  protocol nodes run in.
- [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp): each system node's inputs,
  applied before its core runs.
- [HostIoNodes.cpp](../packages/native/src/host/graph/HostIoNodes.cpp): the host I/O nodes the protocol
  nodes read from and send to, and the N8 cart mirror.
- [EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp#L165): `applyRoleConfig`
  (system-role only).
