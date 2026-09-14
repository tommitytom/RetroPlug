# NESFlix host-sync plan (Options 2 + 3)

Add host-sync to NESFlix (NO CARRIER, 2011 - the NES "GIF player") by reusing RetroPlug's
existing NES EverDrive N8 FIFO seam, the same machinery that drives risa host-sync and was
hardware-verified against a real 2A03. Two layers:

- **Option 2 - FIFO frame-lock:** the video advances one animation frame per clock tick,
  following a start / clock / stop stream. Works in the emulator AND on a real N8.
- **Option 3 - sequence the visuals:** once the FIFO channel exists, carry more than a
  clock - palette / position / direction / scroll / absolute-frame scrub. A strict superset
  of Option 2.

**The likely driver is NOT a DAW.** The realistic deployment is a **Raspberry Pi running the
RetroPlug CLI**, which is itself the clock master - either generating its own clock, or
processing an audio feed and deriving control data from it (an audio-reactive visualizer).
The DAW plugin path still works and is a useful test harness, but it is secondary. The one
thing that makes this clean: the FIFO byte protocol is **source-agnostic**, so the ROM patch
(Options 2 + 3) is identical no matter what generates the bytes. Only the generator changes -
see [Driver: Raspberry Pi + RetroPlug CLI](#driver-raspberry-pi--retroplug-cli-the-actual-target).

Target the **MMC5** build (`nesflix_mmc5.asm`); it matches risa's mapper and RetroPlug's
proven NES sync path. The MMC3 twin is the PowerPak/real-hardware-without-N8 variant and is
out of scope here (the asm ports trivially if wanted later).

---

## Background: the two halves that already exist

**NESFlix engine** (`/workspaces/NESFlix/nesflix_mmc5.asm`). The playhead is a single CHR
bank counter. `animate` bumps `bank1` (0..`frames`-1, `frames`=64) in `dir` direction and
writes it to the MMC5 background-CHR register `$512b`. The whole player is:

```
InfLoop:                 ; nesflix_mmc5.asm:236
    jsr WaitFrame        ; block until the next NMI (60 Hz video frame)
    dec ani_counter
    bne no_ani
    lda ani_speed        ; reload the divider
    sta ani_counter
    jsr animate          ; advance ONE frame (one CHR bank), free-running
no_ani:
    jmp InfLoop
```

So today it free-runs: one frame every `ani_speed` NES frames, with no external clock.
Controllers set rate (`ani_speed`), direction (`dir`), position (`ScreenNumber`), palette
(`PaletteNumber`), scroll and color-cycle.

**RetroPlug NES sync seam.** RetroPlug already attaches an emulated EverDrive N8 FIFO to
*every* NES ROM at `$40F0` (data) / `$40F1` (status), benign if unused:

- [NesEverdriveFifo.hpp](packages/native/src/system/mesen/NesEverdriveFifo.hpp) - the FIFO.
  `$40F1` bit 7 set (`0x80`) = empty, clear = byte ready; reading `$40F0` pops one byte.
- [NesN8FifoRole.hpp](packages/native/src/system/mesen/roles/NesN8FifoRole.hpp) -
  `pushBytes(offset, data, count, flush)` schedules bytes into the FIFO at sample offsets,
  released in true order by `pumpUntil`. The same queue feeds host MIDI and a raw protocol.
- [risaSync.ts](packages/retroplug/src/risaSync.ts) + the `risa-sync` role in
  [dspRoles.ts](packages/retroplug/src/dspRoles.ts) - turn the DAW transport into a raw byte
  stream over `ctx.pushCoreBytes` -> the FIFO: `0xFA` start, `0xF8` clock (24 PPQN), `0xFC`
  stop, `0xF9 0x52 ss cc tt` locate.
- [romProviders.ts](packages/retroplug/src/romProviders.ts) - detects a ROM by header/marker
  and attaches its role(s).

Anything that gets bytes into that FIFO drives the ROM; the byte source is interchangeable:

1. **CLI on a Pi (the actual target)** - a CLI process is the clock master, generating the
   stream from an internal clock or from audio analysis. See the Driver section.
2. **Real N8** - the same generated stream is mirrored to a physical cart
   (`Engine::setCoreByteSink` -> `N8Link`), byte-identical to the emulated stream. On a Pi
   this is the shipping path (arm64 + N8, already proven for risa).
3. **DAW plugin / standalone (secondary)** - transport or MIDI-clock in generates the stream;
   handy as a deterministic test harness even though it is not the deployment.

NESFlix is close to the ideal candidate: it is MMC5, and its playhead is one counter, so the
sync protocol is far simpler than risa's (no song/chain/row/table state to reconstruct).

---

## Driver: Raspberry Pi + RetroPlug CLI (the actual target)

The expected box is a Pi (arm64, which RetroPlug already builds and already drives a real N8
from) running the CLI as a long-lived process: it loads NESFlix into an emulated core and/or
mirrors to a physical N8 cart, and it is the clock master. The ROM protocol below never
changes; the CLI just decides what bytes to emit and when. Two generator modes, and they
compose (an audio-reactive clock IS both):

### Mode 1 - internal clock generator

The CLI free-runs a tempo and emits the Option 2 stream (start once, then `0xF8` at the
configured rate, stop on exit). Two clean ways to produce it, both reusing existing seams:

- **Synthetic transport + role.** The CLI sets `engine.setBpm(bpm)` + `engine.setTransport(true)`
  and lets the `nesflix-sync` DSP role emit the clock exactly as it would under a DAW - the
  role is transport-driven and does not care that the transport is a CLI variable rather than a
  host. Zero new byte-generation code.
- **Direct byte generator (TS).** A CLI session (like the existing `n8-sync` tool) emits the
  bytes itself on a timer. More explicit, and the natural base for Mode 2.

This alone is a self-contained "play the GIF at a musical rate on a real NES" appliance.

### Mode 2 - audio-reactive (process an audio feed, derive control data)

The compelling one for a visualizer: the CLI consumes an audio feed, runs lightweight analysis,
and maps features to NESFlix control (Option 3 ops, optionally plus a beat-locked clock). The
analysis + mapping is a **TS script the CLI runs** - the whole point of the CLI-TS direction, so
users write their own audio-reactive visual mappings without touching C++.

Feature -> control mapping (all Option 3 ops; the source is audio, not DAW automation):

| Audio feature (per analysis hop, ~100 Hz)      | NESFlix control                              |
|------------------------------------------------|----------------------------------------------|
| beat / tempo estimate                          | frame-advance clock (`0xF8`), or note-locked |
| onset / transient (kick, snare)                | frame trigger / direction flip / palette flash|
| RMS / amplitude envelope                        | animation speed (`clocksPerFrame`) or scroll |
| spectral bands (bass / mid / treble)            | palette select / position / color-cycle speed|
| sustained loudness threshold                    | color-cycle on/off, position jump            |

The audio feed is either:

- **External line-in** - capture on the Pi. This is a **new capability**: expose audio *capture*
  to the CLI/TS (the merge just brought PortAudio in, which does capture; a small "audio-in
  facet" would hand blocks to the TS analysis script). Nothing today captures live audio in the
  CLI.
- **RetroPlug's own generated audio (closed loop, available now)** - run a music system (mGB /
  LSDj / risa) in the *same* CLI, analyze the audio blocks the Engine already produces, and drive
  NESFlix from them. One process = chip music out + a NES visualizer locked to it, both to real
  hardware (music via audio out, visuals via N8). No new capture seam needed.

Analysis cost is trivial at hop rate (envelope + a 3-band filterbank or a small hop-based FFT);
fine in TS on a Pi, or drop to C++ if a heavier onset/beat tracker is wanted later.

### What this needs beyond Options 2 + 3

- A CLI session/tool (`retroplug-cli nesflix` or similar, long-running like `n8-sync`) that owns
  the core + optional N8 mirror and runs the chosen generator. Mostly reuse of `n8-sync` +
  `setCoreByteSink` + the long-session infra.
- For Mode 2 external audio: the audio-in facet (new). Mode 2 closed-loop and Mode 1 need no new
  native seam.
- The analysis/mapping TS module (`src/nesflix/` or a user script), unit-testable on recorded WAV.

---

## Option 2 - FIFO frame-lock (true transport lock)

### Protocol (reuse risa's transport bytes verbatim)

| Byte(s)              | Meaning        | NESFlix action                                          |
|----------------------|----------------|---------------------------------------------------------|
| `0xFA`               | start          | arm playback; re-anchor the playhead (see start policy) |
| `0xF8`               | clock (24 PPQN)| advance one frame every `clocksPerFrame` clocks          |
| `0xFC`               | stop           | freeze (ignore clocks)                                    |
| `0xF9 0x52 ss cc tt` | locate/arm     | Option 2: consume + rewind to 0. Option 3: absolute seek |

Only `0xF9`'s 4 payload bytes must be consumed so `ss/cc/tt` are never mistaken for commands.

**Start policy (a decision, see below):**
- *Minimal:* `0xFA` (and any locate) = rewind `bank1` to 0. Simplest, deterministic.
- *Locate-aware:* decode the locate to an absolute frame index so a DAW seek scrubs the
  video. This is really the low end of Option 3; ship Minimal first.

### ROM-side patch (`nesflix_mmc5.asm`)

Poll the FIFO once per main-loop iteration and drive `animate` from it instead of the
free-running `ani_counter`. Drain all pending bytes each iteration so a start + clock burst is
handled in one video frame; write `$512b` once at the end (only the last `bank1` is visible
in a given vblank anyway).

New zero-page state (pick from the free ZP block: `$46-$48`, `$58-$bf`, `$c1-$c2`, `$c5-$d0`,
`$de-$ff`), e.g. `sync_run`, `sync_div`, `sync_div_reload`, plus `sync_dirty`.

```asm
; --- host-sync FIFO drain, called from InfLoop each iteration ------------------
poll_sync:
    lda $40F1            ; status: bit7 set (0x80) = FIFO empty
    bmi sync_done        ; bit7 -> N flag; empty -> done
    lda $40F0            ; pop one byte
    cmp #$F8
    beq sync_clock
    cmp #$FA
    beq sync_start
    cmp #$FC
    beq sync_stop
    cmp #$F9
    beq sync_locate
    jmp poll_sync        ; unknown -> keep draining

sync_clock:
    lda sync_run
    beq poll_sync        ; ignore clocks while stopped
    dec sync_div
    bne poll_sync
    lda sync_div_reload
    sta sync_div
    jsr animate          ; advance one frame (animate already writes $512b)
    jmp poll_sync

sync_start:
    lda #1
    sta sync_run
    lda #0
    sta bank1            ; Minimal start policy: rewind to frame 0
    sta $512b
    lda sync_div_reload
    sta sync_div
    jmp poll_sync

sync_stop:
    lda #0
    sta sync_run
    jmp poll_sync

sync_locate:
    ; Option 2 Minimal: consume 4 payload bytes (52 ss cc tt), treat as rewind.
    ; (Option 3 decodes ss/cc/tt -> absolute frame; see below.)
    ldx #4
sync_locate_drain:
    lda $40F1
    bmi sync_locate_drain ; wait for each payload byte (they arrive contiguously)
    lda $40F0
    dex
    bne sync_locate_drain
    lda #0
    sta bank1
    sta $512b
    jmp poll_sync

sync_done:
    rts
```

`InfLoop` then becomes:

```asm
InfLoop:
    jsr WaitFrame
    jsr poll_sync        ; host-sync drives the playhead
    ; (leave the old ani_counter/animate free-run in only when NOT synced;
    ;  gate it on sync_run so a stock/un-synced session still plays - or drop
    ;  it entirely if the ROM is sync-only. A decision - see below.)
    jmp InfLoop
```

`clocksPerFrame` (`sync_div_reload`) is the one knob: `6` -> 24 PPQN / 6 = 4 frames per beat;
`24` -> 1 frame per beat; `1` -> 4 frames per 16th at 24 PPQN, i.e. fastest. The host role
owns the default and can expose it.

### Host-side: the `nesflix-sync` role + detection

Two small pieces mirroring risa:

1. **Role** - a dedicated `nesflix-sync` (recommended) or reuse `risa-sync` verbatim:
   - *Dedicated* (`nesflixSync.ts` + a `SystemBehavior` in
     [dspRoles.ts](packages/retroplug/src/dspRoles.ts)): ~40 lines mirroring `risaSync`. On
     transport rise / seek send `0xFA` (Minimal) or a locate; while playing emit `0xF8` at 24
     PPQN via `c.eachTick`; on transport fall send `0xFC`. Config `{ clocksPerFrame: number }`
     (default e.g. 6). Clearer, and carries NESFlix-specific config + Option 3 later.
   - *Reuse `risa-sync`:* zero new host code - attach the existing role and let the ROM
     ignore the locate. Fastest to prototype; muddier semantically (risa's 96-clock phrase
     grid is meaningless to NESFlix). Good for a first spike, replace with the dedicated role.

2. **Detection + attach** ([romProviders.ts](packages/retroplug/src/romProviders.ts)): bake a
   marker (e.g. `NFXSYNC`) into the patched ROM at a fixed offset and add a detector
   (`isNesflixSyncRom`) + one provider line, mirroring `isEverMidiRomHeader`
   ([evermidi/romDetect.ts](packages/retroplug/src/evermidi/romDetect.ts)) and `isRisaSyncRom`
   ([risa.ts](packages/retroplug/src/risa.ts)):

   ```ts
   registry.registerRomProvider((rom: RomContext): RoleInstance[] =>
     rom.platform === "nes" && isNesflixSyncRom(rom.header)
       ? [{ kind: "nesflix-sync", config: { clocksPerFrame: 6 } }]
       : [],
   );
   ```

   Confirm the marker offset lands inside whatever `RomContext.header` exposes for NES (the
   evermidi/risa detectors are the reference for how much is visible).

### Verification (headless, no hardware)

- **Role byte golden** - `test/dsp/nesflix-sync.test.ts` mirroring `test/dsp/risa-sync.test.ts`:
  feed a synthetic transport, assert the exact `0xFA / 0xF8.../ 0xFC` stream + tick offsets.
- **Emulator lock test** - construct the NES system with the patched ROM + `nesflix-sync`,
  push start + N clocks across blocks, and read `bank1` (`$53`) back each block via the
  RAM-read seam. Assert it advances exactly one per `clocksPerFrame` clocks, wraps at 64,
  freezes on stop, rewinds on start. Deterministic; no video needed. (`bank1` in RAM is the
  clean observable, the visual analog of risa's audio-drift render.)
- **Optional real N8 (hardware, confirm before firing)** - the CLI (on a Pi, or the dev box)
  mirrors the stream to a real cart running patched NESFlix; the video on the NES locks to the
  CLI clock. The visual payoff, but needs hardware and explicit go-ahead (per the
  hardware-action rule).

---

## Option 3 - sequence the visuals (superset of Option 2)

Once NESFlix reads the FIFO, the stream can carry more than a metronome. Add a private control
header - `0xFD` is undefined in real MIDI System Real-Time, so it is safe alongside risa's
`0xF9` over this non-MIDI FIFO - followed by `(op, value...)`:

| Command                | NESFlix effect                                                        |
|------------------------|-----------------------------------------------------------------------|
| `0xFD 0x00 pal`        | set `PaletteNumber` = `pal`, `jsr LoadNewPalette`                      |
| `0xFD 0x01 pos`        | set `ScreenNumber` = `pos` (0..5), run the LoadScreen/DrawScreen redraw|
| `0xFD 0x02 dir`        | set `dir` (0/1) - direction / reverse                                  |
| `0xFD 0x03 lo hi`      | absolute frame: `bank1` = `hi:lo` (mod `frames`), write `$512b` - scrub|
| `0xFD 0x04 cc`         | color-cycle: toggle (`cc_toggle`) + set cycle speed (`PalNumber`)      |
| `0xFD 0x05 h v`        | set `scroll_h` / `scroll_v` absolutely (or toggle up/down/left/right)  |

ROM side: extend the `poll_sync` dispatch with a `0xFD` case that reads the opcode + operand(s)
and writes the corresponding NESFlix variable, reusing the routines already in the source
(`LoadNewPalette`, the `LoadScreen`/`DrawScreen`/`DrawScreen2`/`Vblank` position-change block,
`animate`'s `$512b` write). Each op is a handful of instructions.

Host side, the generator emits these ops. In the target deployment that generator is the CLI's
**audio-analysis TS mapping** (Driver Mode 2): spectral bands -> palette, onset -> frame trigger
or direction, envelope -> speed/scroll, beat -> `bank1` scrub. In the DAW harness the same ops
come from a MIDI-in translation instead (CC -> palette/position, note -> frame trigger, SPP ->
scrub) - useful for deterministic tests. Either way the ROM sees the identical `0xFD` stream.

This turns NESFlix into an audio-reactive visualizer: the clock drives playback (Opt 2) while
the audio-derived control ops drive look and position (Opt 3), all over the one FIFO channel,
whether the bytes are emitted into the emulated core or mirrored to a real N8.

Verification extends the Option 2 tests: assert each control op writes the expected RAM
variable (`PaletteNumber`, `ScreenNumber`, `dir`, `bank1`, `scroll_*`), and that the
analysis-of-a-known-WAV -> op mapping golden matches (feed a fixture with a kick on known beats,
assert the palette/frame ops land on those beats).

---

## Build + assets

- Patch and assemble `nesflix_mmc5.asm` with the checked-in asm6 (`mmc5_compile.sh`), producing
  the synced `.nes`. Bake the `NFXSYNC` marker in the same build.
- Keep the built ROM as an **external / gitignored test asset** (as risa's ROMs live at
  `/workspaces/risa-v2.2.1-source/build/...`, referenced by path, not committed). NESFlix is
  GPLv3: fine to build and test against, but do not bundle the ROM into the shipped plugin -
  ship only the role/detector code.
- Point the new tests at the external ROM path (env or a fixture constant), matching how the
  native tests reference the risa ROM.

---

## Phasing

- **A. ROM patch (Option 2 Minimal).** FIFO drain + start/clock/stop in `nesflix_mmc5.asm`;
  assemble; sanity-run in the emulator.
- **B. Host role + detection.** `nesflix-sync` role (or risa-sync reuse for the first spike) +
  `isNesflixSyncRom` + provider line.
- **C. Headless verification.** Role byte golden + emulator `bank1` lock test (start/clock/
  stop/wrap). This is the "it truly locks" gate.
- **D. CLI driver Mode 1 (internal clock).** A long-running `retroplug-cli nesflix` that loads
  the ROM, sets a tempo, and drives the clock (+ optional N8 mirror). The minimal Pi appliance.
- **E. Option 3 ROM ops.** `0xFD` control ops in the ROM + per-op RAM tests.
- **F. CLI driver Mode 2 (audio-reactive).** TS analysis + feature->op mapping. Start with the
  closed loop (analyze RetroPlug's own audio - no new native seam); add the external line-in
  capture facet after.
- **G. (Optional, hardware, confirm first.)** Pi + real N8 visual lock check.

A-C are the "it truly locks" milestone (emulator-proven). D makes it a real standalone box. E+F
are the visualizer. G is the victory lap. Each phase stands alone; the ROM protocol is frozen
after A/E so the driver work never reaches back into the cart.

---

## Decisions to make before starting

1. **Dedicated `nesflix-sync` role vs reuse `risa-sync`.** Recommend dedicated (clarity +
   `clocksPerFrame` config + room for Option 3); allow risa-sync reuse only as a throwaway spike.
2. **Start policy.** Minimal (rewind to 0) first; add locate-aware absolute seek with Option 3.
3. **`clocksPerFrame` default.** e.g. 6 (4 frames/beat) - pick what looks good on the sample GIF.
4. **Sync-only ROM vs dual-mode.** Keep the free-running `ani_counter` path gated on `!sync_run`
   so an un-driven session still plays, or make the synced ROM sync-only. Recommend dual-mode.
5. **Marker + offset** for detection, and confirm it sits within `RomContext.header` for NES.
6. **MMC5 only** (confirmed) - MMC3 port deferred.
7. **Clock generation (Mode 1).** Synthetic-transport-driven role (zero byte code) vs a direct
   TS byte generator (explicit, base for Mode 2). Recommend the direct generator for the CLI
   tool, keeping the role for the DAW harness.
8. **Audio feed source (Mode 2).** Closed loop (analyze RetroPlug's own audio - no new seam) vs
   external line-in (needs an audio-capture facet). Recommend closed-loop first; add line-in
   when a real external source is on the table.
9. **Analysis + mapping location.** TS script (recommended - scriptable, the CLI-TS direction)
   vs C++. Start TS; move only a hot onset/beat tracker to C++ if a Pi can't keep up.

## Risks / notes

- The always-attached FIFO is benign and NESFlix never touches `$40F0/$40F1` today, so there is
  no conflict with the stock behavior.
- Sync only changes *who* calls `animate` (which already owns the `$512b` write), so the risk to
  NESFlix's rendering is low.
- Resolution is one NES frame (~16.7 ms): clocks are delivered sample-accurately but the ROM
  acts once per vblank (correct - no tearing). Tight for a visualizer, not sub-frame.
- Fast tempo can advance more than one bank per vblank (visible frame skipping) - inherent, and
  the musically-correct behavior.
- asm6 (NESFlix) differs from risa's cc65/ca65 toolchain, but NESFlix is self-contained, so
  there is no cross-toolchain coupling.
- External live-audio capture in the CLI is a genuinely new seam (nothing captures today); the
  closed-loop source sidesteps it, so Mode 2 can ship without it.
- Audio-reactive adds analysis latency (hop + detector lookahead) on top of the one-frame FIFO
  resolution. Fine for a visualizer; not the place for sample-tight cueing.
- A Pi driving a real N8 is a proven shape (arm64 build + N8Link, already done for risa), so the
  deployment risk is low - the new work is the generator, not the transport.
