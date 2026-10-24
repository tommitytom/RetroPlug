# Design: Phase B0, the parity baseline

**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** designed, not built.

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
| **B0. Parity baseline** (this doc) | Determinism switch, `renderBlocks`, a store-driven case matrix, committed hashes of today's engine, a Reaper render check; the two TS bugs below fixed first | Only the two fixes | Nothing |
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
| How are cases driven? | **Through the stores, in `test:native`**, as the gate; plus **sha256 of the Reaper render WAVs** as a local check of the plugin layer the matrix cannot reach. |

Float results are only bit-stable on one compiler and architecture (r8brain uses SIMD intrinsics;
arm64 fuses multiply-adds), so audio is compared exactly on the baseline's target only (§5.2). Other
platforms are covered by construction: the same float operations in the same order give the same bits.

## 2. Components

### 2.1 The determinism switch

`RETROPLUG_DETERMINISTIC=1` in the environment, read once per process:

- SameBoy: `GB_random_set_enabled(false)`, called before the first core is built. `GB_random()` then
  returns 0, so power-on WRAM, HRAM, OAM and wave RAM are zero rather than seeded from `time(NULL)`.
  The flag is a process-global static, which suits a switch that covers the whole process.
- Mesen NES: `NesConfig::RamPowerOnState = RamState::AllZeros` (the default is `Random`).
- Mesen SMS already pins `AllZeros`, and `GbaConfig` defaults to it; neither changes.

It is an environment variable so it also reaches the plugin inside Reaper. Only the parity rig and the
Reaper parity script set it; the app never does, and its default behaviour is unchanged.

SameBoy's randomness was measured not to reach PCM or SRAM (five seeds, mGB and LSDj); NES RAM does
reach state. The switch makes that independence a guarantee instead of a measurement.

### 2.2 `renderBlocks`

A test-host driver call beside `renderAudio`, on the driver facet only (the plugin channel never
exposes it):

```ts
renderBlocks(req: {
  outputs: 2 | 4 | 6 | 8;              // the lane count processBlock gets
  blocks: {
    frames: number;                    // 1..8192
    midi?: { frame: number; bytes: number[] }[];  // staged in list order
    controller?: number[][];           // control-surface traffic, staged as stageControllerIn stages it
    playing?: boolean; bpm?: number; ppq?: number; // applied after the drain, before processBlock
  }[];
}): { lanes: Float32Array[]; midiOut: { block: number; system: number; frame: number; data: Uint8Array }[] }
```

Per block it repeats `PluginDSP::run`'s order:

1. `engine.stageMidi(frame, bytes)` for each event, then the controller traffic;
2. `invoker.drainInto(engine)`;
3. the transport fields given;
4. `engine.processBlock(frames, lanes, outputs)`;
5. copy `engine.midiOut()`, tagged with the block index, then `clearMidiOut()`.

The plugin's master gain is left out: it is plugin code applied after the engine.

It refuses a frame at or beyond its block, frames that go backwards within a block, and any call while
the background audio thread runs. DAWs deliver events frame-sorted, so the baseline never pins what
today's engine does with unsorted input (which would oblige B1 to copy it). Ties at one frame are
allowed, and their list order is part of the baseline.

The response is raw planar PCM (about 3 MB for 2 s of 8 lanes at 48 kHz). TS hashes it, which keeps the
native side a thin loop and lets a failure write the lanes out (§5.4).

The host also reports the target it was built for (§5.2) through a `buildTarget()` driver call, from a
compile-time definition, so the key always describes the binary that ran.

### 2.3 The parity rig

`test-native/parity/rig.ts` builds the app stack the way the app does (`createRealBackend`,
`ProjectStore` with `buildAppRegistry()`, the DSP runtime with the kernel loaded, the
`syncDspFromStore` wiring, `createAudioDriver`) and exposes `project` and `render(...)`. It is the only
file B2 and B3 edit: a case written against the stores survives every step, since B2 deletes the
backend calls the stores make today but not the stores.

Before each case the rig removes every system, reloads the kernel, and resets routing, MIDI routing,
transport, ppq, bpm and sample rate, then asserts the engine holds no systems.

Block helpers: `even(totalFrames, size)` and `cycle(totalFrames, sizes)`, the second mimicking a DAW's
irregular blocks (Reaper's loop wrap delivers a short one).

### 2.4 Cases and groups

A case is `{ name, needs?, sampleRate, outputs, audible, setup(project), segments }`. A segment is a
run of blocks, optionally followed by a store edit (`after(project)`); `audible` lists the lanes that
must sound (§6). Cases live in a table per group, and each group is one `test-native/parity-<group>.test.ts`
file of about 5 s, so `test:native`'s wall clock does not grow (today: 123 files, about 110 s of CPU,
longest file 7.6 s). One `test()` per case.

An edit is a store call between two `renderBlocks` calls. On the test host it applies at once; in the
plugin the same edit is drained at the start of the next block. Both land before that block's
`processBlock`, so they are equivalent.

### 2.5 The two bugs fixed first

Recording a bug into the baseline would oblige B1 to B3 to copy it. Both are fixed, each with a test,
before anything is recorded:

1. **`ProjectStore.setMidiRouting` never re-drives the kernel.** It writes `projectSettings.midiRouting`,
   but the project-scope `midi-routing` role is synthesized from that setting at projection time
   (`kernelProjection`), so the kernel runs the old mode until the next structural edit. The fix calls
   `onSystemsChange()`, as `setController` already does. Test: a mock-backend check that the projection
   carries the new mode; plus the mid-run switch case in `parity-midi-routing`.
2. **`SystemsStore.duplicateSystem` skips `applyConstructHooks`.** The LSDj, BlipToaster and risa
   asset roles fold their overrides into the ROM in memory at construct time (`onConstruct`), so a
   duplicate boots the unpatched base ROM under a savestate captured from the patched one. The fix runs
   the hooks. Test: a fixture-free BlipToaster asset override survives Duplicate.

## 3. The case matrix

In-repo ROMs cover most of it: `mGB.gb`, `bliptoaster.nes` and its MMC5 / N163 / S5B / VRC6 / VRC7
builds, and smsggdj for SMS and GG. LSDj, risa and the GBA ROM (nanoloop) are sibling fixtures, reached
through `__RESOURCES_DIR__` / `__RISA_SRC__`; those groups skip in CI, and the skips are counted.

| Group | Cases | Covers |
|---|---|---|
| `routing-many` | Stereo / TwoPerInstance / OnePerInstance × outputs {2,4,8} × {1,2,3,5} systems of mixed cores | summation order, OnePerInstance's mono association, more systems than pairs |
| `routing-split` | ChannelSplit (mGB stems, NES core channels), PinSplit, StereoPinSplit × {2,4,8}; NES `channelExportMode` × each expansion build; a split mode with 2 systems | lane plans; the fall-back to Stereo |
| `midi-routing` | the 4 modes × 3 systems; notes on all 16 channels; ties at one frame; a SysEx; a mid-run mode switch | the routing role; fix 1 |
| `blocks` | {mGB, BlipToaster, smsggdj SMS, smsggdj GG} × {44.1 kHz, 48 kHz} × even {64, 199, 512, 1024, 8192} and a cycle; MIDI inside blocks | partitioning, resampler carry, intra-block MIDI frames |
| `link` | two linked mGBs; plus a third unlinked; joining and leaving a group mid-run | the link group as one stepping unit |
| `edits` | gain change mid-note; live role config; reset; add / remove / duplicate / replace mid-run; a split routing, then a second system added | edit timing, per-frame gain smoothing; fix 2 |
| `sync-sms` | sms-sync on SMS and GG: start, stop, locate, bpm change, a short block at a loop wrap | transport-driven sync |
| `controller` | the Launchpad role fed controller traffic | controller-in |
| `lsdj` *(fixture)* | the 9 sync modes, each with its own stimulus; `tempoDivisor` 1/2/4/8; `autoStart`; MI.OUT captured; two LSDj linked | lsdj-sync; MI.OUT's one-block latency |
| `risa` *(fixture)* | risa-sync over the same transport script; risa under PinSplit | risa-sync |
| `gba` *(fixture)* | nanoloop boot; one routing | GBA at all |

Most cases render 0.5 to 2 s; about 150 cases in total.

## 4. What B0 hands B1

Recording today's engine fixes these requirements for B1, all following from §1's strictness:

- **A host block is rendered as one block, never split.** SameBoy drops overproduced samples per block
  and Mesen carries its resampler leftovers, so splitting a block changes the bits. `GraphHost`'s
  `maxFrames` therefore comes from the host's maximum block size (8192 in the matrix).
- MIDI staged at one frame keeps its staging order; the executor's event queues must not reorder ties.
- An edit lands before the next `processBlock`, wherever it is drained.
- MIDI out keeps today's one-block latency and today's frame handling through whatever replaces the
  kernel's MI.OUT path.

## 5. The baseline files

### 5.1 Format

One file per group, `test-native/parity/baseline/<group>.json`, because the group files run as
parallel processes and a shared file would have concurrent writers. Canonical JSON:

```json
{ "target": "linux-x64-gnu13",
  "cases": { "stereo-o8-n3": {
      "segments": [ { "frames": 48000, "lanes": [ { "fnv": "9c1e04a7", "rms": 0.0812 } ] } ],
      "midiOut": { "count": 12, "fnv": "5b0f22c1" },
      "sram": { "1": { "length": 32768, "fnv": "e83a9d10" } } } } }
```

- Lane hashes are FNV-1a (32-bit) over the float bit patterns, one per lane per segment (not running),
  so the first segment that differs can be named.
- `rms` is the lane's RMS for the segment to 4 significant figures. The bits are identical, so it is
  stable; it makes a re-record readable in review ("lane 3 went silent").
- MIDI out is hashed over `(block, system, frame, bytes)` in order, with its count.
- SRAM is read at the end of each case for every battery system, keyed by slot.

### 5.2 Targets

The target key is `<os>-<arch>-<compiler><major>` (`linux-x64-gnu13`), reported by the host binary
(§2.2). Build type is deliberately left out: x86-64 SSE2 math is not reassociated at any optimisation
level without `-ffast-math`.

- **On the baseline's target**, a case compares lanes, MIDI out and SRAM against the baseline.
- **On any other target** (CI's arm64, macOS and Windows jobs), a case does not skip. It renders twice and
  asserts the two audio results are bit-identical, and compares MIDI out and SRAM against the baseline,
  since both are integer emulation output. The test name says which check ran. The skip baseline
  therefore never sees a platform-dependent skip, and the rig is exercised on every CI platform. If MIDI
  out or SRAM ever differ across platforms, that is a finding in its own right.

A fixture case with its fixture absent skips with a counted reason on every target, as today.

### 5.3 Re-recording

`RP_PARITY_UPDATE=1 pnpm test:native parity-<group>` re-records; a filter is allowed, and
`RP_PARITY_ONLY=<case>` narrows a group to one case.

- Refused off the baseline's target.
- Refused for a case whose group-run and alone-run hashes disagree (§6), or whose audible lanes are
  silent.
- A fixture case skipped during an update keeps its existing entry.
- Entries for cases no longer in a group's table are removed, on an unfiltered run of that group only.

### 5.4 Failure reports

One failing `test()` per case, naming the first segment and lane that differ with the expected and
actual RMS, plus the MIDI and SRAM verdicts. With `RP_PARITY_DUMP=<dir>` the case's lanes are also
written as 32-bit float WAVs and its MIDI out as JSON, for comparison against a run at an older commit.

## 6. Proving the harness

- **The comparator, alone:** flipping the low bit of one float in one lane and segment fails and names
  exactly that segment and lane; so does one changed MIDI byte and one changed SRAM byte.
- **No baseline from silence:** recording refuses a case whose `audible` lanes are silent, so a broken
  boot or a stale fixture (the old silent-render VST3 class-id incident) cannot become the baseline.
- **`renderBlocks` against `renderAudio`:** at 2 outputs with even 1024-frame blocks, the two are
  bit-identical. That ties the new call to the path every existing test uses.
- **The determinism switch:** two NES boots with it on give identical RAM; with it off they need not.
- **Independence:** when recording, each case is also run alone (`RP_PARITY_ONLY`), and the hashes must
  match its group run. Leaked engine state would otherwise make a hash depend on case order.

## 7. The Reaper leg

`tools/run-reaper-parity.sh` (`pnpm reaper:parity`) runs the 9 render legs of `reaper:all` with
`RETROPLUG_DETERMINISTIC=1`, under the suite's concurrency cap (the `/dev/shm` limit of about four
jackd servers), and sha256s each WAV into a gitignored `.reaper-parity.json`. `--record` writes it; a
plain run compares.

- It first renders each leg twice. A leg whose renders differ is left out, and the script prints why,
  so it never fails at random.
- The renders are 24-bit, and a one-ulp float drift sits below a 24-bit LSB, so this leg catches
  plugin-layer breaks (event staging, frame offsets, transport, a silent render) rather than numeric
  drift. The matrix covers numeric drift.
- Not in CI: it needs Reaper and the fixtures.

## 8. Work order

Each a commit (or a few), each verified before the next:

1. The two bug fixes with their tests.
2. The determinism switch with its test.
3. `renderBlocks` and `buildTarget`, with the contract's refusals and the `renderAudio` equivalence.
4. The rig, the comparator and the baseline writer, with their self-tests.
5. One commit per case group, each with its recorded baseline.
6. The skip-baseline update.
7. The Reaper leg, and what its repeatability check found.
8. Docs: `spec/06-build-test.md` (the parity suite and how to re-record), §8 of the main design (Phase
   B's split), and AGENTS.md's verification loop.

## 9. Not in B0

- The plugin's own layer (`PluginDSP::run`'s event conversion, CC automation, master gain) is reached
  only by the Reaper leg, not hashed per block. A headless plugin harness was considered and rejected
  as too much new infrastructure for what the Reaper leg already covers.
- The SDL standalone and the offline `RenderHost` call the same `Engine::processBlock`, so they share
  the matrix's coverage; they get no case of their own.
- Exact audio on arm64, macOS and Windows: those targets get the determinism, MIDI-out and SRAM checks
  of §5.2, and the by-construction argument of §1 for the audio bits.
