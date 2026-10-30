# Phase C2: live knobs as system node settings and params

**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** built: section 8's five commits; what building
them changed is in the sections it changed, and what it found is section 10

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

- `gain`: dB, -90 to 12, default 0 (the store's `GAIN_MIN` / `GAIN_MAX`). `expansionVolume`: whole percent of unity
  (an int param, as the core takes it), 0 to 200, default 100. `serialOutCapture`: `off`, `listen` (master sync) or
  `drive` (MI.OUT), default `off`.
- **Enums travel as labels**, never ordinals (design section 2.4): the labels the role configs already spell
  (`"cgbC"`, `"removeDcOffset"`, `"n8"`), which are what a `.rplg` saves, so the document and the saved project agree.
  C++ maps them to its enums (section 8's first question says how); the goldens pin the spelling. An ordinal, another
  spelling, or a setting's number out of its range (`lightTemperature` -1 to 1, `apuLatencyMs` 0.25 to 6) refuses the
  settings, never clamped, as a document's bad value always has; a param's value is clamped, as the planner clamps
  every param.
- **Every setting is optional.** Absent, the node leaves its core as it was built, or as the last document that
  carried the field left it. So a Game Boy's node always carries its capture, `off` included, since one left out would
  leave an armed capture armed; and a system with no core config (a `DirectGraph`'s until it is given one, the B1
  translator goldens' models) carries `core` and its capture alone. A param is optional too, but absent means its
  default: a node's gain starts at 0 dB and a NES's expansion volume at 100%, at its first block.
- **What stays outside C2:** the construct-time fields (SMS `enableFm`, NES `sdRoot`, the FIFO profile and its two
  overrides, the N8 link and its round trip, `channelExportMode`) stay in the build spec until C3 builds cores in their
  nodes; `buildCore`'s facts (the export mode, the link group) stay casts until C4; `linkGroupId` stays a TS-side field
  that only `buildGraph`'s wiring reads.

## 4. The native side

- **Each system node type's settings struct grows its table's fields**, each optional (section 3), and the types
  declare their params (node.hpp's `float_param`, and `int_param` for `expansionVolume`), so the descriptor lists them
  and the planner checks and clamps a document's values.
- **When a value reaches its core.** `on_settings` only copies (node.hpp: never allocate there; the node keeps a
  pointer to the committed settings), so the node applies what moved at the start of its next block, before its inputs
  and `prepareForBlock`, and every setting present once at activation, right after it takes its core and before that
  core first runs. A setting thus lands at the node's next prepare, after any host command queued behind its plan
  (the host drains its ring before a block), so a caller that must act on the restarted core renders a block first.
  Each field is guarded twice: `moved` compares it with what the node applied last (a document's unchanged field is
  not applied again, even where the core was changed by another path) and the core's own setter compares it with what
  the core holds where it has one (`setFastBoot` and a NES's `setRemoveSpriteLimit` write unconditionally). A palette
  change reapplies the display group and restarts nothing, a model change restarts the core and a region change resets
  it, on the block thread as before. A Game Boy applies its fast boot first, which waits for the next restart and so
  is what a model's restart boots with, then the rest in the store's role-config order, the capture last (section 10
  says why the fast boot moved). A param's control stream gives its latest value each block, applied value-guarded after
  the settings and before the inputs: a change lands at the block's start (at most a block late, as the command
  ring's did). A value a wire drives is clamped to the param's range by the node, from the same constants the
  declaration takes its bounds from (the planner and `setParam` clamp a document's or a live edit's). Every param
  reaches the core at the node's first block, not at activation: a node reads its params only in a block (the host
  binds its control streams there, landing the document's values as it adopts the plan), and a core runs nothing
  before its node's first block. That first value is snapped into the core, whose gain smoother starts at 0 dB, so it
  plays its first sample at the document's level; every later gain edit ramps, as it always did.
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
  last published plan does not have (a node the next publish adds takes its value from the document), a NaN, a param
  a wire drives, or a full ring.
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
  capture disagreed. A setting value the node would refuse (another label, a number out of its range, another type) is
  left out rather than sent, the core keeping what it has (section 10). `GraphSystem` gains two optional fields,
  `coreConfig` (the config of the role whose kind is the core) and `gainDb`, which the store's `graphSystems()` fills,
  and `systemKnobs` is the one table of which core-config field is a setting and which a param. The B1 translator
  goldens and the role goldens are re-recorded, the diff exactly the new settings and params on system nodes: 161 of
  the B1 goldens' 238 documents changed, by the capture alone, since their models carry no core config or gain, and 30
  of the 37 role golden cases.
- **The stores.** A gain edit, or a NES's expansion volume, updates the store's value and calls
  `backend.setParam("sys-<id>", ...)`, with no publish; the next publish, for whatever reason, carries the same value,
  and the store hands the graph the systems it just changed (`setOnLiveParam`) so the graph's model is that value too,
  which a render's shape republish builds from.
  An edit of a setting field of `sameboy` / `mesen` updates the role config and publishes, as a link-group edit
  already does. The store tells them apart by the system node's knobs before and after the edit (`systemKnobs`): a
  moved setting publishes, a moved param alone is `setParam`, and an edit moving both publishes, the document carrying
  the param. A construct-time field's edit stays as it is (a rebuild through `reset(id)`). An `lsdj-sync` edit
  already publishes, and the capture rides that document; `syncSerialOutCapture` goes. `reloadOnRomChange` stays a
  TS-only setting with no backend call.
- **`Backend`** gains `setParam(nodeId, paramId, value): boolean` (the real backend over its RPC, on the `emulator`
  facet; the mock records each call), and loses `applySystemSetting`, `applyRoleConfig`, `setSerialOutCapture` and the
  build spec's `serialOutCapture`. `DirectGraph` loses `applyRoleConfig` and gains `setParam` and `setCoreConfig(id,
  partial)`: a core config in the labels the store's role spells, merged over what it gave the system before and
  published (a Game Boy's `linkGroupId` its link cable), which refuses, changing nothing, a field its node does not
  take (section 8's third question). A live `setParam` also gives the graph the model it left, so a shape change
  builds what the node runs and an edit that rebuilds exactly the last document sent is sent rather than skipped.
- **Callers moved:** the `test-native` files, mock-tier tests and the CLI session (`analyze-lsdj-sync`) that call the
  old methods set a mode or a knob the way the app does, through the store or the published document. The parity rig
  (`render-blocks`) is among them; its first-block gain cases moved with the later snap and were re-recorded
  (section 10).

## 6. Tests

- **`retroplug-graph-test`:** a node with `rebuild_key` declares stateless prepare; a change of key rebuilds and a
  same-key change swaps; `setGraph` never waits on its commit.
- **`retroplug-engine-graph-test`:** every setting and param reaches its core (through a plan swap or `setParam`, at the
  next block); the value guards (a palette, highpass or sprite-limit change neither restarts nor resets the core, a model
  change restarts it); every setting applied at activation, before the core's first block, the capture included (a
  param at that first block); the atomic switch (one `setGraph` changing an LSDj's mode gives the Game Boy its capture
  in the block the decoder first runs in the new mode); `Engine::setParam` (unknown node or param refused, values
  clamped, an integer value decoded, a later publish of the same value moving nothing); a live `setParam` is the
  value a later document compares against, and a reshaped plan carries it; a knob a wire drives is clamped to its
  range; a node's first block plays its gain at
  the document's level, snapped rather than ramped from 0 dB; the restart order after a host command sent behind a
  plan, and both layers of a knob's guard; no allocation on the block thread for the params path or the knobs a Game
  Boy applies in place; the new cases under ThreadSanitizer. And every system node of every document in `buildGraph`'s
  goldens names only the settings fields its type's schema lists, and the role goldens' every-node model places a GBA,
  whose gain param reaches a real core (section 10).
- **`pnpm test`:** `buildGraph`'s settings and params for every system type, the capture for each mode and `off` under
  the controller's forced MI.MAP; the store's gain and expansion volume calling `setParam` without publishing, a setting
  edit publishing, an `lsdj-sync` edit publishing its capture; the goldens re-recorded.
- **`test:native`:** `app-settings`, `app-mesen-settings`, `app-sameboy-display`, `app-construct-settings`,
  `cartridge-accuracy` and `expansion-volume` drive the knobs through the real host; `param-republish` holds a
  document's value after a live edit moved the node, through the store and `DirectGraph.setCoreConfig`; the LSDj and
  model-switch tests set their modes through the store, or a `DirectGraph`'s `setCoreConfig` and `setPipeline`; and
  `lsdj-capture-settings` (it was `lsdj-midiout-build-flag`, which tested the build spec's capture) holds an LSDj
  adopted in MI.OUT or master sync to decode from the first document that places it, with no capture in its build
  spec.
- **Parity:** knobs reach the core at a block's start, where the command ring delivered them, so strict parity held
  unchanged through C2. The exception is a node whose first block carries a non-zero gain: it now starts at that level
  instead of ramping from 0 dB, so those cases moved and were re-recorded by the user's decision (section 10). Any
  other case that moves is a finding to explain before anything is re-recorded.

## 7. Docs

spec/01, spec/02 (the system nodes, the command ring's fields, the RPC table), spec/03 (the `Backend` table), spec/04
(the capture), spec/06, spec/07 section 5 (the capture item and the MI.MAP item closed, what C2 leaves);
[modular-graph-design.md](modular-graph-design.md) sections 6.3, 6.5, 7.5 and 8; the master-sync fix design's section
10 first limit marked retired; AGENTS.md where a test file's description changes.

## 8. Landing

Five commits, each green on its own:

1. **node.hpp's `rebuild_key`**, with its graph-layer tests; no existing node changes.
2. **System nodes take settings, and `Engine::setParam`**; the old path still works beside them.
3. **TS switches over, and the system nodes' params:** `buildGraph`'s settings, params and capture, the stores'
   `setParam` and publishes, `Backend.setParam` over its RPC, the goldens re-recorded. The params landed here rather
   than in the second commit: a node gives its core its params' values from its first block, so params placed while
   no document carried a value would have given every core their defaults, over the gain and expansion volume the old
   path or the build had set.
4. **The old path deleted:** `ConfigField`, `applyConfigField`, the three RPCs and `Backend` methods, the build spec's
   capture, the reload flag; the `test-native` callers and the CLI session moved.
5. **The docs.**

As for C1, a prototype is built first in these five commits on a local branch, and the plan is written from it. The
prototype answers (each answer follows its question):

- How C++ maps the role configs' labels to its enums in a settings struct (reflect-cpp's enum names differ in case),
  without a second spelling. **From the enumerators' own names, first letter lowercased.** Every label the role
  configs spell is its C++ enumerator's name with the first letter lowercased (`CgbC` is `"cgbC"`, `RemoveDcOffset`
  `"removeDcOffset"`, `Auto` `"auto"`), so `knobs::Label<E>` in
  [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) is an `rfl::Literal` generated at compile time
  from `rfl::get_enumerator_array<E>()`: it reads exactly those strings (an ordinal, `"CgbC"` or `"cgb-c"` refuses the
  settings), lists them as the schema's enum and maps one back to its enumerator by position. It builds each name with
  `rfl::internal::StringLiteral`, an internal reflect-cpp type, so a reflect-cpp bump is to check it. A test reads
  `settingsEnums.ts` and holds every enum knob's schema list to its TS list (`MODEL_VALUES`, `HIGHPASS_VALUES`,
  `COLOR_CORRECTION_VALUES`, `DMG_PALETTE_VALUES`, `REGION_VALUES`, `CARTRIDGE_ACCURACY_VALUES`, and `off` / `listen`
  / `drive`), so the two spellings cannot drift; it reads the numeric knob's range out of `buildGraph.ts`'s
  `SETTING_FIELDS` and `coreRoles.ts`'s schema too, and holds both to the node's JSON Schema minimum and maximum, so a
  range has one meaning in its three copies.
- Whether a model or region change applied at a block's start, rather than in the command ring's drain, moves any
  parity case (it should not: both run before the block's first step). **None moved.** Strict parity held bit for bit
  at the five commits, with nothing re-recorded, its fixture cases among them (the later first-block gain snap moved
  28 cases, re-recorded by the user's decision, section 10): every parity system node carries its whole
  core config from the first document that places it, and the live edits parity makes (a gain mid-note, the routing
  gains, highpass, APU latency, a VRC6 expansion volume) reach the core through `setParam` or a publish at the block
  the command ring's did. The node's apply is the core's own setter at that block boundary, which a test holds bit for
  bit against a core given the same edit directly.
- Whether every caller of the old methods can set its knob through the store, or a test needs `setParam` or a document
  of its own. **Every one could, through the store or the document.** The parity rig and every store-driven caller
  moved to the store's own calls; the `DirectGraph` callers (two link-group tests, the CLI's `analyze-lsdj-sync`
  session, the WRAM seam and the model-switch test) needed `DirectGraph.setCoreConfig`, since a model switch must name
  its label, and the four that set a capture directly needed nothing, `setPipeline` already publishing it.
  `DirectGraph.setParam` has only mock-tier callers.

## 9. Left as it is

- **DAW automation of node params** (the plugin's parameters mapped onto `setParam`): params make it possible; it is its
  own work.
- **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the forced
  one.
- **The construct-time fields and `buildCore`'s facts:** C3 and C4.
- What section 10 found and leaves: a NES's settings and a model's restart allocating on the block thread, a settings
  field a node does not know ignored, and an enum's two spellings on the C++ side.

## 10. Found while building

The plan's list of departures is recorded across this document: the params landing with the TS switch-over in section
8; the optional knobs and a refused range in section 3; a param's first block and the fast boot's order in section 4;
the capture on every Game Boy node in sections 3 and 10; `expansionVolume`'s integer type in section 3; `buildGraph`'s
dropped value and `DirectGraph.setCoreConfig`'s refusal in sections 5 and 10; and the three latent bugs below.

- **Three latent bugs, which the document's knobs fixed.** Each was proved failing before the switch-over and is
  pinned now. A loaded project's saved gain never reached its core: nothing applied the stored value until the gain
  was edited, so a system saved at -90 dB played at 0 dB (`app-settings`: RMS 0.10399 where 0 was due). The UI's
  System > Render ignored the gain it carried (`render.ts` handed it to `adopt`, which never applied it). And a loaded
  NES's `s5bNoise` and `mmc5PhaseReset` never reached its core (`MesenBackend::build` does not copy them from the
  construct blob). Each system node gives its core the document's values by its first block.
- **The NES core allocates on the block thread on its own**, inside Mesen (8 allocations and frees over two 512-frame
  blocks), and its sprite-limit and accuracy setters allocate whichever path calls them, as they did on the command
  ring. So section 6's "no allocation on the block thread" holds for the knobs a Game Boy applies in place and for
  every param, which the tests and a profile build's three windows hold at zero, and not for a model change that
  restarts the core (which allocates, as it did on the command ring) or for a NES's settings. Left as it is: Mesen's
  own, and the model restart's, as it was on the command ring.
- **The fast boot moved ahead of the model.** The prototype applied the Game Boy's knobs in `applyRoleConfig`'s order,
  model first. Several documents published between two blocks commit together, so a fast boot edit and a model edit
  landed in one block, and the model's restart booted with the old fast boot (`app-sameboy-display` failed). The fast
  boot waits for the next restart, so it goes first, and the edits restart the core as if made one at a time
  (section 4); a test pins the order.
- **The capture is on every Game Boy node, `off` included** (section 3): an absent knob leaves the core as it is, so a
  capture left out once a mode stops needing one would stay armed. Every Game Boy node in the B1 goldens gained
  `"serialOutCapture": "off"`, the only change in their 161 changed documents.
- **`buildGraph` leaves out a value its node would refuse; `DirectGraph.setCoreConfig` refuses it.** A feature role's
  config has been through its role schema, so a value the node would refuse comes from a hand-edited file or a newer
  build's label, and a loaded project keeps its core config as written (`adopt`): sent, it would refuse the whole
  document and silence every system, so `systemKnobs` leaves it out and that one core keeps what it has. `adopt` notes
  each value it dropped, in the log it writes once the core has built, so the stored config, the document and the core
  disagreeing leaves a trace. A `DirectGraph` caller is a test or a tool, which should hear of its mistake:
  `setCoreConfig` refuses an ordinal (`{ model: 1 }`, as two tests passed one before), a construct-time field, another
  core's field or a link group outside 0 to 255, changing nothing.
- **`setCoreConfig` merges where `applyRoleConfig` reset.** The old call decoded a partial config with every absent
  field at its default and applied them all, so `{ linkGroupId: 1 }` also reset the model, highpass, fast boot and
  display group; `setCoreConfig` leaves an absent field as it was. No caller depended on the reset (every number the
  moved tests log is unchanged).
- **A refused document can still have changed a core it took.** Activation runs while a document plans, so the knobs a
  node gives its core there stay on it when the plan is refused and the core goes back to parked. In the app the next
  document carries the same knobs, so nothing shows.
- **A setting lands at the node's next prepare, after the host commands queued behind its plan**, where the command
  ring applied its knobs in ring order: a `writeRam` or staged core bytes sent after a model change reach the core
  the restart replaces, and are wiped (on the NES, the ROM's reset handler runs after them). No in-tree caller does
  that; a caller that must act on the restarted core renders a block first, which SystemNodes.cpp's comment and
  spec/02 state as the contract, and a test pins it.
- **A node's first block plays its gain at the document's level.** Every param reaches the core at the node's first
  block, and a core's gain smoother starts at 0 dB, so a turned-down node ramped down from unity: a -90 dB document
  played near -9 dB at 20 ms, an audible burst on a duplicate, a `loadState`, a reload, a project load or a render with
  `--state`. The first value is now snapped into the smoother, and every later edit ramps as it always did. That moved
  strict parity, as a first-block gain change must: 28 cases in four groups (`routing-stereo`,
  `routing-onePerInstance` and `routing-twoPerInstance` at n>=2, and `render-shapes/perSystem-mixed`), segment 0 to the
  new gain and later segments last-bit only, every moved case with a system at a non-zero gain at its first block. The
  user chose to re-record them; `SystemNode.test.cpp` pins the snap on a sounding core and `app-settings` through a
  duplicate at -90 dB.
- **A wire's value is clamped to its param's range.** A control stream's latest value is clamped before its core call,
  from the same constants the param declaration takes its bounds from, so a wire cannot drive a gain or a NES's
  expansion volume past its range, +-inf included; the NES rounds the clamped whole percent. `rp.test.level` drives
  1e9, -1e9 and both infinities into both params, and the node's clamp is what holds them.
- **The role goldens' every-node model places a GBA.** Its gain param is in the documents the Catch2 golden run plans,
  so a GBA's param reaches a real core too; the five `every-node/*` documents gain only `sys-5`, its wires and the
  routing that names it.
- **A document's param value is sent whenever the node does not hold it**, whoever gave the node the value it has: the
  planner lands a document's value for a fresh node, or one that differs from the value the node was last given, a
  live `setParam` included (`GraphHost::setParam` records what it queued). So a document repeating a value after a live
  edit moved the node away from it sends it again, which is what makes the stores' model, kept in step by every live
  edit, win; a document that names no value for a param leaves the live one alone. A NES a `DirectGraph` places with
  no core config gets `expansionVolume` 100 at its first block, its param's default; only one built with another
  construct-time volume would notice.
- **A C++ node ignores a settings field it does not know** (node.hpp reads settings with `rfl::DefaultIfMissing` and
  no `rfl::NoExtraFields`), so a misspelt knob would plan and do nothing. `BuildGraphGoldens.test.cpp` fails on a
  golden document whose system node names a field its type's schema does not list (it caught a deliberate
  `"dmgPallete"`). That guard replaced the Catch2 case "a build spec's serialOutCapture crosses the wire under the name
  TS sends", deleted with the field. Left as it is: a strict reader would refuse the document at plan time instead.
- **Enum labels have two spellings on the C++ side.** The system nodes' enum settings read the role configs' labels
  (`"cgbC"`), where node.hpp's `enum_param` labels are kebab-case (`"cgb-c"`): a knob moved from a setting to an enum
  param would change the word a document carries. Left as it is; no enum knob is a param.
- **A setting's range rides a type.** `lightTemperature` and `apuLatencyMs` are refused out of range by a validator
  whose bounds are a type's constants rather than floating-point template arguments, which some of the compilers the
  build supports lack (older AppleClang). No Mac has compiled it yet.
- **Nothing was lost with `ConfigField::LinkGroup`.** It wrote a live core's `linkGroupId`, which only `buildCore`'s
  report reads, at the build; the cable is the document's wiring.
- **Renamed:** `test-native/lsdj-midiout-build-flag.test.ts`, whose build-spec flag is gone, is
  `lsdj-capture-settings.test.ts` (section 6); the B2 design, which names the old file, is left as the record it is.
