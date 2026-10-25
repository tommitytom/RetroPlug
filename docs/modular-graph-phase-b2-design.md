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
| How is the NES tap re-armed? | **TS computes the arm, native keeps the code.** The `setGraph` request carries it as the swap's trailer on every edit that re-armed before B2 (§2.5, §3). |
| How does B2 land? | **No long-lived switch.** The new path is built beside the old one, and one flip commit moves the stores and every direct caller over (§8). |
| What about very large graphs? | **Whole documents stay.** B2 adds a planner benchmark at about 1,000 and 10,000 nodes and records what planning costs, and where, in the main design's §12 (§7.4). |

## 2. The pieces

### 2.1 `buildGraph`

`packages/retroplug/src/graph/buildGraph.ts`, a pure function tested in `pnpm test` with no native code:

```ts
import type { AudioRouting } from "../settingsEnums";
// AudioRouting = "stereo" | "twoPerInstance" | "onePerInstance" | "channelSplit" | "pinSplit" | "stereoPinSplit"

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

export interface TapArm {
  id: number;
  mode: 1 | 3;
}

export interface BuiltGraph {
  document: GraphDocument;
  slots: number[];        // system ids in slot order
  tapArm: TapArm | null;  // the NES tap the routing wants armed
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

`packages/retroplug/src/engineGraph.ts`, one per publisher to an engine: the `ProjectStore`'s, so that every
composition publishes (the plugin, SDL, `bootSession`, the UI test harness and the render worker), or a
`DirectGraph`'s or a bare `SystemsStore`'s own (§2.3, §2.4):

- holds the host shape (default `multi(2)`, as B1's translator started), the model (the systems and routing
  the last `publish` gave it) and a dedupe key: the canonical text and the slots of the last document that
  planned (the slots too, since two slot orders can build one document);
- `publish(systems, routing, { rearmTap })` takes the systems and routing as the model, builds the graph,
  and sends it unless the key is unchanged and no re-arm is due. A re-arm with an unchanged document is
  still sent: the plan it produces reuses every instance and carries the trailer;
- `setShape(shape)` is an edit of its own (rebuild the model at the shape, send if changed), and returns
  false when the document is refused, keeping the shape before it; a `multi` shape whose channel count is
  not a positive integer returns false without sending anything;
- `withShape(shape, fn)` sets a shape, runs `fn`, and always puts the previous shape back (§4);
- a refused document is logged with the planner's errors, and a `setGraph` that throws counts as refused,
  its error rethrown to the publisher.
  The model stays whatever the publisher last asked for, refused or not, and the store's model is left as
  it is, as when the translator refused a plan: by construction `buildGraph` only produces documents the
  planner accepts, except for capacity limits (more than 32 systems in a `perSystem` render). A refusal
  clears the dedupe key, so the next document is sent even if it matches the running one: the refused
  document's cores stay parked until a document plans, which places or frees them. A refused re-arm stays
  due and rides the next document sent, by a publish or a shape change. Until a document plans, the
  engine may be running another shape's plan, so a `setShape` or `withShape` to the current shape is sent
  rather than skipped; a `withShape` restore that is refused takes the previous shape all the same, and
  the next publish or shape change sends at it.

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
  constructor(backend: DirectBackend, graph?: EngineGraph);         // a session's graph, for its render calls
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
its link group as native decodes it (absent is 0) and republishes when it changed. An edit whose document is
refused puts `DirectGraph`'s own model back and republishes it, so the graph's model is not the edit that
failed. The callers keep one `DirectGraph` per process (`directGraph()`), since the probe helper and the file
using it would otherwise drop each other's systems. Sharing a session's `EngineGraph` (the constructor's
second argument), so the session's render calls render what the `DirectGraph` places, does not merge the two
models: each publish replaces the graph's model with the publisher's own systems. It is safe only while the
session's stores place nothing and the two publish in turn, as `test-native/parity/render-blocks.test.ts`
and `cli/sessions/analyze-lsdj-sync.ts` do.

### 2.5 Native: `buildCore` and `setGraph`

**`buildCore(spec, id)`** is the old `constructSystem` without the placement:

- refuses id 0 (the snapshot registry's free-slot id) and an `id` that is still live (parked, or held by a
  node of any plan not yet reclaimed). It reclaims first, so a core in a plan already handed back is freed
  before the check; with no audio thread running that covers every removed core;
- builds the core (`factory.build`, control thread) and parks it under `id` with `Engine::parkCore`, which
  claims its snapshot slot, checking that the id is not live before the claim (the registry finds a slot
  by id, first match, so a second slot under a live id, released, would free the live core's);
- takes one new spec field, `serialOutCapture`, set on a Game Boy core before it is parked (§3);
- returns `{ type, exportMode, linkGroup }`, the node type the core must be placed as and the two values
  the graph needs as the core decoded them, or null when the build failed (an unreadable ROM, an unknown
  format, a full snapshot registry, a live id, id 0).

**`setGraph(request)`** takes

```ts
interface SetGraphRequest {
  document: string;                                 // toGraphJson's output
  slots: number[];                                  // system ids in slot order
  tap?: { arm?: { id: number; mode: number } };     // present: re-arm; arm absent: restore only
}
```

and returns `{ ok, errors: { nodeId, port, code, message }[] }`:

- checks that `slots` and the document's system nodes name the same ids, each node as `{ "core": <id> }`,
  that each names a live core and is the node type that core is placed as (a node of another type would
  take the core and give it up), and that a `tap.arm` names one of the slots and a mode from 1 to 3; any
  of these refuses the document before anything is built;
- builds the `SystemTable` (B1 §4.5) from `slots` and the cores the engine holds by id, and hands it to
  `GraphHost::setGraph` as the plan's host data;
- when `tap` is present, builds the `SyncTap` trailer: restore whatever is armed (the record keeps the core
  pointer, as in B1), then arm `tap.arm` if given. `DspCommand::SyncTap` carries `{ arm, mode, id }`
  instead of a routing value;
- after a successful plan, frees every core still parked (each node the document names took its core at
  activation, so a core still parked is one no document placed), releasing its snapshot slot first. A
  document refused after its nodes took their cores (a later node failing to activate, a ring with no
  room for the swap) leaves every core where it was: each one a node of the refused plan took goes back to
  parked, its snapshot slot kept, for a corrected document to place;
- records the plan's host width (the sum of `rp.host.audio-out`'s buses) for the render guard (§4).

`rp.host.cores/1` keeps its shape; its `take` now takes a system id. The engine keeps every live core in a
map by id, added at `buildCore` and removed when the node that owns the core destroys it, or when a
document that plans frees the core still parked.

## 3. Store edits

`setSerialOutCapture` only sets a flag on the Game Boy core, and the store knows a system's roles before it
builds, so a built core carries its serial-out capture in the build spec. A placed system then needs no
command after its plan. The live call remains for an `lsdj-sync` mode change on a running system.

| Store edit | Before B2, in ring order | B2, in ring order |
|---|---|---|
| add, loadRom, openRom, duplicate | plan + tap, serial command, kernel push | build (serial flag inside), plan + tap, kernel push |
| replace, swapRom, reload, reset, loadState, loadSram, newSram, newSramAs | plan + tap (replace), serial command, kernel push | the same as the row above |
| remove | plan + tap, kernel push | plan + tap, kernel push |
| setRoleConfig `sameboy` | 7 config commands, the link plan right after the LinkGroup one, kernel push | 7 config commands, a publish only if the group changed (sent only if the document did), kernel push |
| setRoleConfig (other kinds), setGain, setReloadOnRomChange | commands, kernel push | the same; no plan (the document is unchanged) |
| setAudioRouting, a same-mode one included | plan + tap | plan + tap; an unchanged document is still sent for the re-arm |
| newProject | N × (plan + tap), routing plan + tap, kernel push | the clear's plan + tap (only when it had systems), routing plan + tap, kernel push |
| load (path, bytes, relink) | N × (plan + tap), then per system build plan + tap and serial, routing plan + tap, kernel push | the clear's plan + tap (only when it had systems), N builds, one plan + tap, kernel push |

- **A load publishes its clear first**, then the new systems. Before B2 the old cores were freed before the
  new ones were built; doing both in one plan would hold 2N cores at once, and the snapshot registry has 64
  slots, which 32 systems would fill.
- **`adopt` places its core, as it always did** (before B2 its `constructSystem` placed the core at once).
  It publishes but stays quiet otherwise (no dirty, no kernel push). A project load holds those publishes
  back and places every adopted system with one publish of the loaded routing; the serial flags are
  already inside their cores. A load that throws partway publishes what it adopted, and pushes it to the
  kernel, before rethrowing, so the cores it built are placed rather than left parked.
- **`inheritLinkGroup`** (Add Instance, Duplicate) is one or two `setRoleConfig` edits: the parent is
  promoted to group 1 only when it is ungrouped, then the child joins the parent's group. With no other
  Game Boy already in group 1, the promotion builds the document already running (a group of one
  dissolves), so it sends nothing, and only the child joining sends a plan, without a re-arm.

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
- The render wrappers are `createAudioDriver(graph)` (`audioDriver.ts`), which `bootSession` hands the
  project's `EngineGraph`. Putting the shape back after a render fixes B1's open follow-up (a `perSystem`
  render left its shape behind, and a 33rd system was then refused). A shape the planner refuses makes
  the wrapper render nothing, as the B1 render RPCs did, except `renderBlocks`, which throws.
- Swapping shapes never touches a core: the system nodes are reused, only mixing nodes come and go, and a
  shape change re-arms the tap only to carry a re-arm a refused document left due (§2.2); otherwise it
  never does, as in B1.
- **The render guard.** Each native render RPC refuses when the buffers it was handed do not match the
  running plan's host width (§2.5): with a log line and an empty answer, or, for `renderBlocks`, an error
  string in its result. A raw caller that forgets the shape gets an error, not
  audio routed for the wrong output count.
- Block-size growth (`setMaxBlockFrames`) and sample-rate changes stay native host concerns: they
  re-activate the running graph and change none of its structure.

## 5. Why the output stays bit-identical

- **The same document.** `buildGraph` is the translator's port, held to it by goldens the translator
  wrote before it was deleted (§7.1).
- **No block runs inside an edit in the parity rig.** With no audio thread every push applies at once, so a
  run of plans collapses to its last one: the cores are the same cores, and the commands that land on the
  same cores keep their relative order.
- **The NES tap is history-free.** `NesSoundMixer::SetChannelCapture` resets every capture stream (its blip
  buffer, resampler, history and pending samples), so several restore-and-arm passes with no audio between
  them leave exactly the state one does. Every edit that re-armed before B2 still re-arms, redundant
  `setAudioRouting` calls included, so no reset that happened before B2 is lost.
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
  (their expectations become the goldens, the TS wiring tests and `BuildGraphGoldens.test.cpp`, §7);
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

- `Engine::setSampleRate` re-rates the cores of the running plan's table, in slot order, then the parked
  ones, so a core built before a rate change runs at the new rate once a document places it;
- the native tests that drove structure through the invoker drive it through `setGraph` requests, with
  documents spelled as `buildGraph` writes them (`test/engine-graph/GraphDocs.hpp`): `NesSplitRouting`
  (the tap's arms, restores and lane packing, now one request each) and `EngineSampleRate`; the invoker
  test's host-shape case became `EngineGraph.test.cpp`'s render-guard case;
- the mock backend records `buildCore` calls (the recorder renamed in the 25 `pnpm test` files that read it)
  and every `setGraph` request, so pure tests can assert the wiring;
- `buildCore` and `setGraph` sit on the control-plane facet `constructSystem` lived on.

**Outside this repo:** the shipped SDK types (`cli/sdk-types.d.ts`) never named the three removed calls, and
the in-repo Node addon use goes through `bootSession` and the stores, so SDK consumers are unaffected. A
consumer sending raw `constructSystem` RPCs through the Node addon would break; BlipToaster's code is not in
this repo, so the change notes it. One `bootSession` at a time per process: each session owns an
`EngineGraph` over the process's one engine, so a second session's first edit publishes a graph without
the first session's systems and frees their cores, where B1's one native translator kept both. BlipToaster
boots a session per test and never interleaves two, so it is unaffected; the shipped SDK types say so on
`bootSession`.

## 7. Verification

### 7.1 The translator goldens

While the translator existed, a generator in `retroplug-engine-graph-test` (run with an environment switch,
like the parity re-record) wrote what it built for a matrix of models into one file,
`packages/retroplug/test/graph/golden/build-graph.json`: 20 system sets × 7 host shapes × 6 routings, 840
cases. The sets: none; each kind alone, the NES in each build mode 0 to 3, and lone ids other than 1; a Game
Boy with a NES; link groups (around an unlinked system, two groups, a group of one, groups in order of first
appearance, three members); nine mixed systems; 17 and 33 Game Boys, so the adds chain; sparse ids. The
shapes: `multi(1)`, `multi(2)`, `multi(4)`, `multi(6)`, `multi(8)`, `perSystem` and `perChannel`. Each case
holds the model, the slot order, the tap arm B1's `syncTap` would make, and the index of its document (each
core token normalized to its system id) in the file's list of distinct documents. A `pnpm test` case
(`test/graph/build-graph-goldens.test.ts`) holds `buildGraph` to every case exactly: node order, wire order
and settings. The goldens outlive the translator as `buildGraph`'s expectations; nothing generates the file
now, so an intentional change to the document rewrites it by hand and says why in its commit. Not every
golden document plans: the 33-system `perSystem` documents give `rp.host.audio-out` 33 buses, more than the
32 it takes. The file holds them as the translator wrote them, and the native test that plans every golden
document (§7.3) requires the engine to refuse them.

### 7.2 TS

In `pnpm test`, over the mock backend:

- `buildGraph` against explicit wiring tables for each routing mode and shape (the translator tests' tables,
  carried over), the kernel first, chained adds, slot shifts on remove;
- `EngineGraph`: an unchanged document is not sent, a re-arm is, `withShape` puts the shape back, a refused
  shape is kept off; after a refusal (a send that throws included) the next document is sent, a refused
  re-arm rides it, and a shape change rebuilds the refused model;
- each store edit publishes once, with the re-arm flag §3 gives it, and a load publishes its clear first;
- the serial flag rides in the build spec for a system whose `lsdj-sync` mode needs it;
- `DirectGraph` keeps B1's model rules.

### 7.3 Native

`buildCore` refuses a live id and frees nothing it should not; `setGraph` refuses mismatched `slots`; the
table follows `slots`; a `{ id, mode }` trailer restores then arms, in the swap; parked cores a successful
document does not name are freed with their snapshot slots; the render guard refuses mismatched buffers.
The B1 graph and system-node tests keep running unchanged. `test/engine-graph/BuildGraphGoldens.test.cpp`
hands every distinct request in the goldens (§7.1) to a fresh `Engine` with real cores, each built as its
model says, and requires it to plan (the 33-system `perSystem` ones to be refused), each NES to end in the
mode its arm or its build gives, and one block of each plan to fault no node (`Engine::faultCount`).

### 7.4 The planner benchmark

`test/graph/PlannerBench.test.cpp` in `retroplug-graph-test`, hidden from the normal run
(`build/bin/retroplug-graph-test "[bench]"`): documents of about 1,000 and 10,000 nodes, both chains and wide
fans, timing the planner's `GraphHost::setGraph` on a bare graph rig, with the document's parse and the
plan (plan, publish, adopt, free the plan replaced) apart; `Engine::setGraph`'s own checks are not timed.
Each figure is a median: a cold plan of 3 fresh rigs, and a one-node edit of 5, each of which the bench
checks rebuilt the edited delay, kept the first delay's instance, and left the instance count unchanged.
It reports and does not gate, like `dsp-bench`. The numbers, and where timers around each planner pass
found the time goes (the planner's quadratic `order()`, the per-node JSON round trips of `structure()` and
`reconcile()`, the scans in `assignOutputs`, fresh audio buffers at the bench's block size, then the
parse), are in the main design's §12, with what the TS side adds.

### 7.5 Acceptance

- Parity with the fixtures present and `RP_FAIL_ON_SKIP=1`
  (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`), and once without the
  fixtures (the 140 cases CI runs). The rig changed only in `bootSession` (`rig.ts` needed nothing), as
  B0 promised; `render-blocks.test.ts` moved to a `DirectGraph` sharing the session's graph.
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
- The planner costs of §7.4 (the main design's §12): recorded, fixed when a large graph needs them.
- Rolling the store back when a document is refused: as before B2, the store keeps its model and the
  refusal is logged (§2.2). Only `DirectGraph`, which has no store, puts its own model back (§2.4).

## 10. Found while building

- **A bare `SystemsStore` drove native structure.** Before B2 every store edit reached native through its own
  calls, so 24 `pnpm test` files and many native tests build systems with a `SystemsStore` and no
  `ProjectStore`. Only the `ProjectStore` installed the publish hook, so those systems were built and never
  placed. A `SystemsStore` on its own now publishes at the default stereo routing (§2.3).
- **`adopt` placed its core.** The design kept it quiet, but before B2 its `constructSystem` placed the core
  at once, and fifteen files besides the `ProjectStore` (the render library, two CLI sessions, nine native
  tests and three authoring scripts) relied on it. It publishes again, and a project load holds the
  publishes back to keep one plan (§3).
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
- **Planning dominates at scale.** The benchmark (§7.4) measured about 9 to 13 ms per one-node edit at 1,000
  nodes and 150 to 315 ms at 10,000, an edit costing about what a cold plan does. Its first version timed
  the parse and the plan together, from one run; review split them, took medians, and made each edit check
  that it rebuilt the edited delay, kept the first delay's instance and left the instance count unchanged,
  since an edit that silently rebuilt everything would also time like a cold plan. The TS side is not
  small at that size either: `toGraphJson` of a 10,030-node document takes about 105 ms under QuickJS. The
  numbers and where the time goes are in the main design's §12.
- **The goldens grew in review.** The first matrix held 540 cases (15 system sets, 6 host shapes, 6
  routings). Review added lone ids other than 1, link groups in order of first appearance, a three-member
  group, 33 Game Boys (longer chains of adds, and the `perSystem` documents too wide to plan) and a
  1-channel host, for 840 (§7.1).
- **No native proof that the documents plan.** Deleting `TranslatorPlan.test.cpp` with the translator left
  nothing that planned `buildGraph`'s documents on the engine: the goldens test in `pnpm test` compares
  text. `BuildGraphGoldens.test.cpp` now plans every golden document natively, requires the 33-system
  `perSystem` ones to be refused, and runs one block of each plan, requiring no node to fault
  (`Engine::faultCount`), as `TranslatorPlan` did (§7.3).
- **Native refusals, tightened in review.** A document refused after its nodes had taken their cores freed
  those cores; they now go back to parked, for a corrected document to place (§2.5). `parkCore` claims the
  snapshot slot itself, so `buildCore` is `factory.build` then `parkCore`, and the live-id check runs
  before the claim: the prototype's `buildCore` claimed the slot, and a `parkCore` refusing a live id
  released it again, which, the registry matching a slot by id, first match, would have been the live
  core's. Parked cores follow a sample-rate change, as the running plan's do. A system node of another type
  than its core, and a tap arm naming no slot or a mode outside 1 to 3, are refused before anything is
  built, and id 0, the registry's free-slot id, is refused at the build.
- **A refusal wedged the project.** The prototype's `EngineGraph` kept the systems of the last document that
  planned as its model and rebuilt those on a shape change. After one refusal, a shape change (a render's
  `withShape`, say) placed the systems from before the refused edit, and its plan freed the cores the stores
  still listed, so every later edit naming them was refused. The model is now whatever a publisher last
  asked for, refused or not; a refusal clears the dedupe key, so the next publish is resent; a refused
  re-arm stays due and rides the next document sent; the slots joined the document in the dedupe key;
  `withShape` always puts the shape back, forcing a resend when the restore is refused; a `setGraph` that
  throws counts as refused; and after any failure a same-shape `setShape` or `withShape` is sent rather
  than skipped until a document plans, since the engine may be on another shape's plan (§2.2).
- **`DirectGraph` republishes its rollback.** A `DirectGraph` edit whose document is refused puts its own
  list back. Once `EngineGraph` kept a refused document's systems as its model, that left the graph's model
  at the edit rolled back, which a later shape change would have placed; the restored model is now
  republished (§2.4).
- **A CLI session rendered an empty graph.** `cli/sessions/analyze-lsdj-sync.ts` built its two Game Boys
  through the process's `directGraph()` but rendered through its session's audio driver, which sets the
  shape on the session's `EngineGraph`. That graph held no systems, so its `perSystem` plan dropped both
  cores. The session now builds through a `DirectGraph` sharing `s.project.graph` (§2.4).
- **One session per process.** B1's translator lived in the engine, so two `bootSession`s in one process
  accumulated both sessions' systems. Each session now owns an `EngineGraph` over the one engine, so a
  second session's first edit frees the first's cores; `bootSession` and the shipped SDK types say one at a
  time (§6). BlipToaster boots a session per test and never interleaves two, so it is unaffected.
- **A load that threw left its cores unplaced.** A project load that threw partway (a later entry's
  settings failing to parse, say) left the systems it had adopted built and listed but parked, their
  publishes held back; the next shape change rebuilt the graph without them and freed their cores, wedging
  the project. A throwing load now publishes what it adopted, and pushes it to the kernel, before
  rethrowing (§3).
- **LSDj MIDI-out through the build flag alone.** Before B2 a loaded or duplicated LSDj in a MIDI-out mode
  was armed by a `setSerialOutCapture` after its build. That call is gone, and the build-time
  `serialOutCapture` flag is the only thing arming the capture (§3).
  `test-native/lsdj-midiout-build-flag.test.ts` covers a loaded one: adopted in MIDI-out mode its NoteOns
  reach the host, and built without the flag it sends none.

