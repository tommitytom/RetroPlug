# Phase D2: kit compiles as offline graphs

**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 10

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
queue, the C5 capability-job pattern. TypeScript gets `backend.offlineRun(doc, inputs, outputs, frames)`, a promise
over submit and poll.

**Where it runs.** A small native worker pool (one thread per running job, sized to the machine as `RenderJobRegistry`
is). Each job gets a fresh offline `GraphHost`: its own resource table, no audio thread, the quiescent block driver,
running `GraphHost::run` in fixed blocks for the requested frames (no second block driver). The pool is apart from the
live engine, so a job never touches the plan that is playing; a job's host is built for it and torn down with it, so
every resampler dies inside that host's life (D1's teardown rule for r8brain's statics).

**Inputs and outputs.** An input becomes an `audio-buffer` resource through `createAudioBuffer`, named in the document
as the `res:N` handle the submission assigns. A file input is decoded natively through `SampleCache` (the same
miniaudio decode as today) and windowed natively by an offset, a length and a frame cap (as `compileSample` does); its
creation reports whether any sample is non-finite. A buffer a run writes through `rp.core.buffer-recorder` is returned
in the completion, its samples, rate and channel count.

**Every host.** The RPC sits in a facet the plugin, the SDL standalone, the CLI and the Node addon all bind. A job runs
off every audio and UI thread and allocates nothing on any block thread.

**Failures.** A planner refusal, an activate error (an r8brain allocation failing) or a cancel completes the job with
the reason. Destroying the engine cancels its jobs and waits for their threads.

## 4. The nodes D2 adds or changes

- **Widened descriptors**: `rp.dsp.gain` linear -100..100; `rp.dsp.biquad` frequency 1..96 000 Hz, Q 0.001..1000, gain
  -96..96 dB. The planner cannot see the plan's rate, so the kit compiler refuses a frequency at or above its source
  rate's Nyquist itself. The descriptor goldens re-record once; live patches get the wider ranges.
- **`rp.dsp.normalize`**: a buffer player that normalises. It names an `audio-buffer` in its settings (as
  `rp.dsp.resample` does); at activate, on the control thread, it computes the whole buffer's `rp::dsp::peak` and
  `rp::dsp::normalizedGain(gain, peak)`, then streams the buffer times that gain, sample for sample the multiply
  `GainEffect` makes today.
- **`rp.dsp.dither` gains `seedFrom`**: an optional buffer reference; when set, activate seeds every channel from
  `rp::dsp::inputSeed` over that whole buffer, so channel 0 matches the kit path's seed. The kit graph points it at the
  resampled buffer the dither plays, at its full pre-trim length.
- **`rp.dsp.dpcm`**: risa's DMC encoder as a buffer-reading node. `convertF32ToDpcm`'s whole-buffer DC blocker, peak
  normalise and 1-bit delta move into a kernel in `retroplug-dsp-kernels`; the node runs the kernel over its bound buffer
  at activate into storage it owns, then streams the delta bits as audio levels of plus and minus one.
- **Unchanged**: `rp.dsp.resample`, `rp.core.buffer-player`, `rp.core.buffer-recorder`.

## 5. The TypeScript kit compiler

`compileKit(spec)` and `compileDmc(spec)` become TypeScript (`packages/retroplug/src/kit/`) with the signatures the CLI
sessions call today, returning the same bank bytes. Each slot is a chain of offline jobs; slots run in parallel up to
the pool's size.

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
slot's error, so the CLI's read-back check gains a reason. A refused spec is that slot's error.

**Retired**: `KitCompiler`, the compile use of `SampleCache` beyond the job seam's decode, `LsdjKitCodec`,
`RisaDmcCodec`'s encode, and the harness-only `compileKit` / `compileDmc` RPCs. Kernels and helpers the nodes or tests
use stay.

## 6. Tests

- **The baseline is the oracle.** `kit-baseline.json` (D1's 39 entries) stays the one record. A new test-native suite
  runs the TypeScript compiler through real offline jobs over the same matrix with the same synthesised sources and
  compares each bank's FNV hash and slot length to the golden. Until native retires the C++ suite still holds native
  `KitCompiler` to the same file, so both compilers are held to one record. Every entry must match through the graph
  compile before any caller moves; a mismatch means the graph is wrong, and nothing is re-recorded.
- **The seam**: a job runs a document to completion and returns its buffers; parallel jobs do not interfere (TSan); a
  cancel stops a job; an engine torn down with jobs in flight completes them with an error; a job allocates nothing on
  any audio thread; file inputs equal today's `SampleCache` decode; a job runs while an audio thread plays the live plan
  and changes nothing in it.
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
side effects or re-scoping their byte-identity claim.

## 9. Left beyond D2

- A UI for compiling kits in the plugin: D2 makes it possible and adds no menu row.
- User-authored sample graphs on the same seam.

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
