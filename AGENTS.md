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
  today, so nothing links it yet). `deps/catch2` is now unused (legacy test
  framework) — removable.
- **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
  `build/` is load-bearing for the dev loop.
- **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
  entry points; they run the configure this project needs and build in parallel.
  Bare `./build.sh` = incremental; `--clean` wipes `build/`. (`--tests` /
  `BUILD_TESTING` is now inert — the Catch2 suites are gone; tests are the `pnpm
  test*` scripts.) For a single target after a configure,
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
`test:native` (real host + cores), `test:ui` (LVGL React), `screenshot`, the
`tools/run-sanitizer.sh` thread / address checks, and `validate`. The LSDj-sync /
DAW-timing / audio-quality matrix runs headlessly too — the real-Reaper
`reaper:lsdj-*` renders + `tools/reaper-timing-analyze.py`; see
[docs/lsdj.md](docs/lsdj.md).
