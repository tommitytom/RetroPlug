# Mega Drive support on BlastEm, spec 1: the console and genmddj's clock sync

RetroPlug gains a Sega Mega Drive / Genesis console, run by BlastEm, with [genmddj](https://github.com/little-scale/genmddj)
(little-scale's LSDj-style tracker for the YM2612 + SN76489, smsggdj's sibling) as the target cart. This is the first of
three specs:

| Spec | Scope |
|---|---|
| **1 (this)** | The `blastem` console package (vendored BlastEm, sample-accurate stepping, multi-instance, audio, video, pad, battery, savestates) and a minimal `genmddj` package (detection, IN24 clock sync) |
| 2 | genmddj's tracker integration (SRAM codec, Songs, live load) and MIDI takeover (the 10 voices played from the DAW over the emulated port-2 bridge) |
| 3 | Audio fidelity: Nuked-OPN2 as an FM option, YM2612 / YM3438 models, output-filter choice, per-channel stems |

User decisions (2026-11-13): console then genmddj, in this order; every desktop product, the plugin and the CLI list it;
BlastEm's portable interpreter on every platform, never its JIT; BlastEm comes in as a git submodule of a mirror we own;
the output keeps BlastEm's 3390 Hz low-pass until spec 3; no genmddj ROM is vendored yet. There is no Mega Drive
hardware at hand, so this is emulation work only: "accurate" means agreeing with the emulators and the die-shot model,
and the open hardware questions below stay open.

## 0. Why BlastEm (the spike)

The spike (2026-11-12/13, scratch in the gitignored `.superpowers/mdspike/`, index `SPIKE-REPORT.md`) built Genesis Plus
GX and BlastEm 1.0 as libretro cores, rendered genmddj's demo song through each with genmddj's own
`user-tools/player`, compared the FM chips at chip level against Nuked-OPN2 (identical register stream, native rate), and
studied both cores' code for stepping, globals and threads.

- **Licence.** Genesis Plus GX is non-commercial (the core, its Musashi 68k and its Z80), so it cannot ship beside GPLv3
  Mesen. It stays a test oracle outside the tree. BlastEm is GPLv3.
- **Stepping.** BlastEm's 68k runs to a target cycle and every other chip catches up on absolute master-clock counters,
  so "run until T" anywhere in a frame is about 20-40 lines in `sync_components` (`genesis.c:564`). Genesis Plus GX runs
  per scanline with counters rebased each frame and would need its frame loop rewritten.
- **Port 2.** Every BlastEm port access already receives its exact master-clock cycle (`io.c:1305`, `io.c:1533`).
- **Instances.** BlastEm keeps its state in per-instance structs with about 10 mutable globals; Genesis Plus GX has about
  210 and no context struct.
- **Threads.** Neither core uses thread-local storage. BlastEm's threaded VDP renderer and its audio locks are optional
  and compiled out of a library build.
- **Speed (x86, Ryzen 9950X under load, whole system).** Genesis Plus GX ~70x realtime (MAME FM) and ~22x (Nuked FM);
  BlastEm ~27x (JIT) and ~22x (interpreter). Not measured on the A53 (section 7).
- **FM accuracy against Nuked-OPN2.** Below 4 kHz BlastEm, MAME and Genesis Plus GX all land at 20-21 dB SNR, the residual
  mostly sub-sample timing; pitch agrees across every core to 0.02 cents. One real disagreement: BlastEm steps the LFO
  every N+1 samples where Nuked and MAME step every N (same table, `ym2612.c:264` vs `ym3438.c:836`), so rate 7 is
  69 Hz against 83 Hz. Sega's documented LFO rates match N+1 exactly. Spec 1 keeps BlastEm's behaviour; spec 3 decides.

## 1. Packages and where things live

**`deps/blastem`**: a git submodule of our mirror, `https://github.com/tommitytom/RetroPlug-BlastEm` (upstream is
Mercurial at retrodev.com, so the mirror holds snapshots, not hg history). Its `upstream` branch is one commit per
imported upstream release, starting with 1.0 (hg `2b2a182f4522`); its `retroplug` branch is `upstream` plus our edits,
and a later upstream release is a new `upstream` commit merged into `retroplug`. Every edit carries a `RetroPlug:`
marker comment, the rule `deps/mesen` follows. Only the user pushes to the mirror; until a push, work proceeds on a
local clone and the submodule URL is the mirror's. Upstream 1.0's own library target does not build as shipped; the spike's 62-line fix
(`.superpowers/mdspike/blastem-1.0-libbuild.patch`) is the starting point, though our CMake build replaces BlastEm's
Makefile entirely.

**`packages/blastem`**: the console package, shaped like `packages/mesen`.

- Native: `BlastemMdSystem` (a `SystemBase`), the `rp.blastem.md` node and its build function, the port-2 device
  (section 3), the audio output (section 2).
- TS: the console descriptor (a `.md` / `.bin` / `.gen` ROM is a Mega Drive's by the `SEGA` string at `$100`; an SMS
  `.bin`, which has no such header, still goes to Mesen), the node settings (region `auto` / `ntscUs` / `ntscJp` /
  `pal`, on the NES's pattern), and the 6-button pad mapping for port 1. As built, the region's four words are the
  native enum's own names (a hyphenated `ntsc-us` is refused by the settings parser) and the region is construct-time:
  it fixes the master clock and the frame height, so a change rebuilds the machine. The gain is the node's param, not a
  setting.

**`packages/genmddj`**: the cart package, shaped like `packages/smsggdj`: detection, the `genmddj-sync` role, its ROM
provider and the `rp.genmddj.sync` protocol node (section 4). It declares `@retroplug/smsggdj` for the shared sync state
machine. No tracker integration in spec 1.

**Products**: `blastem` and `genmddj` join the root `package.json`'s `desktop` and `plugin` lists. The shell names no
console and no cart; everything arrives through the packages' contributions.

## 2. Running the core

**Clock.** Each instance keeps a 64-bit absolute master-clock count (MCLK: NTSC 53,693,175 Hz, PAL 53,203,424 Hz).
BlastEm rebases its own counters per frame; that never escapes the system. Host sample `n` at rate `R` is master clock
`floor(n * MCLK / R)`, in exact integer arithmetic so it never drifts; a sample-rate change re-anchors at the current
sample. One 48 kHz sample is about 1,119 master clocks, about 160 68k cycles. As built, MCLK is the core's own
`gen->master_clock` after boot, read as a field and never as a constant: NTSC 53,693,175 and PAL 53,203,395, which is
what the emulated chips actually run at (53,203,424 is the hardware nominal).

**The triad.**

- `prepareForBlock` applies the queued pad transitions. As built there are none to apply: BlastEm samples its pad
  registers live, so `pressButton` sets the bit at once and the block's own polls read it (the engine applies a
  block's presses before its core runs, which is the same contract Mesen's queued apply gives).
- `stepIfBelowTarget(frames)` calls a new `run_until(mclk)` (a mirror commit in `sync_components`) with the block end's
  master clock. BlastEm stops at the first 68k instruction boundary at or past it; the method returns `false`. There is
  no link cable, so it never steps round-robin.
- `finishBlock` drains exactly `frames` samples into the lanes; whatever the chips produced past the boundary carries to
  the next block.

**Block-size invariance (a hard requirement).** The same input rendered with host blocks of 1, 64, 1024 and 4096 frames
is byte-identical. If BlastEm's stops prove not to be transparent, the fallback is to stop on a fixed grid of one host
sample (SameBoy's model), at a few percent of overhead; the test decides, not an assumption. As built, the test decided
against plain stops (27 of the 36 audio and state checks disagreed with the 4096-frame render), so the one-host-sample
grid is what ships: one `run_until` per host sample, absolute time, so every partition hits the same stops. Its cost is
about 15 to 19 percent of a block on x86 (hvPsg 22.7 to 19.7x realtime, the per-VINT DMA image 19.2 to 16.3), not the
few percent estimated here.

**Audio output.** BlastEm's `render_audio.c` is not used: it is a process-global source registry capped at 8 (a ninth
source aborts) and it resamples linearly. In its place:

- the YM2612 (native: one sample per 144 YM clocks, 53,267 Hz NTSC) and the PSG feed band-limited step synthesis
  (blip_buf, as Mesen and Genesis Plus GX use) at the host rate, one buffer per side;
- BlastEm's 3390 Hz one-pole low-pass and its YM/PSG mix ratio are applied at each chip's native rate before the
  resampler, so the console sounds as BlastEm does;
- one stereo `Mix` stream (stems are spec 3); the universal gain setting applies.

**Video.** The VDP renders into a `FrameBufferTriple` at the active mode (320 or 256 wide; 224 lines, 240 on PAL).

**Input.** A 6-button pad on port 1. Port 2 is the RetroPlug port device (section 3) when a role claims it, otherwise
an unplugged pad.

**Battery and savestates.** `saveSramBytes` returns BlastEm's own storage, which keeps odd-byte SRAM compacted (byte `i`
is address `$200001 + 2i`), as on a real odd-byte cart. (Genesis Plus GX stores it linearly, which is why genmddj's
player needed an offset change for BlastEm in the spike.) Savestates use BlastEm's serializer; a state from another
serializer version is refused with a message, never misread. As built, the battery speaks the `.sav`'s order for every
cart: an odd-byte cart keeps its compacted layout, and a `RAM_FLAG_BOTH` 16-bit cart is pair-swapped out of BlastEm's
storage, so it is not rotated against every other emulator's save. The state is BlastEm's serializer behind its own
8-byte identifier (`BLSTSZ\x01\x07`) with a section walk in front, and the battery rides in this host's own section
(id `0x8001`) after BlastEm's, which is also what a Duplicate copies; a refused state does not fail the build, it boots
from power-on with `stateRefused()` set, which is where a caller's message would read it.

**Instances and threads.** The mutable globals become per-instance state in mirror commits. The known list, from the
code study (`.superpowers/mdspike/STEPPING-STUDY.md`): VDP DMA reads the global `current_system` (`genesis.c:309`), so a
second instance would DMA from the first one's RAM; the rest are about ten smaller ones plus `render_audio.c`, which we
drop. The threaded VDP renderer is compiled out and `render_is_threaded_video()` returns 0. The event log is not:
`event_log.c` is in the file set because `vdp.c` calls its VRAM hooks, but nothing in this build ever starts it (no
caller opens its file, its socket or its memory reader), and the mirror's VRAM hooks return before touching their delta
state unless a log is active. An instance runs on whatever thread calls it, one call at a time; the control plane reads
it through the snapshot path like the other cores.

## 3. The port-2 device

A device on controller port 2 that BlastEm's `io.c` calls on every READ of `$A10005` (data) and `$A1000B` (control),
with the access's master clock. As built, only reads reach it (its `io_custom_input`); a write to either port is
BlastEm's own latch, which the ROM reads back while a pin is an output (the last bullet below), so the device never
needs to see one.

- It holds a queue of `(host sample, level byte)` entries. `pushCoreBytes(frame, bytes)` adds the block's first sample
  to the frame offset and enqueues; `hasCoreBytesIn()` is true while a role claims the port.
- A level is on the pins once the stop grid reaches its sample, and every access the run for that sample makes then
  answers with it. The grid is one stop per host sample and it is absolute, so which block delivered the level changes
  nothing. Keying on the access's own clock instead would: every stop lands at the first instruction boundary at or
  past its target, so in a large block the next sample's levels are already queued when the 68k crosses the target and
  in a one-sample block they are not, and the same level would move by a poll with the host's block size.
- A pin the ROM sets as an output in `$A1000B` reads back the ROM's own data latch, as on hardware; only input pins
  read the host's level. That keeps genmddj's OUT and PULSE modes from fighting the host.
- Spec 2's MIDI-takeover bridge is a second behaviour of the same device: it answers each TR edge the ROM drives, at its
  cycle, with the next bit of a queued message.

## 4. genmddj in spec 1: detection and clock sync

**Detection.** `isGenmddjRom(rom)` matches genmddj's identity: the header's product code `GM GENMDDJ-` and its `ver_str`
marker, pinned against a ROM built from the genmddj checkout, as smsggdj's marker was. The gate is required, not tidy:
the sync drives port 2's TR and TH, which every game reads as player 2, so attaching it to any Mega Drive ROM would
inject phantom button presses.

**The sync.** genmddj's IN24 mode is smsggdj's wire, ported (genmddj `src/main.asm:9438` `sync_read`, "ports from
SMSGGDJ"): a 2-bit counter on TR (`$A10005` bit 5) and TH (bit 6), sampled once per frame, counted as the delta since the
last read.

- `rp.genmddj.sync` runs the DAW transport through smsggdj's counter state machine (each clock adds 1 while
  running, STOP freezes) at 24 PPQN, and the counter is deliberately never reset: as built, START does not set it to 0,
  because a reset would hand an un-re-armed ROM a spurious delta (the node's comment says so, and smsggdj's machine is
  only ever advanced). smsggdj's node is refactored to take its level encoder as a parameter, and genmddj supplies
  `mdSyncLevels(counter)`: counter bit 0 on bit 5, bit 1 on bit 6, other lines idle-high. smsggdj's tests pass
  unchanged.
- genmddj's limits carry over: at most 3 clocks per frame survive (a tempo cap), and there is no relocate, so the DAW
  drives start, stop and tempo but not song position.
- The level bytes go out on the node's `bytes` output to `pushCoreBytes` (section 3).

**Surface.** The role appears as smsggdj's does, a feature role on a genmddj system. The console gets the generic system
menus (load ROM, reset, savestates, region) from its descriptor. No Songs or Assets menus until spec 2.

**Persistence.** The role's schema (no fields yet) and the node's settings (region, gain) are new, so additive: no
migration step. A battery is saved like any other.

## 5. Testing

**Native (Catch2, fixture-free, in CI)**, on synthetic Mega Drive ROMs generated in-tree: hand-assembled 68k bytes
(a header, vectors, and a loop that writes YM2612, PSG and port-2 registers on a known schedule), so CI needs no
assembler.

- Boot and audio: a ROM playing fixed FM and PSG notes renders non-silent output, identical run to run.
- Block-size invariance: 2 s rendered at blocks of 1, 64, 1024 and 4096 frames, byte-identical.
- Event timing: a ROM polls port 2 in a tight loop and changes the PSG volume when the level changes; a level staged at
  frame `f` produces its audio edge at the same known delay after `f` at every block size. A second ROM reads
  `$A10005` at known cycles to pin the master-clock mapping, including across a sample-rate change.
- Instances: two instances interleaved on one thread, and two on two threads, each bit-identical to a solo render.
- Saves: savestate round trip (save, run N samples; load, run N samples: identical), battery round trip, the odd-byte
  layout. As built, the promise is the machine's state, not sample-exact identity with the source: the grid's phase
  (the overshoot the last stop had passed and the anchors' intra-sample offset) is host state no state carries, so a
  restored instance stops on its own timeline afterwards and a stop moves the emulation by the cycles it lands on.
  What holds is what the cases assert: two instances restored from the same bytes match exactly, block-size
  invariance survives a load, and the machine's own state (RAM, VRAM, registers, battery) is the source's.
- Port direction: with a pin an output in `$A1000B`, the ROM reads its own latch, not the host's level.

**Goldens.** The synthetic ROMs' renders join the parity baseline as a Mega Drive group; exact bytes on
`linux-x64-gnu13` only, self-agreement elsewhere, per the existing rule. From then on a Mega Drive audio change fails the
suite unless the commit re-records it.

**TS.** Detection against genmddj and non-genmddj synthetic headers (`packages/genmddj/ts/fixtures.ts`, published as
`./testFixtures`); `mdSyncLevels`; the shared state machine; the descriptor's classification of `.md`, `.bin`, `.gen`
(an SMS `.bin` still goes to Mesen).

**genmddj end-to-end (fixture-gated: skips in CI, counted in the skip baseline).** As built it is
`packages/retroplug/test-native/genmddj-sync.test.ts` on the QuickJS host (`test:native`, not `retroplug-cli`): it boots
the real ROM from `RETROPLUG_RESOURCES_DIR` with IN24 set in its SRAM options, drives the DAW transport at one tempo
(120 BPM) and reads genmddj's RAM through the console's `memory()` capability, whose sync variables are fixed addresses
in the source (`sync_last` `$FFD425`, `sync_wait` `$FFD426`). It asserts WAIT clears on START, that the cart's rows
advance as the transport runs, that STOP freezes it, and that the cart's own live-bar counter moved. What it does NOT
do, against the paragraph this replaces: one tempo rather than a few; nothing reports a tempo past 3 clocks per frame
(the cap is a constant and its arithmetic test on the TS side, `mdSyncLevels`); and Z80 RAM is not a published region
(the console publishes work RAM and the battery, and the Z80's 8 KiB is reachable only through the internal
`getMemory(ExtWorkRam)`, spec/06's Region note). CPU debugging is out of scope, as it was.
`tools/fetch-genmddj-fixture.sh` builds the ROM from a genmddj checkout (vasm + wla-dx) into the resources tree.

**Performance.** A profile-host bench records x86 xRT for the genmddj song and the synthetic ROMs.

## 6. Build, platforms and docs

- `cmake/blastem.cmake` builds a `blastem` static library from `deps/blastem`, added beside `deps/mesen` in
  `CMakeLists.txt`. It runs `cpu_dsl.py` (Python 3, a hard configure requirement found by CMake) to generate the 68k/
  Z80 interpreters into the build tree (derived artifacts, never committed) and compiles the Mega Drive file set only:
  no SDL, nuklear, 32X, Sega CD, SMS, Coleco or LaserActive. As built, `event_log.c` is in that file set after all,
  because `vdp.c` calls its VRAM hooks: nothing starts a log, and the mirror's hooks return before their delta state
  unless one is active (section 2). `cmake/packages.cmake` does not admit the library to one package, as this file
  says: it treats a root target as vendored, so the `blastem` library is defined at the root and linked by exactly one
  target, `retroplug-blastem`, with the TS side held by `pnpm check:boundaries` and the native side by review
  ([spec/06](../spec/06-build-test.md#the-packages)).
- Windows: BlastEm is code we do not own, so its target is off `/W4 /WX` and suppresses by number; MSVC portability
  fixes (BlastEm is GNU C99) are mirror commits with `RetroPlug:` markers. The plan's first task was to prove the
  generated interpreters (57 MB of 68k C) compile on every CI platform, MSVC included, before anything builds on them.
  As built that proof is still owed: CI cannot fetch the mirror yet, so the configure makes the failures readable in the
  sources impossible instead (`mem_win.c`, `call` dispatch for the Z80, MegaWiFi out, `ws2_32`, `/std:c11`) and
  [spec/07](../spec/07-remaining-work.md#msvc-has-not-compiled-blastem) carries the open entry, with clang-cl for that
  one target as the fallback.
- CI: the Linux, macOS, Windows and arm64 jobs gain Python 3 where missing. As built, `build.yml`'s four jobs run
  `actions/setup-python@v5` (3.12) for it and `release.yml`'s four do the same, so the two workflows agree; the runners
  ship a python3 either way, and the step is what makes the configure's hard requirement explicit.
- Error handling: a ROM that fails to load fails the system build with a message, like the other consoles; a Mega Drive
  ROM detection rejects gets no sync role; a foreign savestate is refused.
- Docs, in the same commits as the code: spec/00's package table; a Mega Drive section in spec/13 (the port-2 device,
  the timing model, the audio path); spec/06's build notes (Python, the generated cores, the fixture script); one
  AGENTS.md rule line ("every edit to `deps/blastem` is a commit on the mirror's `retroplug` branch with a `RetroPlug:`
  marker; don't bump the pointer without checking"); spec/07 entries for specs 2 and 3 and for the A53 measurement.

## 7. Out of scope, and open questions

Out of scope for spec 1: genmddj's SRAM codec, Songs and live load, and MIDI takeover (spec 2); Nuked-OPN2, chip models,
filter choice and stems (spec 3); CPU debugging and breakpoints for the Mega Drive; Sega CD, 32X and BlastEm's SMS mode
(Mesen runs SMS); the JIT; handheld performance work.

Open, needing hardware or the device:

- **A53 speed.** Estimated from the x86 numbers and Mesen's x86-to-device ratio (~18x): BlastEm about 1.2x realtime,
  BlastEm plus Nuked about 0.7x. Weak: the ratio varies per core, and the generated interpreter is large for the A53's
  caches. The spike's arm64 builds and an unrun bench are in `.superpowers/mdspike/arm/deploy/`.
- **The LFO step period** (N vs N+1, section 0) and the right output filter per model: a hardware recording settles both.
