# Phase D1: the kit baseline and the DSP library

**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** built (D1, six commits); section 10 records the
build and sections 3 to 5 are corrected in place.

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
(the other three algorithms are deterministic already). The hash is FNV-1a-64 over the buffer's float bytes, folded to
a 32-bit word and salted with a stable per-algorithm constant, and the draw is `(rng() >> 8) * 2^-24` rather than
`std::uniform_real_distribution`, whose sequence the standard does not fix. A kit compiles to the same bytes every
time; the noise is still TPDF noise. This is D1's one deliberate kit behaviour change.

**The baseline suite.** A Catch2 suite in `retroplug-audio-test` (which links `retroplug-core`) runs today's
`KitCompiler` over a fixed matrix of cases synthesised in the test, so it needs no fixtures and runs in CI: sines, a
chirp, seeded noise and silence at 22.05, 44.1 and 48 kHz, and a 600-frame source shorter than one resampler chunk;
for LSDj, no effect, gain, normalise (a silent source too), each of the eight filters, each of the five dithers,
rotate on and off, one filter-gain-dither chain, a short source and one two-slot bank; for risa, the DMC codec at five
rate indices, normalise off, loop on, a silent normalise at two rates, a short source and one two-slot bank. The
two-slot banks (a second sample named SNR with its own effects, and on the LSDj one a lowercase over-long bank name)
put the assembler's offset accumulation past slot 0, its 15-slot loop and `writeString`'s uppercase and truncation
inside the baseline. Each case's bank is recorded in a golden
JSON beside the test as its hash, size and slot length, re-recorded only with `RP_KIT_GOLDEN_UPDATE=1`. Exact bytes
are compared on `linux-x64-gnu13` (as parity does), off it a case must agree with itself across two runs while its
size and slot length are still held, and `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turns the fallback into a failure on CI's
linux-x64 job. From then on a change that moves a kit byte fails this suite.

## 4. The DSP library

**The targets.** `retroplug-dsp-kernels` (`packages/native/src/dsp/`) is a static library linking the C++ standard
library and nothing else: gain, peak and the normalising gain, the biquads, dither and quantise. `retroplug-dsp-resample`
is `Resample.cpp` alone, linking r8brain and (for r8brain's filter-cache mutex) `Threads::Threads`, so a build with no
resampler omits that one target and sees neither the heap nor pthreads nor RTTI. `retroplug-core` links both for the kit
path, the node packages link them, and so does `retroplug-graph-test`, which tests the nodes with no core, txiki or
backend. The design's `retroplug-dsp` name is taken: `dpf_add_plugin(retroplug ...)` creates the plugin's own DSP
library of that name.

**The kernels.** Each is a plain state struct and an `init` on the control thread (coefficients; the resampler's
allocation). Gain, biquad and dither add a `process(in, out, frames)` that is `noexcept` and allocates nothing; the
resampler's counts differ, so its streaming calls are the push/pull set below.

- **gain**: a linear multiply; beside it a whole-buffer `peak` measurement and the normalising gain the kit path folds
  into it (a reciprocal multiply, not a division: the bytes depend on it);
- **biquad**: the RBJ setup for all eight types in `float` as today, the same loop, the same clamp (part of the bytes);
  `init` builds the coefficients and clears the state, `setup` moves the coefficients and keeps the state, so a sweep
  does not click;
- **dither / quantise**: the five algorithms plus `none` (today's plain rounding when no dither is set), a bit depth
  (1 to 16) and a seed, out as `uint16_t` codes 0..2^bits-1 with a helper mapping codes to audio levels; `inputSeed()`
  hashes a buffer the way the kit path seeds it and `init` applies the per-algorithm salt. Sierra Lite's and JJN's
  forward error terms live in the state as the last two / four errors, the same arithmetic without the padded copy;
- **resample**: one r8brain `CDSPResampler24`, built in `init`, fed exactly as `convertSamplerate` fed it (fixed
  1024-frame chunks, the tail zero-padded), so its output stream is the same however the input arrives. Its API is
  push/pull rather than `process(in, out, frames)`, because the counts differ: `push` fills the chunk and resamples it
  into a fixed ring, `pushSilence` continues the stream past the source, `pull` hands out output, and the offline
  length rule (`floor(n * out / in)`) is a separate whole-buffer helper, for the kit path now and D2's driver later.

**Today's code on the kernels.** `lsdj/Effects` maps its fields onto the kernels and `processEffect(GainEffect)` moves
out of line; a new `lsdj::quantise(buffer, const DitherEffect*)` gives the 4-bit codes, seeded from `inputSeed`; the
no-dither quantise and the risa DMC path's effects and resample become loops over the kernels. `Effects.cpp`'s math
moves into them and the `rfl` tagged union stays where the kit specs need it. `KitUtil::convertSamplerate` is deleted,
`SampleUtil::convertScaledF32ToNibbles` becomes `convertCodesToNibbles` over the codes, and `fnv1a64` gains an optional
running hash so a big-endian host seeds without a copy. The baseline must not move by a single hash.

## 5. The `rp.dsp` nodes

A new `rp.dsp` package of C++ nodes in the `node.hpp` style, modelled on `rp.core.delay` (state in host-supplied
memory, settings copied to plain data, everything `noexcept`), each wrapping a kernel, registered by the engine and by
the graph test rig (the resampler is a package of its own beside it):

- **`rp.dsp.gain`**: audio in, audio out, any channel count; one linear `gain` param (0..4, default 1). A held value
  is an exact multiply (the kit path's); a change ramps across one block from the previous block's end to the block's
  last value, and the first block after activation snaps, so a document's value is exact from sample 0.
- **`rp.dsp.biquad`**: state per channel; the type a setting; frequency, Q and gain (dB) params (20..20000 Hz, Q
  0.1..10, gain -24..24 dB, the last read by Peak and the shelves only), so a patch can sweep them, the coefficients
  recomputed at block start when one changes; `init` clears the state on activation and on a type change, `setup` keeps
  it on a sweep; the clamp kept, and no other clamp added.
- **`rp.dsp.dither`**: settings for the algorithm (the five, or `none`), the bit depth (1 to 16) and a `seed` (default
  `none`, 4 bits and `0x9e3779b9`); out as audio at the quantised levels. A settings commit restarts the noise. D2's kit
  compile fills `seed` from `inputSeed` over the whole pre-trim buffer; a live patch gets a fixed default.
- **`rp.dsp.resample`**: every port in a plan carries the same frame count at the plan's one rate, so a node cannot take
  1024 frames in and give 256 out. The resampler is a rate-aware sibling of `rp.core.buffer-player`: it takes an
  `audio-buffer` resource as a settings ref (`"buffer":"res:N"`) at that buffer's own rate and outputs at the plan
  rate, pulling input as it needs it. The buffer is a settings ref, not a resource port, because the resamplers are
  built at activation and building one allocates: a port's binding is first seen on the block thread, a settings ref is
  pinned at create on the control thread. The cost is ordering: a `rp.core.buffer-recorder` and a resampler on one
  buffer in one plan are unsupported (the node reads whichever prefix the block schedule wrote, silently), and D2's
  driver fills a buffer before the plan that plays it. It declares `heap` (r8brain allocates), so the planner refuses it
  on the Pico, and it is a package of its own, `rp.dsp.resample`, with a target of its own, so a Pico build also leaves
  it and r8brain out of the binary. `loop` defaults false (silence past the buffer), equal rates still go through
  r8brain and are bit-exact, and a mid-run rate change rebuilds the resamplers and carries on from the buffer position
  the output had reached, repeating up to one input chunk. Running effects at the input rate before resampling, as the
  kit chain does, takes two passes at two rates: D2's driver.

Gain, biquad and dither declare neither `heap` nor `threads`, so they stay planable on the Pico (Phase E). Their package
is `retroplug-dsp-nodes`, a target of its own rather than part of `retroplug-graph`, because the package brings the DSP
library and `retroplug-graph` is what every consumer links; the resampler's package is `retroplug-dsp-resample-nodes`,
beside it, so only a build that wants the resampler reaches r8brain.

## 6. Tests

- **The kit baseline** (section 3): every hash unchanged after the kernel move.
- **Kernels**, in `retroplug-graph-test`: one block equals the same input split into 1-, 7-, 64- and 1024-frame blocks,
  bit for bit; a `process` allocates and frees nothing once warmed, and the resampler from its very first chunk (the
  suite's CountingAllocator); the same seed gives the same codes and different seeds different noise; Sierra Lite and
  JJN match their old padded arithmetic; resample's output is independent of the input's chunking, a source shorter
  than a chunk included.
- **Nodes**, through real plans (the graph rig): each node's output equals its kernel run directly; ragged block splits
  give identical output; a param change ramps over one block and a held one is exact; a plan's blocks allocate and free
  nothing counted from its first; a descriptor golden per type; resample from a 22.05 kHz buffer into a 48 kHz plan
  matches the kernel's stream, and the Pico target refuses it.
- **Unchanged**: strict parity bit for bit (nothing in the live engine path changes), `pnpm test:native`'s `lsdj-rom` and
  `risa-kit` cases plus one new case (a kit compiled twice gives identical bytes), both consumer suites, and the new
  tags under both sanitizers. The tags as built are `[dsp]`, `[dsp-nodes]` and `[dsp-resample]` in
  `retroplug-graph-test`, `[kit-baseline]` and `[dither]` in `retroplug-audio-test` (both carry `[kit]` too, so one
  run covers the kit path's whole byte surface), and `kit-dither` in
  `test-native`.

## 7. Landing

Six commits, each green:

1. **Dither seeded from its input** (the one deliberate kit behaviour change; nothing is re-recorded, as no baseline
   exists yet).
2. **The kit baseline suite** and its golden hashes and slot lengths, recorded at commit 1's tree.
3. **`retroplug-dsp-kernels` and `retroplug-dsp-resample`**: the kernels, and `lsdj/Effects`, `KitUtil`'s resample and
   quantise and the risa DMC path moved onto them; the baseline does not move by one hash.
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
  driver); and whether the plugin gains a kit compile. What D1's build adds to the work:
  - **The dither seed is computed before the run**, from `rp::dsp::inputSeed` over the whole pre-trim buffer at the kit
    rate; a graph that streams cannot hash its own input, so the driver passes that word in `rp.dsp.dither`'s `seed`.
    Hashing only the first block, or the trimmed input, moves every LSDj dither kit.
  - **Normalise must call `rp::dsp::normalizedGain`** (and `peak` over the whole buffer): it is a reciprocal multiply,
    where a division rounds differently and moves kit bytes. The graph form still has to be decided (section 5's gain is
    a plain streaming gain; D2 owns the offline form).
  - **The resample node plays a buffer, it does not fill one.** A recorder and a resampler on the same buffer in one
    plan are unsupported, so the driver fills each buffer before the plan that plays it. The node also repeats up to
    one input chunk on a rate change, and it must not bypass `Resampler` to feed r8brain host blocks: r8brain's double
    output is split-dependent even though the float cast hides almost all of it.
  - **The kit chain's order is the bytes**: window, the effects at the source rate, resample to the kit rate, quantise
    (codes), `frameAlign`, then the nibble pack. D2's graph must keep that order and the whole-buffer cut at
    `resampledLength`; the D1 baseline is its oracle. The codes are a graph's problem: `rp.dsp.dither` outputs audio at
    the quantised levels (section 2), so a graph compile either inverts `codeToLevel` - levels are k/(2^bits-1), so
    `round((level + 1) * 0.5 * maxValue)` recovers the code exactly - or the node gains a codes output D1 does not
    have; the driver decides, and either way the packer must see the same codes.
- **D3**: the CLI `render` and the UI's background renders on the offline driver, keeping their block-size sequence and
  per-block side effects or re-scoping their byte-identity claim. D1 changed neither path, so nothing is owed here
  beyond D2's driver.

## 9. Prototype questions

- What exactly does the dither seed hash (the windowed input at the source rate, or the resampled input the dither
  sees), so D2's graph can compute the same seed before the run?
- Do Sierra Lite's and JJN's padded loops have an exact fixed-size streaming form (do their forward error terms reach
  past a small window), and does the baseline hold through the rewrite?
- Does r8brain allocate anywhere in `process`, or only at construction, so the resample kernel's `process` is truly
  allocation-free?
- Can `retroplug-graph-test` link the DSP libraries (`retroplug-dsp-kernels` and `retroplug-dsp-resample`) without
  pulling anything the graph library must not link, and do the Pico-target refusals hold for `rp.dsp.resample` alone?
- Does anything outside the kit path call `lsdj/Effects` (the CLI's effect parsing, TS's effect descriptor), and does
  the move keep the reflect-cpp shape the RPC decodes?

## 10. As built

Section 7's six commits landed in order, each green, and nothing on the emulated audio path moved, so strict parity held
bit for bit at every one and no parity baseline moved. Section 3's own baseline is the new gate: the first two commits
record it, and every later commit had to leave all its hashes and slot lengths unchanged (stage 3's kernel move, the one
that could have moved a byte, did not). This section records what each stage decided where the design was silent, what
section 9 turned out to be, what building it corrected in sections 3 to 5 (each correction is in place there), and the
findings worth naming; each stage's own report is the full record.

### What each stage settled

- **1, the dither seed.** The hash is over the buffer the dither receives: the windowed, post-effect, r8brain-resampled
  sample at the kit rate (11468 Hz), before `frameAlign` trims it, not the source-rate window the gain and filter see. It
  is `rp::hash::fnv1a64` over the float bytes, canonical least-significant byte first whatever the host's byte order,
  folded `(h ^ (h >> 32))` and salted with a per-algorithm constant (`0x8064fcc5` for HighPassTPDF, `0x262f0818` for
  ShapedTPDF, each the FNV of the algorithm's name) so renumbering `DitherType` cannot re-seed a kit. The draw is
  `(rng() >> 8) * 2^-24`, a uniform from raw `mt19937` output that the standard fixes, where
  `uniform_real_distribution` is implementation-defined; negative zero still hashes apart from zero. The salt, the fold
  and the draw were pinned by an eight-code golden over silence, which is what D2's driver can check against.
- **2, the baseline.** It drives `KitCompiler` and the two codecs directly rather than the RPC (the RPC adds only DTO
  mapping, and constructing it needs an Engine the kit path never touches), and it writes each synthesised source as a
  32-bit float WAV so `SampleCache` and miniaudio stay in the loop: the test asserts the decoded buffer equals the
  samples it computed, so the golden pins the compilers and not a quantisation of their input. The record is
  `{hash, size, slot}` per case: `size` and `slot` are layout facts compared on every host, and `hash` (FNV-1a-64 of
  the bank) only on `linux-x64-gnu13`; off the target a case compiles twice and must agree with itself, and
  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` (set on CI's linux-x64 job) turns that fallback into a failure. The target is
  `rp::buildTarget()`, the same function the RPC reports, and `golden.target` is checked against it. Recording
  (`RP_KIT_GOLDEN_UPDATE=1`) refuses a case whose two compiles disagree, refuses a matrix it cannot represent (the
  record is keyed by case name), refuses to write anywhere but the target host, and writes through a sibling temp file
  renamed into place, so a failed record leaves the committed golden as it was rather than truncated. The suite guards
  against a vacuous pass: every case must compile clean, the bank must carry every slot it compiles, and the golden must
  name exactly the matrix's cases. The matrix grew from 26 to 31 cases in review: silent sources for both normalise
  paths, risa's normalise-off and loop branches, and the slot length beside the hash. A follow-up took it to 33: one
  two-slot bank per codec (a second sample named SNR with its own effects), which puts the assembler's offset
  accumulation past slot 0 and its 15-slot loop inside the baseline, and a lowercase over-long bank name on the LSDj
  one, for `writeString`'s uppercase and truncation. They added two records and moved no existing one.
- **3, the kernels.** The target named `retroplug-dsp` in section 4 was taken by `dpf_add_plugin(retroplug ...)`, which
  creates the plugin's own DSP library of that name, so the library is `retroplug-dsp-kernels` (gain, peak, the
  normalising gain, the biquads, dither and quantise) plus `retroplug-dsp-resample` (r8brain, pthreads, RTTI). The
  resampler's API is push/pull because its counts differ from a block's: `push` fills the 1024-frame chunk and
  resamples it into a ring of `2 * getMaxOutLen(1024)`, `pushSilence` continues the stream past the source, `pull`
  hands out output, and a pull larger than the ring simply takes more pushes. Its `init` is the one allocation, on the
  control thread, and nothing after the first chunk allocates or frees. The dither seed is split into `inputSeed()`
  (unsalted) and `init`'s salt, its codes are `uint16_t` (`0..65535` covers 16 bits), and `codeToLevel` is exactly the
  LSDj decoder's level. Sierra Lite and JJN keep their last two / four forward errors and skip the terms before the
  stream's first sample rather than adding zero, so the padded form's float operations and their order are reproduced.
  `Gain::normalizedGain` is a reciprocal multiply because the bytes depend on it. `Biquad` mirrors `lsdj::FilterType`
  by value with static_asserts, and `KitUtil::convertSamplerate` is deleted.
- **4, gain, biquad and dither.** The package's home is `retroplug-dsp-nodes`, a target of its own: `retroplug-graph`
  is what every consumer links, and the package brings the DSP library with it, which the graph library has no business
  carrying. Gain's param is linear 0..4 default 1; a held value is the kernel's exact multiply, a move ramps across one
  block from the previous block's end to the block's last value, and the first block after activation snaps. Biquad's
  type is a setting, its frequency, Q and gain (dB) are params, `setup` keeps state on a sweep and `init` clears it on
  activation or a type change, and the node hands the kernel an out-of-range frequency rather than clamping, because a
  clamp would be a different kit byte. Dither's defaults are `none`, 4 bits and seed `0x9e3779b9`; a settings commit
  restarts the noise. Its per-channel state is about 5 KB on LP64 (`std::mt19937` is 624 words of 8-byte
  `uint_fast32_t`), sized from `sizeof` and the settings' channel count. All three follow `channels` through
  `adjust_layout`, keep their kernel state in tail memory (gain's one float in a member), and declare neither `heap` nor
  `threads`, so all three plan on the Pico.
- **5, the resampler.** It is a package of its own, `rp.dsp.resample`, with a target of its own
  (`retroplug-dsp-resample-nodes`), because a node that reaches r8brain must be separable at link time, not only
  refused by the planner; its package id and its type id are the same string, which is legal because the registry keeps
  the two namespaces apart. Its buffer is a settings `resource_ref<"audio-buffer">`, not a resource port: building a
  resampler allocates, a port is first seen on the block thread and no control-thread hook sees its binding, whereas a
  settings ref is pinned at create and its handle readable in `activate`, so the resamplers are built there (into a
  local vector and moved in, so a throw leaves the previous stream intact). The cost is ordering: a
  `rp.core.buffer-recorder` and a resampler on one buffer in one plan are unsupported, and D2 fills a buffer before the
  plan that plays it. `loop` defaults false (unlike the player's true), the buffer's end is silence or a wrap into the
  same stream, equal rates still go through r8brain and are bit-exact, and a rate change rebuilds the resamplers and
  resumes from the output's buffer position, `origin + floor(emitted * (inRate / oldRate))`, so r8brain's run-in is
  heard again and up to one input chunk repeats. `node.hpp` gained `resource_ref::audio()`, and a resource ref's schema
  became an inline string (an rfl Parser specialization) so descriptors no longer carry a compiler-dependent `$defs`
  key.
- **6, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build.

### Section 9's questions, answered

- **What exactly does the dither seed hash?** The resampled input the dither sees, at the kit rate, salted with the
  algorithm (stage 1). A streaming node cannot hash its whole input before it starts, so D2's driver must assemble the
  buffer, hash it with `rp::dsp::inputSeed` and pass that word in `rp.dsp.dither`'s `seed`. A node that hashed only its
  first block, or its trimmed input, moves every dither kit.
- **Do Sierra Lite and JJN have an exact streaming form?** Yes. In the padded loop sample i is its scaled input plus the
  errors of i-2 and i-1 (JJN: i-4 to i-1), each times its weight, added oldest first; keeping the last two / four and
  adding them in the same order reproduces the same float operations, and the `i + k < n` guards only drop writes past
  the end. A `[dsp]` case holds the streaming form against verbatim copies of both padded functions at lengths 0 to 5,
  two noises and an overdriven sine, four bit depths (1, 4, 8 and 16) and five block splits. The kit baseline alone is
  too coarse to prove it: swapping Sierra's two terms left all 31 hashes matching while the `[dsp]` case failed.
- **Does r8brain allocate in `process`?** No, only at construction: 8 to 14 blocks in the constructor, then zero
  allocations and frees over 200 chunks from the first `process`, at every kit-relevant rate pair. A related finding
  is load-bearing: r8brain's double output depends on how its input is split across calls (low bits in 60 to 90% of
  samples, though the cast to float hides almost all of it), so the fixed 1024-frame chunk rule is part of the bytes and
  nothing may feed r8brain host blocks. Two 20000-frame `[dsp]` cases make the reference likelier to see such a flip.
- **Can `retroplug-graph-test` link the DSP libraries without the core, and do the Pico refusals hold?** Yes to both.
  The test links `retroplug-graph` + `retroplug-dsp-nodes` + `retroplug-dsp-kernels` (and the resample library for
  `Dsp.test.cpp`'s reference), with no core, txiki, efsw, LVGL or backend; `nm -u` of the kernels plus r8brain shows
  only libm, malloc/free, mem*, libstdc++ (r8brain's RTTI) and pthread mutex calls. Gain, biquad and dither plan on
  `Target::Pico`; `rp.dsp.resample` is refused there with `capability` for `heap`, and it is also absent at link time,
  since the Pico's build omits its target and its registration.
- **Who else calls `lsdj/Effects`?** Only the kit path: `KitUtil::compileSample` and `RisaDmcCodec::encode` (the math),
  as types the two codec specs and the RPC DTOs, and the tests. TS only mirrors the union in `audioDriver.ts` and
  builds effect objects in the CLI sessions. The rfl shape is untouched: the three structs, both enums and the tagged
  union are unchanged; only functions moved.

### What building it changed in sections 3 to 5

- **Section 3**: the seed is over the resampled, pre-trim buffer and salted by a stable constant, with a specified draw;
  the baseline is 33 cases, records the slot length, uses `RP_KIT_GOLDEN_REQUIRE_TARGET` and the shared
  `rp::buildTarget()`, and refuses a disagreeing case at recording.
- **Section 4**: the target is `retroplug-dsp-kernels` + `retroplug-dsp-resample`, not `retroplug-dsp`; the resampler's
  API is push/pull and its `process`-equivalent is allocation-free from the first chunk; the dither's seed is split into
  an unsalted hash and `init`'s salt, and its codes are `uint16_t`; Sierra Lite and JJN keep the last two / four
  errors; `normalizedGain` is a reciprocal multiply the kit path must call.
- **Section 5**: gain ramps at block rate to the block's last value and snaps on activation; the biquad keeps state when
  its coefficients move; dither's defaults and its settings-commit restart are stated; `rp.dsp.resample` is a package
  (and target) of its own whose buffer is a settings ref, built in `activate`, with `loop` false, the recorder caveat,
  and the repeat-at-a-rate-change behaviour.

### Found while building

- **The kit baseline is the only proof of the kernel move, and it is nearly blind to some arithmetic.** A one-ulp
  change in Sierra Lite's term order moves no 4-bit code across all 31 cases; the `[dsp]` cases had to carry verbatim
  references and 16-bit depths to see it. The baseline still catches anything that moves a bank, which is the contract
  D2 needs, but it is not a substitute for the kernel tests.
- **`sizeof(rp::dsp::Dither)` is about 5 KB on LP64**, not the 2.5 KB an MSVC 32-bit `uint_fast32_t` gives; the node's
  `extra_memory` derives it from `sizeof`, so the platform difference is harmless, but a channel cap must count it.
- **A node's per-channel loops are bounded by the span it was attached to, not by its port's channels.** An undersized
  `extra_memory` therefore makes a wrong stream, not a heap write, which is how the six-channel mutation was caught in
  a release build.
- **A resource ref's `$defs` key was compiler-dependent.** rfl names the entry after the C++ type, GCC and Clang spell
  it differently, and the resample node's new descriptor golden would have failed on the Clang CI job; the general fix
  (an inline string schema) also changed the system nodes' descriptors, and nothing reads the old entries.
- **A plan test that renders bigger blocks needs a bigger rig.** `GraphRig`'s host rows hold one default block (1024
  frames), so a 4096-frame split overran them and corrupted the heap; `GraphRig` now takes a `maxFrames` that sizes
  the rows.
- **The engine registers nine static packages plus the script pack** (the `rp.dsp` and `rp.dsp.resample` pair among
  the nine); a refused package is reported at engine construction and `REQUIRE`d by the rig, so a package id
  registered twice cannot fail a distant case.

### The suite, as built

- `retroplug-audio-test` gained `[kit-baseline]` (`test/audio/KitBaseline.test.cpp`, 33 banks, one case, 362
  assertions) and `[dither]` (`test/audio/Dither.test.cpp`, the same-twice cases and the eight-code golden), and
  `test-native` gained `kit-dither` (`test-native/kit-dither.test.ts`): each TPDF kit compiled twice to one bank, the
  bank read back through `KitView` holding a varied sample in both polarities (a constant or all-zero slot would
  otherwise pass, since LSDj's inverted nibbles decode zeros to +1.0), and the same source compiled with no effects
  differing from each dithered bank.
- `retroplug-graph-test` gained `[dsp]` (`test/graph/Dsp.test.cpp`, 17 cases: chunk-exact streaming at 1, 4, 8 and 16
  bits and every split, a `process` allocating nothing once warmed with the resampler counted from its first chunk, the
  padded references, the resampler's split independence, the seed golden), `[dsp-nodes]`
  (`test/graph/DspNodes.test.cpp`, 13 cases: each node against its kernel through a real plan, ragged splits, the gain
  ramp and snap, the biquad's sweep and type change, the dither's seed and channel sizing, a block counted from the
  plan's first block, and the descriptor goldens) and `[dsp-resample]`
  (`test/graph/DspResample.test.cpp`, 12 cases plus the shared descriptor case: a 22.05 kHz buffer into a 48 kHz plan
  and a 48 kHz one into the kit rate, at six splits, a loop committed after the buffer ran out, a rate change, a block
  counted from the plan's first block and the Pico refusal). It went from 208 cases to 250 and from 92566 assertions to
  291744.
- The kit baseline ran in a fresh process and under ASan, hash for hash, and the nodes and kernels under ASan and TSan
  with no report; nothing was re-recorded.
