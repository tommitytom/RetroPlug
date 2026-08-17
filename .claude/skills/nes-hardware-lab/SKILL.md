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

## 0. Prereqs

- Build the CLI once: `./build.sh` (from a RetroPlug worktree) -> `build/bin/retroplug-cli` +
  `build/bin/retroplug-n8-hwtest`. Both auto-detect the N8 (VID:PID `38df:0017`), so `--serial` is
  usually optional; pass `--serial /dev/ttyACM0` (or your N8 node) if auto-detect fails.
- On boot the N8 sits at its **file-browser menu**; no game auto-runs. `n8-load` boots one.

## 1. Power (Home Assistant)

`/workspaces/.nes-power.sh {status|on|off|reset [secs]}`

- `reset` cycles the plug (off, wait, on). After a reset the N8 takes **~30 s** to reach its menu -
  `sleep 35` before talking to it. The N8's USB re-enumerates on power, so a stale `/dev/ttyACM*`
  node may change (auto-detect handles it).
- Use `reset` to recover a **wedged/crashed game** or a **dirty N8 menu** (see the OOM gotcha below).
- The HA token lives at `/workspaces/.ha_token` and is a **secret** - never print or commit it; the
  script reads it for you.

## 2. Run code on the NES (Everdrive N8 over USB)

`retroplug-cli n8-load [options] [<rom.nes>]` drives the N8's on-device menu to load + boot a ROM.
The N8 firmware parses the iNES header and sources the mapper core from its own SD card.

- **Boot a local ROM:** `retroplug-cli n8-load path/to/game.nes` (uploads to `usb-games/` and boots).
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

`retroplug-n8-hwtest <dump|load|restore|peek|poke|read|vramdump|sniff|memwr|info|fstest> <addr|path> [len|byte|dest] [port]`

Direct device memory + FIFO. Key N8 addresses: cart battery RAM `0x1000000`, live sniffer region
`0x1802000`, cart FIFO `0x1810000` (a running ROM reads it at `$40F0/$40F1`).
- `peek <hexaddr> <len>` / `poke <hexaddr> <byte>` - read/write device memory.
- `memwr <hexaddr> <file>` - write a file's bytes to an address; `memwr 0x1810000 <file>` pushes bytes
  into the cart FIFO for a ROM that consumes it.
- `sniff` / `info` - same data as the CLI, bare.

## 3. Capture the NES audio (Zoom L6)

The NES analog audio is on the **Zoom L6** USB interface: ALSA card `hw:L6,0`, format **S32_LE**,
**48000 Hz**, up to **12 channels**. The main **2A03** output lands on capture **channel 3** (record
all 12 and extract ch3; ch4 carries the pair).

Record a few seconds while something plays:
```sh
arecord -D hw:L6,0 -f S32_LE -r 48000 -c 12 -d 5 /tmp/nes.wav
```
Then extract/analyze channel 3 (0-indexed 2). For pitch/spectral checks, prefer the repo's DSP tools
over ad-hoc code: `packages/retroplug/cli/pitch.ts` (`detectPitch`), `spectral-metrics.ts`
(`harmonicEnergy`/`thd`/`noiseFloorDb`), and the `analyze-mgb` / `analyze-lsdj-sync` CLI sessions.

Quick per-channel level + dominant-pitch sniff (numpy):
```python
import wave, numpy as np
w=wave.open('/tmp/nes.wav','rb'); n,ch,fr=w.getnframes(),w.getnchannels(),w.getframerate()
a=np.frombuffer(w.readframes(n),dtype='<i4').reshape(-1,ch).astype(float)/2**31
x=a[:,2]-a[:,2].mean()                                   # ch3 = 2A03
print('ch3 dBFS', 20*np.log10(np.sqrt((x**2).mean())+1e-12))
sp=np.abs(np.fft.rfft(x*np.hanning(len(x)))); f=np.fft.rfftfreq(len(x),1/fr)
print('fundamental', f[f>60][np.argmax(sp[f>60])], 'Hz')
```

**Verification discipline:** a real NES tone TRACKS what you play - if you inject different
notes, the measured pitch must move. A fixed tone that doesn't change with input is a capture
artifact or a stuck note, NOT proof the game is playing. Cross-check pitch against `--sniff`
(the APU timer). The NES here is PAL: `f = 1662607 / (16 * (timer + 1))`.

## 4. Capture the NES video (USB capture card)

The NES video is on a UVC USB capture card at **`/dev/video0`** (YUYV, 720x576 @ 25fps and
720x480 @ 30fps). Grab a single frame to see the current screen (menu or game):
```sh
ffmpeg -y -f v4l2 -input_format yuyv422 -video_size 720x576 -i /dev/video0 -frames:v 1 /tmp/nes.png
```
Read `/tmp/nes.png` to see the N8 menu or the running game. Use this to confirm a ROM booted, read
on-screen text, or watch a game's state. (`/dev/video1` is the same card's second node.)

## Typical loop

1. `nes-power.sh reset` -> `sleep 35` (fresh menu).
2. `n8-load game.nes` -> ROM boots on the real NES.
3. Grab `/dev/video0` frame to confirm it's on screen; `--sniff` for live register state.
4. `arecord ... hw:L6,0` while it plays -> analyze ch3 for the audio.
5. If it wedges or the menu goes OOM: `nes-power.sh reset` and retry.
