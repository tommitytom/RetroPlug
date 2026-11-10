# 03 — The TypeScript layer

`packages/retroplug/` is the TypeScript half of the build — the embodiment
of the thesis **"native owns bytes and cores; TypeScript owns meaning."** Everything a
user *decides* lives here: system identity and order, ROM classification, path/sibling/
suffix resolution, the project model and its serialization, the config schemas, routing
and role behaviour, and the whole UI. TS drives native through one narrow **`Backend`**
interface over resolved paths, opaque byte buffers, and the graph documents it builds;
it never asks native to make a policy choice.

The layer is organised as three concentric rings, each depending only on the one inside
it:

- **Pure kernels** — no `Backend`, no IO, unit-testable in isolation: `platform.ts`,
  `systemsList.ts`, `savPaths.ts`, `projectConfig.ts`, `projectPaths.ts`, `pathUtil.ts`,
  `midiRouting.ts`, `projectBinaries.ts`, `projectMissing.ts`, `recentList.ts`, the
  `*Serialization.ts` codecs, the zod models, plus `graph/buildGraph.ts` / `graph/protocolNodes.ts` /
  `graph/controller.ts`.
- **Stores** — a kernel wired to the `Backend`, owning live state: `systemsStore.ts`,
  `projectStore.ts`, `recentStore.ts`, `userConfigStore.ts`, `bindingsStore.ts`, plus the
  store-adjacent services `fileSelection.ts`, `fileWatcher.ts`, `sramAutoSave.ts`.
- **Runtime / host seams** — `backend.ts` / `realBackend.ts` (control-plane RPC),
  `audioDriver.ts` (the harness's render and profiling client, and the kit compiler's caller),
  `capability.ts` / `offline.ts` (the two promise-based job seams: a node's capability job, and the engine-owned
  offline pool's graph documents and render jobs), `renderJobs.ts` (the renders the UI started, kept in the store
  graph for
  the tile badge), the composition roots `pluginControlPlane.ts` / `appStores.ts` / `appHost.ts`, and the React/LVGL
  `ui/` tree.

Roles and the protocol nodes that run them are their own subject: **doc 04** covers them end to end;
this doc summarises them and links across. For the threading model (the command ring,
the `SnapshotRegistry` read door, the release ring, the QuickJS runtimes), see
**doc 01**; the seams below reference those concepts, they don't re-explain them.

---

## The `Backend` interface

[`src/backend.ts`](../packages/retroplug/src/backend.ts) is the **single** native
contract the application calls. It is deliberately narrow and deliberately grown: *if a
thing can be done in pure TS, it belongs in the application layer, not here.* It is
**synchronous** on purpose — in the real plugin these are in-process calls over the
txiki↔C++ bridge — which keeps application logic straight-line and trivially testable
against an in-memory mock. The exceptions are `openFileBrowser` (it waits on human input) and the job seams'
promises (`capability`, `offlineRun`, `engineRun`), which settle from a pump or a native wait rather than a timer.

The methods, grouped by concern:

| Group | Methods | What native does |
|---|---|---|
| **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `syncFile` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; an fsync for a streamed file before it is renamed into place; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
| **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
| **Emulator lifecycle** | `putResource(spec)` · `releaseResource(id)` · `setGraph(request)` · `loadScriptPack(request)` | Put an immutable byte block in the engine's `ResourceTable` (a ROM, a battery, a savestate) or drop the owner's hold on one; plan a graph document TS built (which systems run, in what order and wired how, each system node naming the resources its core boots from). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over these and the snapshot reads. Load a pack of script node types beside the built-in ones, or reload one, while audio runs ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): `{ ok, error, errors, types }`, the pack's types with their descriptors, or why not (the planner's `errors` when a reload's replan refused). Nothing in the app calls it yet, and the mock refuses it |
| **Job seams** | `capability(nodeId,op,args)` · `pumpJobs()` · `rejectPendingJobs(reason)` · `offlineRun(request)` · `engineRun(kind,spec,{onProgress,onLog})` | Promise-based jobs over the RPC's capability and offline job calls: a node's capability (C5) and a graph document run to completion on an engine-owned pool (D2), both settled by the one `pumpJobs` the UI's frame tick calls. `offlineRun`'s document may name only the built-in C++ packages (no script, system or host I/O node), and its promise settles from the tick or, in a host with none (the CLI, the Node addon, the test host), the native wait `createAudioDriver` turns on ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) sections 3 and 11). `engineRun` runs a render (D3, kind `"render"`): a promise of `{status, message, outputs, renderedMs}` that resolves for `done`, `cancelled` and `error` alike and rejects only on a refusal or a port that throws, with `onProgress` (a running maximum) and `onLog` (a job's console lines); both runs return a handle with `cancel()`, and the mock backend rejects `engineRun` (it has no engine to run one on). `setOfflineYield` makes the poll resume through a macrotask after a native wait (the CLI's render sets it, and `createAudioDriver` on Node, whose timers fire for any caller, so an awaited job there leaves Node's timers and I/O running) |
| **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path |
| **Live params** | `setParam(nodeId,paramId,value)` | Set a param of a node the last published plan places, at the start of the next block, clamped to its range: the store's live gain and a NES's expansion volume, on the system's node `sys-<id>`. A system's other knobs are its node's settings in the document `setGraph` plans, and the audio routing is the graph's wiring, so neither is a call ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)) |
| **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
| **Live reads** | `readState(id)` · `readSram(id)` · `getFrame(id)` | Pull the latest tear-free snapshot from the `SnapshotRegistry` (see doc 01) — never touches a live core |
| **Byte codecs** | `zip(entries)` · `unzip(bytes)` | miniz deflate/inflate — the only native part of `.rplg` framing (TS assembles every entry) |
| **LSDj sav codec** | `savFromJson(json)` | Run the version-aware sav codec; `savFromJson("{}")` yields a valid 128 KiB image |

The real adapter [`src/realBackend.ts`](../packages/retroplug/src/realBackend.ts)'s
`createRealBackend()` resolves `globalThis[Symbol.for("plugin")].__rpcSend` and wraps it in
a self-contained synchronous JSON-RPC `call()`
([rpcClient.ts:28](../packages/retroplug/src/rpcClient.ts#L28)). Binary rides as
`Uint8Array` in both directions (the QuickJS codec decodes a typed byte param straight into
`rfl::Bytestring`), so nothing is base64'd or number-array'd on this path. `putResource`'s adapter
([realBackend.ts](../packages/retroplug/src/realBackend.ts)) sends exactly one of `bytes`, `path` or
`embedded` (so native sees no null source it has to interpret) and narrows the answer to `{ id }` or
`{ error }`.
This same adapter serves all three hosts — the difference is only which
set of RPC facets is bound behind `__rpcSend` (see doc 01/02).

### `ConstructSpec`: TS's description of a boot

A boot takes a `ConstructSpec`
([backend.ts](../packages/retroplug/src/backend.ts)) in which **TS has
already resolved every path, suffix, sibling, and classification**: *native never sees a
suffix, never looks for a sibling, never classifies.* It is no longer a wire type: one helper
([graph/systemResources.ts](../packages/retroplug/src/graph/systemResources.ts)) turns it into the resources a
system node names, with the node type and ROM name the documents write, for the store, `DirectGraph` and the render
path alike.

| Field | Meaning |
|---|---|
| `romPath` / `embeddedRom` | The ROM file the engine reads, or a binary-baked marker (e.g. `"mgb"`) with `romPath = ""` |
| `platform` / `core` | What the ROM targets, which picks the node type (and with it the console its core is built as) via [`platform.ts`](../packages/retroplug/src/platform.ts), and the kind of core whose role config `coreConfig` is (it selects no core) |
| `savPath` / `statePath` | The exact battery file (loaded as the seed when it is there, and the auto-save target) and savestate to boot from, or `null` |
| `romBytes?` / `sramBytes?` / `stateBytes?` | Bytes TS holds (an asset role's patched image, a zip-import blob, a carried battery, a savestate read back from a live core); they win over the files at the paths. A `romBytes` image becomes the `rom` resource's bytes |
| `coreConfig?` | The system's core-config role config in the labels its node reads: its live knobs, and its construct-time fields, which become the node's settings (a value the node would refuse is left out) |

`openFileBrowser` is the odd one out and does **not** ride the RPC bridge: it uses the
UI-direct hooks `__rp_openFileBrowser` / `__rp_onFileBrowserResult` on the shared context
([realBackend.ts:32-55](../packages/retroplug/src/realBackend.ts#L32)), with a
single module-level `pendingBrowse` slot (one dialog at a time). Where the hook is absent
(the headless UI harness), a browse resolves `null` — inert, like the other `__rp_*` seams.

---

## The stores

Every store is a plain, framework-agnostic class with a uniform shape: a constructor
`(backend, onChange?, …)`, getters that return a **fresh** array/object each call (`view()`
even does per-entry `fileExists` RPCs), mutators, and a **no-op-guarded** `onChange` signal
— a mutation that doesn't change serialized state neither writes nor notifies. The
persistent config models and on-disk shapes are the subject of **doc 05**; here we cover
responsibilities and the TS-owns-everything invariants.

The load-bearing point: **TypeScript is the source of truth for system identity, order,
focus, and dirty.** Native holds cores keyed by an id, but it never mints an id, never
tracks order, never knows which system is focused, and never decides the project is dirty.

### SystemsStore — [`src/systemsStore.ts`](../packages/retroplug/src/systemsStore.ts)

The live systems list. `SystemEntry[]`
([systemsList.ts:22](../packages/retroplug/src/systemsList.ts#L22)) is the persistent
model (`{ id, platform, core, romPath, savPath override, savSuffix, embeddedRom, settings,
roles }`, plus the derived `battery`, `romName` and `resources` facts, never serialized); the private state is
`entries`, `focusedId`, `dirty` and the `ResourcePool` that counts its handles
([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)). `view()`
([systemsStore.ts:141](../packages/retroplug/src/systemsStore.ts#L141)) projects a
`SystemView[]` for the UI, computing the live `focused` and `missing` flags each call, and `systems()` gives
the entries, which is how a caller reads the new id a rebuild allocated.

- **TS owns the system-id counter.** `allocSystemId()` increments a module-scoped
  `nextSystemId` ([systemsStore.ts:70](../packages/retroplug/src/systemsStore.ts#L70)):
  *"native never allocates."* Ids are opaque handles, one id space per control-plane JS
  context (1:1 with the native `Engine`), and start at **1** because the snapshot registry
  uses 0 as its free-slot sentinel.
- **`classifyRom`** ([systemsStore.ts:89](../packages/retroplug/src/systemsStore.ts#L89))
  is the one place ROM bytes enter TS — and just the `ROM_SNIFF_LEN` (0x134) header prefix.
- **Mutators through `Backend`:** `addSystem` (append), `loadRom` (replace focused, or defer to a sibling
  `<rom>.rplg`), `loadEmbedded(id)` (a ROM a package bakes in, e.g. mGB's synth), `replaceSystem`, `swapRom`,
  `removeSystem`, plus the per-system `setGain`,
  `setReloadOnRomChange` and `setRoleConfig`
  ([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)). A gain edit updates the store and
  sets the system node's `gain` param live (`backend.setParam("sys-<id>", "gain", dB)`) with no publish; the
  next document, whatever publishes it, carries the same value. `reloadOnRomChange` is TS's alone (the file
  watcher reads it), so it reaches no backend. Every boot puts its ROM and seeds as resources through
  [systemResources.ts](../packages/retroplug/src/graph/systemResources.ts) and records the handles on the entry; every
  boot puts its own ROM (a rebuild reads the file again), and a resource is released when the system that holds it goes.
  An edit of the core-config role (the `"system"`-category role
  whose kind is the core) is diffed through `systemKnobs`
  ([graph/buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts), the one table of which field is a
  setting, which a param and which construct-time): a moved setting field publishes, the node's settings carrying it; a
  moved param alone (a NES's `expansionVolume`) is set live with `setParam`; and a construct-time field (the FIFO
  and its overrides, `sdRoot`, the N8 link, `channelExportMode`, SMS `enableFm`) rebuilds the system at once through
  the reset path (a new id, the ROM read again, the live battery carried), so the new value is in the document
  that replaces the old one, and the store remembers the id it replaced so a `reset` of that id resets the
  replacement (below). The comparison is `systemKnobs` over the stored config as written against the schema's parse of
  the edit, and every field the core-config role carries is written in full (the stored value, else the node's
  default), so a sparse stored config adds no move while a stored value the schema repairs takes the rebuild it needs.
  `gameGear` is the platform's, written `true` for a Game Gear and left out otherwise, and a platform never changes
  under a live id. A feature-role's config reaches only its protocol node's settings, and for `lsdj-sync` also its
  Game Boy node's `serialOutCapture` (`"drive"` for MI.OUT, `"listen"` for master sync, `"off"` otherwise), both in the
  document its edit publishes.
- **`duplicateSystem`** is TS orchestration with **no native duplicate method**: it pulls `readState(id)` (state
  includes SRAM), then boots a `ConstructSpec` seeded with those `stateBytes` under a fresh
  `allocSystemId()` from the ROM put again from its source, and appends.
- **`commitEdit`** is the one publish path: it installs the new entries, publishes once, and only then releases the
  resources the edit replaced (a dropped resource cannot be bound by a new instance). A refused document puts the
  entries and the focus back, gives back what the edit put, republishes the restored model (warning if that is
  refused too) and returns false. A publish that throws is the engine's or the transport's failure rather than a
  refusal the engine answered, and [`engineGraph.ts`](../packages/retroplug/src/engineGraph.ts) counts it as refused
  (the engine keeps the plan it ran), so it takes the same rollback before the error reaches the caller. `clear()`
  publishes the empty document and releases every system's resources anyway; `discard(id)` drops one system and its
  resources at once for a load's refused system, whose document never planned. Each of those loops guards its own
  releases the way `commitEdit` does, warning and continuing: both run after the entries have moved on, so a throw that
  escaped one would strand the handles behind it, and `discard` runs inside the load's retry, where it would fail a load
  whose list is already shortened.
- **The graph.** Every edit that changes what runs, what a system's protocol nodes are told, or a setting of
  a system's node, ends by publishing the engine's graph once (`setOnGraphChange` → `ProjectStore.publishGraph`
  → `EngineGraph.publish`, [engineGraph.ts](../packages/retroplug/src/engineGraph.ts)): the whole
  document, built by [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) from the entries in
  list order, each entry's platform, resources and `romName` (which its system node names), its
  pipeline (`graphSystems`: its roles that have a protocol node, each with the node type it is placed as
  and its parsed config, by [`graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts)'s
  table, with or without a registry), its core-config role's config, its construct-time fields and its gain (its
  system node's settings and params), the project's audio and MIDI routing and its controller, with a NES tap
  re-arm when the edit built or dropped a core. So a `setRoleConfig` on a role with a protocol node publishes, one on
  the core-config role publishes when it moves a setting field (a model, a palette) or a Game Boy's link group, and
  one on a role with neither (an asset override) changes no document. A
  `SystemsStore` used without a `ProjectStore` publishes its systems itself, at the default stereo
  routing and with no MIDI routing or controller (so no MIDI is routed). An unchanged document is not sent unless it
  re-arms or the last one did not plan; a refused one leaves the store's model as it was, and is logged
  ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md) section 2.2).

### The reconstruct-in-place idiom

The control plane **reconstructs** a core rather than live-injecting into it (no `GB_reset`, no
in-place SRAM clear). The private `rebuildInPlace(id, seed)`
([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)) is the shared
body: it captures the source spec (through the load-time role hooks, so an asset override is re-folded), boots
it under a **new id** that takes the old one's slot, and preserves identity + focus
(`replaceById(entries, id, {...src, id: newId})`; `if (focusedId === id) focusedId = newId`).
The `seed` overrides what the boot reads: `sramBytes` = cold-boot battery,
`stateBytes` = boot-from-savestate, while `savPath` stays the auto-save target. Every boot reads its ROM again from
its source, so a file a user rebuilt on disk is what any of these boots, as before C3. Six public operations are
wrappers over it (only `newSramAs` adds anything of its own: it flushes the old save target, then materialises the new
one), and a construct-time `setRoleConfig` is a seventh:

| Op | Seed |
|---|---|
| `reloadSystem` | live SRAM carried forward, cold boot |
| `reset` | live SRAM carried forward (hardware-style reset); an id a rebuild replaced resets its replacement |
| `loadState(path)` | `stateBytes` from the file |
| `loadSram(path)` | `sramBytes` from the file |
| `newSram` | a blank `new Uint8Array(0x20000)` battery |
| `newSramAs(savPath)` ("New SRAM...") | the same blank battery, the auto-save target repointed to the picked file |
| a construct-time `setRoleConfig` | live SRAM carried forward (the battery), the new field in the new document |

Every in-place rebuild that keeps the system's identity (`reset`, `reloadSystem`, `loadState`, `loadSram`, `newSram`,
`newSramAs`, a construct-time `setRoleConfig`) records `old -> new` in the store's `replacedBy` map
([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)), and `reset` is the one verb that reads it: a
`reset(oldId)` follows a chain of rebuilds to the live system, resets that system and answers its id. That keeps
the pre-C3 contract working, where a construct-time `setRoleConfig` was followed by `reset(id)` to learn the id
that carried the new settings. A user's ROM replacement (`replaceSystem`, `swapRom`, `loadRom` / `loadEmbedded`) is
not a rebuild of that system but a different one in its slot, so it records nothing and `reset` of the id it
replaced answers null, as it did before C3. Every other verb treats a replaced id as absent, so a stale handle
cannot remove or edit a system it never named. An entry goes when the live system its chain ends in does
(`removeSystem`, `discard`, `clear`), and the map is capped at 128 entries, since a long session of resets would
otherwise keep every id it ever used.

A separate **project-load rebuild seam** is deliberately quiet (no dirty/onChange, since a
load isn't a user edit): `clear()` tears every system down and, when it had any, publishes the
empty graph;
`adopt(config, blobs?)` (which publishes the graph that places it, except inside a project load,
which publishes once its systems are all built)
([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)) reconstructs
one system preserving its **exact** `savSuffix`/`savPath`, with `blobs` seeding SRAM/state on
a zip import. The publish retries without the system a refusal names, its own node `sys-<id>` or a protocol node
`sys-<id>/<kind>` (`ProjectStore.placeLoadedSystems`), so one unreadable ROM costs only its own system; a refusal that
names no node (an unparsable document, a tap arm outside the slots, two system nodes naming one system, planning out of
memory) cannot be charged to one, so nothing is dropped and the load warns that the listed systems do not run. Default
roles are derived before the boot from a `ROLE_HEADER_LEN` (0x150) header prefix via `defaultRoles`, and each role's
`onConstruct` hook folds over the spec
(`applyConstructHooks`), e.g. the LSDj empty-sav seed, before its resources are put.

### ProjectStore — [`src/projectStore.ts`](../packages/retroplug/src/projectStore.ts)

The top-level source of truth. It **owns** the `SystemsStore` (wiring its `onChange` →
`markDirty` + `onSystemsChange`, and its graph hook to `publishGraph`,
[projectStore.ts:148](../packages/retroplug/src/projectStore.ts#L148)), the project
settings (`ProjectSettings` = `{ layout, midiRouting, audioRouting, zoom, controller }`,
[projectConfig.ts:37](../packages/retroplug/src/projectConfig.ts#L37)), the
`currentPath`, `dirty`, and a `pendingLoad` latch. It exposes **three** change signals:
`addOnSystemsChange` (structural, or a setting the graph's script nodes read: an observer list, kept in order
and isolated from each other's throws; composeAppStores' observer prunes the render list and notifies the editor, and
the plugin's control plane adds the watched-ROM refresh), `setOnChange` (any state: settings, dirty), and the
transient focus signal via `systems.setOnFocusChange` (re-renders tiles without dirtying or publishing the graph).

Project I/O is doc 05's territory, but the store surface is: `newProject`, `save(path)` (a
thin `.rplg` — raw JSON, paths only), `export(path)` (an export `.rplg.zip` — PKZIP of
`project.json` + per-system SRAM/savestate blobs gathered from the read door), `exportBytes()`
(the in-memory chunk for DPF `getState`), `loadBytes(bytes, baseDir)` (the DPF `setState` zip
chunk), and `load(path)` (routes by **extension**: a `.rplg` is always thin JSON, a `.rplg.zip`
is PKZIP; a `.rplg` that isn't pure JSON errors — never loaded as a zip). The load lifecycle
refuses a **newer** schema stamp (`{kind:"incompatible"}`), absolutizes paths, scans for
missing files, and holds `pendingLoad` for `relink` or `commit`
([`beginLoad`, projectStore.ts:704](../packages/retroplug/src/projectStore.ts#L704); [`commit`,
:719](../packages/retroplug/src/projectStore.ts#L719)).
Three project settings reach the engine, each through the graph the project publishes
([`publishGraph`, projectStore.ts:166](../packages/retroplug/src/projectStore.ts#L166)):
`setAudioRouting` as its wiring (`publishRouting()`, which re-arms the NES tap), and `setMidiRouting`
and `setController` as the script nodes ahead of the systems. `publishGraph` publishes
`{ systems, routing, midiRouting, controller }`: each system with its roles that have a protocol node
([`graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts)), the MIDI routing, and
the controller with its app config parsed by the app's schema and the song-timing table it reads from
the controller cart's battery here, on the control plane. [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts)
places the routing node, the controller node and a node per role from them, forcing the MI.MAP
controller's cart into the sync mode it needs ([`graph/controller.ts`](../packages/retroplug/src/graph/controller.ts)).
`refreshControllerSong` publishes too, when its poll finds the controller cart's song
moved or a new start anchor. A load holds the publishes back while it adopts and publishes once; a
load that throws partway places what it adopted through the same drop-and-retry before rethrowing (a system the
placement refuses does not stay listed), and every exit of a load tells the structure's observers
(`onSystemsChange`) once.

The project **name** is two-tier. `name()` is the project's own name - blank unless the user
typed one under `Project` > `Name` (`setName`, which marks the project dirty), and the ONLY
one persisted (`buildConfig` omits a blank `name`, so a nameless `.rplg` carries no field).
`displayName()` is what the window / menu titles SHOW - that name when set, else `deriveName()`:
the primary system's `savPath` stem, else its `romPath` stem (primary = focused, else first).
The recents entry gets a fuller derivation, `recentName()`: the project's own name when set,
else the primary cart's identity `"<sav.ext> [<rom>]"` - the loaded sav's FILENAME, extension and
all (an explicit override, or a battery cart's suffix-derived sibling), then the ROM's stem in
brackets. The sav is named in full even when its stem is just the ROM's (the usual `<rom>.sav`
case), so every cart reads the same way; a cart with no battery and no override has no sav to
name and is the bare ROM stem, and an embedded cart is empty. Every recents record
(`save` / `export` / `adoptRomProject` / load `commit`) passes it alongside `currentSong()`, so
the menu composes `"SONG - mysongs.sav [LSDj-v5.3.0]"` (or `"SONG - project name"`) - and none of
it reaches disk.

**Song rows.** Recents holds one row per SONG a project has had loaded, so a song change records a
row rather than rewriting one: `recordSong(name)` adds it (a known song - the Songs menu's Load
names what it just loaded, no need to wait on a battery snapshot), and `recordCurrentSong()` is
`recordSong` over `currentSong()` - the signal for a load RetroPlug didn't make, i.e. the user
picking a song on LSDj's / risa's own file screen. Nothing else can see that, so the UI polls it:
[`useSongWatch`](../packages/retroplug/ui/app/useSongWatch.ts) calls `recordCurrentSong()` every
~30 render frames. No memo is needed - `RecentStore.add` no-ops (no write, no notify) while the
answer is unchanged. Editor-driven like the file watcher: a DAW instance with its editor closed
records nothing until it's opened.

### The engine's graph: `buildGraph`, `EngineGraph`, `DirectGraph`

[`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) is a pure function from the model
(`{ systems, routing, shape, midiRouting?, controller? }`) to the document `setGraph` plans
([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md) section 2.1; the node
order, the routing as wiring and the host shapes are in [02-native-host.md](02-native-host.md)).
Besides the system nodes, each with its core's knobs as its settings and params (`systemKnobs`, and a Game
Boy's serial-out capture for the mode its LSDj's nodes run, the controller's forced one included:
[modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md) section 5), and their mixing, it
places the script nodes: those that feed a system between the host's inputs and the systems, and those that read
one after the systems, before the host's outputs
([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md) section 4,
[lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) section 3,
[04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)):

- `midi-in` (`rp.host.midi-in`) and, when a controller is enabled, `surface-in`
  (`rp.launchpad.surface-in`);
- `routing` (`rp.midi.route`, settings the routing mode and the system ids in slot order) when the model
  has a MIDI routing, fed from `midi-in`, an output per system;
- `controller` (`rp.controller.session`) when a controller is enabled, its target resolved and its app
  config already parsed by the publisher, fed from `surface-in`'s `in` and `connected`, its control-surface
  output to `surface-out`;
- each system's protocol nodes (`sys-<id>/<role-kind>`, settings the role's parsed config), fed its routed
  MIDI, each output wired to that system node's input of the same name; the MI.MAP controller's cart has
  its `lsdj-sync` nodes forced to the mode the app needs;
- after the systems, each LSDj's serial-out decoder (`sys-<id>/lsdj-sync/out`, `rp.lsdj.serial-out`, on a
  Game Boy, with its `rp.lsdj.sync`'s settings), fed that Game Boy's `serialOut` over a forward wire, so
  MI.OUT and master sync reach it in the block the core captured them;
- `cart-<id>` (`rp.n8.nes`) for the first NES in slot order whose "drive the real cart" setting is on
  (default on), fed by fan-out what that system's protocol nodes send its `midi` and `bytes`;
- `midi-out` (`rp.host.midi-out`, settings `{ systems }`), its inputs the controller's `midiOut` and then
  each `rp.lsdj.serial-out`'s in slot order, and `surface-out` (`rp.launchpad.surface-out`), fed from the
  controller's `controllerOut`.

The surface is two nodes because one that both feeds a session and follows it is a node-level cycle, and the
only wire a cycle breaks on delivers the previous block; the two keep the session's recorded digests, and its
per-block LED stream, unchanged.

[`EngineGraph`](../packages/retroplug/src/engineGraph.ts) is one control-plane context's publisher:
`publish({ systems, routing, midiRouting, controller }, { rearmTap })` makes that its model, builds the document at
its host shape and sends it, skipping the send when the document's canonical text and slots match the
last one that planned, so an edit that changes nothing sends nothing (a re-arm is sent all the same;
after a refusal the next document is sent even if unchanged; and so is one after a live `setParam`,
`systemsMovedLive`, which the store calls whether or not the backend took it, so the next document
lands the store's value).
[`DirectGraph`](../packages/retroplug/src/graph/directGraph.ts) is the publisher for callers with no
stores (the native tests, the CLI's `analyze-lsdj-sync` session and the `lsdj-detect-offsets` tool;
one per process, `directGraph()`). Its `add(spec, id)` / `replace(oldId, spec, id)` / `remove(id)` keep its systems
list in slot order (add appends, replace keeps the slot, remove shifts later slots down); each boot puts the spec's
ROM and seeds through the shared helper, and a resource is given back once the document that stopped naming it has
planned, a refused change giving back what it put and restoring the document before it. A publish that throws (an
engine or transport fault, which the graph counts as refused) rolls back the same way before the error reaches the
caller, as the store's `commitEdit` does. A system it adds or replaces has no roles, and what the script nodes are told
is three calls of its own, each a publish: `setPipeline(id, roles)` (each role with a protocol node placed as one, its
config as given), `setMidiRouting(mode)` and `setController(controller)` (its app config parsed by the app's schema,
as the project's is). Until `setMidiRouting` names a mode, no host MIDI is routed to any system. A system it adds has
no core config either, so its node carries no live setting field (a NES's or SMS's construct-time fields are written at
their node's defaults) and its core keeps the settings it was built with, while its params still reach it at their
defaults (its gain at 0 dB; a NES's expansion volume at 100%), until `setCoreConfig(id, partial)`, a publish, merges
fields over what it gave before, in the labels the store's role spells them (a Game Boy's `linkGroupId` its link
cable), and refuses, changing nothing, any field its node does not take (an ordinal, a construct-time field, another
core's); `setParam(id, param, value)` sets a `gain` or an `expansionVolume` live, as the store does, and keeps it for
the next document.

### RecentStore — [`src/recentStore.ts`](../packages/retroplug/src/recentStore.ts)

A `RecentEntry[]` (`{ path, name, song? }`) most-recent-first, capped at `MAX_ENTRIES = 10`
([recentList.ts](../packages/retroplug/src/recentList.ts)). The dedupe key is `entryKey` =
canonical path + song, so ONE project holds a row per song it has had loaded (a non-tracker
project keeps its single songless row); `remove(path, song)` drops one row and `relink` repoints
every row of the moved file. Incoming paths are canonicalized via `backend.canonicalize`;
`view()` computes live `missing` + `label` (the recorded `name`, else the basename minus the
project extension). `name` is whatever `ProjectStore.recentName()` resolved when the entry was
recorded - there is no per-entry rename; the UI's only naming verb is `Project` > `Name`. `commit(next)` ([recentStore.ts:91](../packages/retroplug/src/recentStore.ts#L91))
serializes and skips both write and notify when identical. Persists atomically to
`<configDir>/recent.json`.

### UserConfigStore — [`src/userConfigStore.ts`](../packages/retroplug/src/userConfigStore.ts)

`UserConfig` = `{ activeKeyboardBindings, activeGamepadBindings, defaultZoom 1-6,
sramAutoSave: "Off"|"OnProjectSave"|"Continuous" }`
([userConfig.ts:23](../packages/retroplug/src/userConfig.ts#L23)), persisted to
`config.json`. `load()` writes defaults on first run; `reload()`
([userConfigStore.ts:37](../packages/retroplug/src/userConfigStore.ts#L37)) is the
file-watch reaction and keeps the current value on a missing / malformed / newer-stamped
file. Setters validate and reject bad input.

### BindingsStore — [`src/bindingsStore.ts`](../packages/retroplug/src/bindingsStore.ts)

Per-profile `bindings/<name>.json` maps (`{ name, keyboard, gamepad, keyboardActions,
gamepadActions }` — the `*Actions` sections bind the app actions Open Menu / Cycle Instances,
resolved by `buildKeyToAction` / `buildGamepadToAction`), reading the
active-profile names through `UserConfigStore`. CRUD: `ensureDefaults` (seeds
`bindings/default.json`), `availableProfiles`, `loadProfile` / `saveProfile`, `renameProfile`
(no-clobber, repoints active refs), `deleteProfile` (refuses the active one), and
`resolvedBindings()` ([bindingsStore.ts:110](../packages/retroplug/src/bindingsStore.ts#L110))
which merges the active keyboard + gamepad profiles, each falling back to a default. Names
are validated by `isValidProfileName`
([bindingsStore.ts:24](../packages/retroplug/src/bindingsStore.ts#L24)).

### Store-adjacent services

- **FileSelection** — [`src/fileSelection.ts`](../packages/retroplug/src/fileSelection.ts):
  turns a file pick into a systems op. `browse(mode)` opens the ROM-or-sav dialog, classifies
  (ROM by content / `.sav` by extension / other), and routes. A picked `.sav` finds its
  sibling ROM, or opens a second ROM-only browser — *"just another `await` inside the same
  Promise"*, no pending-mode latch. `SelectionOutcome` = `loaded | added | deferred | error
  | cancelled`.
- **FileWatcher** — [`src/fileWatcher.ts`](../packages/retroplug/src/fileWatcher.ts):
  the TS reaction to native's watcher. **"Watcher = C++, policy = TS."** `pump()` drains
  `backend.drainChangedPaths()` at idle and routes each: `config.json` → `userConfig.reload()`,
  `bindings/*.json` → a refresh signal, a system's ROM → `reloadSystem(id)` **when
  `reloadOnRomChange` is on**. Native (`NativeFileWatcher`, efsw) owns the watching — the config dir +
  `bindings/` recursively, plus each ROM's parent dir (registered via `setWatchedRoms`, recomputed on
  every systems change); TS owns what a change *means*. The plugin composes it in
  [`pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) and drives `pump()` from the
  UI idle loop (`__rp_pumpWatcher`).
- **SramAutoSaver** — [`src/sramAutoSave.ts`](../packages/retroplug/src/sramAutoSave.ts):
  the loose-`.sav` mirror policy over `backend.readSram(id)` + `resolveSavPath`. `flushDirtySram`
  writes at a project save and `flushBatteryForTeardown` when a cart's core is destroyed — neither
  consults the preference, because both are the user acting; `pump()` is the Continuous idle-tick and
  is the only thing the preference gates. An FNV-1a change hash dedups writes and distinguishes
  seed-vs-write against the on-disk file. Pure decision logic — native only reads SRAM and writes bytes.

---

## The control-plane composition

Three small modules compose the store graph and wire it to the host. They exist so every
host builds the *same* graph the *same* way.

### `pluginControlPlane.ts` — the plugin composition root

[`src/pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) is
evaluated once by the plugin host in its txiki context. It composes the whole store graph through
`composeAppStores()` (backend + registry + recent + userConfig + bindings + project +
fileSelection). It loads nothing for the audio thread: the script nodes are types every Engine has,
and the graph the project publishes carries them and their settings. Its structure observer (`addOnSystemsChange`)
refreshes the watched ROMs; the editor's re-render and the render list's prune are composeAppStores' own observer on
the same signal.

It then exposes the **string-only C++→JS surface** for project I/O — the plugin drives these
directly with no further RPC:

| Global | Purpose |
|---|---|
| `__rp_loadProjectPath(path)` | Autoload a `.rplg` from disk |
| `__rp_loadProjectB64(b64)` | DPF `setState`: load an in-memory chunk (empty chunk = no-op) |
| `__rp_saveProjectB64()` | DPF `getState`: export the project as a base64 `.rplg` chunk |
| `__rp_newProject()` | DPF `setState("")`: reset to an empty project |
| `__rp_ready` | Set true at the end of the bundle's top level, once composition has run |

Base64 is done **here** in runtime-independent code, not native, because DPF state is NUL-terminated UTF-8 while a `.rplg` is binary PKZIP — the
C++ boundary stays string-only.

### `appStores.ts` — the shared store graph

[`src/appStores.ts`](../packages/retroplug/src/appStores.ts)'s `composeAppStores({
backend?, notify? })` builds the full graph — `registry, recent, userConfig, bindings,
project, fileSelection` — so every host constructs it identically. Change notification is
injected as one `notify(channel)` where `StoreChannel = "project" | "systems" | "recent" |
"userConfig" | "bindings"`. It wires the project's two signals plus focus;
the script nodes need no wiring, since the project publishes them with the graph.

**Every host composes through here, and the plugin runs exactly ONE graph.**
`pluginControlPlane.ts` calls `composeAppStores` at construct and publishes the result on the
plugin namespace; the editor's [`StoreProvider`](../packages/retroplug/ui/stores/StoreProvider.tsx)
reuses that published graph when it is present, and composes its own only when there is none (the
ui-test harness). This is load-bearing rather than tidy: when the editor composed a second graph,
the systems the UI showed were not the ones the DSP played or `getState` serialized, and closing
and reopening the editor in a DAW showed the start menu instead of the loaded project.

### `appHost.ts` — host glue

[`src/appHost.ts`](../packages/retroplug/src/appHost.ts) holds the wiring every host
reuses: `buildAppRegistry()` assembles the control-plane role registry (core roles + the feature roles: the five
with protocol nodes, and the asset and marker roles `lsdj-assets`, `risa`, `risa-assets`, `bliptoaster` and
`bliptoaster-assets` + ROM providers, see doc 04).

### The `__rp_*` UI↔native direct-hook pattern

Window-owning and async seams bypass the RPC surface and hang functions on the shared
context (one UI thread), because they touch native window state that the fire-and-forget RPC
model can't. All of them optional-chain the globals, so they are **inert in the headless
harness**:

| Hook | Owner | Purpose |
|---|---|---|
| `__rp_openFileBrowser` / `__rp_onFileBrowserResult` | [realBackend.ts](../packages/retroplug/src/realBackend.ts) | The async file dialog |
| `__rp_setWindowSize` / `__rp_isWindowSizeControlled` | [useWindowSize.ts:42-50](../packages/retroplug/ui/lvgl/useWindowSize.ts#L42) | Fit-to-grid resize / tiling-WM detection |
| `__rp_mountUI` / `__rp_unmountUI` | [main.tsx:31-32](../packages/retroplug/ui/main.tsx#L31) | Render/teardown across window close/reopen |
| `__rp_tagTestId` | [StableSlot.tsx:21](../packages/retroplug/ui/lvgl/StableSlot.tsx#L21) | Test-only widget tagging (inert in production) |

---

## The React / LVGL UI — [`ui/`](../packages/retroplug/ui)

The UI renders React (via `react-reconciler`) to **LVGL** widgets through `lvgljs-ui` /
`lvgljs`. It reads the stores reactively and mutates them imperatively.

### Entry + provider chain

[`main.tsx`](../packages/retroplug/ui/main.tsx) defines
`<StoreProvider><FocusProvider><App/></FocusProvider></StoreProvider>` and installs
`__rp_mountUI` / `__rp_unmountUI` so the host renders after a display attaches and keeps
QuickJS alive across a window close/reopen.

- [`StoreProvider.tsx`](../packages/retroplug/ui/stores/StoreProvider.tsx) builds the
  graph lazily via `useRef` (so `createRealBackend()` resolves after the host binds
  `__rpcSend`) and passes `composeAppStores` a `notify(channel)` that fans out to per-channel
  listener `Set`s; it exposes `subscribe`.
- [`useStores.ts`](../packages/retroplug/ui/stores/useStores.ts) exposes `useStores()`
  (imperative mutators) and `useStoreSnapshot(channel, read)`
  ([useStores.ts:40](../packages/retroplug/ui/stores/useStores.ts#L40)), which caches
  `read()` and recomputes **only after a notify** (via `useSyncExternalStore` + an
  invalidate-on-notify ref) — necessary because store getters return a fresh reference every
  call. Public hooks: `useSystems`, `useProjectSettings`, `useIsDirty`, `useRecent`,
  `useUserConfig`, `useBindings`.

### App controller — [`App.tsx`](../packages/retroplug/ui/App.tsx)

Owns menu open/close and swaps between the start menu (empty project) and the system grid.
**Esc is owned here, in one place**
([App.tsx:61-72](../packages/retroplug/ui/App.tsx#L61)): closed + a focused system →
open the instance menu anchored to it; open → close. The start menu is always open when
empty. When the grid shows idle (no menu), the keypad is pointed at the sink group so arrow
keys don't leak into the clickable tiles. The window is fit to the grid on instance/zoom
change unless a tiling WM owns geometry (`isWindowSizeControlled()`). It drives
`useGameInput({ active: !empty && !menuOpen, focusedId })` and renders `<Menu>` (empty) or
`<SystemGrid>`.

**The unsaved-changes prompts.** Two of them - the standalone's close guard
([`useCloseGuard`](../packages/retroplug/ui/app/useCloseGuard.ts), rendered by App) and the
New / Load discard confirm ([`useProjectModals`](../packages/retroplug/ui/app/useProjectModals.ts))
- both lead with WHAT is unsaved before their buttons:
[`unsavedRows`](../packages/retroplug/ui/app/unsavedRows.ts) maps
`unsavedChanges(backend, project)` ([src/unsavedChanges.ts](../packages/retroplug/src/unsavedChanges.ts))
to one `disabled` row per item - `"Project: song.rplg"` / `"Project: (not saved yet)"`,
`"Battery: lsdj.sav"` (+ `" (new file)"` when the target doesn't exist yet) - closed by a
separator. Menu greys disabled rows and skips them in nav, so Enter still lands on the first
button. The rows are built at tree-build time (not latched when the prompt opened), so a battery
the cart writes while the prompt is up shows on the next render; that costs the same battery scan
the menu's Save-Project label already pays. The yes/no gate stays `hasUnsavedChanges`, which
short-circuits on the project flag precisely because it runs on every menu rebuild.

### The grid — [`screens/grid/`](../packages/retroplug/ui/screens/grid)

- [`SystemGrid.tsx`](../packages/retroplug/ui/screens/grid/SystemGrid.tsx) renders one
  tile per system from `useSystems()`. `fitZoom`
  ([SystemGrid.tsx:25](../packages/retroplug/ui/screens/grid/SystemGrid.tsx#L25)) caps
  zoom to keep the whole grid visible. Each tile is wrapped in a
  `StableSlot key={slot-${sys.id}}`; the slot matching `menuSystemId` swaps its single child
  EmulatorTile↔`<Menu>` so sibling tiles keep rendering.
- [`EmulatorTile.tsx`](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) pulls
  `backend.getFrame(systemId)` on each native `"frame"` event and blits it into an LVGL
  `Canvas` via `setBuffer` ([EmulatorTile.tsx:50](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx#L50)).
  Click → `setFocus`; an unfocused tile gets a translucent dim overlay, a focused one an accent
  border (both suppressed when it is the only tile). Between the dim and the border, `TileViews`
  ([TileViews.tsx](../packages/retroplug/ui/views/TileViews.tsx)) mounts one `StableSlot` per registered
  tile view whose `when` accepts the system: the two debug overlays are their carts' packages now
  (`lsdj-overlay`, `risa-overlay`), and the slot's fixed place is what keeps a strip that appears
  late from landing over the border and the render badge.
- [`layout.ts`](../packages/retroplug/ui/screens/grid/layout.ts) is pure grid math
  (`GB_NATIVE_W/H`, `shapeFor`, `gridContentSize`, `getTileBounds`).

**Why `StableSlot`.** `lv_binding_js`'s `insertChildBefore` ignores its `beforeChild`
argument and always appends, so React reordering a child (or swapping its type) at a stable
position lands the new widget at the *end* of the LVGL child list.
[`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx) is the workaround: a
fixed-position wrapper View whose position never changes, whose *single* child swaps — where
`appendChild` lands correctly. Its `tagTestId`
([StableSlot.tsx:21](../packages/retroplug/ui/lvgl/StableSlot.tsx#L21)) tags a
widget's native uid for the test harness and is inert in production.

### The menu — [`screens/menu/`](../packages/retroplug/ui/screens/menu)

- [`menuTree.ts`](../packages/ui/ts/menu/menuTree.ts) is the pure data
  model: `MenuItem { id, label, kind: "action"|"submenu"|"separator"|"cycler"|"actionCycler", … }`
  where each leaf carries its own effect callback (no dispatch). An **`actionCycler`** is the flat
  alternative to a one-row submenu: the row carries an `actions` list and, **while focused**, renders as
  `[0] TR-606   <  Export...  >` (unfocused it is just its label — the element belongs to the
  cursor), Left/Right picking which verb shows and Enter **running** it
  (where a `cycler`'s Enter *steps*). Which verb a row is showing is transient state in
  `Menu.tsx` — the tree is rebuilt from scratch every render and holds no selection. A verb may carry its
  own `keepOpen`, overriding the row's (a `Select` you run repeatedly, beside an `Export...` that opens a
  dialog). Every tracker **asset** list uses it (`assetRow` in `menuDefs.ts` — kits, themes/palettes, fonts
  alike), so a slot's verbs cost no extra level of menu. The element's width is fixed by the zoom alone, so
  the arrows land in the same columns on every such row regardless of which list it belongs to.
  Asset rows carry two markers, meaning different things: **`*` is the LIVE slot** (what the cart is set to)
  and **`~` an overridden one** (contents replaced from disk); a slot can be both (`[0] NEON *~`). A console
  with a live slot implements `selectedSlot`/`selectSlot` on its `AssetMenuSpec`, which is what puts a
  leading `Select` verb on every row but the live one — BlipToaster's theme + font do, so their lists ARE
  the picker rather than a cycler elsewhere naming a slot by number.
  Unlike every other row, an `actionCycler` is a **`Box` of labels, not a single `Text`** — the arrows hold
  fixed positions as real widgets rather than by space-padding, so the row's alignment is geometry and does
  not depend on the face being monospaced (which it currently is): name at the row's left
  (`justify-content: space-between`), then a fixed-width element at the right made of three explicit columns,
  `arrow | verb | arrow`. Two consequences. The Box must be told its **height** (LVGL gives a plain object a
  default size and lv_binding_js exposes no `LV_SIZE_CONTENT`), so `Menu.tsx` measures a real `Text` row and
  sizes Box rows to match — don't derive it from `itemFont`, which is snapped to the nearest size the font
  table carries. And `flex-grow` is unusable on the children: lv_binding_js's flex pipe drops it unless the
  child's own style also says `display: "flex"`. The UI harness composes a non-label widget's text from its
  descendant labels (`RenderCore::widgetInfo`), so `navTo` / `focused().text` still see these rows.
  The **mouse aims at the cells**: clicking the verb runs it, clicking an arrow steps the pick, and clicking
  the row body only moves the cursor — so a click can't run a verb the pointer was never over, which matters
  because the element is invisible until the row is focused. Each cell hover-highlights and stops the click
  bubbling (Texts are created `LV_OBJ_FLAG_EVENT_BUBBLE`). That splits the two paths that used to share the
  row's `onClick`, so **keypad Enter is handled on the key bus instead** — LVGL routes it through the same
  `CLICKED` event the mouse uses, and only the row body should answer that one.
- [`menuDefs.ts`](../packages/retroplug/ui/screens/menu/menuDefs.ts) builds the start
  and instance menus over a `MenuContext` (stores + current values, rebuilt each render).
  Leaves call store methods directly, current values are baked into labels, and
  `browseThen` opens the OS dialog before applying. The instance menu runs project ops (Load /
  Save / New / Recent) and any tracker submenu first, then the three settings submenus — a
  System submenu (SameBoy model/highpass/fastBoot cyclers, Save/Load State + SRAM, New SRAM,
  Reset) / Project / Settings — and closes with the per-instance group (Add / Duplicate /
  Replace / Remove / Link Group), which is therefore the bottom of the menu.
- [`Menu.tsx`](../packages/retroplug/ui/screens/menu/Menu.tsx) is the keyboard-driven
  tree renderer. The focus highlight is React state driven **only** by explicit nav / click /
  rebuild — never by LVGL `onFocus` events — so there's nothing for stray focus events to
  corrupt. It flattens depth-first into open submenus and **re-keys the inner scrollable View
  on `visibleKey`** ([Menu.tsx:590](../packages/retroplug/ui/screens/menu/Menu.tsx#L590))
  — the same append workaround, forcing a full remount so every row mounts in JSX order.
  `useFocusGroup` claims the keypad; Enter → activate, Up/Down move the cursor, Left/Right →
  `onCycle(±1)`.

### The LVGL kit - [`@retroplug/ui-react`](../packages/ui-react)

- [`Box.tsx`](../packages/ui-react/ts/ui/Box.tsx) - a `<View>` with the LVGL
  default-theme chrome zeroed, caller style merged on top.
- [`useNativeEvent.ts`](../packages/ui-react/ts/ui/useNativeEvent.ts) - subscribes to
  the `lvgljs` `on`/`off` bus (`frame` / `key` / `resize` / …) with a stable listener + handler
  ref (no re-subscribe on inline closures).
- [`lv.ts`](../packages/ui-react/ts/ui/lv.ts) - typed `ViewEx` / `TextEx` / `CanvasEx`: the one
  place the lvgljs-ui components are cast, behind prop types for what native accepts. Plus
  [`PixelCanvas.tsx`](../packages/ui-react/ts/ui/PixelCanvas.tsx) (a crisp, aspect-kept frame
  canvas).
- [`useFramePoll.ts`](../packages/ui-react/ts/ui/useFramePoll.ts) - the frame-clocked polls:
  every-N-frames, versioned native status, and per-frame sampling deduped on a display signature.

### The shell's LVGL primitives - [`ui/lvgl/`](../packages/retroplug/ui/lvgl)

These touch the app's own window and focus model, so they stay in the shell rather than the kit.

- [`FocusProvider.tsx`](../packages/retroplug/ui/lvgl/FocusProvider.tsx) - owns the
  app-wide empty **"sink"** keyboard group's lifecycle so LVGL doesn't route arrows/Enter into
  clickable tiles. It deliberately does *not* claim the keypad itself (parent effects run
  after children's, so it would clobber a just-mounted menu).
- [`useFocusGroup.ts`](../packages/retroplug/ui/lvgl/useFocusGroup.ts) - the reusable
  claim-the-keypad dance: a `useLayoutEffect` builds a fresh group from ordered refs, focuses a
  target, `setKeyboardGroup(group)`, and on cleanup restores the sink (**never null**) and
  destroys the group.
- [`useWindowSize.ts`](../packages/retroplug/ui/lvgl/useWindowSize.ts) - the live window
  size reactive to `"resize"`; `requestWindowSize` / `isWindowSizeControlled` are the TS side of
  the `__rp_*` native window seam.
- [`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx) - a fixed-position wrapper
  for one item of a reordering or type-swapping list, because lv_binding_js's `insertChildBefore`
  always appends: only the wrapper's single child ever changes.

The rest of `ui/` outside `screens/` is split by what it depends on:
[`ui/hooks/`](../packages/retroplug/ui/hooks) holds the one plain React helper left in the shell
(`useStateRef`; the frame and re-render hooks are the React kit's);
[`ui/views/`](../packages/retroplug/ui/views) is the shell's side of a package's views - `viewSession.ts`
(the pure session controller), `useViewSession.ts` (the one effect the three renderers share), the pixels,
readout and component renderers, the window and tile slots - and `ui/input/appActions.ts` dispatches the
contributed actions beside the shell's own three;
[`ui/host/`](../packages/retroplug/ui/host) is the native editor seam - `nativeHooks.ts` types
every `__rp_*` hook once, beside the render-job and open-path seams; and
[`ui/app/`](../packages/retroplug/ui/app) holds the app's own flows - the full-window overlays
(`overlay.ts` + the close guard, project modals, file browser and song import hooks), the
unsaved-changes rows, the interactive save, and the background watchers (song, SRAM auto-save,
N8 SD job, Launchpad scan).

### Game input — [`ui/input/useGameInput.ts`](../packages/retroplug/ui/input/useGameInput.ts)

Resolves each `"key"` bus code to a Game Boy button via `buildKeyToButton(bindings.keyboard)`
([keyCodes.ts](../packages/retroplug/src/keyCodes.ts)) and fires
`backend.pressButton(focusedId, button, down)`. The gamepad twin is `useGamepadInput`. Both route
through `useHeldRouting` ([useHeldInput.ts](../packages/retroplug/ui/input/useHeldInput.ts)): a
slot → routed-system map that drops OS auto-repeat and sends each release to the system its press
went to, so a held button never sticks on the wrong (or a removed) instance. Releases are always
processed, so opening a menu mid-hold can't strand a key down. Pinned by
[game-input.test.ts](../packages/retroplug/test-ui/game-input.test.ts).

---

## Roles and the protocol nodes (summary: see doc 04)

Two concepts touched throughout this doc belong fully to **doc 04**:

- **Roles.** A `RoleInstance = { kind, config }` is what serializes on a `SystemEntry`. A **system-role**
  (e.g. `"sameboy"`: model/highpass/linkGroup/fastBoot) is the *only* role config that reaches the live
  core: its live fields as its system node's settings and params in the published document, its
  construct-time ones in the build spec. **Feature-roles** (LSDj-sync, mGB, the asset overrides) are pure TS
  whose config **never** gets a C++ struct (an `lsdj-sync`'s mode decides its Game Boy node's capture, which
  does). The control-plane `RoleRegistry` (built by `buildAppRegistry`) attaches feature roles to a system
  by ROM identity ([`romProviders.ts`](../packages/retroplug/src/romProviders.ts), the TS replacement for
  the legacy build's native `RomSniffer`, which is gone).
- **The protocol nodes.** A role whose work is the audio thread's runs as a script node of its own
  (its package's `ts/protocol.ts`; the controller session is the launchpad pack's,
  [`packages/launchpad/ts/nodes.ts`](../packages/launchpad/ts/nodes.ts)), which `buildGraph` places for each
  system that has the role ([`graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts)),
  beside the MIDI routing and the controller session it places from the project's settings. They are in
  the bundle every Engine embeds, inside the script host's **bare QuickJS audio context**: the distinct
  runtime, never shared with the control-plane runtime. Their settings are zod-free
  ([`graph/settingsSchema.ts`](../packages/core/ts/settingsSchema.ts)), so the bundle holds no zod.

Doc 04 covers the nodes' ports and settings, what each does per block, the drift-exact PPQ clock in JS,
and how `buildGraph` places and wires them.

---

## Not yet built / deferred

- **The About panel** is the one menu item still marked deferred in
  [`menuDefs.ts`](../packages/retroplug/ui/screens/menu/menuDefs.ts). The keyboard/gamepad
  bindings editor, Open Settings Folder, and the LSDj Mode submenu are all built.
- The `ui?` field on a `RoleType` (a role's own settings render descriptor) is present in the
  model but not yet consumed: it is the seam a role-owned settings UI will use (kit patching does not need it;
  `lsdj-assets` splices at construct).

The store graph is now **unified**: the plugin's control-plane bundle composes the one
`composeAppStores` graph and publishes it on `globalThis[Symbol.for("plugin")]`, and the editor's
`StoreProvider` reuses it — so the editor and the DAW get/setState path observe one model. The
broader remaining feature gaps live in **doc 07**; this list is only the TS-layer-local deferrals.

---

## Key files

| Concern | File |
|---|---|
| Native contract | [`src/backend.ts`](../packages/retroplug/src/backend.ts) · [`src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) |
| Systems | [`src/systemsStore.ts`](../packages/retroplug/src/systemsStore.ts) · [`src/systemsList.ts`](../packages/retroplug/src/systemsList.ts) |
| Project | [`src/projectStore.ts`](../packages/retroplug/src/projectStore.ts) · [`src/projectConfig.ts`](../packages/retroplug/src/projectConfig.ts) |
| Config stores | [`src/recentStore.ts`](../packages/retroplug/src/recentStore.ts) · [`src/userConfigStore.ts`](../packages/retroplug/src/userConfigStore.ts) · [`src/bindingsStore.ts`](../packages/retroplug/src/bindingsStore.ts) |
| Services | [`src/fileSelection.ts`](../packages/retroplug/src/fileSelection.ts) · [`src/fileWatcher.ts`](../packages/retroplug/src/fileWatcher.ts) · [`src/sramAutoSave.ts`](../packages/retroplug/src/sramAutoSave.ts) |
| Composition | [`src/pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) · [`src/appStores.ts`](../packages/retroplug/src/appStores.ts) · [`src/appHost.ts`](../packages/retroplug/src/appHost.ts) |
| UI | [`ui/main.tsx`](../packages/retroplug/ui/main.tsx) · [`ui/App.tsx`](../packages/retroplug/ui/App.tsx) · [`ui/screens/grid/`](../packages/retroplug/ui/screens/grid) · [`ui/screens/menu/`](../packages/retroplug/ui/screens/menu) · [`ui/lvgl/`](../packages/retroplug/ui/lvgl) · [`ui/app/`](../packages/retroplug/ui/app) · [`ui/host/`](../packages/retroplug/ui/host) · [`ui/hooks/`](../packages/retroplug/ui/hooks) |
| Protocol nodes (doc 04) | [`packages/*/ts/protocol.ts`](../packages/core/ts/protocol.ts) · [`packages/launchpad/ts/nodes.ts`](../packages/launchpad/ts/nodes.ts) · [`src/graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts) · [`src/graph/controller.ts`](../packages/retroplug/src/graph/controller.ts) |
