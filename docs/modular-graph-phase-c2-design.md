# Phase C2: live knobs as system node settings and params

**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** designed

A system's live knobs (a Game Boy's model, highpass and display group, a NES's region and accuracy switches, every
system's gain) reach its core today through their own path beside the graph: TS calls `applyRoleConfig` /
`applySystemSetting`, native pushes a `ConfigField` command, and `Engine::applyConfigField` casts the core to its
concrete type and applies the field. LSDj's serial-out capture mode goes the same way through `setSerialOutCapture`,
apart from the document that runs the decoder reading what it captures. This phase moves the knobs into the graph:
each becomes either a **setting** of its system node, in the published document, or a **param** of it, its value in
the document and its live edits through `setParam`. The side path and its casts go, an LSDj mode switch becomes atomic,
and a node graph UI would show exactly the knobs worth playing as a system node's inputs. It is Phase C's second piece
([modular-graph-design.md](modular-graph-design.md) section 8, [spec/07-remaining-work.md](../spec/07-remaining-work.md)
section 5: "The capture mode as a Game Boy node setting"), after C1's script node packs
([modular-graph-phase-c1-design.md](modular-graph-phase-c1-design.md)).

## 1. Before C2

- **The stores** ([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)): `setGain` and
  `setReloadOnRomChange` call `backend.applySystemSetting(id, key, value)`; an edit of a `sameboy` or `mesen` role
  config calls `backend.applyRoleConfig(id, kind, config)` with the whole config, its string enums turned to ordinals
  (`roleConfigForNative`); an `lsdj-sync` edit calls `backend.setSerialOutCapture(id, mode)` (`syncSerialOutCapture`,
  from the stored mode: `serialOutCaptureFor`). A core is built with its capture already armed, through the build
  spec's `serialOutCapture`. Only a link-group change publishes, since the cable is wiring.
- **Native** ([EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp)): each of those pushes one
  `ConfigField` per field through the invoker onto the command ring, and the block thread's
  [Engine::applyConfigField](../packages/native/src/host/engine/Engine.cpp) applies it: the two universal fields
  through `SystemBase`, the rest by `dynamic_cast` to `MesenNesSystem` or `SameBoySystem`, each value-guarded (the store
  resends the whole config, so an unguarded apply would restart a core when a sibling knob moved). `reloadOnRomChange`
  lands on a core flag nothing native reads: the file watcher that acts on it is TS's
  ([fileWatcher.ts](../packages/retroplug/src/fileWatcher.ts)).
- **A system node's settings** ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)) are `{"core":
  <id>}` alone, and `needs_rebuild` compares them, so the type cannot declare stateless prepare (node.hpp: a
  `needs_rebuild` reads the instance's current settings), and a settings edit to one would make `setGraph` wait for the
  audio thread to adopt it.
- **The graph's params** are built and unused above the engine: a document node's `params` (a number, an enum label or
  a bool), `GraphHost::setParam(node, param, value, frame)` (clamped, rounded, applied at a frame of the next block, no
  plan), and node.hpp's `float_param` / `int_param` / `bool_param` / `enum_param`.
- **Two known disagreements.** A live switch from MI.OUT to master sync can decode one block captured under `drive` as
  clocks, since the capture and the decoder's mode change through two paths
  ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md) section 10, its first accepted limit). And the
  controller's MI.MAP override runs an LSDj's nodes in `midiMap` while its capture still follows the stored mode
  (spec/07 section 5).

## 2. Decisions taken

The user's, on 2026-10-30:

- **Settings and params, as the design's section 2.3 has them** (approach B), rather than every knob a setting (every
  edit a publish and a plan, a gain drag dozens a second) or every knob a param (the capture and the model unable to
  change with a plan).
- **The rule: a param is something worth playing, automating or modulating; everything else is a setting**, even a knob
  that could change live without a restart. In the graph's model every param is also a `control` input (design section
  2.3), so a node graph UI would show each as a knob with an inlet, and a wire would drive it; settings live in an
  inspector. Display knobs, accuracy switches and latencies are configuration, not inlets.
- **Highpass is a setting.**

## 3. The split

| System node | Settings (in the document; an edit publishes) | Params (each an input; an edit is `setParam`) |
|---|---|---|
| `rp.sameboy.gb` | `core`, `model`, `fastBoot`, `highpass`, `colorCorrection`, `dmgPalette`, `lightTemperature`, `serialOutCapture` | `gain` |
| `rp.mesen.nes` | `core`, `region`, `removeSpriteLimit`, `apuLatencyMs`, `s5bNoise`, `mmc5PhaseReset` | `gain`, `expansionVolume` |
| `rp.mesen.gba`, `rp.mesen.sms` | `core` | `gain` |

- `gain`: dB, -90 to 12, default 0 (the store's `GAIN_MIN` / `GAIN_MAX`). `expansionVolume`: percent of unity, 0 to
  200, default 100. `serialOutCapture`: `off`, `listen` (master sync) or `drive` (MI.OUT), default `off`.
- **Enums travel as labels**, never ordinals (design section 2.4): the labels the role configs already spell
  (`"cgbC"`, `"removeDcOffset"`, `"n8"`), which are what a `.rplg` saves, so the document and the saved project agree.
  C++ maps them to its enums; the goldens pin the spelling.
- **What stays outside C2:** the construct-time fields (SMS `enableFm`, NES `sdRoot`, the FIFO profile and its two
  overrides, the N8 link and its round trip, `channelExportMode`) stay in the build spec until C3 builds cores in their
  nodes; `buildCore`'s facts (the export mode, the link group) stay casts until C4; `linkGroupId` stays a TS-side field
  that only `buildGraph`'s wiring reads.

## 4. The native side

- **Each system node type's settings struct grows its table's fields**, and the types declare their params (node.hpp's
  `float_param`), so the descriptor lists them and the planner checks and clamps a document's values.
- **When a value reaches its core.** `on_settings` only copies (node.hpp: never allocate there), so the node applies
  what moved at the start of its next block, before `prepareForBlock`, and everything once at activation, right after it
  takes its core and before that core first runs. Each field is value-guarded, as `applyConfigField` was: a palette
  change reapplies the display group and restarts nothing, a model change restarts the core and a region change resets
  it, on the block thread as before. A param's control stream gives its latest value each block, applied value-guarded
  before `prepareForBlock`: a change lands at the block's start (at most a block late, as the command ring's did), and
  the core smooths gain as it does now.
- **The atomic switch.** `serialOutCapture` reaches the Game Boy at the start of the first block of the plan that
  carries it, the block in which `rp.lsdj.serial-out` first runs in the new mode, so no byte captured in one mode is
  decoded in the other.
- **`setGraph` never waits on a system node.** node.hpp gains a `rebuild_key(const Settings&)` hook: a value only a
  rebuild can change, compared against the key the instance was created with, which no commit writes, so a type with it
  may declare `stateless_prepare` where `needs_rebuild` rules that out. The system nodes key on `core` and declare
  stateless prepare, so a settings edit (a mode switch, a palette) never holds the control thread waiting for an audio
  thread that may not be running.
- **`Engine::setParam(nodeId, paramId, value)`** on the control thread, over `GraphHost::setParam`: the command ring,
  flushed inline while quiescent, the value clamped and rounded to the param's range; false for a node or param the
  running plan does not have (a node the next publish adds takes its value from the document).
- **Deleted:** `ConfigField`, `DspCommand::SetConfigField`, `EngineInvoker::applyConfigField` and
  `Engine::applyConfigField` with its casts; the `applySystemSetting`, `applyRoleConfig` and `setSerialOutCapture`
  RPCs; the build spec's `serialOutCapture` and `buildCore`'s arming of it (activation applies it before the core first
  runs, and a parked core never runs); `SystemBase::setRomReload` / `wantsRomReload` and the configs'
  `reloadOnRomChange`.

## 5. The TS side

- **`buildGraph`** writes each system node's settings (`core`, its role config's setting fields, and on a Game Boy
  `serialOutCapture`) and params (`gain` from the system's settings, `expansionVolume` from a NES's `mesen` config). The
  capture is computed from the mode the placed `lsdj-sync` node actually runs, after the controller's MI.MAP override
  (`controllerSyncOverride`): `drive` for MI.OUT, `listen` for master sync, `off` otherwise. `serialOutCaptureFor` moves
  from the store into the graph layer beside the override, which retires the spec/07 item where a forced node and its
  capture disagreed. The B1 translator goldens and the role goldens are re-recorded, the diff exactly the new settings
  and params on system nodes.
- **The stores.** A gain edit, or a NES's expansion volume, updates the store's value and calls
  `backend.setParam("sys-<id>", ...)`, with no publish; the next publish, for whatever reason, carries the same value.
  An edit of a setting field of `sameboy` / `mesen` updates the role config and publishes, as a link-group edit
  already does. A construct-time field's edit stays as it is (a rebuild through `reset(id)`). An `lsdj-sync` edit
  already publishes, and the capture rides that document; `syncSerialOutCapture` goes. `reloadOnRomChange` stays a
  TS-only setting with no backend call.
- **`Backend`** gains `setParam(nodeId, paramId, value): boolean` (the real backend over its RPC, on the `emulator`
  facet; the mock records each call), and loses `applySystemSetting`, `applyRoleConfig`, `setSerialOutCapture` and the
  build spec's `serialOutCapture`. `DirectGraph` loses `applyRoleConfig` and gains `setParam`.
- **Callers moved:** the `test-native` files, mock-tier tests and the CLI session (`analyze-lsdj-sync`) that call the
  old methods set a mode or a knob the way the app does, through the store or the published document. The parity rig
  (`render-blocks`) is among them, and keeps its recorded output.

## 6. Tests

- **`retroplug-graph-test`:** a node with `rebuild_key` declares stateless prepare; a change of key rebuilds and a
  same-key change swaps; `setGraph` never waits on its commit.
- **`retroplug-engine-graph-test`:** every setting and param reaches its core (through a plan swap or `setParam`, at the
  next block); the value guards (a palette, highpass or sprite-limit change neither restarts nor resets the core, a model
  change restarts it); everything applied at activation, before the core's first block, the capture included; the
  atomic switch (one `setGraph` changing an LSDj's mode gives the Game Boy its capture in the block the decoder first
  runs in the new mode); `Engine::setParam` (unknown node or param refused, values clamped, a later publish of the same
  value moving nothing); no allocation on the block thread for the params path; the new cases under ThreadSanitizer.
- **`pnpm test`:** `buildGraph`'s settings and params for every system type, the capture for each mode and `off` under
  the controller's forced MI.MAP; the store's gain and expansion volume calling `setParam` without publishing, a setting
  edit publishing, an `lsdj-sync` edit publishing its capture; the goldens re-recorded.
- **`test:native`:** `app-settings` and `app-mesen-settings` drive every knob through the real host; the LSDj and
  model-switch tests set their modes through the store.
- **Parity:** knobs reach the core at a block's start, where the command ring delivered them, so strict parity must
  hold unchanged; a case that moves is a finding to explain before anything is re-recorded.

## 7. Docs

spec/01, spec/02 (the system nodes, the command ring's fields, the RPC table), spec/03 (the `Backend` table), spec/04
(the capture), spec/06, spec/07 section 5 (the capture item and the MI.MAP item closed, what C2 leaves);
[modular-graph-design.md](modular-graph-design.md) sections 6.3, 6.5, 7.5 and 8; the master-sync fix design's section
10 first limit marked retired; AGENTS.md where a test file's description changes.

## 8. Landing

Five commits, each green on its own:

1. **node.hpp's `rebuild_key`**, with its graph-layer tests; no existing node changes.
2. **System nodes take settings and params, and `Engine::setParam`**; the old path still works beside them.
3. **TS switches over:** `buildGraph`'s settings, params and capture, the stores' `setParam` and publishes,
   `Backend.setParam` over its RPC, the goldens re-recorded.
4. **The old path deleted:** `ConfigField`, `applyConfigField`, the three RPCs and `Backend` methods, the build spec's
   capture, the reload flag; the `test-native` callers and the CLI session moved.
5. **The docs.**

As for C1, a prototype is built first in these five commits on a local branch, and the plan is written from it. The
prototype answers:

- How C++ maps the role configs' labels to its enums in a settings struct (reflect-cpp's enum names differ in case),
  without a second spelling.
- Whether a model or region change applied at a block's start, rather than in the command ring's drain, moves any
  parity case (it should not: both run before the block's first step).
- Whether every caller of the old methods can set its knob through the store, or a test needs `setParam` or a document
  of its own.

## 9. Left as it is

- **DAW automation of node params** (the plugin's parameters mapped onto `setParam`): params make it possible; it is its
  own work.
- **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the forced
  one.
- **The construct-time fields and `buildCore`'s facts:** C3 and C4.
