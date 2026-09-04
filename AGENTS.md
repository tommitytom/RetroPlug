# Agent guide

Start with these — all short:

- [README.md](README.md) — what RetroPlug is, how to build, project layout.
- [spec/](spec/README.md) — the architecture (one doc per concern). The build lives
  in `packages/native/` (C++ host: `src/` core + `src/host/` layer + `plugin/`) +
  `packages/retroplug/` (TS/UI). The older **legacy** build has been removed;
  [spec/07-remaining-work.md](spec/07-remaining-work.md) tracks the residual cleanup + feature gaps.
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
  (the file-watcher — `NativeFileWatcher` behind `HostRpcService::drainChangedPaths`
  — now uses it: `add_subdirectory`'d `EXCLUDE_FROM_ALL` at the root, `efsw-static`
  linked into `retroplug-backend`; the watcher is opt-in via `enableWatching`, which
  only the plugin calls). `deps/catch2` is the C++ unit-test framework:
  it's `add_subdirectory`'d at the root (`EXCLUDE_FROM_ALL`) and linked by the
  `test:plugin` binaries (`retroplug-plugin-test` / `retroplug-classid-test` /
  `retroplug-audio-test` / `retroplug-watcher-test`) as `Catch2::Catch2WithMain`.
- **`deps/sameboy` is patched at configure — a dirty working tree there is
  EXPECTED, not stray changes.** The per-channel (4-stem) Game Boy audio tap lives
  in `Core/apu.{c,h}` and ships as a tracked patch
  ([cmake/patches/sameboy-per-channel-audio.patch](cmake/patches/sameboy-per-channel-audio.patch)),
  applied idempotently at configure by
  [cmake/sameboy.cmake](cmake/sameboy.cmake) via `git apply` (it uses
  `git apply --reverse --check` to no-op when already present). The parent repo
  tracks only the pinned submodule pointer, so after any configure `git status`
  shows `deps/sameboy` with modified `Core/apu.c` / `Core/apu.h` — **don't
  reset / stash / commit that, and don't bump the pointer to "absorb" it.** A
  submodule bump that invalidates the patch fails LOUDLY at configure (rather than
  silently dropping per-channel output); regenerate the patch if that happens.
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
  `RGBDS_DIR` / `NODE_DIR`). Both scripts pass any `-D<var>=<value>` straight
  through to the configure (and force one, so the entry lands on an existing tree).
- **Mesen LTO is opt-in (`-DRETROPLUG_MESEN_LTO=ON`), and that is deliberate.** It
  buys ~10% on the NES core (Cortex-A53: xRT 0.83 -> 0.92), but it turns every mesen
  object into LTO bitcode, so each of the *nine* binaries linking that static lib
  re-runs codegen over ~430 objects. Measured cost of a one-line `.cpp` edit: **1m05s
  wall / 26m51s CPU with it on, 2.4s / 6.6s with it off** — same single compile, the
  rest is eight full LTO links. So it's OFF by default and every `release.yml` job
  passes it ON; shipped builds keep the speedup, the dev loop doesn't pay for it.
  Toggling the option recompiles mesen (~45 s). If you see mesen scroll past on every
  build (the repeated `emu2413.cpp` warnings), that's LTO link-time codegen, not a
  stale-dependency bug — `[NN%] Built target mesen` prints whether or not it did work.
- **`retroplug-sdl` is in the default build** — `build.sh` builds it, and CI covers it
  (CI builds `all`) on all four platforms. It needs nothing extra installed: SDL2 is
  already REQUIRED at root scope for every plugin variant, and rtmidi is an in-tree
  submodule. `pnpm sdl:smoke` builds it by name then runs the headless smoke; that smoke
  is still NOT a CI step, so CI proves it compiles + links, not that it runs.
  `pnpm sdl:pipewire` is the audio-device half the smoke can't cover (it runs with no
  audio server at all): it stands up a PRIVATE PipeWire server + three null sinks in its own
  `XDG_RUNTIME_DIR` and asserts WHICH output device the PortAudio PipeWire backend opens —
  the session default sink (`default.audio.sink`) wins, an explicit Settings pick beats it,
  a stale pick warns and falls back, and a libpipewire-less run degrades to ALSA. That server
  runs with NO session manager: the sinks AND the `default` metadata object are declared as
  `context.objects` in a daemon-only config, because wireplumber needs logind/dbus and exits
  without them here — which silently collapsed every expectation onto the priority fallback
  when the check depended on it. The third sink (`rp-decoy`) always outranks the others on
  `priority.session` and is never the default, so "followed the default" can't pass by
  accident. It also covers WIDE output (`Out Channels` 4/6/8): 8 channels against a stereo
  sink must open, be 8 PORTS wide, and still have pair 1 linked — the port count is the
  assertion that matters, since a stream with no declared layout silently comes out 2 wide
  while the log still says 8. That leg runs LAST and is the one part needing a session
  manager (ports only materialise once something links the stream); it starts wireplumber
  with its bluetooth context dropped, because that context's logind module has no
  `/run/systemd` in a container and takes the daemon down with it. Not in CI (the runners
  have no PipeWire); the devcontainer ships one for it.
  **It ships in the `build.yml` artifact for every platform but is deliberately kept OUT
  of releases** — `release.yml`'s four packaging steps are explicit allowlists, and
  `retroplug-sdl` is not on any of them (each carries a comment saying so). Don't add it
  until the standalone is release-ready. On macOS the artifact binary is build-machine-only:
  the `SDL2.framework` embed covers the plugin/app BUNDLES, and `retroplug-sdl` is a bare
  executable, so it would need a framework copy + an `@executable_path` rpath to run
  elsewhere.
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
[docs/lsdj.md](docs/lsdj.md). `reaper:risa-sync` is the NES/risa twin of the LSDj drift
render (same `--drift` analyzer, whose labels are therefore tracker-neutral): risa host sync
is driven by the DAW **transport** alone, so that project carries no MIDI item at all.
To run the **whole** Reaper leg (all 7 renders + 3
editor checks) at once, `pnpm reaper:all` fans them out concurrently
([tools/run-reaper-suite.sh](tools/run-reaper-suite.sh)); each job is isolated by
`RP_JOB_TAG` via the sourced [tools/reaper-env.sh](tools/reaper-env.sh) (uniquely named
JACK server + per-tag config dir / display / logs), which is also what makes the
individual `reaper:*` scripts safe to run in parallel.

**`retroplug-cli` as a consumer's whole test harness** (`pnpm test:cli-ts`):
`retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `run <session.ts>` does one
file - so a consumer repo (BlipToaster) needs **no Node, npm, esbuild or node_modules**. Its kit is the
binary + `sdk/retroplug-cli.js` + `sdk/retroplug-cli.d.ts` + tests. Two pre-existing facts make it work:
the txiki runtime **already resolves `import` off disk at runtime** (so tests never needed bundling, only
stripping), and consumer tests already use explicit `.js` specifiers, so emitting `X.ts` -> `X.js` at the
same directory DEPTH needs no specifier rewriting. That depth is why stripped output lands in the source
dir's SIBLING `.rp-test-build/` ([cli/tsStrip.ts](packages/retroplug/cli/tsStrip.ts) `buildDirFor`) - put
it anywhere else and every `../sdk/...` import breaks. Each test file runs in its **own process**
(`tjs.spawn`), which is required, not tidy: the TAP harness calls `tjs.exit` when a file ends, its case
list is module-level, and the native `Engine` is per-process. The stripper is ts-blank-space + the TS
parser, compiled in as global-code bytecode (+4 MB) and loaded **on demand** via `__rp_loadTsStripper`, so
no other command pays for it. **Types are stripped, not compiled**: only erasable syntax works - `enum`,
`namespace` and constructor parameter properties are refused with `file:line:col`. Wiring
ts-blank-space's optional `onError` is what enforces that; omit it and an `enum` passes through as invalid
JavaScript. (`@swc/wasm-typescript` was tried first and rejected: under the WAMR interpreter it core-dumped
on 3 of 7 real test files and silently emitted EMPTY output on 2 more, which makes a test file "pass".)

**UI/background rendering** (the `System > Render` menu; [spec/11-ui-rendering.md](spec/11-ui-rendering.md)):
the CLI `render` command and the UI render share one library (`packages/retroplug/src/render/`), run offline
by a bare-QuickJS `RenderHost` (own `Engine`) on a per-job thread (`RenderJobRegistry`). Verify with
`retroplug-render-host-test` (built by name — `cmake --build build --target retroplug-render-host-test`): a
`<job-json>` render is byte-identical to `retroplug-cli render`; `--registry <j>...` runs jobs concurrently;
`--cancel <j>` aborts mid-render — build it in `build-tsan/` for the ThreadSanitizer pass. The UI-seam logic
is `pnpm test render`; the tile badge is `pnpm test:ui render-badge`.
