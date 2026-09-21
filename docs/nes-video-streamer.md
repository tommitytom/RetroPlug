# NES video streamer (RetroPlug-driven, N8-deployed)

A from-scratch NES program that displays **live or streamed video**, developed emulator-first
inside RetroPlug and deployed to a real EverDrive N8 Pro. The host (a Pi running the RetroPlug
CLI, or the dev box) processes a video source, converts it to NES tiles, and streams it into the
running NES - over the N8 USB FIFO (live) or from an SD file the NES pulls (pre-rendered).

This is deliberately NOT the NESFlix work. NESFlix is a pre-baked CHR-ROM GIF player on MMC5;
its frames are fixed at build time. This project is the opposite: a purpose-built ROM on a
**CHR-RAM** mapper chosen for streaming, whose pixels arrive at runtime. The two share only the
FIFO seam, the emulator-first workflow, and the host image-quantization pipeline. See
[nesflix-sync.md](nesflix-sync.md) for the sync/visualizer side; this doc is the streamer.

The bet: because RetroPlug's emulated NES core is byte-identical to a real N8 at the FIFO / SD
boundary, we can build the entire display engine, transport protocol, and host pipeline against
emulation - with framebuffer readback as a fidelity oracle - and the exact bytes then drive real
hardware unchanged. Same playbook that made risa sync work on the first hardware try.

---

## Why RetroPlug is the right dev harness

Everything the streamer needs on the emulator side already exists:

- **The N8 FIFO is emulated** at `$40F0/$40F1`
  ([NesEverdriveFifo.hpp](../packages/native/src/system/mesen/NesEverdriveFifo.hpp)) - the exact
  register interface a real N8 exposes. `pushBytes` (host -> NES) and the ROM's `$40F0` reads are
  the same bytes hardware would move.
- **The Edio SD file protocol is emulated** (`execFileRead` / `F_FOPN` / `F_FRD` in the same
  file), served from a host directory. So an SD-pull streamer can be built and tested with zero
  hardware.
- **Mesen emulates any mapper + CHR-RAM**, so we can prototype on whatever mapper we choose
  before confirming the N8 supports it.
- **The NES framebuffer is renderable** (the UI/screenshot/render path), so a decoded frame can
  be captured and compared to the source video - a headless fidelity test (tile-diff / PSNR).
- **The byte stream fans out to real hardware** via `Engine::setCoreByteSink` -> `N8Link`, so the
  same generated stream drives the emulated core and a physical cart, byte-identical.

Net: a working "NES plays streamed video" demo lives in the emulator long before a cart is
touched, and hardware bring-up reduces to two measured unknowns (mapper support + real USB
throughput), not a design gamble.

---

## The RetroPlug tooling loop (why iteration is fast)

The reason to build this inside RetroPlug rather than a standalone toolchain is iteration time.
The UI + emulated instance give a live authoring loop:

- **Hot-reload the ROM.** The efsw file-watcher is already wired for ROM hot-reload
  (`NativeFileWatcher` behind `HostRpcService::drainChangedPaths`); rebuild the streamer ROM and
  the running instance reloads, no manual reload.
- **Hot-reload the host pipeline (TS).** The video conversion + streaming logic is a TS module the
  CLI/UI runs; edit the quantizer or the delta encoder, reload, see the emulated NES output change
  immediately.
- **Framebuffer readback as ground truth.** Capture the emulated NES output next to the source
  frame to eyeball or diff fidelity while tuning the quantizer.
- **Live protocol tweaking.** Because the FIFO push is just bytes, protocol changes (framing,
  delta format) are a TS edit + reload, not a reflash.

Compared to the classic "edit converter -> rebuild ROM -> flash cart -> squint at a CRT" loop,
this is sub-second and fully headless-testable.

---

## Mapper selection (the key freedom)

Building from scratch means choosing the mapper for the job. Requirements, in priority order:

1. **CHR-RAM** (writable pattern tables) - non-negotiable; the whole point is runtime pixels.
2. **Enough CHR-RAM to double-buffer** - fill an off-screen bank while displaying another, then
   swap, to avoid tearing. Want >= 2x the per-frame CHR footprint.
3. **Emulated by Mesen AND implemented by the N8's FPGA** - the real gate. Mesen covers
   everything; the N8 core list is the constraint to verify up front.
4. **A scanline IRQ (nice-to-have)** - lets the ROM swap CHR banks mid-frame, so a screen can show
   more than 256/512 unique tiles - i.e. higher effective resolution. Trades simplicity for detail.

Candidate families (evaluate against the N8 core list - do not assume support):

| Mapper | CHR-RAM | IRQ | Notes |
|--------|---------|-----|-------|
| UNROM 512 (30) | 32 KB (8x4 KB) | no | Homebrew standard, lots of CHR-RAM for buffering, simple. Strong default. |
| GTROM (111) | 16 KB | no | 4 nametables (double-buffered *nametables* too), homebrew-friendly. |
| MMC3 (4), CHR-RAM cfg | yes | scanline | IRQ enables mid-frame CHR banking -> more detail; ubiquitous. |
| MMC5 (5), CHR-RAM cfg | yes | scanline | Most capable (ExRAM extended attributes could lift color fidelity), most complex. |

The core trade: **UNROM512-style (big CHR-RAM, no IRQ)** = simplest engine, buffer-friendly, capped
at 256/512 unique tiles per frame (fine for a small/quarter-screen target); **MMC3/MMC5 (IRQ)** =
mid-frame banking for higher resolution at the cost of tighter timing. Recommend starting on the
simplest CHR-RAM mapper the N8 supports (likely UNROM512), prove the pipeline, then evaluate an
IRQ mapper only if resolution demands it.

Out of scope: custom FPGA cores (not user-loadable on a stock N8) and the memWR/direct-PSRAM write
path (see Transport - fragile while running).

---

## Display engine (the ROM side, from scratch)

- **CHR-RAM double/triple buffer.** Reserve N CHR-RAM banks; the host streams into an off-screen
  bank; the ROM swaps it in on a vblank once complete. No partial frame is ever shown.
- **Nametable strategy.** The NES background can show only 256 (one 4 KB window) or 512 (8 KB)
  unique tiles at once; a full 256x240 unique-per-tile screen needs 960. Options:
  - *Fixed identity nametable + swap CHR* (like NESFlix): target a smaller image (128x128 ≈ one
    4 KB bank = 256 tiles). Simplest; the default.
  - *Mid-frame CHR banking via a scanline IRQ* (MMC3/MMC5): show more unique tiles by rebanking
    per screen region - higher resolution, tighter timing.
- **Transfer window.** CHR-RAM writes go through the PPU (`$2007`), only safe during vblank or
  forced blank. Forced blank (screen off) gives a full frame of write bandwidth but blacks the
  display, so pair it with double-buffering (write the hidden bank blanked-off, then swap) or keep
  updates within the ~280-byte vblank budget for incremental/delta writes.
- **Delta application.** The on-ROM decoder applies a changed-tile list (tile index + 16 bytes)
  rather than a full 4 KB each frame - the single biggest fps lever for real video.

---

## Transport + protocol

One on-ROM decoder, two interchangeable byte sources (same frame format):

### A. Live push over the N8 USB FIFO (host -> NES)

Host streams frames into `$40F0`; the ROM drains and applies them. A minimal framing over the raw
byte channel:

- `frame-start` marker + target-buffer id
- palette updates (if changed): count + `(index, color)` pairs
- changed-tile list: count + `(tileIndex, 16 bytes CHR)` records
- `frame-end` -> ROM swaps the buffer in on the next vblank

**Flow control** is the crux: the FIFO has finite depth and the NES drains at a fixed rate, so the
host must pace to the NES's consumption (either a fixed budgeted rate, or the ROM ACKs a frame via
`$40F0`-write back through the Edio TX path). Overrun = dropped/torn frames.

### B. SD pull via Edio `F_FRD` (NES -> reads a pre-rendered file)

The ROM issues `F_FOPN`/`F_FRD` (the emulated protocol already serves these) to read a pre-encoded
stream file from the SD card at its own pace. The host's only job is to write that file (via the
SD write path). Robust (no live-timing coupling), supports arbitrarily long video decoupled from
ROM size, and fully emulator-supported today. The more reliable cousin of live streaming.

### C. Direct N8 PSRAM write (speculative - investigate, do not design around)

In principle the N8 could write CHR-RAM directly from USB, bypassing the CPU->PPU bottleneck.
But memWR while the cart is running is fragile (it corrupts live state in existing use), and
frame-coherent direct writes are unproven. Flag as an R&D probe, not a foundation.

---

## Host video pipeline (TS, scriptable)

Per source frame, all in TS (the CLI-TS direction, unit-testable on recorded input):

1. **Decode / capture** the source (file or live feed).
2. **Downscale** to the target (128x128, or smaller for fps headroom).
3. **Quantize to the NES palette** under the attribute-table constraint - one 4-color subpalette
   per 16x16-pixel cell. This is the hard, interesting DSP: per-cell palette selection + dithering
   to stay within 2bpp-per-tile and 4 attribute palettes. (MMC5 ExRAM extended attributes, if that
   mapper is chosen, relax this to per-tile palettes.)
4. **Pack to CHR** (two bitplanes per 8x8 tile).
5. **Delta-encode** vs the previous frame - emit only changed tiles + changed palette entries.
6. **Frame the stream** (push framing for path A, or the on-disk stream format for path B).

Reuses the project's existing image plumbing (the PNG codec + the LSDj/risa asset tooling patterns).

---

## Bandwidth budget (honest numbers)

NTSC NES: ~1.79 MHz, ~29,780 CPU cycles/frame. Interleaved FIFO-drain + PPU-write costs roughly
~25 cycles/byte, so:

| Target | Bytes/frame | Full-frame rate | With delta encoding |
|--------|-------------|-----------------|---------------------|
| 128x128 (one 4 KB bank) | ~4 KB | ~10-20 fps | much higher for typical video (static regions skipped) |
| Full 256x240 unique | ~16 KB | ~3-5 fps | depends on motion |

Two honest caveats:

- **The emulator has no transport limit** (`pushByte` is instant), so it shows an optimistic
  ceiling. The **real N8 USB -> FIFO throughput is an unmeasured unknown** and may be the true
  bottleneck - measure it early on hardware.
- Forced-blank full-frame transfers flicker unless hidden behind double-buffering; the fps figures
  assume a reasonable banking scheme, not naive per-frame `$2007` blasting.

Realistic product target: a small-resolution, delta-encoded feed at a watchable framerate - a
"NES video wall" aesthetic, not broadcast video.

---

## Verification (emulator-first, then hardware)

- **Decoder unit tests (TS).** Golden frame -> expected byte stream; delta encoder round-trips.
- **Emulated fidelity test.** Push a known clip, capture the emulated NES framebuffer, tile-diff /
  PSNR against the (quantized) source. Deterministic, headless, no hardware.
- **SD-pull test.** Stage an encoded stream file in the emulated SD root, run the ROM's `F_FRD`
  reader, assert the same decoded frames.
- **Byte-identity to hardware.** Assert the stream fed to the emulated FIFO equals what
  `setCoreByteSink` -> `N8Link` would mirror to a real N8 (the risa parity check).
- **Hardware bring-up (confirm before firing).** Pi + N8 + the streamer cart: measure real USB
  throughput and the achievable fps, watch a clip play on a real NES.

---

## Phasing

- **P0 - mapper spike.** Pick a CHR-RAM mapper the N8 *and* Mesen support; minimal ROM that shows a
  static image written into CHR-RAM from the FIFO. Proves the writable-pixels foundation.
- **P1 - display engine.** Double-buffered CHR-RAM swap; stream a canned frame sequence over the
  FIFO in emulation; framebuffer readback confirms motion.
- **P2 - host pipeline.** TS decode -> downscale -> attribute-aware quantize -> CHR-pack -> delta.
- **P3 - SD-pull variant.** The `F_FRD` reader path (robust, long clips, host just preps the file).
- **P4 - bandwidth tuning.** Deltas, forced-blank vs vblank, resolution/fps trade; find the ceiling.
- **P5 - hardware (optional, confirm first).** Real N8 throughput measurement + a clip on a real NES.

P0-P1 are the "NES shows runtime pixels" milestone; P2 makes it real video; P3 makes it robust;
P5 is the hardware payoff.

---

## Open questions / risks

- **N8 mapper support is the gating unknown.** The whole project rides on the N8's FPGA exposing a
  CHR-RAM mapper we can also emulate. Resolve in P0 before anything else.
- **Real USB -> FIFO throughput** may cap fps well below the emulator's ceiling - measure early.
- **Attribute-table color fidelity** (one 4-color subpalette per 16x16 cell) is the quality
  ceiling; the quantizer's per-cell palette + dithering is where the visual quality is won or lost.
- **Flicker** with forced-blank transfers unless double-buffering is clean.
- **Audio is out of scope** for the streamer itself; if music is wanted alongside, run a separate
  music system (mGB/LSDj/risa) and, per the NESFlix doc's closed-loop idea, optionally drive the
  visuals from its audio.
- **Scope honesty:** at full ambition this is its own homebrew NES project (a streaming cart +
  engine + host pipeline), not a small RetroPlug feature. The value is that RetroPlug collapses the
  build/test/deploy loop for it.
