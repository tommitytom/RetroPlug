# Downloadable packs: the CLI on a pack. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `retroplug-cli` is built per product. A CLI built without Mesen and BlipToaster gets both from packs: its
dispatcher runs BlipToaster's cli half as a command, and its `run` / `test` / `x.js` sessions build NES and SMS systems
on the Mesen pack. The whole CLI end-to-end suite passes against it, as it does against the desktop CLI, smsggdj render
golden included.

**Architecture:**
- **Every product's tree carries its CLI tools.** The generator writes `cli/generated/tools.ts` for every product, not
  only the desktop. `tools/product-generated.js` points the shell's `cli/` sources at the product's copy, and
  `retroplug_bundles()` lists that file among the product generator's outputs.
- **The embedded SDK is a host of the cli shared set.** A session's `import ... from "retroplug-cli"` resolves to a
  module built in the same esbuild split as the dispatcher (`rp:sdk`, entry `cli/sdkModule.ts`). Its contract modules
  are therefore the set's shared ones, the same modules a pack's TS halves link to. `cli/sdkModule.ts` awaits
  `loadAppPacks()` at the top level and then re-exports `cli/sdk.ts`, so the launch packs' app, consoles, protocol,
  kinds and placer halves commit before a session's body runs.
- **A session child loads the launch packs.** `cli/main.cpp` calls `ensureLaunchPacks()` for a session file as it does
  for the dispatcher, then registers the cli set and the pack loader before the SDK module. The user decided
  (2026-11-27) that a session sees the same packs as the dispatcher and the app.
- **A CLI per product.** `packages/native/cmake/cli.cmake`'s `retroplug_cli(<target> <product>)` builds a product's
  dispatcher, SDK and shared set and links the launcher against them. `retroplug-cli` is
  `retroplug_cli(retroplug-cli desktop)`. `retroplug-cli-packs` (packs-desktop, test-only, built by name) is the second
  one.
- **The suite's pack mode.** `tools/run-cli-ts-tests.sh --packs` runs the whole script against `retroplug-cli-packs`,
  with BlipToaster and Mesen on every command's pack path. A case with a pack path of its own adds to that path. Linux CI
  runs it as `pnpm test:cli-ts:packs`, as the user decided (2026-11-27).

**Tech Stack:** C++20, CMake, txiki/QuickJS (`cli/main.cpp`, `host/js/SharedModules`, `host/js/PackModules`),
esbuild (`tools/build-shared-modules.mjs`, `tools/product-generated.js`), the generator
(`packages/retroplug/scripts/lib/packages.mjs`), `retroplug-modc`, bash (`tools/run-cli-ts-tests.sh`).

---

## Where this sits

Part 1 of [docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) is built in milestones.

**Done:**
- M1;
- M2a to M2f;
- M5a: no heap ownership across the boundary ([its plan](downloadable-packs-m5a-plan.md));
- M5b: Mesen as a whole pack ([its plan](downloadable-packs-m5b-plan.md));
- M5c: BlipToaster as a whole pack ([its plan](downloadable-packs-m5c-plan.md)).

**Left after this plan:**
- M3: placeholders for a missing pack.
- M4:
  - `.rppack` archives;
  - `pack install` / `list` / `remove` and `--pack`;
  - Settings > Packs.
- M5d: two plugin instances, the Node addon, concurrency, a sanitizer leg.

**The user's decisions (2026-11-27).**
1. A `run` / `test` session child **loads the launch packs by default**: installed packs plus `RETROPLUG_PACK_PATH`,
   like the dispatcher. A consumer repo that wants a pinned set points `RETROPLUG_PACKS_DIR` /
   `RETROPLUG_PACK_PATH` at it, and later uses M4's `--pack`.
2. The **whole** `tools/run-cli-ts-tests.sh` runs a second time against a CLI built without Mesen and BlipToaster,
   **in Linux CI**.

**What exists (read before starting).**

*The launcher, `packages/native/cli/main.cpp`:*
- A `.js` `argv[1]` is a session file.
- The dispatcher path:
  - calls `ensureLaunchPacks()`;
  - registers the cli set (`rp_cli_modules`) and the pack loader;
  - evaluates `rp_cli_bundle`.
- The session path:
  - only deserializes the SDK (`registerSdkModule`, `rp_retroplug_cli`) and evaluates the file;
  - loads no pack, native or TS;
  - registers no shared module.
- Launch packs load before `HostServices`, because a pack's kind items join an Engine only at its construction
  (`if (!runFile) ensureLaunchPacks();`).

*The cli shared set, `tools/build-shared-modules.mjs`'s `setOf("cli")`:*
- One host, `rp:cli` (`cli/cli.ts`).
- Its contracts are `SHELL_IMPORTS` and `SHARED_LIBRARIES`. `@retroplug/testing` and `@retroplug/n8/device`, which the
  SDK imports, are both in `SHELL_IMPORTS`.
- It keeps `tjs:path` and `retroplug-cli` external.
- Host outputs are named `rp:<x>` and written to `js/rp/<x>.js`; `h.name.slice(3)` assumes the `rp:` prefix.
- `packages/native/CMakeLists.txt`'s `retroplug-cli-bundle`:
  - builds the set into `build/native/cli-modules`;
  - runs `retroplug-modc table -s rp_cli_modules`;
  - runs `retroplug-modc array -s rp:cli rp_cli_bundle js/rp/cli.js`.

*The SDK:*
- `retroplug-cli-sdk-assets` runs `tools/embed-sdk.mjs`, which:
  - runs `tools/build-cli-sdk.mjs` (one self-contained esbuild bundle of `cli/sdk.ts` into
    `build/cli-sdk/retroplug-cli.js`, desktop generator, everything inlined);
  - embeds the `.d.ts` and a content hash.
- `tjsc -m -n retroplug-cli` then compiles `build/cli-sdk/retroplug-cli.js` to `rp_retroplug_cli`.
- The Node addon's tests (`packages/native/node/test/*.mjs`) import `build/cli-sdk/retroplug-cli.js` directly, and
  `packages/native/node/test/run.mjs` builds it itself. **That self-contained build stays as it is**: the Node host
  loading packs is M5d's.

*Pack loading in TS:*
- `src/packs/loadPacks.ts`'s `loadAppPacks()` is `loadPacks(nativePackHost(), APP_BUILDERS)`. It never rejects, and a
  refusal is a `[packs] pack <id> is refused: ...` console line.
- `cli/packs/cliPacks.ts`'s `loadCliPacks()` adds the cli half.
- `test-native/_packsPrelude.ts` is the native tier's top-level-await precedent.

*Products and the generator:*
- The products are the root `package.json`'s `retroplug.products`; `packs-desktop` is the desktop's list without
  `mesen` and `bliptoaster`.
- `packages/retroplug/scripts/lib/packages.mjs` (around line 1703) writes `cliOutDir` only for the desktop ("A product
  that is not the desktop ships no CLI").
- `renderCliTools({ outDir, shellDir, ... })` already computes relative specifiers from any `outDir`.
- `tools/product-generated.js`'s `GENERATED` map has no `tools` entry, and its resolver redirects only importers under
  the shell's `src/` and `ui/`.
- `packages/native/cmake/product-bundles.cmake`'s `retroplug_bundles()` sets `_cli_generated ""` for a non-desktop set.
  It exports `RP_BUNDLE_<name>_GENERATOR` and the bundle targets/paths, but not the set's directory or tree.
- `retroplug_product(<target> PACKAGES <list> PROVIDERS ...)` (cmake/packages.cmake) picks up a product's render and
  script bundles.
- `retroplug-host-packs` (packages/native/CMakeLists.txt) is the pattern for a test-only product binary.

*The suite, `tools/run-cli-ts-tests.sh` (859 lines):*
- `CLI="${RETROPLUG_CLI:-$REPO/build/bin/retroplug-cli}"`.
- Line 82 exports scratch `RETROPLUG_PACKS_DIR` / `RETROPLUG_PACKS_CACHE_DIR` and unsets `RETROPLUG_PACK_PATH`.
- Section 6d renders `resources/roms/smsggdj_v0_45.sms` and holds it to `render-baseline.json`'s
  `smsggdj/sms/index-0/mix` (hash on the golden's build target).
- Section 8 and the M2d / M2e legs set `RETROPLUG_PACK_PATH` per command to fixture packs under `build/packs/`.
- `PSEP` is defined in section 8.

*Pack runners:*
- `packages/retroplug/scripts/run-native-tests.mjs` reads each pack's version from `packages/<id>/pack.json` and lists
  `[BLIPTOASTER_PACK, MESEN_PACK]` on `RETROPLUG_PACK_PATH`.
- Root `package.json`:
  - `"test:cli-ts": "node scripts/cmake-build.js retroplug-cli retroplug-ts-fixture-packs && tools/run-cli-ts-tests.sh"`
  - `test:native:packs` builds `retroplug-mesen-pack retroplug-bliptoaster-pack`.

*Package cli halves:* `bliptoaster-rom` (BlipToaster), `risa-rom` and `n8-sync` (risa), `lsdj-rom`, the n8 tools and
`launchpad-probe`. Mesen has none. `build/packs/bliptoaster/0.1.0/pack.json` names `halves.cli` (`js/cli.js`), which no
host loads yet.

**Measured on the desktop CLI before planning** (`.superpowers/cli-packs/probe/`):
- A `retroplug-cli test` file that does `bootSession()` then `s.project.systems.loadRom(<resources/roms/bliptoaster.nes>)`
  returns `{"system":N}`. The view reads platform `nes`, core `mesen` and roles
  `["mesen","nes-n8-midi","bliptoaster","bliptoaster-assets"]`.
- `smsggdj_v0_45.sms` reads platform `sms`, core `mesen` and roles `["mesen","sms-sync"]`.
- `retroplug-cli bliptoaster-rom info resources/roms/bliptoaster.nes` exits 0 and prints `themes: 16`, `fonts: 4` and
  `kits: 16 populated / 16 banks`.

## Decisions this plan makes

- **The SDK joins the cli set rather than being built against it separately.** A separate esbuild of `cli/sdk.ts` with
  the contract specifiers external (the native runner's pack mode does this for test bundles) would still carry a second
  copy of any file reached both through a contract module and through a relative import. In the set's split build, such
  a file is one shared chunk. The dispatcher and the SDK share chunks as a result. Neither host evaluates the other: the
  dispatcher process never imports `retroplug-cli`, and a session process never evaluates `rp:cli`.
- **The pack load is the SDK module's, not `bootSession()`'s.** `bootSession()` is synchronous and is shared with the
  render worker (which loads its own packs) and the test host. A top-level await in a module only the embedded SDK
  enters keeps both unchanged, and a session's body runs only once the import has settled.
- **A session that does not import the SDK still loads the native half** (`ensureLaunchPacks()` runs for every session
  child, before `HostServices`). It loads no TS half, because nothing imports the loader. That matches the dispatcher's
  native behaviour and costs one table load per child.
- **`build/cli-sdk/retroplug-cli.js` is the Node SDK only.** `tools/embed-sdk.mjs` hashes the embedded SDK (the set's
  `js/rp/sdk.js`) and the `.d.ts`, and no longer builds anything. `pnpm build:cli-sdk` and the Node tests keep the
  self-contained bundle.
- **`retroplug-cli-packs` is test-only.** It is built by name, never released and never installed, as
  `retroplug-host-packs` is.
- **The pack mode keeps every leg.** The fixture-pack legs run with BlipToaster and Mesen ahead of their fixtures on the
  path. Their refusals and outputs are unchanged, since the M5c tiers already ran the fixtures beside both packs.

## File structure

| File | Change |
|---|---|
| `packages/retroplug/scripts/lib/packages.mjs` | `cliOutDir` for every product |
| `tools/product-generated.js` | `GENERATED.tools = "cli"`; importers under `cli/` redirected |
| `packages/native/cmake/product-bundles.cmake` | a product set's `cli/generated/tools.ts` byproduct; export `RP_BUNDLE_<name>_DIR` and `_TREE` |
| `packages/retroplug/test/script/shared-modules.test.ts` | cli set: the product tree, the `rp:sdk` host |
| `packages/retroplug/cli/sdkModule.ts` (new) | the embedded SDK: `await loadAppPacks()`, `export * from "./sdk"` |
| `tools/build-shared-modules.mjs` | the cli set's second host, `rp:sdk` |
| `tools/embed-sdk.mjs` | hashes a given SDK file; builds nothing |
| `tools/build-cli-sdk.mjs` | header: the Node SDK only |
| `packages/native/cmake/cli.cmake` (new) | `retroplug_cli(<target> <product> [EXCLUDE_FROM_ALL])` |
| `packages/native/CMakeLists.txt` | the CLI block replaced by `include(cmake/cli.cmake)`, `retroplug_cli(retroplug-cli desktop)`, `retroplug_cli(retroplug-cli-packs packs-desktop EXCLUDE_FROM_ALL)` |
| `packages/native/cli/main.cpp` | a session child loads the launch packs and the cli set before the SDK |
| `tools/fixtures/ts-harness-packs/tests/pack-role.test.ts` (new) | a session sees a fixture pack's role (desktop) |
| `tools/fixtures/ts-harness-console/tests/console.test.ts` (new) | NES (BlipToaster) and SMS systems in a session |
| `tools/run-cli-ts-tests.sh` | `--packs`, `packpath`, section 9 |
| `package.json` | `test:cli-ts:packs`; `test:cli-ts` unchanged |
| `.github/workflows/build.yml` | "CLI TypeScript harness with Mesen and BlipToaster loaded as packs" |
| `spec/02`, `spec/06`, `spec/07`, `spec/09`, `spec/14`, the loading design | docs |

---

### Task 1: Every product's tree carries its CLI tools

**Files:**
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (around lines 1700-1707, 1851, 1884)
- Modify: `tools/product-generated.js`
- Modify: `packages/native/cmake/product-bundles.cmake` (the non-desktop branch around line 76, the exports around
  line 202)
- Test: `packages/retroplug/test/script/shared-modules.test.ts`

- [ ] **Step 1: Write the failing test.** Add to `shared-modules.test.ts`, after "the cli set holds the shape and
  reaches no UI":

```ts
test("the packs-desktop cli set is built from its own generated tree and carries no tool its list leaves out", async () => {
  const d = await describe("cli", "packs-desktop");
  holdsTheShape(d);
  const cli = d.closures["rp:cli"];
  expect(cli.some((p) => p.includes("/products/packs-desktop/packages/retroplug/cli/generated/tools.ts"))).toBe(true);
  expect(cli.some((p) => p.includes("/packages/retroplug/cli/generated/")), "the desktop's tools").toBe(false);
  expect(cli.some((p) => p.includes("/packages/bliptoaster/")), "BlipToaster's cli half is the pack's").toBe(false);
  expect(cli.some((p) => p.includes("/packages/mesen/")), "Mesen is the pack's").toBe(false);
});
```

  First read `describe()` and `test/script/shared-modules.mjs`, to see how a product name becomes the
  `--product-dir` (the plugin case passes `"plugin"`). If the tree must exist first, generate it the way that case
  does.

- [ ] **Step 2: Run it and see it fail.**
  Run: `cd packages/retroplug && pnpm exec retroplug-test test/script/shared-modules.test.ts`. Use the command the
  pure tier uses for one file; it is in `packages/retroplug/package.json`'s `test` script.
  Expected: FAIL. There is no `cli/generated/tools.ts` in the product tree, and `product-generated.js` refuses the
  bundle because it carries the desktop's `cli/generated/tools.ts`, or the closure names it.

- [ ] **Step 3: The generator writes `cli/generated/tools.ts` for every product.** In `packages.mjs`, change:

```js
  // Where this product's generated modules land: the shell's tree for the desktop (so the typecheck, the pure tier,
  // the shell's tests and every existing path are unchanged), and a parallel tree under the build directory for any
  // other product, laid out as a shell's is so the same relative specifiers hold. A product that is not the desktop
  // ships no CLI, so its tree has no cli/generated.
  ...
  const cliOutDir = product === DESKTOP ? join(shell.dir, "cli/generated") : null;
```

  to:

```js
  // Where this product's generated modules land: the shell's tree for the desktop (so the typecheck, the pure tier,
  // the shell's tests and every existing path are unchanged), and a parallel tree under the build directory for any
  // other product, laid out as a shell's is so the same relative specifiers hold. Every product's tree carries its CLI
  // tools: a CLI is built per product (packages/native/cmake/cli.cmake).
  ...
  const cliOutDir = join(productTree, "cli/generated");
```

  Then drop the `if (cliOutDir)` guards at the `files.push` for `tools.ts` and at `mkdirSync`, since `cliOutDir` is now
  always set. Update the header comment near line 45 that lists `packages/retroplug/cli/generated/tools.ts`, if it says
  the file is the desktop's alone. Also grep `packages/retroplug/test/packages/` for an assertion that a product tree
  has no `cli/generated` (`git grep -n "cli/generated" packages/retroplug/test`), and turn it around if one exists.

- [ ] **Step 4: `product-generated.js` redirects the tools module.** Add `tools: "cli"` to `GENERATED`. Extend the
  doc comment ("the protocol tables and the app registry are src/, the UI's two are ui/, the CLI's tools cli/"). Make
  the importer test accept the shell's `cli/` too:

```js
        if (!["src", "ui", "cli"].some((tree) => under(importer, path.join(SHELL, tree)))) return undefined;
```

  Update the header's "from an importer under the shell's src/ or ui/" to "src/, ui/ or cli/".

- [ ] **Step 5: CMake lists the product's tools as a generator output.** In `product-bundles.cmake`'s non-desktop
  branch, set `_cli_generated "${_product_tree}/cli/generated"`. The existing `if(_cli_generated)` then adds `tools.ts`
  to the byproducts. Beside the other exports near line 202, add:

```cmake
    set(RP_BUNDLE_${name}_DIR "${_dir}" PARENT_SCOPE)            # this set's build directory (build/native for the desktop)
    set(RP_BUNDLE_${name}_TREE "${_product_tree}" PARENT_SCOPE)  # its generated tree; empty for the desktop (the shell's)
```

  `_dir` and `_product_tree` are set inside `if(NOT _done)`. For a product that shares another's set, read the shared
  key's values the way the existing exports already do. Check how `_suffix` and `_dir` reach the exports for a second
  caller, and follow the same path, for example by storing them as GLOBAL properties under `RP_BUNDLES_${_key}_DIR`.

- [ ] **Step 6: Run it and see it pass.** Run the same command as Step 2. Then run the pure tier and the generator's
  checks: `pnpm test` (root) and `pnpm check:boundaries`.
  Expected: all pass. `build/products/packs-desktop/packages/retroplug/cli/generated/tools.ts` exists, and its imports
  name launchpad, lsdj, n8 and risa only (no bliptoaster).

- [ ] **Step 7: Reconfigure and build the desktop CLI.** Run `cmake build` and then
  `cmake --build build --target retroplug-cli -j$(nproc)`.
  Expected: it builds. The desktop's generated tools are unchanged (`git status` shows no change under
  `packages/retroplug/cli/generated/`).

- [ ] **Step 8: Commit.**

```bash
git add packages/retroplug/scripts/lib/packages.mjs tools/product-generated.js packages/native/cmake/product-bundles.cmake packages/retroplug/test/script/shared-modules.test.ts
git commit -m "products: every product's tree carries its CLI tools, so a CLI can be built for any list"
```

---

### Task 2: A session's SDK shares the cli set and loads the launch packs

**Files:**
- Create: `packages/retroplug/cli/sdkModule.ts`
- Modify: `tools/build-shared-modules.mjs` (`setOf("cli")`)
- Modify: `tools/embed-sdk.mjs`, `tools/build-cli-sdk.mjs` (header only)
- Modify: `packages/native/CMakeLists.txt` (`retroplug-cli-bundle`, `retroplug-cli-sdk-assets`)
- Modify: `packages/native/cli/main.cpp`
- Create: `tools/fixtures/ts-harness-packs/tests/pack-role.test.ts`
- Modify: `tools/run-cli-ts-tests.sh` (section 8)
- Test: `packages/retroplug/test/script/shared-modules.test.ts`

- [ ] **Step 1: Write the failing end-to-end leg.** Create `tools/fixtures/ts-harness-packs/tests/pack-role.test.ts`:

```ts
// A session file sees the launch packs (docs/downloadable-packs-cli-plan.md): the SDK commits their app halves before
// the body runs. The render fixture pack (packages/native/test/packs/ts-fixture/render) gives every ROM its
// tsfix-render role, so a system booted here carries it only if this session loaded the pack. Run by
// tools/run-cli-ts-tests.sh section 8 with that pack on RETROPLUG_PACK_PATH and RP_GB_ROM naming mGB.
import { test, expect, bootSession } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

test("a session's system carries the role a launch pack gives every ROM", () => {
  const s = bootSession();
  const loaded = s.project.systems.loadRom(tjs.env.RP_GB_ROM ?? "");
  if (loaded === null || !("system" in loaded)) throw new Error(`the ROM did not load: ${JSON.stringify(loaded)}`);
  const view = s.project.systems.view().find((v) => v.id === loaded.system);
  expect(view?.roles.map((r) => r.kind).includes("tsfix-render")).toBe(true);
  s.project.systems.removeSystem(loaded.system);
});
```

  In `run-cli-ts-tests.sh` section 8, after the `render --help` routing check and before the render-worker leg, add:

```bash
# A session file loads the launch packs as the dispatcher does (docs/downloadable-packs-cli-plan.md): the render
# fixture pack's role reaches a system a `test` file boots.
SP="$TMP/session-packs"
mkdir -p "$SP/tests"
cp "$FIX/ts-harness-packs/tests/pack-role.test.ts" "$SP/tests/"
RETROPLUG_PACK_PATH="$PACKS/tsfix-render/1.0.0" RP_GB_ROM="$REPO/packages/mgb/rom/mGB.gb" \
	"$CLI" test "$SP/tests" >"$TMP/session-packs.out" 2>&1
check "packs: a session file sees a launch pack's role" 0 $?
contains "packs: ...the role's construct hook ran in the session" "$TMP/session-packs.out" "tsfix-render: constructed"
```

- [ ] **Step 2: Run it and see it fail.**
  Run: `cmake --build build --target retroplug-cli retroplug-ts-fixture-packs -j$(nproc) && tools/run-cli-ts-tests.sh 2>&1 | grep -E "FAIL|PASS"`
  Expected: `FAIL - packs: a session file sees a launch pack's role (expected exit 0, got 1)` and the contains check
  fails. Everything else passes. Keep this pre-change binary for Step 8's timing:
  `cp build/bin/retroplug-cli .superpowers/cli-packs/retroplug-cli.m5c`.

- [ ] **Step 3: The SDK module.** Create `packages/retroplug/cli/sdkModule.ts`:

```ts
// The SDK the CLI binary embeds, which a session file imports as `retroplug-cli` (cli/main.cpp registers it under that
// name): the launch packs' app, consoles, protocol, kinds and placer halves commit into this context's registries
// first (a top-level await, so an importer's body waits for it), and then the authoring surface is cli/sdk.ts's. A
// session therefore sees the packs the dispatcher and the app see (docs/downloadable-packs-cli-plan.md). Built as the
// cli shared set's second host (tools/build-shared-modules.mjs), so its contract modules are the ones a pack's halves
// link to. The Node addon's SDK (tools/build-cli-sdk.mjs) bundles cli/sdk.ts alone and loads no pack.
import { loadAppPacks } from "../src/packs/loadPacks";

await loadAppPacks();

export * from "./sdk";
```

- [ ] **Step 4: The cli set's second host.** In `tools/build-shared-modules.mjs`'s `setOf("cli")`:

```js
  if (name === "cli") {
    return {
      hosts: [
        { name: "rp:cli", entry: path.join(SHELL, "cli/cli.ts") },
        // The SDK a session file imports (cli/sdkModule.ts): cli/main.cpp registers its bytecode as `retroplug-cli`.
        { name: "rp:sdk", entry: path.join(SHELL, "cli/sdkModule.ts") },
      ],
```

  Update the header's `cli:` line ("the CLI's dispatcher (rp:cli), the SDK a session imports (rp:sdk) and
  SHELL_IMPORTS").

  In `shared-modules.test.ts`, change the cli case's `expect(d.hosts.map((h) => h.name)).toEqual(["rp:cli"]);` to
  `["rp:cli", "rp:sdk"]` and add:

```ts
  expect(d.closures["rp:sdk"].some((p) => p.includes("/cli/generated/tools.ts")), "a session carries no tool").toBe(false);
  expect(d.closures["rp:sdk"].some((p) => p.includes("/packages/retroplug/ui/"))).toBe(false);
```

  If `holdsTheShape` assumes one host, or asserts something per host, read it and keep it true for two. The app set
  already has two.

- [ ] **Step 5: `embed-sdk.mjs` hashes the embedded SDK.** Make its usage
  `node tools/embed-sdk.mjs <sdk.js> <out.c>`:
  - `sdk.js` is the cli set's `js/rp/sdk.js`;
  - the `.d.ts` is read from `packages/retroplug/cli/sdk-types.d.ts`;
  - the `execFileSync` of `build-cli-sdk.mjs` and the `outDir` argument are removed;
  - the hash is still `sha256(js + dts)`, first 16 hex.

  Rewrite the header: the embedded SDK is the cli set's `rp:sdk` host, and `tools/build-cli-sdk.mjs` builds the Node
  addon's self-contained SDK. In `tools/build-cli-sdk.mjs`'s header, say it is the Node SDK
  (`packages/native/node/test`) and that the CLI embeds the cli set's `rp:sdk` instead.

- [ ] **Step 6: CMake builds the SDK with the set.** In `retroplug-cli-bundle`, add a `BYPRODUCTS` entry for
  `${SDK_MODULE_C}` and `${SDK_MODULE_C}.new` and a command after the `rp:cli` one:

```cmake
    COMMAND ${_MODC_COMMAND} array -s retroplug-cli rp_retroplug_cli ${CLI_MODULES_DIR}/js/rp/sdk.js ${SDK_MODULE_C}.new
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_MODULE_C}.new ${SDK_MODULE_C}
```

  Check `retroplug-modc array`'s argument order against the `rp:cli` line above it. The name after `-s` is the module
  name a session imports.

  `retroplug-cli-sdk-assets` now:
  - runs `embed-sdk.mjs ${CLI_MODULES_DIR}/js/rp/sdk.js ${SDK_ASSETS_C}.new` and the copy;
  - drops the `tjsc` command and `SDK_MODULE_C` from its byproducts;
  - drops its generator dependency and gains `add_dependencies(retroplug-cli-sdk-assets retroplug-cli-bundle)`.

  Rewrite the comment block above it. `rp_retroplug_cli` is now `retroplug-modc` module bytecode of the set's `rp:sdk`,
  registered as `retroplug-cli`. The point that it resolves from the loaded-module table with no file stays, as does the
  `.d.ts` paragraph.

- [ ] **Step 7: The session child loads the launch packs and the set.** In `cli/main.cpp`:
  1. Replace `if (!runFile) rp::host::packs::ensureLaunchPacks();` and its comment with an unconditional call:

```cpp
    // The launch packs load before the Engine in svc is built, for the dispatcher and a session file alike: a pack's kind
    // items' runtimes join an Engine only at its construction (docs/downloadable-packs-m2f-plan.md), and a session's SDK
    // commits the packs' TS halves before its body runs (docs/downloadable-packs-cli-plan.md).
    rp::host::packs::ensureLaunchPacks();
```

  2. Replace the `if (runFile) { ... } else { ... }` evaluation block with:

```cpp
    // The cli set's contract modules first, on both paths: the dispatcher and the SDK import them by name, and the pack
    // loader links a pack's TS halves to the same ones.
    int rc;
    const std::string why = rp::host::registerSharedModules(ctx, rp_cli_modules, rp_cli_modules_count);
    if (!why.empty()) {
        std::fprintf(stderr, "retroplug-cli: %s\n", why.c_str());
        rc = 1;
    } else {
        rp::host::installPackLoader(ctx, rp::host::packs::processPacks(), rp_cli_modules, rp_cli_modules_count);
        if (runFile) {
            // Only a session file imports the SDK, so only it pays the deserialize; `render` / `n8-bridge` / help never
            // touch it. The SDK's imports resolve to the modules registered above.
            registerSdkModule(ctx);
            const std::string code = slurp(argv[1]);
            rc = host.evalModuleBuffer(code.data(), code.size(), argv[1]);
        } else {
            rc = host.evalModuleBytecode(rp_cli_bundle, rp_cli_bundle_size);
        }
    }
```

  3. `registerSdkModule` now fails if the set is missing, which is no longer silent-safe. On `JS_IsException(mod)` or a
     failed `JS_ResolveModule`, print why before clearing:
     `std::fprintf(stderr, "retroplug-cli: the SDK module did not load\n"); tjs_dump_error(ctx);`. Check
     `tjs_dump_error` takes and clears the pending exception, as the interrupt-hook path's use implies; if it does, drop
     the `JS_FreeValue(ctx, JS_GetException(ctx))`. The import then fails with its own error as before.
  4. Update the file's header: "A `.js` argument is slurped and evaled directly (the dispatcher is skipped)" stays, plus
     one line: both paths load the launch packs and register the cli set.

- [ ] **Step 8: Run it and see it pass.**
  Run: `cmake --build build --target retroplug-cli retroplug-ts-fixture-packs -j$(nproc) && tools/run-cli-ts-tests.sh 2>&1 | tail -5`
  Expected: `PASS: retroplug-cli TypeScript harness`. Section 5's SDK checks still pass (the `.d.ts` is materialized,
  no `.js` is written), and so does the new leg.

  Then run the pure tier's shared-modules file (Task 1's Step 2 command) and `pnpm test:native`.
  Expected: pass, except the known machine-local app-cores GBA case. That case is not new; compare against
  `.superpowers/m5c/native.log`.

  Then time a session child before and after, to report the cost. Time
  `retroplug-cli test tools/fixtures/ts-harness-sdk/tests` five times on this build, and five times on the copy Step 2
  kept at `.superpowers/cli-packs/retroplug-cli.m5c`. Record both medians in `.superpowers/cli-packs/timing.txt`.

- [ ] **Step 9: Commit.**

```bash
git add packages/retroplug/cli/sdkModule.ts tools/build-shared-modules.mjs tools/embed-sdk.mjs tools/build-cli-sdk.mjs packages/native/CMakeLists.txt packages/native/cli/main.cpp tools/fixtures/ts-harness-packs/tests/pack-role.test.ts tools/run-cli-ts-tests.sh packages/retroplug/test/script/shared-modules.test.ts
git commit -m "cli: a session's SDK is the cli set's, and a session loads the launch packs as the dispatcher does"
```

---

### Task 3: A CLI built per product, and the one without Mesen and BlipToaster

**Files:**
- Create: `packages/native/cmake/cli.cmake`
- Modify: `packages/native/CMakeLists.txt` (the CLI block, lines ~227-339 after Task 2)
- Modify: `spec/06-build-test.md:1394` and `spec/09-cli-debugging.md:121` only if a target name they cite changes. The
  desktop's names stay, so neither should.

- [ ] **Step 1: Move the CLI's targets into a function.** Create `packages/native/cmake/cli.cmake`. It defines
  `retroplug_cli(<target> <product> [EXCLUDE_FROM_ALL])` by moving, not rewriting, the bodies of
  `retroplug-cli-bundle`, `retroplug-cli-sdk-assets` and the `retroplug-cli` executable from `CMakeLists.txt`,
  parameterized as follows:

```cmake
# retroplug_cli(<target> <product> [EXCLUDE_FROM_ALL]): retroplug-cli built for one of retroplug.products' lists
# (docs/downloadable-packs-cli-plan.md). The product's cli shared set (its dispatcher rp:cli, the SDK rp:sdk a session
# imports, and the contract modules both link to) is built from the product's generated tree, so its tools are the
# list's and a pack's cli half adds the rest; the type stripper is every product's one. The desktop's targets keep
# their names (retroplug-cli-bundle, retroplug-cli-sdk-assets) and its arrays build/native; another product's carry
# -<product> and live under build/products/<product>/native.
function(retroplug_cli target product)
    cmake_parse_arguments(ARG "EXCLUDE_FROM_ALL" "" "" ${ARGN})
    retroplug_bundles(${product})
    if(product STREQUAL "desktop")
        set(_suffix "")
        set(_product_args "")
    else()
        set(_suffix "-${product}")
        set(_product_args --product-dir "${RP_BUNDLE_${product}_TREE}")
    endif()
    set(_dir "${RP_BUNDLE_${product}_DIR}")
    set(_modules_dir "${_dir}/cli-modules")
    set(_bundle_c  "${_dir}/cli-bundle_data.c")
    set(_modules_c "${_dir}/cli-modules_data.c")
    set(_sdk_c     "${_dir}/sdk-module_data.c")
    set(_assets_c  "${_dir}/sdk-assets_data.c")
    # ... the two custom targets and the executable, as moved from CMakeLists.txt, with every path above substituted,
    #     ${RP_BUNDLE_${product}_GENERATOR} for ${RP_BUNDLE_desktop_GENERATOR}, ${_product_args} appended to the
    #     build-shared-modules.mjs command, and the PACKAGES of retroplug_product being ${RP_PRODUCT_PACKAGES_${product}}
    #     (the desktop's list is ${RP_DESKTOP_PACKAGES}; use whichever variable holds it for "desktop").
    #     A product's bundle and SDK targets must be defined once even if two CLI targets name one product: guard with
    #     if(NOT TARGET retroplug-cli-bundle${_suffix}).
endfunction()
```

  Points to check while moving:
  - **Shared-set reuse.** `retroplug_bundles` shares a set between products with equal lists. Keying the CLI's targets
    on `${product}` is fine, because `packs-desktop` and `desktop` differ. Note it in the comment if a product could
    share.
  - **Variables the function reads.** `_MODC_COMMAND`, `_MODC_DEP` and `_TJSC_DEP` are set earlier in
    `packages/native/CMakeLists.txt`, and a function reads its caller's scope. Include `cli.cmake` after they are set,
    where the CLI block stands now.
  - **Desktop generator.** `retroplug_bundles(desktop)` sets `RP_BUNDLE_desktop_DIR` to `build/native`, so the desktop's
    paths are unchanged.
  - **The type stripper** target and `TS_STRIPPER_BUNDLE_C` stay in `CMakeLists.txt`, defined once. The function adds
    `${TS_STRIPPER_BUNDLE_C}` to every executable and depends on `retroplug-ts-stripper-bundle`.
  - **Target properties** move into the function: `CXX_STANDARD 20`, `CXX_STANDARD_REQUIRED`,
    `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`, the `PROVIDERS n8 launchpad midi`, and any later
    `target_*(retroplug-cli ...)` lines in `CMakeLists.txt` or in an included cmake file. Run
    `git grep -n "retroplug-cli\b" -- '*.cmake' '*CMakeLists.txt'` and move each that should hold for every product.
    One that is release-only (install, packaging) stays on `retroplug-cli` by name.

- [ ] **Step 2: Declare both CLIs.** In `CMakeLists.txt`, where the block was:

```cmake
# retroplug-cli, built per product (cmake/cli.cmake). The desktop's is the one that ships.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cli.cmake)
retroplug_cli(retroplug-cli desktop)
# retroplug-cli-packs: the CLI on packs-desktop (the desktop's list without mesen and bliptoaster), which gets both from
# their packs; `pnpm test:cli-ts:packs` runs the whole CLI suite on it (docs/downloadable-packs-cli-plan.md). Built by
# name, never released.
retroplug_cli(retroplug-cli-packs packs-desktop EXCLUDE_FROM_ALL)
foreach(_pack retroplug-mesen-pack retroplug-bliptoaster-pack)
    if(TARGET ${_pack})
        add_dependencies(retroplug-cli-packs ${_pack})  # the packs its suite loads
    endif()
endforeach()
```

  `retroplug-bliptoaster-pack` is declared in the root `CMakeLists.txt`, after the package loop, which may run after
  `packages/native`. If `if(TARGET ...)` is false at this point, move the `add_dependencies` for that pack to the root
  `CMakeLists.txt` beside `retroplug_pack(bliptoaster TS_HALVES)`. That is the pattern M5c's `tests.cmake` hit; check
  how `retroplug-packs-test` depends on it.

- [ ] **Step 3: Reconfigure and build both.**
  Run: `cmake build && cmake --build build -j$(nproc) && cmake --build build --target retroplug-cli-packs -j$(nproc)`
  Expected:
  - Both link.
  - `build/products/packs-desktop/native/cli-modules/shared.tsv` exists.
  - `git status` is clean apart from `deps/sameboy`, which is dirty by design: never stage, stash or reset it.

- [ ] **Step 4: Check the two CLIs by hand.**

```bash
RETROPLUG_PACK_PATH= RETROPLUG_PACKS_DIR=/nonexistent build/bin/retroplug-cli-packs --help | grep -c bliptoaster-rom   # 0
P=build/packs/bliptoaster/0.1.0:build/packs/mesen/0.1.0
RETROPLUG_PACK_PATH=$P RETROPLUG_PACKS_DIR=/nonexistent build/bin/retroplug-cli-packs --help | grep -c bliptoaster-rom # 1
RETROPLUG_PACK_PATH=$P RETROPLUG_PACKS_DIR=/nonexistent build/bin/retroplug-cli-packs bliptoaster-rom info resources/roms/bliptoaster.nes | head -3
build/bin/retroplug-cli --help | grep -c bliptoaster-rom   # 1, built in
```

  Expected: the counts as commented, and `themes: 16` from the info call. Save the output to
  `.superpowers/cli-packs/t3-hand.txt`.

- [ ] **Step 5: The desktop suite is unchanged.** Run `pnpm test:cli-ts`. Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git add packages/native/cmake/cli.cmake packages/native/CMakeLists.txt
git commit -m "cli: retroplug-cli is built per product, and retroplug-cli-packs is the desktop's list without Mesen and BlipToaster"
```

  (Add the root `CMakeLists.txt` if Step 2 moved a dependency there.)

---

### Task 4: The CLI suite's pack mode

**Files:**
- Create: `tools/fixtures/ts-harness-console/tests/console.test.ts`
- Modify: `tools/run-cli-ts-tests.sh`
- Modify: `package.json` (root)
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: The console session fixture.** Create `tools/fixtures/ts-harness-console/tests/console.test.ts`:

```ts
// A session builds systems on consoles a product links or loads as a pack (docs/downloadable-packs-cli-plan.md): the
// BlipToaster cart on the NES (Mesen's console, BlipToaster's roles) and smsggdj on the SMS. Run by
// tools/run-cli-ts-tests.sh section 9 against the desktop CLI and, in its pack mode, against one that links neither, so
// a session child that loaded no pack fails here. RP_NES_ROM and RP_SMS_ROM name the ROMs.
import { test, expect, bootSession } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

const s = bootSession();

/** Boot `rom`, hand back its view, and remove the system once `check` has read it. */
function booted(rom: string | undefined, check: (v: { platform: string; core: string; kinds: string[] }) => void): void {
  const loaded = s.project.systems.loadRom(rom ?? "");
  if (loaded === null || !("system" in loaded)) throw new Error(`${rom} did not load: ${JSON.stringify(loaded)}`);
  try {
    const view = s.project.systems.view().find((v) => v.id === loaded.system);
    if (!view) throw new Error(`${rom}: no system ${loaded.system} in the view`);
    s.audio.renderAudio(200);
    check({ platform: view.platform, core: view.core, kinds: view.roles.map((r) => r.kind) });
  } finally {
    s.project.systems.removeSystem(loaded.system);
  }
}

test("the BlipToaster cart boots on the NES with BlipToaster's roles", () => {
  booted(tjs.env.RP_NES_ROM, (v) => {
    expect(v.platform).toBe("nes");
    expect(v.core).toBe("mesen");
    expect(v.kinds.includes("bliptoaster")).toBe(true);
    expect(v.kinds.includes("bliptoaster-assets")).toBe(true);
  });
});

test("smsggdj boots on the SMS", () => {
  booted(tjs.env.RP_SMS_ROM, (v) => {
    expect(v.platform).toBe("sms");
    expect(v.core).toBe("mesen");
    expect(v.kinds.includes("sms-sync")).toBe(true);
  });
});
```

  Check `SystemView`'s `platform` / `core` types in `cli/sdk-types.d.ts` (they are strings there, near line 508). If
  the declared type differs, type the callback's parameter from it rather than restating it.

- [ ] **Step 2: Section 9 and the mode flag.** In `tools/run-cli-ts-tests.sh`:

  1. Move `case "$(uname -s)" in ... PSEP=...` from section 8 to just after `FIX=`, and leave section 8 using it.
  2. Replace the `CLI=` line and its comment with:

```bash
# --packs: the pack mode (docs/downloadable-packs-cli-plan.md). The CLI is retroplug-cli-packs, built for packs-desktop
# (the desktop's list without mesen and bliptoaster), and every command runs with the two packs on its pack path,
# BlipToaster's listed first as the other runners list it; a case that sets a pack path of its own adds to theirs
# (packpath). RETROPLUG_CLI names another binary in either mode, which is how section 6h's exit cases are also run
# against an assertions build (build-dbg): a JS reference the exit paths leak aborts JS_FreeRuntime's gc_obj_list check
# there, at exit, where the release build cannot see it.
PACKS_MODE=0
if [ "${1:-}" = "--packs" ]; then
	PACKS_MODE=1
	shift
fi
if [ "$PACKS_MODE" = 1 ]; then CLI="${RETROPLUG_CLI:-$REPO/build/bin/retroplug-cli-packs}"; else CLI="${RETROPLUG_CLI:-$REPO/build/bin/retroplug-cli}"; fi
```

     Make the not-found message name the right target (`retroplug-cli-packs` in the pack mode).
  3. After `PSEP`, add the base path, and `packpath`:

```bash
# The pack mode's packs, each at the version its pack.json names, and a run without one built stops here, naming it.
BASE_PACKS=""
if [ "$PACKS_MODE" = 1 ]; then
	for id in bliptoaster mesen; do
		ver="$(sed -n 's/^ *"version": *"\([^"]*\)".*/\1/p' "$REPO/packages/$id/pack.json" | head -1)"
		dir="$REPO/build/packs/$id/$ver"
		if [ ! -f "$dir/pack.json" ]; then
			echo "error: the $id pack is not built at $dir (pnpm test:cli-ts:packs builds it)" >&2
			exit 2
		fi
		BASE_PACKS="${BASE_PACKS:+$BASE_PACKS$PSEP}$dir"
	done
fi
# packpath <path>: a case's own pack path, after the mode's packs (the mode's alone for an empty one).
packpath() { printf '%s' "${BASE_PACKS:+$BASE_PACKS${1:+$PSEP}}$1"; }
```

  4. Line 82 becomes:

```bash
export RETROPLUG_PACKS_DIR="$TMP/packs" RETROPLUG_PACKS_CACHE_DIR="$TMP/packs-cache"
if [ -n "$BASE_PACKS" ]; then export RETROPLUG_PACK_PATH="$BASE_PACKS"; else unset RETROPLUG_PACK_PATH; fi
```

  5. Every `RETROPLUG_PACK_PATH="<x>"` in the script becomes `RETROPLUG_PACK_PATH="$(packpath "<x>")"`. That covers
     section 8, the session leg Task 2 added, the M2d render legs and `proto_render_leg`. Check with
     `grep -n 'RETROPLUG_PACK_PATH=' tools/run-cli-ts-tests.sh`: every hit but line 82's must call `packpath`.
  6. Add section 9 before the final `echo`:

```bash
# 9. The consoles and carts a product links or loads as packs (docs/downloadable-packs-cli-plan.md): a session file
#    builds NES and SMS systems, and BlipToaster's cli half is a command. Run in both modes: the desktop CLI links
#    Mesen and BlipToaster, the pack mode's CLI gets them from the packs.
CS="$TMP/console-session"
mkdir -p "$CS/tests"
cp "$FIX/ts-harness-console/tests/console.test.ts" "$CS/tests/"
RP_NES_ROM="$REPO/resources/roms/bliptoaster.nes" RP_SMS_ROM="$REPO/resources/roms/smsggdj_v0_45.sms" \
	"$CLI" test "$CS/tests" >"$TMP/console.out" 2>&1
check "consoles: a session boots the BlipToaster cart and smsggdj" 0 $?
contains "consoles: ...both cases passed" "$TMP/console.out" "# 2 passed, 0 skipped, 0 failed"
"$CLI" bliptoaster-rom info "$REPO/resources/roms/bliptoaster.nes" >"$TMP/blip-info.out" 2>"$TMP/blip-info.err"
check "consoles: bliptoaster-rom info exits 0" 0 $?
contains "consoles: ...it reads the ROM's themes" "$TMP/blip-info.out" "themes: 16"
contains "consoles: ...and its kit banks" "$TMP/blip-info.out" "kits: 16 populated / 16 banks"
if [ "$PACKS_MODE" = 1 ]; then
	# The tool above is the pack's: with no pack on the path the binary has no BlipToaster command at all.
	RETROPLUG_PACK_PATH="" "$CLI" --help >"$TMP/nopack-help.out" 2>&1
	check "pack mode: --help with no pack exits 0" 0 $?
	absent "pack mode: the binary links no bliptoaster-rom" "$TMP/nopack-help.out" "bliptoaster-rom"
fi
```

     The harness's summary line format (`# 2 passed, 0 skipped, 0 failed`) is the one the probe printed. Confirm it
     against a run before relying on it.

- [ ] **Step 3: The scripts.** In the root `package.json`, beside `test:cli-ts`:

```json
    "test:cli-ts:packs": "node scripts/cmake-build.js retroplug-cli-packs retroplug-mesen-pack retroplug-bliptoaster-pack retroplug-ts-fixture-packs && tools/run-cli-ts-tests.sh --packs",
```

- [ ] **Step 4: Run both modes.**
  Run: `pnpm test:cli-ts 2>&1 | tee .superpowers/cli-packs/cli-ts.log | tail -3`, then
  `pnpm test:cli-ts:packs 2>&1 | tee .superpowers/cli-packs/cli-ts-packs.log | tail -3`.
  Expected: both end in `PASS: retroplug-cli TypeScript harness`. The pack mode's 6d render writes the golden's bytes:
  on this machine (the golden's build target) the hash case runs, not a size-only fallback.

  **Triage, if the pack mode fails.** Diff the two logs' `ok` / `FAIL` lines
  (`diff <(grep -E '^(ok|FAIL)' .superpowers/cli-packs/cli-ts.log) <(grep -E '^(ok|FAIL)' .superpowers/cli-packs/cli-ts-packs.log)`).
  Read each case that differs:
  - **Output that only a pack mode prints** (a pack-load line on stderr where a case demands `quiet`, say) is a finding.
    Report it with the line; do not loosen the check.
  - **A render's bytes differing** from the golden on the pack is a real audio change (AGENTS.md: a parity failure is
    never noise). Stop and report it with both hashes.
  - **A fixture leg failing only beside the two packs** means the fixture and a real pack disagree. Report which.

- [ ] **Step 5: The CI step.** In `.github/workflows/build.yml`'s linux job, after "CLI TypeScript harness":

```yaml
      - name: CLI TypeScript harness with Mesen and BlipToaster loaded as packs
        # The same suite on retroplug-cli-packs, which links neither, with both on every command's pack path; its
        # smsggdj render runs on the Mesen pack and is held to the same golden (docs/downloadable-packs-cli-plan.md).
        env:
          RP_RENDER_GOLDEN_REQUIRE_TARGET: "1"
        run: pnpm test:cli-ts:packs
```

  Check this step lands in the linux x86 job only (the one with the two pack-tier steps), not linux-arm64.

- [ ] **Step 6: Commit.**

```bash
git add tools/fixtures/ts-harness-console/tests/console.test.ts tools/run-cli-ts-tests.sh package.json .github/workflows/build.yml
git commit -m "test: the CLI suite's pack mode runs every leg on a CLI that gets Mesen and BlipToaster from their packs"
```

---

### Task 5: Docs and verification

**Files:**
- Modify: `spec/02-native-host.md` (Packs: who calls `ensureLaunchPacks`, near line 1444; the CLI's session files)
- Modify: `spec/06-build-test.md` (pnpm scripts: `test:cli-ts:packs`; the `retroplug-cli-bundle` paragraph near
  line 1394; Hosts and test tiers, if it lists the pack modes)
- Modify: `spec/09-cli-debugging.md` (near line 121, if it says how the SDK is built)
- Modify: `spec/14-hosts-harnesses.md` (the CLI harness: the pack mode)
- Modify: `spec/07-remaining-work.md` (Downloadable packs)
- Modify: `docs/downloadable-packs-loading-design.md` (section 5's "Where each proof stands"; Mesen has no cli half)
- Modify: `docs/downloadable-packs-cli-plan.md` ("After the reviews", design questions)

- [ ] **Step 1: spec/02.**
  - The paragraph listing who calls `ensureLaunchPacks` now says that a CLI session file loads the launch packs too:
    native before its Engine, and TS through the SDK module's top-level `loadAppPacks()`.
  - The sentence saying "the CLI's session files" load none is deleted.
  - Add that the embedded SDK is the cli set's `rp:sdk`, and that the Node SDK (`build/cli-sdk`) loads no pack (M5d).

- [ ] **Step 2: spec/06, spec/09, spec/14.**
  - Document `test:cli-ts:packs` beside `test:cli-ts`, and that it runs in Linux CI.
  - Document `retroplug_cli()` and `retroplug-cli-packs` where the CLI's targets are described.
  - Document that `rp_retroplug_cli` is now `retroplug-modc` bytecode of the set's `rp:sdk`.
  - spec/14's CLI harness section names the pack mode.

- [ ] **Step 3: spec/07.**
  - In the Downloadable packs entry, delete "the CLI on a pack (which first loads BlipToaster's cli half)" from what is
    left. What is left is M3, M4 and M5d.
  - In "Open from M2e", delete "a CLI test or run session loads no pack (its SDK bundle has registries of its own, apart
    from the dispatcher's)".
  - In "Open from M5c", delete the clause "BlipToaster's cli half is built and loaded by no host here, the CLI
    milestone's dispatcher being its first loader".
  - Add "Open from the CLI milestone", which lists this plan's design questions (Step 5) in one sentence each, as the
    other "Open from" lists do.

- [ ] **Step 4: The loading design.**
  - Section 5's "Where each proof stands": the CLI's is built (this plan). `pnpm test:cli-ts:packs` runs the CLI suite on
    `retroplug-cli-packs`, a session child loads the launch packs, and the smsggdj render golden holds on the Mesen
    pack.
  - Fix the line that lists Mesen's halves as "native, app, consoles, ui and cli": Mesen's pack has no cli half.

- [ ] **Step 5: The plan's own sections.** Fill in "After the reviews" and "Design questions this plan found" (below)
  from what the tasks found. Expected questions, to confirm or drop:
  - **Session cost.** Every session child now loads the launch packs (the native table, then the TS halves through the
    SDK). Quote `.superpowers/cli-packs/timing.txt`'s medians.
  - **Node SDK.** `build/cli-sdk/retroplug-cli.js` loads no pack, so the Node addon's sessions see none (M5d).
  - **SDK-free sessions.** A session file that does not import the SDK still loads the packs' native halves.
  - **A session's refusals.** These are its own stderr lines, as the dispatcher's are.
  - **Pinning.** A consumer repo cannot yet pin its pack set except through the two environment variables, until M4's
    `--pack`.
  - **Two kinds of SDK hash.** The embedded SDK's hash now changes with the cli set's split, so a dispatcher-only change
    can move it and rewrite a consumer's materialized `.d.ts` stamp.

- [ ] **Step 6: The verification.**
  1. Run `pnpm test`, `pnpm test:native`, `pnpm test:native:packs`, `pnpm test:ui`, `pnpm test:ui:packs`,
     `pnpm test:cli-ts` and `pnpm test:cli-ts:packs`. Save each log under `.superpowers/cli-packs/`.
     Expected: every one passes, except the known machine-local app-cores GBA case in the native tiers, as in M5c.
  2. Run the parallel tip-verify:
     `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 .superpowers/verify/tip-verify-par.sh` with its run directory at
     `.superpowers/verify/runs/cli-packs`, then compare against `runs/m5c`.
     Expected: additions only.

- [ ] **Step 7: Commit.**

```bash
git add spec/02-native-host.md spec/06-build-test.md spec/09-cli-debugging.md spec/14-hosts-harnesses.md spec/07-remaining-work.md docs/downloadable-packs-loading-design.md docs/downloadable-packs-cli-plan.md
git commit -m "docs: the CLI on a pack, a session that loads the launch packs, and the CLI suite's pack mode"
```

---

### Task 6: The CI gate

Needs the user's push approval. Once approved:
1. Push `feature/modularize`.
2. Run `gh workflow run build.yml --ref feature/modularize`.
3. Read every job.

The linux job's new step must pass with the render hash case run. On macOS, Windows and linux-arm64, `retroplug-cli` is
built through `retroplug_cli()`, and their CLI artifacts must still build.

## After the reviews

What the build found that the plan did not say:

- **No deviation of design.** Every task landed as written. No review found anything Critical or Important.
- **Task 1.** A test assertion could be made vacuous by a `/products/` segment in the checkout path; it was tightened.
  A 123-column comment was rewrapped.
- **Task 2.** The top-level-await allowance was first applied to every shared set; it is now the cli set's alone, so the
  app and render sets keep es2020's refusal. A stale `__rp_sdkAsset` comment naming a "js" asset no longer served was
  fixed, and an em dash removed.
- **Task 3.** A header listed a variable the function never reads; over-long lines were rewrapped; the rule that a
  product sharing another's set must not ask for a CLI went from a comment to a FATAL_ERROR; a redundant
  `set_property` was dropped; and `retroplug-cli-packs` joined warnings.cmake's /W4 /WX list.
- **Task 4.** A 121-column line was rewrapped, and two "with no pack" labels that were inaccurate in pack mode now say
  "with no fixture pack". The pack-mode absence check (no `bliptoaster-rom` in `--help`) is paired with a presence
  check.
- **`retroplug-cli-packs` links no Mesen or BlipToaster code:** 0 Mesen symbols against 480 in `retroplug-cli`, and
  21.9 MB against 30.9 MB.
- **Results.** The CLI suite in desktop mode is 166 ok, in pack mode 169 ok, 0 FAIL; the pack mode's smsggdj render
  (section 6d) hashes to the golden (c309fbacf1c2803a) on the Mesen pack.
- **The verification found one miss.** `retroplug-cli-packs` is a product binary, so `Packages.test.cpp`'s golden
  (`test/engine-graph/golden/product-registrations.json`) needs its row, which Task 3 left out: the `plugin` and `egfix`
  legs failed on it. The row (packs-desktop's node types, the same as `retroplug-host-packs`'s) went into Task 3's
  commit.
- **Verification.** The parallel tip-verify (`runs/cli-packs` against `runs/m5c`): additions only, once the golden row
  was in, apart from the known machine-local `app-cores` GBA case in the native tiers, as in M5c.
  `pnpm test:native:packs` has that case alone; `pnpm test:ui:packs` and `pnpm test:cli-ts:packs` pass. `retroplug-cli`
  is 786 KB smaller, since the SDK shares the dispatcher's chunks instead of carrying its own copy.
- **Session cost** (median of 5, `retroplug-cli test tools/fixtures/ts-harness-sdk/tests`, no packs): 0.111 s before,
  0.110 s after; with Mesen and BlipToaster on the path, 0.116 s.

## Design questions this plan found

- **Session cost.** Every session child now loads the launch packs (the native table, then the TS halves through the
  SDK). With both packs on the path the smallest session goes from 0.110 s to 0.116 s; with none it does not move.
  Should a session load lazily, on the first thing that needs a pack?
- **Node SDK.** `build/cli-sdk/retroplug-cli.js` is a self-contained bundle that loads no pack, so the Node addon's
  sessions see none (M5d).
- **SDK-free sessions.** A session file that does not import the SDK still loads the packs' native halves and commits no
  TS half; the example sessions, which bundle their own SDK copy, are such sessions. Should a session that never
  imports the SDK load no pack at all?
- **A session's refusals.** A pack a session refuses is reported on the session's own stderr, as the dispatcher's are,
  not in a result the test can read.
- **Pinning.** A consumer repo cannot yet pin its pack set except through `RETROPLUG_PACKS_DIR` and
  `RETROPLUG_PACK_PATH`, until M4's `--pack`.
- **Two kinds of SDK hash.** The embedded SDK's hash now changes with the cli set's split, so a dispatcher-only change
  can move it and rewrite a consumer's materialized `.d.ts` stamp. Should the stamp hash the declarations alone?
