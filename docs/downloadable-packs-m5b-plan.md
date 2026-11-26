# Downloadable packs M5b: Mesen as a whole pack, and its suites run against it. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Mesen package, built from this tree as a whole pack (its native module and its app, consoles and ui
halves), loads at launch into test hosts whose product does not link Mesen, and the whole native and UI test tiers pass
against it as they do against the built-in Mesen.

**Architecture:**
- **A product without Mesen boots.** A built-in real cart whose platform no console registers yet (the N8 beside the NES
  only Mesen provides) is dormant until a pack's consoles register the platform, rather than a throw at seed.
- **zod is shared.** It joins the shared module sets as one named module, so a pack's halves and the shell build their
  schemas with one instance, and a pack's app half no longer carries its own copy.
- **One writer per pack folder.** `retroplug_pack(mesen TS_HALVES)` builds the native module and runs
  `tools/build-pack-halves.mjs` on the manifest template the configure stamps, so `build/packs/mesen/0.1.0/pack.json`
  names all four halves, and a depfile re-runs the halves when any source they bundle changes.
- **A test product and its hosts.** `packs-desktop` is the desktop list without `mesen`. `retroplug-host-packs` (the
  native tier's host) and `retroplug-ui-test-packs` (the UI tier's) are built on it and load launch packs:
  `ensureLaunchPacks` before any Engine, the product's shared contract modules registered, the pack loader installed.
- **The runners' pack mode.** `run-native-tests.mjs --packs` and `run-ui-tests.mjs --packs` bundle every file against
  `packs-desktop`'s generated registries, leave the shared contract modules (zod among them) external so the test and the
  pack's halves share one instance of each, load the launch packs' halves before the test's own modules evaluate, and run
  on the pack hosts with `RETROPLUG_PACK_PATH` naming the Mesen pack.

**Tech Stack:** C++20, CMake (DEPFILE), the shared module sets (`tools/build-shared-modules.mjs`, retroplug-modc), the
product bundles (`packages/native/cmake/product-bundles.cmake`, `tools/product-generated.js`), the TS pack loader
(`src/packs/loadPacks.ts`), esbuild, the native and UI test tiers, Catch2.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones; done:
M1, M2a to M2f, M5a (no heap ownership across the pack boundary, [its plan](downloadable-packs-m5a-plan.md)). M5 was
split in four with the user on 2026-11-25; the CLI was split out of M5b on 2026-11-26:

- **M5b (this plan):** Mesen as a whole pack, a desktop-minus-Mesen product, pack-loading native and UI test hosts, and
  the whole native and UI tiers run against it. BlipToaster, risa and smsggdj stay built in.
- **The CLI on a pack (its own plan, before M5d):** a dispatcher built per product, session children (`retroplug-cli
  x.js`, `run`, `test`) that load packs, and the smsggdj render-golden leg on the pack.
- **M5c:** BlipToaster as a whole pack.
- **M5d:** the host legs (two plugin instances, the Node addon, concurrency, a sanitizer leg).

**The user's decisions (2026-11-26).**
1. A built-in real cart whose platform is not registered is **dormant until a pack provides the platform**.
2. The **whole native and UI tiers** run a second time against the pack, **in CI on Linux only**.
3. The **CLI is a separate milestone**.
4. **zod becomes a shared module**, its version part of the API level.

**What exists (read before starting).**
- The product lists are the root `package.json`'s `retroplug.products` (`desktop`, `plugin`, `packs-host`).
  `retroplug_check_product_lists` holds each to a subsequence of desktop's. Every list gets its own generated tree under
  `build/products/<name>/` (`generate({ product })` in `packages/retroplug/scripts/lib/packages.mjs`) and its own four
  embedded bundles and module sets (`retroplug_bundles(<name>)`, `RP_BUNDLE_<name>_*`, the plugin's
  `packages/native/cmake/plugin.cmake` is the pattern). `tools/product-generated.js`'s esbuild plugin points the shell's
  `../generated/*` imports at a product's tree.
- `packages/retroplug/src/contributions/consoleRegistries.ts`'s `realCartsRegistry` check throws "the real cart X names
  the platform P, which no console descriptor registers". With the desktop's `n8` and no `mesen`, the generated
  `consoles.ts` throws at seed, so a `packs-host` control plane or render worker cannot start today.
- The shared module sets (`tools/build-shared-modules.mjs`: `app`, `cli`, `render`) build every contract specifier
  (`scripts/lib/contracts.mjs`' `SHELL_IMPORTS`, `UI_IMPORTS`, the host modules) as a named module. A pack's halves import
  only those names (`tools/build-pack-halves.mjs` `packExternals()`, the native loader's guard). zod is in none of them,
  so a pack's app half bundles its own (Mesen's `js/app.js` is about 550 KB, most of it zod).
- `retroplug_pack(mesen)` (`packages/mesen/native/CMakeLists.txt`) builds `build/packs/mesen/0.1.0` with
  `native/<platform>/rp-mesen.<ext>` and a `pack.json` the configure copies (`halves.native` only).
  `tools/build-pack-halves.mjs packages/mesen <dir> <template>` already builds Mesen's three halves cleanly (probed
  2026-11-26: `js/app.js`, `js/consoles.js`, `js/ui.js`), but nothing runs it for Mesen, and two writers of one
  `pack.json` would fight. `tools/esbuild-shared.js` has `writeDepfile(depPath, target, metafile)`.
- `retroplug-host` (`packages/native/src/main.cpp`) loads no packs: a plain `TjsHostRuntime`, no shared modules, no pack
  loader. The UI harness (`packages/native/test/ui/UiHarness.{hpp,cpp}`) already loads launch packs, registers
  `rp_app_modules` and installs the loader; its UI bundle runs `loadUiPacks()`.
- The native runner bundles each test self-contained against the desktop's generated registries; a test composes its own
  stores (`bootSession`, `createRealBackend`, the stores), so its registries are the bundle's own. `loadAppPacks()`
  (`src/packs/loadPacks.ts`) commits the launch packs' app, consoles, protocol, kinds and placer halves into the
  registries of the bundle that calls it; `packStatus()` says what loaded and what was refused.
- Each product links its own render module set (`rp_render_modules`, the shell's contracts alone, for a bare QuickJS
  render worker): `RenderHost.cpp` registers it and installs the bare pack loader. It is the set a non-UI test host can
  register.

**Execution model.**
- Sonnet implements each task from its text; Opus reviews each task's diff before the next starts.
- The code here was not compiled. Where the tree differs, the implementer follows the tree, keeps the step's intent,
  and reports the deviation.
- Task 9 is triage: it runs both tiers against the pack and REPORTS before fixing anything beyond a one-line cause.
- Task 11 ends with a CI gate, which needs the user's go-ahead to push.

## Decisions this plan makes

- **The dormant real cart.** `realCartsRegistry`'s check keeps its two duplicate rules and drops the unregistered-platform
  throw. `src/consoles.ts`' index lists only the carts whose platform a registered console declares (`realCartOf`,
  `registeredRealCarts`), so a dormant cart is not offered, placed or listed, and the commit that registers its platform
  (a pack's consoles half) re-derives the index and brings it alive. The generator's own refusal of a real cart naming an
  unknown platform (the `packages-generator` case) stays for a cart whose platform no list provides; it moves to accept a
  list that omits the console's package (the check becomes: a platform some package in the WHOLE tree declares).
- **zod is a shared library.** `contracts.mjs` gains `SHARED_LIBRARIES = [{ specifier: "zod", reason }]`, which every
  set's contracts and `packExternals()` include. Rule 9 (the boundary check) does not judge it (it names `@retroplug/*`
  specifiers). A kinds half still refuses it. **`RP_API_LEVEL` becomes 3**: a pack built against this level imports zod
  from the host, which a level-2 host does not serve.
- **The pack folder's one writer.** `retroplug_pack(<package> TS_HALVES)` hands `retroplug_pack_module` a `TS_HALVES
  <package dir>`: the configure still stamps the template (`<binary dir>/packs/<target>.pack.json`), but writes no
  `pack.json` into the folder; a custom command runs `build-pack-halves.mjs <package dir> <folder> <template> --depfile
  <file>` with OUTPUT the folder's `pack.json` and each half's `js/<half>.js` (read from the package's manifest at
  configure), and the module target depends on it, so `cmake --build build --target retroplug-mesen-pack` makes the whole
  folder.
- **The test product is `packs-desktop`**: the desktop list without `mesen`. It keeps `bliptoaster`, `risa`, `smsggdj`
  and the audio packages, so the tiers' files that use them still run (M5c drops `bliptoaster` from it).
- **Two pack hosts, from the existing sources.** `retroplug-host-packs` is `src/main.cpp` built with `RP_HOST_LOADS_PACKS`
  on `packs-desktop`: `ensureLaunchPacks()` before anything, a `PackHostRuntime`, `rp_render_modules` registered and the
  pack loader installed over them before the bundle evaluates. `retroplug-ui-test-packs` is the UI harness's sources on
  `packs-desktop`'s app module set and UI bundle. Both are built by name (`EXCLUDE_FROM_ALL`).
- **The pack mode of a runner.** `--packs` (a valueless runner flag):
  - bundles against `packs-desktop`'s tree (`productGenerated`);
  - leaves external exactly the names the host registers: the render set's contracts on the native host, the app set's
    on the UI harness (`setContracts(name)`, newly exported by `build-shared-modules.mjs`);
  - prepends a prelude that awaits `loadAppPacks()` and fails the file at once when Mesen is not among `packStatus()`'s
    loaded packs (`test-native/_packsPrelude.ts`, `test-ui/_packsPrelude.ts`);
  - runs on the pack host with `RETROPLUG_PACK_PATH` naming `build/packs/mesen/<version>` (read from
    `packages/mesen/pack.json`) and empty `RETROPLUG_PACKS_DIR`/`RETROPLUG_PACKS_CACHE_DIR`;
  - writes to its own out dir, timings file and skip-baseline key (`native-packs`, `ui-packs`).
  `pnpm test:native:packs` and `pnpm test:ui:packs` build the host and the pack and run it.
- **What "unchanged" means.** The same test files, the same goldens (parity, kits, renders), byte for byte. A file that
  fails only in the pack mode is a finding: Task 9 classifies each one before anything is changed.

## File structure

| File | Change |
|---|---|
| `packages/retroplug/src/contributions/consoleRegistries.ts`, `src/consoles.ts` | The dormant real cart |
| `packages/retroplug/scripts/lib/packages.mjs` | The generator's real-cart check reads the whole tree's platforms |
| `packages/retroplug/test/contributions/consoles-live.test.ts`, `test/packages/packages-generator.test.ts`, a new `test/consoles/product-without-console.test.ts` | Its cases |
| `packages/retroplug/scripts/lib/contracts.mjs`, `tools/build-shared-modules.mjs`, `tools/build-pack-halves.mjs` | zod shared; `setContracts`; `--depfile` |
| `packages/node-sdk/native/src/graph/rp_pack.h` | `RP_API_LEVEL 3u` |
| `cmake/packs.cmake`, `packages/mesen/native/CMakeLists.txt` | `TS_HALVES` |
| root `package.json` | `packs-desktop`; `test:native:packs`, `test:ui:packs` |
| `packages/native/src/main.cpp`, `packages/native/CMakeLists.txt` | `retroplug-host-packs`, `retroplug-ui-test-packs` |
| `packages/native/test/engine-graph/golden/product-registrations.json` | Their rows |
| `packages/retroplug/scripts/run-native-tests.mjs`, `run-ui-tests.mjs`, `scripts/lib/testPool.mjs` | `--packs` |
| `packages/retroplug/test-native/_packsPrelude.ts`, `test-ui/_packsPrelude.ts` | **New.** The preludes |
| `packages/retroplug/scripts/skip-baseline.json` | `native-packs`, `ui-packs` |
| `.github/workflows/build.yml` | The Linux job's two pack steps |
| docs, spec/02, spec/06, spec/07, spec/14 | The pack mode, the hosts, the open questions |

---

### Task 1: A real cart waits for its platform

**Files:**
- Modify: `packages/retroplug/src/contributions/consoleRegistries.ts`
- Modify: `packages/retroplug/src/consoles.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (the generator's real-cart check)
- Modify: `packages/retroplug/test/contributions/consoles-live.test.ts`, `test/packages/packages-generator.test.ts`
- Create: `packages/retroplug/test/consoles/product-without-console.test.ts`

- [ ] **Step 1: The failing cases**

In `consoles-live.test.ts`, the case expecting "the real cart rp.packfixture.orphan names the platform nowhere, which no
console descriptor registers" (line ~79) becomes the dormant case. Rewrite it to: commit the orphan cart (no throw);
`realCartOf("nowhere")` is undefined and `registeredRealCarts()` does not contain it; then commit a console descriptor
declaring the platform `nowhere` (a fixture descriptor in the file's style); now `realCartOf("nowhere")` is the cart and
`registeredRealCarts()` contains it. Keep the duplicate-platform and duplicate-node cases as they are.

Create `test/consoles/product-without-console.test.ts`, following `test/graph/plugin-product.test.ts`'s way of building
against a product's tree (it generates the plugin's tree and bundles a module with `productGenerated`): generate the
`packs-host` tree, bundle a tiny entry importing `src/consoles.ts`'s `realCartOf` and `registeredRealCarts` with
`productGenerated({ dir: <build>/products/packs-host/packages/retroplug })`, evaluate it, and check it does not throw,
`realCartOf("nes")` is undefined and no registered real cart names `nes`. (Today this throws at seed: the M5b
exploration reproduced it.)

In `packages-generator.test.ts`, the case at ~1040 ("the real cart rp.alpha.cart names the platform n64 ...") stays a
refusal, but for a platform no package in the WHOLE tree declares; add a case: a scratch product list whose console
package is absent while the cart's package is present generates without error (follow the file's fixture-tree style).

- [ ] **Step 2: Run them, see them fail**

Run: `pnpm test contributions/consoles-live consoles/product-without-console packages/packages-generator`
(the pure tier takes slug filters; check `packages/retroplug/scripts/run-tests.mjs` for the form).
Expected: the dormant case and the product case fail on the throw; the generator's new case fails on its refusal.

- [ ] **Step 3: The registry and the lookups**

`consoleRegistries.ts`, the real-carts check:

```ts
/** The physical carts a project can drive. One cart per platform and one per node, because a document places at most
 *  one: the store's lookup, buildGraph's placement and the menu's jobs all resolve a platform's cart, and with two
 *  registered the answer would be whichever was registered last. A cart whose platform no console registers yet is
 *  dormant, not an error (docs/downloadable-packs-m5b-plan.md): a built-in cart beside a console a pack brings (the N8
 *  beside Mesen's NES) waits for the pack's consoles, and src/consoles.ts lists only the carts whose platform is
 *  registered, so a dormant cart is never offered or placed. */
export const realCartsRegistry = new Registry<RealCart>("realCarts", (entries) => {
  const cartPlatforms = new Map<string, string>();
  const cartNodes = new Map<string, string>();
  for (const { item: realCart } of entries) {
    // (the two duplicate checks, unchanged)
  }
});
```

`consoles.ts`: in `index`, register a cart only when its platform is registered, and keep the active list:

```ts
  const realCarts: RealCart[] = [];
  for (const cart of realCartsRegistry.items()) {
    // A cart whose platform no console registers yet is dormant (consoleRegistries.ts): absent from every lookup.
    if (!byPlatform.has(cart.platform)) continue;
    realCartByPlatform.set(cart.platform, cart);
    realCarts.push(cart);
  }
```

add `readonly realCarts: readonly RealCart[];` to `ConsoleIndex` and return it, and make `registeredRealCarts()` return
`index().realCarts` with its comment saying "every registered physical cart whose platform a console registers".

`packages.mjs`: find the generator's real-cart check (grep "which no console descriptor registers"); make it collect
platforms from every package in the tree (not only the product's list) before refusing. If the generator has no tree
view at that point, report it and keep the product-level refusal but make it skip carts whose platform's package is
absent from the list (so `packs-host` and `packs-desktop` generate).

- [ ] **Step 4: Run them, see them pass**

Run: `pnpm test` (whole pure tier). Expected: all pass, the three new or changed cases included.

- [ ] **Step 5: Commit**

```sh
git add packages/retroplug/src/contributions/consoleRegistries.ts packages/retroplug/src/consoles.ts \
        packages/retroplug/scripts/lib/packages.mjs packages/retroplug/test
git commit -m "consoles: a real cart whose platform no console registers waits for one, so a product without that console boots"
```

---

### Task 2: zod is one shared module

**Files:**
- Modify: `packages/retroplug/scripts/lib/contracts.mjs`
- Modify: `tools/build-shared-modules.mjs`, `tools/build-pack-halves.mjs`
- Modify: `packages/node-sdk/native/src/graph/rp_pack.h`
- Modify: `packages/retroplug/test/script/build-pack-halves.test.ts`

- [ ] **Step 1: The failing case**

In `build-pack-halves.test.ts`, add: building Mesen's halves into a temporary dir (the tool takes `<package dir> <out dir>
<template>`; use `packages/mesen/pack.json` as the template) gives a `js/app.js` that imports `"zod"` (an `import ...
from "zod"` statement) and carries none of zod's own source (no `ZodError` class definition, a stable marker; check
the string the tool currently bundles, e.g. `class ZodError` or `$ZodError`), and is under 64 KiB.

Run: `pnpm test script/build-pack-halves`. Expected: FAIL (app.js is ~550 KB and bundles zod).

- [ ] **Step 2: zod in every set and in a pack's externals**

`contracts.mjs`, after `UI_IMPORTS`:

```js
/** The libraries every shared module set carries as one named module, which a pack's halves import rather than bundle
 *  (docs/downloadable-packs-m5b-plan.md): a role config's schema a pack builds composes with the host's configFields,
 *  and one schema built from two zod instances is two type systems; one copy also keeps a pack's app half small. Rule 9
 *  does not judge these (it names @retroplug/* specifiers), and a kinds half still refuses them (its script contexts
 *  carry no schema library). Their versions are part of the API level (graph/rp_pack.h). */
export const SHARED_LIBRARIES = [
  { specifier: "zod", reason: "the schema library role configs, node settings and the stores' configs are built with" },
];
```

`build-shared-modules.mjs`: import `SHARED_LIBRARIES` and add `...SHARED_LIBRARIES.map((e) => e.specifier)` to the
`contracts` of all three sets (`app`, `cli`, `render`). Export the contracts of a set for the runners (Task 7, 8):

```js
/** The names a set's module table registers: what a bundle built to run beside that table leaves external. */
export function setContracts(name) {
  return setOf(name).contracts;
}
```

`build-pack-halves.mjs` `packExternals()`: add `...SHARED_LIBRARIES.map((e) => e.specifier)`.

`rp_pack.h`: `#define RP_API_LEVEL 3u`, and extend its comment's list ("the C ABI in rp_node.h, SystemBase and the contract
headers, the TS contract modules") with "and the shared libraries they are built with (contracts.mjs' SHARED_LIBRARIES:
zod's major version)".

- [ ] **Step 3: Build, run, see it pass**

```sh
cmake --build build -j$(nproc)
pnpm test script/build-pack-halves
pnpm test
pnpm test:ui packs-ts-fixture
build/bin/retroplug-packs-test 2>&1 | tail -2
```
Expected: all pass. The full build rebuilds every set with zod as a module (check
`build/native/app-modules/shared.tsv` lists `zod`). `packs-ts-fixture` proves a pack's app half still links in the UI
harness.

- [ ] **Step 4: Commit**

```sh
git add packages/retroplug/scripts/lib/contracts.mjs tools/build-shared-modules.mjs tools/build-pack-halves.mjs \
        packages/node-sdk/native/src/graph/rp_pack.h packages/retroplug/test/script/build-pack-halves.test.ts
git commit -m "packs: zod is one shared module a pack's halves import, and the API level is 3"
```

---

### Task 3: Mesen's pack folder holds all its halves

**Files:**
- Modify: `cmake/packs.cmake` (`retroplug_pack_module`, `retroplug_pack`)
- Modify: `tools/build-pack-halves.mjs` (`--depfile`)
- Modify: `packages/mesen/native/CMakeLists.txt`
- Modify: `packages/native/test/packs/MesenPack.test.cpp`

- [ ] **Step 1: The failing case**

In `MesenPack.test.cpp`, add a case "the Mesen pack's folder names its native and TS halves, each present"
(`[packs-mesen]`): read `mesenPack() / "pack.json"` (std::ifstream, then `rfl::json::read<rfl::Generic>` or the
PackManifest reader the table uses: `rp::host::packs::readManifest` or its tree's name), and check `halves` holds exactly
`native: "rp-mesen"`, `app: "js/app.js"`, `consoles: "js/consoles.js"`, `ui: "js/ui.js"`, and that each JS file exists.
Also check the table's load of the folder succeeds as before (the first case does).

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-mesen]"`.
Expected: the new case FAILS (halves.native only).

- [ ] **Step 2: The depfile**

`build-pack-halves.mjs`:
- Accept `--depfile <path>` in its CLI argument parsing (beside `--only`, `--mqjs`, `--runner`).
- Ask each ESM half's esbuild build for `metafile: true` (the kinds half already does) and collect every build's
  `metafile.inputs` into one object.
- When `--depfile` was given, after `pack.json` is written: `writeDepfile(depfile, join(outDir, "pack.json"), merged)`
  with `writeDepfile` from `tools/esbuild-shared.js` (`createRequire(import.meta.url)("./esbuild-shared.js")`). Check
  its signature (`(depPath, target, metafile)`) and that it writes absolute paths.

- [ ] **Step 3: `TS_HALVES`**

`cmake/packs.cmake`, `retroplug_pack_module`: add the one-value keyword `TS_HALVES` (a package directory) and document
it: "builds the package's TS halves into the folder with tools/build-pack-halves.mjs, which then writes the folder's
pack.json from the stamped template (the folder's one writer of it); the module target depends on that build".

- When `M_TS_HALVES` is set, skip the configure-time `file(COPY_FILE ... "${_dir}/pack.json" ...)` (keep writing the
  template to `${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json`).
- Read the halves the tool will write from the package's manifest at configure:

```cmake
    if(M_TS_HALVES)
        file(READ "${M_TS_HALVES}/package.json" _pkg)
        set(_halves_out "${_dir}/pack.json")
        foreach(_half app consoles ui views cli)
            string(JSON _entry ERROR_VARIABLE _err GET "${_pkg}" retroplug ${_half})
            if(NOT _err)
                list(APPEND _halves_out "${_dir}/js/${_half}.js")
            endif()
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${M_TS_HALVES}/package.json")
        set(_depfile "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}-halves.d")
        add_custom_command(
            OUTPUT ${_halves_out}
            COMMAND ${NODE_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs" "${M_TS_HALVES}" "${_dir}"
                    "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" --depfile "${_depfile}"
            DEPFILE "${_depfile}"
            DEPENDS "${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs"
                    "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json"
                    "${M_TS_HALVES}/package.json"
                    "${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs"
                    "${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json"
                    "${_rp_pack_header}"
            COMMENT "Building ${target}'s TS halves into ${_dir}"
            VERBATIM)
        add_custom_target(${target}-halves DEPENDS ${_halves_out})
    endif()
```

  and after `add_library(${target} MODULE ...)`, `if(M_TS_HALVES) add_dependencies(${target} ${target}-halves) endif()`.
  (`NODE_EXECUTABLE` is what other custom commands use; check its name in this tree. `_rp_pack_header` is set at the
  top of packs.cmake.) A platform with no pack folder (`RP_PACK_PLATFORM` empty) returns before the library: build the
  halves target anyway, or return before it too, consistently with the module (report which).
- `retroplug_pack(<package> [MANIFEST <file>] [TS_HALVES])`: a valueless `TS_HALVES` passes `TS_HALVES
  "${RP_PACKAGES_DIR}/${package}"` on.

`packages/mesen/native/CMakeLists.txt`: `retroplug_pack(mesen TS_HALVES)`, and replace its comment "Only the native
half: the TS halves join a pack in M2" with one saying the pack carries its native module and its app, consoles and ui
halves (docs/downloadable-packs-m5b-plan.md).

- [ ] **Step 4: Build, run, see it pass, and the depfile works**

```sh
cmake --build build --target retroplug-mesen-pack -j$(nproc)
cat build/packs/mesen/0.1.0/pack.json
build/bin/retroplug-packs-test "[packs-mesen]"
touch packages/mesen/ts/consoles.ts && cmake --build build --target retroplug-mesen-pack -j$(nproc) 2>&1 | grep -c "TS halves"
```
Expected: `pack.json` names the four halves, the cases pass, and touching a bundled source re-runs the halves (count 1).
Also run the whole `build/bin/retroplug-packs-test` and `build/bin/retroplug-packs-heap-test` (the `mesen-heap` pack is
native only and unchanged).

- [ ] **Step 5: Commit**

```sh
git add cmake/packs.cmake tools/build-pack-halves.mjs packages/mesen/native/CMakeLists.txt \
        packages/native/test/packs/MesenPack.test.cpp
git commit -m "packs: the Mesen pack's folder carries its app, consoles and ui halves, built by the folder's one pack.json writer with a depfile"
```

---

### Task 4: The test product

**Files:**
- Modify: root `package.json` (`retroplug.products`)
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json` (in Tasks 5 and 6, as each product lands)

- [ ] **Step 1: The list**

Add to `retroplug.products`, after `packs-host`:

```json
"packs-desktop": ["core", "audio", "audio-portaudio", "audio-sdl", "midi", "launchpad", "system", "sameboy", "dsp", "mgb", "lsdj", "lsdj-launchpad", "graph-runtime", "n8-wire", "n8", "smsggdj", "risa", "host-io", "native", "bliptoaster"]
```

(the desktop list in its order, without `mesen`: check against the file's `desktop` entry and copy it minus `mesen`).
Add a comment wherever the lists are described (the root manifest has none; `CMakeLists.txt`'s product-list comment and
spec/06's products paragraph name the lists: add "packs-desktop: the desktop's without mesen, the product the pack
mode's test hosts are built on (docs/downloadable-packs-m5b-plan.md)").

- [ ] **Step 2: It configures and generates**

```sh
cmake -S . -B build >/dev/null && echo configured
node packages/retroplug/scripts/lib/packages.mjs --product packs-desktop --build-dir build && ls build/products/packs-desktop/packages/retroplug/src/generated
pnpm typecheck
```
(check the generator's CLI form in `packages.mjs`). Expected: configured, the tree generated, typecheck passes
(`scripts/typecheck-products.mjs` typechecks every non-desktop tree).

- [ ] **Step 3: Commit**

```sh
git add package.json CMakeLists.txt spec/06-build-test.md
git commit -m "products: packs-desktop, the desktop's list without mesen, for the hosts that load Mesen as a pack"
```

---

### Task 5: `retroplug-host-packs`

**Files:**
- Modify: `packages/native/src/main.cpp`
- Modify: `packages/native/CMakeLists.txt`
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json`

- [ ] **Step 1: The host loads launch packs when built to**

`main.cpp`, at the includes:

```cpp
#if RP_HOST_LOADS_PACKS
#include "host/js/PackModules.hpp"     // PackHostRuntime, installPackLoader
#include "host/js/SharedModules.hpp"   // registerSharedModules
#include "host/packs/ProcessPacks.hpp" // ensureLaunchPacks, processPacks

// The product's render module set (the shell's contract modules and the shared libraries, for a host with no UI):
// what a test bundle built in the runner's pack mode leaves external, and what a pack's halves link to.
extern const rp_js_module  rp_render_modules[];
extern const std::uint32_t rp_render_modules_count;
#endif
```

(check the type name of a module table entry, `rp_js_module` in `RenderHost.cpp`, and the headers' paths and namespaces
in this tree). In `main()`, before `TjsHostRuntime host;`:

```cpp
#if RP_HOST_LOADS_PACKS
    // The pack mode's host (docs/downloadable-packs-m5b-plan.md): the launch packs load before anything else, since an
    // Engine reads the process's table as it is built and a pack's kind items join it only then; the runtime then
    // settles the packs' modules before it is freed.
    rp::host::packs::ensureLaunchPacks();
    rp::host::PackHostRuntime host;
#else
    TjsHostRuntime host;
#endif
```

and after `JSContext* ctx = host.context();`:

```cpp
#if RP_HOST_LOADS_PACKS
    // The contract modules as named modules, and the pack loader serving pack:<id>/<half> against the same names, before
    // anything evaluates: a test bundle built with those names external shares one instance of each with a pack's
    // halves, so the halves its prelude loads commit into the registries the test reads.
    if (const std::string why = rp::host::registerSharedModules(ctx, rp_render_modules, rp_render_modules_count);
        !why.empty()) {
        std::fprintf(stderr, "[retroplug-host] the shared modules did not register: %s\n", why.c_str());
        return 1;
    }
    rp::host::installPackLoader(ctx, rp::host::packs::processPacks(), rp_render_modules, rp_render_modules_count);
#endif
```

- [ ] **Step 2: The target**

`packages/native/CMakeLists.txt`, after `retroplug-host`'s block:

```cmake
# retroplug-host-packs: the same test host on packs-desktop (the desktop's list without mesen), loading the launch packs
# (RP_HOST_LOADS_PACKS): the native tier's pack mode runs every file on it with Mesen loaded as a pack
# (docs/downloadable-packs-m5b-plan.md, `pnpm test:native:packs`). Built by name.
add_executable(retroplug-host-packs EXCLUDE_FROM_ALL src/main.cpp src/host/SessionPump.cpp)
target_link_libraries(retroplug-host-packs PRIVATE retroplug-backend retroplug-tjs-runtime retroplug-launchpad-harness)
target_compile_definitions(retroplug-host-packs PRIVATE RP_HOST_LOADS_PACKS=1)
retroplug_product(retroplug-host-packs PACKAGES ${RP_PRODUCT_PACKAGES_packs-desktop} PROVIDERS n8 launchpad midi)
target_include_directories(retroplug-host-packs PRIVATE ${CMAKE_SOURCE_DIR}/packages/n8/native/test
                                                        ${CMAKE_SOURCE_DIR}/packages/launchpad/native/test
                                                        ${CMAKE_SOURCE_DIR}/packages/midi/native/test-support)
set_target_properties(retroplug-host-packs PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(TARGET retroplug-mesen-pack)
    add_dependencies(retroplug-host-packs retroplug-mesen-pack)  # the pack its suite loads
endif()
```

If `retroplug_product` of a host PROVIDES n8/launchpad/midi only on the desktop list, follow what it requires. If the
render module set's symbol is not linked into a product binary by `retroplug_product` (it is for the render worker),
report how it reaches `retroplug-host-packs`.

The golden: add `"retroplug-host-packs"` to `product-registrations.json` with the node types `retroplug-host`'s row
holds minus every `rp.mesen.*` type, in order (or run `build/bin/retroplug-engine-graph-test` and copy the row its
failure prints; keep the keys sorted).

- [ ] **Step 3: A first file on it, by hand**

```sh
cmake --build build --target retroplug-host-packs retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test 2>&1 | tail -2
```
Expected: builds; the golden case passes. (Running a test on it needs Task 7's bundles.)

- [ ] **Step 4: Commit**

```sh
git add packages/native/src/main.cpp packages/native/CMakeLists.txt \
        packages/native/test/engine-graph/golden/product-registrations.json
git commit -m "native: retroplug-host-packs, the test host on packs-desktop, loads the launch packs and serves the shell's contracts to them"
```

---

### Task 6: `retroplug-ui-test-packs`

**Files:**
- Modify: `packages/native/CMakeLists.txt`
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json`

- [ ] **Step 1: The target**

After `retroplug-ui-test`'s block, a twin on `packs-desktop`'s module set and UI bundle (the plugin's
`retroplug_bundles(plugin)` and `RP_BUNDLE_plugin_*` are the pattern):

```cmake
# retroplug-ui-test-packs: the UI harness on packs-desktop's app module set and UI bundle, so the UI tier's pack mode
# runs every file with Mesen loaded as a pack (docs/downloadable-packs-m5b-plan.md, `pnpm test:ui:packs`). The harness
# loads the launch packs already (UiHarness.hpp). Built by name.
set_property(GLOBAL PROPERTY RP_BUNDLE_PACKAGES_packs-desktop "${RP_PRODUCT_PACKAGES_packs-desktop}")
retroplug_bundles(packs-desktop)
add_executable(retroplug-ui-test-packs EXCLUDE_FROM_ALL
    test/ui/UiRunner.cpp
    test/ui/UiHarness.cpp
    test/ui/RenderCore.cpp
    ${RP_BUNDLE_packs-desktop_MODULES_C}   # packs-desktop's shared modules (rp_app_modules)
    ${RP_BUNDLE_packs-desktop_UI_C})       # its UI bytecode (rp_ui_bundle)
target_link_libraries(retroplug-ui-test-packs PRIVATE lvgl-js-native retroplug-backend retroplug-tjs-runtime)
retroplug_product(retroplug-ui-test-packs PACKAGES ${RP_PRODUCT_PACKAGES_packs-desktop})
target_include_directories(retroplug-ui-test-packs PRIVATE ${RP_QUICKJS_INCLUDE})
set_target_properties(retroplug-ui-test-packs PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(TARGET retroplug-mesen-pack)
    add_dependencies(retroplug-ui-test-packs retroplug-mesen-pack)
endif()
```

Copy every other property and dependency `retroplug-ui-test` has (its `add_dependencies` on the app-modules target, its
compile definitions), using `packs-desktop`'s targets (`RP_BUNDLE_packs-desktop_*` names them). If `retroplug_bundles`
must run before `retroplug_product` names the list, or the reverse, follow the plugin's order.

The golden row for `retroplug-ui-test-packs`: `retroplug-ui-test`'s minus every `rp.mesen.*` type.

- [ ] **Step 2: Build**

```sh
cmake --build build --target retroplug-ui-test-packs retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test 2>&1 | tail -2
```
Expected: builds (the packs-desktop control plane and UI bundles build too); the golden case passes.

- [ ] **Step 3: Commit**

```sh
git add packages/native/CMakeLists.txt packages/native/test/engine-graph/golden/product-registrations.json
git commit -m "native: retroplug-ui-test-packs, the UI harness on packs-desktop's bundles"
```

---

### Task 7: The native runner's pack mode

**Files:**
- Modify: `packages/retroplug/scripts/run-native-tests.mjs`
- Modify: `packages/retroplug/scripts/lib/testPool.mjs` (`VALUELESS_FLAGS`)
- Create: `packages/retroplug/test-native/_packsPrelude.ts`
- Modify: root `package.json` (`test:native:packs`)

- [ ] **Step 1: The prelude**

`packages/retroplug/test-native/_packsPrelude.ts` (no `.test.ts` suffix, so the runner's walk never takes it as a test):

```ts
// The first module of every bundle the native runner builds in its pack mode (scripts/run-native-tests.mjs --packs,
// docs/downloadable-packs-m5b-plan.md): the launch packs' app and consoles halves commit into this bundle's registries
// before any of the test's own modules evaluate (a top-level await: the modules imported after this one wait for it),
// and a run whose Mesen pack did not load fails here, naming why, rather than in whichever case first needs a NES.
import { loadAppPacks, packStatus } from "../src/packs/loadPacks";

await loadAppPacks();
const status = packStatus();
if (!status.loaded.mesen) {
  throw new Error(`the pack mode's Mesen pack did not load: ${JSON.stringify(status)}`);
}
```

(`packStatus()` returns `{ loaded: Record<string, HalfKind[]>; refused: Record<string, string> }`.)

- [ ] **Step 2: The mode**

`testPool.mjs`: `const VALUELESS_FLAGS = new Set(["--update-skip-baseline", "--packs"]);`.

`run-native-tests.mjs`:

```js
import { readFileSync } from "node:fs";
import { productGenerated } from "../../../tools/product-generated.js";   // CJS: use createRequire if import fails
import { setContracts } from "../../../tools/build-shared-modules.mjs";

// The pack mode (docs/downloadable-packs-m5b-plan.md): every file again, on retroplug-host-packs, a host of
// packs-desktop (the desktop's list without mesen) that loads Mesen as a pack. A bundle reads that product's generated
// registries, leaves the host's shared contract modules (the render set's names, zod among them) external so the test
// and the pack's halves share one instance of each, and starts with a prelude that loads the packs' halves.
const PACKS = process.argv.slice(2).includes("--packs");
const PACK_PRODUCT = "packs-desktop";
const MESEN_VERSION = JSON.parse(readFileSync(join(REPO, "packages/mesen/pack.json"), "utf8")).version;
const MESEN_PACK = join(REPO, "build/packs/mesen", MESEN_VERSION);
const PRODUCT_TREE = join(REPO, "build/products", PACK_PRODUCT, "packages/retroplug");
const PRELUDE = join(TEST_DIR, "_packsPrelude.ts");
const EXTERNALS = PACKS ? setContracts("render") : [];
```

- `generate()` becomes `generate(PACKS ? { product: PACK_PRODUCT } : {})` (the desktop's call stays as it was).
- `OUT_DIR` becomes `join(PKG, PACKS ? ".native-build-packs" : ".native-build")`.
- `HOST` defaults to `build/bin/retroplug-host-packs[.exe]` in the pack mode (override `RETROPLUG_HOST_PACKS`), and the
  missing-binary message names that target. In the pack mode, also refuse a missing `MESEN_PACK/pack.json`
  ("build it: cmake --build build --target retroplug-mesen-pack").
- The build in `runOne`, in the pack mode: the entry is a stdin module importing the prelude and then the file, and
  the two plugin and external options are added:

```js
      ...(PACKS
        ? {
            entryPoints: undefined,
            stdin: {
              contents: `import ${JSON.stringify(PRELUDE)};\nimport ${JSON.stringify(file)};\n`,
              resolveDir: dirname(file),
              sourcefile: `${slug}.packs-entry.js`,
              loader: "js",
            },
            external: EXTERNALS,
            plugins: [productGenerated({ dir: PRODUCT_TREE })],
          }
        : {}),
```

  (put it after the existing options so it overrides `entryPoints`; esbuild refuses both `entryPoints` and `stdin`, so
  build the options object conditionally if spreading leaves `entryPoints` set.)
- The env, in the pack mode: `RETROPLUG_PACK_PATH: MESEN_PACK`, `RETROPLUG_PACKS_DIR: <cfgDir>/packs`,
  `RETROPLUG_PACKS_CACHE_DIR: <cfgDir>/packs-cache`. In the desktop mode, delete an inherited `RETROPLUG_PACK_PATH` so
  a developer's never reaches the desktop host (it loads none today, but the mode should not depend on that).
- Timings: `.test-timings/${PACKS ? "native-packs" : "native"}.json`; the skip-baseline key `PACKS ? "native-packs" :
  "native"`; the summary lines say `native (packs)` in the pack mode.

Root `package.json`:

```json
"test:native:packs": "node scripts/cmake-build.js retroplug-host-packs retroplug-mesen-pack && node packages/retroplug/scripts/run-native-tests.mjs --packs",
```

- [ ] **Step 3: One file, then a handful**

```sh
pnpm test:native:packs app-play-nes
pnpm test:native:packs parity/blocks-nes
pnpm test:native:packs sms-render
pnpm test:native:packs cli-profile
```
Expected: each passes as it does in `pnpm test:native <slug>`. If the prelude throws, its message names the refusal; if a
bundle fails to build, the external list or the product tree is the first suspect. Stop and report anything that does not
pass with the evidence (the file's output), without changing tests.

- [ ] **Step 4: Commit**

```sh
git add packages/retroplug/scripts/run-native-tests.mjs packages/retroplug/scripts/lib/testPool.mjs \
        packages/retroplug/test-native/_packsPrelude.ts package.json
git commit -m "test: the native runner's pack mode runs a file on retroplug-host-packs with Mesen loaded as a pack"
```

---

### Task 8: The UI runner's pack mode

**Files:**
- Modify: `packages/retroplug/scripts/run-ui-tests.mjs`
- Create: `packages/retroplug/test-ui/_packsPrelude.ts`
- Modify: root `package.json` (`test:ui:packs`)

- [ ] **Step 1: The prelude and the mode**

`test-ui/_packsPrelude.ts`: the native prelude's text with its comment naming `run-ui-tests.mjs --packs` and saying why
the UI tier needs it too: a UI test's bundle runs in the UI's own context, and a file that imports the shell's stores
(`working-song-smsggdj`, `recent-smsggdj`, `kit-compile`, `scriptedRenders.ts`'s users) carries its own copy of the
registries, apart from the UI bundle's, which `loadUiPacks()` fills.

`run-ui-tests.mjs`, as Task 7 does for the native runner:
- `--packs` (now valueless): `generate({ product: "packs-desktop" })`, the out dir `.ui-build-packs` (check the
  current name), `HOST` defaulting to `build/bin/retroplug-ui-test-packs[.exe]` (override `RETROPLUG_UI_TEST_PACKS`),
  timings `ui-packs`, skip key `ui-packs`.
- The bundle: a stdin entry importing the prelude then the file, `external: setContracts("app")`, and
  `plugins: [productGenerated({ dir: PRODUCT_TREE })]`, keeping the `ui-harness` alias.
- The env: `RETROPLUG_PACK_PATH` is the Mesen pack's folder, joined (`:` or `;`) with the slug's `PACK_TESTS` folders.
  `packPath`'s hard-coded `1.0.0` becomes each fixture's own version: read it from the fixture's staged
  `build/packs/<id>/*/pack.json` (the one version folder there), so a Mesen `0.1.0` and a fixture `1.0.0` mix.

Root `package.json`:

```json
"test:ui:packs": "node scripts/cmake-build.js retroplug-ui-test-packs retroplug-mesen-pack retroplug-ts-fixture-packs && node packages/retroplug/scripts/run-ui-tests.mjs --packs",
```

- [ ] **Step 2: A handful**

```sh
pnpm test:ui:packs audio-routing-nes
pnpm test:ui:packs recent-smsggdj
pnpm test:ui:packs packs-ts-fixture
pnpm test:ui:packs bliptoaster-settings
```
Expected: each passes as in `pnpm test:ui <slug>`. Report anything else with its output, without changing tests.

- [ ] **Step 3: Commit**

```sh
git add packages/retroplug/scripts/run-ui-tests.mjs packages/retroplug/test-ui/_packsPrelude.ts package.json
git commit -m "test: the UI runner's pack mode runs a file on retroplug-ui-test-packs with Mesen loaded as a pack"
```

---

### Task 9: Both tiers against the pack: run and triage

**Files:** none until the triage is reviewed.

- [ ] **Step 1: Run both tiers whole, in both modes**

```sh
pnpm test:native 2>&1 | tail -6
pnpm test:native:packs 2>&1 | tail -8
pnpm test:ui 2>&1 | tail -6
pnpm test:ui:packs 2>&1 | tail -8
```
(foreground with a generous timeout, or background with a completion notice; each takes minutes.)

- [ ] **Step 2: Report, do not fix**

For every file that fails in a pack mode and passes in its desktop mode, report: the slug, the failing case, its
assertion and output, and a first classification:
- **(a) registration order:** a pack's consoles register after the product's, so an order a test reads (the platforms,
  the extensions, a menu's rows, a recorded matrix) differs;
- **(b) a path the pack does not reach:** something the built-in Mesen provides that the pack's halves or module do not
  (a role, a contribution kind, an embed, a CLI tool, a host hook, an import the bundle resolved to its own copy);
- **(c) the mode itself:** the prelude, the externals, the product tree, the env;
- **(d) other.**

Also report the pack modes' skip counts against the desktop modes', and any file whose output differs in a golden
(a parity, kit or render hash): that is never noise (AGENTS.md).

The controller decides each fix (a test change only where the test reads an order that is not a product promise; a
code fix otherwise) and hands them back as follow-up steps of this task, each its own commit.

- [ ] **Step 3: The skip baselines**

Once both pack modes pass: `pnpm test:native:packs --update-skip-baseline` and `pnpm test:ui:packs
--update-skip-baseline`, check `scripts/skip-baseline.json`'s new `native-packs` and `ui-packs` keys hold the same files
and counts as `native` and `ui` (a pack-only skip is a finding to report), and commit:

```sh
git add packages/retroplug/scripts/skip-baseline.json
git commit -m "test: the pack modes' skip baselines"
```

---

### Task 10: CI runs the pack modes on Linux

**Files:**
- Modify: `.github/workflows/build.yml` (the `linux` job)

- [ ] **Step 1: The steps**

In the `linux` job, after its `pnpm test:ui` step, add two steps in the same shape (the same env the tier steps set:
copy whatever `test:native` and `test:ui` have, e.g. `RP_PARITY_REQUIRE_TARGET` for the native one):

```yaml
      - name: Native tier with Mesen loaded as a pack
        run: pnpm test:native:packs
      - name: UI tier with Mesen loaded as a pack
        run: pnpm test:ui:packs
```

Not on `linux-arm64`, `macos` or `windows` (the user's decision: Linux only).

- [ ] **Step 2: Commit**

```sh
git add .github/workflows/build.yml
git commit -m "ci: the Linux job runs the native and UI tiers with Mesen loaded as a pack"
```

---

### Task 11: Docs, verification and the CI gate

**Files:**
- Modify: `docs/downloadable-packs-loading-design.md` (section 5), `spec/02-native-host.md` (Packs), `spec/06-build-test.md`,
  `spec/07-remaining-work.md`, `spec/14-hosts-harnesses.md`
- Modify: `docs/downloadable-packs-m5b-plan.md` (After the reviews, design questions)

- [ ] **Step 1: The docs**

- The loading design's section 5: the proof for Mesen is built (the test product is `packs-desktop`, the tiers' pack
  mode), BlipToaster's is M5c, the CLI's its own milestone.
- spec/02's Packs section: the dormant real cart; zod as a shared library; a pack folder's one `pack.json` writer
  (`TS_HALVES`); the two pack hosts.
- spec/06: the `packs-desktop` list; rows for `retroplug-host-packs` and `retroplug-ui-test-packs`; the `test:native:packs`
  and `test:ui:packs` scripts and what they prove; the skip-baseline keys; which command proves a change to the pack path.
- spec/14: the two pack hosts beside their desktop twins.
- spec/07's Downloadable packs entry: M5b done; what is left is the CLI on a pack, M5c, M5d (and M3, M4); add "Open from
  M5b" with this plan's design questions.

- [ ] **Step 2: Verify**

```sh
pnpm typecheck && pnpm check:boundaries && pnpm check:docs
cmake --build build -j$(nproc)
mkdir -p .superpowers/verify/runs/m5b
env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=.superpowers/verify/runs/m5b/tv bash .superpowers/verify/tip-verify-par.sh
bash .superpowers/verify/compare.sh .superpowers/verify/runs/m5a/tv .superpowers/verify/runs/m5b/tv
pnpm test:native:packs && pnpm test:ui:packs
```
(the tip verify in the background with a completion notice). Expected: the compare differs from M5a's only by additions;
both pack modes pass; the known `app-cores` GBA case is the only native failure in either mode.

- [ ] **Step 3: The plan's own record and commit**

Append "## After the reviews" and "## Design questions this plan found" to this plan (at least the ones below), then:

```sh
git add docs spec
git commit -m "docs: Mesen as a whole pack, the test product, the pack hosts and the tiers' pack modes"
```

Design questions to carry (answered or left open):
1. **Registration order.** A pack's consoles register after the product's, so the platform order (menus, extension
   lists, a detector rank tie's id order is stable, but list positions are not) differs from a product that links the
   console. Part 2 drops Mesen from the desktop: does a product want a pack's console placed where a built-in one was?
2. **zod's version is the API's.** A host moving zod's major version breaks every pack: the level must move with it.
3. **Two registries in a UI context.** In the UI tier's pack mode a test that imports the shell's stores loads the packs
   into its own registries beside the UI bundle's: one module instance per half, committed twice.
4. **The pack hosts are built by name and run on Linux CI only**; macOS and Windows run no pack mode.
5. **A dormant real cart and placeholders (M3).** A project naming a real cart's platform before its console's pack is
   installed places the system without the cart; M3's placeholders decide how such a project shows and saves.
6. **The render set as a test host's contracts.** A native test that imports a UI contract module keeps its own copy in
   the pack mode (it is not in the render set): harmless for a module with no registry, a split for one with.

- [ ] **Step 4: The CI gate**

Ask the user to push `feature/modularize`. Then dispatch `build.yml` and read every job: the linux job's two new steps
must run and pass; the other jobs as before.

---

## After the reviews

What the build found that the plan did not say:

- **No generator change.** The generator has no platform check of its own, so there is no generate-time refusal and
  Task 1's generator step was unnecessary. A real cart whose platform no console registers is dormant
  (`src/consoles.ts` lists only active carts); a desktop-product case guards that every cart there is active, a typo
  guard.
- **The depfile resolves against each build's working dir.** `build-pack-halves.mjs` writes inputs relative to the
  directory the build ran in, and a `PRE_LINK` `mkdir` creates `native/<platform>/` before the library links.
- **es2022.** The pack modes' prelude awaits `loadAppPacks()` at top level, so the bundles target es2022.
- **The UI prelude goes only to files reaching `src/`.** A metafile pass decides: a second copy of the shell in the UI's
  context displaced device tests' fakes (audio-driver, audio-device, midi-settings, game-input failed).
- **Baselines were recorded fixture-less** (no risa or resources trees), as CI runs: identical files and counts to the
  desktop modes. `engine-run`'s recorded total in the desktop key is stale (5 against the actual 13), not a pack
  difference.
- **Results:** the whole native tier passes in the pack mode but the known machine-local `app-cores` GBA case (176/177,
  as in the desktop mode); the whole UI tier 50/50.

## Design questions this plan found

1. **Registration order.** A pack's consoles register after the product's, so the platform order (menus, extension
   lists) differs from a product that links the console. Part 2 drops Mesen from the desktop: does a product want a
   pack's console placed where a built-in one was?
2. **zod's version is the API's.** A host moving zod's major version breaks every pack: the level must move with it.
3. **Two registries in a UI context.** In the UI tier's pack mode a test that imports the shell's stores loads the packs
   into its own registries beside the UI bundle's: one module instance per half, committed twice.
4. **The pack hosts are built by name and run on Linux CI only**; macOS and Windows run no pack mode.
5. **A dormant real cart and placeholders (M3).** A project naming a real cart's platform before its console's pack is
   installed places the system without the cart; M3's placeholders decide how such a project shows and saves.
6. **The render set as a test host's contracts.** A native test that imports a UI contract module keeps its own copy in
   the pack mode (it is not in the render set): harmless for a module with no registry, a split for one with.
7. **The second-copy hazard, generalised.** Any bundle beside the UI that carries its own shell copy can displace a
   global hook, as the UI prelude did to the device tests' fakes.
8. **zod subpaths are not shared modules.** `zod/v4` and the like are not in the sets, so a pack importing one would not
   resolve.
9. **The full build builds more.** `retroplug_bundles(packs-desktop)` puts its app-modules target in ALL.
10. **Registration order is unread by the tiers.** No registration-order failure appeared, so no test reads the order a
    pack's console lands in: should a UI test pin it?
