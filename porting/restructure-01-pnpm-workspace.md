# restructure-01 — pnpm workspace skeleton

**Status:** Done (2026-06-13).

## As-built (what actually landed vs. the plan below)

Refined to the **minimal behaviour-preserving relocation** once the live wiring
was read. The plan below proposed moving the plugin client, the generated
client, `runtime/lvgljs`, and `test/harness` into `packages/retroplug` in this
step; that was deferred because those belong with the packages that gain real
content later, and moving them now would be churn (the client is *unified and
regenerated* in [restructure-04](./restructure-04-unify-rpc-surface.md), and
`runtime/lvgljs` is framework that leaves for dpf.js in
[restructure-07](./restructure-07-dpfjs-extract.md)). What landed:

- **npm → pnpm** at the root: `pnpm-lock.yaml` (imported), `package-lock.json`
  removed, `pnpm-workspace.yaml` with `packages/*`, `packageManager` pinned to
  `pnpm@9.15.9` (the 9.x line still supports the container's Node 18). Committed
  separately as the conversion chunk.
- **`ui/` → `packages/ui/src/`** wholesale (preserves all intra-`ui` relative
  imports + git history). `ui/tsconfig.json` lifted to `packages/ui/tsconfig.json`,
  now extending a new root `tsconfig.base.json` (shared compiler options + the
  alias `paths`, `./`-prefixed so esbuild accepts them without `baseUrl`).
- **Stub `package.json`** for `packages/{native,retroplug,cli,ui}` (cli + ui
  declare a `workspace:*` dep on `@retroplug/retroplug` to express the graph;
  not yet load-bearing — esbuild still resolves via aliases until restructure-02).
- **Left in place** (relocate later): `runtime/lvgljs` (root → dpf.js at 07),
  `test/harness` + `test/ts` (root), the generated client (`build/ui/generated/`,
  → `packages/retroplug` at 04), and the plugin client (rode along inside
  `packages/ui/src/plugin/`, → `packages/retroplug` at 04).
- **Edits required by the move:** `tools/build-ui.js` entry point
  (`ui/PluginUI.tsx` → `packages/ui/src/PluginUI.tsx`) and the two escaping
  relative imports of `runtime/lvgljs/input` ([PluginUI.tsx](../packages/ui/src/PluginUI.tsx),
  [menu/Menu.tsx](../packages/ui/src/menu/Menu.tsx)). **`CMakeLists.txt` needed
  no changes** — it references only `build/ui/` outputs, never the `ui/` source.
- **Verified:** full `cmake --build` clean, `ui-ts-test` green (boots the
  relocated React bundle), `cli-smoke` green (harness toolchain under pnpm).

### Addendum (2026-06-14): C++ relocated into `packages/native/`

This step stood up `packages/native` as a stub `package.json` and left the C++
tree at the repo root (above: "C++ stays here"). That deferral was never picked
up by a later step, so the package stayed hollow. The RetroPlug-specific C++ has
now been moved to match the [overview](./restructure-00-overview.md)'s target
layout:

- `src/` → `packages/native/src/`
- `cli/` → `packages/native/cli/`
- the Catch2 suites + the headless UI runner → `packages/native/test/`
  (`test/ts` and `test/harness` stay at the repo root — they're the shared TS
  suites run through the embedded runtime, not native unit tests).

Both `add_subdirectory` calls pass an explicit binary dir (`test`, `cli`) so the
`build/test/` and `build/cli/` output paths are unchanged. Behaviour-identical:
screenshot SHA `b1e147d7` unchanged, `test:cli` 30 / `test:ui` 11 / all Catch2
green, `validate` clap=0 vst3=0.

The original plan follows for reference.

## Goal

Stand up the pnpm monorepo workspace skeleton (`packages/{native,retroplug,cli,ui}`) and relocate TypeScript sources into their final locations, without changing build behaviour or test results. The C++ and TS continue to build, link, and test as before. This is the "workspace-first" decision from the [overview](./restructure-00-overview.md).

## Depends on

Nothing. This is the first step and unblocks [restructure-02 — JS toolchain consolidation](./restructure-02-js-toolchain.md), [restructure-03 — CMake → package.json scripts](./restructure-03-cmake-scripts.md), and [restructure-04 — Unify the native↔TS surface](./restructure-04-unify-rpc-surface.md).

## Architecture introduced

**Before:** Flat tree with two overlapping npm roots.
- `ui/` contains PluginUI.tsx + runtime/lvgljs imports; ui/tsconfig.json is the only TypeScript config.
- `deps/lv_binding_js/` has its own pnpm-workspace.yaml (allowBuilds for core-js/esbuild), node_modules, and esbuild 0.14.43.
- Root `package.json` names itself "retroplug2-ui", lists @msgpack/msgpack and zod; root `node_modules` is built from package-lock.json.
- `test/harness/` (index.ts, ui.ts) and `test/ts/` live outside any package boundary.
- Generated RPC client lives at `build/ui/generated/PluginService.ts` (derived, gitignored).

**After:** pnpm workspace with four packages, each with its own package.json and tsconfig.
```
retroplug/  (root)
  pnpm-workspace.yaml  (packages: [packages/*, !deps/**])
  package.json          (shared devDeps: esbuild, zod, @msgpack/msgpack, @types/react, etc.)
  pnpm-lock.yaml        (checksums for all transitive deps)
  tsconfig.json         (base, extends with paths shared across packages)
  packages/
    native/
      package.json      (@retroplug/native)
      [C++ stays here, CMakeLists.txt remains unchanged]
    retroplug/
      package.json      (@retroplug/retroplug)
      src/
        client.ts       (from ui/plugin/client.ts)
        transport.ts    (from ui/plugin/transport.ts)
        memory.ts       (from ui/plugin/memory.ts)
        generated/      (dir for PluginService.ts, still gitignored)
        runtime/        (from runtime/lvgljs/**, moved en masse)
      tsconfig.json     (extends ../tsconfig.json with aliases)
    cli/
      package.json      (@retroplug/cli)
      src/
        main.ts         (new entry for CLI app, replaces Script.hpp/TestHarness; lands in step 05)
      tsconfig.json
    ui/
      package.json      (@retroplug/ui)
      src/
        PluginUI.tsx    (from ui/PluginUI.tsx, keeping folder structure)
        [other .tsx/.ts from ui/]
      tsconfig.json     (extends ../tsconfig.json with aliases)
  test/
    harness/            (moved from test/harness/)
    ts/                 (stays as test/ts/ relative to root, accessible to all packages)
  tools/
    build-ui.js         (updated paths only)
    gen-rpc-ts.js       (updated paths only)
```

The key insight: `@retroplug/retroplug` is the **single shared client package**, consumed by `@retroplug/ui` and (after step 04) `@retroplug/cli` and the test harness. It is txiki-compatible (no `node:*` APIs on hot paths).

## Tasks

1. **Migrate from npm to pnpm.** Run `pnpm import` from the root to generate pnpm-lock.yaml from package-lock.json. Delete package-lock.json. Verify `pnpm install` resolves correctly and populates `node_modules/`. (The dual node_modules at `deps/lv_binding_js/node_modules` remains until restructure-02.)

2. **Create the workspace skeleton.** Add `pnpm-workspace.yaml` at the root:
   ```yaml
   packages:
     - packages/*
   ```
   Move `deps/lv_binding_js` references into `packages/.pnpmrc.yaml` or guard with `!deps/**` if needed to prevent its own pnpm-workspace.yaml from being merged into the root workspace. Verify `pnpm ls` shows the four packages and excludes deps/.

3. **Create `packages/native/package.json`.** Name: `@retroplug/native`. No TS dependencies (C++ only); scripts shell out to CMake. Example:
   ```json
   {
     "name": "@retroplug/native",
     "version": "0.1.0",
     "private": true,
     "scripts": {
       "build": "cmake --build ../.. -j$(nproc)"
     }
   }
   ```
   Rationale: Later (step 03), workflow scripts like `pnpm run retroplug:build` will call this.

4. **Create `packages/retroplug/` and relocate plugin/transport/runtime.** Move:
   - `ui/plugin/client.ts` → `packages/retroplug/src/client.ts`
   - `ui/plugin/transport.ts` → `packages/retroplug/src/transport.ts`
   - `ui/plugin/memory.ts` → `packages/retroplug/src/memory.ts`
   - `runtime/lvgljs/` → `packages/retroplug/src/runtime/lvgljs/` (entire subtree)
   
   Create `packages/retroplug/package.json`:
   ```json
   {
     "name": "@retroplug/retroplug",
     "version": "0.1.0",
     "private": true,
     "description": "RetroPlug RPC client and txiki runtime bindings",
     "exports": {
       ".": "./src/index.ts",
       "./client": "./src/client.ts",
       "./runtime": "./src/runtime/lvgljs/index.ts"
     }
   }
   ```
   Create `packages/retroplug/tsconfig.json` extending the root base. Note: generated PluginService.ts will live at `packages/retroplug/src/generated/PluginService.ts` and remain gitignored (step 02 moves its generation fully into the build DAG).

5. **Create `packages/cli/package.json`.** Name: `@retroplug/cli`. No TS yet (step 05 adds main.ts); for now it is a stub:
   ```json
   {
     "name": "@retroplug/cli",
     "version": "0.1.0",
     "private": true,
     "dependencies": {
       "@retroplug/retroplug": "workspace:*"
     }
   }
   ```

6. **Create `packages/ui/` and relocate ui/.** Move:
   - `ui/*.tsx` + `ui/*.ts` (PluginUI.tsx, EmulatorTile.tsx, KitEditor.tsx, SystemGrid.tsx, layout.ts, useBindingsEditor.ts) → `packages/ui/src/`
   - `ui/menu/` → `packages/ui/src/menu/`
   - `ui/runtime/` (console.ts) → `packages/ui/src/runtime/` (NOT the lvgljs runtime; that moved to packages/retroplug in step 4)
   
   Create `packages/ui/package.json`:
   ```json
   {
     "name": "@retroplug/ui",
     "version": "0.1.0",
     "private": true,
     "dependencies": {
       "@retroplug/retroplug": "workspace:*"
     }
   }
   ```
   Create `packages/ui/tsconfig.json` extending the root base with paths for `lvgljs-ui`, `@rpcpp/*`, etc. (same aliases as today's ui/tsconfig.json, adapted for the new depth).

7. **Create root tsconfig.json.** Define the base config with shared compiler options and paths. Each package's tsconfig.json extends this. The paths block should repoint the deps/ aliases to their actual locations (e.g., `lvgljs-ui` → `../deps/lv_binding_js/src/render/react`). Example structure:
   ```json
   {
     "compilerOptions": {
       "target": "ES2020",
       "module": "ESNext",
       "moduleResolution": "bundler",
       "jsx": "react-jsx",
       "lib": ["ES2020", "DOM"],
       "paths": {
         "lvgljs-ui": ["./deps/lv_binding_js/src/render/react"],
         "lvgljs": ["./packages/retroplug/src/runtime/lvgljs"],
         "@rpcpp/*": ["./deps/rpcpp/clients/typescript/client/src/*"],
         "plugin-service": ["./packages/retroplug/src/generated/PluginService.ts"]
       }
     }
   }
   ```

8. **Relocate test/harness and test/ts to root-accessible locations.** 
   - Move `test/harness/` → `test/harness/` (leave at repo root; it is shared across packages and CMake).
   - `test/ts/` stays at repo root.
   - Update CMakeLists.txt GLOBs to repoint from `${CMAKE_SOURCE_DIR}/test/ts` to the same path (no change needed) and `${CMAKE_SOURCE_DIR}/test/harness` (same). The `CONFIGURE_DEPENDS` glob will re-trigger on file changes even if packages move.

9. **Update build-ui.js and gen-rpc-ts.js paths.** Minimal changes:
   - `build-ui.js` line 27: change entrypoint from `../ui/PluginUI.tsx` to `../packages/ui/src/PluginUI.tsx`.
   - `build-ui.js` line 41: change lvgljs alias from `../runtime/lvgljs/index.ts` to `../packages/retroplug/src/runtime/lvgljs/index.ts`.
   - `gen-rpc-ts.js` line 8: change output from `build/ui/generated/PluginService.ts` to `build/ui/generated/PluginService.ts` (stays the same for now; step 02 will move it to packages/retroplug/src/generated/).
   - Update the alias path in build-ui.js line 52 to point to the new location when it moves (step 02).

10. **Update .gitignore.** The existing `/node_modules` entry covers the root. Add:
    ```
    /packages/*/node_modules
    /packages/retroplug/src/generated/
    ```
    (The second line will move into the existing derived-artifact rules once the build DAG is refactored in step 02.)

11. **Verify the build DAG is unbroken.** Run `cmake --build build -j$(nproc)` to confirm the plugin and CLI still build. The C++ build does not know or care about the pnpm workspace; it only consumes the outputs of build-ui.js and gen-rpc-ts.js, which are called from CMake targets (`ui-regenerate`, etc.).

## Verification

- `pnpm install` resolves without errors; `pnpm ls` shows four packages under packages/, no packages under deps/.
- `cmake --build build -j$(nproc)` produces `bin/retroplug` (plugin binary) + CLI binary without errors.
- `make -C build cli-ts-test` (or the equivalent once step 03 renames it) runs all test/ts/*.test.ts tests and passes green.
- `make -C build ui-ts-test` runs all test/ts/ui/*.test.ts tests and passes green.
- `make -C build validate` (end-to-end smoke tests) passes; plugin loads in a host and responds to MIDI.
- `cmake --build build-tsan -j$(nproc)` and `tools/run-sanitizers.sh thread` pass (no new races introduced).
- Verifying the TS module boundaries: `pnpm ls @retroplug/retroplug` shows it is depended on by @retroplug/ui and @retroplug/cli (once the latter's stubs are filled). `pnpm why @msgpack/msgpack` roots to the workspace devDeps.

## Risks / open questions

- **Dual node_modules until step 02.** The lv_binding_js submodule has its own pnpm-workspace.yaml and will maintain a separate node_modules until we hoist esbuild to a workspace devDep and unify. Until then, build-ui.js and gen-rpc-ts.js continue to pull from both roots via nodePaths. This is harmless; esbuild will still resolve @msgpack/msgpack from the root once we consolidate.
- **CONFIGURE_DEPENDS glob repointing.** CMakeLists.txt has `file(GLOB_RECURSE TS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/test/ts/*.test.ts)` at line 573. The test files move from test/ts/ to test/ts/ (no movement); the glob path is the same. No changes needed. However, if test files are ever relocated to a subdirectory inside packages/, the glob will need updating. For now, tests stay at the repo root so all packages' tests are discoverable from a single CMake invocation.
- **Generated RPC client location.** For this step, the PluginService.ts stays at `build/ui/generated/PluginService.ts` (build artifact, not in source). Step 02 will move its generation into the build DAG and place it in packages/retroplug/src/generated/ (still derived, still gitignored). The alias in tsconfig.json repoints to wherever it ends up.
- **lv_binding_js submodule isolation.** The submodule has `pnpm-workspace.yaml` and must not be swept into the root workspace. The root `pnpm-workspace.yaml` should list only `packages/*` (not `deps/**`); pnpm respects the nearest workspace root, so deps/lv_binding_js is treated as its own independent workspace. Verify with `pnpm ls` and `pnpm -C deps/lv_binding_js ls`.
