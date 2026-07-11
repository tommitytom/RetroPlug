# Agent guide

Start with these — all short:

- [README.md](README.md) — what RetroPlug2 is, how to build, project layout.
- [spec/](spec/README.md) — the architecture (one doc per concern). The build lives
  in `packages/native/` (C++ host: `src/` core + `src/host/` layer + `plugin/`) +
  `packages/retroplug/` (TS/UI). The older **legacy** build has been removed;
  [spec/07-migration.md](spec/07-migration.md) tracks the residual cleanup + feature gaps.
- [docs/lsdj.md](docs/lsdj.md) — the LSDj domain reference + how to test LSDj sync /
  DAW timing / audio quality headlessly.

The React / LVGL / QuickJS framework itself is the `deps/dpf.js` submodule
(documented in its own README).

The rules below are the parts that don't fit those.

## Workflow rules

- **Don't push to remotes or open PRs without an explicit ask.** The user pushes
  their own work.
- **`dpf.js`** (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) is a
  nested git submodule at `deps/dpf.js` — clone `--recursive`. It's consumed via
  `require.resolve('dpf.js')` + `add_subdirectory` + a pnpm `link:./deps/dpf.js`;
  `pnpm install` wires the link before the first configure. lv_binding_js has its
  OWN pnpm workspace (react / react-reconciler / lvgljs-ui) — after a fresh checkout
  also run `pnpm install` in `deps/dpf.js/deps/lv_binding_js`, or the UI bundle
  can't resolve `react`. Don't bump any submodule pointer (in either repo) without
  checking — they're managed deliberately. RetroPlug also keeps `deps/sameboy` +
  `deps/mesen` / `deps/r8brain` / `deps/enkiTS` (the shared core), and `deps/efsw`
  (greenfield's file-watcher is *designed* to use it — `drainChangedPaths` is a stub
  today, so nothing links it yet). `deps/catch2` is the C++ unit-test framework:
  it's `add_subdirectory`'d at the root (`EXCLUDE_FROM_ALL`) and linked by the
  `test:plugin` binaries (`retroplug-plugin-test` / `retroplug-classid-test`) and
  the LSDj sav-codec oracle binaries (`test:lsdj-diff` / `test:lsdj-sav`, which
  compile vendored liblsdj + the test-only C++ codec) as `Catch2::Catch2WithMain`.
- **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
  `build/` is load-bearing for the dev loop.
- **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
  entry points; they run the configure this project needs and build in parallel.
  Bare `./build.sh` = incremental; `--clean` wipes `build/`. (`--tests` /
  `BUILD_TESTING` is inert — the C++ Catch2 test binaries are `EXCLUDE_FROM_ALL`,
  built by name from the `pnpm test*` scripts, not gated on `BUILD_TESTING`.) For a
  single target after a configure,
  `cmake --build build --target <t> -j$(nproc)` — always pass `-j`, the default is
  a single-threaded slog. `build.bat` enters vcvars64 + the tool PATH and runs the
  `cl` + vcpkg-`x64-windows-static` configure (overridable via `VCPKG_ROOT` /
  `RGBDS_DIR` / `NODE_DIR`).
- **Never commit derived artifacts** — the embedded bundle C arrays
  (`build/native/*bundle_data.c`).
- **Config migrations (versioned, raw-JSON).** Persistence is TS-owned. Every serialized
  JSON root (project / DPF state, user config, bindings, recent) is version-**stamped**: a
  file stamped newer than the build is refused; one stamped older is **migrated** up. Keep
  only the LATEST zod schema per root — never a per-version copy. A breaking (non-additive)
  change bumps that root's version constant (`K_PROJECT` / `*_SCHEMA`) and adds one raw
  `(obj) => obj` step to its migrations map
  ([migrate.ts](packages/retroplug/src/migrate.ts)), applied to the raw JSON *before* zod
  validates; steps must be idempotent-safe. Additive-only changes still need no step (zod
  `.default()`s fill them). Two exceptions: the per-system **role-config** that crosses to
  native stays reflect-cpp `DefaultIfMissing`-tolerant (the one outlier), and the LSDj `.sav`
  carries its own binary format version. The model + stamps live in
  [spec/05-data-persistence.md](spec/05-data-persistence.md).

## Framework gotchas

**`lv_binding_js` ignores `insertChildBefore` (always appends).** React reorders
children by calling `insertChildBefore`, but lv_binding_js's
[comp.cpp](deps/dpf.js/deps/lv_binding_js/src/render/native/core/basic/comp.cpp)
ignores `beforeChild` and always appends. So swapping a component at a stable React
position (e.g. a tile → a menu) or a mid-list insert lands the new node at the END
of the LVGL child list, not its source position. Two workarounds, both already in
the code:

- **A stable per-id wrapper** whose position never changes, with a single swappable
  child (`appendChild` lands correctly when the parent has ≤1 child) — `StableSlot`
  in [SystemGrid.tsx](packages/retroplug/ui/screens/grid/SystemGrid.tsx).
- **Re-key the parent on the visible set** to force a full remount, so every child
  mounts fresh in JSX order —
  [Menu.tsx](packages/retroplug/ui/screens/menu/Menu.tsx).

If a tile / row / menu renders in a confusingly wrong slot, this is almost certainly
why. (Build-target gotchas — which target rebuilds the standalone vs the embedded
bundle — are in [spec/06-build-test.md](spec/06-build-test.md).)

## Verification loop

Verify your own work headlessly before claiming it's done — **a "tests pass" claim
must be backed by an actual exit-zero** from one of these.

The headless loop (the only path — legacy is gone) is documented in
[spec/06-build-test.md](spec/06-build-test.md): `pnpm test` (pure-TS mock),
`test:native` (real host + cores), `test:ui` (LVGL React), `test:plugin` (Catch2
C++ unit checks — the per-context window-hook routing + the class-id counter sync
that keeps the DAW-hosted editor from rendering blank), `screenshot`,
`reaper:editor` (`tools/run-reaper-editor.sh` — floats the hosted plugin editor in
headless Reaper and asserts its LVGL snapshot rendered; the only check of on-screen
editor rendering, not in CI), `reaper:editor-reopen` (`tools/run-reaper-editor-reopen.sh`
— loads mGB through the UI, closes + reopens the editor, and asserts the project is
still shown; mouse-driven since keys don't reach the plugin editor headlessly),
`reaper:editor-autoload` (`tools/run-reaper-editor-autoload.sh` — floats the editor with a
project already in the control plane and asserts the editor shows it, the session-restore /
setState half; deterministic, no mouse) — both guard the editor↔control-plane single-store
graph (a close/reopen or a setState-restored project showing the start menu means the UI
composed its own store again); not in CI. Then the `tools/run-sanitizer.sh` thread / address checks,
and `validate`. The LSDj-sync / DAW-timing / audio-quality matrix runs headlessly
too — the real-Reaper
`reaper:lsdj-*` renders + `tools/reaper-timing-analyze.py`; see
[docs/lsdj.md](docs/lsdj.md).
