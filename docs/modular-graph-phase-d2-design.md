# Phase D2: kit compiles as offline graphs

**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** built (D2, seven commits); section 11 records
the build, sections 3 to 6 are corrected in place, and sections 8 and 9 carry what it learned.

D1 ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md)) made kit output reproducible, recorded it as
a byte baseline, moved the kit DSP into core-free kernels and put those kernels in the graph as `rp.dsp` nodes. D2 runs
the kit compile itself as graphs: a generic offline job seam runs a graph document to completion on its own host and
returns the buffers it recorded, TypeScript builds the kit chain's documents and packs the bank, and native
`KitCompiler` retires. Every kit that compiles stays byte-identical to D1's baseline. The same seam later serves D3's
renders and user-built sample graphs, and because it runs in every host, the plugin gains kit compiles. Phase D's
outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).

## 1. Before D2

- **The kit compile is native and harness-only.** `KitCompiler` decodes each slot's file through `SampleCache`
  (miniaudio, mono float), runs the codec's `encode` per slot on an enkiTS pool, then `assemble`s the bank.
  `compileKit` and `compileDmc` are registered in the harness facet only, so the CLI can compile a kit and the plugin
  cannot.
- **The chain's order is the bytes** (D1 section 8): window, the effects at the source rate in list order, resample to
  the kit rate, quantise to codes (the dither, the last one winning, or plain rounding), trim to whole 32-sample frames,
  the nibble pack (with `rotate`), the bank. risa skips the dither and runs `convertF32ToDpcm` (int16, a DC blocker, a
  whole-buffer peak normalise to a 7-bit swing, a 1-bit delta) and its own 8 KB bank.
- **Two steps need the whole buffer**: a normalising gain's peak, measured at its point in the chain, and the dither's
  seed, `rp::dsp::inputSeed` over the whole resampled, pre-trim buffer. A streaming graph cannot do either in one pass.
- **What the graph offers today**: the executor already runs with no audio thread (the quiescent path); the `rp.dsp`
  nodes, `rp.dsp.resample` (a buffer player resampling to the plan rate), `rp.core.buffer-player` and
  `rp.core.buffer-recorder`. Missing: any way to put audio into a document (`ResourceTable::createAudioBuffer` has no
  production caller and no RPC), any way to read a buffer back, and a run that ends.
- **D1's nodes cannot carry every kit as they are**: their param ranges are narrower than a kit spec's and the planner
  clamps silently; the biquad and dither guards differ from the kernels on a non-finite sample and on a filter run away
  above Nyquist; a plan cannot fill a buffer and resample it; `rp.dsp.dither` outputs levels, not codes.

## 2. Decisions taken

The user's, on 2026-11-01:

- **TypeScript builds, native runs.** A generic offline-run seam runs documents; TypeScript builds the kit chain's
  documents and packs the bank; native `KitCompiler` retires. Not native building and running the graphs internally.
- **Whole-buffer steps are nodes reading a bound buffer**: passes split at each whole-buffer step, and the next pass's
  node reads the recorded buffer whole when it activates, on the control thread (a normalising player; the dither's
  seed). Native does every float the bytes depend on. Not TypeScript computing between passes, and not measure nodes
  reporting values.
- **Widened ranges, refusal beyond**: `rp.dsp.gain` and `rp.dsp.biquad` cover any sane kit value, and the kit compiler
  refuses a spec outside them with an error. Not params fed by unclamped wires, and not unclamped settings.
- **The guard regimes are refused**: a source with a non-finite sample and a filter at or above its source rate's
  Nyquist are errors. Every kit that compiles is byte-identical; the guards stay as they are for live patches. Not
  sanitising the input, and not an unguarded kit mode.
- **Offline runs are async jobs on a worker pool**, in every host, the plugin included; slots run in parallel. Not a
  synchronous harness-only RPC, and not one job at a time.
- **Every prototype stage is reviewed by Opus.**

## 3. The offline job seam

**A job.** One engine RPC submits a job: a graph document, its input buffers (interleaved float samples with a rate and
a channel count, or a file path native decodes, below), which recorded buffers to return, and the frames to run. It
returns a job id, or a refusal; completions (the job id, ok or the reason, the returned buffers) drain from a polled
queue, the C5 capability-job pattern. TypeScript gets `backend.offlineRun(request)`, a promise over submit and poll.

**Where it runs.** A small native worker pool (one thread per running job, at most clamp(hardware_concurrency, 1, 8)
workers, started on demand and parked when idle). Each job gets a fresh offline `GraphHost`: its own resource table, no
audio thread, the quiescent block driver, calling `GraphHost::processBlock` (drain, then run) in fixed blocks for the
requested frames (no second block driver). The pool is apart from the live engine, so a job never touches the plan that
is playing; a job's host is built for it and torn down with it, so every resampler dies inside that host's life (D1's
teardown rule for r8brain's statics).

**Inputs and outputs.** An input becomes an `audio-buffer` resource through `createAudioBuffer`, named in the document
as the `res:N` handle the submission assigns. A file input is decoded natively through `SampleCache` (the miniaudio
decode the kit compiler reads its sources with) and windowed natively by an offset, a length and a frame cap (as the
kit compiler's window does); its creation reports whether any sample is non-finite. A buffer a run writes through
`rp.core.buffer-recorder` is returned in the completion, its samples, rate and channel count, and whether any sample in
it is not finite.

**Every host.** The RPC sits in a facet the plugin, the SDL standalone, the CLI and the Node addon all bind. A job runs
off every audio and UI thread and allocates nothing on any block thread.

**Failures.** A planner refusal, an activate error (an r8brain allocation failing), a node fault or a cancel completes
the job with the reason. A guard a node reports does not fail a job: the completion carries how many times one fired
and the first reason, and a non-finite value in a returned buffer is flagged, so what either means is the caller's to
decide (a kit compile refuses the pass). Destroying the engine abandons its jobs in flight: the queued ones are dropped
unrun, the running ones stop at their next block boundary, every worker is joined, and completions not yet drained go
with the pool.

## 4. The nodes D2 adds or changes

- **Widened descriptors**: `rp.dsp.gain` linear -100..100; `rp.dsp.biquad` frequency 1..96 000 Hz, Q 0.001..1000, gain
  -96..96 dB. The planner cannot see the plan's rate, so the kit compiler refuses a frequency at or above its source
  rate's Nyquist itself. The descriptor goldens re-record once; live patches get the wider ranges.
- **`rp.dsp.normalize`**: a buffer player that normalises. It names an `audio-buffer` in its settings (as
  `rp.dsp.resample` does); at activate, on the control thread, it computes the peak over the channels it plays (the
  loudest of each played channel's `rp::dsp::peak`) and `rp::dsp::normalizedGain(gain, peak)`, then streams the buffer
  times that gain, sample for sample the multiply `GainEffect` makes today. A buffer channel it does not play is
  silence and never scales a played one.
- **`rp.dsp.dither` gains `seedFrom`**: an optional buffer reference; when set, activate seeds every channel from
  `rp::dsp::inputSeed` over that whole buffer, so channel 0 matches the kit path's seed. The kit graph points it at the
  resampled buffer the dither plays, at its full pre-trim length.
- **`rp.dsp.dpcm`**: risa's DMC encoder as a buffer-reading node. `convertF32ToDpcm`'s whole-buffer DC blocker, peak
  normalise and 1-bit delta move into a kernel in `retroplug-dsp-kernels`; the node runs the kernel over its bound buffer
  at activate into storage it owns, then streams the delta bits as audio levels of plus and minus one.
- **`rp.host.guards/1`**: a host service (`rp_node.h`) a node reports into when one of its guards replaces what its
  kernel would have computed, with a string-literal reason, called on the block thread and never blocking or
  allocating. The biquad's reset-and-silence and the dither's restart report there; the live engine offers no service,
  so a live patch's guard is as it was, and an offline job's host counts the reports into its completion.
- **Unchanged**: `rp.dsp.resample`, `rp.core.buffer-player`, `rp.core.buffer-recorder`.

## 5. The TypeScript kit compiler

`compileKit(spec)` and `compileDmc(spec)` become TypeScript (`packages/retroplug/src/kit/`), returning the same bank
bytes: their first argument is a runner (a backend's offline jobs) and their result is `{bytes, errors}`, the bank and
one reason per spec sample, null exactly when the bank holds that sample whole. Each slot is a chain of offline jobs;
slots run in parallel up to the pool's size.

**LSDj, per slot, in today's order:**

1. **Input**: the slot's file, decoded and windowed natively. A non-finite sample refuses the slot.
2. **Effects at the source rate**: the effect list split at each normalising gain, each segment one job (the previous
   buffer played, by `rp.dsp.normalize` where the segment starts with a normalising gain, then the gains and biquads in
   list order, into a recorder). A dither effect is remembered, the last winning. A value outside the node ranges or a
   filter at or above the source's Nyquist refuses the slot.
3. **Resample**: `rp.dsp.resample` to 11 468 Hz into a recorder, run for exactly `resampledLength(n, in, out)` frames.
4. **Quantise**: the resampled buffer played into `rp.dsp.dither` (the effect's algorithm with `seedFrom` that buffer,
   or `none`) at 4 bits, into a recorder.
5. **Pack, in TypeScript**: invert the levels to codes (`round((level + 1) * 0.5 * 15)`, exact), trim to whole 32-sample
   frames, pack nibbles with `rotate`, and assemble the 16 KB bank through the existing `buildKitBank` (made to match
   native `assemble` byte for byte wherever it does not).

**risa**: the same source-rate effects (no dither), resample to the slot's DMC rate, then `rp.dsp.dpcm` into a recorder;
TypeScript packs the delta bits LSB first, pads to a legal `16k+1` length and assembles the 8 KB bank with the existing
TS risa packer.

**Failures, as today**: a slot that fails is left empty and the kit still compiles; new, the result also lists each
slot's error, so the CLI's read-back check gains a reason. A refused spec is that slot's error, and so is a pass that
records a non-finite sample or one a node's guard stepped into. Every slot the bank does not hold whole has a reason,
a failure or not: an empty window, a window that resamples to under one wave frame, a sample the bank's space clipped
or could not fit, and a sample past the bank's slot count.

**Retired**: `KitCompiler`, the compile use of `SampleCache` beyond the job seam's decode, `LsdjKitCodec`,
`RisaDmcCodec`'s encode, and the harness-only `compileKit` / `compileDmc` RPCs. Kernels and helpers the nodes or tests
use stay.

## 6. Tests

- **The baseline is the oracle.** `kit-baseline.json` (D1's 39 entries) stays the one record. A test-native suite
  runs the TypeScript compiler through real offline jobs over the same matrix with the same synthesised sources and
  compares each bank's FNV hash and slot length to the golden. While both compilers exist, the C++ suite holds native
  `KitCompiler` to the same file too, so they are held to one record until it retires with the compiler; a second
  golden, `kit-edges.json`, records the banks past the baseline's matrix. Every entry must match through the graph
  compile before any caller moves; a mismatch means the graph is wrong, and nothing is re-recorded.
- **The seam**: a job runs a document to completion and returns its buffers; parallel jobs do not interfere (TSan); a
  cancel stops a job; an engine torn down with jobs in flight abandons them (the running ones stop and nothing
  completes); a job allocates nothing on any audio thread; file inputs equal the `SampleCache` decode; a job runs while
  an audio thread plays the live plan and changes nothing in it; a guard a node reports is counted in the completion
  and a non-finite recording is flagged; a job computes in the default floating-point environment whatever its
  submitter's.
- **The nodes**: `rp.dsp.normalize` equals today's normalising `GainEffect` bit for bit; `seedFrom` gives channel 0 the
  kit path's seed; `rp.dsp.dpcm`'s bits equal `convertF32ToDpcm`'s; the widened descriptors' goldens.
- **Refusals**: a non-finite source, a filter at or above Nyquist and each out-of-range value give their errors, and a
  refused slot leaves the rest of the kit compiling.
- **Unchanged**: strict parity bit for bit; test-native's `lsdj-rom` and `risa-kit`; the CLI's `build-kit`,
  `import-sample`, `risa-rom` and `bliptoaster-rom` sessions; both consumer suites; the new tags under both sanitizers.
- **New**: a test that the plugin build compiles a kit (a capability it gains).

## 7. Landing

Seven commits, each green:

1. **Widened `rp.dsp.gain` / `rp.dsp.biquad` ranges** (the descriptor goldens re-recorded).
2. **The offline job seam**: the native pool and per-job offline `GraphHost`, the submit / drain RPCs in a facet every
   host binds, file inputs (decoded, windowed, the non-finite report), TypeScript `offlineRun()`, and its tests.
3. **The nodes**: `rp.dsp.normalize`, dither's `seedFrom`, the DPCM kernel and `rp.dsp.dpcm`.
4. **The TypeScript kit compiler**, beside native, with the test-native baseline runner proving all 39 entries.
5. **The switch-over**: the CLI sessions and `audioDriver` on the TypeScript compiler, per-slot errors to the CLI's
   check, the plugin-host kit compile test.
6. **Native retires**: `KitCompiler`, `LsdjKitCodec`, the DMC encode path and the two harness RPCs deleted; the C++
   baseline suite goes (the test-native runner and the shared golden are the oracle).
7. **Docs.**

As D1, with one change: a prototype on a local `proto/` branch, its stages 2 (the job seam's threading and lifetimes)
and 4 (the compiler, where byte identity is earned) built by Opus and the rest by DeepSeek, every stage reviewed by Opus
and fixed after its review; the plan generated from the prototype and replayed, then an Opus plan review; DeepSeek
implementers and follow-ups, Opus code reviews of tasks 2, 4 and 6, a DeepSeek acceptance run, an Opus final review.

## 8. Left for D3

The CLI `render` and the UI's background renders on the offline jobs, keeping their block-size sequence and per-block
side effects or re-scoping their byte-identity claim. What the seam hands them, and what D2's build settled about it:

- A render sees `guardsFired` / `guardReason` and each buffer's `nonFinite`, and decides. Nothing fails on either, and
  a cancelled or torn-down job has no buffers; a render that must stay byte-exact refuses a job whose count is not 0,
  as the kit compiler does.
- A job runs in the default floating-point environment whatever the caller's is (the audio thread's flush-to-zero
  included), so a render's arithmetic is the CLI's.
- Jobs are async and bounded (64 outstanding, up to 8 workers); the UI's render jobs already own a registry and a
  thread each, and moving them onto this seam replaces that with the engine's pool. A host with no UI tick settles a
  job through the native wait (`setOfflineWait`, which `createAudioDriver` turns on) or not at all.
- Block size is the job seam's (1024 frames), not the caller's: D3 either accepts that, or the seam grows a block-size
  request and a render's per-block side effects move with it.
- What a render needs and the seam does not offer, so D3 must extend or decide it:
  - The job registry is `rp.core`, `rp.dsp` and `rp.dsp.resample` only. A render builds an emulated system and runs its
    roles' protocol script nodes, so system nodes (a `SystemFactory`, ROM and battery resources) and a script host must
    reach a job, against invariant 9 and section 11's "a job needs nothing from the engine".
  - There is no TypeScript cancel (stage 2 left it out on purpose, and `offline.ts` has none), where the UI's renders
    cancel today.
  - A completion returns its buffers only at the end of its fixed `frames`, where a render streams per-chunk WAV and
    resolves its length mid-run (LSDj HFF auto-detect via NR52).
  - Jobs are not rejected at project close (stage 2), where capability jobs are.

## 9. Left beyond D2

- A UI for compiling kits in the plugin: D2 makes it possible and adds no menu row. The row imports `src/kit` and runs
  it on the stores' backend, not `audioDriver`: the driver's compile is a CLI convenience, and a UI row must not turn on
  the offline poll's native wait, which would block the frame the compile settles on.
- User-authored sample graphs on the same seam. A script runtime for jobs is not one: a job's document may name only
  the built-in C++ packages, so a user graph of script nodes needs a job-side script host of its own.
- The `deps/enkiTS` directory, vendored but unbuilt: no RetroPlug code links it since the native compile retired, so a
  later cleanup decides whether it goes.
- A kit patcher UI, whose knob-to-ear latency D2's compile does not meet: about 23 ms typical and 83 ms worst for a
  15-slot kit (native was 1.7 and 4.9 ms), nearly all of it buffers crossing the JS thread between passes. The lever is
  chaining a slot's passes inside one job or keeping the intermediates native behind handles, so only the final buffer
  crosses, and recompiling only the slot a knob touched.

## 10. Prototype questions

- Can `createAudioBuffer`, the resource table and a fresh `GraphHost` be built and torn down per job off the control
  thread, with the node registry shared read-only across jobs, and what does a job's host need from the engine (the
  packages, the script host for script nodes, or none)?
- What exactly does today's windowing do with an offset past the end, a zero length and the frame cap, so the native
  window at input creation reproduces it?
- Does `buildKitBank` (and the TS risa packer) produce native `assemble`'s bytes for every baseline case, and where not,
  which side is right?
- How long is a 15-slot kit compile as jobs against today's enkiTS compile, and is the per-job host setup the cost?
- Does the plugin's facet set allow a new engine facet, and how do completions reach a plugin UI that only polls?

## 11. As built

Section 7's seven commits landed in order, each green, and nothing on the emulated audio path moved: strict parity held
bit for bit at every one, and no parity or kit golden was re-recorded (the `rp.dsp` descriptor goldens were, in
stages 1 and 3, as their bullets say). The 39 baseline banks matched the TypeScript compiler through real offline jobs
on the first run, and no later commit moved one; `kit-edges.json` was recorded once, from the compiler, in a run where
every bank also equalled native's. This section records what each stage decided where the design was silent, what
section 10 turned out to be, what building it corrected in sections 3 to 6 (each correction is in place there), and
the findings worth naming; each stage's own report is the full record. Reviews asked for fixes, landed as follow-ups
inside the stages' own commits: stage 1's clamp cases made to bite and its comments corrected, stage 2's poll stalled
chains that await through extra async layers, stage 3's DPCM re-encode moved off the block thread and normalize's peak
made the channels it plays, stage 4's guard handling, non-finite flag and floating-point environment added (the guard
reporting where it first failed a job), stage 5's plugin test settled through a path the plugin does not have, and
stage 6 took enkiTS out of the build. Where a fix changed what a stage built, its bullet says so, and no follow-up
moved a kit byte or a parity baseline either.

### What each stage settled

- **1, the widened ranges.** `rp.dsp.gain` is linear -100..100 (default 1) and `rp.dsp.biquad` 1..96 000 Hz,
  Q 0.001..1000 and -96..96 dB, so a kit spec's plain gain, its normalising gain, a 10 Hz high-pass at Q 20 and a
  30 dB shelf all reach the kernels as written. The planners and `setParam` still clamp a document's value silently and
  a wire is still unclamped, and the biquad clamps nothing itself, Nyquist included, so the compiler does the refusal
  (stage 4). The two descriptor goldens were re-recorded once; the diff is exactly each param's `min` / `max`. Every
  clamp case asks beyond a bound, runs the kernel at the asked value too, and requires the two to differ, so it proves
  the clamp rather than a coincidence at the bound. The frequency ceiling (96 kHz) cannot be a clamp oracle on a
  48 kHz plan, because the guard fires above Nyquist there; the low bounds carry that case's proof.
- **2, the offline seam.** The pool is an `Engine` member (`OfflineJobs`) made at the first submission and reset
  explicitly in `~Engine`, so a host that never submits has no registry and no thread. It runs
  clamp(hardware_concurrency, 1, 8) workers, started on demand while every started one is busy and never retired, with
  64 jobs outstanding (queued, running or finished-undrained) and 64 inputs plus outputs per job. A job's blocks are
  1024 frames, the last partial; its transport is its rate, 120 bpm, ppq 0, not playing. Handles are deterministic:
  input i is `res:(i + 1)`, output j is `res:(inputs + j + 1)`, computed on the TS side (`offlineInputRef` /
  `offlineOutputRef`), and `returns` defaults to every output. The registry is the pool's, filled before any worker
  exists with rp.core, rp.dsp and rp.dsp.resample, never changed, and shared read-only, so a script pack's load or
  reload (which swaps only the engine's registry) cannot race a job; a script, system or host I/O node in a document is
  refused as `unknown-type`. Each job decodes through a `SampleCache` of its own, so a file edited between two compiles
  decodes as it is now. Interleaved float32 crosses the wire in the host's byte order. An input of no frames is
  accepted, reported and returned, but its handle names a placeholder freed at once, and an output of no frames is
  refused, so a caller whose chain meets an empty buffer submits nothing after it. A node fault fails the job: a
  bypassed node means the recording is not what the document asked for. A cancel completes a queued job at once and
  stops a running one at its next block boundary (or after its last block), both with the reason `cancelled`, and a
  cancel that returned true always completes `cancelled`. `drainOfflineJobs` takes a wait (at most 1000 ms) and answers
  the completions plus `inFlight`, the jobs still queued or running, which is how the TS side tells a live promise from
  one whose job is gone. Stage 2's fix pass made the TypeScript poll drain without waiting while its chain is busy
  (blocking only after eight quiet steps, so a chain that awaits through async layers reaches its next submission),
  held runs past the 64-job bound in a FIFO instead of refusing them busy, started a worker when the only parked one
  was claimed, and rejected a pending promise native no longer holds. Follow-up S made a port call that throws fail the
  run it belonged to rather than the queue: a submit throw rejects that run, held or not, a drain throw rejects every
  run the queue was holding, and the poll's `polling` flag is cleared on every exit; in a host with no UI tick one
  `host.pump()` still does not return until nothing is pending, which D3's minute-long renders will need to yield
  around. The live-plan case now runs long jobs through its whole counted window and records the outstanding count at
  the first and the last counted block, a cancel that arrives after a job's last block is held to `cancelled`, and a
  cap whose scaled frame count is not finite or is past `size_t`'s range fails its job rather than being cast. `src/kit`
  refuses a spec's `offset` / `length` that is not a whole frame count as that slot's error, in the spec's own words
  rather than the RPC's decode message, and reads an explicit null there as absent, as native's optional does. Follow-up
  S's review pass gave those cases the reach they claimed: the throwing submit is held behind runs staggered so most
  are still pending when it throws, with one more held behind it and one made straight from `run()`; the live-plan
  case's control thread drains the pool beside the running blocks, and the case records that a drain did run while
  blocks were still playing; the guard case reads MXCSR's rounding bits inside the guard, which `fegetround` does not
  see on glibc; and a drain throw leaving native's jobs uncounted by the queue until they are drained is said where the
  queue's bound is.
- **3, the nodes.** `rp.dsp.normalize`, `rp.dsp.dpcm` and dither's `seedFrom` are all in `rp.dsp`, in the existing
  `retroplug-dsp-nodes` target, so the pool's registry planned them unchanged; `rp.dsp.dpcm` declares `heap`, the
  package's first such type, which shows a `heap` type stays here while rp.dsp.resample's split is about r8brain at
  link time. Normalize's settings are `buffer`, `gain` and `channels`, its port is fixed `channels` wide, it plays
  frames from frame 0 and keeps its position across a re-activation, and a settings commit re-folds a changed gain
  against the peak the activation measured. Dither's effective seed is settled in `on_settings` as well as `activate`,
  because a plan replacement commits settings to the live instance without re-activating it (the existing
  `[dsp-nodes]` seed case caught that). The DPCM kernel writes one unpacked bit per byte and needs no scratch (two
  passes over the input); the node encodes at activate into storage it owns and declares `normalize` its `rebuild_key`,
  so a toggle rebuilds and re-encodes on the control thread rather than running a whole-buffer pass in a block-thread
  commit, and a re-activation whose buffer is the length it already encoded keeps those bits. "No buffer named" is a
  `settings-invalid` refusal for both new nodes, and `rp.dsp.dpcm` refuses a multi-channel buffer at activate. The
  review's fix pass moved the DPCM re-encode off the block thread, gave each node a commit case, and made normalize's
  peak the channels it plays. Its descriptor goldens: dither's re-recorded for `seedFrom`, and `normalize`'s and
  `dpcm`'s added.
- **4, the compiler.** `src/kit/` is three files: `pack.ts` (the integer half and the three assemblers' constants),
  `chain.ts` (validation, the refusals and the documents) and `compile.ts` (the two entry points). A slot's chain
  starts with a document-less load job whose file input carries the cap, so native hands back the windowed samples and
  the decoded rate. Missing spec fields take the native structs' defaults, which is a real departure from the old RPC
  (its decode had no defaults, so any effect field left out threw for the whole kit); a non-finite normalising gain, an
  out-of-range value and a filter at or above the source's Nyquist are refused after the load job, before any DSP pass
  (Nyquist is relative to the rate the load decoded). Samples past the
  bank's slot count are not compiled and get a reason. The two TypeScript assemblers were changed to native's byte
  rules: native uppercases only a-z over the UTF-8 bytes it receives, and JavaScript's Unicode `toUpperCase` can change
  a name's length, so no baseline bank moved but non-ASCII names now agree too. The two test-native files are
  `kit-baseline` (the 39 banks) and `kit-compile` (the refusals, and the edge banks past them); `SOURCE_HASHES` binds
  the synthesised sources to what the retired C++ suite generated, and both goldens are read at run time through
  `__KIT_GOLDEN__` / `__KIT_EDGES_GOLDEN__`. A refused risa slot is empty (its entry 0xFF) where its empty window
  alone would have placed `{0}`, and a refused LSDj slot keeps its name at length 0, which differs from the kit
  without that slot.
- **5, the switch-over.** `audioDriver.compileKit` / `compileDmc` return `Promise<KitCompileResult>` and run `src/kit`;
  their runner is `realBackend`'s one offline queue (a second `OfflineJobs` would drain the same native completion queue
  behind the first), and `createAudioDriver` turns the poll's native wait on. `runSession` and `CliTool.run` accept a
  promise, so `await` works in the CLI's baked-in sessions and in a consumer's. The per-slot reason reaches the CLI's
  read-back check, and a clipped slot with bytes prints a warning. The plugin's compile is checked on the UI test host,
  whose registration is exactly the plugin's: it settles the compile from `ui.pump` + `backend.pumpJobs` alone, proves
  a microtask-only stretch settles nothing, and asserts the harness RPCs are unbound. The compiler's callers moved with
  it: the compile code reached the CLI bundle and the render worker's session path, not the UI bundle. `test:cli-ts`'s
  new block drives the real binary's async exit path: the bank is on disk when `build-kit` exits 0, and a missing
  source exits 1 with its reason on stderr. This stage also exposed a latent bug: the CLI's `--filter` left `gain` out
  of the effect, so the old RPC had thrown `Field named 'gain' not found` for every such kit and the flag never
  worked; the TypeScript compiler fills the 0 dB default and the flag now compiles, which a fix pass pinned there along
  with the clipped-slot warning (a filter flag's bank differs from the same spec's without it, and a partly placed
  sample's reason is read back from stderr).
- **6, native retires.** `KitCompiler`, `KitCodec.hpp`, `LsdjKitCodec`, `RisaDmcCodec`, `KitUtil`, `SampleUtil`, the
  harness `compileKit` / `compileDmc` RPCs, their DTOs, the `LsdjEffect` tagged union and the C++ baseline suite are
  gone. What tests still name of the LSDj bank moved to `src/lsdj/KitBank.hpp` (the rate, `0x3fa0` and its frame
  count), whose comment documents it as the format the TypeScript compiler packs. `retroplug-core` compiles only
  `SampleCache.cpp` from the kit path and links neither DSP library nor enkiTS any more; `retroplug-audio-test` and
  `retroplug-graph-test` compile `src/lsdj/Effects.cpp` themselves, as the kit path's reference. The harness facet is
  20 methods and the union 90 (was 22 and 92). enkiTS leaves the build entirely (the `add_subdirectory`, the warning
  suppression and the third-party credit), since no shipped binary can contain it now; the vendored directory
  stays. CI's Catch2 step drops `RP_KIT_GOLDEN_REQUIRE_TARGET` with the suite it guarded, and the `test:native` step
  keeps it, because the TypeScript goldens still compare exact bytes there. The C++ seam test no longer holds the
  compiler's side of an empty window: `kit-compile`'s EMP slots and the edges golden do.
- **7, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build (and `src/dsp/`'s nodes, the
  compiler and the seam describe what they do now rather than what they replaced).

### Section 10's questions, answered

- **Can `createAudioBuffer`, the resource table and a fresh `GraphHost` be built and torn down per job off the control
  thread, with the registry shared read-only, and what does a job need from the engine?** Yes. A job's table, inputs,
  outputs, host, plan and node instances are locals of the worker's run function, destroyed on that thread with the
  table after the host; the pool's registry is filled once before any worker exists; and a job needs nothing from the
  engine: no script host (script, system and host nodes are refused), no packages beyond the three statics. TSan is
  clean with 25 jobs in parallel and beside a live audio thread, and a job changes nothing of the engine's plan.
  Tests may give an `OfflineJobs` they build directly extra packages, which is how the activate-failure and fault
  cases get their nodes.
- **What does the window do at the edges?** `start = min(offset, size)` and
  `frames = min(cap, min(length == 0 ? size : length, size - start))`, so an offset at or past the end is an empty
  window (not an error), a zero length means "to the end", and the cap is scaled to the decoded rate. The cap's
  arithmetic is part of the bytes: LSDj's is `ceil(float(32576) * (float(rate) / float(11468)))`, all in float with the
  ratio first, and risa's `ceil(double(32648) * double(rate) / palRate)`, in double with the product first; computing
  LSDj's in double moves the cap at 464 of the integer rates 1000..96000 (the mutation the test made). A cap whose
  scaled frame count is not finite or is past `size_t`'s range has no window: the job fails with the cap named rather
  than casting (only a nonsense cap reaches it, and the job could not carry that many frames anyway). What the
  compilers make of an empty window is settled in TypeScript rather than run: LSDj has no bytes (its assembler still
  names the slot, with length 0), risa is its one-byte `{0}`. A window that resamples to fewer than 32 frames is LSDj
  empty bytes too, and one that resamples to 0 is risa `{0}`.
- **Does `buildKitBank` (and the risa packer) produce native `assemble`'s bytes?** Yes for all 39 baseline cases and
  for every edge case, with the layout code untouched. They differed only on names outside printable ASCII, because the
  TypeScript side uppercased with JavaScript's Unicode rule; native's byte rule is right (it is what the baseline and
  every whole-kit compile so far were made by), so the TypeScript side now matches it. No splice changes, because a
  name read back from a ROM is printable ASCII by construction.
- **How long is a 15-slot kit as jobs against today's enkiTS compile, and is the per-job host setup the cost?**
  Release build, this box (32 threads, so 8 workers), 8 runs each, on stage 4's temporary (uncommitted) bench:

  | measurement | native `compileKit` (RPC) | TypeScript `compileKit` |
  |---|---|---|
  | 15 slots x 1 s at 44.1 kHz, three effects | best 1.5-1.6 ms, median 1.7-1.8 ms | best 22.0 ms, median 22.7-23.1 ms |
  | 15 slots x 8192 frames, no effects | best 0.4-0.5 ms, median 0.5 ms | best 4.1-4.2 ms, median 4.2 ms |
  | the 1 s kit, slot by slot (serial) | 10.5-11.1 ms | 49-51 ms |

  Per job (serial round-trip means) for the 1 s kit: load 0.29-0.34, effects 0.47-0.49, the normalise pass 0.24-0.26,
  resample 0.70-0.71, quantise 0.31-0.38 ms. **Per-job host setup is not the cost**: a trivial job is 0.011-0.017 ms
  (a document-less load of 64 frames) to 0.03 ms (a 64-frame player into a recorder), about 2 ms across a kit's 75
  jobs. The cost is moving buffers through TypeScript between passes: an echo job (buffer in, played, recorded,
  returned) takes 0.07 ms at 8192 frames, 0.18 at 44 100 and 1.1 at 176 400, about 0.15 ms per 2 x 176 KB, all on the
  one JS thread's msgpack and copies; a 1 s slot pushes about 1 MB through the RPC and a kit about 15 MB. The DSP
  itself is the native milliseconds. The worst case the switch-over has to survive is 15 slots of 4 s at 96 kHz, each
  capped at 272 700 frames with a filter, a normalise and a dither: native 4.9 ms median (4.8 best), TypeScript 83.2
  ms (77.9 best), where an early TypeScript-side scan for non-finite samples had made that 467 ms (it moved into
  native, below). About 80 ms of the UI thread is the cost of a worst-case kit compile if a future row ever runs one.
  If the buffer traffic ever matters, the lever is keeping intermediates native (a job returning a handle the next job
  reads, or a chained submission); the host setup needs no work.
- **Does the plugin's facet set allow a new engine facet, and how do completions reach a UI that only polls?** Yes: the
  three RPCs sit in the emulator facet, which the plugin, the SDL standalone, the CLI, the test hosts and the Node
  addon all bind, so no host changed. Completions reach the plugin UI through the existing frame tick, `useJobPump` ->
  `backend.pumpJobs()`, which drains offline completions too; with the editor closed the workers keep running and
  completions wait in native's bounded queue (counting against the 64). Nothing in the plugin ever waits natively,
  which is what the UI test case proves by settling a whole compile from frame pumps.

### The guards, the non-finite flag and the floating-point environment

Three mechanisms, all added by stage 4's reviews, make section 2's promise exact for values that never reach a bank
whole:

- **`rp.host.guards/1`** (`rp_node.h`) is a host service a node reports into when one of its guards replaces what its
  kernel would have computed: `fired(ctx, reason)`, called on the block thread, never blocking or allocating, the
  reason a string literal the reporting instance keeps alive. `rp.dsp.biquad` reports its reset-and-silence and
  `rp.dsp.dither` its channel restart, exactly where each diverges from the kernel run directly. A host that offers no
  service answers null and the node skips the call, so the live engine's behaviour is unchanged (and
  `Engine::graphService` is pinned to answer null for it). Each offline job's host offers the service, and its
  completion carries `guardsFired` and the first `guardReason`; a job a guard stepped into still completes `ok`, and
  what the count means is the caller's to decide.
- **`nonFinite`** is on every buffer an offline completion returns, read by native as it copies the buffer out. A
  non-finite value no guard touches (a normalising gain over a subnormal peak, a plain gain over samples near
  `FLT_MAX`) lands in the recording, and this flag is how a caller sees it without scanning the samples (a TypeScript
  scan put the 15-slot worst case at 467 ms, against 83 ms with native's flag).
- **The default floating-point environment** is held for a job's whole run, decode to copy-out, with the caller's
  environment saved before the default is pinned and put back after (rounding mode and the flush bits included): round
  to nearest, no trapping, and the flush bits cleared explicitly (x86 MXCSR FTZ and DAZ, AArch64 FPCR FZ and FZ16),
  because a worker inherits its submitter's mode under glibc (a Windows thread starts in the default mode) and audio
  hosts commonly flush. Without it a job submitted from an audio thread with flush-to-zero on would compile a different
  bank from the kernels', and without putting the caller's back, a job run inline on a caller thread would leave that
  thread in the default mode, its flush-to-zero lost.

Together they make "every kit that compiles is byte-identical to the kernels" exact rather than a hope: every pass's
input is finite (a checked window or a checked recording); a gain or a normalise has neither guard nor clamp, so a
non-finite value it makes is in the recording or reaches a biquad; the biquad's clamp is the one place a pass could
hide one, and a non-finite input or output leaves its state non-finite, which its guard reports; the resample has
neither, so what it makes is recorded; and the quantise and DMC encode only ever get checked inputs, the dither
reporting any restart. So a node departs from its kernel only where a guard reported it, and every other non-finite
value is flagged. The compiler refuses a slot on either (guard count above 0, or a flagged recording), so a compiled
bank is the kernels' bytes. D3 inherits the reports rather than a policy: a render sees the count and the flags and
decides, and nothing fails on them.

### What building it changed in sections 3 to 6

- **Section 3**: a completion reports guards and a per-buffer non-finite flag, a node fault fails a job, and teardown
  abandons jobs in flight rather than completing them with an error; the pool's 64-job bound, its 8-worker ceiling,
  its 1024-frame blocks, its handles and its `inFlight` count are the seam's contract; the FP environment is pinned and
  the caller's put back; a cap that scales to no frame count fails the job; and a throwing port call fails the runs it
  belongs to, not the queue.
- **Section 4**: normalize's peak is over the channels it plays; a missing buffer is a refusal for both new nodes and
  a multi-channel buffer a refusal for `rp.dsp.dpcm`; the DPCM's encode is a rebuild key; and `rp.host.guards/1` is the
  service both guards report into.
- **Section 5**: the entry points take a runner and return `{bytes, errors}`; a pass a guard stepped into or a
  non-finite recording refuses the slot; missing spec fields take native's defaults, an explicit null for `offset` or
  `length` among them (native's optional reads it as absent, so it is 0); a spec's `offset` and `length` must otherwise
  be whole frame counts, refused as that slot's reason before a job is submitted; and every empty or short slot, not
  only a failed one, carries a reason.
- **Section 6**: the C++ suite's second watch on the same file ends with the compiler it watched; `kit-edges.json`
  records the edge banks; the teardown case asserts abandonment; and the suite gained the guard, non-finite and
  FP-environment cases, the FP one reading MXCSR's rounding bits inside the guard since `fegetround` sees only the x87
  control word where the kernels compute in SSE.

### Found while building

- **The kit baseline is a coarse oracle, and the compiler needed a wider net.** All 39 matched on the first run, and
  the runner bites (forcing the quantise to `none` fails a case), but the baseline only sees 4-bit codes.
  `kit-compile`'s edge banks over 31 further slots (dithers on chirps and noise, a five-effect chain with two normalise
  passes, cut windows), plus the `[dsp-buffer-nodes]` and `[dpcm]` references, are what hold the arithmetic.
- **A plan replacement does not re-activate a node**, so anything computed only in `activate` silently ignores a
  settings commit. The dither's seed is now settled in both hooks; a user-built graph can hit the same trap.
- **A guard report is the clean seam**, not a heuristic: a node need not know whether it runs offline, and a caller
  gets an exact count instead of a scan. The alternative attempted first (scanning every recording in TypeScript) cost
  most of a worst-case compile.
- **`retroplug-graph-test` can hold a node to `rp::lsdj::processEffect`** by compiling `src/lsdj/Effects.cpp` into it
  (it needs the kernels only), which is how "bit for bit with the kit path" is tested rather than "bit for bit with a
  kernel the kit path also calls".
- **The live engine's first periodic state snapshot allocated** (about block 46 of a booting Game Boy, two
  allocations, with or without offline jobs). It is the 0.5 s state publish
  [07-remaining-work.md](../spec/07-remaining-work.md) listed among the allocations left on the block thread, seen here
  on a Game Boy's `readStateSnapshot` rather than Mesen's serializer: the read scratch it sized on first use and the
  registry's publish scratch it grew. The publish now reads the core's snapshot slot straight into the registry's own
  slot, which `SnapshotRegistry::claim` sizes to match on the control thread, so the block thread copies once into
  memory no claim touches and 07 no longer lists it. (Sizing the two scratches at claim instead, tried first, let a
  larger core's claim inside a setGraph free the shared publish scratch under a running plan's publish; a
  `[sanitizer]` case driving the registry from two threads holds that.) A Game Boy's publish of the same model
  allocates nothing; a model switch's restart block also grows its capture scratch by the state delta. C4's
  whole-block cases never reached the first publish, so the hole was theirs as much as this seam's: they now count a
  Game Boy through the Engine across two publishes and a NES to its registry publish, and this case counts from the
  first block after warm-up rather than past block 60.
- **`SampleCache`'s native behaviour differed from the seam's.** The old RPC kept one cache for the engine's life,
  keyed by path and never invalidated, so a path rewritten in one session compiled the old audio; a job decodes fresh,
  so a compile sees the file as it is now.
- **The plugin test had to settle through the plugin's own path.** A case that made an audio driver settled through
  the native wait, which the plugin never uses; it now settles from the frame tick and proves the wait is off.
- **enkiTS was building nothing.** `add_subdirectory` still compiled the static library into `all` though nothing
  linked it; it is out of the build now, and the vendored directory remains for a later cleanup to judge.
- **The compiler reached the CLI and render bundles, not the UI's.** `audioDriver` imports `src/kit` and
  `realBackend.offlineRunner`, so `cli-bundle.js` and the render worker's session path grew; `ui/main.tsx` and the
  plugin control plane import neither.
- **Not yet compiled with MSVC.** This box builds Linux, and the prototype's commits are local, so the Windows build
  has not seen this tree; stage 6 also removed transitive includes (`lsdj/Effects.hpp` -> `BackendTypes.hpp` among
  them), so "confirm the removed transitive includes build under MSVC" is a residual in
  [07-remaining-work.md](../spec/07-remaining-work.md). The floating-point guard's inside mode is the other half only
  MSVC sees: if the Windows job goes red on that assertion, the class wants MXCSR's rounding field set explicitly
  rather than trusting `fesetenv(FE_DFL_ENV)` to carry it.

### The suite, as built

- `retroplug-engine-graph-test` gained `[offline]` (`test/engine-graph/OfflineJobs.test.cpp`): a job returns what the
  same nodes give run directly, a kit-shaped job naming one buffer twice, two jobs on two workers, the pool's
  concurrency and bound, a cancel queued and running (and one that arrives after a job's last block), a teardown that
  abandons, every refusal's reason, the RPC's sample bytes, both windows and the cap's arithmetic against copies of
  each formula (and a cap that scales to no frame count refused rather than cast), the file input against
  `SampleCache`'s decode, the live plan compared bit for bit with long jobs outstanding at both ends of its counted
  window (which starts at the first block after warm-up and so spans every state publish it carries) and its control
  thread draining the pool beside it, the guard count and the non-finite flag, the FP environment across a flushing
  submitter and the caller's mode put back, and a hidden `[offline-bench]`. It went from 268 cases to 284, and the two
  threaded snapshot cases the claim fix added (section 11) make it 286 (277 passed and 9 skipped without fixtures; with
  the fixtures present the whole suite passes).
- `retroplug-graph-test` gained `[dsp-buffer-nodes]` (`test/graph/DspBufferNodes.test.cpp`, 12 cases: normalize whole
  and split, its peak, its committed gain, `seedFrom`'s channel 0 and the other channels, a committed seed, the DPCM's
  bits, its `normalize` rebuild, a block counted from the plan's first block allocating nothing, the refusals, and the
  Pico plan) and `[dsp][dpcm]` (the kernel against a verbatim copy of the retired encoder), the binary going from 259
  cases to 272: these 13 and stage 1's two `[dsp-nodes]` clamp cases. A fix pass added a re-activation case, so
  `[dsp-buffer-nodes]` is 13 cases and the binary 273.
- `retroplug-audio-test` lost `[kit-baseline]` with the compiler (109 cases and 1 235 263 assertions to 108 and
  1 234 835); `[dither]` and the rest are unchanged. `retroplug-audio-test "[kit-baseline]"` is now a Catch2 failure
  ("No tests ran"), so a stale script fails loudly.
- `test-native` went from 160 files to 163: `kit-baseline` (the 39 banks, 3 cases), `kit-compile` (against
  `kit-edges.json` and the refusals, 9 cases) and `offline-run` (the promise side over the real host, 5 cases).
  `test:ui` gained `kit-compile` (40 files), `pnpm test` gained `offline` and `kit` (171 files), and `test:n8` and the
  launchpad suite are unchanged.
- The goldens: `packages/native/test/audio/golden/kit-baseline.json` untouched and still the one record, beside it the
  new `kit-edges.json` (12 banks, `{hash, size}`), both read through defines `run-native-tests.mjs` sets. The
  TypeScript runner carries the baseline's rules: size and slot everywhere, the hash only on `linux-x64-gnu13`, a
  second compile agreeing with itself elsewhere, `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turning that into a failure, and
  `RP_KIT_GOLDEN_UPDATE=1` refused off the target, for a case that disagrees with itself and for a run whose source
  check failed.
- Both sanitizers are clean over the new tags (TSan with 25 jobs beside a live audio thread; ASan with leak detection
  on), the consumer suites pass from scratch copies (BlipToaster 57 files, NESVJ 19), and the only count that differs
  between the two sanitizer builds is the existing device-thread progress case, whose progress publishes depend on
  scheduling.
