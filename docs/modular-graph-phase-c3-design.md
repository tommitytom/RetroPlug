# Phase C3: system nodes build their own cores

**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** built: section 8's five commits; what building
them changed is in the sections it changed, and what it found is section 10

A system's core was built before this phase by a call of its own beside the graph: TS hands `buildCore` a
`ConstructSpec` (the ROM as a path or patched bytes, the battery and savestate seeds, and an opaque JSON blob of the
construct-time fields), the engine boots the core and parks it under the system id, and the system node the next
document places takes it at activation. This phase moves the build into the node. A system node's settings name
everything its core is built from: the ROM and the seeds as **resources**, typed byte blocks TS puts into the engine,
and every construct-time field as a plain setting. The node builds its core at activation, `buildCore` and the parked
cores go, and a construct-time edit takes effect at once instead of waiting for the user to press Reset. It is Phase C's
third piece ([modular-graph-design.md](modular-graph-design.md) section 8), after C1's script node packs
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
- **Resources** exist in the graph layer
  ([ResourceTable.hpp](../packages/graph-runtime/native/src/graph/ResourceTable.hpp)):
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
| `sram`, `state` | Optional `"res:N"` seeds, an `sram` and a `state` resource; given both, the savestate wins, as it does at the core |
| `romName` | Optional: the ROM's file stem, which Mesen stages a Master System or Game Gear ROM under (its battery stem). A node that sends none stages under `system-<id>`, so two unnamed systems never share one |
| construct-time | NES `sdRoot`, `fifo`, `fifoDepth`, `fifoBytesPerSecond`, `n8Link`, `n8LinkRttUs`, `channelExportMode`; SMS `enableFm`, `gameGear` |
| C2's | The live settings and params, unchanged |

The construct-time fields are optional in the native settings, an absent one building the core at its default, and
`buildGraph` writes every field the core-config role carries: the stored value where the node takes it, else the node's
own default, which the role schema fills for a fresh config. `gameGear` is the platform's rather than the role's: it is
written `true` for a Game Gear and left out otherwise, and a system's platform never changes under a live id, so it
flips no key either. So no document flips a field between absent and present under one system id, which matters because
the key tells the two apart. Their enums are labels as the role configs spell them, read by C2's `knobs::Label`.

**`rebuild_key`** covers `system`, `romName` and every construct-time field, so a change to any of them rebuilds the
node (and the system nodes keep `stateless_prepare`: a live knob edit still never makes `setGraph` wait). The resource
fields need not be in it: `node.hpp` compares the `res:N` refs a prepared settings object names with the pins its
instance holds and answers a rebuild itself, so a moved resource rebuilds the node whatever its type's key says. The
new instance claims the snapshot slot under its `system` id at activation, refusing a live id, so a construct-time
change under the id an older instance still holds fails the document loudly ("system N is live") rather than doing
nothing. TS moves to a new id for every rebuild.

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
  embedded: "mgb" }` (the built-in ROM). A file that cannot be opened or cannot be read is answered here, before any
  document is written, and reading it whole is what tells the two empties apart: the read goes by the size the stream
  reports, so a path naming a directory (which opens, and then fails every read) and a read that stops short are
  refusals rather than the short or empty payload they would otherwise be. A file that opens and genuinely holds
  nothing is not one: an empty payload is a resource, so a zero-byte `.sav` beside a ROM is a fresh cartridge rather
  than a load failure, while a zero-byte ROM is refused since it has nothing to boot.
- **`releaseResource(id)`**, the owner's drop: idempotent.

The types are `rom`, `sram` and `state`; a node refuses a resource of another type. An empty payload is legal (a
blank battery is one, whether TS put the bytes or the file the engine read holds nothing), and the `rp_bytes` a node
reads for it carries a valid data pointer at size 0, since a consumer forms a range from `(data, data + size)` and that
arithmetic is undefined on a null pointer.

**A node holds a resource its settings name** through a host interface, `rp.host.resources/1` (`acquire` a pin by id,
`release` it). A system node acquires its `rom`, `sram` and `state` at create, on the control thread, and releases them
when it is destroyed, at reclaim, on the control thread. A resource field that moves is a rebuild `node.hpp` decides
itself, so a resource changes only by a rebuild, and no pin is taken or dropped on the audio thread; a prepare that
keeps the same refs copies the instance's own pins into the new settings object, so a knob edit keeps them. A missing,
dropped or wrong-typed resource fails the node's create. A plan's own binding pin could not serve instead: the executor
binds a port view (`rp_port_view.u.resource`) just before `process`, `prepare` or `step`, which is after the planner has
created and activated the node, and a system node builds its core at activation.

**TS owns them per system.** A store entry owns its ROM resource and the seeds it booted from for as long as the system
lives, since every later document names them too, and drops them when the system is removed or rebuilt under a new id.
Every boot puts its own ROM, read again from its source (a path, an embedded marker or the asset roles' folded bytes),
so a rebuild after a ROM file changed on disk boots the new bytes and a duplicate gets a resource of its own; no two
systems share one. A dropped resource is freed when the plan that last ran its node is reclaimed. Holding the seeds
costs nothing extra: the node pins them for its lifetime regardless. A boot that stops short gives back every handle
it took: a put the engine refuses answers its reason, a put that throws is given back and rethrown, and each release is
offered on its own so a transport that died on one cannot strand the rest.

**The bytes come from** a path (a plain ROM, a `.sav`, a savestate file), the asset roles (`lsdj-assets`,
`risa-assets`, `bliptoaster-assets` fold their overrides into ROM bytes, and `lsdj-sync`'s empty-save seeding becomes an
`sram` resource), a zip import's blobs, or the published snapshot (`readSram` / `readState`, for a reset or a
duplicate).

## 5. The native side

**Create** (control thread) parses the node's settings field by field, C2's machinery (optional fields, refused ranges
and labels, so one bad field never defaults the rest), refuses a node naming no system or no ROM, and acquires its
resources, checking their types.

**Activate** (control thread, inside `setGraph`) builds the core through `rp.host.systems/1`, which replaces
`rp.host.cores/1`:

1. `build(...)` takes the request by value and hands the backend a typed config made from the node's settings: SameBoy
   a `SameBoyConfig` with every knob present (the model and fast boot among them), Mesen a `MesenNesConfig`,
   `MesenSmsConfig` or `MesenGbaConfig` with the construct-time fields, the region and the accuracy pair, and both the
   ROM and seed bytes and `romName`. A node's pins are copied into that request once, and its bytes are moved into the
   core from there.
2. The backend constructs the core with that config and boots it: the core applies the model, fast boot, highpass and
   display group inside `onActivate`, before the ROM and the seeds load, then restores the seeds and enables the state
   snapshot. A loaded non-default model still boots as that model, never restarting afterwards; the node seeds the
   settings it built from (less what the config does not carry, a Game Boy's capture) as what the core already has, so
   its own first application finds every value equal and calls nothing, as in C2.
3. The host claims the snapshot slot under `system`, refusing a live id, records the core among the engine's live
   cores, and fills the plan's `SystemTable` entry for that id, so the table is complete before the plan is published.
   The claim comes after the build because it seeds the slot from the core: a GBA pays its boot before a full pool can
   refuse it.

The backends keep the console code (C4 moves it into the nodes); `SystemBuildSpec`'s opaque blob gives way to the typed
config, and the backends' ROM gates (`detectRomFormat`) stay.

**A failure to build** (the core's ROM gate, a Mesen boot that fails, a full snapshot pool, a live id) is the planner's
existing `activate-failed`, with the node id and a reason. A build outside the `setGraph` that activates it is refused
too, with its own reason: no document can cause it. Nor can one cause a build for an id the plan being built has no
slot for: that host bug is asserted, and refused through the same helper, which destroys the core it built and gives
back the slot and the live-core entry the claim took first, since a refusal that left them behind would hold that id
live for the rest of the process.
A refused document changes nothing: the running plan keeps running, and the cores the refused plan built are destroyed
as its instances unwind, on the control thread, releasing their slots. There is no "parked for a corrected document"
state; a retry builds again.

**Destroy** (at reclaim, control thread) goes through `rp.host.systems/1`'s `destroy`: releases the snapshot slot,
leaves the live-core map and deletes the core; the node's own destructor releases its resources.

**Unchanged:** the linked Game Boys' `set_peers` (the plan hands each node its peers' live cores at adoption, after
activation); the NES tap record (`Engine::syncTap` compares core addresses as well as ids, so a core that has gone is
never restored by id; the address half's stale-record hazard predates C3, section 10); the N8
mirror; MIDI routing; lanes; `setSampleRate` over the plan's cores, which are every live core while quiescent;
deterministic power-on, which happens inside `onActivate`. `setGraph` includes each new system's boot in one call, the
audio thread running the old plan meanwhile. A project load still boots N cores and plans once.

## 6. The TS side

**The store.** An entry's `built: { type, exportMode, linkGroup }` gives way to its platform (detected by
[platform.ts](../packages/retroplug/src/platform.ts), which gives the node type), the `romName` its boot answers
([systemResources.ts](../packages/retroplug/src/graph/systemResources.ts) `romNameOf`) and its resources
`{ rom, sram?, state? }` (the handles `bootSystem` put, counted); the export mode and the link group come from
its config, where they started. Every verb that called `buildCore` keeps its orchestration over the new calls
(put the resources, allocate the id, change the model, publish):

| Verb | Resources | Carries |
|---|---|---|
| add, load, replace | the ROM by path or embedded, folded by the asset roles | the battery from its `.sav` (an `sram` seed by path) |
| swap ROM | the new ROM; an `sram` seed from `readSram` | the live battery |
| duplicate | the ROM put again from its source; a `state` seed from `readState` | the whole machine |
| reload, reset | the ROM put again from its source; an `sram` seed from `readSram` | the battery |
| new SRAM | the ROM put again from its source; a blank `sram` seed | nothing |
| load state, load SRAM | the ROM put again from its source; the file's bytes as a `state` seed (load state) or an `sram` seed (load SRAM) | the file |
| project load | each system's ROM; the archive's blobs | what the archive saved |
| a construct-time edit | the ROM put again from its source; an `sram` seed from `readSram` | the battery |

Every boot reads its ROM again, so a file a user rebuilt on disk is what Reset, New SRAM, Load SRAM, Load State, a
construct-time edit and a duplicate boot (the pre-C3 behaviour, when the build took the `romPath` for every in-place
verb); an asset role's hook folds its current bytes, so a changed override is the image booted. An edit's resources
are released only once its document has planned: a dropped resource cannot be bound by a new instance, so the store
publishes first and drops afterwards. A release that throws there is said rather than thrown - the document's answer is
the caller's, not what the transport did to a handle afterwards - and the pool keeps a handle until the engine takes it
back. `discard` drops one system's resources at once: the document it was in never planned, so nothing pinned them.
Every release loop guards each handle that way, `clear`'s and `discard`'s included: both release once their entries
have moved on, so a throw that escaped one would strand the handles behind it with nothing left to give them back - and
`discard` releases inside a project load's retry, where that throw would fail a load whose list is already shortened.

**A construct-time edit** is the reset path, run by `setRoleConfig` itself: the `systemKnobs` table gains a third
column beside settings and params, and when one of its fields moves the store rebuilds the system under a new id with
the new settings, carrying its battery, and answers whether that build planned. Its two sides are `systemKnobs`: the
stored config as written, which is what the running document carried (a param-only edit stores its parse unpublished,
and reaching that path means its construct fields are the values the live core was built with), against the edit's
schema parse. Every field the core-config role carries is written in full (`gameGear` is the platform's, and a
platform never changes under a live id), so a sparse stored config names the same values a fresh one does and a live
knob edit stays live rather than rebuilding; a stored value the schema repairs becomes one the node takes, which the
running core was not built with, so that edit takes the rebuild it needs. A stored value the role schema would repair on
the way in (a number clamped or rounded into range) is one the running node never took, so its place in the document is
the node's default: that matches the old repair where the repair was the default, and differs where it clamped or
rounded. An unknown label keeps its old meaning, since the schema's default for one is the native default.

**The id a rebuild replaced still resolves for `reset`.** An in-place rebuild that keeps the system's identity
(`reset`, reload, new SRAM, load state, load SRAM, a construct-time edit) records `old -> new` in the store's
`replacedBy` map, and `reset` is the one verb that reads it: a `reset(oldId)` follows a chain of rebuilds to the live
system, resets that system and answers its id. That is what keeps the pre-C3 contract working, where a construct-time
`setRoleConfig` was followed by `reset(id)` to learn the id carrying the new settings (section 10). A user's ROM
replacement (`replaceSystem`, `swapRom`, `loadRom` / `loadMgb`) is not one: it puts a system of its own in the slot and
records nothing, so `reset` of the id it replaced answers null, as it did before C3. An entry goes when the live system
its chain ends in does (`removeSystem`, `discard`, `clear`), and the map is capped at 128 entries, because a long
session of resets would otherwise keep every id it ever used. Every other verb treats a replaced id as gone, so a stale
handle cannot remove or edit a system it never named (a second `setRoleConfig` on it answers false). Nothing serializes
the map: a project load's `clear` empties it.

**A refused publish names the node.** The store puts its model and its focus back as they were, gives back what the
edit put, sends the restored model again and reports the reason, the failure the user saw when a core did not boot. A
publish that throws (an engine or transport fault, which the graph counts as refused) takes the same rollback before
the error reaches the caller. A project load publishes once for every system, as today; a refusal naming `sys-K` or one
of its protocol nodes (`sys-K/<kind>`, `sys-K/<kind>/out`) drops system K with a load note and publishes the rest, so
one unreadable ROM still costs only its own system. A refusal that names no node cannot be charged to a system, so
nothing is dropped and the load warns that the systems it lists do not run: an unparsable document, a tap that arms a
mode or a system outside the slots, two system nodes naming one system, planning out of memory.

**`buildGraph`** writes `system`, `rom`, `sram`, `state`, `romName`, every construct-time field the core-config role
carries (and a Game Gear's `gameGear`), C2's knobs and the params. **`ConstructSpec`** stays as TS's description of what
to boot, no longer a wire type: one helper
([systemResources.ts](../packages/retroplug/src/graph/systemResources.ts)) turns it into the resources both the store
and `DirectGraph` name, with the node type and ROM name their documents write, so the native tests that hand
`DirectGraph.add` a literal spec barely change. Its `core` names the kind of core whose role config `coreConfig` is and
selects no core. **The mock backend** implements `putResource` and `releaseResource`, takes native's rule that a resource
carries exactly one source (a null one counting as none) and quotes the caller's path and marker as native does, and
refuses a document naming an unknown or dropped resource, a wrong-typed one, a bad `res:` spelling or a system node with
no ROM, and one that moves a system node's rebuild key under an id it already places (native's "system N is live"). The
render path and every host go through the store as today.

**Persistence:** nothing changes. A project saves roles and blobs; resources live only at runtime. No migration.

## 7. Tests

- **Parity** (`pnpm test:native parity`) holds bit for bit at every commit, nothing re-recorded: the core boots from the
  same config in the same order relative to its seeds. A moved case is a finding to explain.
- **`retroplug-engine-graph-test`**: each console's node, built from resources, renders what a core built directly from
  the same bytes renders, bit for bit, seeds included (a Game Boy plain, with a battery seed and with a savestate seed;
  a non-default model under a savestate; a NES with its region, export mode and expansion volume; an SMS with FM off,
  its battery and a `romName`; a GBA; an LSDj); each type's every build knob, away from its default, is read back off
  the built core, with a battery-backed cart's seed saved back by the core it reached; and each construct-time field's
  fallback, read out of `buildGraph.ts`, builds the core its absence builds. A Master System's two nodes stage under
  stems of their own. A missing resource, an unrecognised ROM, a live id, a full snapshot pool and a build outside
  `setGraph` each refuse the document and leave the running plan, the pins and the slots as they were (a dropped or
  wrong-typed resource is `retroplug-graph-test`'s, below). A resource put from a file that opens and is empty is
  created (a zero-byte `.sav` is a blank battery) while a file that cannot be opened or read is refused - the reader's
  own case, on a path that is absent, one that is a directory, an empty file and a payload bigger than the chunk it
  reads by - and so is an empty ROM. A construct-time change under the same id is refused and under a new id rebuilds;
  a document changing only a param or a live knob keeps the core; every pin and every snapshot slot comes back after a
  removal, a rebuild and a refused plan; ThreadSanitizer and AddressSanitizer over a system added and removed while an
  audio thread plays.
- **The goldens** ([build-graph-goldens.test.ts](../packages/retroplug/test/graph/build-graph-goldens.test.ts),
  [build-graph-role-goldens.test.ts](../packages/retroplug/test/graph/build-graph-role-goldens.test.ts) and
  `BuildGraphGoldens.test.cpp` in the engine-graph binary): the documents `buildGraph` writes for the B1 matrix and for
  its own roles record, held whole and re-recorded when a change moves them (a document's `res:` ids renumbered in one
  pass, since two can swap places); and every one of those documents planned on a real Engine, the matrix a quiet
  block each and the roles record twenty driven blocks each.
- **`retroplug-graph-test`**: `rp.host.resources/1` with a toy node (acquire at create, release at destroy, a moved ref
  rebuilding, a knob edit keeping the pin, a refused plan unwinding its new pins, a dropped resource readable until the
  plan that pinned it is reclaimed, pins balanced, and the field walk that names each field letting what its callers
  build throw out of it, so a create's catch sees it as a refusal rather than a `noexcept` terminate).
- **`pnpm test`** ([store-resources.test.ts](../packages/retroplug/test/systems/store-resources.test.ts)): each verb's
  resource and publish order, the release after a publish, the rollback after a refusal, a restore republish refused
  too, a project load dropping its bad system, the construct-time reset, a construct-time edit to the value already
  stored moving nothing, a publish that throws rolling back as a refusal does, a put that throws partway through a boot
  giving back what that boot put and rethrowing, a release that throws finishing the rollback it is in and leaving the
  verb's own answer alone, `clear` and `discard` offering every handle either holds when one release throws, a sparse
  stored config's live edits, a stored value the schema repairs taking its rebuild, a user's ROM replacement recording
  no replaced id so `reset` of the id it took answers null, DirectGraph's add/replace/remove and a DirectGraph publish
  that throws rolling back as a refusal does, every put balanced by a release; and the mock backend's own refusals
  ([meta.test.ts](../packages/retroplug/test/meta.test.ts)): a resource spec with no source or more than one, a null
  source counting as none, a path that is there and empty becoming an empty resource (a zero-byte `.sav`) while an
  empty one for a `rom` is refused, and a system node's rebuild key moved under a live id (native's "system N is
  live").
- **`test:native`** ([app-construct-edit.test.ts](../packages/retroplug/test-native/app-construct-edit.test.ts)): a NES
  FIFO profile edit changes the queue's capacity with no Reset, an export-mode edit makes the per-channel streams
  appear, an SMS FM edit silences an FM song, a sparse stored config keeps a live edit, a param and a later live edit
  live, a ROM rewritten on disk is what reset and a construct-time edit boot and a ROM deleted since fails its rebuild
  again, duplicate/reset/load-state carry what they carry today, and a project with one unreadable ROM loads the rest;
  and a ROM beside a zero-byte `.sav` boots a fresh cartridge, plays and resets, and a project naming it loads whole
  ([app-empty-sav.test.ts](../packages/retroplug/test-native/app-empty-sav.test.ts)).
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
5. **Docs:** spec/00 to 04, 06, 07, 09, 10, 11, 13, AGENTS.md, the main design, the C2 and LSDj master-sync designs'
   pointers and `sdk-types.d.ts`.

Then the acceptance run. As C2: a prototype on a local branch first, the plan written from it, executed on the pi
models with Claude for the deep reviews and the final one.

## 9. Prototype questions, answered

- **What a boot costs per console.** `build` is construct + `onActivate` (ROM, battery, savestate) +
  `enableStateSnapshot`; `through setGraph` is the same through the engine (slot claim, one plan, published), Release
  at 48 kHz, median of 5, printed by `build/bin/retroplug-engine-graph-test "[bench]"`:

  | console | build | through setGraph | ROM |
  |---|---|---|---|
  | mGB (embedded) | 0.1 ms | 0.4 ms | 64 KiB |
  | LSDj | 0.4 ms | 0.8 ms | 1024 KiB |
  | NES (bliptoaster) | 0.8 ms | 1.0 ms | 288 KiB |
  | Master System (smsggdj) | 0.3 ms | 0.7 ms | 128 KiB |
  | GBA (nanoloop287d) | 15.3 ms | 16.5 ms | 8192 KiB |

  `setGraph` carries the boot, and only the GBA's is large enough to be noticed (its 8 MiB ROM is copied once, from
  the pinned resource into the request, and moved into the core from there). A project load of N systems still plans
  once. Strict parity
  held bit for bit at every commit with nothing re-recorded: the core boots from the same config in the same order
  relative to its seeds, so build order moved no bit.
- **How `rp.host.resources/1` sits in create, and whether the planner's bindings could serve.** As a settings field
  type, `rp::resource_ref<Type>`: `node.hpp` pins what it names at create and drops it at destroy, resolves the handle
  in the settings object create installs and in the one a prepare that keeps the refs installs (a prepare whose ref
  moved is a rebuild before `needs_rebuild` runs, and the hooks that run before create has pinned anything see only
  `ref`), and decides a moved ref as a rebuild itself. A plan's binding cannot serve:
  the executor binds a port view only just before a call into the node, after create and activation, and a system node
  builds its core at activation.
- **Whether any host, CLI session or consumer reaches `buildCore` or the `ConstructSpec` wire shape.** No. The store,
  `DirectGraph` (the CLI's analyze session and the native tests included) and the render path all boot from resources;
  the only remaining trace is `ConstructSpec` itself, which is TS's own description of a boot.
- **Whether a Master System's staged file and battery stem need anything beyond `romName`.** A refinement: `romName`
  is enough when it is sent, and TS always sends it (the ROM's stem, `mgb` for the embedded ROM). A node that sends
  none stages under `system-<id>`, unique within a Project, so two unnamed systems never share a battery; only a real
  stem lets two instances of one game share its own battery.

## 10. Found while building

The plan's list of departures is recorded across this document: the resources a document names, `rebuild_key`'s
narrowed list and the staged stem in section 3; `rp::resource_ref` and the pins a node holds in section 4; the build at
activation through `rp.host.systems/1`, the `SystemTable` it fills and the refusals in section 5; the store's verbs,
the construct-time rebuild and its comparison in section 6; the per-console twins and the goldens in
section 7; and the findings below.

- **A resource field needs no `rebuild_key` entry, and a knob edit keeps the pin.** `node.hpp` compares the prepared
  settings' `res:N` refs with the pins its instance holds, so a moved ref is a rebuild on its own, and a prepare that
  keeps them copies the instance's pins into the new settings object. That is stronger than section 3's rule, and it
  keeps `stateless_prepare` sound because it reads only what create fixed (sections 3 and 4).
- **Every rebuild reads its ROM again.** C3 first shared the old ROM resource across an in-place rebuild, which made
  Reset boot stale bytes after a user rebuilt a ROM on disk (and made an asset role's previous image stick). Every
  boot now puts its own ROM from its source, as the pre-C3 build did with the `romPath`, so Reset, New SRAM, Load
  SRAM, Load State, a construct-time edit and a duplicate all boot what is on disk now, and a rebuild of a ROM since
  deleted fails again as it did before C3, and a case in section 7's native list drives that reset.
- **A stored value the role schema would have repaired now takes the node's default.** There is no blob to carry a
  repaired value. `systemKnobs` writes the node's own default where the stored value is one it would refuse, and notes
  it, so an out-of-range number or a fractional integer takes that default where the blob took the clamp or the rounded
  value; an unknown label keeps its meaning because the schema's default and the native default agree (section 6).
- **`buildGraph` writes a system node's construct-time fields, and a construct-time edit is compared over the stored
  config as written.** A system node's rebuild key tells an absent construct-time field from one present at its default,
  and a project saved before the schema grew a field (every NES project saved before `n8Link`) carries a sparse config.
  A document that began writing the omitted defaults under the same id moved the key, so the engine refused the first
  live knob edit ("system N is live"), and a param-only edit's later publish too. Every field the core-config role
  carries is now written (the stored value, else the node's default), `gameGear` is the platform's (written for a Game
  Gear alone), and a platform never changes under a live id; the store compares `systemKnobs` over the raw stored config,
  which is what the running document carried, with the edit's schema parse: a sparse config adds no move, while a stored
  value the schema repairs takes the rebuild it needs. The mock backend refuses a rebuild-key move under a live id with
  native's text, so the mock tier can see this class of bug (sections 3 and 6).
- **The pre-C3 `setRoleConfig` + `reset(oldId)` pattern still works.** The plan's review found both consumer suites
  relied on it: they move a construct-time field and then call `reset` on the id they held to learn the id carrying the
  new settings (BlipToaster's `bootRomWith` does `setRoleConfig(id0, "mesen", { n8Link: "off" })` then
  `reset(id0) ?? id0`; NESVJ's `bootRegion` throws when the reset answers null). Since the edit rebuilds the system
  itself, that id names nothing any more; the store's `replacedBy` map keeps the forward and `reset` follows it to the
  live system (section 6), so neither consumer changes. Only a rebuild records that move: an id a user's ROM
  replacement took the slot from still answers null, and editing that id again (a second `setRoleConfig`) is refused,
  since the edit belongs to the live id `systems()` lists.
- **The gain's first value is snapped, and a node's core is always fresh.** A core a node builds is silent in the
  block its gain first lands in, so the first-value snap is observable only through a savestate seed taken mid-note:
  the case that pins it builds the node with that seed at a 64-frame boundary whose next 64 frames sound (section 7).
- **A stale NES-tap record can still match a fresh core allocated where the armed one was freed.** `armedTap_`
  compares the core's address, and the record survives a drop and reclaim. The hazard predates C3; TS never gets there
  because its system ids only go up and every edit that drops a core re-arms, which drops the record. The suite pins
  the new-id replacement, not the allocator-dependent half (section 5).
- **The `SystemTable`'s pre-fill is load-bearing for reuse.** A node reused from the previous plan (its key unchanged)
  returns from `activate` without building, so `publishGraph` seeds its slot's core from the live cores before the
  planner runs; the table is complete before publication and nothing writes it afterwards (section 5).
- **Two `SystemFactory` instances existed while both paths did.** The Engine owns the one every node builds through;
  the RPC service held a second for `buildCore` until the old path went (section 5; commit 4 deleted the second).
- **A golden runner's synthetic GBA ROM could not boot through a node.** A KiB of zeros was enough for a directly
  constructed `MesenGbaSystem`, but a node's build gates the bytes, so the runner carries the Nintendo logo. The
  golden documents' `res:` ids also had to be renumbered in one pass, since two can swap places (section 7).
- **The unnamed Master System stem.** `romName` is optional for the build and not for battery identity: the fallback
  is `system-<id>`, unique within a Project but not across the several Projects one process holds (sections 3 and 9).
- **A refused document still activates every other system in it.** The planner creates and activates every node even
  after one has failed, so a document of N systems with one bad ROM pays N-1 boots for the refusal, and a project load's
  recovery (drop the refused system, publish the rest) pays them twice. It buys one pass over every bad system in the
  document, and only a GBA's boot is large enough to notice (section 9).

The follow-ups after the landing found these, each recorded the way the findings above are:

- **An empty resource is legal, and its `rp_bytes` is never null.** A blank battery ("new SRAM") is a zero-length
  payload, and a file that opens and holds nothing is that same resource: only a file that cannot be opened or cannot
  be read is a refusal, which is what keeps a zero-byte `.sav` beside a ROM a fresh cartridge while a zero-byte ROM is
  still refused (sections 4 and 7). A consumer forms a range from `(data, data + size)`, so the table points one at a
  static byte rather than at null: a node reads size 0 with a valid pointer.
- **A file is read whole, by the size it reports, and that size is never an allocation.** Opening and reading are two
  questions and a stream answers only the first: a path naming a directory opens, and it is the read that fails. A copy
  run to the end cannot tell a file that ended from one whose read stopped partway, and the short or empty vector that
  answers either would become a blank battery the core auto-saves over the user's real save - so the reader asks for
  the size the stream reported and refuses whatever does not add up to it. That size is only a target: `lseek` on a
  directory succeeds with the largest offset the platform can name rather than an error, so the read goes in chunks and
  nothing reserves what the stream claimed. A pseudo-file is refused along the way, and rightly for a ROM or a battery:
  what a pipe or a character device reports is no size at all (`/dev/zero`, which a copy to the end slurps forever,
  among them), and what a sysfs attribute reports is its buffer rather than its content.
- **A resource handle is resolved before `needs_rebuild`, and a moved ref skips that hook.** The prepare resolves each
  resource field into the settings it installs first, then asks `needs_rebuild`, so a node that reads a resource there
  sees a handle on both sides; a moved ref is already a rebuild, so the hook does not run on the document that moved it
  (section 4, and section 9's answer).
- **The named-field walk may throw, and a create's failure is its caller's refusal.** `forEachNamedField` has no
  `noexcept`: its callers build the reason strings and take the pins inside it, and an allocation that fails there must
  reach the catch around `createFn`/`prepareFn`, which turns it into that node's refusal rather than a terminate
  (sections 4, 5 and 7).
- **The build request is taken by value.** A node's pins are copied into it once and its bytes are moved into the core
  from there, so a GBA's 8 MiB ROM is not copied a second time (sections 5 and 9).
- **The not-in-the-slots refusal gives everything back.** That host bug destroys the core it built and gives back the
  snapshot slot and the live-core entry before the reason goes out, so a refusal cannot hold that id live for the rest
  of the process (section 5).
- **The configs' ROM fields went with the old path.** `SameBoyConfig`'s `romPath`, `embedRom`, `embeddedRom` and
  `romBytes`, the Mesen configs' `embedRom` and `romBytes`, and `SystemBase::romPath()` with its overrides had no reader
  left once a node handed the build its bytes and `romName` (section 5); Mesen's `romPath` stays, since its consoles
  stage and log under it.
- **Each type's build knobs and a battery seed have cases of their own.** Every construct-time knob of each console,
  away from its default, is read back off the built core field by field, and an LSDj's `sram` seed is saved back by the
  core it reached, so a knob a build forgets and a seed a build drops both fail (section 7).
- **A refusal names the node it belongs to, or no node at all.** A system's own node is `sys-<id>` and its
  protocol nodes `sys-<id>/<kind>` (and `sys-<id>/<kind>/out`), and the planner reports a node's own failure under that
  id, so a project load's retry drops the system either way. The refusals that name no node (an unparsable document, a
  tap that arms a mode or a system outside the slots, two system nodes naming one system, planning out of memory)
  cannot be charged to one, and the load then warns that the systems it lists do not run (section 6).
- **The mock backend takes native's resource-source rule and its quoting.** A spec carries exactly one of `bytes`,
  `path` and `embedded` (a null counts as absent, as the optional does on the wire), and a refusal quotes the caller's
  path and marker as native's does, so the mock tier sees the class of bug the real one reports (sections 4 and 6).
- **A release that throws finishes the loop it is in.** A publish that throws: the store restores its model and focus,
  gives back the edit's puts, resends the restored model and rethrows the first failure; a throwing release is logged
  and the next handle still offered, on that rollback and after a planned document alike (where the verb's own answer
  is already settled). The same guard holds in `clear` and `discard`, which release once their entries have moved on
  and have no rollback left to finish. A release that dies keeps its handle: the pool forgets an id only once the
  engine took it back, so the one way left to drop that resource is not lost (section 6).

## 11. Left as it is

- **C4:** the console code moves into the nodes; `SystemBase` shrinks to the capability interfaces.
- **A stable node id across a ROM change:** a graph UI may later want a node to keep its id when its ROM changes. C3
  keeps every core generation a new id.
- **The seeds' memory:** a node holds its seed bytes for its lifetime, a savestate per system.
- **The pinned ROM's memory, and a removed system's resources until the next edit.** A system's `rom` resource stays in
  the engine's table for the system's whole life, one more full copy beside the vector the build hands the core and
  Mesen's own: a 32 MB GBA ROM is +32 MB per system, per plugin instance. The plugin and SDL reclaim only inside
  `setGraph` and `loadScriptPack`, so a removed system's core, ROM and seeds stay until the next one of those (the core
  always lived that long; its bytes are new). C4, which moves the console code into the nodes, could let a core borrow
  the pinned bytes rather than copy them.
- **The SD card:** a NES's default SD card is a fresh scratch directory per core, so a rebuild starts it empty, as a
  reset does today.
