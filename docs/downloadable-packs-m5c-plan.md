# Downloadable packs M5c: BlipToaster as a whole pack. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The BlipToaster package, built from this tree as a whole pack (its app, ui, cli and script halves, no native
module), loads at launch beside the Mesen pack into test hosts whose product links neither, and the whole native and UI
test tiers pass against the two packs as they do against the built-in packages.

**Architecture:**
- **A pack may bundle pure library code.** `contracts.mjs` gains `PACK_BUNDLED`, the built-in packages' modules a pack's
  halves may carry their own copy of (tracker's NES ROM helpers, which BlipToaster shares with risa).
  `tools/build-pack-halves.mjs` resolves those through their package's exports map and bundles them, and refuses a half
  whose bundle holds any other file outside the package and its npm libraries.
- **A pack with no native half.** `retroplug_pack(<package> TS_HALVES)` accepts a package with no native entry: the
  folder is the halves build's alone (`retroplug-<package>-pack`). A package's device script pack (`retroplug.script`)
  becomes the pack's script half by copying the product build's own toolchain output (`--script-from`), so the pack's
  bundles are the bytes the static build compiles. BlipToaster's pack is declared from the root `CMakeLists.txt`, as the
  package has no `native/CMakeLists.txt`.
- **A required platform orders the load.** The native pack table loads a pack whose `requires.platforms` names a
  platform after every other pending pack whose `provides.platforms` names it, so BlipToaster (`requires.platforms:
  ["nes"]`) loads after Mesen whatever order the folders were found in.
- **The test product loses BlipToaster.** `packs-desktop` becomes the desktop list without `mesen` and `bliptoaster`.
  The runners' pack mode names both packs, and the preludes fail a file whose BlipToaster pack did not load.
- **The M2d fixture goes.** The script fixture's `bliptoaster` (its script half alone, at `build/packs/bliptoaster/1.0.0`)
  is replaced by the real pack at `build/packs/bliptoaster/0.1.0`, which `PackScript.test.cpp` now holds to the static
  build.

**Tech Stack:** C++20, CMake, the device-pack toolchain (`packages/retroplug/scripts/build-device-pack.mjs`), esbuild
(`tools/build-pack-halves.mjs`), the TS pack loader (`src/packs/loadPacks.ts`), the native pack table
(`host/packs/PackTable.cpp`), the native and UI test tiers, Catch2.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones; done:
M1, M2a to M2f, M5a (no heap ownership across the pack boundary, [its plan](downloadable-packs-m5a-plan.md)), M5b (Mesen
as a whole pack, [its plan](downloadable-packs-m5b-plan.md)). Left after this plan:

- **The CLI on a pack (its own plan, before M5d):** a dispatcher built per product, session children (`retroplug-cli
  x.js`, `run`, `test`) that load packs, and the smsggdj render-golden leg on the pack. BlipToaster's cli half, which
  this plan builds, is first loaded there.
- **M5d:** the host legs (two plugin instances, the Node addon, concurrency, a sanitizer leg).

**The user's decision (2026-11-26).** BlipToaster's app, ui and cli halves import five `@retroplug/tracker` modules
that are not contract modules (`ritAssets`, `nes/constants`, `nes/kitBank`, `nes/theme`, `nes/find`; the ui half also
imports `ritAssetOps`), which a pack's build refuses today. They are pure (no module state; they import only contract
modules, zod and each other), about 600 lines, and risa and the N8 CLI use them too. **A pack bundles its own copy**:
a list in `contracts.mjs` names the modules a pack may bundle, and a build-time check holds a half's bundle to its own
files, its npm libraries and the listed modules. Not chosen: a library pack (a pack half that exports modules, with
cross-pack imports and version ranges on `requires.packs`; it would earn its keep for shared code that is large,
stateful or released on its own, and stays possible later), and the host serving them (one instance, but the helpers
would become pack API and the host would carry NES code for packs' sake).

**What exists (read before starting).**
- The probe (2026-11-26): `node tools/build-pack-halves.mjs packages/bliptoaster <out> <template> --mqjs build/bin/mqjs
  --runner build/bin/retroplug-mqjs-run` fails the app, ui and cli halves on the tracker imports above, and the cli half
  also on `import { NO_SLOT_REASON } from "@retroplug/dsp"` (dsp's root, `ts/kitJobs.ts`). The script half (`--only
  script`) builds cleanly.
- `tools/build-pack-halves.mjs`: each ES half is an esbuild bundle with `externals = packExternals()` and a
  `refuse-foreign-retroplug` plugin (every `@retroplug/*` import is a contract, or an error); relative and absolute
  imports are bundled unchecked. `buildScriptHalf` runs the device-pack toolchain under the pack's id in a temp dir and
  copies `<id>.js` and `<id>.device.js` in as `js/script.js` and `js/script.device.js`, checking `<id>.types.json`
  against `provides.nodeTypes`. `scriptRealmImports` already resolves `@retroplug/<pkg>/<sub>` through a package's
  exports map (`workspacePackages()`), for the kinds half.
- `cmake/packs.cmake`: `retroplug_pack_module(... TS_HALVES <dir>)` builds the ES halves into the folder (with a
  depfile) and writes `pack.json`; `NO_LIBRARY` stages a manifest alone. `retroplug_pack(<package> TS_HALVES)` refuses a
  package with a `SCRIPT_ENTRY` (stale since M2d loads script halves) and one with no native entry. Its work files go to
  `${CMAKE_CURRENT_BINARY_DIR}/packs/`, which for a call from the root is `build/packs/` itself, the development packs'
  folder.
- `retroplug_device_script_pack` (`cmake/packages.cmake`) defines `retroplug-<package>-script-pack`, a custom target that
  runs on every build and rewrites `build/packages/<package>/script/<package>.{js,device.js,types.json,...}` only when
  their bytes change (they are its BYPRODUCTS). `retroplug_package(bliptoaster)` defines it, from the desktop list's
  loop in the root `CMakeLists.txt`.
- `PackTable::loadAll` (`packages/native/src/host/packs/PackTable.cpp`) orders candidates by `requires.packs` alone
  (Kahn's passes). `requires.platforms` is checked in TS only (`loadPacks.ts`'s `missingPlatform`, against the platforms
  of the consoles registered before the pack in `host.list()`'s order, which is the table's load order). With
  `RETROPLUG_PACK_PATH=<bliptoaster>:<mesen>`, BlipToaster would be refused for the NES today.
- The M2d script fixture: `packages/native/test/packs/script-fixture/CMakeLists.txt`'s `bliptoaster` block and
  `bliptoaster.pack.json`, which `PackScript.test.cpp` ("BlipToaster's script half as a pack filters MIDI exactly as its
  static build does") loads as `fixture("bliptoaster")`, and `test/script/build-pack-halves.test.ts` names as a template.
- The pack modes (M5b): `run-native-tests.mjs --packs` and `run-ui-tests.mjs --packs` set `RETROPLUG_PACK_PATH` to the
  Mesen pack (the UI runner joins it with a test's fixture packs by `SEP`); `test-native/_packsPrelude.ts` and
  `test-ui/_packsPrelude.ts` throw unless `packStatus().loaded.mesen`.
- `retroplug-packs-test` (product `packs-host`, which already lacks `bliptoaster`) links the static
  `retroplug-bliptoaster-script` beside the pack; `MesenPack.test.cpp`'s `mesenPack()` is `RP_PACKS_BUILD_DIR /
  "mesen" / "0.1.0"`.
- The golden `packages/native/test/engine-graph/golden/product-registrations.json` lists `rp.bliptoaster.midi-policy`
  (a device script pack a product lists registers last) in the `retroplug-host-packs` and `retroplug-ui-test-packs`
  rows; `Packages.test.cpp` checks every product's row (`pnpm test:plugin engine-graph`).

**Execution model.**
- Sonnet implements each task from its text; Opus reviews each task's diff before the next starts.
- The code here was not compiled. Where the tree differs, the implementer follows the tree, keeps the step's intent,
  and reports the deviation.
- Task 7 is triage: it runs both tiers against the packs and REPORTS before fixing anything beyond a one-line cause.
- Task 9 ends with a CI gate, which needs the user's go-ahead to push.

## Decisions this plan makes

- **`NO_SLOT_REASON` moves into the kit contract.** The one value the cli half takes from dsp's root is a string
  constant; it moves to `@retroplug/dsp/kitContract` (a contract module), `kitJobs.ts` re-exports it, so lsdj's and
  risa's root imports keep working. No API level change: a contract module gains an export.
- **`PACK_BUNDLED`** (in `contracts.mjs`, beside `SHARED_LIBRARIES`): `{ specifier, reason }` entries, the six tracker
  modules above. It is no contract: the shell is still held to `SHELL_IMPORTS`/`UI_IMPORTS`, no shared module set builds
  it, and an entry changes no API level. A listed specifier is never also a contract or shared library.
- **The bundle's inputs are checked, not the imports alone.** After each ES half's build, every metafile input that is
  neither in the package's own folder, nor under a `node_modules` folder, nor a listed module's file is refused, naming
  the file and the module that imports it. That holds a listed module's own relative imports too (a listed module
  importing an unlisted tracker file fails the build), and closes the gap where a relative or absolute path into another
  package was bundled unchecked.
- **The pack folder's work files move to `pack-work/`.** `retroplug_pack_module` writes its template, entry, export list
  and depfile under `${CMAKE_CURRENT_BINARY_DIR}/pack-work/`, so a pack declared from the root does not write into
  `build/packs/`.
- **A TS-only pack.** `retroplug_pack(<package> TS_HALVES)` with no native entry calls `retroplug_pack_module(... NO_LIBRARY
  TS_HALVES ...)` and defines `retroplug-<package>-pack` over its halves target. `TS_HALVES` lifts the `SCRIPT_ENTRY`
  refusal. The halves are built where the module is (`RP_PACK_PLATFORM` set), so the target is absent on the same
  platforms Mesen's is.
- **The script half copies the product build's toolchain output.** With `TS_HALVES` and a `retroplug.script` member,
  the halves command passes `--script-from build/packages/<package>/script` and depends on
  `retroplug-<package>-script-pack` and its three files; the pack's id must be the package's folder name (the toolchain
  names its files and its bytecode's source after it). Without `--script-from` the tool runs the toolchain itself, as
  before (an SDK repo building a pack has no product build).
- **BlipToaster's manifest** is `packages/bliptoaster/pack.json`: id `bliptoaster`, version `0.1.0` (the package's),
  MIT, `requires.platforms: ["nes"]`, `provides.nodeTypes: ["rp.bliptoaster.midi-policy"]`, `provides.roles:
  ["bliptoaster", "bliptoaster-assets"]`. Declared from the root `CMakeLists.txt`, after the desktop list's loop.
- **Platform order.** A pending pack waits while another pending pack provides a platform it requires. A platform no
  other pending pack provides (the product's, or nobody's) holds nothing back: the TS loader judges it. The runners list
  BlipToaster before Mesen (as a packs folder lists them, by id), so every pack-mode run exercises the order.
- **The test product is `packs-desktop` without `bliptoaster`.** It keeps `risa`, `smsggdj` and the rest, so the tiers'
  files that use them still run.
- **What "unchanged" means.** The same test files, the same goldens, byte for byte. A file that fails only in a pack
  mode is a finding: Task 7 classifies each one before anything is changed.

## File structure

| File | Change |
|---|---|
| `packages/dsp/ts/kitContract.ts`, `ts/kitJobs.ts`, `packages/bliptoaster/ts/cli/bliptoasterRom.ts` | `NO_SLOT_REASON` in the contract |
| `packages/retroplug/scripts/lib/contracts.mjs` | `PACK_BUNDLED` |
| `tools/build-pack-halves.mjs` | Bundling the listed modules; the inputs check; `--script-from` |
| `packages/retroplug/test/script/build-pack-halves.test.ts` | Their cases; the template path |
| `cmake/packs.cmake` | `pack-work/`; the TS-only pack; the script half by copy |
| `packages/bliptoaster/pack.json` | **New.** The manifest template |
| `CMakeLists.txt` | `retroplug_pack(bliptoaster TS_HALVES)` |
| `packages/native/test/packs/script-fixture/CMakeLists.txt`, `bliptoaster.pack.json` | The fixture's BlipToaster removed (the file deleted) |
| `packages/native/test/packs/PackScript.test.cpp`, `packages/native/cmake/tests.cmake` | The real pack |
| `packages/native/src/host/packs/PackTable.cpp`, `packages/native/test/packs/PackLoader.test.cpp` | Platform order |
| root `package.json` | `packs-desktop` without `bliptoaster`; the pack scripts build its pack |
| `packages/native/test/engine-graph/golden/product-registrations.json` | Two rows lose `rp.bliptoaster.midi-policy` |
| `packages/retroplug/scripts/run-native-tests.mjs`, `run-ui-tests.mjs` | Both packs on the pack path |
| `packages/retroplug/test-native/_packsPrelude.ts`, `test-ui/_packsPrelude.ts` | Both packs required |
| `.github/workflows/build.yml` | The pack steps' names |
| `packages/retroplug/scripts/skip-baseline.json` | Only if the pack modes' skips move |
| Docs (Task 8) | spec/02, 06, 07, 14, the loading design, this plan |

---

### Task 1: `NO_SLOT_REASON` in the kit contract

**Files:**
- Modify: `packages/dsp/ts/kitContract.ts`, `packages/dsp/ts/kitJobs.ts`
- Modify: `packages/bliptoaster/ts/cli/bliptoasterRom.ts:25`

- [ ] **Step 1: Move the constant**

In `kitContract.ts`, after its imports, add:

```ts
/** What a compile error says when the compiler left a slot empty and gave no reason: the kit tools' fallback message,
 *  in the contract so a pack's tool (which may import only contract modules) words it as the built-in ones do. */
export const NO_SLOT_REASON = "the compiler left the slot empty without saying why";
```

In `kitJobs.ts`, replace `export const NO_SLOT_REASON = "the compiler left the slot empty without saying why";` with
`export { NO_SLOT_REASON } from "./kitContract";`, and if `kitJobs.ts` itself uses the name, add `import {
NO_SLOT_REASON } from "./kitContract";` beside its existing `import type ... from "./kitContract"`. dsp's root
(`ts/index.ts`, `export * from "./kitJobs"`) keeps exporting it, so `packages/lsdj/ts/cli/lsdjRom.ts` and
`packages/risa/ts/cli/risaRom.ts` are unchanged. If `index.ts` also wildcard-exports `./kitContract`, the two re-exports
name one binding, which ES modules and TypeScript accept; check `pnpm typecheck` says so.

In `bliptoasterRom.ts`, replace `import { NO_SLOT_REASON } from "@retroplug/dsp";` with `import { NO_SLOT_REASON } from
"@retroplug/dsp/kitContract";`.

- [ ] **Step 2: Check it**

```sh
pnpm typecheck
node tools/build-pack-halves.mjs packages/bliptoaster /tmp/m5c-t1 .superpowers/m5c/probe.pack.json --only cli 2>&1 | grep -o 'imports @retroplug[^,]*' | sort -u
```
Expected: typecheck passes; the probe's refusals no longer name `@retroplug/dsp` (the tracker ones remain). If
`.superpowers/m5c/probe.pack.json` is missing, write any template with `"id": "bliptoaster"` and `"halves": {}`. Then
`pnpm test kit` (the TS tier's kit files) passes.

- [ ] **Step 3: Commit**

```sh
git add packages/dsp/ts/kitContract.ts packages/dsp/ts/kitJobs.ts packages/bliptoaster/ts/cli/bliptoasterRom.ts
git commit -m "dsp: the kit tools' no-reason message is the kit contract's, so a pack's tool can word it"
```

---

### Task 2: A pack bundles the pure modules `PACK_BUNDLED` lists, and nothing else of another package

**Files:**
- Modify: `packages/retroplug/scripts/lib/contracts.mjs`
- Modify: `tools/build-pack-halves.mjs`
- Test: `packages/retroplug/test/script/build-pack-halves.test.ts`

- [ ] **Step 1: The failing cases**

Add three cases to `build-pack-halves.test.ts`, in the file's style (a Node script run by `runNode`, from the package
dir, two levels below the repo):

```ts
test("BlipToaster's app, ui and cli halves bundle the tracker modules PACK_BUNDLED lists and import only contracts", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-bt-"));',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bliptoaster", name: "x", version: "0.1.0", apiLevel: 0,',
    '  license: "MIT", source: "x", requires: { packs: [], platforms: ["nes"] },',
    '  provides: { platforms: [], nodeTypes: [], roles: ["bliptoaster", "bliptoaster-assets"] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: path.join(repo, "packages/bliptoaster"), outDir: path.join(work, "v"),',
    '    template, only: ["app", "ui", "cli"] });',
    '  const imports = {}, src = {};',
    '  for (const half of Object.keys(pack.halves)) {',
    '    src[half] = fs.readFileSync(path.join(work, "v", pack.halves[half]), "utf8");',
    '    imports[half] = [...src[half].matchAll(/^import[^"\']*["\']([^"\']+)["\']/gm)].map((x) => x[1]);',
    '  }',
    '  const contracts = m.packExternals();',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '  process.stdout.write(JSON.stringify({ halves: pack.halves, imports, contracts,',
    '    findBundled: /function findMagicInRange\\b/.test(src.app), themeBundled: /function decodeThemeFromRom\\b/.test(src.ui) }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const got = JSON.parse(run.out) as { halves: Record<string, string>; imports: Record<string, string[]>; contracts: string[];
    findBundled: boolean; themeBundled: boolean };
  expect(got.halves).toEqual({ app: "js/app.js", ui: "js/ui.js", cli: "js/cli.js" });
  for (const [half, list] of Object.entries(got.imports)) {
    for (const name of list) {
      if (!got.contracts.includes(name)) throw new Error(`${half} imports ${name}, which is not a contract`);
    }
  }
  expect(got.findBundled).toBe(true);
  expect(got.themeBundled).toBe(true);
});

test("a half whose bundle reaches another package's file that PACK_BUNDLED does not list is refused, naming it", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-reach-"));',
    'fs.mkdirSync(path.join(work, "pkg/ts"), { recursive: true });',
    'fs.writeFileSync(path.join(work, "pkg/package.json"), JSON.stringify({ retroplug: { app: "ts/app.ts" } }));',
    'const font = path.join(repo, "packages/tracker/ts/nes/font.ts");',
    'fs.writeFileSync(path.join(work, "pkg/ts/app.ts"), `import * as f from ${JSON.stringify(font)}; export default { roles: [], f };`);',
    'fs.writeFileSync(path.join(work, "pack.json"), JSON.stringify({ id: "x", halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href)',
    '  .then((m) => m.buildPackHalves({ packageDir: path.join(work, "pkg"), outDir: path.join(work, "out"),',
    '    template: path.join(work, "pack.json") }))',
    '  .then(() => { process.stdout.write("built"); }, (e) => { process.stdout.write(String(e.message)); })',
    '  .finally(() => fs.rmSync(work, { recursive: true, force: true }));',
  ].join("\n");
  const run = await runNode(script);
  expect(run.out.includes("packages/tracker/ts/nes/font.ts"), run.out).toBe(true);
  expect(run.out.includes("ts/app.ts"), run.out).toBe(true);
  expect(run.out.includes("PACK_BUNDLED"), run.out).toBe(true);
});

test("PACK_BUNDLED names only modules their packages export, none of them a contract", async () => {
  const script = [
    'const path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then((m) => {',
    '  const files = m.bundledModuleFiles();',
    '  process.stdout.write(JSON.stringify({ specifiers: [...files.keys()], contracts: m.packExternals() }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const got = JSON.parse(run.out) as { specifiers: string[]; contracts: string[] };
  expect(got.specifiers.length > 0).toBe(true);
  for (const s of got.specifiers) expect(got.contracts.includes(s), s).toBe(false);
});
```

`nes/font.ts` is a tracker module nothing lists (any unlisted tracker file serves: check it exists in this tree, else
pick another under `packages/tracker/ts/nes/` that `PACK_BUNDLED` does not name).

Run: `pnpm test script/build-pack-halves`.
Expected: the first FAILS (the tracker imports are refused), the second FAILS (`built`: an absolute import is bundled
unchecked), the third FAILS (`bundledModuleFiles` is not exported).

- [ ] **Step 2: The list**

In `contracts.mjs`, after `SHARED_LIBRARIES`:

```js
/** The modules a pack's halves may bundle their own copy of (docs/downloadable-packs-m5c-plan.md): a built-in
 *  package's pure code, holding no state, so a pack's copy and the host's (or another pack's) can differ in nothing but
 *  their version. tools/build-pack-halves.mjs bundles each through its package's exports map, and refuses a half whose
 *  bundle holds any other file outside the pack's own package and its npm libraries. This is no contract: the shell
 *  is not held to it, no shared module set builds it, and an entry here changes no API level. */
export const PACK_BUNDLED = [
  // The NES ROM helpers the NES carts' asset views and patchers share (risa's, BlipToaster's).
  { specifier: "@retroplug/tracker/nes/constants", reason: "the NES ROM layout's sizes and offsets" },
  { specifier: "@retroplug/tracker/nes/find", reason: "the marker search a NES cart's ROM view finds its blocks with" },
  { specifier: "@retroplug/tracker/nes/kitBank", reason: "the DMC sample bank's reader" },
  { specifier: "@retroplug/tracker/nes/theme", reason: "the theme record's codec" },
  { specifier: "@retroplug/tracker/ritAssets", reason: "the ROM asset overrides the NES carts' asset roles share" },
  { specifier: "@retroplug/tracker/ritAssetOps", reason: "the asset menus' export and replace actions over those overrides" },
];
```

Check each reason against its module's header comment and correct the wording where the header says otherwise (one
line each).

- [ ] **Step 3: Bundling and the inputs check**

In `build-pack-halves.mjs`:
- Import `PACK_BUNDLED` beside the other lists.
- Add and export:

```js
/** PACK_BUNDLED's modules, each specifier to the file its package's exports map names (a real path). A specifier its
 *  package does not export is a mistake in the list, and throws. */
export function bundledModuleFiles() {
  const workspace = workspacePackages();
  const files = new Map();
  for (const { specifier } of PACK_BUNDLED) {
    const m = /^(@retroplug\/[^/]+)(?:\/(.+))?$/.exec(specifier);
    const dir = m && workspace.get(m[1]);
    const target = dir ? (JSON.parse(readFileSync(join(dir, "package.json"), "utf8")).exports ?? {})[m[2] ? `./${m[2]}` : "."] : undefined;
    if (typeof target !== "string") throw new Error(`PACK_BUNDLED names ${specifier}, which no workspace package exports`);
    files.set(specifier, realpathSync(join(dir, target)));
  }
  return files;
}

/** A half's bundle holds the package's own files, its npm libraries and PACK_BUNDLED's modules. Any other file (another
 *  package's module reached by a relative or absolute path, or a file a listed module imports that is not listed itself)
 *  is refused: every such file, each with a module that imports it, so the message does not hang on esbuild's order. */
function checkBundleInputs(half, metafile, packageDir, allowed) {
  const own = realpathSync(packageDir) + sep;
  const refused = [];
  for (const key of Object.keys(metafile.inputs)) {
    const file = resolve(packageDir, key);
    if (!existsSync(file)) continue;  // a stdin entry's key names no file
    const real = realpathSync(file);
    if (real.startsWith(own) || real.split(sep).includes("node_modules") || allowed.has(real)) continue;
    const importer = Object.entries(metafile.inputs).find(([, i]) => (i.imports ?? []).some((x) => x.path === key))?.[0];
    refused.push(relative(REPO, real).split(sep).join("/") + (importer ? ` (imported by ${importer})` : ""));
  }
  if (refused.length) {
    throw new Error(`the ${half} half bundles ${refused.join(", ")}, which is neither the package's own nor a module ` +
      "PACK_BUNDLED lists (scripts/lib/contracts.mjs): a pack may bundle only those, and must import a contract module");
  }
}
```

  (If `metafile.inputs[key].imports[].path` is keyed differently from `inputs`' own keys in this esbuild version, match
  on the resolved file instead and report.)
- In `buildPackHalves`, before the halves loop: `const bundled = bundledModuleFiles(); const allowed = new
  Set(bundled.values());`.
- In the `refuse-foreign-retroplug` plugin: after the externals check, `if (bundled.has(args.path)) return { path:
  bundled.get(args.path) };`, and change the error text to: `` `${args.importer} imports ${args.path}, which is not a
  contract module and not one PACK_BUNDLED lets a pack bundle: a pack may import only the contracts
  (scripts/lib/contracts.mjs) and the host modules` `` (keep the words "which is not a contract module": an existing
  case matches them).
- After each ES half's `esbuild.build`, `checkBundleInputs(half, built.metafile, packageDir, allowed);` before
  `collectInputs`. The protocol and kinds halves are not checked (their rules are their own).
- Update the file's header comment: "the package's other imports are bundled in, except another @retroplug package's
  module, which a pack may not carry a copy of and is refused" becomes: the package's own files and its npm libraries
  are bundled in, and so are the modules `PACK_BUNDLED` lists (pure code a pack may carry its own copy of); any other
  file of another package is refused.

- [ ] **Step 4: Run it**

```sh
pnpm test script/build-pack-halves
node tools/build-pack-halves.mjs packages/mesen /tmp/m5c-mesen packages/mesen/pack.json && echo mesen-ok
node tools/build-pack-halves.mjs packages/native/test/packs/ts-fixture /tmp/m5c-tsfix packages/native/test/packs/ts-fixture/pack.json && echo tsfix-ok
```
Expected: every case in the file passes (the three new ones too); Mesen's and the TS fixture's halves still build (the
inputs check finds nothing of another package in them). If either is refused, report the file it names before changing
anything.

- [ ] **Step 5: Commit**

```sh
git add packages/retroplug/scripts/lib/contracts.mjs tools/build-pack-halves.mjs \
        packages/retroplug/test/script/build-pack-halves.test.ts
git commit -m "packs: a pack's halves bundle the pure modules PACK_BUNDLED lists, and a bundle holding any other package's file is refused"
```

---

### Task 3: The script half copied from the product build's toolchain output

**Files:**
- Modify: `tools/build-pack-halves.mjs` (`--script-from`)
- Test: `packages/retroplug/test/script/build-pack-halves.test.ts`

- [ ] **Step 1: The failing cases**

Add, beside the script-half cases (reuse `toolScript`'s shape, or write the script inline as below):

```ts
// The script half from a toolchain output folder (--script-from): copied, not rebuilt, and held to the template.
function scriptFromScript(declared: string[], files: boolean): string {
  return [
    'const fs = require("fs"), os = require("os"), path = require("path"), cp = require("child_process");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-script-from-"));',
    'const from = path.join(work, "toolchain");',
    'fs.mkdirSync(from);',
    `if (${files}) {`,
    '  fs.writeFileSync(path.join(from, "bliptoaster.js"), "var whole = 1;\\n");',
    '  fs.writeFileSync(path.join(from, "bliptoaster.device.js"), "var device = 1;\\n");',
    '  fs.writeFileSync(path.join(from, "bliptoaster.types.json"), JSON.stringify(["rp.bliptoaster.midi-policy"]));',
    '}',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bliptoaster", name: "x", version: "0.1.0", apiLevel: 0,',
    '  license: "MIT", source: "x", requires: { packs: [], platforms: [] },',
    `  provides: { platforms: [], nodeTypes: ${JSON.stringify(declared)}, roles: [] }, halves: {} }));`,
    'const out = path.join(work, "v");',
    'const r = cp.spawnSync(process.execPath, [path.join(repo, "tools/build-pack-halves.mjs"),',
    '  path.join(repo, "packages/bliptoaster"), out, template, "--only", "script", "--script-from", from],',
    '  { encoding: "utf8", cwd: repo });',
    'const read = (f) => { try { return fs.readFileSync(path.join(out, f), "utf8"); } catch (e) { return null; } };',
    'const rec = { status: r.status, stderr: r.stderr, pack: read("pack.json"), whole: read("js/script.js"),',
    '  runtime: read("js/script.device.js") };',
    'fs.rmSync(work, { recursive: true, force: true });',
    'process.stdout.write(JSON.stringify(rec));',
  ].join("\n");
}

test("--script-from copies a toolchain's bundles in as the script half, held to the template's types", async () => {
  const run = await runNode(scriptFromScript(["rp.bliptoaster.midi-policy"], true));
  if (run.status !== 0) throw new Error(run.err);
  const rec = JSON.parse(run.out) as ToolRun;
  expect(rec.status, rec.stderr).toBe(0);
  expect(rec.whole).toBe("var whole = 1;\n");
  expect(rec.runtime).toBe("var device = 1;\n");
  const pack = JSON.parse(rec.pack!) as { halves: Record<string, string>; script: unknown };
  expect(pack.halves).toEqual({ script: "js/script.js" });
  expect(pack.script).toEqual({ runtime: "js/script.device.js", heapBytes: 24576, nodeTypes: ["rp.bliptoaster.midi-policy"] });

  const undeclared = JSON.parse((await runNode(scriptFromScript([], true))).out) as ToolRun;
  expect(undeclared.status).toBe(1);
  expect(undeclared.stderr.includes("the script half defines rp.bliptoaster.midi-policy, which the template's " +
    "provides.nodeTypes does not declare"), undeclared.stderr).toBe(true);
});

test("--script-from naming a folder without the toolchain's files is refused, saying what builds them", async () => {
  const rec = JSON.parse((await runNode(scriptFromScript(["rp.bliptoaster.midi-policy"], false))).out) as ToolRun;
  expect(rec.status).toBe(1);
  expect(rec.stderr.includes("retroplug-bliptoaster-script-pack"), rec.stderr).toBe(true);
});
```

(`pack.halves` equals `{ script: "js/script.js" }` as the existing toolchain case has it: check that case's assertion
and match it.)

Run: `pnpm test script/build-pack-halves`. Expected: both FAIL (the tool's usage refuses `--script-from`).

- [ ] **Step 2: `--script-from`**

In `build-pack-halves.mjs`:
- The CLI accepts `--script-from <dir>` (add it to the known options and the usage line, and pass `scriptFrom: resolve(...)`
  on), and `buildPackHalves` takes `scriptFrom` and hands it to `buildScriptHalf`.
- `buildScriptHalf({ ..., scriptFrom })`: keep the `script` member's shape check. When `scriptFrom` is set, do not run
  the toolchain; read from it instead:

```js
  if (scriptFrom) {
    const file = (ext) => join(scriptFrom, `${pack.id}${ext}`);
    for (const ext of [".js", ".device.js", ".types.json"]) {
      if (!existsSync(file(ext))) {
        throw new Error(`the script half's ${file(ext)} does not exist: build the package's device script pack ` +
          `(retroplug-${pack.id}-script-pack) first`);
      }
    }
    return placeScriptHalf({ outDir, pack, script, types: JSON.parse(readFileSync(file(".types.json"), "utf8")),
      whole: file(".js"), runtime: file(".device.js") });
  }
```

  and move the existing tail (the types check against `provides.nodeTypes`, the two copies, `pack.script = ...`, return
  `"js/script.js"`) into `placeScriptHalf`, which the toolchain path calls too with its temp dir's files. One types
  check, one copy, for both paths.
- The header comment: a `--script-from <dir>` names a folder holding the toolchain's `<id>.js`, `<id>.device.js` and
  `<id>.types.json` (the product build's `build/packages/<package>/script`), copied in rather than rebuilt.

- [ ] **Step 3: Run it**

Run: `pnpm test script/build-pack-halves`. Expected: every case passes.

- [ ] **Step 4: Commit**

```sh
git add tools/build-pack-halves.mjs packages/retroplug/test/script/build-pack-halves.test.ts
git commit -m "packs: --script-from takes a pack's script half from the toolchain's own output, so its bundles are the static build's"
```

---

### Task 4: BlipToaster's pack folder, and the fixture it replaces

**Files:**
- Create: `packages/bliptoaster/pack.json`
- Modify: `cmake/packs.cmake` (`retroplug_pack_module`, `retroplug_pack`)
- Modify: `CMakeLists.txt` (after the desktop list's `retroplug_package` loop)
- Modify: `packages/native/test/packs/script-fixture/CMakeLists.txt`; Delete:
  `packages/native/test/packs/script-fixture/bliptoaster.pack.json`
- Modify: `packages/native/test/packs/PackScript.test.cpp`, `packages/native/cmake/tests.cmake`
- Modify: `packages/retroplug/test/script/build-pack-halves.test.ts` (the template path)

- [ ] **Step 1: The failing case**

In `PackScript.test.cpp`, beside the other helpers:

```cpp
// BlipToaster's whole pack (packages/bliptoaster/pack.json, built by retroplug-bliptoaster-pack): its app, ui and cli
// halves and, as its script half, the package's own toolchain output.
fs::path blipToasterPack() { return fs::path(RP_PACKS_BUILD_DIR) / "bliptoaster" / "0.1.0"; }
```

Change "BlipToaster's script half as a pack filters MIDI exactly as its static build does" to load
`table.load(blipToasterPack())` instead of `fixture("bliptoaster")`, and add a case:

```cpp
TEST_CASE("BlipToaster's pack folder carries its app, ui, cli and script halves, the script half the toolchain's own",
          "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(blipToasterPack());
    INFO(load.error);
    REQUIRE(load);
    for (const char* half : {"js/app.js", "js/ui.js", "js/cli.js", "js/script.js", "js/script.device.js"}) {
        INFO(half);
        CHECK(fs::exists(blipToasterPack() / half));
    }
    // Copied from the package's static script pack, byte for byte: the bytecode the host compiles is the static build's.
    CHECK(readText(blipToasterPack() / "js/script.js") == readText(toolchainOut("bliptoaster") / "bliptoaster.js"));
    CHECK(readText(blipToasterPack() / "js/script.device.js") ==
          readText(toolchainOut("bliptoaster") / "bliptoaster.device.js"));
    CHECK(load.pack->manifest.requiresPlatforms == std::vector<std::string>{"nes"});
}
```

(`PackLoad`/`PackTable` are declared further down the file by `using`: place the case after them. If the table's
manifest names its halves, also check the five names it holds; use the member's name in this tree.)

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-script]"`.
Expected: the new case and the retargeted one FAIL (no `build/packs/bliptoaster/0.1.0`).

- [ ] **Step 2: The manifest**

`packages/bliptoaster/pack.json`:

```json
{
  "id": "bliptoaster",
  "name": "BlipToaster",
  "version": "0.1.0",
  "license": "MIT",
  "source": "https://github.com/tommitytom/RetroPlug",
  "requires": {
    "packs": [],
    "platforms": ["nes"]
  },
  "provides": {
    "platforms": [],
    "nodeTypes": ["rp.bliptoaster.midi-policy"],
    "roles": ["bliptoaster", "bliptoaster-assets"]
  },
  "halves": {}
}
```

(The version is `packages/bliptoaster/package.json`'s; Mesen's `pack.json` is the pattern, and has no `apiLevel`: the
configure stamps it.)

- [ ] **Step 3: `cmake/packs.cmake`**

In `retroplug_pack_module`:
- Every work file under `${CMAKE_CURRENT_BINARY_DIR}/packs/` moves to `${CMAKE_CURRENT_BINARY_DIR}/pack-work/`: set
  `set(_work "${CMAKE_CURRENT_BINARY_DIR}/pack-work")` once, near the top, and use `${_work}/${target}.pack.json`,
  `${_work}/${target}-halves.d`, `${_work}/${target}-entry.cpp`, `${_work}/${target}.exports`, `${_work}/${target}.map`
  (every `${CMAKE_CURRENT_BINARY_DIR}/packs/` in the function; nothing outside it reads them). The reason, in a comment:
  a pack declared from the root would otherwise write into `build/packs/`, the development packs' folder.
- The halves block builds when `M_TS_HALVES AND RP_PACK_PLATFORM` as now. Inside it, after the `foreach(_half ...)`:

```cmake
        # A device script pack (retroplug.script) is the script half: the package's own toolchain output
        # (retroplug_device_script_pack, build/packages/<package>/script), copied in with --script-from, so the pack's
        # bundles are the bytes the static build compiles. That target runs on every build and rewrites a file only when
        # its bytes change, so depending on its files copies them again exactly when they move.
        set(_script_args "")
        set(_script_deps "")
        string(JSON _script ERROR_VARIABLE _err GET "${_pkg}" retroplug script)
        if(NOT _err)
            get_filename_component(_pkgname "${M_TS_HALVES}" NAME)
            if(NOT _id STREQUAL _pkgname)
                message(FATAL_ERROR "retroplug_pack_module(${target}): the script half is ${_pkgname}'s own script "
                                    "pack, which the toolchain names after the package, so the pack's id must be "
                                    "${_pkgname}, not ${_id}")
            endif()
            if(NOT TARGET retroplug-${_pkgname}-script-pack)
                message(FATAL_ERROR "retroplug_pack_module(${target}): ${_pkgname} declares retroplug.script, and "
                                    "retroplug-${_pkgname}-script-pack is not defined: retroplug_package(${_pkgname}) "
                                    "must come first")
            endif()
            set(_from "${CMAKE_BINARY_DIR}/packages/${_pkgname}/script")
            set(_script_args --script-from "${_from}")
            set(_script_deps retroplug-${_pkgname}-script-pack "${_from}/${_pkgname}.js"
                             "${_from}/${_pkgname}.device.js" "${_from}/${_pkgname}.types.json")
            list(APPEND _halves_out "${_dir}/js/script.js" "${_dir}/js/script.device.js")
        endif()
```

  and add `${_script_args}` to the `add_custom_command`'s `COMMAND` (after `--depfile "${_depfile}"`) and
  `${_script_deps}` to its `DEPENDS`. Check `retroplug_device_script_pack`'s default out dir is
  `${CMAKE_BINARY_DIR}/packages/<name>/script` with `<name>` the package's (it is called with the package's name), and
  that `<name>.types.json` is among its BYPRODUCTS.
- Update the function's header comment: `TS_HALVES` also ships a package's device script pack as the script half
  (copied from its toolchain output); `NO_LIBRARY` with `TS_HALVES` is a pack with no native half, whose folder the
  halves build alone writes.

In `retroplug_pack`:
- The `foreach(_part PROVIDERS AUDIO_BACKEND_TARGETS SCRIPT_ENTRY EMBED_IDS)` refusal skips `SCRIPT_ENTRY` when
  `P_TS_HALVES` (the halves build ships it).
- "names no native entry" is refused only without `P_TS_HALVES`. With it and no entry:

```cmake
    if(NOT _entries)
        # A pack with no native half: its TS halves (and its script half) alone, the folder the halves build's.
        retroplug_pack_module(retroplug-${package}-pack MANIFEST "${P_MANIFEST}" NO_LIBRARY ${_halves})
        if(TARGET retroplug-${package}-pack-halves)  # absent on a platform with no pack folder, as a module is
            add_custom_target(retroplug-${package}-pack)
            add_dependencies(retroplug-${package}-pack retroplug-${package}-pack-halves)
        endif()
        return()
    endif()
```

  placed after `_halves` is set and before the existing `retroplug_pack_module(... LIBS ...)` call.
- Its header comment: a package with no native entry is a pack too when `TS_HALVES` is given, and a script pack is
  refused only without it.

- [ ] **Step 4: Declare it, and remove the fixture**

Root `CMakeLists.txt`, right after the `foreach(_rp_package IN LISTS ...) retroplug_package(...) endforeach()`:

```cmake
# The packages built as packs that have no native/CMakeLists.txt to call retroplug_pack() from: BlipToaster's app, ui,
# cli and script halves (docs/downloadable-packs-m5c-plan.md). After the list, whose retroplug_package(bliptoaster)
# defines the script pack its script half is copied from.
retroplug_pack(bliptoaster TS_HALVES)
```

`packages/native/test/packs/script-fixture/CMakeLists.txt`: delete the header line `#   bliptoaster ...`, the whole
`# BlipToaster's script half ...` block (from its comment through `list(APPEND _sf_outputs ... script.device.js")`), and
`git rm packages/native/test/packs/script-fixture/bliptoaster.pack.json`.

`packages/native/cmake/tests.cmake`: after `add_dependencies(retroplug-packs-test retroplug-script-fixture-packs)`:

```cmake
if(TARGET retroplug-bliptoaster-pack)  # absent on a platform with no pack folder (cmake/packs.cmake)
    add_dependencies(retroplug-packs-test retroplug-bliptoaster-pack)
endif()
```

`build-pack-halves.test.ts`: the case "a package's device script pack becomes the pack's script half" passes
`"packages/native/test/packs/script-fixture/bliptoaster.pack.json"` to `toolScript`; change it to
`"packages/bliptoaster/pack.json"`, and check its assertions still hold (it asserts the `script` member; the real
template's version and roles do not touch it).

Look at the stale fixture output before removing it: `ls build/packs/bliptoaster/` (expect `1.0.0`, the M2d fixture's
folder, no longer built), then `rm -r build/packs/bliptoaster/1.0.0`.

- [ ] **Step 5: Build, run, see it pass**

```sh
cmake --build build --target retroplug-bliptoaster-pack -j$(nproc)
cat build/packs/bliptoaster/0.1.0/pack.json && ls build/packs/bliptoaster/0.1.0/js
ls build/packs/*.pack.json 2>/dev/null; echo "no stray work files: $?"
cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test
cmake --build build --target retroplug-mesen-pack -j$(nproc) && ls build/packages/mesen/pack-work
pnpm test script/build-pack-halves
touch packages/tracker/ts/nes/theme.ts && cmake --build build --target retroplug-bliptoaster-pack -j$(nproc) 2>&1 | grep -c "TS halves"
```
Expected: `pack.json` names `app`, `ui`, `cli` and `script` with the `script` member and `apiLevel` 3; `js/` holds the
five files; no `*.pack.json` directly in `build/packs/`; the whole packs test passes (`[packs-script]` with the two
BlipToaster cases, `[packs-mesen]` as before); Mesen's work files are under `pack-work/`; touching a bundled tracker
module re-runs the halves (count 1). Also `cmake --build build -j$(nproc)` (the full build still configures and builds;
`retroplug-bliptoaster-pack` is not in ALL, as Mesen's is not).

- [ ] **Step 6: Commit**

```sh
git add packages/bliptoaster/pack.json cmake/packs.cmake CMakeLists.txt \
        packages/native/test/packs/script-fixture/CMakeLists.txt packages/native/test/packs/PackScript.test.cpp \
        packages/native/cmake/tests.cmake packages/retroplug/test/script/build-pack-halves.test.ts
git commit -m "packs: BlipToaster's pack folder carries its app, ui, cli and script halves, the script half its toolchain's own, replacing the script fixture"
```

(`git rm` already staged the deleted template.)

---

### Task 5: A pack waits for the pack that provides a platform it requires

**Files:**
- Modify: `packages/native/src/host/packs/PackTable.cpp` (`loadAll`)
- Test: `packages/native/test/packs/PackLoader.test.cpp`

- [ ] **Step 1: The failing case**

In `PackLoader.test.cpp`, after "packs load in dependency order, whatever order they are given in":

```cpp
// A pack folder with an app half alone (a file the native table only finds; the TS loader reads it), requiring and
// providing the platforms given.
std::filesystem::path platformPack(const TempDir& tmp, const std::string& id, const std::string& requires,
                                   const std::string& provides) {
    const auto dir = writePack(tmp, id,
        "{\"id\": \"" + id + "\", \"name\": \"n\", \"version\": \"1.0.0\", \"apiLevel\": " + std::to_string(RP_API_LEVEL) +
        ", \"license\": \"MIT\", \"source\": \"s\", \"requires\": {\"packs\": [], \"platforms\": " + requires +
        "}, \"provides\": {\"platforms\": " + provides + ", \"nodeTypes\": [], \"roles\": []}, " +
        "\"halves\": {\"app\": \"js/app.js\"}}");
    std::filesystem::create_directories(dir / "js");
    std::ofstream(dir / "js" / "app.js") << "export default {};\n";
    return dir;
}

TEST_CASE("a pack requiring a platform loads after the pack that provides it, whatever order they are given in",
          "[packs-loader]") {
    TempDir tmp;
    const auto user = platformPack(tmp, "plat-user", "[\"plat\"]", "[]");
    const auto other = platformPack(tmp, "plat-other", "[]", "[]");
    const auto provider = platformPack(tmp, "plat-provider", "[]", "[\"plat\"]");
    PackTable table(rp::graph::productPackages());
    const auto loads = table.loadAll({{user}, {other}, {provider}});
    std::string refusals;
    for (const auto& r : loads.refused) refusals += r.id + ": " + r.reason + "\n";
    INFO(refusals);
    REQUIRE(loads.loaded.size() == 3);
    CHECK(loads.loaded[0]->manifest.id == "plat-other");
    CHECK(loads.loaded[1]->manifest.id == "plat-provider");
    CHECK(loads.loaded[2]->manifest.id == "plat-user");

    // A platform no other pack given provides (the product's, or nobody's) holds nothing back: the TS loader judges it.
    TempDir alone;
    PackTable second(rp::graph::productPackages());
    const auto one = second.loadAll({{platformPack(alone, "plat-user", "[\"plat\"]", "[]")}});
    CHECK(one.loaded.size() == 1);
    CHECK(one.refused.empty());
}
```

(`platformPack` goes in the file's anonymous namespace beside `writePack`. If the table refuses these folders for a reason this case did not mean, copy the shape of a TS-only pack the tree already loads
natively (the `tsfix` fixture's `pack.json`) and report the difference.)

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-loader]"`.
Expected: the new case FAILS (`plat-user` loads first, in the given order).

- [ ] **Step 2: The order**

In `PackTable::loadAll`, give `Pending` the platforms:

```cpp
    struct Pending {
        PackCandidate            candidate;
        std::string              id;
        std::vector<std::string> needs;      // requires.packs
        std::vector<std::string> platforms;  // requires.platforms
        std::vector<std::string> provides;   // provides.platforms
    };
```

fill them from the manifest (`manifest->requiresPlatforms`, `manifest->providesPlatforms`), and add, beside `loaded`:

```cpp
    // A platform a pack requires holds it back while another pending pack provides it: a pack needing the NES loads
    // after the pack whose consoles bring the NES, whichever order the folders were found in. A platform no other pending
    // pack provides is the product's or nobody's; the TS loader judges that, and it holds nothing back here.
    const auto waitsOnPlatform = [&](const Pending& p) {
        for (const auto& q : pending) {
            if (&q == &p) continue;
            for (const auto& platform : p.platforms)
                if (std::find(q.provides.begin(), q.provides.end(), platform) != q.provides.end()) return true;
        }
        return false;
    };
```

and in the pass loop, `bool ready = !waitsOnPlatform(*it);` before the `needs` loop (which keeps `ready = ready &&
loaded(dep)`). The leftover handling is unchanged: a pack still waiting when a pass loads none (its provider waits on it
in turn) is loaded by the leftover loop in the given order, and the TS loader refuses whichever cannot find its
platform. Update the Kahn comment to say a pass also waits on a required platform's provider.

- [ ] **Step 3: Run it**

```sh
cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test
```
Expected: the whole packs test passes.

- [ ] **Step 4: Commit**

```sh
git add packages/native/src/host/packs/PackTable.cpp packages/native/test/packs/PackLoader.test.cpp
git commit -m "packs: a pack requiring a platform loads after the pack that provides it, whatever order the folders were found in"
```

---

### Task 6: The test product without BlipToaster, and the pack modes load both packs

**Files:**
- Modify: root `package.json` (`retroplug.products["packs-desktop"]`, `test:native:packs`, `test:ui:packs`)
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json`
- Modify: `packages/retroplug/scripts/run-native-tests.mjs`, `packages/retroplug/scripts/run-ui-tests.mjs`
- Modify: `packages/retroplug/test-native/_packsPrelude.ts`, `packages/retroplug/test-ui/_packsPrelude.ts`
- Modify: `.github/workflows/build.yml` (the pack steps' names and comment)

- [ ] **Step 1: The product and the golden**

Root `package.json`: remove `"bliptoaster"` from `packs-desktop` (its last entry). In the two scripts, add
`retroplug-bliptoaster-pack` beside `retroplug-mesen-pack`:

```json
"test:native:packs": "node scripts/cmake-build.js retroplug-host-packs retroplug-mesen-pack retroplug-bliptoaster-pack && node packages/retroplug/scripts/run-native-tests.mjs --packs",
"test:ui:packs": "node scripts/cmake-build.js retroplug-ui-test-packs retroplug-mesen-pack retroplug-bliptoaster-pack retroplug-ts-fixture-packs && node packages/retroplug/scripts/run-ui-tests.mjs --packs",
```

`product-registrations.json`: remove `"rp.bliptoaster.midi-policy"` from the `retroplug-host-packs` and
`retroplug-ui-test-packs` rows (and the comma before it), nothing else.

```sh
cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[products]"
```
(use the tag `Packages.test.cpp`'s cases carry in this tree; `pnpm test:plugin engine-graph` runs the whole binary).
Expected: passes; with the golden unedited it fails on the two rows, which is the check that it reads them (try it).

- [ ] **Step 2: The runners**

`run-native-tests.mjs`, beside `MESEN_VERSION`/`MESEN_PACK`:

```js
const BLIPTOASTER_VERSION = JSON.parse(readFileSync(join(REPO, "packages/bliptoaster/pack.json"), "utf8")).version;
const BLIPTOASTER_PACK = join(REPO, "build/packs/bliptoaster", BLIPTOASTER_VERSION);
const SEP = process.platform === "win32" ? ";" : ":";
```

(skip `SEP` if the file has one); the not-built check covers both (one message per missing pack, naming its target
`retroplug-bliptoaster-pack`); and:

```js
    // BlipToaster first, as a packs folder lists them (by id): the table loads it after Mesen, whose consoles bring the
    // NES it requires (requires.platforms), so every pack-mode run holds that order.
    env.RETROPLUG_PACK_PATH = [BLIPTOASTER_PACK, MESEN_PACK].join(SEP);
```

The header comment's "packs-desktop (the desktop's list without mesen) that loads Mesen as a pack" becomes "(the
desktop's list without mesen and bliptoaster) that loads Mesen and BlipToaster as packs".

`run-ui-tests.mjs`: the same constants and check, and `packPath` becomes `[...(PACKS ? [BLIPTOASTER_PACK, MESEN_PACK] :
[]), ...(PACK_TESTS[slug] ?? []).map(fixtureFolder)].join(SEP)`; the same header change.

- [ ] **Step 3: The preludes**

Both `_packsPrelude.ts` files: replace the Mesen check with

```ts
for (const id of ["mesen", "bliptoaster"]) {
  if (!status.loaded[id]) throw new Error(`the pack mode's ${id} pack did not load: ${JSON.stringify(status)}`);
}
```

(check `packStatus().loaded`'s type in `src/packs/loadPacks.ts`; index it as that type allows), and their header
comments: "a run whose Mesen or BlipToaster pack did not load fails here".

- [ ] **Step 4: CI**

`.github/workflows/build.yml`, the linux job's pack steps: the comment above them says Mesen and BlipToaster are built
as whole packs (`build/packs/mesen`, `build/packs/bliptoaster`) and loaded at launch into hosts that link neither; the
two steps are named "Native tier with Mesen and BlipToaster loaded as packs" and "UI tier with Mesen and BlipToaster
loaded as packs". Their commands are the scripts, unchanged.

- [ ] **Step 5: A first run**

```sh
pnpm test:native:packs app-play-nes bliptoaster-rom
pnpm test:ui:packs bliptoaster-settings
```
(the runners take slugs as filters; check how this tree spells a filter). Expected: they pass, the prelude finding both
packs loaded. A failure here goes to Task 7's report, not a fix.

- [ ] **Step 6: Commit**

```sh
git add package.json packages/native/test/engine-graph/golden/product-registrations.json \
        packages/retroplug/scripts/run-native-tests.mjs packages/retroplug/scripts/run-ui-tests.mjs \
        packages/retroplug/test-native/_packsPrelude.ts packages/retroplug/test-ui/_packsPrelude.ts .github/workflows/build.yml
git commit -m "test: packs-desktop leaves BlipToaster out, and the tiers' pack modes load it beside Mesen, listed first"
```

---

### Task 7: Both tiers against the two packs: run and triage

**Files:** none until the controller decides the fixes.

- [ ] **Step 1: Run both pack modes and both desktop modes**

```sh
RISA_SRC=/nonexistent pnpm test:native:packs 2>&1 | tee .superpowers/m5c/native-packs.log
RISA_SRC=/nonexistent pnpm test:ui:packs 2>&1 | tee .superpowers/m5c/ui-packs.log
pnpm test:native 2>&1 | tail -5; pnpm test:ui 2>&1 | tail -5
```
(background each with a completion notice; each takes minutes. `RISA_SRC=/nonexistent` keeps a local risa source tree
out of the run, as CI has none, which is how M5b recorded its baselines.)

- [ ] **Step 2: Report, do not fix**

For every file that fails in a pack mode and passes in its desktop mode, report: the slug, the failing case, its
assertion and output, and a first classification:
- **(a) registration order:** BlipToaster's roles, ROM provider and tracker integration register after the product's
  contributions, so an order a test reads (a role list, a provider's rank, a menu's rows, the Assets menu's consoles)
  differs;
- **(b) a path the pack does not reach:** something the built-in BlipToaster provides that the pack's halves do not
  (a contribution, a host hook, a script node in some context, a module a test imports resolving to its own copy);
- **(c) the mode itself:** the prelude, the externals, the product tree, the env, the pack order;
- **(d) other.**

Also report the pack modes' skip counts against the desktop modes' (`skip-baseline.json`'s `native-packs` and
`ui-packs` against `native` and `ui`), and any file whose output differs in a golden (a parity, kit or render hash):
that is never noise (AGENTS.md). The known machine-local `app-cores` GBA failure (in both modes) is not a finding.

The controller decides each fix (a test change only where the test reads an order that is not a product promise; a
code fix otherwise) and hands them back as follow-up steps of this task, each its own commit.

- [ ] **Step 3: The skip baselines, only if they moved**

If a pack mode's skips differ from the baseline's: `RISA_SRC=/nonexistent pnpm test:native:packs
--update-skip-baseline` (and the UI's), check `native-packs` and `ui-packs` still hold the same files and counts as
`native` and `ui` (a pack-only skip is a finding to report), and commit:

```sh
git add packages/retroplug/scripts/skip-baseline.json
git commit -m "test: the pack modes' skip baselines with BlipToaster loaded as a pack"
```

---

### Task 8: Docs and verification

**Files:**
- Modify: `docs/downloadable-packs-loading-design.md` (section 5), `spec/02-native-host.md` (Packs),
  `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/14-hosts-harnesses.md` (if it names the pack hosts' packs)
- Modify: `docs/downloadable-packs-m5c-plan.md` (this plan: After the reviews, design questions)

- [ ] **Step 1: The docs**

- The loading design's section 5, "Where each proof stands": Mesen's and BlipToaster's proofs are built (M5b, M5c: the
  test product `packs-desktop` is the desktop list without `mesen` and `bliptoaster`, and the tiers' pack modes load
  both); the CLI's is its own milestone, which first loads BlipToaster's cli half. Section 5's first paragraph already
  names the test product as both left out: check it reads true.
- spec/02's Packs section: `PACK_BUNDLED` (what a pack may bundle a copy of, the inputs check, and that it is no
  contract and no API); a pack with no native half (`retroplug_pack(<package> TS_HALVES)`, declared from the root for a
  package with no native/ folder); the script half copied from the package's toolchain output; a required platform
  orders the load.
- spec/06: `packs-desktop`'s list; the pack scripts build `retroplug-bliptoaster-pack`; `build/packs/bliptoaster/0.1.0`;
  the pack modes load both packs; which command proves a change to a pack's halves (`pnpm test script/build-pack-halves`,
  `retroplug-packs-test "[packs-script]"`, the pack modes).
- spec/14: wherever it describes the pack hosts' packs, both.
- spec/07's Downloadable packs entry: M5c done; what is left is the CLI on a pack and M5d (and M3, M4). Delete, from
  "Open from M2d", the clause about the BlipToaster fixture staging at `build/packs/bliptoaster/1.0.0` (this plan
  replaced it); add "Open from M5c" with this plan's design questions.
- AGENTS.md: only if a rule changed. The pack-boundary rules there name the native boundary; a pack's TS imports are
  spec/02's, so no line is needed unless the reviewer says otherwise.

- [ ] **Step 2: Verify**

```sh
pnpm typecheck && pnpm check:boundaries && pnpm check:docs
cmake --build build -j$(nproc)
pnpm test
build/bin/retroplug-packs-test && build/bin/retroplug-packs-heap-test && pnpm test:plugin engine-graph
mkdir -p .superpowers/verify/runs/m5c
env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=.superpowers/verify/runs/m5c/tv bash .superpowers/verify/tip-verify-par.sh
bash .superpowers/verify/compare.sh .superpowers/verify/runs/m5b/tv .superpowers/verify/runs/m5c/tv
RISA_SRC=/nonexistent pnpm test:native:packs && RISA_SRC=/nonexistent pnpm test:ui:packs
```
(the tip verify in the background with a completion notice; check the m5b run folder's name under
`.superpowers/verify/runs/` first). Expected: the compare differs from M5b's only by additions; both pack modes pass;
the known `app-cores` GBA case is the only native failure in either mode.

- [ ] **Step 3: The plan's own record and commit**

Append "## After the reviews" (what the reviews changed, task by task) and "## Design questions this plan found" to this
plan (at least the ones below), then:

```sh
git add docs spec
git commit -m "docs: BlipToaster as a whole pack, PACK_BUNDLED, a pack with no native half, and the platform load order"
```

Design questions to carry (answered or left open):
1. **A bundled copy drifts.** A pack's copy of a `PACK_BUNDLED` module is its build's: a fix to tracker's NES helpers
   reaches a pack only when the pack is rebuilt. Accepted (the user's decision); a library pack stays the route if
   shared code grows large, holds state or ships on its own schedule.
2. **`PACK_BUNDLED` is held to its imports, not to purity.** The inputs check keeps a listed module from pulling in an
   unlisted file, but a listed module that grows module state would still be copied into every pack. Should a check
   read the listed modules for top-level mutable state?
3. **Two packs requiring each other's platforms** both wait; the leftover loop loads them in the given order and the
   TS loader refuses whichever finds its platform missing. Should the table refuse them as a cycle, as it does for
   `requires.packs`?
4. **The cli half is built and not loaded here.** No host in this plan loads BlipToaster's cli half: the CLI milestone's
   dispatcher built without `bliptoaster` is its first loader.
5. **The script half needs a product build.** `--script-from` copies the toolchain output a configured product builds;
   an SDK repo building a pack runs the toolchain through the tool instead, and the two paths' bytes are equal only
   while the toolchain names its source after the pack's id.
6. **A TS-only pack is declared from the root.** A package with no native/ folder has no CMakeLists to call
   `retroplug_pack()` from: should `retroplug_package()` build a pack for any package with a `pack.json`?

---

### Task 9: The CI gate

- [ ] **Step 1: Ask the user to push `feature/modularize`.** Then dispatch `build.yml` and read every job: the linux
  job's two pack steps must run with both packs and pass; the other jobs as before (the full build now also defines
  `retroplug-bliptoaster-pack` on every platform with a pack folder, and the packs test builds it on Windows and macOS).
