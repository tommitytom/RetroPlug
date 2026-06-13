# restructure-02 — JS toolchain consolidation

**Status:** Done (2026-06-13).

## As-built (what actually landed vs. the plan below)

The build **tooling** moved onto the workspace; the framework deps stay in the
submodule until dpf.js extraction (restructure-07), so this step does *not* fully
collapse to a single `node_modules` — that lands at 07. What landed:

- **esbuild hoisted to the workspace** — `esbuild@^0.28` (resolved 0.28.1) is a
  root `devDependency`; all three scripts now `require("esbuild")` from the
  workspace. The 2022-era esbuild in `deps/lv_binding_js` is no longer used by the
  host build.
- **esbuild-plugin-alias dropped** in favour of esbuild's native `alias` option.
- **One shared module** [tools/esbuild-shared.js](../tools/esbuild-shared.js):
  the esbuild instance, the framework/generated alias map, `mainFields`,
  `target`, `reactNodePath`, and the deduped `writeDepfile`/`escapeMake` helpers.
  [build-ui.js](../tools/build-ui.js), [build-test.js](../tools/build-test.js),
  and [gen-rpc-ts.js](../tools/gen-rpc-ts.js) all import it.
- **@msgpack/msgpack resolves natively** via `mainFields: ["module","main"]`
  (the real reason for the old hard alias: `platform:"neutral"` has no default
  mainFields). The `dist.esm` hard alias is gone. react/react-reconciler have no
  `module` field so they fall to `main` — identical bundling.
- **Native-alias cwd fix:** every build sets `absWorkingDir: REPO_ROOT` (CMake
  runs the scripts with cwd=`build/`, and native `alias` resolves values vs the
  working dir). Alias values are absolute.
- **`target: "es2020"`** on the UI + test bundles (matches `tsconfig.base.json`)
  to stay within QuickJS-ng 0.13's syntax ceiling under modern esbuild.
- **nodePaths trimmed:** build-ui keeps a single nodePath into the submodule for
  react/react-reconciler/scheduler (framework, → 07); the root-`node_modules`
  entry is gone (@msgpack resolves by walk-up). build-test dropped nodePaths
  entirely (self-contained harness).
- **Kept as native-alias entries** (removed at 07): `lvgljs-ui`, `lvgljs`,
  `@rpcpp/createClient` / `@rpcpp/MsgpackCodec` / `@rpcpp/transport` (the package
  index pulls a `node:child_process` Stdio transport), `plugin-service` (the
  generated client).
- **Unchanged:** `tsconfig.base.json` / `packages/ui/tsconfig.json` (already
  consolidated in 01), `.gitignore`, `CMakeLists.txt`. `post-create.sh` keeps the
  `deps/lv_binding_js` npm install (now only for react et al. until 07; comment
  reworded). `zod` stays a root dep but isn't bundled (the codegen only emits an
  `import { z } from "zod"` string).
- **Verified:** full `cmake --build` clean; `ui-ts-test` (boots the esbuild-0.28
  bundle in QuickJS), full `cli-ts-test`, `validate` (clap-validator 18/0 +
  pluginval SUCCESS), and `screenshot` all green.

The original plan follows for reference.

## Goal

Unify the JavaScript build toolchain from two separate `node_modules` roots (one in the repo, one in `deps/lv_binding_js`) into a single pnpm workspace. Make `esbuild` a workspace devDependency (modern version), replace path aliases with real package resolution, collapse redundant `tsconfig.json` files into a shared base, and establish a single entry point for UI bundling. This eliminates the need to know which of two roots a dependency belongs in.

## Depends on

- [Step 01 — pnpm workspace skeleton](./restructure-01-pnpm-workspace.md) — the workspace layout (`packages/{native,retroplug,cli,ui}`) and relocation of `ui/`, `runtime/lvgljs`, and `test/harness` must be in place first.

## Architecture introduced

- **Workspace esbuild.** Hoist `esbuild` (currently pinned 0.14.43 in `deps/lv_binding_js/package.json`) into the root `package.json` as a workspace devDependency. Both `tools/build-ui.js` and `tools/gen-rpc-ts.js` resolve it from the workspace, not the submodule. Decide on a version bump: keep the pin for compatibility or upgrade to a modern version (available on npm).
- **Single `node_modules` graph.** Root and workspace packages share one dependency tree resolved by pnpm. The current alias plugin in `tools/build-ui.js` (lines 38–53) bypasses pnpm's file-dep mechanism by pointing directly at source paths. After restructure-01, these become real workspace packages or properly copied by pnpm, eliminating the need for the plugin (or narrowing it to rpcpp client pieces, deferred to restructure-04).
  - `lvgljs-ui` → post-restructure-01 source path or workspace package.
  - `lvgljs` → post-restructure-01 source path.
  - `@rpcpp/createClient`, `@rpcpp/MsgpackCodec`, `@rpcpp/transport` → deferred; keep direct paths for now, or move to workspace packages in restructure-04.
  - `@msgpack/msgpack` → normal workspace resolution (no alias needed).
- **Shared `tsconfig` base.** Create a root `tsconfig.base.json` with shared compiler options and path mappings. Each package (`packages/ui`, `packages/cli`, `packages/retroplug`) extends it and adds package-specific settings. Collapse the duplicate alias paths currently maintained in `ui/tsconfig.json` (lines 13–22).
- **React + reconciler placement.** React 19.2.0 and react-reconciler 0.33.0 are part of the generic DPF.js framework; restructure-07 will extract them. **Recommendation:** for restructure-02, keep importing them from `deps/lv_binding_js/node_modules` via tsconfig alias; defer explicit workspace package setup to restructure-07. This minimizes scope.

## Tasks

1. Create or update `pnpm-workspace.yaml` at the root to declare `packages/{native,retroplug,cli,ui}` as workspace members (from restructure-01 if not already done).

2. Add `esbuild` to the root `package.json` as a devDependency. Decide: keep version 0.14.43 or upgrade (e.g., to 0.23.x or later). Document the decision. Update `tools/build-ui.js` line 9 and `tools/gen-rpc-ts.js` line 22: replace `require(path.join(LV_BINDING_DIR, "node_modules/esbuild"))` with `require("esbuild")`, relying on Node's module resolution from the workspace root.

3. In `tools/build-ui.js`, remove or repoint the alias plugin (lines 38–53). If keeping rpcpp aliases for now:
   - Update the alias paths to reflect post-restructure-01 locations (e.g., `packages/ui/runtime/lvgljs`).
   - Remove the `@msgpack/msgpack` alias; pnpm will resolve it automatically.
   - If React/reconciler stay in `deps/lv_binding_js`, keep those paths in tsconfig (next task), not the alias plugin.

4. Create a `tsconfig.base.json` at the root with shared options and path mappings:
   ```json
   {
     "compilerOptions": {
       "target": "ES2020",
       "module": "ESNext",
       "moduleResolution": "bundler",
       "jsx": "react-jsx",
       "lib": ["ES2020", "DOM"],
       "esModuleInterop": true,
       "allowSyntheticDefaultImports": true,
       "skipLibCheck": true,
       "forceConsistentCasingInFileNames": true,
       "noEmit": true,
       "paths": {
         "lvgljs-ui": ["packages/ui/deps/lv_binding_js/src/render/react"],
         "lvgljs": ["packages/ui/runtime/lvgljs"],
         "react": ["deps/lv_binding_js/node_modules/@types/react"],
         "@rpcpp/*": ["deps/rpcpp/clients/typescript/client/src/*"]
       }
     }
   }
   ```
   (Adjust paths based on post-restructure-01 relocation.)

5. Update `packages/ui/tsconfig.json` to extend `tsconfig.base.json` and remove duplicate path mappings. Add package-specific `include` and `exclude` as needed.

6. Remove or simplify the `nodePaths` array in `tools/build-ui.js` (lines 33–36) — esbuild will resolve via the workspace's single `node_modules`. Keep platform `neutral` and all other esbuild options unchanged.

7. (Optional, if upgrading esbuild) Test the version upgrade in isolation: run `node tools/build-ui.js` and verify the bundle output format is unchanged (ESM, `platform: neutral`, compatible with `tjsc`). Confirm no new esbuild config errors.

8. Verify CMake's `ui-regenerate` target still works: `cmake --build build -j$(nproc)`.

## Verification

- `pnpm install` produces a single `node_modules` tree at the workspace root; no separate roots exist.
- `node tools/build-ui.js` and `node tools/gen-rpc-ts.js <exe> <out>` both succeed without errors.
- `cmake --build build -j$(nproc)` — full build succeeds; the `ui-regenerate` target completes.
- `make -C build ui-ts-test` and `make -C build cli-ts-test` pass (headless test loops).
- `make -C build validate` passes (integration tests).
- Plugin or standalone (`bin/retroplug`) runs and the UI displays correctly; embedded bytecode bundle is functional.
- IDE (VS Code TypeScript language server) resolves `lvgljs`, `lvgljs-ui`, and `@rpcpp/…` imports without red squiggles.

## Risks / open questions

- **esbuild 0.14 → modern: API/plugin breaks.** Upgrading esbuild may require changes to the build script or plugin. The `esbuild-plugin-alias` package (v0.2.1) was written for esbuild 0.14 and may not work with modern versions; test in isolation. Alternatively, defer the bump to a follow-up.
- **Why the alias plugin exists.** It bypasses pnpm's file-dep mechanism. After restructure-01, `lvgljs-ui` and `lvgljs` should be real workspace packages or properly copied by pnpm; the alias can be removed. For rpcpp client pieces, the alias exists because those are source files in a subdirectory of a third-party package. After restructure-04, when a typed `packages/retroplug` client is generated, the alias can be removed entirely.
- **React + reconciler: defer to restructure-07.** These live in the DPF.js framework slice and will move in restructure-07. Keeping them in `deps/lv_binding_js` for now (via tsconfig alias) and deferring explicit workspace package setup avoids scope creep. Document this clearly so it's not forgotten.
- **Generated client gitignored.** Confirm `build/ui/generated/PluginService.ts` remains `.gitignore`d.
- **CMake environment.** The scripts are invoked by CMake in the build root; ensure NODE_PATH or module resolution still finds the workspace `node_modules`. No special CMake changes should be needed if the scripts use `require()` with proper Node resolution.
