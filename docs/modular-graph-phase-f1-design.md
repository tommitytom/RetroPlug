# Phase F1: the TS package mechanism and the leaf TS moves

**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** designed, prototyped on `proto/f1-packages`: sections 1 to 10 are corrected in place to what the
prototype found, and section 11 (As built) records the build

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

Approved by the user, 2026-11-07, and corrected to what the build found. A package is `packages/<name>/` with
`package.json`, `ts/` and (existing ones) `native/`. Every package with a `ts/` tree has a two-line `tsconfig.json`
extending the root `tsconfig.base.json` and a `typecheck` script.

| Package | Gets (TS) | Native in F1 |
| --- | --- | --- |
| `core` | `defineNode`, `descriptor`, `settingsSchema`, `graphDocument`, `canonicalJson`, `register` (the script host's global), `definePackage`, the role contract (`systemRoles`: `RoleType`, `RoleRegistry`, `RoleInstance`, `RomContext`, `ConstructCaps`), `constructSpec` (`ConstructSpec`), `platform` (the `Platform` and `Core` types), `offlineJob` (the engine's job API types and `OfflineRunError`), `configFields` (the zod field builders a role's schema is made of), the MIDI route node (`midiRoute`, label 16) with `midiRouting` (the routing enum it switches on) and `ticks`, `clock`, `ports`, and `runtime.d.ts` (the declared runtime contract); a `protocol` member and no `app` member | unchanged |
| `dsp` | the kit chain and pack (`chain`, `pack`), `kitEffect`, `kitContract` (`KitCompileResult`, `KitJobRunner`, `KitFormat`, a slot's source spec), `kitJobs` (the chain half of the compile driver: `load`, `recorded`, `runEffects`, `settle`, `compileSlots`, `guardWords`, `NO_SLOT_REASON`), and the `kitFormats` contribution key | unchanged |
| `sameboy` | role kind `sameboy`, its settings enums (model, highpass, colour correction, palette) and `cartridge` (the Game Boy title decode both Game Boy ROM providers match on) | none until F2 |
| `mesen` | role kind `mesen`, its settings enums (region, cartridge accuracy, channel export, FIFO profile, and the N8 link mode, which is a field of the Mesen role) | none until F2 |
| `tracker` | `playbackModel`, the catalog contracts (`SongCatalog`, `AssetCatalog`), `TrackerIntegration` and its `trackerIntegrations` contribution key, `workingSongBlob`, the neutral NES ROM helpers in `nes/` (the magic scanner, the planar tile codec, the CHR image codec, the theme record with its `.rit` text format, the 8 KB DMC kit bank codec, the format constants) and `ritAssets` (the override contract both NES carts' asset roles share); a library: no `app` or `protocol` member and no entry in the product list | none |
| `lsdj` | the whole of `src/lsdj`, the `lsdj*.ts` modules that are not store orchestration, its catalogs and tracker integration, the `lsdj-assets` and `lsdj-sync` roles, `LSDJ_MODE_VALUES`, its kit format, the `lsdjSync` (21) and `lsdjSerialOut` (23) protocol nodes, its ROM provider, its fixtures | none |
| `mgb` | the `mgb` role, the `mgbMidi` protocol node (17), its ROM provider | none |
| `risa` | the whole of `src/risa`, `risaSav`, `risaSongOps`, `risaSync` and its translator, its catalogs and tracker integration, the `risa`, `risa-assets` and `risa-sync` roles, its kit format (the DMC bank, which BlipToaster shares), the `risaSync` node (19), its ROM provider, its fixtures | none |
| `smsggdj` | the whole of `src/smsggdj`, `smsSync`, its catalogs and tracker integration (its `songWrites`), the `sms-sync` role, the `smsSync` node (20), its ROM provider | none |
| `bliptoaster` | widened: the ROM's SIG block and chip detection, `rom/` (the DMC kit, theme and CHR-font view and the in-memory patcher), the `bliptoaster` and `bliptoaster-assets` roles, its asset catalog, its tracker integration, its ROM provider, its fixtures; keeps its device script pack | unchanged |
| `n8` | gains the `nes-n8-midi` role, the `n8MidiToFifo` protocol node (18) and the always-on NES ROM provider that attaches the role | unchanged |
| `testing` | new: `harness`, `rig` (the node test rig), `bytes` and `deepEqual` | none |

What stays in the shell, `packages/retroplug`: the stores (`projectStore`, `systemsStore`, `appStores`, `recentStore`,
`userConfigStore`, `bindingsStore`), `backend`, `realBackend`, `rpcClient`, `render/`, `renderJobs`, `graph/buildGraph.ts`,
`graph/directGraph.ts`, `graph/controller.ts`, the dispatch half of `romProviders.ts`, `kitFormats.ts` and
`trackerRegistry.ts` (the reads of the generated registry), the kit-compile driver's door (`kit/compile.ts`),
`tracker/liveSav.ts`, `lsdjSongImport.ts` (it orchestrates the stores), `src/controller` and `src/launchpad` with the
controller session node and the controller's LSDj parts (until F3), the shell's own enums (layout, audio routing, the
controller's launch target), `ui/`, `cli/`, `testing/mockBackend.ts` and the `test-native` and `test-ui` suites. Its
`src/app.ts` is an empty `definePackage`: the shell contributes nothing to the app's lists.

**The product list** is `core sameboy mesen dsp mgb lsdj graph-runtime n8 smsggdj risa native bliptoaster`, a dependency
order (section 3). `tracker`, `testing`, `node-sdk`, `device-io` and `devtest` are not in it: the list is what a product
registers, and a library with no contribution registers nothing.

**Dependencies as built** (an arrow is "depends on"; every TS package also depends on `core`, left off below, and
declares `zod` where it imports it and `typescript` where it has a `typecheck` script):

- `lsdj` to `dsp`, `sameboy`, `tracker`
- `risa` to `dsp`, `tracker`
- `smsggdj` to `tracker`
- `bliptoaster` to `tracker`
- `mgb` to `sameboy`
- `n8` to `node-sdk` (for its native target)
- `sameboy`, `mesen` and `tracker` to `core` alone; `dsp` to `core`
- `testing` to `core`; every package with tests has `testing` as a devDependency, and `core` does too (the cycle is
  `devDependencies` only, which the checks do not count)
- the shell to every package it imports; no package to the shell

The design as first approved had `risa`, `smsggdj` and `bliptoaster` depend on `mesen`, `bliptoaster` on `n8`, and `n8`
on `mesen`. No file in those packages imports the console package (the roles are kind strings and the NES helpers they
share are `@retroplug/tracker/nes/*`), and a declared dependency nothing uses is a warning, so they declare what they
use. The console edge becomes real in F2 if the carts take the console's enums, or never. BlipToaster does not reach
risa either: the `.rit` text format is the tracker layer's (`nes/theme`), so no cart depends on another cart, tests
included. Controller's LSDj app stays in the shell with the controller until F3, which decides where it goes.

## 3. Manifests, the product list and generation

**Dependencies are the `package.json` dependencies.** A package lists the `@retroplug/*` packages it uses as
`dependencies` (`"workspace:*"`) and its third-party imports beside them: `zod` is declared by every package that imports
it (core no longer re-exports it, so the boundary check sees each use), `typescript` by every package with a `typecheck`
script. One list serves both languages: the TS check reads it, and `cmake/packages.cmake` reads the same list for native
link and include rules (section 4). `devDependencies` cover what a package's tests import (`@retroplug/testing`). The
native packages declare `@retroplug/node-sdk`, and a package that links the engine declares `core`.

**The app member.** A package that contributes to the app has `"retroplug": { "app": "ts/app.ts" }`, the module that
default-exports `definePackage({ roles, romProviders, trackerIntegrations, kitFormats })`. Each list is optional.
`definePackage` lives in `core` and is the identity function, **non-generic**: its parameter is `PackageContributions`,
so a misspelt key is an excess property and a type error (two `@ts-expect-error` cases hold it). `core` declares only
the kinds its own contract types cover (`roles`, `romProviders`); the other kinds are typed by module augmentation from
their owners, `tracker` (`trackerIntegrations`, `packageContributions.ts`) and `dsp` (`kitFormats`), each a type-only
`declare module "@retroplug/core/definePackage"`, so `core` never imports `tracker` or `dsp`. `roles` is a list of
`RoleType` objects (no functions); a package's app module imports only its own package, `core` and the packages it
declares, never the generated module (that would be a module-init cycle). A role's module keeps its own file when a
test calls its `register*` by name, and `app.ts` only assembles.

**The protocol member.** `"retroplug": { "protocol": { "module": "ts/protocol.ts", "nodes": [...], "roles": {...} } }`:
`module` is the node definitions' module (default-exporting the `DefinedNode` list); `nodes` is the data
`protocolNodes.ts` held (type id, label, and for a wired node `midiIn`, `writes`, `launches`, `serialOut`, `midiOut`,
`out`); `roles` maps a role kind to a node type and may be absent or `{}`. The data is separate from the module so that
`buildGraph` can read it without importing the node code. The generator refuses a key no protocol node reads, a node
with `midiIn` and no `writes` or the reverse (presence is the test, so a `midiIn: null` node still reads as wired), a
duplicate type, a duplicate label, a label below 16, a gap, and a role naming a type no package declares or one with no
wiring. The eight types keep labels 16 to 23 whatever the package order: `core` carries `midiRoute` (16), `mgb`
`mgbMidi` (17), `n8` `n8MidiToFifo` (18), `risa` `risaSync` (19), `smsggdj` `smsSync` (20), `lsdj` `lsdjSync` (21) and
`lsdjSerialOut` (23), and the shell supplies the controller session (22) until F3 through the same member in its own
manifest (its `roles` is `{}`), which is a manifest like any other.

**`script` stays the device pack.** `retroplug.script` still means a MicroQuickJS device pack built by
`build-device-pack.mjs` with `device: true`; `bliptoaster` keeps it. The protocol member is a different thing (the
desktop's one QuickJS bundle: one runtime pair, source boundaries enforced instead of runtime ones) and a package may
have both, or `app` and `script` (`bliptoaster` does): a device pack is bundled from its own entry, so sibling modules
cannot move its bytes.

**The product list.** The root `package.json` has `"retroplug": { "products": { "desktop": [ ... ] } }`, the package
names a desktop product carries. It is **the dependency order and the registration order at once**, one array both
languages read: the root `CMakeLists.txt` reads it into `RP_DESKTOP_PACKAGES` (type, non-empty, each entry a string and
no duplicate are refused, and the root `package.json` is a configure input), and the generator emits the lists in that
order. The generator refuses a list that has a package before one of its listed dependencies ("a package must follow
its dependencies"), which also refuses a cycle, so the two languages cannot order differently. The native subsequence
is `core dsp graph-runtime n8 native bliptoaster`, so `product-registrations.json` and the `[packages]` golden are
unchanged. CMake registers the packages by iterating the list (`node-sdk` first, because every library is built on it,
and `device-io` and `devtest` after the list, because registering a script pack needs the host `graph-runtime` adds);
`retroplug_product` iterates its own list for registration and linking. A package with no `native` member registers
nothing and adds no target; a manifest needs one of `native`, `script`, `app` or `protocol` (a manifest with none is
refused). Only a ROM provider's place in the list is behaviour (the first provider to match a ROM fixes a system's role
list, so `n8`'s always-on NES provider precedes `risa`'s and `bliptoaster`'s); a role's kind, a kit format's id and a
tracker integration's marker role are looked up, never sequenced. The generated registry therefore refuses a
**duplicate role kind, kit format id or marker role**, each with its own message, and the generator refuses a duplicate
node type, span label or role kind across manifests; a reorder is proved by the `defaultRoles` fixtures the registry
test holds.

**The shell.** `packages/retroplug` is not a package a product lists. It is one fixed name in the generator (a
constant, derived in CMake from the generator's own path, not a manifest key), **appended last**, so the app's own
contributions close every list. The generator refuses a product list that names it, a package that depends on it
(including by an alias spec), and a contributing package (an `app` or `protocol` member) that the shell's manifest does
not declare in `dependencies`, because pnpm links it into the shell only then; the configure checks that
`packages/retroplug/node_modules/@retroplug/<name>` exists for every package the shell declares and says to run
`pnpm install` if not.

**Dependencies the product does not list.** A listed package's dependency on an unlisted one is refused only when the
unlisted one **contributes** (an `app` or `protocol` member): the product would silently lose its contribution. An
unlisted dependency with neither, with a manifest that has no `retroplug` member, or with no manifest, is left out of the
ordering, so a native link dependency (`node-sdk`) is legal. A listed package with no manifest or no member is refused,
and a manifest whose `name` is not `@retroplug/<directory>` is refused (the checks read that name).

**Generation.** One generator, `packages/retroplug/scripts/lib/packages.mjs`, reads the product list and each listed
package's manifest, and writes three gitignored modules under `packages/retroplug/src/generated`:

- `packages.ts`: imports each `app` module and concatenates the lists in product order, the shell last; exports `ROLES`,
  `ROM_PROVIDERS`, `TRACKER_INTEGRATIONS` and `KIT_FORMATS`, and the duplicate refusals above. `buildAppRegistry()`,
  `registerRomProviders`, `resolveTracker` and the kit compiler's `kitFormat(id)` read it.
- `protocol.ts`: `PROTOCOL_NODES` and `ROLE_NODES` as plain data, keys in label order (a role's by the label of the node it
  names), **importing only a type**, so `protocolNodes.ts` (and through it `buildGraph`) stays free of the app and of zod:
  bundled, `protocolNodes.ts` is two modules and `buildGraph.ts` eight, held under a ceiling by a test.
- `scriptNodes.ts`: the script bundle's entry, importing each `protocol.module`, sorting the concatenated nodes by the
  manifests' labels and **cross-checking at load**: it throws unless the registered set is exactly the manifests'
  declarations (naming a missing node, or an extra one no manifest declares) and the labels are `16 + i` with no gap.
  `src/script/bundle.ts` stays as a one-line entry because the `bundle-no-zod` test bundles that path.

A generated module names a package's contribution by its **exported specifier**, not a path: the generator reads the
manifest's `exports` (plain string targets) and imports `@retroplug/<name>/<module>` by the key whose target is the
manifest's file, and refuses one the package does not publish, naming the file and the exports it has. The shell's own
modules are relative.

Who runs it: `run-tests.mjs`, `run-ui-tests.mjs`, `run-native-tests.mjs` and `pnpm typecheck` call it first, the
standalone esbuild tools over `src` (`build-cli-sdk.mjs`, `bundle-native-test.mjs`, `gen-lsdj-offsets.mjs`, the
`author-*-rplg` tools) call it, and CMake runs it as one always-run target, `retroplug-ts-packages`, ordered before every
bundle step that reaches `src` (control plane, UI, script, render worker, CLI dispatcher, example sessions, CLI SDK
assets). It writes through a per-process temporary and a rename (retried once, accepting identical content, for
Windows), only when the output changes, so mtimes do not churn. The bundle steps re-run on every build anyway; narrowing
them to `OUTPUT`/`DEPENDS` is a separate change.

What reads the generated modules: only shell modules that stay (`appHost.ts`, `romProviders.ts`, `kitFormats.ts`,
`trackerRegistry.ts`, `graph/protocolNodes.ts`). A pack never imports a generated file; it exports to it. One cost is
known: every consumer of the registry evaluates every package's app module, so the control-plane bundle grew by 9
modules and about 16 KB at the first stage; F4's per-product bundles, or one generated module per contribution kind, is
where to revisit it.

## 4. Boundary checks

Both checks guard against **accident and drift, not a determined author**: they hold a package to what its manifest
says, and each one's header lists what it does not cover (below). They are tests, not review, and neither is a sandbox.

**The TS check.** `packages/retroplug/scripts/check-boundaries.mjs`, run at the start of `pnpm test` (quiet unless
`RP_BOUNDARIES_VERBOSE`) and as `pnpm check:boundaries`. For each package with a `ts/` tree it reads every file's imports
with TypeScript's scanner (`ts.preProcessFile`, from the root's `typescript-5` alias because TypeScript 7 exports none),
not esbuild's metafile, because esbuild drops a type-only import. The scan covers `import`, `export ... from`,
`import type`, a type-position `import("x").T`, `require`, and the **triple-slash `reference` path and types
directives**; a relative specifier resolves against the importing file, and an import that cannot be resolved is
reported without hiding the rest. Each refusal has its own message naming the file and the import:

1. a relative import, or a `reference path`, that leaves the package's own `ts/` tree (a `../../retroplug/...` reach);
2. an `@retroplug/x` the package does not declare (a test file, a file under `ts/test/` or named `*.test.ts(x)`, may also
   use `devDependencies`; a source file may not, nor import a test module of its own package);
3. a third-party import the package does not declare (`zod/v4` is `zod`; a `reference types` is satisfied by `@types/x`);
4. any dependency on a shell, declared, imported or **aliased** (`workspace:@retroplug/retroplug@*`, `link:../retroplug`);
5. a cycle in the declared **dependencies** (`devDependencies` order nothing, so `core` and `testing` do not cycle);
6. a native `#include`, quoted or angled, relative or absolute, that resolves into another package, scanned over the
   whole package directory;
7. a declared `@retroplug/x` naming no package, a dependency spec (`workspace:`, `npm:`, `link:`, `file:`, `portal:`)
   that names another package than its key, and an alias of a package that exists under another key;
8. an `exports` entry that publishes a file outside `ts/`, a test module, a pattern that can name either, or a file that
   does not exist.

A declared dependency no file uses is a warning (a dependency whose CMake target the package's native sources name
counts as used, and so does `typescript` when the package's `typecheck` script runs it); the tree reports zero. The
known exceptions are one list, `EXCEPTIONS`, each entry naming the file, import, rule and what deletes it, and an entry
no refusal matches fails; it is empty. The checker is run against fixture packages first
(`packages/retroplug/test/boundaries/fixtures`, one tree per rule, an `allowed` tree that must be clean) and then against
the real tree, which it found twelve imports in (the packs' and `n8`'s relative reaches).

**The native check.** `retroplug_check_package_boundaries()` in `cmake/packages.cmake`, **deferred to the end of the root
`CMakeLists`** so every target exists. It walks the build's targets and attributes each to the package whose directory
holds its `SOURCE_DIR` (an `ALIAS` is resolved to its real target), and judges, for every target of a package:
`LINK_LIBRARIES` and `INTERFACE_LINK_LIBRARIES` (a link item spelled as a file path is judged by where it lies),
`INCLUDE_DIRECTORIES` and `INTERFACE_INCLUDE_DIRECTORIES`, `SOURCES` (a relative source resolved against the
`SOURCE_DIR`), `COMPILE_OPTIONS` (the `-I` family: `-I`, `-iquote`, `-idirafter`, `-isystem`, `/I`, `/external:I` and the
`SHELL:` form, a relative one read from the `SOURCE_DIR`) and `LINK_OPTIONS` (`-L<dir>`, `-l<name>`). A link to a target
of another package the manifest does not declare, an include or source or search directory in another package's tree, and
a link to a target of the shell (named as such) are refused, each naming the package, the target and the dependency.
`devDependencies` count only for a target a `retroplug_add_test_binary` marked, and `node-sdk` is always allowed.
Generator expressions are evaluated when they are a form the check knows (`$<BUILD_INTERFACE>`, `$<LINK_ONLY>`,
`$<TARGET_NAME>`, and a `COMPILE_LANGUAGE` or `CONFIG` condition); one it cannot evaluate, or a flag whose value it cannot
attribute, is refused as unjudged unless the target is marked `RP_BOUNDARY_SKIP` with a reason. A product target is not
judged (its `PACKAGES` list is the declaration) and neither is a `-device` twin; `graph-runtime` is not exempt. Both
shells (`packages/native`, `packages/retroplug`) are exempt in the way they always were (they may use everything,
nothing uses them); `retroplug_check_products` stays. The rule is driven over plain lists by
`cmake/check-native-boundaries.cmake` (the refused and read fixtures in `packages/native/test/packages`), and the
gatherer by a project of its own (`packages/native/test/packages/gather`) whose cases each must hold or be refused with
their reason, all at every configure.

**What neither covers** (the headers list them, so a later hardening starts from the list): TS, computed specifiers and
bundler plugins, `tsconfig` `paths`, `imports` and symlinks, `peerDependencies` and `optionalDependencies`, an `exports`
condition's resolution, and the order a declaration reaches a package in; native, per-source properties, cache flags and
`LINK_DIRECTORIES`, generator expressions whose value only the build generator knows (a compound condition included),
`-Xlinker` and `-Wl` wrapping, a library name matching no target, `OUTPUT_NAME` and `LINK_DEPENDS`, and an `include()` of
another package's CMake file.

**`@retroplug/testing`.** `packages/testing` (`ts/harness.ts`, `rig.ts`, `bytes.ts`, `deepEqual.ts`, published as
`@retroplug/testing`, `/rig`, `/bytes`, `/deepEqual`), a devDependency of every package with tests, so `n8` and the cart
packages stop reaching into the shell. `testing` depends on `core` (the rig's types), and `core` has `testing` as a
devDependency for its own tests; pnpm prints a `cyclic workspace dependencies` warning for the pair, which is the
intended edge (the rig is test-support API, not core's). `mockBackend.ts` stays in the shell: it is a fake of the
shell's `backend`.

**Which tests move.** A test moves when its subject is wholly in the package: the codec, ROM and runtime tests of the
carts, the protocol node units (`lsdjSync`, `lsdjSerialOut`, `lsdjArduinoboy`, `mgbMidi`, `risaSync`, `smsSync`,
`n8MidiToFifo`), the node mechanism's tests in `core`, `test/lsdj/golden` (13) and `test/risa/golden` (5) with their
runners, and BlipToaster's ROM test. A test that drives the shell's app registry stays (`system-settings/*`, the
risa, BlipToaster and smsggdj provider cases, the catalog tests, which resolve through `trackerRegistry` and `render`),
and so do `canonical-json`, `graph-document` and `conformance`, whose oracle is `packages/native/test/graph/golden`, which
a package may not reach. The generator's and checker's own tests stay in the shell: they test the mechanism over fixture
packages. A cart whose synthetic ROM images the shell's tests read publishes them as `./testFixtures` (the name says
they are tests'; they sit outside `ts/test` because a source file may use no devDependencies). The runner's slug for a
moved test is `<package>/<path>`; the **skip baseline** is untouched, because the ts tier records no case for any of the
trees, and every moved file kept its case count.

**tsconfig, the runtime contract and the bundles.** `tsconfig.base.json` at the root says what `tsc` accepts and
nothing else (its `files` is core's `runtime.d.ts` through `${configDir}`); every package with a `ts/` tree has a
two-line `tsconfig.json` extending it, so each typechecks alone, and `pnpm typecheck` is `pnpm -r typecheck` (each
package's `tsc -p tsconfig.json`; the shell's also covers `../*/ts` and the UI program). The runtime contract is
`packages/core/ts/runtime.d.ts`, the globals a package may assume (`console`, `TextEncoder`, `TextDecoder`) and nothing
the shell's `env.d.ts` used to supply. **One TypeScript:** every package with a `typecheck` script declares the shell's
`typescript` version, not the root alias's.

Because esbuild reads the nearest `tsconfig` per input file, giving a package one would have changed bundles: it did,
live, when `strict` implied `alwaysStrict` and a `"use strict";` directive moved the device pack's bytecode. So the bundle
builds pin their options: `tools/ts-options.js` is passed to esbuild as `tsconfigRaw` by the script-node bundle, render
worker, CLI dispatcher and sessions and test bundle (`tools/build-session.js`), the control-plane bundle, the device
packs (`build-device-pack.mjs`), the CLI test SDK, the TS stripper and the native test bundle, each stating its own
`alwaysStrict` (on for the iife bundles that carry the directive, off for the device packs, whose toolchain prepends its
own). **The UI bundle keeps discovery** (`tools/build-ui.js`): its modules come from two trees with different
tsconfigs, the shell's (`target: ES2020`, class fields assigned) and `lv_binding_js`'s (no `target`, define semantics),
and one pin would change how every dpf.js component class initialises, a behaviour change and not a bundler preference.

**Checks that read the manifests, not a list.** The C++ `[settings]` oracle's source list is one place: it globs
`packages/*/ts/**/*.ts` (test files excluded) with the shell's `settingsEnums.ts`, `systemSettings.ts` and
`graph/buildGraph.ts`, and an enum or range must be declared in exactly one of them (a second declaration fails naming
both). `test/script/bundle-no-zod` spawns `test/script/bundleInputs.mjs`, which builds the bundle as `bundles.cmake`
does, and denies every `retroplug.app` module of the product's packages, refusing a run where that list is empty (a
protocol module is a script node, so it is not denied; the node modules are matched by exact path). The hardware
loop's staleness check is `pico/n8-host/exporter-stale.sh`, which `loop.sh` calls: every package's `native/`, the
exporter's directory, `packages/core/ts` (derived from `build-device-pack.mjs`'s own SDK list, exiting 2 if an entry
lies outside it), the toolchain, `tools/ts-options.js`, `cmake/`, the root `CMakeLists.txt`, `deps/mquickjs`, and each
script pack's `ts/` and `package.json`; the tsconfigs are deliberately not inputs, because the toolchain pins its options.

## 5. What does not change

- Behaviour. No schema, wire format, node id, port, setting, golden or device image changes.
- The UI. `menuDefs.ts`, `TRACKER_UI`, the HD player and the overlays keep importing the packs directly (by package
  name now, not relative path); F4 moves them onto extensions.
- The stores' deep imports of carts (`projectStore`, `render.ts`, `systemsStore`) stay, by package name; F4. So do four
  shell modules that re-export a package's names (`kit/compile.ts`, `audioDriver.ts`, `tracker/index.ts`,
  `trackerRegistry.ts`), whose callers are that same work.
- The native tree. `sameboy` and `mesen` have no `native` member, so `rp_sameboy_package` and `rp_mesen_package` stay on
  `retroplug-backend` under `packages/native`; F2 moves them. The native registration order stays
  `core dsp graph-runtime n8 native bliptoaster` (the product list's native subsequence).
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
- `ScriptNodes.test.cpp` and `ProtocolNodes.test.cpp`, with the span labels 16 to 23 in order; `test/script/bundle-no-zod` and `bundle-protocol`.
- `pnpm test` (pure TS, with the skip baseline's counts matching), `pnpm test:ui`, `pnpm test:plugin`, `pnpm typecheck`.
- `pnpm device:check` and `pnpm device:box`: the image hashes are unchanged and the `bliptoaster` device pack's bytecode
  is byte-equal to the one built before the phase (its source moved packages, not content).
- The LSDj and risa golden suites in their new home, with the same case counts.

The new checks are built red-first. Each refusal in section 4 has a fixture that breaks exactly one rule and a test that
the check refuses it with that rule's message: a fixture package under `packages/retroplug/test/boundaries/fixtures` for
each TS rule (an undeclared package or `zod`, a relative reach into the shell, a cycle, a climbing `#include`, a type-only
import, a reference directive, an aliased shell, a published test module) beside an `allowed` tree that must be clean, and
for the native check a configure-time fixture (`packages/native/test/packages`: the refused and read manifests and the
gather project) per link, include, source, option and generator-expression door. Each is first run and seen to fail, then
the real tree is brought under the check. The generator has its own tests: a fixture product list gives the expected
`packages.ts`, `protocol.ts` and `scriptNodes.ts`, and every refusal in section 3 (an out-of-order list, a duplicate or
missing span label, a duplicate role kind, an unpublished export, a node's unknown key) has a case; an unchanged input
leaves the files untouched.

There is no hardware leg.

## 7. Docs to update

- `AGENTS.md`: the package rule (TS halves, the manifest's `app` and `protocol` members, the product list, the two checks).
- `spec/06-build-test.md` (the generator, `check:boundaries`, the moved tests' slugs) and `spec/07-remaining-work.md`
  (section 11's backlog items done or deferred to F2 to F4).
- `docs/package-split.md`: the F1 row gets its as-built note; `docs/modular-graph-design.md` section 11 links it.
- `packages/retroplug/README.md` and each new package's `package.json` `description`.

## 8. How it is built

A prototype first, as E1 to E4: branch `proto/f1-packages`, five stages, each reviewed (they landed as listed; section 11):

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

Each question stays as asked; the answer is what the prototype found.

1. **`systemRoles.ts` imports `./backend`** for `ConstructSpec`, and `RoleType.onConstruct` is typed on it. The role
   contract types move to `core`, so either `ConstructSpec` moves down with them (a type, but it names the backend's
   fields) or `onConstruct` is typed on a narrower interface in `core`. Which is smaller, and does the generated registry
   or the stores need the shell's full type?

   **Answer.** `ConstructSpec` moves down to `core` (`constructSpec.ts`), with the `Platform` and `Core` types it names.
   It is a TypeScript-only data shape that names no shell runtime, the shell's `backend` re-exports it, and the narrower
   interface would have left the store unable to pass `onConstruct`'s result back as a `ConstructSpec` without a cast and
   put a second copy of the shape in `core`. Neither the registry nor the stores need another shell type. Two cart files
   still reached it through `./backend`; they import `@retroplug/core/constructSpec`.

2. **`liveSav` and `workingSongBlob` neutrality.** `tracker/liveSav.ts` imports `../backend`, `../systemsStore` and
   `../systemRoles`: it edits a live battery through the stores, so it is likely shell code, not `tracker`. Does it split
   (the byte transforms in `tracker`, the store calls in the shell), and is `workingSongBlob` free of the stores?

   **Answer.** `liveSav` does not split and stays whole in the shell: all ten entry points are orchestration over a
   running system (the published SRAM, the `.sav` path, the backup, the cold boot or work-RAM write with its conditional
   write loop, the wait for a battery write), and the byte transforms it schedules are already the tracker contract's
   (`SongCatalog.load`, `commitWorkingSong`, `slotImage`, `workingSongImageDirty`). Its only pure parts would be wait
   helpers on the app's pumped timer. `workingSongBlob` touches nothing but a `WorkingSongImage` and a `DataView` and moved
   to `tracker` whole.

3. **Where risa's neutral ROM helpers land.** `bliptoaster/rom/rom.ts` imports `../../risa/rom` and
   `bliptoasterAssetsRole` imports `ritAssetsRole`. Which of these are NES-neutral (go to `tracker`), which are the
   "rit" asset format (BlipToaster and risa share it: a package of its own, or `tracker`), and does `bliptoaster` end up
   needing `risa` after all?

   **Answer.** All of it is `tracker`'s, in `ts/nes/`: the magic scanner, the planar tile codec, the CHR image codec, the
   theme record with `parseRit`/`serializeRit` (the `.rit` format is the theme file), the 8 KB kit bank codec, and the
   format constants (`constants.ts` and the old `kit.ts` were split: the format numbers and the bank codec went down,
   while risa's own ROM layout, its resident kit-metadata mirror and `deriveMetaFromBank` stayed). `ritAssetsRole` became
   `tracker`'s `ritAssets.ts`, which names no cart, so the two assets roles are siblings. A package of its own would have
   bought a name and no edge. `bliptoaster` needs `tracker` alone: it imports nothing of `risa`, tests included.

4. **The kit-compile driver: `dsp` or `tracker`?** The decision places it in `dsp` with formats registered by carts, but
   `compile.ts` takes `KitCompileSpec` and `RisaKitCompileSpec` from `../audioDriver` and the offline-job types from
   `../offline`, and the compile is a chain of offline graph jobs over the host. If those types and the job driver pull in
   the shell, the driver belongs in `tracker` (or stays in the shell with only the formats registered). Count what it
   needs before placing it.

   **Answer.** Neither as one piece. The count: `compile.ts` (262 lines) reached four shell modules, two of them the
   carts' bank assemblers, so moving it whole would have made `dsp` depend on the carts. The chain half moved to `dsp`
   (`chain`, `pack`, `kitEffect`, `kitContract`, `kitJobs`), the engine's job API types (`offlineJob`, with
   `OfflineRunError`) moved to `core` because it is not the DSP library's (a package that runs an offline job and has
   nothing to do with kits would otherwise need `dsp`), each cart's format is its own package's `kitFormat.ts`
   (`KitCompileSpec` and the risa specs are the format's parameters), and the shell keeps the dispatch (`kitFormats.ts`)
   and a re-exporting door (`kit/compile.ts`, `audioDriver.ts`) until F4.

5. **How `settingsEnums.ts` splits without changing a schema or a golden.** Each enum's `*_VALUES` array and its type go to
   the owner (`sameboy`, `mesen`, `lsdj`, shell for layout and routing), but the zod schemas and the role-config goldens
   read the values' order and the type's name. The split must leave the generated schema and every golden byte-equal, and
   the shell must keep re-exporting nothing a pack owns.

   **Answer.** It split one owner at a time and the shell re-exports nothing a pack owns: the four SameBoy enums to
   `sameboy`, the four Mesen ones and the N8 link mode (a field of the Mesen role, configuring the emulated N8 link) to
   `mesen`, `LSDJ_MODE_VALUES` to `lsdj`, MIDI routing to `core` (with the node that switches on it), and layout, audio
   routing and the controller's launch target stay the shell's. Each tuple keeps its position (the native enum's
   integer). The Opus review confirmed 14 enum tuples and 16 zod schemas identical, and the `[settings]` oracle (20
   cases, 831 assertions) held unchanged once its source list became the manifests' (section 4). The zod field builders
   a role's schema is made of moved to `core`'s `configFields`, because a role's config schema is part of the contract
   and a package may not import the shell; the shell keeps only `stringifyConfig`.

6. **Does generation keep everything reproducible?** `tsc` (typecheck must see the generated modules, so they are either
   emitted before it or `include`d), the UI test runner's own esbuild call, and the CMake bundle steps must all produce the
   same output from a clean checkout, a second run and a manifest edit. What is the minimal dependency list that makes the
   bundle targets rebuild on a manifest edit and on nothing else?

   **Answer.** Yes. The modules live under `src`, so `tsc` needs no extra wiring beyond the generator running first
   (`pnpm typecheck` does); the three TS runners and the standalone esbuild tools call it before their first bundle; CMake
   needs one always-run target ordered before every bundle step that reaches `src`. The bundle steps already re-run on
   every build, so a manifest edit rebuilds them and a build that changes nothing rewrites nothing (the generator writes
   only on change). Verified with `src/generated` deleted, through a parallel build and `build:cli-sdk`.

7. **How the controller session node (label 22) is supplied before F3**, and whether the shell's `package.json` carrying a
   `protocol` member is the cleanest form or a special case the generator should name.

   **Answer.** The shell's own `protocol` member, with `roles: {}`; no special case. The generator names the shell once,
   as the fixed last package (section 3), and F3 can move the entry to `packages/launchpad` without touching it.

8. **Where the generator and checker live.** The design puts both under `packages/retroplug/scripts` beside the runners
   that call them; the root has a `scripts/` of its own (`cmake-build.js`). Is the CMake side (which needs the same list) better served by
   the root?

   **Answer.** `packages/retroplug/scripts` for the generator and the TS check, `cmake/packages.cmake` for the native
   check, and the root `scripts/` keeps `cmake-build.js` only. CMake needs no script of ours for the list: it reads the
   root `package.json` itself, and derives the shell from the generator's path.

9. **Anything the survey missed**, found while moving files: an import cycle between two carts that only a package boundary
   shows, a test that asserts a path, a doc link check (`pnpm check:docs`) that a moved file breaks.

   **Answer.** No cart cycle. What the moves showed: the provider order could not be produced while two providers stayed
   in the shell (the NES provider moved to `n8` with its role); a test that drives the shell's registry must stay in the
   shell; a cart's synthetic ROM images are read by six shell test files, so a package publishes them; a check can name a
   moved file by basename (`bundle-no-zod`) or read it as text (the C++ `[settings]` oracle, `RP_PORTS_TS_PATH`), which no
   import points at, so a move must follow them; a script's output path (`gen-*.mjs`, `author-*-rplg`) moves with the
   file; a protocol data module that imports the app pulls zod into `buildGraph` (the third generated module); a package's
   own tsconfig changes bundle bytes (section 4); and `check:docs` breaks on a moved file's links, so every living doc's
   links were repointed (the `*-plan.md` records only by link target).

## 10. Left beyond F1

- **F3** ([package-split.md](package-split.md)): the Launchpad pack, a port of `feature/launchpad` into
  `packages/launchpad`; the controller session node (label 22), `src/controller` and `src/launchpad` move there, with
  `CONTROLLER_TARGET_VALUES` and the controller's LSDj parts (`lsdjMidiMap`, `trackerTarget`); the LSDj app the controller
  carries is contributed by `lsdj`. The shell's empty `src/app.ts` is where its roles go.
- **F2**: native halves of `sameboy` and `mesen` (and the compile-out of Mesen's consoles, `MESEN_ENABLE_<X>`, a filtered
  glob and a patch to about twenty files), `registerCoreBackends` generated per product, `host-io` as a package, the mGB
  ROM embed (`mgb-rom`, `EmbeddedRoms.hpp`, `putResource` `embedded:"mgb"`) behind a seam, `txiki` and `efsw` out of
  `retroplug-backend` as opt-in libraries. It also opens the closed unions `core` still names (section 11).
- **F4**: the menu item model, `TRACKER_UI`, the HD player and the overlays on extensions (`defineExtension.ts`'s first
  consumers), the stores' deep imports of carts through the registrations (and with them the four shell doors that
  re-export a package's names), per-product bundles, and the cost of every registry consumer evaluating every app module.
- A package that is a third party's, loaded from outside the tree, and downloadable packs: the road section 11 keeps.

## 11. As built

The prototype, on `proto/f1-packages` over the design commit `a085f3d6b`, is five commits, one per stage, each reviewed
and amended until its review passed. The sections above were corrected in place to what these found; this section records
what was built, where it departs from the design as first committed, and what it verified.

**The stages**, by commit subject:

1. `feat(packages): generate the TS package registries from the product list`: the product list in the root
   `package.json` read by CMake and the generator, the `app` and `protocol` members, a manifest with neither `native` nor
   `script` accepted, the three generated modules, the registry consumers (`appHost`, `romProviders`, `kitFormats`,
   `trackerRegistry`, `protocolNodes`) reading them with the lists still in the shell, the generator before every
   bundle step.
2. `feat(packages): hold the packages to the boundaries their manifests declare`: `check-boundaries.mjs` and the native
   check in `cmake/packages.cmake`, their red fixtures, and `@retroplug/node-sdk` declared by the native packages.
3. `feat(packages): give core, dsp and testing their TS halves`: the node mechanism, the role contract, the chain and pack
   and the harness moved out of the shell, `bliptoaster`, `devtest` and `n8` off their relative reaches, a tsconfig per
   package.
4. `feat(packages): give SameBoy, Mesen and the tracker layer their own packages`: `sameboy`, `mesen` and `tracker`,
   `settingsEnums.ts` split, the neutral NES helpers moved down, the bundle builds' pinned options.
5. `feat(packages): give the six carts their own TS packages and close the split`: `lsdj`, `mgb`, `risa`, `smsggdj`,
   `bliptoaster` widened and `n8` widened, with their tests and goldens, the kit driver's shared half in `dsp`, and the
   checks widened to the doors the build had.

**Departures from the design as committed, and why.**

- **The shell is not a manifest key.** A `retroplug.shell` member in the root `package.json` was tried and dropped: the
  shell is the app itself, so it is one constant in the generator (derived in CMake from the generator's path), appended
  last.
- **The product list is a dependency order that is refused otherwise, not a set the generator sorts.** A sorting
  generator would put the TS registrations in one order and CMake's in another. The list is no longer
  `core dsp graph-runtime n8 native bliptoaster` plus the new names appended: the new packages sit where their
  dependencies put them, the native subsequence is the old one, and the `[packages]` golden is untouched.
- **Three generated modules, not two.** With the protocol data in `packages.ts`, `protocolNodes.ts` pulled the whole app
  and zod into every `buildGraph` consumer (174 modules and 750 KB against 2 and about 2 KB), so the data is a module that
  imports only a type. The script entry also cross-checks the manifests at load, which the design did not have.
- **`definePackage` is non-generic, and the contribution kinds are typed by module augmentation** from `tracker` and
  `dsp`, so `core` imports neither; roles are objects, not `register*` functions. The first version was generic (a typo
  compiled) and put the kinds in `core`.
- **`retroplug.order` was tried and dropped.** A dependency order cannot reproduce today's interleaved role list (`risa-sync`
  sits between `n8`'s and `smsggdj`'s roles), so the first stage 5 added an explicit order to the root `package.json`
  (role kinds, and package names for providers, integrations and kit formats). The review found it dropped registrations
  silently, and that only a ROM provider's order is observable (roles live in a `Map` keyed by kind and are never
  iterated, the tracker markers are exclusive, the kit ids are looked up). The final form is the product list as the one
  order, the NES provider moved into `n8` with its role so no shell block has to sit in the middle of the providers, and
  a **duplicate refused** by the generated module where the real hazard was. The role and tracker integration lists' order
  changed (the roles by package block, and `risa` after `smsggdj`) with nothing able to observe it, and the registry test now pins the role kinds as a
  set and the `defaultRoles` result over eight fixture configurations.
- **Dependencies are what is used**, not the design's console edges (section 2): `risa`, `smsggdj`, `bliptoaster` and `n8`
  declare neither `mesen` nor `n8`, and `bliptoaster` not `risa`; `lsdj` and `risa` depend on `dsp` (their kit
  formats), which the design had not said.
- **Where things ended up that the design left open** (the answers to section 9): `ConstructSpec`, `Platform`/`Core`
  and `offlineJob` in `core`; the NES helpers, the `.rit` format and `workingSongBlob` in `tracker`; `liveSav` in the
  shell; the kit driver split between `dsp` (the chain half), each cart (its format) and the shell (the door); the test
  rig in `testing` (the review moved it out of `core`, so `core` publishes no test module); `tracker` a library outside
  the product list, `core` with no `app` member, and the shell's `src/app.ts` kept as an empty contribution.
- **Re-export doors stay in the shell until F4:** `kit/compile.ts`, `audioDriver.ts`, `tracker/index.ts` and
  `trackerRegistry.ts` re-export a package's names, because their callers (`menuDefs.ts`, `render.ts`, the stores,
  `test-native`) are F4's UI and store work. A package imports `@retroplug/<name>` directly, and nothing else in the shell
  re-exports another package's names. `settingsEnums.ts` re-exports nothing.
- **A package's `exports` is an explicit entry list, with no wildcard** (`lsdj` has 56): the entry list is the package's API,
  a new file does not become public by existing, and the generator resolves exactly the key whose target is the file.
  The checker would accept a scoped pattern; it refuses one that could reach `ts/test`.
- **The checks widened past the design in three rounds**, each to a bypass a review found by reproducing it: type-only
  imports and reference directives (the first checker read esbuild's metafile), targets by `SOURCE_DIR` and a deferred
  gatherer (the first judged per call and missed later targets), `ALIAS`, the `-I` family, generator expressions, link by
  path and link options, `devDependencies` for test targets only, aliased dependency specs. Then they stopped, and say
  where (section 4).
- **The pinned bundle options are a finding, not a design item.** Giving a package its own tsconfig moved the device
  pack's bytecode (an `alwaysStrict` directive), caught by the pack-bytes leg; the answer is `tools/ts-options.js`, with
  the UI bundle the one exception.
- **Bundle differences.** The built-in script bundle loads the same eight nodes in the same order with every
  `descriptor`, `create`, `layout` and `reshape` source identical; its text differs only in esbuild's module-path comments
  and module order and the generated `lists` array, which churns when the product order changes. Twenty-one bundles of the
  shell's TS (control plane, render worker, CLI, UI, SDK, sessions) differ from the baseline in nothing but the wrapping
  of one array literal (`AUDIO_ROUTING_VALUES` in `settingsEnums.ts`) and module order. The `bliptoaster` pack's lowered
  text is identical.
- **Tests** that the design said move did, with two exceptions the build forced: tests that drive the shell's registry
  stay, and `canonical-json`, `graph-document` and `conformance` stay for their native golden. The generator has its own
  29 cases and the checker its fixtures; the three LSDj node and decoder tests moved after a first pass left them in the
  shell.

**Verification.** F1 changes no behaviour, so every oracle ran unmodified, with no `RP_*_UPDATE` set. A baseline run at the
design commit (29 legs, every one exit 0) was compared with the same run at the final commit: every leg exit 0 and the
summary byte-identical. The legs:

- the device images, hashed: box PIO `4b4f9abc`, native `af36f223`, device check `8cc7ae7d`, identical; the `bliptoaster`
  pack bytes identical (`066c7980` at 64 bits, `fd1daf9b` and `b5017c7c` for the device runtime at 32 and 64);
  `pnpm device:check`, `pnpm device:box` and `retroplug-device-mode-test` (96 cases) exit 0;
- `pnpm test`: 182 files and 1905 cases, zero skips (baseline 177 and 1850; the difference is F1's own generator,
  checker and registry cases), and the skip baseline untouched;
- `pnpm test:native parity`: 197 cases in 26 groups, no re-record; `pnpm test:native` 169 files and 615 cases with
  resources, and the CI picture (no fixtures) 144 skips in the same 65 files; the kit baselines (3 and 9 cases) and the
  render legs (5 and 5 baseline cases, the render tests, the UI's render files), no re-record;
- `pnpm test:plugin` (11 binaries, `[settings]` 20 cases and 831 assertions, `[packages]` 2 and 504, the engine-graph
  binary 335 cases), `pnpm test:ui` (42 files, the same 2 skips), `pnpm test:node`, `test:cli-ts`, `test:n8`, the two
  consumer suites, `sdl:smoke`, `pnpm typecheck` over every package, `pnpm check:boundaries` (no exception, no warning),
  and `pnpm check:docs`.

Windows, macOS and arm64 builds of F1 were never run, and CI was not triggered; the TS side is portable by construction
(the generator's retried rename is for Windows) but the first run on those platforms is owed.

**Residuals `core` still names**, all as approved and changing no behaviour, for whichever phase opens them:
`core/ts/platform.ts`'s `Platform` and `Core` are closed unions of the consoles and the two emulators, so a third-party
console cannot add one and `sameboy` and `mesen` own their role kinds but not these; `ConstructCaps` carries `savFromJson`
and `pngDecode`, which are LSDj's capabilities; and `core/ts/ports.ts` sizes the controller's and LSDj's launch queues
(`LAUNCHES`, `CONTROLLER_OUT`).

**Raised by the reviews for later phases.** F3: the controller session node, `src/controller`, `src/launchpad`,
`CONTROLLER_TARGET_VALUES` and the controller's LSDj parts are the shell's until the pack exists, and `lsdjSongImport.ts`
stays with the stores. F2: the carts' console edge (they would depend on `mesen`'s enums only if they took them), the
native halves, and the native check's uncovered cases (per-source properties, cache flags, `LINK_DIRECTORIES`) if the
carve adds doors. F4: the four shell doors; every registry consumer evaluating every app module (a per-kind generated
module, or per-product bundles); `@retroplug/tracker`'s barrel re-exporting `ritAssets`, so importing the bare package
pulls zod (the `bundle-no-zod` test denies the role modules by manifest, and the modules the barrel exists for are
zod-free); the script bundle's text churning with the product order (the loaded nodes are the invariant). Left as it is:
the `RisaTheme`, `RisaFontSlot` and `PlacedRisaKitBank` type names, which carry risa's name inside `tracker` (a rename is
a diff of its own), and pnpm's `cyclic workspace dependencies` warning for `core` and `testing`, which is the
devDependency edge section 4 intends.
