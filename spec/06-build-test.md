# 06 — Build & verification

How the build works today, and — the practical half of this doc — **which command
proves which kind of change**. RetroPlug is a single build out of one CMake configure.
For the runtime concepts referenced here (the three hosts, control plane vs audio thread,
command ring, snapshot registry, release ring), see [01-architecture.md](01-architecture.md).

## One configure

The root [CMakeLists.txt](../CMakeLists.txt) declares the project as
`NAME retroplug` and pulls in the native build with a single
`add_subdirectory(packages/native native)` ([CMakeLists.txt](../CMakeLists.txt)), after the
`deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, r8brain, enkiTS, efsw, catch2).
A full build produces the plugin (all formats) + the standalone.

- `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
  **zero build-specific special-casing** — a bare `./build.sh` builds every `ALL` target. See
  [AGENTS.md](../AGENTS.md) for their flags.
- `pnpm configure` runs `cmake -S . -B build`; `pnpm build` invokes
  [scripts/cmake-build.js](../scripts/cmake-build.js) (no target = a full build).
- The per-package `.native-build/`, `.ui-build/`, `.test-build/` dirs under
  `packages/retroplug/` are **esbuild output dirs** created by the run scripts, not
  CMake build dirs. The CMake build dirs are `build/` (the load-bearing one), `build-tsan/` and
  `build-asan/` ([run-sanitizer.sh](../tools/run-sanitizer.sh)), `build-prof/`
  ([run-profile.sh](../tools/run-profile.sh)), and `build-arm/` + `build-arm-prof/`
  ([arm-build.sh](../tools/arm-build.sh)). All six are gitignored.

## CMake targets

All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).

| Target | Kind | Produces | In `ALL`? |
|---|---|---|---|
| `retroplug-core` | static lib | The shared emulator / kit-codec core. | yes (via consumers) |
| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
| `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
| `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
| `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)). Shipped in the CI artifact, deliberately **not** in releases. | yes |
| `retroplug-cli` | executable | `build/bin/retroplug-cli` — the CLI runner + `render` and `test` subcommands ([09-cli-debugging.md](09-cli-debugging.md)). | yes |
| `retroplug-host` | executable | `build/bin/retroplug-host` — the real-Backend test host that evals a TS bundle over `__rpcSend`. | yes |
| `retroplug-ui-test` | executable | `build/bin/retroplug-ui-test` — boots the real React UI on a headless software LVGL display via `UiHarness`. | yes |
| `retroplug-node` | shared module | The N-API addon — the same backend behind `require()`. Gated on `RETROPLUG_NODE_ADDON=ON` (needs `node_api.h`). | opt-in |
| `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
| `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
| `retroplug-render-host-test` | executable | Drives the offline `RenderHost` directly ([11-ui-rendering.md](11-ui-rendering.md)). `EXCLUDE_FROM_ALL`. | no |
| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` | executable | The ten Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`. | no |
| `retroplug-n8-test` / `-n8-hwtest` | executable | Everdrive N8 protocol checks; `-hwtest` needs the physical console. `EXCLUDE_FROM_ALL`. | no |

The plugin's identity is vendor-owned (not the DPF example namespace):
`DISTRHO_PLUGIN_NAME "RetroPlug"`, URI `https://retroplug.io`, CLAP id
`net.tommitytom.retroplug`, all in
[DistrhoPluginInfo.h](../packages/native/plugin/DistrhoPluginInfo.h). It exposes 8 outputs — four
stereo pairs `out_1..4` ([10-multichannel-audio-out.md](10-multichannel-audio-out.md)).

### Not yet built / deferred

The build declares `clap`, `vst3`, `vst2`, `au`, and `jack` (the `dpf_add_plugin` call in
[packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt)); AU is gated by DPF to macOS. Only
**LV2** is not built — its out-of-process DSP/UI split doesn't fit RetroPlug
([07-remaining-work.md](07-remaining-work.md)).

## Hosts → test tiers

Three of the C++ hosts (see [01-architecture.md](01-architecture.md)) each back a headless TS test
tier; `test:plugin` is a fourth tier with no TS runner at all.
The TS runners live in `packages/retroplug/scripts/`; every runner discovers
`*.test.ts` files, bundles each with esbuild (es2020, one process per file), and reports TAP with
a nonzero exit on failure. Slugs accept slash or dash form, and a prefix runs everything under it (e.g.
`pnpm test paths` or `paths-rebase`): in slash form a prefix must be a whole directory, while in dash form it may
end at any dash, so `parity-blocks-tiny` runs the five `parity/blocks-tiny-*` files and `parity/blocks-tiny`
matches nothing.

### Skips are reported, and ratcheted

A case that cannot run — a fixture that is not checked out, a counter only a profile host exposes —
calls `skip(reason)` from [testing/harness.ts](../packages/retroplug/testing/harness.ts) and is
reported as TAP `ok N - name # SKIP reason`, counted separately from a pass. It **throws**, so it
works from inside a helper or a loop (several files delegate a whole case to a shared helper), and
`return skip(...)` type-checks anywhere. Call it only from code the test body runs or awaits: any
`catch` between it and the harness swallows it, which is why `toThrow` re-throws a skip.

The runners parse that TAP rather than reading only the exit code, which also lets them fail a file
that **reported nothing at all** — the harness schedules its report from inside `test()`, so a file
registering no cases prints nothing and exits 0.

Which files may skip is pinned by
[scripts/skip-baseline.json](../packages/retroplug/scripts/skip-baseline.json), keyed suite → slug →
`{skipped, total}`. A file may skip only if listed and only up to its listed count, and may not drop
below its recorded `total` (deleting cases from a skipping file lowers its skip count, which would
otherwise read as an improvement). Skipping **fewer** is never an error: the baseline is generated
with the sibling fixture trees absent, which is CI's worst case, so a dev box that owns the fixtures
simply runs more of the suite. Regenerate with `--update-skip-baseline` (refused on a filtered run,
which has no reading for the files it did not execute), and set `RP_FAIL_ON_SKIP=1` to demand zero
skips on a machine with a complete fixture set.

Fixtures are reached through injected paths, never a literal: `__REPO_RESOURCES_DIR__` (committed,
in-repo, always present), `__RESOURCES_DIR__` (the sibling resources tree, `RETROPLUG_RESOURCES_DIR`)
and `__RISA_SRC__` (`RISA_SRC`). Pointing the last two at a nonexistent directory is how the CI
picture is reproduced locally.

Runners execute their per-file (and, for `test:plugin`, per-binary) work in a **bounded parallel
pool** — each unit is an isolated child process (its own `mkdtemp` config dir; UI runs an in-process
software display), so concurrency is safe and needs no coordination. Concurrency defaults to **half
the logical threads**; override with `--jobs N` / `-j N` on the runner or the `TEST_JOBS` env, and
`TEST_JOBS=1` restores serial one-at-a-time output. Output is buffered per child and flushed as a
labelled `# <slug>` block on completion (rather than live-interleaved). The shared pool/spawn helper
is [scripts/lib/testPool.mjs](../packages/retroplug/scripts/lib/testPool.mjs). Work is dispatched
LONGEST-FIRST from durations the previous run recorded under `packages/retroplug/.test-timings/`, so
the wall clock is bounded by the slowest FILE rather than by when the pool happened to reach it.

The `reaper:*` tiers are parallel too, by a different mechanism — see
[Running the Reaper leg in parallel](#running-the-reaper-leg-in-parallel).

### The parity baseline (`test-native/parity/`)

Exact output, not levels. Every other audio test asserts RMS, peaks or timing tolerances, which a
refactor can move without failing one. The parity suite renders 197 scripted cases in 26 groups on the
real engine and records, per case, a hash of every output lane of every segment, the host MIDI out, and
each system's SRAM and work RAM at the end; one changed bit fails it. It is the gate Phase B of the
modular graph engine is held to ([docs/modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)).
The suite is 30 files: a file per group, and four with no baseline of their own (`comparator`,
`determinism`, `host-midi-order` and `render-blocks`).

- **Cases are driven through the stores**, as the UI drives them, so they survive the engine moving
  underneath. [parity/rig.ts](../packages/retroplug/test-native/parity/rig.ts) boots the app's own control
  plane and resets it before each case; [parity/scenes.ts](../packages/retroplug/test-native/parity/scenes.ts)
  holds the systems and stimuli the groups share; each `parity/<group>.test.ts` is a table of cases.
- **Scenes use `rig.press` and `rig.writeFile`** for input and files, so a change to the engine under them
  edits only `rig.ts` and `bootSession` (plus `render-blocks.test.ts`, which tests `renderBlocks` itself:
  it builds its systems, their DSP pipelines and the project's stages through a `DirectGraph` sharing the
  session's graph, with `setPipeline` and `setProjectStages`, and calls `setSerialOutCapture` directly).
- **Rendering goes through `renderBlocks`**, a harness-facet driver call that repeats `PluginDSP::run`'s
  order for every host block: stage MIDI at its frame, drain the command ring, apply transport,
  `processBlock` at 2/4/6/8 lanes (the host shape `multi(outputs)`, which TS's audio driver sets around
  the call with `withShape`), take the MIDI out. The
  `render-shapes` group covers the other two shapes, `renderAudioPerSystem` and
  `renderAudioPerChannel`. The rig fails any render after which the graph has dropped a message on a
  full queue (`audio.droppedMessages()`, the engine's count), so every case runs with nothing dropped.
- **Block sizes.** The `blocks-*` groups render the same scenes in 64, 199, 512, 1024 and 8192-frame
  blocks and a mixed cycle; the mGB and NES cases end with an edge segment of irregular blocks, a 7-frame
  one among them. The five `blocks-tiny-*` groups
  ([parity/blocks-tiny.ts](../packages/retroplug/test-native/parity/blocks-tiny.ts)) take the partitions
  a splitting host hands a plugin: 1-frame blocks, the ragged cycle `[33, 1, 222, 1, 7, 4096, 2]` (`fl`)
  and FL Studio's own shape, a 256-frame buffer split inside at automation points (`fl256`), each at
  44.1 and 48 kHz for mGB and BlipToaster (fixture-free), with notes and automation CCs on a block's first
  and last frame and on a 1-frame block's only one; and, from the sibling fixtures, LSDj's MIDI sync,
  Arduinoboy, MI.OUT and master sync (master sync in `blocks-tiny-lsdj-master`, so the two LSDj files run in
  parallel) and risa's host sync in the same partitions, their tempo and transport points on 1-frame blocks.
  `host-midi-order` pins the one behaviour B3a changed on purpose
  ([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 3.4): host MIDI
  staged out of frame order, as the plugin stages a block (the DAW's notes, then each moved automation
  slot's CC at frame 0), renders on mGB and BlipToaster exactly as the same messages staged in frame order
  do, since `rp.host.midi-in` stable-sorts the staging. `renderBlocks` refuses unsorted MIDI, so it stages
  through `stageMidiIn`.
- **Cores power on in a fixed state.** The runner sets `RETROPLUG_DETERMINISTIC=1` for `parity/` files
  only ([util/Determinism.hpp](../packages/native/src/util/Determinism.hpp)): SameBoy's power-on randomness
  off, Mesen NES RAM all zeros, the SMS VDP starting at scanline 0. The app keeps the random power-on.
- **Baselines** are `parity/baseline/<group>.json`, one value per line, each lane `<fnv> <rms>`, so a
  re-record diffs readably. Audio is compared exactly only on the target a baseline was recorded on
  (`linux-x64-gnu13`: the devcontainer and CI's linux-x64 job, both GCC 13.3), because float output is
  bit-stable only for one compiler on one architecture. On any other target each case renders twice and
  must agree with itself, and its MIDI out, SRAM and work RAM are still held to the baseline. CI's
  linux-x64 `test:native` step sets `RP_PARITY_REQUIRE_TARGET=1`: there a host that is not the baseline's
  target FAILS instead of falling back, so exact checking cannot fade out after a toolchain change.
  Moving the target is one commit: set `PARITY_TARGET` in `parity/group.ts` to the new host's
  `buildTarget()`, re-record every group unfiltered on that host with the fixtures present (each baseline
  naming the old target is re-labelled with every digest recorded fresh; a re-label is refused under
  `RP_PARITY_ONLY` or when any case skips, so no old-target digest survives), and move CI's linux-x64
  runner image to that toolchain in the same commit, since its `RP_PARITY_REQUIRE_TARGET` step fails on
  any other.
- **Re-record** with `RP_PARITY_UPDATE=1 pnpm test:native parity/<group>` (`RP_PARITY_ONLY=<case>` for one
  case; several groups by a dash prefix, `parity-blocks-tiny`). A recording is refused: on any host but
  `PARITY_TARGET`; for a case whose hashes change when it is re-run alone in a fresh process (it would
  depend on the cases before it); for a case whose `audible` lanes are silent after its first (boot)
  segment; and for a case whose MIDI, controller traffic and
  transport change nothing (a stimulus sent before a cart finished booting records only the boot).
  The alone-run child gets its own config dir, so it shares no disk state with the run being checked. A
  filtered recording (`RP_PARITY_ONLY`) skips the alone-run check: its case runs first in its process, so it
  is alone by construction, and the next unfiltered check compares it with the group order.
  `RP_PARITY_ONLY` can be given to the whole suite (`RP_PARITY_ONLY=<case> pnpm test:native parity`): every
  group without that case runs nothing and passes with a note.
  A failing case names the first segment and lane that differ, then gives its whole actual digest as one
  line of JSON, so a CI failure carries CI's output and not only the difference.
  `RP_PARITY_DUMP=<dir>` writes every case a check runs, passing or failing, to `<dir>/<group>/<case>/`:
  its lanes as float WAVs and its MIDI out as JSON, so the same run at an older commit (where the case still
  passes) gives the files to compare against. An
  intentional audio change re-records the groups it moves in the same commit, and that diff is the record
  of what moved.
- **Recorded defects.** The baseline pins five engine defects on purpose; fixing each is its own change
  with a deliberate re-record: Arduinoboy MIDI sync plays only in some host block partitions (at divisor
  1 silent at 128 to 768 frames, in 1-frame blocks and in `fl256`, playing in `fl`, most of whose blocks
  carry almost no clock tick; at divisor 4 silent at 1024 too); a single NES under a split routing on a host narrower than the split arms the capture tap and drifts (~25 frames per block at 48 kHz / 512, about 49 ms per
  second); LSDj master sync is broken at 48 kHz and above while serial-out capture is armed; MI.OUT's
  decoded notes depend on the host sample rate; in small host blocks master sync decodes a stopped LSDj's
  idle flood as clocks. Details in design section 2.5
  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)).
- LSDj, risa, nanoloop and the controller cases need the sibling fixtures (`RETROPLUG_RESOURCES_DIR`), so
  those groups skip in CI, counted in the skip baseline. CI therefore runs only the 152 fixture-free cases
  (every MIDI-out, controller-in, LSDj, risa and GBA case is fixture-only, LSDj's link-cable cases among
  them: 45 of the 197 skip without the fixtures, and one `render-blocks` case besides), so a Phase B step is
  accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`
  (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`).

| Tier | Backend under test | Command | Runner | Test dir |
|---|---|---|---|---|
| **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
| **real host** | the real Backend RPC surface (fs/config/codec + a live `SameBoySystem` on the real engine graph, the DSP kernel running as its script node) | `pnpm test:native` | [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) on `retroplug-host` | `test-native/` |
| **LVGL React UI** | the real UI bundle on a software LVGL display driven by `UiHarness` | `pnpm test:ui` | [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) on `retroplug-ui-test` | `test-ui/` |
| **C++ unit** | native classes directly, no TS in the loop | `pnpm test:plugin` | [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) over ten Catch2 binaries | `packages/native/test/` |

Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
`retroplug-host`; UI → `retroplug-ui-test`), so a `pnpm test*`
invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.

The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
(isolated real disk). The native runner also injects the resources dirs so tests can locate ROMs by
absolute path; the DSP role kernel needs no injection, since every Engine embeds it as a script node. The UI runner aliases `import ... from "ui-harness"` to the build's
own [test-ui/uiHarness.ts](../packages/retroplug/test-ui/uiHarness.ts).

## pnpm scripts

Root [package.json](../package.json). Each builds its CMake target(s) first, then runs.

| Script | Builds | Does |
|---|---|---|
| `test` | `tjs-cli` | Mock-backend TS suite. |
| `test:native` | `retroplug-host` | Real-host suite. |
| `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds five `[profile]` cases, which `tools/run-profile.sh unit` runs); the kernel node held to the digests of what it sent when it was recorded, block by block, a Game Boy's serial input hashed byte by byte so the runs of same-frame bytes it now sends kept every recorded value, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes. A hidden bench in it, `ScriptBench`, times a script node per block at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
| `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
| `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
| `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
| `test:cli-ts` | `retroplug-cli` | Runs a directory of `.ts` tests through the CLI's own embedded stripper + harness — the consumer-kit path ([09-cli-debugging.md](09-cli-debugging.md)). In CI. |
| `validate` | `-clap` + `-vst3` | `clap-validator` + `pluginval` against the built binaries via the shared [validate-plugins.sh](../tools/validate-plugins.sh). Fails if a validator is missing rather than skipping it. |
| `reaper:mgb-smoke-author` | `-vst3` | Authors + bakes the `.rplg` fixture for the Reaper render. |
| `reaper:mgb-smoke` | `-vst3` | Renders `examples/reaper/mgb_smoke.rpp` through real Reaper — end-to-end DAW proof. The `.rpp` fixtures are gitignored and derived: every `reaper:*` run re-authors its own before rendering, so a fresh clone needs no `examples/`. |
| `reaper:all` | `-vst3` | Builds + authors once, then runs the **whole** Reaper leg — **15 checks: 9 audio renders + 6 editor/host checks** — concurrently, with a PASS/FAIL summary ([run-reaper-suite.sh](../tools/run-reaper-suite.sh)). |

`pnpm test` runs the mock-backend TS suite; the native, UI, plugin, and Reaper tiers are invoked
explicitly by their own scripts.

### Running the Reaper leg in parallel

Each Reaper check boots its own headless stack (Xvfb + openbox + dummy JACK + isolated Reaper
config + the built VST3). Individually those are the `reaper:*` scripts; to run the entire leg at
once, `pnpm reaper:all` fans all fifteen out concurrently — the 9 render scenarios
(`mgb-smoke`, `mgb-midi-timing`, `n8-midi-timing`, `lsdj-midi-metro`, `lsdj-arduinoboy-metro`,
`lsdj-midi-drift`, `risa-sync`, `sms-sync`, `gg-sync`) and the 6 editor/host ones (`editor`,
`editor-reopen`, `editor-autoload`, `params-vst3`, `params-clap`, `params-vrc7`). The two
multi-instance checks (`reaper:two-instances`, `-load`) are deliberately outside the suite: they
stand up several plugin instances in one host and are run on their own. The shared bring-up/tear-down lives in one
sourced helper, [tools/reaper-env.sh](../tools/reaper-env.sh), which keys every otherwise-shared
resource off `RP_JOB_TAG` — a **uniquely named JACK server** (`jackd -n` + `JACK_DEFAULT_SERVER`,
the thing that actually lets two Reapers coexist), a per-tag config dir, a `-displayfd`-allocated
Xvfb display, and per-tag logs (under `build/reaper-cfg-<tag>/logs/`). A bare `reaper:*` run
defaults the tag to the scenario, so single runs are unchanged. The renders are
NOT bit-repeatable run to run, even with cores powered on in a fixed state: measured over two
runs of two re-authored passes each, four and then five of the nine legs rendered different PCM in
consecutive passes, and which legs changed from run to run (why is not yet known). So the analyzers judge timing and level with
tolerances, and the bit-exact engine baseline is the headless one ([The parity
baseline](#the-parity-baseline-test-nativeparity)). Cap concurrency with
`RP_SUITE_JOBS`; the default is whatever `/dev/shm` can back (each jackd mmaps ~107 MB), capped at 8,
because a dev container's common 512 MB fits only four and the fifth jackd dies with a bus error while
its Reaper waits forever. Skip the rebuild + fixture regen with `RP_SUITE_NO_BUILD=1`. Not in CI
— it needs a full DAW + X stack.

## The dpf.js seam

The generic framework (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) lives in the
nested `deps/dpf.js` submodule, consumed through this seam:

1. **Submodule** — `.gitmodules` registers `deps/dpf.js` (clone with `--recursive`).
2. **pnpm link** — `"dpf.js": "link:./deps/dpf.js"` in the root [package.json](../package.json) `devDependencies`; `pnpm install` wires it before the first configure. lv_binding_js has its own pnpm workspace (react/react-reconciler/lvgljs-ui) that needs its own `pnpm install` — see [AGENTS.md](../AGENTS.md).
3. **CMake resolve + add_subdirectory** — the root runs `node -e "require.resolve('dpf.js/package.json')"` → `DPFJS_PATH`, then `add_subdirectory("${DPFJS_PATH}" ...)` defines `dpf_add_plugin`, `dpfjs::core`, `lvgl-js-native`, `tjs`, `tjsc`, `tjs-cli`, `rpcpp`, `miniz`.
4. **Use of `${DPFJS_PATH}`** — the backend compiles the shared txiki host `TjsHostRuntime.cpp` and includes the txiki/QuickJS headers; the plugin embeds the generic `dpf-widgets/generic/LVGL.cpp` and links `dpfjs::core` + `lvgl-js-native` + `tjs`. All in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).
5. **React resolution** — the UI ([ui/main.tsx](../packages/retroplug/ui/main.tsx)) is bundled by the shared [tools/build-ui.js](../tools/build-ui.js), which resolves `react`/`react-reconciler`/`lvgljs-ui` from dpf.js's own pnpm workspace under `deps/dpf.js/deps/lv_binding_js/node_modules`.

The shared C++ core (`SystemBase`, the SameBoy/Mesen systems, the LSDj kit codec, the
transport primitives) is compiled once into the `retroplug-core` static lib and PUBLIC-linked by
[the backend](../packages/native/CMakeLists.txt), beside `retroplug-graph`, the graph the engine
runs on. See [01-architecture.md](01-architecture.md)
for the "shared core" boundary.

## The headless verification loop — which command proves which change

Pick the cheapest tier that actually exercises the code you touched. Trust-but-verify: a claim that
"tests pass" should be backed by an exit-zero from one of these.

| You changed… | Prove it with | Why this tier |
|---|---|---|
| **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug/src` that doesn't need a live core. |
| **The engine's output, bit for bit**: routing, block handling, MIDI staging, sync, anything Phase B moves | `pnpm test:native parity` | The parity baseline: one changed bit in any lane, MIDI byte or RAM byte fails, which RMS-based tests never see. |
| **DSP behaviour / cores / the command-ring & snapshot seam**: role kernel, the kernel node's wires, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem` on the real engine graph, the DSP kernel running as the script node every Engine embeds. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
| **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
| **Anything you want to eyeball** in the standalone | `pnpm screenshot` → Read `/tmp/retroplug.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug"`. |
| **The audio-thread / control-thread seam**: `QueuedInvoker`, the command ring, plan swaps, `SnapshotRegistry` publish/read, the release-ring ownership handoff | `tools/run-sanitizer.sh thread` and `… address` | Builds `retroplug-host` instrumented into `build-tsan/` / `build-asan/` (the load-bearing `build/` is untouched) and reruns the audio-thread native tests under TSan / ASan. `thread` proves the seam is race-free; `address` proves the cross-thread ownership handoff (build → park → a node takes the core; remove → the replaced plan handed back → freed with its cores at `reclaim` / `drainReleased`) has no use-after-free or leak. Both are expected to need **no** new suppressions. |
| **DPF wrapper / plugin-format / state-restore behaviour** | `pnpm validate` | `clap-validator` + `pluginval` against the `.clap`/`.vst3`. Catches ABI / state-restore / threading regressions in the format adapters. |
| **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke` | Instantiates `retroplug.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |

### Sanitizer specifics

[run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
directly: `tools/run-sanitizer.sh <thread|address> [slug]`. It configures a separate
`build-tsan/` / `build-asan/` with `-DRETROPLUG_SANITIZE=…`, builds the instrumented host, then
drives it via `run-native-tests.mjs` with `RETROPLUG_HOST` pointed at the instrumented
binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2 seqlock
suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)); a clean run **is** the
proof that the script host's audio runtime + cores are touched only by the audio thread. Any finding aborts
the host → nonzero exit.

## Build gotchas

- **The embedded bundles are derived — never commit them.** `cp-bundle.js`/`cp-bundle_data.c` and
  `ui-bundle.js`/`ui-bundle_data.c` are CMake `BYPRODUCTS` under `build/native/`
  (CMake `BYPRODUCTS` of the `retroplug-cp-bundle` / `-ui-bundle` custom targets), regenerated from
  [tools/build-controlplane.js](../tools/build-controlplane.js) and
  [tools/build-ui.js](../tools/build-ui.js) on each build.
- **`retroplug-ui-test` rebuilds the UI bundle; `run-ui-tests.mjs` alone does not.**
  The UI app bytecode (`rp_ui_bundle`) is embedded **in the test binary**, and the target
  `add_dependencies` on `retroplug-ui-bundle` rebuild it. `pnpm
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

- [CMakeLists.txt](../CMakeLists.txt) — the root configure: deps, the vendored cores, and the single `add_subdirectory(packages/native native)`.
- [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt) — every native target (backend, hosts, bundles, plugin, ui-test, the Catch2 binaries).
- [package.json](../package.json) — the pnpm scripts.
- [scripts/run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) / [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) / [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) / [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) — the four test-tier runners.
- [scripts/lib/testPool.mjs](../packages/retroplug/scripts/lib/testPool.mjs) / [skipBaseline.mjs](../packages/retroplug/scripts/lib/skipBaseline.mjs) — the shared pool, the TAP parser, and the skip ratchet.
- [testing/mockBackend.ts](../packages/retroplug/testing/mockBackend.ts) — the in-memory Backend double for the mock tier.
- [tools/run-sanitizer.sh](../tools/run-sanitizer.sh) — TSan/ASan over the native host.
- [tools/run-standalone.sh](../tools/run-standalone.sh) — headless standalone + screenshot.
