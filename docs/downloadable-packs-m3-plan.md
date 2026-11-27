# Downloadable packs M3: placeholders for a missing pack. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A project naming a pack this build does not have opens. A system whose platform, core or embedded ROM the
pack provides becomes a placeholder: a slot of its own that keeps the system's JSON and its zip entries, saves them
back byte for byte, shows a card naming the pack, and comes alive when the pack arrives. A role whose pack is missing,
refused in TS or refused by the Engine is left out of the graph, kept in the config at its position, and marked on the
tile's menu. A live load (`loadPackNow`) brings a development pack folder in now, and the project re-resolves.

**Architecture:**
- **The parse keeps what it cannot build.** `parseConfig` sorts each system of the file into a parsed `SystemThin` (with
  its file index and raw JSON beside it, `sources`) or a placeholder (`placeholders`: index, raw, `needs`); a malformed
  system is still dropped. `needs` come from the system's `provenance`, else from the platform or kind name.
- **A placeholder is a system in the systems store.** `SystemsStore` keeps placeholders in a list of its own, each with
  an id and a slot number among all slots. `view()` stays the live systems (every reader of what runs is unchanged);
  `slotView()` is every slot in save order, a placeholder's view carrying `placeholder: { needs }`, and is what the UI's
  grid reads. `buildConfig` and `buildArchive` write the slots in order, a placeholder's raw object and blobs untouched.
- **A placeholder holds its slot everywhere a slot is counted.** The graph model carries every slot's id
  (`GraphModel.slots`, placeholders included); `buildGraph` places nothing for an empty slot but numbers by slot: the
  MIDI routing's positions (the routing node keeps an output for the empty slot, unread), the per-system and
  per-instance audio buses, and the "one system" test of a split and the NES tap. The DAW parameter map is projected
  over `slotView()`, so an empty slot claims no parameter and a later system keeps its ids. The real cart's "first in
  slot order" pick reads only live systems.
- **Provenance at save.** Each live system writes `provenance`: the package that provided its platform (and core), its
  embedded ROM and each role, from the registries' `pkg`, named from the pack list (`__rp_packList` gains `name`) or the
  generated `PACKAGE_NAMES` (a package's `pack.json` name). Roles are written in the order the file stored them.
- **Refusals reach TS.** `__rp_packRefusals()` lists the native table's refusals; every `setGraph` answer carries the
  Engine's `refusedPacks`; the TS loader's refusals are recorded beside them. `statusOf` turns a need into `missing` or
  `refused` (with its reason, and `needsUpdate` for an API-level refusal).
- **The missing-role rule.** A role with no registered kind places nothing already. A role node the Engine answers
  `unknown-type` for is left out (`SystemsStore.leaveOut`) and the document sent again, for a load and an edit alike,
  so the system stays; `placeLoadedSystems` still discards a system refused for a reason of its own.
- **Re-resolve.** The host that owns the live project registers its store (`ProjectStore.followPacks()`: the plugin's
  control plane, the UI when it composes its own stores, a CLI session's `bootSession`). A registry commit or a move in
  the pack status schedules one re-resolve of that store in a microtask, so it runs once the loader's synchronous commit
  stretch (its commit, its halves marked loaded, its kind items accepted, its fallback's per-pack commits) is done. It
  tries each placeholder again through the load path (a success keeps its id and slot), places the roles now there
  (rebuilding a system whose role's construct hook never ran), adopts sections and services, publishes once and
  notifies, and leaves the project as clean as it was. A store nobody registered adopts lazily, as today.
- **The live load.** `loadPackNow(dir)` asks native (`__rp_packLoadNow`, `PackTable::load(dir)` on the process table),
  then runs `loadPacks` again with the builder sets this module set has loaded with. The plugin exposes it on its
  bridge, the CLI SDK exports it, the test hosts have the native hook.
- **UI.** A placeholder tile (frame, focus border, the card's text), its menu (Remove only), a `[!]` row per missing
  role on a live tile's menu, and one "Missing Packs" notice per loaded project.

**Tech Stack:** TypeScript (the shell's `src/`, `ui/`, `cli/`), zod, C++20 (`host/js/PackLoaderCore.cpp`,
`host/packs/PackTable.cpp`, `host/rpc/EngineRpcService.cpp`), the generator (`scripts/lib/packages.mjs`), the pure,
native, UI and plugin tiers, Catch2, `tools/run-cli-ts-tests.sh`.

---

## Where this sits

Part 1 of [docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) is built in milestones; M3's
design is [docs/downloadable-packs-m3-design.md](downloadable-packs-m3-design.md), and this plan builds it.

**Done:**
- M1;
- M2a to M2f;
- M5a, M5b, M5c ([the M5c plan](downloadable-packs-m5c-plan.md));
- the CLI on a pack ([its plan](downloadable-packs-cli-plan.md)).

**Left after this plan:**
- M4:
  - `.rppack` archives;
  - `pack install` / `list` / `remove` and `--pack`, on `loadPackNow`;
  - Settings > Packs, reading the pack status this plan fills.
- M5d: two plugin instances, the Node addon, concurrency, a sanitizer leg (a live load's included).

**The user's decisions (2026-11-27), from the design.**
1. **`rp.core.missing` is deferred.** No saved file names a node type; M3 covers systems (platform, core, embed) and
   roles.
2. **Naming the pack is recorded at save.** Each system notes the package that provided its platform and core and each
   role's, built in or a pack alike. A file saved before M3 falls back to the platform or role kind's name.
3. **Live arrival.** M3 builds the re-resolve and a minimal live load that drives it (a development pack folder loaded
   now); M4 puts install and Settings > Packs on that hook.
4. **A placeholder is a system in the systems store**, not a side table and not a raw layer under every system.

**What exists (read before starting).** Paths are relative to `packages/retroplug/` unless they start with
`packages/` or `tools/`.

*Load and save, `src/projectStore.ts`:*
- `LoadOutcome` (line 55): `loaded` carries `systems: number`.
- `load(path)` (822) routes by extension to `loadThin` (850) or `loadZip` (862); the DAW chunk is `loadBytes` (811).
  All three parse through `this.parse` (265), which calls `parseConfig`.
- `beginLoad` (874) refuses a newer schema, calls `toAbsolute`, runs `scanMissingFiles`, and either parks a
  `pendingLoad` for relink or calls `commit` (889).
- `commit`: `systems.clear()`, then `cfg.systems.forEach((s, i) => ...)` looks up `sramKey(i)`, `stateKey(i)`,
  `songKey(i)` by the FILTERED list's index and calls `systems.adopt(s, blobs)` with `holdGraph`; then
  `placeLoadedSystems()` (989) and `writeHeldBatteries()`; marks clean; returns `{ kind: "loaded", systems: n }`.
- `placeLoadedSystems` parses `sys-<id>` from `graph.lastErrors[].nodeId`, `systems.discard(id)`, logs
  `"[systems] ...: the engine refused it, so the project loads without it"`, and retries.
- `save` (734) and `buildArchive` (789) call `buildConfig(this.projectSettings, this.systems.systems(), name)`;
  `buildArchive` keys blobs by `systems().forEach((sys, i) => ...)`.
- `publishGraph` (278) returns early while `holdGraph`; `adoptPending` (221) adopts sections and services committed
  since; `adoptContributions` (255) publishes and notifies after it and has no production caller.
- The constructor (188) takes `contributions: ProjectContributions` (a test's fixed sections and services).
- `new ProjectStore` is called only by `src/appStores.ts:91` and `src/bootSession.ts:34` in the shell.

*The config, `src/projectConfig.ts`:*
- `SystemThin` (52), `ProjectConfig` (64), `ProjectFileConfig` (85, `extends Omit<ProjectConfig, "settings">`).
- `roleInstanceSchema` (317), `platformSchema` (330) and `coreSchema` (337) refine against the live console registry
  with `code: "custom"` issues; `systemThinSchema` (346) is strict; `projectConfigSchema` (369).
- `PROJECT_MIGRATIONS` (496), `K_PROJECT = 4` (510); `projectV1toV2` leaves an unknown platform's `core` unset.
- `buildConfig` (544), `mapSystemPaths` (576), `serializeConfig` (586, spreads `cfg`), `parseConfig` (597, drops a
  failing system at 615-619), `toAbsolute` (627).
- `src/configSchema.ts`'s `stringifyConfig` formats every root: 2-space indent, short primitive arrays inline, key order
  as the object has it.
- `src/projectBinaries.ts`: `sramKey(i)`, `stateKey(i)`, `songKey(i)`, `romKey(i)` = `systems/{i}/...`.
- `src/projectMissing.ts`: `scanMissingFiles(cfg, blobKeys, exists)` keys blobs by `cfg.systems` index; `relinkInConfig`
  indexes `cfg.systems[item.systemIndex]`.

*The systems store, `src/systemsStore.ts`:*
- `SystemView` (122); `entries` (146); `currentId` (214); `view()` (226); `systems()` (245); `graphSystems()` (256,
  `pipeline: protocolPipeline(e.roles)`); `primary()` (279); `setFocus` (318); `focusNext` (329); `duplicateSystem`
  (428); `removeSystem` (473); `rebuildInPlace` (508, spreads `...src` into the new entry); `clear()` (1079);
  `discard` (1118); `adopt` (1147); `construct` (1308); `suggestedFirst` (1443); `withMissingFeatureRoles` (1492);
  `boot` (1649, spreads its `rest` into the entry); `commitEdit` (1670).
- `adopt` returns null for an undeclared `embeddedRom` (1171), a ROM `classifyRom` answers `"unknown"` for, and a
  platform with no `defaultCoreFor` (1181). A stored role kind the registry lacks is kept, unparsed and inert.
- `adopt` writes `this.suggestedFirst(this.withMissingFeatureRoles(stored, ...), suggested)` into the entry's roles,
  so a save writes the moved order: `test/systems/store-graph.test.ts` pins that at lines 931 and 1001-1043.
- `allocSystemId()` (98) is module-scoped; ids start at 1, and 0 means "no focus".
- `graphSystems()` maps `entries` alone, and `SystemsStore`'s own `onGraphChange` default (157) publishes
  `{ systems: this.graphSystems(), routing: "stereo" }`.

*The graph, the routing and the parameter map:*
- `src/graph/buildGraph.ts`: `GraphModel` (~95-108: `systems`, `routing`, `shape`, `midiRouting?`, `models?`);
  `buildGraph` (406). It numbers everything by the index into `model.systems`:
  - the routing node, `{ mode, systems: systems.map((s) => s.id) }` (468), an output `sys-<id>` per entry;
  - `perSystem`: slot i's mix on host channels 2i and 2i+1 (564);
  - the multi-out lanes: `onePerInstance` `addTo(i % n, ...)`, `twoPerInstance` `addTo((2 * i) % n, ...)` (~610-616);
  - the one-system tests: `tapArmOf` (`model.systems.length === 1`, 394), `perChannel` (570), `splitValid` (591);
  - `linkGroups` (373) and `sumOrder` (384) by index, which order a sum, not a slot number;
  - `drivenCartSystem(systems)` (173): the first in slot order whose platform has a registered cart.
  - It returns `slots: systems.map((s) => s.id)` (658), the native system table, which must name exactly the
    document's system nodes.
- `packages/core/ts/midiRoute.ts`: the routing node routes by an output's POSITION in `settings.systems`
  (`routeTarget(data, mode, count)`), and refuses only a repeated id; an output nothing reads is allowed.
- `src/engineGraph.ts` `publish` (~58) copies `systems`, `routing`, `midiRouting`, `models` into its model.
- `src/parameterMap.ts` `projectParameterMap(systems, mode, registry)`: system `i`'s lanes are pool slots
  `i * CC_SLOTS_PER_SYSTEM + n`, and `preRoutingChannel(mode, i, systems.length, ...)` mirrors the routing's count; a
  system whose placed roles serve no `parameters` claims nothing. `src/pluginControlPlane.ts:107` passes
  `project.systems.view()`.
- `ui/screens/menu/menuDefs.ts`: `ctx.systems` sizes `multi` (1318) and `validAudioRoutings` (149, "exactly one
  system"); `buildContext` (926) hands `ctx.systems` to the packages' menu rows (sameboy's link group,
  `sameboy/ts/ui/index.ts:125`, needs two peers). `ui/main.tsx:47` sizes the first window from `view().length`.
  `ui/App.tsx` drives `useGameInput({ active: playing, focusedId })` at the focused id.
- `ui/stores/StoreProvider.tsx:42` composes the UI's own stores (`composeAppStores({ notify })`) when no control plane
  shares its (the UI harness).
- `src/systemsList.ts`: `SystemEntry`, `appendEntry`, `removeById`, `replaceById`, `nextFocusAfterRemove` (111).

*Registries and packs:*
- `src/contributions/registry.ts`: `commitContributions` swaps the registries, bumps the counter, then calls each
  `subscribeContributions` listener synchronously; `derived(build)` rebuilds on the first read after a commit.
- `loadPacks`' commit stretch (`loadUnguarded`, after its last `await`) is synchronous: `commitContributions`, then
  `staged.forEach(accept)` (marks the halves loaded, `kindsAdded`), or the fallback's per-pack commit-and-accept loop. A
  listener called inside the commit sees the registries swapped and the halves not yet marked loaded.
- `src/contributions/consoleRegistries.ts`: `consolesRegistry` (a descriptor's `core` must be a registered `system`
  role, checked in `appRegistries.ts`). `src/contributions/app.ts` re-exports `rolesRegistry`, `embedsRegistry` and
  the other app registries. Each entry is `{ pkg, item }`.
- `src/packs/loadPacks.ts`: `refused` map and `refuse()` (130-154), `packStatus()` (136), `loadPacks(host, builders)`
  (257, calls run one at a time on `chain`), `loadAppPacks()` (351). `src/packs/packHost.ts`: `PackInfo` (25, no
  `name`), `nativePackHost()` reads `__rp_packList`.
- `cli/sdkModule.ts`, `src/pluginControlPlane.ts:116`, `ui/main.tsx:63` (`loadUiPacks`), `src/render/worker.ts` and
  `test-native/_packsPrelude.ts` each load packs once. The plugin's control plane and UI are two hosts of one shared
  set (`tools/build-shared-modules.mjs` `setOf("app")`: `rp:cp`, `rp:ui`), so `loadPacks.ts` is one module instance for
  both.
- `src/generated/packages.ts` (gitignored, written by `scripts/lib/packages.mjs`'s `renderPackages`, line 854) exports
  `PACKAGE_IDS`; `test/packages/fixtures/expected/packages.ts` is its byte-for-byte golden. Only
  `packages/mesen/pack.json` ("Mesen") and `packages/bliptoaster/pack.json` ("BlipToaster") exist.

*Native:*
- `packages/native/src/host/js/PackLoaderCore.cpp`: `g_table`, `jsonString`, `jsonList`, `packList` (lists loaded packs
  with a TS half; no `name`), `packRefused`, `installGlobals(ctx, contracts, count, refusals)` (189); every txiki host
  calls `installPackLoader`, which passes `refusals = true`; a render's bare runtime passes false.
- `packages/native/src/host/packs/PackCandidate.hpp:28`: `struct PackRefusal { id; dir; reason; }`.
- `packages/native/src/host/packs/PackTable.cpp`: `levelAdvice` (44); the two API-level refusals at 302-305 and
  382-384; `refuse()` (250) pushes `{id, candidate.dir, reason}`.
- `packages/native/src/host/packs/ProcessPacks.cpp`: `processPacks()` sets the protocol and kinds trials.
- `packages/native/src/host/engine/Engine.cpp:736` `syncPacks()` runs at the start of every `setGraph`; a pack with a
  kinds half not loaded at the Engine's construction is refused there ("its kind items reach an Engine built after it
  loaded; they take effect at the next launch"). `Engine.hpp:387` `refusedPacks()`, `refusalReason(id)`.
- `packages/native/src/host/rpc/BackendTypes.hpp:91` `struct RpcSetGraphResult { bool ok; std::vector<RpcGraphError>
  errors; };`, filled at `EngineRpcService.cpp:119`. Both are in the global namespace.
- `packages/graph-runtime/native/src/graph/Planner.cpp:181-185`: a node of an unregistered type is
  `error(n.id, "", "unknown-type", "no node type \"<t>\" is registered")`.

*Tests and runners:*
- `test/consoles/registry.test.ts:300-324` asserts the silent drop of an unknown platform.
- `test/systems/store-graph.test.ts:483-520` and `test/systems/store-resources.test.ts:205-240` drop a system refused
  `activate-failed`; they stay as they are.
- `scripts/run-native-tests.mjs`: `--packs` bundles every `test-native/**/*.test.ts` on `retroplug-host-packs` with the
  render set's contracts external, `productGenerated({ dir: PRODUCT_TREE })` and `_packsPrelude.ts`, and sets
  `RETROPLUG_PACK_PATH` to BlipToaster and Mesen. Defines include `__CONFIG_DIR__`, `__REPO_RESOURCES_DIR__`,
  `__MGB_ROM__`.
- `test-native/plugin-controlplane.test.ts` imports `../src/pluginControlPlane` and drives `__rp_loadProjectB64` and
  `__rp_saveProjectB64`, the DPF state seams.
- `packages/native/test/plugin/PackModules.test.cpp:128` checks `__rp_packList()`'s exact string.
- `packages/native/test/packs/PackLoader.test.cpp:46` `checkRefused`, 192-200 the API-level fixtures.
- `packages/native/test/packs/PackEngines.test.cpp:103` builds an Engine that refuses `packfixture`.
- The API-level fixture is `packfixture-api-older` (target `retroplug-packfixture-api-older-pack`, folder
  `build/packs/packfixture-api-older/1.0.0`, EXCLUDE_FROM_ALL).
- `packages/native/test/packs/ts-fixture/CMakeLists.txt:102` stages the hand-written variants with `configure_file`.
- `resources/roms/bliptoaster.nes` has no battery (iNES byte 6 is `0x51`).

**Execution model.**
- Sonnet implements each task from its text; Opus reviews each task's diff before the next starts.
- The code here was not compiled. Where the tree differs, the implementer follows the tree, keeps the step's intent,
  and reports the deviation.
- Some code lines here run past 120 columns: wrap them to the tree's width as you write them, changing nothing else.
- Every task ends green on the tiers it lists, then commits (no attribution trailer, files staged by name, never
  `deps/sameboy`).
- Task 12 is the CI gate, which needs the user's go-ahead to push.

## Decisions this plan makes

- **The config keeps its `systems`.** `ProjectConfig` gains two optional members rather than a changed `systems`:
  `sources` (each parsed system's file index and raw JSON, parallel to `systems`) and `placeholders` (index, raw,
  needs). Every reader of `cfg.systems` and every test of it stays as it is. `serializeConfig` writes the four file
  members only, so neither leaks into a file.
- **`view()` is the live systems; `slotView()` is every slot.** What drives a running system reads `view()`: the CLI
  SDK, the sync latency, the render list and the render job's default, the watched ROMs, the packages' views and
  menu rows (`uiHost`, `buildContext`), the game input. What counts or lays out slots reads `slotView()`: the grid,
  the shell's menus (`multi`, `validAudioRoutings`), the first window's size, and the DAW parameter map. A
  placeholder's view keeps the running fields, read from its raw object (`roles: []`, `placed: []`,
  `battery: false`), plus `placeholder: { needs }`; a reader of `slotView()` that acts on a system skips an entry with
  `placeholder` set, and Task 9 lists each.
- **A placeholder holds its slot in the graph.** The design keeps a placeholder's slot so the routing and layout
  settings that point at a slot keep working. `GraphModel` gains `slots?: readonly number[]`, every slot's system id
  in slot order, an empty slot's (a placeholder's) included; `SystemsStore.graphSlots()` answers it and every publish
  passes it. `buildGraph` places nothing for an empty slot and numbers by slot: the routing node's `systems` is the
  slot list (the empty slot's output is unread, so its channels reach nothing, as they would reach a pack's system
  that ignored them), `perSystem` keeps an empty slot's silent bus, the multi-out lanes take `slotOf(s)`, and the
  one-system tests (split, NES tap, `perChannel`) count slots. The native slot table (`BuiltGraph.slots`) stays the
  live ids, which is what it must name. `linkGroups` and `sumOrder` need no change (they order a sum among live
  systems). The real cart's pick reads live systems only (`drivenCartSystem` is handed `graphSystems()`; a caller
  handing it views skips a placeholder). A `slots` list that does not hold every live id is ignored (the live ids
  are used), so a hand-built model keeps today's numbering.
- **The parameter map is projected over `slotView()`.** A placeholder has no roles, so it claims no parameter, and a
  system after it keeps the pool slots and the pre-routing channel it has on a build where the pack is loaded.
- **Slots are numbers on the placeholders.** A placeholder holds `slot`, its index among all slots; a live entry's slot
  is implied by the merge (`mergeSlots`). Only removals shift slots (a live removal, a discard, a placeholder's
  Remove). Appends go after every slot. `addPlaceholder(raw, blobs, needs)` appends; the design's `index` argument is
  not needed because `commit` walks the file in order.
- **A swap keeps the id.** `adoptInto(placeholderId, ...)` builds the system under the placeholder's id (it never had a
  core, so native never saw it) and inserts it at the placeholder's slot.
- **Role order: the save writes the file's order.** `suggestedFirst` still moves a role ahead in memory (the pipeline
  and `resolveTracker` read that order), and the entry records `savedOrder`, each role's stored position; `buildConfig`
  writes `roles` in that order. `rebuildInPlace` and `setRoleConfig` keep it (the first spreads the entry, the second
  edits in place); `duplicateSystem` copies it.
- **Provenance's shape.** `platform` names the console's package (it provides the core too); `embed` (additive, beside
  the design's two members) names the embedded ROM's package; `roles` maps each kind. A role kind the registry lacks
  keeps the provenance the file had for it. A built-in package's name is its `pack.json` name through the generated
  `PACKAGE_NAMES`, else its id; a pack's name and version come from `__rp_packList`, which gains `name`.
- **Which needs a placeholder has.** The platform need when the platform is missing, else the core need; then one need
  per stored role kind the registry lacks, except the core's own config role (the console's pack). An `embed` need for
  an undeclared `embeddedRom` is found at the parse; "no detector claims the ROM" and "no default core" at
  `adoptOrHold`, for a ROM file that exists (a missing file is still the relink's).
- **A v1 file's unknown-platform system stays dropped**: `projectV1toV2` leaves its `core` unset, so the parse fails on
  a missing field, not on a pack-provided name.
- **Placeholder paths.** A placeholder's raw `romPath` / `savPath` are made absolute at load and rebased at save, as a
  live system's are, so a Save As to another folder still names the file; a file saved in place writes the same bytes.
- **Placeholders are not in the missing-file scan.** Nothing can be built for them, so a gone ROM is not a relink.
- **`needsUpdate` and the refused version.** `PackRefusal` gains `needsUpdate` (set at the two API-level refusals);
  `__rp_packRefusals()` answers `[{id, reason, version?, needsUpdate}]`, the version read off a `<id>/<version>` folder.
- **Engine refusals ride `setGraph`.** `RpcSetGraphResult` gains `refusedPacks: [{id, reason}]` from the Engine, on
  every answer; no new RPC method. `EngineGraph` records them (`noteEngineRefusals`). An Engine refuses a pack in the
  `syncPacks` that starts a `setGraph`, so the first document naming the pack's node is already sent when TS learns of
  it.
- **The missing-role rule is reactive.** `ProjectStore.publishGraph` reads `unknown-type` errors at
  `sys-<id>/<kind>` or `sys-<id>/<kind>/out` for a role the system has, leaves that kind out (`SystemsStore.leaveOut`,
  kept with the planner's message) and sends again, until a document plans or nothing new is left out. That covers a
  load and an edit alike; a re-resolve forgets the left-out kinds and learns them again.
- **A role whose construct hook never ran is rebuilt.** An entry built while one of its roles had no registered type
  records `builtWithout`; when a commit registers one whose role type has `onConstruct`, the re-resolve rebuilds the
  system in place, its battery and working song carried as a reload carries them.
- **Who re-resolves: the store its host registers.** `ProjectStore.followPacks()` marks a store as the module set's
  live project, the one a pack's arrival re-resolves; a later call by another store takes over (one live project per
  module set). Called by the plugin's control plane (`pluginControlPlane.ts`, whose stores the editor reuses), by the
  UI's `StoreProvider` when it composes stores of its own (the UI harness, through `composeAppStores({ live: true })`),
  and by `bootSession` (a CLI session; the render worker builds no store through it that a pack arrives for, and one
  more registration there is harmless). A store nobody registered (a test's, an earlier session's) adopts what arrived
  lazily on its own reads, as every store does today; a pure test that wants the re-resolve calls `followPacks()`.
- **When it re-resolves: in a microtask after the loader's commit stretch.** The commit listener only schedules it
  (one pending re-resolve at a time), as does a move in the pack status (a live load's answer). A microtask runs once
  the synchronous code that committed is done: in `loadPacks` that is the commit, `accept` (the halves marked loaded,
  the kind items' `kindsAdded`) and, on its fallback path, every per-pack commit, so one re-resolve sees the whole load,
  and it runs before anything awaiting the load continues (`loadPackNow`'s caller, the control plane's `__rp_ready`).
  No timer: the native test host never fires `setTimeout`, and a promise job runs on every host. A registry commit from
  anywhere else (a test, the UI's commit seam) re-resolves the same way. The re-resolve restores the dirty flag it
  found.
- **The native hooks.** `__rp_packLoadNow(dir)` answers `{"loaded": id}` or `{"refused": reason}`; `__rp_packRefusals()`
  answers the table's refusals. Both are installed with `__rp_packRefused` (txiki hosts only), so the control plane,
  the UI, the CLI, the standalones and both test hosts have them, and a render's bare runtime has neither.
- **The builders a live load uses.** `loadPacks` records the union of the builder sets this module set has loaded
  with (`loadedBuilders()`); `loadPackNow` runs `loadPacks` with it (`APP_BUILDERS` when nothing loaded yet). In the
  plugin the UI and the control plane share the module, so once the editor's `loadUiPacks` has run, a live load loads
  the new pack's `ui` and `views` halves too; an editor opened later loads them at its own `loadUiPacks`.
- **The plugin bridge.** `__rp_loadPackNow(dir)` (fire and forget) and `pluginNs.loadPackNow` on the control plane; no
  C++ caller in M3. The CLI SDK exports `loadPackNow`.
- **Pack-less native files.** `test-native/packless/**` runs only in `--packs` mode, on `retroplug-host-packs`, bundled
  as the pack mode bundles (externals, product tree) but with no prelude and no `RETROPLUG_PACK_PATH`; the runner
  passes the pack folders as `__MESEN_PACK__`, `__BLIPTOASTER_PACK__` and `__PACKS_BUILD_DIR__`. `pnpm test:native`
  skips the folder.
- **The Engine-refused role's fixture.** `tsfix-proto-kind`, a hand-written variant with an app, a protocol and a kinds
  half: loaded live, every running Engine refuses it (its kind items wait for a launch), so its role's node is
  `unknown-type` while TS has registered the role.
- **SRAM through a swap is proven in the pure tier**, where the mock records the seed bytes a boot was handed: the
  BlipToaster ROM the native case uses has no battery.
- **`LoadOutcome.loaded` gains `missingPacks` only when it is non-empty**, so every existing `toEqual({ kind: "loaded",
  systems: n })` holds.
- **The notice shows once per project revision**: at a load's outcome, or at the editor's mount for a project the
  control plane loaded before it (the plugin).
- **No API level bump.** Nothing a pack links or imports changes: the refusal struct, the RPC types and the pack list
  are the host's, and no contract module changes.

## File structure

| File | Change |
|---|---|
| `src/projectConfig.ts` | `sources`, `placeholders`, `classifySystem`, `provenance` on `SystemThin`, `buildConfig`'s slot merge and saved role order, `serializeConfig` picks members, paths over raw systems |
| `src/projectMissing.ts` | blob keys by file index; relink updates the raw |
| `src/systemsList.ts` | `SystemEntry.savedOrder` / `provenance` / `builtWithout`; `mergeSlots`; `inSavedOrder` |
| `src/systemsStore.ts` | placeholders, `slotView`, `adoptOrHold`, `adoptInto`, `leaveOut`, `retryMissingRoles`, saved order |
| `src/projectStore.ts` | `seedOf`, the index fix, placeholders in load and save, `missingPacks`, the graph's slot list, the leave-out retry, `followPacks`, `reresolve` |
| `src/graph/buildGraph.ts`, `src/engineGraph.ts` | `GraphModel.slots`, numbering by slot; the model keeps it |
| `src/appStores.ts`, `src/bootSession.ts`, `ui/stores/StoreProvider.tsx` | the live store registered |
| `ui/App.tsx`, `ui/main.tsx` | game input skips a placeholder; the first window counts slots |
| `src/provenance.ts` (new) | `PackRef`, `Provenance`, its schema, `provenanceOf`, `packRefOf` |
| `src/packs/needs.ts` (new) | `PackNeed`, `needOf`, `statusOf`, `MissingPack`, `missingPacksOf`, the texts |
| `src/packs/refusals.ts` (new) | native, TS and Engine refusals; the pack status signal |
| `src/packs/loadPackNow.ts` (new) | the live load |
| `src/packs/loadPacks.ts`, `src/packs/packHost.ts` | `packStatus` members, TS refusals recorded, `loadedBuilders`; `PackInfo.name` |
| `src/engineGraph.ts`, `src/backend.ts` | `refusedPacks` on `SetGraphResult`, recorded |
| `src/pluginControlPlane.ts`, `cli/sdk.ts`, `cli/sdk-types.d.ts` | the parameter map over `slotView()`, `followPacks`, the bridge, the SDK export |
| `scripts/lib/packages.mjs`, `test/packages/fixtures/expected/packages.ts` | `PACKAGE_NAMES` |
| `scripts/run-native-tests.mjs`, root `package.json` | the pack-less folder; the fixtures `test:native:packs` builds |
| `packages/native/src/host/js/PackLoaderCore.cpp` | `name` in the pack list, `__rp_packRefusals`, `__rp_packLoadNow` |
| `packages/native/src/host/packs/PackCandidate.hpp`, `PackTable.cpp` | `needsUpdate` |
| `packages/native/src/host/rpc/BackendTypes.hpp`, `EngineRpcService.cpp` | `refusedPacks` |
| `packages/native/test/plugin/PackModules.test.cpp`, `test/packs/PackLoader.test.cpp`, `test/packs/PackEngines.test.cpp` | Catch2 cases |
| `packages/native/test/packs/ts-fixture/variants/tsfix-proto-kind/` (new), its `CMakeLists.txt` | the Engine-refused fixture |
| `ui/stores/useStores.ts`, `ui/screens/grid/SystemGrid.tsx`, `ui/screens/grid/PlaceholderTile.tsx` (new), `ui/screens/grid/placeholderText.ts` (new) | the tile |
| `ui/screens/menu/menuDefs.ts`, `ui/app/projectModals.ts`, `ui/app/useProjectModals.ts` | the menu, the `[!]` rows, the notice |
| Tests (new): `test/project/placeholder-slots.test.ts`, `test/project/placeholders.test.ts`, `test/project/provenance.test.ts`, `test/project/reresolve.test.ts`, `test/packs/refusals.test.ts`, `test/packs/load-pack-now.test.ts`, `test/menu/missing-packs.test.ts`, `test-native/packless/live-load.test.ts`, `test-native/packless/missing-packs.test.ts`, `test-ui/missing-packs.test.ts`, `tools/fixtures/ts-harness-packs/tests/live-load.test.ts` | |
| Tests (changed): `test/consoles/registry.test.ts`, `test/systems/store-graph.test.ts`, `test/project/export.test.ts`, `test/project/config.test.ts`, `test/menu/project-modals.test.ts`, `test/graph/build-graph.test.ts`, `test-native/plugin-controlplane.test.ts`, `tools/run-cli-ts-tests.sh` | |
| `spec/02`, `spec/03`, `spec/05`, `spec/06`, `spec/07`, `spec/09`, the loading design, this plan | docs |

---

### Task 1: A system keeps its file index for its blobs, and a save writes the roles in the file's order

The two latent fixes the design names: a dropped system no longer hands its zip entries to the system after it, and a
load that moves a role ahead (`suggestedFirst`) no longer moves it in the saved file.

**Files:**
- Modify: `packages/retroplug/src/projectConfig.ts` (`ProjectConfig` at 64, `ProjectFileConfig` at 85, `buildConfig`
  at 544, `serializeConfig` at 586, `parseConfig` at 597)
- Modify: `packages/retroplug/src/projectStore.ts` (`commit` at 889)
- Modify: `packages/retroplug/src/projectMissing.ts`
- Modify: `packages/retroplug/src/systemsList.ts` (`SystemEntry`)
- Modify: `packages/retroplug/src/systemsStore.ts` (`adopt` at 1147, `suggestedFirst` at 1443, `duplicateSystem` at
  428)
- Test: `packages/retroplug/test/project/export.test.ts`, `packages/retroplug/test/systems/store-graph.test.ts`

- [ ] **Step 1: Write the failing tests.** Append to `test/project/export.test.ts` (add `gbRomBattery` to its
  `../systems/fixtures` import):

```ts
test("a zip's entries reach the system they were written for when an entry before it is dropped", () => {
  // The design's blob shift (docs/downloadable-packs-m3-design.md, "Where things stand"): the parse drops system 0, and
  // the system after it used to read systems/0/sram, the dropped one's.
  const be = new MockBackend("/cfg");
  be.seed("/proj/a.gb", gbRomBattery());
  const { project } = newProject(be);
  const enc = new TextEncoder();
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [
      { platform: 42, core: "sameboy", romPath: "/proj/a.gb" }, // malformed: the parse drops it
      { platform: "gb", core: "sameboy", romPath: "/proj/a.gb" },
    ],
  };
  const own = new Uint8Array([7, 7, 7, 7]);
  const zip = be.zip([
    { name: "project.json", bytes: enc.encode(JSON.stringify(doc)) },
    { name: "systems/0/sram", bytes: new Uint8Array([1, 1, 1, 1]) },
    { name: "systems/1/sram", bytes: own },
  ])!;
  expect(project.loadBytes(zip, "")).toEqual({ kind: "loaded", systems: 1 });
  const [entry] = project.systems.systems();
  expect(be.bootOf(entry.id)?.sramBytes).toEqual(own);
});
```

  In `test/systems/store-graph.test.ts`, append beside "a collision's outcome survives a save and a load" (line 1080):

```ts
test("a save writes the roles in the order the file stored them, whatever order the load placed them in", () => {
  // docs/downloadable-packs-m3-design.md section 3: the load still moves the role this ROM calls for ahead (the
  // pipeline and the tracker read the system's order), and the file keeps its own.
  const { be, p, sys } = project();
  seedLsdjProject(be, "/proj/order-kept.rplg", [
    { kind: "sameboy", config: {} },
    { kind: "mgb", config: {} },
    { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiMap } },
  ]);
  const { result } = warned(() => p.load("/proj/order-kept.rplg"));
  expect(result.kind).toBe("loaded");
  const [entry] = sys.systems();
  expect(sys.view()[0].roles.map((r) => r.kind).slice(0, 3)).toEqual(["sameboy", "lsdj-sync", "mgb"]);
  expect(be.protocolNodes(entry.id)).toEqual(roleNodes(entry.id, "lsdj-sync"));
  expect(p.save("/proj/order-kept-saved.rplg")).toBe(true);
  const saved = JSON.parse(new TextDecoder().decode(be.readFile("/proj/order-kept-saved.rplg")!)) as {
    systems: { roles: { kind: string }[] }[];
  };
  expect(saved.systems[0].roles.map((r) => r.kind).slice(0, 3)).toEqual(["sameboy", "mgb", "lsdj-sync"]);
});
```

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/export && node scripts/run-tests.mjs systems/store-graph`
  Expected: the export case fails (`sramBytes` is `[1,1,1,1]`, or undefined); the order case fails at the saved
  order (`["sameboy","lsdj-sync","mgb"]`).

- [ ] **Step 3: `sources` on the parsed config.** In `src/projectConfig.ts`, after the `ProjectConfig` interface, add:

```ts
/** A parsed system's place in the file: its index in the file's `systems` array (what a zip's `systems/{i}/...` entries
 *  are keyed by) and its JSON as read, migrated. */
export interface SystemSource {
  index: number;
  raw: RawObject;
}
```

  Add to `ProjectConfig`, after `systems`:

```ts
  /** Each of `systems`' place in the file, index for index (parseConfig fills it; absent for a config a test or a
   *  build made, where `systems[k]` is the file's k-th). */
  sources?: SystemSource[];
```

  Change `ProjectFileConfig` to `extends Omit<ProjectConfig, "settings" | "sources">`, and add below the interfaces:

```ts
/** The file index of `cfg.systems[k]`: its source's, else `k`. */
export function sourceIndexOf(cfg: Pick<ProjectConfig, "sources">, k: number): number {
  return cfg.sources?.[k]?.index ?? k;
}
```

  In `parseConfig`, replace the systems loop:

```ts
  // Systems: keep each valid (object) entry, drop garbage.
  const systems: SystemThin[] = [];
  for (const raw of parsed.systems as unknown[]) {
    const r = systemThinSchema.safeParse(raw);
    if (r.success) systems.push(r.data as SystemThin);
  }
```

  with:

```ts
  // Systems: keep each valid (object) entry, drop garbage. Each kept one remembers its index in the file, which its zip
  // entries are keyed by, and its JSON as read.
  const systems: SystemThin[] = [];
  const sources: SystemSource[] = [];
  (parsed.systems as unknown[]).forEach((raw, index) => {
    const r = systemThinSchema.safeParse(raw);
    if (!r.success) return;
    systems.push(r.data as SystemThin);
    sources.push({ index, raw: raw as RawObject });
  });
```

  and the return line to `return { ...parsed, schemaVersion: parsed.schemaVersion as string, settings, systems,
  sources } as ProjectConfig;`.

- [ ] **Step 4: `serializeConfig` writes the file's members only.** Replace its first line
  (`const out: ProjectFileConfig = { ...cfg, ... }`) with:

```ts
  // The file's four members, in the order buildConfig writes them: a parsed config handed back here (a test's round
  // trip) carries `sources` and `placeholders`, which are the parse's bookkeeping and not the file's.
  const out: ProjectFileConfig = {
    schemaVersion: cfg.schemaVersion,
    ...(cfg.name !== undefined ? { name: cfg.name } : {}),
    settings: { ...cfg.settings },
    systems: cfg.systems.map((s) => ({ ...s })),
  };
```

- [ ] **Step 5: The blob lookup by file index.** In `src/projectStore.ts`, add above `export class ProjectStore`:

```ts
/** A system's seed from its archive entries: a savestate alone (it holds the battery, and seeding the battery too would
 *  cold-reseed it and drop the restored runtime state), else the battery; and a working song the export was carrying,
 *  with the battery current beside it, which is written over the savestate's own once the core exists. */
function seedOf(sram?: Uint8Array, state?: Uint8Array, songBytes?: Uint8Array): Parameters<SystemsStore["adopt"]>[1] {
  const sysBlobs = state ? { stateBytes: state } : sram ? { sramBytes: sram } : undefined;
  const song = songBytes ? decodeWorkingSong(songBytes) : null;
  return song ? { ...sysBlobs, song, freshSram: state ? sram : undefined } : sysBlobs;
}
```

  In `commit`, replace the body of `cfg.systems.forEach((s, i) => { ... })` (the blob lookups, the comments and the
  `adopt` call) with:

```ts
      cfg.systems.forEach((s, k) => {
        // Keyed by the system's index in the FILE: a system the parse dropped keeps its entries to itself.
        const i = sourceIndexOf(cfg, k);
        this.systems.adopt(s, seedOf(blobs.get(sramKey(i)), blobs.get(stateKey(i)), blobs.get(songKey(i))));
      });
```

  Import `sourceIndexOf` from `./projectConfig`. Keep the savestate and working-song comments: move them onto
  `seedOf`'s doc comment if anything there is not already said.

- [ ] **Step 6: The missing-file scan by file index.** In `src/projectMissing.ts`, import `sourceIndexOf` (a value
  import from `./projectConfig`) and in `scanMissingFiles` key the three blob lookups by `const at =
  sourceIndexOf(cfg, i);` (`romKey(at)`, `sramKey(at)`, `stateKey(at)`). `systemIndex` stays `i`, the index into
  `cfg.systems` that `relinkInConfig` reads. Update the header's "Addressed by config INDEX" to say the blob keys are
  the file's index.

- [ ] **Step 7: The saved order.** In `src/systemsList.ts`, add to `SystemEntry`:

```ts
  /** Where each of `roles` sits in the order a save writes, index for index: set when a load moved a role ahead
   *  (SystemsStore.suggestedFirst), so the file keeps the order it had (docs/downloadable-packs-m3-design.md section 3).
   *  Absent: `roles` is that order. Not serialized. */
  savedOrder?: readonly number[];
```

  and below `replaceById`:

```ts
/** `roles` in the order a save writes them (SystemEntry.savedOrder). */
export function inSavedOrder<T>(roles: readonly T[], savedOrder: readonly number[] | undefined): T[] {
  if (!savedOrder) return roles.slice();
  return roles
    .map((role, k) => ({ role, at: savedOrder[k] ?? k }))
    .sort((a, b) => a.at - b.at)
    .map((r) => r.role);
}
```

  In `src/systemsStore.ts`, make `suggestedFirst` generic, its body unchanged but its types:

```ts
  private suggestedFirst<T extends { readonly kind: string }>(roles: readonly T[], suggested: readonly RoleInstance[]): T[] {
```

  (the inner `collides` takes `(q: T)`). In `adopt`, replace:

```ts
    const roles = stored
      ? this.suggestedFirst(this.withMissingFeatureRoles(stored, core, suggested, romPath, embeddedRom), suggested)
      : suggested;
```

  with:

```ts
    // The order a save writes is the file's (docs/downloadable-packs-m3-design.md section 3): suggestedFirst moves a
    // role ahead for the pipeline and the tracker lookup, and savedOrder puts each role back where the file had it (a
    // role the load appended goes after the stored ones, where it was appended).
    const ordered = stored
      ? this.suggestedFirst(
          this.withMissingFeatureRoles(stored, core, suggested, romPath, embeddedRom).map((role, at) => ({
            kind: role.kind,
            role,
            at,
          })),
          suggested,
        )
      : null;
    const roles = ordered ? ordered.map((o) => o.role) : suggested;
    const savedOrder = ordered?.some((o, k) => o.at !== k) ? ordered.map((o) => o.at) : undefined;
```

  and add `...(savedOrder ? { savedOrder } : {}),` to the object `adopt` passes to `this.boot(spec, { ... })`. In
  `duplicateSystem`, add `...(src.savedOrder ? { savedOrder: [...src.savedOrder] } : {}),` to the entry it boots.

- [ ] **Step 8: `buildConfig` writes the saved order.** In `src/projectConfig.ts`, import `inSavedOrder` from
  `./systemsList` and change `if (e.roles && e.roles.length) t.roles = e.roles;` to:

```ts
      if (e.roles && e.roles.length) t.roles = inSavedOrder(e.roles, e.savedOrder);
```

- [ ] **Step 9: Change the tests that pinned the moved order.** Why the stored order is now what a save writes: the
  design's section 3 ("its roles keep their stored order (a known kind's config is still parsed; nothing moves)") and
  its "Where things stand" ("Its stored position can move (`suggestedFirst`)") make the file's order the one a save
  keeps. A role kind a missing pack provides sits between the others in a placeholder's or a live system's roles, and a
  save that moved the roles around it would no longer be "unchanged". The tests below pinned the moved order as what a
  later build reads first; they change, and the in-memory order they also pin (what places the protocol and resolves
  the tracker) stays as it is. In `test/systems/store-graph.test.ts`, exactly these:
  - "of two roles whose nodes write serialIn, a loaded system places the one its ROM calls for, either way round"
    (917): its line-931 comment "which is the order a save then writes" becomes "in memory; a save writes the order
    the file stored (docs/downloadable-packs-m3-design.md section 3)".
  - "a project storing both tracker markers resolves the one its ROM earns, and a save writes the moved order"
    (1001-1043) becomes "...and a save writes the order the file
    stored". Its saved-file expectation changes from `moved` to the stored order:

```ts
  expect(saved.systems[0].roles.map((r) => r.kind).slice(0, 4)).toEqual(["mesen", "risa", "risa-assets", "bliptoaster"]);
```

    and its comment says the file keeps its order while a load moves the earned marker ahead in memory; `again`'s
    expectations stay (a second load moves it the same way).
  - "of two tracker markers, a loaded system resolves the one its ROM earns, and keeps the stale one" (961): its
    line-989 comment "a save writes that order" becomes "a save writes the order the file stored"; its expectations
    read the view, which keeps the moved order, so none changes.
  - "a collision's outcome survives a save and a load" (1080) and "a suggested role that collides with nothing keeps
    its place" (1107) read the view after a save and a load: they pass unchanged, since a load of the stored order moves
    the same role again. Run them to confirm; any other case in the file or elsewhere that fails on a saved role order
    is listed in the report with the design's reason, never changed silently.

- [ ] **Step 10: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/export && node scripts/run-tests.mjs systems/store-graph`
  Expected: PASS.
  Then from the repo root: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`.
  Expected: all pass (the native tier's known machine-local `app-cores` GBA case aside, if it fails as before this
  task: compare with a run on the parent commit before calling anything new).

- [ ] **Step 11: Commit.**

```bash
git add packages/retroplug/src/projectConfig.ts packages/retroplug/src/projectStore.ts packages/retroplug/src/projectMissing.ts packages/retroplug/src/systemsList.ts packages/retroplug/src/systemsStore.ts packages/retroplug/test/project/export.test.ts packages/retroplug/test/systems/store-graph.test.ts
git commit -m "project: a system's zip entries follow its file index, and a save keeps the roles in the file's order"
```

---

### Task 2: Provenance written at save

**Files:**
- Create: `packages/retroplug/src/provenance.ts`
- Modify: `packages/retroplug/src/projectConfig.ts` (`SystemThin`, `systemThinSchema`, `buildConfig`)
- Modify: `packages/retroplug/src/systemsList.ts` (`SystemEntry.provenance`)
- Modify: `packages/retroplug/src/systemsStore.ts` (`adopt`'s config type and entry, `duplicateSystem`)
- Modify: `packages/retroplug/src/packs/packHost.ts` (`PackInfo.name`)
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderPackages`, line 854)
- Modify: `packages/retroplug/test/packages/fixtures/expected/packages.ts`
- Modify: `packages/native/src/host/js/PackLoaderCore.cpp` (`packList`)
- Modify: `packages/native/test/plugin/PackModules.test.cpp:128`
- Test: `packages/retroplug/test/project/provenance.test.ts` (new); changed:
  `packages/retroplug/test/project/config.test.ts`

- [ ] **Step 1: Write the failing test.** Create `test/project/provenance.test.ts`:

```ts
// A saved system names the package that provided each thing it names (docs/downloadable-packs-m3-design.md section 4):
// its platform's console, its embedded ROM's and each role's, built in or a pack alike, from the registries' `pkg`, with
// a pack's name and version from the pack list and a built-in's name from its pack.json (or its id).
import { test, expect } from "@retroplug/testing";
import { buildConfig, DEFAULT_SETTINGS, parseConfig } from "../../src/projectConfig";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import type { SystemEntry } from "../../src/systemsList";
import type { RoleType } from "@retroplug/core/systemRoles";

function entry(over: Partial<SystemEntry>): SystemEntry {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/a.gb",
    savPath: "",
    savSuffix: 0,
    embeddedRom: "",
    battery: false,
    settings: { gainDb: 0, reloadOnRomChange: false },
    roles: [],
    romName: "a",
    resources: { rom: 1 },
    ...over,
  } as SystemEntry;
}

test("a built-in package is recorded by its id, and by its pack.json name where it has one", () => {
  const [gb] = buildConfig(DEFAULT_SETTINGS, [entry({ roles: [{ kind: "sameboy", config: {} }] })]).systems;
  expect(gb.provenance).toEqual({
    platform: { id: "sameboy", name: "sameboy" },
    roles: { sameboy: { id: "sameboy", name: "sameboy" } },
  });
  // The desktop links Mesen, whose pack.json names it: a project saved here names Mesen on a build where it is a pack.
  const [nes] = buildConfig(DEFAULT_SETTINGS, [
    entry({ platform: "nes", core: "mesen", romPath: "/roms/a.nes", roles: [{ kind: "mesen", config: {} }] }),
  ]).systems;
  expect(nes.provenance?.platform).toEqual({ id: "mesen", name: "Mesen" });
  expect(nes.provenance?.roles?.mesen).toEqual({ id: "mesen", name: "Mesen" });
});

test("a pack is recorded with the name and version the pack list gives it", () => {
  const g = globalThis as Record<string, unknown>;
  g.__rp_packList = () =>
    JSON.stringify([
      { id: "provfix", name: "Provenance Fixture", version: "4.5.6", halves: ["app"],
        provides: { platforms: [], nodeTypes: [], roles: ["provfix-role"] }, requires: { packs: [], platforms: [] } },
    ]);
  const role = { kind: "provfix-role", category: "feature", schema: { parse: () => ({}) } } as unknown as RoleType;
  commitContributions(appAdditions("provfix", { roles: [role] }));
  const [sys] = buildConfig(DEFAULT_SETTINGS, [entry({ roles: [{ kind: "provfix-role", config: {} }] })]).systems;
  expect(sys.provenance?.roles?.["provfix-role"]).toEqual({ id: "provfix", name: "Provenance Fixture", version: "4.5.6" });
  delete g.__rp_packList;
});

test("a role kind nothing registers keeps the provenance the file had for it, and a reader keeps provenance", () => {
  const stored = { platform: { id: "sameboy", name: "sameboy" }, roles: { "gone-kind": { id: "gone-pack", name: "Gone", version: "1.0.0" } } };
  const parsed = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [{ platform: "gb", core: "sameboy", romPath: "/roms/a.gb", roles: [{ kind: "gone-kind", config: {} }], provenance: stored }],
  }));
  expect(parsed.systems[0].provenance).toEqual(stored);
  const [sys] = buildConfig(DEFAULT_SETTINGS, [
    entry({ roles: [{ kind: "gone-kind", config: {} }], provenance: parsed.systems[0].provenance }),
  ]).systems;
  expect(sys.provenance?.roles?.["gone-kind"]).toEqual({ id: "gone-pack", name: "Gone", version: "1.0.0" });
});

test("a malformed provenance is read as none, and the system still parses", () => {
  const parsed = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [{ platform: "gb", core: "sameboy", romPath: "/roms/a.gb", provenance: { platform: 5, roles: "x" } }],
  }));
  expect(parsed.systems.length).toBe(1);
  expect(parsed.systems[0].provenance).toEqual({});
});
```

- [ ] **Step 2: Run it and see it fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/provenance`
  Expected: FAIL (a typecheck or bundle error: `provenance` is not a `SystemThin` member; `SystemEntry` has none).

- [ ] **Step 3: The generated names.** In `scripts/lib/packages.mjs`'s `renderPackages`, after the `PACKAGE_IDS`
  block's `"];",` and its blank line, add:

```js
    "/** Each package's display name where its pack.json gives one (a package with none is named by its id): what a saved",
    " *  project's provenance records for a built-in package (docs/downloadable-packs-m3-design.md section 4). */",
    "export const PACKAGE_NAMES: Readonly<Record<string, string>> = {",
    ...packNames(packages).map(([id, name]) => `  ${JSON.stringify(id)}: ${JSON.stringify(name)},`),
    "};",
    "",
```

  and above `renderPackages`:

```js
/** [id, name] for each package with a pack.json that names it, in product order. */
function packNames(packages) {
  const out = [];
  for (const p of packages) {
    const file = join(p.pkg.dir, "pack.json");
    if (!existsSync(file)) continue;
    const name = readJson(file).name;
    if (typeof name === "string" && name !== "") out.push([p.pkg.name, name]);
  }
  return out;
}
```

  Check that `renderPackages`' `packages` items carry `pkg.dir` (`readPackage` returns `{ name, dir, ... }`; the caller
  passes `contributionsOf`, built from `ordered` at line 1708). In `test/packages/fixtures/expected/packages.ts`, after
  the `PACKAGE_IDS` block, add the empty block the fixture tree yields (none of its packages has a `pack.json`):

```ts
/** Each package's display name where its pack.json gives one (a package with none is named by its id): what a saved
 *  project's provenance records for a built-in package (docs/downloadable-packs-m3-design.md section 4). */
export const PACKAGE_NAMES: Readonly<Record<string, string>> = {
};
```

- [ ] **Step 4: The pack list's name.** In `packages/native/src/host/js/PackLoaderCore.cpp`'s `packList`, change
  `"{\"id\":" + jsonString(m.id) + ",\"version\":"` to
  `"{\"id\":" + jsonString(m.id) + ",\"name\":" + jsonString(m.name) + ",\"version\":"`. In `PackModules.test.cpp:128`
  the expected list becomes:

```cpp
    CHECK(readString(rt, "globalThis.__rp_packList()") ==
          R"([{"id":"pmgood","name":"Pack modules fixture (good)","version":"1.0.0","halves":["app","ui"],)"
          R"("provides":{"platforms":[],"nodeTypes":[],"roles":[]},"requires":{"packs":[],"platforms":[]}}])");
```

  Update `PackModules.hpp`'s comment on `__rp_packList` (line ~31) to list `name`. In `src/packs/packHost.ts`, add to
  `PackInfo` after `id`:

```ts
  /** pack.json's name: what a project's provenance and Settings > Packs call the pack (absent from a test's host). */
  readonly name?: string;
```

- [ ] **Step 5: The provenance module.** Create `src/provenance.ts`:

```ts
// Which package provided what a saved system names (docs/downloadable-packs-m3-design.md section 4): its platform and
// core (the console's package), its embedded ROM's and each role's, so a project opened where a pack is missing can name
// the pack it needs. Written at every save, read back with the system; additive, so no migration step (a reader that
// does not know it ignores it). A pack is named and versioned from the pack list (__rp_packList), a built-in package by
// its pack.json name (the generated PACKAGE_NAMES) or its id.
import { z } from "zod";
import { consolesRegistry } from "./contributions/consoleRegistries";
import { embedsRegistry, rolesRegistry } from "./contributions/app";
import { derived } from "./contributions/registry";
import { PACKAGE_NAMES } from "./generated/packages";
import { nativePackHost } from "./packs/packHost";

/** A package as a saved project names it: its id, its display name, and its version when it is a pack. */
export interface PackRef {
  readonly id: string;
  readonly name: string;
  readonly version?: string;
}

export interface Provenance {
  /** The console's package: it provides the platform and its core. */
  readonly platform?: PackRef;
  /** The embedded ROM's package. */
  readonly embed?: PackRef;
  /** Each role kind's package, by kind. */
  readonly roles?: Readonly<Record<string, PackRef>>;
}

const packRefSchema = z.object({ id: z.string(), name: z.string(), version: z.string().optional() });

const ref = (r: PackRef): PackRef =>
  r.version !== undefined ? { id: r.id, name: r.name, version: r.version } : { id: r.id, name: r.name };

/** A parsed provenance with every absent member left out (zod's `.catch(undefined)` leaves the key in place). */
function stripUndefined(p: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> }): Provenance {
  const out: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> } = {};
  if (p.platform) out.platform = ref(p.platform);
  if (p.embed) out.embed = ref(p.embed);
  if (p.roles) {
    const roles: Record<string, PackRef> = {};
    for (const [kind, r] of Object.entries(p.roles)) put(roles, kind, ref(r));
    out.roles = roles;
  }
  return out;
}

/** Tolerant: a malformed member reads as absent, and the system it is on still parses. The output carries only the
 *  members that read, so a parsed system compares equal to the provenance its file wrote. */
export const provenanceSchema = z
  .object({
    platform: packRefSchema.optional().catch(undefined),
    embed: packRefSchema.optional().catch(undefined),
    roles: z.record(z.string(), packRefSchema).optional().catch(undefined),
  })
  .transform(stripUndefined);

/** A raw provenance member as this build reads it, or undefined for none / not an object. */
export function readProvenance(raw: unknown): Provenance | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const r = provenanceSchema.safeParse(raw);
  return r.success ? r.data : undefined;
}

/** Store `value` as an own property: a hand-edited `__proto__` kind is data, never the record's prototype. */
function put(record: Record<string, PackRef>, key: string, value: PackRef): void {
  Object.defineProperty(record, key, { value, enumerable: true, writable: true, configurable: true });
}

/** The registries' providers, rebuilt after a commit (a pack's commit follows its native load, so the pack list is read
 *  again with them). */
const providers = derived(() => {
  const platforms = new Map<string, string>();
  for (const { pkg, item } of consolesRegistry.entries()) for (const p of item.platforms) platforms.set(p.id, pkg);
  const roles = new Map<string, string>();
  for (const { pkg, item } of rolesRegistry.entries()) roles.set(item.kind, pkg);
  const embeds = new Map<string, string>();
  for (const { pkg, item } of embedsRegistry.entries()) embeds.set(item.id, pkg);
  const packs = new Map(nativePackHost().list().map((p) => [p.id, p]));
  return { platforms, roles, embeds, packs };
});

/** The package `pkg` (a contribution's `pkg`) as a project records it. */
export function packRefOf(pkg: string): PackRef {
  const pack = providers().packs.get(pkg);
  if (pack) return { id: pkg, name: pack.name ?? pkg, version: pack.version };
  const name = Object.prototype.hasOwnProperty.call(PACKAGE_NAMES, pkg) ? PACKAGE_NAMES[pkg] : pkg;
  return { id: pkg, name };
}

const providerOf = (map: ReadonlyMap<string, string>, key: string): PackRef | undefined => {
  const pkg = map.get(key);
  return pkg === undefined ? undefined : packRefOf(pkg);
};

/** What a provenance is written from: a system's platform, embedded ROM and roles (in the order a save writes them),
 *  and the provenance it was read with. */
export interface ProvenanceSource {
  readonly platform: string;
  readonly embeddedRom: string;
  readonly roles: readonly { readonly kind: string }[];
  readonly provenance?: Provenance;
}

/** The provenance a save writes for `s`: each thing it names, by the package that registers it now, else by the
 *  package the file named for it (a role kind nothing registers keeps its pack). Undefined when it names nothing. */
export function provenanceOf(s: ProvenanceSource): Provenance | undefined {
  const { platforms, roles: roleProviders, embeds } = providers();
  const platform = providerOf(platforms, s.platform) ?? s.provenance?.platform;
  const embed = s.embeddedRom ? (providerOf(embeds, s.embeddedRom) ?? s.provenance?.embed) : undefined;
  const roles: Record<string, PackRef> = {};
  for (const { kind } of s.roles) {
    if (Object.prototype.hasOwnProperty.call(roles, kind)) continue;
    const r = providerOf(roleProviders, kind) ?? s.provenance?.roles?.[kind];
    if (r) put(roles, kind, r);
  }
  const out: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> } = {};
  if (platform) out.platform = platform;
  if (embed) out.embed = embed;
  if (Object.keys(roles).length) out.roles = roles;
  return Object.keys(out).length ? out : undefined;
}
```

  `stringifyConfig` drops undefined keys, but `ref` and `stripUndefined` keep an absent member absent, so `toEqual`
  holds either way.

- [ ] **Step 6: Read and write it.** In `src/projectConfig.ts`:
  - Import `{ provenanceOf, provenanceSchema, type Provenance }` from `./provenance`.
  - Add to `SystemThin`, last:
    `provenance?: Provenance; // which package provided what it names (docs/downloadable-packs-m3-design.md 4)`.
  - Add to `systemThinSchema`, after `workingSlot`: `// Additive, so no migration: absent for a file saved before M3.`
    then `provenance: provenanceSchema.optional().catch(undefined),`.
  - In `buildConfig`'s system map, after the `workingSlot` line, replace the roles line from Task 1 and add the
    provenance, so both follow the saved order:

```ts
      const roles = inSavedOrder(e.roles ?? [], e.savedOrder);
      if (roles.length) t.roles = roles;
      if (e.workingSlot) t.workingSlot = { ...e.workingSlot };
      const provenance = provenanceOf({ platform: e.platform, embeddedRom: e.embeddedRom, roles, provenance: e.provenance });
      if (provenance) t.provenance = provenance;
```

    (move the existing `if (e.workingSlot)` line rather than duplicating it). A file with no `provenance` parses
    with none: `.optional()` never runs the object schema on an absent key, so `tolerance.test.ts`'s whole-system
    `toEqual` is unchanged; check that on the run, and report it if zod leaves a `provenance: undefined` key.
  - In `src/systemsList.ts`, add to `SystemEntry`:
    `/** The provenance the system was read with: what a save names for a role kind nothing registers now. Not
    rewritten here; buildConfig writes a fresh one. */ provenance?: Provenance;` (type import from `./provenance`).
  - In `src/systemsStore.ts`'s `adopt`, add `provenance?: Provenance;` to the `config` parameter's type, and
    `...(config.provenance ? { provenance: config.provenance } : {}),` to the entry it boots; in `duplicateSystem`,
    `...(src.provenance ? { provenance: src.provenance } : {}),`.

- [ ] **Step 7: Change the tests whose built systems now carry provenance.** In `test/project/config.test.ts`, the
  cases "buildConfig: systems → thin entries..." (line 42) and "serializeConfig + parseConfig: round-trips" (line 66)
  compare
  whole systems: add the provenance each now carries (`provenance: { platform: { id: "sameboy", name: "sameboy" } }`
  for a `gb` system with no roles, and for `embeddedRom: "mgb"` also `embed: { id: "mgb", name: "mgb" }`). Run the
  pure tier and change, the same way, every other expectation whose only difference is the new `provenance` member;
  list each in the report. A difference in anything else is a regression: stop and report it.

- [ ] **Step 8: Run it and see it pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/provenance && node scripts/run-tests.mjs packages/packages-generator`
  Expected: PASS.
  Then: `cmake --build build --target retroplug-plugin-test -j$(nproc) && node packages/retroplug/scripts/run-plugin-tests.mjs plugin-test`,
  `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`, `pnpm test:cli-ts`.
  Expected: all pass. `grep -c PACKAGE_NAMES packages/retroplug/src/generated/packages.ts` is 1 or more, and the block
  holds `"mesen": "Mesen"` and `"bliptoaster": "BlipToaster"`.

- [ ] **Step 9: Commit.**

```bash
git add packages/retroplug/src/provenance.ts packages/retroplug/src/projectConfig.ts packages/retroplug/src/systemsList.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/packs/packHost.ts packages/retroplug/scripts/lib/packages.mjs packages/retroplug/test/packages/fixtures/expected/packages.ts packages/native/src/host/js/PackLoaderCore.cpp packages/native/src/host/js/PackModules.hpp packages/native/test/plugin/PackModules.test.cpp packages/retroplug/test/project/provenance.test.ts packages/retroplug/test/project/config.test.ts
git commit -m "project: a saved system names the package that provided its platform, its embed and each role"
```

  (Add each other test file Step 7 changed, by name.)

---

### Task 3: Placeholders: parsed, held in the systems store, saved back byte for byte, removable

**Files:**
- Create: `packages/retroplug/src/packs/needs.ts`
- Modify: `packages/retroplug/src/projectConfig.ts` (`PlaceholderThin`, `ProjectConfig.placeholders`, `classifySystem`,
  `parseConfig`, `buildConfig`'s `held`, `mapSystemPaths`, `toAbsolute`)
- Modify: `packages/retroplug/src/systemsList.ts` (`mergeSlots`)
- Modify: `packages/retroplug/src/systemsStore.ts` (`PlaceholderEntry`, `SystemView.placeholder`, `addPlaceholder`,
  `placeholders`, `saveOrder`, `slotView`, `removeSystem`, `discard`, `clear`, `setFocus`, `focusNext`, `currentId`)
- Modify: `packages/retroplug/src/projectStore.ts` (`LoadOutcome`, `commit`, `buildArchive`, `save`, `missingPacks`)
- Test: `packages/retroplug/test/project/placeholders.test.ts` (new); changed:
  `packages/retroplug/test/consoles/registry.test.ts:313-323`

- [ ] **Step 1: Write the failing tests.** Create `test/project/placeholders.test.ts`:

```ts
// A system this build cannot build because a pack is missing (docs/downloadable-packs-m3-design.md sections 1 to 3):
// the parse keeps it with what it needs, the systems store holds it in a slot of its own, and a save writes its JSON and
// its zip entries back as they came. A malformed system is still dropped. Every id here is this file's own.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { parseConfig } from "../../src/projectConfig";
import { gbRomBattery } from "../systems/fixtures";

const enc = new TextEncoder();
const dec = new TextDecoder();

const FX = { id: "fxpack", name: "FX Pack", version: "2.0.0" };
const FX_TRACKER = { id: "fxtracker", name: "FX Tracker", version: "1.1.0" };

/** A system on a platform no package of this product provides, with a role nothing registers and a member no build
 *  reads: everything a placeholder must write back. */
const PLACEHOLDER = {
  platform: "fx-missing",
  core: "fx-core",
  romPath: "/roms/song.fx",
  savSuffix: 2,
  roles: [{ kind: "fx-core", config: { level: 3 } }, { kind: "fx-marker", config: {} }],
  provenance: { platform: FX, roles: { "fx-marker": FX_TRACKER } },
  futureField: { kept: true, list: [1, 2, 3] },
};

const gb = (romPath: string) => ({ platform: "gb", core: "sameboy", romPath });

function newProject() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  be.seed("/roms/b.gb", gbRomBattery());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  return { be, project };
}

function zipOf(be: MockBackend, systems: unknown[], blobs: Record<string, number[]>): Uint8Array {
  const doc = { schemaVersion: "4", settings: {}, systems };
  return be.zip([
    { name: "project.json", bytes: enc.encode(JSON.stringify(doc)) },
    ...Object.entries(blobs).map(([name, bytes]) => ({ name, bytes: new Uint8Array(bytes) })),
  ])!;
}

test("the parse keeps a system that fails only on a pack-provided name, with what it needs, and drops a malformed one", () => {
  const cfg = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [gb("/roms/a.gb"), PLACEHOLDER, { platform: "n64-missing", core: "sameboy" }, { platform: 7, core: "sameboy" }],
  }));
  expect(cfg.systems.map((s) => s.romPath)).toEqual(["/roms/a.gb"]);
  expect(cfg.placeholders?.map((p) => p.index)).toEqual([1, 2]);
  expect(cfg.placeholders?.[0].raw).toEqual(PLACEHOLDER);
  // The platform from its provenance; the core's own config role is the console's, so it is not listed again; a role
  // kind nothing registers, by the pack the file named for it.
  expect(cfg.placeholders?.[0].needs).toEqual([
    { what: "platform", key: "fx-missing", pack: FX, status: "missing" },
    { what: "role", key: "fx-marker", pack: FX_TRACKER, status: "missing" },
  ]);
  // Saved before M3: no provenance, so the need names the platform.
  expect(cfg.placeholders?.[1].needs).toEqual([{ what: "platform", key: "n64-missing", status: "missing" }]);
});

test("an embedded ROM no package declares makes a placeholder at the parse", () => {
  const cfg = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [{ platform: "gb", core: "sameboy", embeddedRom: "fx-synth", provenance: { embed: { id: "fxembeds", name: "FX Embeds" } } }],
  }));
  expect(cfg.systems).toEqual([]);
  expect(cfg.placeholders?.[0].needs).toEqual([
    { what: "embed", key: "fx-synth", pack: { id: "fxembeds", name: "FX Embeds" }, status: "missing" },
  ]);
});

test("a placeholder between two live systems keeps its slot, its JSON and its entries, and the system after it its own", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER, gb("/roms/b.gb")], {
    "systems/0/sram": [1, 1, 1],
    "systems/1/sram": [9, 8, 7],
    "systems/1/state": [6, 5],
    "systems/1/song": [4],
    "systems/2/sram": [3, 3, 3],
  });
  const outcome = project.loadBytes(zip, "");
  expect(outcome.kind).toBe("loaded");
  if (outcome.kind !== "loaded") return;
  expect(outcome.systems).toBe(2);
  expect(outcome.missingPacks).toEqual([
    { id: "fxpack", name: "FX Pack", version: "2.0.0", status: "missing", systems: 1, roles: 0 },
    { id: "fxtracker", name: "FX Tracker", version: "1.1.0", status: "missing", systems: 1, roles: 0 },
  ]);
  const slots = project.systems.slotView();
  expect(slots.map((v) => v.placeholder !== undefined)).toEqual([false, true, false]);
  expect(slots[1].platform).toBe("fx-missing");
  expect(slots[1].romPath).toBe("/roms/song.fx");
  expect(project.systems.view().map((v) => v.id)).toEqual([slots[0].id, slots[2].id]);
  expect(be.bootOf(slots[2].id)?.sramBytes).toEqual(new Uint8Array([3, 3, 3]));
  expect(project.isDirty()).toBe(false);

  const entries = be.unzip(project.exportBytes()!)!;
  const byName = (name: string) => entries.find((e) => e.name === name)?.bytes;
  const doc = JSON.parse(dec.decode(byName("project.json")!)) as { systems: unknown[] };
  expect(doc.systems.length).toBe(3);
  expect(doc.systems[1]).toEqual(PLACEHOLDER);
  expect(byName("systems/1/sram")).toEqual(new Uint8Array([9, 8, 7]));
  expect(byName("systems/1/state")).toEqual(new Uint8Array([6, 5]));
  expect(byName("systems/1/song")).toEqual(new Uint8Array([4]));
  // The live systems' entries are the mock pump's, under their own slots' indices.
  expect(byName("systems/0/sram") !== undefined).toBe(true);
  expect(byName("systems/2/sram") !== undefined).toBe(true);
});

test("a file this build wrote saves to identical bytes, its placeholder included", () => {
  const { be, project } = newProject();
  be.seed("/proj/m3.rplg", JSON.stringify({ schemaVersion: "4", settings: {}, systems: [gb("/roms/a.gb"), PLACEHOLDER] }));
  expect(project.load("/proj/m3.rplg").kind).toBe("loaded");
  expect(project.save("/proj/one.rplg")).toBe(true);
  expect(project.load("/proj/one.rplg").kind).toBe("loaded");
  expect(project.save("/proj/two.rplg")).toBe(true);
  const one = be.readText("/proj/one.rplg")!;
  expect(be.readText("/proj/two.rplg")).toBe(one);
  // The placeholder's ROM is outside the project's folder, so it stays absolute, exactly as the file had it.
  expect((JSON.parse(one) as { systems: unknown[] }).systems[1]).toEqual(PLACEHOLDER);
});

test("Remove is a placeholder's one edit: the project is dirty, and its slot and entries are gone", () => {
  const { be, project } = newProject();
  const zip = zipOf(be, [PLACEHOLDER, gb("/roms/b.gb")], { "systems/0/sram": [9, 8, 7], "systems/1/sram": [3, 3, 3] });
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  const [held, live] = project.systems.slotView();
  expect(held.placeholder !== undefined).toBe(true);
  expect(project.systems.removeSystem(held.id)).toBe(true);
  expect(project.isDirty()).toBe(true);
  expect(project.systems.slotView().map((v) => v.id)).toEqual([live.id]);
  const entries = be.unzip(project.exportBytes()!)!;
  const doc = JSON.parse(dec.decode(entries.find((e) => e.name === "project.json")!.bytes)) as { systems: { romPath: string }[] };
  expect(doc.systems.map((s) => s.romPath)).toEqual(["/roms/b.gb"]);
  // Renumbered with the slots: the live system's entries are systems/0 now, and nothing is left at systems/1.
  expect(entries.some((e) => e.name.startsWith("systems/1/"))).toBe(false);
  expect(entries.some((e) => e.name === "systems/0/sram")).toBe(true);
});

test("a placeholder can take the focus, and the instance it names follows it", () => {
  const { be, project } = newProject();
  expect(project.loadBytes(zipOf(be, [gb("/roms/a.gb"), PLACEHOLDER], {}), "").kind).toBe("loaded");
  const [, held] = project.systems.slotView();
  expect(project.systems.setFocus(held.id)).toBe(true);
  expect(project.systems.slotView()[1].focused).toBe(true);
  expect(project.systems.currentId(held.id)).toBe(held.id);
});
```

  In `test/consoles/registry.test.ts`, the end of "projectConfig refuses an unknown platform and core" (lines 313-323)
  becomes:

```ts
  // parseConfig keeps an entry that fails only on a platform or core no package registers as a placeholder
  // (docs/downloadable-packs-m3-design.md section 1), so a file with one such entry loads the others and saves it back.
  const loaded = parseConfig(
    JSON.stringify({
      schemaVersion: "4",
      systems: [
        { platform: "n64", core: "mesen" },
        { platform: "gb", core: "sameboy" },
      ],
    }),
  );
  expect(loaded.systems.map((s) => s.platform)).toEqual(["gb"]);
  expect(loaded.placeholders?.map((p) => p.needs)).toEqual([[{ what: "platform", key: "n64", status: "missing" }]]);
```

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholders`
  Expected: FAIL (`placeholders`, `slotView` and `missingPacks` do not exist).

- [ ] **Step 3: The needs module.** Create `src/packs/needs.ts`:

```ts
// What a project needs that this build lacks (docs/downloadable-packs-m3-design.md sections 1, 2 and 8): a placeholder
// system's platform, core or embedded ROM and the role kinds it stores that nothing registers, and a live system's
// missing roles; the pack each comes from (the file's provenance) and whether that pack is missing or refused. The shell
// names no pack, platform or kind: every string the UI shows is built here from a need.
import type { PackRef } from "../provenance";
import type { SystemView } from "../systemsStore";

export type NeedWhat = "platform" | "core" | "embed" | "role";

export interface PackNeed {
  readonly what: NeedWhat;
  /** The platform id, core id, embedded ROM id or role kind the file names. */
  readonly key: string;
  /** The package the file says provides it; absent for a file saved before M3. */
  readonly pack?: PackRef;
  readonly status: "missing" | "refused";
  /** Why the pack was refused. */
  readonly reason?: string;
  /** The refusal is the pack's API level against this RetroPlug's: the "needs updating" card. */
  readonly needsUpdate?: boolean;
}

/** A need as the parse finds it: missing until statusOf reads the refusals. */
export function needOf(what: NeedWhat, key: string, pack?: PackRef): PackNeed {
  return pack ? { what, key, pack, status: "missing" } : { what, key, status: "missing" };
}

/** `need` with its status as the packs stand now. (Refusals are read here from M3's refusal sources, see
 *  src/packs/refusals.ts.) */
export function statusOf(need: PackNeed): PackNeed {
  return need;
}

/** One pack (or, with no provenance, one platform or kind) a project needs, and what it covers. */
export interface MissingPack {
  /** The pack's id, or `<what>:<key>` for a need the file names no pack for. */
  readonly id: string;
  readonly name: string;
  readonly version?: string;
  readonly status: "missing" | "refused";
  readonly reason?: string;
  readonly needsUpdate?: boolean;
  /** The placeholder systems that need it, and the live systems' roles. */
  readonly systems: number;
  readonly roles: number;
}

const groupOf = (need: PackNeed): string => need.pack?.id ?? `${need.what}:${need.key}`;

/** The distinct packs `views` (SystemsStore.slotView) need, in the order first met: each placeholder's needs (a system
 *  counted once per pack) and each live system's missing roles. */
export function missingPacksOf(views: readonly SystemView[]): MissingPack[] {
  const groups = new Map<string, { first: PackNeed; systems: Set<number>; roles: number }>();
  const add = (need: PackNeed, system: number, role: boolean): void => {
    const id = groupOf(need);
    let g = groups.get(id);
    if (!g) {
      g = { first: need, systems: new Set(), roles: 0 };
      groups.set(id, g);
    }
    if (role) g.roles++;
    else g.systems.add(system);
  };
  for (const v of views) {
    for (const need of v.placeholder?.needs ?? []) add(need, v.id, false);
    for (const need of v.missingRoles ?? []) add(need, v.id, true);
  }
  return [...groups].map(([id, g]) => {
    const n = g.first;
    const version = n.pack?.version;
    return {
      id,
      name: n.pack?.name ?? n.key,
      ...(version !== undefined ? { version } : {}),
      status: n.status,
      ...(n.reason !== undefined ? { reason: n.reason } : {}),
      ...(n.needsUpdate ? { needsUpdate: true } : {}),
      systems: g.systems.size,
      roles: g.roles,
    };
  });
}

/** A need as a placeholder's card says it: "Needs the Mesen pack", "Mesen 1.2 needs updating for this RetroPlug". */
export function needText(need: PackNeed): string {
  const name = need.pack?.name;
  if (need.status === "refused") {
    const who = name ? `${name}${need.pack?.version ? ` ${need.pack.version}` : ""}` : need.key;
    return need.needsUpdate ? `${who} needs updating for this RetroPlug` : `${who} is refused`;
  }
  return name ? `Needs the ${name} pack` : `Needs a pack for the ${need.what} ${need.key}`;
}

/** A missing role as a live tile's menu says it: "BlipToaster pack missing". */
export function roleNeedText(need: PackNeed): string {
  if (need.status === "refused") return needText(need);
  return `${need.pack?.name ?? need.key} pack missing`;
}

/** One row of the load's notice: the pack and what it covers. */
export function missingPackText(p: MissingPack): string {
  const covers = [
    ...(p.systems ? [`${p.systems} system${p.systems === 1 ? "" : "s"}`] : []),
    ...(p.roles ? [`${p.roles} role${p.roles === 1 ? "" : "s"}`] : []),
  ].join(", ");
  const state = p.status === "refused" ? (p.needsUpdate ? " (needs updating)" : " (refused)") : "";
  return `${p.name}${state}: ${covers}`;
}
```

  `SystemView.missingRoles` is added in Task 6; until then `missingPacksOf` reads it as absent. Add
  `missingRoles?: readonly PackNeed[];` to `SystemView` now (Step 6) so the type exists, documented as "filled from
  Task 6's rule" in plain words: "the roles this system stores that the graph cannot place, with why".

- [ ] **Step 4: The parse keeps placeholders.** In `src/projectConfig.ts`:
  - Import `{ needOf, type PackNeed }` from `./packs/needs`, `{ readProvenance, type Provenance }` from
    `./provenance`, `{ embeddedRomOf }` from `./embeds`, and `rolesRegistry` from `./contributions/app` (beside
    `projectSectionEntries`).
  - After `SystemSource`, add:

```ts
/** A system the file holds that this build cannot build, because a name a pack provides is not registered
 *  (docs/downloadable-packs-m3-design.md section 1): its index in the file's `systems`, its JSON as read (migrated;
 *  beginLoad makes its paths absolute, as every system's are) and what it needs. */
export interface PlaceholderThin {
  index: number;
  raw: RawObject;
  needs: PackNeed[];
}
```

  - Add to `ProjectConfig`, after `sources`: `/** The systems kept as placeholders, in file order; absent when there are
    none. */ placeholders?: PlaceholderThin[];`. `ProjectFileConfig` becomes
    `extends Omit<ProjectConfig, "settings" | "sources" | "placeholders">`.
  - After `systemThinSchema`, add:

```ts
/** The role kinds `raw` stores that no package registers, but the core's own config role (the console's pack provides
 *  it), each once, by the pack the file names for it. */
function roleNeeds(raw: RawObject, provenance: Provenance | undefined): PackNeed[] {
  const roles = Array.isArray(raw.roles) ? (raw.roles as unknown[]) : [];
  const registered = new Set(rolesRegistry.items().map((r) => r.kind));
  const seen = new Set<string>();
  const out: PackNeed[] = [];
  for (const r of roles) {
    const kind = r && typeof r === "object" ? (r as RawObject).kind : undefined;
    if (typeof kind !== "string" || kind === raw.core || registered.has(kind) || seen.has(kind)) continue;
    seen.add(kind);
    out.push(needOf("role", kind, provenance?.roles?.[kind]));
  }
  return out;
}

/** One system of the file as this build reads it: parsed, a placeholder's needs (it fails only on a platform or core no
 *  console registers, or names an embedded ROM no package declares), or null for a malformed entry, which is dropped. */
export function classifySystem(raw: unknown): { system: SystemThin } | { needs: PackNeed[] } | null {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  const obj = raw as RawObject;
  const provenance = readProvenance(obj.provenance);
  const r = systemThinSchema.safeParse(obj);
  if (r.success) {
    const system = r.data as SystemThin;
    if (system.embeddedRom && !embeddedRomOf(system.embeddedRom)) {
      return { needs: [needOf("embed", system.embeddedRom, provenance?.embed), ...roleNeeds(obj, provenance)] };
    }
    return { system };
  }
  // Only the two registry refinements fail with a custom issue: anything else wrong with the entry is a malformed one.
  const issues = r.error.issues;
  const packOnly = issues.every((i) => i.code === "custom" && (i.path[0] === "platform" || i.path[0] === "core"));
  if (!packOnly) return null;
  const platformMissing = issues.some((i) => i.path[0] === "platform");
  const first = platformMissing
    ? needOf("platform", obj.platform as string, provenance?.platform)
    : needOf("core", obj.core as string, provenance?.platform);
  return { needs: [first, ...roleNeeds(obj, provenance)] };
}
```

  - In `parseConfig`, the loop from Task 1 becomes:

```ts
  const systems: SystemThin[] = [];
  const sources: SystemSource[] = [];
  const placeholders: PlaceholderThin[] = [];
  (parsed.systems as unknown[]).forEach((raw, index) => {
    const c = classifySystem(raw);
    if (!c) return;
    if ("system" in c) {
      systems.push(c.system);
      sources.push({ index, raw: raw as RawObject });
    } else {
      placeholders.push({ index, raw: raw as RawObject, needs: c.needs });
    }
  });
```

    and the return adds `...(placeholders.length ? { placeholders } : {})`. Update `parseConfig`'s doc comment: a
    system that fails only on a pack-provided name is kept as a placeholder.

- [ ] **Step 5: Paths over raw systems, and the slot merge at save.** In `src/projectConfig.ts`, replace
  `mapSystemPaths`:

```ts
// Apply `fn` to the asset paths a system carries (romPath + savPath), storing the result back: the one place the fields
// are enumerated (mirrors native visitPaths). A raw system (a placeholder's, or a source kept beside a parsed one)
// carries whatever its file had, so only a non-empty string is a path.
function mapPaths(s: { romPath?: unknown; savPath?: unknown }, fn: (path: string) => string): void {
  if (typeof s.romPath === "string" && s.romPath) s.romPath = fn(s.romPath);
  if (typeof s.savPath === "string" && s.savPath) s.savPath = fn(s.savPath);
}

function mapSystemPaths(cfg: { systems: readonly object[] }, fn: (path: string) => string): void {
  for (const s of cfg.systems) mapPaths(s as { romPath?: unknown; savPath?: unknown }, fn);
}
```

  `toAbsolute` becomes:

```ts
/** Rebase each asset path to absolute against `baseDir`, in place (load side): the parsed systems, and the raw ones a
 *  placeholder or a source keeps, so a placeholder saved elsewhere still names its files. */
export function toAbsolute(cfg: ProjectConfig, baseDir: string): void {
  const abs = (p: string) => rebaseToAbsolute(p, baseDir);
  mapSystemPaths(cfg, abs);
  for (const s of cfg.sources ?? []) mapPaths(s.raw, abs);
  for (const h of cfg.placeholders ?? []) mapPaths(h.raw, abs);
}
```

  In `src/systemsList.ts`, add:

```ts
/** One slot of a project in save order: a live system, or a held one (a placeholder) at its slot number. */
export type Slot<L, H> = { live: L } | { held: H };

/** `live` and `held` merged into slots: each held entry at its `slot`, the live ones in order around them. Never loses
 *  an entry: a held slot past the end (which no edit leaves) goes last. */
export function mergeSlots<L, H extends { readonly slot: number }>(live: readonly L[], held: readonly H[]): Slot<L, H>[] {
  const order = [...held].sort((a, b) => a.slot - b.slot);
  const out: Slot<L, H>[] = [];
  let next = 0;
  for (const h of order) {
    while (next < live.length && out.length < h.slot) out.push({ live: live[next++] });
    out.push({ held: h });
  }
  while (next < live.length) out.push({ live: live[next++] });
  return out;
}
```

  `buildConfig` gains a fourth parameter and writes the merge:

```ts
export function buildConfig(
  settings: ProjectSettings,
  systems: SystemEntry[],
  name = "",
  held: readonly { readonly slot: number; readonly raw: Readonly<Record<string, unknown>> }[] = [],
): ProjectFileConfig {
```

  Its `systems:` member becomes `mergeSlots(systems.map(thinOf), held).map((s) => ("live" in s ? s.live : (s.held.raw as
  unknown as SystemThin)))`, where `thinOf` is the existing `(e) => { const t: SystemThin = ...; return t; }` body,
  moved into a named function above `buildConfig`. The cast is the one place a placeholder's raw object stands in a
  `SystemThin[]`: comment it ("a placeholder's JSON as read, written back unchanged; every reader of a built config
  reads the members it knows").

- [ ] **Step 6: The store holds placeholders.** In `src/systemsStore.ts`:
  - Import `{ mergeSlots, type Slot }` from `./systemsList`, `{ statusOf, type PackNeed }` from `./packs/needs`.
  - Add, above `export class SystemsStore`:

```ts
/** A system the store holds but cannot build (docs/downloadable-packs-m3-design.md section 1): no core, nothing in the
 *  graph. `slot` is its place among every slot (live systems and placeholders together), in the order a save writes. */
export interface PlaceholderEntry {
  readonly id: number;
  slot: number;
  /** Its JSON as read (paths absolute), written back unchanged. */
  readonly raw: Readonly<Record<string, unknown>>;
  /** Its zip entries by the name after `systems/<i>/` ("sram", "state", "song"), untouched. */
  readonly blobs: ReadonlyMap<string, Uint8Array>;
  needs: readonly PackNeed[];
}
```

  - Add to `SystemView`:

```ts
  /** Set for a placeholder (SystemsStore.slotView): what it needs. Its other fields are read from its JSON, and it
   *  runs nothing (no roles, nothing placed, no battery). */
  placeholder?: { readonly needs: readonly PackNeed[] };
  /** The roles this system stores that the graph cannot place, with why (absent when there are none). */
  missingRoles?: readonly PackNeed[];
```

  - Add the field `private held: PlaceholderEntry[] = [];` beside `entries`.
  - Add the methods, after `systems()`:

```ts
  /** The placeholders, in slot order. */
  placeholders(): readonly PlaceholderEntry[] {
    return [...this.held].sort((a, b) => a.slot - b.slot);
  }

  /** Every slot in save order: the live systems and the placeholders (what buildConfig and buildArchive write). */
  saveOrder(): Slot<SystemEntry, PlaceholderEntry>[] {
    return mergeSlots(this.entries, this.held);
  }

  /** The UI's systems: every slot in save order, a placeholder's view carrying `placeholder`
   *  (docs/downloadable-packs-m3-design.md section 8). `view()` stays the live systems, which is what runs. */
  slotView(): SystemView[] {
    const live = this.view();
    return mergeSlots(live, this.held).map((s) => ("live" in s ? s.live : this.placeholderView(s.held)));
  }

  /** Hold `raw` as a placeholder in the next slot, with its entries and needs, and answer its id. Quiet, as adopt is (a
   *  project load holds them in file order). It takes the focus only when nothing has it. */
  addPlaceholder(raw: Readonly<Record<string, unknown>>, blobs: ReadonlyMap<string, Uint8Array>, needs: readonly PackNeed[]): number {
    const id = allocSystemId();
    this.held.push({ id, slot: this.entries.length + this.held.length, raw, blobs, needs });
    if (this.focusedId === 0) this.focusedId = id;
    return id;
  }

  private placeholderView(h: PlaceholderEntry): SystemView {
    const str = (v: unknown): string => (typeof v === "string" ? v : "");
    const embeddedRom = str(h.raw.embeddedRom);
    return {
      id: h.id,
      platform: str(h.raw.platform),
      core: str(h.raw.core),
      romPath: str(h.raw.romPath),
      savPath: str(h.raw.savPath),
      savSuffix: typeof h.raw.savSuffix === "number" ? h.raw.savSuffix : 0,
      embedded: embeddedRom !== "",
      embeddedRom,
      battery: false,
      focused: h.id === this.focusedId,
      missing: false,
      settings: DEFAULT_COMMON_SETTINGS,
      roles: [],
      placed: [],
      placeholder: { needs: h.needs.map((need) => statusOf(need)) },
    };
  }

  /** The slot `id` holds (live or placeholder), or -1. */
  private slotOf(id: number): number {
    return this.saveOrder().findIndex((s) => ("live" in s ? s.live.id : s.held.id) === id);
  }

  /** A slot was removed: every placeholder after it moves up one. */
  private closeSlot(slot: number): void {
    for (const h of this.held) if (h.slot > slot) h.slot--;
  }

  /** Remove placeholder `id`: its slot closes and its entries go with it. An ordinary edit: dirty, and notified. */
  private removePlaceholder(id: number): boolean {
    const at = this.held.findIndex((h) => h.id === id);
    if (at < 0) return false;
    const [h] = this.held.splice(at, 1);
    this.closeSlot(h.slot);
    if (this.focusedId === id) this.focusedId = this.entries[0]?.id ?? this.placeholders()[0]?.id ?? 0;
    this.markDirty();
    return true;
  }
```

  - `removeSystem(id)`: first line `if (this.held.some((h) => h.id === id)) return this.removePlaceholder(id);`; record
    `const slot = this.slotOf(id);` before the `commitEdit`; after it succeeds, `this.closeSlot(slot);` before
    `this.markDirty()`.
  - `discard(id)`: record the slot before `removeById` and call `this.closeSlot(slot)` after it.
  - `clear()`: `this.held = [];` beside `this.entries = [];`.
  - `setFocus(id)`: accept a placeholder:
    `if (!this.entries.some((e) => e.id === id) && !this.held.some((h) => h.id === id)) return false;`.
  - `focusNext(dir)`: walk the slot order: `const ids = this.saveOrder().map((s) => ("live" in s ? s.live.id :
    s.held.id));` in place of `this.entries`, with `ids.length` and `ids[...]`.
  - `currentId(id)`: first line `if (this.held.some((h) => h.id === id)) return id;`.

- [ ] **Step 7: Load and save the slots.** In `src/projectStore.ts`:
  - Import `{ missingPacksOf, type MissingPack }` from `./packs/needs`.
  - `LoadOutcome`'s `loaded` member becomes
    `| { kind: "loaded"; systems: number; missingPacks?: MissingPack[] } // missingPacks: present when any is missing`.
  - In `commit`, replace the Task 1 `cfg.systems.forEach(...)` with the file-order walk:

```ts
      // In file order, so each system takes its slot: a parsed one is adopted, a placeholder held with its entries.
      const order = [
        ...cfg.systems.map((system, k) => ({ index: sourceIndexOf(cfg, k), system, held: undefined })),
        ...(cfg.placeholders ?? []).map((held) => ({ index: held.index, system: undefined, held })),
      ].sort((a, b) => a.index - b.index);
      for (const { index, system, held } of order) {
        const sram = blobs.get(sramKey(index));
        const state = blobs.get(stateKey(index));
        const songBytes = blobs.get(songKey(index));
        if (held) {
          const kept = new Map<string, Uint8Array>();
          if (state) kept.set("state", state);
          if (sram) kept.set("sram", sram);
          if (songBytes) kept.set("song", songBytes);
          this.systems.addPlaceholder(held.raw, kept, held.needs);
        } else {
          this.systems.adopt(system!, seedOf(sram, state, songBytes));
        }
      }
```

  - Its final return becomes:

```ts
    const missingPacks = this.missingPacks();
    return missingPacks.length
      ? { kind: "loaded", systems: this.systems.systems().length, missingPacks }
      : { kind: "loaded", systems: this.systems.systems().length };
```

  - Add after `isDirty()`:

```ts
  /** The packs the open project needs (docs/downloadable-packs-m3-design.md section 2.4): its placeholders' and its live
   *  systems' missing roles', each pack once with what it covers. */
  missingPacks(): MissingPack[] {
    return missingPacksOf(this.systems.slotView());
  }
```

  - `save` and `buildArchive` pass the placeholders:
    `buildConfig(this.projectSettings, this.systems.systems(), this.projectName, this.systems.placeholders())`.
  - `buildArchive`'s per-system loop becomes:

```ts
    // Index i is the slot's, as buildConfig writes the systems: a placeholder's entries go back byte for byte under its
    // slot's index, and a removed system renumbers every later one's.
    this.systems.saveOrder().forEach((slot, i) => {
      if ("held" in slot) {
        const keyOf: Record<string, (i: number) => string> = { state: stateKey, sram: sramKey, song: songKey };
        for (const [name, bytes] of slot.held.blobs) {
          const key = keyOf[name];
          if (key) entries.push({ name: key(i), bytes });
        }
        return;
      }
      const sys = slot.live;
      // ... the existing body: readState / readSram / exportableWorkingSong, keyed by i
    });
```

- [ ] **Step 8: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholders && node scripts/run-tests.mjs consoles/registry`
  Expected: PASS. If the round-trip case fails on bytes, diff `one` and `two` and fix the cause (a member written in
  another order, a path rebased twice): byte equality is the claim, never loosen it.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`, `pnpm test:ui`.
  Expected: all pass. The UI still reads `view()` (Task 9 switches it).

- [ ] **Step 9: Commit.**

```bash
git add packages/retroplug/src/packs/needs.ts packages/retroplug/src/projectConfig.ts packages/retroplug/src/systemsList.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/projectStore.ts packages/retroplug/test/project/placeholders.test.ts packages/retroplug/test/consoles/registry.test.ts
git commit -m "project: a system a missing pack provides is held as a placeholder that saves back byte for byte"
```

---

### Task 4: A placeholder holds its slot in the graph and in the DAW's parameter pool

The design keeps a placeholder's slot "so the routing and layout settings that point at a system's slot keep working"
(section 1). Task 3 keeps it in the grid and the file; this task keeps it wherever a slot is numbered, so a system after
a placeholder keeps the MIDI channel, the host channels and the automation lanes it has on a build where the
placeholder's pack is loaded, and a DAW project's automation never moves to another system while a pack is missing.

**Files:**
- Modify: `packages/retroplug/src/graph/buildGraph.ts` (`GraphModel`, `tapArmOf`, `buildGraph`)
- Modify: `packages/retroplug/src/engineGraph.ts` (`publish`)
- Modify: `packages/retroplug/src/systemsStore.ts` (`graphSlots`, the store's own `onGraphChange`)
- Modify: `packages/retroplug/src/projectStore.ts` (`publishGraph`)
- Modify: `packages/retroplug/src/pluginControlPlane.ts:107` (the parameter map)
- Test: `packages/retroplug/test/project/placeholder-slots.test.ts` (new),
  `packages/retroplug/test/graph/build-graph.test.ts`

- [ ] **Step 1: Write the failing tests.** Create `test/project/placeholder-slots.test.ts`:

```ts
// A placeholder holds its slot in the graph and in the DAW's parameter pool (docs/downloadable-packs-m3-design.md
// section 1: the routing and layout settings that point at a system's slot keep working). The same file is opened twice,
// before and after a fixture pack provides the placeholder's platform, and the system after the placeholder is compared:
// its MIDI routing position (and so its channel), its host channels and its automation lanes are the same in both.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { GraphDocument } from "@retroplug/core/graphDocument";
import { MidiRouting } from "@retroplug/core/midiRouting";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { appAdditions } from "../../src/contributions/app";
import { CC_SLOTS_PER_SYSTEM, projectParameterMap } from "../../src/parameterMap";

const MAGIC = [0x50, 0x53, 0x4c, 0x21]; // "PSL!"

const DOC = {
  schemaVersion: "4",
  settings: { midiRouting: MidiRouting.MidiChannelToInstance, audioRouting: "twoPerInstance" },
  systems: [
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
    { platform: "pslot", core: "pslot-core", romPath: "/roms/song.pslot", roles: [{ kind: "pslot-core", config: {} }] },
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
  ],
};

/** Open DOC in a fresh store on eight host channels, and read what slot 2's system has. */
function open() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/song.pslot", new Uint8Array([...MAGIC, 0, 0, 0, 0]));
  be.seed("/proj/slots.rplg", JSON.stringify(DOC));
  const registry = buildAppRegistry();
  const p = new ProjectStore(be, new RecentStore(be), registry);
  expect(p.graph.setShape({ kind: "multi", channels: 8 })).toBe(true);
  expect(p.load("/proj/slots.rplg").kind).toBe("loaded");
  const slots = p.systems.slotView();
  const last = slots[2];
  const request = be.graphRequests[be.graphRequests.length - 1];
  const document = JSON.parse(request.document) as GraphDocument;
  const routing = document.nodes.find((n) => n.id === "routing")!.settings as { systems: number[] };
  const into = (port: string): string[] => document.wires.filter((w) => w.to === port).map((w) => w.from);
  return {
    held: slots.map((v) => v.placeholder !== undefined),
    nativeSlots: request.slots.length,
    routingIndex: routing.systems.indexOf(last.id),
    routingCount: routing.systems.length,
    // twoPerInstance on eight channels: slot 2's left and right on host channels 5 and 6.
    left: into("out.out.5").includes(`split-${last.id}.out1`),
    right: into("out.out.6").includes(`split-${last.id}.out2`),
    lanes: projectParameterMap(slots, p.settings().midiRouting, registry)
      .filter((s) => s.slot >= 2 * CC_SLOTS_PER_SYSTEM)
      .map((s) => [s.slot, s.channel]),
    placeholderLanes: projectParameterMap(slots, p.settings().midiRouting, registry).filter(
      (s) => s.slot >= CC_SLOTS_PER_SYSTEM && s.slot < 2 * CC_SLOTS_PER_SYSTEM,
    ).length,
  };
}

test("a system after a placeholder keeps its MIDI position, its host channels and its automation lanes", () => {
  const missing = open();
  expect(missing.held).toEqual([false, true, false]);
  expect(missing.nativeSlots).toBe(2); // the engine's system table names only what runs
  expect(missing.placeholderLanes).toBe(0); // the empty slot claims no lane, and keeps its pool range
  commitContributions([
    {
      registry: consolesRegistry,
      pkg: "pslot-pack",
      items: [{
        id: "pslot-console",
        core: "pslot-core",
        nodeType: "rp.pslot.system",
        platforms: [{ id: "pslot", label: "PSLOT", extensions: [".pslot"] }],
        detect: { prefixBytes: 4, rank: 950, run: (b: Uint8Array) => (MAGIC.every((m, i) => b[i] === m) ? "pslot" : null) },
      } as unknown as ConsoleDescriptor],
    },
    ...appAdditions("pslot-pack", {
      roles: [{ kind: "pslot-core", category: "system", schema: { parse: (raw: unknown) => ({ ...(raw as object) }) } } as unknown as RoleType],
    }),
  ]);
  const loaded = open();
  expect(loaded.held).toEqual([false, false, false]);
  for (const build of [missing, loaded]) {
    expect(build.routingIndex).toBe(2);
    expect(build.routingCount).toBe(3);
    expect(build.left && build.right).toBe(true);
    expect(build.lanes.length > 0).toBe(true);
    expect(build.lanes.every(([slot, channel]) => slot < 3 * CC_SLOTS_PER_SYSTEM && channel === 2)).toBe(true);
  }
  expect(missing.lanes).toEqual(loaded.lanes);
});
```

  Append to `test/graph/build-graph.test.ts`:

```ts
test("an empty slot keeps its number: the routing's positions, the per-system buses and the lanes count it", () => {
  // docs/downloadable-packs-m3-plan.md, Task 4: slot 1 (id 7) is a placeholder's, which places nothing.
  const model = {
    systems: [gb(5), gb(9)],
    routing: "twoPerInstance" as AudioRouting,
    shape: multi(8),
    midiRouting: "midiChannelToInstance" as MidiRouting,
    slots: [5, 7, 9],
  };
  const { document, slots } = buildGraph(model);
  expect(slots).toEqual([5, 9]);
  expect(document.nodes.some((n) => n.id === "sys-7")).toBe(false);
  const routing = document.nodes.find((n) => n.id === "routing")!.settings as { systems: number[] };
  expect(routing.systems).toEqual([5, 7, 9]);
  expect(document.wires.some((w) => w.from === "split-9.out1" && w.to === "out.out.5")).toBe(true);
  const perSystem = buildGraph({ ...model, shape: { kind: "perSystem" } }).document;
  expect(perSystem.wires.some((w) => w.from === "sys-9.mix" && w.to === "out.out.5-6")).toBe(true);
  expect(perSystem.nodes.find((n) => n.id === "out")!.settings).toEqual({ buses: [2, 2, 2] });
});

test("a split and the NES tap need one slot, not one live system; a slot list missing a live id is ignored", () => {
  const two = buildGraph({ systems: [nes(3)], routing: "channelSplit", shape: multi(8), slots: [3, 4] });
  expect(two.tapArm).toBe(null);
  expect(two.document.nodes.some((n) => n.id.startsWith("stem-"))).toBe(false);
  expect(two.document.wires.some((w) => w.from === "split-3.out1")).toBe(true);
  const wrong = buildGraph({ systems: [gb(5)], routing: "stereo", shape: multi(2), midiRouting: "sendToAll" as MidiRouting, slots: [6] });
  expect((wrong.document.nodes.find((n) => n.id === "routing")!.settings as { systems: number[] }).systems).toEqual([5]);
});
```

  Import `buildGraph` there if the file reaches it only through `built` (it imports it from `../../src/graph/buildGraph`
  beside `GraphSystem`; check the import list at line 11). `MidiRouting` is imported as a type there; the casts above
  need only the type.

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholder-slots && node scripts/run-tests.mjs graph/build-graph`
  Expected: FAIL: the placeholder case's `routingIndex` is 1 while the placeholder is held, `left` is false (the system
  sits on channels 3 and 4) and its lanes start at pool slot 160 on channel 1; the build-graph cases fail on the
  routing's `systems` and the buses.

- [ ] **Step 3: Numbering by slot in `buildGraph`.** In `src/graph/buildGraph.ts`, add to `GraphModel`:

```ts
  /** Every slot's system id in slot order, an empty slot's (a placeholder's, docs/downloadable-packs-m3-design.md
   *  section 1) included: what the MIDI routing's positions, the per-system buses, the multi-out lanes and the
   *  one-system tests count, so a system keeps its numbers while a slot before it is empty. An empty slot places
   *  nothing. Absent, or missing a system's id: the systems' own ids, today's numbering. */
  slots?: readonly number[];
```

  `tapArmOf(model)` becomes `tapArmOf(model, slotCount: number)`, its first line
  `const only = slotCount === 1 && model.systems.length === 1 ? model.systems[0] : undefined;`. In `buildGraph`, right
  after `const { systems, routing, shape } = model;`:

```ts
  // Every slot's id, an empty one's included; a live system's number is its index here (slotOf).
  const given = model.slots;
  const slotIds: readonly number[] =
    given && systems.every((s) => given.includes(s.id)) ? given : systems.map((s) => s.id);
  const slotOf = (s: GraphSystem): number => slotIds.indexOf(s.id);
  // The one system of a one-slot project: what a split, the NES tap and the per-channel shape need.
  const lone = slotIds.length === 1 && systems.length === 1 ? systems[0] : undefined;
```

  and `const tapArm = tapArmOf(model, slotIds.length);`. Then:
  - the routing node: `settings: { mode: midiRouting, systems: [...slotIds] }`, its comment adding "an empty slot keeps
    its position, its output unread";
  - `perSystem`:

```ts
    // Slot i's mix on channels 2i and 2i + 1; an empty slot's bus stays, silent.
    slotIds.forEach((id, i) => {
      const s = systems.find((x) => x.id === id);
      if (s) wire(`${systemNodeId(s.id)}.mix`, outPort(2 * i, 2));
      buses.push(2);
    });
```

  - `perChannel`: `if (lone) { const s = lone; ... }` in place of
    `if (systems.length === 1) { const s = systems[0]; ...`;
  - `splitValid`: `isSplit(routing) && lone !== undefined && !(isPins(routing) && splitOf(lone.type, "pins") ===
    undefined)`, and `streamsOf(lone.type, nesMode(tapArm, lone))` and `const s = lone;` in the split branch;
  - the multi-out fallback: `addTo(slotOf(s) % n, lo)` / `addTo(slotOf(s) % n, ro)` for `onePerInstance`, and
    `const p = (2 * slotOf(s)) % n;` for `twoPerInstance` (the loop still walks `order`, so a sum keeps its order).
  - `slots: systems.map((s) => s.id)` in the return stays: the engine's table names the document's system nodes.
  Update the header comment's first lines: the systems in slot order, an empty slot numbered and unplaced.

- [ ] **Step 4: The model carries the slots.** In `src/engineGraph.ts`'s `publish`, the copied model gains
  `...(model.slots ? { slots: model.slots.slice() } : {}),` (and the `model` field's initial value stays as it is; a
  `withShape` rebuild and `systemsMovedLive` keep the last published slots). In `src/systemsStore.ts`, add after
  `saveOrder()`:

```ts
  /** Every slot's id in slot order, a placeholder's included: what the graph numbers its routing positions, buses and
   *  lanes by (GraphModel.slots), so a placeholder holds its slot in the graph too. */
  graphSlots(): number[] {
    return this.saveOrder().map((s) => ("live" in s ? s.live.id : s.held.id));
  }
```

  and the store's own `onGraphChange` default publishes `{ systems: this.graphSystems(), slots: this.graphSlots(),
  routing: "stereo" }`. In `src/projectStore.ts`'s `publishGraph`, the model gains `slots: this.systems.graphSlots(),`
  beside `systems`.

- [ ] **Step 5: The parameter map over every slot.** In `src/pluginControlPlane.ts`, `__rp_parameterMapJson` projects
  `project.systems.slotView()` in place of `view()`, with a comment: a placeholder has no roles, so it claims no lane,
  and a system after it keeps the lanes (and the pre-routing channel) it has where the pack is loaded, so a DAW's
  automation stays on its system. Its `systemCount` is then the slot count, which is the count the routing node fans
  out to.

- [ ] **Step 6: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholder-slots && node scripts/run-tests.mjs graph/build-graph`
  Expected: PASS.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`,
  and the engine-graph binary, where buildGraph's goldens are planned:
  `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && node packages/retroplug/scripts/run-plugin-tests.mjs engine-graph`
  (they build no `slots`, so they are unchanged).
  Expected: all pass, the goldens byte for byte.

- [ ] **Step 7: Commit.**

```bash
git add packages/retroplug/src/graph/buildGraph.ts packages/retroplug/src/engineGraph.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/projectStore.ts packages/retroplug/src/pluginControlPlane.ts packages/retroplug/test/project/placeholder-slots.test.ts packages/retroplug/test/graph/build-graph.test.ts
git commit -m "graph: a placeholder holds its slot, so a later system keeps its MIDI channel, buses and automation lanes"
```

---

### Task 5: A placeholder when no detector claims the ROM or its platform has no default core

**Files:**
- Modify: `packages/retroplug/src/systemsStore.ts` (`adoptOrHold`, `adoptNeeds`)
- Modify: `packages/retroplug/src/projectStore.ts` (`commit`)
- Modify: `packages/retroplug/src/projectMissing.ts` (`relinkInConfig` updates the raw)
- Test: `packages/retroplug/test/project/placeholders.test.ts`

- [ ] **Step 1: Write the failing tests.** Append to `test/project/placeholders.test.ts` (import `garbage` from
  `../systems/fixtures` too):

```ts
test("a ROM no detector claims makes a placeholder at the adopt, and a ROM that is gone is still the relink's", () => {
  const { be, project } = newProject();
  be.seed("/roms/strange.fxrom", garbage()); // readable, and no console's detector claims it
  const strange = { platform: "gb", core: "sameboy", romPath: "/roms/strange.fxrom", provenance: { platform: FX } };
  be.seed("/proj/strange.rplg", JSON.stringify({ schemaVersion: "4", settings: {}, systems: [gb("/roms/a.gb"), strange] }));
  const outcome = project.load("/proj/strange.rplg");
  expect(outcome.kind).toBe("loaded");
  const slots = project.systems.slotView();
  expect(slots.length).toBe(2);
  expect(slots[1].placeholder?.needs).toEqual([{ what: "platform", key: "gb", pack: FX, status: "missing" }]);
  expect(project.save("/proj/strange-saved.rplg")).toBe(true);
  expect((JSON.parse(be.readText("/proj/strange-saved.rplg")!) as { systems: unknown[] }).systems[1]).toEqual(strange);

  const gone = { platform: "gb", core: "sameboy", romPath: "/roms/gone.gb" };
  be.seed("/proj/gone.rplg", JSON.stringify({ schemaVersion: "4", settings: {}, systems: [gone] }));
  expect(project.load("/proj/gone.rplg").kind).toBe("missing");
});

test("a relinked system's kept JSON follows the new path", () => {
  const { be, project } = newProject();
  be.seed("/proj/r.rplg", JSON.stringify({ schemaVersion: "4", settings: {}, systems: [gb("/old/a.gb")] }));
  const pending = project.load("/proj/r.rplg");
  expect(pending.kind).toBe("missing");
  if (pending.kind !== "missing") return;
  expect(project.relink(pending.missing[0], "/roms/a.gb").kind).toBe("loaded");
  expect(project.systems.view()[0].romPath).toBe("/roms/a.gb");
});
```

- [ ] **Step 2: Run it and see it fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholders`
  Expected: the first case FAILS (the strange system is dropped: one slot). The relink case may already pass; it
  guards the raw update below.

- [ ] **Step 3: `adoptOrHold`.** In `src/systemsStore.ts`, import `{ needOf }` from `./packs/needs` and
  `{ readProvenance }` from `./provenance`, and add after `adopt`:

```ts
  /** A project load's adopt: a system whose ROM no console's detector claims, or whose platform has no default core
   *  (docs/downloadable-packs-m3-design.md section 1), is held as a placeholder with its JSON and entries instead of
   *  dropped; anything else is adopted (and a ROM that is gone, which the load's relink handles, is refused as before). */
  adoptOrHold(
    config: Parameters<SystemsStore["adopt"]>[0] & { platform?: string },
    raw: Readonly<Record<string, unknown>>,
    seed: Parameters<SystemsStore["adopt"]>[1],
    blobs: ReadonlyMap<string, Uint8Array>,
  ): number | null {
    const needs = this.adoptNeeds(config, raw);
    if (needs.length) return this.addPlaceholder(raw, blobs, needs);
    return this.adopt(config, seed);
  }

  /** What `config` needs before adopt can build it: a detector for its ROM, or a default core for its platform. None for
   *  an embedded ROM (the parse checked its declaration) or a ROM file that is not there. */
  private adoptNeeds(config: { platform?: string; core?: Core; romPath?: string; embeddedRom?: string }, raw: Readonly<Record<string, unknown>>): PackNeed[] {
    if (config.embeddedRom) return [];
    const romPath = config.romPath ?? "";
    if (!romPath || !this.backend.fileExists(romPath)) return [];
    const provenance = readProvenance(raw.provenance);
    const fmt = classifyRom(this.backend, romPath);
    if (fmt === "unknown") return [needOf("platform", config.platform ?? "", provenance?.platform)];
    if ((config.core ?? defaultCoreFor(fmt)) === undefined) return [needOf("core", fmt, provenance?.platform)];
    return [];
  }
```

  In `src/projectStore.ts`'s `commit`, the parsed branch keeps its blobs map too, and calls `adoptOrHold`:

```ts
        const kept = new Map<string, Uint8Array>();
        if (state) kept.set("state", state);
        if (sram) kept.set("sram", sram);
        if (songBytes) kept.set("song", songBytes);
        if (held) this.systems.addPlaceholder(held.raw, kept, held.needs);
        else this.systems.adoptOrHold(system!, rawOf(cfg, system!), seedOf(sram, state, songBytes), kept);
```

  with `rawOf` above the class:

```ts
/** The JSON `system` was parsed from (its source), or the parsed system itself for a config that kept none. */
function rawOf(cfg: ProjectConfig, system: SystemThin): Readonly<Record<string, unknown>> {
  const k = cfg.systems.indexOf(system);
  return cfg.sources?.[k]?.raw ?? (system as unknown as Readonly<Record<string, unknown>>);
}
```

  (import `type SystemThin`). The order array from Task 3 can carry `k` instead of searching: if it does, pass
  `cfg.sources?.[k]?.raw` directly and drop `rawOf`.

- [ ] **Step 4: Relink updates the kept JSON.** In `src/projectMissing.ts`'s `relinkInConfig`, after setting the
  parsed field, set the same field on `cfg.sources?.[item.systemIndex]?.raw` when it is there (`raw.romPath = newPath`
  or `raw.savPath = newPath`), so a system relinked and then held keeps the new path.

- [ ] **Step 5: Run it and see it pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/placeholders`
  Expected: PASS.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`.
  Expected: all pass. `test/systems/store-add.test.ts:73` and `store-graph.test.ts:1074` are `addSystem` and a bare
  `adopt`, which this does not change.

- [ ] **Step 6: Commit.**

```bash
git add packages/retroplug/src/systemsStore.ts packages/retroplug/src/projectStore.ts packages/retroplug/src/projectMissing.ts packages/retroplug/test/project/placeholders.test.ts
git commit -m "project: a ROM no detector claims, or a platform with no default core, is held as a placeholder"
```

---

### Task 6: Refusals reach TS, and a role the graph cannot place is left out

**Files:**
- Create: `packages/retroplug/src/packs/refusals.ts`
- Modify: `packages/retroplug/src/packs/needs.ts` (`statusOf`)
- Modify: `packages/retroplug/src/packs/loadPacks.ts` (`refuse`, `packStatus`)
- Modify: `packages/retroplug/src/backend.ts` (`SetGraphResult` at 828), `packages/retroplug/src/engineGraph.ts`
  (`send`, at 132)
- Modify: `packages/retroplug/src/systemsStore.ts` (`unplaceable`, `leaveOut`, `graphSystems`, `view`)
- Modify: `packages/retroplug/src/projectStore.ts` (`publishGraph` at 278)
- Modify: `packages/native/src/host/packs/PackCandidate.hpp`, `PackTable.cpp` (302-305, 382-384)
- Modify: `packages/native/src/host/js/PackLoaderCore.cpp` (`__rp_packRefusals`), `PackModules.hpp`
- Modify: `packages/native/src/host/rpc/BackendTypes.hpp:91`, `EngineRpcService.cpp:119`
- Test: `packages/retroplug/test/packs/refusals.test.ts` (new), `packages/retroplug/test/systems/store-graph.test.ts`;
  Catch2: `packages/native/test/plugin/PackModules.test.cpp`, `packages/native/test/packs/PackLoader.test.cpp`,
  `packages/native/test/packs/PackEngines.test.cpp`

- [ ] **Step 1: Write the failing tests.** Create `test/packs/refusals.test.ts`:

```ts
// A pack's refusal reaches what a project shows (docs/downloadable-packs-m3-design.md section 5): the native table's
// (__rp_packRefusals: API level, no library, discovery), the TS loader's, and an Engine's (each setGraph answer's
// refusedPacks), so a need whose pack is refused reads `refused` with the reason, the "needs updating" card's data.
import { test, expect } from "@retroplug/testing";
import { needOf, statusOf } from "../../src/packs/needs";
import { forgetNativeRefusals, noteEngineRefusals } from "../../src/packs/refusals";
import { packStatus } from "../../src/packs/loadPacks";
import { EngineGraph } from "../../src/engineGraph";

const g = globalThis as Record<string, unknown>;

test("a need whose pack the native table refused for its API level reads refused, needing an update", () => {
  g.__rp_packRefusals = () =>
    JSON.stringify([{ id: "refix-old", reason: "pack refix-old is built for API level 2", version: "1.2.0", needsUpdate: true }]);
  forgetNativeRefusals();
  const need = statusOf(needOf("platform", "refix-platform", { id: "refix-old", name: "Refix", version: "1.0.0" }));
  expect(need).toEqual({
    what: "platform",
    key: "refix-platform",
    pack: { id: "refix-old", name: "Refix", version: "1.2.0" },
    status: "refused",
    reason: "pack refix-old is built for API level 2",
    needsUpdate: true,
  });
  expect(packStatus().native["refix-old"]?.needsUpdate).toBe(true);
  delete g.__rp_packRefusals;
  forgetNativeRefusals();
  expect(statusOf(needOf("platform", "refix-platform", { id: "refix-old", name: "Refix" })).status).toBe("missing");
});

test("an Engine's refusal rides its setGraph answer and reaches the status", () => {
  const graph = new EngineGraph({
    setGraph: () => ({ ok: true, errors: [], refusedPacks: [{ id: "refix-engine", reason: "a clash in this engine" }] }),
  });
  graph.publish({ systems: [], routing: "stereo" });
  expect(packStatus().engine["refix-engine"]).toBe("a clash in this engine");
  expect(statusOf(needOf("role", "refix-role", { id: "refix-engine", name: "Refix Engine" }))).toEqual({
    what: "role",
    key: "refix-role",
    pack: { id: "refix-engine", name: "Refix Engine" },
    status: "refused",
    reason: "a clash in this engine",
  });
  noteEngineRefusals([]);
});
```

  `noteEngineRefusals([])` leaves earlier entries (an Engine's refusals only grow); the ids are this file's own, so
  nothing else reads them.

  Append to `test/systems/store-graph.test.ts`, after "a load's retry drops a system whose protocol node the refusal
  names" (line 483):

```ts
test("a role whose node type the engine does not have is left out, its system placed, and the role kept", () => {
  // docs/downloadable-packs-m3-design.md section 2.3: an unknown-type naming a role's node is a missing role, not a
  // discarded system. The engine here refuses any document placing an lsdj-sync node, as one whose pack it refused would.
  const { be, p, sys } = project();
  const lsdj = sys.addSystem("/roms/lsdj.gb")!;
  sys.setRoleConfig(lsdj, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
  const bytes = p.exportBytes()!;
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    const doc = JSON.parse(request.document) as GraphDocument;
    const role = doc.nodes.find((n) => /^sys-\d+\/lsdj-sync$/.test(n.id));
    if (role) {
      be.graphRequests.push(request);
      return { ok: false, errors: [{ nodeId: role.id, port: "", code: "unknown-type", message: `no node type "${role.type}" is registered` }] };
    }
    return setGraph(request);
  };
  const { result, warnings } = warned(() => p.loadBytes(bytes, "/"));
  // The export wrote the role's provenance (the lsdj package), so the need names it; its reason is the planner's.
  const why = 'no node type "rp.lsdj.sync" is registered';
  expect(result).toEqual({
    kind: "loaded",
    systems: 1,
    missingPacks: [{ id: "lsdj", name: "lsdj", status: "refused", reason: why, systems: 0, roles: 1 }],
  });
  const [view] = sys.view();
  expect(view.roles.some((r) => r.kind === "lsdj-sync")).toBe(true); // kept in the config, at its position
  expect(view.placed.includes("lsdj-sync")).toBe(false);
  expect(view.missingRoles?.map((n) => [n.key, n.status])).toEqual([["lsdj-sync", "refused"]]);
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${view.id}`)).toBe(true); // the system runs
  expect(warnings.some((w) => w.includes("loads without it"))).toBe(false);
  // An edit publishes without it too, and a save keeps it.
  expect(sys.setRoleConfig(view.id, "lsdj-sync", { mode: LsdjSyncMode.MidiMap })).toBe(true);
  be.setGraph = setGraph;
  expect(p.save("/proj/left-out.rplg")).toBe(true);
  const saved = JSON.parse(new TextDecoder().decode(be.readFile("/proj/left-out.rplg")!)) as { systems: { roles: { kind: string }[] }[] };
  expect(saved.systems[0].roles.some((r) => r.kind === "lsdj-sync")).toBe(true);
});
```

  Read `rp.lsdj.sync` off the tree (the `lsdj-sync` role's protocol node type) and adjust the expected message if it
  is named otherwise.

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs packs/refusals && node scripts/run-tests.mjs systems/store-graph`
  Expected: FAIL (no `refusals.ts`; the store discards the system, so the load answers `systems: 0`).

- [ ] **Step 3: The refusals module.** Create `src/packs/refusals.ts`:

```ts
// Where a pack was refused, as this module set hears of it (docs/downloadable-packs-m3-design.md section 5): the native
// table's refusals (__rp_packRefusals: an API level, no library for this platform, discovery, and the TS loaders'
// reports), this set's TS loader's, and this context's Engine's (every setGraph answer's refusedPacks). What a
// placeholder's card and a missing role's row read, and Settings > Packs (M4) after them. Imports nothing, so the
// loader, the graph and the needs can all read it.

/** A refusal as the native table answers it. */
export interface NativeRefusal {
  readonly id: string;
  readonly reason: string;
  /** The refused folder's version, when it is a `<id>/<version>` folder. */
  readonly version?: string;
  /** Refused for its API level. */
  readonly needsUpdate?: boolean;
}

interface RefusalHooks {
  __rp_packRefusals?: () => string;
}

let native: Map<string, NativeRefusal> | null = null;
const ts = new Map<string, string>();
const engine = new Map<string, string>();

/** The native table's refusals by pack id (the last for an id wins), read once and again after a live load. */
export function nativeRefusals(): ReadonlyMap<string, NativeRefusal> {
  if (!native) {
    native = new Map();
    const hook = (globalThis as RefusalHooks).__rp_packRefusals;
    if (hook) {
      try {
        for (const r of JSON.parse(hook()) as NativeRefusal[]) native.set(r.id, r);
      } catch (e) {
        console.warn(`[packs] the host's refusal list did not read: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
  }
  return native;
}

/** Read the native refusals again at the next ask (a live load may have added one). */
export function forgetNativeRefusals(): void {
  native = null;
}

/** The TS loader's refusal of `id` (src/packs/loadPacks.ts calls this from its refuse). */
export function noteTsRefusal(id: string, reason: string): void {
  ts.set(id, reason);
}

export function tsRefusals(): ReadonlyMap<string, string> {
  return ts;
}

/** An Engine's refusals as a setGraph answer lists them. Recorded only: a publish is under way, so nothing is told. */
export function noteEngineRefusals(list: readonly { readonly id: string; readonly reason: string }[]): void {
  for (const { id, reason } of list) engine.set(id, reason);
}

export function engineRefusals(): ReadonlyMap<string, string> {
  return engine;
}
```

- [ ] **Step 4: The loader and the status.** In `src/packs/loadPacks.ts`, import `{ engineRefusals, nativeRefusals,
  noteTsRefusal, type NativeRefusal }` from `./refusals`; in `refuse()`, after `refused.set(id, reason);`, add
  `noteTsRefusal(id, reason);`. `packStatus()` becomes:

```ts
/** What this module set loaded and refused: each pack's halves, and each refused pack's reason; the native table's
 *  refusals and this context's Engine's beside them (docs/downloadable-packs-m3-design.md section 5). */
export function packStatus(): {
  loaded: Record<string, HalfKind[]>;
  refused: Record<string, string>;
  native: Record<string, NativeRefusal>;
  engine: Record<string, string>;
} {
  return {
    loaded: Object.fromEntries([...loaded].map(([id, halves]) => [id, [...halves]])),
    refused: Object.fromEntries(refused),
    native: Object.fromEntries(nativeRefusals()),
    engine: Object.fromEntries(engineRefusals()),
  };
}
```

  Update `ui/host/nativeHooks.ts:28`'s `__rp_packStatus` type to the new shape.

- [ ] **Step 5: `statusOf` reads them.** In `src/packs/needs.ts`, import
  `{ engineRefusals, nativeRefusals, tsRefusals }` from `./refusals` and replace `statusOf`:

```ts
/** `need` with its status as the packs stand now: refused (with the reason, and the refused folder's version) when its
 *  pack was refused natively, in TS or by this context's Engine; else refused with `fallback` when the caller has a
 *  reason of its own (a role node the engine could not place); else missing. */
export function statusOf(need: PackNeed, fallback?: string): PackNeed {
  const base: PackNeed = need.pack ? { what: need.what, key: need.key, pack: need.pack, status: "missing" } : needOf(need.what, need.key);
  const id = need.pack?.id;
  const n = id ? nativeRefusals().get(id) : undefined;
  if (n) {
    const pack = n.version !== undefined ? { ...need.pack!, version: n.version } : need.pack!;
    return { ...base, pack, status: "refused", reason: n.reason, ...(n.needsUpdate ? { needsUpdate: true } : {}) };
  }
  const reason = (id ? (tsRefusals().get(id) ?? engineRefusals().get(id)) : undefined) ?? fallback;
  return reason !== undefined ? { ...base, status: "refused", reason } : base;
}
```

- [ ] **Step 6: Engine refusals on the answer.** In `src/backend.ts`, `SetGraphResult` gains
  `/** The packs this Engine refused, with why, on every answer (docs/downloadable-packs-m3-design.md section 5). Absent
  from a mock. */ refusedPacks?: { id: string; reason: string }[];`. In `src/engineGraph.ts`, import
  `{ noteEngineRefusals }` from `./packs/refusals` and, right after `result = this.backend.setGraph(request);` returns
  (before `if (!result.ok)`), add `if (result.refusedPacks) noteEngineRefusals(result.refusedPacks);`.

  Native: in `packages/native/src/host/rpc/BackendTypes.hpp`, beside `RpcGraphError`:

```cpp
// A pack this Engine refused (Engine::refusedPacks), with why: every setGraph answer lists them, so TS learns of an
// Engine's refusal in the answer to the first document that named its node (docs/downloadable-packs-m3-design.md 5).
struct RpcRefusedPack { std::string id; std::string reason; };
struct RpcSetGraphResult { bool ok; std::vector<RpcGraphError> errors; std::vector<RpcRefusedPack> refusedPacks; };
```

  In `EngineRpcService.cpp`'s `setGraph`, `RpcSetGraphResult out{result.ok, {}, {}};` and after the errors loop:

```cpp
    for (const std::string& id : engine_.refusedPacks()) out.refusedPacks.push_back({id, engine_.refusalReason(id)});
```

- [ ] **Step 7: `needsUpdate` and the native list.** In `PackCandidate.hpp`'s `PackRefusal`, add
  `bool needsUpdate = false;  // refused for its API level: the pack (or RetroPlug) needs updating`. In `PackTable.cpp`,
  add a parameter to the `refuse` member and the `refuseHere` lambda so the two API-level refusals (302-305 and
  382-384) push a refusal with `needsUpdate = true`: the simplest form is `refuseHere(id, reason, true)` with a
  defaulted `bool needsUpdate = false` threaded into `refusals_.push_back({id, candidate.dir, reason, needsUpdate})`.
  Keep every other call as it is.

  In `PackLoaderCore.cpp`, beside `packList`:

```cpp
// The table's refusals, as JSON: each pack's id, reason, its folder's version when the folder is <id>/<version>, and
// whether it was refused for its API level (docs/downloadable-packs-m3-design.md section 5).
JSValue packRefusals(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::string out = "[";
    if (packs::PackTable* table = g_table.load()) {
        bool first = true;
        for (const packs::PackRefusal& r : table->refusals()) {
            const std::string leaf = r.dir.filename().generic_string();
            const std::string version = packs::isPackVersion(leaf) ? ",\"version\":" + jsonString(leaf) : std::string();
            out += std::string(first ? "" : ",") + "{\"id\":" + jsonString(r.id) + ",\"reason\":" + jsonString(r.reason) +
                   version + ",\"needsUpdate\":" + (r.needsUpdate ? "true" : "false") + "}";
            first = false;
        }
    }
    out += "]";
    return JS_NewStringLen(ctx, out.data(), out.size());
}
```

  and in `installGlobals`, inside `if (refusals)`, define `__rp_packRefusals` as `__rp_packRefused` is defined (0
  arguments). `jsonString` escapes `"` and `\` only: if a reason can carry a control character (a path), extend
  `jsonString` to escape below 0x20 as `\u00XX`, and say so. Document the global in `PackModules.hpp`'s list.

- [ ] **Step 8: Leave out what the engine cannot place.** In `src/systemsStore.ts`:
  - Add the field:

```ts
  /** Role kinds the engine answered unknown-type for (a pack it refused), with the planner's message: left out of the
   *  graph until a commit (retryMissingRoles in the re-resolve). */
  private readonly unplaceable = new Map<string, string>();
```

  - Add the method:

```ts
  /** Leave role `kind` of system `id` out of the graph, as the engine cannot place its node (it answered unknown-type,
   *  `reason`). True when that is new (the caller then sends the document again); false for a system with no such role,
   *  or a kind already left out. */
  leaveOut(id: number, kind: string, reason: string): boolean {
    const e = findById(this.entries, id);
    if (!e || !e.roles.some((r) => r.kind === kind) || this.unplaceable.has(kind)) return false;
    this.unplaceable.set(kind, reason);
    return true;
  }

  /** `roles` without the kinds the engine cannot place. */
  private placeable(roles: readonly RoleInstance[]): RoleInstance[] {
    return this.unplaceable.size ? roles.filter((r) => !this.unplaceable.has(r.kind)) : roles.slice();
  }

  /** The roles `e` stores that the graph cannot place: a kind nothing registers, or one the engine could not place;
   *  each once, by the pack its provenance names (docs/downloadable-packs-m3-design.md section 2.3). */
  private missingRolesOf(e: SystemEntry): PackNeed[] {
    if (!this.registry) return [];
    const out: PackNeed[] = [];
    const seen = new Set<string>();
    for (const r of e.roles) {
      if (seen.has(r.kind)) continue;
      seen.add(r.kind);
      const pack = e.provenance?.roles?.[r.kind];
      if (!this.registry.roleType(r.kind)) out.push(statusOf(needOf("role", r.kind, pack)));
      else if (this.unplaceable.has(r.kind)) out.push(statusOf(needOf("role", r.kind, pack), this.unplaceable.get(r.kind)));
    }
    return out;
  }
```

  - `graphSystems()`: `pipeline: protocolPipeline(this.placeable(e.roles)),`.
  - `view()`: `placed: placedKinds(this.placeable(e.roles ?? [])),` and, last in the object,
    `...((m) => (m.length ? { missingRoles: m } : {}))(this.missingRolesOf(e)),` (or a local `const missing = ...`
    inside a block-bodied map callback).
  - `clear()`: leave `unplaceable` as it is: the engine's refusals outlive a project.

  In `src/projectStore.ts`, `publishGraph` becomes:

```ts
  private publishGraph(rearmTap: boolean): boolean {
    if (this.holdGraph) return true;
    for (;;) {
      const planned = this.graph.publish(
        {
          systems: this.systems.graphSystems(),
          slots: this.systems.graphSlots(), // a placeholder holds its slot (GraphModel.slots)
          routing: this.projectSettings.audioRouting,
          midiRouting: this.projectSettings.midiRouting,
          models: this.graphModels(),
        },
        { rearmTap },
      );
      if (planned) return true;
      // A role node whose type the engine does not have (a pack it refused, or one no build of it carries) is a
      // missing role, not a refused system (docs/downloadable-packs-m3-design.md section 2.3): it is left out and the
      // document sent again, for a load and an edit alike. Each pass leaves out at least one more kind, so this ends.
      let more = false;
      for (const error of this.graph.lastErrors) {
        const match = error.code === "unknown-type" ? /^sys-(\d+)\/([^/]+)(?:\/out)?$/.exec(error.nodeId) : null;
        if (match && this.systems.leaveOut(Number(match[1]), match[2], error.message)) more = true;
      }
      if (!more) return false;
    }
  }
```

  Keep its doc comment, adding the leave-out in one sentence.

- [ ] **Step 9: The Catch2 cases.** In `PackModules.test.cpp`, after "the pack list and the refusal report":

```cpp
TEST_CASE("the refusal list carries each refusal's reason, its folder's version and its API-level flag", "[pack-modules]") {
    PackTable table;
    table.recordRefusal({"pmold", fs::path("/packs/pmold/1.2.0"), "pack pmold is built for API level 1", true});
    table.recordRefusal({"pmplain", fs::path("/dev/pmplain"), "pack pmplain has no library", false});
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    CHECK(readString(rt, "globalThis.__rp_packRefusals()") ==
          R"([{"id":"pmold","reason":"pack pmold is built for API level 1","version":"1.2.0","needsUpdate":true},)"
          R"({"id":"pmplain","reason":"pack pmplain has no library","needsUpdate":false}])");
}
```

  In `PackLoader.test.cpp`'s API-level case, after each of the two `checkRefused` calls:
  `CHECK(table.refusals().back().needsUpdate);`, and after "a pack whose requirement is not loaded" 's `checkRefused`:
  `CHECK_FALSE(table.refusals().back().needsUpdate);`.

  In `PackEngines.test.cpp`, include `"host/engine/EngineInvoker.hpp"` and `"host/rpc/EngineRpcService.hpp"`, and after
  "an Engine that already holds a pack's type refuses the whole pack":

```cpp
TEST_CASE("every setGraph answer over the RPC layer names the packs its Engine refused, with why", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    REQUIRE(table.load(fixture("packfixture")));
    QueuedInvoker invoker(holder);
    EngineRpcService rpc(holder, invoker);
    const auto doc = toneDocument(0.5f);
    const RpcSetGraphResult answer = rpc.setGraph(RpcSetGraphRequest{doc.document, {}, std::nullopt});
    CHECK(answer.ok);
    REQUIRE(answer.refusedPacks.size() == 1);
    CHECK(answer.refusedPacks[0].id == "packfixture");
    CHECK_FALSE(answer.refusedPacks[0].reason.empty());
    CHECK(answer.refusedPacks[0].reason == holder.refusalReason("packfixture"));
}
```

  Check `Engine::GraphRequest`'s member name for the document (`toneDocument` builds one through
  `rp::host::test::graphRequest`) and that `retroplug-packs-test` links `EngineRpcService.cpp`; if it does not, add the
  case to `test/engine-graph/EngineGraph.test.cpp` instead with its own `PackTable` (that binary already uses the RPC
  service) and say so.

- [ ] **Step 10: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs packs/refusals && node scripts/run-tests.mjs systems/store-graph`
  Expected: PASS.
  Then: `cmake --build build --target retroplug-plugin-test retroplug-packs-test -j$(nproc) && node packages/retroplug/scripts/run-plugin-tests.mjs plugin-test packs-test`,
  `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`, `pnpm test:native:packs`.
  Run `pnpm test:node` too: the Node addon reads the same `RpcSetGraphResult`.
  Expected: all pass. The existing `activate-failed` drop cases (store-graph 483, store-resources 205,
  `test-native/app-construct-edit.test.ts:312`) pass unchanged: a system refused for its own reason is still dropped.

- [ ] **Step 11: Commit.**

```bash
git add packages/retroplug/src/packs/refusals.ts packages/retroplug/src/packs/needs.ts packages/retroplug/src/packs/loadPacks.ts packages/retroplug/ui/host/nativeHooks.ts packages/retroplug/src/backend.ts packages/retroplug/src/engineGraph.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/projectStore.ts packages/native/src/host/packs/PackCandidate.hpp packages/native/src/host/packs/PackTable.cpp packages/native/src/host/js/PackLoaderCore.cpp packages/native/src/host/js/PackModules.hpp packages/native/src/host/rpc/BackendTypes.hpp packages/native/src/host/rpc/EngineRpcService.cpp packages/native/test/plugin/PackModules.test.cpp packages/native/test/packs/PackLoader.test.cpp packages/native/test/packs/PackEngines.test.cpp packages/retroplug/test/packs/refusals.test.ts packages/retroplug/test/systems/store-graph.test.ts
git commit -m "packs: refusals reach TS, and a role node the engine cannot place is left out rather than its system"
```

---

### Task 7: A pack's arrival re-resolves the live project

**Files:**
- Modify: `packages/retroplug/src/packs/refusals.ts` (`subscribePackStatus`, `packStatusChanged`)
- Modify: `packages/retroplug/src/systemsList.ts` (`SystemEntry.builtWithout`)
- Modify: `packages/retroplug/src/systemsStore.ts` (`adoptAs`, `adoptInto`, `setNeeds`, `retryMissingRoles`)
- Modify: `packages/retroplug/src/projectStore.ts` (`followPacks`, the scheduler, `reresolve`)
- Modify: `packages/retroplug/src/appStores.ts` (`ComposeOptions.live`), `packages/retroplug/src/pluginControlPlane.ts`,
  `packages/retroplug/ui/stores/StoreProvider.tsx` (line 42), `packages/retroplug/src/bootSession.ts`
- Test: `packages/retroplug/test/project/reresolve.test.ts` (new)

- [ ] **Step 1: Write the failing tests.** Create `test/project/reresolve.test.ts`:

```ts
// The live project re-resolves when a pack arrives (docs/downloadable-packs-m3-design.md section 6): once the load that
// brought the pack has committed and marked its halves loaded, each placeholder the pack now provides is swapped into the
// same id and slot with its ROM and entries, the project stays as clean as it was, and one that still fails has its
// needs read again. Only the store its host registered (followPacks) re-resolves. Every console, role and id here is
// this file's own, and each commit is permanent in this file's registries, so each case commits its own.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { RoleType } from "@retroplug/core/systemRoles";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { appAdditions } from "../../src/contributions/app";
import { APP_BUILDERS, loadPacks, packStatus } from "../../src/packs/loadPacks";
import type { PackHost, PackInfo } from "../../src/packs/packHost";
import { gbRomBattery } from "../systems/fixtures";

const enc = new TextEncoder();

/** Let the re-resolve a commit scheduled run: it is a microtask queued at the commit, so it runs before this. */
const settled = (): Promise<void> => Promise.resolve().then(() => {});

function consoleOf(platform: string, magic: number[]): ConsoleDescriptor {
  return {
    id: `${platform}-console`,
    core: `${platform}-core`,
    nodeType: `rp.${platform}.system`,
    platforms: [{ id: platform, label: platform.toUpperCase(), extensions: [`.${platform}`] }],
    detect: { prefixBytes: 4, rank: 950, run: (b: Uint8Array) => (magic.every((m, i) => b[i] === m) ? platform : null) },
  } as unknown as ConsoleDescriptor;
}

function role(kind: string, category: "system" | "feature"): RoleType {
  return { kind, category, schema: { parse: (raw: unknown) => ({ ...(raw as object) }) } } as unknown as RoleType;
}

/** Commit a console for `platform` and its core's system role, as a pack's consoles and app halves do. */
function arrive(platform: string, magic: number[]): void {
  commitContributions([
    { registry: consolesRegistry, pkg: `${platform}-pack`, items: [consoleOf(platform, magic)] },
    ...appAdditions(`${platform}-pack`, { roles: [role(`${platform}-core`, "system")] }),
  ]);
}

function heldSystem(platform: string, extraRoles: { kind: string; config: object }[] = []) {
  return {
    platform,
    core: `${platform}-core`,
    romPath: `/roms/song.${platform}`,
    roles: [{ kind: `${platform}-core`, config: { level: 2 } }, ...extraRoles],
    provenance: { platform: { id: `${platform}-pack`, name: `${platform} pack`, version: "1.0.0" } },
  };
}

/** A project holding a placeholder for `platform` (slot 0, with a savestate entry) and a live Game Boy (slot 1). */
function loaded(platform: string, magic: number[], opts: { follow?: boolean; extraRoles?: { kind: string; config: object }[] } = {}) {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  be.seed(`/roms/song.${platform}`, new Uint8Array([...magic, 0, 0, 0, 0]));
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  if (opts.follow !== false) project.followPacks();
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [heldSystem(platform, opts.extraRoles), { platform: "gb", core: "sameboy", romPath: "/roms/a.gb" }],
  };
  const zip = be.zip([
    { name: "project.json", bytes: enc.encode(JSON.stringify(doc)) },
    { name: "systems/0/state", bytes: new Uint8Array([6, 5, 4]) },
  ])!;
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  return { be, project };
}

test("a commit that provides a placeholder's platform swaps it in at its id and slot, with its state, and the project stays clean", async () => {
  const { be, project } = loaded("rrone", [0x52, 0x52, 0x31, 0x21]);
  const [held, gb] = project.systems.slotView();
  expect(held.placeholder !== undefined).toBe(true);
  expect(project.isDirty()).toBe(false);
  arrive("rrone", [0x52, 0x52, 0x31, 0x21]);
  // Scheduled, not run inside the commit: the loader that committed has not finished its pass yet.
  expect(project.systems.slotView()[0].placeholder !== undefined).toBe(true);
  await settled();
  const [now, still] = project.systems.slotView();
  expect(now.placeholder).toBe(undefined);
  expect(now.id).toBe(held.id);
  expect(now.platform).toBe("rrone");
  expect(now.romPath).toBe("/roms/song.rrone");
  expect(still.id).toBe(gb.id);
  expect(be.bootOf(now.id)?.stateBytes).toEqual(new Uint8Array([6, 5, 4]));
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${now.id}`)).toBe(true);
  expect(project.isDirty()).toBe(false);
  expect(project.missingPacks()).toEqual([]);
});

test("a commit that does not provide it leaves the placeholder, and its needs are read again", async () => {
  const { project } = loaded("rrtwo", [0x52, 0x52, 0x32, 0x21]);
  arrive("rrother", [0x52, 0x52, 0x39, 0x21]);
  await settled();
  const [held] = project.systems.slotView();
  expect(held.placeholder?.needs.map((n) => n.key)).toEqual(["rrtwo"]);
  expect(project.isDirty()).toBe(false);
});

test("only the registered store re-resolves; one nobody registered keeps its placeholder until it loads again", async () => {
  const unregistered = loaded("rrthree", [0x52, 0x52, 0x33, 0x21], { follow: false });
  const registered = loaded("rrfour", [0x52, 0x52, 0x34, 0x21]);
  arrive("rrthree", [0x52, 0x52, 0x33, 0x21]);
  await settled();
  expect(unregistered.project.systems.slotView()[0].placeholder !== undefined).toBe(true);
  arrive("rrfour", [0x52, 0x52, 0x34, 0x21]);
  await settled();
  expect(registered.project.systems.slotView()[0].placeholder).toBe(undefined);
});

test("one load whose app half commits roles and whose consoles half commits a platform re-resolves once, after both", async () => {
  // The real ordering: loadPacks commits the pack's consoles and app halves in one commit, marks both halves loaded and
  // accepts its kind items, and only then does the live project re-resolve, once, seeing the platform and the roles.
  const magic = [0x52, 0x52, 0x35, 0x21];
  const { project } = loaded("rrorder", magic, { extraRoles: [{ kind: "rrorder-marker", config: {} }] });
  const seen: string[][] = [];
  const reresolve = project.reresolve.bind(project);
  project.reresolve = () => {
    seen.push(packStatus().loaded["rrorder-pack"] ?? []);
    return reresolve();
  };
  const pack: PackInfo = {
    id: "rrorder-pack",
    version: "1.0.0",
    halves: ["app", "consoles"],
    provides: { platforms: ["rrorder"], nodeTypes: ["rp.rrorder.system"], roles: ["rrorder-core", "rrorder-marker"] },
    requires: { packs: [], platforms: [] },
  };
  const modules: Record<string, unknown> = {
    app: { roles: [role("rrorder-core", "system"), role("rrorder-marker", "feature")] },
    consoles: { consoles: [consoleOf("rrorder", magic)] },
  };
  const host: PackHost = {
    list: () => [pack],
    importHalf: async (_id, half) => ({ default: modules[half] }),
    refuse: (id, reason) => {
      throw new Error(`${id} was refused: ${reason}`);
    },
  };
  await loadPacks(host, APP_BUILDERS);
  expect(seen).toEqual([["app", "consoles"]]);
  const [now] = project.systems.slotView();
  expect(now.placeholder).toBe(undefined);
  expect(now.roles.map((r) => r.kind)).toEqual(["rrorder-core", "rrorder-marker"]);
  expect(now.missingRoles).toBe(undefined);
  expect(project.isDirty()).toBe(false);
});
```

  `test/contributions/sections-live.test.ts` changes nothing: its stores are composed without `live`, so they adopt
  lazily as before. Check that it passes unchanged in Step 6.

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/reresolve`
  Expected: FAIL (`followPacks` does not exist).

- [ ] **Step 3: The status signal.** Append to `src/packs/refusals.ts`:

```ts
const listeners = new Set<() => void>();

/** Call `listener` when the packs' status moves outside a registry commit (a live load answered: a pack loaded or
 *  refused natively). Returns the unsubscribe. */
export function subscribePackStatus(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** The status moved: the native refusals are read again, and every listener told (a listener's throw is reported). */
export function packStatusChanged(): void {
  forgetNativeRefusals();
  for (const listener of [...listeners]) {
    try {
      listener();
    } catch (e) {
      console.warn(`[packs] a status listener threw: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
}
```

- [ ] **Step 4: The swap and the missing roles in the store.** In `src/systemsList.ts`, add to `SystemEntry`:
  `/** The role kinds it stores that had no registered type when it was built (their construct hooks did not run): a
  commit that registers one with a hook rebuilds it (SystemsStore.retryMissingRoles). Not serialized. */
  builtWithout?: readonly string[];`.

  In `src/systemsStore.ts`:
  - Rename `adopt`'s body into `private adoptAs(id: number, config: ..., blobs?: ..., at?: number): number | null`
    (the same parameters as `adopt`, plus the id and a live index); delete its `const id = allocSystemId();` line; in
    its `commitEdit("adopt", ...)` use `entries: at === undefined ? appendEntry(this.entries, prepared.entry) :
    [...this.entries.slice(0, at), prepared.entry, ...this.entries.slice(at)],`; and add to the entry it boots
    `...(missing.length ? { builtWithout: missing } : {}),` with `missing` computed after `roles`:

```ts
    const registry = this.registry;
    const missing = registry ? [...new Set(roles.filter((r) => !registry.roleType(r.kind)).map((r) => r.kind))] : [];
```

  - `adopt(config, blobs)` becomes `return this.adoptAs(allocSystemId(), config, blobs);`, its doc comment kept.
  - Add:

```ts
  /** Build placeholder `id` through the load path (docs/downloadable-packs-m3-design.md section 6.1): under the same id,
   *  in the same slot, seeded from its entries. Null when it is still not buildable (its needs are updated: a detector
   *  or a default core still missing) or the build failed (it stays a placeholder). Quiet, as adopt is. */
  adoptInto(
    id: number,
    config: Parameters<SystemsStore["adopt"]>[0] & { platform?: string },
    seed: Parameters<SystemsStore["adopt"]>[1],
  ): number | null {
    const held = this.held.find((h) => h.id === id);
    if (!held) return null;
    const needs = this.adoptNeeds(config, held.raw);
    if (needs.length) {
      held.needs = needs;
      return null;
    }
    const liveIndex = held.slot - this.held.filter((h) => h.slot < held.slot).length;
    const built = this.adoptAs(id, config, seed, liveIndex);
    if (built === null) return null;
    this.held.splice(this.held.indexOf(held), 1);
    return built;
  }

  /** Replace placeholder `id`'s needs (a commit read them again). */
  setNeeds(id: number, needs: readonly PackNeed[]): void {
    const held = this.held.find((h) => h.id === id);
    if (held) held.needs = needs;
  }

  /** After a commit (docs/downloadable-packs-m3-design.md section 6.2): forget the role kinds the engine could not place,
   *  so the next publish tries them (a pack loaded since may register their node types; one still refused is left out
   *  again), and rebuild in place each live system built while a role of it had no registered type that has a construct
   *  hook now, its battery and working song carried as a reload carries them. Leaves the store as dirty as it was. True
   *  when anything moved. */
  retryMissingRoles(): boolean {
    const wasDirty = this.dirty;
    let moved = this.unplaceable.size > 0;
    this.unplaceable.clear();
    for (const e of this.entries.slice()) {
      const now = (e.builtWithout ?? []).filter((kind) => this.registry?.roleType(kind));
      if (!now.length) continue;
      moved = true;
      const rest = e.builtWithout!.filter((kind) => !now.includes(kind));
      const hooked = now.some((kind) => this.registry!.roleType(kind)!.onConstruct);
      let id: number | null = e.id;
      if (hooked) {
        this.flushBeforeDrop(e);
        const song = this.takeWorkingSong(e);
        const sramBytes = this.backend.readSram(e.id) ?? undefined;
        id = this.carryWorkingSong(this.rebuildInPlace(e.id, { sramBytes }), song);
      }
      const built = id === null ? undefined : findById(this.entries, id);
      if (built) {
        const entry: SystemEntry = { ...built };
        if (rest.length) entry.builtWithout = rest;
        else delete entry.builtWithout;
        this.entries = replaceById(this.entries, built.id, entry);
      }
    }
    this.dirty = wasDirty;
    return moved;
  }
```

  Check `takeWorkingSong` and `carryWorkingSong`'s signatures against `reloadSystem` (line 492), which this mirrors.

- [ ] **Step 5: The registration, the scheduler and the re-resolve.** In `src/projectStore.ts`:
  - Import `{ subscribeContributions }` from `./contributions/registry`, `{ subscribePackStatus }` from
    `./packs/refusals`, `{ classifySystem }` from `./projectConfig`.
  - Above `export class ProjectStore`:

```ts
/** The store a pack's arrival re-resolves: the one its host registered with followPacks (the plugin's control plane, the
 *  UI's own stores where no control plane shares its, a CLI session). One per module set; a store nobody registered
 *  adopts what arrived lazily, on its own reads, as every store always has. */
let liveStore: ProjectStore | null = null;
let following = false;
let scheduled = false;

/** One re-resolve of the live store, in a microtask: it runs once the code that committed has finished its synchronous
 *  stretch (loadPacks' commit, its halves marked loaded and its kind items accepted, its fallback's per-pack commits),
 *  and before anything awaiting that load continues. A promise job, not a timer: the native test host never fires one. */
function scheduleReresolve(): void {
  if (scheduled || !liveStore) return;
  scheduled = true;
  void Promise.resolve().then(() => {
    scheduled = false;
    try {
      liveStore?.reresolve();
    } catch (e) {
      console.warn(`[project] re-resolving the project after a pack arrived threw: ${String(e)}`);
    }
  });
}
```

  - Add after `adoptContributions`:

```ts
  /** Make this store the module set's live project: a commit to the registries, or a move in the packs' status (a
   *  live load answered), re-resolves it (docs/downloadable-packs-m3-design.md section 6). The host that owns the live
   *  project calls this once; a later call by another store takes over. */
  followPacks(): void {
    liveStore = this;
    if (following) return;
    following = true;
    subscribeContributions(scheduleReresolve);
    subscribePackStatus(scheduleReresolve);
  }

  /** What a pack's arrival does to the open project (docs/downloadable-packs-m3-design.md section 6): each placeholder
   *  is tried again through the load path, a success taking its id and slot with its ROM and entries, and one that still
   *  fails keeps its needs, read again; the roles that were missing are placed (SystemsStore.retryMissingRoles); the
   *  sections and services committed since are adopted; the graph is published once and the observers told. Nothing the
   *  user did changed the project, so it is as clean or dirty as it was. True when anything changed. Called by the
   *  scheduler for the registered store, after the load that committed. */
  reresolve(): boolean {
    const wasDirty = this.dirty;
    let changed = false;
    this.holdGraph = true;
    try {
      for (const held of this.systems.placeholders()) {
        const parsed = classifySystem(held.raw);
        if (!parsed) continue; // a raw that parsed once parses again: kept as it is
        if ("needs" in parsed) {
          this.systems.setNeeds(held.id, parsed.needs);
          continue;
        }
        const seed = seedOf(held.blobs.get("sram"), held.blobs.get("state"), held.blobs.get("song"));
        if (this.systems.adoptInto(held.id, parsed.system, seed) !== null) changed = true;
      }
      if (this.systems.retryMissingRoles()) changed = true;
    } finally {
      this.holdGraph = false;
    }
    const adopted = this.adoptPending();
    if (changed || adopted) {
      try {
        // As a load places its systems: a system the engine refuses for a reason of its own is discarded, noted.
        this.placeLoadedSystems();
        this.systems.writeHeldBatteries();
      } catch (e) {
        console.warn(`[project] placing what a commit brought threw: ${String(e)}`);
      }
    }
    this.dirty = wasDirty;
    this.notifySystemsChange();
    this.onChangeCb();
    return changed || adopted;
  }
```

  The hosts register their stores:
  - `src/appStores.ts`: `ComposeOptions` gains
    `/** This graph's project is the module set's live one, which a pack's arrival re-resolves
    (ProjectStore.followPacks): the plugin's control plane, or the UI's own stores where no control plane shares its. */
    live?: boolean;`, and
    `composeAppStores` calls `project.followPacks()` after building the project when `live` is true.
  - `src/pluginControlPlane.ts`: `composeAppStores({ notify: (channel) => uiNotify.fn(channel), live: true })`. The
    composition comes before `loadAppPacks()`, so the launch packs' commit re-resolves the (still empty) project.
  - `ui/stores/StoreProvider.tsx:42`: `ns?.stores ?? composeAppStores({ notify, live: true })` (the control plane's
    stores are already registered).
  - `src/bootSession.ts`: `project.followPacks();` after building it, and one doc sentence: a session's project is the
    live one, so a pack it loads (`loadPackNow`) re-resolves it. A newer session takes over, as its engine use already
    does ("one session at a time").

- [ ] **Step 6: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs project/reresolve && node scripts/run-tests.mjs contributions/sections-live`
  Expected: PASS.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`, `pnpm test:native:packs`,
  `pnpm test:ui`, `pnpm test:ui:packs`, `pnpm test:cli-ts`.
  Expected: all pass. A pack mode's launch commit now re-resolves the registered store (empty at that point), and
  nothing a case reads moves; a case that does is a finding, reported with its output.

- [ ] **Step 7: Commit.**

```bash
git add packages/retroplug/src/packs/refusals.ts packages/retroplug/src/systemsList.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/projectStore.ts packages/retroplug/src/appStores.ts packages/retroplug/src/pluginControlPlane.ts packages/retroplug/ui/stores/StoreProvider.tsx packages/retroplug/src/bootSession.ts packages/retroplug/test/project/reresolve.test.ts
git commit -m "project: a pack's arrival re-resolves the live project, once its load has committed, placeholders swapped in place"
```

---

### Task 8: `loadPackNow`, the live load, in every host

**Files:**
- Create: `packages/retroplug/src/packs/loadPackNow.ts`
- Modify: `packages/retroplug/src/packs/loadPacks.ts` (`loadedBuilders`)
- Modify: `packages/native/src/host/js/PackLoaderCore.cpp` (`__rp_packLoadNow`), `PackModules.hpp`
- Modify: `packages/retroplug/src/pluginControlPlane.ts`, `packages/retroplug/cli/sdk.ts`,
  `packages/retroplug/cli/sdk-types.d.ts`
- Modify: `packages/retroplug/scripts/run-native-tests.mjs`, root `package.json` (`test:native:packs`)
- Modify: `tools/run-cli-ts-tests.sh` (section 8)
- Test (new): `packages/retroplug/test/packs/load-pack-now.test.ts`,
  `packages/retroplug/test-native/packless/live-load.test.ts`,
  `tools/fixtures/ts-harness-packs/tests/live-load.test.ts`;
  changed: `packages/native/test/plugin/PackModules.test.cpp`,
  `packages/retroplug/test-native/plugin-controlplane.test.ts`

- [ ] **Step 1: Write the failing tests.** Create `test/packs/load-pack-now.test.ts`:

```ts
// The live load (docs/downloadable-packs-m3-design.md section 7): one development pack folder loaded now. Native loads
// it into the process's table (__rp_packLoadNow) and answers; on a load this module set's loader runs again with the
// builders it has loaded with, which loads only the new pack's halves; either way the status listeners hear of it.
import { test, expect } from "@retroplug/testing";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { APP_BUILDERS, loadedBuilders, loadPacks } from "../../src/packs/loadPacks";
import { subscribePackStatus } from "../../src/packs/refusals";
import type { PackHost } from "../../src/packs/packHost";

const g = globalThis as Record<string, unknown>;

test("a host with no live load answers so, and tells nobody", async () => {
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/x/1.0.0")).toEqual({ refused: "this host loads no pack live" });
  expect(told).toBe(0);
  off();
});

test("a refused live load answers the refusal and tells the status listeners", async () => {
  g.__rp_packLoadNow = (dir: string) => JSON.stringify({ refused: `pack x is refused: nothing at ${dir}` });
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/x/1.0.0")).toEqual({ refused: "pack x is refused: nothing at /packs/x/1.0.0" });
  expect(told).toBe(1);
  off();
  delete g.__rp_packLoadNow;
});

test("a loaded pack's halves load with the builder sets this module set has loaded with", async () => {
  const empty: PackHost = { list: () => [], importHalf: async () => ({}), refuse: () => {} };
  expect(loadedBuilders()).toBe(undefined);
  const ui = { ui: () => [] };
  await loadPacks(empty, APP_BUILDERS);
  await loadPacks(empty, ui);
  const union = loadedBuilders()!;
  expect(Object.keys(union).sort()).toEqual([...Object.keys(APP_BUILDERS), "ui"].sort());
  g.__rp_packLoadNow = () => JSON.stringify({ loaded: "lpn-a" });
  g.__rp_packList = () => "[]";
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/lpn-a/1.0.0")).toEqual({ loaded: "lpn-a" });
  expect(told).toBe(1);
  off();
  delete g.__rp_packLoadNow;
  delete g.__rp_packList;
});
```

  Create `test-native/packless/live-load.test.ts`:

```ts
// The live load on a real host (docs/downloadable-packs-m3-design.md section 7): run by `run-native-tests.mjs --packs`
// on retroplug-host-packs with no pack on the path (the pack-less folder). The render fixture pack gives every ROM its
// tsfix-render role, so a system booted after the load carries it, and one booted before does not.
import { test, expect } from "@retroplug/testing";
import { bootSession } from "../../src/bootSession";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { packStatus } from "../../src/packs/loadPacks";

declare const __MGB_ROM__: string;
declare const __PACKS_BUILD_DIR__: string;

test("a pack loaded live reaches the next system, and a second load of its folder answers the same", async () => {
  const s = bootSession();
  const kinds = (): string[] => {
    const loaded = s.project.systems.loadRom(__MGB_ROM__);
    if (loaded === null || !("system" in loaded)) throw new Error(`mGB did not load: ${JSON.stringify(loaded)}`);
    const view = s.project.systems.view().find((v) => v.id === loaded.system);
    s.project.systems.removeSystem(loaded.system);
    return view?.roles.map((r) => r.kind) ?? [];
  };
  expect(kinds().includes("tsfix-render")).toBe(false);
  const dir = `${__PACKS_BUILD_DIR__}/tsfix-render/1.0.0`;
  expect(await loadPackNow(dir)).toEqual({ loaded: "tsfix-render" });
  expect(packStatus().loaded["tsfix-render"]).toEqual(["app"]);
  expect(kinds().includes("tsfix-render")).toBe(true);
  expect(await loadPackNow(dir)).toEqual({ loaded: "tsfix-render" });
  const nowhere = await loadPackNow(`${__PACKS_BUILD_DIR__}/no-such-pack/1.0.0`);
  expect("refused" in nowhere).toBe(true);
  s.project.newProject();
});
```

  Create `tools/fixtures/ts-harness-packs/tests/live-load.test.ts`:

```ts
// A session loads a pack live (docs/downloadable-packs-m3-plan.md, Task 8): `loadPackNow` from the SDK loads the render
// fixture pack into this session, and the next system it boots carries the pack's role. Run by
// tools/run-cli-ts-tests.sh section 8 with RP_LIVE_PACK naming the pack's folder and RP_GB_ROM naming mGB.
import { test, expect, bootSession, loadPackNow } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

test("a pack loaded live reaches the next system a session boots", async () => {
  const s = bootSession();
  expect(await loadPackNow(tjs.env.RP_LIVE_PACK ?? "")).toEqual({ loaded: "tsfix-render" });
  const loaded = s.project.systems.loadRom(tjs.env.RP_GB_ROM ?? "");
  if (loaded === null || !("system" in loaded)) throw new Error(`the ROM did not load: ${JSON.stringify(loaded)}`);
  const view = s.project.systems.view().find((v) => v.id === loaded.system);
  expect(view?.roles.map((r) => r.kind).includes("tsfix-render")).toBe(true);
  s.project.systems.removeSystem(loaded.system);
});
```

  In `tools/run-cli-ts-tests.sh` section 8, after the session-packs leg the CLI plan added:

```bash
# A session loads a pack live (docs/downloadable-packs-m3-plan.md): the render fixture pack, not on the launch path,
# reaches the next system the session boots.
LL="$TMP/live-load"
mkdir -p "$LL/tests"
cp "$FIX/ts-harness-packs/tests/live-load.test.ts" "$LL/tests/"
RETROPLUG_PACK_PATH="$(packpath "")" RP_LIVE_PACK="$PACKS/tsfix-render/1.0.0" RP_GB_ROM="$REPO/packages/mgb/rom/mGB.gb" \
	"$CLI" test "$LL/tests" >"$TMP/live-load.out" 2>&1
check "packs: a session loads a pack live, and its next system carries the pack's role" 0 $?
```

  In `PackModules.test.cpp`, after the refusal-list case Task 6 added:

```cpp
TEST_CASE("a live load answers the pack it loaded, or why it refused", "[pack-modules]") {
    PackTable table;
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    const std::string good = fixture("pmgood").generic_string();
    CHECK(readString(rt, "globalThis.__rp_packLoadNow(" + quoted(good) + ")") == R"({"loaded":"pmgood"})");
    CHECK(table.find("pmgood") != nullptr);
    CHECK(readString(rt, "globalThis.__rp_packLoadNow(" + quoted(good) + ")") == R"({"loaded":"pmgood"})");  // same folder
    const std::string refused = readString(rt, "globalThis.__rp_packLoadNow('/no/such/pack/1.0.0')");
    CHECK(refused.rfind(R"({"refused":")", 0) == 0);
    CHECK(readString(rt, "globalThis.__rp_packRefusals()").find("\"needsUpdate\":false") != std::string::npos);
}
```

  In `test-native/plugin-controlplane.test.ts`'s first case, add
  `expect(typeof g.__rp_loadPackNow).toBe("function"); // the live load's bridge`.

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs packs/load-pack-now`
  Expected: FAIL (no `loadPackNow.ts`, no `loadedBuilders`).

- [ ] **Step 3: The builders a module set has loaded with.** In `src/packs/loadPacks.ts`, beside `loaded`:

```ts
/** The union of the builder sets this module set has loaded with: what a live load runs again (loadPackNow), so a pack
 *  arriving later gets every half a pack loaded at launch got here. */
let builtWith: Partial<Record<HalfKind, HalfBuilder>> | undefined;

/** The builders this module set has loaded with, or undefined before its first load. */
export function loadedBuilders(): Partial<Record<HalfKind, HalfBuilder>> | undefined {
  return builtWith;
}
```

  and as the first line of `loadPacks`: `builtWith = { ...builtWith, ...builders };`.

- [ ] **Step 4: The TS half of the live load.** Create `src/packs/loadPackNow.ts`:

```ts
// The live load (docs/downloadable-packs-m3-design.md section 7): one development pack folder loaded now, a development
// and test hook that M4's `pack install` and Settings > Packs call. Native loads the folder into the process's table
// (__rp_packLoadNow: PackTable::load, every Engine registering it at its next setGraph, a kinds half waiting for the next
// launch as M2f decided); on a load this module set's loader runs again with the builders it has loaded with, which
// loads only the new pack's halves, and the registry commit re-resolves the open project (ProjectStore.reresolve). The
// status listeners hear of a refusal too, so a placeholder waiting for the pack shows it refused.
import { APP_BUILDERS, loadedBuilders, loadPacks, type HalfBuilder } from "./loadPacks";
import { nativePackHost, type HalfKind } from "./packHost";
import { packStatusChanged } from "./refusals";

export type LoadPackNowResult = { loaded: string } | { refused: string };

interface LiveLoadHook {
  __rp_packLoadNow?: (dir: string) => string;
}

/** Load the pack whose version folder is `dir` now. Never rejects: a refusal is the answer. `builders` defaults to the
 *  ones this module set has loaded with (the control plane's, the UI's once its editor loaded them, a CLI session's),
 *  or the control plane's before any load. */
export async function loadPackNow(
  dir: string,
  builders?: Partial<Record<HalfKind, HalfBuilder>>,
): Promise<LoadPackNowResult> {
  const hook = (globalThis as LiveLoadHook).__rp_packLoadNow;
  if (!hook) return { refused: "this host loads no pack live" };
  let answer: LoadPackNowResult;
  try {
    answer = JSON.parse(hook(dir)) as LoadPackNowResult;
  } catch (e) {
    return { refused: `the host's live load failed: ${e instanceof Error ? e.message : String(e)}` };
  }
  if ("loaded" in answer) await loadPacks(nativePackHost(), builders ?? loadedBuilders() ?? APP_BUILDERS);
  packStatusChanged();
  return answer;
}
```

- [ ] **Step 5: The native hook.** In `PackLoaderCore.cpp`:

```cpp
// A live load (docs/downloadable-packs-m3-design.md section 7): the version folder `dir` loaded into the table now,
// answered as JSON, {"loaded": id} or {"refused": reason}. A folder already loaded answers its load; a refusal is in the
// table's refusals and on stderr, as a launch's are.
JSValue packLoadNow(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    packs::PackTable* table = g_table.load();
    if (!table || argc < 1) return JS_ThrowTypeError(ctx, "__rp_packLoadNow(dir) takes a pack's version folder");
    const char* dir = JS_ToCString(ctx, argv[0]);
    if (!dir) return JS_EXCEPTION;
    const std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(dir)));
    JS_FreeCString(ctx, dir);
    const packs::PackLoad load = table->load(path);
    if (!load) std::fprintf(stderr, "[packs] %s\n", load.error.c_str());
    const std::string out = load ? "{\"loaded\":" + jsonString(load.pack->manifest.id) + "}"
                                 : "{\"refused\":" + jsonString(load.error) + "}";
    return JS_NewStringLen(ctx, out.data(), out.size());
}
```

  (include `<cstdio>`), and install `__rp_packLoadNow` (1 argument) inside `if (refusals)` in `installGlobals`.
  Document both new globals in `PackModules.hpp` beside `__rp_packRefused`, and in `PackLoaderCore.hpp`'s note that the
  bare runtime installs `__rp_packList` alone.

- [ ] **Step 6: The bridges.** In `src/pluginControlPlane.ts`: import `{ loadPackNow }` from `./packs/loadPackNow`; add
  to the header's list `//   __rp_loadPackNow(dir)       - load a development pack folder now (a test and M4's hook)`;
  set `pluginNs.loadPackNow = loadPackNow;` inside the `if (pluginNs)` block; and after `__rp_packStatus`:

```ts
// The live load (docs/downloadable-packs-m3-design.md section 7): a development pack folder loaded now, its result in
// the pack status and the project re-resolved. Fire and forget: the C++ boundary is synchronous.
g.__rp_loadPackNow = (dir: string): void => {
  void loadPackNow(dir);
};
```

  In `cli/sdk.ts`, after the `keepAlive` export: `export { loadPackNow } from "../src/packs/loadPackNow";` and
  `export type { LoadPackNowResult } from "../src/packs/loadPackNow";`. In `cli/sdk-types.d.ts`, after `runSession`:

```ts
/** Load the pack whose version folder is `dir` into this session now (docs/downloadable-packs-m3-design.md section 7):
 *  the next system booted sees what it provides, and an open project's placeholders it provides come alive. Resolves to
 *  `{ loaded: <pack id> }` or `{ refused: <why> }`; never rejects. The Node addon's SDK loads no pack and answers a
 *  refusal. */
export type LoadPackNowResult = { loaded: string } | { refused: string };
export declare function loadPackNow(dir: string): Promise<LoadPackNowResult>;
```

- [ ] **Step 7: The pack-less folder.** In `scripts/run-native-tests.mjs`:
  - Beside `PRELUDE`:

```js
// A file under packless/ runs in the pack mode only, with no pack on the path and no prelude: the host links neither
// Mesen nor BlipToaster and loads nothing until the file does (docs/downloadable-packs-m3-plan.md).
const PACKLESS = "packless/";
const PACKS_BUILD_DIR = join(REPO, "build/packs");
```

  - The `tests` list filters out `t.slug.startsWith(PACKLESS)` when `!PACKS`.
  - In `runOne`, `const packless = PACKS && slug.startsWith(PACKLESS);`, and the stdin entry's contents leave the
    prelude import out when `packless`.
  - The `define` block gains `__MESEN_PACK__: JSON.stringify(MESEN_PACK.replaceAll("\\", "/"))`,
    `__BLIPTOASTER_PACK__: JSON.stringify(BLIPTOASTER_PACK.replaceAll("\\", "/"))`,
    `__PACKS_BUILD_DIR__: JSON.stringify(PACKS_BUILD_DIR.replaceAll("\\", "/"))`.
  - The env: when `packless`, `delete env.RETROPLUG_PACK_PATH` (the scratch `RETROPLUG_PACKS_DIR` and cache stay).
  - Update the header comment with the folder's rule.
  In the root `package.json`, `test:native:packs` builds `retroplug-ts-fixture-packs` and
  `retroplug-packfixture-api-older-pack` beside its two packs.

- [ ] **Step 8: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs packs/load-pack-now`
  Expected: PASS.
  Run: `cmake --build build --target retroplug-host-packs retroplug-ts-fixture-packs retroplug-plugin-test retroplug-cli -j$(nproc) && node packages/retroplug/scripts/run-native-tests.mjs --packs packless && node packages/retroplug/scripts/run-plugin-tests.mjs plugin-test`
  Expected: `packless/live-load` passes; the plugin binary passes.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native` (the folder is not run there; check
  with `node packages/retroplug/scripts/run-native-tests.mjs packless`, which must say no tests match),
  `pnpm test:native:packs`, `pnpm test:cli-ts`, `pnpm test:cli-ts:packs`.
  Expected: all pass, the CLI suites with the new leg's `ok`.

- [ ] **Step 9: Commit.**

```bash
git add packages/retroplug/src/packs/loadPackNow.ts packages/retroplug/src/packs/loadPacks.ts packages/native/src/host/js/PackLoaderCore.cpp packages/native/src/host/js/PackLoaderCore.hpp packages/native/src/host/js/PackModules.hpp packages/retroplug/src/pluginControlPlane.ts packages/retroplug/cli/sdk.ts packages/retroplug/cli/sdk-types.d.ts packages/retroplug/scripts/run-native-tests.mjs package.json tools/run-cli-ts-tests.sh packages/retroplug/test/packs/load-pack-now.test.ts packages/retroplug/test-native/packless/live-load.test.ts tools/fixtures/ts-harness-packs/tests/live-load.test.ts packages/native/test/plugin/PackModules.test.cpp packages/retroplug/test-native/plugin-controlplane.test.ts
git commit -m "packs: loadPackNow loads a development pack folder live, from the CLI SDK, the plugin's bridge and the test hosts"
```

---

### Task 9: The UI: the placeholder tile, the `[!]` row, the notice

**Files:**
- Create: `packages/retroplug/ui/screens/grid/placeholderText.ts`,
  `packages/retroplug/ui/screens/grid/PlaceholderTile.tsx`
- Modify: `packages/retroplug/ui/stores/useStores.ts` (`useSystems`, line 68)
- Modify: `packages/retroplug/ui/screens/grid/SystemGrid.tsx`
- Modify: `packages/retroplug/ui/screens/menu/menuDefs.ts` (`buildInstanceMenu`, line 1315; `buildContext`, 926)
- Modify: `packages/retroplug/ui/App.tsx` (the game input, 336-337), `packages/retroplug/ui/main.tsx` (47)
- Modify: `packages/retroplug/ui/app/projectModals.ts` (`PendingModal`, `buildProjectModal`),
  `packages/retroplug/ui/app/useProjectModals.ts` (`handleOutcome`, the mount)
- Test: `packages/retroplug/test/menu/missing-packs.test.ts` (new),
  `packages/retroplug/test/menu/project-modals.test.ts`, `packages/retroplug/test-ui/missing-packs.test.ts` (new)

- [ ] **Step 1: Write the failing tests.** Create `test/menu/missing-packs.test.ts`:

```ts
// What the UI shows for a missing pack (docs/downloadable-packs-m3-design.md section 8), as pure data: a placeholder's
// card text and its menu (Remove only), a live system's missing role as a `[!]` row, and the load's notice rows. Every
// string comes from a need, so the shell names no pack, platform or kind.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import { placeholderLines } from "../../ui/screens/grid/placeholderText";
import { missingPackText } from "../../src/packs/needs";
import type { MenuItem } from "@retroplug/ui/menu";
import { gbRomBattery } from "../systems/fixtures";

function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.slotView(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    beginSongImport: () => {},
  };
}

function loaded() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  const stores = composeAppStores({ backend: be });
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [
      { platform: "gb", core: "sameboy", romPath: "/roms/a.gb",
        roles: [{ kind: "sameboy", config: {} }, { kind: "mpui-role", config: {} }],
        provenance: { roles: { "mpui-role": { id: "mpui-roles", name: "MPUI Roles" } } } },
      { platform: "mpui-platform", core: "mpui-core", romPath: "/roms/tune.mpui",
        provenance: { platform: { id: "mpui-console", name: "MPUI Console", version: "3.1.0" } } },
    ],
  };
  be.seed("/proj/mpui.rplg", JSON.stringify(doc));
  const outcome = stores.project.load("/proj/mpui.rplg");
  return { be, stores, outcome };
}

const labels = (items: MenuItem[]) => items.filter((i) => i.kind !== "separator").map((i) => i.label);

test("a placeholder's card names its platform, its ROM and the pack it needs", () => {
  const { stores } = loaded();
  const held = stores.project.systems.slotView()[1];
  expect(placeholderLines(held)).toEqual(["mpui-platform", "tune.mpui", "Needs the MPUI Console pack"]);
});

test("a placeholder's menu has Remove only, under what it needs", () => {
  const { stores } = loaded();
  const held = stores.project.systems.slotView()[1];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: held });
  expect(labels(tree.items)).toEqual(["Needs the MPUI Console pack", "Remove Instance"]);
  tree.items.find((i) => i.id === "inst-remove")!.onSelect!();
  expect(stores.project.systems.slotView().length).toBe(1);
  expect(stores.project.isDirty()).toBe(true);
});

test("a package's peer-only row does not count a placeholder as a peer", () => {
  // sameboy's Link Group needs two systems: here there is one live Game Boy and one placeholder.
  const { stores } = loaded();
  const live = stores.project.systems.slotView()[0];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: live });
  expect(tree.items.some((i) => i.id === "inst-link")).toBe(false);
  expect(tree.items.some((i) => i.id === "inst-remove")).toBe(true); // the shell's own Remove counts the slot
});

test("a live system's missing role is a warning row at the top of its menu", () => {
  const { stores } = loaded();
  const live = stores.project.systems.slotView()[0];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: live });
  expect(tree.items[0].label).toBe("MPUI Roles pack missing [!]");
  expect(tree.items[0].warn).toBe(true);
  expect(tree.items[0].disabled).toBe(true);
});

test("the load's outcome lists each pack it needs, and the notice row says what each covers", () => {
  const { outcome } = loaded();
  expect(outcome.kind === "loaded" ? outcome.missingPacks?.map(missingPackText) : undefined).toEqual([
    "MPUI Roles: 1 role",
    "MPUI Console: 1 system",
  ]);
});
```

  Append to `test/menu/project-modals.test.ts`:

```ts
test("the missing-packs notice lists the packs, says they are kept, and opens on OK", () => {
  const { stores, calls, h } = setup();
  const tree = buildProjectModal(
    { kind: "missingPacks", packs: [{ id: "mpx", name: "MPX", status: "missing", systems: 1, roles: 0 }] },
    stores,
    h,
  );
  expect(tree.title).toBe("Missing Packs");
  expect(tree.items.map((i) => i.label)).toEqual(["MPX: 1 system", "Kept and saved unchanged", "OK"]);
  const first = openingRow(tree);
  expect(first.id).toBe("missing-packs-ok");
  first.onSelect!();
  expect(calls).toEqual(["close"]);
});
```

  Create `test-ui/missing-packs.test.ts`:

```ts
// A project naming packs this build does not have, in the real app on the headless display
// (docs/downloadable-packs-m3-design.md section 8): the load raises one notice, the placeholder's tile keeps its place
// with the card's text, its menu removes it, and the live tile's menu marks its missing role. No pack is loaded: every
// name here is this file's own, so the case runs the same in the pack mode.
import { test, expect, ui, navTo, Key, type WidgetInfo } from "ui-harness";
import { createRealBackend } from "../src/realBackend";

function clickCenter(w: WidgetInfo): void {
  ui.clickAt(w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2));
}

test("placeholders and missing roles: the notice, the tile, the [!] row, and Remove", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  const be = createRealBackend();
  const project = ui.romDir() + "/rp-missing-packs.rplg";
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [
      { platform: "gb", core: "sameboy", embeddedRom: "mgb",
        roles: [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }, { kind: "mpui-role", config: {} }],
        provenance: { roles: { "mpui-role": { id: "mpui-roles", name: "MPUI Roles" } } } },
      { platform: "mpui-platform", core: "mpui-core", romPath: ui.romDir() + "/tune.mpui",
        provenance: { platform: { id: "mpui-console", name: "MPUI Console", version: "3.1.0" } } },
    ],
  };
  expect(be.writeFile(project, new TextEncoder().encode(JSON.stringify(doc)))).toBeTruthy();

  ui.fileDrop(project, 0, 0);
  ui.pump(40);
  expect(ui.findByText("Missing Packs") != null).toBeTruthy();
  expect(ui.findByTextContaining("MPUI Console: 1 system") != null).toBeTruthy();
  expect(ui.findByTextContaining("MPUI Roles: 1 role") != null).toBeTruthy();
  expect(navTo("OK")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("placeholder-1") != null).toBeTruthy();
  expect(ui.findByTextContaining("Needs the MPUI Console pack") != null).toBeTruthy();

  // The live tile is focused: its menu leads with the missing role.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("MPUI Roles pack missing [!]") != null).toBeTruthy();
  ui.tapKey(Key.Esc);
  ui.pump(10);

  // The placeholder's menu: Remove only.
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance")).toBe(null);
  expect(navTo("Remove Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-1")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  be.deleteFile(project);
});
```

- [ ] **Step 2: Run them and see them fail.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs menu/missing-packs && node scripts/run-tests.mjs menu/project-modals`
  Expected: FAIL (`placeholderText.ts` does not exist; the menu builds a live system's rows for a placeholder; no
  `missingPacks` modal).

- [ ] **Step 3: The card's text.** Create `ui/screens/grid/placeholderText.ts`:

```ts
// A placeholder tile's lines (docs/downloadable-packs-m3-design.md section 8), as pure data the tile renders and the
// pure tier checks: the platform's label (its id when no console registers it, the usual case), the ROM's file name
// (or the embedded ROM's id), then each distinct reason its needs give. Every string comes from the view and its needs.
import type { SystemView } from "../../../src/systemsStore";
import { registeredPlatforms } from "../../../src/consoles";
import { basename } from "../../../src/pathUtil";
import { needText } from "../../../src/packs/needs";

export function placeholderLines(view: SystemView): string[] {
  const label = registeredPlatforms().find((p) => p.id === view.platform)?.label ?? view.platform;
  const rom = basename(view.romPath) || view.embeddedRom;
  const reasons = [...new Set((view.placeholder?.needs ?? []).map(needText))];
  return [label, ...(rom ? [rom] : []), ...reasons];
}
```

- [ ] **Step 4: The tile.** Create `ui/screens/grid/PlaceholderTile.tsx`:

```tsx
// A placeholder's tile (docs/downloadable-packs-m3-design.md section 8): the tile's frame and focus border, so the grid
// does not move, and the card's text where the screen would be. No frame is pulled: nothing runs. Clicking it focuses
// it, so its menu (Remove only) opens on it.
import { EAlignType, Text } from "lvgljs-ui";
import { Box, tagTestId } from "@retroplug/ui-react";
import type { SystemView } from "../../../src/systemsStore";
import { placeholderLines } from "./placeholderText";

export function PlaceholderTile({
  view,
  focused,
  single,
  width,
  height,
  onFocus,
  testId,
  dimTestId,
}: {
  view: SystemView;
  focused: boolean;
  single?: boolean;
  width: number;
  height: number;
  onFocus?: () => void;
  testId: string;
  dimTestId?: string;
}) {
  const lines = placeholderLines(view);
  const font = Math.max(8, Math.round(height / 16));
  const lineH = Math.round(font * 1.6);
  const top = Math.max(0, Math.round((height - lines.length * lineH) / 2));
  const showBorder = focused && !single;
  const dim = !focused && !single;
  return (
    <Box innerRef={tagTestId(testId)} onClick={onFocus} style={{ width, height, "background-color": "#101010" }}>
      {lines.map((line, i) => (
        <Text
          key={`line-${i}`}
          align={{ type: EAlignType.ALIGN_TOP_MID, pos: [0, top + i * lineH] }}
          style={{ "text-color": i === 0 ? "#ffffff" : "#c8c8c8", "font-size": font, width: width - 8, "text-align": "center" }}
        >
          {line}
        </Text>
      ))}
      {dim && (
        <Box
          innerRef={dimTestId ? tagTestId(dimTestId) : undefined}
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-color": "#000000", "background-opacity": 0.5 }}
        />
      )}
      {showBorder && (
        <Box
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-opacity": 0, "border-width": 2, "border-color": "#4a86e8" }}
        />
      )}
    </Box>
  );
}
```

  Check `Text`'s `align` prop and the `"text-align"` style against another `Text` with an `align` in the tree (the
  menu, the render badge); use what they use. The text lines must sit inside the tile and the overlays above them,
  in the child order written (lv_binding_js appends, spec/03's framework gotchas).

- [ ] **Step 5: The grid reads every slot.** In `ui/stores/useStores.ts`, `useSystems()` reads
  `project.systems.slotView()`, and its doc comment says so ("every slot, a placeholder's view carrying
  `placeholder`"). In `SystemGrid.tsx`, import `PlaceholderTile` and render it in the slot's non-menu branch:

```tsx
                  {showMenu ? (
                    <Menu width={tw} height={th} zoom={zoom} tree={menuTree!} onClose={onMenuClose ?? (() => {})} />
                  ) : sys.placeholder ? (
                    <PlaceholderTile
                      key={`placeholder-${sys.id}`}
                      view={sys}
                      focused={sys.focused}
                      single={single}
                      width={tw}
                      height={th}
                      testId={`placeholder-${index}`}
                      dimTestId={`dim-${index}`}
                      onFocus={() => stores.project.systems.setFocus(sys.id)}
                    />
                  ) : (
```

  Update the file's header (a placeholder's slot renders its card). Then the readers that must not treat a
  placeholder as runnable, each changed here (a placeholder's view has `roles: []`, so a role lookup already finds
  nothing, and these are the ones that act on the system itself):
  - `ui/App.tsx:336-337`: the game input and the gamepad input go to the focused system only when it is live:
    `const focusedSlot = systems.find((v) => v.id === stores.project.systems.focused());` and
    `active: playing && !focusedSlot?.placeholder` for both hooks (a placeholder has no core to press a button on).
  - `ui/screens/menu/menuDefs.ts`'s `buildContext` (926): the packages' menu rows get the live systems,
    `systems: ctx.systems.filter((v) => !v.placeholder)`, so a peer-only row (sameboy's Link Group,
    `sameboy/ts/ui/index.ts:125`) does not count a placeholder as a peer; the shell's own `multi` (1318) and
    `validAudioRoutings` (1013) keep `ctx.systems`, the slot count the graph's split rule counts (Task 4).
  - `ui/main.tsx:47`: `initialWindowSize` counts `stores.project.systems.slotView().length`, the grid's tiles.
  - `ui/App.tsx:84` and `:96` (the views a tile opens) and `ui/host/uiHost.ts:135` (the packages' `host.systems.view()`)
    already read `view()`, the live systems: unchanged, so a package's view or its real-cart pick
    (`uiHost.ts:151`, `drivenCartSystem`) never sees a placeholder.
  - `ui/App.tsx`'s window title (`focusedSys`, `trackerCartLabel`) reads a placeholder's `roles: []` and finds no
    tracker: unchanged.
  Run `git grep -n "useSystems()\|useSystem(\|ctx.systems" -- packages/retroplug/ui packages/*/ts/ui` for any reader
  this list misses, and list in the report each one and what it does with a placeholder.

- [ ] **Step 6: The menus.** In `ui/screens/menu/menuDefs.ts`, import `{ needText, roleNeedText }` from
  `../../../src/packs/needs`, and at the top of `buildInstanceMenu`:

```ts
  if (sys.placeholder) return placeholderMenu(ctx, sys);
```

  Above `buildInstanceMenu`:

```ts
/** A placeholder's menu (docs/downloadable-packs-m3-design.md section 8): what it needs, then Remove, its only edit. */
function placeholderMenu(ctx: MenuContext, sys: SystemView): MenuTree {
  const reasons = [...new Set((sys.placeholder?.needs ?? []).map(needText))];
  const items: MenuItem[] = [
    ...reasons.map((reason, i) => ({ ...infoRow(`inst-needs-${i}`, reason), warn: true })),
    separator("inst-sep-needs"),
    action("inst-remove", "Remove Instance", () => ctx.stores.project.systems.removeSystem(sys.id)),
  ];
  const base = ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug";
  const rom = basename(sys.romPath) || sys.embeddedRom;
  return { title: rom ? `${base} - ${rom}` : base, items };
}
```

  (`basename` from `../../../src/pathUtil` if `menuDefs.ts` does not import it yet.) In `buildInstanceMenu`, before
  the `firstWinsMenuIds` call, lead with the missing roles:

```ts
  // A role the graph cannot place, its pack missing or refused, is marked at the top of the menu in the warning style
  // (docs/downloadable-packs-m3-design.md section 8): read-only, so the cursor opens on the first real row.
  const missing: MenuItem[] = (sys.missingRoles ?? []).map((need, i) => ({
    ...infoRow(`inst-missing-${i}`, `${roleNeedText(need)} [!]`),
    warn: true,
  }));
```

  and pass `[...missing, ...items]` to `firstWinsMenuIds`.

- [ ] **Step 7: The notice.** In `ui/app/projectModals.ts`, import `{ missingPackText, type MissingPack }` from
  `../../src/packs/needs`; add to `PendingModal`:

```ts
  // A load's packs it does not have (docs/downloadable-packs-m3-design.md section 8): what each covers, kept and saved.
  | { kind: "missingPacks"; packs: MissingPack[] }
```

  and in `buildProjectModal`, before the `notice` branch:

```ts
  if (pending.kind === "missingPacks") {
    return {
      title: "Missing Packs",
      items: [
        ...pending.packs.map((p, i) => infoRow(`missing-pack-${i}`, missingPackText(p))),
        infoRow("missing-packs-kept", "Kept and saved unchanged"),
        button("missing-packs-ok", "OK", onClose),
      ],
    };
  }
```

  In `ui/app/useProjectModals.ts`, above the hook:

```ts
/** The project revision the missing-packs notice last showed for: once per project, whether the load came through a
 *  menu here or (in the plugin) through the control plane before the editor opened. */
let noticeRevision = -1;
```

  In `handleOutcome`'s `loaded` case, replace `setPending(null);` with:

```ts
        if (outcome.missingPacks?.length) {
          noticeRevision = project.revision();
          setPending({ kind: "missingPacks", packs: outcome.missingPacks });
        } else {
          setPending(null);
        }
```

  and add a mount effect beside the hook's other effects:

```ts
  // A project the control plane loaded before this editor mounted (a DAW session's state chunk) raises the notice here,
  // once per project.
  useEffect(() => {
    const packs = project.missingPacks();
    if (packs.length && noticeRevision !== project.revision()) {
      noticeRevision = project.revision();
      setPending({ kind: "missingPacks", packs });
    }
  }, []);
```

  (import `useEffect` if the file does not).

- [ ] **Step 8: Run them and see them pass, then the tiers.**
  Run: `cd packages/retroplug && node scripts/run-tests.mjs menu/missing-packs && node scripts/run-tests.mjs menu/project-modals`
  Expected: PASS.
  Run: `cmake --build build -j$(nproc) && node packages/retroplug/scripts/run-ui-tests.mjs missing-packs`
  (the bare build: a UI change needs it, AGENTS.md). Expected: PASS. Save a snapshot with
  `ui.snapshotPng("/tmp/ui-missing-packs.png")` while the placeholder shows, read it, and check the text sits inside
  the tile; remove the call before committing if no other UI test keeps one.
  Then: `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:ui`, `pnpm test:ui:packs`.
  Expected: all pass.

- [ ] **Step 9: Commit.**

```bash
git add packages/retroplug/ui/screens/grid/placeholderText.ts packages/retroplug/ui/screens/grid/PlaceholderTile.tsx packages/retroplug/ui/stores/useStores.ts packages/retroplug/ui/screens/grid/SystemGrid.tsx packages/retroplug/ui/screens/menu/menuDefs.ts packages/retroplug/ui/app/projectModals.ts packages/retroplug/ui/app/useProjectModals.ts packages/retroplug/ui/App.tsx packages/retroplug/ui/main.tsx packages/retroplug/test/menu/missing-packs.test.ts packages/retroplug/test/menu/project-modals.test.ts packages/retroplug/test-ui/missing-packs.test.ts
git commit -m "ui: a placeholder's tile and menu, a missing role's [!] row, and the missing-packs notice"
```

  (Add any other reader Step 5's search changed, by name.)

---

### Task 10: End to end on a host with neither pack, and the plugin's getState

**Files:**
- Create: `packages/native/test/packs/ts-fixture/variants/tsfix-proto-kind/pack.json.in`, `js/app.js`, `js/protocol.js`,
  `js/kinds.js`
- Modify: `packages/native/test/packs/ts-fixture/CMakeLists.txt` (line 102's list, and the header)
- Create: `packages/retroplug/test-native/packless/missing-packs.test.ts`
- Modify: `packages/retroplug/test-native/plugin-controlplane.test.ts`

- [ ] **Step 1: The Engine-refused fixture.** Create
  `packages/native/test/packs/ts-fixture/variants/tsfix-proto-kind/pack.json.in`:

```json
{
  "id": "tsfix-proto-kind", "name": "TS fixture variant tsfix-proto-kind", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": ["rp.tsfix-proto-kind.echo"], "roles": ["tsfix-proto-kind"] },
  "halves": { "app": "js/app.js", "protocol": "js/protocol.js", "kinds": "js/kinds.js" },
  "protocol": {
    "nodes": [{ "type": "rp.tsfix-proto-kind.echo", "midiIn": "in", "writes": [], "midiOut": true }],
    "roles": { "tsfix-proto-kind": "rp.tsfix-proto-kind.echo" }
  },
  "kinds": { "controllerApps": [{ "id": "tsfix-proto-kind-app" }] }
}
```

  `js/app.js`:

```js
// A role placed as a protocol node, beside a kinds half (docs/downloadable-packs-m3-plan.md, Task 10): loaded live, every
// running Engine refuses the whole pack (its kind items wait for the next launch), so the role is registered in TS and
// its node is a type the Engine does not have.
export default {
  roles: [{ kind: "tsfix-proto-kind", category: "feature", schema: { parse: () => ({}) } }],
  controllerApps: [{ id: "tsfix-proto-kind-app", label: "TS Fixture Proto Kind App", config: { parse: () => ({}) } }],
};
```

  `js/protocol.js`, the echo node `tsfix-proto` builds, by hand, with the ports the manifest wires (`in`, `midiOut`):

```js
// The protocol node of tsfix-proto-kind: its ports are the ones pack.json wires (in, midiOut). Its process sends nothing.
(function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  var id = "rp.tsfix-proto-kind.echo";
  var ports = [{ id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }];
  var layout = { ports: ports, params: [] };
  nodes.push({
    id: id,
    descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
    parseSettings: function (raw) { return raw === undefined ? {} : raw; },
    layout: function () { return layout; },
    create: function () { return { process: function () {} }; },
    reshapes: false,
    faultOnThrow: false,
  });
})();
```

  `js/kinds.js`, `build/packs/tsfix-kind/1.0.0/js/kinds.js`'s runtime with this pack's id (copy that file after a
  build of `retroplug-ts-fixture-packs` and change `tsfix-app` to `tsfix-proto-kind-app` in both places; keep the
  rest byte for byte, the runtime object and the `__rp_registerKindItems("controllerApps", [...], [...])` call).

  Add `tsfix-proto-kind` to the `foreach(_v ...)` list at `ts-fixture/CMakeLists.txt:102`, and a line to the header's
  list: `# tsfix-proto-kind  an app, a protocol and a kinds half: an Engine refuses it when it is loaded live`.
  Reconfigure (`cmake build`) so the variant is staged at `build/packs/tsfix-proto-kind/1.0.0`.

- [ ] **Step 2: Write the end-to-end file.** Create `test-native/packless/missing-packs.test.ts`:

```ts
// M3 end to end (docs/downloadable-packs-m3-design.md section 9), on retroplug-host-packs with no pack on the path, so
// the host links neither Mesen nor BlipToaster: a project with a BlipToaster NES system, a Game Boy system and a system
// on a pack refused for its API level opens with the Game Boy running and the other two placeholders; it saves to
// identical bytes; Mesen then BlipToaster loaded live bring the NES system up with its ROM and roles, and it renders; the
// API-level fixture gives the refused reason; and a role whose pack the Engine refuses is left out while its system runs.
import { test, expect } from "@retroplug/testing";
import { bootSession } from "../../src/bootSession";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { packStatus } from "../../src/packs/loadPacks";

declare const __CONFIG_DIR__: string;
declare const __REPO_RESOURCES_DIR__: string;
declare const __MESEN_PACK__: string;
declare const __BLIPTOASTER_PACK__: string;
declare const __PACKS_BUILD_DIR__: string;

const versionOf = (dir: string): string => dir.slice(dir.lastIndexOf("/") + 1);

test("a project naming missing packs opens, saves back unchanged, and comes alive as the packs arrive", async () => {
  const s = bootSession();
  const be = s.backend;
  const MESEN = { id: "mesen", name: "Mesen", version: versionOf(__MESEN_PACK__) };
  const BLIP = { id: "bliptoaster", name: "BlipToaster", version: versionOf(__BLIPTOASTER_PACK__) };
  const PROTO_KIND = { id: "tsfix-proto-kind", name: "TS fixture variant tsfix-proto-kind", version: "1.0.0" };
  const OLD = { id: "packfixture-api-older", name: "Pack fixture (packfixture-api-older)", version: "1.0.0" };
  const nes = {
    platform: "nes",
    core: "mesen",
    romPath: `${__REPO_RESOURCES_DIR__}/roms/bliptoaster.nes`,
    roles: [
      { kind: "mesen", config: {} },
      { kind: "nes-n8-midi", config: {} },
      { kind: "bliptoaster", config: {} },
      { kind: "bliptoaster-assets", config: { overrides: [] } },
    ],
    provenance: { platform: MESEN, roles: { mesen: MESEN, bliptoaster: BLIP, "bliptoaster-assets": BLIP } },
  };
  const old = { platform: "m3-old", core: "m3-old", romPath: `${__REPO_RESOURCES_DIR__}/roms/bliptoaster.nes`, provenance: { platform: OLD } };
  const gb = {
    platform: "gb",
    core: "sameboy",
    embeddedRom: "mgb",
    roles: [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }, { kind: "tsfix-proto-kind", config: {} }],
    provenance: { roles: { "tsfix-proto-kind": PROTO_KIND } },
  };
  const path = `${__CONFIG_DIR__}/m3.rplg`;
  expect(be.writeFile(path, new TextEncoder().encode(JSON.stringify({ schemaVersion: "4", settings: {}, systems: [gb, nes, old] })))).toBe(true);

  // It opens: the Game Boy runs, the other two are placeholders, and the outcome names every pack it needs.
  const outcome = s.project.load(path);
  expect(outcome.kind).toBe("loaded");
  if (outcome.kind !== "loaded") return;
  expect(outcome.systems).toBe(1);
  expect(outcome.missingPacks?.map((p) => p.id)).toEqual(["tsfix-proto-kind", "mesen", "bliptoaster", "packfixture-api-older"]);
  const opened = s.project.systems.slotView();
  expect(opened.map((v) => v.placeholder !== undefined)).toEqual([false, true, true]);
  s.audio.renderAudio(200);
  expect(be.readState(opened[0].id) !== null).toBe(true);
  expect(s.project.isDirty()).toBe(false);

  // It saves to identical bytes, the placeholders as written.
  expect(s.project.save(`${__CONFIG_DIR__}/m3-one.rplg`)).toBe(true);
  expect(s.project.load(`${__CONFIG_DIR__}/m3-one.rplg`).kind).toBe("loaded");
  expect(s.project.save(`${__CONFIG_DIR__}/m3-two.rplg`)).toBe(true);
  const one = new TextDecoder().decode(be.readFile(`${__CONFIG_DIR__}/m3-one.rplg`)!);
  expect(new TextDecoder().decode(be.readFile(`${__CONFIG_DIR__}/m3-two.rplg`)!)).toBe(one);
  const saved = JSON.parse(one) as { systems: unknown[] };
  expect(saved.systems[1]).toEqual(nes);
  expect(saved.systems[2]).toEqual(old);
  const ids = s.project.systems.slotView().map((v) => v.id);

  // The API-level fixture is refused, and its placeholder says it needs updating.
  const refused = await loadPackNow(`${__PACKS_BUILD_DIR__}/packfixture-api-older/1.0.0`);
  expect("refused" in refused && refused.refused.includes("API level")).toBe(true);
  const oldNeed = s.project.systems.slotView()[2].placeholder?.needs[0];
  expect(oldNeed?.status).toBe("refused");
  expect(oldNeed?.needsUpdate).toBe(true);

  // Mesen arrives: the NES system comes up in its slot, under its id, its roles as stored; BlipToaster's are missing.
  expect(await loadPackNow(__MESEN_PACK__)).toEqual({ loaded: "mesen" });
  let now = s.project.systems.slotView();
  expect(now[1].placeholder).toBe(undefined);
  expect(now[1].id).toBe(ids[1]);
  expect(now[1].platform).toBe("nes");
  expect(now[1].romPath).toBe(nes.romPath);
  expect(now[1].roles.map((r) => r.kind)).toEqual(["mesen", "nes-n8-midi", "bliptoaster", "bliptoaster-assets"]);
  expect(now[1].missingRoles?.map((n) => n.key)).toEqual(["bliptoaster", "bliptoaster-assets"]);
  expect(s.project.isDirty()).toBe(false);

  // BlipToaster arrives: nothing is missing on the NES system, and it renders.
  expect(await loadPackNow(__BLIPTOASTER_PACK__)).toEqual({ loaded: "bliptoaster" });
  now = s.project.systems.slotView();
  const nesNow = now[1].id;
  expect(now[1].missingRoles).toBe(undefined);
  expect(now[1].roles.map((r) => r.kind)).toEqual(["mesen", "nes-n8-midi", "bliptoaster", "bliptoaster-assets"]);
  s.audio.renderAudio(500);
  expect(be.readState(nesNow) !== null).toBe(true);
  expect(be.getFrame(nesNow)?.published).toBe(true);
  expect(s.project.isDirty()).toBe(false);

  // A pack every running Engine refuses (its kind items wait for a launch): its role stays missing, now refused, and the
  // Game Boy still runs.
  expect(await loadPackNow(`${__PACKS_BUILD_DIR__}/tsfix-proto-kind/1.0.0`)).toEqual({ loaded: "tsfix-proto-kind" });
  const gbNow = s.project.systems.slotView()[0];
  expect(gbNow.missingRoles?.map((n) => [n.key, n.status])).toEqual([["tsfix-proto-kind", "refused"]]);
  // The reason is the Engine's (its setGraph answer listed the pack), not only the planner's unknown-type.
  const engineWhy = packStatus().engine["tsfix-proto-kind"];
  expect(engineWhy !== undefined && engineWhy.length > 0).toBe(true);
  expect(gbNow.missingRoles?.[0].reason).toBe(engineWhy);
  s.audio.renderAudio(200);
  expect(be.readState(gbNow.id) !== null).toBe(true);
  expect(s.project.isDirty()).toBe(false);
  s.project.newProject();
});
```

  The round trip reloads the project, so `ids` (read after it) are the ones the later checks follow. If
  `getFrame`'s shape differs (`published`), read it off `src/backend.ts` and keep the claim:
  a frame of the NES system was published.

- [ ] **Step 3: The plugin's getState keeps a placeholder.** Append to `test-native/plugin-controlplane.test.ts`:

```ts
test("getState after opening a project with a placeholder writes it back, and its slot keeps the DAW's lanes", () => {
  // docs/downloadable-packs-m3-design.md sections 1 and 3: the DPF state chunk goes through the same save path, so a
  // DAW save keeps a system whose pack is missing (no package provides the platform below); and the placeholder holds
  // slot 0, so the mGB after it keeps slot 1's automation lanes, as it has where the pack is loaded.
  const be = createRealBackend();
  const held = { platform: "m3-plugin-missing", core: "m3-plugin-core", romPath: "/nowhere/song.m3p", futureField: [1, 2] };
  const doc = {
    schemaVersion: "4",
    settings: { midiRouting: "midiChannelToInstance" },
    systems: [held, { platform: "gb", core: "sameboy", embeddedRom: "mgb" }],
  };
  const chunk = be.zip([
    { name: "project.json", bytes: new TextEncoder().encode(JSON.stringify(doc)) },
    { name: "systems/0/sram", bytes: new Uint8Array([5, 4, 3, 2, 1]) },
  ])!;
  const loadB64 = g.__rp_loadProjectB64 as (b: string) => boolean;
  const saveB64 = g.__rp_saveProjectB64 as () => string;
  expect(loadB64(b64encode(chunk))).toBeTruthy();
  const lanes = JSON.parse((g.__rp_parameterMapJson as () => string)()) as { slot: number; channel: number }[];
  expect(lanes.length > 0).toBe(true);
  expect(lanes.every((l) => l.slot >= 160 && l.slot < 320 && l.channel === 1)).toBe(true); // CC_SLOTS_PER_SYSTEM
  const entries = be.unzip(b64decode(saveB64()))!;
  const project = JSON.parse(new TextDecoder().decode(entries.find((e) => e.name === "project.json")!.bytes)) as {
    systems: unknown[];
  };
  expect(project.systems[0]).toEqual(held);
  expect(entries.find((e) => e.name === "systems/0/sram")?.bytes).toEqual(new Uint8Array([5, 4, 3, 2, 1]));
  (g.__rp_newProject as () => void)();
});
```

  Import `{ b64encode, b64decode }` from `../src/base64`.

- [ ] **Step 4: Run them.**
  Run: `cmake build && cmake --build build --target retroplug-host retroplug-host-packs retroplug-mesen-pack retroplug-bliptoaster-pack retroplug-ts-fixture-packs retroplug-packfixture-api-older-pack -j$(nproc)`, then
  `node packages/retroplug/scripts/run-native-tests.mjs --packs packless` and
  `node packages/retroplug/scripts/run-native-tests.mjs plugin-controlplane`.
  Expected: PASS.
  **Triage, if the outcome's pack list differs.** A role the packs-desktop product does not register without Mesen
  (`nes-n8-midi` is expected to be registered: the n8 package is built in) adds a need with no provenance
  (`role:<kind>`). Read which, add its package to the NES system's `provenance.roles`, and report it.
  **Triage, if the Engine-refused step fails.** Print `packStatus()` (`native`, `engine`) and the role's need. If the
  table or the TS loader refused `tsfix-proto-kind` (its kinds or protocol half), the fixture is wrong: fix it from the
  refusal's message. If the Engine took it (no `engine` entry), report it: the M2f rule this case rests on moved, and
  the case needs another way to make an Engine refuse a role's node. Do not weaken the case to pass.

- [ ] **Step 5: The tiers.** Run `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test:native`,
  `pnpm test:native:packs`, `pnpm test:ui`, `pnpm test:ui:packs`, `pnpm test:plugin`.
  Expected: all pass.

- [ ] **Step 6: Commit.**

```bash
git add packages/native/test/packs/ts-fixture/variants/tsfix-proto-kind packages/native/test/packs/ts-fixture/CMakeLists.txt packages/retroplug/test-native/packless/missing-packs.test.ts packages/retroplug/test-native/plugin-controlplane.test.ts
git commit -m "test: a project naming missing packs opens on a host with neither, saves back unchanged, and comes alive live"
```

---

### Task 11: Docs and verification

**Files:**
- Modify: `spec/05-data-persistence.md` (Persistence policy, line 158; Plugin state, line 92)
- Modify: `spec/02-native-host.md` (the Packs section: the live load, the refusal list, `refusedPacks`)
- Modify: `spec/03-ts-layer.md` (the grid, near line 502)
- Modify: `spec/06-build-test.md` (the native runner's pack mode: `test-native/packless/`)
- Modify: `spec/09-cli-debugging.md` (the SDK's `loadPackNow`, where the SDK's exports are listed)
- Modify: `spec/12-dynamic-parameters.md` (the pool: a placeholder keeps its system's range, claiming nothing)
- Modify: `spec/07-remaining-work.md` (Downloadable packs, line 162)
- Modify: `docs/downloadable-packs-loading-design.md` (section 4)
- Modify: `docs/downloadable-packs-m3-plan.md` ("After the reviews", "Design questions this plan found")

- [ ] **Step 1: spec/05.** In the persistence policy: a system a missing pack provides is a placeholder that writes its
  JSON and entries back byte for byte; `provenance` (its three members, additive, no migration); a save writes roles in
  the file's order; zip entries are keyed by the slot index at save and the file index at load. In Plugin state: the
  DAW chunk keeps a placeholder.

- [ ] **Step 2: spec/02, spec/03, spec/06, spec/09.**
  - spec/02: `__rp_packLoadNow` and `__rp_packRefusals` beside `__rp_packList` / `__rp_packRefused`; `name` in the
    pack list; `PackRefusal.needsUpdate`; `RpcSetGraphResult.refusedPacks`; `loadPackNow` and the re-resolve; the
    missing-role rule (`unknown-type` at a role node leaves the role out).
  - spec/03: the grid reads `slotView()` and what drives a system reads `view()` (Task 9's list); the placeholder tile;
    the `[!]` row; the notice and when it shows; `followPacks` and the microtask re-resolve; `GraphModel.slots`.
  - spec/12: the parameter map is projected over every slot, so a placeholder's pool range stays reserved and a later
    system's lanes do not move while a pack is missing.
  - spec/06: `test-native/packless/` runs in `--packs` mode only, with no pack on the path; what
    `test:native:packs` builds.
  - spec/09: `loadPackNow` in the SDK.

- [ ] **Step 3: spec/07.** Delete M3 from the Downloadable packs entry's "what is left" (its checklist line and its
  detail, per AGENTS.md). Delete the M2e question "should the control plane leave out the roles of packs its Engine
  refused?" (answered: it does) and the M2f question "should the TS side learn of an Engine's kind refusals?" if the
  refusal list now answers it (it learns of a refused pack; say so in one clause if a part stays open). Delete the M5b
  clause "M3's placeholders decide how it shows and saves" about a dormant real cart's platform and replace it with
  what this plan does (a project naming a dormant cart's platform before its pack holds the system as a placeholder).
  Add "Open from M3", one sentence per design question from Step 5.

- [ ] **Step 4: The loading design.** Section 4: `rp.core.missing` is deferred to user-built graphs (no saved file names
  a node type); the project-config placeholder and saving are built (M3, [its plan](downloadable-packs-m3-plan.md)); the
  live-load hook is `loadPackNow`, which M4's install and Settings > Packs call. Section 5's "Placeholders" paragraph:
  where the proof stands (`test-native/packless/missing-packs.test.ts`).

- [ ] **Step 5: The plan's own sections.** Fill in "After the reviews" (what each task's review changed, and any
  deviation from this text) and "Design questions this plan found". Expected questions, to confirm or drop:
  - **An empty slot's MIDI and audio.** A placeholder holds its routing position and its buses, so the channels a
    per-instance routing sends it and the host channels it would sum into carry nothing while its pack is missing; a
    `renderAudioPerSystem` returns its slot's buffer silent. Should the UI say so on the card (the channel it holds)?
  - **More than one project per module set.** One store is registered; a host that ever held two live projects in one
    module set would need the registration to be a list.
  - **A gone ROM on a placeholder** is not offered for relink, and stays a placeholder after its pack arrives (adopt
    refuses the missing file). Should the scan include placeholders?
  - **A v1 file** with an unknown platform is still dropped (its core is never backfilled).
  - **A role registered late keeps its stored config unparsed** until the next load, unless its system was rebuilt for
    a construct hook.
  - **The reactive leave-out** costs one refused `setGraph` after each re-resolve while an Engine refuses a pack.
  - **Engine refusals only grow** in TS (an Engine never takes a pack back), so a pack refused by one plugin instance's
    Engine reads refused in that instance only.
  - **A live load's kinds half** waits for the next launch in every running Engine (M2f), so a pack with one always
    shows refused until a relaunch.

- [ ] **Step 6: The verification.**
  1. Run `pnpm test`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`, `pnpm test:native`,
     `pnpm test:native:packs`, `pnpm test:ui`, `pnpm test:ui:packs`, `pnpm test:plugin`, `pnpm test:cli-ts`,
     `pnpm test:cli-ts:packs`. Save each log under `.superpowers/m3/`.
     Expected: every one passes, except the known machine-local `app-cores` GBA case in the native tiers, as in M5c and
     the CLI milestone.
  2. The parity, kit and render goldens are among those tiers: a byte that moved is a real change (AGENTS.md), so stop
     and report it with both hashes rather than re-record.
  3. Run the parallel tip-verify,
     `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 .superpowers/verify/tip-verify-par.sh`, with its run directory at
     `.superpowers/verify/runs/m3`, and compare against `runs/cli-packs`.
     Expected: additions only.

- [ ] **Step 7: Commit.**

```bash
git add spec/05-data-persistence.md spec/02-native-host.md spec/03-ts-layer.md spec/06-build-test.md spec/09-cli-debugging.md spec/12-dynamic-parameters.md spec/07-remaining-work.md docs/downloadable-packs-loading-design.md docs/downloadable-packs-m3-plan.md
git commit -m "docs: M3, placeholders for a missing pack, provenance, the live load and refusals in the pack status"
```

---

### Task 12: The CI gate

Needs the user's push approval. Once approved:
1. Push `feature/modularize`.
2. Run `gh workflow run build.yml --ref feature/modularize`.
3. Read every job.

The linux job's `test:native:packs` step runs the pack-less folder (it now builds the TS fixture packs and the
API-level fixture), and the pure, UI, plugin and CLI steps run the new cases. On Windows the C++ changes build under
`/W4 /WX` (the new struct member, the `char8_t` path, the JSON builders); a warning there is fixed, not suppressed. On
macOS and linux-arm64 the native changes build and their tiers pass.

## After the reviews

## Design questions this plan found
