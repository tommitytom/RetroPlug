# Design: Phase B2, TS owns the graph

**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** built
([modular-graph-phase-b2-plan.md](modular-graph-phase-b2-plan.md)); B3 not yet built.

Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph. B1
([modular-graph-phase-b1-design.md](modular-graph-phase-b1-design.md)) made `Engine::processBlock` run a
`GraphHost` plan that a native translator built from the structural calls the stores make. B2 moves the
graph to TS: a pure `buildGraph` writes the document from the project model, the stores send it with one
`setGraph` call per edit, and the translator goes. Every system still renders through the same cores, so
the output must stay bit-identical against the B0 baseline.

---

## 0. Where B2 sits

B0 §0 has the four-step table. B2 as designed here differs from that table in what it deletes: the
role-config commands (`applySystemSetting`, `applyRoleConfig`, `ConfigField`) stay until Phase C, when
nodes own their knowledge and those fields become node params, and the kernel commands (`dspLoadKernel`,
`dspSetSystems`) stay until B3 deletes the kernel itself. B2 hands TS the graph's **structure**: which
systems exist and in what order, the audio-routing wiring, the link cables, and the host's output shape.

## 1. Decisions taken

| Question | Decision |
|---|---|
| How much of the Backend surface switches? | **Structure only.** Building a core stays a native call; live knobs and the kernel commands keep their paths (§0). |
| When does TS send a document? | **Once per store edit**, the whole project, at the point the kernel push already happens (§3). |
| How does a document name a core? | **By its system id.** `buildCore` parks the core under the id TS allocated, and the system node's settings are `{ "core": <id> }` (§2.5). |
| Who owns the host shape? | **TS.** Hosts report their output count through a hook; the render wrappers set a shape and put the previous one back (§4). |
| How is the NES tap re-armed? | **TS computes the arm, native keeps the code.** The `setGraph` request carries it as the swap's trailer on every edit that re-arms today (§2.5, §3). |
| How does B2 land? | **No long-lived switch.** The new path is built beside the old one, and one flip commit moves the stores and every direct caller over (§8). |
| What about very large graphs? | **Whole documents stay.** B2 adds a planner benchmark at about 1,000 and 10,000 nodes and records the scale levers in the main design's §12 (§7.4). |

## 2. The pieces

### 2.1 `buildGraph`

`packages/retroplug/src/graph/buildGraph.ts`, a pure function tested in `pnpm test` with no native code:

```ts
export type AudioRouting = "stereo" | "twoPerInstance" | "onePerInstance" | "channelSplit" | "pinSplit" | "stereoPinSplit";
export type HostShape = { kind: "multi"; channels: number } | { kind: "perSystem" } | { kind: "perChannel" };
export type SystemNodeType = "rp.sameboy.gb" | "rp.mesen.nes" | "rp.mesen.gba" | "rp.mesen.sms";

export interface GraphSystem {
  id: number;             // the TS system id
  type: SystemNodeType;   // as buildCore reported it
  linkGroup: number;      // Game Boys only; 0 = unlinked
  exportMode: number;     // NES only: the channel export mode its core was built with
}

export interface GraphModel {
  systems: readonly GraphSystem[];  // slot order
  routing: AudioRouting;
  shape: HostShape;
}

export interface BuiltGraph {
  document: GraphDocument;
  slots: number[];                           // system ids in slot order
  tapArm: { id: number; mode: 1 | 3 } | null; // the NES tap the routing wants armed
}

export function buildGraph(model: GraphModel): BuiltGraph;
```

It is a line-for-line port of `Translator::documentJson` (B1 §4): the same node ids (`kernel`, `sys-<id>`,
`split-<id>`, `stem-<id>-<k>`, `lane-<c>[-<j>]`, `out`), the same document order (the kernel, unlinked
systems by slot, then each link group by first appearance with its members by slot, then the mixing nodes,
then `rp.host.audio-out`), the same per-lane contribution lists, and `rp.core.add` chained past 16 inputs.
The OnePerInstance mixing stays flat, as B1 §4.3 requires. `tapArm` is B1's `syncTap` decision: a split
routing, exactly one system, and that system a NES give `{ id, mode: pins ? 1 : 3 }`; anything else gives
null. The wiring uses the mode the NES has once armed (`tapArm`'s mode, else its `exportMode`), as the
translator's `nesMode` did.

The document crosses through Phase A's `toGraphJson`, so it is checked and written in canonical JSON.

### 2.2 `EngineGraph`

`packages/retroplug/src/engineGraph.ts`, one per control-plane context, owned by the `ProjectStore` so that
every composition publishes (the plugin, SDL, `bootSession`, the UI test harness and the render worker):

- holds the host shape (default `multi(2)`, as B1's translator started) and the canonical text of the last
  document sent;
- `publish(systems, routing, { rearmTap })` builds the graph, and sends it unless the text is unchanged and
  no re-arm is due. A re-arm with an unchanged document is still sent: the plan it produces reuses every
  instance and carries the trailer;
- `setShape(shape)` is an edit of its own (rebuild, send if changed), and returns false when the document
  is refused, keeping the shape before it;
- `withShape(shape, fn)` sets a shape, runs `fn`, and puts the previous shape back (§4);
- a refused document is logged with the planner's errors. The store's model is left as it is, which is what
  happens today when the translator refuses a plan: by construction `buildGraph` only produces documents
  the planner accepts, except for capacity limits (more than 32 systems in a `perSystem` render).

### 2.3 The stores

The `SystemsStore` and `ProjectStore` derive the model from what they already hold: the entries in list
order (append, replace in place, remove shifts later ones down, which is exactly the translator's slot
model), each entry's built facts (§2.5), each Game Boy's `linkGroupId`, and `projectSettings.audioRouting`.
They call `EngineGraph.publish` once at the end of each edit, before the `onSystemsChange` hook that pushes
the kernel structure. §3 lists the edits. A `SystemsStore` used without a `ProjectStore` publishes its systems
itself, through an `EngineGraph` of its own, at the default stereo routing.

Each entry gains a transient `built: { type, exportMode, linkGroup }`, filled from `buildCore`'s answer and
never persisted. A live link-group edit (`setRoleConfig` on the `sameboy` role) updates `built.linkGroup` to
the parsed value it sends native, as the translator's `setLinkGroup` followed every applied LinkGroup field.

### 2.4 `DirectGraph`

`packages/retroplug/src/graph/directGraph.ts`, for the callers that build systems without stores: 44
`test-native` files, `cli/sessions/analyze-lsdj-sync.ts` and `tools-src/lsdj-detect-offsets.ts`. It keeps
a system list with B1's model rules and publishes after each call through its own `EngineGraph`:

```ts
class DirectGraph {
  constructor(backend: DirectBackend, graph?: EngineGraph);         // share a session's graph to mix with its stores
  add(spec: ConstructSpec, id: number): boolean;                   // appends
  replace(oldId: number, spec: ConstructSpec, id: number): boolean; // keeps oldId's slot
  remove(id: number): boolean;                                      // later slots shift down
  setRouting(routing: AudioRouting): boolean;
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean; // a sameboy link group rewires
  ids(): number[];
  readonly graph: EngineGraph;                                      // for setShape / withShape
}
directGraph(): DirectGraph;                                         // the process's one, over the real backend
```

Every call but `applyRoleConfig` re-arms the tap, as its native counterpart did (a remove or replace of an id
already gone included). `applyRoleConfig` forwards the config to the core and, for a `sameboy` config, takes
its link group as native decodes it (absent is 0) and republishes. The callers keep one `DirectGraph` per
process (`directGraph()`), since the probe helper and the file using it would otherwise drop each other's
systems; a file that also uses a session's stores shares the session's `EngineGraph`, so each publisher sees
the documents the other sent.

### 2.5 Native: `buildCore` and `setGraph`

**`buildCore(spec, id)`** is today's `constructSystem` without the placement:

- refuses an `id` that is still live (parked, or held by a node of any plan not yet reclaimed). It reclaims
  first, so a core in a plan already handed back is freed before the check; with no audio thread running
  that covers every removed core;
- builds the core (`factory.build`, control thread), claims its snapshot slot, and parks it under `id`;
- takes one new spec field, `serialOutCapture`, set on a Game Boy core before it is parked (§3);
- returns `{ type, exportMode, linkGroup }`, the node type the core must be placed as and the two values
  the graph needs as the core decoded them, or null when the build failed (an unreadable ROM, an unknown
  format, a full snapshot registry, a live id).

**`setGraph(request)`** takes

```ts
interface SetGraphRequest {
  document: string;                                 // toGraphJson's output
  slots: number[];                                  // system ids in slot order
  tap?: { arm?: { id: number; mode: number } };     // present: re-arm; arm absent: restore only
}
```

and returns `{ ok, errors: { nodeId, port, code, message }[] }`:

- checks that `slots` and the document's system nodes name the same ids, each node as `{ "core": <id> }`;
- builds the `SystemTable` (B1 §4.5) from `slots` and the cores the engine holds by id, and hands it to
  `GraphHost::setGraph` as the plan's host data;
- when `tap` is present, builds the `SyncTap` trailer: restore whatever is armed (the record keeps the core
  pointer, as in B1), then arm `tap.arm` if given. `DspCommand::SyncTap` carries `{ id, mode }` instead of
  a routing value;
- after a successful plan, frees every core still parked (each node the document names took its core at
  activation, so a core still parked is one no document placed), releasing its snapshot slot first;
- records the plan's host width (the sum of `rp.host.audio-out`'s buses) for the render guard (§4).

`rp.host.cores/1` keeps its shape; its `take` now takes a system id. The engine keeps every live core in a
map by id, added at `buildCore` and removed when the node that owns the core destroys it.

## 3. Store edits

`setSerialOutCapture` only sets a flag on the Game Boy core, and the store knows a system's roles before it
builds, so a built core carries its serial-out capture in the build spec. A placed system then needs no
command after its plan. The live call remains for an `lsdj-sync` mode change on a running system.

| Store edit | Today, in ring order | B2, in ring order |
|---|---|---|
| add, loadRom, openRom, duplicate | plan + tap, serial command, kernel push | build (serial flag inside), plan + tap, kernel push |
| replace, swapRom, reload, reset, loadState, loadSram, newSram, newSramAs | plan + tap (replace), serial command, kernel push | the same as the row above |
| remove | plan + tap, kernel push | plan + tap, kernel push |
| setRoleConfig `sameboy` | 7 config commands, the link plan right after the LinkGroup one, kernel push | 7 config commands, the link plan (only if the group changed), kernel push |
| setRoleConfig (other kinds), setGain, setReloadOnRomChange | commands, kernel push | the same; no plan (the document is unchanged) |
| setAudioRouting, a same-mode one included | plan + tap | plan + tap; an unchanged document is still sent for the re-arm |
| newProject | N × (plan + tap), routing plan + tap, kernel push | the clear's plan + tap, routing plan + tap, kernel push |
| load (path, bytes, relink) | N × (plan + tap), then per system build plan + tap and serial, routing plan + tap, kernel push | the clear's plan + tap, N builds, one plan + tap, kernel push |

- **A load publishes its clear first**, then the new systems. Today the old cores are freed before the new
  ones are built; doing both in one plan would hold 2N cores at once, and the snapshot registry has 64
  slots, which 32 systems would fill.
- **`adopt` places its core, as it always did** (before B2 its `constructSystem` placed the core at once).
  It publishes but stays quiet otherwise (no dirty, no kernel push). A project load holds those publishes
  back and places every adopted system with one publish of the loaded routing; the serial flags are
  already inside their cores.
- **`inheritLinkGroup`** (Add Instance, Duplicate) is two `setRoleConfig` edits, each with its own link plan,
  as today.

## 4. The host shape

The shape decides the wiring, so the document's author holds it.

| Host or call | Shape | How it reaches TS |
|---|---|---|
| Plugin | `multi(8)` | native calls `__rp_setHostOutputs(DISTRHO_PLUGIN_NUM_OUTPUTS)` once the control plane has booted, before any project loads. B1 set it at `activate`; nothing renders before `activate`, so no audio changes |
| SDL standalone | `multi(device channels)` | native calls `__rp_setHostOutputs(n)` at startup before audio starts and in `reconfigureAudio` after the device reopens, both on the UI thread that runs the control plane |
| CLI live audio (`startAudio`) | `multi(2)` | TS's audio wrapper sets it before calling `startAudio` |
| `renderAudio` | `multi(2)` | TS's audio wrapper, `withShape` |
| `renderBlocks(outputs)` | `multi(outputs)` | TS's audio wrapper, `withShape` |
| `renderAudioPerSystem` | `perSystem` | TS's audio wrapper, `withShape` |
| `renderAudioPerChannel` | `perChannel` | TS's audio wrapper, `withShape` |

- `__rp_setHostOutputs` is a control-plane global (`pluginControlPlane.ts`), calling `setShape`.
- The render wrappers live where `bootSession` composes `audio`, over the project's `EngineGraph`. Putting
  the shape back after a render fixes B1's open follow-up (a `perSystem` render left its shape behind, and
  a 33rd system was then refused). A shape the planner refuses makes the wrapper render nothing, as the B1
  render RPCs did.
- Swapping shapes never touches a core: the system nodes are reused, only mixing nodes come and go, and a
  shape change never re-arms the tap, as in B1.
- **The render guard.** Each native render RPC refuses, with a log line, when the buffers it was handed do
  not match the running plan's host width (§2.5). A raw caller that forgets the shape gets an error, not
  audio routed for the wrong output count.
- Block-size growth (`setMaxBlockFrames`) and sample-rate changes stay native host concerns: they
  re-activate the running graph and change none of its structure.

## 5. Why the output stays bit-identical

- **The same document.** `buildGraph` is the translator's port, held to it by goldens the translator
  writes before it is deleted (§7.1).
- **No block runs inside an edit in the parity rig.** With no audio thread every push applies at once, so a
  run of plans collapses to its last one: the cores are the same cores, and the commands that land on the
  same cores keep their relative order.
- **The NES tap is history-free.** `NesSoundMixer::SetChannelCapture` resets every capture stream (its blip
  buffer, resampler, history and pending samples), so several restore-and-arm passes with no audio between
  them leave exactly the state one does. Every edit that re-arms today still re-arms, redundant
  `setAudioRouting` calls included, so no reset that happens today is lost.
- **Built facts come from the core.** The link group and export mode are what `buildCore` read back from
  the core it built, so TS never re-derives a value native decoded differently (a stored config missing a
  field, say).
- **The serial flag is the same flag.** Set at build or by a command applied before the core's first block,
  the core holds the same value when it first runs.

While audio runs (the plugin, SDL), an edit now lands whole in one block, and a project load is two plans
instead of 2N + 1. Parity cannot see that, and it is better behaviour.

## 6. What goes and what stays

**Deleted:**

- `host/graph/Translator.{hpp,cpp}`, `test/engine-graph/Translator.test.cpp` and `TranslatorPlan.test.cpp`
  (their expectations become the goldens and the TS wiring tests, §7);
- `QueuedInvoker`'s `adoptSystem`, `replaceSystem`, `removeSystem`, `setAudioRouting`, `setHostShape` and
  the LinkGroup interception; `Engine::translator()` and `Engine::pushSyncTap`. The LinkGroup field still
  reaches the core's config through `applyRoleConfig`;
- native `rp::host::AudioRouting` and `rp::host::HostShape`. The routing enum survives only in TS;
- the RPCs `constructSystem`, `removeSystem` and `setAudioRouting`, with their `Backend`, `realBackend` and
  mock entries (the mock's stale "0 to 3" routing check goes with them).

**Stays:** `applySystemSetting`, `applyRoleConfig`, `ConfigField`, `setSerialOutCapture` (live mode changes),
`compileScript`, `dspLoadKernel`, `dspSetSystems`, `rp.dsp.kernel` (first in every document), the armed-tap
record, every read, `pressButton`, `stageSystemMidi` and `writeRam`.

**Changes:**

- `Engine::setSampleRate` re-rates the cores of the running plan's table, in slot order;
- the native tests that drove structure through the invoker drive it through `setGraph` requests, with
  documents spelled as `buildGraph` writes them (`test/engine-graph/GraphDocs.hpp`): `NesSplitRouting`
  (the tap's arms, restores and lane packing, now one request each) and `EngineSampleRate`; the invoker
  test's host-shape case became `EngineGraph.test.cpp`'s render-guard case;
- the mock backend records `buildCore` calls (the recorder renamed in the 25 `pnpm test` files that read it)
  and every `setGraph` request, so pure tests can assert the wiring;
- `buildCore` and `setGraph` sit on the control-plane facet `constructSystem` lives on today.

**Outside this repo:** the shipped SDK types (`cli/sdk-types.d.ts`) never named the three removed calls, and
the in-repo Node addon use goes through `bootSession` and the stores, so SDK consumers are unaffected. A
consumer sending raw `constructSystem` RPCs through the Node addon would break; BlipToaster's code is not in
this repo, so the change notes it.

## 7. Verification

### 7.1 The translator goldens

While the translator exists, a generator in `retroplug-engine-graph-test` (run with an environment switch,
like the parity re-record) builds a matrix of models and writes one golden per model under
`packages/retroplug/test/graph/goldens/translator/`: 0 to 9 systems, and 17 or more so the adds chain;
every kind; unlinked Game Boys, one link group and several; all six routings crossed with NES build modes 0
to 3; `multi(2)`, `multi(4)`, `multi(6)`, `multi(8)`, `perSystem` and `perChannel`. Each golden holds the
model, the document with each core token normalized to its system id, the slot order, and the tap arm B1's
`syncTap` would make. A `pnpm test` case holds `buildGraph` to every golden exactly: node order, wire order
and settings. The goldens outlive the translator as `buildGraph`'s expectations.

### 7.2 TS

In `pnpm test`, over the mock backend:

- `buildGraph` against explicit wiring tables for each routing mode and shape (the translator tests' tables,
  carried over), the kernel first, chained adds, slot shifts on remove;
- `EngineGraph`: an unchanged document is not sent, a re-arm is, `withShape` puts the shape back, a refused
  shape is kept off;
- each store edit publishes once, with the re-arm flag §3 gives it, and a load publishes its clear first;
- the serial flag rides in the build spec for a system whose `lsdj-sync` mode needs it;
- `DirectGraph` keeps B1's model rules.

### 7.3 Native

`buildCore` refuses a live id and frees nothing it should not; `setGraph` refuses mismatched `slots`; the
table follows `slots`; a `{ id, mode }` trailer restores then arms, in the swap; parked cores a successful
document does not name are freed with their snapshot slots; the render guard refuses mismatched buffers.
The B1 graph and system-node tests keep running unchanged.

### 7.4 The planner benchmark

Documents of about 1,000 and 10,000 nodes, both chains and wide fans, timing `setGraph` end to end (parse,
plan, publish). It reports and does not gate, like `dsp-bench`. The numbers go into the main design's §12
next to the four scale levers found while designing B2: every plan allocates fresh audio buffers (pool them
across plans); the output-lifetime pass scans every wire per output port (index the wires once); every
node's settings are re-parsed per edit (cache by settings text); and the document itself (a patch form of
`setGraph` applied to native's stored copy saves transfer and parse, not planning).

### 7.5 Acceptance

- Parity with the fixtures present and `RP_FAIL_ON_SKIP=1`
  (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`), and once without the
  fixtures (the 140 cases CI runs). The rig changes only in `bootSession` and `rig.ts`, as B0 promised;
  `render-blocks.test.ts` moves to `DirectGraph`.
- All of `pnpm test:native`, `pnpm test`, `pnpm test:plugin`, `pnpm test:node` and `tsc`; the host
  ThreadSanitizer and AddressSanitizer passes.
- `reaper:editor-reopen`, `reaper:editor-autoload`, `reaper:two-instances-load`, `reaper:params` and
  `reaper:params-clap`, where a live host drives the ring and the plugin's shape hook.
- `pnpm sdl:smoke` and `pnpm sdl:pipewire`: SDL's shape now arrives through the hook, and the pipewire run's
  wide-output leg covers `multi(8)`.

## 8. Work order

Each step its own commit, with parity unchanged:

1. The translator goldens (§7.1).
2. `buildGraph`, its golden test and its wiring-table tests.
3. Native: `buildCore` (with `serialOutCapture`), the `setGraph` request (slots, tap, parked-core freeing,
   live-id refusal) and the render guard, beside the old path, which keeps working. `Engine::setSampleRate`
   moves to the plan's table here, so it serves both paths.
4. `EngineGraph`, the new `Backend` calls in the mock and real backends, and the `__rp_setHostOutputs`
   global. `DirectGraph` is built over the old calls first, and the direct callers move onto it, which
   changes nothing yet.
5. **The flip.** The stores and `DirectGraph` publish through `EngineGraph`; the plugin and SDL call the
   hook; the render wrappers set and restore the shape. Parity acceptance runs here.
6. The deletions of §6, and the invoker tests moved to `setGraph` requests.
7. The planner benchmark (§7.4).
8. Docs: `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/06-build-test.md`,
   `spec/13-subsystems.md`; the main design's §5.1 ("tens of nodes"), §7.5, §8 (B2 built) and §12; B0's §0
   table row for B2; the AGENTS.md binary descriptions that name the translator.

## 9. Not in B2

- **Gain is never sent to native on a project load, a duplicate or a UI render.** Found while mapping the
  store flows, from the code, not yet run: `adopt` and `duplicateSystem` carry gain only in the TS entry,
  and the construct settings blob holds the role config alone, so a loaded project plays at unity gain until
  its gain is touched. B2 keeps the behaviour exactly; the fix is its own commit afterwards, with a
  re-record if a baseline moves.
- Node params for the live knobs and the end of `ConfigField`: Phase C.
- The kernel commands, the kernel node and the script-node runtime: B3.
- System nodes building their own cores from settings and resources (`putResource`), and duplicate or
  reload as a node with a state-seed resource: Phase C, with the knobs.
- The four scale levers of §7.4: recorded, built when a large graph needs them.
- Rolling the store back when a document is refused: as today, the store keeps its model and the refusal is
  logged.

## 10. Found while building

- **A bare `SystemsStore` drove native structure.** Before B2 every store edit reached native through its own
  calls, so 24 `pnpm test` files and many native tests build systems with a `SystemsStore` and no
  `ProjectStore`. Only the `ProjectStore` installed the publish hook, so those systems were built and never
  placed. A `SystemsStore` on its own now publishes at the default stereo routing (§2.3).
- **`adopt` placed its core.** The design kept it quiet, but before B2 its `constructSystem` placed the core
  at once, and twelve callers (the render library, two CLI sessions, native tests and authoring scripts)
  relied on it. It publishes again, and a project load holds the publishes back to keep one plan (§3).
- **Two cores under one id.** The second test in `dsp-risa-sync` and in `dsp-risa-sync-locate` built a core
  under id 1 while the first test's was still live, which before B2 quietly ran both under one id (each
  claimed a snapshot slot under it). `buildCore` refuses a live id, so the tests now remove their systems,
  as AGENTS.md asks of every test file.
- **A raw call the migration missed.** `dsp-sms-sync` removed its system through a second backend named
  `be2`, which the mechanical move to `DirectGraph` did not match; the old call reached the translator, so
  the system stayed in the document and played into the next case.
- **The mock had to answer before a core is placed.** Native claims a built core's snapshot slot at the
  build, so its reads answer at once; the mock now treats a built core as live until a published document
  drops it.
- **Planning dominates at scale.** The benchmark (§7.4) measured about 10 to 14 ms per edit at 1,000 nodes
  and 160 to 330 ms at 10,000, with a one-node edit costing what a cold plan does. The numbers and the
  levers are in the main design's §12.

