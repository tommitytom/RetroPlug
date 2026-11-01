# Phase D1: the kit baseline and the DSP library

**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9

Phase D gives the graph DSP of its own ([modular-graph-design.md](modular-graph-design.md) section 8): library nodes
(gain, biquad, dither, resample), kit compiles as offline graphs held byte-identical to today's `KitCompiler`, and
renders on the offline executor. It is split in three. D1 (this design) makes kit output reproducible, records it as a
baseline, moves the kit path's DSP into a core-free library of block-streaming kernels, and puts that library in the
graph as `rp.dsp` nodes. D2 builds the offline driver (audio buffers in and out of a document, a run to completion,
normalise and the two-rate kit chain) and compiles kits as graphs, held to D1's baseline. D3 moves the CLI and
background renders onto that driver.

## 1. Before D1

- **The DSP is the kit path's, whole-buffer, in `retroplug-core`.** `lsdj/Effects` holds three effects in one tagged
  union (`GainEffect`, `FilterEffect`, `DitherEffect`) that each process a whole `std::vector<float>` in place:
  - gain: an optional peak normalise over the whole buffer, then a linear multiply;
  - filter: eight RBJ biquad types in `float`, a transposed direct-form loop with per-call state and its output clamped
    to [-1, 1];
  - dither: five algorithms quantising to 4 bits, out as floats in [0, 15] for the nibble packer.

  `KitUtil::convertSamplerate` builds an r8brain `CDSPResampler24` per call and feeds it fixed 1024-frame chunks, the
  tail zero-padded, the output cut to `floor(n * out / in)`. The risa DMC codec shares the effects and the resampler.
  All of it compiles into `retroplug-core`, which links every emulator core, so no graph package can use it.
- **Kit output is not reproducible.** Both TPDF dithers (`HighPassTPDF`, the default, and `ShapedTPDF`) seed a
  `std::mt19937` from `std::random_device` on every call, so a kit compiled twice gives different bytes.
- **No test pins kit bytes.** The kit tests check structure and audibility (a 16 KB bank, readable slots, a decoded
  peak, a short source not overflowing the resampler, rotate round-tripping); none compares bytes.
- **Two allocations per call:** Sierra Lite and JJN dither build a padded copy of the buffer, and the resampler is
  built per call.
- **The graph has no DSP.** Its nodes sum, split, delay and play or record buffers; gain exists only as a system
  node's param, applied by the core. No biquad, dither or resample node exists, and a node's ports all carry the same
  frame count at the plan's one rate.
- **Kits compile only in the CLI.** `compileKit` and `compileDmc` are harness-facet RPCs; the plugin cannot build one.

## 2. Decisions taken

The user's, on 2026-11-01:

- **D is split in three** (D1, D2, D3), each with its own design, prototype, plan and execution.
- **Dither is seeded from its input**, always: a kit compiles to the same bytes every time. Not a fixed seed only under
  the test switch, and not a user-facing seed setting.
- **Shared kernels**: the DSP moves into a core-free target as block-streaming kernels, today's whole-buffer kit
  functions become loops over them, and the nodes wrap the same kernels. Not a second implementation beside the old.
- **Normalise: a kernel now, a graph form in D2.** D1 ships a whole-buffer peak kernel the kit path uses as a pre-pass;
  `rp.dsp.gain` is a plain streaming gain. Not an offline-only node now.
- **The dither node outputs audio**: the kernel yields integer codes (the kit packer's), and the node maps them back to
  audio at the quantised levels, so it is also a bit-crusher in a live patch. Not codes as the node's output.

## 3. The baseline

D1's first two commits come before any DSP moves.

**The seed.** The kit path seeds the two TPDF algorithms from a hash of the dither's input samples and its algorithm
(the other three algorithms are deterministic already). A kit compiles to the same bytes every time; the noise is still
TPDF noise. This is D1's one deliberate kit behaviour change.

**The baseline suite.** A Catch2 suite in `retroplug-audio-test` (which links `retroplug-core`) runs today's
`KitCompiler` over a fixed matrix of cases synthesised in the test, so it needs no fixtures and runs in CI: sines, a
chirp and seeded noise at 22.05, 44.1 and 48 kHz, and a 600-frame source shorter than one resampler chunk; for LSDj,
no effect, gain, normalise, each of the eight filters, each of the five dithers, rotate on and off; for risa, the DMC
codec at several rate indices. Each case's bank is hashed into a golden JSON beside the test, re-recorded only with
`RP_KIT_GOLDEN_UPDATE=1`. Exact bytes are compared on `linux-x64-gnu13` (as parity does); elsewhere a case must agree
with itself across two runs. From then on a change that moves a kit byte fails this suite.

## 4. The DSP library

**The target.** `retroplug-dsp` (`packages/native/src/dsp/`) is a static library linking the C++ standard library and,
for the resampler kernel only, r8brain. `retroplug-core` links it for the kit path, the node package links it, and so
does `retroplug-graph-test`, which tests the nodes with no core, txiki or backend.

**The kernels.** Each is a plain state struct, an `init` on the control thread (coefficients; the resampler's
allocation), and a `process(in, out, frames)` that is `noexcept` and allocates nothing:

- **gain**: a linear multiply; beside it a whole-buffer `peak` measurement, which the kit path normalises with;
- **biquad**: the RBJ setup for all eight types in `float` as today, the same loop, the same clamp (part of the bytes);
- **dither / quantise**: the five algorithms plus `none` (today's plain rounding when no dither is set), a bit depth
  and a seed, out as integer codes 0..2^bits-1 with a helper mapping codes to audio levels; Sierra Lite's and JJN's
  forward error terms live in a small fixed array in the state, the same arithmetic without the padded copy;
- **resample**: one r8brain `CDSPResampler24`, built in `init`, fed exactly as `convertSamplerate` feeds it (fixed
  1024-frame chunks, the tail zero-padded), so its output stream is the same however the input arrives; the offline
  length rule (`floor(n * out / in)`) is a separate whole-buffer helper, for the kit path now and D2's driver later.

**Today's code on the kernels.** `lsdj/Effects`, `KitUtil::convertSamplerate`, the no-dither quantise and the risa DMC
path's effects and resample become loops over the kernels; `Effects.cpp`'s math moves into them, and the `rfl` tagged
union stays where the kit specs need it. The baseline must not move by a single hash.

## 5. The `rp.dsp` nodes

A new `rp.dsp` package of C++ nodes in the `node.hpp` style, modelled on `rp.core.delay` (state in host-supplied
memory, settings copied to plain data, everything `noexcept`), each wrapping a kernel, registered by the engine and by
the graph test rig:

- **`rp.dsp.gain`**: audio in, audio out, any channel count; a linear `gain` param. A held value is an exact multiply
  (the kit path's); a change ramps across one block so a live move does not click.
- **`rp.dsp.biquad`**: state per channel; the type a setting; frequency, Q and gain (dB) params, so a patch can sweep
  them, the coefficients recomputed at block start when one changes; the clamp kept.
- **`rp.dsp.dither`**: settings for the algorithm (the five, or `none`), the bit depth (1 to 16) and a `seed`; out as
  audio at the quantised levels. D2's kit compile fills `seed` from the input hash; a live patch gets a fixed default.
- **`rp.dsp.resample`**: every port in a plan carries the same frame count at the plan's one rate, so a node cannot take
  1024 frames in and give 256 out. The resampler is a rate-aware sibling of `rp.core.buffer-player`: it reads an
  `audio-buffer` resource at that buffer's own rate and outputs at the plan rate, pulling input as it needs it. It
  declares `heap` (r8brain allocates), so the planner refuses it on the Pico. Running effects at the input rate before
  resampling, as the kit chain does, takes two passes at two rates: D2's driver.

Gain, biquad and dither declare neither `heap` nor `threads`, so they stay planable on the Pico (Phase E).

## 6. Tests

- **The kit baseline** (section 3): every hash unchanged after the kernel move.
- **Kernels**, in `retroplug-graph-test`: one block equals the same input split into 1-, 7-, 64- and 1024-frame blocks,
  bit for bit; a warmed `process` allocates and frees nothing (the suite's CountingAllocator); the same seed gives the
  same codes and different seeds different noise; Sierra Lite and JJN match their old padded arithmetic; resample's
  output is independent of the input's chunking, a source shorter than a chunk included.
- **Nodes**, through real plans (the graph rig): each node's output equals its kernel run directly; ragged block splits
  give identical output; a param change ramps over one block and a held one is exact; a warmed plan block allocates
  nothing; a descriptor golden per type; resample from a 22.05 kHz buffer into a 48 kHz plan matches the kernel's
  stream, and the Pico target refuses it.
- **Unchanged**: strict parity bit for bit (nothing in the live engine path changes), `pnpm test:native`'s `lsdj-rom` and
  `risa-kit` cases plus one new case (a CLI kit compiled twice gives identical bytes), both consumer suites, and the new
  tags under both sanitizers.

## 7. Landing

Six commits, each green:

1. **Dither seeded from its input** (the one deliberate kit behaviour change; nothing is re-recorded, as no baseline
   exists yet).
2. **The kit baseline suite** and its golden hashes, recorded at commit 1's tree.
3. **`retroplug-dsp`**: the kernels, and `lsdj/Effects`, `KitUtil`'s resample and quantise and the risa DMC path moved
   onto them; the baseline does not move by one hash.
4. **`rp.dsp.gain`, `rp.dsp.biquad`, `rp.dsp.dither`.**
5. **`rp.dsp.resample`.**
6. **Docs**: this design's as-built record, the specs, AGENTS.md.

As C5: a prototype on a local `proto/` branch, its stages 3 (the kernel move, where byte identity is earned) and 5
(resample's chunk-exact streaming) built by Opus and reviewed by DeepSeek, the rest built and reviewed by DeepSeek, each
fixed after its review; the plan generated from the prototype and replayed, then an Opus plan review; DeepSeek
implementers and follow-ups, Opus code reviews of tasks 3 and 5, a DeepSeek acceptance run, an Opus final review.

## 8. Left for D2 and D3

- **D2**: audio buffers in and out of a document (an RPC creating an `audio-buffer` resource from samples or a decoded
  file, and one reading a buffer back), a run-to-completion driver over `GraphHost`, normalise in a graph, the two-rate
  kit chain, and kit compiles as graphs behind the same `compileKit` / `compileDmc` RPCs, byte-identical to D1's
  baseline; the throughput question (one plan per slot on N threads, as today's enkiTS fan-out, or the fan-out in the
  driver); and whether the plugin gains a kit compile.
- **D3**: the CLI `render` and the UI's background renders on the offline driver, keeping their block-size sequence and
  per-block side effects or re-scoping their byte-identity claim.

## 9. Prototype questions

- What exactly does the dither seed hash (the windowed input at the source rate, or the resampled input the dither
  sees), so D2's graph can compute the same seed before the run?
- Do Sierra Lite's and JJN's padded loops have an exact fixed-size streaming form (do their forward error terms reach
  past a small window), and does the baseline hold through the rewrite?
- Does r8brain allocate anywhere in `process`, or only at construction, so the resample kernel's `process` is truly
  allocation-free?
- Can `retroplug-graph-test` link `retroplug-dsp` (with r8brain) without pulling anything the graph library must not
  link, and do the Pico-target refusals hold for `rp.dsp.resample` alone?
- Does anything outside the kit path call `lsdj/Effects` (the CLI's effect parsing, TS's effect descriptor), and does
  the move keep the reflect-cpp shape the RPC decodes?
