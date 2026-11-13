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

### Mega Drive (BlastEm)

The Sega Mega Drive / Genesis, on BlastEm 1.0, and genmddj's IN24 clock sync over controller port 2. The console is
`packages/blastem` over the vendored `deps/blastem` (our mirror's `retroplug` branch); the cart's half is
`packages/genmddj`. The descriptor classifies by the header (`SEGA` at `$100`, not `SEGA 32X`; extensions `.md`,
`.bin`, `.gen`), at the deep tier with rank 4 (last), so a crafted file carrying another console's header as well is
that console and a Master System `.bin` stays Mesen's. Its region is construct-time (`auto`, `ntscUs`, `ntscJp`,
`pal`): it fixes the master clock and the frame height, so a change is a rebuild. The gain is the node's param, not a
setting.

- Code: `packages/blastem/native/src/system/blastem/` (`BlastemMdSystem`, `BlastemPortDevice`, `BlastemAudio`,
  `BlastemVideo`, `BlastemMemory`, `BlastemShim`, `BlastemBackend`, `BlastemNode`), `packages/blastem/ts/consoles.ts`
  and `packages/genmddj/ts/`.
- **The clock.** Each instance keeps a 64-bit absolute master clock; host sample `n` at rate `R` is
  `floor(n * MCLK / R)` from an anchor, in exact integers, so a rate change re-anchors at the current sample and never
  drifts. `MCLK` is the core's own `gen->master_clock`: 53,693,175 NTSC and 53,203,395 PAL as BlastEm's constant has it
  (the hardware nominal is 53,203,424). `run_until(mclk)` sets `stop_cycle` and resumes the core, and
  `stepIfBelowTarget` stops once per HOST SAMPLE, not once per block: a block's end as a stop point moves the emulation
  by the refresh and interrupt-latency syncs BlastEm charges where it syncs, so a 1-frame and a 4096-frame render of the
  same 2 s disagreed (27 of the invariance case's 36 audio and state checks did, before the grid). The grid is absolute
  time, so every partition hits the same stops, and a sample the core has overrun costs nothing. It is not free: about
  15 to 19 percent of a block on x86 (hvPsg 22.7 to 19.7x real time, the per-VINT DMA image 19.2 to 16.2), measured in
  the core's own invariance work and, through the engine, in [spec/08](08-profiling.md).
- **Port 2** is the host's device. The mirror's `io.c` names the instance (`io_port.device.custom.host`), the port stays
  `IO_CUSTOM`, and `PortDevice` holds (host sample, level byte) entries, one packed word each. A level is on the pins
  from the stop grid's own sample on, whichever block delivered it. The key is the host sample, not a clock: every stop
  lands at the first instruction boundary at or past its target, so a clock-keyed schedule answered differently in a
  large block and in a 1-frame one. `pushCoreBytes(frame, bytes)` enqueues each byte at `sampleBase_ + frame`; bit 5 is
  TR and bit 6 TH, and the other lines idle high. A pin the ROM makes an output in `$A1000B` reads back the ROM's own
  latch, so genmddj's OUT and PULSE modes do not fight the host. The port is open only for a system whose nodes claim
  its `bytes` input (the descriptor's `claimedBytesIn`, written as the node's `coreBytesIn`), so a staged message or a
  routed SysEx cannot press player 2 on any other Mega Drive game.
- **Audio** never goes through BlastEm's `render_audio.c`, which is not compiled (a process-global registry capped at
  8, linear resampling). `render_audio.h`'s entry points feed the instance's `AudioSink`: each chip is a source at its
  own native rate (`master_clock / sample_divider`: 1,008 master clocks a stereo sample for the YM2612, 53,267 Hz NTSC;
  240 for the PSG; 112 while the VDP test register halves the Z80's clock), BlastEm's 3390 Hz one-pole low-pass and the
  chip gains apply at that rate, and blip_buf band-limits each step to the host rate, one ring per side. A block drains
  the ring exactly, after the chips are synced to the block's end clock, because a 68k-to-VDP DMA can carry the 68k
  past the stop with the sound chips behind it. Each ring carries a quarter second over the block (the longest DMA costs
  11,980,603 master clocks); `overrunSamples()` and `starvedSamples()` are 0 on a driven core, and `clampedBlocks()` is 0
  in any real run. The sink removes DC (part of a peak differs from BlastEm's), takes a little treble (-0.35 dB at
  3 kHz down to -3.4 dB at 16 kHz for the PSG at 48 kHz, less for the YM), and its bass corner scales with the host
  rate, which [spec/07](07-remaining-work.md#mega-drive-audio-fidelity-spec-3) owns.
- **Video**: the VDP draws into BlastEm's own bordered frame, and the frame end copies the active picture into the
  `FrameBufferTriple` at 320 wide (the widest mode) by the region's 224 or 240 lines. The frame is allocated once at
  boot, so a mode change moves the picture inside it; a picture taller than the frame is centred.
- **Memory, battery and savestates.** `getMemory(Ram)` is the 68k's 64 KiB in address order (not BlastEm's word order),
  `ExtWorkRam` the Z80's 8 KiB, and only `work` is published as a region. The battery is reached in its `.sav`'s order
  for every cart (an odd-byte cart keeps byte `i` at `$200001 + 2i`; a 16-bit cart is pair-swapped out of BlastEm's
  storage), through `saveSramBytes`, `getMemory(Sram)` and the capability's write alike. A savestate is BlastEm's
  serializer behind BlastEm's own 8-byte identifier (`BLSTSZ\x01\x07`), with a section walk in front that requires the
  Genesis section set exactly once, the mapper section exactly when the cart is banked and the model's TMSS section at
  most once, and this host's own section (id `0x8001`) after BlastEm's, carrying the battery. The ceiling is 256 KiB
  plus the battery (a state measures 141,639 bytes). A refused state boots from power-on and reports `stateRefused()`;
  a load re-anchors the clock and the grid and resets the sink, because the grid's phase is host state no state
  carries, so a Duplicate is not sample-exact against its source (two instances restored from the same bytes match
  exactly).
- **Faults do not exit.** The mirror's `machine_freeze` guard is `ISLIB || IS_LIB` and logs at WARN, so an unmapped
  access, an HV counter write, a Z80 VDP window write or a VDP port read the decoder has no answer for is a dropped
  write or an open-bus read and the run continues. The remaining `fatal_error` sites are the paths
  [spec/07](07-remaining-work.md#blastems-remaining-process-exits) lists.
- **The cart.** `packages/genmddj` is genmddj's identity (product code `GM GENMDDJ-` at `$180` and its `V<number>.<number>`
  version string inside the first 64 KiB), `mdSyncLevels` (counter bit 0 on TR, bit 1 on TH, `0x60` idle), the
  `genmddj-sync` role, its ROM provider and `rp.genmddj.sync` (span label 24), which runs smsggdj's `CounterSync` state
  machine with that encoder at 24 PPQN. The counter is never reset (START does not zero it: a reset would hand an
  un-re-armed ROM a spurious delta), and at most 3 clocks survive one of the ROM's frame polls, which is the tempo cap.
  There is no relocate, so the DAW drives start, stop and tempo but not song position.
- **Not ours.** `deps/blastem` is a git submodule of our mirror, every edit carrying a `RetroPlug:` marker, and a mirror
  commit is pushed to the local mirror before the outer pointer (only the user pushes it on). The threaded VDP renderer
  is compiled out (`render_is_threaded_video()` returns 0), and the event log is compiled in but never started: nothing
  opens its file, socket or memory reader, and the mirror's VRAM hooks return before their delta state unless a log is
  active. `BlastemShim.cpp` supplies the frontend globals and the functions a 32X, Sega CD, Pico, Coleco or SMS cart
  would reach, each stating the field that keeps it out of a Mega Drive.
- **Prove a change:** `pnpm test:plugin blastem` (`retroplug-blastem-test`: boot and the ROM gate, the stop and its
  32-bit rebase, block-size invariance at 1, 64, 1024 and 4096 frames, the port device at every block size and its
  direction rule, the audio sink's DMA burst, the pad, memory and the battery's order, the savestate's walk and
  refusals, a cart's faults, and the two-instance cases), `pnpm test:native parity-md` (the seven synthetic images,
  exact bytes on `linux-x64-gnu13`), and, with the fixture tree, `pnpm test:native genmddj-sync`
  ([tools/fetch-genmddj-fixture.sh](../tools/fetch-genmddj-fixture.sh) builds the ROM). `pnpm test blastem` and
  `pnpm test genmddj` run the packages' own TS cases.
- Deeper: [docs/megadrive-blastem-design.md](../docs/megadrive-blastem-design.md) and its
  [plan](../docs/megadrive-blastem-plan.md).

### The LSDj HD player

A full-resolution LSDj view: a pure-TS reader over live WRAM, tile-diffed into an LVGL canvas. No
new native code — it polls `readRam` through the snapshot registry on a frame divider, because
copying 128 KiB and decoding a song every frame is too expensive (see the live-memory entry in
[07-remaining-work.md](07-remaining-work.md#live-memory-region-subscription)).

- Code: `packages/lsdj/ts/hd/` (tiles, render, canvas) and
  `packages/lsdj/ts/ui/hdView.ts` (the "lsdj-hd" window view the shell renders).
- **Prove a change:** `pnpm test:ui lsdj-hd`, `pnpm test lsdj/hd`.

---

## Hardware

### The Everdrive N8 bridge

Drives a **real** NES over USB: MIDI in (from a DAW or a controller) is forwarded to a physical
Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart.

- Code: the workspace package `packages/n8`: natively `native/src/n8/`, the portable target `retroplug-n8`
  (`device: true`): the one `Edio`, written once in C++ for the desktop and the Pico box over an abstract transport,
  with no exceptions, beside the menu commands (`Menu`), the operations (`Ops`: `bootRom`, `readFile`, `writeFile`),
  `rp.n8.nes` and the `CartLink` it drives; and `native/src/n8/desktop/`, the desktop target `retroplug-n8-desktop` (the
  serial transport, `N8Link`, `N8Host`, the `N8CartLink`, the RPC facet `N8Rpc` and the provider `rp_n8_provider`); in
  TS `packages/n8/ts` (`createN8Client`, the sniffer and the other decoders, the load helpers). The emulated twin is the
  mesen package's `native/src/system/mesen/NesEverdriveFifo.hpp`, which every NES ROM gets at `$40F0`/`$40F1`.
- Every product that lists the pack and includes its generated provider header starts it (the plugin, the SDL
  standalone, the headless host and the CLI, whose sessions use the facet's session half; a product's `PROVIDERS` list
  says which packs it starts). The Pico box (`pico/n8-host`) is the same Edio on TinyUSB. BlipToaster's MIDI policy
  (`rp.bliptoaster.midi-policy`) is a node of the box's plan; the desktop's N8 graphs do not use the policy node.
- A listing is pulled 32 records a request (`Edio::listDir`): one request for a whole large directory fails on silicon
  (a 181-entry one answered "dir read error 0x82"), so every listing, every file read (which lists its directory first)
  and the cart jobs' battery lookup reach past 32 entries; `FakeN8` answers the range asked for. A session call that
  fails on the line marks its session dirty in the facet, and the next call first drains the line (about 0.4 s at most)
  and re-runs the handshake (0.3 s, or the timeout the session was last connected with; a handshake on a dirty session
  is that resync, run once with its caller's timeout), answering the handshake's failure without sending itself when the
  line does not answer (`N8Rpc::ready`), so the CLI's tools, the bridges and a session script's `connectN8()`
  ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)) recover from a hiccup. The bridges
  (`packages/n8/ts/bridge.ts`, `n8-bridge` and risa's `n8-sync`) log and count a failed FIFO write and exit once a run
  of at least 5 failures, with no write succeeding between them and no gap of more than 3 s between two of them, has
  lasted 3 s (`failureWatch`), and a fatal exit runs
  once. A resync recovers a late reply, not a line the N8 is still inside a command on (the handshake's bytes are taken
  as that command's data, so each call answers the handshake's failure until it has gone out or the N8 is reset).
- What the cart allows, and the pack accepts: a cart's battery is reachable only at the N8 menu, through the SD card's
  save file `EDN8/gamedata/<rom>/bram.srm`, and a dump takes about a minute for 64 KB; a running game refuses every cart
  job (a direct cart-memory read wedges it and leaves the menu deaf), and a restore goes before a `rom.upload` so the
  game loads it at its next boot. `rom.header` / `rom.title` answer for what this host last booted. Each connect reads
  the mapper the cart runs and forgets the image when it differs or cannot be read. A game of the same mapper goes
  unnoticed, and so does any game started from the cart's browser while the link stays up, until the next connect: the
  cart has no query for the image it runs. A cart job pauses streaming (pushes during it are dropped and counted), and a
  `rom.upload` leaves streaming stopped until the host reconnects. The CLI's n8 tools keep their own raw serial path
  (the battery dump among them, the one cart-memory read left); the capabilities deliberately offer no peek, poke,
  sniffer or FIFO reads.
- **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
  `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session` and `pnpm test:native n8-sdk` (`connectN8`
  over the fake N8); for the box, `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
  given BlipToaster with `--rom` (`bliptoaster.nes`): without `--rom` the firmware boots its default, `n8-midi.nes`,
  which no check uses. `retroplug-n8-hwtest` needs the physical console.
- Deeper: [docs/n8-bridge-plan.md](../docs/n8-bridge-plan.md),
  [docs/n8-usb-capabilities.md](../docs/n8-usb-capabilities.md), and the pack's design,
  [docs/modular-graph-phase-e2-design.md](../docs/modular-graph-phase-e2-design.md).

### Launchpad

A Novation Launchpad on a real MIDI port drives a real cart, the first consumer being LSDj MI.MAP song-row launching
from the 8x8 grid. Since F3 the whole thing is a pack: the transport and the scan's ports are
[packages/launchpad](../packages/launchpad)'s native halves, the protocol, the port scan and the session are its
TypeScript, and the behaviour is an app (the controller settings select it) that runs on the audio thread, in the
controller session node (below). The selection is the user's: `config.json`'s `controlSurface`, which TS owns and
writes with a re-read, so two processes can share the file. Its fields (`input`, `output`, `device`, `enabled`)
are additive, so no migration; and the SDL standalone holds MIDI and audio until TS's `restored()` lands (500 ms at
most once its audio settings have applied: `retroplug-sdl` below), so a restored pair is never opened by the shared
stream first. One controller link per process, and one per `rp.host.devices/2` name; more than one is a registry
change. The plugin has no control surface: its host MIDI out caps a message at 4 bytes, so no SysEx
reaches a surface, and it names no launchpad provider.

- Code: [packages/launchpad](../packages/launchpad) - the portable target (`retroplug-launchpad`, `device: true`) with
  the two surface nodes, and `retroplug-launchpad-desktop` with the transport (`ControllerLink`, `PortClaim`,
  `MidiPortPool`, `MidiPort`), the provider and the facet (`LaunchpadRpc`) - beside
  [ui/launchpadDevices.ts](../packages/launchpad/ts/ui/launchpadDevices.ts), which drives the
  facet, and the MI.MAP app in [packages/lsdj-launchpad](../packages/lsdj-launchpad), a bridge that depends on
  `launchpad` and `lsdj` and which `launchpad` never imports. The provider is `rp_launchpad_provider`, and the facet
  is TS's only door to a port, through `createLaunchpadClient(call)` in `@retroplug/launchpad/client`.
  `ControllerLink`'s writer thread drains the send ring and a UI queue of its own, so `send()` waits for no write in
  flight: a push takes a lock the writer holds only for a swap. The pack's portable core is held to MicroQuickJS by the
  device-pack toolchain's scenario mode (byte-identical transcripts under Node and `retroplug-mqjs-run`), but it
  declares no `script` member, so the scenarios are the proof and no box image carries it.
- **Prove a change:** `retroplug-launchpad-test` (one of the sixteen `pnpm test:plugin` binaries),
  `pnpm test:native launchpad` (the facet's cases over the headless host's scripted device, and
  `launchpadDevices.ts`'s over it), `pnpm test launchpad`, `pnpm test:native lsdj-launchpad`,
  `tools/run-launchpad-loopback.sh` for the shared MIDI stream's receive path, and `pnpm sdl:controller` for the whole
  seam in the standalone against an ALSA fake (Linux, not in CI, under the test-only port filter
  `RP_MIDI_PORT_ALLOW`).
- Deeper: [docs/launchpad-plan.md](../docs/launchpad-plan.md),
  [docs/modular-graph-phase-f3-design.md](../docs/modular-graph-phase-f3-design.md).

### The controller role

A project-level setting (`settings.controller`) that points one attached controller at one target:
`{ enabled, app, target, systemId, appConfig }`, additive and off by default. The Launchpad pack owns the block as a
`ProjectSection`, its project service publishes the model, and its graph placer
(`launchpad/ts/control/placer.ts`) places it as the `rp.controller.session` script node (its target resolved, the
MI.MAP cart's sync mode forced), so the chosen app runs on the audio thread as part of the per-block graph rather
than as UI-thread glue. `appConfig` is deliberately opaque at this level: the chosen app's own schema validates it on
the control plane (`parseAppConfig`), and the node takes it parsed.
The apps themselves are contributions (`controllerApps`, in two halves joined by id, F3): MI.MAP is
[packages/lsdj-launchpad](../packages/lsdj-launchpad)'s, and a tracker with another app adds its own bridge. The
control plane's half is `definePackage({ controllerApps })` (`id`, `label`, the app's config schema), which the
kind's `accept` files in the pack's own `ts/registry.ts` table, the one `parseAppConfig` reads.

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
gamepad, and the audio and MIDI packages' backends for output and MIDI (PortAudio and SDL audio, chosen at
runtime and named by the product's `AUDIO_BACKENDS`; RtMidi behind `packages/midi`).

It is in the default `all` target, so every CI platform compiles and links it and it rides in the build artifact (CI
builds `all` on all four platforms), but it is deliberately **not** in any release: `release.yml`'s packaging steps are
explicit allowlists and it is on none of them, and each carries a comment saying so. The standalone needs nothing extra
installed: SDL2 is already REQUIRED at root scope for every plugin variant, and rtmidi and PortAudio are in-tree
submodules. On macOS the artifact binary is build-machine-only: the `SDL2.framework` embed covers the plugin and app
bundles, and `retroplug-sdl` is a bare executable, so shipping it would need a framework copy and an `@executable_path`
rpath.

At startup it holds MIDI and audio until the UI has reported: the control surface's `restored()` (only when it
started the launchpad provider) and the `audio` section's apply. The wait is bounded in time, at 2000 ms
(`kAudioBoundMs`) while the audio apply has not landed and 500 ms once it has, after which it goes on without what has
not landed, logging each (an unapplied audio section starts on the defaults). `--audio-backend` and `--block-size`
override the saved section for that run, and `--render-ahead` exists only as a flag.

- **Prove a change:** `pnpm sdl:smoke` (headless, no display or audio server — in CI) and
  `pnpm sdl:pipewire` (which output device PortAudio actually opens; not in CI, needs PipeWire).
- Deeper: [docs/sdl-standalone.md](../docs/sdl-standalone.md), and the audio and MIDI packages' design,
  [docs/modular-graph-phase-f5b-design.md](../docs/modular-graph-phase-f5b-design.md).

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
the Engine group of [07-remaining-work.md](07-remaining-work.md#engine).

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
