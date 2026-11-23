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
- **`trackerIntegrations`** - the live registry (`src/contributions/appRegistries.ts`) that maps a system to its catalogs.
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
- **A cart at its menu is not streamed to.** The menu takes commands over the same FIFO a game reads MIDI from, so a
  data pair inside a message (`0x2A 0x73` is `*s`: start an app) can reach it. `N8Link` keeps the cart's at-menu state,
  and asks the menu for it ([the design](../docs/n8-menu-guard-reset-design.md)): the probe is `F0 2A 74 F7`
  (`N8Link::kMenuProbe`), a SysEx a MIDI game ignores and whose `*t` the menu reads (it skips every byte that is not a
  `*`), and the answer is the menu's `k` on the back-channel outside an ack (an ack is `F5 <next> <last>`, both 7-bit,
  so a `0x6B` inside one is not it). The FPGA config block's map_idx cannot tell: it reads 255 at a menu reached by
  power-on only, and a console reset leaves it at the game's mapper. The state is seeded at connect (the probe, with 100
  ms for the answer, so a connect to a game takes 100 ms longer; the lab's menu answers in about 20 ms, 27 ms the worst
  of 8), and the stream asks again by writing the probe where its own bytes are between two messages (a message left
  open holds the probe back however long it stays open, and a frame never leaves one open): on a raw link every 250 ms,
  with no read and no wait (the answer is heard on a later turn's back-channel read), which relies on a game's MIDI
  parser ending running status at the `F0`, as MIDI's rule is (one that skipped the `F0` would hear note 42 at velocity
  116 four times a second; on the lab's Famicom a busy BlipToaster's ticks land the same with the probe as without, 3
  runs each, 2026-11-22); on a framed link only once the window has STOPPED MOVING (the oldest frame unacknowledged
  past the link's own 100 ms timeout, which is the console reset), holding new frames and re-sends until a `k` (the
  menu) or an ack (a game whose acks are lost or damaged, which streams on and is not asked again for a second: a framed
  BlipToaster drops the probe's bytes as unframed and acknowledges them `F5 <next> <next>`, which is what resumes a
  stall for a game), asking again each second that hears neither, and after two such seconds giving up the window and
  what the ring held (counted) and probing the link again (a game without the link, started from the cart's browser
  after a reset the guard never saw, is then streamed raw); and at the menu never: there the guard reads the FPGA
  config block every 20 ms instead (the first read 100 ms after the `k` that entered it), which the cart's MCU answers
  without the FIFO, and leaves the menu (re-running the link probe) once the same non-zero block has held for 500 ms,
  and either an all-zero block was read since entering or the settled block differs from the one read on entering,
  byte 3 (the cart's master volume, which the host writes) left out of every compare. Every game started
  from the cart's own browser clears the block before writing the game's, the same game started again after a RESET
  included ([the config watch](../docs/n8-menu-guard-config-watch-design.md)); a start the reads miss leaves the guard
  at the menu until a reconnect. A `k` trips the guard whenever it is heard. A turn whose
  probe still awaits its answer, or that has read half an ack, is not quiet for a read, and a job waits the probe out,
  so a `k` that comes within the probe's 100 ms never lands inside another reply. A job's menu probe asks the same
  question on a quiet turn (on a raw link, between two messages), and the stream's tracker books it as it books its own,
  so a running status it ended is written back; it may trip the guard but never clears it, and at a running game, which
  the submission is refused for, it holds the serial thread, and so the stream, for its 100 ms wait (a submission whose
  probe began just before its second ran out is waited for, not refused as a failed port). The job's own first step asks
  again over its port, and at a menu reads past what the menu still owes for two probe windows (200 ms from its probe),
  so a slow menu's second `k` is not read as the first reply of the job's first command; a menu that takes longer than
  100 ms to answer that probe is taken for a game, and the job is refused as at a game (a retry works). While it says
  menu, `push` answers false and counts, the ring, the reliable window and any pending note release are discarded, and
  nothing is written; leaving the menu re-runs the link probe, which flushes the input before its sync, as
  does the connect-time one, so only the sync's own ack turns the framed link on. Jobs still run there (the SD card is
  reachable only at the menu), and a cart node's `connected` reads 0: what it pushes there is refused, and a note-off it
  loses stays owed until a game reads connected again (a note-off to a game that never sounded that note is harmless).
  Settings > N8's status row says "At the N8 menu (not streaming)".
  - **What is left exposed.** The guard knows the cart is at its menu only once the `k` is heard, so bytes written
    before it reach the menu: raw, up to about a second plus one probe cadence (250 ms) of MIDI after a RESET, since the
    booting OS answers no probe for about its first second; framed, the frames written before the stall shows (the
    link's 100 ms timeout), and up to a second more after a stall's probe has heard an ack (the trusted second: a reset
    inside it streams frames and re-sends until it ends, or until the window has drained, which a lossy link's game does
    between notes (under a dense stream the second runs on), so a reset after that is probed at the 100 ms timeout). At
    an OS that is up and idle, a `*` followed by a byte the menu takes as a command (a GM hi-hat, `9n 2A 73`, is `*s`
    and starts the app) does harm. While the OS works (a folder load, a game start, its boot) any byte can: it reads its
    MCU's status replies from the FIFO the host writes (edn8-pro-pub `edio/everdrive.c`, `ed_check_status`), so a host
    byte there is taken for one and stops it at ERROR:40 (the raw-link limit below). The framed hold's trigger is not a
    lost frame as such: it is the OLDEST outstanding frame unacknowledged 100 ms after its first send. That covers a
    frame lost twice in a row, and equally a cart whose FIFO does not run empty for 100 ms under a dense stream and so
    acks only then. Everything behind it waits for the menu's answer or an ack: the re-send, which used to go out at
    once, and the new chunks too. That is deliberate, since a re-send decided while the window has stopped moving is
    what a menu would read, so a slow recovery on a lossy NTSC link is that trade and not a new fault. The hardware
    check the answer rests on is [spec/07](07-remaining-work.md#the-n8-menus-back-channel).
  - **Where the guard runs.** On the app's path to a cart, `N8Link` under `N8Host`, which the cart node streams through
    in the plugin and the standalones, and on the Pico box, whose cart link (`BoxCartLink`) drives the same
    `N8MenuGuard` and `N8ReliableSender` (the `n8-wire` package's) at each main-loop drain
    ([the box's design](../docs/pico-box-framed-link-design.md)): it probes the link at a connect without holding the
    loop, its probe goes at the front of a write (a drain starts between two messages), the menu's `k` sends the box
    back through its boot (straight to `*t`, then the staged ROM, about 6 s from the press to playing, most of it the
    boot's wait for the game), and the box's own Edio reads (`CMD_STATUS`, the sniff, the volume's read-back) wait for a
    quiet pass. The box's framed drain stops framing at one frame short of the window outstanding, so the sender's
    48-frame give-up cannot happen there; a write that fails on a silent cart is followed by the guard's stall probe
    first, and BlipToaster's ack to it sets off the re-send. On the lab's NTSC Famicom (2026-11-20) it held: no tick
    missed framed with the DMC kicks where raw missed 1 to 6 a run, framing costing under 0.1 ms at p50 and p95, and 3
    RESETs mid-song back playing in about 5.7 s (the box's README). A menu back within 5 s of a boot is that boot
    failing, not a new RESET, and goes to the boot's retry rather than booting again at once. The CLI's
    `n8-bridge` and `n8-play` and risa's `n8-sync` write the cart FIFO without it, raw and unguarded
    ([spec/07](07-remaining-work.md#the-clis-n8-tools-stream-unframed-and-unguarded)).
  - **The raw-link limit.** A console RESET during a raw stream of notes will usually leave the N8 OS at ERROR:40: on
    the lab's cart, a note byte landing about 330 to 400 ms into the booting OS's life breaks it, as the booting OS
    reads its MCU's status from the FIFO and takes the note byte for one (one note at 200, 300 or 1000 ms after the
    reset is clean; notes every 60 ms over 200 to 600 ms are not, over 400 to 600 ms are), and a raw link cannot see the
    reset before the menu answers (the OS answers no probe for about its first second). The guard still trips and writes
    nothing after, but the damage is done. The Pico box's raw build met it as `ERROR:48 Mapper file not found` instead
    (3 RESETs of 3): the OS takes the install, then fails every start until a second RESET. MIDI clock alone is safe
    (`F8` every 20 ms over the same window is clean), and so is an idle menu taking notes. A framed link streaming when
    the RESET comes is protected: the stall shows in 100 ms, nothing more is written, and the guard trips 1.1 s after
    the acks stop, with the menu clean, on the PAL NES and on an NTSC Famicom alike. That is what was measured. A RESET
    during a pause in the stream is protected too: the next note's frames and the stall's probes reached the booting OS
    within about 370 ms of the RESET, 6 of 6 clean (one with 10% test loss). A menu
    at ERROR:40 still answers `*t` with `k`, and a second RESET brings back a clean menu. `retroplug-n8-hwtest
    bootwindow` and `pausereset` measure it.
- **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
  `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session` and `pnpm test:native n8-sdk` (`connectN8`
  over the fake N8); for the box, `retroplug-device-mode-test "[box]"` (its cart link's framed link and guard are
  `[box-link]`), `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
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
  flight: a push takes a lock the writer holds only for a swap. A session's EXIT is where the close bytes go out: the
  provider's destroy disconnects the link, which writes the device's `exitToLiveMode` bytes (a pad left in Programmer
  mode has its front panel locked) before the ports close. That is the CLI's teardown path, so it runs on a normal
  `tjs.exit` as it does after a thrown session or Ctrl-C
  ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)). The pack's portable core is held to MicroQuickJS
  by the device-pack toolchain's scenario mode (byte-identical transcripts under Node and `retroplug-mqjs-run`), but it
  declares no `script` member, so the scenarios are the proof and no box image carries it.
- **Prove a change:** `retroplug-launchpad-test` (one of the seventeen `pnpm test:plugin` binaries),
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

- **MI.MAP's quantise, in two clocks.** The app's `quantise` knob has four timed values beside `immediate` and
  `rowEnd`: `beat` and `bar` count the CART's clock, and `hostBeat` and `hostBar` the DAW's. The cart is clocked at
  24 / tickDivisor ticks a quarter, so its beat is 24 x divisor DAW ticks and its bar 96 x divisor (one LSDj phrase),
  phased from ppq 0; the host values keep 24 and 96 whatever the cart does. The divisor reaches the app on its
  controller context (`ControllerCtx.tickDivisor`, read every block from the session's settings, so a change needs no
  rebuild). A project at the default divisor 1 behaves exactly as it always has.
- **Prove a change:** `pnpm test controller`, `pnpm test script/nodes/controller-session`,
  `pnpm test graph/build-graph`, `pnpm test lsdj-launchpad`, and `pnpm test:plugin` (`retroplug-launchpad-test` for
  the transport the session's surface reads).

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

MIDI OUT LEAVES THE AUDIO CALLBACK here, and only here: this app drains the Engine's MIDI out from the audio callback
([audioLoop.cpp](../packages/native/sdl/audioLoop.cpp)), and a port's send can block until the bytes are on the wire
(WinMM's SysEx path waits for a 538-byte Launchpad repaint, about 170 ms at DIN rate), so its `MidiStream` runs in
`Threaded` mode. Each selected hardware output then has a `PortWriter` of its own
([PortWriter.hpp](../packages/midi/native/src/midi/PortWriter.hpp)): `send` copies the message into a preallocated ring
(all or nothing, so a SysEx is never torn) and wakes a thread that does the blocking write. The wake is a wait on a flag
word with a message counter beside it: the writer's snapshot of the counter is taken at the TOP of each pass, before it
looks at the ring, and all four operations are `seq_cst`, so a push that lands as the writer goes idle wakes it or is
caught by the re-read (a snapshot taken after the look leaves the record queued). A message that does not fit is
dropped whole and counted, and the stream reads each writer's `dropped()` itself, printing one
`MIDI out (<port>): N message(s) dropped` line when it lets a writer go, so a lost message is not invisible outside the
tests. A port leaving the selection, or the stream closing, stops its writer with a bounded FLUSH, because a dropped
Note Off is a stuck note: `kFlushDeadline` counts 500 ms from the stop, with up to two sends on top (one already
running, one that started just before the deadline), and what the deadline leaves is dropped and counted. A threaded
writer whose port never opened starts no thread. The CLI's and the test host's MIDI facet stays `Inline` - the caller
there is the JS thread, and a script relies on a returned `midiSend` being on the wire - and the virtual output is
inline in both modes.

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
