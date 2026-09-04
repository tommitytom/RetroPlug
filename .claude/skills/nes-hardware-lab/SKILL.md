---
name: nes-hardware-lab
description: >-
  Control the physical NES + Everdrive N8 Pro wired to this dev container, and run code on the
  real console. Use whenever the task involves the REAL hardware (not the emulator): load / boot /
  live-patch a .nes ROM on the N8 over USB, read a running game's live state (APU/PPU/CHR/save),
  record the NES audio off the Zoom L6 capture interface, grab NES video frames off the USB capture
  card, or power-cycle the console via Home Assistant. Triggers on "on the real NES/N8", "load a ROM
  on hardware", "record the NES audio", "screenshot/grab the NES screen", "power-cycle the console",
  "is it actually playing on hardware".
---

# NES hardware lab

A real NES with an Everdrive N8 Pro cartridge is wired to this container. Four things are passed
through and controllable from the shell:

- **N8 over USB** - load/boot/patch ROMs and read live game state (`retroplug-cli` / `retroplug-n8-hwtest`).
- **Audio** - the NES analog output is captured by a Zoom L6 USB interface (ALSA `hw:L6`).
- **Video** - the NES video feed is on a USB capture card (`/dev/video0`, V4L2).
- **Power** - the console's mains plug is a Home Assistant switch (`/workspaces/.nes-power.sh`).

You can't see or hear the console directly - **capture audio/video to observe it**. When you claim
something plays or shows on screen, back it with a recording/frame, not an assumption.

The rig is shared by the **RetroPlug** and **BlipToaster** dev containers - both pass the same four
devices through, and both see the same `/workspaces` mount (so the power script and its token are
already there). Only the binary location differs; see [Prereqs](#0-prereqs).

> **NEVER use Python. Under any circumstances.** No `python`/`python3`, no numpy, no venv, no inline
> `-c` snippets - not for audio analysis, not for MIDI bytes, not for "just a quick check". Everything
> here is `retroplug-cli` plus ordinary shell tools (`arecord`, `ffmpeg`, `dd`). If a measurement
> looks like it needs Python, it needs a `retroplug-cli` subcommand instead - see
> [Adding a measurement](#adding-a-measurement) for how, from either repo. That keeps every hardware
> measurement reproducible, reviewable and shared with the emulator-side tests.

## 0. Prereqs

This skill works from **either** repo on this machine, and the only difference is where the two
binaries come from. Set `RPBIN` once and every command below is copy-pasteable:

| you are in | binaries | how to get them |
|---|---|---|
| a **RetroPlug** worktree | `build/bin/` | `./build.sh`, plus `cmake --build build --target retroplug-n8-hwtest -j$(nproc)` (it's `EXCLUDE_FROM_ALL`) |
| the **BlipToaster** repo | `retroplug-cli/bin/` | prebuilt + gitignored; populate from a RetroPlug checkout with `tools/sync-cli-to-bliptoaster.sh`. **That image has no C++ toolchain - you cannot build them here.** |

```sh
RPBIN=build/bin            # RetroPlug worktree
RPBIN=retroplug-cli/bin    # BlipToaster repo
```

- Both binaries auto-detect the N8 (VID:PID `38df:0017`), so `--serial` is usually optional; pass
  `--serial /dev/ttyACM0` (or your N8 node) if auto-detect fails.
- On boot the N8 sits at its **file-browser menu**; no game auto-runs. `n8-load` boots one.
- **The N8 serial is exclusive.** Both dev containers pass `/dev/ttyACM0` through, but only one
  process can hold it. `Edio: serial read timeout` can therefore also mean "the other container is
  driving the N8", not just "a game is running".

## 1. Power (Home Assistant)

`/workspaces/.nes-power.sh {status|on|off|reset [secs]}`

- `reset` cycles the plug (off, wait, on). After a reset the N8 takes **~30 s** to reach its menu -
  `sleep 35` before talking to it. The N8's USB re-enumerates on power, so a stale `/dev/ttyACM*`
  node may change (auto-detect handles it).
- Use `reset` to recover a **wedged/crashed game** or a **dirty N8 menu** (see the OOM gotcha below).
- The HA token lives at `/workspaces/.ha_token` and is a **secret** - never print or commit it; the
  script reads it for you.

## 2. Run code on the NES (Everdrive N8 over USB)

`$RPBIN/retroplug-cli n8-load [options] [<rom.nes>]` drives the N8's on-device menu to load + boot a
ROM. The N8 firmware parses the iNES header and sources the mapper core from its own SD card.

- **Boot a local ROM:** `$RPBIN/retroplug-cli n8-load path/to/game.nes` (uploads to `usb-games/` and
  boots). From BlipToaster the ROM is `build/bliptoaster.nes` or a mapper variant
  (`make MAPPER=s5b clean all` -> `build/bliptoaster-s5b.nes`).
- **Boot a ROM already on the SD:** `n8-load --sd-path usb-games/game.nes`.
- **Device info:** `n8-load --info` (serial, firmware, form factor, flash, voltages) - a quick "is
  the N8 alive over USB" check.
- **List / read SD files:** `--ls <dir>` (use `/` for root), `--get-file <sd-path> <local>`, `--df`
  (free space), `--mkdir <path>`, `--rm <path>` (permanent).
- **GOTCHA - dirty menu OOM:** if a load fails with "out of memory", the menu heap is dirty from a
  prior failed load -> **power-cycle** (`nes-power.sh reset`) to a fresh menu and retry.

### Observing / introspecting a running game

A game must be running for these (the sniffer is off at the menu):

- **Live APU/PPU/OAM state:** `n8-load --sniff` (decoded) or `--sniff-raw <file>` (raw 512 B). This is
  the ground-truth of what the game is actually driving - use it to confirm a note is playing, a
  register is set, etc., independent of the audio/video capture.
- **Menu screenshot over USB:** `n8-load --screenshot <out.png>` (only while the **menu** is showing;
  for a running game use the video capture card instead - see below).
- **Graphics:** `--dump-chr <out.png>` (visible CHR tiles; add `--color [--palette N]` for real
  colours), `--patch-chr <hexoff> <file>` (live-patch graphics, shows next frame).
- **Code:** `--patch-prg <hexoff> <file>` live-patches PRG (a bad patch crashes the game ->
  power-cycle to recover). `--savestate <sd-path>` decodes a full N8 save-state.
- **Saves:** `--dump-sram <file>` reads cart battery RAM; `--srm <save.srm>` restores one on boot.

### Low-level access (`retroplug-n8-hwtest`)

`$RPBIN/retroplug-n8-hwtest <dump|load|restore|peek|poke|read|vramdump|sniff|memwr|fifowr|info|fstest> <addr|path> [len|byte|dest] [port]`

Direct device memory + FIFO. Key N8 addresses: cart battery RAM `0x1000000`, live sniffer region
`0x1802000`, cart FIFO `0x1810000` (a running ROM reads it at `$40F0/$40F1`).
- `peek <hexaddr> <len>` / `poke <hexaddr> <byte>` - read/write device memory.
- `memwr <hexaddr> <file>` - write a file's bytes to an address, **with a readback verify** (for
  live-patching CHR/PRG).
- `fifowr <file>` - push bytes into the cart FIFO. Use this, NOT `memwr 0x1810000`: `memwr` always
  verifies by reading back, and a FIFO is drained by the NES, so that verify can never pass.
- `sniff` / `info` - same data as the CLI, bare.

## 2b. Drive a ROM with MIDI (`n8-play`)

`$RPBIN/retroplug-cli n8-play [--serial <port>] [--exp-vol <0-255>] <step>...` plays a **scripted** MIDI
sequence into the cart FIFO, so a BlipToaster check is one reproducible command with no controller
attached. (`n8-bridge` is the live twin, and needs a real MIDI input port.)

Steps are 1-based on MIDI channel, matching the BlipToaster monitor's `CH` column:
`on:<ch>:<note>[:<vel>]`, `off:<ch>:<note>`, `cc:<ch>:<num>:<val>`, `wait:<ms>`.

```sh
# hold a 2 s A4 on the S5B's Square A with the hardware envelope on
$RPBIN/retroplug-cli n8-play --exp-vol 128 cc:6:29:80 cc:6:28:64 cc:6:20:127 on:6:69 wait:2000 off:6:69
```

- **`--exp-vol` is REQUIRED for expansion audio** (VRC6 / VRC7 / N163 / S5B / MMC5). It writes the
  FPGA master volume (`0x1800023`, 0 mute / 128 unity / 255 2x), which the N8 mixes expansion audio
  through. Get it wrong and every expansion voice is silent no matter how correct the ROM is - a
  trap that reads exactly like a chip bug. Write-only and live-only (applies to the running cart).
- BlipToaster **drops its first MIDI message after boot**; `n8-play` sends a priming CC automatically
  (`--prime off` to skip).
- **Confirm receipt independently of audio:** grab a video frame while a note is held. The BlipToaster
  monitor lights the channel's note dot and shows `KEY`/`LEVEL`, which proves the FIFO -> parse path
  worked even when you hear nothing.

## 3. Capture the NES audio (Zoom L6)

The NES analog audio is on the **Zoom L6** USB interface: ALSA card `hw:L6,0`, format **S32_LE**,
**48000 Hz**, up to **12 channels**. Two SEPARATE feeds are wired:

| capture channel | carries |
|---|---|
| **ch3 / ch4** | the **2A03** (the console's own APU: pulse 1/2, triangle, noise, DMC) |
| **ch5** | the **expansion audio** (VRC6 / VRC7 / N163 / S5B / MMC5, off the N8) |

**Look at ch5 for expansion chips.** They are NOT mixed into ch3 - an expansion note reads as pure
silence on ch3 while sitting at a healthy level on ch5, which looks exactly like "the chip is broken"
and is not. Idle levels tell the channels apart: ch3/ch4 rest at ~-76 dBFS, ch5 at ~-80 dBFS, and a
genuinely unconnected input sits near -107. When unsure which feed a voice is on, run
`analyze-capture` with NO `--channel` - the survey shows every channel at once and the answer is
obvious.

The L6 offers **only `S32_LE` and `FLOAT_LE`** - `S16_LE` fails with "Sample format non available".
Record a few seconds while something plays, then measure it with `analyze-capture`:

```sh
arecord -D hw:L6,0 -f S32_LE -r 48000 -c 12 -d 5 /tmp/nes.wav
$RPBIN/retroplug-cli analyze-capture /tmp/nes.wav                    # level survey of all 12 channels
$RPBIN/retroplug-cli analyze-capture /tmp/nes.wav --channel 3 --expect-hz 440
```

`analyze-capture` reports level, fundamental (`detectPitch`, with cents error vs `--expect-hz`), a
short-time **envelope** (min/max/swing over the sounding part), and `--band lo:hi` energy. It shares
RetroPlug's DSP helpers (`packages/retroplug/cli/pitch.ts`, `cli/spectral-metrics.ts`) with the
emulator-side tests, so a hardware number is comparable with a rendered one.

- **The envelope swing is the point for hardware-envelope chips.** A flat tone and one pulsing under
  the Sunsoft 5B's envelope generator have the SAME rms - only the swing separates them.
- **A silent capture reads as ~-76 dBFS** on ch3/ch4 (the real analog noise floor with the console
  idle); genuinely unconnected inputs sit near -107. So "-76" means connected-but-silent, and the
  default `--floor -70` treats anything above it as sounding.
- To record and drive at once, background the `arecord` and give it ~0.5 s before `n8-play`.

**Verification discipline:** a real NES tone TRACKS what you play - if you inject different
notes, the measured pitch must move. A fixed tone that doesn't change with input is a capture
artifact or a stuck note, NOT proof the game is playing. Cross-check pitch against `--sniff`
(the APU timer): `f = 1662607 / (16 * (timer + 1))`, because **this console is PAL**.

> **`--sniff` prints NTSC-decoded frequencies, and this console is PAL - so its Hz reads ~7.6% HIGH.**
> `decodeSniffer()` takes a `cpuHz` that defaults to `CPU_HZ_NTSC`
> ([src/n8/sniffer.ts](../../packages/retroplug/src/n8/sniffer.ts)), and `n8-load --sniff` doesn't
> override it. A capture measuring 440 Hz against a sniff reading 474 Hz is **agreement, not a tuning
> bug**: 474 x 1662607/1789773 = 440.3. Convert before you compare, or you'll go hunting a 7.6% error
> that isn't there.

## 4. Capture the NES video (USB capture card)

The NES video is on a UVC USB capture card at **`/dev/video0`** (YUYV, 720x576 @ 25fps and
720x480 @ 30fps). Grab a single frame to see the current screen (menu or game):
```sh
ffmpeg -y -f v4l2 -input_format yuyv422 -video_size 720x576 -i /dev/video0 -frames:v 1 /tmp/nes.png
```
Read `/tmp/nes.png` to see the N8 menu or the running game. Use this to confirm a ROM booted, read
on-screen text, or watch a game's state. (`/dev/video1` is the same card's second node.)

## Typical loop

1. `/workspaces/.nes-power.sh reset` -> `sleep 35` (fresh menu).
2. `$RPBIN/retroplug-cli n8-load game.nes` -> ROM boots on the real NES.
3. Grab `/dev/video0` frame to confirm it's on screen; `--sniff` for live register state.
4. Background `arecord ... hw:L6,0`, drive it with `n8-play` (MIDI ROMs) or just let it run, then
   `$RPBIN/retroplug-cli analyze-capture <wav> --channel 3`.
5. If it wedges or the menu goes OOM: `/workspaces/.nes-power.sh reset` and retry.

Always take a **known-good control** in the same session before believing a negative. For BlipToaster
that's a 2A03 note (`n8-play on:1:69 wait:2500 off:1:69` -> ch3 should read ~440 Hz within a few
cents): it proves the FIFO, the ROM, the analog path and the capture all work, so a silent expansion
chip is a real finding and not a broken rig. For an **expansion** chip take the control on that side
too - a known-good VRC6 note on ch5 - before concluding a chip is dead.

**Loading a second ROM needs a power-cycle.** `n8-load` drives the N8's file-browser MENU, and a
running game isn't the menu: the load fails with `Edio: serial read timeout (no N8 response)`. That
timeout means "a game is running", not "the N8 is broken". `/workspaces/.nes-power.sh reset` ->
`sleep 40` -> load.

## Adding a measurement

A hardware number that `analyze-capture` / `--sniff` can't produce yet is a **missing
`retroplug-cli` subcommand**, never a Python script. How you add one depends on where you are:

- **In a RetroPlug worktree:** write it as `packages/retroplug/cli/sessions/*.ts`, register it in
  `cli/tools.ts` (the only place a tool is registered), then
  `cmake --build build --target retroplug-cli -j$(nproc)`.
- **In the BlipToaster repo:** you can't - that image carries no C++ toolchain, and the binaries are
  synced artefacts. Do the work on the RetroPlug side (the `cli-debug` skill covers the handoff),
  then re-sync with `tools/sync-cli-to-bliptoaster.sh` and re-run here. **Do not** work around the
  gap with a throwaway script.

Either way the new subcommand is shared with the emulator-side tests, which is the whole point: the
hardware measurement and the rendered one stay comparable.
