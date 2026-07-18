# 06 — Build & verification

How the build works today, and — the practical half of this doc — **which command
proves which kind of change**. RetroPlug is a single build out of one CMake configure.
For the runtime concepts referenced here (the three hosts, control plane vs audio thread,
command ring, snapshot registry, release ring), see [01-architecture.md](01-architecture.md).

## One configure

The root [CMakeLists.txt](../CMakeLists.txt) declares the project as
`NAME retroplug` ([CMakeLists.txt:29](../CMakeLists.txt#L29)) and pulls in the native build with a
single `add_subdirectory(packages/native native)` ([CMakeLists.txt:271](../CMakeLists.txt#L271)),
after the `deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, r8brain, enkiTS, catch2).
A full build produces the plugin (all formats) + the standalone.

- `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
  **zero build-specific special-casing** — a bare `./build.sh` builds every `ALL` target. See
  [AGENTS.md](../AGENTS.md) for their flags.
- `pnpm configure` runs `cmake -S . -B build`
  ([package.json:11](../package.json#L11)); `pnpm build` invokes
  [scripts/cmake-build.js](../scripts/cmake-build.js) (no target = a full build).
- The per-package `.native-build/`, `.ui-build/`, `.test-build/` dirs under
  `packages/retroplug/` are **esbuild output dirs** created by the run scripts, not
  CMake build dirs. The only CMake build dirs are `build/`, `build-tsan/`, `build-asan/`.

## CMake targets

All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).

| Target | Kind | Produces | In `ALL`? |
|---|---|---|---|
| `retroplug-core` | static lib | The shared emulator / Project / kit-codec core. | yes (via consumers) |
| `retroplug-backend` | static lib | `retroplug-core` + the Engine, the RPC services, the DSP runtime, and the shared txiki host. Composed by every host. | yes (via consumers) |
| `retroplug-host` | executable | `build/bin/retroplug-host` — the real-Backend test host that evals a TS bundle over `__rpcSend`. | no |
| `retroplug-cli` | executable | `build/bin/retroplug-cli` — the CLI runner + `render` subcommand ([09-cli-debugging.md](09-cli-debugging.md)). | yes |
| `retroplug-cp-bundle` / `-ui-bundle` | custom | `build/native/*bundle_data.c` — control-plane / React-UI bytecode, embedded in the plugin. | cp: yes; ui: via dependents |
| `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
| `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary** | yes |
| `retroplug-ui-test` | executable | `build/bin/retroplug-ui-test` — boots the real React UI on a headless software LVGL display via `UiHarness`. | no |
| `retroplug-plugin-test` / `-classid-test` / `-audio-test` | executable | Catch2 C++ unit checks (window-hook routing / class-id sync / per-channel audio). `EXCLUDE_FROM_ALL`. | no |

The plugin's identity is vendor-owned (not the DPF example namespace):
`DISTRHO_PLUGIN_NAME "RetroPlug"`, URI `https://retroplug.io`, CLAP id
`net.tommitytom.retroplug`
([DistrhoPluginInfo.h:8](../packages/native/plugin/DistrhoPluginInfo.h#L8)). It exposes
8 outputs — four stereo pairs `out_1..4`
([DistrhoPluginInfo.h:13](../packages/native/plugin/DistrhoPluginInfo.h#L13)).

### Not yet built / deferred

The build declares `clap`, `vst3`, `vst2`, `au`, and `jack`
([CMakeLists.txt:232](../packages/native/CMakeLists.txt#L232)); AU is gated by DPF to macOS. Only
**LV2** is not built — its out-of-process DSP/UI split doesn't fit RetroPlug
([07-remaining-work.md](07-remaining-work.md)).

## The three hosts → three test tiers

The three C++ hosts (see [01-architecture.md](01-architecture.md)) each back a headless test tier.
The TS runners live in `packages/retroplug/scripts/`; every runner discovers
`*.test.ts` files, bundles each with esbuild (es2020, one process per file), and reports TAP with
a nonzero exit on failure. Slugs accept slash or dash form and a directory prefix runs everything
under it (e.g. `pnpm test paths` or `paths-rebase`).

Runners execute their per-file (and, for `test:plugin`, per-binary) work in a **bounded parallel
pool** — each unit is an isolated child process (its own `mkdtemp` config dir; UI runs an in-process
software display), so concurrency is safe and needs no coordination. Concurrency defaults to **half
the logical threads**; override with `--jobs N` / `-j N` on the runner or the `TEST_JOBS` env, and
`TEST_JOBS=1` restores serial one-at-a-time output. Output is buffered per child and flushed as a
labelled `# <slug>` block on completion (rather than live-interleaved). The shared pool/spawn helper
is [scripts/lib/testPool.mjs](../packages/retroplug/scripts/lib/testPool.mjs). The `reaper:*` tiers
stay serial — they share a single unnamed jackd server and a per-family Reaper config dir.

| Tier | Backend under test | Command | Runner | Test dir |
|---|---|---|---|---|
| **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
| **real host** | the real Backend RPC surface (fs/config/codec + a live `SameBoySystem` in a real `Project`, real DSP kernel) | `pnpm test:native` | [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) on `retroplug-host` | `test-native/` |
| **LVGL React UI** | the real UI bundle on a software LVGL display driven by `UiHarness` | `pnpm test:ui` | [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) on `retroplug-ui-test` | `test-ui/` |

Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
`retroplug-host`; UI → `retroplug-ui-test`), so a `pnpm test*`
invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.

The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
(isolated real disk). The native runner also injects the compiled DSP role kernel source
(`__DSP_KERNEL_BUNDLE__`) and the resources dirs so tests can load the real per-block program and
locate ROMs by absolute path. The UI runner aliases `import ... from "ui-harness"` to the build's
own [test-ui/uiHarness.ts](../packages/retroplug/test-ui/uiHarness.ts).

## pnpm scripts

Root [package.json](../package.json). Each builds its CMake target(s) first, then runs.

| Script | Builds | Does |
|---|---|---|
| `test` | `tjs-cli` | Mock-backend TS suite ([:16](../package.json#L16)). |
| `test:native` | `retroplug-host` | Real-host suite ([:17](../package.json#L17)). |
| `test:ui` | `retroplug-ui-test` | LVGL React UI suite ([:18](../package.json#L18)). |
| `test:plugin` | `retroplug-plugin-test` + `-classid-test` + `-audio-test` | Pure-C++ Catch2 unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp) proven on two live `JSContext`s so concurrent instances never cross-route), the class-id counter sync that keeps the DAW-hosted editor from rendering blank, and the per-channel audio split ([:19](../package.json#L19)). |
| `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png` ([:20](../package.json#L20)). |
| `validate` | `-clap` + `-vst3` | `clap-validator` + `pluginval` against the built binaries via the shared [validate-plugins.sh](../tools/validate-plugins.sh) ([:22](../package.json#L22)). |
| `reaper:mgb-smoke-author` | `-vst3` | Authors + bakes the `.rplg` fixture for the Reaper render. |
| `reaper:mgb-smoke` | `-vst3` | Renders [mgb_smoke.rpp](../examples/reaper/mgb_smoke.rpp) through real Reaper — end-to-end DAW proof. |

`pnpm test` runs the mock-backend TS suite; the native, UI, plugin, and Reaper tiers are invoked
explicitly by their own scripts.

## The dpf.js seam

The generic framework (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) lives in the
nested `deps/dpf.js` submodule, consumed through this seam:

1. **Submodule** — `.gitmodules` registers `deps/dpf.js` (clone with `--recursive`).
2. **pnpm link** — `"dpf.js": "link:./deps/dpf.js"` ([package.json:43](../package.json#L43)); `pnpm install` wires it before the first configure. lv_binding_js has its own pnpm workspace (react/react-reconciler/lvgljs-ui) that needs its own `pnpm install` — see [AGENTS.md](../AGENTS.md).
3. **CMake resolve + add_subdirectory** — the root runs `node -e "require.resolve('dpf.js/package.json')"` → `DPFJS_PATH`, then `add_subdirectory("${DPFJS_PATH}" ...)` defines `dpf_add_plugin`, `dpfjs::core`, `lvgl-js-native`, `tjs`, `tjsc`, `tjs-cli`, `rpcpp`, `miniz`.
4. **Use of `${DPFJS_PATH}`** — the backend compiles the shared txiki host `TjsHostRuntime.cpp` and includes the txiki/QuickJS headers ([CMakeLists.txt:26](../packages/native/CMakeLists.txt#L26), [:33–35](../packages/native/CMakeLists.txt#L33)); the plugin embeds the generic `dpf-widgets/generic/LVGL.cpp` and links `dpfjs::core` + `lvgl-js-native` + `tjs` ([:114](../packages/native/CMakeLists.txt#L114), [:121–124](../packages/native/CMakeLists.txt#L121)).
5. **React resolution** — the UI ([ui/main.tsx](../packages/retroplug/ui/main.tsx)) is bundled by the shared [tools/build-ui.js](../tools/build-ui.js), which resolves `react`/`react-reconciler`/`lvgljs-ui` from dpf.js's own pnpm workspace under `deps/dpf.js/deps/lv_binding_js/node_modules`.

The shared C++ core — `Project`, `SystemBase`, the SameBoy/Mesen systems, the LSDj kit codec, the
transport primitives — is compiled once into the `retroplug-core` static lib and PUBLIC-linked by
[the backend](../packages/native/CMakeLists.txt#L77). See [01-architecture.md](01-architecture.md)
for the "shared core" boundary.

## The headless verification loop — which command proves which change

Pick the cheapest tier that actually exercises the code you touched. Trust-but-verify: a claim that
"tests pass" should be backed by an exit-zero from one of these.

| You changed… | Prove it with | Why this tier |
|---|---|---|
| **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug/src` that doesn't need a live core. |
| **DSP behaviour / cores / the command-ring & snapshot seam** — role kernel, byte-sinks, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem`, real `Project`, and the compiled DSP kernel. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
| **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
| **Anything you want to eyeball** in the standalone | `pnpm screenshot` → Read `/tmp/retroplug.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug"`. |
| **The audio-thread / control-thread seam** — `QueuedInvoker`, the command ring, `SnapshotRegistry` publish/read, the release-ring ownership handoff | `tools/run-sanitizer.sh thread` and `… address` | Builds `retroplug-host` instrumented into `build-tsan/` / `build-asan/` (the load-bearing `build/` is untouched) and reruns the audio-thread native tests under TSan / ASan. `thread` proves the seam is race-free; `address` proves the cross-thread `new`/`delete` handoff (add→adopt, remove→release→`drainReleased` delete) has no use-after-free or leak. Both are expected to need **no** new suppressions. |
| **DPF wrapper / plugin-format / state-restore behaviour** | `pnpm validate` | `clap-validator` + `pluginval` against the `.clap`/`.vst3`. Catches ABI / state-restore / threading regressions in the format adapters. |
| **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke` | Instantiates `retroplug.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |

### Sanitizer specifics

[run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
directly: `tools/run-sanitizer.sh <thread|address> [slug]`. It configures a separate
`build-tsan/` / `build-asan/` with `-DRETROPLUG_SANITIZE=…`, builds the instrumented host, then
drives it via `run-native-tests.mjs` with `RETROPLUG_HOST` pointed at the instrumented
binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2 seqlock
suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)); a clean run **is** the
proof that the QuickJS DSP context + cores are touched only by the audio thread. Any finding aborts
the host → nonzero exit.

## Build gotchas

- **The embedded bundles are derived — never commit them.** `cp-bundle.js`/`cp-bundle_data.c` and
  `ui-bundle.js`/`ui-bundle_data.c` are CMake `BYPRODUCTS` under `build/native/`
  ([CMakeLists.txt:70–100](../packages/native/CMakeLists.txt#L70)), regenerated from
  [tools/build-controlplane.js](../tools/build-controlplane.js) and
  [tools/build-ui.js](../tools/build-ui.js) on each build.
- **`retroplug-ui-test` rebuilds the UI bundle; `run-ui-tests.mjs` alone does not.**
  The UI app bytecode (`rp_ui_bundle`) is embedded **in the test binary**, and the target
  `add_dependencies` on `retroplug-ui-bundle`
  ([CMakeLists.txt:159](../packages/native/CMakeLists.txt#L159)) rebuild it. `pnpm
  test:ui` therefore always tests a fresh bundle. But invoking
  `run-ui-tests.mjs` directly (skipping the `cmake-build.js` step) reuses whatever binary already
  exists — a **stale** UI bundle if you edited `ui/` since. The runner only re-bundles the *test
  file*, not the app. Rebuild the target, or use the pnpm script.
- **`--target retroplug` does not rebuild the standalone.** The umbrella
  `retroplug` builds the plugin libs + regenerates the cp/ui bundles, but the standalone
  binary `build/bin/retroplug` is produced by `retroplug-jack`. To verify a UI
  change in the standalone, build with a bare `cmake --build build -j$(nproc)` (no `--target`) or
  `--target retroplug-jack` — otherwise the screenshot shows old behaviour against a fresh
  bundle. (This standalone-vs-umbrella gotcha is also noted in [AGENTS.md](../AGENTS.md).)
- **Don't `rm -rf build` to "fix" CMake.** The configured `build/` is load-bearing for the dev loop;
  the sanitizer dirs (`build-tsan/`/`build-asan/`) are the only ones that get reconfigured freely.

## Key files

- [CMakeLists.txt:554–574](../CMakeLists.txt#L554) — the `BUILD_CLI` block that pulls the native build into the one configure.
- [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt) — every native target (backend, host, bundles, plugin, ui-test).
- [package.json:16–25](../package.json#L16) — the pnpm scripts.
- [scripts/run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) / [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) / [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) — the three test-tier runners.
- [testing/mockBackend.ts](../packages/retroplug/testing/mockBackend.ts) — the in-memory Backend double for the mock tier.
- [tools/run-sanitizer.sh](../tools/run-sanitizer.sh) — TSan/ASan over the native host.
- [tools/run-standalone.sh](../tools/run-standalone.sh) — headless standalone + screenshot.
