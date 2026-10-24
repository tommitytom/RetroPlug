# Design: Phase B0, the parity baseline

**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** designed and prototyped (every group
recorded and green); the implementation plan is [modular-graph-phase-b0-plan.md](modular-graph-phase-b0-plan.md).

Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph,
and its parity target is bit-identical audio in every routing mode, MIDI routing mode and sync mode.
Nothing today can check that: almost every audio test asserts RMS, peaks or timing tolerances, and no
PCM hash or golden WAV is checked in. B0 builds the check and records today's engine with it, before
anything moves.

---

## 0. Where B0 sits

Phase B is four sub-projects. Each lands working and is checked bit for bit against the B0 baseline.

| | Delivers | Changes for users | Deletes |
|---|---|---|---|
| **B0. Parity baseline** (this doc) | Determinism switch, `renderBlocks`, a store-driven case matrix with committed hashes of today's engine; three bugs fixed first (§2.5) | Only the fixes | Nothing |
| **B1. Native runs on the executor** | Cores as system nodes, linked GBs as a coupled group, the kernel as one node, today's routing modes as wiring a native translator builds from the existing calls; the two open decisions of §8 settled. TS untouched | Nothing | The routers, `BlockRunner`, `LinkGroup`, `AudioRouting` |
| **B2. TS owns the graph** | `buildGraph`, the stores call `setGraph`, the Backend surface switch (§7.5) | Nothing | The B1 translator; the construct / remove / routing / role-config / kernel commands |
| **B3. The kernel dissolves** | The script-node runtime, the DSP roles as TS nodes, MIDI routing as a node, marker roles as extensions | Nothing | The DSP kernel and its bundle |

B1 comes before B2 so the riskiest change, the executor swap (float order, event order, block
partitioning), is made with no TS change at all: a parity break in B1 can only be native.

## 1. Decisions taken

| Question | Decision |
|---|---|
| Where does the baseline live? | **Committed hash files**, compared in `pnpm test:native` on this devcontainer and in CI's linux-x64 job (both ubuntu-24.04, GCC 13.3). It stays after Phase B as the project's first exact-audio regression guard. |
| How strict is bit-identical? | **Today's behaviour is copied exactly**, including quirks and the blocks around a mid-run edit. Each B step is a pure refactor; fixing a quirk is its own commit afterwards, with a deliberate re-record whose diff shows what moved. |
| How are cases driven? | **Through the stores, in `test:native`.** The Reaper render hashes that were to back it up were dropped (§7): the renders are not bit-repeatable, which was the agreed condition for falling back to the matrix alone. |

Float results are only bit-stable on one compiler and architecture (r8brain uses SIMD intrinsics;
arm64 fuses multiply-adds), so audio is compared exactly on the baseline's target only (§5.2). Other
platforms are covered by construction: the same float operations in the same order give the same bits.

## 2. Components

### 2.1 The determinism switch

`RETROPLUG_DETERMINISTIC=1` in the environment, read once per process
(`packages/native/src/util/Determinism.hpp`):

- SameBoy: `GB_random_set_enabled(false)` before `GB_init`. `GB_random()` then returns 0, so power-on
  WRAM, HRAM, OAM and wave RAM are zero rather than seeded from `time(NULL)`. The flag is a
  process-global static, which suits a switch that covers the whole process.
- Mesen NES: `NesConfig::RamPowerOnState = RamState::AllZeros` (the default is `Random`).
- Mesen SMS / GG: `SmsConfig::RandomizeVdpStartScanline = false`, a field B0 adds to the vendored Mesen
  (default true, so stock behaviour is unchanged). `SmsVdp::Init` otherwise starts the VDP at a random
  scanline from `std::random_device`, which shifts every frame against the audio: a boot is silent, so it
  only shows once a song plays, where three fresh processes gave three different results. SMS RAM was
  already pinned to `AllZeros`; `GbaConfig` defaults to it.

The test runner sets it for `test-native/parity/` files only. The app never does, and its power-on stays
random. Without the switch the determinism test (§6) fails on all three cores.

### 2.2 `renderBlocks` and `buildTarget`

Two calls on the **harness** facet (CLI and test hosts only, beside `renderAudio`; the plugin channel never
exposes them):

```ts
renderBlocks(outputs: 2 | 4 | 6 | 8, blocks: {
  frames: number;                                   // 1..8192
  midi?: { frame: number; bytes: number[] }[];      // staged in list order
  controller?: number[][];                          // control-surface traffic
  playing?: boolean; bpm?: number; ppq?: number;    // applied after the drain, before processBlock
}[]): { lanes: Float32Array[]; midiOut: { block: number; system: number; frame: number; data: Uint8Array }[] }
buildTarget(): string                               // "linux-x64-gnu13"
```

Per block it repeats `PluginDSP::run`'s order: `engine.stageMidi(frame, bytes)` for each event, the
controller traffic, `invoker.drainInto(engine)`, the transport fields given (the engine's setters are plain
assignments, so applying only the given ones matches the plugin's per-block calls), `processBlock(frames,
lanes, outputs)`, then the kernel's MIDI out tagged with the block index, and `clearMidiOut()`. The
plugin's master gain is left out: it is plugin code applied after the engine.

A refused request returns its reason (the driver throws it) and renders nothing: a bad output count, a
block of 0 or more than 8192 frames, a frame at or beyond its block, frames going backwards within a block
(DAWs deliver events sorted, so the baseline never pins what today's engine does with unsorted input;
ties keep their list order), an empty MIDI message, a controller message outside 1..4 bytes, a
bpm that is not a positive finite number, a non-finite ppq, or the background audio thread running. The response is raw planar PCM; TS hashes it.

`buildTarget` is `<os>-<arch>-<compiler><major>`, derived at compile time in `EngineRpcService.cpp`
from the compiler's predefined macros, so each slice of a universal macOS build names its own arch and the key
describes the binary that ran. MSVC reports the full `_MSC_VER` (e.g. `msvc1951`).

The test host's main loop also gains `__rp_keepAlive()`, the CLI's existing opt-in: a recording awaits a
child process (§5.3), and the host's bounded pump otherwise gives up before it finishes. Asking is
required: txiki keeps a handle of its own alive, so "the loop still has work" would also hold a hung test.

### 2.3 The parity rig

`test-native/parity/rig.ts` is the app's own control plane (`bootSession()`, the boot path the CLI and
the render worker share) with a `render(outputs, segment)` over `renderBlocks`. A case written against the
stores survives every step, since B2 deletes the backend calls the stores make today but not the stores.
B2 and B3 edit only `bootSession` and this file.

Before each case it calls `newProject()`, deletes every ROM copy, reloads the kernel, stops the transport
at ppq 0 and 120 bpm, sets the case's sample rate, drains the MIDI out, and asserts no system survived.

ROMs are copied into the file's temp dir before they load (`rig.rom`), so a case never writes a
committed `.sav` (battery flush on teardown would rewrite `resources/roms/smsggdj_v0_45.sav`) and always
starts from the same battery. A second instance of one ROM path gets a fresh `-2.sav` battery slot, which
a case cannot seed in advance, so two systems that each need a battery each get their own copy
(`rig.rom(src, prefix)`).

### 2.4 Cases and groups

A case is `{ name, needs?, sampleRate?, outputs, audible, setup(rig), segments }`. A segment is a run of
blocks with events placed by absolute frame (MIDI, controller traffic, transport), optionally followed by
a store edit (`after(rig)`). An edit is a store call between two `renderBlocks` calls: on the test host it
applies at once, in the plugin it is drained at the start of the next block, and both land before that
block's `processBlock`.

Every MIDI-driven scene boots first. mGB ignores MIDI for about 1.5 s (`BOOT_MS`, which also covers a
BlipToaster build's 1 s); smsggdj needs 3 s, a work-RAM poke of its song (it autoloads none) and a PAUSE
press; LSDj needs 6 s to reach its song screen; risa 2.5 s.

`test-native/parity/scenes.ts` holds the shared systems and stimuli; each `parity/<group>.test.ts` is a
table of cases with one `test()` per case.

### 2.5 The bugs fixed first

Recording a bug into the baseline would oblige B1 to B3 to copy it. These are fixed, each with a test that
fails first, before anything is recorded:

1. **`ProjectStore.setMidiRouting` never re-drove the kernel.** It wrote `projectSettings.midiRouting`,
   but the project-scope `midi-routing` role is synthesized from that setting at projection time
   (`kernelProjection`), so the kernel ran the old mode until the next structural edit. It now calls
   `onSystemsChange()`, as `setController` does.
2. **`SystemsStore.duplicateSystem` skipped `applyConstructHooks`.** The LSDj, BlipToaster and risa asset
   roles fold their overrides into the ROM in memory at construct time, so a duplicate booted the
   unpatched base ROM under a savestate captured from the patched one. It now runs the hooks, as
   `rebuildInPlace` does (the LSDj seed hook leaves a savestate-seeded spec alone).
3. **SMS truncated host blocks longer than about 4600 frames.** Its per-block instruction backstop was a
   flat 80000, which an 8192-frame block of dense code exceeds (Reaper renders at a JACK period of 8192),
   so the core fell behind the host clock and each block's tail was blank; smsggdj booted in 8192-frame
   blocks never started its song. The budget is now derived from the block, as `MesenNesSystem`'s is.
   Found by the matrix's silence guard; covered by a new Catch2 case on a NOP-spin ROM.

A fourth defect was found and is recorded as it is, not fixed (§1): **Arduinoboy MIDI sync plays only
when every host block carries a clock tick.** At 120 bpm and divisor 1 that needs blocks of 1024 frames or
more, so at 128 to 768 (ordinary DAW buffers) LSDj never starts, a divisor of 4 never starts at 1024
either, and 2048 plays louder than 1024. The existing test passes only because `renderAudio` always uses
1024-frame blocks. The baseline pins the silent cases; fixing it is its own change with a re-record.

## 3. The case matrix

In-repo ROMs cover most of it: `mGB.gb` (and the embedded build), `bliptoaster.nes` and its MMC5 / N163 /
S5B / VRC6 / VRC7 builds, and smsggdj for SMS and GG. LSDj, risa (2.3.0 with `let_go.srm`) and nanoloop
(GBA) are sibling fixtures under `RETROPLUG_RESOURCES_DIR`; those groups skip in CI, counted in the skip
baseline. 149 cases in 20 group files; the parity suite is about 94 s of CPU against 175 s for the rest of
`test:native`, its slowest file about 8 s.

| File(s) | Cases | Covers |
|---|---|---|
| `blocks-{mgb,nes,sms,gg}` | each system × {44.1, 48 kHz} × blocks of {64, 199, 512, 1024, 8192} and a cycle of all of them | partitioning, resampler carry, MIDI inside blocks |
| `routing-{stereo,twoPerInstance,onePerInstance}` | outputs {2,4,8} × {1,2,3,5} systems, mixed mGB / BlipToaster / VRC6 | summation order, OnePerInstance's mono association, more systems than pairs |
| `routing-split`, `routing-split-expansion` | ChannelSplit (GB stems, NES core channels), PinSplit, StereoPinSplit × {2,4,8}; each expansion build under PinSplit and ChannelSplit; a split with two systems | lane plans, the fall-back to Stereo |
| `midi-routing` | the 4 modes over 3 systems on separate pairs, notes on all 16 channels with ties, a SysEx; a mid-run mode switch | the routing role; fix 1 |
| `link` | two linked mGBs, a third unlinked, a link made and broken mid-run | the link group as one stepping unit (a linked mGB hears no MIDI: its serial port is the cable) |
| `edits-live`, `edits-structure` | gain through a held note, live role knobs; reset, duplicate, add, remove, replace mid-run, a split losing and regaining its single system, a pin split left | edit timing, per-frame gain smoothing, the tap restore; fix 2 |
| `sync-sms` | smsggdj SYNC IN24 on SMS and GG: start, bpm change, stop, locate, a short block at a loop wrap | transport-driven sync |
| `lsdj-sync` *(fixture)* | SYNC=MIDI at divisors 1/2/4/8 and with autoStart; Arduinoboy at 1024 and 512 | lsdj-sync's clock modes; the Arduinoboy defect |
| `lsdj-modes` *(fixture)* | off, keyboard, midiMap, keyboardMidi, midiPassthrough (raw clocks into SYNC=MIDI), midiOut (play / stop / play), masterSync | every other lsdj-sync mode; MI.OUT and master clock as MIDI out |
| `lsdj-link` *(fixture)* | leader and follower on one cable | link-cable sync |
| `risa` *(fixture)* | the transport script, also under PinSplit | risa-sync |
| `gba` *(fixture)* | nanoloop booting under two routings (silent until played) | the GBA core at all |
| `controller` *(fixture)* | pad presses launching rows on an Arduinoboy LSDj in MIDIMAP | the Launchpad role, controller-in |

Plus `determinism`, `render-blocks` and `comparator`, the harness's own tests (§6).

## 4. What B0 hands B1

Recording today's engine fixes these requirements for B1, all following from §1's strictness:

- **A host block is rendered as one block, never split.** SameBoy drops overproduced samples per block,
  Mesen carries its resampler leftovers, and Arduinoboy sync depends on whether a block carries a tick,
  so splitting a block changes the output. `GraphHost`'s `maxFrames` therefore comes from the host's
  maximum block size (8192 in the matrix).
- MIDI staged at one frame keeps its staging order; the executor's event queues must not reorder ties.
- An edit lands before the next `processBlock`, wherever it is drained.
- MIDI out keeps today's one-block latency and frame handling through whatever replaces the kernel's
  MI.OUT and master-clock paths.

## 5. The baseline files

### 5.1 Format

One file per group, `test-native/parity/baseline/<group>.json`, because the group files run as parallel
processes. Keys sorted, one value per line, so a re-record diffs line by line:

```json
{ "cases": { "stereo-o8-n3": {
      "midiOut": { "count": 12, "fnv": "5b0f22c1" },
      "ram": { "0": { "fnv": "0c1d7e3a", "length": 8192 } },
      "segments": [ { "frames": 72000, "lanes": [ "9c1e04a7 0.08120", "..." ] } ],
      "sram": { "1": { "fnv": "e83a9d10", "length": 32768 } } } },
  "target": "linux-x64-gnu13" }
```

- A lane is `"<fnv> <rms>"`: FNV-1a (32-bit) over its float bit patterns for that segment (not running,
  so the first segment that differs can be named), and its RMS to 4 significant figures ("0" for exact
  silence). The RMS comes from the same bits, so it is as stable as the hash; it makes a re-record
  readable in review ("lane 3 went silent").
- MIDI out is hashed over `(block, slot, frame, bytes)` in order, with its count. Systems are keyed by
  project slot, not id: ids differ between a case run in its group and run alone.
- At the end of a case: SRAM for each battery system, and **work RAM for every system**. Work RAM is
  stable only because of §2.1; it catches state a stimulus changed without making a sound (raw MIDI
  passthrough, a sync clock that has not yet started a note).

### 5.2 Targets

- **On the baseline's target**, a case compares lanes, MIDI out, SRAM and work RAM.
- **On any other target** (CI's arm64, macOS and Windows jobs), a case does not skip. It renders twice and
  asserts the two audio results are bit-identical, and compares MIDI out, SRAM and work RAM against the
  baseline, since those are integer emulation output. The test name says which check ran
  (`<case> (deterministic here; audio baseline is linux-x64-gnu13)`). The skip baseline therefore never sees
  a platform-dependent skip, and the rig is exercised on every CI platform; it does cost those jobs a
  second render of each case. Should MIDI out or work RAM ever differ across platforms, that is a finding
  in its own right.

A fixture case with its fixture absent skips with a counted reason on every target, as today.

### 5.3 Re-recording

`RP_PARITY_UPDATE=1 pnpm test:native parity/<group>` re-records; `RP_PARITY_ONLY=<case>` narrows a group to
one case. A recording is refused, case by case, and the file is then left untouched:

- off the baseline's target;
- when a case's `audible` lanes are silent after its first segment (the boot: a boot chime is not what a
  case means by "sounds"). A case whose recorded behaviour IS silence lists no lanes and says why;
- when its MIDI, controller traffic and transport change nothing: the case is also run with them stripped,
  and an identical digest means no system heard the stimulus (MIDI sent before a cart finished booting
  records only the boot, which is exactly what the first draft of the mGB cases did);
- when it depends on the cases before it: each case is re-run alone in a fresh host process (the bundle
  spawns itself with `RP_PARITY_EMIT`), and its digest must match the group run's.

A fixture case skipped during a recording keeps its entry. Entries for cases no longer in a group's table
are dropped on an unfiltered recording, and an unfiltered check fails while any remain.

### 5.4 Failure reports

One failing `test()` per case, naming the first segment and lane that differ with the expected and actual
hash and RMS, plus the MIDI, SRAM and work-RAM verdicts. With `RP_PARITY_DUMP=<dir>` the case's lanes are
also written as 32-bit float WAVs and its MIDI out as JSON, for comparison against a run at an older commit.

## 6. Proving the harness

- **The comparator, alone** (`comparator.test.ts`): one flipped low bit in one float fails and names that
  segment and lane; so does one changed MIDI byte, a moved MIDI frame, one SRAM byte and one work-RAM byte.
- **End to end**: scaling SameBoy's output gain by `1 + 1e-7` (far below a 24-bit LSB) failed all twelve
  `routing-stereo` cases, each of which holds an mGB, with identical RMS (`0.01011`) and a different hash,
  while the NES-only `blocks-nes` stayed green.
- **`renderBlocks` against `renderAudio`**: at 2 outputs in 1024-frame blocks the two are bit-identical,
  which ties the new call to the path every existing test uses; and an 8-lane render of one system under
  Stereo lands on the first pair only.
- **The determinism switch** (`determinism.test.ts`): two SameBoy boots and two NES boots have identical
  work RAM, and two smsggdj plays render identical audio. All three fail with the switch off.
- **The guards** of §5.3 each caught a real mistake while the matrix was built: the isolation check found
  the SMS VDP's random start; the silence guard found the SMS block cap, the Arduinoboy defect and a
  `loadMgb()` that replaces rather than adds; the stimulus guard found MIDI sent during mGB's boot.

## 7. The Reaper leg: dropped

The plan was to sha256 the nine `reaper:all` render WAVs before B1 and compare after each step, reaching
`PluginDSP::run` (DAW event staging, frame offsets, transport), which the matrix does not. Measured with
cores powered on in a fixed state and each WAV's PCM data chunk hashed (Reaper's BWF header carries a
timestamp): over two runs of two re-authored passes each, four and then five of the nine legs rendered
different PCM in consecutive passes, and which legs changed from run to run. No leg is repeatable enough
to hold bit for bit, so per the agreed fallback B0 is the matrix alone. `spec/06-build-test.md` claimed the
renders were byte-identical run to run; that sentence is corrected. Why they vary is not yet known.

The plugin layer is therefore not hashed. It stays covered by the existing tolerance-based Reaper
analyzers.

## 8. Work order

The implementation plan's tasks, each verified before the next:

1. The two store fixes.
2. The determinism switch for SameBoy and NES.
3. `renderBlocks` and `buildTarget`.
4. The digest and comparator.
5. The host's keep-alive, the rig, the scenes and the group runner.
6. SMS determinism (the VDP start).
7. The SMS block budget.
8. One task per case group, each recorded and checked.
9. The skip baseline.
10. Docs: `spec/06-build-test.md`, AGENTS.md, and §8 of the main design.

## 9. Not in B0

- The plugin's own layer (`PluginDSP::run`'s event conversion, CC automation, master gain), per §7.
- The SDL standalone and the offline `RenderHost` call the same `Engine::processBlock`, so they share the
  matrix's coverage; they get no case of their own.
- Exact audio on arm64, macOS and Windows: those targets get the determinism and integer-state checks of
  §5.2, and the by-construction argument of §1 for the audio bits.
- Fixing the Arduinoboy block-size defect (§2.5).
