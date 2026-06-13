# Restructure — overview

This is a **second, independent plan track** alongside the feature-migration
steps in [README.md](./README.md) (01–22, "old → new shell"). Those steps are
about reaching feature parity. *This* track is about the **repo, build, and
package structure** of the new shell itself — turning a single CMake-driven C++
tree with TypeScript bolted on into a TypeScript/node-style monorepo with the
generic framework extracted as a reusable `dpf.js` package.

The files are prefixed `restructure-NN-` so they sort together and never get
confused with the numeric feature steps.

## Why

The new shell works, but its structure grew the same way the legacy code did:

- **One giant `CMakeLists.txt`** (~800 lines) that mixes the actual build with
  21 `add_custom_target`s for developer workflows — tests, screenshots, plugin
  validation, and a dozen Reaper render/analyze/author targets. Build and
  workflow are tangled.
- **JS tooling reaches into a submodule.** The UI bundle is built with an
  esbuild pinned at **0.14.43 (2022)** living inside
  [deps/lv_binding_js/node_modules](../deps/lv_binding_js/), and the root
  `node_modules` holds a second, separate set of deps. `tools/build-ui.js`
  bridges the two with an alias plugin and dual `nodePaths`. Adding a dep means
  knowing which of two roots it belongs in.
- **Two hand-maintained native↔TS surfaces.** [PluginRpcService](../src/PluginRpcService.hpp)
  (57 methods, auto-exposed via the rpcpp/reflect-cpp generator) serves the UI;
  [cli/TestHarness.cpp](../cli/TestHarness.cpp) exposes a *separate* `emu` global
  of ~40 native trampolines, hand-mirrored in
  [test/harness/index.ts](../test/harness/index.ts), for the CLI and tests.
  They overlap heavily (`loadRom`, `readMemory`, run loops…) and drift.
- **The framework and the product are entangled.** DPF + LVGL + txiki + the
  rpcpp generator + `LvglJsEngine` are generic infrastructure, but they live in
  the same tree as the emulator cores, LSDJ, and the plugin. Nothing can be
  reused for a different DPF.js plugin without copying RetroPlug.
- **Native code uses npm, but as an afterthought.** The root `package.json`
  describes itself as "Build-time JS deps for the UI bundle"; there is no
  workspace, no per-package boundary, no place for a generated client to live.

The goal is a layout where the **only** C++ is the core RetroPlug functionality,
everything else is TypeScript, and the framework is an installable dependency.

## The key architectural fact

The current architecture is **inverted** from a normal Node stack: **C++ hosts
the JS engine, and TypeScript is the guest.** Both consumers embed txiki/QuickJS
inside a C++ binary and call into native over an *in-process* FFI shim:

- The **plugin** (VST3/CLAP/…) embeds txiki via [LvglJsEngine](../src/LvglJsEngine.cpp),
  runs the bundled UI TS inside itself, and TS calls C++ through
  `plugin.__rpcSend` ([ui/plugin/transport.ts](../ui/plugin/transport.ts)).
- The **CLI / test harness** ([cli/main.cpp](../cli/main.cpp) `--test`) does the
  same: a C++ binary embeds QuickJS and exposes the `emu` global.

This is *why* the CLI can be TypeScript without Node — it ships as a
self-contained native binary with the TS embedded as bytecode, exactly like the
UI bundle. It is also why the boundary is already clean: the rpcpp client
([client.ts](../ui/plugin/client.ts)) is built over an **injectable transport**;
today that transport is in-process, but the design doesn't care.

## Target architecture

```
dpf.js  (external npm package + template repo — restructure-07)
  └── generic C++: DPF + LVGL + txiki/QuickJS + rpcpp generator + LvglJsEngine
      generic TS:  runtime/lvgljs front door, esbuild build + alias config,
                   tsconfig bases, React reconciler glue
      generic host: the "embed txiki + a native rpc service + run a TS bundle"
                    launcher (used by BOTH the plugin and the CLI)

RetroPlug monorepo (pnpm workspace)
  packages/native      RetroPlug-specific C++ only: emulator cores wiring,
                       PluginRpcService (the ONE rpc surface), project/lsdj/dsp.
                       Consumes dpf.js from node_modules via add_subdirectory.
  packages/retroplug   TS: the generated typed client (gitignored, built from
                       native's OpenRPC schema) + domain ergonomics. txiki-
                       targeted, single runtime. Consumed by everything below.
  packages/cli         TS app → bundled into the generic txiki host → a single
                       end-user `retroplug` executable. No Node at runtime.
  packages/ui          React app → bundled → embedded into the plugin binary
                       that packages/native links.
```

Everything is the same shape: **one native core exposing one rpcpp service;
every consumer is a TS app running inside an embedded txiki host that wires the
in-process transport.** Tests become just another consumer of
`packages/retroplug`.

## Decisions (made 2026-06-13, recorded so they don't get relitigated)

- **CLI runtime = txiki-hosted TS, not Node.** End-users run the CLI without
  installing Node; it ships as a native binary with embedded TS bytecode. This
  collapses the "two runtimes" problem — txiki is the only JS engine anywhere.
- **Dependencies = keep git submodules.** A full CPM migration is a poor fit for
  this fork-heavy, 3-level-nested-submodule graph (RetroPlug → lv_binding_js →
  txiki → libuv/quickjs/…); commit-pinned CPM forks are just submodules by
  another name, and CPM's `GIT_SUBMODULES` forwarding is buggy for nested
  chains. Submodules already give recorded-SHA determinism for free. Revisit
  only if dpf.js extraction leaves a few trivial leaf deps.
- **Sequencing = internal first.** Everything that can be done inside this one
  repo (restructure-01 … 06) lands and is verified with the existing test
  suites before the cross-repo dpf.js extraction (restructure-07).
- **Workspace-first.** Stand up the pnpm workspace skeleton before moving
  workflow targets to scripts, so scripts land in their final home once.
- **Bundling stays esbuild → tjsc/qjsc bytecode.** txiki's own compiler was
  evaluated and rejected: `tjs compile` ships a source ZIP (no bytecode, no
  parse-time win), and `tjs bundle` just downloads the real esbuild. The current
  hybrid is already the recommended one.
- **dpf.js ships as npm source + a template repo**, not prebuilt libs (a
  linkable C++ ABI matrix is the wrong problem to take on; consumers already
  have a toolchain). Consumers resolve it with
  `require.resolve('dpf.js/package.json')` + `add_subdirectory`.

## The steps

Each `restructure-NN` file is a self-contained, PR-sized milestone with the same
structure as the feature steps (Goal / Depends on / Architecture / Tasks /
Verification / Risks). **restructure-01, 02, and 03 are Done** (2026-06-13 — see
each file's *As-built* note); 04–07 are Pending.

| Step | What lands | Depends on |
| --- | --- | --- |
| [01 — pnpm workspace skeleton](./restructure-01-pnpm-workspace.md) | Convert npm→pnpm; `packages/{native,retroplug,cli,ui}`; relocate `ui/`, `runtime/lvgljs`, `test/harness`. Nothing else changes behaviourally. | — |
| [02 — JS toolchain consolidation](./restructure-02-js-toolchain.md) | Hoist esbuild to a workspace devDep; unify the two `node_modules` roots; tsconfig bases; one bundling entry. | 01 |
| [03 — CMake → package.json scripts](./restructure-03-cmake-scripts.md) | Move the ~18 workflow/test `add_custom_target`s into pnpm scripts. **Keep** the `ui-regenerate` build DAG and `sav-regenerate` codegen in CMake. | 01 |
| [04 — Unify the native↔TS surface](./restructure-04-unify-rpc-surface.md) | **Keystone.** Fold the `TestHarness` `emu` methods into the rpcpp service so there is ONE generated client in `packages/retroplug`, consumed by UI, CLI, and tests. | 01 |
| [05 — Generic txiki host + TS CLI](./restructure-05-txiki-host-and-cli.md) | Extract the generic "embed txiki + native rpc + run a TS bundle" launcher; rewrite the CLI as a TS app; retire `cli/Script.hpp` + `TestHarness.cpp`. | 04 |
| [06 — Decouple dpf.js (in-repo)](./restructure-06-dpfjs-decouple.md) | Fix the 4 framework/product coupling points in place (env-var namespace, parameter-spec injection, RPC-service injection, plugin-identity/UI-entry) so the seam is clean while RetroPlug still builds as one repo. | 03, 04 |
| [07 — Extract & publish dpf.js](./restructure-07-dpfjs-extract.md) | Move the generic framework to its own repo, publish as npm source + template, repoint RetroPlug to consume it via `require.resolve` + `add_subdirectory`. Flattens the dep graph. | 02, 06 |

## Cross-cutting concerns (true of more than one step)

- **The build DAG is a build-time cycle**, not workflow noise:
  `native → OpenRPC schema → generated client → ui/cli bundle → bytecode →
  embed → final plugin link`. pnpm becomes the *human* entry point, but CMake
  keeps owning that chain (`ui-regenerate`). Don't try to move it into pnpm.
- **`packages/retroplug` must stay txiki-compatible** — no `node:*` APIs on its
  hot path — because the same package is bundled into both the plugin and the
  CLI, neither of which is Node.
- **Generated artifacts stay derived, never committed:** the OpenRPC-generated
  TS client, `bundle.js`, `bundle_data.c`, and any generated CMake config. This
  is already the rule; the package split must not accidentally check them in.
- **The load-bearing `build/` dir** ([AGENTS.md](../AGENTS.md)) must survive
  every step. No step should require `rm -rf build` to proceed.
- **miniz symbol collision** (mesen vs txiki, patched today by
  [a force-include rename](../deps/mesen/Utilities/mesen_miniz_renames.h))
  becomes a *cross-package* concern only at restructure-07, when txiki moves into
  dpf.js and mesen stays in RetroPlug. Harmless until then; flagged there.
- **No format/back-compat shims** ([AGENTS.md](../AGENTS.md) pre-release rule)
  applies to serialized shapes. It does **not** forbid semver-versioning the
  dpf.js npm package itself — that versioning is needed and is separate.
- **Devcontainer toolchain (Node + pnpm):** the base image originally shipped
  **Node v18.19.1** (EOL) and no pnpm/corepack. Fixed 2026-06-13 — the Dockerfile
  now installs **Node 26 from NodeSource** (replacing `/usr/bin/node` in place, so
  the cached `NODE_EXECUTABLE` stays valid) and provisions pnpm via
  `corepack` (installed from npm, since Node 26 unbundled it) honouring the
  `packageManager` field. pnpm stays on 9.x to avoid lockfile churn. The 2022-era
  esbuild in `deps/lv_binding_js` still works under Node 26, so it isn't a
  blocker for restructure-02 — but modernizing it there is still wanted.

## Working with this plan

Land the steps roughly in table order; the `Depends on` column is the real
constraint. After each step, the relevant verification loop from
[AGENTS.md](../AGENTS.md) (`cli-ts-test`, `ui-ts-test`, `validate`, the Catch2
binaries) must still pass — the whole point of internal-first is that every step
is independently verifiable in this one repo.
