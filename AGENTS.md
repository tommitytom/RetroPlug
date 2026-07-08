# Agent guide

Start with these — all short:

- [README.md](README.md) — what RetroPlug2 is, how to build, project layout.
- [spec/](spec/README.md) — the **greenfield** architecture (one doc per concern).
  The active build lives in `packages/native-greenfield/` +
  `packages/retroplug-greenfield/`; the older **legacy** build is being removed
  ([spec/07-migration.md](spec/07-migration.md)).
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
  checking — they're managed deliberately. RetroPlug also keeps `deps/sameboy`,
  `deps/catch2`, `deps/efsw`.
- **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
  `build/` is load-bearing for the dev loop.
- **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
  entry points; they run the configure this project needs and build in parallel.
  Bare `./build.sh` = incremental; `--clean` wipes `build/`; `--tests` configures
  `-DBUILD_TESTING=ON` (Catch2). For a single target after a configure,
  `cmake --build build --target <t> -j$(nproc)` — always pass `-j`, the default is
  a single-threaded slog. `build.bat` enters vcvars64 + the tool PATH and runs the
  `cl` + vcpkg-`x64-windows-static` configure (overridable via `VCPKG_ROOT` /
  `RGBDS_DIR` / `NODE_DIR`).
- **Never commit derived artifacts** — the embedded bundle C arrays
  (`build/native-greenfield/*bundle_data.c`, legacy `build/ui/bundle*`) and any
  generated typed RPC client.
- **No versioned migrations (pre-release).** Nothing is released; when you change a
  serialized shape (project / DPF state, config, kit-patch, sav), just change it —
  no read-old/write-new shims, no `version: 2` transform. Renames / restructures are
  expected to break old saves. **But reads are forward-tolerant**
  (`rfl::DefaultIfMissing`): additive / removed fields don't break old files, so
  give a new field a sensible default and don't switch a reader back to strict
  `read<T>`. Each serialized root is version-**stamped** and refused if stamped
  newer than the running build — detection, not migration. The greenfield
  persistence model + the stamps live in
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
  in [SystemGrid.tsx](packages/retroplug-greenfield/ui/screens/grid/SystemGrid.tsx).
- **Re-key the parent on the visible set** to force a full remount, so every child
  mounts fresh in JSX order —
  [Menu.tsx](packages/retroplug-greenfield/ui/screens/menu/Menu.tsx).

If a tile / row / menu renders in a confusingly wrong slot, this is almost certainly
why. (Build-target gotchas — which target rebuilds the standalone vs the embedded
bundle — are in [spec/06-build-test.md](spec/06-build-test.md).)

## Verification loop

Verify your own work headlessly before claiming it's done — **a "tests pass" claim
must be backed by an actual exit-zero** from one of these.

The **greenfield** loop is the primary path, documented in
[spec/06-build-test.md](spec/06-build-test.md): `pnpm test:greenfield` (pure-TS
mock), `test:greenfield-native` (real host + cores), `test:greenfield-ui` (LVGL
React), `screenshot:greenfield`, the `tools/run-greenfield-sanitizer.sh` thread /
address checks, and `validate:greenfield`.

The **legacy** CLI + reaper harness (`pnpm test:cli` / `test:ui` / `validate` /
`reaper:*`, Catch2 `retroplug-tests` + `tools/run-sanitizers.sh`) still covers the
LSDj-sync + DAW-timing + audio-quality matrix that greenfield hasn't replaced yet —
see [docs/lsdj.md](docs/lsdj.md).
