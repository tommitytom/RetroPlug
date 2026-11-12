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
under `deps/` with nothing adding or linking it since the native kit compile retired. `deps/efsw` (the file watcher)
and `deps/catch2` (the C++ unit-test framework) are added `EXCLUDE_FROM_ALL`, so only a target that links one builds
it. A full build produces the plugin (all formats) + the standalone.

- `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
  **zero build-specific special-casing**: a bare `./build.sh` builds every `ALL` target. Their flags
  follow.
- Both run the configure this project needs and build in parallel (`-j$(nproc)`, `sysctl` on macOS). A bare
  `./build.sh` is incremental: it configures only when `build/CMakeCache.txt` is missing or a `-D` / `--tests` is
  given, and `--clean` wipes `build/` first. Either script passes any `-D<var>=<value>` straight through to the
  configure and forces one, so the entry lands on an existing tree. `--tests` / `BUILD_TESTING` is inert: the
  Catch2 binaries are `EXCLUDE_FROM_ALL` and built by name from the `pnpm test*` scripts. `build.bat` enters
  vcvars64 and the tool PATH and runs the Ninja + `cl` + vcpkg `x64-windows-static` configure, its tool locations
  overridable through `VCPKG_ROOT` / `RGBDS_DIR` / `NODE_DIR`. For one target after a configure,
  `cmake --build build --target <t> -j$(nproc)`: always pass `-j`, since the default is a single job.
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
| `mesen` (the vendored emulator) | static lib | The consoles' emulator, [deps/mesen](../deps/mesen/CMakeLists.txt): 288 of its 426 translation units, because `MESEN_ENABLE_SNES` / `-PCE` / `-WS` are OFF in the root's cache defaults (ON as upstream builds them; `tools/build-mesen-all-consoles.sh`, `pnpm build:mesen-all`, proves that side). The root sets them before `add_subdirectory(deps/mesen)`, and a `-D` overrides them. Each option drops a console's `Core/<Dir>/` units and its CPUs' evaluators through one table in that CMakeLists, and is a PUBLIC compile definition the guards around the call sites read. | yes (via `retroplug-mesen`) |
| `retroplug-system` / `retroplug-sameboy` / `retroplug-mesen` / `retroplug-host-io` | static lib | The F2 packages (`docs/modular-graph-phase-f2-design.md` section 2), one row for the four: `system` is the console-neutral contract every console implements and the engine drives (`SystemBase` and the capabilities, the build request and build function, `HostSystems`, the `SystemNode` template); `sameboy` is the Game Boy core, its config, boot ROMs, button table, ROM gate and `rp.sameboy.gb`; `mesen` is the NES, GBA and SMS/GG cores over the vendored `mesen` library, their configs, the NES debug session, the emulated N8 FIFO and SMS sync roles and `rp.mesen.nes` / `.gba` / `.sms`; `host-io` is `rp.host.midi-in` / `-out` and the engine host-I/O record. Each declares its dependencies in its `package.json` (`mesen` declares `n8-wire`) and each keeps its include paths; the transport primitives the cores and the host share are `system`'s (`packages/system/native/src/transport/`), and `retroplug-backend` keeps `SampleCache`, `FileSync` and lodepng. `src/lsdj/` is test-reference code only: its effect wrappers, compiled into the two test binaries that hold the dither and the buffer-reading nodes to them. Nothing in production calls `rp::lsdj::processEffect`: it is not the kit path, so do not wire a caller back to it. | yes (via consumers) |
| `retroplug-node-sdk` / `retroplug-node-sdk-headers` / `retroplug-reflect-headers` | static lib / interface | The node SDK ([packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)): canonical JSON, descriptors, the graph document, the JSON to msgpack converter (the library); node-sdk's include root alone, which the executor and the launchpad and N8 tests take; reflect-cpp's headers alone, as `SYSTEM`, for the device-mode objects. | yes (via `retroplug-graph`) |
| `retroplug-graph-exec` / `retroplug-graph` | static lib | The executor (no heap, no exceptions, builds for the RP2350) and, over it and the node SDK, the registry, planner, resources, `GraphHost` and the host nodes ([packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
| `retroplug-core-nodes` | static lib | The `rp.core` node package ([packages/core](../packages/core/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
| `retroplug-device-mode-objects` / `rp-device-check-cases` / `rp-device-check` | object lib / executable / firmware | The `RP_DEVICE` objects `retroplug-device-mode-test` links, built from every `<target>-device` check target; the desktop tool that writes the device check's cases (builds into `build/device-check/cases/`, not `build/bin`), beside `retroplug-export-plan` (`build/export-plan/`, `EXCLUDE_FROM_ALL`, which writes the device check's and the box's plan blobs and which `device:check`, `device:box` and the box's loop.sh need built first); and the RP2350 image `pnpm device:check` builds in its own CMake project (`build-device/`). The first two are `EXCLUDE_FROM_ALL`. | no |
| `retroplug-device-io-nodes` / `retroplug-devtest-nodes` | static lib | The `rp.device.midi-in` node ([packages/device-io](../packages/device-io)) and the device check's test nodes ([packages/devtest](../packages/devtest), listed by no product); both `device: true`, so they have `-device` object checks too. [packages/bliptoaster](../packages/bliptoaster) and devtest also carry a `retroplug.script` member (E4): a TypeScript pack, no static library, built at build time by the custom target `retroplug-<name>-script-pack` (`retroplug_device_script_pack`) into `build/packages/<name>/script/`. | no |
| `mqjs_stdlib` / `mqjs` / `retroplug-mqjs-engine` / `retroplug-mqjs-host` / `retroplug-mqjs-host-device` / `retroplug-mqjs-run` / `retroplug-mqjs-run-gc` / `retroplug-mqjs-engine-gc` / `retroplug-mqjs-tables-32` | executable / static lib / object lib / custom target | MicroQuickJS ([deps/mquickjs](../deps/mquickjs), vendored, [cmake/mquickjs.cmake](../cmake/mquickjs.cmake)), upstream's files unpatched with their LICENSE: its stdlib table generator and the `mqjs` compiler (the only bytecode compiler), the engine wrapped by `rp_mqjs_engine.c` and renamed `rp_mqjs_*` (a post-build check refuses any other external name), the script host ([packages/graph-runtime/native/src/script](../packages/graph-runtime/native/src/script)) in the desktop's build and `RP_DEVICE` mode, the toolchain's runner over RetroPlug's own stdlib, and the same runner over an engine built to collect at every allocation (`retroplug-mqjs-engine-gc`, `EXCLUDE_FROM_ALL`, which `retroplug-device-mode-test` needs), and the 32-bit stdlib tables the box's bytecode is compiled against (`retroplug-mqjs-tables-32`, which `retroplug-export-plan-current` builds). `mqjs_stdlib`, `mqjs` and the runners are `EXCLUDE_FROM_ALL` and built on demand by the script packs' targets. | yes: `retroplug-mqjs-host-device` directly, the engine and host via every product; the tools and the `-gc` pair no |
| `retroplug-graph-exec-device` / `retroplug-box-usb-cdc` | object lib | The executor and the plan loader compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` in the default build (a non-portable loader line fails there), and `UsbCdcTransport.cpp` over a fake TinyUSB for the desktop test (`EXCLUDE_FROM_ALL`). Both on the MSVC `/W4 /WX` list. | no |
| `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
| `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
| `retroplug-r8brain-base` | static lib | r8brain's static members, one vendored translation unit (`R8bBase.cpp` includes `r8bbase.cpp`) compiled on a throwing allocator and linked into `retroplug-dsp-resample`. Kept off RetroPlug's `/W4 /WX` (vendored code keeps the compiler's default level) and given the deps' `-w` on GNU/Clang. | yes (via `retroplug-dsp-resample`) |
| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in the `dsp` package's [CMakeLists.txt](../packages/dsp/native/CMakeLists.txt), so `retroplug-graph` carries neither. `retroplug-dsp-nodes` and `retroplug-core-nodes` (the `rp.core` package, [packages/core](../packages/core/native/CMakeLists.txt)) each also get a `<target>-device` object library in `ALL` (the sources compiled with `RP_DEVICE`, nothing linked; on the `/W4 /WX` list) that fails the desktop build for an unguarded desktop hook. | yes (via `retroplug-backend`) |
| `retroplug-backend` | static lib | The shell: the Engine and its graph nodes, the RPC services, `SnapshotRegistry`, the offline job pool, `SampleCache` and `FileSync`, lodepng, the script host with its embedded script-node bundle, and the render host on bare QuickJS. It links `retroplug-graph` (the graph executor and planner, [packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt), over the node SDK in [packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)) and `qjs`; a product adds the console packages it lists. Composed by every host; it links neither txiki nor efsw, and it names no console. | yes (via consumers) |
| `retroplug-tjs-runtime` | static lib | The shared txiki host ([dpf.js](../deps/dpf.js)'s `TjsHostRuntime.cpp`), PUBLIC-linked to `tjs`. Linked by the hosts that instantiate it (the plugin, the SDL standalone, the test host, the CLI, `retroplug-classid-test`) and by nothing that only needs the engine. | yes (via those hosts) |
| `retroplug-file-watcher` | static lib | `NativeFileWatcher` over `efsw-static`, implementing the `FileWatcher` interface `HostRpcService` is handed. Only the plugin builds one (and `retroplug-watcher-test` links the library). | yes (via the plugin) |
| `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
| `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
| `retroplug-audio` / `retroplug-midi` | static lib | The F5b packages' native halves ([packages/audio](../packages/audio/native/CMakeLists.txt), [packages/midi](../packages/midi/native/CMakeLists.txt)): the audio backend contract (`AudioBackend` / `AudioStream`), the null device and `AudioHost`; and the MIDI backend contract, RtMidi (`systemMidi()`), `FakeMidiBackend`, the one copy of the port filters and `MidiStream`. `retroplug-midi` links `rtmidi` PRIVATE. Both are UNLISTED library targets (not manifest `native` entries), linked by name: a product that lists the package without naming it links neither (the audio backends likewise, below). `retroplug-midi-facet` is the midi package's provider (`MidiRpcService` + `rp_midi_provider` + `rp::midi::registerRpc`), started by the products whose `PROVIDERS` name `midi` (the CLI and the headless host). | yes (via the products that link them) |
| `retroplug-audio-portaudio` / `retroplug-audio-sdl` | static lib | The audio package's two real backends ([packages/audio-portaudio](../packages/audio-portaudio/native/CMakeLists.txt), [packages/audio-sdl](../packages/audio-sdl/native/CMakeLists.txt)): PortAudio (with ALSA on Linux, and the log quietening that needs it), and SDL2 audio. Each is a manifest `audioBackend` item, so a product names it in `AUDIO_BACKENDS`; `retroplug-sdl` names `portaudio sdl`, and the CLI, the headless host, `retroplug-render-host-test`, `retroplug-ui-test` and the Node addon name none and link neither (the kit's `audiolink` leg checks the binaries' symbols). PortAudio is added at root scope by [cmake/portaudio.cmake](../cmake/portaudio.cmake), not by a package. | yes (via `retroplug-sdl`) |
| `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)), `PROVIDERS n8 launchpad` and `AUDIO_BACKENDS portaudio sdl`. Shipped in the CI artifact, deliberately **not** in releases. | yes |
| `retroplug-cli` | executable | `build/bin/retroplug-cli` — the CLI runner + `render` and `test` subcommands ([09-cli-debugging.md](09-cli-debugging.md)). | yes |
| `retroplug-host` | executable | `build/bin/retroplug-host` — the real-Backend test host that evals a TS bundle over `__rpcSend`. | yes |
| `retroplug-ui-test` | executable | `build/bin/retroplug-ui-test` — boots the real React UI on a headless software LVGL display via `UiHarness`. | yes |
| `retroplug-node` | shared module | The N-API addon — the same backend behind `require()`. Gated on `RETROPLUG_NODE_ADDON=ON` (needs `node_api.h`). | opt-in |
| `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
| `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
| `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` / `-audio-host-test` | executable | The fourteen Catch2 binaries `pnpm test:plugin` builds and runs. Each is made by `retroplug_add_test_binary` ([cmake/test-binary.cmake](../cmake/test-binary.cmake)) and links `Catch2::Catch2WithMain`. `EXCLUDE_FROM_ALL`; the console packages', the midi package's and the launchpad pack's are declared in their own `native/CMakeLists.txt` (`retroplug-midi-test` holds `MidiStream` over `FakeMidiBackend`, the port filters and the ring; `retroplug-audio-host-test` holds `AudioHost` over `FakeAudioBackend` and the run-override rule). | no |
| `retroplug-n8-test` / `-serial-test` / `-n8-hwtest` | executable | The Everdrive N8 pack's protocol and serial checks (`packages/n8/native/test/`), the first two `pnpm test:n8`'s; `-hwtest` needs the physical console and links no Catch2 (`NO_CATCH2`). `EXCLUDE_FROM_ALL`. | no |

The plugin's identity is vendor-owned (not the DPF example namespace):
`DISTRHO_PLUGIN_NAME "RetroPlug"`, URI `https://retroplug.io`, CLAP id
`net.tommitytom.retroplug`, all in
[DistrhoPluginInfo.h](../packages/native/plugin/DistrhoPluginInfo.h). It exposes 8 outputs — four
stereo pairs `out_1..4` ([10-multichannel-audio-out.md](10-multichannel-audio-out.md)).

### Not yet built / deferred

The build declares `clap`, `vst3`, `vst2`, `au`, and `jack` (the `dpf_add_plugin` call in
[packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt)); AU is gated by DPF to macOS. Only
**LV2** is not built — its out-of-process DSP/UI split doesn't fit RetroPlug
([07-remaining-work.md](07-remaining-work.md#deferred)).

## The packages

Every package under `packages/` is a workspace package: `@retroplug/<name>`, `private`, type `module`, a
`package.json` whose `retroplug` member carries the contributions its language builds, and (where it has TypeScript) a
`ts/` tree with a `tsconfig.json` that extends [tsconfig.base.json](../tsconfig.base.json). The members are `native`
(a C++ target and the `extern "C"` entry a product registers it by, [cmake/packages.cmake](../cmake/packages.cmake)'s;
`system`'s names a target and no entry: a contract, not a node set), `script` (a device script pack),
`app` (`ts/app.ts`, the module default-exporting `definePackage({ roles, romProviders, trackerIntegrations,
kitFormats, embeds, projectSections, projectServices, configSections, controllerApps })`: core's contract declares
`roles`, `romProviders`, `embeds`, `projectSections` and `configSections`, and the declaring packages' augmentations
the rest, dsp's `kitFormats`, tracker's `trackerIntegrations` and `projectServices`, the launchpad kind's
`controllerApps`) and `protocol` (`ts/protocol.ts`, the module default-exporting the package's
`DefinedNode`s, beside each node's `type` and span `label` and the role kinds it places; a package with no node names
no module and lists only `apps`, the runtime halves of the controller apps whose control-plane halves its `app` module
contributes). A package may have any combination; a manifest naming no member at all is refused at configure for a
package CMake registers through `retroplug_package` (the product list's, the SDK's, a device build's extras). `tracker`
carries no `retroplug` member and `testing` only a `native` target with no entry (`retroplug-testing`, the test
helpers' headers), so both are added as libraries only: tracker is a module library no product lists, and testing is a
devDependency of every package with tests. The mechanism's design is the F1 design's
([modular-graph-phase-f1-design.md](../docs/modular-graph-phase-f1-design.md)).

**A product starts a pack's provider only when it names it.** A manifest entry with a `provider` (and, where the pack
has a facet, an `rpc`) is a device service the pack's desktop target holds, and `retroplug_product(<target> PACKAGES ...
PROVIDERS <package>...)` is the product's half: the packages whose provider it starts, in the order it names them
(started in that order, stopped in the reverse, their facets mounted in it), generating `rp_product_providers.hpp`
(`rp_start_providers`, `rp_restore_providers`, `rp_stop_providers`, `rp_wake_providers`, `rp_register_package_rpc`). The
list is EXPLICIT and it is the product's own, not its package order: the package list is `core launchpad ... n8 ...` and
every product that has a control surface names `n8 launchpad`. The product calls `rp_wake_providers` once per frame;
that is the pump that writes what the audio thread queued, so a provider that paints is silent without it. The default is
NONE, because a provider opens ports and runs threads, so the package list alone adds node types and links no port
library; a duplicate is refused, and an item may not carry both `entry` and `provider` (a product that listed the package
without naming it would silently lose its node type). Today `retroplug-sdl` names `n8 launchpad` (it uses
`MidiStream` itself), the CLI and the headless host name `PROVIDERS n8 launchpad midi` (they mount the stream's facet
as a provider), the plugin names `n8` alone (its MIDI seam caps a message at 4 bytes, so no SysEx fits), and
`plugin.cmake` refuses, at configure, a plugin target linked to `retroplug-launchpad-desktop`. A test binary that is a
product takes the same argument.

**The registries are generated, not written.**
[packages/retroplug/scripts/lib/packages.mjs](../packages/retroplug/scripts/lib/packages.mjs) writes the shell's
`src/generated/{packages,consoles,protocol,scriptNodes,placers}.ts` from a product list (`retroplug.products.<name>` in
the root [package.json](../package.json)): `packages.ts` concatenates each package's `app` contribution, `consoles.ts`
each package's `retroplug.consoles` module (its console descriptors and the physical carts one of them can drive, kept
apart from the app because the planner reads them and must carry no role schema), `protocol.ts` is the table
`buildGraph` and the stores wire a role through (and the contribution kinds' data), `scriptNodes.ts` is the script-node
bundle's entry, in span-label order, `placers.ts` each package's `retroplug.protocol.placer` module,
`ui/generated/menus.ts` is each package's `retroplug.ui` module in product order (its `defineUi({ rows, menus, trackers,
services, actions })`: the menu contributions, the UI services and the app actions the key maps dispatch, imported only
by the UI), `ui/generated/views.ts` each package's `retroplug.views` module (the views, imported only by the shell's
view slots), and `cli/generated/tools.ts` is each package's `retroplug.cli` module in product order (the CLI tools,
imported only by the dispatcher). **Every product but the desktop gets its own tree**, and its own bundles (the four
embedded bundles: the control plane, the React UI, the script nodes and the render worker): `generate({ product })` (the
CLI's `--product` with `--build-dir`, or `--products-dir` for the typecheck's tree) writes
`build/products/<name>/packages/retroplug/{src,ui}/generated/`, in the shell's shape, with a `tsconfig.json` over it
that `pnpm typecheck` runs for every non-desktop list (into `packages/retroplug/.products/`, so the typecheck creates no
`build/` for a build to mistake for a configured tree); no product but the desktop has a CLI tree. The shell's sources
read their `../generated/*` modules through esbuild's `onResolve`
([tools/product-generated.js](../tools/product-generated.js)): a bundle built for a product answers those relative
imports with the product's file. A product list is validated as the desktop's is, and it must be a SUBSEQUENCE of the
desktop's, in its relative order; its script-node span labels are its own positions (the plugin numbers
`rp.lsdj.serial-out` 22, where the desktop's Launchpad session holds 22 and the decoder 23). `desktop` is the union of
the lists: its 21 packages hold the plugin's 15. The desktop's run also writes the typecheck paths:
`tsconfig.host-modules.json` (each host module's declaration file, which a package with a `retroplug.host` declaration
extends) and `tsconfig.paths.json` (the same entries plus the shell's own aliases, from `tsconfig.aliases.json`: a
child's `paths` REPLACE its parent's, so the shell's program needs one file carrying both). The product list is the
**dependency order and the registration order both** (a package before the ones that use it, the shell last), and only a
ROM provider's place in it is behaviour, and a tracker integration's for a system storing two node-less markers ([a
known bug](07-remaining-work.md#a-tracker-marker-with-no-protocol-node)): the providers run in that order, and the first
to match a ROM fixes a system's role-list order. A role's kind, a kit format's id and a tracker integration's marker
role are otherwise looked up rather than sequenced (one package's roles interleave another's without consequence), so
for those a duplicate is the hazard: the generated module refuses a role kind, a kit format id or a marker role declared
twice, each with its own message. The generator itself refuses a list that puts a package before a dependency, a
dependency the product does not carry whose contribution would be lost, a dependency on the shell, a duplicate product
entry, a node type or span label declared twice, a label that is below 16 or leaves a gap, a role naming a node no
package wires, a manifest that names a missing module or one its `exports` do not publish, and a contributing package
the shell does not declare. A package's `retroplug.embed` entry (the bytes a product bakes in) is paired at load with
the `embeds` declaration its **app module** makes (`{ id, label, platform }`): the generated module refuses a manifest
entry no app module declares, a declaration no manifest bakes in (or one whose halves are in two packages), an id
declared twice and a platform no console registers, each naming the package(s). A protocol node whose core must CAPTURE
its serial out declares, in the manifest, what the core's capture should do in each of the cart's sync modes: `capture:
{ "midiOut": "drive", "masterSync": "listen", ... }`, each value `"off"`, `"listen"` or `"drive"`. The generator refuses
a capture that is not an object of mode: capture, a value that is none of the three, and a capture on a node that is not
wired (no `midiIn` and no `writes`), on a node with no `out`, or on one whose `out` no package declares or declares no
`serialOut`: a capture whose bytes nothing decodes would clock a core for nothing. The generated modules and both path
configs are gitignored, and `retroplug-ts-packages` writes them only when their content changes. Seven targets depend on
it, so they exist before the first esbuild run of a fresh checkout: the control-plane, UI, script, render-worker and CLI
bundles, the example sessions and the CLI SDK assets. The root `pnpm typecheck` script runs the generator before any
package's `tsc`, so a checkout with no generated file typechecks (CI's order is install, typecheck, build). A package
that lists examples, or declares a host module the UI's surface owns, must have a `tsconfig.json` whose parsed `extends`
names `../<shell>/tsconfig.host-modules.json` (the generator refuses one without: the shell's program compiles
`../*/examples`, and a `.tsx` resolves `react` and `react/jsx-runtime` only through those paths; naming the file in a
comment is not extending it); a package with `retroplug.host` declares each host module it imports, and the generator
and rule 3 both hold the declaration to the one shell-owned list,
[host-modules.json](../packages/retroplug/host-modules.json): five modules over two surfaces, `retroplug-cli` for `cli`
and `react`, `react/jsx-runtime`, `lvgljs` and `lvgljs-ui` for `ui`. That list's `surface` is enforced too: a host
module is importable only from the files its surface owns, which is the package's `examples/` and `ts/cli/` for `cli`
and its `ts/ui/` for `ui`, and rule 3 refuses one imported from an app, protocol, console or test file, saying which
surface the module belongs to. Rule 3's other half is where a package's React may live: a `.tsx` is admitted only under
the package's `ts/ui/` tree, and automatic JSX makes it a use of `react/jsx-runtime` with no specifier, so a package
with one declares that module.

**The host contract.** The three TypeScript surfaces build a host for their hooks and tools from
[core/ts/host.ts](../packages/core/ts/host.ts): `Host<G>` is an object of named groups (`files`, `png`, `kits`,
`systems`, `songs`, `liveSongs`, `emulator`, `dialogs`, `devices`, `process`, `views`, `project`, `config`), and a
hook or a tool names the groups it uses. The UI's host offers ten of them, the CLI's six (`files`, `png`, `kits`,
`devices`, `process`, `songs`; `cli/cliHost.ts` builds it per tool run), and the render worker's two (`emulator`,
`files`); asking a surface for a group it lacks is a `tsc` error, which is what makes a surface's abilities its type.
`packages/ui/ts` holds the menu model and the contribution types the UI and the package menus share
(`@retroplug/ui/menu`, `@retroplug/ui/contribution`).

**Fixtures are a package's, published for tests by name.** A cart's synthetic ROM images live in its `ts/fixtures.ts`
and a manifest that publishes them names the subpath `./testFixtures`, never `./fixtures`: the name says what it is,
and the boundary check still sees the file as the package's source (it is outside `ts/test`, so it may use no
`devDependencies`). The shell's own tests ([test/systems/fixtures.ts](../packages/retroplug/test/systems/fixtures.ts))
re-export the ones they share, so a store or menu test keeps one import.

**The shell imports contracts, not carts (rule 9).** The boundary check judges the app shell's three own trees, `src/`,
`cli/` and `ui/`, each against an explicit list of the `@retroplug/*` specifiers it may import, each entry with the
reason it is a contract rather than a cart. `src/` and `cli/` share `SHELL_IMPORTS`: core's contract modules
(`systemRoles`, `consoles`, `graphDocument`, `midiRouting`, `romParameters` and the rest), the tracker contract
(`songCatalog`, `assetCatalog`, `trackerIntegration`, `projectService`, `trackerRender`, `workingSongBlob` and its
package-contributions shape), dsp's kit contract, WAV codec and package-contributions shape, and the test harness; no
controller module, since the shell names no controller. `ui/` has a list of its own, `UI_IMPORTS`, because the kit
and the view contract are the UI's alone (no `src/` or `cli/` file may name `@retroplug/ui-react`) and the control
plane needs no menu model or contribution type. A cart's
or a console's own module is reached through the generated registries, which is why the four doors that used to
re-export carts (`src/kit`, `src/tracker/index.ts`, `trackerRegistry`'s integration re-exports and `audioDriver`'s spec
re-exports) are gone: an importer names the owning package's subpath (`@retroplug/lsdj/kitFormat`, `@retroplug/dsp`,
`@retroplug/smsggdj/smsggdjIntegration`), and the shell's own cart-named entry points are lookups in the registries
(`SystemsStore.loadEmbedded(id)`, `AudioDriver.compileKitFormat(id, spec)`). A relative import is judged by where it
lands: a `src/` file stays in `src/`, a `cli/` file in `cli/` or `src/` (the CLI is a surface above the control plane),
and a `ui/` file in `ui/` or `src/` (the UI is a surface over the stores). An allowlist entry no import of its root
uses fails the check, and a root's `generated/` tree is not judged (it is the one place that names every package).

**The two boundary checks.** `pnpm check:boundaries`
([check-boundaries.mjs](../packages/retroplug/scripts/check-boundaries.mjs)) judges every file under a package's
`ts/` tree, tests included, by TypeScript's own scanner: a relative import that leaves the package, an `@retroplug/x`
or third-party module the manifest does not declare (a test may use `devDependencies`), any dependency on a shell
(`@retroplug/retroplug` or `@retroplug/native`), a cycle in the declared dependencies, an `#include` that climbs out
of the package, an `exports` entry that publishes a test module or names no file, and a `workspace:`/`npm:`/`link:`
dependency whose spec names a shell or a package the workspace does not have (an alias of a package that exists is
refused too: every other rule reads the key). A declared dependency no file uses is a warning, not a refusal. The
native twin is `cmake/packages.cmake`'s: a package's targets may link, include and search only declared packages'
trees, by target, by a library path, or through an include flag (`-I`, `-iquote`, `-idirafter`, `-isystem`,
`/external:I`, a `SHELL:` list), a link option (`-L`, `-l`) or a compile definition whose value is a path (a test
binary that opens a package's file, say its embedded ROM, reaches that package's tree though it links nothing of it),
and the configure refuses anything else. It reads the TARGET's own properties, never a CMakeLists' text, so a path a
comment names is no reach; the TS check, which has no target to read, counts a declaration as used from the file's text
with its comments stripped, and only to raise or silence a warning. The TS check's allowlists are rule 9's
`SHELL_IMPORTS` and `UI_IMPORTS` (the shell's contract modules per root, each with the reason it is one); otherwise
neither check excuses a reach (`EXCEPTIONS` is empty, and the native check has no exception at all). Each has its own
fixtures ([test/boundaries](../packages/retroplug/test/boundaries); `packages/native/test/packages`) that fail one rule
each; the TS check's rule 9 (the shell's src, cli and ui may import only their own list's contract modules) has a
shell tree per answer: one per root with a cart import, one per root with only contract imports beside a generated one,
and a tree whose UI file names a contract module only the shell's own list carries. They guard against accident and
drift, not a determined author: each checker's header lists what it deliberately does not cover. `pnpm test` runs the
TS check before it bundles a single test file, so a boundary a change broke is named as such rather than surfacing as a
bundler error.

**The shell's sources hold no cart or console literal.** A test
([test/source-literals](../packages/retroplug/test/source-literals)) walks `packages/retroplug/src` (not
`src/generated/`) with TypeScript's own syntax tree and fails on a literal that is a platform id (`"gb"`, `"nes"`,
...), a ROM extension (`".gb"`, `"*.gba"`, ...), a console's core id or its node type: a quoted string, a template
with no substitution and each span of a template that has one are read, as is the name a table's key is written with
(an object literal's property, a shorthand, a method, an accessor, an interface member, an enum member; a property
access such as `x.gb` is not, since the declaration is what is judged), and text that ends in a ROM extension (the
`rom.gb` tail; a string with whitespace in it is prose, not a path, so a help text naming a file is not a hit), and a
regular expression whose source spells an escaped dot and matches `rom<ext>` but not the extension's bare letters (so
`/\.gbc?$/i` is a hit and `/^[\x20-\x7e]*$/` is not). A comment is trivia and a `${...}` cannot move the walk's
place. The ids, extensions, cores and node types are the console registry's, read from the generated `CONSOLES` list,
and the cart ids (an embed's, a tracker integration's and a kit format's) from the generated package list, so a
console or a cart a product adds is guarded without an edit to the test, and its knowledge stays its package's. The
exemptions are the frozen v2 to v3 step's four package role-kind keys, which may not read the registry (decision 13).
`cli/` and `ui/` are scanned too, each skipping its own `generated/` tree, and the SDK's declaration file. What it
cannot see: a
device package's ids and node types (an `n8`, a `launchpad`, an `rp.n8.*`), a non-console role kind, any id inside a
string with whitespace in it, and the SDK's declaration file, which it skips. Like rule 9 it catches an accident rather
than a determined author (a computed `["g", "b"].join("")`, a computed table key and a dotless pattern such as
`/(gb|gbc)$/` are invisible to it).

**A bundle check keeps the surfaces apart.**
[test/script/bundle-ui-split.test.ts](../packages/retroplug/test/script/bundle-ui-split.test.ts) holds the control
plane, the render worker, the script nodes, the SDK, the dispatcher and the planner to carrying no UI (no `ui` package
module, no package's `ts/ui/`, no `ui/generated`), and the UI to carrying no CLI (no package's `ts/cli/`, no
`core/cliTool.ts`, no `cli/generated`). The generated modules are split so that this can hold, the menus and views
imported by the UI alone and the tools by the dispatcher alone, and the bundle check keeps that true.

**A package's tests are its own.** A test whose subject is wholly in a package lives in its `ts/test/`, runs on the
package's `tsconfig` and keeps its fixtures and goldens beside it
([packages/lsdj/ts/test/golden](../packages/lsdj/ts/test/golden) and the `.sav` corpus under `ts/test/savs`,
[packages/risa/ts/test/golden](../packages/risa/ts/test/golden)); the runner's slug for one is `<package>/<path>`
(`lsdj/sav`, `risa/record`, `bliptoaster/rom`, `smsggdj/rle`, `n8/n8MidiToFifo`), so a filter's spelling follows the
move. Tests of the stores, the menus, `buildGraph`, the engine, `test-native` and `test-ui` stay with the shell, as do
the ones that drive its registry (`test/system-settings/*`, `test/risa/detect`, `test/bliptoaster/romDetect`,
`test/dsp/sms-sync`), whose subjects are the shell's provider dispatch and generated lists. The skip baseline
([scripts/skip-baseline.json](../packages/retroplug/scripts/skip-baseline.json)) records one ts-tier file,
`lsdj/ui-views` (one of its five cases skips), so moving a file that has an entry (today only that one) moves its entry
to the new slug.

**The shared test helpers ([packages/testing/native](../packages/testing/native)).** A package's tests are its own, but
some things two packages' tests must agree on are neither's: the TAP harness, the script-node rig and the byte helpers
are `@retroplug/testing`'s TypeScript, and its native half is two targets. `retroplug-testing` is an INTERFACE library
holding the headers: `graph/CountingAllocator.hpp` and `LinkRomModel.hpp` (BlipToaster's link ROM's rules, driven from
both sides of the N8/Mesen edge), with the package's `src/` as their include root. `retroplug-testing-allocator` is the
OBJECT library holding `graph/CountingAllocator.cpp`, a replaced global `operator new` (and on glibc
`malloc`/`calloc`/`realloc`/`free`) that the no-allocation cases count through; it links the header library PUBLIC, so a
test binary that needs both links the allocator alone. A test binary links one through its package's `devDependency` on
`@retroplug/testing`; nothing else may. A test that only needs a header links `retroplug-testing` and does not run under
a replaced allocator (`retroplug-n8-test` is one: it includes `LinkRomModel.hpp` and nothing else). The configure refuses
a target that reaches either without `RP_TEST_BINARY` (`retroplug_check_test_helpers`, beside `retroplug_check_products`),
since the allocator would count production allocations and a library that linked it would hand it to its consumers, and
the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by name) need it.

**Adding a package.** In the order a configure demands it:

1. The directory's name is the package's name: `packages/<name>` is `@retroplug/<name>`.
2. `package.json`: `private`, `type: "module"`, the `retroplug` member(s) it contributes (`native`, `script`, `app`,
   `consoles`, `protocol`, `embed`, `ui`, `views`, `kinds`, `cli`, `examples`, `host`), an `exports` map of explicit
   entries with no wildcard (each entry is public deliberately), and a `typecheck` script
   (`tsc -p tsconfig.json`) when it has TypeScript. A **console package** adds a `consoles` module default-exporting its
   console descriptors (a platform id, a node type, the extensions it can be recognised by, its detectors in rank order,
   its setting/construct fields and params, its split modes, its link cable, its real-cart option) and any physical cart
   it can drive; the generator refuses a duplicate platform, node type or real cart, a descriptor whose core has no
   registered system role, and a real cart naming a platform no descriptor registers. A **tracker package** adds a
   `trackerIntegrations` entry to its app module carrying the cart hooks the shell reaches through it
   ([trackerRender.ts](../packages/tracker/ts/trackerRender.ts),
   [trackerPlayback.ts](../packages/tracker/ts/trackerPlayback.ts) and
   [trackerSongImport.ts](../packages/tracker/ts/trackerSongImport.ts), beside its `sramSignature`) and the role hooks
   its role types declare ([systemRoles.ts](../packages/core/ts/systemRoles.ts): `parameters`, `deriveConfig`,
   `hostLatencyMs`); its manifest `capture` keys are its own sync-mode spellings, held by its own test as `lsdj`'s is. A
   package that bakes bytes in
   declares `retroplug.embed` AND the app module's matching `embeds` entry (its id, label and platform), as `mgb` does. `embed` carries a file inside the package into every product that lists
   it, for `putResource`'s `embedded` field: `[{ "id": "mgb", "file": "rom/mGB.gb" }]`, the id a C identifier (it
   names the generated array) and the file inside the package. Use it for an asset the binary must run without a file
   on disk and that belongs to one package - the mGB synth's ROM is F2's one
   ([mgb](../packages/mgb/package.json) has no native code, the ROM is all it gives a product, and a product gets its
   bytes in the generated `rp_embedded_resources()`). A ROM a menu opens is TS's path, not an embed. `typescript` is a
   devDependency for that script, and `zod` a dependency where it is imported. A package's `dependencies` are what its
   library links; a TEST may use its `devDependencies` as well, which is how a test binary links `@retroplug/testing`'s
   native targets and reaches a fixture another package owns (sameboy's test binary opens the ROM the `mgb`
   package embeds, by the path its manifest gives, so sameboy declares `@retroplug/mgb` a devDependency). The native
   boundary check holds that: a test target may link, include or define a path into a devDependency's tree, and a
   library target may not. A dev-only cycle (mgb -> sameboy -> mgb through that devDependency) is allowed, as pnpm's
   `ignoreWorkspaceCycles` is; a `dependencies` cycle is refused.
3. The shell declares it: add it to [packages/retroplug/package.json](../packages/retroplug/package.json)'s
   `dependencies` as `workspace:*` and run a plain `pnpm install` (not `--frozen-lockfile`); the configure refuses a
   contributing package the shell does not declare.
4. Its place in `retroplug.products.desktop` (the root [package.json](../package.json)) is after its dependencies: the
   list is the dependency order and the registration order both, and the generator refuses a package listed before one
   it depends on. A product that differs from the desktop names its own list (`retroplug.products.<name>`), which is a
   subsequence of the desktop's in the same order, and `retroplug_product(<product> LIST <name> ...)` is how that
   product carries it. A package a product should carry must therefore be in that product's list too: the plugin's
   `plugin` list is the desktop's without `launchpad`, `lsdj-launchpad`, `midi` and the three audio packages
   (`audio`, `audio-portaudio`, `audio-sdl`), and that is what makes the package split a fact about a build. Only a ROM provider's place is behaviour. A node type's place in the list is what a product's
   `rp_register_packages()` holds, so moving a package moves its types in the registration golden
   (`test/engine-graph/golden/product-registrations.json`), which is re-recorded for an intended move alone.
   **Audio backends are a second, narrower list.** A package may declare a manifest `audioBackend` item -
   `{ "target": "retroplug-audio-portaudio", "audioBackend": "rp_portaudio_backend", "id": "portaudio",
   "device": false }`, a C++ factory returning `std::unique_ptr<rp::audio::AudioBackend>` rather than an `extern "C"`
   provider - and a product links it only when its `retroplug_product(... AUDIO_BACKENDS portaudio sdl)` names the id,
   in preference order (Auto takes the first whose auto driver opens). A product that lists the package and names no
   backend drops the target, which is why the CLI, the headless host and the Node addon link neither PortAudio nor SDL
   audio; `retroplug-sdl` names both, so CI's `all` compiles both on every platform. The configure refuses a backend id
   no manifest declares, one named twice, one whose package the product does not list, an `audioBackend` item that is
   also an `entry` or a `provider`, and one that is `device: true`. The generated
   `rp_product_audio_backends.hpp` declares each named factory and defines `rp::audio::BackendList
   rp_product_audio_backends()`; only the unit that constructs the host includes it. `tools/check-audio-linkage.sh`
   (the kit's `audiolink` leg) holds the binaries to it: no `Pa_*` symbol and no audio-library dependency in
   `retroplug-cli`, `retroplug-host` or the Node addon.

   **What a backend author needs** ([AudioBackend.hpp](../packages/audio/native/src/audio/AudioBackend.hpp) is the
   contract and its comments are the rules). The item is refused without an `id` or a `device` value, with an id a
   manifest already declares (or `auto` / `null`, which are the host's own), beside an `entry` or a `provider`, or with
   `device: true`. The factory's `AudioBackend::id()` must return the manifest's id, because that is what a saved choice
   is matched to, and the target is linked into the products that NAME the id, so the factory needs no export macro. The
   backend is constructed at product start but must initialise NO library there: `drivers`, `devices` and `autoDriver`
   are the first use, and the host calls them only for the backend in force (or on an explicit "More drivers..." step),
   which is what keeps a run that never chose it from opening a device. `drivers` / `devices` must never disturb a
   running stream. `open` returns a stream that is NOT started; `stop` joins the callback (idempotent, with
   `stopWithin(bound)` for a backend whose stop could wait), `pull` never runs on two threads at once, and `lost()` /
   `info()` are read from the control thread beside the callback. The log line the harnesses grep
   (`audio: <label> [<driver label>] '<device>' N Hz, B frames, C ch`) is the host's, so `label()`, `driverLabel()` and
   `StreamInfo` are what a leg reads. `AUDIO_BACKENDS` in
   [packages/audio/ts/configSection.ts](../packages/audio/ts/configSection.ts) is the product's list for a doc or a
   test, NOT a set a saved `audio.backend` is validated against: the section keeps any non-empty id, because native is
   what knows the set.

   **A package that uses MIDI** takes its own backend rather than a shared stream: link the unlisted `retroplug-midi`
   library by name (as `launchpad` and the shell's facet do) and declare `@retroplug/midi` in its `dependencies`; take
   an instance through `rtMidiBackend("<client>")` in
   [RtMidiBackend.hpp](../packages/midi/native/src/midi/RtMidiBackend.hpp) (never construct RtMidi itself, and keep
   "RetroPlug" in the client name: the port filters judge a listing by that FAMILY, so a stream's own and every sibling
   RetroPlug's ports stay out of its "all devices"); filter through
   [PortFilter.hpp](../packages/midi/native/src/midi/PortFilter.hpp)'s one copy, which honours the test-only
   `RP_MIDI_PORT_ALLOW`; keep a pair a control surface holds out of the shared stream through the provider ABI's
   `reserve_midi`; write a blocking send (WinMM's bulk SysEx, ~170 ms) on a thread of its own, never from the audio
   callback; and test over
   [FakeMidiBackend.hpp](../packages/midi/native/test-support/midi/FakeMidiBackend.hpp), which is test support, so a
   test binary adds `retroplug-midi-test-support` and no product does. Its rows live in its `ts/ui/` and read the host's
   `midiDevice` group when the product owns a stream.
5. The four members through which a package owns part of a project and of the graph. `retroplug.app` may declare
   `projectSections`: a block of a project's `settings`, as `{ key, schema, defaults, version?, migrations?, merge? }`.
   The key is the block's name under `settings`, and it is never one of the shell's own core keys
   (`layout`, `midiRouting`, `audioRouting`, `zoom`), never two packages' (the generated registry refuses both), and
   `__proto__` is refused by the writer. `schema.parse` is the raw value's one door (defaults filled, junk clamped,
   never a throw), `defaults()` is what a fresh project reads, an optional `version` stamps the block (an unstamped one
   reads as version 1) with `migrations` keyed by FROM-version, and `merge` is how a patch lands (a shallow merge by
   default; it may throw to refuse one). The UI reaches a section through `host.project.section(key)` and
   `host.project.setSection(key, patch)`; a block no package contributes is kept verbatim and written back, which is
   what a product without the pack reads. `projectServices` is the section's control-plane half: a per-store factory
   (`{ key, create() }`) the store builds one instance of, whose `poll` the editor's watch drives, whose `graphModel`
   the store publishes into `GraphModel.models` and whose `state()` a UI reads through `host.project.serviceState(key)`.
   `projectServices` and the service context are typed by `@retroplug/tracker`'s augmentation of
   `PackageContributions`, so an app module that declares one imports `@retroplug/tracker/packageContributions` for the
   type. The manifest's `protocol.placer` names a module default-exporting `definePlacer({ key, normalize, place })`,
   which buildGraph calls at the slots it holds; its `key` is the `projectServices` factory's key whose `graphModel` is
   the model it places (the generated registry refuses a service or placer key that is one of the shell's own or that
   names no section). A `placer`, a kind's `control` module and a kind item's control-plane half are CONTROL-PLANE
   code: they live in the package's `ts/control/`, and they may read no node module and no zod beyond what the planner's
   bundle check allows. A device pack's scenario leaves that tree out only when its `app` entry IS `ts/control/index.ts`:
   the scanner skips the `app`, `ui` and `cli` modules and the directory of any of them named `index` (item 10), so with
   `app: ts/app.ts` a `ts/control/` sibling is scanned like any other module of the core.

   **A config section** is the same shape at `config.json`'s root: `retroplug.app` may declare `configSections`, the
   same `{ key, schema, defaults, version?, migrations?, merge? }` with the same stamp rules and the same
   `section` / `setSection` group on the host (`host.config`). Its `key` is a ROOT key of config.json, never one of the
   shell's own (`CORE_CONFIG_KEYS`, derived from `userConfigSchema`'s shape plus `schemaVersion`) and never two
   packages' (the generated registry refuses both, and `core/ts/host.ts`'s `ConfigHost` doc states the file rules). Two
   differences from a project section, both because the file is shared by builds of different ages: an absent key reads
   the defaults but is NOT written until `setSection`, and a block this session did not set is written back VERBATIM, so
   an additive field a newer build wrote survives. A section's parse should therefore drop nothing it cannot keep: the
   `midi` section keeps each entry's unknown fields (`{ name, ...rest }`) for exactly that reason. The audio package's
   `audio` section and the Launchpad pack's `controlSurface` are the two in tree, and `test/config/config-sections.test.ts`
   holds the rules (a core key refused, a duplicate refused, refuse-newer over the re-read file, a verbatim block, a
   session-set block, the core-key list against the generator's).
6. `tsconfig.json` extends [tsconfig.base.json](../tsconfig.base.json) in two lines, so the package typechecks alone.
   A package with `retroplug.ui` exports the module it names (the shell imports it through the generated menus), one
   with `retroplug.views` exports `./views` (the generated view table imports it there), one with `retroplug.cli`
   exports its tools module (default-exporting `defineCliTools`), one with `retroplug.examples`
   keeps each entry under `examples/` and adds the generated `../retroplug/tsconfig.host-modules.json` to its
   `extends`, and one with `retroplug.host` declares every host module it imports (one of the shell's five:
   `retroplug-cli` for the `cli` surface, or a React module for `ui`). The
   generator refuses a missing module, a duplicate tool name or example name, an example outside `examples/`, a host
   declaration the shell's list lacks, and a missing `extends` on a package that lists examples or declares a UI host
   module. Every module a manifest names must be PUBLISHED: the generated imports name a package's contribution by the
   export key whose target is that same file (`.`, `./app`, `./views`, `./placer`, a kind's `./control`, a
   contributing item's runtime key), and a specifier derived from the path would load another module or nothing, which
   only the bundler would say. Rule 3 (the boundary check) is what refuses React outside a package's `ts/ui/`: a `.tsx`
   there, a React host module imported from another tree, and `@retroplug/ui-react` from a file that is not the
   package's UI.
7. A protocol node's span label is the next free one (16 up, with no gaps in the DESKTOP's list), which the manifest's
   `label` and the generator's check demand. Any other product's list is the desktop's with packages dropped, so its
   labels are its positions; nothing is renumbered in a manifest.
8. A contribution KIND is data (the F5a design's section 6.5). The declaring package's manifest states it
   (`retroplug.kinds.<kind>`): a `control` module default-exporting `defineKind({ accept, check? })` from
   [core/ts/defineKind.ts](../packages/core/ts/defineKind.ts), a `runtime` `{ module, register, type }`, and the `data`
   keys a contributing entry may carry. Any listed package may contribute items: its app module carries a `<kind>` list
   (the control-plane halves, so the type-only import of the declaring package's `packageContributions`, which owes the
   `declare module "@retroplug/core/definePackage"` augmentation, and its `ts/control/` tree is where that module lives
   when it must not reach a device pack's core) and its manifest's `protocol.kinds.<kind>` entries name each item's
   runtime module and its data. The generated `packages.ts` joins the halves by id, refuses a duplicate id (naming both
   packages, or the one that listed it twice) and an item with one half alone, runs the declaring package's `check` over
   the data with the product's WIRED protocol node types, then hands the package its items and their data (`accept`, at
   load); the generated `scriptNodes.ts` registers the runtime halves in product order and holds the registered set to
   the manifests. A kind may not be named after an app member (`roles`, `embeds`, `projectSections`, ...), which the
   generator refuses. `controllerApps` is the worked example
   ([modular-graph-phase-f3-design.md](../docs/modular-graph-phase-f3-design.md) section 4 and the F5a design's 6.5):
   declared in [launchpad/package.json](../packages/launchpad/package.json), its control half
   [launchpad/ts/control/kind.ts](../packages/launchpad/ts/control/kind.ts), with MI.MAP contributing the control-plane
   half from [lsdj-launchpad/ts/app.ts](../packages/lsdj-launchpad/ts/app.ts) and the runtime half and `syncOverride`
   data from its manifest. What an app's launches need the cart to run in travels as `syncOverride` DATA (`node`,
   `role`, `mode`, `listening`, `off`, `divisor`), applied by the launchpad pack's graph placer
   (`ts/control/placer.ts`), never as a function a planner's bundle would have to import. The generator and the
   fixture suite hold the mechanism over a fixture kind that is no real pack's (`widgets`); each real kind's own rules
   are its package's test ([kind.test.ts](../packages/launchpad/ts/test/kind.test.ts)).
9. A device pack's imports are limited to `build-device-pack.mjs`'s four-file core SDK (`defineNode`,
   `settingsSchema`, `descriptor`, `register`), and `pico/n8-host/exporter-stale.sh` watches `packages/core/ts` plus
   each pack's own `ts/`.
10. A scenario's whole-core scan skips THREE modules of the package, the control plane's halves: the manifest's
   `retroplug.app`, `retroplug.ui` and `retroplug.cli`, because those are the desktop's (zod's, React's, the CLI's) and
   never run on the engine. It also skips the whole directory of one of them that is an `index` module
   ([build-device-pack.mjs](../packages/retroplug/scripts/build-device-pack.mjs) builds the skip list): an
   `app: ts/control/index.ts` leaves `ts/control/` out with it, which is why item 5's convention holds only for that
   entry shape. A helper the scan does not skip (an `app: ts/app.ts` with a `ts/control/` sibling, say) is scanned like
   any other module of the core, and a library call the engine lacks there is refused, naming the helper's source line.
   A core module that imports a skipped module is refused by name (the importer and the import), since the scan would
   otherwise report whichever library call zod's internals make, and the core bundle is held to the same allow list the
   scenario bundle has: a module the scenario never reaches may not import another package either.
   [test/graph/device-pack.test.ts](../packages/retroplug/test/graph/device-pack.test.ts) holds all of it (a pack whose
   `app` calls what the engine lacks, accepted; a pack whose app imports one that does, refused; a core importing its own
   `app` module, refused by name; an unreached core module importing an undeclared package, refused by name).
11. A provider is the pack's device service, and a product starts it only by naming it (above). The manifest's native
    entry carries `provider` (and `rpc` where the pack has a facet), on the package's DESKTOP half, and a target carries a
    provider or an entry, never both: a configure refusal, because a product that listed the package without naming it
    would keep the entry and drop the target. A product adds `PROVIDERS <package>` to its `retroplug_product` call, in its
    own start order, and configure refuses PROVIDERS naming a package the product's `PACKAGES` does not hold, one whose
    manifest names no provider, a duplicate in PROVIDERS, and PROVIDERS on a `DEVICE` product (a box runs no host
    service). A host and a provider of different `RP_PROVIDER_ABI_VERSION`s refuse each other in both directions, at
    runtime: there is no optional read of a newer member. The product calls `rp_wake_providers` once per frame, and stops
    its providers after reclaiming its plans.
12. A **view** is declared in the package's `retroplug.views` module (`defineViews({ views })`, under `ts/ui/`), which
    the generator writes into `ui/generated/views.ts` - apart from the menus' module, so the pure tier never bundles a
    React view - not coded in the shell: an
    id, a label, `placement` `window` or `tile`, an optional `when(sys)` gate, an optional `visible` flag (a tile view
    only), and one body kind. Two need nothing but plain TypeScript: a `pixels` body's `open` returns a session whose
    `frame()` is the next `{ pixels, width, height }` (or null when nothing moved), and a `readout` body's `open`
    returns a sampler whose `sample()` is the strip's lines and playing flag (null = an unsupported cart, undefined =
    unreadable this frame). The shell renders both. A `component` body is React and is built with `reactView` from
    `@retroplug/ui-react`: the package declares that workspace package in `dependencies` (as
    [ui-fixture's manifest](../packages/ui-fixture/package.json) does) and, in `retroplug.host`, the React host modules
    it imports (`react`, `react/jsx-runtime`, and `lvgljs` / `lvgljs-ui` where it names them; `retroplug.host` takes
    only what the shell's host-modules.json lists), and its `tsconfig.json` extends the generated
    `../retroplug/tsconfig.host-modules.json` so the typecheck resolves them; a package whose views are plain TS needs
    neither the dependency nor the host declarations. React is admitted only under a package's `ts/ui/` tree, so a React
    view's test cannot live in `ts/test/`: the pure controller (open, reopen, close) is tested in the SHELL's suite, over
    fake views and a fake host ([view-session.test.ts](../packages/retroplug/test/ui/view-session.test.ts)), and what the
    component draws is proved by a `test:ui` case in the shell, where the display is - which needs a product to list the
    package, since the UI test binary embeds the production bundle. A plain-TypeScript view needs none of that: its own tests
    live in the package's `ts/test/` over a fake host, as
    [ui-views.test.ts](../packages/lsdj/ts/test/ui-views.test.ts) does. The generated registry refuses a duplicate view
    id (naming both packages), a placement or body kind the shell has not, a session body with no `open` or a component
    body with no `component`, and a `visible` on a window view. The `lvgljs-ui` widgets a component draws through are the
    SHELL's type slice ([lvgljs-ui.d.ts](../packages/retroplug/ui/types/lvgljs-ui.d.ts)), since the package ships
    TypeScript source and the slice is what `tsc` reads: a component pack that needs a widget the slice lacks extends
    that shell file, which is the one thing about a component view a package cannot do alone.
13. An **action** is a key binding the package owns (`defineUi({ actions })`): `{ id, label, keys?, pad?, run }`. Its id
    is prefixed by its own package's manifest name (`lsdj.debug-overlay`), because a stored binding profile keeps
    ids and a user's profile must name the owner; `label` is the bindings menus' row text, `keys`/`pad` are the
    DEFAULTS (a missing entry reads `-`), and `run(host, ctx)` gets the UI host and the focused system's id (null on the
    start screen). The generated registry refuses a duplicate action id, an id not prefixed by its package's name, a
    default key `resolveKeyName` cannot resolve, a single uppercase letter as a default (the hosts deliver letters
    unshifted, so `F` would bind and never fire, and the refusal names the lowercase key), a default pad name outside
    `GAMEPAD_BUTTON_NAMES` (SDL's button set), and a default that collides with a shell default or a Game Boy button's (a
    key on both would fire the action and the joypad). Two packages may share a default key; that is how one backtick
    still flips both debug overlays.

**A console package's native half.** A package whose console the engine runs is a native entry like any other, plus
three rules the two console packages (four node types between them) follow (F2,
[modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 2):

- It declares `@retroplug/system` and `@retroplug/node-sdk` beside whatever else it needs (`mesen` declares `n8-wire`,
  whose reliable-link header its emulated FIFO speaks). Its target is a `retroplug_package_library` linking
  `retroplug-system`, `retroplug-node-sdk` and its third-party library (`sameboy`, `mesen`), and its include root
  is `src/`, so a moved source keeps the include text it had.
- Its node type carries `static constexpr SystemBuildFn kBuild`, the package's own build function (section 3 of
  [modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md)), which the request hands to
  `rp.host.systems/2`; the engine knows no console. It declares `category = "system"`: that is what the engine
  recognises a system node by, and the `SystemNode<Self, Core, Settings>` template (`system/SystemNode.hpp`) refuses a
  type built on it that does not, at compile time (a constexpr string comparison in `activate`). The engine also holds a
  type that declares the category to a built core: a plan whose slot stayed null is refused before it is published, and
  a type that builds a core but declares no category is refused at that build, naming the type.
- Its settings struct carries `system`, the `rom` / `sram` / `state` resource refs, an optional `romName` (the file stem
  a Master System or Game Gear stages its ROM under) and a `key()` of the fields a rebuild depends on; its
  `Settings::Applied` with `liveKnobs()` holds the live knobs alone and is trivially copyable, since the block thread
  copies it. Beside those the type supplies `config(const Settings&)`, its lanes and its knobs' application; the
  console's knob helpers stay beside it.
- What it computes is its own: the console's ROM gate (magic bytes, not the extension) and its button table live in the
  package, and so does its audio path. A case that builds its core directly needs no `Engine`, so it belongs in the
  package's own test binary (`retroplug-sameboy-test`, `retroplug-mesen-test`), built by `retroplug_add_test_binary` and
  listed in `pnpm test:plugin`; a case that drives an `Engine` stays in the shell's `retroplug-audio-test`.
- Two shell edits a new console makes, because neither list is generated: the new package targets go on
  [packages/native/cmake/warnings.cmake](../packages/native/cmake/warnings.cmake)'s `_rp_werror_targets` (Windows' `/W4
  /WX`; the console test binaries are deliberately off it until a Windows run decides), and its test binary's name goes
  in `test:plugin`'s two lists, [packages/retroplug/package.json](../packages/retroplug/package.json)'s script and
  [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs), which is what builds and runs it.

**A portable core is proved by a scenario, if the package is not itself a pack.** `build-device-pack.mjs --scenario
<file.ts>` bundles a scenario instead of a node entry: under the same allow list (which a scenario may widen to the
package's declared dependencies, since a bridge's scenario is how it proves its half), lowered to ES5, scanned for the
library calls MicroQuickJS lacks both as the scenario and over the package's whole portable core (every module under
its `ts/` but its tests and the `app` module, item 10 above), then run under Node and on `retroplug-mqjs-run` with
byte-identical transcripts required inside `--heap-bytes`; it writes no pack. A package's scenario test runs it from
`pnpm test`, so a library call the engine lacks on the path the scenario takes fails there rather than on the box. The
launchpad pack and its LSDj bridge are the two that have one (budgets 76800 and 184320 bytes, each measured by
bisecting the heap, and neither a claim that the box's 64 KiB arena fits the core). The toolchain's other consequence
for a package's sources is item 10's: a zod-using control-plane module has to BE the manifest's `app` module.

## Hosts → test tiers

Three of the C++ hosts (see [01-architecture.md](01-architecture.md)) each back a headless TS test
tier; `test:plugin` is a fourth tier with no TS runner at all.
The TS runners live in `packages/retroplug/scripts/`; every runner discovers
`*.test.ts` files, bundles each with esbuild (es2020, one process per file), and reports TAP with
a nonzero exit on failure. Slugs accept slash or dash form, and a prefix runs everything under it (e.g.
`pnpm test paths` or `paths-rebase`): in slash form a prefix must be a whole directory, while in dash form it may
end at any dash, so `parity-blocks-tiny` runs the five `parity/blocks-tiny-*` files and `parity/blocks-tiny`
matches nothing.

On Windows the suites run through `test.bat` (alone it runs all four; `test.bat native ui` a subset): it enters the
MSVC env the `cmake-build` step needs and pins pnpm through corepack, because pnpm strips `PATH` for the children it
spawns on Windows. Without it the scripts die on `spawnSync cmake ENOENT`, which looks like a broken tree but is not.

### Skips are reported, and ratcheted

A case that cannot run — a fixture that is not checked out, a counter only a profile host exposes —
calls `skip(reason)` from [testing/harness.ts](../packages/testing/ts/harness.ts) (the CLI SDK exports it too) and is
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
in-repo, always present), `__RESOURCES_DIR__` (the sibling resources tree, `RETROPLUG_RESOURCES_DIR`, which the pure
runner injects as the native one does),
`__RISA_SRC__` (`RISA_SRC`) and `__MGB_ROM__` (the mGB synth's committed ROM, `packages/mgb/rom/mGB.gb`,
which F2 moved into its package). Pointing `__RESOURCES_DIR__` and `__RISA_SRC__` at a nonexistent directory is how
the CI picture is reproduced locally.

Runners execute their per-file (and, for `test:plugin`, per-binary) work in a **bounded parallel
pool** — each unit is an isolated child process (its own `mkdtemp` config dir; UI runs an in-process
software display), so concurrency is safe and needs no coordination. Concurrency defaults to **half
the logical threads**; override with `--jobs N` / `-j N` on the runner or the `TEST_JOBS` env, and
`TEST_JOBS=1` restores serial one-at-a-time output. Output is buffered per child and flushed as a
labelled `# <slug>` block on completion (rather than live-interleaved). The shared pool/spawn helper
is [scripts/lib/testPool.mjs](../packages/retroplug/scripts/lib/testPool.mjs). Work is dispatched
LONGEST-FIRST from durations the previous run recorded under `packages/retroplug/.test-timings/`, so
the wall clock is bounded by the slowest FILE rather than by when the pool happened to reach it.

So a slow suite is nearly always one slow file, not the pool: read `.test-timings/native.json` (the directory is
gitignored), and if its top entry is near the whole wall clock, raising `--jobs` does nothing. Two traps put a file
there, each of which has cost `test:native` a 10x factor. `audio.renderAudio(ms)` takes MILLISECONDS, not frames
(`renderAudio(44100)` is 44 seconds of emulated audio). And a system a test builds and never removes is emulated by
every later test in that file, since `renderAudio` advances every live system in the host process, so a file's cost
grows with the square of its boots: remove each system once its state has been read,
`s.project.systems.removeSystem(id)` for one the stores built, `graph.remove(id)` for one a `DirectGraph` placed, or
`LsdjProbe.closeAll()`, which the probe-based files call at the TOP of each test so an early return still cleans up. A
file whose realtime factor degrades case by case has this.

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
- **Two traps for a new case.** mGB ignores MIDI for its first 1.5 s, so a case sends nothing before `BOOT_MS`
  ([parity/scenes.ts](../packages/retroplug/test-native/parity/scenes.ts)); and a second instance of one ROM path
  gets a fresh `-2.sav` battery slot, so two systems that each need a battery each need their own copy
  (`rig.rom(src, prefix)`).
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
  ([util/Determinism.hpp](../packages/system/native/src/util/Determinism.hpp)): SameBoy's power-on randomness
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
- **Recorded defects.** The baseline pins one engine defect on purpose; fixing it is its own change
  with a deliberate re-record: Arduinoboy MIDI sync plays only in some host block partitions (at divisor
  1 silent at 128 to 768 frames, in 1-frame blocks and in `fl256`, playing in `fl`, most of whose blocks
  carry almost no clock tick; at divisor 4 silent at 1024 too). Details in design section 2.5
  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Four more were pinned and
  fixed: a single NES under a split routing on a host narrower than the split (the capture tap armed over the mix,
  which drifted ~25 frames per block at 48 kHz / 512; the core now steps on the ring the block drains, so the
  narrow host hears the mix, and its cases were re-recorded with that fix, design section 2.5 and
  [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md) section 10), LSDj master sync
  silent at 48 kHz together with its decoder reading a stopped LSDj as clocks in small blocks
  ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)), and MI.OUT's decoded notes depending
  on the host sample rate (the capture fed the Game Boy 1s where an Arduinoboy holds its serial input at 0, so the
  bits it clocked between LSDj's writes read as start bits; the four MI.OUT cases were re-recorded with the fix,
  their MIDI out and work RAM alone, [known-bugs-batch-2-design.md](../docs/known-bugs-batch-2-design.md) section
  3). The MI.OUT and master-sync cases (`lsdj-modes`' `midiOut` and two `masterSync`, all six of
  `blocks-tiny-lsdj-master`, `blocks-tiny-lsdj`'s three `midiOut-*`) record the host MIDI out of
  `rp.lsdj.serial-out`, after the Game Boy ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) sections
  7 and 12): each message at its byte's frame in the block the core captured it, which here lies off frame 0 but in
  the 1-frame splits and for one message of 85, a clock, in each master-sync `fl256` case, whose byte the core
  completed at its block's first frame.
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

- **The compiler.** Each cart's [`ts/kitFormat.ts`](../packages/lsdj/ts/kitFormat.ts) (over dsp's
  [`kitJobs.ts`](../packages/dsp/ts/kitJobs.ts)) compiles a spec as chains of offline graph jobs (D2):
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
  committed golden as it was. Without the variable the files are only ever read. The C++ `[kit-baseline]` suite went
  with native's compiler: no case carries that tag now, so `retroplug-audio-test "[kit-baseline]"` exits 2 (Catch2's
  "no tests ran").
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
- - **Fixtures and CI.** The LSDj and risa cases need `RETROPLUG_RESOURCES_DIR` (`__RESOURCES_DIR__`), so CI runs the 5
  fixture-free cases (2 of the 5 test cases in the file; the rest skip, counted in the skip baseline), and a change to
  the render path is accepted on a run with fixtures and `RP_FAIL_ON_SKIP=1`. CI's coverage of the continuous path in
  this suite is smsggdj and the SGB jingle only (none of a NES, a split render or a Game Boy's stems until renders can
  be fed MIDI); CI's one fixture-free per-channel render is C++'s, `[engine-jobs]` rendering mGB's stems driven through
  `rp.mgb.midi` (`EngineJobs.test.cpp`).
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
| **C++ unit** | native classes directly, no TS in the loop | `pnpm test:plugin` | [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) over fourteen Catch2 binaries | `packages/native/test/`, and each package's `native/test/` |

Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
`retroplug-host`; UI → `retroplug-ui-test`), so a `pnpm test*`
invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.
The script nodes are tested there too, each over a `Block` stand-in, in the package whose node it is
([lsdj/ts/test/lsdjSerialOut.test.ts](../packages/lsdj/ts/test/lsdjSerialOut.test.ts),
[mgb/ts/test/mgbMidi.test.ts](../packages/mgb/ts/test/mgbMidi.test.ts) and each cart's beside them, where the role
tests that drove the DSP kernel moved when it was deleted; the controller session's stays with the shell, in
[test/script/nodes/](../packages/retroplug/test/script/nodes): `lsdjSerialOut.test.ts` holds MI.OUT's and
master sync's messages each at its frame in its own block, and master sync's stop 200 ms after its byte across a
change in the host's rate, read off each block's transport), and so are the pure functions they run (`test/dsp/`,
and the carts' own, where [lsdjArduinoboy.test.ts](../packages/lsdj/ts/test/lsdjArduinoboy.test.ts) feeds the MI.OUT
and master-sync decoders one byte stream cut into 1-frame, FL-style
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
`lsdj-mastersync` holds master sync to one story in 1-frame, FL-style ragged and 1024-frame blocks at 44.1 and 48 kHz,
and its clocks to LSDj's tempo in absolute frames in every split (consecutive clocks of a run within 3% of a tempo
period, a run's first gap, which LSDj sends early, within 30%), which a decoder sending at a block's frame 0 fails in
the ragged and 1024-frame splits; `lsdj-midiout` holds MI.OUT's messages to frames inside their blocks, most past frame
0. `dsp-bench`, in a profile host only, holds an mGB window and a playing LSDj's master-sync and MI.OUT windows to zero
allocations a block (`RP_BENCH_LSDJ=0` leaves the LSDj windows out; [08-profiling.md](08-profiling.md)). CI decodes no
real LSDj's MI.OUT: the fixture-free `HostIo.test.cpp` cases hold the capture's wiring, that mGB's idle line decodes to
nothing, and a real mGB capture clocked through a master-sync decoder, while the real decode is the fixture-gated
`lsdj-midiout` files' and parity's, and the clock path's one real-LSDj case is `lsdj-midiout`'s N command of 0x7F
([docs/lsdj.md](../docs/lsdj.md)); its other cases are the decoder's unit test, `lsdjArduinoboy.test.ts`.

The C++ unit tier's fourteen binaries are the [`pnpm scripts`](#pnpm-scripts) table's `test:plugin` row, which says what
each case holds; four carry most of the engine's work, and each has a line here:

- - `retroplug-engine-graph-test` (`packages/native/test/engine-graph/*.test.cpp`) holds the engine's graph to the cores
  it wraps, bit for bit; its tags include `[alloc]`, `[video]`, `[input-drops]`, `[devices]`, `[capability]`,
  `[n8-node]`, `[n8-adapters]` (`N8Adapters.test.cpp`), `[launchpad]`, `[stage]`, `[offline]` (`OfflineJobs.test.cpp`),
  `[engine-jobs]`, `[script]`, `[packs]`, the hidden `[bench]` and `[offline-bench]`, and a profile build's `[profile]`.
  The detail is the designs': C2 section 6, C3 7, C4 6, C5 10, F2 8, D2 3 and 11, D3 10, B3a 7.3, C1 9, B3b 6 and 9, and
  the serial-out move's 8 and 12.
- `retroplug-graph-test` holds the DSP library and its nodes (`[dsp]` with `[dsp][dpcm]`, `[dsp-nodes]`,
  `[dsp-resample]`, `[dsp-buffer-nodes]`; the D1 design's sections 6 and 10) beside the graph's foundations.
- `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`) with
  its OWN `DistrhoPluginInfo.h` first on the include path, building no plugin format, to drive
  `PluginExporter::reinitParameters` over a toy plugin.
- - `retroplug-device-mode-test` holds the node SDK built for the RP2350 to the desktop's (the E1 design's section 8,
  E3's 8, E4's 7): `rp.core` and `rp.dsp`, and in the `[device-check]` cases `rp.n8.nes`, `rp.launchpad.surface-in` /
  `-out`, `rp.device.midi-in` and `rp.devtest`, compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an
  object library that also builds the box's link in device mode (`BoxCartLink`). `[script-pack]` has 51 cases (its
  fixture packs in `test/device-mode/script-packs`), `[bliptoaster]` holds the policy's heap over 64 full blocks,
  `[plan]` the loader (an exported document loaded into an arena runs bit for bit as GraphHost does), `[mutation]` every
  bit of three of the four plan blob goldens that pin the format (`plan-blob.hex`, `box-plan.hex`,
  `device-check-plan.hex`; the fourth, `device-check-script-plan.hex`, is the device check's script plan), all recorded
  with `RP_PLAN_BLOB_UPDATE=1`, `rp.device.midi-in` has four cases (`DeviceIo.test.cpp`, tagged `[device-io]`, as the
  box bridge plan case also is), and `[box]` has the `retroplug-export-plan` tool run as a process, which is why the
  binary depends on it. The device check's digest literal is compared only on `linux-x64-gnu13`. Its configure runs five
  refused-source checks (`test/device-mode/refused/`: `HeapType`, `PointerSettings`, `UnguardedValidate`,
  `UnguardedLayout` and, under libstdc++ only, `HeapHeader`), none on MSVC.

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
| `test:plugin` | fourteen Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-sameboy-test`, `-mesen-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`, `-device-mode-test`, `-audio-host-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split (the console packages' own cases are `-sameboy-test` / `-mesen-test`, which build their cores directly and link no shell target), the efsw file watcher, MIDI in/out, the control-surface pack's transport, provider and facet (`retroplug-launchpad-test`: a claim's generation and close bytes, the writer thread and its UI queue, `PortClaim` and `MidiPortPool`'s held edge, the port filter, the provider's `wake`, and the facet's twelve methods over a scripted device), the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`, the fixed-capacity settings types `[settings-types]`, the field walk `[fields]` and the enum scan `[enums]` among them), the node SDK built for the device (`retroplug-device-mode-test`, below), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), the engine's build seam (F2: a request that names no build function refused, claiming nothing; a fixture console package the engine places, builds, slots, runs and frees with no console named; and a plan whose system slot would stay null refused before it is published, the running plan untouched); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). E1's `retroplug-device-mode-test` ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md) section 8) compiles `rp.core` and `rp.dsp` with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI and holds 96 cases in eight files: a device-mode node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, for every portable type over a table of settings, allocating nothing in create, process or destroy; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON refuses in the same words, naming the field (two inputs part by choice, pinned: design section 6); the JSON to msgpack converter; and nineteen `[device-check]` cases that hold the image's cases to the manifests' device entries, run the whole check in the image's arena with no allocation, stop it at each of its failure steps, judge its 10-node plan and run the devtest script pack's side (its types, a bench, a 4-node script plan held to the desktop's digest, the clock a node's deadline is read from, the deadline case's loops over a builtin in the box's pack's heap, the policy case's worst blocks); the script host (E4, [modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md), `[script-pack]`, 51 cases in `ScriptPack.test.cpp`, `BlipToaster.test.cpp` and `DeviceCheck.test.cpp`): a TypeScript node on MicroQuickJS in both builds of the host, its block API, every fault (a throw, a deadline, a loop over a builtin held to it, the bytes a comparison reads and a callback's returns counted toward the next poll, the calls `call`, `apply` and a bound function hand on polled, a heap running out silencing only its pack, the engine's dropped exceptions, a getter the host reads, the re-entry cap), a warmed block allocating nothing, a plan exported and loaded over the device-mode builds digest-identical to GraphHost and the loader's package memory, with `[bliptoaster]` holding `rp.bliptoaster.midi-policy` to `rp.core.midi-filter` message for message and `[debug-gc]` running the packs on an engine that collects at every allocation; the plan loader (`[plan]`: an exported document loaded into an arena equals GraphHost's run bit for bit, every refusal, every bit of three of the blob goldens flipped (`[mutation]`), four blob goldens pinning the format), `rp.device.midi-in` (`[device-io]`), the box's host pieces (`[box]`) and `UsbCdcTransport` over a fake TinyUSB (`[usb-cdc]`). Its configure also runs five refused-source checks (a heap type, a pointer in `Settings`, an unguarded `validate`, an unguarded `adjust_layout` and a heap header must each fail in device mode with their own reason; the heap header is skipped under libc++, and none runs under MSVC). The engine-graph binary's `[packages]` holds each product's generated registration to a golden list, and an engine's registry to that list plus the built-in script pack's types and nothing else. The configure holds the products too: `retroplug_check_products` (deferred to the root's end) refuses a binary that links a library carrying the engine, computed outward from `retroplug-backend`, and is no product; `retroplug_check_runtime_libraries` beside it refuses a target that is no host of the txiki runtime or the file watcher and reaches `tjs`, `uv_a` or `efsw-static` by any path, naming the chain. |
| `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
| `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. It runs under a `mktemp -d` config dir of its own and the test-only port filter set to a name no port has (`RP_MIDI_PORT_ALLOW=RP_NO_PORT`), so a saved `controlSurface` is never claimed and no real MIDI port is listed or opened (see `sdl:controller`). It pins the `null` audio backend and puts the tracked `/dev/snd` guard in front of `LD_PRELOAD` itself. In CI. |
| `sdl:controller` | `retroplug-sdl` | [tools/run-sdl-controller.sh](../tools/run-sdl-controller.sh): the control surface through the REAL standalone against an ALSA fake Pro MK3 ([tools/fixtures/fake-launchpad.c](../tools/fixtures/fake-launchpad.c)), five legs that each fail if the thing they name is broken (nothing selected and the fake's notes reach the shared stream; a restored pair claimed before the audio device opens and before the stream opens a hardware port (and the notes not reaching it); the device lifecycle's order, Programmer mode first and the release last; a real frame-polled scan persisted and claimed mid-session beside a second device; a second process refused the claim). Linux only (an ALSA sequencer), not in CI, needs `libasound2-dev`. It runs under the TEST-ONLY port filter `RP_MIDI_PORT_ALLOW` (a comma-separated list of port-name substrings, honoured by the pack's lister, opener, pool and link and by `MidiStream`, so a filtered process can neither list nor open a port outside it), which it sets to its two fakes' WHOLE port names - `RPFakeLPProMK3:LPProMK3 MIDI 128:0` and `RPTestKeys:RPTestKeys 129:0`, the fake's client name being one no real device has - itself, refusing to run if a caller set the variable at all. Its listing pass, before anything is claimed or probed, fails if any other device appears AND if a listed port that is not a fake is named like a Launchpad, so a real Pro MK3 stops the run even if the list is loosened later. It pins `null` as its audio backend and puts the tracked `/dev/snd` guard ([tools/sdl-audio-guard](../tools/sdl-audio-guard)) in front of `LD_PRELOAD` itself. |
| `device:check` | `rp-device-check` | [tools/build-device-check.sh](../tools/build-device-check.sh): builds the cases generator and `retroplug-export-plan` in the desktop build (configured first), then the RP2350 image of every portable node type of `core dsp n8 launchpad device-io devtest` (25 types, the devtest pack's three script types among them and the control surface's two), the N8 pack's Edio run over a scripted cart, a 10-node plan exported at build time and run through the loader on a 4 KB stack guarded by `MSPLIM`, and the devtest script pack on MicroQuickJS (a bench, the deadline case's loops over a builtin and a 4-node script plan, whose stack is judged in two phases), with `-Werror=stack-usage=1024` on every compile (Pico SDK 2.1.1 via `PICO_SDK_PATH`, falling back to `/workspaces/pico-sdk` when unset; `arm-none-eabi-g++` 13; reflect-cpp's headers at the desktop's pin; `build-device/`) with a link trap on the heap, on exceptions and on RTTI (87 `--wrap`ped symbols with arm-none-eabi GCC 13.2.1's libstdc++), the trap probe (a program that does `new int(1)` and `std::malloc(16)`, which must FAIL to link naming both), and prints the sizes by owner. The image is built, never run. In CI (linux-x64). |
| `device:box` | `n8_host` (PIO-USB and `RP_NATIVE_USB`) | [tools/build-box.sh](../tools/build-box.sh): builds the Pico box's firmware ([pico/n8-host](../pico/n8-host)), C++ on the N8 pack's Edio and, since E3, a graph host (`pico/host/`) whose bridge is `plan.json` (`rp.device.midi-in` to the `bliptoaster` policy, a TypeScript node on MicroQuickJS, to `rp.n8.nes`), exported by `retroplug-export-plan` (built first) into a 64 KiB static arena with `PICO_HEAP_SIZE=0` and `-Werror=stack-usage=1024`, in both USB variants in `build-box/{pio,native}/`, each linked under the same 87-symbol trap over the whole image (TinyUSB and Pico-PIO-USB included) and with its own `n8_host_trap_probe` that must FAIL to link naming `__wrap__Znwj` and `__wrap_malloc`, and prints each image's flash, data and bss. Needs the SDK's TinyUSB submodule (`PICO_SDK_PATH` when it carries one, else `/workspaces/pico-sdk`, said aloud) and a Pico-PIO-USB checkout at 5a37a66 (`PICO_PIO_USB_PATH`, default `/workspaces/Pico-PIO-USB`); no UF2 (`PICO_NO_PICOTOOL`). Built, never run: the rig's gate is `pico/n8-host/loop.sh`, `stress.sh` and `faults.sh`. In CI (linux-x64, beside `device:check`). |
| `build:mesen-all` | `retroplug-cli` | [tools/build-mesen-all-consoles.sh](../tools/build-mesen-all-consoles.sh), two modes, both exiting non-zero on any failure: with no argument it configures `build-mesen-all/` with `MESEN_ENABLE_SNES` / `-PCE` / `-WS` all ON (Release), builds the `mesen` library and `retroplug-cli`, and asserts every console's symbol is in the binary (`SnesPpu`, `GbPpu`, `PceVdc`, `WsPpu`), so a guard that only compiles one way is caught; `--off` configures `build-mesen-all-off/` as Debug (-O0) with the three OFF, builds the `mesen` library alone, asserts the four symbols are absent, and refuses an archive that references a C++ symbol no member defines (a call into a compiled-out console that Release folded away). Not in CI; the ON side is what `deps/mesen`'s own defaults build for upstream. |
| `sdl:pipewire` | `retroplug-sdl` | [tools/run-sdl-pipewire.sh](../tools/run-sdl-pipewire.sh): stands up a private PipeWire server (its own `XDG_RUNTIME_DIR`, three null sinks, a `default` metadata object, no session manager) and asserts WHICH output device the host opens in BOTH audio modes - an AUTO leg first (no backend flag, the section `auto`, the state a user's config is in: PortAudio's PipeWire host API on the session default), then PortAudio's session-default / explicit-pick / stale-warn / explicit-driver cases, a libpipewire-less run degrading (its `default` a private `ALSA_CONFIG_PATH` config's null PCM; here the host falls back to `null` with a status, and the leg holds exactly that: no card's playback node attempted), 8 channels against a stereo sink (PortAudio: 8 ports, pair 1 linked; SDL reports 8 at the API but its PipeWire node shows 2 ports (only pair 1 reaches the sink), which the backend warns about), and a live menu build over a running SDL stream. The configuration is a `config.json` `audio` block (additive, so unstamped); a leg pins its backend with `--audio-backend`. The SDL and wide legs start wireplumber on a private dbus with its ALSA, v4l2 and libcamera device monitors disabled (the script checks the copied config, and the daemon's log for a loaded one: those monitors are what would open the capture card on `/dev/video0` and mirror the MIDI interfaces), and a missing wireplumber is reported as SKIP (counted, not silent). The whole script runs under the tracked device guard ([tools/sdl-audio-guard](../tools/sdl-audio-guard)) with `NOSND_LOG`, which records every refused device path and every audio-library dlopen, so the run proves no real sound card, capture card or serial device was opened; the guard is built from its source into the run's own scratch directory and self-tests before anything starts. The kit runs it as the guarded `pipewire` leg. Not in CI (the runners have no PipeWire). |
| `audiolink` (`pnpm check:audio-linkage`) | `retroplug-cli`, `retroplug-host`, the Node addon, `retroplug-ui-test`,
`retroplug-render-host-test` | [tools/check-audio-linkage.sh](../tools/check-audio-linkage.sh): `readelf -d`/`otool -L` plus `nm -g` hold the products that name no audio backend to naming neither backend (no `rp_portaudio_backend` / `rp_sdl_backend` factory symbol and no `Pa_*`, no audio-library dependency - SDL2 alone would prove nothing, since the window needs it), and `retroplug-sdl`'s backends to staying static. A binary with an empty symbol table fails rather than passing vacuously. |
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
Xvfb display, and per-tag logs (under `build/reaper-cfg-<tag>/logs/`). It also sources the tracked device guard
([tools/sdl-audio-guard](../tools/sdl-audio-guard)) before Xvfb and jackd, so a BARE `pnpm reaper:*` is guarded
rather than only a kit's copy: jackd's ALSA backend opens every card's control node at startup, and this machine's
ALSA default is a real card. Each leg's teardown prints the guard log's counts and fails the run on any refused
card playback, video or serial node (the control-node refusals are the ALSA probe and are only reported). A bare
`reaper:*` run
defaults the tag to the scenario, so single runs are unchanged, and the same keying is what makes two individual
`reaper:*` scripts safe to run in parallel. The renders are
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
nested `deps/dpf.js` submodule (documented in its own README), consumed through this seam:

1. **Submodule** — `.gitmodules` registers `deps/dpf.js` (clone with `--recursive`).
2. 2. **pnpm link** - `"dpf.js": "link:./deps/dpf.js"` in the root [package.json](../package.json) `devDependencies`;
   `pnpm install` wires it before the first configure. lv_binding_js has its own pnpm workspace
   (react/react-reconciler/lvgljs-ui) that needs its own `pnpm install` in `deps/dpf.js/deps/lv_binding_js` after a
   fresh checkout, or the UI bundle cannot resolve `react`.
3. **CMake resolve + add_subdirectory** — the root runs `node -e "require.resolve('dpf.js/package.json')"` → `DPFJS_PATH`, then `add_subdirectory("${DPFJS_PATH}" ...)` defines `dpf_add_plugin`, `dpfjs::core`, `lvgl-js-native`, `tjs`, `tjsc`, `tjs-cli`, `rpcpp`, `miniz`.
4. **Use of `${DPFJS_PATH}`**: the shell's `retroplug-tjs-runtime` library compiles the shared txiki host `TjsHostRuntime.cpp` and links `tjs`; the backend takes the bare QuickJS headers (`quickjs.h`) and links `qjs`; the plugin embeds the generic `dpf-widgets/generic/LVGL.cpp` and links `dpfjs::core` + `lvgl-js-native` + `retroplug-tjs-runtime`. All in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).
5. **React resolution** — the UI ([ui/main.tsx](../packages/retroplug/ui/main.tsx)) is bundled by the shared [tools/build-ui.js](../tools/build-ui.js), which resolves `react`/`react-reconciler`/`lvgljs-ui` from dpf.js's own pnpm workspace under `deps/dpf.js/deps/lv_binding_js/node_modules`.

The shared C++ contract (`SystemBase`, the capabilities, the build request) is `retroplug-system`'s, and each console's
core is its package's (`retroplug-sameboy`, `retroplug-mesen`); the transport primitives the cores and the host share
are `system`'s too (`packages/system/native/src/transport/`), while the shell keeps `SampleCache`, `FileSync` and
lodepng, beside `retroplug-graph`, the graph the engine runs on. The product links the packages it lists
([cmake/packages.cmake](../cmake/packages.cmake)). See [01-architecture.md](01-architecture.md) for the "shared core"
boundary.

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
| **The control surface**: the pack's link, claim, pool and facet, the menu's device rows, and TS's restore/scan | `pnpm test:plugin` (`retroplug-launchpad-test`), `pnpm test:native launchpad` (`launchpad-facet`, `launchpad-devices`, over the headless host's scripted device), `pnpm test launchpad`, and `pnpm sdl:controller` (Linux, not CI) for the standalone against a fake device | The pack's binary holds the transport and the facet to its promises (generations, close bytes, one claim per port, the writer never blocking the UI); the native cases drive `packages/launchpad/ts/ui/launchpadDevices.ts` over the real facet, a second PROCESS included; `sdl:controller` is the only check of the whole seam in a product (port filter on). |
| **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke` | Instantiates `retroplug.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |

### The device build

`pnpm device:check` ([tools/build-device-check.sh](../tools/build-device-check.sh)) builds `rp-device-check`
([packages/graph-runtime/device-check/](../packages/graph-runtime/device-check)), the RP2350 image that runs each
portable type of six packages (`core dsp n8 launchpad device-io devtest`, 25 types, the devtest pack's three script
types among them and the control surface's two) once in `RP_DEVICE` mode from its default settings as msgpack, in a
static 400 KiB arena, and then the N8 pack's Edio, menu commands and operations over a scripted cart (`N8Protocol.cpp`:
a boot, a file read, a cancelled boot, every primitive, held on the desktop by a `[device-check]` case), reporting on
the UART. It links with `--wrap` on 87 symbols (with arm-none-eabi GCC 13.2.1; the malloc family, `sbrk`,
`__cxa_guard_*`, every `operator new` / `delete` and `std::__throw_*` the device `libstdc++.a` defines, the throw entry
points, and the exception-handling and RTTI runtime: the personality routine, the catch and cleanup entries,
`__dynamic_cast` and the class `type_info` vtables), so any heap use or throw in what it links is a link error naming
the object, as is an object built with exceptions or RTTI that keeps a handler, a cleanup or a polymorphic class's
`type_info`; a post-link check also refuses an image that defines a wrapper or refers to one. The Pico SDK's
`pico_malloc` is taken out of `pico_runtime` in place, the one part of this image's runtime the trap cannot have (stdio
over the UART links under it); the box's firmware, whose Pico-PIO-USB host makes an alarm pool on the heap, needs more
(below). The pin, the trap list and the in-place `pico_malloc` removal are shared with the firmware through
`cmake/reflectcpp-pin.cmake`, `cmake/device-image.cmake` and `cmake/check-trap.cmake`. The script also links a probe
that allocates and requires it to fail naming `__wrap__Znwj` and `__wrap_malloc`, on every run, so the trap cannot
silently stop reaching the link. It needs `arm-none-eabi-g++` and a Pico SDK (no submodules), and building it needs no
hardware; the CI
linux-x64 job installs `gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`, clones SDK 2.1.1 into
`$RUNNER_TEMP` and runs the script. The image is not run in CI; the controller flashes it once on the rig, where it
prints, in order: the per-type pass (25 types: rp.core, rp.dsp, `rp.n8.nes`, the control surface's two,
`rp.device.midi-in` and the `rp.devtest` nodes, the script pack's three among them), the protocol pass, a plan pass, a
script bench pass (a first block, an empty block, a note and a 64-byte SysEx, in cycles and microseconds at 150 MHz from
flash, with the XIP cache's hits), a script deadline pass (`rp.devtest.script-loop`'s ten loops over a builtin, five
after plain code, in a context of the box's pack's heap, each entered, which its line counts, and faulted by its 10 ms
deadline, the worst block judged against a third of the box's 3 s watchdog), a script policy heap pass
(`rp.bliptoaster.midi-policy` in a context of the box's heap less 4 KB, over the blocks that fill its heap most: SysEx
left at each place in turn, a 4 KB SysEx after 63 short ones, full queues of SysEx), a script plan pass (4 nodes through
`rp.device.midi-in` and a deep `replace` nest, judged by its digest against the desktop's exactly), the script pack's
report (its memory and the heap in use) and the stack's measured high-water mark, in two phases (the load and the
blocks) for the script plan, each kept 512 bytes under the 4 KB stack. The plan pass loads a 10-node plan (a coupled
pair, a feedback wire, a wired param, eight moves) that `retroplug-export-plan` wrote at build time, through the loader
in the arena, runs 48000 frames in ragged blocks and judges it (within the export's estimate, every move pushed, no
drops, a digest that heard sound); the desktop's digest is printed beside the device's and not judged (FMA contraction
may differ). The script builds the exporter in the desktop build first (`build/` must be configured; its `--packages`
names the packages an image's plan may use), and the image's 4 KB stack with `MSPLIM` as a guard
(`PICO_USE_STACK_GUARDS`) is in its CMakeLists, and `-Werror=stack-usage=1024` comes from `cmake/device-image.cmake`.
Release sizes: core, dsp and the executor were about 42 KB of flash and no static RAM; the whole image is 302 KB flash
(MicroQuickJS 100 KB, the script host 20.5 KB, the executor and the loader about 25 KB, the devtest script pack 22.5 KB,
the N8 pack's nodes and Edio about 14 KB, the devtest nodes 7.5 KB, the bliptoaster script pack 7 KB) and about 424 KB
RAM (data and bss), almost all of it the arena. The image takes a script pack's heap from `RP_SCRIPT_HEAP_BYTES_devtest`
(45056) in its registration (`RP_SCRIPT_HEAP_BYTES_<package>` sets a pack's heap in one image, and is refused in any
build but a device build given `RP_DESKTOP_BUILD_DIR`, or for a package with no script pack), links the bliptoaster
pack's bytecode for its policy case (which registers it at 20480, the box's 24576 less 4096), and `RP_DESKTOP_BUILD_DIR`
(derived from `RP_EXPORT_PLAN_TOOL`) names the desktop build whose generated pack sources it embeds.

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
data / bss are 199536 / 46120 / 120600 and 199920 / 32084 / 114368, the image running from SRAM (`copy_to_ram`: 358704
and 338824 bytes of the 512 KiB; E3's were 101200 / 23832 / 188020 and 101576 / 9836 / 181788, 312484 and 292636,
before MicroQuickJS, the pack and the halved arena; E2's were 48796 / 15324 / 32360 and 48756 / 1560 / 26128). The
boot prints `[host] plan loaded: 3 nodes, <N> of 65536 arena bytes, 48000 Hz, 256 frames a block`, a line per node, a
`[host]   pack bliptoaster, <bytes> bytes` line and, after the first block, the pack's heap in use against its
declaration with the load's and the first block's times, the proof that the plan and the script pack loaded. CI
installs TinyUSB and clones Pico-PIO-USB at 5a37a66 for it. The hardware gate (`loop.sh`, `stress.sh`, `faults.sh`) is
run by hand on the rig with BlipToaster; it is not CI.

**The MicroQuickJS pin is guarded.** The engine's sources in `deps/mquickjs` are upstream's, unpatched; its fixes are
wrappers in [rp_mqjs_engine.c](../packages/graph-runtime/native/src/script/rp_mqjs_engine.c), and a configure-time hash
in [cmake/mquickjs.cmake](../cmake/mquickjs.cmake) guards each upstream span a wrapper replaces or rests on (with
counted mentions of the names it relies on). A bump of the pin that changes one fails the configure, naming the
wrapper: the fix is regenerating the wrapper (re-reviewing it against the new text, then recording the new hash), never
loosening the guard. The wrapper's first fix (the name table's growth under a collection) is guarded by that hash alone,
since no test reaches it. In a device image the engine is compiled at -Os and passes the link trap with `abort` wrapped
(`LINKER:--wrap=abort` on `retroplug-mqjs-engine`, whose wrapper calls the SDK's panic).

**What the desktop's device objects prove.** The `<target>-device` object libraries and the device-mode test's
objects are compiled with `-fno-exceptions` on GCC and Clang, but MSVC keeps its `/EHsc` (a second `/EH` flag is
`D9025`), so only the GCC and Clang builds prove the no-exceptions build.

### Sanitizer specifics

[run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
directly: `tools/run-sanitizer.sh <thread|address> [slug]`. It configures a separate
`build-tsan/` / `build-asan/` with `-DRETROPLUG_SANITIZE=…`, builds the instrumented host, then
drives it via `run-native-tests.mjs` with `RETROPLUG_HOST` pointed at the instrumented
binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2
suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)), which names only efsw's teardown: the
triple-buffer seqlock needs no entry, since its reader's copy ignores its own racing read under TSan and records the
write of its destination ([SeqlockCopy.hpp](../packages/system/native/src/transport/SeqlockCopy.hpp)); a clean run **is** the
proof that the cores and the built-in script pack's audio runtime are touched only by the thread that runs
blocks. It loads no other pack: a loaded pack's runtimes are made, loaded and freed on the control thread
while an audio thread runs blocks in the others, which those slugs never do. The Catch2 cases that do,
`retroplug-engine-graph-test "[packs]"`, are built by name in `build-tsan/` and `build-asan/` and run with the
same options (the C1 design's section 9). Any finding aborts the host → nonzero exit. The thread run also builds
and runs `retroplug-launchpad-test`, `retroplug-midi-test` and `retroplug-audio-host-test` under ThreadSanitizer after
the native slugs. `retroplug-device-mode-test`'s `[mutation]` bounds checks bite only under `build-asan`, which is
run by hand: no CI or `test:plugin` leg builds that binary there.

## Build gotchas

- - **`deps/sameboy` is patched at configure, twice.** `cmake/sameboy.cmake` applies two tracked patches idempotently
  (`git apply --reverse --check` no-ops one already present): `cmake/patches/sameboy-per-channel-audio.patch` (the
  per-channel audio tap, `Core/apu.{c,h}`) and then `cmake/patches/sameboy-process-statics.patch` (the power-on random
  seed and its enable flag as relaxed atomics in `Core/random.c`, the camera's noise seed in `Core/camera.c`, and the
  SGB's `fm_sin` table built by a load-time constructor in `Core/sgb.c`, so that parallel Game Boy cores in engine jobs
  do not race; it uses GNU `__atomic` builtins, which every compiler of the core has: GCC, Clang, and clang-cl on
  Windows, where `cl` never builds it). After any configure `git status` shows five modified files in `deps/sameboy`:
  expected. The parent repo tracks only the pinned submodule pointer, so the patched files are committed nowhere: never
  reset, stash or commit them, or bump the pointer to absorb them. A bump that invalidates either patch fails loudly at
  configure. The fix is to regenerate the patch.
- **`deps/mesen` is vendored, and its fixes are marked.** The tree is SourMesen/Mesen2 at `b9fa69d` with no `.git`
  of its own, so a fix is a plain source change carrying a `RetroPlug:` comment (`//`, or `/* */` where the file is
  C-style): 46 files carry one (`grep -rl RetroPlug: deps/mesen`). An upstream merge re-applies them over the new
  snapshot and then runs both modes of `tools/build-mesen-all-consoles.sh`; `--off` is the one that catches a guard
  the merge dropped, since a Release link folds the arm away.
- **The consoles RetroPlug never runs are compiled out** (the `mesen` row of [the targets](#cmake-targets)). The call
  sites into a dropped console are guarded `#if MESEN_ENABLE_*` with a `// RetroPlug:` marker. A `case` whose console
  is off is gone from its switch, unreachable because a dropped console cannot be constructed; a switch that had a
  default keeps it, a void one without becomes a no-op for that CPU, and a value one falls to the code after it
  (`nullptr`, `false`, its own `return` or its throw).
- - **Mesen LTO is opt-in (`-DRETROPLUG_MESEN_LTO=ON`).** It buys about 10% on the NES core (Cortex-A53: xRT 0.83 ->
  0.92), but it makes every mesen object LTO bitcode, so each binary linking the static library re-runs codegen over its
  288 objects (426 with `MESEN_ENABLE_SNES` / `-PCE` / `-WS` all on) at every link: fourteen on Linux with the opt-in
  Node addon (thirteen without it; the plugin's three formats, the JACK standalone, the host, the CLI, the Node addon,
  the SDL binary, `rp-golden-dump` and five test binaries, the render host one of them; macOS adds the AU). So it is OFF
  by default and every `release.yml` job passes it ON: shipped builds keep the speedup, and the dev loop does not pay
  for it. Toggling it recompiles mesen (8 s with LTO and the default console set, F2 design section 11). A build with it
  ON that seems to stall on every link is that link-time codegen, not a stale-dependency bug (`[NN%] Built target mesen`
  prints whether or not it did work); the `-Wodr` and `emu2413.cpp` warnings that link-time codegen raised at every link
  are silenced by a `-w` that `deps/mesen/CMakeLists.txt` puts on the consumers' link line under LTO (GNU/Clang), since
  the per-TU `-w` cannot reach it.
- - **The Windows build is `/W4 /WX` on RetroPlug's OWN targets.**
  [packages/native/cmake/warnings.cmake](../packages/native/cmake/warnings.cmake), included at the end of
  [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt), puts it on the `_rp_werror_targets` list; the
  vendored subtrees keep their own levels, and `RETROPLUG_WERROR=OFF` opts out of `/WX`. Windows is the one platform
  whose compiler sees the `#if defined(_WIN32)` half of a platform guard, so it is where a rotted guard shows up. When a
  warning fires, fix our code; suppress only what we do not own, by number, each `/wd` pinned to the one header that
  raises it, so a new warning class still fails. A level cannot be lowered with `/W0` or `/w`: a second `/W`-family flag
  trips command-line warning `D9025`, which cannot itself be suppressed. That is why GNU/Clang's lever,
  `retroplug_silence_dep_warnings()` in the root [CMakeLists.txt](../CMakeLists.txt) putting `-w` on a dependency's own
  target, is not Windows': that form trips `D9025` on MSVC. A vendored `.cpp` compiled straight into one of our targets
  (lodepng, dpf-widgets' `LVGL.cpp`) takes a per-source `/wd` list through `set_source_files_properties`, appended after
  the target's options so it wins over `/W4`; a whole vendored translation unit with no reason to sit under our level
  gets a target of its own, kept off `_rp_werror_targets` and given the deps' `-w` on GNU/Clang
  (`retroplug-r8brain-base`). Both Windows CI jobs run `windows-2025-vs2026` (VS 2026, MSVC 14.51) to match the dev box:
  plain `windows-2025` ships the same VS 2022 17.14 / MSVC 14.44 as `windows-2022`, and 14.44 and 14.51 disagree about
  which warnings exist, so with the compilers matched a warning CI raises also fails the dev box's build: fix it there,
  in the code, never by a change that only one compiler accepts.
- **`NativeFileWatcher`'s `efsw::FileWatcher` member is its LAST**, so the efsw thread is joined before the state its
  callback touches goes; a member declared after it would be destroyed while the thread still runs.
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
  bundle.
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
