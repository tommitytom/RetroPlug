# 00 — Overview

RetroPlug is a multi-instance chiptune plugin: it hosts Game Boy (SameBoy) and
NES/GBA (Mesen) emulator cores inside a DAW or as a standalone app, wires their
serial/MIDI/audio together, and drives music-tracker workflows (LSDj sync, mGB,
Arduinoboy, kit patching) on top. This `spec/` describes it — the C++ host in
[`packages/native/`](../packages/native) and the TypeScript + React/LVGL control
plane/UI in [`packages/retroplug/`](../packages/retroplug). It is written in the
present tense: it documents what exists today, and calls out what does not yet
exist in explicit "Not yet built" subsections.

> **The port is complete.** RetroPlug is a single build. The older **legacy**
> build (the twin `Plugin*.cpp` DSP/UI and the old generated RPC client) has been
> deleted; this is the sole build, with the canonical `RetroPlug` plugin identity.
> `packages/ui/` is the F4b menu-contribution package, not the legacy UI. The
> remaining open work, the feature gaps among it, lives in [07-remaining-work.md](07-remaining-work.md).

## The thesis: native owns bytes and cores; TypeScript owns meaning

Every design decision serves one split:

- **Native (C++)** owns the emulator **cores** and their **raw bytes** — ROM,
  SRAM, savestate, framebuffer, zip archives, the LSDj sav codec — plus the
  real-time **audio thread** and the lock-free seams between threads. Native
  makes no policy decisions; it is a role-agnostic runner of bytes.
- **TypeScript** owns **all meaning** — system identity and ids, ROM
  classification, path/sibling/suffix resolution, the project model and its
  serialization, config schemas, role/DSP behaviour, routing decisions, and the
  UI. TS drives native through one narrow **`Backend`** RPC surface over resolved
  paths, opaque byte buffers, and the graph documents it builds.

Two consequences show up everywhere and are worth stating once here:

- **Cores are built bare.** (The legacy build did it with `setSniffDefaultRoles(false)`; no such call
  exists now.) No feature behaviour is baked into C++. LSDj
  sync, mGB and Arduinoboy behaviour live in TypeScript **protocol script nodes**,
  one per role (each in its package's `ts/protocol.ts`, except the controller session's, the launchpad pack's; B3b
  deleted the DSP role kernel they replaced), which run in the engine's graph, in
  the bare QuickJS runtime of the Engine's script host. (Kit-patch is the exception: the `lsdj-assets` role's `onConstruct` folds the
  overrides into the ROM in memory when the core is constructed; it has no protocol
  node.) See [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md).
- **TS owns the system-id counter and orchestration.** Native never mints an id;
  `duplicate`/`reload` are TS orchestration over a new system node seeded from a resource
  (a savestate for a duplicate, the battery for a reload) plus snapshot reads, not native methods.
  See [03-ts-layer.md](03-ts-layer.md).

## The three hosts

Three C++ entry points compose the **same** Backend RPC surface — the capability
facets ([01-architecture.md](01-architecture.md)), each host mounting the subset it
is allowed to expose. Each binds `globalThis[Symbol.for("plugin")].__rpcSend`, and one
TypeScript adapter ([realBackend.ts](../packages/retroplug/src/realBackend.ts))
targets exactly that namespace — so the control-plane code is identical across all
three. The threading model, the command ring, the graph the engine runs on, the
snapshot registry and the release ring that these hosts share are defined
canonically in [01-architecture.md](01-architecture.md).

| Host | Entry point | Role |
|---|---|---|
| DPF plugin | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) + [`PluginUI.cpp`](../packages/native/plugin/PluginUI.cpp) | The shipping plugin. DSP + UI link in one binary; the editor runs in-process on the DSP's control-plane context. |
| Native test host | [`src/main.cpp`](../packages/native/src/main.cpp) (`retroplug-host`) | Headless txiki host that evals a TS bundle over the real `Backend`. The `test:native` runner. |
| Headless UI-test host | [`test/ui/UiHarness.cpp`](../packages/native/test/ui/UiHarness.cpp) | Boots the real React UI on a software LVGL display, no audio thread. The `test:ui` runner. |

## The artifact set

The DPF plugin builds in four plugin formats plus the JACK standalone
(`dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)`); the two test hosts are
separate executables. Targets are verified against the configured build; see
[06-build-test.md](06-build-test.md) for how they build and how to run each.

| Artifact | CMake target | Output |
|---|---|---|
| CLAP plugin | `retroplug-clap` | `bin/retroplug.clap` |
| VST3 plugin | `retroplug-vst3` | `bin/retroplug.vst3` |
| VST2 plugin | `retroplug-vst2` | `bin/retroplug-vst2.so` |
| AU plugin (macOS only) | `retroplug-au` | `bin/retroplug.component` |
| Standalone (GUI) app | `retroplug-jack` | `bin/retroplug` |
| Native test host | `retroplug-host` | `bin/retroplug-host` |
| Headless UI-test host | `retroplug-ui-test` | `bin/retroplug-ui-test` |

**LV2** is the only format not built (its out-of-process DSP/UI split doesn't fit
RetroPlug); AU is gated by DPF to macOS. The DPF identity is name `RetroPlug`, URI
`https://retroplug.io`, unique id `RPlg`
([DistrhoPluginInfo.h](../packages/native/plugin/DistrhoPluginInfo.h)).
The plugin exposes 0 inputs / **8 outputs = four stereo pairs** (`out_1..4`), each
system routed to one pair by its audio routing.

## Package layout

| Package | Contents | Lifetime |
|---|---|---|
| [`packages/native/`](../packages/native) | The C++ **shell**: the RPC services + their facets, `Engine`, the command/snapshot/release seams, the script host the TS nodes (the MIDI routing, the controller session and the protocol nodes) run in, the offline job pool, the DPF plugin, both test hosts, and the format-neutral helpers only they use (`SampleCache`, `FileSync`, lodepng). It names no console: a system node carries its own package's build function (`rp.host.systems/2`). | The one tree every product links. |
| [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane, the CLI and the React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the protocol nodes that run them, the UI, and the CLI's dispatcher, sessions and SDK. Its own trees (`src/`, `cli/` and `ui/`) name no cart and no console (the boundary check's rule 9 over all three): a cart is reached through its package's contribution (its tracker integration's hooks and render hooks, its role hooks, its kit format, its menu module and its CLI tool through the generated tables), a console through its descriptor, and an embedded ROM through the `embeds` declaration `SystemsStore.loadEmbedded(id)` looks up. | The sole control-plane + UI + CLI tree. |
| [`packages/ui/`](../packages/ui) | The plain-TypeScript UI contract: `@retroplug/ui/menu` (the menu model and its item builders), `@retroplug/ui/contribution` (the row, builder, tracker-UI, service and action shapes a package's `retroplug.ui` module declares, gated by `defineUi`, and the `ViewDef` shapes its own `retroplug.views` module declares, gated by `defineViews`) and `@retroplug/ui/format` (the readout's byte format). A view is apart from the menus so a component view's React is reached only by the shell's view slots. The React kit is a package of its own. | The shell and every package with menu rows, views or actions. |
| [`packages/ui-react/`](../packages/ui-react) | The React half of the UI: the typed lvgljs-ui handles (`lv`, `Box`, `PixelCanvas`), the frame-bus and re-render hooks, `DebugReadout`, `tagTestId`, and `reactView` (the typed way to build a component view's body). Its `ts/ui/` tree is the one place rule 3 admits React into, so a package that draws pixels or text never imports it; [`packages/ui-fixture/`](../packages/ui-fixture) is a component view's fixture, listed by no product. | The shell's own components and every package with a React view. |
| [`packages/system/`](../packages/system) | The **contract** every console implements and the engine drives: `SystemBase` and the capability interfaces, the shared system types, the transport primitives (`FrameBufferTriple`, `MemorySnapshotTriple`; `SpscRing` lives in `node-sdk`), the build request and the build function a node's package carries, the host's system services (`rp.host.systems/2`, `rp.engine.sync-trace/1`), the generic `SystemNode` template with the knob helpers, and the core-only utilities. | Shared by every console package and by the shell. |
| [`packages/sameboy/`](../packages/sameboy) / [`packages/mesen/`](../packages/mesen) | The consoles, each beside its TS: `SameBoySystem` and the Game Boy node (`rp.sameboy.gb`); the NES, GBA and SMS/GG systems, their roles, the emulated N8 FIFO, their build functions and nodes (`rp.mesen.nes` / `.gba` / `.sms`). Their TS is the console descriptor the shell classifies a ROM with and places a system by (its detectors' rank, extensions, settings, split modes, link cable and real cart), plus their roles, catalogs and protocol nodes. `mesen` and `n8` both declare `n8-wire` (the reliable link's header and the expansion-volume range) and `system`; `sameboy` declares `system`. | The products that list them. |
| [`packages/host-io/`](../packages/host-io) | The host's own I/O as node types (`rp.host.midi-in` / `rp.host.midi-out`) and the engine service they read, `rp.engine.host-io/3`, with the staging bounds the engine sizes from. | Every desktop product. |
| [`packages/audio/`](../packages/audio) / [`packages/audio-portaudio/`](../packages/audio-portaudio) / [`packages/audio-sdl/`](../packages/audio-sdl) | The SDL standalone's audio: the backend contract (`AudioBackend` / `AudioStream`), the null device and `AudioHost` (the one open/start/stop/reconfigure sequence, the muted fallback with a status, render-ahead, the no-pull watchdog), plus config.json's `audio` section and the Settings > Audio rows; and PortAudio and SDL2 audio as backends of their own, linked only by a product whose `AUDIO_BACKENDS` names them (`retroplug-sdl` does; the CLI, the headless host and the Node addon name none). | `retroplug-sdl`; the packages' TS half by the desktop products. |
| [`packages/midi/`](../packages/midi) | The host's MIDI: `MidiBackend` / `MidiPort`, RtMidi under one `systemMidi()`, `FakeMidiBackend`, the one copy of the port filters and `MidiStream` (the receive ring, the output set swapped with no audio stop, the control surface's reservation and the scan's held edge); the project's `midi` section, the Settings > MIDI rows and the MIDI Clock submenu; and `MidiRpcService` as the provider a product starts by naming `midi`. | The desktop products (`retroplug-sdl` uses the stream directly and starts no facet); the plugin lists neither this nor the audio packages. |
| [`packages/node-sdk/`](../packages/node-sdk) | The node SDK (E1): the C ABI (`rp_node.h`), `node.hpp` built for the desktop or, with `RP_DEVICE`, for the RP2350, the descriptor and canonical-JSON support library, the fixed-capacity settings types (`rp::text`, `rp::list`, `rp::choice`), the msgpack settings decode and converter, and `SpscRing`. A node pack depends on this alone. | Shared by every package. |
| [`packages/graph-runtime/`](../packages/graph-runtime) | The executor (`retroplug-graph-exec`, no heap, no exceptions), the planner, the registry, resources and `GraphHost` (`retroplug-graph`), the plan blob's `exportPlan`, loader and `retroplug-export-plan` tool (E3), and `device-check/`, the RP2350 image that proves the portable nodes link under a heap-and-exception trap and, when flashed, that a plan loads and runs. | Shared by every host. |
| [`packages/launchpad/`](../packages/launchpad) / [`packages/lsdj-launchpad/`](../packages/lsdj-launchpad) | The Launchpad pack: the protocol, the session and its `rp.controller.session` node, the port scan and profiles, the provider and the facet, the app registry, the `controllerApps` contribution kind, the project's `controller` section, its service and the graph placer that places them - and MI.MAP, the app that drives LSDj through it (the bridge beside the pack, which imports it and `lsdj`, never the reverse). | The desktop products alone: the plugin's `plugin` list carries neither, so a DAW host is handed no control surface. |
| [`packages/n8-wire/`](../packages/n8-wire) | The header-only wire between a console and the N8: `N8ReliableLink.hpp` and the expansion-volume range, device-ready, so the emulated NES FIFO role and the cart's link share one definition instead of `mesen` depending on `n8`. | `n8` and `mesen`. |
| [`packages/lsdj/`](../packages/lsdj) / [`packages/mgb/`](../packages/mgb) / [`packages/risa/`](../packages/risa) / [`packages/smsggdj/`](../packages/smsggdj) | The carts, each the cart's half of the app in TypeScript: its roles and ROM provider, its protocol node (`rp.lsdj.sync` and `rp.lsdj.serial-out`, `rp.mgb.midi`, `rp.risa.sync`, `rp.sms.sync`), and, for the trackers (`lsdj`, `risa`, `smsggdj`), the save and song codecs, the RAM layout reader, the tracker integration and their menus; `lsdj` and `risa` also carry the ROM asset layer, the kit bank format, views and CLI tools. `mgb` also bakes its ROM in (`retroplug.embed`). | Every desktop product and the plugin: the `desktop` and `plugin` lists carry all four. |
| [`packages/tracker/`](../packages/tracker) | The cart-neutral half of the tracker layer: the song and asset catalog contracts, a cart's `TrackerIntegration`, the playback-position model a control surface codes against, a working song as bytes, the shared asset-override contract and its patcher, and the NES asset formats every tracker cart reads the same way. It has no `retroplug` member: a module library, no node and no contribution of its own. | Imported by `lsdj`, `risa`, `smsggdj`, `bliptoaster`, `n8`, `ui`, the two launchpad packages and the shell; listed by no product. |
| [`packages/testing/`](../packages/testing) | The test harness the packages share: `@retroplug/testing`'s TAP harness, the script-node rig and the byte and deep-equal helpers in TypeScript, and a native half of two targets (`retroplug-testing`, the headers such as `CountingAllocator.hpp` and `LinkRomModel.hpp`; `retroplug-testing-allocator`, the object library that replaces the global `operator new`). | A devDependency of every package with tests; listed by no product. |
| [`packages/core/`](../packages/core) / [`packages/dsp/`](../packages/dsp) | The `rp.core` mixing nodes; the DSP kernels and the `rp.dsp` nodes (`rp.dsp.resample` beside them, desktop only). Both build for the device. | Shared by every host. |
| [`packages/n8/`](../packages/n8) / [`packages/device-io/`](../packages/device-io) / [`packages/bliptoaster/`](../packages/bliptoaster) / [`packages/devtest/`](../packages/devtest) | The N8 pack (`rp.n8.nes`, the one Edio, its provider and facet); the box's `rp.device.midi-in`; BlipToaster's MIDI policy, a TypeScript node on MicroQuickJS (E4), and the cart's app half (its ROM detection, asset view and patcher, roles, ROM provider, asset-only tracker integration, menus and CLI tool); the device check's test nodes (listed by no product). | The N8 pack by every product and the box; BlipToaster by the box (the policy) and by every desktop product and the plugin; the others by the device images and the exporter. |

The LSDj-persistence path is TS-owned. `retroplug-backend` is the shell's one library: the
`Engine` (which runs the systems on `retroplug-graph`, through the node types the console and
host-I/O packages register, in the graph documents the TS stores build) + the RPC surface, and every
host (plugin, `retroplug-host`, `retroplug-cli`) links that. ROM classification, the LSDj sav codec
and the kit compiler are TS-owned.

## Component map

```
  React / LVGL UI  (App · SystemGrid · tiles · Menu)
        │  events + __rp_* window/file-dialog hooks
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ control plane  (main / UI thread)                             │
 │   control-plane QuickJS runtime (txiki)                       │
 │   TS stores (+ EngineGraph) ─► Backend ─► __rpcSend           │
 └──────────────────────────────┬────────────────────────────────┘
                                │  in-process RPC (QuickJSCodec)
                                ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ Backend RPC facets  (C++ services, per RPC server)            │
 │   HostRpcService · EngineRpcService · AudioDriverRpcService · DebugRpcService │
 └──────┬───────────────────────────────────────────▲────────────┘
        │ setGraph: the document → a plan;           │ SnapshotRegistry (reads)
        │ edits → commands; ONE ring                 │ + release ring (replaced plans)
        ▼                                            │
 ┌───────────────────────────────────────────────────────────────┐
 │ audio thread  (DPF run() / spawned audioThread_)              │
 │   Engine ─► GraphHost plan: host I/O · script · systems · mix │
 │   script host: the protocol nodes  (bare QuickJS, no txiki)   │
 └───────────────────────────────────────────────────────────────┘
```

The control plane and the audio thread **never touch each other's memory
directly**: every control→audio change goes through the one command ring (a plan of a
graph document the stores built, or a command), and every audio→control
observation goes through the snapshot registry (reads) plus the release ring (a
replaced plan, and the cores only it held, handed back to be freed). Native runs
**separate QuickJS runtimes** that share no value: the control-plane one (txiki, full
host) and, per Engine, two bare ones for each script node pack its script host holds
(the built-in bundle the first): one the control thread uses only to check a node's
settings and lay out its ports, and one that holds every instance of the pack's nodes
and runs each on the thread that runs blocks (the audio thread in a live host). All of
this is specified in [01-architecture.md](01-architecture.md).

## Where to go next

| Doc | Scope |
|---|---|
| [01-architecture.md](01-architecture.md) | The canonical runtime architecture: three hosts over one Backend RPC surface (the capability facets); control plane vs audio thread; the command ring, the graph the engine runs on, snapshot registry, release ring; the QuickJS runtimes; threading/ownership invariants. |
| [02-native-host.md](02-native-host.md) | The shell and the console packages: the full RPC surface, `Engine`/`EngineInvoker` and its graph requests, `SnapshotRegistry` internals, the build seam each console's node carries, the script host and script compiler. |
| [03-ts-layer.md](03-ts-layer.md) | `packages/retroplug/`: the `Backend` interface, the stores, the control-plane composition, the `__rp_*` UI↔native hooks, and the React/LVGL UI. |
| [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md) | The role model: system-role vs feature-role, the role registry, and the protocol nodes: a script node per role, the MIDI routing and the controller session, their settings and ports, and how `buildGraph` places them. |
| [05-data-persistence.md](05-data-persistence.md) | The project model + `.rplg`, DPF get/setState + autoload, config models, forward-tolerant reads + version stamps, the LSDj sav codec, SRAM auto-save. |
| [06-build-test.md](06-build-test.md) | How the project builds, the pnpm scripts, the dpf.js seam, and the headless verification loop. |
| [07-remaining-work.md](07-remaining-work.md) | Open work only: a checklist (before merge, the package split, known bugs, the engine, renders and kits, the Pico, feature gaps and the SDL standalone) whose lines link each item's detail, then what is deliberately deferred. |

## Key files

- [`packages/native/src/host/rpc/BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — where the RPC facets are mounted onto a server; the C++ surface the three hosts share.
- [`packages/retroplug/src/backend.ts`](../packages/retroplug/src/backend.ts) — the TypeScript side of that contract.
- [`packages/retroplug/src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) — the adapter that binds `Symbol.for("plugin").__rpcSend`.
- [`packages/native/src/host/engine/Engine.hpp`](../packages/native/src/host/engine/Engine.hpp): the single-threaded owner of the graph the cores run on, the script host its TS nodes (the MIDI routing, the controller session and each role's protocol node) run in, and the snapshot registry.
- [`packages/retroplug/src/graph/buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts): the engine's graph document, built in TS from the stores' systems, routing and host shape.
- [`packages/*/ts/protocol.ts`](../packages/core/ts/protocol.ts): the script nodes buildGraph places, a package each: core's MIDI routing, then the carts' protocol nodes (B3b deleted the DSP role kernel they replaced). The controller session is the launchpad pack's, at [`packages/launchpad/ts/nodes.ts`](../packages/launchpad/ts/nodes.ts).
