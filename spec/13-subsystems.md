# 13 — Subsystem index

The other docs cover the spine: the architecture, the native host, the TS layer, roles, persistence,
the build. This one covers everything else — the subsystems that are real, shipped and tested, and
that until now appeared in `spec/` only in passing, if at all.

It is deliberately shallow. Each entry says **what the thing is, where it lives, and how to prove a
change to it**, then points at whatever goes deeper. If you are orienting from `spec/` and a
subsystem here is not mentioned anywhere else, that is the gap this document closes, not a sign it
is unimportant.

---

## Trackers

### `@retroplug/tracker` - the common tracker interface

The four music carts RetroPlug knows (LSDj, risa, smsggdj, BlipToaster) each have songs in a
battery and swappable ROM assets. `packages/tracker/ts/` is the shape they share, so the menus and the
import/export paths are written once:

- **`SongCatalog`** — the slots in a cart's `.sav`: list, load, import, export, reorder. Backed by a
  per-console implementation (`lsdjSongCatalog`, `risaSongCatalog`, `smsggdjSongCatalog`).
- **`AssetCatalog`** — the replaceable ROM assets (kits, palettes/themes, fonts): the base ROM's
  slots, the effective merge with the per-system override list, `applyRoleConfig` and `hasEdits`.
  All four consoles implement both sides.
- **`TRACKER_INTEGRATIONS`** — the registry that maps a system to its catalogs.
- **`liveSav`** — reads the battery out of the *running* core rather than off disk, which is why
  the Songs menu acts on what the cart currently has.
- **Kit compiles** are TypeScript (a cart's [`ts/kitFormat.ts`](../packages/lsdj/ts/kitFormat.ts) over dsp's
  [`kitJobs.ts`](../packages/dsp/ts/kitJobs.ts)): each slot is a chain of offline graph
  jobs whose passes are `rp.dsp` documents (the effects and resample kernels in `packages/dsp/native/src/dsp/`, which
  those nodes wrap), and TypeScript packs the codes and bits and assembles the bank. TPDF dither seeds from a hash of
  its input, so a kit compiles to the same bytes every time, and `test-native`'s `kit-baseline` and `kit-compile` hold
  the compiled banks to their goldens (D1 and D2,
  [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
  [modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11). Every host compiles a kit,
  the plugin included; the native compiler and its harness-facet RPCs are gone.

What it does **not** abstract is role registration; each console registers its own roles in
[romProviders.ts](../packages/retroplug/src/romProviders.ts).

**Prove a change:** `pnpm test tracker`, plus `pnpm test menu` for the rows built on it.

### risa (NES / MMC5)

An LSDj-class NES tracker. RetroPlug supports its sav/song model, its asset editing (kits as NES
DPCM, themes, fonts), and host sync — the cart follows the DAW **transport**, with no MIDI involved:
the `risa-sync` role turns transport into risa's arm / start / 24-PPQN clock / stop byte stream over
the emulated EverDrive N8 FIFO. Layout support covers risa 2.3.0.

- Code: `packages/risa/ts/` (codec + ROM + runtime), `risaRole.ts`, `risaSync.ts`, `risaSongCatalog.ts` and
  `risaAssetCatalog.ts`. The DMC kit encode is `rp.dsp.dpcm` in the kit chain, and the bank packer is
  `ts/rom/kit.ts`; there is no native risa codec.
- **Prove a change:** `pnpm test risa` / `pnpm test:native risa`, and `pnpm reaper:risa-sync` for
  the real-DAW drift render (not in CI).
- Deeper: [docs/risa-integration-plan.md](../docs/risa-integration-plan.md) (design rationale; the
  work shipped).

### smsggdj (Master System / Game Gear)

The SMS/GG tracker, on the Mesen SMS core. Like risa it follows the DAW transport with no MIDI:
`sms-sync` drives a 2-bit counter on controller port 2. The Game Gear build is the same project
shape one layer down — it reads its EXT parallel port (`$01`/PC4-PC6) instead.

- Code: `packages/smsggdj/ts/smsSync.ts`, `smsggdjSongCatalog.ts` and `smsggdjAssetCatalog.ts`; the core is
  `src/system/mesen/MesenSmsSystem.*`.
- **Prove a change:** `pnpm test:native dsp-sms-sync-drift` (measures the ROM's own row counter),
  then `pnpm reaper:sms-sync` / `reaper:gg-sync` for the real-DAW halves.
- Deeper: [docs/sms-support.md](../docs/sms-support.md).

### The LSDj HD player

A full-resolution LSDj view: a pure-TS reader over live WRAM, tile-diffed into an LVGL canvas. No
new native code — it polls `readRam` through the snapshot registry on a frame divider, because
copying 128 KiB and decoding a song every frame is too expensive (see the live-memory entry in
[07-remaining-work.md](07-remaining-work.md)).

- Code: `packages/lsdj/ts/hd/` (tiles, render, canvas), `ui/screens/hd/`.
- **Prove a change:** `pnpm test:ui lsdj-hd`, `pnpm test lsdj/hd`.

---

## Hardware

### The Everdrive N8 bridge

Drives a **real** NES over USB: MIDI in (from a DAW or a controller) is forwarded to a physical
Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart.

- Code: the workspace package `packages/n8`: natively `native/src/n8/` (the one `Edio`, written once in C++ for the
  desktop and the Pico box over an abstract transport, with `Menu`, `Ops`, `rp.n8.nes` and `CartLink`) and
  `native/src/n8/desktop/` (`N8Link`, `N8Host`, the serial transport, the RPC facet and the provider); in TS
  `packages/n8/ts` (`createN8Client`, the sniffer and the other decoders, the load helpers). The emulated twin is
  `src/system/mesen/NesEverdriveFifo.hpp`, which every NES ROM gets at `$40F0`/`$40F1`.
- Every product that lists the pack and includes its generated provider header starts it (the plugin, the SDL
  standalone and the CLI, whose sessions use the facet's session half; `RETROPLUG_N8_BRIDGE` gates only the CLI's
  live-MIDI facet and rtmidi). The Pico box (`pico/n8-host`) is the same Edio on TinyUSB.
- **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
  `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session`; for the box, `pnpm device:box` and the rig's
  `loop.sh`, `stress.sh` and `faults.sh`. `retroplug-n8-hwtest` needs the physical console.
- Deeper: [docs/n8-bridge-plan.md](../docs/n8-bridge-plan.md),
  [docs/n8-usb-capabilities.md](../docs/n8-usb-capabilities.md).

### Launchpad

A Novation Launchpad on a real MIDI port drives a real cart, the first consumer being LSDj MI.MAP song-row launching
from the 8x8 grid. Since F3 the whole thing is a pack: the transport and the scan's ports are
[packages/launchpad](../packages/launchpad)'s native halves, the protocol, the port scan and the session are its
TypeScript, and the behaviour is an app (the controller settings select it) that runs on the audio thread, in the
controller session node (below). The selection is the user's: `config.json`'s `controlSurface`, which TS owns and
writes with a re-read, so two processes can share the file.

- Code: [packages/launchpad](../packages/launchpad) - the portable target (`retroplug-launchpad`, `device: true`) with
  the two surface nodes, and `retroplug-launchpad-desktop` with the transport (`ControllerLink`, `PortClaim`,
  `MidiPortPool`, `RtMidiPort`), the provider and the facet (`LaunchpadRpc`) - beside
  [ui/launchpadDevices.ts](../packages/launchpad/ts/ui/launchpadDevices.ts), which drives the
  facet, and the MI.MAP app in [packages/lsdj-launchpad](../packages/lsdj-launchpad).
- **Prove a change:** `retroplug-launchpad-test` (one of the eleven `pnpm test:plugin` binaries),
  `pnpm test:native launchpad` (the facet's cases over the headless host's scripted device, and
  `launchpadDevices.ts`'s over it), `pnpm test launchpad`, `pnpm test:native lsdj-launchpad`,
  `tools/run-launchpad-loopback.sh` for the shared MIDI stream's receive path, and `pnpm sdl:controller` for the whole
  seam in the standalone against an ALSA fake (Linux, not in CI, under the test-only port filter
  `RP_MIDI_PORT_ALLOW`).
- Deeper: [docs/launchpad-plan.md](../docs/launchpad-plan.md),
  [docs/modular-graph-phase-f3-design.md](../docs/modular-graph-phase-f3-design.md).

### The controller role

A project-level setting (`settings.controller`) that points one attached controller at one target:
`{ enabled, app, target, systemId, appConfig }`, additive and off by default. `ProjectStore` hands it to
`buildGraph`, which places it as the `rp.controller.session` script node (its target resolved, the MI.MAP
cart's sync mode forced: `graph/controller.ts`), so the chosen app runs on the audio thread as part of the
per-block graph rather than as UI-thread glue. `appConfig` is deliberately opaque at this level: the
chosen app's own schema validates it on the control plane (`parseAppConfig`), and the node takes it parsed.
The apps themselves are contributions (`controllerApps`, in two halves joined by id, F3): MI.MAP is
[packages/lsdj-launchpad](../packages/lsdj-launchpad)'s, and a tracker with another app adds its own bridge.

- **Prove a change:** `pnpm test controller`, `pnpm test script/nodes/controller-session`,
  `pnpm test graph/build-graph`, and `pnpm test:plugin` (`retroplug-launchpad-test` for the transport the
  session's surface reads).

### Pico firmware (`pico/`)

Standalone "MIDI in, N8 out, no computer" firmware for a Pico 2 (RP2350), built with the Pico SDK but through
the repo's `cmake/device-image.cmake` and the workspace packages (`pnpm device:box`). Since E3 it is a graph host:
`pico/host/` (the block driver, the UART MIDI staging, the device `rp_host`, the built-in plan's load) and
`pico/n8-host/` (the N8 flows, the box cart link and `plan.json`, which `retroplug-export-plan` turns into the blob
the firmware embeds); see [modular-graph-phase-e3-design.md](../docs/modular-graph-phase-e3-design.md).

- Deeper: [pico/README.md](../pico/README.md) and the per-stage READMEs.

---

## Hosts beyond the plugin

### `retroplug-sdl` — the SDL2 standalone

A second standalone with near-full parity with the DPF one, built for handhelds (proven on
Anbernic/muOS). It composes the same backend service graph as every other host
([02-native-host.md](02-native-host.md)); what differs is the shell — SDL for window, input and
gamepad, PortAudio for output, RtMidi for MIDI.

It is in the default `all` target, so every CI platform compiles and links it and it rides in the
build artifact, but it is deliberately **not** in any release: `release.yml`'s packaging steps are
explicit allowlists and it is on none of them.

- **Prove a change:** `pnpm sdl:smoke` (headless, no display or audio server — in CI) and
  `pnpm sdl:pipewire` (which output device PortAudio actually opens; not in CI, needs PipeWire).
- Deeper: [docs/sdl-standalone.md](../docs/sdl-standalone.md).

### `retroplug-node` — the N-API addon

The same backend behind a Node `require()`, for tooling that wants the emulator in a Node process.
The TS layer is unchanged and its PCM output is bit-identical to the QuickJS hosts. It is an addon
rather than a subprocess because `__rpcSend` is synchronous.

Opt-in at configure (`-DRETROPLUG_NODE_ADDON=ON`, needs `node_api.h`), so it is not in a default
build. Distribution is the standing objection to it replacing the CLI.

- Code: `packages/native/node/`.
- **Prove a change:** `pnpm test:node`.

## The node graph (Phases A, B1 and B2)

### `graph/` - the node-graph engine's foundations

The contracts and engine the modular rewrite builds on: the C ABI every node implements
(`graph/rp_node.h`, CLAP-style, host-supplied memory; a node type answers `layout_for(settings)`, so
the planner computes every layout from the document and runs every layout check before it creates any
instance), the no-heap executor, the C++ planner and `GraphHost` (one command ring carrying plan swaps,
params and the embedder's own commands in push order; one release ring; state kept across plan swaps),
the C++ authoring layer (`graph/node.hpp`: a plain struct becomes a node through reflect-cpp), the
`rp.core` package (`+`, merge, join, split, downmix, relabel, delay, constant, buffer player and
recorder), and the TS `defineNode` / `defineExtension` contract, held byte for byte to the C++
descriptors by shared golden files.

**The native engine runs on it** (Phase B1,
[docs/modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md)):
`Engine::processBlock` runs a `GraphHost` plan of the engine's own nodes: the host MIDI I/O nodes
(`rp.host.midi-in`, `-midi-out`), the script nodes that feed the systems (and LSDj's serial-out
decoder, which reads its Game Boy), one system node per core (`rp.sameboy.gb`, `rp.mesen.nes`,
`rp.mesen.gba`, `rp.mesen.sms`) and, where a setting places them, the hardware nodes: `rp.n8.nes` feeding a
physical N8, and the control surface's `rp.launchpad.surface-in` / `-out`, mixed by `rp.core.split` /
`rp.core.add` into `rp.host.audio-out`.
**TS builds the graph** (Phase B2,
[docs/modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)): the stores publish one
whole document per structural edit through `EngineGraph` (`buildGraph` writes it from the systems, the
routing and the host shape), each system node builds its core from the resources its settings name and native plans
the document with `setGraph`, and every other edit rides the one ring as a command
([01-architecture.md](01-architecture.md), [02-native-host.md](02-native-host.md)). **The kernel became
script nodes** (Phase B3a,
[docs/modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md), then B3b,
[docs/modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)): a TS node per role
(`rp.mgb.midi`, `rp.lsdj.sync`, ...), the MIDI routing and the controller session, run by the script host
every Engine embeds, each with its settings in the published graph, its inputs and outputs wires from the
host I/O nodes and to its system node's inputs, and each LSDj's Game Boy's `serialOut` going forward into
its serial-out decoder, `rp.lsdj.serial-out`, placed after the systems
([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). What is left is
[07-remaining-work.md](07-remaining-work.md) section 5.

- Code: `packages/node-sdk/`, `packages/graph-runtime/`, `packages/core/` and `packages/dsp/` (C++: the node SDK, the
  executor, planner and plan loader, the `rp.core` and `rp.dsp` nodes; `packages/n8/` holds `rp.n8.nes`,
  `packages/device-io/` `rp.device.midi-in`, `packages/devtest/` the device check's test nodes),
  `packages/native/src/host/graph/` (the engine's nodes), `packages/native/src/host/script/` (the script host),
  `packages/retroplug/src/graph/` (TS: the node and extension contracts, `buildGraph`, `DirectGraph`, the zod-free
  settings schema), `packages/retroplug/src/script/` (the script-node bundle and its nodes) and
  `packages/retroplug/src/engineGraph.ts`.
- **Prove a change:** `pnpm test:plugin graph` (a substring filter, so both `retroplug-graph-test`
  and `retroplug-engine-graph-test`), `pnpm test graph`, `pnpm test:native parity` for anything the
  engine renders, and the ThreadSanitizer build of `retroplug-graph-test` (`build-tsan/`, as in
  Task 9 of [docs/modular-graph-phase-a-plan.md](../docs/modular-graph-phase-a-plan.md)). How the
  planner scales: `build/bin/retroplug-graph-test "[bench]"`; what a script node costs a block:
  `build/bin/retroplug-engine-graph-test "[bench]"`.
- Deeper: [docs/modular-graph-design.md](../docs/modular-graph-design.md).
