# 04 — Roles & the DSP kernel

Roles are how RetroPlug attaches *meaning* to a system without teaching the C++ cores anything.
A role is a `{ kind, config }` pair keyed by a string; the core treats `config` as an opaque
per-kind blob. Everything specific to a system — a backend's emulator knobs, or an optional
feature like LSDj sync or mGB passthrough — is a role, so the cores stay free of backend/LSDj/mGB
knowledge. Cores are built **bare** (`setSniffDefaultRoles(false)`); all feature behaviour lives in
TypeScript and runs inside the **DSP role kernel**.

This is the sharpest expression of the thesis (*"native owns bytes and cores; TypeScript owns
meaning"*): the identical TS kernel code runs under the mock (a `CollectingSink`) in unit tests and
on the audio thread (bound C sink thunks) in the real hosts, and native carries the kernel bytecode
and system structure without interpreting either.

This doc covers the role model and the DSP kernel end to end. For the runtime plumbing the kernel
rides on — the command ring, the two QuickJS runtimes, the audio-thread ownership model — see
[01-architecture.md](01-architecture.md); this doc references those concepts rather than
re-explaining them. For the C++ `DspRuntime` runner that executes the kernel bytecode, see
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
([systemRoles.ts:41](../packages/retroplug/src/systemRoles.ts#L41)):

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

[`RoleRegistry`](../packages/retroplug/src/systemRoles.ts#L85) holds a `Map<kind, RoleType>`
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
the **control-plane** registry only; the bare DSP-context bundle never sees them.

### `ConstructCaps` + `onConstruct` hooks

`onConstruct` runs at construct time for each role actually attached to a system (so it is inherently
ROM-gated), transforming the resolved `ConstructSpec`. It sees only a narrow `ConstructCaps` slice —
`{ savFromJson, fileExists }` — which the full `Backend` satisfies structurally, so the store passes
itself.

The one hook today is LSDj's empty-sav seed
([dspRoles.ts:26](../packages/retroplug/src/dspRoles.ts#L26)): a fresh LSDj cart with no
save data runs a 12–15 s cartridge self-test on boot. When nothing else will seed the battery (no
savestate, no SRAM blob, no on-disk `.sav` for native to load) the hook hands it a valid empty sav
via `savFromJson("{}")` so LSDj boots straight to the song screen. It is additive — the spec is
returned untouched when real save data is present.

### Where derivation happens in the store

[SystemsStore](../packages/retroplug/src/systemsStore.ts) drives the registry:
`defaultRoles(...)` reads a `ROLE_HEADER_LEN = 0x150` header prefix and calls
`registry.defaultRoles(...)`
([systemsStore.ts:492](../packages/retroplug/src/systemsStore.ts#L492)); `applyConstructHooks`
folds each attached role's `onConstruct` over the spec before the build call
([systemsStore.ts:503](../packages/retroplug/src/systemsStore.ts#L503)). See
[03-ts-layer.md](03-ts-layer.md) for the surrounding store idioms.

## The built-in roles

Three registration functions populate the control-plane registry
([appHost.ts](../packages/retroplug/src/appHost.ts) `buildAppRegistry`):
`registerCoreRoles` + `registerDspRoles` + `registerRomProviders`.

**System roles** ([coreRoles.ts](../packages/retroplug/src/coreRoles.ts)) — keyed by core
value, ranges mirroring the native enums:

| Role | Config (zod, defaults) |
|---|---|
| `sameboy` | `model` 0–13 (CgbC=9), `highpass` 0–2, `linkGroupId` 0–255, `fastBoot` bool (true) |

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
| a **system-role** config edit | `applyRoleConfig(id, kind, config)` | the live core, `sameboy` only |
| a **feature-role** config edit | *(none)* | only the DSP kernel, as part of the re-pushed structure |

[`SystemsStore.setRoleConfig`](../packages/retroplug/src/systemsStore.ts#L329) merges the
partial, re-parses through the role schema, and calls `backend.applyRoleConfig` **only when the role's
`category === "system"`**. On the native side
[`EngineRpcService::applyRoleConfig`](../packages/native/src/host/rpc/EngineRpcService.cpp#L111)
accepts `kind == "sameboy"` only, decodes the whole role config, and pushes Model/Highpass/LinkGroup/
FastBoot as guarded config fields (an unchanged field is a no-op, so moving `highpass` never triggers
a spurious model restart).

A **feature**-role config edit takes a different path: `setRoleConfig` updates TS state and calls
`markDirty`, which fires the systems `onChange` → the project's `onSystemsChange`
([projectStore.ts:62](../packages/retroplug/src/projectStore.ts#L62)) → `syncDspFromStore`,
re-pushing the whole `KernelStructure` (its config rides inside the JSON). The core is never told. (A
menu/UI to *trigger* an LSDj-mode edit live is still a gap — see
[07-remaining-work.md](07-remaining-work.md).)

## The DSP kernel

[dspKernel.ts](../packages/retroplug/src/dspKernel.ts) is pure TS with no `Backend`
dependency. The host hands `processBlock` everything it collected for one audio block; project-scope
behaviours fan the input to systems, then each system's ordered pipeline of behaviours reads its
inputs and writes frame-tagged byte sinks.

### Structure vs per-block input

| Type | Pushed | Contents |
|---|---|---|
| `KernelStructure` | once, via `setSystems` | `{ project?: RoleInstance[], systems: { id, pipeline: RoleInstance[] }[] }` |
| `BlockInput` | every block, via `processBlock` | `BlockInfo` (`frames`, `sampleRate`, `tempo`, `ppqStart`, `transport`) + `midiIn` (global), `buttons`, `keys` |

**System order is authoritative**: positional MIDI routing maps a channel to a system by its index in
`systems`, so the list defines both the set and the order. `DspKernel` keeps ONE persistent `Block`
view and overwrites its dynamic fields each block — no per-block allocation.

### Execution order

[`DspKernel.processBlock`](../packages/retroplug/src/dspKernel.ts#L200):

1. **Project scope first** — each project-scope stage (routing) runs, populating a `routed`
   `Map<systemId, MidiEvent[]>` from the global `midiIn`.
2. **System scope** — for each system in order, filter this system's `keys`/`buttons` once, then run
   each pipeline stage whose `RoleType` has a `dsp` and matching scope. A stage whose roleType is
   missing, project-scope, or has no `dsp` is **skipped** — so a system's `sameboy` system-role rides
   the pipeline as harmless dead bytes and needs no filtering upstream.

`setSystems` also **prunes per-system tick state for ids no longer present**, so a removed-then-readded
id starts a fresh clock instead of resuming mid-count.

### The byte-sink ABI

A behaviour never carries a `sys` argument; it writes through a per-system `SystemCtx`
([dspKernel.ts:103](../packages/retroplug/src/dspKernel.ts#L103)) whose sinks are scoped to
the system id. The kernel forwards each call to an injected `SinkTarget`:

| Sink | Signature (on `SystemCtx`) | Direction |
|---|---|---|
| `pushSerialIn` | `(frame, byte)` | → the system's serial input (LSDj clock, mGB passthrough) |
| `emitMidiOut` | `(frame, data)` | → host MIDI out |
| `pressButton` | `(button, down)` | → role-generated joypad transition (distinct from a UI tap) |
| `eachTick` | `(resolution, cb)` | walk the PPQ ticks in this block (see below) |

`SinkTarget` ([dspKernel.ts:73](../packages/retroplug/src/dspKernel.ts#L73)) is the
system-addressed form — `pushSerialIn(system, frame, byte)`, `emitMidiOut(system, frame, data)`,
`pressButton(system, frame, button, down)`, optional `reset()`. Under the mock a `CollectingSink`
gathers calls into a `Sinks` object the assertions read; natively the target's methods **are** the
bound C thunks, so bytes cross as scalars with no intermediate JS arrays. (Role-generated
`pressButton` is emitted at `frame 0`.)

### The drift-exact PPQ clock — in JS

The tick clock lives **entirely in the kernel**; native owns no `nextTick`/`eachTick` primitive.
[`walkTicks`](../packages/retroplug/src/dspKernel.ts#L131) is a faithful TS twin of native
`PpqUtil::eachTick`: it walks the ticks at `resolution` ticks/quarter that fall in this block, calling
`cb(tick, offsetSamples)` for each. The caller-owned `nextTick` persists across blocks (the kernel
stores it per system id), so the clock is drift-free at block edges — each tick fires exactly once,
never double or missed, and a >1-tick transport jump (seek/loop/start) resyncs. `lsdj-sync` uses it at
24 PPQN.

## `kernelProjection` — store → kernel

[projectKernelStructure(views, midiRouting)](../packages/retroplug/src/kernelProjection.ts#L16)
is the single seam that turns "what the app has" into "what the DSP runs":

- each system's `roles` map straight into its `pipeline`, order preserved;
- the project-scope `{ kind: "midi-routing", config: { mode: midiRouting } }` role is **synthesized**
  from the project's MIDI-routing setting (it lives in project settings, not on any system).

It is pure and **registry-free** — the kernel self-guards scope, so a system's backend `sameboy` role
projects through as dead bytes without needing to be filtered here.
[`syncDspFromStore`](../packages/retroplug/src/appHost.ts#L32) wraps this and pushes the
result to the DSP runtime; it is installed on `ProjectStore.onSystemsChange`, so every structural edit
(and every feature-role config edit) re-drives the kernel.

## Running on the bare DSP context

The kernel runs inside a **second, bare QuickJS context** (`DspRuntime`, no txiki) on the audio
thread — distinct from the control-plane runtime; see [01-architecture.md](01-architecture.md) for the
two-runtimes model. Three artifacts bridge control plane to that context:

**Control-plane client** —
[dspRuntime.ts](../packages/retroplug/src/dspRuntime.ts). `createDspRuntime()` returns a
`DspRuntimeClient` with `compileScript(source) → bytecode`, `loadKernel(bytecode)`, and
`setSystems(struct)`. It rides the same `globalThis[Symbol.for("plugin")].__rpcSend` channel as
`realBackend`, but is a **distinct capability from `Backend`** (it never joins that interface, so the
mock stays clean). The seam is bytes only: the kernel crosses as QuickJS **bytecode**, the structure
as a **JSON string** (`dspSetSystems`); a JS object never crosses. (Bytecode input crosses as a plain
`number[]` because reflect-cpp's byte reader rejects a typed array.)

**Bare-context entry** —
[dspKernelBundle.ts](../packages/retroplug/src/dspKernelBundle.ts). esbuild bundles this
into one IIFE; native compiles it to bytecode. It builds its **own** registry (**`dspRoles` only** —
no core roles, no ROM providers), wires the host-bound `pushSerialIn`/`emitMidiOut`/`pressButton`
global thunks into a `SinkTarget`, and exposes the two globals native calls: `setSystems(json)` (once
per structure change) and `processBlock(input)` (once per audio block).

**Native runner** — `DspRuntime` (C++, see [02-native-host.md](02-native-host.md)) loads the bytecode
(`loadKernel` = `JS_ReadObject` + `JS_EvalFunction`; re-loading hot-swaps the kernel), binds the three
sink thunks, and drives `processBlock` per block.
[`Engine::processBlock`](../packages/native/src/host/engine/Engine.cpp#L74) wires it in: when the DSP
stage is active it builds a `BlockInfo` at the block-start `ppq_`, runs the kernel, then fans the
system-addressed sinks to cores — `serialIn_` → each addressed core's serial FIFO, `buttonOut_` →
`pressButton` — before the block renders. The kernel's host `midiOut_` is drained by the plugin after
the block.

**Two registries, one kernel.** The control-plane registry (core + DSP + ROM providers) drives the
stores and derives roles; the bare bundle's registry holds only the DSP behaviours. This is safe
because `kernelProjection` is registry-free and the kernel self-guards scope, so the structure a
system projects (including its `sameboy` system-role) is interpreted identically whether or not the
bundle knows that kind.

**Plugin wiring.** [pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts)
composes it: `dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__))` (the bundle injected as a build
define), then `project.setOnSystemsChange(() => syncDspFromStore(project, dsp))`.

## Not yet built / deferred

Only the pieces below are open; the [07-remaining-work.md](07-remaining-work.md) doc is the authoritative
inventory of remaining work.

- **Raw Keyboard mode is the one unbuilt LSDj sync mode.** `lsdj-sync` authors every other mode —
  MidiSync, MidiSyncArduinoboy, MidiMap, KeyboardMidi, MidiPassthrough, and the two host-out
  directions **MidiOut** (ArduinoboyMaster MI.OUT, `emulator serial-out → decode → emitMidiOut`) and
  **MasterSync** — backed by the `ctx.state` scratch bag and a live-apply menu. Raw **Keyboard**
  (mode 4) still emits nothing: it needs the per-block `keys` feed (below).
- **Per-block buttons/keys aren't fed yet.** The kernel carries `buttons`/`keys` inputs and per-system
  filtering, but the live `Engine` path feeds only host MIDI (`kNoButtons`/`kNoKeys`), so the ABI is
  present but not marshalled from the running host — which is what blocks raw Keyboard mode.
- **kit-patch UI-thread behaviour.** LSDj kit-patch runs entirely on the UI / control thread: it
  compiles the kit (over the native `KitCompiler` primitives) and writes the already-patched memory
  regions **into the core**. It has **no DSP-kernel component** and does not use the byte-sink ABI
  above — the DSP kernel never sees kit data. It needs the deferred `ui?` render seam and a
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
- [dspKernel.ts](../packages/retroplug/src/dspKernel.ts) — `DspKernel`, `SystemCtx`, the
  sink ABI, `walkTicks`.
- [kernelProjection.ts](../packages/retroplug/src/kernelProjection.ts) — store →
  `KernelStructure`.
- [dspRuntime.ts](../packages/retroplug/src/dspRuntime.ts) /
  [dspKernelBundle.ts](../packages/retroplug/src/dspKernelBundle.ts) — the DSP-context
  client + bare-context entry.
- [appHost.ts](../packages/retroplug/src/appHost.ts) — `buildAppRegistry` +
  `syncDspFromStore`.

Native ([packages/native/src/](../packages/native/src/)):

- [DspRuntime.hpp](../packages/native/src/host/dsp/DspRuntime.hpp) /
  [DspRuntime.cpp](../packages/native/src/host/dsp/DspRuntime.cpp) — the bare-context runner + sink
  thunks.
- [Engine.cpp](../packages/native/src/host/engine/Engine.cpp#L74) — per-block wiring + sink-to-core fan.
- [EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp#L111) — `applyRoleConfig`
  (system-role only), `compileScript` / `dspLoadKernel` / `dspSetSystems`.
