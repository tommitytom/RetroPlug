# 06 — Build & verification

How the build works today, and — the practical half of this doc — **which command
proves which kind of change**. RetroPlug is a single build out of one CMake configure.
For the runtime concepts referenced here (the three hosts, control plane vs audio thread,
command ring, snapshot registry, release ring), see [01-architecture.md](01-architecture.md).

## One configure

The root [CMakeLists.txt](../CMakeLists.txt) declares the project as
`NAME retroplug` and pulls in the native build with a single
`add_subdirectory(packages/native native)` ([CMakeLists.txt](../CMakeLists.txt)), after the
`deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, efsw, catch2); r8brain
supplies headers only, so it is not added as a subdirectory, and `deps/enkiTS` is vendored
under `deps/` with nothing adding or linking it since the native kit compile retired. A full
build produces the plugin (all formats) + the standalone.

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

Defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt) and, for the graph libraries, in the
CMakeLists of [node-sdk](../packages/node-sdk/native/CMakeLists.txt),
[graph-runtime](../packages/graph-runtime/native/CMakeLists.txt),
[core](../packages/core/native/CMakeLists.txt) and [dsp](../packages/dsp/native/CMakeLists.txt) (registered by
[cmake/packages.cmake](../cmake/packages.cmake)).

| Target | Kind | Produces | In `ALL`? |
|---|---|---|---|
| `retroplug-core` | static lib | The shared emulator core: the system backends, the transport primitives, `SampleCache` (the offline seam's file decode) and the RPC-free utility the hosts need. `src/lsdj/` is test-reference code only now (the kit path's effect arithmetic, compiled into the two test binaries that hold the nodes to it). | yes (via consumers) |
| `retroplug-node-sdk` / `retroplug-node-sdk-headers` / `retroplug-reflect-headers` | static lib / interface | The node SDK ([packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)): canonical JSON, descriptors, the graph document, the JSON to msgpack converter (the library); node-sdk's include root alone, which the executor and the launchpad and N8 tests take; reflect-cpp's headers alone, as `SYSTEM`, for the device-mode objects. | yes (via `retroplug-graph`) |
| `retroplug-graph-exec` / `retroplug-graph` | static lib | The executor (no heap, no exceptions, builds for the RP2350) and, over it and the node SDK, the registry, planner, resources, `GraphHost` and the host nodes ([packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
| `retroplug-core-nodes` | static lib | The `rp.core` node package ([packages/core](../packages/core/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
| `retroplug-device-mode-objects` / `rp-device-check-cases` / `rp-device-check` | object lib / executable / firmware | The `RP_DEVICE` objects `retroplug-device-mode-test` links, built from every `<target>-device` check target; the desktop tool that writes the device check's cases (builds into `build/device-check/cases/`, not `build/bin`), beside `retroplug-export-plan` (`build/export-plan/`, `EXCLUDE_FROM_ALL`, which writes the device check's and the box's plan blobs and which `device:check`, `device:box` and the box's loop.sh need built first); and the RP2350 image `pnpm device:check` builds in its own CMake project (`build-device/`). The first two are `EXCLUDE_FROM_ALL`. | no |
| `retroplug-device-io-nodes` / `retroplug-devtest-nodes` | static lib | The `rp.device.midi-in` node ([packages/device-io](../packages/device-io)) and the device check's test nodes ([packages/devtest](../packages/devtest), listed by no product); both `device: true`, so they have `-device` object checks too. [packages/bliptoaster](../packages/bliptoaster) and devtest also carry a `retroplug.script` member (E4): a TypeScript pack, no static library, built by `retroplug_device_script_pack` into `build/packages/<name>/script/`. | no |
| `mqjs_stdlib` / `mqjs` / `retroplug-mqjs-engine` / `retroplug-mqjs-host` / `retroplug-mqjs-host-device` / `retroplug-mqjs-run` / `retroplug-mqjs-run-gc` / `retroplug-mqjs-engine-gc` / `retroplug-mqjs-tables-32` | executable / static lib / object lib / custom target | MicroQuickJS ([deps/mquickjs](../deps/mquickjs), vendored, [cmake/mquickjs.cmake](../cmake/mquickjs.cmake)): its stdlib table generator and the `mqjs` compiler (the only bytecode compiler), the engine wrapped by `rp_mqjs_engine.c` and renamed `rp_mqjs_*` (a post-build check refuses any other external name), the script host ([packages/graph-runtime/native/src/script](../packages/graph-runtime/native/src/script)) in the desktop's build and `RP_DEVICE` mode, the toolchain's runner over RetroPlug's own stdlib, and the same runner over an engine built to collect at every allocation (`retroplug-mqjs-engine-gc`, `EXCLUDE_FROM_ALL`, which `retroplug-device-mode-test` needs), and the 32-bit stdlib tables the box's bytecode is compiled against (`retroplug-mqjs-tables-32`, which `retroplug-export-plan-current` builds). `mqjs_stdlib`, `mqjs` and the runners are `EXCLUDE_FROM_ALL` and built on demand by the script packs' targets. | yes: `retroplug-mqjs-host-device` directly, the engine and host via every product; the tools and the `-gc` pair no |
| `retroplug-graph-exec-device` / `retroplug-box-usb-cdc` | object lib | The executor and the plan loader compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` in the default build (a non-portable loader line fails there), and `UsbCdcTransport.cpp` over a fake TinyUSB for the desktop test (`EXCLUDE_FROM_ALL`). Both on the MSVC `/W4 /WX` list. | no |
| `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
| `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
| `retroplug-r8brain-base` | static lib | r8brain's static members, one vendored translation unit (`R8bBase.cpp` includes `r8bbase.cpp`) compiled on a throwing allocator and linked into `retroplug-dsp-resample`. Kept off RetroPlug's `/W4 /WX` (vendored code keeps the compiler's default level) and given the deps' `-w` on GNU/Clang. | yes (via `retroplug-dsp-resample`) |
| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in the `dsp` package's [CMakeLists.txt](../packages/dsp/native/CMakeLists.txt), so `retroplug-graph` carries neither. `retroplug-dsp-nodes` and `retroplug-core-nodes` (the `rp.core` package, [packages/core](../packages/core/native/CMakeLists.txt)) each also get a `<target>-device` object library in `ALL` (the sources compiled with `RP_DEVICE`, nothing linked; on the `/W4 /WX` list) that fails the desktop build for an unguarded desktop hook. | yes (via `retroplug-backend`) |
| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt), over the node SDK in [packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
| `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
| `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
| `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)). Shipped in the CI artifact, deliberately **not** in releases. | yes |
| `retroplug-cli` | executable | `build/bin/retroplug-cli` — the CLI runner + `render` and `test` subcommands ([09-cli-debugging.md](09-cli-debugging.md)). | yes |
| `retroplug-host` | executable | `build/bin/retroplug-host` — the real-Backend test host that evals a TS bundle over `__rpcSend`. | yes |
| `retroplug-ui-test` | executable | `build/bin/retroplug-ui-test` — boots the real React UI on a headless software LVGL display via `UiHarness`. | yes |
| `retroplug-node` | shared module | The N-API addon — the same backend behind `require()`. Gated on `RETROPLUG_NODE_ADDON=ON` (needs `node_api.h`). | opt-in |
| `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
| `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
| `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` | executable | The eleven Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`. | no |
| `retroplug-n8-test` / `-n8-hwtest` | executable | Everdrive N8 protocol checks (`packages/n8/native/test/`); `-hwtest` needs the physical console. `EXCLUDE_FROM_ALL`. | no |

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
  it builds its systems, their roles and the MIDI routing through a `DirectGraph` sharing the session's
  graph, with `setPipeline` and `setMidiRouting`, the document `setPipeline` publishes carrying an LSDj's
  serial-out capture as its Game Boy node's setting, as the stores' does).
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
- **Cores power on in a fixed state.** The runner sets `RETROPLUG_DETERMINISTIC=1` for `parity/` files and
  for `render-baseline`, `render-chunks` and `render-cli` (`DETERMINISTIC_FILES`)
  ([util/Determinism.hpp](../packages/native/src/util/Determinism.hpp)): SameBoy's power-on randomness
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
- **Recorded defects.** The baseline pins two engine defects on purpose; fixing each is its own change
  with a deliberate re-record: Arduinoboy MIDI sync plays only in some host block partitions (at divisor
  1 silent at 128 to 768 frames, in 1-frame blocks and in `fl256`, playing in `fl`, most of whose blocks
  carry almost no clock tick; at divisor 4 silent at 1024 too); MI.OUT's decoded notes depend on the host sample
  rate. Details in design section 2.5
  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Three more were pinned and
  fixed: a single NES under a split routing on a host narrower than the split (the capture tap armed over the mix,
  which drifted ~25 frames per block at 48 kHz / 512; the core now steps on the ring the block drains, so the
  narrow host hears the mix, and its cases were re-recorded with that fix, design section 2.5 and
  [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md) section 10), and LSDj master sync
  silent at 48 kHz together with its decoder reading a stopped LSDj as clocks in small
  blocks ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)). The MI.OUT and
  master-sync cases (`lsdj-modes`' `midiOut` and two `masterSync`, all six of `blocks-tiny-lsdj-master`,
  `blocks-tiny-lsdj`'s three `midiOut-*`) record the host MIDI out of `rp.lsdj.serial-out`, after the Game Boy
  ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) sections 7 and 12): each message at its
  byte's frame in the block the core captured it, which here lies off frame 0 but in the 1-frame splits and for
  one message of 85, a clock, in each master-sync `fl256` case, whose byte the core completed at its block's
  first frame.
- LSDj, risa, nanoloop and the controller cases need the sibling fixtures (`RETROPLUG_RESOURCES_DIR`), so
  those groups skip in CI, counted in the skip baseline. CI therefore runs only the 152 fixture-free cases
  (every MIDI-out, controller, LSDj, risa and GBA case is fixture-only, LSDj's link-cable cases among
  them: 45 of the 197 skip without the fixtures, and one `render-blocks` case besides), so a Phase B step is
  accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`
  (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`).

### The kit baselines (`kit-baseline`, `kit-compile`)

Exact kit bytes, not structure and audibility. A compiled bank is what LSDj and risa play, and these two goldens are
the gate: D1 recorded 39 banks from the compiler native then had, D2's TypeScript compiler had to reproduce them
through real offline jobs before any caller moved, and they are still the oracle now that native's compiler is gone
([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
[modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11).

- **The compiler.** [`src/kit/`](../packages/retroplug/src/kit) compiles a spec as chains of offline graph jobs (D2):
  each pass of a slot is a graph document the `rp.dsp` nodes run, native does every float the bytes depend on, and
  TypeScript cuts the buffers, packs the codes and bits and assembles the bank.
- **Matrix.** The kit baseline
  ([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts), run by `pnpm test:native`)
  synthesises every source in the test (two sines, a quiet sine, a chirp, seeded noise and silence at 22.05, 44.1 and
  48 kHz, plus a 600-frame source shorter than one resampler chunk), writes each as a 32-bit float WAV in a temp dir,
  and compiles it through real offline jobs, so `SampleCache` and miniaudio's decode are in the loop and the suite
  needs no fixtures. 39 cases: LSDj has no effect, gain, normalise (a silent source too), a quiet source normalised at
  every rate and through a dither, a negative gain, each of the eight filters, a 10 Hz high-pass at Q 20 and a 30 dB
  shelf, each of the five dithers, rotate on and off, a filter-gain-dither chain, the short source and a two-slot
  bank; risa has five DMC rate indices, normalise off, loop on, a silent normalise at two rates, the short source and
  a two-slot bank. The two-slot banks add a second sample named SNR with its own effects (on risa, a rate index of its
  own too) and, on the LSDj one, a lowercase over-long bank name, so the assembler's offset accumulation past slot 0, a
  second sample name and `writeString`'s uppercase and truncation are held by a hash too (the 15-slot loop is in every
  case already). `SOURCE_HASHES` binds the synthesised sources to the ones the retired C++ suite wrote, on the target
  where their `sin` values were recorded.
- **The edge banks.** [kit-compile.test.ts](../packages/retroplug/test-native/kit-compile.test.ts) holds a second
  golden, [kit-edges.json](../packages/native/test/audio/golden/kit-edges.json), 12 banks past the baseline's matrix:
  a full 15-slot and a 16-slot kit, samples past the slot count, clipped and empty slots, filled defaults, rotate on,
  and a filter-normalise-dither chain, each beside the refusal cases. (The byte rule for names outside ASCII is not a
  golden: `test/kit/kit.test.ts`'s case pins it.)
- **What a case records.** The baseline records `{hash, size, slot}`: the bank's FNV-1a-64 in lowercase hex, its size,
  and slot 0's length. The edge golden records `{hash, size}` only, because `kit-compile` refuses slots by design. Each
  suite asserts that every case's bank compiles (a refused slot included), that the bank carries every slot it
  compiles, and that the case names equal the golden's keys exactly, so a broken case cannot pin a stable empty bank.
- **Targets.** As parity: exact bytes only on `linux-x64-gnu13` (float arithmetic is bit-stable only per compiler and
  architecture); elsewhere a case compiles twice and must agree with itself, and `size` / `slot` are still compared.
  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead, and CI's linux-x64 job sets it on its
  `test:native` step. Off that target (CI's arm64, macOS and Windows jobs) the banks are therefore held only by each
  case's self-agreement and the structural asserts; the native comparison that used to make every edge bank exact on
  every host went with the compiler.
- **Re-record** with `RP_KIT_GOLDEN_UPDATE=1` on either runner (`pnpm test:native kit-baseline`, `... kit-compile`).
  It refuses on any other host and refuses a case whose two compiles disagree (that case depends on something outside
  its input), and the file is written through a sibling temp file and renamed into place, so a failed write leaves the
  committed golden as it was. Without the variable the files are only ever read.
- **A trap.** The baseline is a 4-bit view: a one-ulp change in a dither's arithmetic can move no code across every
  case (D1's Sierra Lite mutation), and `kit-edges` is the same view. That is why the kernels have `[dsp]`'s verbatim
  references at 16 bits and the buffered nodes `[dsp-buffer-nodes]`' own. Treat a kit golden failure as a real change
  to the bytes, and do not re-record it to make a refactor pass.

### The render baseline (`render-baseline`, `render-chunks`, `render-cli`)

Exact render bytes, the way the kit baseline holds kit bytes. A render is an engine job
([11-ui-rendering.md](11-ui-rendering.md), [modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md)
section 10) and these files hold what it writes.

- **Matrix.** [render-cases.ts](../packages/retroplug/test-native/render-cases.ts) defines 13 cases and
  [render-baseline.test.ts](../packages/retroplug/test-native/render-baseline.test.ts) renders each through `engineRun`,
  reads the WAVs back and compares each with `golden/render-baseline.json` (`{ms, files: [{file, hash, size}]}` a case):
  5 fixture-free (smsggdj playing songs authored into its battery: `smsggdj/sms/index-0/mix`,
  `smsggdj/sms/name-chord/mix-48000`, `smsggdj/gg/index-1/mix`, the host-synced
  `smsggdj/sms/sync-in24/transport-120bpm`, and `sameboy/mgb-sgb/no-start/mix`, mGB as a
  Super Game Boy whose boot jingle sounds with no MIDI), 4 LSDj (tripledipper fixed 5 s as mix and 4 stems; telemelt's
  MACHETE auto-detected, by name as a mix and by index as 4 stems) and 4 risa (ecoli_soul HFF on PAL and, through a
  carried region role, NTSC; blumarbl fixed 3 s as channels and pins). The hash is the kit baseline's FNV-1a-64 over the
  whole WAV, header included. Every case must sound (and every file, but the ones a case lists as silent).
- **Targets.** Length and size are compared everywhere; the hash only on `linux-x64-gnu13`, elsewhere a case renders
  twice and must agree with itself. `RP_RENDER_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead; CI's
  linux-x64
  `test:native` step sets it ([build.yml](../.github/workflows/build.yml)). `RP_RENDER_GOLDEN_UPDATE=1` re-records
  (`pnpm test:native render-baseline`) and is refused off the target, for a case that disagrees with itself and for a
  run that did not render every case (fixtures absent). The runner gives these files `RETROPLUG_DETERMINISTIC=1`.
- **Fixtures and CI.** The LSDj and risa cases need `RETROPLUG_RESOURCES_DIR` (`__RESOURCES_DIR__`), so CI runs the 5
  fixture-free cases (2 of the 5 test cases in the file; the rest skip, counted in the skip baseline), and a change to
  the render path is accepted on a run with fixtures and `RP_FAIL_ON_SKIP=1`. CI's coverage of the continuous path is
  smsggdj and the SGB jingle only: there is none of a NES, a split render or a Game Boy's stems until renders can be
  fed MIDI.
- **`render-chunks`** holds the chunk independence (5 cases): the host-synced smsggdj render, a fixed smsggdj render,
  LSDj stems and risa pins each write the same files at pulls of 100, 37 and 250 ms (`RenderOpts.pullMs`), and an
  auto-detect render writes the same samples and ends within one pull. Forcing engine jobs back to exact blocks fails
  exactly the host-synced case. **`render-cli`** (6 cases) runs the CLI's `render` tool in the test host against the
  same
  golden, and the real binary's leg is `tools/run-cli-ts-tests.sh` (`pnpm test:cli-ts`: relative paths, the golden's
  size and hash, errors, SIGINT leaving no file).
- **A trap.** An intentional change to the render path's audio re-records in the same commit, and the diff is the
  record of what moved; treat a failure as a real change to the bytes. Hashing in QuickJS is the long pole (about 150
  ns a byte): the baseline takes about 60 s with fixtures.

### The test tiers

| Tier | Backend under test | Command | Runner | Test dir |
|---|---|---|---|---|
| **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
| **real host** | the real Backend RPC surface (fs/config/codec + a live `SameBoySystem` on the real engine graph, the protocol script nodes running in its script host) | `pnpm test:native` | [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) on `retroplug-host` | `test-native/` |
| **LVGL React UI** | the real UI bundle on a software LVGL display driven by `UiHarness` | `pnpm test:ui` | [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) on `retroplug-ui-test` | `test-ui/` |
| **C++ unit** | native classes directly, no TS in the loop | `pnpm test:plugin` | [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) over eleven Catch2 binaries | `packages/native/test/` |

Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
`retroplug-host`; UI → `retroplug-ui-test`), so a `pnpm test*`
invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.
The script nodes are tested there too, each over a `Block` stand-in (`test/script/nodes/`, a file per type, where
the role tests that drove the DSP kernel moved when it was deleted; `lsdj-serial-out.test.ts` holds MI.OUT's and
master sync's messages each at its frame in its own block, and master sync's stop 200 ms after its byte across a
change in the host's rate, read off each block's transport), and so are the pure functions they run (`test/dsp/`,
where `lsdj-arduinoboy.test.ts` feeds the MI.OUT and master-sync decoders one byte stream cut into 1-frame, FL-style
ragged, FL's split 256-frame, longer ragged and 1024-frame blocks and holds every cut to the same messages at the
same absolute frames, so the decoders add no dependence on the split, MI.OUT's stream spanning 10155 frames so that
every cut lands at places of its own; holds master sync's stop 200 ms after its byte across a drop and a rise in the
host's rate and two changes inside one silence, and at a block's first frame where a rise leaves it overdue; and
holds the emit's five arguments, 0 past the length, for every length each decoder sends), with the zod-free settings
schema held to zod's own output (`test/graph/settings-schema.test.ts`) and the script-node bundle held free of zod
(`test/script/bundle-no-zod.test.ts`, which builds it with the build's own options in a `node` child (the runner's
`RP_NODE`) and reads esbuild's metafile, with a 200 KB bound on the bundle). The protocol nodes' port names are held
to their modules by `test/graph/protocol-nodes.test.ts`, which holds a type that reads a system's `serialOut` to
being some role's `out` too, and an `out` to reading neither routed MIDI nor launches; and `buildGraph`'s documents
with roles to `golden/build-graph-roles.json` by `test/graph/build-graph-role-goldens.test.ts`, which holds every
wire of them to run forward and none to be a feedback wire, and which `RP_GOLDEN_UPDATE=1` re-records.

In the real-host tier, beside parity, a few files hold timing to tolerances the baseline's bits cannot state:
`lsdj-mastersync` holds master sync to one story in 1-frame, FL-style ragged and 1024-frame blocks at 44.1 and
48 kHz, and its clocks to LSDj's tempo in absolute frames in every split (consecutive clocks of a run within 3%
of a tempo period, a run's first gap, which LSDj sends early, within 30%), which a decoder sending at a block's
frame 0 fails in the ragged and 1024-frame splits; `lsdj-midiout` holds MI.OUT's messages to frames inside their
blocks, most past frame 0. `dsp-bench`, in a profile host only, holds an mGB window and a playing LSDj's
master-sync and MI.OUT windows to zero allocations a block (`RP_BENCH_LSDJ=0` leaves the LSDj windows out;
[08-profiling.md](08-profiling.md)).

The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
(isolated real disk). The native runner also injects the resources dirs so tests can locate ROMs by
absolute path; the script nodes need no injection, since every Engine embeds their bundle. The UI runner aliases `import ... from "ui-harness"` to the build's
own [test-ui/uiHarness.ts](../packages/retroplug/test-ui/uiHarness.ts).

## pnpm scripts

Root [package.json](../package.json). Each builds its CMake target(s) first, then runs.

| Script | Builds | Does |
|---|---|---|
| `test` | `tjs-cli` | Mock-backend TS suite (C3's `test/systems/store-resources.test.ts`: each verb's resource and publish order, rollback after a refusal, the construct-time reset, DirectGraph, every put balanced; C5's `test/capability/capability.test.ts` and `test/graph/build-graph.test.ts`'s placement and fan-out cases). |
| `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
| `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 pack's protocol and serial checks (built from `packages/n8/native/test/`, not part of `test:plugin`): the one Edio held to its byte-level golden (`edio-golden.json`, 28 cases), every status code, one write per frame, a cancel at each step of `bootRom`, the menu commands and operations over `FakeN8`, the facet's sessions and link methods (`[n8-rpc]`, the port guard both ways), the provider (`[n8-provider]`), and `[cart-jobs]` and `[cart-release]` since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread; the serial test also holds a POSIX port exclusive (`TIOCEXCL`: a second open fails busy). Not a CI step. |
| `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
| `test:plugin` | eleven Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`, `-device-mode-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`, the fixed-capacity settings types `[settings-types]`, the field walk `[fields]` and the enum scan `[enums]` among them), the node SDK built for the device (`retroplug-device-mode-test`, below), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). E1's `retroplug-device-mode-test` ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md) section 8) compiles `rp.core` and `rp.dsp` with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI and holds 87 cases in eight files: a device-mode node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, for every portable type over a table of settings, allocating nothing in create, process or destroy; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON refuses in the same words, naming the field (two inputs part by choice, pinned: design section 6); the JSON to msgpack converter; and eighteen `[device-check]` cases that hold the image's cases to the manifests' device entries, run the whole check in the image's arena with no allocation, stop it at each of its failure steps, judge its 10-node plan and run the devtest script pack's side (its types, a bench, a 4-node script plan held to the desktop's digest, the clock a node's deadline is read from, the deadline case's loops over a builtin in the box's pack's heap); the script host (E4, [modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md), `[script-pack]`, 42 cases in `ScriptPack.test.cpp`, `BlipToaster.test.cpp` and `DeviceCheck.test.cpp`): a TypeScript node on MicroQuickJS in both builds of the host, its block API, every fault (a throw, a deadline, a loop over a builtin held to it, a heap running out silencing only its pack, the engine's dropped exceptions, a getter the host reads, the re-entry cap), a warmed block allocating nothing, a plan exported and loaded over the device-mode builds digest-identical to GraphHost and the loader's package memory, with `[bliptoaster]` holding `rp.bliptoaster.midi-policy` to `rp.core.midi-filter` message for message and `[debug-gc]` running the packs on an engine that collects at every allocation; the plan loader (`[plan]`: an exported document loaded into an arena equals GraphHost's run bit for bit, every refusal, every bit of the three blob goldens flipped, three blob goldens), `rp.device.midi-in` (`[device-io]`), the box's host pieces (`[box]`) and `UsbCdcTransport` over a fake TinyUSB (`[usb-cdc]`). Its configure also runs three refused-source checks (a heap type, a pointer in `Settings` and a heap header must each fail in device mode with their own reason; the heap header is skipped under libc++). The engine-graph binary's `[packages]` holds each product's generated registration to a golden list, and an engine's registry to that list plus the built-in script pack's types and nothing else. The configure holds the products too: `retroplug_check_products` (deferred to the root's end) refuses a binary that links a library carrying the engine, computed outward from `retroplug-backend`, and is no product. |
| `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
| `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. In CI. |
| `device:check` | `rp-device-check` | [tools/build-device-check.sh](../tools/build-device-check.sh): builds the cases generator and `retroplug-export-plan` in the desktop build (configured first), then the RP2350 image of every portable node type of `core dsp n8 device-io devtest` (22 types, the devtest pack's two script types among them), the N8 pack's Edio run over a scripted cart, a 10-node plan exported at build time and run through the loader on a 4 KB stack guarded by `MSPLIM`, and the devtest script pack on MicroQuickJS (a bench and a 4-node script plan, whose stack is judged in two phases), with `-Werror=stack-usage=1024` on every compile (Pico SDK 2.1.1 via `PICO_SDK_PATH`, falling back to `/workspaces/pico-sdk` when unset; `arm-none-eabi-g++` 13; reflect-cpp's headers at the desktop's pin; `build-device/`) with a link trap on the heap, on exceptions and on RTTI (87 `--wrap`ped symbols with arm-none-eabi GCC 13.2.1's libstdc++), the trap probe (a program that does `new int(1)` and `std::malloc(16)`, which must FAIL to link naming both), and prints the sizes by owner. The image is built, never run. In CI (linux-x64). |
| `device:box` | `n8_host` (PIO-USB and `RP_NATIVE_USB`) | [tools/build-box.sh](../tools/build-box.sh): builds the Pico box's firmware ([pico/n8-host](../pico/n8-host)), C++ on the N8 pack's Edio and, since E3, a graph host (`pico/host/`) whose bridge is `plan.json` (`rp.device.midi-in` to the `bliptoaster` policy, a TypeScript node on MicroQuickJS, to `rp.n8.nes`), exported by `retroplug-export-plan` (built first) into a 64 KiB static arena with `PICO_HEAP_SIZE=0` and `-Werror=stack-usage=1024`, in both USB variants in `build-box/{pio,native}/`, each linked under the same 87-symbol trap over the whole image (TinyUSB and Pico-PIO-USB included) and with its own `n8_host_trap_probe` that must FAIL to link naming `__wrap__Znwj` and `__wrap_malloc`, and prints each image's flash, data and bss. Needs the SDK's TinyUSB submodule (`PICO_SDK_PATH` when it carries one, else `/workspaces/pico-sdk`, said aloud) and a Pico-PIO-USB checkout at 5a37a66 (`PICO_PIO_USB_PATH`, default `/workspaces/Pico-PIO-USB`); no UF2 (`PICO_NO_PICOTOOL`). Built, never run: the rig's gate is `pico/n8-host/loop.sh`, `stress.sh` and `faults.sh`. In CI (linux-x64, beside `device:check`). |
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
| **A kit compile**: `src/kit`'s chains, the packing, the bank, and the offline seam they run on | `pnpm test:native kit` (`kit-baseline`, `kit-compile`, `kit-dither`) and `pnpm test:native offline-run`; `pnpm test:ui kit-compile` for the plugin's facets | The 39 baseline banks and the 12 edge banks, exact bytes on `linux-x64-gnu13` and each case's self-agreement plus the structural asserts elsewhere ([the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `offline-run` is the promise side over the real host, and the UI case settles a whole compile from frame pumps on the plugin's own registration. |
| **A render**: the render library, engine jobs, the CLI's `render`, the Render menu and its badge | `pnpm test:native render` (`render-baseline`, `render-chunks`, `render-cli`; `engine-run` by name) with `RETROPLUG_RESOURCES_DIR` and `RP_FAIL_ON_SKIP=1`, `retroplug-engine-graph-test "[engine-jobs]"` (under `build-tsan` / `build-asan` for the pool), `pnpm test render`, `pnpm test:ui` (`render-menu`, `render-badge`, `render-job`), `pnpm test:cli-ts` | The baseline holds the bytes (exact on `linux-x64-gnu13`), `[engine-jobs]` the pool's threading and the continuous blocks, the UI cases the submission and the badge on a scripted backend and on a real 3 s render, and `test:cli-ts` the real binary's output, exit codes and Ctrl-C. |
| **The DSP kernels/nodes**: the chain's effects and resample, the buffers the kit passes read | `pnpm test:plugin` (`[dsp]`, `[dsp-nodes]`, `[dsp-resample]`, `[dsp-buffer-nodes]`, and `[offline]` for the pool) | The `[dsp]` cases hold the kernels to their padded references at 16 bits where a 4-bit code cannot see a one-ulp move; the nodes' cases hold each node to its kernel, the state guards, the dither's per-channel noise and the buffer-reading nodes' peaks and seeds; `[offline]` holds the job pool. |
| **DSP behaviour / cores / the command-ring & snapshot seam**: the protocol nodes and their wires, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem` on the real engine graph, the protocol nodes running in the script host every Engine embeds. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
| **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
| **Anything you want to eyeball** in the standalone | `pnpm screenshot` → Read `/tmp/retroplug.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug"`. |
| **The audio-thread / control-thread seam**: `QueuedInvoker`, the command ring, plan swaps, `SnapshotRegistry` publish/read, the release-ring ownership handoff | `tools/run-sanitizer.sh thread` and `… address` | Builds `retroplug-host` instrumented into `build-tsan/` / `build-asan/` (the load-bearing `build/` is untouched) and reruns the audio-thread native tests under TSan / ASan. `thread` proves the seam is race-free; `address` proves the cross-thread ownership handoff (a node builds its core at activation; remove → the replaced plan handed back → the core destroyed and its pins dropped at `reclaim` / `drainReleased`) has no use-after-free or leak. Both are expected to need **no** new suppressions. |
| **DPF wrapper / plugin-format / state-restore behaviour** | `pnpm validate` | `clap-validator` + `pluginval` against the `.clap`/`.vst3`. Catches ABI / state-restore / threading regressions in the format adapters. |
| **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke` | Instantiates `retroplug.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |

### The device build

`pnpm device:check` ([tools/build-device-check.sh](../tools/build-device-check.sh)) builds `rp-device-check`
([packages/graph-runtime/device-check/](../packages/graph-runtime/device-check)), the RP2350 image that runs each
portable type of five packages (`core dsp n8 device-io devtest`, 23 types, the devtest pack's three script types
among them) once in `RP_DEVICE` mode from its default settings as msgpack, in a static 400 KiB arena, and then the N8
pack's Edio, menu commands and operations over a scripted cart
(`N8Protocol.cpp`: a boot, a file read, a cancelled boot, every primitive, held on the desktop by a `[device-check]`
case), reporting on the UART. It links with `--wrap` on 87 symbols (with arm-none-eabi GCC 13.2.1; the malloc family,
`sbrk`, `__cxa_guard_*`, every `operator new` / `delete` and `std::__throw_*` the device `libstdc++.a` defines, the
throw entry points, and the exception-handling and RTTI runtime: the personality routine, the catch and cleanup
entries, `__dynamic_cast` and the class `type_info` vtables), so any heap use or throw in what it links is a link
error naming the object, as is an object built with exceptions or RTTI that keeps a handler, a cleanup or a
polymorphic class's `type_info`; a post-link check also refuses an image that defines a wrapper or refers to one. The
Pico SDK's `pico_malloc` is taken out of `pico_runtime` in place, the one part of this image's runtime the trap cannot
have (stdio over the UART links under it); the box's firmware, whose Pico-PIO-USB host makes an alarm pool on the
heap, needs more (below). The pin, the trap list and the in-place `pico_malloc` removal are shared with the firmware
through `cmake/reflectcpp-pin.cmake`, `cmake/device-image.cmake` and `cmake/check-trap.cmake`. The script also links a
probe that allocates and requires it to fail naming `__wrap__Znwj` and `__wrap_malloc`, on every run, so the trap
cannot silently stop reaching the link. It needs `arm-none-eabi-g++` and a Pico SDK (no submodules); the CI linux-x64
job installs `gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`, clones SDK 2.1.1 into
`$RUNNER_TEMP` and runs the script. The image is not run in CI; the controller flashes it once on the rig, where it
prints, in order: the per-type pass (23 types: rp.core, rp.dsp, `rp.n8.nes`, `rp.device.midi-in` and the `rp.devtest`
nodes, the script pack's three among them), the protocol pass, a plan pass, a script bench pass (a first block, an empty
block, a note and a 64-byte SysEx, in cycles and microseconds at 150 MHz from flash, with the XIP cache's hits), a
script deadline pass (`rp.devtest.script-loop`'s four loops over a builtin in a context of the box's pack's heap, each
faulted by its 10 ms deadline, the worst block judged against a third of the box's 3 s watchdog), a script plan pass (4 nodes through `rp.device.midi-in` and a deep `replace` nest, judged by its digest against the
desktop's exactly), the script pack's report (its memory and the heap in use) and the stack's measured high-water
mark, in two phases (the load and the blocks) for the script plan, each kept 512 bytes under the 4 KB stack. The plan
pass loads a 10-node plan (a coupled
pair, a feedback wire, a wired param, eight moves) that `retroplug-export-plan` wrote at build time, through the loader
in the arena, runs 48000 frames in ragged blocks and judges it (within the export's estimate, every move pushed, no
drops, a digest that heard sound); the desktop's digest is printed beside the device's and not judged (FMA
contraction may differ). The script builds the exporter in the desktop build first (`build/` must be configured), and
the image's 4 KB stack with `MSPLIM` as a guard (`PICO_USE_STACK_GUARDS`) is in its CMakeLists, and
`-Werror=stack-usage=1024` comes from `cmake/device-image.cmake`. Release sizes: core, dsp and the executor were
about 42 KB of flash and no static RAM; the
whole image is 286 KB flash (MicroQuickJS 99 KB, the script host 19 KB, the executor and the loader about 25 KB, the
devtest script pack 20 KB, the pack's nodes and Edio about 13 KB, the devtest nodes 7.5 KB)
and about 424 KB RAM (data and bss), almost all of it the arena. The image takes a script pack's heap from
`RP_SCRIPT_HEAP_BYTES_devtest` (45056) in its registration, and `RP_DESKTOP_BUILD_DIR` (derived from
`RP_EXPORT_PLAN_TOOL`) names the desktop build whose generated pack sources it embeds.

`pnpm device:box` ([tools/build-box.sh](../tools/build-box.sh)) builds the Pico box's firmware under the same trap.
The firmware is `retroplug_product(n8-host DEVICE PACKAGES core device-io n8 bliptoaster TARGETS n8_host)`: C++ on the
N8 pack and, since E3, a graph host (`pico/host/`): `plan.json` is exported by `retroplug-export-plan` (built first by
the script, found through `RP_EXPORT_PLAN_TOOL`; `--arena 65536 --sample-rate 48000 --max-frames 256 --header
kBuiltInPlan --packages core,device-io,n8,bliptoaster`), embedded as a generated header and loaded at boot into a 64
KiB static arena, with the `bliptoaster` pack's context (MicroQuickJS, wrapped by `rp_mqjs_engine.c` and compiled at
-Os) set up inside it from 32-bit bytecode that stays in flash until the pack's setup copies it. The packages are
compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti`, linking no allocator (`PICO_HEAP_SIZE=0`, so no `.heap`
reservation), and every compile of the image, the SDK's and TinyUSB's included, takes `-Werror=stack-usage=1024`.
Pico-PIO-USB's SOF timer was an alarm pool made with `malloc`, so the firmware sets `skip_alarm_pool`, runs a static
SOF timer of its own on hardware alarm 2 (`SofTimer.cpp`) and links with `--wrap=alarm_pool_create_on_timer`, since
the config flag alone leaves the function (and the heap) linked; the unreachable wrapper panics. The main stack is 4
KB (`PICO_STACK_SIZE=0x1000`; the PIO-USB image, which links `pico_multicore`, keeps core 1's 2 KB reservation, the
native one has none), asserted only as a reservation of SCRATCH_Y. Both variants, PIO-USB (the rig's; the N8 on
GP2/GP3) and `RP_NATIVE_USB=ON`, link under the trap, each with a probe that must fail to link; the sizes in flash /
data / bss are 196688 / 46056 / 120600 and 197056 / 32020 / 114368, the image running from SRAM (`copy_to_ram`: 355824
and 335880 bytes of the 512 KiB; E3's were 101200 / 23832 / 188020 and 101576 / 9836 / 181788, 312484 and 292636,
before MicroQuickJS, the pack and the halved arena; E2's were 48796 / 15324 / 32360 and 48756 / 1560 / 26128). The
boot prints `[host] plan loaded: 3 nodes, <N> of 65536 arena bytes, 48000 Hz, 256 frames a block`, a line per node, a
`[host]   pack bliptoaster, <bytes> bytes` line and, after the first block, the pack's heap in use against its
declaration with the load's and the first block's times, the proof that the plan and the script pack loaded. CI
installs TinyUSB and clones Pico-PIO-USB at 5a37a66 for it. The hardware gate (`loop.sh`, `stress.sh`, `faults.sh`) is
run by hand on the rig with BlipToaster; it is not CI.

### Sanitizer specifics

[run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
directly: `tools/run-sanitizer.sh <thread|address> [slug]`. It configures a separate
`build-tsan/` / `build-asan/` with `-DRETROPLUG_SANITIZE=…`, builds the instrumented host, then
drives it via `run-native-tests.mjs` with `RETROPLUG_HOST` pointed at the instrumented
binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2
suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)), which names only efsw's teardown: the
triple-buffer seqlock needs no entry, since its reader's copy ignores its own racing read under TSan and records the
write of its destination ([SeqlockCopy.hpp](../packages/native/src/transport/SeqlockCopy.hpp)); a clean run **is** the
proof that the cores and the built-in script pack's audio runtime are touched only by the thread that runs
blocks. It loads no other pack: a loaded pack's runtimes are made, loaded and freed on the control thread
while an audio thread runs blocks in the others, which those slugs never do. The Catch2 cases that do,
`retroplug-engine-graph-test "[packs]"`, are built by name in `build-tsan/` and `build-asan/` and run with the
same options (the C1 design's section 9). Any finding aborts the host → nonzero exit.

## Build gotchas

- **`deps/sameboy` is patched at configure, twice.** `cmake/sameboy.cmake` applies two tracked patches idempotently
  (`git apply --reverse --check` no-ops one already present): `cmake/patches/sameboy-per-channel-audio.patch` (the
  per-channel audio tap, `Core/apu.{c,h}`) and then `cmake/patches/sameboy-process-statics.patch` (the power-on random
  seed and its enable flag as relaxed atomics in `Core/random.c`, the camera's noise seed in `Core/camera.c`, and the
  SGB's `fm_sin` table built by a load-time constructor in `Core/sgb.c`, so that parallel Game Boy cores in engine jobs
  do not race). After any configure `git status` shows five modified files in `deps/sameboy`: expected. A bump that
  invalidates either patch fails loudly at configure.
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
