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

### `src/tracker/` — the common tracker interface

The four music carts RetroPlug knows (LSDj, risa, smsggdj, BlipToaster) each have songs in a
battery and swappable ROM assets. `src/tracker/` is the shape they share, so the menus and the
import/export paths are written once:

- **`SongCatalog`** — the slots in a cart's `.sav`: list, load, import, export, reorder. Backed by a
  per-console implementation (`lsdjSongCatalog`, `risaSongCatalog`, `smsggdjSongCatalog`).
- **`AssetCatalog`** — the replaceable ROM assets (kits, palettes/themes, fonts): the base ROM's
  slots, the effective merge with the per-system override list, `applyRoleConfig` and `hasEdits`.
  All four consoles implement both sides.
- **`TRACKER_INTEGRATIONS`** — the registry that maps a system to its catalogs.
- **`liveSav`** — reads the battery out of the *running* core rather than off disk, which is why
  the Songs menu acts on what the cart currently has.

What it does **not** abstract is role registration; each console registers its own roles in
[romProviders.ts](../packages/retroplug/src/romProviders.ts).

**Prove a change:** `pnpm test tracker`, plus `pnpm test menu` for the rows built on it.

### risa (NES / MMC5)

An LSDj-class NES tracker. RetroPlug supports its sav/song model, its asset editing (kits as NES
DPCM, themes, fonts), and host sync — the cart follows the DAW **transport**, with no MIDI involved:
the `risa-sync` role turns transport into risa's arm / start / 24-PPQN clock / stop byte stream over
the emulated EverDrive N8 FIFO. Layout support covers risa 2.3.0.

- Code: `src/risa/` (codec + ROM + runtime), `src/risaRole.ts`, `src/risaSync.ts`,
  `src/tracker/risa*Catalog.ts`. The DMC kit codec is native (`src/risa/RisaDmcCodec.cpp`).
- **Prove a change:** `pnpm test risa` / `pnpm test:native risa`, and `pnpm reaper:risa-sync` for
  the real-DAW drift render (not in CI).
- Deeper: [docs/risa-integration-plan.md](../docs/risa-integration-plan.md) (design rationale; the
  work shipped).

### smsggdj (Master System / Game Gear)

The SMS/GG tracker, on the Mesen SMS core. Like risa it follows the DAW transport with no MIDI:
`sms-sync` drives a 2-bit counter on controller port 2. The Game Gear build is the same project
shape one layer down — it reads its EXT parallel port (`$01`/PC4-PC6) instead.

- Code: `src/smsSync.ts`, `src/tracker/smsggdj*Catalog.ts`; the core is
  `src/system/mesen/MesenSmsSystem.*`.
- **Prove a change:** `pnpm test:native dsp-sms-sync-drift` (measures the ROM's own row counter),
  then `pnpm reaper:sms-sync` / `reaper:gg-sync` for the real-DAW halves.
- Deeper: [docs/sms-support.md](../docs/sms-support.md).

### The LSDj HD player

A full-resolution LSDj view: a pure-TS reader over live WRAM, tile-diffed into an LVGL canvas. No
new native code — it polls `readRam` through the snapshot registry on a frame divider, because
copying 128 KiB and decoding a song every frame is too expensive (see the live-memory entry in
[07-remaining-work.md](07-remaining-work.md)).

- Code: `src/lsdj/hd/` (tiles, render, canvas), `ui/screens/hd/`.
- **Prove a change:** `pnpm test:ui lsdj-hd`, `pnpm test lsdj/hd`.

---

## Hardware

### The Everdrive N8 bridge

Drives a **real** NES over USB: MIDI in (from a DAW or a controller) is forwarded to a physical
Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart.

- Code: `src/host/n8/` natively (`Edio`, `N8Link`, `N8Host`, the serial port), `src/n8/` in TS
  (the protocol, the sniffer, the SD/menu images). The emulated twin is
  `src/system/mesen/NesEverdriveFifo.hpp`, which every NES ROM gets at `$40F0`/`$40F1`.
- The plugin and the SDL standalone link it; `retroplug-cli` gates it behind
  `RETROPLUG_N8_BRIDGE` so the CLI does not drag in rtmidi and serial.
- **Prove a change:** `pnpm test:n8` (protocol, no hardware), the `NesN8FifoTiming` and
  `N8Host` Catch2 cases, and `pnpm test n8`. `retroplug-n8-hwtest` needs the physical console.
- Deeper: [docs/n8-bridge-plan.md](../docs/n8-bridge-plan.md),
  [docs/n8-usb-capabilities.md](../docs/n8-usb-capabilities.md).

### Launchpad

A Novation Launchpad on a real MIDI port drives a real cart — the first consumer being LSDj MI.MAP
song-row launching from the 8x8 grid. The host half scans for the device and owns the MIDI ports;
the behaviour is a control-plane app selected by the controller settings.

- Code: `src/host/launchpad/` (`LaunchpadHost`, `LaunchpadLink`, `LaunchpadScanner`, `RtMidiPort`),
  with the TS side in `src/launchpad/`.
- **Prove a change:** `retroplug-launchpad-test` (one of the eight `pnpm test:plugin` binaries),
  `pnpm test:native lsdj-launchpad`, `pnpm test launchpad`, and `tools/run-launchpad-loopback.sh`
  for the port seam.
- Deeper: [docs/launchpad-plan.md](../docs/launchpad-plan.md).

### The controller role

A project-level setting (`settings.controller`) that points one attached controller at one target:
`{ enabled, app, target, systemId, appConfig }`, additive and off by default. `appHost.ts`'s
`controllerProjection` folds it into the DSP kernel structure, so the chosen app runs as part of the
per-block program rather than as UI-thread glue. `appConfig` is deliberately opaque at this level —
the chosen app's own schema validates it.

- **Prove a change:** `pnpm test controller`, `pnpm test dsp/projection`.

### Pico firmware (`pico/`)

Standalone "MIDI in, N8 out, no computer" firmware for a Pico 2 (RP2350), developed in this repo but
built with the Pico SDK rather than the RetroPlug CMake — nothing in the RetroPlug build references
it. Stage 1 (hardware MIDI in) is verified; stage 2 (the Pico as a USB host speaking Edio) is
verified for reads and blocked on a PIO-USB FIFO-write limitation.

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
