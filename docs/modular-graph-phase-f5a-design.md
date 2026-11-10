# Phase F5a: the package-split leftovers

**Date:** 2026-11-10 · **Branch:** `feature/modularize` **Status:** designed `bef622dac`; prototyped (five stages, each
Opus-reviewed and fixed; stage 4 in two halves); as built in section 13; executed, accepted and reviewed in section 14.
The design was written over `787313f39` (the F4c tip plus the open-items checklist). The user's decisions are the
`### F5` entries of `.superpowers/f-split/decisions.md` (taken 2026-11-10); the user approved sections 1 to 4 of the
design as presented, which this document writes out with the verification the presentation did not have. Sections 1 to
11 were corrected in place to what the prototype built (the sentences that still say a plan are marked "as built" or
"the design first had"), section 11's risks are answered where the prototype answered them, and section 13 records how:
the commits, what each review found, every departure from the design as committed, and what acceptance must run. What
the verification found that the presentation did not say was put to the user and the controller, and every answer is
folded into the text and the decisions table (rows 8 to 25).

F5a is the first of two phases that close the package split's open list. After F4a
([modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md)), F4b
([modular-graph-phase-f4b-design.md](modular-graph-phase-f4b-design.md)) and F4c
([modular-graph-phase-f4c-design.md](modular-graph-phase-f4c-design.md)) every surface of the shell is a contribution a
package makes, and six things were recorded as owed: a display bug in the controller-app rows, per-product bundles, the
shell's hard dependency on the Launchpad, views that cannot be separated from menus, a `mesen` to `n8` edge for two
headers, and a second `tsc` run for one directory ([package-split.md](package-split.md); the survey behind this phase is
`.superpowers/f-split/f5-survey.md`, taken at `787313f39`, whose part B is this phase and whose part A is F5b). F5a
closes those, and proves the result by making the DAW plugin the first product whose package list differs from the
desktop's.

The phase is a boundary refactor with one visible behaviour change for one population (section 8, E2): no audio change,
no saved-file change, no graph document change for any product that lists the Launchpad, and no baseline re-recorded
except the recorded menu matrix's `hand-controller` case, the registration golden's plugin row, and the pieces section 8
names.

## 1. Goal and scope

**Goal.** The shell names no Launchpad and no controller. Views live in a module the menus never import. A second
product list exists and the plugin uses it, so "the package split works" is a fact about a build rather than a claim
about imports. `mesen` no longer depends on `n8`.

**Scope (one phase, five stages, by mechanism).** The small fixes (the display bug, the `tsconfig.ui.json` fold,
spec/07's stale lines: section 3); the `n8-wire` package and the `mesen` to `n8` seam (section 4); the views split
(section 5); the generic project slots that carry the controller out of the shell, and the contribution kinds that carry
the generator's last two hard-coded Launchpad names (section 6); real per-product bundles with the plugin as the first
differing product (section 7). It runs as one prototype in five stages (section 10). The order is fixed by what touches
what: stage 1's display fix edits the Launchpad's UI module that stage 4 reshapes, so it goes first; stage 5 needs stage
4 (a plugin without the Launchpad cannot exist while the shell imports it); stages 2 and 3 are independent of each other
and of 4.

**Not in F5a**, and where it goes:

- **F5b: the SDL app's audio and MIDI as packages behind generic backend interfaces.** The survey's part A: 1945 lines
  of `packages/native/sdl/main.cpp`, an `AudioBackend` and a `MidiBackend` seam, `MidiIo`'s multi-producer ring race,
  the Launchpad pack's own smaller port seam. It waits for two unmerged branches to land or be decided
  (`feature/handheld-perf`'s render-ahead and `feature/sdl-standalone`'s `MidiIo` ring fix), because both rewrite the
  region F5b would move. Nothing in F5a touches `sdl/main.cpp` except through the generated providers header.
- **`host.kits` in the UI host**, **gamepad axis tokens as action bindings**, **`MemoryType`'s closed wire enum** and
  **the view reopen key's width** (survey B.4, B.5, B.7, B.8): no consumer, as F4b and F4c left them.
- **Downloadable packs and third-party controller apps**, which this phase makes cheaper (a product list is data; a
  contribution kind is data) and does not build.
- **E2's second-pack template gaps** (global RPC names replaced silently, no pack capabilities, the test host's provider
  seam, the 54-line `lvgljs-ui` types slice a component pack cannot extend, the example sessions' `extends` into
  `../retroplug/`).
- **Windows, macOS, arm64 and CI** for every new script, package and target. Nothing F5a adds is platform code except
  the CMake of `n8-wire` and the per-list bundle targets; the first run of both on those hosts is on push.
- **A real `component` view on a host.** Stage 3 proves a React view package bundles; it does not draw one (section 9).

**What stays declared, said plainly.** `packages/retroplug/package.json` still declares `@retroplug/launchpad` and
`@retroplug/lsdj-launchpad` after F5a. The generator refuses a product package the shell does not declare, because the
desktop's generated modules import those packages by name through the shell's `node_modules` (spec/06 item 3), and the
shell's tests import them. The property F5a proves is that no hand-written shell source names them (rule 9, the literal
scanner, the bundle checks), and that the plugin, which does not list them, builds and runs without them. The backend
declaration is different: `packages/native` DOES drop `@retroplug/launchpad` (6.7).

**What the tree has** (checked against `787313f39`; every figure was counted or opened, and the survey's are corrected
in section 11).

- **Where the controller reaches the shell: six places, not five.** The project schema
  (`projectConfig.ts:24-25, 29, 38, 44-54, 91-106`: a `controller` block whose `target` is a `CONTROLLER_TARGET_VALUES`
  zod enum from `@retroplug/launchpad/enums`, and whose `app` default is the registry's first app); `ProjectStore` (the
  six fields at `projectStore.ts:129-134`, `controllerModel()` at `:183-218`, the poll, anchor and reader at `:476-560`,
  `setController` at `:587-601`: about 140 lines, the brief's 60 undercounts); `buildGraph` (`:404`, `:421-435`, `:438`,
  `:454-455`, `:471-483`, `:514-517`, and the `CONTROLLER_SESSION` constant at `:111`); `DirectGraph`
  (`directGraph.ts:38, 114-117, 176-181, 239-240`); the UI host's `controller` group (`ui/host/uiHost.ts:64-91` and
  `isControllerTarget` at `:131-134`, with `ControllerHost`, `ControllerApp` and `ControllerSettings` declared in
  `packages/core/ts/host.ts:90-125, 262-274, 295`); and `src/controller/registry.ts` and `src/graph/controller.ts`,
  which are the shell's own modules over the Launchpad's registry. The sixth place the brief did not list:
  `userConfig.ts:91` and the `config` host group carry a `controlSurface` block (the machine's port pair), which names
  the concept and imports nothing; F5a leaves it (section 11, item 1).
- **The generator names the Launchpad twice.** `packages.mjs:600` imports `ControllerAppContribution` from
  `@retroplug/launchpad/registry` into the generated `packages.ts`, and `:1341` imports `registerControllerApps` from
  `@retroplug/launchpad/apps` into the generated script entry. `CONTROLLER_SYNC` in the generated `protocol.ts`
  (`packages.mjs:1246-1300`) is the third, as data.
- **Rule 9 lists.** `SHELL_IMPORTS` (`check-boundaries.mjs:122-166`) carries `@retroplug/launchpad/enums` and
  `/registry` (`:153-154`) and `@retroplug/tracker/trackerPlayback`, whose only two importers are the controller's
  (`projectStore.ts:26`, `graph/controller.ts:8`); `UI_IMPORTS` (`:168-181`) carries `launchpad/enums` (`:180`).
- **One product list.** The root `package.json:76-79` has `retroplug.products.desktop`, 16 packages; the root
  `CMakeLists.txt:307-350` reads it into `RP_DESKTOP_PACKAGES`; every desktop product takes it (`retroplug-host`,
  `-render-host-test`, `-cli`, `-sdl`, `-ui-test`, `-node`, the plugin, and the test binaries that list packages). The
  generator reads the same array and writes into the shell's tree.
- **Where the bundles are made and embedded.** `bundles.cmake:25-130` makes the control-plane, UI and script-node
  bundles and `native/CMakeLists.txt:40-60` the render-worker bundle. The control-plane and UI bundles are embedded by
  their product's sources (`plugin.cmake:17,21`, `native/CMakeLists.txt:345-346, 401`), **but the script-node bundle and
  the render-worker bundle are compiled into `retroplug-backend`** (`bundles.cmake:121-123`,
  `native/CMakeLists.txt:117`), which every product links: `ScriptHost::embeddedBundle()` reads `rp_script_bundle`
  (`ScriptHost.cpp:35-36, 1786`) and `RenderHost.cpp:22-23, 297` reads `rp_render_worker_bundle`. Section 7.2 is built
  on that.
- **Views are one module with menus.** `ui/generated/menus.ts` is written by `renderMenus` (`packages.mjs:755-1000`) and
  holds `UI_CONTRIBUTIONS`, `VIEWS`, `ACTIONS` and every check; its one reader is `ui/host/menuContributions.ts` (131
  lines), which three files import: `ui/App.tsx:42`, `ui/views/TileViews.tsx:16` and
  `ui/screens/menu/menuDefs.ts:14-20`. `lsdj` and `risa` declare their views in their `ts/ui/index.ts`
  (`lsdj/ts/ui/index.ts:289-293`, `risa/ts/ui/index.ts:140-144`); the fixture package `ui-fixture` declares one in
  `ts/ui/index.ts`.
- **Two TypeScript programs for the shell.** `packages/retroplug/tsconfig.json` includes `src`, `testing`, `test`,
  `test-native`, `test-ui`, `cli`, `tools-src` and every package's `ts` by a glob; `tsconfig.ui.json` is
  `{ extends, include: ["ui"] }`; `package.json:9` runs both.

## 2. The decisions

| # | Decision | Chosen | Alternatives not taken |
| --- | --- | --- | --- |
| 1 | The phase split | F5 is two phases, leftovers first: F5a is the package-split leftovers (mostly TypeScript, no `main.cpp`), F5b the audio and MIDI backends after the two SDL branches land or are decided (user, A) | Audio and MIDI first (B); one phase (C) |
| 2 | F5a's scope | The structural leftovers with the plugin as the first differing product: the display bug, the `tsconfig.ui.json` fold, spec/07's stale lines, views split out with a pure-tier proof, the shell's Launchpad dependency removed, real per-product bundles, the `mesen` to `n8` seam (user, A). Out: `host.kits`, gamepad axis actions, `MemoryType`, the reopen key, downloadable packs, E2's template gaps | The same without per-product bundles, a fixture product instead (B); small items and the views split only (C); everything except downloadable packs (D) |
| 3 | The project's `controller` block | A project-settings contribution the Launchpad package owns: `projectSections` (`key`, `schema`, `defaults`), the package owning its migrations, applied before its schema parses. The shell keeps a section with no contributor as raw JSON and writes it back unchanged (the plugin, a build lacking a pack). Same key, same contents, no migration (user, B) | Keep it in the shell and loosen `target` (A); drop it in a product without the package (C, ruled out: the plugin would erase a project's block on its next save) |
| 4 | `mesen` to `n8` | A new header-only, device-ready package, working name `n8-wire`, holding `N8ReliableLink.hpp` and the expansion-volume range; `n8` and `mesen` both depend on it and `mesen` no longer on `n8` (user, A) | Move by owner: the link to `bliptoaster`, the range to `system` (B); accept the edge (C) |
| 5 | Views | Their own manifest member `retroplug.views`, a module default-exporting `defineViews({ views })`, generated into `ui/generated/views.ts` and imported only by the shell's view slots; `defineUi` keeps rows, menus, trackers, services and actions; `lsdj` and `risa` move their views into views modules, `ui-fixture` gets one; a new check bundles the fixture's views as the UI bundle does (user, A) | Stub React in the pure tier (B); lazy-load the views (C, not workable: the bundles are single-file bytecode with no module loader) |
| 6 | The design as presented | Five stages, edges E1 to E4, desktop goldens byte-identical, acceptance adding `reaper:all`, the editor legs and both two-instance legs, a fresh-clone typecheck, BlipToaster and NESVJ, and no hardware leg (user approved sections 1 to 4) | n/a |
| 7 | The prototype model | DeepSeek Flash builds each stage and Opus reviews each, the parallel kit (`.superpowers/verify/tip-verify-par.sh`) with `sdl:controller` run alone, and F4b's per-stage tree rule: after any fix-pass rebase every stage's tree is extracted after the final rewrite, proven equal to its commit, and typechecked and tested alone, never symlinking the worktree's `build/` for a run that writes; then a plan generator, Flash-Next implementers, the serial acceptance kit, an Opus final review and follow-ups (as F4a to F4c ran) | n/a |
| 8 | The backend's Launchpad link | Move `HarnessSurfaceLink` and the harness's controller staging out of `retroplug-backend` into the Launchpad pack; `EngineRpcService` reaches them through a small staging hook; the backend links no Launchpad code and `packages/native` drops the dependency (user, A; 6.7) | Exempt packages with no TypeScript contribution from the list check (B); keep `launchpad` in the plugin list and drop `lsdj-launchpad` alone (C) |
| 9 | Span labels per product | Positional per product, the desktop's strict no-gap rule kept (controller; 7.1) | Renumber the manifests so the session node is last, moving every desktop document's span labels |
| 10 | The shell's declarations | The shell keeps declaring `launchpad` and `lsdj-launchpad` because its generated modules import them; the property proven is that no hand-written shell source names them (controller; section 1) | Generate the desktop's modules outside the shell's tree |
| 11 | Unknown `settings` keys | Every unknown key is kept verbatim and written back unchanged (user, A; 6.1, E5) | A workspace-wide section-key list read from every manifest (B); a container key for sections (C, ruled out by the no-migration decision) |
| 12 | A section's migration stamp | A numeric `schemaVersion` inside the section (controller; 6.1) | A `sectionVersions` record at the root |
| 13 | Where the service context type lives | `@retroplug/tracker`, beside the playback contract; `ProjectSection` stays in core (controller; 6.2) | A structural `playbackOf` typed `unknown` in core |
| 14 | Placers | A `protocol.placer` module and a generated `placers.ts`, not the node module (controller; 6.3) | Placers in the `protocol.module` that exports the nodes |
| 15 | The goldens' `model` member | The case runners translate the recorded vocabulary; the files are byte-identical and `documents` are the oracle (controller; 6.3) | Re-record the `model` member |
| 16 | The extend rule | Left as F4c decided it: examples or a UI host module, not "has views" (controller; 5.3) | Generalise to "has views" and add the line to risa |
| 17 | Bundle path resolution | An esbuild `onResolve` plugin, the alias claim confirmed by a one-file bundle at the start of stage 5 (controller; 7.2) | `alias` as the brief had it |
| 18 | An unstamped section | Reads as version 1, the shape before any stamp, so a section that starts unversioned can still migrate: its first versioned release declares 2 and adds one step keyed 1 (controller, stage 4's review; corrects 6.1 and decision 12) | Read as the current version, which can never migrate |
| 19 | The backend's no-Launchpad property | A GENERIC native boundary rule: `packages/native`'s non-product, non-test targets are judged by `packages/native/package.json` like any package's, and `cmake/packages.cmake` names no pack (controller, stage 4's review; 6.7) | A configure check naming the Launchpad; the shell exempt as before |
| 20 | A fresh project in a product with no contributor for a section | Writes no block for it (E8): a section's contents come from its contributor's `defaults()` (controller, stage 4's review) | A shell-side default block for `controller` |
| 21 | A product's bundle set | Chosen by the CONTENT of its package list, so no `LIST` default: desktop products keep `PACKAGES ${RP_DESKTOP_PACKAGES}`; a non-DEVICE product whose packages equal no `retroplug.products` list is refused at configure (prototype, stage 5 and its fix pass; 7.2) | `retroplug_product(LIST ...)` with a `desktop` default; a bespoke list handed to the generator |
| 22 | Where `pnpm typecheck` writes a product's tree | `packages/retroplug/.products/<product>/`, gitignored, one `tsc` per non-desktop list the manifest names; `build.sh` and `build.bat` configure when `build/CMakeCache.txt` is missing, not when `build/` is (stage 5's review, its critical; 7.2) | A tree under `build/`, which a fresh `./build.sh` then mistakes for a configured one |
| 23 | The kind contract | `defineKind({ accept, check? })` with no `idOf` (an item's id is its `id`); a kind named after an app member is refused; `check` is handed the product's WIRED protocol node types (stage 4's review and its fix pass; 6.5) | `idOf` on the kind; every script node type handed to `check` |
| 24 | How a product's tree resolves workspace packages | A generated `paths` file and esbuild's `nodePaths`, not a `node_modules` link, which needs a privilege on Windows (prototype, stage 5; 7.2) | A symlinked `node_modules` |
| 25 | The service context, the section's parse and the control modules' place | A section's parse is hand-written (the pack declares no zod); the pack's control-plane modules live in `ts/control/` beside the app module, whose directory the device-pack scanner excludes from the portable core (prototype, stage 4; 6.1, 6.5) | zod in the pack; `ts/projectSection.ts` and friends beside the portable core |

Decisions the draft makes that the user did not take (each is a detail the design needed, rows 9 to 17 are the ones the
controller took after review, and rows 18 to 25 are the ones the prototype and its reviews added): the views module must
sit under `ts/ui/` and the generator and rule 3 both enforce it (5.2); a section's migration stamp lives inside the
section (6.1); the project services are a per-store factory, not a module singleton (6.2); the graph placer is its own
manifest member, not the node module (6.3); a project service publishes a `state` for the UI (6.2);
`project.serviceState` is a host member (6.4); the contribution kind's manifest is `retroplug.kinds` (6.5); the script
bundle and the render-worker bundle become product-supplied sources (7.2); a product's script-node span labels are its
positions (7.1); `n8wire/ExpansionVolume.hpp` is the new header's name (4.1).

## 3. Stage 1: three small fixes

### 3.1 The controller-app display bug

A hand-edited `appConfig` shows one value in the Launchpad submenu and runs another. The rows read
`legacyConfigIndex(field, current(field.key))` (`launchpad/ts/ui/index.ts:96`), which is index 0 for a choice the stored
value does not match; the runtime reads `parseAppConfig(app.id, config)` (`src/controller/registry.ts:26-29`, called by
`projectStore.ts:212` and `directGraph.ts:115`), which is `app.config.parse(config ?? {})`, where `enumField` and
`boolField` snap an unknown or mistyped value to the schema's default (`core/ts/configFields.ts:47-60`). The recorded
case is `hand-controller` (`test/menu/recorded-cases.ts:440-449`: `{ quantise: 3, follow: "yes" }`): `quantise: 3` is
not in `QUANTISE_VALUES`, so the row shows "Immediately" (index 0) while the app runs "bar" (the schema default, index
2, "Next bar"); `follow: "yes"` reads On both ways (`legacyConfigIndex` returns On for anything but `false`,
`boolField(true)` returns the default true), so that row does not move.

The fix reads what the runtime runs, through the same parse:

```ts
// launchpad/ts/ui/index.ts, controllerAppRows (:85-100) function controllerAppRows(host: UiHost, app:
ControllerAppContribution | undefined, config: Record<string, unknown>) { if (!app || !app.menu) return []; const
running = app.config.parse(config); // exactly what parseAppConfig(app.id, config) hands the runtime const defaults =
app.config.parse({}); return app.menu.map((field) =>
    configRowCycler(`lp-app-${field.key}`, field, running[field.key], defaults[field.key], (value) =>
      host.controller.set({ appConfig: { [field.key]: value } })));   // stage 4 turns this call into the project group's
}
```

`legacyConfigIndex` is deleted (`ui/ts/contribution.ts:340-372`, its doc comments at `:126` and `:345-347`, the comment
at `ui/ts/test/rows.test.ts:92`), and with it `configRowCycler`'s `index` parameter, whose only caller passed it. A
range row never had a legacy reading (`legacyConfigIndex` returned `undefined` for one), and a row over the same value
it already showed does not move, so no other RECORDED row changes. **One class of row does change, and no shipped app
has one:** a `toggle` knob whose schema default is `false`, stored as a non-boolean value. `legacyConfigIndex` read any
value but `false` as On, and `boolField(false)` snaps a non-boolean to `false`, so such a row now reads Off where it
read On. MI.MAP's `follow` defaults `true`, where both readings are On, so E1 stays exact; the case over a fake app pins
the toggle too (stage 1's report). Edge E1 (section 8): `hand-controller`'s `lp-app-quantise` cycler moves from
"Immediately" to "Next bar"; the case is re-recorded (`test/menu/recorded-matrix.json:48909`, one cycler's
`index`/`value` in one case, spliced from a throwaway recorder as F4c spliced its bindings rows, and the diff is the
record). **RED first:** a unit case over a fake app whose schema defaults a knob away from index 0 fails on the old
reading (the row shows 0, the parse gives 2) and passes on the new one. **As built:** the case lives in the shell's
`test/menu/launchpad.test.ts` (`controllerAppRows` is private, and that file owns the fake host and facet): it
re-registers a fake app under MI.MAP's id and restores the real one in a `finally`, so the registry stays one app. It
also pins the toggle-defaulting-false row and a range clamp (the review's option b, each shown RED), and
`recorded-cases.ts`'s case comment, which asserted the old reading, is corrected. The re-recorded cycler's four steps
come in a rotated order (the recorder steps from the row's own index), which is still that one cycler and nothing else.

### 3.2 `tsconfig.ui.json` folds into the shell's program

`tsconfig.ui.json` exists only to add `ui/` to a second `tsc` run (its own comment says it is kept "until someone folds
ui/ into tsconfig.json's include"). F4c measured the shell's program with `ui/` in it at 906 ms against 921 ms (F4c
section 10.13). The change is one edit, `"ui"` added to the `include` array of `packages/retroplug/tsconfig.json`, the
file deleted, and `package.json:9`'s script reduced to `node scripts/lib/packages.mjs && tsc --noEmit`. Every file that
names it, found by grep over the tree (CI workflows included, which name none): `packages/retroplug/package.json:9`;
`spec/07-remaining-work.md:36` and `:982`; and `docs/ui-shared-utils-audit.md:155`, which links the file (a doc link to
a deleted file fails `check:docs`, so that link is repointed, as F4c repointed four). The `docs/modular-graph-phase-*`
designs and plans that name it are records of the trees they describe and are not rewritten. **The check** is
`pnpm typecheck` green with `ui/` in one program, and `check:docs`.

### 3.3 spec/07's stale lines

`spec/07-remaining-work.md:640-641` says the N8 menus, `chrImage.ts` and `risaSyncTranslator.ts` "stay in
`packages/retroplug`". The tree: the N8 menus are `packages/n8/ts/ui/{index.ts, n8Devices.ts, n8SdOps.ts}` (F4b);
`chrImage.ts` is `packages/tracker/ts/nes/chrImage.ts` and `risaSyncTranslator.ts` is
`packages/risa/ts/risaSyncTranslator.ts` (F1); what still stays is the CLI sessions and `buildGraph`'s `cart-<id>`
placement (survey B.9). `:657`'s "the TS half needs its `ts/` in `packages/retroplug/tsconfig.json`'s `include`" is
closed by F1's `../*/ts` glob (`tsconfig.json`'s include). The lines are corrected in place, with the doc pass of
section 10.

## 4. Stage 2: `n8-wire`

### 4.1 What it is

`mesen` includes two headers of `n8` and nothing else: `transport/N8ReliableLink.hpp` (`NesN8FifoRole.hpp:13`, the cart
side of BlipToaster's reliable link) and `n8/N8Node.hpp` (`MesenNode.cpp:24`, for
`rp::n8::kExpansionVolumeMin/Max/Default` at `:79-80` and `:292-293`, so the emulated chip and the real cart's register
take one range). `mesen/package.json:19` declares `@retroplug/n8` for it and `mesen/native/CMakeLists.txt:32` links
`retroplug-n8`. A pack that drives a real cart and a console that emulates the same cart's FIFO share a wire protocol
and a number range; neither owns the other.

```
packages/n8-wire/ package.json { "name": "@retroplug/n8-wire", private, description,
                                    "dependencies": { "@retroplug/node-sdk": "workspace:*" },
                                    "retroplug": { "native": { "target": "retroplug-n8-wire" } } }
native/CMakeLists.txt add_library(retroplug-n8-wire INTERFACE)
                                  target_include_directories(retroplug-n8-wire INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
                                  target_link_libraries(retroplug-n8-wire INTERFACE retroplug-node-sdk-headers)
native/src/transport/N8ReliableLink.hpp moved by git mv from packages/n8/native/src/transport/ (309 lines)
native/src/n8wire/ExpansionVolume.hpp new: the three constants
```

The reliable link keeps its include path (`"transport/N8ReliableLink.hpp"`), so its four includers do not change
(`n8/desktop/N8Link.hpp:16`, `mesen/.../NesN8FifoRole.hpp:13`, and the two tests,
`n8/native/test/N8ReliableLink.test.cpp:18` and `mesen/native/test/N8ReliableLink.test.cpp:20`). It needs
`util/FixedQueue.hpp`, which is `node-sdk`'s (`packages/node-sdk/native/src/util/FixedQueue.hpp`), hence the link, to
node-sdk's headers target (the header alone; the library is not needed for it).

`n8wire/ExpansionVolume.hpp` holds `kExpansionVolumeMin = 0`, `Max = 200`, `Default = 100` in `namespace rp::n8`, the
names they have today, moved out of `N8Node.hpp:24-26`. `N8Node.hpp` includes it, so `N8Node.cpp:26, 159`,
`native/test/engine-graph/N8Node.test.cpp:345-347` and `native/test/device-mode/DevicePlan.test.cpp:1285` do not change,
and `MesenNode.cpp:24` includes `"n8wire/ExpansionVolume.hpp"` instead of `"n8/N8Node.hpp"`. `expansionVolumeRegister`
(the percent to the cart's 0..255 register, 128 unity) stays in `n8`: only the cart's side needs it. **The name** is
`ExpansionVolume.hpp` because it is the header's one subject (the `kExpansionVolume*` constants), in a folder `n8wire/`
that cannot collide with `n8`'s own `n8/` or the engine's `transport/`; `ExpansionRange.hpp` was the alternative, and
says less. The engine-graph test that holds the two nodes' ranges equal (`N8Node.test.cpp:345-347`) becomes true by
construction and stays as the guard against a second copy.

### 4.2 What changes, by file

- **`n8`:** `native/CMakeLists.txt` gives `retroplug-n8` a PUBLIC link to `retroplug-n8-wire` (so `N8Link.hpp` finds
  `N8ReliableLink.hpp` through `retroplug-n8-desktop`'s link to `retroplug-n8`); `package.json` declares
  `@retroplug/n8-wire`; its comment at `CMakeLists.txt:4-5` (the link "the emulated NES FIFO role in packages/native
  speaks it from the cart's side") is reworded.
- **`mesen`:** `native/CMakeLists.txt:32` links `retroplug-n8-wire` and drops `retroplug-n8`, and its header comment
  (`:7-10`, "the edge runs one way: n8 imports neither this package nor the contract") says the shared package instead;
  `package.json:19` swaps `@retroplug/n8` for `@retroplug/n8-wire`.
- **Lists.** The root `package.json` desktop list gains `"n8-wire"` before `"n8"` (after `"graph-runtime"`). The package
  has no node type, no TypeScript and no entry, so the registration golden (`product-registrations.json`), the
  `[packages]` generator output and the device images do not move. `n8-wire` joins the lists that call
  `retroplug_package()` in a device build, before `n8`: `pico/n8-host/CMakeLists.txt:81-86` and
  `packages/graph-runtime/device-check/CMakeLists.txt:80-88`. The export tool's list cannot gain it: the export tool
  calls no `retroplug_package()` (its device objects are the desktop build's `<target>-device` twins, which exist
  because `n8-wire` is in the root desktop list), and its `RP_EXPORT_PLAN_PACKAGES` is a DEVICE product's list, which
  must not carry the package (stage 2's report). **It does not join a DEVICE product's `PACKAGES`**: `retroplug_product`
  refuses a device product's package with no `device` entry and no script pack (`cmake/packages.cmake:1522-1530`), so
  `RP_DEVICE_CHECK_PACKAGES` (`core dsp n8 launchpad device-io devtest`), `RP_EXPORT_PLAN_PACKAGES` and
  `N8_HOST_PACKAGES` keep their members (they are also the export tool's `--packages` names, which are node packages).
  The brief says the package "joins the device lists"; the `retroplug_package()` lists are what that can mean (section
  11, item 3).
- **The device twin.** `_retroplug_device_check` compiles a `device` target again as `<target>-device`, copying its
  `INCLUDE_DIRECTORIES` through `$<TARGET_PROPERTY:t,INCLUDE_DIRECTORIES>` (`packages.cmake:1263`), which the build
  generator evaluates against the target's USAGE REQUIREMENTS: what the target links comes with it.
  `retroplug-n8-device` compiles `N8Node.cpp`, which includes `n8wire/ExpansionVolume.hpp`, and sees `n8-wire`'s include
  root because `retroplug-n8` links `retroplug-n8-wire` PUBLIC: the twin copies the target's own directories
  transitively, and `packages/n8/native/CMakeLists.txt:23-24` names only its own `src`. Neither option this section
  first left is taken: no consumer names the wire's root in its own `target_include_directories`, and the twin's
  contract is unchanged (it copies what the target itself says, which is now its link's interface too).
- **"Device-ready"** is claimed for what can be proved: both headers compile under the device's flags
  (`-fno-exceptions -fno-rtti`, no heap in `ExpansionVolume.hpp`), by a device check over the package's headers (a
  `native` manifest form with `"device": true`, no `entry`, and a `headers` list is the sketch: the manifest reader,
  `cmake/check-manifest.cmake` and the refused-manifest cases in `tests.cmake:181-210` all need their cases, and none is
  written; section 11, item 3). **What stage 2 built instead**, and it is what the prototype holds: a header-only
  manifest (`retroplug.native` naming a target and no entry, provider or device) that the existing reader already
  accepts, and a scratch link-boundary pair in `packages/native/test/packages/refused-native/` (the directory a target
  property's fixtures live in, with the accepted twin in `read/`), not a manifest case in `refused/`. A per-header
  device check is still not built (section 11, item 3). `N8ReliableLink.hpp` uses `std::function` for its emit callback
  (`:59`), which a device image's link trap would refuse if anything on the device linked it; nothing does (`N8Link` is
  desktop-only), so "device-ready" is a compile claim and not a link-trap one, and is said that way in the package's
  description.

### 4.3 Proof

**RED first, on the real tree:** edit `mesen/package.json` (drop `@retroplug/n8`, add `@retroplug/n8-wire`) and leave
`mesen/native/CMakeLists.txt:32` linking `retroplug-n8`: the configure's native boundary check
(`retroplug_check_package_boundaries`, deferred to the end of the root configure) refuses a target that links a package
its manifest does not declare. Then link `retroplug-n8-wire` and the configure passes. As built, the fixture pair that
holds the same refusal at every configure is `refused-native/N8WireUndeclared.json` with its accepted twin
`read/MesenN8WireDeclared.json` (a link boundary is a target property, which a `refused/` manifest case cannot express),
beside the existing `MesenN8Undeclared` pair. **Oracles held by construction:** the registration golden,
`edio-golden.json` (28 cases), the device-check and box image hashes and the bliptoaster pack bytes are compared before
and after (the headers move, no byte of an object should; if an image hash moves it is a path string, and the compare
says so), `pnpm test:n8`, `test:plugin` (`retroplug-mesen-test`'s reliable-link case among them) and
`tools/build-mesen-all-consoles.sh --off`.

## 5. Stage 3: the views split

### 5.1 The contract

`@retroplug/ui/contribution` gains one identity function beside `defineUi`; `UiContribution` loses `views`.

```ts
/** What a package's views module default-exports: the window and tile views it draws. */ export interface
ViewsContribution { readonly views: readonly ViewDef[] } export function defineViews(contribution: ViewsContribution):
ViewsContribution { return contribution; } // UiContribution (contribution.ts:279-290) keeps rows, menus, trackers,
services, actions; `views` moves.
```

The manifest member is `"views": "ts/ui/views.ts"` beside `"ui"`, published at the export key `./views` (the generator
imports a contribution by the key whose target is the file: `packages.mjs:559-575`). A package may declare `views` and
no `ui` (the fixture does), or the reverse (every package but `lsdj`, `risa` and the fixture). `ViewsContribution.views`
is required (a package with nothing to draw declares no module); the generated module still reads `views.views ?? []`,
so a plain-data fixture module cannot crash it before a check speaks.

### 5.2 The generated module and where it must live

The generator reads the member as it reads `ui` (`contributions()`, `packages.mjs:262-320`) and `contributes()` counts
it. It writes `packages/retroplug/ui/generated/views.ts`:

```ts
// ui/generated/views.ts (generated) import { type ViewDef, type ViewsContribution } from "@retroplug/ui/contribution";
import views_lsdj from "@retroplug/lsdj/views"; import views_risa from "@retroplug/risa/views"; /** Each package's
views, in product order. */ export const VIEW_CONTRIBUTIONS: readonly { pkg: string; views: ViewsContribution }[] =
[ ... ]; /** The product's views, in product order, with the package each came from. */ export const VIEWS: readonly {
pkg: string; view: ViewDef }[] = VIEW_CONTRIBUTIONS.flatMap(...); // checkView(pkg, view): the checks that are in
menus.ts today (packages.mjs:829-835, 876-912), moved whole.
```

The view checks move out of `renderMenus` with their state (`viewOwners`, `viewPlacements`, `bodyKinds`, `checkView`): a
view id declared twice in one package or two; a placement that is neither `window` nor `tile`; a body `kind` that is
none of `pixels`, `readout`, `component` (or no body, which names the package rather than throwing); a `pixels` or
`readout` body with no `open`, a `component` body with no `component` function; a `visible` on a window view; and the
cross-package duplicate, which is the same map. `menus.ts` keeps the row, tracker, service, builder-placement and action
checks (the action checks stay in `menus.ts` as the brief says: they read the binding tables, which are the key maps'
and not the views'), and loses `VIEWS` and the `ViewDef` import.

**The views module is UI, and must sit under the package's `ts/ui/`.** Rule 3 admits React host modules only from
`ts/ui/` files (`check-boundaries.mjs:314-322`, `surfaceOf`), and the bundle check's `isUi` classifies a package module
as UI by that same directory (`bundle-ui-split.test.ts:65-68`). A views module outside it could import React with no
declaration being checked and would be reported as non-UI. So the generator refuses a `retroplug.views` path outside
`ts/ui/` (new check, naming the package and the path), and rule 3 gains the same refusal over EVERY package's manifest,
so an unlisted package (the fixture) is judged too. `retroplug.ui` has had no such check; all nine current `ui` entries
are `ts/ui/index.ts`, so the same refusal is added for it in the same commit (an entry that is none of those would be a
finding).

`isUi` already covers the whole `packages/retroplug/ui/generated/` directory, so `ui/generated/views.ts` is UI-only
without an edit (the brief's "names `ui/generated/views` as UI-only" is already true). What the bundle test adds is a
positive assertion that the UI bundle carries it, and `check-boundaries.mjs` needs no change for the new generated file
(rule 9's `ignores` is the `ui/generated/` prefix; its only mention of `generated/menus` is a comment at `:171`,
reworded). **As built:** the `ts/ui/` refusal is one helper in the generator (`uiMemberFile`) and a plain loop in
`check-boundaries.mjs` (the two read different manifests: the product's, and every package's), with one message shape;
`ui/generated/views.ts` is among `bundles.cmake`'s `BYPRODUCTS` (stage 3's review found it missing, and stage 4's found
`placers.ts` missing the same way); and the generator's fixture tree moved its `ts/ui.ts` to `ts/ui/index.ts`, because a
UI module outside the directory is exactly what rule 3's `surfaceOf` cannot see.

### 5.3 The shell's side

`ui/host/menuContributions.ts` splits. The view functions (`viewById`, `windowViewOf`, `windowViewOrReport`,
`tileViews`, `:25-52`) move to `ui/host/viewContributions.ts`, which imports `../generated/views`; the menu and action
functions stay and import `../generated/menus` alone. The importers: `ui/App.tsx:42` takes `windowViewOrReport` and
`windowViewOf` from the new module and the two action functions from the old; `ui/views/TileViews.tsx:16` takes
`tileViews` from the new one; `ui/screens/menu/menuDefs.ts:14-20` stops importing a view function. **That last change is
the load-bearing one:** `menuDefs.ts:120` calls `windowViewOrReport` in its fallback `hostOf` (the host a menu test
builds over the stores alone), which would pull `views.ts` into every pure-tier test that reaches `menuDefs.ts` (about
25 files). The fallback host's `views.open` therefore reports "the menu's host has no window to open <id> in" and
nothing else; the unknown-id and tile-view wordings stay where App runs them (`App.tsx:89`). No test asserts either
wording (grep over the tree), so this is not an edge a user or a test can see.

`lsdj/ts/ui/hdView.ts` and `overlayView.ts` are unchanged; `lsdj/ts/ui/views.ts` is
`export default defineViews({ views: [hdView, overlayView] })`, and `index.ts` drops the two imports (`:29-30`) and the
`views` line (`:291`), keeping `trackers` and `actions` (`lsdjDebugOverlay` is an action; the overlay's `visible` flag
is the shared `debugOverlay.ts` signal both modules import). `risa` is the same with one view (`index.ts:15, 142`).
`ui-fixture` becomes a views-only package: its manifest drops `ui` and gains `views`, its `ts/ui/index.ts` becomes
`views.ts` (`export default defineViews({ views: [badgeView] })`), and the package's `exports` publishes `./views`. Each
moved package's `exports` map gains `./views` (no wildcard: rule 8).

The extend rule F4c recorded (decision 11) stays: the generator asks a package for the generated host-modules `extends`
when it lists examples or declares a UI host module, not when it has views. F4c's note said the rule "must be
generalised to 'has views' AND risa must gain the line" once the generator could see views. It can now, and the reason
for the original rule still holds: a plain-TypeScript views module (risa's) needs no React path, and a views module that
uses React declares the React host modules, which is what triggers the extend. Left as it is, said here so it is a
choice and not an oversight (decision 16).

### 5.4 The pure-tier proof

F4c found that the first product to list a React view package would break the pure tier, because `menus.ts` imported
every `ui` module eagerly and `run-tests.mjs` bundles with no aliases and no `nodePaths` (`run-tests.mjs:120-135`). The
split removes the cause; the new check proves it, as a test that needs no product to list the fixture:

1. **A view package bundles as the UI bundle bundles.** A new `test/script/bundle-view-package.test.ts` (a Node child
   through `bundle-views.mjs`, the driver beside `bundle-split.mjs`, as the F4b bundle checks run) bundles
   `packages/ui-fixture/ts/ui/views.ts` with `uiBuildOptions` and `bundleInputs` from `tools/build-ui.js` (its aliases
   for `lvgljs`, `lvgljs-ui`, its `nodePaths` into the dpf.js `node_modules`, `jsx: "automatic"`): it must succeed, and
   its inputs must include `packages/ui-react/` and a `react` module.
2. **The pure tier never reaches it.** The same driver bundles `ui/host/menuContributions.ts`, the real
   `ui/generated/menus.ts` and, as the third pure entry, `ui/screens/menu/menuDefs.ts`, with the pure runner's own
   options (`platform: "neutral"`, no alias): they must succeed and their inputs must contain no `ui/generated/views`,
   no package's views module and nothing under `packages/ui-react/`. The third entry is stage 3's review's: with only
   the first two, a re-import of a view function in `menuDefs.ts` passed every check, and the entry's RED is 315 modules
   with three views reaching the pure tier.
3. **The cause, held RED.** A generator fixture tree (`test/packages/run-generator.mjs`, which already bundles the
   generated `menus.ts` with aliases, `:200-210`) lists a package whose `views` module imports `react`: the generated
   `menus.ts` bundles with the pure options, and the generated `views.ts` does not (`Could not resolve "react"`),
   asserted as the expected failure so the split cannot be undone silently. The RED for the whole stage is this case run
   against the pre-split generator with the fixture's React view in its `ui` module: the menus bundle fails.

The generator's nine `views-*` fixture cases (`run-generator.mjs:510-690`) are repointed at the views module and the new
`views.ts` expected output, and gain the out-of-`ts/ui/` refusal; `test/packages/fixtures/expected/` gains `views.ts`.
As built, the fixture package `zeta` is views-only (the generator's `views` mode, RED with the deletion off), so a
package with `views` and no `ui` is run through the generator, and the fixtures share one alias map
(`fixtureAliases.mjs`) where two drifting copies stood.

### 5.5 Docs

`spec/06-build-test.md`'s "Adding a package" gains the member in item 2's list and a sentence in item 5 ("a package with
`retroplug.views` exports the module at `./views`; it must be under `ts/ui/`"); `spec/00-overview.md:94`'s `ui` row
names `defineViews`; AGENTS.md's `retroplug.ui` paragraph gains `retroplug.views`.

## 6. Stage 4: the shell free of the Launchpad

### 6.1 Project sections

`@retroplug/core/definePackage` gains one contribution list, zod-free (a section's `schema` is anything with a `parse`,
as `ControllerApp.config` is today):

```ts
export interface ProjectSection<T = unknown> { /** The key under `settings` in a .rplg ("controller"). Never one of the
shell's own (layout, midiRouting, audioRouting,
   * zoom): the generated registry refuses it, and a key two packages declare; `__proto__` is refused by the writer. */
readonly key: string; /** Raw, migrated value -> the section: defaults filled, values clamped, never throws on junk. */
readonly schema: { parse(raw: unknown): T }; /** What a project that never set the section reads (a fresh project, a
missing key). */ defaults(): T; /** When the section ever needed a migration: the stamp this build writes, and steps
keyed by FROM-version as
   * migrate.ts's MigrationMap is. Absent = additive only (the controller). */
readonly version?: number; readonly migrations?: Readonly<Record<number, (raw: Record<string, unknown>) =>
Record<string, unknown>>>; /** How setSection's patch lands; the default is a shallow merge. It may throw to refuse a
patch. */ merge?(current: T, patch: Readonly<Record<string, unknown>>): T; } // PackageContributions gains: readonly
projectSections?: readonly ProjectSection[];
```

**In the shell.** `ProjectSettings` becomes
`{ layout, midiRouting, audioRouting, zoom, sections: Readonly<Record<string, unknown>> }`; the `controller` field,
`ControllerSettings`, `DEFAULT_CONTROLLER_APP` and the schema's `controller` entry leave `projectConfig.ts`. The `.rplg`
shape is unchanged: `buildConfig` spreads the sections at the top level of `settings`
(`settings: { ...core, ...sections }`), and `parseConfig` reads them back, so `controller` stays at its key. After the
core fields parse, every other key of the raw `settings` object is a section: through its contributor's migration chain
then `schema.parse` when a package contributes it, kept verbatim as raw JSON when none does; a contributor's key absent
from the file is filled by `defaults()`. Typed read: `projectSection<T>(settings, key): T` (the parsed value for a
contributor, the raw one otherwise). `ProjectStore.settings()` (`:251`) returns the same shape; `newProject` and `load`
(`:606, :823`) spread `DEFAULT_SETTINGS` as before.

**Migrations.** The root keeps `K_PROJECT` and `PROJECT_MIGRATIONS` (`projectConfig.ts:299-305`): a breaking change to
the project's own fields still bumps it and adds one raw step, applied first over the whole document. A section is then
migrated by its package: `migrateRaw(section, stampOf(section), contributor.version, contributor.migrations)` with
`migrate.ts`'s existing function, before `schema.parse`, so the invariants of AGENTS.md hold (raw `(obj) => obj` steps,
only the latest schema, steps idempotent-safe). An UNSTAMPED section reads as version 1, the shape before any stamp, so
a section that starts unversioned can still migrate: the first versioned release declares 2 and adds one step keyed 1
(stage 4's decision, correcting this section). The stamp is a numeric `schemaVersion` inside the section object, which
`buildConfig` writes when (and only when) the contributor declares a `version`; a section stamped newer than its
contributor's `version` is the refuse-newer rule at section scope: kept verbatim, not parsed, and read-only for the
session (`setSection` refuses it with one warning naming the key and both versions), while the rest of the project
loads. The controller declares no `version`, so nothing is stamped, its bytes are unchanged, and "same key, same
contents, no migration" holds. Where the stamp lives is decision 12.

**As built, the key rules and the two functions.** `assertSectionKey(key)` (in `projectConfig.ts`) refuses a core key
and `__proto__`, each named in its throw, and `ProjectStore.section` and `setSection` call it first; `buildConfig` also
filters core keys out of the sections it spreads (a writer that bypassed `setSection` cannot make the file's `zoom` an
object); the generated registry refuses a core key and a key two packages declare, and `__proto__` is the writer's alone
(a manifest's member of that name is not its JSON's to declare). `parseSections` stores each section with
`Object.defineProperty`, so a hand-edited `__proto__` key is kept as DATA (E5's rule) and does not become the settings
object's prototype and vanish on the next save. A stamp is read through `readSectionVersion` (an unstamped block is
`SECTION_BASELINE_VERSION`, 1). `parseProjectSection` and `serializeProjectSection` are exported and take the
contributor explicitly: the controller declares no version, so the versioned half (a chain, a stamp, a block stamped
newer kept verbatim) has no product to be tested through, and `test/project/sections.test.ts` holds it with a synthetic
contributor (the unstamped-is-1 case RED first).

**The controller's section** moves to `packages/launchpad/ts/control/projectSection.ts`: the schema at
`projectConfig.ts:95-106`, written out by hand and not in zod (the pack declares no zod, and the contract asks only for
a `parse`; the coercions are the old field helpers', held by `test/project/tolerance.test.ts` and the recorded matrix)
(its preprocess that turns a malformed block into `{}`, `enabled`, `app`, `target` over `CONTROLLER_TARGET_VALUES`,
`systemId` clamped, the opaque `appConfig` record), `defaults()` with `app` defaulting to the registry's first app (the
registry is the package's own, 6.5), and `merge`, which is `ProjectStore.setController`'s body (`:593-601`): a patch
naming a different `app` starts from the patch's `appConfig`, the same app merges one knob at a time, and an unknown
`target` THROWS naming the value (today's refusal, `uiHost.ts:76-77`, moved to where the closed enum lives).

### 6.2 Project services

The controller's control-plane half is about 140 lines of `ProjectStore` (6.6) that read the battery and the WRAM,
decode the song's timing, notice the cart start on its own, and hand the graph a model. They move into the package
behind a contract. Because a plugin host runs one control plane per instance and the store is per instance (the
multi-instance hazard class of the project's memory), the contribution is a per-store factory, not a module singleton:

```ts
export interface ProjectServiceFactory { readonly key: string; create(): ProjectService } export interface
ProjectService { /** One poll, on the cadence the UI's song watch calls it at (every ~30 frames, editor-driven:
useSongWatch.ts:19-28).
   * True = something the graph reads moved: the store publishes and notifies. */
poll?(ctx: ProjectServiceContext): boolean; /** The model the graph places for this section (GraphModel.models[key]), or
null for none. Called at every publish. */ graphModel?(ctx: ProjectServiceContext): unknown | null; /** What a UI may
read of the service (the controller cart's polled SYNC), through host.project.serviceState(key). */ state?(ctx:
ProjectServiceContext): unknown; } export interface ProjectServiceContext { section<T = unknown>(): T; // this service's
section, parsed systems(): readonly { id: number; romPath: string; roles: readonly RoleInstance[] }[]; // systems.view()
readSram(id: number): Uint8Array | null; // systems.readSram / backend.readSram (:204, :493) readRam(id: number):
Uint8Array | null; // backend.readRam (:518) readFilePrefix(path: string, bytes: number): Uint8Array | null; //
backend.readFilePrefix(rom, ROLE_HEADER_LEN) (:543) playbackOf(roles: readonly RoleInstance[]): TrackerPlayback | null;
// resolveTracker(roles)?.playback (:533) } // PackageContributions gains: readonly projectServices?: readonly
ProjectServiceFactory[];
```

That context is everything the code uses today, enumerated: the systems list and each system's id, ROM path and roles
(`controllerModel`, `refreshControllerSong`, `controllerReader`), the battery read (the timing table and the song
signature), the WRAM read (the start-edge anchor), the ROM header prefix (the playback reader, built once per ROM), and
the tracker integration's `playback` hook. `ProjectStore` builds one instance per factory at construction, calls `poll`
from a new `pollServices()` (the one call `useSongWatch.ts:27` makes instead of `refreshControllerSong`; when any poll
answers true it does today's `publishGraph(false); notifySystemsChange()`), and `publishGraph` (`:164-181`) puts each
`graphModel` into `GraphModel.models[key]`. State a service keeps (the song signature, the anchor, the polled SYNC, the
edge detector, the reader cache) is its instance's and is not reset by `newProject()`, as the fields are not today
(`:603-615`). `controllerStartAnchor()` and `controllerCartSync()` (`:551, :558`) become the service's `state()`; the
tests that call the first (`test/controller/songRefresh.test.ts:184-218`) read it through `serviceState`. **As built,**
the service is `packages/launchpad/ts/control/projectService.ts`, `serviceState` is typed `T | undefined` and returns
the service's own `state()` object (`{ cartSync, anchor }` for the controller), which a UI reads by the fields it knows
and the shell never names, and `ProjectService` and its context are `@retroplug/tracker/projectService`, which the shell
imports by that entry (`SHELL_IMPORTS` swaps `tracker/trackerPlayback` for it).

**Where the context type lives** is a placement question: it names `TrackerPlayback`, which is `@retroplug/tracker`'s,
and core cannot import tracker. The draft puts `ProjectService`, its context and the `projectServices` key in
`@retroplug/tracker` (`packageContributions.ts` already augments `PackageContributions` with `trackerIntegrations`, and
`trackerPlayback.ts` is the contract the poll reads), and `ProjectSection` in core, which needs no tracker type
(decision 13).

### 6.3 Graph placers

`buildGraph` hard-codes the controller's four contributions to a document: `surface-in` right after `midi-in` (`:404`),
the `controller` node and its three surface wires after the routing node and its wire (`:421-435`), the first slot of
host MIDI out's inputs (`:434`), the `launches` wire into each system's sync node (`:454-455`), the forced sync mode on
that node's settings and on its serial-out twin (`:438, :457-461`) with the Game Boy's capture following it
(`:471-483`), and `surface-out` after host MIDI out (`:514-517`). Each becomes a slot a package's placer fills. Document
ORDER is behaviour (the planner breaks ties by document order, and the goldens hold it), so the slots are positions in
`buildGraph`'s own sequence and not a bag of nodes:

```ts
// @retroplug/core/placer (types and an identity function; imports no node code, no zod) export interface Placed {
readonly nodes: readonly NodeDoc[]; readonly wires: readonly WireDoc[] } export interface PlaceSystem { readonly id:
number; readonly type: string; readonly pipeline: readonly ProtocolRole[] } export interface PlaceContext { readonly
systems: readonly PlaceSystem[]; // slot order readonly kindData: (kind: string) => Readonly<Record<string, unknown>>;
// generated protocol.ts's KIND_DATA, 6.5 } export interface Placement { readonly inputs?: Placed; // after midi-in,
before routing: surface-in readonly session?: Placed; // after routing and its wire: controller + its surface wires
readonly outputs?: Placed; // after host MIDI out and its wires: surface-out /** Host MIDI out's first inputs, ahead of
every system's, so they win a tie at a frame. */ readonly hostMidi?: readonly { readonly from: string; readonly system:
number }[]; /** Settings merged over a system's protocol node of a type, and over its role's second node. */ readonly
overrides?: readonly { readonly system: number; readonly node: string; readonly settings: Readonly<Record<string,
unknown>> }[]; /** Wires to add beside a protocol node's own (called after its routing wire, before its writes): the
launches. */ wires?(system: number, role: PlaceRole, spec: PlaceSpec, nodeId: string): readonly WireDoc[]; // nodeId:
the protocol node's own id } export interface GraphPlacer<M = unknown> { readonly key: string; // the section / model
key: "controller" normalize?(model: M): M; // the app config's parse, which DirectGraph shares (below) place(model: M,
ctx: PlaceContext): Placement; }
```

`buildGraph(model)` walks `model.models` in product order and, for each key with a placer and a non-null model, calls
`place`; the six places above read its slots at the positions they have now. The package's placer module is a new
manifest member under `protocol` (`"protocol": { "module", "nodes", "placer": "ts/control/placer.ts" }`),
default-exporting `definePlacer({ ... })`, and is a module of its own and NOT the `protocol.module` that exports the
`DefinedNode`s (as built, `packages/launchpad/ts/control/placer.ts`, beside the app module for the scanner's sake:
decision 25). The brief says "in a package's protocol module"; the node module is script-node code, and
`generated/protocol.ts` is imported by the planner precisely because it "imports nothing but a type, so a bundle of
buildGraph carries the tables without a single node's code" (`bundle-protocol.test.ts` holds it). The placer imports
only types and constants, so the new generated `src/generated/placers.ts` (the list of `{ pkg, placer }`, one import per
declaring package) stays inside that rule, and the planner bundle test grows an assertion that it carries no zod and no
node module with the placers in (decision 14). **As built,** `bundle-protocol.test.ts` refuses a node module BY NAME: it
reads the product list and every manifest's `protocol.module`, plus core's `defineNode.ts`, and `forbidden()` refuses
any of them, with the module ceiling at 22 (the real count is 21, at 34,075 bytes, under the 64 KB byte ceiling). The
stage 4 review's reproduction, a placer importing `../nodes`, passed a ceiling of 30 and now fails on the name (RED
shown).

**What moves into `launchpad/ts/control/placer.ts`:** the surface node type ids (`rp.launchpad.surface-in/-out`,
`buildGraph.ts:404, 515`), `CONTROLLER_SESSION` (`:111`; the package already exports `CONTROLLER_SESSION_NODE_TYPE` from
`controllerSession.ts`), `controllerSettings`, `controllerTarget`, `NO_SYSTEM`, `controllerSyncOverride` and
`syncModeFor` (`graph/controller.ts`), and `GraphController` with `ControllerSyncRule`, which lives in the pack's placer
ALONE (stage 4 first declared it in core, because the generated `protocol.ts` typed `CONTROLLER_SYNC` with it; the
generated module types `KIND_DATA` with core's `KindData` instead, so a product that lists no pack imports no pack's
type, and the stage 4 review had it deleted from core). The sync rule per app is manifest data (`CONTROLLER_SYNC` today,
`generated/protocol.ts:37`; `KIND_DATA` in 6.5), reaching the placer through `ctx.kindData`. `serialOutCaptureFor` and
`SerialOutCaptureMode` stay in the shell (they read `PROTOCOL_NODES`, which is generic) and move to
`graph/serialCapture.ts`.

**"Capture follows the forced mode" stays generic.** `buildGraph` applies a placement's overrides when it makes a
protocol node's settings (`:445-447`) and its second node's (`:473-477`), and derives each Game Boy's `forced` mode from
the same overrides (`:487-490`): `forced` is the `mode` setting of the override whose `node` type the system's pipeline
places, which is what `controllerSyncOverride`'s `sync.mode` is today, because `mode` is the key the manifest's
`capture` table is keyed by. The existing test that holds it (`test/graph/build-graph.test.ts:408-470`, the `forced` and
capture cases) is the equivalence proof. `DirectGraph` takes the placers' `normalize` (the app config parse that
`setController` runs through `parseAppConfig`, `directGraph.ts:114-117`) and keeps a `models` record in place of
`controller`; its `setController(model | null)` becomes `setModel(key, model | null)` (callers:
`test-native/lsdj-launchpad-role.test.ts:60, 104`, `test-native/lsdjPlaybackProbe.ts:155`,
`test/systems/store-graph.test.ts:1301, 1311`). `DirectGraph` imports the generated registry (`../generated/packages`)
for its side effect, which is what fills a kind's registry before it normalizes a model: without it an app's config
parsed to `{}` silently (the stage 4 review's `norm.ts` reproduction, now `test/graph/direct-graph.test.ts`, RED shown).
A module that reads a kind-filled registry imports the generated registry, and `projectConfig.ts` says so generically.
`Placement.wires` is handed the shell's `ProtocolNodeSpec` structurally, through core's `PlaceSpec` (core cannot name
the shell's type; the one member a placer may read is `launches`), and the protocol node's own id, so the pack no longer
builds the shell's `sys-<id>/<kind>` convention itself.

**The oracle.** The build-graph goldens record, per case, `model` (the input) and `document` (an index into
`documents`): `golden/build-graph.json` has 298 documents over 840 cases and `build-graph-roles.json` 36 over 37. The
`documents` are what must be byte-identical, and every case that holds a controller (`"controller"` appears in both
files) proves the slots land at the same positions. The recorded `model` member carries a `controller` field in the
pre-split vocabulary; the role-golden runner (`build-graph-role-goldens.test.ts`, the one whose cases record a
controller model) translates it to `models.controller` on load, so the files stay byte-identical and nothing is
re-recorded. **The RED:** a placer slot moved by one position failed 12 controller cases. If the prototype prefers to
re-record the `model` vocabulary, only that member may move, never `documents`, `slots` or `tapArm` (decision 15).

### 6.4 The `project` host group

`core/ts/host.ts` loses `ControllerApp`, `ControllerAppField`, `ControllerSettings` and `ControllerHost`
(`:90-125, 262-274`), and `HostGroups.controller` (`:295`) becomes:

```ts
export interface ProjectHost { /** The section as its contributor parses it (raw when no package contributes `key`). */
section<T = Record<string, unknown>>(key: string): T; /** Apply a patch through the contributor's `merge`, mark the
project dirty, publish the graph and notify. It throws when
   * the contributor refuses the patch, and does nothing for a section stamped newer than its contributor. */
setSection(key: string, patch: Readonly<Record<string, unknown>>): void; /** What a project service published (the
controller cart's polled SYNC), or undefined for no such service. */ serviceState<T = unknown>(key: string): T |
undefined; }
```

`UiHost`'s group list (`ui/ts/contribution.ts:19-21`) swaps `"controller"` for `"project"`; `createUiHost`
(`uiHost.ts:48-95`) builds the group over `ProjectStore.section/setSection/serviceState`, and `isControllerTarget`, the
two `@retroplug/launchpad/enums` and `src/controller/registry` imports and the `controllerSyncOverride` import leave it.
`serviceState` is an addition to the brief's `section` / `setSection` pair: the Launchpad's submenu reads the cart's
polled SYNC (`ui/index.ts:110`, `host.controller.cartSync()`) and the override's driving flag (`:113`,
`host.controller.syncOverride()`), and with the service in a package the UI has no other door to the polled value. The
menu then computes the override itself, from `host.project.section("controller")`, `serviceState("controller")` and
`host.systems.view()`, with the package's own `controllerSyncOverride`. The Launchpad's menus use the group
(`ui/index.ts:93, 101, 110, 113, 114, 153, 155, 161, 267`'s `host.controller.*`); `host-groups.test.ts:49, 156-163` and
`launchpad.test.ts:551-583` move to `ui.project`, the throw on an unknown target asserted against the section's `merge`.

### 6.5 Contribution kinds

`controllerApps` is the one contribution kind a package declares for other packages to fill (`launchpad` declares it,
`lsdj-launchpad` contributes `MI_MAP`), and its mechanism is hard-coded in the generator three times: the type import in
the generated `packages.ts` (`packages.mjs:600`), the `CONTROLLER_APP_SOURCES` / `CONTROLLER_APPS` lists and the pairing
checks with the manifests' runtime halves (`:660-700`), the `registerControllerApps` import and call in the generated
script entry (`:1341, :1440`), and `CONTROLLER_SYNC` in `protocol.ts`. spec/06 item 7 says a new kind "is not data: it
means editing `renderPackages`". After this stage it is data. The declaring package's manifest says what the kind is; a
contributing package says what it contributes:

```jsonc
// packages/launchpad/package.json "retroplug": { "kinds": { "controllerApps": {
    "control": "ts/control/kind.ts",          // control-plane half: default-exports defineKind({ accept, check? })
    "runtime": { "module": "ts/apps/index.ts", "register": "registerControllerApps", "type": "ControllerAppRuntime" },
    "data": ["syncOverride"]                  // the keys a contributing entry may carry, passed through as data
} } } // packages/lsdj-launchpad/package.json: protocol.apps becomes protocol.kinds.controllerApps, same entries
"protocol": { "kinds": { "controllerApps": [ { "id": "lsdj-midimap", "module": "ts/appRuntime.ts",
                                              "syncOverride": { "node": "rp.lsdj.sync", "mode": "midiMap", ... } } ] } }
```

The generated `packages.ts` collects each kind's control-plane items from the packages' app modules in product order
(`...(app_x.controllerApps ?? [])`, by the declared kind id, typed from the declaring package's augmentation of
`PackageContributions`, which `launchpad/ts/packageContributions.ts` already is), and hands each declaring package its
list through its `control` module's `accept(items)` at load: the Launchpad's `accept` fills its own `ControllerRegistry`
singleton, so `src/controller/registry.ts` is deleted and `DEFAULT_CONTROLLER_APP` is `registry.list()[0]?.id ?? ""`
inside the package. The generated module runs the refusals it runs today, generically: a duplicate item id, naming both
packages or the one that listed it twice (an item's id is its `id`; the first design had a `defineKind` `idOf`,
dropped), an item with a control half and no runtime half and the reverse (the manifests' runtime ids against the
items'), and the declaring package's own `check` over the data. The generated script entry imports the declaring
package's runtime `register` function and calls it with the contributing manifests' modules in product order, keeping
the registered-set-equals-manifests check; `CONTROLLER_SYNC` becomes `KIND_DATA` in `generated/protocol.ts`
(`{ controllerApps: { "lsdj-midimap": { syncOverride: {...} } } }`), data and types only, which the placer reads through
`ctx.kindData` and the packages' `accept` receives beside the items (the UI's override row needs the same rule). The
generator keeps validating only what it can without running TypeScript (shape, duplicate ids); the node-type existence
check `syncRule` does today (`packages.mjs:225-260, 1508-1516`) moves into the Launchpad's `check`, run at load in the
generated module with the product's node types handed to it, as F4b moved the other checks into generated TypeScript.
`ControllerAppContribution` and its 12-line sketch stay as they are in `launchpad/ts/registry.ts`.

**As built.**

- **The contract** is `@retroplug/core/defineKind` (`KindItem`, `KindData`, `KindCheckContext`, `ContributionKind<I>`
  with `accept(items, data)` and `check?(items, data, ctx)`, and `defineKind`), importing nothing above core. A
  declaring package owes an augmentation of `PackageContributions` for the list its app module carries, or the generated
  registry fails `tsc` at a line that names no manifest. A kind named after one of the app module's own members
  (`roles`, `romProviders`, `embeds`, `projectSections`, `trackerIntegrations`, `projectServices`, `kitFormats`) is
  refused with its own message (RED shown with the check disabled).
- **The pack's side** is `launchpad/ts/control/kind.ts`: `accept` registers every control half in the pack's own
  `ControllerRegistry` and hands the manifests' data to the menu's table (`setControllerSyncData`), the one table the
  menu and the graph both read through `ruleOfData`. ALL of the `syncOverride` validation is the pack's `check`, not
  only the node-type existence: the generator knows no meaning of `syncOverride` and keeps the generic rules only (the
  declaration's and the entries' shape, a data key its declaring manifest does not list, a duplicate entry id per kind).
  The pack's REAL check has its own test (`launchpad/ts/test/kind.test.ts`, 8 cases, the review's two-refusals mutation
  RED), and the generator's fixture kinds (`widgets`, declared by the fixture `alpha`, and a stub `controllerApps` kind)
  hold the mechanism without naming the Launchpad. Writing that test found a real defect: the node-type check sat after
  the `listening === undefined` early return, so the common shape never reached it.
- **`nodeTypes` is the WIRED set**, `Object.keys(PROTOCOL_NODES)`, because only the data module carries node types into
  the control plane without a node's code and a mode is set on a node the graph places. The old generator's check
  accepted a type the graph does not wire (`rp.midi.route`, `rp.controller.session`); the kind refuses one, and the
  wording changed from `names the node rp.x, which no package declares` to
  `names "rp.x", which no package declares a protocol node for`.
- **`KIND_DATA` is the one carrier** of the data in the generated protocol module (data and types only, importing
  `KindData` alone); `buildGraph`'s adapter is gone and its `kindData` reads it. The generated item locals are
  `item_<kind>_<pkg>_<id>`, collision-checked.
- **`src/controller/registry.ts` is deleted.** The module that needs the registry filled imports the generated one
  (`DirectGraph`, `projectConfig.ts`: 6.3).

### 6.6 What the shell loses

After this stage: no `@retroplug/launchpad` import in `packages/retroplug/{src,ui,cli}` (the generated modules emit what
the product's packages declare, and are not judged); `SHELL_IMPORTS` loses `launchpad/enums`, `launchpad/registry` and
`tracker/trackerPlayback` (an entry no import uses fails the check, `check-boundaries.mjs:943-958`) and gains
`core/defineKind`, `core/placer` and `tracker/projectService`, the three contracts it now reads a package through,
`UI_IMPORTS` loses `launchpad/enums`; `buildGraph.ts` spells no `rp.launchpad.*` or `rp.controller.*` type (its header
comment and `audioDriver.ts:56, 129`'s comments are reworded); `graph/controller.ts`, `controller/registry.ts` and
`ui/host/uiHost.ts`'s controller group are gone or reduced. **`packages/retroplug/package.json` keeps
`@retroplug/launchpad` and `@retroplug/lsdj-launchpad`**, which the brief says it drops: the generator refuses a product
package the shell does not declare ("a package that contributes to the app is the shell's dependency",
`packages.mjs:203-216`, spec/06 item 3), because the desktop's generated modules import them by name through the shell's
`node_modules`, and 16 shell tests import them (`test/menu/launchpad.test.ts`, `test-native/lsdj-launchpad*.test.ts`
among them). What F5a proves is that no shell SOURCE names them, which is rule 9, the literal scanner and the bundle
checks (decision 10).

### 6.7 The backend's Launchpad link moves into the pack

`retroplug-backend` links `retroplug-launchpad` (`native/CMakeLists.txt:145`) and `packages/native/package.json`
declares `@retroplug/launchpad`, for one reason: the engine's RPC harness stages control-surface messages through a
concrete `rp::launchpad::SurfaceLink`. `HarnessSurfaceLink` (`packages/native/src/host/rpc/HarnessSurfaceLink.hpp`)
implements it over an SPSC ring with no device behind it; `EngineRpcService.hpp:12, 149, 217` holds one
(`setHarnessSurface`) and `stageControllerIn` (`EngineRpcService.cpp:557`) and `renderBlocks`' `controller` field
(`:648`) push into it. The class is mounted in two places: the headless host
(`packages/native/src/main.cpp:40, 118, 211-212`, which installs it on the engine by the device name) and one
engine-graph test (`test/engine-graph/EngineGraph.test.cpp:42, 318`). The plugin and the SDL standalone install real
links; the CLI, the Node addon and the render host mount none, and refuse controller traffic outright
(`audioDriver.ts:55-64`). So the dependency is a test harness's, carried by every product.

The move: `HarnessSurfaceLink.hpp` goes to `packages/launchpad/native/src/launchpad/harness/HarnessSurfaceLink.hpp`
(namespace `rp::launchpad`, no longer `rp::host`), in a header-only target of its own, `retroplug-launchpad-harness`
(`INTERFACE`, linking `retroplug-launchpad` for `SurfaceLink.hpp` and `retroplug-node-sdk-headers` for `SpscRing.hpp`,
the header alone; declared below the pack's device early-return, so a device build defines none), not in the portable
`retroplug-launchpad` (a `device: true` target that must stay device-ready) and not in `retroplug-launchpad-desktop`
(ports and threads, which an in-process ring has neither of). The pack's existing test support
(`packages/launchpad/native/test/FakePorts.hpp`) is the model: `retroplug-host`'s include path already reaches that
directory (`native/CMakeLists.txt:176`). `retroplug-host` and `retroplug-engine-graph-test` link the new target.

`EngineRpcService` loses the concrete type and gains a hook in the engine's own vocabulary,
`std::function<bool(std::span<const std::uint8_t>)>` set by `setControllerStaging(...)` (what `setHarnessSurface` is
now), which `stageControllerIn` and `renderBlocks` call; unset it answers false, as an unset surface does today. The
host's `main.cpp` wires `harnessSurface.queueInput` into it. `BackendRpcRegistration.hpp:78`'s method registration and
the TS side (`audioDriver.ts`'s `stageControllerIn` and `RenderBlocks.controller`) are generic already and do not
change; their comments that name `rp.launchpad.surface-in` are reworded. Then `packages/native/package.json` drops
`@retroplug/launchpad` and the CMake line `native/CMakeLists.txt:145` goes. This is the user's choice A (decision 8).
The stage is **stage 4**: it is the controller's move out of the shell's neighbourhood, it must land before stage 5's
plugin list can omit `launchpad`, and the tests that prove it are the controller's own.

**RED first.** Remove `@retroplug/launchpad` from `packages/native/package.json` before moving anything: the configure's
native boundary check (`retroplug_check_package_boundaries`) refuses the backend's link to `retroplug-launchpad` and the
`HarnessSurfaceLink` include, which is the RED; moving the header and adding the hook turns it green. The RED this
section first expected could not be run as written: the check exempted a shell outright, so the property was made a
GENERIC rule instead (stage 4's review, and the decision recorded with it): `packages/native`'s non-product, non-test
targets are judged by `packages/native/package.json` like any package's, so a link or an include of a target of a
package it does not declare is refused, and `cmake/packages.cmake` names no pack. The one native target judged against a
product list instead of its own manifest is the plugin's own static library, whose link is its product's list
(`RP_BOUNDARY_PACKAGES`); a target whose reach its directory manifest does not describe states it where it is made
(`RP_BOUNDARY_DECLARED`: the box link's test objects reach the N8 pack and nothing else). That check replaces a link-map
read of the plugin's binary: the plugin's link line names no Launchpad target because the backend names none and the
plugin's product list names none, and the native check says so at configure.

**What the generic rule needed to be true, as built** (each a hole the review named or the tree exposed): the genex
forms `$<TARGET_PROPERTY:t,INTERFACE_INCLUDE_DIRECTORIES>` and `$<TARGET_OBJECTS:t>` are judged by `t`'s owner (the
second only after the fix pass: stage 4's own commit message claimed it while `_retroplug_unwrap_item` reduced it to a
bare name read as a path in the target's own directory, so it passed silently; the gather cases
`includes-target-property`, `objects` and `objects-declared` hold them, RED shown); the source rule gained the
declared-package allowance the link and include rules already had; `native`'s `devDependencies` name the packs its test
binaries reach (`launchpad`, `n8`, `device-io`, `devtest`, which the lockfile gains); and three targets are vouched for
where they are made, each with the reason it is not judged against native's manifest:
`retroplug-ts-packages[-<product>]` (the shell's generator), `retroplug-box-usb-cdc` (the box link's test objects,
`RP_BOUNDARY_DECLARED n8`) and `retroplug` (the plugin's own library, `RP_BOUNDARY_PACKAGES plugin`, with `plugin.cmake`
restating the Launchpad-free property over it and the formats; REDs: `retroplug-testing` linked into `retroplug`, and
`retroplug-launchpad` into the box objects, are refused). `RP_BOUNDARY_SKIP` is left for generated code that is nobody's
(the device twins, a script pack's registration). The RED the design first named is shown on both halves: with the
backend's old link restored the configure fails
`retroplug-backend links retroplug-launchpad, a target of the launchpad package, which native does not declare`, and
with the include idiom added it fails `takes the include directories of retroplug-launchpad`. An unset staging hook has
its own engine-graph case (`stageControllerIn` answers false, a block carrying controller messages is refused in the
renderer's words, the same block without one renders): the +1 case of 337.

**Held green:** the C5 harness cases (the `[surface]` and controller cases of `retroplug-engine-graph-test` over
`HarnessSurfaceLink`, and `[protocol-nodes]`' controller-session cases), `pnpm test:native`'s controller cases
(`launchpad-facet`, `launchpad-devices`, `lsdj-launchpad`, `lsdj-launchpad-role`, `parity/controller`),
`retroplug-launchpad-test` and the headless host's scripted-device wiring (`main.cpp:163-227`). The case list, recorded
from the sources before the move and held after it: `[launchpad]` 12 cases (198 assertions), `[protocol-nodes]` 12
cases, `[devices]` 20 cases, and in `test:native` `launchpad-facet` 8, `launchpad-devices` 5, `lsdj-launchpad` 2,
`lsdj-launchpad-role` 4 and `parity/controller` 2 (with fixtures and `RP_FAIL_ON_SKIP=1`).

## 7. Stage 5: per-product bundles

### 7.1 Product lists and the generator

The root `package.json`'s `retroplug.products` gains one list:

```jsonc
"products": { "desktop":
["core", "launchpad", "system", "sameboy", "dsp", "mgb", "lsdj", "lsdj-launchpad", "graph-runtime",
              "n8-wire", "n8", "mesen", "smsggdj", "risa", "host-io", "native", "bliptoaster"],
"plugin": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime",
              "n8-wire", "n8", "mesen", "smsggdj", "risa", "host-io", "native", "bliptoaster"]
}
```

A list is still the dependency order and the registration order. `generate({ root, product, buildDir })` (`desktop` when
no product is named; `--product`, `--build-dir` and `--products-dir` on the command line) validates a list as the
desktop's is validated (`generate()` at `packages.mjs:1479-1500`: names, no duplicate, no shell; `order()`: a package
after its dependencies, a contributing dependency present, the shell declaring what it imports). Three things the brief
asks for are refined by the code:

- **"Every dependency of a listed package present in the same list"** cannot be taken literally: it would refuse
  `node-sdk`, `testing`, `ui`, `ui-react` and `tracker`, which every list leaves out because they contribute nothing the
  list carries (the existing rule is "present when it contributes", `packages.mjs:157-165`). It stays as it is for every
  list, and a new rule is added: a non-desktop list is a subsequence of `desktop` in desktop's relative order, so the
  registration order and the set of packages the configure adds (`CMakeLists.txt:398` adds the desktop's) cannot
  disagree.
- **`native` no longer blocks the plugin list.** `packages/native/package.json` declared `@retroplug/launchpad`, which
  `order()` would have refused for a list without it; 6.7 removes the dependency, so the rule needs no exemption (the
  alternatives, an exemption for packages with no TypeScript contribution or keeping `launchpad` in the plugin list,
  were rejected: decision 8).
- **Span labels.** `nodeTable` (`packages.mjs:460-490`) refuses labels that do not run "from 16 with no gap", and the
  Launchpad holds label 22 (`rp.controller.session`) between `rp.lsdj.sync` (21) and `rp.lsdj.serial-out` (23), so the
  plugin's list has a hole at 22 and is refused. The labels are positional at run time, not stored: the host numbers the
  script nodes it is handed in registration order (`ScriptHost::spanLabelFor`, `ScriptHost.cpp:2211-2220`), and the
  generated script entry already checks each node is at `FIRST_LABEL + its index` (`packages.mjs:1388-1410`). So a
  product's labels are its positions: in the plugin `rp.lsdj.serial-out` is 22. The draft keeps the strict no-gap rule
  for the `desktop` list (the union, and the numbers spec/08 and the manifests speak), requires every other list to
  preserve desktop's relative label order, and emits per-product LABELS as positions. The only visible effect is a trace
  name's number (E6). Decision 9.

**As built,** the shell is skipped by `order()`'s "a contributing dependency the shell declares" rule for a NON-desktop
list only (a plugin list may drop a contributing package deliberately), so the desktop, the union, keeps the old refusal
of a shell dependency it does not list, held by a `shell-unlisted` generator case (the first pass skipped it for every
list and the review found nothing held it). The subsequence rule refuses a duplicate by name (`lists <pkg> twice`) as
well as an out-of-order package, in the generator (five fixture lists, four RED on the old generator) and, as a function
`retroplug_check_product_lists()` in `cmake/packages.cmake` so a fixture project can hold it, at the root configure (its
first form let a duplicate through, since a duplicate sits at the same index). CMake cannot enumerate a JSON object's
keys, so `packages/retroplug/scripts/lib/product-lists.mjs` reports the manifest's lists and the generator validates
them. The plugin list carries `n8-wire` by hand: nothing refuses its absence, and a consumer that links the wire would
fail to resolve its headers (a hand check, said plainly).

### 7.2 What the product owns

**Generation.** `desktop` keeps writing `src/generated/`, `ui/generated/` and `cli/generated/` where it does, so the
typecheck, the pure tier, the shell's tests and every existing path are unchanged. Any other product writes a parallel
tree, `build/products/<product>/packages/retroplug/{src,ui}/generated/` for a BUILD (the CLI tree is desktop-only: the
plugin ships no CLI), or the same layout under `packages/retroplug/.products/` when the TYPECHECK writes it
(`--products-dir`; gitignored, with its reason in `.gitignore`): the first form of this design put the typecheck's tree
under `build/`, and stage 5's review found that `build.sh` and `build.bat` configured only when `build/` did not EXIST,
so CI's own order, a typecheck and then `./build.sh`, skipped the configure and failed on `could not load cache`. As
built, both scripts test for `build/CMakeCache.txt` (the batch file's `BUILD_EXISTED` is `CONFIGURED`), and the
typecheck, which runs one `tsc -p` per non-desktop list the manifest names (`scripts/typecheck-products.mjs`), leaves no
`build/`. Both trees have a `tsconfig.json` over them (the shell's program, a `paths` file of the workspace's published
modules plus the shell's host modules and aliases, and `files` naming core's ambient globals again, since `${configDir}`
points at the product's directory: decision 24). Five specifiers in the generator's output are hard-coded relative to
the shell's tree and must be computed from the output directory: `./consoles` stays (a sibling), and
`"../../src/generated/packages"`, `"../../src/keyCodes"`, `"../../src/bindingMap"` (`packages.mjs:777-782`) and
`"../graph/protocolNodes"`, `"../graph/controller"` (`:1255-1256`) become paths computed to the shell's real files (the
second goes with stage 4).

**Resolution.** The shell's sources import `../generated/packages` and friends by RELATIVE path, and esbuild's `alias`
substitutes package specifiers only, so the brief's "esbuild alias" would not redirect them. Stage 5 opened with the
one-file bundle that confirms it: an entry importing `./generated/packages.js`, built with
`alias: { generated: <the product's file> }`, compiled the DESKTOP's file, and `alias` refuses a relative name outright
(`Invalid alias name`); the same bundle through an `onResolve` filter answered with the product's file (decision 17).
The mechanism is an esbuild `onResolve` plugin, `tools/product-generated.js`, matching imports of
`.../generated/(packages|consoles|protocol|scriptNodes| placers|menus|views)` whose importer is under the shell's `src/`
or `ui/` and answering with the product's file. It is taken by `tools/build-controlplane.js`, `build-ui.js`
(`uiBuildOptions(entry, outfile, product)`, so `bundleInputs` serves the checks too) and `build-session.js`
(`buildOptions(entry, outFile, format, product)`) behind a `--product-dir <dir>` flag, the same form in all three,
refused when bare and emitted by CMake only for a set that has a product tree (no argument is the desktop). A product's
tree is not under the shell, so its builds also hand the shell's `node_modules` to esbuild as `nodePaths`, and
`bundleInputs` is async in both scripts (esbuild refuses plugins in `buildSync`; its four callers, three tests and a
driver, await). The depfile (`writeDepfile`) already lists every input, so an edited manifest rebuilds the plugin's
bundles.

**The bundles are product-supplied, not the backend's.** The script-node bundle and the render-worker bundle are
compiled into `retroplug-backend` (section 1), so one product cannot differ without both leaving it. They become sources
`retroplug_product` adds to each of its `TARGETS` (as it adds the generated registration source today,
`packages.cmake:1805`), and the backend declares them extern with no definition of its own. Every consumer of the
backend is already a product (a configure check fails a binary that links the engine and is not one,
`packages.cmake:1949-2012`), so none is left without a definition. As built, the two arrays are not compiled in every
product target: `retroplug_bundles(<set>)` makes an OBJECT library of them, `retroplug-bundles<set>-objects` (dependent
on both bundle targets, and listed for the `/W4 /WX` set through a global property so a third list needs no edit), and
`retroplug_product` adds its `$<TARGET_OBJECTS:...>` to each of its `TARGETS`. The first form compiled the 4.7 MB
render-worker array in every target (measured at `-O2`: 272 ms for it and 25 ms for the script array, about 0.3 s a
target over 13 test binaries, the desktop's products and four plugin formats), which one compile per distinct set
replaces. The control-plane and UI bundles are embedded by the plugin and the SDL binary through variables
(`plugin.cmake:17, 21`, `native/CMakeLists.txt:345-346`), which take the product's bundle's output. Each bundle's symbol
is derived from its output stem (`tjsc -p rp_`), so the plugin's output files keep the stems (`cp-bundle`, `ui-bundle`,
`script-bundle`, `render-worker-bundle`) in a directory of their own.

**CMake.** `CMakeLists.txt:307-350` reads every member of `retroplug.products` into `RP_PRODUCT_PACKAGES_<name>` and
keeps `RP_DESKTOP_PACKAGES` as the desktop's (every existing consumer, `bundles.cmake:26`, `tests.cmake:69, 145`,
`packages.cmake:2246`, `node-addon.cmake:44` and the products at `native/CMakeLists.txt:172, 189, 278, 355, 406`, is
unchanged); the configure refuses a list that is not a subsequence of the desktop's.
`retroplug_product(<product> LIST <name> ...)` is `PACKAGES ${RP_PRODUCT_PACKAGES_<name>}` plus the list's bundles;
exactly one of `LIST` and `PACKAGES` is accepted, and **there is no `LIST` default**: the design first had `desktop` as
the default, but every desktop product and test binary already passes `PACKAGES ${RP_DESKTOP_PACKAGES}`, and the bundle
set is chosen by the CONTENT of the package list (decision 21). `retroplug_bundles(<set>)` (in the new
`packages/native/cmake/product-bundles.cmake`, included before the first product; the design had it in `bundles.cmake`)
makes the generator target and the four bundle targets once per DISTINCT list content (the desktop's keep their names:
`retroplug-cp-bundle`, `-ui-bundle`, `-script-bundle`, `-render-worker-bundle`; the plugin's take a `-plugin` suffix and
write under `build/products/plugin/native/`), so products whose lists are equal share bundles. A non-DEVICE product
whose packages equal no list is REFUSED at configure, which the review's third important forced: the first form said
such a product "makes its own set named after itself", and it failed at BUILD, because the generator only knows the
manifest's lists (a DEVICE product, or a project defining no `retroplug_bundles`, may name any packages; a fixture
project in `test/packages/product-lists/` holds `list-both`, `list-unknown`, `packages-no-list`,
`subsequence-out-of-order` and `subsequence-twice` at every configure). The plugin's formats pass `LIST plugin`
(`plugin.cmake:77`); every other product passes the desktop's packages. The generated `rp_product_providers.hpp` and the
registration source are per product already. Moving the bundle block out of `bundles.cmake` lost one ordering that only
a fresh tree shows (stage 5's review): `retroplug-cli-bundle`, `retroplug-cli-sdk-assets` and
`retroplug-example-session` each depend on the desktop's generator target again, or a parallel build from a tree with no
generated files could bundle the CLI before `cli/generated/tools.ts` exists. The backend's `add_dependencies` on the
render-worker bundle is gone with its embedding.

### 7.3 Checks

- **`plugin.cmake:85-97`'s two assertions invert.** Today the plugin carries the Launchpad's node types and no desktop
  half; with the list it carries neither, so each format target must link neither `retroplug-launchpad` nor
  `retroplug-launchpad-desktop` directly, and with 6.7 nothing reaches either transitively through the backend. The RED
  swaps `LIST plugin` back to `PACKAGES ${RP_DESKTOP_PACKAGES}`: the configure fails
  `plugin.cmake: retroplug-clap links retroplug-launchpad: the plugin's product list carries no Launchpad ...`.
- **The registration golden.** `product-registrations.json`'s `retroplug` row (the plugin) is re-recorded: it loses
  `rp.launchpad.surface-in` and `-out`; no other product's row moves (E3). The brief's "the `[packages]` golden gains a
  plugin entry" is not what the tree does: `retroplug` is already a row, and `rp.controller.session` is a script node,
  which that C++ golden does not hold. As built, the dump's writer re-indents the whole file, so the two-line deletion
  was applied BY HAND to the committed formatting, and `[packages-dump]` (no update variable) parses to exactly the
  committed file, every row and the key order equal (the stage 5 review's check).
- **The bundle split, per product.** `test/script/bundle-split.mjs` builds each surface's bundles for `desktop` and
  `plugin` (generating the plugin's tree to a temporary directory) and `bundle-ui-split.test.ts` asserts that the
  plugin's control-plane, UI, script-node and render-worker bundles carry no module under `packages/launchpad/` or
  `packages/lsdj-launchpad/` and that the desktop's carry both (the positive control, as built counting the pack and the
  bridge SEPARATELY in all four: 15, 15, 15 and 19 modules for the control plane, render worker, script nodes and UI,
  against 0 in each of the plugin's); and that the plugin's script entry registers no `rp.controller.session` and
  numbers `rp.lsdj.serial-out` 22. `bundle-protocol.test.ts` also bundles `buildGraph`'s planner against the plugin's
  tree (2 modules, no node module).
- **A `tsc` run over every non-desktop product's generated tree** (each list's `tsconfig.json`, extending the shell's),
  run by `pnpm typecheck` after the generator writes it, from the lists the manifest declares rather than a hard-coded
  product: `packages/retroplug/.products/<product>/{tsconfig.json,packages/...}`, outside `build/` for the reason 7.2
  gives. Each checks the generated modules against the shell's real sources; the shell program itself still resolves
  `generated/*` to the desktop's, which has the same shapes, so this does not typecheck the shell against the plugin's
  tree (said so, section 11, item 5).
- **The plugin's planner without the placers.** A `test/graph` case plans a model with `models.controller` enabled under
  the plugin's (empty) placer list and asserts no `controller`, `surface-in` or `surface-out` node and the cart's
  `lsdj-sync` node at the user's own mode. As built, `test/graph/plugin-product.test.ts` holds three cases: the desktop
  places the controller and forces `midiMap` for a model with one enabled; the plugin's plan (the shell's `buildGraph`
  bundled against the plugin's tree, in a Node child) has none of it and leaves the cart at the user's `midiSync` (E2);
  and a fresh plugin project's settings have no `controller` key while a project carrying the block keeps it, held to
  its RAW INPUT and not to another build's re-serialisation (E8).
- **The Reaper legs run the plugin's bundles for real**: `reaper:all` (15 checks), including `reaper:params` (2725
  parameters before and after click-loading mGB on VST3, and 644 on CLAP: the design's figure and the brief's are the
  two formats' counts, both PASS on the prototype, with `reaper:editor` PASS) and the editor and two-instance legs. The
  prototype ran `reaper:params`, `-clap` and `reaper:editor`; the rest is acceptance's (section 13).

### 7.4 What the plugin shows today, and what changes (verified)

The Launchpad's submenu is built by `launchpadInstanceMenu` (`launchpad/ts/ui/index.ts:167-174`) and the scan rows by
`launchpadScanRows` (`:209-216`); both read `getLaunchpadConfig(host.config, host.devices.rpc)` and return `[]` when the
RPC channel has no Launchpad facet (`launchpadDevices.ts:159-162`), and the plugin names `n8` alone in `PROVIDERS`
(`plugin.cmake:77`), so it mounts none. **No visible row is lost.** What else changes for the plugin: the `launchpad` UI
service (`index.ts:271`: inert restore and a no-op poll without a facet) and the `__rp_scanControlSurface` hook its
start installs are not loaded; the script-node bundle drops `rp.controller.session` and `registerControllerApps`, and
its labels renumber (E6); the control plane drops the controller registry and the song poll's second call; the planner
drops the controller's placement and sync override (E2); the `ProjectSection` for `controller` has no contributor, so a
project's block is kept verbatim (E5); the plugin's link line names no Launchpad target, because the backend no longer
links one (6.7) and the product list names none; the stage 4 configure check holds the backend half.

## 8. Behaviour edges

None changes audio, a graph document of a product that lists the Launchpad, or a saved file's shape. Each is listed with
what was found.

- **E1. The `hand-controller` case's `lp-app-quantise` row shows "Next bar", not "Immediately"** (3.1): the menu shows
  the value the app runs for a hand-edited `appConfig` (`quantise: 3` snaps to the schema's `bar`). `lp-app-follow` is
  unchanged. One cycler in one recorded case re-recorded; the matrix's other 34 cases and every other row are held.
- **E2. In the plugin a project with an enabled controller places no controller nodes and no sync override.** Before:
  the project's `controller.enabled` is true (written by the standalone, or by hand; the plugin's own submenu cannot
  enable it, since it returns `[]`), so `controllerModel()` (`projectStore.ts:195`) returns a model and `buildGraph`
  places `surface-in`, the `controller` session, `surface-out`, and, for the default target `system` and an app whose
  manifest has a `syncOverride` (`lsdj-midimap`'s), forces the LSDj cart's `rp.lsdj.sync` and its serial-out twin to a
  mode of the controller's choosing (`controllerSyncOverride`, `graph/controller.ts:77-91`; `syncModeFor`, `:101-104`).
  That mode is `midiMap` when the cart's own SYNC has not been polled (`cartSync` null) or reads `MidiMap`, and `off`
  when it has been polled as anything else. The poll is `refreshControllerSong`, driven by the EDITOR's song watch
  (`useSongWatch.ts:19-28`), so with the editor closed the mode is `midiMap`, with it open and SYNC set to MIDI sync it
  is `off`: either way the cart stops following the DAW's MIDI clock the user set, and its serial capture follows the
  forced mode (`serialOutCaptureFor`). After F5a the plugin has no placer for the section: nothing is placed, and the
  cart syncs as its own settings say. The section is kept verbatim and written back, so a project that returns to the
  standalone loses nothing. The reading is verified in the code, and the "before" is recorded by the existing
  role-golden cases that hold a controller (the plugin ran the same planner), not by a run of an old plugin.
- **E3. The registration golden's `retroplug` row loses `rp.launchpad.surface-in` and `-out`** (7.3): re-recorded in
  stage 5, the diff is the record.
- **E4. The plugin's bundles shrink**: the control-plane, UI, script-node and render-worker bundles lose the Launchpad's
  and the bridge's modules. Sizes are reported at stage 5 and are no oracle (a bundle's hash moves when any bundled path
  moves, F4c decision 12).
- **E5. A `settings` key no package contributes is kept, not stripped.** `parseConfig` strips unknown keys at every
  level today (`test/project/tolerance.test.ts:28-47` asserts `futureSetting` is stripped); with sections, the shell
  cannot tell `controller` in a plugin that does not know it from a key a newer build wrote, so every non-core key under
  `settings` is kept verbatim and written back. That test's `futureSetting` expectation is the one test that changes
  meaning. It is the forward-compatible side of the refuse-newer rule and a change to a recorded property; the user
  chose it (decision 11, option A).
- **E6. A product's script-node span labels are its positions** (7.1): in the plugin `rp.lsdj.serial-out` is 22, not 23.
  A trace name and a profile's span number only; no document, no oracle.
- **E7. The menu tests' fallback host words one refusal fewer** (5.3): not user-visible, no test asserts it.
- **E9. The CLI SDK's `DirectGraph` surface changed.** The CLI SDK (`cli/sdk.ts`) exports `DirectGraph`:
  `setController(controller)` is now `setModel(key, model)`, the store lost `setController`, `refreshControllerSong`,
  `controllerCartSync` and `controllerStartAnchor`, and `settings().controller` is now `settings().sections.controller`.
  BlipToaster and NESVJ use none of these (F5a's acceptance ran both consumers' suites), but it is an SDK edge, not a
  private move: it is recorded here and in spec/07's F5a entry. The design first listed "the CLI's package set" as
  unchanged and said nothing of the SDK's API.
- **E8. A fresh project written by the plugin carries no `controller` block.** A section's contents come from its
  contributor: the block exists in a fresh project because the Launchpad package's `projectSection` supplies a
  `defaults()`, and a product whose list carries no such package has none to write (6.1, and E5 for the block a project
  already has: it is kept verbatim). A `.rplg` the desktop writes opens in the plugin unchanged and back again, its
  `controller` key where it was; what changes is only what a project the PLUGIN creates starts with. The case writes a
  fresh project with `buildConfig(DEFAULT_SETTINGS, [])` and holds its settings to having no `controller` key (stage 5;
  the desktop's own fresh project has one).

Not edges, and why: the Launchpad submenu's rows for a valid config, the desktop product's every document and its three
control-plane bundles' behaviour, the `.rplg` shape for a project that carries a controller (the key and its contents
are unchanged, and an unstamped section is not stamped), the standalone's, the CLI's and the Node addon's package set.

## 9. Proof

**The oracles held** (compared before and after each stage, on a run with fixtures and `RP_FAIL_ON_SKIP=1` for a stage
that can move audio, plus CI's fixture-free set): the strict parity baseline (`pnpm test:native parity`: 197 cases in 26
groups by the baseline directory; the brief's 30 groups and 250 cases are not what the tree holds), the kit baseline and
the render baseline, the `buildGraph` and role goldens' documents (298 and 36), the protocol-node digests
(`[protocol-nodes]`), `edio-golden.json` (28 cases), the device-check and box image hashes and the bliptoaster pack
bytes, the recorded menu matrix except E1, `reaper:params` (2725 parameters held), and the registration golden except
E3. **Rules and checks held:** `pnpm typecheck` (every program), `pnpm check:boundaries` with its lists as 6.6 gives
them, the native boundary check, the literal scanner over `src`, `cli` and `ui`, `check:docs`, and the generator's
fixture suite.

**RED first, and the new tests, by stage:**

1. **Stage 1.** The display case against the old code (3.1); the matrix diff for `hand-controller` alone;
   `pnpm typecheck` with one program; `check:docs`.
2. **Stage 2.** The boundary RED of 4.3 and the scratch refused-manifest case; the device twin compiles with the include
   root; every oracle by construction, the image hashes compared.
3. **Stage 3.** The three bundle checks of 5.4, the third RED against the pre-split generator; the generator's views
   cases repointed and the new refusal; rule 3's out-of-`ts/ui/` refusal on a fixture tree, RED then green; the UI
   bundle's normalised diff (not its hash) unchanged; `test:ui`'s `lsdj-hd`, `lsdj-overlay` and `risa-overlay` cases
   unchanged.
4. **Stage 4.** The backend's Launchpad link: the boundary RED then green and the scratch case of 6.7, with the C5
   harness cases and the controller cases of `test:native` held green. The goldens' documents byte-identical, a RED run
   of the case runners with a slot moved by one position; `project` group cases (a section read, a patch merged, an
   unknown target refused, a section stamped newer refused, a section with no contributor kept verbatim through
   `parseConfig` and `buildConfig`); the placers' slots against the pre-split `buildGraph` for every controller case; a
   service instance per store (two stores, no shared state); the kind mechanism's refusals (a duplicate item, an item
   with one half, a runtime naming an unknown node type) over generator fixtures with a fixture kind that is not the
   Launchpad's, so the mechanism is held without the name; rule 9 and the scanner with the lists emptied of the
   Launchpad, RED on a tree that still imports it; `test/controller/*`, `test/project/controller.test.ts` and
   `songRefresh.test.ts` moved to the package or repointed.
5. **Stage 5.** The per-product bundle checks of 7.3, whose positive control counts the pack and the bridge separately
   in all four of the desktop's bundles; the plugin planner case; the registration golden's re-record; every non-desktop
   list's `tsc` (outside `build/`, so a typecheck before a build leaves no tree for a build to mistake for a configured
   one); `plugin.cmake`'s inverted assertions, with a RED that swaps the plugin's `LIST plugin` back to
   `PACKAGES ${RP_DESKTOP_PACKAGES}`; the configure's held cases for `LIST`/`PACKAGES`, a list the manifest does not
   declare, packages that match no list, an out-of-order list and a duplicate one; the fresh-project case for E8, which
   compares against its INPUT block; the Reaper legs.

**What is not proved, said plainly.** A `component` view drawing on a real host (stage 3 proves it bundles, as F4c
proved it typechecks, and no product lists one); the plugin's tile and menu look in a DAW (the Reaper editor legs prove
it renders, not that every pixel is unchanged); the Windows, macOS and arm64 builds of `n8-wire`, the per-list bundle
targets and the supplied-sources change (first on push).

## 10. The stages

Five stages on a prototype branch, one commit each, DeepSeek Flash implementing and Opus reviewing each before the next
(F4a to F4c's routing). Each stage leaves the tree green on every oracle of section 9 it can reach. After any fix-pass
rebase, each stage's tree is extracted after the final rewrite, proven equal to its commit, and typechecked and tested
alone (F4b section 11), never symlinking the worktree's `build/` for a run that writes.

1. **Small fixes** (section 3). The display fix, `legacyConfigIndex` and `configRowCycler`'s index deleted,
   `hand-controller` re-recorded; `tsconfig.ui.json` folded; spec/07's stale lines corrected. *Exit:* E1's diff is one
   cycler in one case; `pnpm typecheck` green in one shell program; `check:docs` green; every other oracle held.
2. **`n8-wire`** (section 4). The package, the two headers moved and the constants split out, `mesen` off `n8`, the
   lists and the device twin. *Exit:* the boundary RED then green; the registration golden, `edio-golden.json`, the
   device-check and box image hashes, the pack bytes, `test:n8`, `test:plugin` and `build-mesen-all-consoles.sh --off`
   unchanged.
3. **Views split** (section 5). `defineViews`, the manifest member, the generated `views.ts` and the moved checks, the
   `menuContributions` split and `menuDefs.ts`'s fallback host, `lsdj`, `risa` and the fixture moved, rule 3 and the
   generator's `ts/ui/` refusal, the pure-tier proof. *Exit:* the three checks of 5.4 with the third RED first; the UI
   bundle's normalised diff clean; `test:ui` unchanged; spec/06 updated.
4. **The shell and the backend free of the Launchpad** (section 6): sections, services, placers, the `project` group,
   the contribution kinds and the backend's harness move (6.7). One commit, built and reviewed in two halves (sections,
   services, placers and the `project` group; then the kinds and the harness move), each half Opus-reviewed before the
   next. *Exit:* the goldens' `documents` byte-identical; no `@retroplug/launchpad` import in `src`, `ui` or `cli`;
   `packages/native` declares and links no Launchpad target; rule 9's lists as 6.6; `buildGraph` spelling no
   `rp.launchpad.*` or `rp.controller.*`; the parity baseline, the controller cases, `reaper:params` and every oracle
   held on the desktop; AGENTS.md's Launchpad paragraph and spec/03 updated.
5. **Per-product bundles** (section 7). The `plugin` list, the generator's `--product`, the plugin's generated tree and
   `tsc`, the esbuild plugin, the product-supplied bundles, `retroplug_product(LIST ...)`, the inverted `plugin.cmake`
   assertions, the checks of 7.3, the registration golden's re-record. *Exit:* E2 and E3 recorded; the plugin's bundles
   carry no Launchpad module; every desktop oracle held; `reaper:all` and both two-instance legs run on the plugin's
   bundles; the docs pass (AGENTS.md, spec/00, spec/03, spec/06, spec/07's checklist and the F5a entry,
   [package-split.md](package-split.md)).

After the prototype: a plan generator, Flash-Next implementers, the serial acceptance kit (adding `reaper:all`, the
editor legs and both two-instance legs, a fresh-clone typecheck before any build, BlipToaster and NESVJ on scratch
copies, and no hardware leg), an Opus final review and follow-ups. Windows, macOS, arm64 and CI are owed.

## 11. Risks and open questions

Nothing here waits on the user. Every question the first draft put (the `native` dependency, the span labels, the
shell's declarations, unknown `settings` keys, the stamp, the context type, the placer module, the goldens' `model`
member, the extend rule, the alias) is answered in the decisions table (rows 8 to 17) and the sections it names. What
remains are risks the prototype settles by reading or by a small experiment; each says which, and the prototype's answer
(or that the item stayed open) is marked in place.

1. **Left alone on purpose.** The `controlSurface` block of `userConfig` and the `config` host group
   (`userConfig.ts:91`, `ConfigHost` in `core/ts/host.ts`) name the concept and import nothing; no check sees them.
   **Left alone, as designed.**
2. **esbuild's `alias` and relative imports** (7.2): decision 17 says the `onResolve` plugin, and stage 5 opens with the
   one-file bundle that confirms the alias would not have worked. **Answered, and the plugin is what was built. It did
   not:** a one-file bundle whose entry imports `./generated/packages.js` compiled the desktop's file with
   `alias: { generated: <the product's file> }`, while the same bundle through an `onResolve` filter answered with the
   product's file (and `alias` even refuses a relative name). The plugin is the form built.
3. **A header-only package's manifest is unwritten; its device story is settled as far as the tree needs it.** `n8-wire`
   has a `native` target and no `entry`, which the existing reader accepts; a DEVICE product refuses a package with no
   `device` entry (`packages.cmake:1675-1684`), so it joins the `retroplug_package()` lists and not a device product's
   `PACKAGES`; the device twin copies the target's `INCLUDE_DIRECTORIES` through a generator expression the build
   generator evaluates (`packages.cmake:1263`), so it sees what the target links PUBLIC as well, and `retroplug-n8`
   names no other package's root of its own (4.2). **Answered as far as the tree needs it:** stage 2 first named the
   root literally in `retroplug-n8`'s own include directories, on the premise that the twin copies only a target's own
   directories; its review reproduced that the genex is transitive, so the literal root was dropped and the twins were
   rebuilt through the link (the images identical). The package's manifest is the header-only form the reader already
   accepts (a target, no entry, provider or device), and "device-ready" says what is checked: the range is compiled by
   every device build through `rp.n8.nes`, the link by none (its `std::function` emit may allocate), so it is ready by
   inspection. **A per-header device check is still not built (open).** `retroplug-launchpad-harness` (6.7) is a
   header-only target too and meets the same questions; it is declared after the device early-return, so a device build
   defines none (stage 4's review).
4. **The device image hashes may carry a source path.** The headers move, so the objects' bytes should not; if a hash
   moves it is a path string, and the stage 2 compare must say so rather than the hash being re-recorded. **Answered:**
   none moved; the three image hashes and the three `bliptoaster` pack hashes are identical to the baseline at every
   stage and at the tip.
5. **The plugin's `tsc` run does not typecheck the shell against the plugin's tree**: the shell program resolves
   `generated/*` to the desktop's, which has the same shapes. A program with the shell's sources and the plugin's
   generated files would need `rootDirs` or a copy, and was not designed. What is checked is the generated modules
   themselves. **Stayed open:** the stage 5 review confirmed that the plugin's program also lists the desktop's
   `src/generated/protocol.ts`, pulled in through the shell's `graph/protocolNodes.ts`, and no launchpad file.
6. **The harness move's test tags are unverified.** 6.7 names the C5 harness cases by what they cover; the prototype
   lists the exact tags and `test:native` files from the sources before the move and holds that list green after it.
   Whether `EngineGraph.test.cpp:318` and `main.cpp` are the only two mounts is read from a grep, and a third (a
   Node-addon or CLI build that stages controller traffic) would surface as a link error, not a silent miss.
   **Answered:** the tags and test files are listed in 6.7 from the sources before the move and held after it, and the
   kit, which builds every host, linked with no third mount.
7. **Not verified here:** `reaper:params`' 644 (only the 2725 appeared in the repository's records). **Answered:** 644
   is the CLAP count and 2725 the VST3's, each held before and after click-loading mGB. Still open: the Windows, macOS
   and arm64 builds, CI, and the real-host look at the plugin's editor and menus with the Launchpad gone
   (`reaper:editor` passed on the prototype; the rest of the Reaper legs run at acceptance).

**Corrections to the brief and the survey** (what the code showed): the controller's `ProjectStore` code is about 140
lines, not 60, and reaches six places not five (the sixth, `src/controller/registry.ts`, is the shell's own module over
the registry); `tsconfig.ui.json` is named only by `packages/retroplug/package.json:9`, `spec/07` and one audit doc (no
CI file); the bundle-split test's `isUi` already covers `ui/generated/` whole, and `check-boundaries.mjs` names
`generated/menus` in one comment; the script-node and render-worker bundles are compiled into `retroplug-backend`, not
the product (7.2); `[packages]` is the registration golden, whose `retroplug` row exists and loses two types, and
`rp.controller.session` is a script node it does not hold; the parity baseline is 197 cases in 26 groups; `native`
depended on `launchpad` (resolved, 6.7); the span labels refuse the plugin list (decision 9); a DEVICE product refuses a
package with no `device` entry (4.2); the Launchpad menus' builders are `ui/index.ts:167-174` and `:209-216`;
`ProjectService` needs a per-store factory and a `state` (6.2); the survey's `legacyConfigIndex` range
`contribution.ts:367-372` is the function, with its doc at `:340-366`. And the kind's `check` is handed the product's
WIRED protocol node types (the generated `PROTOCOL_NODES`), not every script node type the manifests declare: only the
data module carries a node-type set into the control plane without a node's code, and a mode is set on a node the graph
places. The old generator's check accepted a type the graph does not wire (`rp.midi.route`, `rp.controller.session`);
the kind refuses one now, and `KindCheckContext.nodeTypes`' doc says which set it is (stage 4's review).

## 12. What it leaves

For `spec/07-remaining-work.md`'s F5a entry and the package split's closing note, **F5b first**:

- **F5b: the SDL standalone's audio and MIDI as packages behind generic backend interfaces** (survey A), after
  `feature/handheld-perf` (render-ahead, `4b9e39ea8`) and `feature/sdl-standalone` (the `MidiIo` ring fix `e059a7862`)
  land or are decided. Its hard parts are recorded in the survey: the audio thread owns the engine, MIDI hot-swap needs
  audio stopped, the restore rendezvous, PortAudio concepts leaking into `audio.json`, and log lines that are harness
  contracts.
- **Downloadable packs and third-party controller apps.** F5a made a product list and a contribution kind data; a pack
  format, an authoring loop, unload and menus on extensions are not built.
- **A `component` view drawn on a real host**, with its `test:ui` case, when a package ships one (F4c, unchanged).
- **A UI kit compile (`host.kits`), gamepad axis tokens as action bindings, `MemoryType`, the view reopen key** (F4b,
  F4c, F2: no consumer).
- **E2's template gaps** (global RPC names, pack capabilities, the test host's provider seam, the `lvgljs-ui` types
  slice, the example sessions' `extends`).
- **What F5a decided to leave** (section 11): the `controlSurface` user-config block and `config` group; a typecheck of
  the shell against a non-desktop generated tree; a header-only package's device manifest form, if it is wanted beyond
  `n8-wire`.
- **What the prototype found for later** (the reviews'; none moves an oracle): - **A `ui` module may still import
  React.** Rule 3 admits a React host module from every file under `ts/ui/`, the `ui`
    module included, so `UiContribution` losing `views` does not stop a package putting React in its menus module. For a
    listed package the menu tests now fail loudly; for an unlisted one nothing does. A rule ("nothing reachable from a
    `retroplug.ui` module imports a React host module") is not designed.
  - **The shell is typechecked against the desktop's generated tree only.** A product's tree is checked by its own
    `tsc`,
    which also lists the desktop's `src/generated/protocol.ts` through `graph/protocolNodes.ts` (section 11, item 5).
  - **A per-header device check** for a header-only package (`n8-wire`'s link is device-ready by inspection, not by a
    check).
  - **The plugin list carrying `n8-wire` is held by hand.** `order()` refuses an unlisted dependency only when it
    contributes, and a native-only package contributes nothing, so a list's dependency closure for such a package is
    unchecked; the same holds for `retroplug-host` linking `retroplug-launchpad-harness` by hand, which a second product
    wanting staging must do too.
  - **Comment edits move the desktop's bundle bytes.** esbuild emits comments inside object literals, so rewrapping a
    comment in a shell source moved five of the desktop's bundles by +9 to +11 bytes (proved comment-only, section 13). A
    future byte comparison must not read it as a code change, and a stage that wants byte-identical bundles under the line
    rule should exempt those sources.
  - **No check pairs a service key with a placer or a section key**, so a typo places nothing in silence;
    a pairing check was left.
  - **`Placement.wires` reads `PlaceSpec.launches` only**, structurally, because core cannot name the shell's
    `ProtocolNodeSpec`.
  - **`retroplug-launchpad-test`'s mid-drain swap case** is load-sensitive: one kit
    run failed on it and 25 isolated reruns passed (the sensitivity F4b recorded).
  - **Windows, macOS, arm64 and CI** for `n8-wire`, the per-list bundle targets, `product-lists.mjs`, the object
    libraries,
    the product-supplied bundle sources and the plugin's generated tree, as F4a to F4c's were. For the user's Windows
    agent, the three risks the final review named and this follow-up could only record (it fixed the cross-drive `./C:/`
    specifier and `build.bat`'s stale comment):
    - **The child fixture configures pick their own generator.** `packages/native/cmake/tests.cmake`'s fixture children
      pass no `-G`, toolchain or compiler, and the `objects` / `objects-declared` / `packages-no-list` cases
      `enable_language(CXX)` (`test/packages/gather/CMakeLists.txt`, `test/packages/product-lists/CMakeLists.txt`):
      on Windows that is the Visual Studio generator's compiler detection under a Ninja parent, slow on the first
      configure and possibly a different compiler. Give the children the parent's generator and toolchain if it is slow
      or disagrees.
    - **`packages/native`'s targets are judged by the native boundary rule for the first time.** The exemption is gone
      (`cmake/packages.cmake`), so the WIN32 / APPLE / MSVC branches Linux never evaluates (`/bigobj`, `plugin.cmake`'s
      platform blocks, `RP_BOUNDARY_PACKAGES plugin` on `retroplug`) meet the generic rule first on push.
    - **Long paths and `/W4 /WX`.** `build\products\plugin\...` and the per-set object library's object paths are long,
      and the bundle arrays are compiled under `/W4 /WX` as their own target.
- **Spec bookkeeping** is done by stage 5 (the checklist's five lines ticked, the F5a entry with E1 to E9 and F5b first,
  spec/06's item 7 replaced by the manifest form of 6.5, spec/06's "Adding a package" restructured to items 1 to 13).

## 13. As built (the prototype)

The prototype is five commits on the design commit `bef622dac`, one per stage, each built by DeepSeek Flash and reviewed
by Opus (stage 4 in two halves), with each review's fix pass folded into the commit it belongs to by fixup and
autosquash (the last pass split two hunks that belonged to stage 5 out of stage 4 and amended the tip's message), so the
five trees stand as final and a stage report's sha is older than the branch's. **The shas in this section are the
PROTOTYPE's:** `e9f92dc2d`, `c4292ebb5`, `a481a6cf1`, `4a7ff2c19` and stage 5, the commit that carries this section (so
it cannot name its own sha: the tip of `proto/f5a-leftovers`). The whole prototype is 190 files, +6,352 -2,237 over the
design commit (this section excluded), most of it stage 4's. Sections 1 to 11 above were corrected in place to what the
build found; this section is the record of how, and the plan is written against it.

**The stages**, by commit subject and current sha (files changed, lines added and removed):

1. `e9f92dc2d fix(f5a): the app rows show what the app runs, and the UI joins one shell program` (14, +92 -69): the
   display fix and `legacyConfigIndex` and `configRowCycler`'s index deleted, `hand-controller` re-recorded (E1),
   `tsconfig.ui.json` folded, spec/07's stale lines and the audit doc's link.
2. `c4292ebb5 feat(f5a): the N8 wire is a package of its own, and mesen no longer declares n8` (22, +164 -59):
   `packages/n8-wire` with the reliable link moved and the three constants split out, `mesen` off `n8`, the lists, the
   boundary fixtures.
3. `a481a6cf1 feat(f5a): a package's views are their own module, apart from the menus` (47, +866 -339): `defineViews`,
   `retroplug.views`, the generated `views.ts` with the moved checks, the `menuContributions` split, `lsdj`, `risa` and
   the fixture moved, rule 3's and the generator's `ts/ui/` refusal, the three-part pure-tier proof.
4. `4a7ff2c19 feat(f5a): the shell names no controller, and a contribution is manifest data` (106, +3,489 -1,357):
   `projectSections`, `projectServices`, graph placers, the `project` host group, contribution kinds, the backend's
   harness move, rule 9's lists and the generic native boundary rule.
5. `59a9353ae feat(f5a): the plugin is its own product, and a bundle is a product's` (41, +1,821 -493): the `plugin`
   list, the generator's `--product`, the `onResolve` plugin, the product-supplied bundles, the inverted `plugin.cmake`
   assertions, the per-product checks, E3, and the docs pass (the tip of `proto/f5a-leftovers`).

**What each review found, and what the fix pass changed.**

1. *Stage 1 (ready, no Critical).* The RED reproduced, E1 exactly one cycler, the fold an identical file set; 11 minors.
   Controller decision: the display fix's fake-app case also pins a `toggle` knob defaulting false and a range clamp
   (the review's option b). Fix pass (amending stage 1): both rows RED then green, spec/07's `:36` line restored with
   credits.
2. *Stage 2 (with fixes, no Critical).* Important: the literal `n8-wire` include root in `n8`'s CMake was redundant and
   its premise false (the twin's `INCLUDE_DIRECTORIES` genex is transitive; reproduced); six source comments still said
   mesen declares `n8`. Minors: the device-ready claim (reworded to what is checked), a history comment, a column, the
   `node-sdk-headers` link, an empty `transport/` directory. Fix pass: the root dropped and the twins rebuilt through
   the link (images identical), the comments, the headers target.
3. *Stage 3 (with fixes, no Critical).* Important: the pure proof did not bundle `menuDefs.ts`, so a re-import of a view
   function passed everything (reproduced); no generator fixture ran a views-only package; `bundles.cmake`'s
   `BYPRODUCTS` lacked `views.ts`. Fix pass: `menuDefs.ts` the third pure entry (RED: 315 modules), `zeta` views-only
   (RED with the deletion off), the byproduct, one shared fixture alias map and the minors.
4. *Stage 4, first half (with fixes, no Critical; the desktop unchanged over 464,192 `buildGraph` inputs and 390 round
   trips, with E5 the one diff).* Important: an unstamped section read as current and could never migrate (decision 18);
   `setSection` and `buildConfig` took core keys and `__proto__`; `placers.ts` missing from the byproducts; the planner
   bundle test must refuse node modules by name; `Placement.wires` needs the node id. Sixteen minors. Fix pass folded
   into the second half's commit.
5. *Stage 4 as a whole (with fixes, no Critical; the desktop unchanged against the first half).* Important: the pack's
   real kind check untested (a fixture copy stood in, and writing the test found the node-type check sitting after an
   early return); `DirectGraph` no longer loaded the registry fill (an app's config parsed to `{}`, reproduced); nothing
   guarded `buildGraph` against spelling a package's node type; `ControllerSyncRule` still in core; and the configure's
   native check named the Launchpad. Fix pass (amending stage 4): the real check's own test, `DirectGraph`'s import, the
   scanner's `DECLARED_NODE_TYPES` and package-prefix guard (RED on the pre-fix tree), the rule deleted from core, and
   the GENERIC native boundary rule (decision 19) with its REDs on both halves.
6. *Stage 5 and the whole prototype (with fixes).* **Critical:** `pnpm typecheck` created `build/` (the plugin's tree),
   so CI's own order, a typecheck and then `./build.sh`, skipped the configure and failed on `could not load cache`
   (reproduced; the parallel kit ran on a configured `build/` and could not see it). Important: the CLI bundle, SDK
   assets and example sessions lost their ordering on the generator; `$<TARGET_OBJECTS:t>` passed unjudged (stage 4's
   message said otherwise); a `PACKAGES` product matching no list failed at build; the design contradicted the tree on
   the twin and lacked E8; the guides still described `defineUi({ views })`, the controller group and two item 8s; the
   harness target sat before the device return. Fifteen minors. Last fix pass (fixups folded by autosquash into the
   stages that introduced each defect; no golden, baseline, digest, image or pack byte moved): the typecheck's tree
   under `packages/retroplug/.products/` and `build.sh` / `build.bat` testing `build/CMakeCache.txt` (RED on a scratch
   extract, then a fresh-tree real configure), the generator ordering restored, objects judged (`objects` and
   `objects-declared`), an unlisted `PACKAGES` refused at configure with a held fixture project, the docs, the narrower
   vouchers, an object library per bundle set, and the comment rewraps.

**Departures from the design as committed (`bef622dac`), and why.** Each is also corrected in place above.

- **The E1 step list rotates** (3.1): the recorder steps from the row's own index; and the recorded case's own comment
  and the audit doc's prose were corrected, since both asserted the old reading.
- **No `n8-wire` in the export tool's list, and no literal include root** (4.2): the tool calls no
  `retroplug_package()`, and the twin's genex is transitive. **A header-only manifest, not a refused-manifest case**,
  and the boundary fixtures are `refused-native/` and `read/` (a link boundary is a target property). The wire links
  `node-sdk-headers`.
- **The pure proof has three entries, and the generator's fixture tree puts its UI module under `ts/ui/`** (5.2, 5.4).
  `ViewsContribution.views` is required.
- **An unstamped section reads as version 1** (6.1, decision 18); **`setSection`, `section` and `buildConfig` refuse
  core keys and `__proto__`, and a hand-edited `__proto__` key is kept as data** (6.1).
- **The section's parse is hand-written, and the pack's control modules are in `ts/control/`** (6.1, 6.5, decision 25).
- **`ControllerSyncRule` is in the pack's placer alone, `Placement.wires` takes the node id, and `PlaceSpec` is core's
  structural type** (6.3).
- **The kind has no `idOf`, refuses an app-member name, takes the pack's whole validation in its `check`, and
  `nodeTypes` is the wired set** (6.5, decision 23): a type the graph does not wire is refused where the old generator
  accepted it.
- **The registry is filled by a side-effect import in `DirectGraph` and `projectConfig`,** not by `accept` alone (6.3).
- **The role-golden runner alone translates `model.controller`** (6.3); `build-graph.test.ts` needs none.
- **`SHELL_IMPORTS` gains `core/defineKind`, `core/placer` and `tracker/projectService`** and loses
  `tracker/trackerPlayback` (6.6); the stage 4 report said `trackerPlayback` was never listed, and the diff shows it
  removed.
- **The native boundary rule is generic, not a configure check naming the Launchpad** (6.7, decision 19): the design's
  RED could not run as written because the check exempted a shell outright. `native`'s `devDependencies` gain
  `launchpad`, `n8`, `device-io` and `devtest`, and three targets are vouched for where they are made
  (`retroplug-ts-packages`, `retroplug-box-usb-cdc`, `retroplug` against the plugin's list).
- **The harness target links `node-sdk-headers` and is declared below the device early-return** (6.7).
- **The alias claim was confirmed, so the `onResolve` plugin** (7.2, decision 17); **a product's tree resolves through a
  `paths` file and `nodePaths`,** and `bundleInputs` is async (decision 24).
- **No `LIST` default; a bundle set is the list's content; an unlisted `PACKAGES` is refused at configure** (7.2,
  decision 21); the bundles' CMake is `packages/native/cmake/product-bundles.cmake` and one OBJECT library per set.
- **`product-lists.mjs` exists because CMake cannot enumerate a JSON object's keys** (7.1); the subsequence rule refuses
  a duplicate by name and is a function so a fixture can hold it; `order()` skips the shell for a non-desktop list only.
- **The typecheck's tree is `packages/retroplug/.products/`, not `build/`,** and one `tsc` runs per non-desktop list the
  manifest names (7.2, decision 22). **`--product-dir` is one form in all three build scripts.**
- **E3 was applied by hand to the committed formatting** (7.3): the dump's writer re-indents the whole file. The stage 5
  review checked the dump parses to exactly the committed file.
- **E8 is an edge** (8, decision 20), and spec/06's "Adding a package" is restructured to items 1 to 13 (the kinds' item
  pushes the rest down) with the section, service, placer and kind rules stated rather than cited.
- **Stage 4's sections are one commit built in two halves,** as the design said; the controller decisions of the stage 4
  review (decisions 18 to 20) correct 6.1, 6.7 and section 8.

**Behaviour edges as built.** E1: `hand-controller`'s `lp-app-quantise` cycler only (index, value and the rotated steps;
no other recorded case moved). E2: the plugin's plan places no controller node and no sync override, held by the planner
case against the plugin's own tree and not by a run of an old plugin (the real-plugin render is acceptance's). E3: the
registration golden's `retroplug` row loses two types. E4: the plugin's bundles shrink by 20,326 (control plane), 48,384
(UI), 21,257 (render worker) and 38,445 (script nodes) bytes, the first three moved by +9, +9 and +11 comment bytes on
the desktop's side since the first measurement (-20,317, -48,375, -21,246). E5: `futureSetting` is kept, the one test
whose meaning changed. E6: the plugin's `rp.lsdj.serial-out` is span label 22. E7: the fallback host's one wording,
asserted by no test. E8: a fresh plugin project has no `controller` block, held against its input. E9: the CLI SDK's
`DirectGraph`/store surface changed (`setModel(key, model)`, `settings().sections.controller`, four controller members
gone); BlipToaster and NESVJ use none of them, and the final review named it as the unrecorded edge the follow-up
records. E1 to E9 are all recorded; no audio, graph document of a product listing the Launchpad, or saved-file shape
moved.

**Verification.** A baseline run at `bef622dac` and a run at every stage and at the tip were each the parallel kit
(`.superpowers/verify/tip-verify-par.sh`), every leg exit 0 with no retry, with `pnpm sdl:controller` run alone
(`SDL controller OK`); the tip's final run is `/tmp/f5apro/s5fv-*`, compared to the baseline with `compare.sh`.

| | baseline (`bef622dac`) | prototype tip (`proto/f5a-leftovers`) |
| --- | --- | --- |
| `pnpm test` | 226 files, 2364 cases | 231 files, 2417 cases |
| `test:native` (fixtures, `RETROPLUG_RESOURCES_DIR`) | 172 files, 629 cases, 2 documented skips | identical |
| `test:plugin`, `retroplug-engine-graph-test` | 13 binaries, 336 cases | 13 binaries, 337 (the unset staging hook's case) |
| `test:ui` | 48 files, 54 cases | identical |
| `pnpm typecheck` | every program | plus every non-desktop list's `tsc`, and no `build/` created |
| `check:boundaries`, `check:docs` | 0 refusals, 99 files clean | 0 exceptions, 0 warnings, 99 files clean |
| parity, kit, render baselines, `render-chunks`, `render-cli`, `edio-golden.json`, digests, tsan and asan legs, device check, box, device mode | recorded | identical |
| the three image hashes and the three `bliptoaster` pack hashes | recorded | identical |
| the desktop's generated modules | | byte-identical to the pre-pass generator's and to `8cc29d988`'s |
| the desktop's bundles | | identical but for emitted comment bytes (+9 to +11 bytes on five), proved by rebuilding with the pre-pass comment text |
| the plugin's four bundles against the desktop's | | -20,326 / -48,384 / -21,257 / -38,445 bytes; 0 modules of `launchpad` or `lsdj-launchpad` (the desktop's carry 15, 15, 15 and 19) |
| `reaper:params` / `-clap` | | 2725 / 644 before and after click-loading mGB, PASS; `reaper:editor` PASS |

`test` rose by the phase's own tests (the stage 5 files and the generator's `shell-unlisted` case among them), and the
recorded menu matrix moved only in E1, and no `RP_*_UPDATE` variable was set in the final passes. Each of the five stage
trees was also extracted after the final rewrite, proven equal to its commit, and typechecked and tested alone (`menu`
105, `packages` 109 to 126, `hosts` 3, and each stage's own filters, each exit 0); the tip's tree is `1f1a36e03`'s, so
every run was a run of the final tree.

**The dev-loop cost, measured.** A warm no-op `./build.sh` was timed three times at `e7b7a7dc9` and three times on an
archive of `bef622dac` configured and built to the same state: 920, 915 and 924 ms at the baseline, and 950, 970 and
943 ms at the tip. A no-op run does NOT re-run the extra generator and its four bundle pipelines (CMake's byproducts and
`copy_if_different` keep them up to date when nothing moved), so the review's estimate that the wall time roughly
doubles was not reproduced: the median is about 3% higher, inside the noise of two trees on one box. No fix is
proposed, because the cost the review feared is paid only when an input actually moves.

**What acceptance must run beyond the parallel kit** (the stage 5 review's list, since the kit runs on a configured
`build/` and could see neither the critical nor the lost ordering):

- A fresh recursive `git clone` (a scratch directory, both `pnpm install`s), then CI's own order: `pnpm typecheck`, then
  `./build.sh`; and `cmake --build <fresh> --target retroplug-cli -j` on a tree with no generated files.
- The configure-time gather suite with the `objects` cases, `product-lists/` and a configure of `build-tsan/` and
  `build-asan/` (their product trees land under their own binary directories, the typecheck's under `.products/`).
- `reaper:all` (15 checks, all running the plugin's own bundles), `reaper:two-instances` and `reaper:two-instances-load`
  on `vst3` and `clap` with `RP_LOAD_MODE=dialog`, `reaper:editor-reopen` and `reaper:editor-autoload`, and
  `reaper:params` and `-clap` again.
- **E2 on the real plugin:** a Reaper render of an LSDj project whose saved state carries `controller.enabled: true`,
  showing the cart following MIDI clock rather than being forced into `midiMap` (today only the planner case shows it).
- `test:native` whole with fixtures and `RP_FAIL_ON_SKIP=1` (parity strict, the kit and render baselines,
  `render-chunks`, `render-cli`), `test:plugin`, `test:ui`, `test:n8`, `device:check` and `device:box` with the image
  hashes compared, and `tools/build-mesen-all-consoles.sh` in both modes.
- BlipToaster and NESVJ on scratch copies, with the synced CLI.
- Owed on push: Windows (`/W4 /WX` on the per-format C arrays, `product-lists.mjs` through `execute_process`, long
  `build/products/plugin/...` paths, `build.bat`'s `CONFIGURED`), macOS and arm64.

