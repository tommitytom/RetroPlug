# Phase C3: system nodes build their own cores

**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9

A system's core is built today by a call of its own beside the graph: TS hands `buildCore` a `ConstructSpec` (the ROM
as a path or patched bytes, the battery and savestate seeds, and an opaque JSON blob of the construct-time fields), the
engine boots the core and parks it under the system id, and the system node the next document places takes it at
activation. This phase moves the build into the node. A system node's settings name everything its core is built from:
the ROM and the seeds as **resources**, typed byte blocks TS puts into the engine, and every construct-time field as a
plain setting. The node builds its core at activation, `buildCore` and the parked cores go, and a construct-time edit
takes effect at once instead of waiting for the user to press Reset. It is Phase C's third piece
([modular-graph-design.md](modular-graph-design.md) section 8), after C1's script node packs
([modular-graph-phase-c1-design.md](modular-graph-phase-c1-design.md)) and C2's live knobs
([modular-graph-phase-c2-design.md](modular-graph-phase-c2-design.md)), whose section 3 left exactly this: "the
construct-time fields ... stay in the build spec until C3 builds cores in their nodes".

## 1. Before C3

- **`buildCore(spec, id)`** ([EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp)) runs on
  the control thread, may run while the audio thread plays, and refuses id 0 and a live id. It turns the wire spec into
  a `SystemBuildSpec` (`toBuildSpec`: the SRAM seed is `sramBytes` else the file at `savPath`, the state seed likewise),
  and `SystemFactory::build` boots the core: a `SameBoySystem` or a Mesen system constructed, `onActivate`d (the ROM,
  the battery, then the savestate restored) and `enableStateSnapshot`ed. `Engine::parkCore` claims the core's snapshot
  slot and parks it; the answer is `{ type, exportMode, linkGroup }`, the node type and two values cast out of the
  concrete core (the link group a pure echo of the build spec: the cable is TS's wiring).
- **The settings blob** carries the backend's "system" role config. SameBoy decodes all of it
  (`decodeSameBoyRoleConfig`: the model, fast boot and the display group applied before `onActivate`, so a loaded
  non-default model never restarts the core it just restored); Mesen's NES decodes the construct-time fields, its region,
  sprite limit, APU latency and expansion volume (not the accuracy pair); its SMS reads `enableFm`; its GBA reads
  nothing. One unreadable field defaults the whole struct, which is why TS now builds the blob from its role schema's
  parse (`SystemsStore.configForBlob`, C2's section 10).
- **The construct-time fields** are read only in `onActivate`: NES `sdRoot`, the FIFO profile `fifo` and its overrides
  `fifoDepth` and `fifoBytesPerSecond`, the N8 link `n8Link` and its round trip `n8LinkRttUs`, `channelExportMode`; SMS
  `enableFm`, and Game Gear (the platform). An edit of one is stored and does nothing until the user resets the system
  (the System menu's Reset row is the only caller of `SystemsStore.reset`).
- **A system node** ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)) has settings `{ "core":
  <id>, ...C2's knobs }`, `rebuild_key` the core id. Its `activate` takes the parked core once through
  `rp.host.cores/1`; its destructor hands it to the engine's `released`, which re-parks a core a refused document took
  (so a corrected document can place it) or releases its snapshot slot so the node deletes it. A document that plans
  frees every core still parked; `Engine::setSampleRate` re-rates the parked cores as well as the plan's.
- **The store** ([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)) calls `buildCore` from `construct`
  (add, load, replace, swap ROM), `duplicateSystem`, `rebuildInPlace` (reload, reset, new SRAM, load state, load SRAM)
  and `adopt` (a project load: one build per system, then one publish). System ids are never reused, so every rebuild
  is a new id and a new node id `sys-<id>`. The load-time roles' `onConstruct` hooks fold asset overrides into
  `romBytes` (`lsdj-assets`, `risa-assets`, `bliptoaster-assets`) or seed an empty LSDj battery (`lsdj-sync`).
- **Resources** exist in the graph layer ([ResourceTable.hpp](../packages/native/src/graph/ResourceTable.hpp)):
  immutable byte blocks with one owner and a pin per plan binding, freed once the owner has dropped them and no pin is
  left, ids never reused. Nothing creates one over RPC yet, and no node reads one from its settings: main design
  section 2.5 planned that host service "with the first system node".

## 2. Decisions taken

The user's, on 2026-10-30:

- **A construct-time edit resets the system at once, carrying its battery**: exactly what pressing Reset does today,
  without the extra step. Not applying the field live where a setter happens to exist (a mixed rule), not carrying the
  savestate across (a state taken under one build restored into another), and not leaving it manual.
- **Settings and resources** (approach A): a system node's settings name the ROM, the seeds and every construct-time
  field, and the node builds its core at activation. Not staging the bytes out of band under the system id (the
  parked-core pattern with bytes instead of a core, and a document still incomplete without the calls before it), and
  not keeping `buildCore` with only the fields moved (the nodes would still not build their cores).

## 3. What a system node's document carries

Every system node type (`rp.sameboy.gb`, `rp.mesen.nes`, `rp.mesen.gba`, `rp.mesen.sms`) takes:

| Setting | Meaning |
|---|---|
| `system` | The system id TS allocated (it replaces `core`): the snapshot slot's key, and why the node id is `sys-<id>` |
| `rom` | `"res:N"`, a `rom` resource. Always a resource: a node never touches the filesystem |
| `sram`, `state` | Optional `"res:N"` seeds, an `sram` and a `state` resource; given both, the savestate wins, as today |
| `romName` | Optional: the ROM's file stem, which Mesen stages a Master System or Game Gear ROM under (its battery stem) |
| construct-time | NES `sdRoot`, `fifo`, `fifoDepth`, `fifoBytesPerSecond`, `n8Link`, `n8LinkRttUs`, `channelExportMode`; SMS `enableFm`, `gameGear` |
| C2's | The live settings and params, unchanged |

The construct-time fields are optional, like C2's settings: an absent one builds the core at its default. Their enums
are labels as the role configs spell them, read by C2's `knobs::Label`.

**`rebuild_key`** covers `system`, every resource field and every construct-time field, so a change to any of them
rebuilds the node (and the system nodes keep `stateless_prepare`: a live knob edit still never makes `setGraph` wait).
The new instance claims the snapshot slot under its `system` id at activation, refusing a live id, so a construct-time
change under the id an older instance still holds fails the document loudly ("system N is live") rather than doing
nothing. TS moves to a new id for every rebuild, as it does today.

**What goes:** `buildCore` and its facts (TS writes the node type, the export mode and the link group itself);
`BackendConstructSpec`, `toBuildSpec`, `SystemBuildSpec`'s blob and the three role-config decoders; the parked cores
(`parkCore`, `takeCore`, the re-park of a core a refused document took, the free of what stayed parked, re-rating parked
cores); the store's edit-then-Reset step. **What stays:** the node id convention, the command ring, the snapshot
registry as the only read door, and the rule that the control plane never touches a live core.

## 4. Resources

**Two RPCs** join the emulator facet, called directly on the control thread like `setGraph`:

- **`putResource(spec) → { id } | { error }`** creates an immutable byte resource in the engine's `ResourceTable`. The
  spec is one of `{ type, bytes }` (a patched ROM, a battery or savestate seed), `{ type, path }` (native reads the
  file, so an unpatched ROM or a `.sav` never crosses the RPC as bytes; a GBA ROM can be 32 MB) or `{ type: "rom",
  embedded: "mgb" }` (the built-in ROM). An unreadable file is answered here, before any document is written.
- **`releaseResource(id)`**, the owner's drop: idempotent.

The types are `rom`, `sram` and `state`; a node refuses a resource of another type.

**A node holds a resource its settings name** through a host interface, `rp.host.resources/1` (`acquire` a pin by id,
`release` it). A system node acquires its `rom`, `sram` and `state` at create, on the control thread, and releases them
when it is destroyed, at reclaim, on the control thread. Every resource field is in `rebuild_key`, so a resource changes
only by a rebuild, and no pin is taken or dropped on the audio thread. A missing, dropped or wrong-typed resource fails
the node's create.

**TS owns them per system.** A store entry owns its ROM resource and the seeds it booted from for as long as the system
lives, since every later document names them too, and drops them when the system is removed or rebuilt under a new id.
A rebuild that keeps the same ROM reuses its resource, and a duplicate shares the source's, so TS counts references to
each and drops a resource when its count reaches zero. A dropped resource is freed when the plan that last ran its node
is reclaimed. Holding the seeds costs nothing extra: the node pins them for its lifetime regardless.

**The bytes come from** a path (a plain ROM, a `.sav`, a savestate file), the asset roles (`lsdj-assets`,
`risa-assets`, `bliptoaster-assets` fold their overrides into ROM bytes, and `lsdj-sync`'s empty-save seeding becomes an
`sram` resource), a zip import's blobs, or the published snapshot (`readSram` / `readState`, for a reset or a
duplicate).

## 5. The native side

**Create** (control thread) parses the node's settings field by field, C2's machinery (optional fields, refused ranges
and labels, so one bad field never defaults the rest), and acquires its resources, checking their types.

**Activate** (control thread, inside `setGraph`) builds the core through `rp.host.systems/1`, which replaces
`rp.host.cores/1`:

1. `build(...)` hands the backend a typed config made from the node's settings: SameBoy a `SameBoyConfig` with every
   knob present (the model and fast boot among them), Mesen a `MesenNesConfig`, `MesenSmsConfig` or `MesenGbaConfig`
   with the construct-time fields, the region and the accuracy pair, and both the ROM and seed bytes and `romName`.
2. The backend applies before `onActivate` what it applies there today, boots, restores the seeds and enables the state
   snapshot. A loaded non-default model still boots as that model, never restarting afterwards; the node's own first
   application then finds every value equal and applies nothing, as in C2.
3. The host claims the snapshot slot under `system`, refusing a live id, and records the core among the engine's live
   cores.

The backends keep the console code (C4 moves it into the nodes); `SystemBuildSpec`'s opaque blob gives way to the typed
config, and the backends' ROM gates (`detectRomFormat`) stay.

**A failure to build** (the core's ROM gate, a Mesen boot that fails, a full snapshot pool, a live id) is the planner's
existing `activate-failed`, with the node id and a reason. A refused document changes nothing: the running plan keeps
running, and the cores the refused plan built are destroyed as its instances unwind, on the control thread, releasing
their slots. There is no "parked for a corrected document" state; a retry builds again.

**Destroy** (at reclaim, control thread) releases the snapshot slot, leaves the live-core map, deletes the core and
releases the node's resources.

**Unchanged:** the linked Game Boys' `set_peers` (the plan hands each node its peers' live cores at adoption, after
activation); the NES tap record (`Engine::syncTap` compares core addresses, which monotonic ids keep apart); the N8
mirror; MIDI routing; lanes; `setSampleRate` over the plan's cores (the parked half goes); deterministic power-on,
which happens inside `onActivate`. `setGraph` now includes each new system's boot: the work `buildCore` plus `setGraph`
did before, in one call, the audio thread running the old plan meanwhile. A project load still boots N cores and plans
once.

## 6. The TS side

**The store.** An entry's `built: { type, exportMode, linkGroup }` gives way to its platform (detected by
[platform.ts](../packages/retroplug/src/platform.ts), which gives the node type) and its resources `{ rom, sram?,
state? }`, counted handles; the export mode and the link group come from its config, where they started. Every verb
that called `buildCore` keeps its orchestration over the new calls (put the resources, allocate the id, change the
model, publish):

| Verb | Resources | Carries |
|---|---|---|
| add, load, replace | the ROM by path or embedded, folded by the asset roles | the battery from its `.sav` (an `sram` seed by path) |
| swap ROM | the new ROM; an `sram` seed from `readSram` | the live battery |
| duplicate | the source's ROM (shared); a `state` seed from `readState` | the whole machine |
| reload, reset | the same ROM; an `sram` seed from `readSram` | the battery |
| new SRAM | the same ROM; a blank `sram` seed | nothing |
| load state, load SRAM | the same ROM; the file by path | the file |
| project load | each system's ROM; the archive's blobs | what the archive saved |
| a construct-time edit | the same ROM; an `sram` seed from `readSram` | the battery |

**A construct-time edit** is the reset path, run by `setRoleConfig` itself: the `systemKnobs` table gains a third
column beside settings and params, and when one of its fields moves the store rebuilds the system under a new id with
the new settings, carrying its battery.

**A refused publish names the node.** The store puts its model back as it was, releases what it put, and reports the
reason, the failure the user saw when `buildCore` returned null. A project load publishes once for every system, as
today; a refusal naming `sys-K` drops system K with a load note and publishes the rest, so one unreadable ROM still costs
only its own system.

**`buildGraph`** writes `system`, `rom`, `sram`, `state`, `romName`, the construct-time fields, C2's knobs and the
params. **`ConstructSpec`** stays as TS's description of what to boot, no longer a wire type: one helper turns it into
resources and node settings for both the store and `DirectGraph`, so the native tests that hand `DirectGraph.add` a
literal spec barely change. **The mock backend** implements `putResource` and `releaseResource`, and refuses a document
naming an unknown or dropped resource. The render path and every host go through the store as today.

**Persistence:** nothing changes. A project saves roles and blobs; resources live only at runtime. No migration.

## 7. Tests

- **Parity** (`pnpm test:native parity`) holds bit for bit at every commit, nothing re-recorded: the core boots from the
  same config in the same order relative to its seeds. A moved case is a finding to explain.
- **`retroplug-engine-graph-test`**: each console's node, built from resources, renders what a core built directly from
  the same bytes renders, bit for bit, seeds included; a missing, dropped or wrong-typed resource, an unrecognised ROM,
  a live id and a full snapshot pool each refuse the document and leave the running plan; a construct-time change under
  the same id is refused and under a new id rebuilds; every pin and every snapshot slot comes back after a removal, a
  rebuild and a refused plan; ThreadSanitizer and AddressSanitizer over a system added and removed while an audio thread
  plays.
- **`retroplug-graph-test`**: `rp.host.resources/1` with a toy node (acquire at create, release at destroy, a dropped
  resource refused, pins balanced).
- **`pnpm test`**: each verb's resource and publish order, the rollback after a refusal, a project load skipping its bad
  system, the construct-time reset, every put balanced by a release.
- **`test:native`**: each construct-time field lands with no Reset, observably (the export mode's lanes, the FM chip's
  sound, the FIFO profile, the SD root); duplicate, reset and load state carry exactly what they carry today; a project
  with one bad ROM loads the rest.
- **The acceptance run** as C2's: every suite, the Reaper leg, SDL, BlipToaster and NESVJ.

## 8. Landing

Five commits, each green:

1. **Resources over RPC:** `putResource`, `releaseResource`, `rp.host.resources/1`; the toy node's cases.
2. **System nodes build their cores:** a node whose settings name a `rom` resource builds its core at activation
   through `rp.host.systems/1`; `core` and `buildCore` keep working beside it. The twins and refusals.
3. **The TS switch-over:** the store, `buildGraph`, `DirectGraph` and the mock move to resources; the construct-time
   reset; the goldens re-recorded.
4. **The old path deleted:** `buildCore`, the parked cores, the facts, the blob and its decoders, `toBuildSpec` and
   the wire DTO.
5. **Docs:** spec/01 to 04, 06, 07, AGENTS.md, the main design.

Then the acceptance run. As C2: a prototype on a local branch first, the plan written from it, executed on the pi
models with Claude for the deep reviews and the final one.

## 9. Prototype questions

- What a boot costs per console (now inside `setGraph`), and whether build order can move a bit of parity.
- How `rp.host.resources/1` sits in node.hpp's create path (a C++ node's settings naming resources), and whether the
  planner's own resource pins can serve instead.
- Whether any host, CLI session or consumer (BlipToaster, NESVJ) reaches `buildCore` or the `ConstructSpec` wire shape.
- Whether a Master System's staged file and battery stem need anything beyond `romName`.

## 10. Left as it is

- **C4:** the console code moves into the nodes; `SystemBase` shrinks to the capability interfaces.
- **A stable node id across a ROM change:** a graph UI may later want a node to keep its id when its ROM changes. C3
  keeps every core generation a new id.
- **The seeds' memory:** a node holds its seed bytes for its lifetime, a savestate per system.
- **The SD card:** a NES's default SD card is a fresh scratch directory per core, so a rebuild starts it empty, as a
  reset does today.
