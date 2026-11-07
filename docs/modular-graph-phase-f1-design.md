# Phase F1: the TS package mechanism and the leaf TS moves

**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** designed

F1 is the first phase of the package split ([package-split.md](package-split.md)), the road
[modular-graph-design.md](modular-graph-design.md) section 11 left open after E4. The graph's C++ was carved into
workspace packages in E1 and E2; its TypeScript was not. Today `packages/retroplug/src` holds the LSDj, risa, smsggdj and
BlipToaster codecs, roles, catalogs and protocol nodes beside the stores and the app, and the only package with a TS
half that does not reach into it is `n8`. F1 gives the TS side the same package shape the C++ side has, with the boundary
checked, and moves every cart's TS that has no UI or store in it into its own package. It changes no behaviour:
every baseline holds unmodified.

## 1. Before F1

Facts, checked against `228105400`.

- **The packages.** `packages/{node-sdk,graph-runtime,core,dsp,n8,device-io,devtest,bliptoaster,native,retroplug}`. A
  package's manifest is the `retroplug` member of its `package.json`, read by `cmake/packages.cmake`: `native` (one
  object or a list of targets with an `entry`, `device`, optionally a `provider` and `rpc`) and `script` (a device pack:
  `entry`, `device: true`, `heapBytes`). `packages.cmake` refuses a `script` with `device: false`, and a manifest needs
  one of the two. Only `n8` has a TS tree in use (`ts/index.ts`, `ts/test`); `packages/retroplug/package.json` depends
  on `@retroplug/n8` and `zod`, and on nothing else of ours.
- **The TS halves of `core` and `dsp` are not carved.** `defineNode`, `descriptor`, `settingsSchema`, `graphDocument`,
  `canonicalJson` and `systemResources` are in `packages/retroplug/src/graph` (11 files, 2746 lines); `script/register.ts`
  and the node helpers `clock.ts` and `ports.ts` in `src/script`; the kit chain and pack in `src/kit` (4 files, 671
  lines). `packages/bliptoaster/ts/index.ts` imports `../../retroplug/src/graph/defineNode` and
  `../../retroplug/src/script/register` by relative path, and `packages/n8/ts/test/*.test.ts` import
  `../../../retroplug/testing/harness`; `packages/retroplug/tsconfig.json`'s `include` lists `../n8/ts`, `../devtest/ts`
  and `../bliptoaster/ts` by hand.
- **Cart code by directory** (files / lines): `src/lsdj` 38 / 5941, `src/risa` 22 / 2451, `src/smsggdj` 7 / 1199,
  `src/bliptoaster` 4 / 669, `src/tracker` 14 / 1665, `src/controller` 6 / 580, `src/launchpad` 5 / 585, plus top-level
  `lsdj*.ts` (`lsdjSav`, `lsdjSongOps`, `lsdjSongImport`, `lsdjLsdprjImport`, `lsdjArduinoboy`, `lsdjKeyboardMap`,
  `lsdjSyncBytes`, `lsdjAssetsRole`), `risa*.ts` (`risaRole`, `risaSav`, `risaSongOps`, `risaSync`, `risaSyncTranslator`,
  `risaAssetsRole`, `ritAssetsRole`), `smsSync.ts`, `bliptoasterRole.ts` and `bliptoasterAssetsRole.ts`.
- **Registries that are hardcoded lists.**
  - `src/appHost.ts` (31 lines), `buildAppRegistry()`: `registerCoreRoles`, `registerDspRoles`, `registerLsdjAssetsRole`,
    `registerRisaRole`, `registerRisaAssetsRole`, `registerBlipToasterRole`, `registerBlipToasterAssetsRole`,
    `registerRomProviders`.
  - `src/romProviders.ts` (88 lines): the ROM providers for mgb, lsdj, nes-n8, sms, risa and bliptoaster, importing
    `isRisaRomHeader`, `isBlipToasterRomHeader`, `isSmsggdjRom` and `LsdjSyncMode` directly.
  - `src/graph/protocolNodes.ts` (146 lines): `PROTOCOL_NODES` and `ROLE_NODES`, import-free on purpose (its header says
    why: a bundle must not pull every protocol into whatever imports `buildGraph`).
  - `src/script/bundle.ts` (34 lines): the script bundle's entry, an array of eight nodes whose ORDER is the profiler's
    span labels 16 to 23, which `packages/native/test/engine-graph/ScriptNodes.test.cpp` (the case at about line 2845)
    holds: `midiRoute`, `mgbMidi`, `n8MidiToFifo`, `risaSync`, `smsSync`, `lsdjSync`, `controllerSession`,
    `lsdjSerialOut`. The nodes are `src/script/nodes/*.ts` (11 files with `clock`, `ports` and `lsdjSyncSettings`).
  - `src/tracker/trackerIntegration.ts` (234 lines): `TRACKER_INTEGRATIONS = [lsdj, risa, smsggdj, bliptoaster]` at
    line 214, importing every cart's catalogs, detectors and layouts; the smsggdj entry carries about a hundred lines of
    `songWrites` inline. The catalogs themselves (`lsdjSongCatalog`, `risaSongCatalog`, `smsggdjSongCatalog`,
    `lsdjAssetCatalog`, `risaAssetCatalog`, `smsggdjAssetCatalog`, `bliptoasterAssetCatalog`) sit in `src/tracker` too.
  - `src/kit/compile.ts` imports `../lsdj/rom/buildKit`, `../lsdj/rom/constants`, `../risa/rom/kit` and
    `../risa/rom/constants`, and the spec types from `../audioDriver` and `../offline`.
- **Role kinds** are registered in `src/coreRoles.ts` (`sameboy`, `mesen`) and `src/dspRoles.ts` (`mgb`, `nes-n8-midi`,
  `risa-sync`, `sms-sync`, `lsdj-sync`), and by `lsdjAssetsRole`, `risaRole`, `risaAssetsRole`, `bliptoasterRole`,
  `bliptoasterAssetsRole`. `src/settingsEnums.ts` (150 lines) holds every enum any of them uses: layout, MIDI and audio
  routing, the SameBoy model, highpass, colour correction and palette, the console region, cartridge accuracy, channel
  export, FIFO profile, N8 link mode and `LsdjSyncMode`.
- **Cross-imports between carts.** `lsdj` imports `tracker/playbackModel` (twice); `bliptoaster/rom/rom.ts` imports
  `../../risa/rom`; `bliptoasterAssetsRole` imports `ritAssetsRole`; `smsggdj` imports `smsSync`; `controller` imports
  `launchpad`, `tracker/playbackModel`, `systemRoles` and `lsdj`'s `lsdjMidiMap` and `trackerTarget`. `systemRoles.ts`
  (133 lines) imports `./backend` (for `ConstructSpec`) and `./platform`; `tracker/liveSav.ts` imports `../backend`,
  `../systemsStore`, `../systemRoles` and `../savPaths`.
- **The UI and the stores import carts directly**: `menuDefs.ts` (`TRACKER_UI` at line 2073), the HD player, the overlays,
  and `projectStore`, `render.ts` and `systemsStore`. `defineExtension.ts` (201 lines) has no production consumer.
- **Tests.** `scripts/run-tests.mjs` already sweeps `packages/*/ts/test/**` (slug `<name>/<path>`) as well as
  `packages/retroplug/test`. Under `test/`: `lsdj` (with `golden/`, 13 files), `risa` (`golden/`, 5), `smsggdj`,
  `bliptoaster`, `tracker`, `kit`, `launchpad`, `controller`, `graph` (`golden/build-graph*.json`), `script`
  (`bundle-no-zod.test.ts`, `register.test.ts`, `nodes/`), `menu`, `systems`, `project`, and more. `scripts/skip-baseline.json`
  records each file's allowed skips. The Windows runner and the UI runner (`run-ui-tests.mjs`) bundle from `src` with
  esbuild.
- **Products and the registration order.** The root `CMakeLists.txt` (line 387) sets
  `RP_DESKTOP_PACKAGES core dsp graph-runtime n8 native bliptoaster`; the order is held by
  `packages/native/test/engine-graph/golden/product-registrations.json` and the `[packages]` cases.
  `packages/native`'s manifest lists four entries on `retroplug-backend`: `rp_sameboy_package`, `rp_mesen_package`
  (both in `src/host/graph/SystemNodes.cpp`, 876 lines), `rp_host_io_package` and `rp_launchpad_package`.

## 2. The package map

Approved by the user, 2026-11-07. A package is `packages/<name>/` with `package.json`, `ts/` and (existing ones) `native/`.

| Package | Gets (TS) | Native in F1 |
| --- | --- | --- |
| `core` | `defineNode`, `descriptor`, `settingsSchema`, `graphDocument`, `canonicalJson`, `script/register`, the role contract types (`RoleType`, `RoleRegistry`, `RoleInstance`, `RomContext`, `ConstructCaps`), the MIDI route node (`midiRoute`) and `clock.ts` / `ports.ts` | unchanged |
| `dsp` | the kit chain and pack (`src/kit/chain.ts`, `pack.ts`), the kit-compile driver with registered formats | unchanged |
| `sameboy` | role kind `sameboy`, its settings enums (model, highpass, colour correction, palette) | none until F2 |
| `mesen` | role kind `mesen`, its settings enums (region, cartridge accuracy, channel export, FIFO profile) | none until F2 |
| `tracker` | `playbackModel`, the catalog contracts (`SongCatalog`, `AssetCatalog`), `TrackerIntegration`, `liveSav` and `workingSongBlob` if neutral, the neutral NES ROM helpers | none |
| `lsdj` | `src/lsdj`, the top-level `lsdj*.ts`, its catalogs, `lsdj-assets` and `lsdj-sync` roles, `lsdjSync` and `lsdjSerialOut` protocol nodes, its ROM provider | none |
| `mgb` | the `mgb` role, `mgbMidi` protocol node, its ROM provider | none |
| `risa` | `src/risa`, the `risa*.ts` files, its catalogs, `risa`, `risa-assets` and `risa-sync` roles, `risaSync`, its provider | none |
| `smsggdj` | `src/smsggdj`, `smsSync`, its catalogs, the `sms-sync` role, its tracker integration (its `songWrites`), its provider | none |
| `bliptoaster` | widened: `src/bliptoaster`, its roles and catalog, its tracker integration; keeps its device script pack | unchanged |
| `n8` | gains the `nes-n8-midi` role and the `n8MidiToFifo` protocol node | unchanged |

What stays in the shell, `packages/retroplug`: the stores (`projectStore`, `systemsStore`, `appStores`, `recentStore`,
`userConfigStore`, `bindingsStore`), `backend`, `realBackend`, `rpcClient`, `render/`, `renderJobs`, `graph/buildGraph.ts`,
`graph/directGraph.ts`, `graph/controller.ts`, the dispatch half of `romProviders.ts`, `src/controller` and
`src/launchpad` (until F3), `ui/`, `cli/`, `testing/mockBackend.ts` and the `test-native` and `test-ui` suites.

**Dependencies** (an arrow is "depends on"; every package also depends on `core` for `defineNode` and the role types, left
off below):

- `lsdj` to `tracker`, `sameboy`
- `risa` to `tracker`, `mesen`
- `smsggdj` to `tracker`, `mesen`
- `bliptoaster` to `tracker`, `mesen`, `n8`
- `mgb` to `sameboy`
- `n8` to `mesen` (the `nes-n8-midi` role is a role on a Mesen NES)
- `dsp` and `tracker` to `core`; `sameboy` and `mesen` to `core`
- the shell to every package; no package to the shell

`bliptoaster` reaching `risa`'s ROM helpers today (`bliptoaster/rom/rom.ts` imports `../../risa/rom`) becomes a
dependency on `tracker` only, because the neutral helpers move there (prototype question 3). Controller's LSDj app stays in
the shell with the controller until F3, which decides where it goes.

## 3. Manifests, the product list and generation

**Dependencies are the `package.json` dependencies.** A package lists the `@retroplug/*` packages it uses as
`dependencies` (`"workspace:*"`), and its third-party imports (`zod`) beside them. One list serves both languages: the
TS check reads it, and `cmake/packages.cmake` reads the same list for native link and include rules (section 4). `devDependencies`
cover what a package's tests import (`@retroplug/testing`).

**The app member.** A package that contributes to the app has `"retroplug": { "app": "ts/app.ts" }`, the module that
default-exports `definePackage({ roles, romProviders, trackerIntegrations, kitFormats })`. Each list is optional. `definePackage`
lives in `core` and is the identity function with types; it holds no registry. The lists are the pure registrations
that today are hardcoded in `appHost.ts`, `romProviders.ts`, `trackerIntegration.ts` and `kit/compile.ts`; a role is the
same `RoleType` object (or `register*` function's product) the shell registers today, in the same order.

**The protocol member.** `"retroplug": { "protocol": { "module": "ts/protocol.ts", "nodes": [...], "roles": {...} } }`:
`module` is the node definitions' module (a script-node bundle contribution, default-exporting the `DefinedNode` list);
`nodes` is the data `protocolNodes.ts` holds today (type id, `midiIn`, `writes`, `launches`, `serialOut`, `midiOut`, `out`)
with the node's profiler span label added; `roles` maps a role kind to a node type. The data is separate from the module
so that `buildGraph` can read it without importing the node code, which is why `protocolNodes.ts` is import-free today.
The generated bundle registers nodes **in span-label order** and refuses a duplicate label and a gap: the eight types
keep labels 16 to 23 whatever the package order, and a ninth node takes 24. `core` carries `midiRoute` (16), `mgb` `mgbMidi`
(17), `n8` `n8MidiToFifo` (18), `risa` `risaSync` (19), `smsggdj` `smsSync` (20), `lsdj` `lsdjSync` (21); the
controller session (22) stays in the shell until F3; `lsdj` carries `lsdjSerialOut` (23). Because 22 has no package
owner until F3, the shell supplies it through the same member in `packages/retroplug/package.json`, which is a manifest like
any other.

**`script` stays the device pack.** `retroplug.script` still means a MicroQuickJS device pack built by
`build-device-pack.mjs` with `device: true`; `bliptoaster` keeps it. The protocol member is a different thing (the
desktop's one QuickJS bundle, as today: one runtime pair, source boundaries enforced instead of runtime ones) and a
package may have both.

**The product list.** The root `package.json` gains `"retroplug": { "products": { "desktop": [ ... ] } }`, the package names
a desktop product carries in registration order. The root `CMakeLists.txt` reads it into `RP_DESKTOP_PACKAGES` (replacing the
literal at line 387), in the same order, so `product-registrations.json` and the `[packages]` golden stay unchanged. The
TS side reads the same list. A package with no `native` member (every new one in F1) is accepted by `packages.cmake`: it
registers nothing and adds no target; today's rule (a manifest needs `native` or `script`) widens to
`native`, `script`, `app` or `protocol`. Packages in the list with no native part are skipped by `retroplug_product`.

**Generation.** One generator, `packages/retroplug/scripts/lib/packages.mjs`, reads the product list, each listed package's
manifest and its `dependencies`, orders them topologically (ties by list order), and writes two gitignored modules:

- `packages/retroplug/src/generated/packages.ts`: imports each `app` module, concatenates the lists in package order,
  and exports `ROLES`, `ROM_PROVIDERS`, `TRACKER_INTEGRATIONS`, `KIT_FORMATS` and the protocol tables
  (`PROTOCOL_NODES`, `ROLE_NODES`) as plain data. `buildAppRegistry()`, `registerRomProviders`, `resolveTracker`, the kit
  compiler and `protocolNodeOf` read it.
- `packages/retroplug/src/generated/scriptNodes.ts`: the script bundle's entry, importing each `protocol.module` and
  registering the nodes in label order. `esbuild` bundles it as `src/script/bundle.ts` is bundled today (that file goes).

Who runs it: `run-tests.mjs`, `run-ui-tests.mjs`, `run-native-tests.mjs` and `pnpm typecheck` call it first, and the CMake
bundle steps (`packages/native/cmake/bundles.cmake`: control plane, UI, script, render worker, CLI) call it with every
package manifest and the product list as dependencies, so an edit to a manifest regenerates and rebuilds. It writes only
when the output changes, so mtimes do not churn.

What reads the generated modules: only the shell (`appHost.ts`, `romProviders.ts`, `protocolNodes.ts`,
`tracker/trackerIntegration.ts` as a thin resolver left in `tracker`, `kit/compile.ts`'s registry). A pack never imports a
generated file; it exports to it.

## 4. Boundary checks

**The TS check.** `packages/retroplug/scripts/check-boundaries.mjs`, run at the start of `pnpm test` and as
`pnpm check:boundaries`. For each package it bundles every entry under `ts/**` (app, protocol, index and tests) with esbuild
and reads the metafile, mapping each import to an owner: a file in the same package, another package, the shell, or a
third-party module. It refuses, each with its own message naming the file and the import:

1. a relative import that leaves the package's own tree (a `../../retroplug/...` reach);
2. an import of an `@retroplug/x` the package does not declare (a test may also use `devDependencies`);
3. a third-party import (`zod`) the package does not declare;
4. any dependency on `@retroplug/retroplug`, declared or imported (the shell rule);
5. a cycle in the declared dependencies, and a declared dependency no file uses is reported but not refused.

`packages/n8` and `packages/bliptoaster`, which break rule 1 today, are the first things the check fails on; moving their
imports is part of the first pass.

**The native check.** `cmake/packages.cmake` gains a configure check: a package's targets may link only the targets of
declared packages (and the SDK) and add include directories only inside their own tree or a declared package's. An
undeclared link or include directory fails configure with the package, the target and the dependency named. The same script
scans native `#include` paths for ones that climb out of the package (`#include "../../other/..."`). `packages/native` is a
shell: it is exempt in the way `packages/retroplug` is (it may use everything, nothing uses it); the existing
`retroplug_check_products` (a non-product reaching the backend) stays.

**`@retroplug/testing`.** `packages/retroplug/testing/harness.ts` becomes `packages/testing` (`@retroplug/testing`,
`ts/harness.ts`), a devDependency of every package with tests, so `n8` and the cart packages stop reaching into the shell. `mockBackend.ts`
stays in the shell: it is a fake of the shell's `backend`, and pack tests that need one are shell tests.

**Which tests move.** A test moves when its subject is wholly in the package: the codec, ROM and runtime tests of `lsdj`,
`risa`, `smsggdj` and `bliptoaster`, the catalogs, the protocol node units, `test/lsdj/golden` (13) and `test/risa/golden`
(5) with their runners. The generator's and checker's own tests stay in the shell: they test the mechanism over
fixture packages. Tests of stores, menus, `buildGraph`, the engine, `test-native` and `test-ui` stay. The runner's slug
for a moved test is `<package>/<path>`, so a filter changes spelling; the **skip baseline** is regenerated with
`--update-skip-baseline` and its case counts per file are compared before and after: a moved file keeps its count, a file
that gains or loses a case fails the pass. A package's tests run from `ts/test` with the package's `tsconfig`; the shell's `include`
list loses the hand-written `../n8/ts` style entries for a generated or glob form.

## 5. What does not change

- Behaviour. No schema, wire format, node id, port, setting, golden or device image changes.
- The UI. `menuDefs.ts`, `TRACKER_UI`, the HD player and the overlays keep importing the packs directly (by package
  name now, not relative path); F4 moves them onto extensions.
- The stores' deep imports of carts (`projectStore`, `render.ts`, `systemsStore`) stay, by package name; F4.
- The native tree. `sameboy` and `mesen` have no `native` member, so `rp_sameboy_package` and `rp_mesen_package` stay on
  `retroplug-backend` under `packages/native`; F2 moves them. The registration order stays
  `core dsp graph-runtime n8 native bliptoaster`.
- `defineExtension.ts` stays unconsumed until F4.
- Persistence. No `K_*` version is bumped, no migration is added.

## 6. Proof

F1 changes no behaviour, so the proof is every existing oracle, run unmodified, with no re-record. A golden that moves is a
defect in F1, not a record to update.

- Strict parity: `pnpm test:native parity` (197 cases in 26 groups), on a run with the resources tree,
  `RP_FAIL_ON_SKIP=1` and `RP_PARITY_REQUIRE_TARGET=1`.
- The kit goldens (`kit`) and the render baseline (`render-baseline`, `render-chunks`, `render-cli`).
- `buildGraph`'s goldens: `test/graph/golden/build-graph*.json` and `build-graph-roles.json`, and the engine's run of every
  one (`ProtocolNodes.test.cpp`).
- The `[packages]` golden and `product-registrations.json`.
- `ScriptNodes.test.cpp` and `ProtocolNodes.test.cpp`, with the span labels 16 to 23 in order; `test/script/bundle-no-zod`.
- `pnpm test` (pure TS, with the skip baseline's counts matching), `pnpm test:ui`, `pnpm test:plugin`, `pnpm typecheck`.
- `pnpm device:check` and `pnpm device:box`: the image hashes are unchanged and the `bliptoaster` device pack's bytecode
  is byte-equal to the one built before the phase (its source moved packages, not content).
- The LSDj and risa golden suites in their new home, with the same case counts.

The new checks are built red-first. Each refusal in section 4 has a fixture package under the checker's own test tree
(`packages/retroplug/test/boundaries/fixtures`) that breaks exactly one rule, and a test that the checker fails it with that
rule's message: an undeclared package import, a relative reach into the shell, an undeclared `zod`, a dependency cycle, a
native `#include` that climbs out, and an undeclared native link. The checker is first run against the fixtures
and seen to fail, then the real tree is brought under it. The generator has its own tests: a fixture product list gives
the expected `packages.ts` and `scriptNodes.ts`, a duplicate or missing span label is refused, and an unchanged input
leaves the file untouched.

There is no hardware leg.

## 7. Docs to update

- `AGENTS.md`: the package rule (TS halves, the manifest's `app` and `protocol` members, the product list, the two checks).
- `spec/06-build-test.md` (the generator, `check:boundaries`, the moved tests' slugs) and `spec/07-remaining-work.md`
  (section 11's backlog items done or deferred to F2 to F4).
- `docs/package-split.md`: the F1 row gets its as-built note; `docs/modular-graph-design.md` section 11 links it.
- `packages/retroplug/README.md` and each new package's `package.json` `description`.

## 8. How it is built

A prototype first, as E1 to E4: branch `proto/f1-packages`, five stages, each reviewed:

1. the generator and the product list (the manifest members, `packages.cmake` accepting a package with no native, the
   generated modules, the registry consumers reading them with the lists still pointing at the old places);
2. the boundary checks and their red fixtures;
3. `core`, `dsp` and `testing`: the TS halves and the harness, `bliptoaster` and `n8` off their relative reaches;
4. the console and tracker packages: `sameboy`, `mesen`, `tracker`, with `settingsEnums.ts` split;
5. the cart packages: `lsdj`, `mgb`, `risa`, `smsggdj`, `bliptoaster` widened, `n8` widened, their tests and goldens moved.

Model routing (the user, 2026-11-07): DeepSeek Flash for the prototype stages, the plan generator and the follow-ups;
Flash-Next at `--thinking xhigh` (two at most) for the plan's task implementers and the acceptance run; Opus for
the prototype stages' reviews and the final review; the controller coordinates and reviews the plan and the risky
code. Work directories (gitignored, under
`.superpowers/`): `f1pro` (prototype), `f1plan` (plan), `f1exec` (execution). The prototype is thrown away; the plan is
written against what it found, and execution starts from `feature/modularize`.

## 9. Prototype questions

1. **`systemRoles.ts` imports `./backend`** for `ConstructSpec`, and `RoleType.onConstruct` is typed on it. The role
   contract types move to `core`, so either `ConstructSpec` moves down with them (a type, but it names the backend's
   fields) or `onConstruct` is typed on a narrower interface in `core`. Which is smaller, and does the generated registry
   or the stores need the shell's full type?
2. **`liveSav` and `workingSongBlob` neutrality.** `tracker/liveSav.ts` imports `../backend`, `../systemsStore` and
   `../systemRoles`: it edits a live battery through the stores, so it is likely shell code, not `tracker`. Does it split
   (the byte transforms in `tracker`, the store calls in the shell), and is `workingSongBlob` free of the stores?
3. **Where risa's neutral ROM helpers land.** `bliptoaster/rom/rom.ts` imports `../../risa/rom` and
   `bliptoasterAssetsRole` imports `ritAssetsRole`. Which of these are NES-neutral (go to `tracker`), which are the
   "rit" asset format (BlipToaster and risa share it: a package of its own, or `tracker`), and does `bliptoaster` end up
   needing `risa` after all?
4. **The kit-compile driver: `dsp` or `tracker`?** The decision places it in `dsp` with formats registered by carts, but
   `compile.ts` takes `KitCompileSpec` and `RisaKitCompileSpec` from `../audioDriver` and the offline-job types from
   `../offline`, and the compile is a chain of offline graph jobs over the host. If those types and the job driver pull in
   the shell, the driver belongs in `tracker` (or stays in the shell with only the formats registered). Count what it
   needs before placing it.
5. **How `settingsEnums.ts` splits without changing a schema or a golden.** Each enum's `*_VALUES` array and its type go to
   the owner (`sameboy`, `mesen`, `lsdj`, shell for layout and routing), but the zod schemas and the role-config goldens
   read the values' order and the type's name. The split must leave the generated schema and every golden byte-equal, and
   the shell must keep re-exporting nothing a pack owns.
6. **Does generation keep everything reproducible?** `tsc` (typecheck must see the generated modules, so they are either
   emitted before it or `include`d), the UI test runner's own esbuild call, and the CMake bundle steps must all produce the
   same output from a clean checkout, a second run and a manifest edit. What is the minimal dependency list that makes the
   bundle targets rebuild on a manifest edit and on nothing else?
7. **How the controller session node (label 22) is supplied before F3**, and whether the shell's `package.json` carrying a
   `protocol` member is the cleanest form or a special case the generator should name.
8. **Where the generator and checker live.** The design puts both under `packages/retroplug/scripts` beside the runners
   that call them; the root has a `scripts/` of its own (`cmake-build.js`). Is the CMake side (which needs the same list) better served by
   the root?
9. **Anything the survey missed**, found while moving files: an import cycle between two carts that only a package boundary
   shows, a test that asserts a path, a doc link check (`pnpm check:docs`) that a moved file breaks.

## 10. Left beyond F1

- **F3** ([package-split.md](package-split.md)): the Launchpad pack, a port of `feature/launchpad` into
  `packages/launchpad`; the controller session node (label 22), `src/controller` and `src/launchpad` move there; the LSDj
  app the controller carries is contributed by `lsdj`.
- **F2**: native halves of `sameboy` and `mesen` (and the compile-out of Mesen's consoles, `MESEN_ENABLE_<X>`, a filtered
  glob and a patch to about twenty files), `registerCoreBackends` generated per product, `host-io` as a package, the mGB
  ROM embed (`mgb-rom`, `EmbeddedRoms.hpp`, `putResource` `embedded:"mgb"`) behind a seam, `txiki` and `efsw` out of
  `retroplug-backend` as opt-in libraries.
- **F4**: the menu item model, `TRACKER_UI`, the HD player and the overlays on extensions (`defineExtension.ts`'s first
  consumers), the stores' deep imports of carts through the registrations, per-product bundles.
- A package that is a third party's, loaded from outside the tree, and downloadable packs: the road section 11 keeps.
