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
  `.bin`, which has no such header, still goes to Mesen), the node settings (region `auto` / `ntsc-us` / `ntsc-jp` /
  `pal` on the NES's pattern, gain), and the 6-button pad mapping for port 1.

**`packages/genmddj`**: the cart package, shaped like `packages/smsggdj`: detection, the `genmddj-sync` role, its ROM
provider and the `rp.genmddj.sync` protocol node (section 4). It declares `@retroplug/smsggdj` for the shared sync state
machine. No tracker integration in spec 1.

**Products**: `blastem` and `genmddj` join the root `package.json`'s `desktop` and `plugin` lists. The shell names no
console and no cart; everything arrives through the packages' contributions.

## 2. Running the core

**Clock.** Each instance keeps a 64-bit absolute master-clock count (MCLK: NTSC 53,693,175 Hz, PAL 53,203,424 Hz).
BlastEm rebases its own counters per frame; that never escapes the system. Host sample `n` at rate `R` is master clock
`floor(n * MCLK / R)`, in exact integer arithmetic so it never drifts; a sample-rate change re-anchors at the current
sample. One 48 kHz sample is about 1,119 master clocks, about 160 68k cycles.

**The triad.**

- `prepareForBlock` applies the queued pad transitions.
- `stepIfBelowTarget(frames)` calls a new `run_until(mclk)` (a mirror commit in `sync_components`) with the block end's
  master clock. BlastEm stops at the first 68k instruction boundary at or past it; the method returns `false`. There is
  no link cable, so it never steps round-robin.
- `finishBlock` drains exactly `frames` samples into the lanes; whatever the chips produced past the boundary carries to
  the next block.

**Block-size invariance (a hard requirement).** The same input rendered with host blocks of 1, 64, 1024 and 4096 frames
is byte-identical. If BlastEm's stops prove not to be transparent, the fallback is to stop on a fixed grid of one host
sample (SameBoy's model), at a few percent of overhead; the test decides, not an assumption.

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
serializer version is refused with a message, never misread.

**Instances and threads.** The mutable globals become per-instance state in mirror commits. The known list, from the
code study (`.superpowers/mdspike/STEPPING-STUDY.md`): VDP DMA reads the global `current_system` (`genesis.c:309`), so a
second instance would DMA from the first one's RAM; the rest are about ten smaller ones plus `render_audio.c`, which we
drop. The threaded VDP renderer and the event log are compiled out and `render_is_threaded_video()` returns 0. An
instance runs on whatever thread calls it, one call at a time; the control plane reads it through the snapshot path
like the other cores.

## 3. The port-2 device

A device on controller port 2 that BlastEm's `io.c` calls on every read and write of `$A10005` (data) and `$A1000B`
(control), with the access's master clock.

- It holds a queue of `(mclk, level byte)` entries. `pushCoreBytes(frame, bytes)` converts the frame offset to a master
  clock (section 2) and enqueues; `hasCoreBytesIn()` is true while a role claims the port.
- On each access it first applies every entry due at or before that access's cycle, then answers. So the whole block
  runs in one call and a level still lands on its exact sample, unlike the SMS role, which releases levels as the
  emulated position passes their offset.
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

- `rp.genmddj.sync` runs the DAW transport through smsggdj's counter state machine (START sets 0, each clock adds 1 while
  running, STOP freezes) at 24 PPQN. smsggdj's node is refactored to take its level encoder as a parameter, and genmddj
  supplies `mdSyncLevels(counter)`: counter bit 0 on bit 5, bit 1 on bit 6, other lines idle-high. smsggdj's tests pass
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
  layout.
- Port direction: with a pin an output in `$A1000B`, the ROM reads its own latch, not the host's level.

**Goldens.** The synthetic ROMs' renders join the parity baseline as a Mega Drive group; exact bytes on
`linux-x64-gnu13` only, self-agreement elsewhere, per the existing rule. From then on a Mega Drive audio change fails the
suite unless the commit re-records it.

**TS.** Detection against genmddj and non-genmddj synthetic headers (`packages/genmddj/ts/fixtures.ts`, published as
`./testFixtures`); `mdSyncLevels`; the shared state machine; the descriptor's classification of `.md`, `.bin`, `.gen`
(an SMS `.bin` still goes to Mesen).

**genmddj end-to-end (fixture-gated: skips in CI, counted in the skip baseline).** A `retroplug-cli` TS test boots the
real ROM from `RETROPLUG_RESOURCES_DIR` with IN24 set in its SRAM options, runs the DAW transport at a few tempos and
reads genmddj's RAM, whose sync variables are fixed addresses in the source (`sync_last` `$FFD425`, `sync_wait`
`$FFD426`). It asserts WAIT clears on START, rows advance one per 6 clocks, STOP freezes, and a tempo past 3 clocks per
frame is reported. The console exposes work RAM, Z80 RAM and battery RAM through the `memory()` / `sram()` capability
accessors for this; CPU debugging is out of scope. `tools/fetch-genmddj-fixture.sh` builds the ROM from a genmddj
checkout (vasm + wla-dx) into the resources tree.

**Performance.** A profile-host bench records x86 xRT for the genmddj song and the synthetic ROMs.

## 6. Build, platforms and docs

- `cmake/blastem.cmake` builds a `blastem` static library from `deps/blastem`, added beside `deps/mesen` in
  `CMakeLists.txt`. It runs `cpu_dsl.py` (Python 3, a hard configure requirement found by CMake) to generate the 68k and
  Z80 interpreters into the build tree (derived artifacts, never committed) and compiles the Mega Drive file set only:
  no SDL, nuklear, 32X, Sega CD, SMS, Coleco or LaserActive. `cmake/packages.cmake`'s boundary admits it only to
  `packages/blastem/native`.
- Windows: BlastEm is code we do not own, so its target is off `/W4 /WX` and suppresses by number; MSVC portability
  fixes (BlastEm is GNU C99) are mirror commits with `RetroPlug:` markers. **The plan's first task proves the generated
  68k interpreter (57 MB of C) compiles on every CI platform, MSVC included**, before anything builds on it; clang-cl
  for that one target is the fallback.
- CI: the Linux, macOS, Windows and arm64 jobs gain Python 3 where missing.
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
