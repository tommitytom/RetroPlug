# 06 — Build & verification

How greenfield builds today, and — the practical half of this doc — **which command
proves which kind of change**. Greenfield ships alongside the legacy build out of one
CMake configure; the two coexist as distinct targets with distinct plugin identities.
For the runtime concepts referenced here (the three hosts, control plane vs audio thread,
command ring, snapshot registry, release ring), see [01-architecture.md](01-architecture.md).

## One configure builds both trees

There is no separate greenfield project. The root [CMakeLists.txt](../CMakeLists.txt) declares
the whole tree as `NAME retroplug` ([CMakeLists.txt:29](../CMakeLists.txt#L29)), and greenfield
is pulled in by a single `add_subdirectory(packages/native-greenfield ...)` at
[CMakeLists.txt:574](../CMakeLists.txt#L574), nested inside the `if(BUILD_CLI)` block
([CMakeLists.txt:554](../CMakeLists.txt#L554) / [:567](../CMakeLists.txt#L567); `BUILD_CLI` defaults
`ON`). So every full build produces **both** plugins.

- `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
  **zero greenfield-specific logic** — a bare `./build.sh` builds every `ALL` target of both
  trees. See [AGENTS.md](../AGENTS.md) for their flags.
- `pnpm configure` runs `cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_CLI=ON`
  ([package.json:11](../package.json#L11)); `pnpm build` invokes
  [scripts/cmake-build.js](../scripts/cmake-build.js) (no target = full build of both).
- The per-package `.native-build/`, `.ui-build/`, `.test-build/` dirs under
  `packages/retroplug-greenfield/` are **esbuild output dirs** created by the run scripts, not
  CMake build dirs. The only CMake build dirs are `build/`, `build-tsan/`, `build-asan/`.

## Greenfield CMake targets

All defined in [packages/native-greenfield/CMakeLists.txt](../packages/native-greenfield/CMakeLists.txt).

| Target | Kind | Produces | In `ALL`? |
|---|---|---|---|
| `retroplug-greenfield-backend` | static lib | The Engine/Backend/DspRuntime + shared txiki host, PUBLIC-linking `retroplug-cli-core` + `tjs`. Composed by every host. | yes (via consumers) |
| `native-greenfield-host` | executable | `build/bin/native-greenfield-host` — the real-Backend test host that evals a TS bundle over `__rpcSend`. | no |
| `retroplug-greenfield-cp-bundle` | custom | `build/native-greenfield/cp-bundle_data.c` — control-plane bytecode (`gfcp_` prefix), embedded in the DSP. | yes |
| `retroplug-greenfield-ui-bundle` | custom | `build/native-greenfield/ui-bundle_data.c` — React UI bytecode (`gfui_` prefix). | no (pulled in by dependents) |
| `retroplug-greenfield` | umbrella plugin lib | Aggregator over the DPF variants (`dpf_add_plugin`, `TARGETS clap vst3 jack`). | yes |
| `retroplug-greenfield-clap` | DPF variant | `build/bin/retroplug-greenfield.clap` | yes |
| `retroplug-greenfield-vst3` | DPF variant | `build/bin/retroplug-greenfield.vst3` | yes |
| `retroplug-greenfield-jack` | DPF variant | `build/bin/retroplug-greenfield` — the **standalone binary** | yes |
| `retroplug-greenfield-ui` / `-dsp` | DPF aggregators | UI-side / DSP-side static aggregators (internal). | yes |
| `retroplug-greenfield-ui-test` | executable | `build/bin/retroplug-greenfield-ui-test` — boots the real React UI on a headless software LVGL display via `GreenfieldUiHarness`. | no |

The plugin's identity is deliberately distinct from legacy so the two coexist in one DAW:
`DISTRHO_PLUGIN_NAME "RetroPlug Greenfield"`, URI `urn:distrho:retroplug-greenfield`, CLAP id
`studio.kx.distrho.retroplug-greenfield`
([DistrhoPluginInfo.h:8](../packages/native-greenfield/plugin/DistrhoPluginInfo.h#L8)). It exposes
8 outputs — four stereo pairs `out_1..4`
([DistrhoPluginInfo.h:13](../packages/native-greenfield/plugin/DistrhoPluginInfo.h#L13)).

### Not yet built / deferred

Greenfield declares only `clap`, `vst3`, and `jack`
([CMakeLists.txt:106](../packages/native-greenfield/CMakeLists.txt#L106)). There is **no** lv2,
vst2, or au variant (legacy still carries those). Dropping the `-greenfield` suffix and reverting
to the legacy plugin identity is the switchover's job — see [07-migration.md](07-migration.md).

## The three hosts → three test tiers

The three C++ hosts (see [01-architecture.md](01-architecture.md)) each back a headless test tier.
The TS runners live in `packages/retroplug-greenfield/scripts/`; every runner discovers
`*.test.ts` files, bundles each with esbuild (es2020, one process per file), and reports TAP with
a nonzero exit on failure. Slugs accept slash or dash form and a directory prefix runs everything
under it (e.g. `pnpm test:greenfield paths` or `paths-rebase`).

| Tier | Backend under test | Command | Runner | Test dir |
|---|---|---|---|---|
| **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug-greenfield/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test:greenfield` | [run-tests.mjs](../packages/retroplug-greenfield/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
| **real host** | the real `BackendFacade` (fs/config/codec + a live `SameBoySystem` in a real `Project`, real DSP kernel) | `pnpm test:greenfield-native` | [run-native-tests.mjs](../packages/retroplug-greenfield/scripts/run-native-tests.mjs) on `native-greenfield-host` | `test-native/` |
| **LVGL React UI** | the real UI bundle on a software LVGL display driven by `GreenfieldUiHarness` | `pnpm test:greenfield-ui` | [run-ui-tests.mjs](../packages/retroplug-greenfield/scripts/run-ui-tests.mjs) on `retroplug-greenfield-ui-test` | `test-ui/` |

Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
`native-greenfield-host`; UI → `retroplug-greenfield-ui-test`), so a `pnpm test:greenfield*`
invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.

The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
(isolated real disk). The native runner also injects the compiled DSP role kernel source
(`__DSP_KERNEL_BUNDLE__`) and the resources dirs so tests can load the real per-block program and
locate ROMs by absolute path. The UI runner aliases `import ... from "ui-harness"` to greenfield's
own [test-ui/uiHarness.ts](../packages/retroplug-greenfield/test-ui/uiHarness.ts) — it does **not**
depend on the legacy `test/harness/ui.ts`.

## pnpm scripts (greenfield)

Root [package.json](../package.json). Each builds its CMake target(s) first, then runs.

| Script | Builds | Does |
|---|---|---|
| `test:greenfield` | `tjs-cli` | Mock-backend TS suite ([:16](../package.json#L16)). |
| `test:greenfield-native` | `native-greenfield-host` | Real-host suite ([:17](../package.json#L17)). |
| `test:greenfield-ui` | `retroplug-greenfield-ui-test` | LVGL React UI suite ([:18](../package.json#L18)). |
| `screenshot:greenfield` | `retroplug-greenfield-jack` | Boots the standalone headlessly → `/tmp/retroplug-greenfield.png` ([:21](../package.json#L21)). |
| `validate:greenfield` | `-clap` + `-vst3` | `clap-validator` + `pluginval`, with `RETROPLUG_CLAP_NAME`/`RETROPLUG_VST3_NAME=retroplug-greenfield` so the shared [validate-plugins.sh](../tools/validate-plugins.sh) targets the greenfield binaries ([:23](../package.json#L23)). |
| `reaper:mgb-smoke-greenfield-author` | `-vst3` | Authors + bakes the `.rplg` fixture for the Reaper render ([:24](../package.json#L24)). |
| `reaper:mgb-smoke-greenfield` | `-vst3` | Renders [mgb_smoke_greenfield.rpp](../examples/reaper/mgb_smoke_greenfield.rpp) through real Reaper — end-to-end DAW proof ([:25](../package.json#L25)). |

The aggregate `pnpm test` ([package.json:13](../package.json#L13)) runs the **legacy** `test:cli` +
`test:ui` only; greenfield tiers are invoked explicitly by their own scripts.

## The dpf.js seam

Greenfield consumes dpf.js through the **same** seam as legacy — nothing greenfield-specific.
The generic framework (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) lives in the
nested `deps/dpf.js` submodule.

1. **Submodule** — `.gitmodules` registers `deps/dpf.js` (clone with `--recursive`).
2. **pnpm link** — `"dpf.js": "link:./deps/dpf.js"` ([package.json:43](../package.json#L43)); `pnpm install` wires it before the first configure. lv_binding_js has its own pnpm workspace (react/react-reconciler/lvgljs-ui) that needs its own `pnpm install` — see [AGENTS.md](../AGENTS.md).
3. **CMake resolve + add_subdirectory** — the root runs `node -e "require.resolve('dpf.js/package.json')"` → `DPFJS_PATH`, then `add_subdirectory("${DPFJS_PATH}" ...)` defines `dpf_add_plugin`, `dpfjs::core`, `lvgl-js-native`, `tjs`, `tjsc`, `tjs-cli`, `rpcpp`, `miniz`.
4. **Greenfield's use of `${DPFJS_PATH}`** — the backend compiles the shared txiki host `TjsHostRuntime.cpp` and includes the txiki/QuickJS headers ([CMakeLists.txt:26](../packages/native-greenfield/CMakeLists.txt#L26), [:33–35](../packages/native-greenfield/CMakeLists.txt#L33)); the plugin embeds the generic `dpf-widgets/generic/LVGL.cpp` and links `dpfjs::core` + `lvgl-js-native` + `tjs` ([:114](../packages/native-greenfield/CMakeLists.txt#L114), [:121–124](../packages/native-greenfield/CMakeLists.txt#L121)).
5. **React resolution** — the greenfield UI ([ui/main.tsx](../packages/retroplug-greenfield/ui/main.tsx)) is bundled by the shared [tools/build-ui.js](../tools/build-ui.js), which resolves `react`/`react-reconciler`/`lvgljs-ui` from dpf.js's own pnpm workspace under `deps/dpf.js/deps/lv_binding_js/node_modules`.

The shared C++ that survives the legacy deletion — `Project`, `SystemBase`, the SameBoy/Mesen
systems, the LSDj sav codec, the transport primitives — is compiled once into the
`retroplug-cli-core` static lib and PUBLIC-linked by
[the backend](../packages/native-greenfield/CMakeLists.txt#L41). See [01-architecture.md](01-architecture.md)
for the "shared core" boundary and [07-migration.md](07-migration.md) for the re-homing risk.

### Known drift

[tsconfig.base.json](../tsconfig.base.json) still maps `react`/`lvgljs-ui`/`@rpcpp/*` at a `../dpf.js`
**sibling** path, but the live location is the `deps/dpf.js` submodule. Greenfield's own
[tsconfig.json](../packages/retroplug-greenfield/tsconfig.json) does not extend base (`"types": []`) and
relies on esbuild resolution, so this stale mapping does not affect the greenfield build — it is a
cleanup item tracked in [07-migration.md](07-migration.md).

## The headless verification loop — which command proves which change

Pick the cheapest tier that actually exercises the code you touched. Trust-but-verify: a claim that
"tests pass" should be backed by an exit-zero from one of these.

| You changed… | Prove it with | Why this tier |
|---|---|---|
| **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test:greenfield [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug-greenfield/src` that doesn't need a live core. |
| **DSP behaviour / cores / the command-ring & snapshot seam** — role kernel, byte-sinks, MIDI/serial, transport, actual emulator output | `pnpm test:greenfield-native [slug]` | Runs the **real** `BackendFacade` with a live `SameBoySystem`, real `Project`, and the compiled DSP kernel. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
| **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:greenfield-ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
| **Anything you want to eyeball** in the standalone | `pnpm screenshot:greenfield` → Read `/tmp/retroplug-greenfield.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug Greenfield"`. |
| **The audio-thread / control-thread seam** — `QueuedInvoker`, the command ring, `SnapshotRegistry` publish/read, the release-ring ownership handoff | `tools/run-greenfield-sanitizer.sh thread` and `… address` | Builds `native-greenfield-host` instrumented into `build-tsan/` / `build-asan/` (the load-bearing `build/` is untouched) and reruns the audio-thread native tests under TSan / ASan. `thread` proves the seam is race-free; `address` proves the cross-thread `new`/`delete` handoff (add→adopt, remove→release→`drainReleased` delete) has no use-after-free or leak. Both are expected to need **no** new suppressions. |
| **DPF wrapper / plugin-format / state-restore behaviour** | `pnpm validate:greenfield` | `clap-validator` + `pluginval` against the greenfield `.clap`/`.vst3`. Catches ABI / state-restore / threading regressions in the format adapters. |
| **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke-greenfield` | Instantiates `retroplug-greenfield.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |

### Sanitizer specifics

[run-greenfield-sanitizer.sh](../tools/run-greenfield-sanitizer.sh) is **not** a pnpm script — run it
directly: `tools/run-greenfield-sanitizer.sh <thread|address> [slug]`. It configures a separate
`build-tsan/` / `build-asan/` with `-DRETROPLUG_SANITIZE=…`, builds the instrumented host, then
drives it via `run-native-tests.mjs` with `RETROPLUG_GREENFIELD_HOST` pointed at the instrumented
binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2 seqlock
suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)); a clean run **is** the
proof that the QuickJS DSP context + cores are touched only by the audio thread. Any finding aborts
the host → nonzero exit. This is the greenfield counterpart to the legacy
`tools/run-sanitizers.sh` (which only builds/runs the Catch2 targets).

## Build gotchas

- **The embedded bundles are derived — never commit them.** `cp-bundle.js`/`cp-bundle_data.c` and
  `ui-bundle.js`/`ui-bundle_data.c` are CMake `BYPRODUCTS` under `build/native-greenfield/`
  ([CMakeLists.txt:70–100](../packages/native-greenfield/CMakeLists.txt#L70)), regenerated from
  [tools/build-greenfield-controlplane.js](../tools/build-greenfield-controlplane.js) and
  [tools/build-ui.js](../tools/build-ui.js) on each build. Same rule as the legacy `bundle.js`.
- **`retroplug-greenfield-ui-test` rebuilds the UI bundle; `run-ui-tests.mjs` alone does not.**
  The UI app bytecode (`gfui_ui_bundle`) is embedded **in the test binary**, and the target
  `add_dependencies` on `retroplug-greenfield-ui-bundle`
  ([CMakeLists.txt:159](../packages/native-greenfield/CMakeLists.txt#L159)) rebuild it. `pnpm
  test:greenfield-ui` therefore always tests a fresh bundle. But invoking
  `run-ui-tests.mjs` directly (skipping the `cmake-build.js` step) reuses whatever binary already
  exists — a **stale** UI bundle if you edited `ui/` since. The runner only re-bundles the *test
  file*, not the app. Rebuild the target, or use the pnpm script.
- **`--target retroplug-greenfield` does not rebuild the standalone.** The umbrella
  `retroplug-greenfield` builds the plugin libs + regenerates the cp/ui bundles, but the standalone
  binary `build/bin/retroplug-greenfield` is produced by `retroplug-greenfield-jack`. To verify a UI
  change in the standalone, build with a bare `cmake --build build -j$(nproc)` (no `--target`) or
  `--target retroplug-greenfield-jack` — otherwise the screenshot shows old behaviour against a fresh
  bundle. (Mirrors the legacy `retroplug` vs `retroplug-jack` gotcha in [AGENTS.md](../AGENTS.md).)
- **Don't `rm -rf build` to "fix" CMake.** The configured `build/` is load-bearing for the dev loop;
  the sanitizer dirs (`build-tsan/`/`build-asan/`) are the only ones that get reconfigured freely.

## Key files

- [CMakeLists.txt:554–574](../CMakeLists.txt#L554) — the `BUILD_CLI` block that pulls greenfield into the one configure.
- [packages/native-greenfield/CMakeLists.txt](../packages/native-greenfield/CMakeLists.txt) — every greenfield target (backend, host, bundles, plugin, ui-test).
- [package.json:16–25](../package.json#L16) — the greenfield pnpm scripts.
- [scripts/run-tests.mjs](../packages/retroplug-greenfield/scripts/run-tests.mjs) / [run-native-tests.mjs](../packages/retroplug-greenfield/scripts/run-native-tests.mjs) / [run-ui-tests.mjs](../packages/retroplug-greenfield/scripts/run-ui-tests.mjs) — the three test-tier runners.
- [testing/mockBackend.ts](../packages/retroplug-greenfield/testing/mockBackend.ts) — the in-memory Backend double for the mock tier.
- [tools/run-greenfield-sanitizer.sh](../tools/run-greenfield-sanitizer.sh) — TSan/ASan over the native host.
- [tools/run-standalone-greenfield.sh](../tools/run-standalone-greenfield.sh) — headless standalone + screenshot.
