# Phase D1: the kit baseline and the DSP library - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** The kit path's DSP becomes a core-free library of block-streaming kernels and the kit path's output becomes
reproducible and pinned: the two TPDF dithers are seeded from the input they quantise, so a kit compiles to the same
bytes every time; a 31-case Catch2 baseline holds every kit the LSDj and risa compilers build to its recorded hash,
size and slot length; the gain, biquad, dither and r8brain-resample math moves out of `retroplug-core` into
`retroplug-dsp-kernels` / `retroplug-dsp-resample` with `lsdj/Effects`, `KitUtil` and the risa DMC codec rewritten as
loops over them; and the graph gains `rp.dsp.gain`, `rp.dsp.biquad`, `rp.dsp.dither` and `rp.dsp.resample`, each
wrapping the same kernel the kit path uses, so a live patch and an offline kit compile run one implementation.

**Architecture:** Per [modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), which Task 6 lands with its
Status line, sections 3 to 5 corrected in place, section 7's landing list and section 8's D2 and D3 notes brought
forward, and a new section 10 ("As built"). Each kernel is a plain state struct with an `init` on the control thread
(coefficients; the resampler's one allocation) and a `noexcept`, allocation-free `process`; the resampler's counts
differ from a block's, so its streaming calls are `push` / `pushSilence` / `pull`.
The kit path keeps its whole-buffer shape as loops over the kernels and must not move one bank byte: the baseline is
the oracle for the move, and strict parity (which watches the live engine, not the kit path) must stay bit for bit at
every task. The `rp.dsp` nodes live in two packages, `rp.dsp` (gain, biquad, dither: host tail memory, no heap, no
threads, planable on the Pico) and `rp.dsp.resample` (its own package, source file and target, because a node that
reaches r8brain must be separable at link time), each wrapping its kernel and keeping the kit path's arithmetic
exactly.

**Tech stack:** C++20 with Catch2 (`retroplug-audio-test`, `retroplug-graph-test`, `retroplug-engine-graph-test`),
r8brain, reflect-cpp for descriptors and the golden JSON, the graph layer of Phases A to C5, and TypeScript for the one
`test-native` case. No submodule moves.

**Spec:** [modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md).

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/d1-dsp` in this worktree, on top of the design commit 37c8fd7a: six commits, one per task (Task 7 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A block that creates a file is that
file as the prototype commit left it; one that changes or deletes files is the prototype's diff for them, so `git
apply` it from the step's text (or apply it hunk by hunk with the Edit tool). The five generated artifacts (one kit
golden and four descriptor goldens) are quoted whole rather than diffed:
`test/audio/golden/kit-baseline.json` (31 entries of one line each, 2799 bytes) and the four
`test/graph/golden/rp.dsp.*.descriptor.json` files (one line each, under 1 KB), which is small enough to read and to
apply, unlike C5's megabyte golden documents. Every expected pass, test count and figure is quoted from the stage
reports in /tmp/d1pro or from the tip verification's logs there (`tv-*.log`), which is what the generator checks; a
failing-test step's expected failure is what the missing code produces, as each step says, plus the prototype's
measured red where a report records one; where no report states a count, the step says what to check instead. When
this plan was generated it was replayed in a scratch clone of 37c8fd7a, every block written or applied in order, and
each task committed with the command given: each commit's tree came out exactly as the prototype's, and each commit
message as its. The prototype commits, by task:

- Task 1 `8c1b5ab0`: the TPDF dithers are seeded from their input, so a kit compiles to the same bytes every time.
- Task 2 `b1f7c288`: a byte baseline holds every kit the compilers build.
- Task 3 `8e226504`: the kit DSP is a core-free library of block-streaming kernels.
- Task 4 `09c57e09`: `rp.dsp.gain`, `rp.dsp.biquad` and `rp.dsp.dither`.
- Task 5 `b6cbb412`: `rp.dsp.resample`, a buffer player that resamples to the plan rate.
- Task 6 `00c59da9`: the docs; Task 7 is the acceptance run and commits nothing.

**Where this plan departs from the design as committed (37c8fd7a).** The design was written before the prototype;
this plan follows what was built, and Task 6 lands the design with its Status line, sections 3 to 5 corrected in
place, section 7's landing list and section 8's D2 and D3 notes brought forward, and a new section 10 ("As built"). In
short:
- **The DSP target is `retroplug-dsp-kernels` plus `retroplug-dsp-resample`, not `retroplug-dsp`.** The design's
  name is taken: `dpf_add_plugin(retroplug ...)` creates the plugin's own DSP library of that name.
  `retroplug-dsp-kernels` (gain, peak and the normalising gain, the biquads, dither and quantise) links the C++
  standard library and nothing else; `retroplug-dsp-resample` is `Resample.cpp` alone with r8brain and
  `Threads::Threads`, so a build with no resampler omits one target and sees neither the heap nor pthreads nor RTTI.
  The node packages are `retroplug-dsp-nodes` and `retroplug-dsp-resample-nodes`, targets of their own beside
  `retroplug-graph` (which is what every consumer links and carries no DSP).
- **The dither seed is specified exactly.** `inputSeed()` is FNV-1a-64 over the buffer's floats, each float's
  binary32 bytes least-significant byte first whatever the host's byte order (a big-endian build uses
  `std::bit_cast` and the running hash, not a copy), folded to a 32-bit word as `h ^ (h >> 32)`; `init` salts it
  with a stable per-algorithm constant, `0x8064fcc5` for `HighPassTPDF` and `0x262f0818` for `ShapedTPDF` (each the
  FNV-1a-64 of the algorithm's name folded), so renumbering `DitherType` cannot re-seed a kit; and the noise draws
  `(rng() >> 8) * 2^-24` from raw `mt19937` output, because the standard fixes `mt19937`'s sequence and does not fix
  `std::uniform_real_distribution`'s. The buffer hashed is the one the dither is handed: the windowed, post-effect,
  r8brain-resampled sample at the kit rate (11468 Hz), before `frameAlign` trims it. `inputSeed()` is unsalted and
  the salt is `init`'s, so `rp.dsp.dither`'s `seed` setting is a kit compile's `inputSeed(buffer)` and a live
  patch's default still gives the two TPDFs different noise.
- **The noise is platform-independent, with a stated scope.** Same input, same noise on any host whose float
  arithmetic is IEEE-754 binary32; a whole bank is byte-identical only where r8brain and the filters agree too,
  which is why exact kit bytes are compared on `linux-x64-gnu13` only. The `inputSeed` big-endian half cannot be
  exercised on a little-endian host, so the two halves are held together by construction, not by a case.
- **The resampler's API is push/pull, not `process(in, out, frames)`.** `init` is the one allocation, on the control
  thread; `push` fills the 1024-frame chunk and resamples it into a ring of `2 * getMaxOutLen(1024)`, `pushSilence`
  continues the stream past the source, `pull` hands out output (a pull larger than the ring simply takes more
  pushes), and `resampledLength` is the offline cut. Nothing may feed r8brain host blocks: its double output depends
  on how its input is split across calls (low bits in 60 to 90% of samples, which the float cast hides almost all
  of), so the fixed 1024-frame chunk is part of the bytes. Nothing after the first chunk allocates or frees.
- **`rp.dsp.resample` is a package of its own, `rp.dsp.resample`, with its own target.** The registry keeps package
  ids and type ids apart, so the package id and its one type's id are the same string, but it cannot be a second
  package under `rp.dsp` (a package id registers once) and a node that reaches r8brain must be separable at link
  time, not only refused by the planner: the Pico's build omits `retroplug-dsp-resample-nodes` and its registration,
  and the planner still refuses the node there with `capability` for `heap`.
- **Its resamplers are node members built in `activate`, and its buffer is a settings field.** Tail memory is never
  destroyed (nothing runs a destructor over it), so a node whose state owns anything keeps it in a member built in
  `activate`; the resamplers are built into a local vector and moved in, so a `bad_alloc` leaves the previous stream
  intact and they are freed with the instance on the control thread. The buffer is a settings `resource_ref` and not
  a resource port because building a resampler allocates: a port's binding is first seen on the block thread and no
  control-thread hook sees it, a settings ref is pinned at create and readable in `activate`. The cost is ordering,
  and it is exactly this: a plan refuses a resource a port writer fills and a port reader binds
  (`resource-reader-unordered`), but a settings ref is not a port binding, so a `rp.core.buffer-recorder` and a
  resampler on one buffer in one plan plan cleanly and the resampler reads whichever prefix the block schedule
  wrote, silently. That pairing is unsupported, and D2's driver fills a buffer before the plan that plays it.
- **`node.hpp`'s `resource_ref` schema is an inline string.** rfl names a `$defs` entry after the C++ type, and GCC
  and Clang spell a class template over a `fixed_string` differently (GCC writes the argument's type, Clang drops
  it), so the new resample descriptor golden would have failed the Clang CI job; an `rfl::parsing::Parser`
  specialization writes the ref as the plain string with a string schema inline. The system nodes' descriptors
  changed shape with it (their rom/sram/state refs lost their `$defs` entries), and nothing reads the old entries:
  the two engine-graph helpers now pick the one `$defs` entry with properties, and TS has no `$defs` consumer. The
  same change adds `resource_ref::audio()` beside `bytes()`.
- **The baseline records `{hash, size, slot}` for 31 cases, and its target rules are its own.** 21 LSDj and 10 risa
  banks; `size` and `slot` are layout facts compared on every host, `hash` only on `linux-x64-gnu13`;
  `RP_KIT_GOLDEN_UPDATE=1` records and refuses a case whose two compiles disagree and refuses to write anywhere but
  the target host; `RP_KIT_GOLDEN_REQUIRE_TARGET=1` (set on CI's linux-x64 job only) turns the off-target fallback
  into a failure. The target string is `rp::buildTarget()` in `src/util/BuildTarget.hpp`, the same function the
  engine's `buildTarget` RPC returns, so the golden's `target` and the RPC cannot drift. `RisaDmcCodec` exposes its
  bank layout constants, so the test's bank reader names the offsets its assembler writes.
- **The baseline is coarse, and the kernel tests are the fine oracle.** Two mutations pass all 31 kit hashes:
  swapping Sierra Lite's two forward-error terms (a one-ulp change) and resampling the partial chunk at each push's
  end. The `[dsp]` cases catch both with the fine oracles they copy verbatim: the padded whole-buffer Sierra Lite
  and JJN loops at 1, 4, 8 and 16 bits catch the term order, and the reference that feeds r8brain the fixed
  1024-frame chunks directly catches the partial flush. The baseline still catches anything that moves a bank, which
  is D2's contract; it is not a substitute for the kernels' own cases.
- **Other findings the build recorded.** `sizeof(rp::dsp::Dither)` is about 5 KB on LP64 (`std::mt19937` is 624
  words of 8-byte `uint_fast32_t`), and the dither node's `extra_memory` derives from `sizeof` and the settings'
  channel count, so the platform difference is harmless; a node's per-channel loops are bounded by the span it was
  attached to, so an undersized `extra_memory` is a wrong stream and not a heap write; `GraphRig` takes a
  `maxFrames` that sizes its host rows (a 4096-frame split overran the old 1024-frame ones and corrupted the heap);
  the engine registers nine static packages plus the script pack; a refused package is reported at engine
  construction and `REQUIRE`d by the rig; the allocation cases count a plan's blocks from its first block after
  `setGraph`, and `[dsp]` counts the resampler from its first chunk.
- **`rp.dsp.dither` outputs audio, not codes.** The kernel yields `uint16_t` codes 0..2^bits-1 (16 bits fits) and
  the node maps them back with `codeToLevel`, so it is a bit-crusher in a live patch. D2's kit compile therefore
  either inverts `codeToLevel` (`round((level + 1) * 0.5 * maxValue)`, exact because levels are k/(2^bits-1)) or
  needs a codes output D1 does not have; either way the packer must see the same codes.
- **Three review findings are recorded and deliberately not fixed**, so no step below touches them: two `TEST_CASE`
  names still say "warmed" though the cases count from the plan's first block (`DspNodes.test.cpp`,
  `DspResample.test.cpp`); `[kit]` tags only the three dither cases, so `retroplug-audio-test "[kit]"` does not run
  `[kit-baseline]`; and two comments in `DspResampleNodes.hpp` / `.cpp` say the planner orders a port's reader after
  its writer where the planner refuses that pairing (`resource-reader-unordered`). A later code commit can take
  them; the plan stays equal to the prototype's trees.


**Ground rules for whoever executes this:**
- This plan lives at `docs/modular-graph-phase-d1-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's link to the design is relative to that location; every command and path in the steps is relative to
  the worktree root.
- Commit after each task (the commands are given; Task 7 commits nothing unless a step fails and is fixed, in a commit
  of its own). No `Co-Authored-By` or "generated with" trailer. Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with uncommitted
  work in the tree. Never touch a submodule. `deps/sameboy` shows as modified after any configure: that is the tracked
  audio patch, expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- **Parity holds the engine's output to the bit, and no task re-records it.** D1 changes nothing on the emulated audio
  path, so a strict parity failure is a real change: stop and find it. Never set `RP_PARITY_UPDATE`.
- **Two things are recorded in this plan, each in exactly one step, and no later task touches them**: Task 2's kit
  golden (placed, then re-recorded to prove the recorder lands the same bytes) and Tasks 4 and 5's descriptor goldens
  (recorded with `RP_GRAPH_GOLDEN_UPDATE=1`, then compared with the prototype's bytes). Re-recording is how an
  intentional change would be made later; here it is a check.
- **The kit baseline's exact bytes exist on `linux-x64-gnu13` only**, so this plan is executed on that host: recording
  refuses any other (`a recording only happens on ...`), and CI's linux-x64 job is the other place they are compared.
  On a foreign host Task 2's recording step refuses and the plan stops there; do not force it.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; without it their
  cases skip. A shell `export` does not survive between commands.
- The Catch2 binaries are `EXCLUDE_FROM_ALL`: `cmake --build build` does not relink them, so a library edit needs the
  test target built by name before a run. A new test source needs `cmake -S . -B build` first.
- The sanitizer trees are their own: `build-tsan/` and `build-asan/`. Configure one only if its `CMakeCache.txt` is
  missing (`cmake -S . -B build-tsan -DRETROPLUG_SANITIZE=thread`, or `build-asan` with `=address`). Build the binary in
  that tree before running it there; a stale prototype binary proves nothing.
- Case counts are exact. A `test cases: N | P passed | S skipped` line, an `All tests passed (A assertions in N test
  cases)` line and a per-file count must match the quoted N/P/S. Assertion totals are exact too, with one exception: a
  run that includes `[capability]`, and so any run of the whole engine-graph binary, may report 2 or 4 more assertions
  than quoted, from the device-thread progress case (a drain that lands between the final progress store and `Done`
  legitimately yields one more entry, 2 assertions; two more entries make 4). `[dsp]`, `[dsp-nodes]`,
  `[dsp-resample]`, `[kit-baseline]` and `[dither]` carry no such case, so their totals are exact.
- The engine-graph selection **Task 7** runs reports 93 cases where C5's plan recorded 90 for the same tags: main added
  three engine-graph cases (commit `17a60f7c0`, the cart adapters' jobs and a host's teardown as sections of their own)
  between C5's tip and this design commit. That is not a D1 change, and the full engine-graph count (267 cases) is the
  same as the baseline's.
- The prototype branch `proto/d1-dsp` is this plan's provenance, and three steps read it: Task 2 Step 3, Task 4 Step 3
  and Task 5 Step 3 compare a recorded golden with the prototype commit's bytes. Keep the branch until Task 6 is
  committed; after that no step reads it.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks one
  at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper; pgrep -a jackd`).

**Commit this plan before Task 1.** Before the commit, check where the tree is: `git rev-parse --abbrev-ref HEAD` must
print `feature/modularize`, `git rev-parse --short HEAD` must print `37c8fd7a`, and `git status --short` must show only
` m deps/sameboy`. If any of the three differs, stop: every step below assumes the design commit on a clean tree. The
run below copies the plan to its home in the repository and commits it on its own, so every later task's `git status`
and `git add` see a clean tree:

````bash
cp /tmp/d1plan/plan.md docs/modular-graph-phase-d1-plan.md
git add -- docs/modular-graph-phase-d1-plan.md
git commit -F - <<'EOF'
docs: the D1 implementation plan

The plan for Phase D1, the kit baseline and the DSP library, generated from the prototype's six commits on
proto/d1-dsp: the dither seed, the 31-case kit baseline, the core-free kernels and today's code on them, the
rp.dsp gain, biquad and dither nodes and rp.dsp.resample, then the docs. Every expected count is quoted from a
stage report or the tip verification, and the code was replayed commit by commit against the prototype's trees.
EOF
````

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/dsp/` | `Gain`, `Biquad`, `Dither` and `Resample` (Task 3; the resampler gets its own target in Task 4): the kernels, plain state structs with an `init` and an allocation-free `process` (the resampler's push/pull) |
| `packages/native/src/lsdj/` | `Effects` mapped onto the kernels and `quantise` (Task 3), `KitUtil`'s chain and `convertCodesToNibbles`, `convertSamplerate` deleted (Task 3) |
| `packages/native/src/risa/RisaDmcCodec.{hpp,cpp}` | the public bank-layout constants (Task 2), the DMC path on the kernels (Task 3) |
| `packages/native/src/util/` | `BuildTarget.hpp` (Task 2), `Hash.hpp`'s running hash (Task 3) |
| `packages/native/src/graph/node.hpp` | the tail-memory hook note (Task 4), `resource_ref::audio()` and the inline schema (Task 5) |
| `packages/native/src/graph/nodes/DspNodes.{hpp,cpp}` | `rp.dsp.gain`, `rp.dsp.biquad`, `rp.dsp.dither` and the package (Task 4); the resample package's sibling note (Task 5) |
| `packages/native/src/graph/nodes/DspResampleNodes.{hpp,cpp}` | `rp.dsp.resample`, a package of its own (Task 5) |
| `packages/native/src/graph/Descriptor.{hpp,cpp}` | `checkChannels` (Task 4) and `checkLayout` (Task 5) lifted out of `CoreNodes.cpp` |
| `packages/native/src/host/engine/Engine.cpp` | the two packages registered, a refusal reported (Tasks 4 and 5) |
| `packages/native/cmake/` | the four DSP targets, `Threads`, and the new test sources and libraries (Tasks 3 to 5) |
| `packages/native/test/audio/` | `[dither]` (Task 1), `[kit-baseline]` and its golden (Task 2) |
| `packages/native/test/graph/` | `[dsp]` (Task 3), `[dsp-nodes]` (Task 4), `[dsp-resample]` (Task 5), the descriptor goldens and `GraphRig`'s packages and `maxFrames` (Tasks 4 and 5) |
| `packages/retroplug/test-native/kit-dither.test.ts` | a kit compiled twice gives identical bytes, through the RPC and the real codecs (Task 1) |
| `.github/workflows/build.yml` | `RP_KIT_GOLDEN_REQUIRE_TARGET` on the linux-x64 job (Task 2) |
| `spec/`, `docs/`, `AGENTS.md` | the design's as-built record, the specs and the live docs (Task 6) |

---

### Task 1: the TPDF dithers are seeded from their input

Design sections 2 and 3, and section 9's first question. The two TPDF dithers no longer seed a `std::mt19937` from
`std::random_device`: the seed is a hash of the buffer they quantise, so a kit compiles to the same bytes every time.
This is D1's one deliberate kit behaviour change, and nothing is re-recorded, because no baseline exists yet. The hash
is over the float bytes, folded to `mt19937`'s 32-bit word and salted with a stable per-algorithm constant, and the
draw is `(rng() >> 8) * 2^-24` from raw `mt19937` output rather than `std::uniform_real_distribution`, whose sequence
the standard does not fix. The buffer hashed is the one the dither is handed: the windowed, post-effect,
r8brain-resampled sample at the kit rate (11468 Hz), before `frameAlign` trims it to whole frames. The risa kit path
skips dither entirely, so only the LSDj path's default chain moves.

**Files:**
- Create: `packages/native/test/audio/Dither.test.cpp`, `packages/retroplug/test-native/kit-dither.test.ts`
- Modify: `packages/native/cmake/tests.cmake`, `packages/native/src/lsdj/Effects.cpp`

- [ ] **Step 1: Write the failing cases**

`Dither.test.cpp` (new, tag `[dither][kit]`, in `retroplug-audio-test`) has three cases: a TPDF dither over one input
gives the same codes twice (both algorithms), two buffers differing in one sample by `1.0e-6` get different noise
(more than 16 of 1024 samples differ, which a fixed seed would not give), and eight silent samples give the exact
codes each algorithm produces, which pins the hash, the fold, the salt and the draw at once. `kit-dither.test.ts`
(new, in `test-native`) compiles one generated WAV with each TPDF dither twice through `audioDriver.compileKit` (the
RPC, `KitCompiler`, `LsdjKitCodec`) and compares the 16 KB banks byte for byte; it first asserts each compile is a
`BANK_SIZE` bank dropped into a fresh ROM's slot 0 and read back through `KitView` (`kit.valid`, one sample, frames
above zero, a decoded peak above 0.3), so the byte compare cannot pass vacuously. Neither file skips, so neither needs
a `skip-baseline.json` entry.

Create `packages/native/test/audio/Dither.test.cpp`:

````cpp
// The kit path's two TPDF dithers, seeded from the samples they quantise.
//
// `KitUtil::compileSample` hands the dither the WINDOWED, POST-EFFECT, RESAMPLED buffer at the Game Boy kit rate
// (11468 Hz), before `frameAlign` trims it to whole 32-sample frames, so that is the input the seed is drawn from.
// Both cases here call the effect directly on a buffer at that rate, the way the kit path does, and hold what the
// seed has to give: a compile gives the same bytes for the same input, two different samples do not share one noise
// sequence (which a single fixed seed would), and the noise is a specified function of the input and the algorithm,
// so it is the same on any host whose float arithmetic is IEEE-754 - the draw comes from raw `mt19937` output and the
// hash reads the floats' bytes little-endian, neither of which depends on the standard library's or the host's choices.
// A whole kit bank is byte-identical only where the resampler and the filters agree as well, which is why the exact
// bank bytes are compared on one reference target.
//
// Run via `pnpm test:plugin`.

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "lsdj/Effects.hpp"
#include "lsdj/KitUtil.hpp"

namespace {

// The rate the dither runs at: the kit's target rate, which is also its input rate once `compileSample` has resampled.
constexpr float kKitRate = static_cast<float>(rp::lsdj::kGameboySampleRate);

/** A decaying tone as a source sample: something with real 4-bit structure for the dither to quantise. */
std::vector<float> tone(std::size_t frames) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kKitRate;
        out[i] = 0.5f * std::sin(2.0f * rp::lsdj::kPi * 220.0f * t) * std::exp(-t * 4.0f);
    }
    return out;
}

std::vector<float> dithered(std::vector<float> buf, rp::lsdj::DitherType type) {
    rp::lsdj::processEffect(rp::lsdj::DitherEffect{type}, buf, kKitRate);
    return buf;
}

/**
 * The index of the first sample two runs disagree on, or -1. Comparing the bytes keeps a failing `CHECK` down to one
 * number: the vectors are thousands of floats and dumping them buries the message.
 */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0;
    const std::size_t bytes = a.size() * sizeof(float);
    if (std::memcmp(a.data(), b.data(), bytes) == 0) return -1;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    }
    return 0;
}

} // namespace

TEST_CASE("a TPDF dither quantises the same input to the same codes twice", "[dither][kit]") {
    for (const auto type : {rp::lsdj::DitherType::HighPassTPDF, rp::lsdj::DitherType::ShapedTPDF}) {
        INFO("algorithm " << static_cast<int>(type));
        const auto first  = dithered(tone(1024), type);
        const auto second = dithered(tone(1024), type);
        CHECK(firstDifference(first, second) == -1);
    }
}

TEST_CASE("a TPDF dither draws different noise for a different input", "[dither][kit]") {
    // Silence is the cleanest probe: every code comes from the dither, so a differing noise sequence shows up as
    // differing codes. The two buffers differ in ONE sample by a millionth, far below a 4-bit quantum, so a fixed
    // seed would leave the two passes agreeing almost everywhere, while a seed drawn from the input walks a
    // different sequence from sample 0. The threshold is loose on purpose: it only has to tell "the noise changed"
    // apart from "only the one sample changed".
    std::vector<float> a(1024, 0.0f);
    std::vector<float> b = a;
    b[0] = 1.0e-6f;

    const auto da = dithered(a, rp::lsdj::DitherType::HighPassTPDF);
    const auto db = dithered(b, rp::lsdj::DitherType::HighPassTPDF);

    std::size_t differing = 0;
    for (std::size_t i = 0; i < da.size(); ++i) {
        if (da[i] != db[i]) ++differing;
    }
    CHECK(differing > 16);
}

TEST_CASE("each TPDF dither's noise from a fixed input is those exact codes", "[dither][kit]") {
    // Eight silent samples, and the codes each algorithm quantises them to, recorded from the implementation. This
    // pins the whole seed derivation - the hash, its fold, the per-algorithm salt, the draw from `mt19937` - so a
    // drift in any of them fails here with a number instead of surfacing later as a kit byte diff. The codes hold on
    // any host: nothing in either pass is anything but exact binary32 arithmetic on a silent buffer.
    const std::vector<float> silence(8, 0.0f);

    const std::vector<float> highPass{
        7.0f, 8.0f, 7.0f, 7.0f, 8.0f, 7.0f, 8.0f, 8.0f,
    };
    const std::vector<float> shaped{
        7.0f, 7.0f, 7.0f, 7.0f, 8.0f, 7.0f, 8.0f, 7.0f,
    };

    const auto hp = dithered(silence, rp::lsdj::DitherType::HighPassTPDF);
    const auto sp = dithered(silence, rp::lsdj::DitherType::ShapedTPDF);
    CHECK(firstDifference(hp, highPass) == -1);
    CHECK(firstDifference(sp, shaped) == -1);
    // The salt's whole purpose: the two algorithms over one buffer must not start from one word.
    CHECK(firstDifference(hp, sp) != -1);
}
````

Create `packages/retroplug/test-native/kit-dither.test.ts`:

````ts
// The kit compiler's two TPDF dithers are seeded from the samples they quantise, so two compiles of one sample give
// one bank. The callers that compile a kit from scratch are the CLI sessions (`cli/sessions/lsdj-rom.ts`), and today
// that is the only path that reaches `KitCompiler`.
//
// Everything here goes through the path a session uses (audioDriver.compileKit -> the harness RPC -> KitCompiler ->
// LsdjKitCodec), not the codec directly, so the case covers the effect descriptor decoding and the per-slot fan-out
// as well. The bank is read back through `KitView` before it is compared: an empty or undecoded slot would make two
// compiles agree byte for byte for the wrong reason. The source WAV is generated, so this needs no fixtures.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { KitEffect } from "../src/audioDriver";
import { KitView } from "../src/lsdj/rom/kit";
import { KIT_LOOKUP, BANK_SIZE, ROM_SIZE } from "../src/lsdj/rom/constants";
import { encodeWav } from "../cli/wav";

const WAV = "/tmp/rp-kit-dither.wav";

// A decaying 330 Hz tone at 44.1k, so the dither quantises a real signal through its whole range.
function writeSource(be: ReturnType<typeof createRealBackend>): void {
  const frames = 6000;
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 330) * Math.exp(-i / 2500);
  if (!be.writeFile(WAV, encodeWav(pcm, 44100, 1))) throw new Error(`write failed: ${WAV}`);
}

// Drop a compiled bank into a fresh ROM's first kit slot and read the slot back, the way a ROM's own kit is read.
function readBack(bank: Uint8Array): { frames: number; peak: number } {
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bank, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.valid).toBeTruthy();
  expect(kit.sampleCount()).toBe(1);
  const pcm = kit.sampleData(0);
  let peak = 0;
  for (const v of pcm) peak = Math.max(peak, Math.abs(v));
  return { frames: pcm.length, peak };
}

test("compileKit gives identical bytes twice for each TPDF dither", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  writeSource(be);

  for (const ditherType of ["HighPassTPDF", "ShapedTPDF"] as const) {
    const effects: KitEffect[] = [{ type: "dither", ditherType }];
    const spec = { name: "DITH", samples: [{ path: WAV, name: "SIN", effects }] };

    const first = audio.compileKit(spec);
    const second = audio.compileKit(spec);

    // The compared bank is a whole 16 KB bank whose slot decoded to audible samples (a decaying sine keeps its
    // energy through the resample and the 4-bit quantise), so "identical" can never mean "identically empty".
    expect(first.length).toBe(BANK_SIZE);
    expect(second.length).toBe(BANK_SIZE);
    const { frames, peak } = readBack(first);
    expect(frames > 0).toBeTruthy();
    expect(peak > 0.3).toBeTruthy();

    let firstDiff = -1;
    for (let i = 0; i < first.length; i++) {
      if (first[i] !== second[i]) {
        firstDiff = i;
        break;
      }
    }
    expect(firstDiff).toBe(-1);
    console.log(
      `[kit-dither] ${ditherType}: two compiles agree over ${first.length} bytes, ` +
        `slot ${frames} frames, peak ${peak.toFixed(2)}`,
    );
  }
});
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 8b2b84038..e57b02b94 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -68,6 +68,7 @@ retroplug_add_test_binary(
              test/audio/NesTeardown.test.cpp           # the NES twin of SMS's construct/destruct guard
              test/audio/SameBoyModels.test.cpp         # every SameBoyModel (incl. SGB variants) boots + renders
              test/audio/EngineInvoker.test.cpp         # the teardown discards a staged-message payload
+             test/audio/Dither.test.cpp                # the kit TPDF dithers are seeded from their input
              test/graph/CountingAllocator.cpp          # replaces operator new / delete: the N8 FIFO's [alloc] case
     LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
````

Run: `sed -n '452,463p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure picks up the new source in `retroplug-audio-test`'s list.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[dither]"`
Expected: the same-twice case fails for both algorithms (its `CHECK(firstDifference(first, second) == -1)` reports a first differing sample) and the eight-code case fails too, so Catch2 exits non-zero (the report's two-case red recorded exit 42). Its red was measured before the review fix, when the file had two cases and compared the vectors with `CHECK( first == second )` (`test cases: 2 | 1 passed | 1 failed`); report this tree's own counts.

Run: `pnpm test:native kit-dither`
Expected: `not ok 1` with `expected -1, got <offset>`, where the offset is a bank byte just past the 0x60 header, in the sample-data region. The value follows the random seed, so report yours: the prototype measured 96 before the review fix and 97 after.

- [ ] **Step 2: Seed both TPDFs from the buffer they quantise**

`Effects.cpp` gains `kHighPassTPDFSalt` / `kShapedTPDFSalt` (FNV-1a-64 of each algorithm's name, folded to 32 bits, so
renumbering `DitherType` cannot re-seed a kit), `hashFloats` (FNV-1a-64 over the floats' bytes, hashed in place on a
little-endian host and through `std::bit_cast` byte vectors on a big-endian one, so both seed the same), `uniform01`
(`(rng() >> 8) * 2^-24`, exact in binary32) and `ditherRng`, and both algorithms seed from it. Every other line of the
two algorithms stays as it is: the distribution, the quantum, the shaping coefficients, the clamp and the rounding.
The comments state the boundary (the buffer the dither receives: resampled, pre-`frameAlign`, not the source-rate
window the gain and filter see) and the scope (same input, same noise wherever a float is binary32; a whole bank only
where r8brain and the filters agree too).

````diff
diff --git a/packages/native/src/lsdj/Effects.cpp b/packages/native/src/lsdj/Effects.cpp
index 76f20b471..ccc34cd6c 100644
--- a/packages/native/src/lsdj/Effects.cpp
+++ b/packages/native/src/lsdj/Effects.cpp
@@ -1,6 +1,10 @@
 #include "lsdj/Effects.hpp"
 
+#include <bit>
 #include <cmath>
+#include <cstdint>
+
+#include "util/Hash.hpp"
 
 namespace rp::lsdj {
 
@@ -191,23 +195,71 @@ void jjnErrorDiffusion(std::vector<float>& buf, int bitDepth) {
     }
 }
 
+// One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
+// `DitherType`'s ordinal, so the order the enum happens to declare its algorithms in cannot re-seed a kit that uses
+// one of them.
+constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fnv1a64("HighPassTPDF")
+constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fnv1a64("ShapedTPDF")
+
+// The buffer's samples as the bytes FNV-1a hashes, least-significant byte of each float first whatever the host's
+// own byte order, so a big-endian build seeds the same as a little-endian one. A float is IEEE-754 binary32 on every
+// target; on a little-endian host the bytes are already in that order, so they are hashed in place.
+std::uint64_t hashFloats(const std::vector<float>& buf) {
+    if constexpr (std::endian::native == std::endian::little) {
+        return rp::hash::fnv1a64(reinterpret_cast<const std::uint8_t*>(buf.data()),
+                                 buf.size() * sizeof(float));
+    } else {
+        std::vector<std::uint8_t> bytes(buf.size() * sizeof(float));
+        for (std::size_t i = 0; i < buf.size(); ++i) {
+            const std::uint32_t bits = std::bit_cast<std::uint32_t>(buf[i]);
+            for (std::size_t b = 0; b < sizeof(float); ++b) {
+                bytes[i * sizeof(float) + b] = static_cast<std::uint8_t>(bits >> (8 * b));
+            }
+        }
+        return rp::hash::fnv1a64(bytes.data(), bytes.size());
+    }
+}
+
+// A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
+// mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
+// three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
+float uniform01(std::mt19937& rng) {
+    return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
+}
+
+// The seed both TPDF dithers draw their noise from: `hashFloats` over the buffer this pass quantises, folded to
+// `mt19937`'s 32-bit word and salted with the algorithm, so the two dithers over one buffer do not run the same
+// sequence. Drawing it from the input is what makes a compile reproducible: a kit bank is a file, and two compiles of
+// the same sample have to be the same file. The seed is a function of the buffer's contents and the algorithm only,
+// not of the bit depth or any other setting.
+//
+// The input is the buffer the dither is handed, which for `KitUtil::compileSample` is the windowed, post-effect,
+// resampled sample at the Game Boy kit rate (11468 Hz) as the dither receives it, BEFORE `frameAlign` trims it to
+// whole frames - not the source-rate window the gain and filter see, and not the trimmed bank.
+//
+// Same input, same noise on any host: the draw and the hash are both specified, so the dither's own arithmetic is
+// exact wherever a float is binary32. A whole bank is byte-identical only where the resampler and the filters agree
+// too, which is why exact kit bytes are compared on one reference target.
+std::mt19937 ditherRng(const std::vector<float>& buf, std::uint32_t salt) {
+    const std::uint64_t hash = hashFloats(buf);
+    const std::uint32_t folded = static_cast<std::uint32_t>(hash ^ (hash >> 32));
+    return std::mt19937(folded ^ salt);
+}
+
 void highPassTPDF(std::vector<float>& buf, int bitDepth) {
     const int   levels    = 1 << bitDepth;
     const float maxValue  = static_cast<float>(levels - 1);
     const float halfScale = maxValue / 2.0f;
     constexpr float quantum = 1.0f;
 
-    // Per-call RNG so two parallel sample compiles don't share state. The
-    // resulting non-reproducibility is the same as the legacy code: dither
-    // bytes change run-to-run but the audible result is indistinguishable.
-    std::mt19937 rng(std::random_device{}());
-    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
+    // A per-call RNG so two parallel sample compiles don't share state, seeded from the input so they agree.
+    std::mt19937 rng = ditherRng(buf, kHighPassTPDFSalt);
 
     float prevDither   = 0.0f;
     float shapingState = 0.0f;
 
     for (float& v : buf) {
-        const float dither = (dist(rng) + dist(rng) - 1.0f) * quantum * 0.5f;
+        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
         float shaped = dither - prevDither * 0.94f;
         prevDither = dither;
         shaped -= shapingState * 0.5f;
@@ -225,12 +277,11 @@ void shapedTPDF2ndOrder(std::vector<float>& buf, int bitDepth) {
     const float halfScale = maxValue / 2.0f;
     constexpr float quantum = 1.0f;
 
-    std::mt19937 rng(std::random_device{}());
-    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
+    std::mt19937 rng = ditherRng(buf, kShapedTPDFSalt);
 
     float err1 = 0.0f, err2 = 0.0f;
     for (float& v : buf) {
-        const float dither = (dist(rng) + dist(rng) - 1.0f) * quantum * 0.5f;
+        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
         // Coefficients tuned for ~11 kHz target rate (LSDJ kit sample rate)
         float value = (v + 1.0f) * halfScale + dither + (1.623f * err1) - (0.982f * err2);
         value = std::clamp(value, 0.0f, maxValue);
````

Run: `sed -n '490,597p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`: the three cases, green.

Run: `pnpm test:native kit-dither`
Expected: one case passes, and its log carries `[kit-dither] HighPassTPDF: two compiles agree over 16384 bytes, slot 1536 frames, peak 1.00` and the same line for `ShapedTPDF`.

- [ ] **Step 3: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 with no line matching `warning` or `error`: the full build, every target (the report's `0` is that grep's count: `pnpm build | grep -ciE 'warning|error'`).

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm test`
Expected: `# 169 test file(s) passed (jobs=16)` (unchanged: no TS source moved).

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. `retroplug-audio-test` `All tests passed (1234835 assertions in 108 test cases)` (the baseline's 1234829 / 105 plus this file's three cases); `retroplug-engine-graph-test` without fixtures `test cases: 267 | 258 passed | 9 skipped`; `retroplug-graph-test` `All tests passed (92566 assertions in 208 test cases)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, no file skipped, no baseline re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, the skip check passing.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 160 native test file(s) passed (jobs=16)` (the baseline's 159 plus `kit-dither`).

Run: `pnpm check:docs`
Expected: `check-doc-links: 73 file(s) clean`: the prototype's 72 plus this plan, which is committed before Task 1.

- [ ] **Step 4: The sanitizers**

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]" && cmake --build build-tsan --target retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "[dither]"`
Expected: both builds link and both runs pass with no AddressSanitizer, LeakSanitizer or ThreadSanitizer report. No stage report records this tag under either sanitizer (stage 1's verification is the release build): expect `6 assertions in 3 test cases` in both trees (stage 3's ASan run of `[kit-baseline],[dither]` was 352 assertions in 4 cases, and `[kit-baseline]` alone is 346 in 1, leaving 6 in 3 for `[dither]`), and report both counts.

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/test/audio/Dither.test.cpp packages/retroplug/test-native/kit-dither.test.ts \
  packages/native/cmake/tests.cmake packages/native/src/lsdj/Effects.cpp
git commit -F - <<'EOF'
fix(kit): dither is seeded from its input, so a kit compiles to the same bytes every time

Both TPDF dithers (HighPassTPDF, the default, and ShapedTPDF) seeded their std::mt19937 from
std::random_device, so compiling one kit twice gave two different files for the same inputs. They now seed it from
FNV-1a-64 (util/Hash.hpp, the hash SampleCache already uses) over the bytes of the buffer the dither is handed, fed
least-significant byte of each float first so a big-endian build seeds the same words, folded to mt19937's 32-bit
word and salted with a fixed constant per algorithm (FNV-1a of its name, not DitherType's ordinal, so renumbering the
enum cannot re-seed a kit), so the two dithers over one buffer do not run the same noise sequence. The TPDF draw is
now (mt19937() >> 8) * 2^-24 rather than std::uniform_real_distribution, whose algorithm the standard leaves to the
library: the noise is the same on any host, and only the resampler and the filters decide whether a whole bank agrees
with another host's, which is why exact kit bytes are still compared on linux-x64-gnu13. The three error-diffusion
algorithms carry no RNG and were already deterministic.

That buffer is the design's section 9 question, and reading KitUtil::compileSample answers it: the seed hashes the
WINDOWED, POST-EFFECT, RESAMPLED sample at the Game Boy kit rate (11468 Hz), as the dither receives it and before
frameAlign trims it to whole 32-sample frames, not the source-rate window the gain and filter see and not the trimmed
bank. Dither is the only effect deferred past the resample, so it is the resampled buffer that reaches it. Nothing
else in either algorithm moved: same shaping coefficients, same clamp and rounding, so the noise is still TPDF noise,
just drawn from a seed a compile can reproduce. The seed is a function of the buffer's contents and the algorithm
only, not of the bit depth.

Three cases pin it. test-native/kit-dither.test.ts compiles the same generated WAV with each TPDF dither twice
through audioDriver.compileKit (the CLI sessions' path: RPC, KitCompiler, LsdjKitCodec), reads the bank back through
KitView to prove the slot decoded to audible frames before comparing the 16 KB banks byte for byte (an empty slot
would otherwise agree with itself for the wrong reason); against the old seed it failed with the banks differing at
byte 97. test/audio/Dither.test.cpp calls the effect directly: the same buffer twice gives identical codes for both
algorithms, two buffers differing in one sample by a millionth (far below a 4-bit quantum) get different noise, which
a single fixed seed could not, and one case records the eight codes each algorithm gives a fixed silent buffer, which
pins the hash, the fold, the salt and the draw at once.

Parity is untouched (no parity case compiles a kit), and the other three dither algorithms are unchanged.
EOF
````

### Task 2: a byte baseline holds every kit the compilers build

Design section 3 and section 7 item 2, and the `[kit-baseline]` half of section 6. The suite runs today's kit path
(the `KitCompiler` plus `LsdjKitCodec` / `RisaDmcCodec`, not the RPC: the RPC adds only DTO mapping and needs an
`Engine` the kit path never touches) over a fixed matrix synthesised in the test, so it needs no fixtures and runs in
CI: sines, a chirp, seeded noise and silence at 22.05, 44.1 and 48 kHz, and a 600-frame source shorter than one
resampler chunk; for LSDj, no effect, gain, normalise (a silent source too), each of the eight filters, each of the
five dithers, rotate on and off, one filter-gain-dither chain and a short source; for risa, the DMC codec at five rate
indices, normalise off, loop on, a silent normalise at two rates and a short source. 31 cases: 21 LSDj banks of 16384
bytes and 10 risa banks of 8192. Each source reaches the compilers as a 32-bit float WAV, so `SampleCache` and
miniaudio stay in the loop, and the test asserts the decoded buffer equals the samples it computed: the golden pins the
compilers, not a quantisation of their input. From this task on, a change that moves a kit byte fails this suite.

**Files:**
- Create: `packages/native/src/util/BuildTarget.hpp`, `packages/native/test/audio/KitBaseline.test.cpp`, `packages/native/test/audio/golden/kit-baseline.json`
- Modify: `.github/workflows/build.yml`, `packages/native/cmake/tests.cmake`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/risa/RisaDmcCodec.cpp`, `packages/native/src/risa/RisaDmcCodec.hpp`

- [ ] **Step 1: Write the failing case**

`KitBaseline.test.cpp` (new, one `TEST_CASE`, tag `[kit-baseline]`, 546 lines) is the matrix and the golden's reader:
a `TempDir` that owns its WAVs, a case table of source and effect, the two compiles each case needs off the target
host, the vacuous-pass guards (every case compiles with `ok` and an empty `error`, the bank carries its sample into
slot 0, the golden lists exactly the matrix's names), and the target rules. It reads the golden from
`RP_KIT_GOLDEN_DIR`, calls `rp::buildTarget()`, and names `RisaDmcCodec::kBankSize` / `kIndexOffset` / `kIndexEntry`
to read a risa slot's length register; none of those exists yet, which is this step's red.

Create `packages/native/test/audio/KitBaseline.test.cpp`:

````cpp
// The kit compilers' byte baseline: every kit these compilers build is recorded as a bank hash, so a change that
// moves one byte of one kit fails here.
//
// Every case synthesises its source audio in this file, writes it to a temp dir as a 32-bit float WAV, and compiles
// it through the same objects the `compileKit` / `compileDmc` RPCs build - `rp::kit::KitCompiler` over
// `LsdjKitCodec` / `RisaDmcCodec`, with the source decoded from its path by `SampleCache` - so the baseline covers
// the real decode path (miniaudio) as well as the window, the effects, the r8brain resample and the pack. Nothing
// here reads a fixture: the matrix runs wherever the binary does.
//
// The matrix is fixed. Sources: two sine frequencies, a chirp, seeded noise (`std::mt19937`, a fixed seed) and
// silence, each at 22.05, 44.1 and 48 kHz, plus a 600-frame source shorter than one 1024-frame resampler chunk. LSDj
// cases: no effect, a non-unity gain with normalise off, the same gain with normalise on, that normalise over a
// silent source, each of the eight filter types, each of the five dither algorithms, rotate on and off, one chain of
// filter then gain then dither, and the short source on its own. risa cases: the DMC codec at five rate indices, with
// and without a pre-effect, with normalise off and with loop set, a normalise of a silent source, and the short source
// on its own.
//
// Exact bytes are compared only on the host the golden was recorded on (`linux-x64-gnu13`), as the engine's parity
// baseline does, because float arithmetic is bit-stable only for one compiler on one architecture. Elsewhere a case
// compiles twice and must agree with itself, and its bank size and slot length are still held to the golden.
// RP_KIT_GOLDEN_REQUIRE_TARGET=1 makes a foreign host fail instead of falling back, as RP_PARITY_REQUIRE_TARGET does
// for parity (CI's linux-x64 job sets that one, so exact checking there cannot fade out).
//
// RP_KIT_GOLDEN_UPDATE=1 re-records golden/kit-baseline.json from this tree, and only on the baseline's target: a
// recording refuses a case whose two compiles disagree, because such a case depends on something outside its input
// and its digest would mean nothing, and it refuses to write a file anywhere else, so a fallback digest can never be
// recorded. Without the variable the file is only ever read.
//
// Run via `pnpm test:plugin`, or one tag: `build/bin/retroplug-audio-test "[kit-baseline]"`.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rfl/json.hpp>

#include "kit/KitCompiler.hpp"
#include "lsdj/Effects.hpp"
#include "lsdj/LsdjKitCodec.hpp"
#include "lsdj/KitUtil.hpp"  // rp::lsdj::Kit, the LSDj bank layout
#include "risa/RisaDmcCodec.hpp"
#include "util/BuildTarget.hpp"
#include "util/Hash.hpp"

// A golden case's record: the bank's FNV-1a-64 in lowercase hex, the bank's size, and the byte length of slot 0
// within it. The size and the slot length are compared on every host (a bank layout is not float arithmetic); the
// hash only on the target it was recorded on. The slot length is the half that carries information off the target: a
// bank is a fixed size whatever happened, but a slot is as long as the resample and the pack made it, so a truncated
// resample on a foreign host moves it.
//
// These structs live in a NAMED namespace deliberately: reflect-cpp reflects a struct through a fake-object
// conversion operator, which clang refuses to define for a type with no linkage.
namespace kit_baseline {

struct BankGolden {
    std::string   hash;
    std::uint64_t size = 0;
    std::uint64_t slot = 0;
};

struct Golden {
    std::string                       target;
    std::map<std::string, BankGolden> cases;
};

} // namespace kit_baseline

namespace {

/** The host name the parity baseline's `PARITY_TARGET` uses, from `rp::buildTarget()` exactly as the engine's
 *  `buildTarget` RPC reports it, so a baseline recorded here and one recorded there name one toolchain. */
constexpr const char* kBaselineTarget = "linux-x64-gnu13";

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}

/** The source signals. Every one is used at every rate below (the short one only at 44.1 kHz, a rate whose resampler
 *  chunk it is shorter than), so a case that changed only one rate's source would not go untested. Silence is a
 *  source of its own because it is the only input that reaches the effects' "no peak to normalise by" branch. */
enum class Signal { Sine440, Sine1200, Chirp, Noise, Silence, Short };

struct SignalDesc {
    Signal      signal;
    const char* name;
    std::size_t frames;
};

constexpr SignalDesc kSignals[] = {
    {Signal::Sine440, "sine440", 8192},
    {Signal::Sine1200, "sine1200", 8192},
    {Signal::Chirp, "chirp", 8192},
    {Signal::Noise, "noise", 8192},
    {Signal::Silence, "silence", 8192},
    {Signal::Short, "short", 600},
};

constexpr std::uint32_t kRates[] = {22050, 44100, 48000};

constexpr double kPi = 3.14159265358979323846;

constexpr std::size_t signalFrames(Signal signal) {
    for (const SignalDesc& desc : kSignals) {
        if (desc.signal == signal) return desc.frames;
    }
    return 0;
}

std::vector<float> sine(double hz, std::uint32_t rate, std::size_t frames) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(rate);
        out[i] = 0.8f * static_cast<float>(std::sin(2.0 * kPi * hz * t));
    }
    return out;
}

/** A linear sweep from 200 Hz to 4000 Hz across the buffer, its phase integrated so the sweep stays continuous. */
std::vector<float> chirp(std::uint32_t rate, std::size_t frames) {
    std::vector<float> out(frames);
    double phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double f = 200.0 + (4000.0 - 200.0) * static_cast<double>(i) / static_cast<double>(frames);
        phase += 2.0 * kPi * f / static_cast<double>(rate);
        out[i] = 0.7f * static_cast<float>(std::sin(phase));
    }
    return out;
}

/** Uniform noise in [-0.6, 0.6) from raw `mt19937` output: the generator's sequence is specified and the draw is exact
 *  in binary32, so this source is the same on every host (a `uniform_real_distribution`'s is not specified). */
std::vector<float> noise(std::size_t frames) {
    std::mt19937 rng(0x5eed1234u);
    std::vector<float> out(frames);
    for (float& v : out) {
        const float unit = static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
        v = 1.2f * unit - 0.6f;
    }
    return out;
}

std::vector<float> signalSamples(Signal signal, std::uint32_t rate) {
    const std::size_t frames = signalFrames(signal);
    switch (signal) {
    case Signal::Sine440:  return sine(440.0, rate, frames);
    case Signal::Sine1200: return sine(1200.0, rate, frames);
    case Signal::Chirp:    return chirp(rate, frames);
    case Signal::Noise:    return noise(frames);
    case Signal::Silence:  return std::vector<float>(frames, 0.0f);
    case Signal::Short:    return sine(440.0, rate, frames);
    }
    return {};
}

const char* signalName(Signal signal) {
    for (const SignalDesc& desc : kSignals) {
        if (desc.signal == signal) return desc.name;
    }
    return "";
}

/**
 * A 32-bit float, mono WAV (fmt tag 3). Float rather than 16-bit PCM because the sources are synthesised in float and
 * miniaudio decodes a float WAV straight into `ma_format_f32`, so the samples the compilers see are exactly the ones
 * this test computed: a 16-bit source would quantise them before the compilers ever ran, and the golden's job is to
 * pin the DSP on known values. Every field is written byte by byte, little-endian, so a big-endian host writes the
 * same file.
 */
void writeFloatWav(const std::filesystem::path& path, const std::vector<float>& samples, std::uint32_t rate) {
    std::vector<std::uint8_t> bytes;
    const auto u16 = [&](std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(v >> shift));
    };
    const auto tag = [&](std::string_view s) { bytes.insert(bytes.end(), s.begin(), s.end()); };

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    tag("RIFF");
    u32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    u32(16);               // fmt chunk size
    u16(3);                // IEEE float
    u16(1);                // mono
    u32(rate);
    u32(rate * 4);         // byte rate
    u16(4);                // block align
    u16(32);               // bits per sample
    tag("data");
    u32(dataBytes);
    for (const float v : samples) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good()); // a failed write would otherwise surface as "cannot decode" from the cache below
}

enum class KitCodec { Lsdj, Risa };

/** One matrix entry. `rate` is the source's own rate; `dmcRate` is risa's PAL DPCM rate index, unused by LSDj;
 *  `normalize` and `loop` are risa's DMC encode flags, unused by LSDj (LSDj normalises through a `GainEffect`). */
struct KitCase {
    const char*                       name;
    KitCodec                          codec;
    Signal                            signal;
    std::uint32_t                     rate;
    std::vector<rp::lsdj::LsdjEffect> effects;
    bool                              rotate    = true;
    std::uint8_t                      dmcRate   = 12;
    bool                              normalize = true;
    bool                              loop      = false;
};

rp::lsdj::LsdjEffect gainEffect(float gain, bool normalize) {
    return rp::lsdj::GainEffect{normalize, gain};
}

rp::lsdj::LsdjEffect filterEffect(rp::lsdj::FilterType type, float frequency, float q, float gainDb = 0.0f) {
    return rp::lsdj::FilterEffect{type, frequency, q, gainDb};
}

rp::lsdj::LsdjEffect ditherEffect(rp::lsdj::DitherType type) {
    return rp::lsdj::DitherEffect{type};
}

/** The matrix, in the order this file lists it (the golden's map sorts by name). Filters run at the source rate,
 *  before the resample, so their frequencies are the source's; every one is below 11.025 kHz, valid at all rates. */
std::vector<KitCase> kitCases() {
    using rp::lsdj::DitherType;
    using rp::lsdj::FilterType;

    return {
        {"lsdj/no-effect", KitCodec::Lsdj, Signal::Sine440, 44100, {}},
        {"lsdj/gain", KitCodec::Lsdj, Signal::Sine440, 44100, {gainEffect(0.37f, false)}},
        {"lsdj/normalize", KitCodec::Lsdj, Signal::Sine440, 44100, {gainEffect(0.37f, true)}},
        // A silent source is the only input that reaches `GainEffect`'s "maxAmp > 0" guard, the one division in the
        // effect: a non-silent source never takes that branch.
        {"lsdj/normalize-silence", KitCodec::Lsdj, Signal::Silence, 44100, {gainEffect(0.37f, true)}},
        {"lsdj/filter/low-pass", KitCodec::Lsdj, Signal::Sine440, 44100,
         {filterEffect(FilterType::LowPass, 2000.0f, 0.9f)}},
        {"lsdj/filter/high-pass", KitCodec::Lsdj, Signal::Sine440, 22050,
         {filterEffect(FilterType::HighPass, 800.0f, 1.4f)}},
        {"lsdj/filter/band-pass", KitCodec::Lsdj, Signal::Sine1200, 48000,
         {filterEffect(FilterType::BandPass, 1200.0f, 2.0f)}},
        {"lsdj/filter/band-stop", KitCodec::Lsdj, Signal::Chirp, 48000,
         {filterEffect(FilterType::BandStop, 4000.0f, 0.7f)}},
        {"lsdj/filter/peak", KitCodec::Lsdj, Signal::Sine1200, 44100,
         {filterEffect(FilterType::Peak, 1000.0f, 1.0f, 6.0f)}},
        {"lsdj/filter/low-shelf", KitCodec::Lsdj, Signal::Noise, 22050,
         {filterEffect(FilterType::LowShelf, 800.0f, 0.7f, -6.0f)}},
        {"lsdj/filter/high-shelf", KitCodec::Lsdj, Signal::Chirp, 22050,
         {filterEffect(FilterType::HighShelf, 3000.0f, 0.7f, 4.0f)}},
        {"lsdj/filter/all-pass", KitCodec::Lsdj, Signal::Noise, 48000,
         {filterEffect(FilterType::AllPass, 1500.0f, 1.2f)}},
        {"lsdj/dither/high-pass-tpdf", KitCodec::Lsdj, Signal::Sine1200, 22050,
         {ditherEffect(DitherType::HighPassTPDF)}},
        {"lsdj/dither/shaped-tpdf", KitCodec::Lsdj, Signal::Noise, 44100,
         {ditherEffect(DitherType::ShapedTPDF)}},
        {"lsdj/dither/error-diffusion", KitCodec::Lsdj, Signal::Chirp, 44100,
         {ditherEffect(DitherType::ErrorDiffusion)}},
        {"lsdj/dither/jjn", KitCodec::Lsdj, Signal::Sine440, 48000,
         {ditherEffect(DitherType::JJN)}},
        {"lsdj/dither/sierra-lite", KitCodec::Lsdj, Signal::Sine1200, 44100,
         {ditherEffect(DitherType::SierraLite)}},
        // The pair differs in `rotate` alone, so a rotate that stopped packing (or stopped round-tripping) moves one
        // of the two banks and not the other.
        {"lsdj/rotate-off", KitCodec::Lsdj, Signal::Chirp, 44100, {}, false},
        {"lsdj/rotate-on", KitCodec::Lsdj, Signal::Chirp, 44100, {}, true},
        {"lsdj/chain/filter-gain-dither", KitCodec::Lsdj, Signal::Noise, 22050,
         {filterEffect(FilterType::LowPass, 3000.0f, 1.0f), gainEffect(0.5f, false),
          ditherEffect(DitherType::HighPassTPDF)}},
        {"lsdj/short-source", KitCodec::Lsdj, Signal::Short, 44100, {}},

        {"risa/no-effect-rate-4", KitCodec::Risa, Signal::Sine440, 44100, {}, true, 4},
        {"risa/no-effect-rate-12", KitCodec::Risa, Signal::Chirp, 48000, {}, true, 12},
        {"risa/no-effect-rate-15", KitCodec::Risa, Signal::Noise, 22050, {}, true, 15},
        {"risa/gain-rate-10", KitCodec::Risa, Signal::Sine1200, 44100, {gainEffect(0.4f, false)}, true, 10},
        {"risa/filter-rate-8", KitCodec::Risa, Signal::Noise, 48000,
         {filterEffect(FilterType::LowPass, 3000.0f, 1.0f)}, true, 8},
        {"risa/short-source-rate-12", KitCodec::Risa, Signal::Short, 44100, {}, true, 12},
        // The DMC encode's other two branches, reachable from the RPC (`normalize.value_or(true)`,
        // `loop.value_or(false)`): the non-normalising 7-bit map and the loop flag in the index entry's byte 3.
        {"risa/no-normalize-rate-12", KitCodec::Risa, Signal::Noise, 44100, {}, true, 12, false},
        {"risa/loop-rate-4", KitCodec::Risa, Signal::Sine440, 44100, {}, true, 4, true, true},
        // A silent source pins `convertF32ToDpcm`'s peak floor of 1 at both remaining rates, and with it the
        // no-peak branch of the DMC normalise.
        {"risa/normalize-silence-rate-12", KitCodec::Risa, Signal::Silence, 22050, {}, true, 12},
        {"risa/normalize-silence-rate-4", KitCodec::Risa, Signal::Silence, 48000, {}, true, 4},
    };
}

/** One source file, by signal and rate, as it was written and as it should decode. */
struct SourceFile {
    std::filesystem::path    path;
    std::uint32_t            rate = 0;
    std::vector<float>       samples;
};

std::string sourceKey(Signal signal, std::uint32_t rate) {
    return std::string(signalName(signal)) + "-" + std::to_string(rate);
}

/** A directory of its own for the run's WAVs, removed on the way out (including on a failed assertion). The name is
 *  drawn from `std::random_device` so two runs sharing a temp dir cannot write over each other's sources. */
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        std::mt19937 rng(std::random_device{}());
        path = std::filesystem::temp_directory_path() / ("rp-kit-baseline-" + std::to_string(rng()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/** Compile one case the way its RPC does: build the codec with a single slot, then hand it to a shared compiler. The
 *  compiler owns the `SampleCache`, so a case compiled twice decodes once; `SampleCache` keys on the path and both of
 *  a case's compiles pass the same one. */
rp::kit::CompiledKit compileCase(rp::kit::KitCompiler& compiler, const KitCase& c,
                                 const SourceFile& source) {
    if (c.codec == KitCodec::Lsdj) {
        rp::lsdj::CompileSampleSpec slot;
        slot.path    = source.path.string();
        slot.name    = "KIK";
        slot.effects = c.effects;
        slot.rotate  = c.rotate;
        const rp::lsdj::LsdjKitCodec codec("BASELINE", {slot});
        return compiler.compile(codec);
    }

    rp::risa::CompileDmcSampleSpec slot;
    slot.path      = source.path.string();
    slot.name      = "KIK";
    slot.effects   = c.effects;
    slot.rate      = c.dmcRate;
    slot.normalize = c.normalize;
    slot.loop      = c.loop;
    const rp::risa::RisaDmcCodec codec("RISA", {slot});
    return compiler.compile(codec);
}

/** Whether the bank carries the case's sample in slot 0. A case whose decode or resample failed leaves the slot empty,
 *  and an empty slot is a stable bank, so without this a broken case would pin nothing and still pass. */
bool bankHasSlot(const KitCase& c, const std::vector<std::uint8_t>& bank) {
    if (c.codec == KitCodec::Lsdj) {
        // The offset table's first two little-endian u16 entries: offsets[0] is the sample-data start, offsets[1] the
        // end of slot 0. A slot with no sample gets neither.
        if (bank.size() != rp::lsdj::Kit::kSize) return false;
        const auto entry = [&](std::size_t i) {
            return static_cast<std::uint16_t>(bank[i * 2] | (bank[i * 2 + 1] << 8));
        };
        return entry(0) == rp::lsdj::Kit::kBankBase + rp::lsdj::Kit::kSampleDataOffset && entry(1) > entry(0);
    }
    // The risa bank's slot-0 index entry: its address byte is 0xFF when the slot is empty.
    using rp::risa::RisaDmcCodec;
    return bank.size() == RisaDmcCodec::kBankSize && bank[RisaDmcCodec::kIndexOffset] != 0xFF;
}

/** Slot 0's byte length in an assembled bank, as the bank itself records it: LSDj's offset table gives the start and
 *  end of the slot's packed nibbles, and risa's index entry gives its length register (a count of 16-byte steps, the
 *  last one partial). Unlike the bank size this is not a constant: it follows the resample and the pack. */
std::uint64_t slotLength(const KitCase& c, const std::vector<std::uint8_t>& bank) {
    if (c.codec == KitCodec::Lsdj) {
        const auto entry = [&](std::size_t i) {
            return static_cast<std::uint16_t>(bank[i * 2] | (bank[i * 2 + 1] << 8));
        };
        return static_cast<std::uint64_t>(entry(1) - entry(0));
    }
    using rp::risa::RisaDmcCodec;
    const std::uint8_t lengthReg = bank[RisaDmcCodec::kIndexOffset + 1];
    return static_cast<std::uint64_t>(lengthReg + 1) * 16;
}

std::string hexHash(std::uint64_t hash) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

void writeGolden(const std::filesystem::path& path, const kit_baseline::Golden& golden) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n  \"target\": \"" << golden.target << "\",\n  \"cases\": {\n";
    std::size_t written = 0;
    for (const auto& [name, bank] : golden.cases) {
        out << "    \"" << name << "\": { \"hash\": \"" << bank.hash << "\", \"size\": " << bank.size
            << ", \"slot\": " << bank.slot << " }";
        out << (++written < golden.cases.size() ? ",\n" : "\n");
    }
    out << "  }\n}\n";
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text;
    if (!in) return text;
    in.seekg(0, std::ios::end);
    text.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    return text;
}

} // namespace

TEST_CASE("every kit case compiles to the bank the golden records", "[kit-baseline]") {
    const auto cases = kitCases();
    const std::filesystem::path goldenPath = std::filesystem::path(RP_KIT_GOLDEN_DIR) / "kit-baseline.json";
    const std::string host = rp::buildTarget();
    const bool onTarget = host == kBaselineTarget;
    const bool update = envFlag("RP_KIT_GOLDEN_UPDATE");
    const bool requireTarget = envFlag("RP_KIT_GOLDEN_REQUIRE_TARGET");

    static_assert(signalFrames(Signal::Short) < 1024, "the short source must be shorter than one resampler chunk");

    TempDir dir;

    // Every (signal, rate) pair a case names is written once, and every signal is used at every rate: the matrix's
    // claim about its own coverage, checked rather than trusted.
    std::map<std::string, SourceFile> sources;
    for (const KitCase& c : cases) {
        const std::string key = sourceKey(c.signal, c.rate);
        if (sources.find(key) != sources.end()) continue;
        SourceFile source;
        source.rate    = c.rate;
        source.samples = signalSamples(c.signal, c.rate);
        source.path    = dir.path / (key + ".wav");
        writeFloatWav(source.path, source.samples, source.rate);
        sources.emplace(key, std::move(source));
    }
    for (const SignalDesc& desc : kSignals) {
        for (const std::uint32_t rate : kRates) {
            if (desc.signal == Signal::Short && rate != 44100) continue;
            INFO("signal " << desc.name << " at " << rate << " Hz");
            CHECK(sources.count(sourceKey(desc.signal, rate)) == 1);
        }
    }

    // The decode step is part of what the baseline covers, so hold it to the files written above: the same rate, the
    // same frame count, and the same samples. The cache decodes each path once, and every case's compile below reuses
    // this entry, so a WAV miniaudio read differently would show up here before a bank hash is even reached.
    rp::kit::KitCompiler compiler;
    for (const auto& [key, source] : sources) {
        INFO("source " << key);
        const rp::kit::SampleData* data = compiler.cache().getOrLoad(source.path.string());
        REQUIRE(data != nullptr);
        CHECK(data->sampleRate == source.rate);
        CHECK(data->buffer.size() == source.samples.size());
        CHECK(data->buffer == source.samples);
    }

    if (update) {
        // A digest recorded on any other host would be a self-agreement fallback wearing the golden's clothes, so a
        // recording refuses to happen off the target rather than merely warning.
        if (!onTarget) {
            FAIL("a recording only happens on " << kBaselineTarget << ", and this host is " << host
                                               << "; a digest recorded here would be a fallback, not the baseline");
        }
        kit_baseline::Golden recorded{kBaselineTarget, {}};
        for (const KitCase& c : cases) {
            INFO("case " << c.name);
            const auto first = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            REQUIRE(first.ok);
            REQUIRE(first.error.empty());
            REQUIRE(bankHasSlot(c, first.bytes));
            const auto second = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            // REQUIRE, not CHECK: a case whose two compiles disagree depends on something outside its input, so its
            // digest would mean nothing and writing it (let alone re-recording a different one next time) would put
            // a random bank in the commit. Aborting here leaves the file untouched.
            REQUIRE(second.bytes == first.bytes);
            recorded.cases[c.name] = {hexHash(first.hash), first.bytes.size(), slotLength(c, first.bytes)};
        }
        writeGolden(goldenPath, recorded);
        SUCCEED("recorded " << recorded.cases.size() << " banks to " << goldenPath.string());
        return;
    }

    const std::string text = readFile(goldenPath);
    const auto parsed = rfl::json::read<kit_baseline::Golden>(text);
    if (!parsed.has_value()) {
        FAIL("cannot read " << goldenPath.string() << " (" << parsed.error().what()
                            << "); record it with RP_KIT_GOLDEN_UPDATE=1 on " << kBaselineTarget);
    }
    const kit_baseline::Golden golden = *parsed;
    CHECK(golden.target == kBaselineTarget);
    // A renamed or dropped case must be re-recorded, not silently skipped: the golden holds exactly this matrix.
    REQUIRE(golden.cases.size() == cases.size());

    if (requireTarget && !onTarget) {
        FAIL("this host is " << host << " but the baseline was recorded on " << golden.target
                             << ", so exact kit bytes would not be compared; run on " << golden.target
                             << " (or re-record the baseline there and move kBaselineTarget)");
    }

    for (const KitCase& c : cases) {
        INFO("case " << c.name << " on " << host);
        const auto kit = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
        // These three hold `KitCompiler`'s contract, not the case's: `compile` sets `ok` unconditionally and computes
        // its hash as the FNV of the bytes it returns (the dirty-tracking hash the RPC throws away), so they fail
        // only if that contract changes, and the check keeps the golden a hash of the bank and nothing else.
        // `bankHasSlot` below is the case's own vacuous-pass guard.
        REQUIRE(kit.ok);
        REQUIRE(kit.error.empty());
        CHECK(kit.hash == rp::hash::fnv1a64(kit.bytes.data(), kit.bytes.size()));
        REQUIRE(bankHasSlot(c, kit.bytes));

        const auto expected = golden.cases.find(c.name);
        REQUIRE(expected != golden.cases.end());
        CHECK(expected->second.size == kit.bytes.size());
        CHECK(expected->second.slot == slotLength(c, kit.bytes));
        if (onTarget) {
            CHECK(expected->second.hash == hexHash(kit.hash));
        } else {
            // Off the target only self-agreement is meaningful; the recorded digest is not comparable, but the case
            // still has to be reproducible from its input alone.
            const auto again = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            CHECK(again.hash == kit.hash);
            CHECK(again.bytes == kit.bytes);
        }
    }
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index e57b02b94..5d2ff0b20 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -69,13 +69,15 @@ retroplug_add_test_binary(
              test/audio/SameBoyModels.test.cpp         # every SameBoyModel (incl. SGB variants) boots + renders
              test/audio/EngineInvoker.test.cpp         # the teardown discards a staged-message payload
              test/audio/Dither.test.cpp                # the kit TPDF dithers are seeded from their input
+             test/audio/KitBaseline.test.cpp           # every kit the compilers build is held to its recorded bank hash
              test/graph/CountingAllocator.cpp          # replaces operator new / delete: the N8 FIFO's [alloc] case
     LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
              RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
-             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")
+             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg"
+             RP_KIT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/audio/golden")
 
 # Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md,
 # -c2-design.md): each system node renders exactly what its core renders driven directly, its inputs and its settings'
````

Run: `sed -n '1258,1278p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds the source and the `RP_KIT_GOLDEN_DIR` definition to `retroplug-audio-test`.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: the build stops at the first missing header, `util/BuildTarget.hpp`; with that placed, `RisaDmcCodec` has no `kBankSize` / `kIndexOffset` / `kIndexEntry` next, so Catch2 never runs. No stage report records this red (stage 2's recorded reds are the tamper and the foreign-target checks below); report the compiler's lines.

- [ ] **Step 2: Move the build target out of the RPC and open risa's bank layout**

`util/BuildTarget.hpp` (new) holds `rp::buildTarget()`, the `"<os>-<arch>-<compiler><major>"` string read from the
compiler's own macros, and `EngineRpcService::buildTarget()` returns it, so the golden's `target` and the RPC cannot
drift. `RisaDmcCodec` gains public `static constexpr` `kBankSize`, `kIndexOffset` and `kIndexEntry` (the assembler's
own constants, moved out of the anonymous namespace), so a bank reader names the offsets its assembler writes.

Create `packages/native/src/util/BuildTarget.hpp`:

````cpp
#pragma once

#include <string>

// The host this translation unit was compiled for, "<os>-<arch>-<compiler><major>", read from the compiler's own
// macros: a CMake variable would name the configure host, so each slice of a universal binary names itself.
//
// The engine's `buildTarget` RPC and the kit baseline's golden both key on this string, so they share one spelling:
// a golden recorded on one host and a check run on another must name the two toolchains the same way to be
// comparable. On Windows the SameBoy core is compiled by clang-cl (cmake/sameboy.cmake), whose version this key does
// not carry; that would matter only if a Windows baseline were ever recorded.
namespace rp {

inline std::string buildTarget() {
#if defined(__linux__)
    const std::string os = "linux";
#elif defined(__APPLE__)
    const std::string os = "darwin";
#elif defined(_WIN32)
    const std::string os = "windows";
#else
    const std::string os = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    const std::string arch = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::string arch = "arm64";
#else
    const std::string arch = "unknown";
#endif
#if defined(__clang__) && defined(__apple_build_version__)
    const std::string cc = "appleclang" + std::to_string(__clang_major__);
#elif defined(__clang__)
    const std::string cc = "clang" + std::to_string(__clang_major__);
#elif defined(_MSC_VER)
    const std::string cc = "msvc" + std::to_string(_MSC_VER);  // the full number: MSVC 14.44 and 14.51 differ
#elif defined(__GNUC__)
    const std::string cc = "gnu" + std::to_string(__GNUC__);
#else
    const std::string cc = "unknown";
#endif
    return os + "-" + arch + "-" + cc;
}

} // namespace rp
````

````diff
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 7fda1d3bd..aa11311b3 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -18,6 +18,7 @@
 #include "kit/KitCompiler.hpp"       // rp::kit::KitCompiler (generic, compileKit) - heavy, so only in this TU
 #include "lsdj/LsdjKitCodec.hpp"     // rp::lsdj::LsdjKitCodec (the LSDJ nibble/16KB-bank codec)
 #include "risa/RisaDmcCodec.hpp"     // rp::risa::RisaDmcCodec (the risa NES-DPCM/8KB-bank codec)
+#include "util/BuildTarget.hpp"      // rp::buildTarget (shared with the kit baseline's golden)
 #include "util/SlurpFile.hpp"
 
 namespace {
@@ -583,49 +584,7 @@ RpcRenderBlocksResult EngineRpcService::renderBlocks(RpcRenderBlocksRequest req)
 
 std::uint32_t EngineRpcService::droppedMessages() const { return engine_.droppedMessages(); }
 
-// "<os>-<arch>-<compiler><major>", from the compiler's own macros so each slice of a universal binary
-// names itself (a CMake variable would name the configure host).
-#define RP_STR2(x) #x
-#define RP_STR(x) RP_STR2(x)
-#if defined(__linux__)
-#define RP_TARGET_OS "linux"
-#elif defined(__APPLE__)
-#define RP_TARGET_OS "darwin"
-#elif defined(_WIN32)
-#define RP_TARGET_OS "windows"
-#else
-#define RP_TARGET_OS "unknown"
-#endif
-#if defined(__x86_64__) || defined(_M_X64)
-#define RP_TARGET_ARCH "x64"
-#elif defined(__aarch64__) || defined(_M_ARM64)
-#define RP_TARGET_ARCH "arm64"
-#else
-#define RP_TARGET_ARCH "unknown"
-#endif
-#if defined(__clang__) && defined(__apple_build_version__)
-#define RP_TARGET_CC "appleclang" RP_STR(__clang_major__)
-#elif defined(__clang__)
-#define RP_TARGET_CC "clang" RP_STR(__clang_major__)
-#elif defined(_MSC_VER)
-#define RP_TARGET_CC "msvc" RP_STR(_MSC_VER)  // the full number: MSVC 14.44 and 14.51 differ
-#elif defined(__GNUC__)
-#define RP_TARGET_CC "gnu" RP_STR(__GNUC__)
-#else
-#define RP_TARGET_CC "unknown"
-#endif
-
-std::string EngineRpcService::buildTarget() const {
-    return RP_TARGET_OS "-" RP_TARGET_ARCH "-" RP_TARGET_CC;
-}
-
-// On Windows the SameBoy core is compiled by clang-cl (cmake/sameboy.cmake), whose version this key does not
-// carry; that would matter only if a Windows baseline were ever recorded.
-#undef RP_STR2
-#undef RP_STR
-#undef RP_TARGET_OS
-#undef RP_TARGET_ARCH
-#undef RP_TARGET_CC
+std::string EngineRpcService::buildTarget() const { return rp::buildTarget(); }
 
 // Direct engine_ access (not the invoker): the benchmark drives the synchronous renderAudio pull path
 // and never startAudio, so the control thread owns the Engine here — same regime as readState.
diff --git a/packages/native/src/risa/RisaDmcCodec.cpp b/packages/native/src/risa/RisaDmcCodec.cpp
index 5a90ca24d..2e683227f 100644
--- a/packages/native/src/risa/RisaDmcCodec.cpp
+++ b/packages/native/src/risa/RisaDmcCodec.cpp
@@ -12,15 +12,13 @@ namespace rp::risa {
 namespace {
 
 // --- DMC / kit-bank constants (tools/rom_patcher/src/kit_editor/constants.js) ---
-constexpr std::size_t kBankSize        = 0x2000; // 8 KB
+// The bank size and the index table's offset live on RisaDmcCodec, so a bank reader and this assembler share them.
 constexpr std::size_t kSampleRegion    = 0x1EC0; // 7872 B for packed DPCM
 constexpr std::size_t kNameOffset      = 0x1EC0;
 constexpr std::size_t kNameSize        = 16;
 constexpr std::size_t kNameMaxChars    = 6;
 constexpr std::size_t kSampleNames     = 0x1ED0;
 constexpr std::size_t kSampleNameLen   = 3;
-constexpr std::size_t kIndexOffset     = 0x1F00;
-constexpr std::size_t kIndexEntry      = 4;
 constexpr std::size_t kSlotCount       = 16;
 constexpr std::size_t kMagicOffset     = 0x1F40;
 constexpr std::uint8_t kMagic          = 0xA5;
diff --git a/packages/native/src/risa/RisaDmcCodec.hpp b/packages/native/src/risa/RisaDmcCodec.hpp
index 71221dcd7..b28f331ef 100644
--- a/packages/native/src/risa/RisaDmcCodec.hpp
+++ b/packages/native/src/risa/RisaDmcCodec.hpp
@@ -35,6 +35,12 @@ struct CompileDmcSampleSpec {
 
 class RisaDmcCodec final : public rp::kit::KitCodec {
 public:
+    // The bank layout this codec assembles, exposed beside it so a reader of a compiled bank names the same offsets
+    // the assembler writes: 16 slots, each with a 4-byte index entry [addr, length-reg, rate, flags].
+    static constexpr std::size_t kBankSize    = 0x2000; // 8 KB
+    static constexpr std::size_t kIndexOffset = 0x1F00;
+    static constexpr std::size_t kIndexEntry  = 4;
+
     RisaDmcCodec(std::string kitName, std::vector<CompileDmcSampleSpec> samples)
         : kitName_(std::move(kitName)), samples_(std::move(samples)) {}
 
````

Run: `sed -n '1348,1448p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: the build and link succeed; the case then fails because `test/audio/golden/kit-baseline.json` does not exist (it reports the path it tried to read). Report the line this tree prints.

- [ ] **Step 3: Record the golden, here and nowhere else**

The golden is 31 entries of one line each plus a `target` line, 2799 bytes, so it is quoted whole rather than
generated by a tool: it is small enough to review and to `git apply`, unlike C5's megabyte documents.
`.github/workflows/build.yml` sets `RP_KIT_GOLDEN_REQUIRE_TARGET` on the linux-x64 job's existing Catch2 step only, so
a toolchain move cannot silently turn exact checking into the fallback, as `RP_PARITY_REQUIRE_TARGET` guards parity.
The recording here is a check: the recorder must write these same bytes.

Create `packages/native/test/audio/golden/kit-baseline.json`:

````json
{
  "target": "linux-x64-gnu13",
  "cases": {
    "lsdj/chain/filter-gain-dither": { "hash": "478f61ee5aa3ca28", "size": 16384, "slot": 2128 },
    "lsdj/dither/error-diffusion": { "hash": "bff639bd204ffd83", "size": 16384, "slot": 1056 },
    "lsdj/dither/high-pass-tpdf": { "hash": "6100bd63e3b5b855", "size": 16384, "slot": 2128 },
    "lsdj/dither/jjn": { "hash": "e58571c28e84e123", "size": 16384, "slot": 976 },
    "lsdj/dither/shaped-tpdf": { "hash": "bd6ae99c29370911", "size": 16384, "slot": 1056 },
    "lsdj/dither/sierra-lite": { "hash": "bb56f9bb4ff53895", "size": 16384, "slot": 1056 },
    "lsdj/filter/all-pass": { "hash": "f876abf985a17d7c", "size": 16384, "slot": 976 },
    "lsdj/filter/band-pass": { "hash": "be27e6b79894122b", "size": 16384, "slot": 976 },
    "lsdj/filter/band-stop": { "hash": "c41844fd5d6a3094", "size": 16384, "slot": 976 },
    "lsdj/filter/high-pass": { "hash": "deaaf53fe0b2b70e", "size": 16384, "slot": 2128 },
    "lsdj/filter/high-shelf": { "hash": "666f82e029080d3f", "size": 16384, "slot": 2128 },
    "lsdj/filter/low-pass": { "hash": "e5bd608222c28027", "size": 16384, "slot": 1056 },
    "lsdj/filter/low-shelf": { "hash": "26cb3f8000796f86", "size": 16384, "slot": 2128 },
    "lsdj/filter/peak": { "hash": "4e20af4721c4bb74", "size": 16384, "slot": 1056 },
    "lsdj/gain": { "hash": "17b1acaa70881e83", "size": 16384, "slot": 1056 },
    "lsdj/no-effect": { "hash": "ad978610b16d72a4", "size": 16384, "slot": 1056 },
    "lsdj/normalize": { "hash": "62335f096ad12a03", "size": 16384, "slot": 1056 },
    "lsdj/normalize-silence": { "hash": "258e16094d430c84", "size": 16384, "slot": 1056 },
    "lsdj/rotate-off": { "hash": "dbdbec83c10ef215", "size": 16384, "slot": 1056 },
    "lsdj/rotate-on": { "hash": "e692c7cb87c64887", "size": 16384, "slot": 1056 },
    "lsdj/short-source": { "hash": "d53ebc650e589004", "size": 16384, "slot": 64 },
    "risa/filter-rate-8": { "hash": "ea46d3533e71797d", "size": 8192, "slot": 224 },
    "risa/gain-rate-10": { "hash": "a598d2590bf9306e", "size": 8192, "slot": 320 },
    "risa/loop-rate-4": { "hash": "8c53bcce60861b79", "size": 8192, "slot": 160 },
    "risa/no-effect-rate-12": { "hash": "2574fa68a51fafc0", "size": 8192, "slot": 384 },
    "risa/no-effect-rate-15": { "hash": "44390ac562c28ed1", "size": 8192, "slot": 1568 },
    "risa/no-effect-rate-4": { "hash": "083548d2f2a7497a", "size": 8192, "slot": 160 },
    "risa/no-normalize-rate-12": { "hash": "919fac26b94094ea", "size": 8192, "slot": 416 },
    "risa/normalize-silence-rate-12": { "hash": "492a6c104578b10f", "size": 8192, "slot": 816 },
    "risa/normalize-silence-rate-4": { "hash": "519a7d01915f2a38", "size": 8192, "slot": 144 },
    "risa/short-source-rate-12": { "hash": "205a41e6da035559", "size": 8192, "slot": 48 }
  }
}
````

````diff
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index 2d8f95eb4..cd9f013bb 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -87,6 +87,11 @@ jobs:
         run: pnpm test:native
 
       - name: Plugin unit tests (Catch2)
+        # The kit baseline compares exact bank bytes only on the host it recorded them on, so this job demands that
+        # this is that host, as the parity step above demands it of the audio baseline; the arm64, macOS and Windows
+        # jobs run the same step without it and fall back to each case agreeing with itself.
+        env:
+          RP_KIT_GOLDEN_REQUIRE_TARGET: "1"
         run: pnpm test:plugin
 
       - name: UI tests (headless LVGL)
````

Run: `sed -n '1507,1522p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (346 assertions in 1 test case)`: 31 banks, one case.

Run: `RP_KIT_GOLDEN_UPDATE=1 build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: exit 0: the recorder writes the same `target` and 31 cases over the file, in name order. It refuses a case whose two compiles disagree (`REQUIRE(second.bytes == first.bytes)` before any write) and refuses a foreign host with `a recording only happens on linux-x64-gnu13`.

Run: `git add -- packages/native/test/audio/golden/kit-baseline.json && git diff --cached --stat b1f7c288 -- packages/native/test/audio/golden/kit-baseline.json`
Expected: nothing printed: the recorder's file is the prototype's byte for byte, which is what makes the quoted block above the file this tree records.

- [ ] **Step 4: Check the fallback and its flags**

The suite's other half is what happens off the reference host, and it can only be checked by pretending to be there.
Save the two files first, since the mutations are temporary: the golden's tamper and the test's target constant are
each restored from `/tmp` and the tree rebuilt before the commit.

Run: `cp packages/native/test/audio/golden/kit-baseline.json /tmp/d1-kit-golden.json && cp packages/native/src/util/BuildTarget.hpp /tmp/d1-buildtarget.hpp`
Expected: nothing printed (both files saved for the restores below).

Run: `sed -i '0,/6100bd63e3b5b855/s//7100bd63e3b5b855/' packages/native/test/audio/golden/kit-baseline.json && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: exit 42: one case fails with `CHECK( expected->second.hash == hexHash(kit.hash) )`, naming `lsdj/dither/high-pass-tpdf`'s hash, so a bank that moves is this suite's failure.

Run: `cp /tmp/d1-kit-golden.json packages/native/test/audio/golden/kit-baseline.json`
Expected: nothing printed (the golden is restored before the next check).

Run: `sed -i 's|    return os + "-" + arch + "-" + cc;|    return "foreign-host";|' packages/native/src/util/BuildTarget.hpp && cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: the case passes off the target host: the golden's `target` still names the constant, but `rp::buildTarget()` no longer matches it, so each case compiles twice and must agree with itself while its `size` and `slot` are still held. Report the assertion count (it is not 346: the off-target branch checks self-agreement instead of the recorded hash, and adds a second compile's checks).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: exit 42: `this host is ... but the baseline was recorded on ...`, the `FAIL` the flag exists for.

Run: `RP_KIT_GOLDEN_UPDATE=1 build/bin/retroplug-audio-test "[kit-baseline]" || true; git diff --quiet packages/native/test/audio/golden/kit-baseline.json`
Expected: the recording refuses with `a recording only happens on linux-x64-gnu13`, and the golden is unchanged by the attempt (`git diff --quiet` exits 0, so nothing was written).

Run: `cp /tmp/d1-buildtarget.hpp packages/native/src/util/BuildTarget.hpp && cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: the restored tree is back on the target: `All tests passed (346 assertions in 1 test case)`, byte for byte.

- [ ] **Step 5: The baseline's own repeated runs**

Run: `build/bin/retroplug-audio-test "[kit-baseline]" && build/bin/retroplug-audio-test "[kit-baseline]" && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: three fresh processes, each `All tests passed (346 assertions in 1 test case)`: nothing in the kit path still varies per process, the TPDF cases included.

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[kit-baseline]"`
Expected: exit 0, `All tests passed (346 assertions in 1 test case)`, no AddressSanitizer or LeakSanitizer report: the ASan build's codegen agrees with the release build's on this target, hash for hash.

- [ ] **Step 6: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 with no line matching `warning` or `error` (the report's `0` is that grep's count: `pnpm build | grep -ciE 'warning|error'`).

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. `retroplug-audio-test` `All tests passed (1235181 assertions in 109 test cases)` (Task 1's 1234835 / 108 plus this suite); `retroplug-graph-test` `92566 / 208`; engine-graph without fixtures `test cases: 267 | 258 passed | 9 skipped`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/render-blocks`
Expected: exit 0, `8 passed`: the shared `buildTarget` RPC still reports this host, so the golden's target and the RPC name one toolchain.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, nothing skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, the skip check passing (`46 cases skipped in 10 files`).

Run: `pnpm test`
Expected: `# 169 test file(s) passed (jobs=16)`.

Run: `pnpm typecheck`
Expected: exit 0.

Run: `git status --short`
Expected: this task's 8 files plus ` m deps/sameboy`, nothing else: `A` for `packages/native/test/audio/golden/kit-baseline.json` (staged by the recording check above), `??` for `packages/native/src/util/BuildTarget.hpp` and `packages/native/test/audio/KitBaseline.test.cpp`, and ` M` for the other five (the CMake entry, the RPC, `RisaDmcCodec`'s two files and `build.yml`).

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/src/util/BuildTarget.hpp packages/native/test/audio/KitBaseline.test.cpp \
  packages/native/test/audio/golden/kit-baseline.json .github/workflows/build.yml packages/native/cmake/tests.cmake \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/risa/RisaDmcCodec.cpp \
  packages/native/src/risa/RisaDmcCodec.hpp
git commit -F - <<'EOF'
test(kit): a byte baseline holds every kit the compilers build

A Catch2 suite in retroplug-audio-test (tag [kit-baseline]) compiles a fixed matrix of
synthesised sources through the same KitCompiler, LsdjKitCodec and RisaDmcCodec the
compileKit / compileDmc RPCs build, and hashes each assembled bank with FNV-1a-64 into
test/audio/golden/kit-baseline.json beside it. The matrix needs no fixtures: two sine
frequencies, a chirp, seeded noise and silence at 22.05, 44.1 and 48 kHz, a 600-frame source
shorter than one resampler chunk, every LSDj effect plus rotate on and off, one chain of
filter then gain then dither, and the risa DMC codec at five rate indices with and without a
pre-effect, with normalise off and with loop set. Thirty-one cases, each source written to a
temp dir as a 32-bit float WAV so SampleCache's real miniaudio decode is part of what the
hashes pin. The silent cases reach the "no peak to normalise by" branch of the gain effect
and the DMC encode, which no non-silent source does.

Each record carries the bank's size and slot 0's byte length beside its hash. The bank size is
a layout constant; the slot length is what the resample and the pack make of the source, so it
is the cross-platform half of the record that still catches a truncated resample when a
foreign host only checks self-agreement.

Exact bytes are compared only on linux-x64-gnu13, the host parity records on; elsewhere a
case compiles twice and must agree with itself. RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign
host instead of falling back, mirroring RP_PARITY_REQUIRE_TARGET, and CI's linux-x64 job sets
it as it sets the parity one, so a toolchain move cannot turn exact checking into a silent
fallback. RP_KIT_GOLDEN_UPDATE=1 re-records the file: it refuses off the target, and it
refuses a case whose two compiles disagree (a REQUIRE, so the file is never written from a
digest the same run could not reproduce). Each case also holds that its source decodes back
to exactly the samples written, that its bank is the codec's size and carries its sample (an
empty slot is a stable bank and would pin nothing), and that KitCompiler's dirty-tracking hash
is the FNV of the bank it returns.

The host name comes from rp::buildTarget() in src/util/BuildTarget.hpp, which the engine's
buildTarget RPC reports too, so the golden's target and the RPC cannot drift apart. The risa
bank's size and index offset are exposed on RisaDmcCodec for the same reason on the layout
side: the test reads the offsets the assembler writes.
EOF
````

### Task 3: the kit DSP is a core-free library of block-streaming kernels

Design sections 4 and 6, and section 9's questions 2 to 5. The kit path's DSP moves out of `retroplug-core`, which
links every emulator core, into `packages/native/src/dsp/` as block-streaming kernels: gain, `peak` and the
normalising gain (a reciprocal multiply, because the bytes depend on it), the eight RBJ biquads, dither and quantise,
and the r8brain resampler. Today's code becomes loops over them: `lsdj/Effects` maps its fields onto the kernels and
gains `lsdj::quantise` for the 4-bit codes, `KitUtil::compileSample` is window, effects, resample, quantise,
`frameAlign`, nibbles, `KitUtil::convertSamplerate` is deleted, `SampleUtil::convertScaledF32ToNibbles` becomes
`convertCodesToNibbles` over the codes, and the risa DMC path's effects and resample are loops too. The target is
`retroplug-dsp-kernels` (gain, biquad, dither, quantise and, for now, the resampler), because the design's
`retroplug-dsp` is DPF's own plugin DSP library; Task 4 splits `retroplug-dsp-resample` out of it so that the node
package links a library with no r8brain on its line. The baseline must not move by a single hash: that is the whole
task.

**Files:**
- Create: `packages/native/src/dsp/Biquad.cpp`, `packages/native/src/dsp/Biquad.hpp`, `packages/native/src/dsp/Dither.cpp`, `packages/native/src/dsp/Dither.hpp`, `packages/native/src/dsp/Gain.cpp`, `packages/native/src/dsp/Gain.hpp`, `packages/native/src/dsp/Resample.cpp`, `packages/native/src/dsp/Resample.hpp`, `packages/native/test/graph/Dsp.test.cpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/src/lsdj/Effects.cpp`, `packages/native/src/lsdj/Effects.hpp`, `packages/native/src/lsdj/KitUtil.cpp`, `packages/native/src/lsdj/KitUtil.hpp`, `packages/native/src/lsdj/SampleUtil.hpp`, `packages/native/src/risa/RisaDmcCodec.cpp`, `packages/native/src/util/Hash.hpp`, `packages/retroplug/test-native/lsdj-rom.test.ts`

- [ ] **Step 1: Write the failing cases**

`Dsp.test.cpp` (new, 814 lines, 17 cases, tag `[dsp]` with `[gain]`, `[biquad]`, `[dither]`, `[resample]` and
`[noalloc]` sub-tags) holds each kernel to what the kit path relies on: one block equals the same input split into 1,
7, 64 and 1024 frames and a ragged mix, bit for bit; a `process` allocates and frees nothing once warmed, and the
resampler from its very first chunk; the eight biquad calculators and the loop, copied verbatim from today's
`Effects.cpp` with their types, give the kernel's coefficients field for field and its buffer sample for sample; the
dither's seed cases and its eight-code golden are the kit path's own; Sierra Lite and JJN, kept as their last two and
four errors, give verbatim copies of the padded whole-buffer functions the same codes at lengths 0 to 5, two noises,
an overdriven sine, four bit depths and five block splits; and the resampler's stream is independent of how its input
arrives, a source shorter than one chunk included, and equals a reference that feeds r8brain the fixed 1024-frame
chunks directly.

Create `packages/native/test/graph/Dsp.test.cpp`:

````cpp
// The rp::dsp kernels (src/dsp/, docs/modular-graph-phase-d1-design.md section 4), with no graph, core or file in the
// way: what each promises a node that runs it a block at a time, and what the kit path that runs it over whole
// buffers relies on.
//
// - A stream split into blocks of 1, 7, 64 or 1024 frames (and a ragged mix) gives the samples one call over the
//   whole of it gives, bit for bit: every kernel carries its state across the split.
// - A warmed process allocates and frees nothing (the counting allocator; the C library's blocks are counted too
//   where this build can see them, which matters for r8brain, whose own allocator is malloc).
// - Biquad: the eight coefficient calculators and the loop, written out here from lsdj/Effects.cpp, give the
//   kernel's coefficients (every field bit for bit) and its filtered buffer sample for sample.
// - Dither: the same seed gives the same codes and a different seed different noise; Sierra Lite and JJN, kept as a
//   few past errors, give the codes their padded whole-buffer arithmetic gives; and the seed drawn from a buffer
//   gives the codes test/audio/Dither.test.cpp pins through the kit effect. (inputSeed's big-endian half cannot be
//   exercised on a little-endian host; the two halves are held to one value by construction, not by a case here.)
// - Resample: the output stream is independent of how the input arrives (a source shorter than one chunk included),
//   and equals a direct fixed-chunk feed of r8brain, written out here as a reference.
//
// The kit baseline (test/audio/KitBaseline.test.cpp) holds the same kernels to whole kit banks' bytes.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <random>
#include <thread>
#include <vector>

#include <CDSPResampler.h>

#include "CountingAllocator.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "dsp/Resample.hpp"

namespace {

using rp::dsp::BiquadType;
using rp::dsp::DitherAlgorithm;

constexpr std::size_t kFrames = 4096;

// The block sizes a stream is split into: the extremes, an odd size that never lands on a power of two, the usual
// host block, the resampler's chunk, and a ragged sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {1024}, {1, 31, 512, 3, 1024, 77}};

constexpr DitherAlgorithm kAlgorithms[] = {
    DitherAlgorithm::None,           DitherAlgorithm::HighPassTPDF, DitherAlgorithm::ShapedTPDF,
    DitherAlgorithm::ErrorDiffusion, DitherAlgorithm::JJN,          DitherAlgorithm::SierraLite,
};

constexpr BiquadType kBiquadTypes[] = {
    BiquadType::LowPass, BiquadType::HighPass, BiquadType::BandPass,  BiquadType::BandStop,
    BiquadType::Peak,    BiquadType::LowShelf, BiquadType::HighShelf, BiquadType::AllPass,
};

/** Uniform noise in [-amp, amp) from a fixed seed, drawn from raw mt19937 output so the sequence is specified. */
std::vector<float> noise(std::size_t frames, float amp, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<float> out(frames);
    for (auto& v : out) v = (static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * amp;
    return out;
}

/** A sine plus noise, overdriven past [-1, 1] in places, so the clamps and the error terms all do work. */
std::vector<float> signal(std::size_t frames) {
    std::vector<float> out = noise(frames, 0.3f, 0x5eed0001u);
    for (std::size_t i = 0; i < frames; ++i) out[i] += 1.1f * std::sin(static_cast<float>(i) * 0.031f);
    return out;
}

/** Calls `run(offset, n)` over [0, frames) in the block sizes `split` cycles through. */
template <class Run>
void inBlocks(std::size_t frames, const std::vector<std::size_t>& split, Run run) {
    std::size_t at = 0, k = 0;
    while (at < frames) {
        const std::size_t n = std::min(split[k++ % split.size()], frames - at);
        run(at, n);
        at += n;
    }
}

/** The first index two buffers differ at, bit for bit, or -1. One number keeps a failing CHECK readable. */
template <class T>
std::ptrdiff_t firstDifference(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(T)) != 0) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

/** The same bits, so a -0.0f where 0.0f was expected counts as a difference where `==` would not. */
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// ---- the padded whole-buffer forms of Sierra Lite and JJN (the legacy AudioDithering arithmetic) ----------------
// Codes as floats in [0, 2^bits - 1], so the kernel's streaming form is held to the padded arithmetic bit for bit.

std::vector<float> paddedSierraLite(std::vector<float> buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    const std::size_t n = buf.size();
    std::vector<float> padded(n + 2, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        padded[i] = (buf[i] + 1.0f) * halfScale;

    for (std::size_t i = 0; i < n; ++i) {
        const float value = padded[i];
        const float q = std::round(std::clamp(value, 0.0f, maxValue));
        buf[i] = q;
        const float err = value - q;
        if (i + 1 < n) padded[i + 1] += err * 0.5f;
        if (i + 2 < n) padded[i + 2] += err * 0.25f;
    }
    return buf;
}

std::vector<float> paddedJjn(std::vector<float> buf, int bitDepth) {
    const int   levels    = 1 << bitDepth;
    const float maxValue  = static_cast<float>(levels - 1);
    const float halfScale = maxValue / 2.0f;
    const std::size_t n = buf.size();
    std::vector<float> padded(n + 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        padded[i] = (buf[i] + 1.0f) * halfScale;

    constexpr float w1 = 7.0f / 48.0f;
    constexpr float w2 = 5.0f / 48.0f;
    constexpr float w3 = 3.0f / 48.0f;
    constexpr float w4 = 1.0f / 48.0f;

    for (std::size_t i = 0; i < n; ++i) {
        const float value = padded[i];
        const float q = std::round(std::clamp(value, 0.0f, maxValue));
        buf[i] = q;
        const float err = value - q;
        if (i + 1 < n) padded[i + 1] += err * w1;
        if (i + 2 < n) padded[i + 2] += err * w2;
        if (i + 3 < n) padded[i + 3] += err * w3;
        if (i + 4 < n) padded[i + 4] += err * w4;
    }
    return buf;
}

// ---- the kit path's biquad as it was before the kernel ------------------------------------------------------------
// The eight RBJ calculators and the loop, copied verbatim from lsdj/Effects.cpp with their types, so the bodies need no
// edit and this file needs no reflect-cpp or core link: what the kernel's coefficients and its filtered buffer are
// held to, bit for bit.

constexpr float kPi = 3.14159265358979323846f;

enum class FilterType : std::uint32_t {
    LowPass   = 0,
    HighPass  = 1,
    BandPass  = 2,
    BandStop  = 3,
    Peak      = 4,
    LowShelf  = 5,
    HighShelf = 6,
    AllPass   = 7,
};

struct FilterEffect {
    FilterType filterType = FilterType::LowPass;
    float      frequency  = 5734.0f;   // GAMEBOY_SAMPLE_RATE / 2; nyquist for 11468 Hz
    float      q          = 1.0f;
    float      gain       = 0.0f;      // dB, used by Peak / LowShelf / HighShelf
};

struct FilterState {
    float sampleRate = 48000.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

void calculateLowPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - cw) / (2.0f * a0);
    s.b1 = (1.0f - cw) / a0;
    s.b2 = (1.0f - cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateHighPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  (1.0f + cw) / (2.0f * a0);
    s.b1 = -(1.0f + cw) / a0;
    s.b2 =  (1.0f + cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateBandPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  sw / (2.0f * a0);
    s.b1 = 0.0f;
    s.b2 = -sw / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculateBandStop(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = 1.0f / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = 1.0f / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void calculatePeak(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha / A;
    s.b0 = (1.0f + alpha * A) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f - alpha * A) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha / A) / a0;
}

void calculateLowShelf(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float beta = std::sqrt(A) / effect.q;
    const float a0 = (A + 1.0f) + (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) + (A - 1.0f) * cw - beta * sw) / a0;
}

void calculateHighShelf(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, effect.gain / 40.0f);
    const float beta = std::sqrt(A) / effect.q;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) - (A - 1.0f) * cw - beta * sw) / a0;
}

void calculateAllPass(const FilterEffect& effect, FilterState& s) {
    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * effect.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - alpha) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f + alpha) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

void updateCoefficients(const FilterEffect& effect, FilterState& s) {
    switch (effect.filterType) {
        case FilterType::LowPass:   calculateLowPass(effect, s);   break;
        case FilterType::HighPass:  calculateHighPass(effect, s);  break;
        case FilterType::BandPass:  calculateBandPass(effect, s);  break;
        case FilterType::BandStop:  calculateBandStop(effect, s);  break;
        case FilterType::Peak:      calculatePeak(effect, s);      break;
        case FilterType::LowShelf:  calculateLowShelf(effect, s);  break;
        case FilterType::HighShelf: calculateHighShelf(effect, s); break;
        case FilterType::AllPass:   calculateAllPass(effect, s);   break;
    }
}

/** The old whole-buffer pass: the coefficients once, then the transposed loop, the feedback taken before the clamp. */
std::vector<float> referenceBiquad(const FilterEffect& effect, float sampleRate, std::vector<float> target) {
    FilterState s;
    s.sampleRate = sampleRate;
    updateCoefficients(effect, s);

    for (float& sample : target) {
        const float in = sample;
        float out = s.b0 * in + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
        s.x2 = s.x1; s.x1 = in;
        s.y2 = s.y1; s.y1 = out;
        out = std::clamp(out, -1.0f, 1.0f);
        sample = out;
    }
    return target;
}

/** The coefficients the old calculators give, for the field-for-field comparison. */
FilterState referenceFilterState(const FilterEffect& effect, float sampleRate) {
    FilterState s;
    s.sampleRate = sampleRate;
    updateCoefficients(effect, s);
    return s;
}

// ---- the kit path's resample, fed straight to r8brain -----------------------------------------------------------
// One resampler per call, a fixed 1024-frame chunk zero-padded at the tail, the output cut to floor(n * out / in).

std::vector<float> referenceResample(double inRate, double outRate, const std::vector<float>& buffer) {
    std::vector<float> target;
    if (buffer.empty()) return target;
    constexpr std::size_t kInBufCapacity = 1024;
    r8b::CFixedBuffer<double> inBuf;
    inBuf.alloc(static_cast<int>(kInBufCapacity));
    r8b::CPtrKeeper<r8b::CDSPResampler24*> resampler =
        new r8b::CDSPResampler24(inRate, outRate, static_cast<int>(kInBufCapacity));
    const std::size_t targetSize = static_cast<std::size_t>(buffer.size() * (outRate / inRate));
    target.resize(targetSize);
    std::size_t sourcePos = 0, targetPos = 0;
    while (targetPos < targetSize) {
        std::fill_n(inBuf.getPtr(), kInBufCapacity, 0.0);
        const std::size_t chunkSize = std::min(kInBufCapacity, buffer.size() - sourcePos);
        for (std::size_t i = 0; i < chunkSize; ++i) inBuf[i] = static_cast<double>(buffer[sourcePos++]);
        double* out = nullptr;
        std::size_t writeCount =
            static_cast<std::size_t>(resampler->process(inBuf.getPtr(), static_cast<int>(kInBufCapacity), out));
        if (targetPos + writeCount > targetSize) writeCount = targetSize - targetPos;
        for (std::size_t i = 0; i < writeCount; ++i) target[targetPos++] = static_cast<float>(out[i]);
    }
    return target;
}

/**
 * The resampler driven as a block-streaming consumer would drive it: input pushed in `pushSplit` pieces, output
 * pulled in `pullSplit` pieces, silence after the source, until `length` frames are out.
 */
std::vector<float> streamResample(double inRate, double outRate, const std::vector<float>& in, std::size_t length,
                                  const std::vector<std::size_t>& pushSplit,
                                  const std::vector<std::size_t>& pullSplit) {
    rp::dsp::Resampler r;
    r.init(inRate, outRate);
    std::vector<float> out(length);
    std::size_t consumed = 0, written = 0, k = 0, j = 0;
    while (written < length) {
        const std::size_t want = pushSplit[k++ % pushSplit.size()];
        if (consumed < in.size()) consumed += r.push(in.data() + consumed, std::min(want, in.size() - consumed));
        else r.pushSilence(want);
        const std::size_t pull = std::min(pullSplit[j++ % pullSplit.size()], length - written);
        written += r.pull(out.data() + written, pull);
    }
    return out;
}

} // namespace

TEST_CASE("the gain kernel multiplies exactly and a block split changes nothing", "[dsp][gain]") {
    const std::vector<float> in = signal(kFrames);
    rp::dsp::Gain g;
    g.init(0.37f);
    std::vector<float> whole(kFrames);
    g.process(in.data(), whole.data(), kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) REQUIRE(whole[i] == in[i] * 0.37f);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        std::vector<float> blocks(kFrames);
        inBlocks(kFrames, split,
                 [&](std::size_t at, std::size_t n) { g.process(in.data() + at, blocks.data() + at, n); });
        CHECK(firstDifference(whole, blocks) == -1);
    }

    // In place, as the kit path runs it.
    std::vector<float> inPlace = in;
    g.process(inPlace.data(), inPlace.data(), kFrames);
    CHECK(firstDifference(whole, inPlace) == -1);
}

TEST_CASE("peak and the normalising gain are the kit path's", "[dsp][gain]") {
    const std::vector<float> in = {0.25f, -0.8f, 0.5f, std::nanf(""), -0.1f};
    CHECK(rp::dsp::peak(in.data(), in.size()) == 0.8f);  // a NaN leaves the running peak where it was
    CHECK(rp::dsp::peak(in.data(), 0) == 0.0f);
    // 0.37 * (1/0.9) and 0.37 / 0.9 differ in binary32 (0.411111146 against 0.411111116), and neither operand is a
    // power of two, so this pair can tell the reciprocal multiply the kit path's bytes depend on from a division. A
    // round pair cannot: 1/0.8 rounds to exactly 1.25f, so 0.5 * (1/0.8) and 0.5 / 0.8 are one float.
    CHECK(rp::dsp::normalizedGain(0.37f, 0.9f) == 0.37f * (1.0f / 0.9f));
    CHECK(rp::dsp::normalizedGain(0.37f, 0.9f) != 0.37f / 0.9f);
    CHECK(rp::dsp::normalizedGain(0.5f, 0.0f) == 0.5f);  // silence keeps the gain as set
}

TEST_CASE("the biquad kernel is the kit path's filter to the bit", "[dsp][biquad]") {
    // The calculators and the loop above are lsdj/Effects.cpp's, so this holds the kernel to the arithmetic the kit
    // baseline's bytes were recorded from: every coefficient bit for bit (not ==, which cannot tell -0.0f from 0.0f)
    // and the filtered buffer sample for sample, over the same eight types and signal the streaming case uses. The
    // shape is swept as well as the type: a reciprocal substituted for a division can round the same at one
    // frequency and differently at another (44100/1500/0.9 does not see it, 22050/500/0.9 does), so one shape would
    // leave that edit to the kit baseline, which a 1-ulp coefficient moves only if a code happens to flip.
    struct Shape {
        float rate, frequency, q, gainDb;
    };
    const Shape shapes[] = {
        {44100.0f, 1500.0f, 0.9f, 5.0f},  // the streaming case's
        {22050.0f, 500.0f, 0.9f, 5.0f},   // low and slow, where a coefficient's last bit shows
        {11468.0f, 9000.0f, 2.0f, 5.0f},  // at the kit rate, toward nyquist, with a high Q
        {48000.0f, 5000.0f, 0.5f, -6.0f}, // a cut, so the shelves' gain term is negative
    };
    const std::vector<float> in = signal(kFrames);
    for (const BiquadType type : kBiquadTypes) {
        for (const Shape& shape : shapes) {
            INFO("type " << static_cast<int>(type) << ", " << shape.rate << " Hz, " << shape.frequency << " Hz, q "
                         << shape.q);
            const rp::dsp::BiquadSettings settings{type, shape.rate, shape.frequency, shape.q, shape.gainDb};
            const FilterEffect old{static_cast<FilterType>(static_cast<std::uint32_t>(type)), shape.frequency,
                                   shape.q, shape.gainDb};

            const rp::dsp::BiquadCoefficients c = rp::dsp::biquadCoefficients(settings);
            const FilterState s = referenceFilterState(old, settings.sampleRate);
            CHECK(sameBits(c.b0, s.b0));
            CHECK(sameBits(c.b1, s.b1));
            CHECK(sameBits(c.b2, s.b2));
            CHECK(sameBits(c.a1, s.a1));
            CHECK(sameBits(c.a2, s.a2));

            rp::dsp::Biquad b;
            b.init(settings);
            std::vector<float> filtered(kFrames);
            b.process(in.data(), filtered.data(), kFrames);
            CHECK(firstDifference(referenceBiquad(old, settings.sampleRate, in), filtered) == -1);
        }
    }
}

TEST_CASE("the biquad kernel streams: any block split gives one call's output", "[dsp][biquad]") {
    const std::vector<float> in = signal(kFrames);
    for (const BiquadType type : kBiquadTypes) {
        INFO("type " << static_cast<int>(type));
        const rp::dsp::BiquadSettings settings{type, 44100.0f, 1500.0f, 0.9f, 5.0f};
        rp::dsp::Biquad b;
        b.init(settings);
        std::vector<float> whole(kFrames);
        b.process(in.data(), whole.data(), kFrames);
        bool clamped = false;
        for (float v : whole) {
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            clamped = clamped || v == 1.0f || v == -1.0f;
        }
        // The overdriven sine (about 220 Hz) reaches the clamp through every filter that passes it.
        if (type != BiquadType::HighPass && type != BiquadType::BandPass) CHECK(clamped);

        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            b.init(settings);
            std::vector<float> blocks(kFrames);
            inBlocks(kFrames, split,
                     [&](std::size_t at, std::size_t n) { b.process(in.data() + at, blocks.data() + at, n); });
            CHECK(firstDifference(whole, blocks) == -1);
        }
    }
}

TEST_CASE("setup keeps a biquad's state while init clears it", "[dsp][biquad]") {
    const std::vector<float> in = signal(256);
    const rp::dsp::BiquadSettings a{BiquadType::LowPass, 48000.0f, 800.0f, 0.7f, 0.0f};
    const rp::dsp::BiquadSettings b{BiquadType::LowPass, 48000.0f, 3000.0f, 0.7f, 0.0f};
    rp::dsp::Biquad swept;
    swept.init(a);
    std::vector<float> out(256);
    swept.process(in.data(), out.data(), 128);
    const float y1 = swept.y1;
    swept.setup(b);
    CHECK(swept.y1 == y1);
    swept.init(b);
    CHECK(swept.y1 == 0.0f);
}

TEST_CASE("the dither kernel streams: any block split gives one call's codes", "[dsp][dither]") {
    const std::vector<float> in = signal(kFrames);
    for (const DitherAlgorithm algo : kAlgorithms) {
        for (const int bits : {1, 4, 8, 16}) {
            INFO("algorithm " << static_cast<int>(algo) << ", " << bits << " bits");
            rp::dsp::Dither d;
            d.init(algo, bits, 0x1234abcdu);
            std::vector<std::uint16_t> whole(kFrames);
            d.process(in.data(), whole.data(), kFrames);
            const std::uint32_t maxCode = (1u << bits) - 1u;
            for (auto c : whole) REQUIRE(c <= maxCode);

            for (const auto& split : kSplits) {
                INFO("split starting " << split[0]);
                d.init(algo, bits, 0x1234abcdu);
                std::vector<std::uint16_t> blocks(kFrames);
                inBlocks(kFrames, split,
                         [&](std::size_t at, std::size_t n) { d.process(in.data() + at, blocks.data() + at, n); });
                CHECK(firstDifference(whole, blocks) == -1);
            }

            // The audio form is the same codes mapped through codeToLevel, block split or not, in place or not.
            d.init(algo, bits, 0x1234abcdu);
            std::vector<float> levels = in;
            inBlocks(kFrames, kSplits.back(), [&](std::size_t at, std::size_t n) {
                d.processLevels(levels.data() + at, levels.data() + at, n);
            });
            std::vector<float> mapped(kFrames);
            for (std::size_t i = 0; i < kFrames; ++i) mapped[i] = rp::dsp::codeToLevel(whole[i], bits);
            CHECK(firstDifference(mapped, levels) == -1);
        }
    }
}

TEST_CASE("codes map back to the quantised levels", "[dsp][dither]") {
    CHECK(rp::dsp::codeToLevel(0, 4) == -1.0f);
    CHECK(rp::dsp::codeToLevel(15, 4) == 1.0f);
    CHECK(rp::dsp::codeToLevel(1, 1) == 1.0f);
    CHECK(rp::dsp::codeToLevel(65535, 16) == 1.0f);
    // The LSDj kit decoder's level for a 4-bit code (SampleUtil::convertNibblesToF32), so a dither node's audio is
    // what the Game Boy would play.
    for (std::uint32_t c = 0; c < 16; ++c) CHECK(rp::dsp::codeToLevel(c, 4) == (static_cast<float>(c) / 15.0f) * 2.0f - 1.0f);
}

TEST_CASE("init clamps the bit depth and a NaN reads as the lowest code", "[dsp][dither]") {
    // Both are documented behaviour a node's setting and a garbage sample depend on: the clamp in Dither.hpp, the NaN
    // rule in Dither.cpp's toCode (an undefined float-to-int cast otherwise).
    rp::dsp::Dither d;
    d.init(DitherAlgorithm::None, 0, 0);
    CHECK(d.bits == rp::dsp::kMinDitherBits);
    CHECK(d.maxValue == 1.0f);
    d.init(DitherAlgorithm::None, 24, 0);
    CHECK(d.bits == rp::dsp::kMaxDitherBits);
    CHECK(d.maxValue == 65535.0f);
    CHECK(rp::dsp::codeToLevel(3, 0) == rp::dsp::codeToLevel(3, rp::dsp::kMinDitherBits));
    CHECK(rp::dsp::codeToLevel(3, 24) == rp::dsp::codeToLevel(3, rp::dsp::kMaxDitherBits));

    const std::vector<float> nanos = {std::nanf(""), 0.5f, -std::nanf("")};
    std::vector<std::uint16_t> codes(nanos.size());
    d.init(DitherAlgorithm::None, 4, 0);
    d.process(nanos.data(), codes.data(), codes.size());
    CHECK(codes[0] == 0);
    CHECK(codes[1] == 11);  // round((0.5 + 1) * 0.5 * 15)
    CHECK(codes[2] == 0);
}

TEST_CASE("the same seed gives the same codes, a different seed different noise", "[dsp][dither]") {
    // Silence is the cleanest probe: every code a TPDF dither gives it comes from the noise.
    const std::vector<float> silence(1024, 0.0f);
    for (const DitherAlgorithm algo : {DitherAlgorithm::HighPassTPDF, DitherAlgorithm::ShapedTPDF}) {
        INFO("algorithm " << static_cast<int>(algo));
        const auto run = [&](std::uint32_t seed) {
            rp::dsp::Dither d;
            d.init(algo, 4, seed);
            std::vector<std::uint16_t> codes(silence.size());
            d.process(silence.data(), codes.data(), silence.size());
            return codes;
        };
        CHECK(firstDifference(run(42), run(42)) == -1);
        const auto a = run(42), b = run(43);
        std::size_t differ = 0;
        for (std::size_t i = 0; i < a.size(); ++i) differ += a[i] != b[i];
        CHECK(differ > 16);
    }

    // The two TPDF algorithms salt one seed differently, so they do not walk one noise sequence.
    rp::dsp::Dither hp, sh;
    hp.init(DitherAlgorithm::HighPassTPDF, 4, 7);
    sh.init(DitherAlgorithm::ShapedTPDF, 4, 7);
    std::uint32_t hpFirst = hp.rng(), shFirst = sh.rng();
    CHECK(hpFirst != shFirst);

    // The deterministic algorithms ignore the seed.
    for (const DitherAlgorithm algo : {DitherAlgorithm::None, DitherAlgorithm::ErrorDiffusion, DitherAlgorithm::JJN,
                                       DitherAlgorithm::SierraLite}) {
        INFO("algorithm " << static_cast<int>(algo));
        const std::vector<float> in = signal(512);
        rp::dsp::Dither x, y;
        x.init(algo, 4, 1);
        y.init(algo, 4, 2);
        std::vector<std::uint16_t> cx(512), cy(512);
        x.process(in.data(), cx.data(), 512);
        y.process(in.data(), cy.data(), 512);
        CHECK(firstDifference(cx, cy) == -1);
    }
}

TEST_CASE("the input seed gives the codes the kit path pinned", "[dsp][dither]") {
    // test/audio/Dither.test.cpp pins these eight codes for a silent eight-sample buffer through the kit effect; the
    // kernel seeded from inputSeed over the same buffer must give them, which holds the hash, the fold, the salts, the
    // draw and both algorithms at once.
    const std::vector<float> silence(8, 0.0f);
    const std::uint32_t seed = rp::dsp::inputSeed(silence.data(), silence.size());
    const auto codes = [&](DitherAlgorithm algo) {
        rp::dsp::Dither d;
        d.init(algo, 4, seed);
        std::vector<std::uint16_t> out(8);
        d.process(silence.data(), out.data(), 8);
        return out;
    };
    CHECK(codes(DitherAlgorithm::HighPassTPDF) == std::vector<std::uint16_t>{7, 8, 7, 7, 8, 7, 8, 8});
    CHECK(codes(DitherAlgorithm::ShapedTPDF) == std::vector<std::uint16_t>{7, 7, 7, 7, 8, 7, 8, 7});

    // A function of the samples: one changed bit moves it.
    std::vector<float> nudged = silence;
    nudged[5] = 1.0e-30f;
    CHECK(rp::dsp::inputSeed(nudged.data(), nudged.size()) != seed);
}

TEST_CASE("Sierra Lite and JJN as past errors give their padded arithmetic's codes", "[dsp][dither]") {
    // Lengths below and around the error window (0 to 5) exercise the terms a padded copy never had before the
    // stream's start and the ones it dropped past its end; the long ones, overdriven noise and a quiet signal, put
    // every error term to work, and a split into blocks puts the carried errors across every block edge.
    std::vector<std::vector<float>> inputs;
    for (std::size_t n = 0; n <= 5; ++n) inputs.push_back(noise(n, 1.3f, 0x51e77a00u + static_cast<std::uint32_t>(n)));
    inputs.push_back(noise(kFrames, 1.3f, 0x51e77a10u));
    inputs.push_back(noise(kFrames, 0.02f, 0x51e77a11u));
    inputs.push_back(signal(kFrames));

    for (const int bits : {1, 4, 8, 16}) {
        for (const auto& in : inputs) {
            INFO(bits << " bits, " << in.size() << " frames");
            for (const DitherAlgorithm algo : {DitherAlgorithm::SierraLite, DitherAlgorithm::JJN}) {
                INFO("algorithm " << static_cast<int>(algo));
                const std::vector<float> reference =
                    algo == DitherAlgorithm::SierraLite ? paddedSierraLite(in, bits) : paddedJjn(in, bits);
                for (const auto& split : kSplits) {
                    INFO("split starting " << split[0]);
                    rp::dsp::Dither d;
                    d.init(algo, bits, 0);
                    std::vector<std::uint16_t> codes(in.size());
                    inBlocks(in.size(), split, [&](std::size_t at, std::size_t n) {
                        d.process(in.data() + at, codes.data() + at, n);
                    });
                    std::vector<float> asFloats(codes.begin(), codes.end());
                    CHECK(firstDifference(reference, asFloats) == -1);
                }
            }
        }
    }
}

TEST_CASE("the resampler's output stream is independent of how its input arrives", "[dsp][resample]") {
    struct Case {
        double inRate, outRate;
        std::size_t frames;
    };
    // The kit's rates: LSDj's 11468 Hz from the three source rates, and risa DMC rates up and down. 600 frames is a
    // source shorter than one chunk; 1024 exactly one; 5000 a few chunks and a partial; 20000 enough output that the
    // rare sample a different chunking would move is likely to be in it. (r8brain's DOUBLE output depends on how its
    // input is split into calls in most samples; the cast to float hides nearly all of that, so only a long stream
    // shows the difference reliably. A kernel that resampled a partial chunk at a push's end fails here at 44100 ->
    // 16965.4 while every kit-baseline bank still matched.)
    const Case cases[] = {
        {44100.0, 11468.0, 5000},   {22050.0, 11468.0, 5000},   {48000.0, 11468.0, 600},
        {44100.0, 11468.0, 1024},   {44100.0, 16965.40, 5000},  {22050.0, 33252.09, 600},
        {22050.0, 33252.09, 20000}, {48000.0, 4177.40, 20000},
    };
    for (const Case& c : cases) {
        INFO(c.inRate << " -> " << c.outRate << ", " << c.frames << " frames");
        const std::vector<float> in = signal(c.frames);
        const std::vector<float> reference = referenceResample(c.inRate, c.outRate, in);
        REQUIRE(reference.size() == rp::dsp::resampledLength(c.frames, c.inRate, c.outRate));
        REQUIRE(!reference.empty());

        std::vector<float> whole;
        rp::dsp::resample(c.inRate, c.outRate, in.data(), in.size(), whole);
        CHECK(firstDifference(reference, whole) == -1);

        for (const auto& pushSplit : kSplits) {
            for (const auto& pullSplit : std::initializer_list<std::vector<std::size_t>>{{1}, {64}, {4096}, {5, 300}}) {
                INFO("push split starting " << pushSplit[0] << ", pull split starting " << pullSplit[0]);
                const auto streamed = streamResample(c.inRate, c.outRate, in, reference.size(), pushSplit, pullSplit);
                CHECK(firstDifference(reference, streamed) == -1);
            }
        }
    }

    std::vector<float> none{1.0f};
    rp::dsp::resample(44100.0, 11468.0, nullptr, 0, none);
    CHECK(none.empty());
}

TEST_CASE("resamplers built and run on parallel threads each give the single-thread stream", "[dsp][resample]") {
    // The kit compiler resamples every slot on its own worker at once, and r8brain shares one filter cache between
    // all its resamplers (built under its own lock). Each thread here builds its own kernel from scratch, at rates
    // that share cached filters, and must give what one thread alone gives; under ThreadSanitizer this is the case
    // that shows the kernels share nothing unguarded.
    const std::vector<float> in = signal(6000);
    constexpr double kRates[] = {44100.0, 22050.0, 48000.0, 44100.0, 22050.0, 48000.0, 44100.0, 22050.0};
    std::vector<std::vector<float>> alone(std::size(kRates)), together(std::size(kRates));
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        rp::dsp::resample(kRates[t], 11468.0, in.data(), in.size(), alone[t]);
    }

    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        threads.emplace_back([&, t] { rp::dsp::resample(kRates[t], 11468.0, in.data(), in.size(), together[t]); });
    }
    for (auto& th : threads) th.join();
    for (std::size_t t = 0; t < std::size(kRates); ++t) {
        INFO("thread " << t << " at " << kRates[t]);
        CHECK(firstDifference(alone[t], together[t]) == -1);
    }
}

TEST_CASE("the resampler's available count follows what is pushed and pulled", "[dsp][resample]") {
    // The store/ring contract by name: a chunk pushed in shows as output to pull, and a pull takes it back down
    // (r8brain's latency means "a chunk" is not necessarily the first one to produce anything).
    rp::dsp::Resampler r;
    r.init(44100.0, 11468.0);
    const std::vector<float> in(rp::dsp::Resampler::kChunk, 0.25f);
    CHECK(r.available() == 0);
    std::size_t pushed = 0;
    while (r.available() == 0 && pushed < 8 * rp::dsp::Resampler::kChunk) pushed += r.push(in.data(), in.size());
    const std::size_t produced = r.available();
    CHECK(produced > 0);
    CHECK(pushed <= 8 * rp::dsp::Resampler::kChunk);  // it stopped as soon as there was something to pull

    float out[64] = {};
    CHECK(r.pull(out, std::size(out)) == std::size(out));
    CHECK(r.available() == produced - std::size(out));
}

TEST_CASE("a re-initialised resampler starts a fresh stream", "[dsp][resample]") {
    const std::vector<float> in = signal(3000);
    std::vector<float> fresh;
    rp::dsp::resample(44100.0, 11468.0, in.data(), in.size(), fresh);

    rp::dsp::Resampler r;
    r.init(44100.0, 11468.0);
    float discard[256] = {};
    r.push(in.data(), in.size());
    (void)r.pull(discard, std::size(discard));  // part of the old stream read, the rest dropped by the re-init
    r.init(44100.0, 11468.0);
    CHECK(r.available() == 0);

    std::vector<float> again(fresh.size());
    std::size_t consumed = 0, written = 0;
    while (written < again.size()) {
        if (consumed < in.size()) {
            consumed += r.push(in.data() + consumed, std::min(rp::dsp::Resampler::kChunk, in.size() - consumed));
        } else {
            r.pushSilence(rp::dsp::Resampler::kChunk);
        }
        written += r.pull(again.data() + written, again.size() - written);
    }
    CHECK(firstDifference(fresh, again) == -1);
}

TEST_CASE("a resampler that is not built takes nothing and gives nothing", "[dsp][resample]") {
    rp::dsp::Resampler r;
    const float x[4] = {};
    float y[4] = {};
    CHECK(!r.ready());
    CHECK(r.push(x, 4) == 0);
    CHECK(r.pushSilence(4) == 0);
    CHECK(r.pull(y, 4) == 0);
    CHECK(r.available() == 0);
}

TEST_CASE("an initialised kernel processes without allocating or freeing", "[dsp][noalloc]") {
    // The C library's blocks are counted too (r8brain allocates through malloc), where this build can see them: a
    // plain glibc build. Under ASan/TSan, on Windows and on macOS the operator-new half is what the zeros below hold
    // (see CountingAllocator.hpp).
    const std::vector<float> in = signal(kFrames);
    std::vector<float> out(kFrames);
    std::vector<std::uint16_t> codes(kFrames);

    rp::dsp::Gain gain;
    gain.init(0.5f);
    rp::dsp::Biquad biquad;
    biquad.init({BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f});
    rp::dsp::Dither dithers[std::size(kAlgorithms)];
    for (std::size_t a = 0; a < std::size(kAlgorithms); ++a) dithers[a].init(kAlgorithms[a], 4, 99);
    // The resampler is counted from its first chunk, not warmed: r8brain sizes every buffer it uses in its
    // constructor, which `init` runs, so not even the first `process` may allocate.
    rp::dsp::Resampler resampler;
    resampler.init(44100.0, 11468.0);

    rp::test::beginCountingAllocations(true);
    for (int block = 0; block < 8; ++block) {
        const float* src = in.data() + (block % 4) * 1024;
        gain.process(src, out.data(), 1024);
        (void)rp::dsp::peak(src, 1024);
        biquad.process(src, out.data(), 1024);
        for (auto& d : dithers) {
            d.process(src, codes.data(), 1024);
            d.processLevels(src, out.data(), 1024);
        }
        (void)rp::dsp::inputSeed(src, 1024);
        std::size_t taken = 0;
        while (taken < 1024) {
            taken += resampler.push(src + taken, 1024 - taken);
            resampler.pull(out.data(), kFrames);
        }
        resampler.pushSilence(300);
        resampler.pull(out.data(), 7);
    }
    const auto counted = rp::test::endCountingAllocations();
    INFO("C allocations counted: " << rp::test::countsCAllocations());
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 5d2ff0b20..0dd3e508d 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -241,8 +241,8 @@ retroplug_add_test_binary(
 # Guards the node-graph engine (docs/modular-graph-design.md, Phase A): the executor over raw C-ABI
 # nodes, the canonical JSON C++ and TS share, descriptors, the C++ authoring layer, the planner's
 # validation codes, plan swaps that keep node state, a node reshaped across a layout change, queues sized
-# by their ports and the drops they report, the rp.core nodes, and a no-heap check of the block path.
-# Self-contained: no core, no txiki, no backend.
+# by their ports and the drops they report, the rp.core nodes, a no-heap check of the block path, and the rp::dsp
+# kernels. Self-contained: no core, no txiki, no backend.
 retroplug_add_test_binary(
     NAME     retroplug-graph-test
     SOURCES  test/graph/Executor.test.cpp
@@ -262,6 +262,9 @@ retroplug_add_test_binary(
              test/graph/Resources.test.cpp            # a node that reads a resource its settings name
              test/graph/QueueCapacity.test.cpp
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
+             test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's arithmetic
     LIBS     retroplug-graph
+             retroplug-dsp-kernels                     # core-free: the standard library and r8brain
+             r8brain                                   # Dsp.test.cpp's reference copy of the kit path's resample
              Threads::Threads                          # SwapStress runs a real audio thread
     DEFS     RP_GRAPH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/graph/golden")
````

Run: `sed -n '2494,2518p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds the source, `retroplug-dsp-kernels` (not a target yet, so it is a plain link name) and `r8brain` to `retroplug-graph-test`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp]"`
Expected: the build stops at the first missing header, `dsp/Gain.hpp` (its includes name `Biquad.hpp`, `Dither.hpp` and `Resample.hpp` too, and the file uses an `rp::dsp` namespace that does not exist yet), so Catch2 never runs. Report the compiler's first few errors.

- [ ] **Step 2: Add the kernels, their targets and today's code on them**

`src/dsp/` gains `Gain.{hpp,cpp}` (`Gain`, `peak`, `normalizedGain`), `Biquad.{hpp,cpp}` (`BiquadType` mirrored by
value from `lsdj::FilterType` with `static_assert`s, `BiquadSettings`, `biquadCoefficients`, and a `Biquad` whose
`init` builds the coefficients and clears the state and whose `setup` moves them and keeps the state),
`Dither.{hpp,cpp}` (`DitherAlgorithm` with the extra `None`, `Dither` with `init(algo, bits, seed)`, `process` for
`uint16_t` codes and `processLevels` for audio, `inputSeed` as the unsalted hash, `codeToLevel`, and the 1..16 bit
clamp) and `Resample.{hpp,cpp}` (the pimpl `Resampler` with `init`, `ready`, `push`, `pushSilence`, `pull`,
`available`, plus `resampledLength` and the whole-buffer `resample`). `util/Hash.hpp` gains the optional running hash.
`Effects.cpp` maps the three effects onto the kernels, keeps the rfl tagged union untouched, and moves
`processEffect(GainEffect)` out of line; `KitUtil` and `SampleUtil` change shape as the design says;
`RisaDmcCodec::encode` resamples through the kernel. `CMakeLists.txt` declares `retroplug-dsp-kernels` (with
`find_package(Threads REQUIRED)` before it) and `retroplug-core` links it in place of `r8brain`; `warnings.cmake` puts
the new target on the MSVC `/W4 /WX` list.

Create `packages/native/src/dsp/Biquad.cpp`:

````cpp
#include "dsp/Biquad.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {

namespace {

// A `float` pi, as every coefficient below is computed in `float`.
constexpr float kPi = 3.14159265358979323846f;

BiquadCoefficients lowPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - cw) / (2.0f * a0);
    s.b1 = (1.0f - cw) / a0;
    s.b2 = (1.0f - cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients highPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  (1.0f + cw) / (2.0f * a0);
    s.b1 = -(1.0f + cw) / a0;
    s.b2 =  (1.0f + cw) / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients bandPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 =  sw / (2.0f * a0);
    s.b1 = 0.0f;
    s.b2 = -sw / (2.0f * a0);
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients bandStop(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = 1.0f / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = 1.0f / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

BiquadCoefficients peaking(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha / A;
    s.b0 = (1.0f + alpha * A) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f - alpha * A) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha / A) / a0;
    return s;
}

BiquadCoefficients lowShelf(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float beta = std::sqrt(A) / p.q;
    const float a0 = (A + 1.0f) + (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) + (A - 1.0f) * cw - beta * sw) / a0;
    return s;
}

BiquadCoefficients highShelf(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float A = std::pow(10.0f, p.gainDb / 40.0f);
    const float beta = std::sqrt(A) / p.q;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cw + beta * sw;
    s.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + beta * sw)) / a0;
    s.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    s.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - beta * sw)) / a0;
    s.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    s.a2 = ((A + 1.0f) - (A - 1.0f) * cw - beta * sw) / a0;
    return s;
}

BiquadCoefficients allPass(const BiquadSettings& p) noexcept {
    BiquadCoefficients s;
    const float omega = 2.0f * kPi * p.frequency / p.sampleRate;
    const float sw = std::sin(omega), cw = std::cos(omega);
    const float alpha = sw / (2.0f * p.q);
    const float a0 = 1.0f + alpha;
    s.b0 = (1.0f - alpha) / a0;
    s.b1 = (-2.0f * cw) / a0;
    s.b2 = (1.0f + alpha) / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
    return s;
}

} // namespace

BiquadCoefficients biquadCoefficients(const BiquadSettings& s) noexcept {
    switch (s.type) {
        case BiquadType::LowPass:   return lowPass(s);
        case BiquadType::HighPass:  return highPass(s);
        case BiquadType::BandPass:  return bandPass(s);
        case BiquadType::BandStop:  return bandStop(s);
        case BiquadType::Peak:      return peaking(s);
        case BiquadType::LowShelf:  return lowShelf(s);
        case BiquadType::HighShelf: return highShelf(s);
        case BiquadType::AllPass:   return allPass(s);
    }
    // A value outside the enum (a cast, not a parse: reflect-cpp refuses an unknown name) passes the signal through.
    return {};
}

void Biquad::process(const float* in, float* out, std::size_t frames) noexcept {
    float sx1 = x1, sx2 = x2, sy1 = y1, sy2 = y2;
    for (std::size_t i = 0; i < frames; ++i) {
        const float x = in[i];
        const float y = c.b0 * x + c.b1 * sx1 + c.b2 * sx2
                      - c.a1 * sy1 - c.a2 * sy2;
        sx2 = sx1; sx1 = x;
        sy2 = sy1; sy1 = y;
        out[i] = std::clamp(y, -1.0f, 1.0f);
    }
    x1 = sx1; x2 = sx2; y1 = sy1; y2 = sy2;
}

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Biquad.hpp`:

````cpp
#pragma once

// The biquad kernel: the RBJ cookbook's eight filters, in `float`.
//
// The arithmetic is the kit path's to the bit (a kit bank's bytes depend on it): each coefficient set is computed in
// `float` with a `float` pi and normalised to a0 = 1 at setup, the loop is a transposed direct form with its feedback
// taken BEFORE the output clamp, and the output is clamped to [-1, 1].

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

// The eight filters. The values are the kit effect's `FilterType`'s, which lsdj/Effects.cpp checks at compile time.
enum class BiquadType : std::uint32_t {
    LowPass   = 0,
    HighPass  = 1,
    BandPass  = 2,
    BandStop  = 3,
    Peak      = 4,
    LowShelf  = 5,
    HighShelf = 6,
    AllPass   = 7,
};

// One filter's shape: `gainDb` is read by Peak and the two shelves only.
struct BiquadSettings {
    BiquadType type       = BiquadType::LowPass;
    float      sampleRate = 48000.0f;
    float      frequency  = 5734.0f;
    float      q          = 1.0f;
    float      gainDb     = 0.0f;
};

// Normalised to a0 = 1.
struct BiquadCoefficients {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

BiquadCoefficients biquadCoefficients(const BiquadSettings& s) noexcept;

// One channel's filter. `init` computes the coefficients and clears the state; `setup` recomputes the coefficients
// and keeps the state, which is what a filter being swept between blocks wants.
struct Biquad {
    BiquadCoefficients c;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void init(const BiquadSettings& s) noexcept {
        setup(s);
        reset();
    }
    void setup(const BiquadSettings& s) noexcept { c = biquadCoefficients(s); }
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    // `in` and `out` may be the same buffer.
    void process(const float* in, float* out, std::size_t frames) noexcept;
};

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Dither.cpp`:

````cpp
#include "dsp/Dither.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "util/Hash.hpp"

namespace rp::dsp {

namespace {

// One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
// the enum's ordinal, so the order `DitherAlgorithm` (or the kit effect's `DitherType`) declares its algorithms in
// cannot re-seed a kit that uses one of them.
constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fnv1a64("HighPassTPDF")
constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fnv1a64("ShapedTPDF")

// A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
// mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
// three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
float uniform01(std::mt19937& rng) noexcept {
    return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
}

// A rounded value as its code. The value is integral and in [0, maxValue] for any input but a NaN, which the clamp
// passes through and which reads as code 0 here rather than as an undefined conversion.
std::uint16_t toCode(float q) noexcept {
    return q >= 0.0f ? static_cast<std::uint16_t>(q) : std::uint16_t{0};
}

// A code as audio. The caller passes `maxValue` because the dither loop has it already: `codeToLevel` recomputes it
// from the bit depth, but a per-sample recompute there would be wasted work in `processLevels`.
float levelOf(std::uint32_t code, float maxValue) noexcept {
    return (static_cast<float>(code) / maxValue) * 2.0f - 1.0f;
}

} // namespace

// Same input, same seed on any host: the hash reads each float's binary32 bytes least-significant first, which on a
// little-endian host is their order in memory, so they are hashed in place; elsewhere each float is reordered into
// four bytes and the hash continued over them, which gives the same value without a copy of the buffer.
std::uint32_t inputSeed(const float* in, std::size_t frames) noexcept {
    std::uint64_t hash = rp::hash::kFnv1a64Basis;
    if constexpr (std::endian::native == std::endian::little) {
        hash = rp::hash::fnv1a64(reinterpret_cast<const std::uint8_t*>(in), frames * sizeof(float));
    } else {
        for (std::size_t i = 0; i < frames; ++i) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(in[i]);
            const std::uint8_t bytes[4] = {
                static_cast<std::uint8_t>(bits),       static_cast<std::uint8_t>(bits >> 8),
                static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 24),
            };
            hash = rp::hash::fnv1a64(bytes, sizeof(bytes), hash);
        }
    }
    return static_cast<std::uint32_t>(hash ^ (hash >> 32));
}

float codeToLevel(std::uint32_t code, int bits) noexcept {
    bits = std::clamp(bits, kMinDitherBits, kMaxDitherBits);
    return levelOf(code, static_cast<float>((1 << bits) - 1));
}

void Dither::init(DitherAlgorithm algo, int bitDepth, std::uint32_t seed) noexcept {
    algorithm = algo;
    bits      = std::clamp(bitDepth, kMinDitherBits, kMaxDitherBits);
    maxValue  = static_cast<float>((1 << bits) - 1);
    halfScale = maxValue / 2.0f;

    std::uint32_t salt = 0;
    if (algo == DitherAlgorithm::HighPassTPDF) salt = kHighPassTPDFSalt;
    if (algo == DitherAlgorithm::ShapedTPDF) salt = kShapedTPDFSalt;
    rng.seed(seed ^ salt);

    prevDither = shapingState = 0.0f;
    err1 = err2 = 0.0f;
    error = 0.0f;
    std::fill(std::begin(past), std::end(past), 0.0f);
    pastCount = 0;
}

// Each algorithm is the kit path's loop over the stream, with what it carries from one sample to the next kept in the
// struct rather than in locals. Sierra Lite and JJN are forward diffusions, whose legacy form runs over a padded copy
// of the buffer and adds each sample's error into the samples ahead of it; a sample's value there is its scaled input
// plus the errors of the samples behind it, added oldest first. Keeping those errors (`past`, newest first) and adding
// them oldest first gives the same float sums in the same order with no copy, so no allocation and any block split
// (Dsp.test.cpp holds the two forms equal). A term for a sample before the stream's first is skipped rather than added
// as zero, as the padded form has none to add.
template <class Emit>
void Dither::run(const float* in, std::size_t frames, Emit emit) noexcept {
    constexpr float quantum = 1.0f;

    switch (algorithm) {
        case DitherAlgorithm::None:
            for (std::size_t i = 0; i < frames; ++i) {
                const float v = (std::clamp(in[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
                emit(i, std::round(v * maxValue));
            }
            break;

        case DitherAlgorithm::HighPassTPDF:
            for (std::size_t i = 0; i < frames; ++i) {
                const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
                float shaped = dither - prevDither * 0.94f;
                prevDither = dither;
                shaped -= shapingState * 0.5f;
                float value = (in[i] + 1.0f) * halfScale + shaped;
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                shapingState = q - value;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::ShapedTPDF:
            for (std::size_t i = 0; i < frames; ++i) {
                const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
                // Coefficients tuned for ~11 kHz target rate (LSDJ kit sample rate)
                float value = (in[i] + 1.0f) * halfScale + dither + (1.623f * err1) - (0.982f * err2);
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                const float qe = q - value;
                err2 = err1;
                err1 = qe;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::ErrorDiffusion:
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                value += error * 0.9f;
                value = std::clamp(value, 0.0f, maxValue);
                const float q = std::round(value);
                error = value - q;
                emit(i, q);
            }
            break;

        case DitherAlgorithm::JJN: {
            constexpr float w1 = 7.0f / 48.0f;
            constexpr float w2 = 5.0f / 48.0f;
            constexpr float w3 = 3.0f / 48.0f;
            constexpr float w4 = 1.0f / 48.0f;
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                if (pastCount >= 4) value += past[3] * w4;
                if (pastCount >= 3) value += past[2] * w3;
                if (pastCount >= 2) value += past[1] * w2;
                if (pastCount >= 1) value += past[0] * w1;
                const float q = std::round(std::clamp(value, 0.0f, maxValue));
                past[3] = past[2]; past[2] = past[1]; past[1] = past[0];
                past[0] = value - q;
                if (pastCount < 4) ++pastCount;
                emit(i, q);
            }
            break;
        }

        case DitherAlgorithm::SierraLite:
            for (std::size_t i = 0; i < frames; ++i) {
                float value = (in[i] + 1.0f) * halfScale;
                if (pastCount >= 2) value += past[1] * 0.25f;
                if (pastCount >= 1) value += past[0] * 0.5f;
                const float q = std::round(std::clamp(value, 0.0f, maxValue));
                past[1] = past[0];
                past[0] = value - q;
                if (pastCount < 2) ++pastCount;
                emit(i, q);
            }
            break;

        default:
            // A value outside the enum (a cast, never a parse) quantises as plain rounding would, rather than leaving
            // the output unwritten.
            for (std::size_t i = 0; i < frames; ++i) {
                const float v = (std::clamp(in[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
                emit(i, std::round(v * maxValue));
            }
            break;
    }
}

void Dither::process(const float* in, std::uint16_t* codes, std::size_t frames) noexcept {
    run(in, frames, [codes](std::size_t i, float q) noexcept { codes[i] = toCode(q); });
}

void Dither::processLevels(const float* in, float* out, std::size_t frames) noexcept {
    const float maxValue_ = maxValue;
    run(in, frames, [out, maxValue_](std::size_t i, float q) noexcept { out[i] = levelOf(toCode(q), maxValue_); });
}

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Dither.hpp`:

````cpp
#pragma once

// The dither / quantise kernel: audio in [-1, 1] to integer codes 0 .. 2^bits - 1.
//
// Six algorithms. `None` is plain rounding, which is what the kit path does when no dither is set; the other five are
// the kit effect's, ported from the legacy build's AudioDithering helpers. Their arithmetic is the kit path's to the
// bit: a code is `round(clamp(value, 0, 2^bits - 1))` of the input scaled by `(v + 1) * (2^bits - 1) / 2` plus that
// algorithm's error or noise term. The two TPDF algorithms draw their noise from an `mt19937` seeded once, at `init`,
// so the codes are a function of the input, the algorithm, the bit depth and the seed alone.
//
// Streaming: every algorithm's state is a few floats plus the noise generator (the error-diffusion forms keep their
// past errors, one for ErrorDiffusion, two for Sierra Lite and four for JJN, rather than a padded copy of the buffer),
// so any split of a stream into blocks gives the codes one call over the whole of it gives.

#include <cstddef>
#include <cstdint>
#include <random>

namespace rp::dsp {

enum class DitherAlgorithm : std::uint32_t {
    None           = 0,  // round(((clamp(v, -1, 1) + 1) * 0.5) * (2^bits - 1))
    HighPassTPDF   = 1,  // TPDF noise high-passed, with first-order error feedback (the LSDj kit default)
    ShapedTPDF     = 2,  // TPDF noise with a second-order error-shaping filter tuned for ~11 kHz
    ErrorDiffusion = 3,  // one error term, fed back at 0.9
    JJN            = 4,  // Jarvis-Judice-Ninke's weights along one dimension: 7, 5, 3, 1 / 48 over four samples
    SierraLite     = 5,  // Sierra Lite's along one dimension: 1/2, 1/4 over two samples
};

inline constexpr int kMinDitherBits = 1;
inline constexpr int kMaxDitherBits = 16;

// The seed the kit path draws its noise from: FNV-1a 64 over the samples' IEEE-754 bytes, least-significant byte of
// each float first whatever the host's byte order, folded to 32 bits. A function of the samples alone; `init` salts it
// with the algorithm. The kit path hashes the whole buffer the dither quantises (the resampled sample at the kit rate,
// before it is trimmed to whole frames), so a driver that runs the kernel over that buffer in blocks hashes all of it
// with this first, and passes the result to `init`. Allocates nothing.
std::uint32_t inputSeed(const float* in, std::size_t frames) noexcept;

// What a code means as audio: `(code / (2^bits - 1)) * 2 - 1`, the level the LSDj kit decoder gives a 4-bit code, so
// codes mapped back through this are the quantised signal (a bit crusher's output).
float codeToLevel(std::uint32_t code, int bits) noexcept;

struct Dither {
    DitherAlgorithm algorithm = DitherAlgorithm::None;
    int             bits      = 4;
    float           maxValue  = 15.0f;  // 2^bits - 1
    float           halfScale = 7.5f;   // maxValue / 2

    std::mt19937 rng;                  // the TPDF noise, seeded at init
    float        prevDither   = 0.0f;  // HighPassTPDF
    float        shapingState = 0.0f;  // HighPassTPDF
    float        err1 = 0.0f, err2 = 0.0f;  // ShapedTPDF: the last two quantisation errors, newest first
    float        error = 0.0f;              // ErrorDiffusion
    float        past[4] = {};              // JJN / Sierra Lite: the last errors, newest first
    std::uint32_t pastCount = 0;            // how many of `past` the stream has produced yet (at most 4)

    // `bits` is clamped to [kMinDitherBits, kMaxDitherBits]. `seed` is read by the two TPDF algorithms only; each
    // salts it with its own constant, so the two over one buffer and one seed do not draw the same noise.
    void init(DitherAlgorithm algo, int bitDepth, std::uint32_t seed) noexcept;

    // Codes out, 0 .. 2^bits - 1.
    void process(const float* in, std::uint16_t* codes, std::size_t frames) noexcept;

    // The same codes as audio levels (codeToLevel), for a node that outputs audio. `in` and `out` may be the same
    // buffer.
    void processLevels(const float* in, float* out, std::size_t frames) noexcept;

private:
    template <class Emit>
    void run(const float* in, std::size_t frames, Emit emit) noexcept;
};

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Gain.cpp`:

````cpp
#include "dsp/Gain.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {

void Gain::process(const float* in, float* out, std::size_t frames) const noexcept {
    for (std::size_t i = 0; i < frames; ++i) out[i] = in[i] * gain;
}

float peak(const float* in, std::size_t frames) noexcept {
    float maxAmp = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) maxAmp = std::max(maxAmp, std::abs(in[i]));
    return maxAmp;
}

float normalizedGain(float gain, float peakLevel) noexcept {
    if (peakLevel > 0.0f) gain *= 1.0f / peakLevel;
    return gain;
}

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Gain.hpp`:

````cpp
#pragma once

// The gain kernel and the whole-buffer peak the kit path normalises with.
//
// Every kernel in rp::dsp has the same shape: a plain state struct, an `init` that runs on the control thread, and
// a `process` that is `noexcept`, allocates nothing and may be handed any split of a stream into blocks, giving the
// same samples as one call over the whole of it (the resampler's input and output counts differ, so it takes input
// and gives output in two calls, Resample.hpp). They know nothing of a graph, a core or a file: the kit compilers
// run them in loops over whole buffers, and the rp.dsp nodes wrap the same structs.

#include <cstddef>

namespace rp::dsp {

// A linear multiply, exact: out = in * gain, sample by sample. A held gain needs no state, so a block split can
// change nothing.
struct Gain {
    float gain = 1.0f;

    void init(float linear) noexcept { gain = linear; }

    // `in` and `out` may be the same buffer.
    void process(const float* in, float* out, std::size_t frames) const noexcept;
};

// The largest absolute sample of a whole buffer, 0 for an empty one. The fold is `std::max(peak, |v|)` from 0 in
// order, which is the kit path's: a NaN sample leaves the running peak where it was.
float peak(const float* in, std::size_t frames) noexcept;

// The gain that peak-normalises a buffer whose peak is `peakLevel`, folded into a level `gain` already set:
// `gain * (1 / peakLevel)`, and `gain` alone for silence. Spelled as one function because the kit path's bytes depend
// on it being a multiply by the reciprocal rather than a division, so D2's graph normalise must compute it the same.
float normalizedGain(float gain, float peakLevel) noexcept;

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Resample.cpp`:

````cpp
#include "dsp/Resample.hpp"

#include <algorithm>
#include <cassert>

#include <CDSPResampler.h>

namespace rp::dsp {

struct Resampler::Impl {
    Impl(double inRate, double outRate)
        : resampler(inRate, outRate, static_cast<int>(kChunk)) {
        chunk.alloc(static_cast<int>(kChunk));
        maxOut = static_cast<std::size_t>(resampler.getMaxOutLen(static_cast<int>(kChunk)));
        store.assign(2 * maxOut, 0.0f);
    }

    r8b::CDSPResampler24      resampler;
    r8b::CFixedBuffer<double> chunk;  // the chunk being gathered, as the doubles r8brain reads
    std::size_t               fill = 0;
    std::vector<float>        store;  // a ring of resampled frames
    std::size_t               maxOut = 0;  // the most one chunk can produce
    std::size_t               read = 0, count = 0;
};

Resampler::Resampler() = default;
Resampler::~Resampler() = default;
Resampler::Resampler(Resampler&&) noexcept = default;
Resampler& Resampler::operator=(Resampler&&) noexcept = default;

void Resampler::init(double inRate, double outRate) {
    impl_.reset();
    impl_ = std::make_unique<Impl>(inRate, outRate);
}

// Resamples the full chunk into the store, if the store has room for the most it can produce.
bool Resampler::flushChunk() noexcept {
    Impl& s = *impl_;
    const std::size_t capacity = s.store.size();
    if (capacity - s.count < s.maxOut) return false;

    double* produced = nullptr;
    const int n = s.resampler.process(s.chunk.getPtr(), static_cast<int>(kChunk), produced);
    // `maxOut` is getMaxOutLen's bound, and the whole ring-size rule rests on r8brain staying inside it: a chunk that
    // returned more would overwrite frames not yet pulled. (An assert rather than a clamp: release builds pay nothing.)
    assert(static_cast<std::size_t>(n) <= s.maxOut);
    std::size_t write = (s.read + s.count) % capacity;
    for (int i = 0; i < n; ++i) {
        s.store[write] = static_cast<float>(produced[i]);
        write = write + 1 == capacity ? 0 : write + 1;
    }
    s.count += static_cast<std::size_t>(n);
    s.fill = 0;
    return true;
}

template <class Fill>
std::size_t Resampler::gather(std::size_t frames, Fill fill) noexcept {
    if (!impl_) return 0;
    Impl& s = *impl_;
    std::size_t taken = 0;
    while (taken < frames) {
        if (s.fill == kChunk && !flushChunk()) break;
        const std::size_t n = std::min(frames - taken, kChunk - s.fill);
        fill(s.chunk.getPtr() + s.fill, taken, n);
        s.fill += n;
        taken += n;
    }
    // A chunk this call completed is resampled now when the store has room, so its output can be pulled at once.
    if (s.fill == kChunk) flushChunk();
    return taken;
}

std::size_t Resampler::push(const float* in, std::size_t frames) noexcept {
    return gather(frames, [in](double* dst, std::size_t from, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<double>(in[from + i]);
    });
}

std::size_t Resampler::pushSilence(std::size_t frames) noexcept {
    return gather(frames, [](double* dst, std::size_t, std::size_t n) noexcept { std::fill_n(dst, n, 0.0); });
}

std::size_t Resampler::pull(float* out, std::size_t frames) noexcept {
    if (!impl_) return 0;
    Impl& s = *impl_;
    const std::size_t capacity = s.store.size();
    const std::size_t n = std::min(frames, s.count);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = s.store[s.read];
        s.read = s.read + 1 == capacity ? 0 : s.read + 1;
    }
    s.count -= n;
    // A chunk left waiting for room is resampled as soon as there is some.
    if (s.fill == kChunk) flushChunk();
    return n;
}

std::size_t Resampler::available() const noexcept {
    return impl_ ? impl_->count : 0;
}

std::size_t resampledLength(std::size_t frames, double inRate, double outRate) noexcept {
    return static_cast<std::size_t>(static_cast<double>(frames) * (outRate / inRate));
}

void resample(double inRate, double outRate, const float* in, std::size_t frames, std::vector<float>& out) {
    if (frames == 0) {
        out.clear();
        return;
    }
    Resampler r;
    r.init(inRate, outRate);
    const std::size_t length = resampledLength(frames, inRate, outRate);
    out.resize(length);

    std::size_t consumed = 0, written = 0;
    while (written < length) {
        if (consumed < frames) consumed += r.push(in + consumed, frames - consumed);
        else r.pushSilence(Resampler::kChunk);
        written += r.pull(out.data() + written, length - written);
    }
}

} // namespace rp::dsp
````

Create `packages/native/src/dsp/Resample.hpp`:

````cpp
#pragma once

// The resample kernel: r8brain's CDSPResampler24, fed fixed 1024-frame chunks.
//
// r8brain's output depends on how its input is split into calls, so the kernel always hands it a whole chunk: input is
// gathered into a 1024-frame chunk and resampled only once the chunk is full, and the stream past the source's end is
// silence (`pushSilence`), which zero-pads the last partial chunk and every one after it. The resampler only ever sees
// the stream cut at multiples of 1024 frames from its start, so the output stream is the same however the input
// arrives, in one call or a frame at a time. This is the kit path's resample to the bit: its bytes depend on the
// chunking.
//
// The input and output counts differ, so the kernel takes input and gives output in two calls. `push` gathers input
// and resamples each chunk it completes into an output store sized at `init` for two chunks' worst-case output;
// `pull` reads from that store. When the store cannot take another chunk's output, `push` stops consuming (it returns
// how many frames it took) until `pull` makes room, so neither ever allocates. A puller wanting N frames alternates
// the two until it has them. The resampler and the store are built in `init`, on the control thread: r8brain
// allocates there, and only there (its `process` writes into buffers it sized at construction).

#include <cstddef>
#include <memory>
#include <vector>

namespace rp::dsp {

class Resampler {
public:
    // The frames r8brain is handed per call; its maximum input length, fixed at construction.
    static constexpr std::size_t kChunk = 1024;

    Resampler();
    ~Resampler();
    Resampler(Resampler&&) noexcept;
    Resampler& operator=(Resampler&&) noexcept;

    // Builds the resampler and its stores for `inRate` -> `outRate` (both > 0), discarding any stream in progress.
    // Allocates; throws std::bad_alloc when it cannot, leaving the resampler unbuilt (`ready()` is false) rather than
    // holding the old stream, so a caller that catches the throw can drop it or call `init` again.
    void init(double inRate, double outRate);
    bool ready() const noexcept { return impl_ != nullptr; }

    // Gathers up to `frames` input frames, resampling each chunk it completes. Returns how many it took: fewer than
    // `frames` only when the output store is full, so a caller pulls and pushes the rest. Before `init`, takes none.
    std::size_t push(const float* in, std::size_t frames) noexcept;
    // The same with `frames` frames of silence: the stream after a source's end.
    std::size_t pushSilence(std::size_t frames) noexcept;

    // Reads up to `frames` resampled frames, in stream order; returns how many it read.
    std::size_t pull(float* out, std::size_t frames) noexcept;
    // Resampled frames waiting to be pulled.
    std::size_t available() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    template <class Fill>
    std::size_t gather(std::size_t frames, Fill fill) noexcept;
    bool flushChunk() noexcept;
};

// A whole buffer's resampled length, as the kit path cuts it: floor(frames * (outRate / inRate)), the ratio taken in
// double first.
std::size_t resampledLength(std::size_t frames, double inRate, double outRate) noexcept;

// A whole buffer resampled and cut to resampledLength: the source, then silence until the cut. Empty in, empty out.
void resample(double inRate, double outRate, const float* in, std::size_t frames, std::vector<float>& out);

} // namespace rp::dsp
````

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 0e2111125..82ca815db 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -36,6 +36,30 @@ endif()
 # The shape shared by every native test binary (see the file for why).
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)
 
+# --- retroplug-dsp-kernels: the DSP the kit compilers run, as block-streaming kernels (src/dsp/,
+# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, dither / quantise, and the
+# r8brain resampler. Core-free on purpose: it links the C++ standard library and, for the resampler alone,
+# r8brain, so retroplug-graph-test and the rp.dsp node package can use it with no core, txiki or backend.
+# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.) r8brain is
+# PRIVATE because no header here includes it (Resample.hpp holds it behind a pointer); a static library still
+# hands it to whatever links this one.
+# Threads is found before the target because the resampler object alone brings pthreads and RTTI through r8brain: its
+# filter cache is a std::mutex and its resampler classes are virtual. On a libc that does not fold pthreads into libc
+# this library would otherwise fail to link for a consumer that links nothing else thread-related. It is also why a
+# Pico build wants a source list without Resample.cpp.
+find_package(Threads REQUIRED)
+add_library(retroplug-dsp-kernels STATIC
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
+target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
+target_link_libraries(retroplug-dsp-kernels PRIVATE r8brain Threads::Threads)
+set_target_properties(retroplug-dsp-kernels PROPERTIES
+    CXX_STANDARD 20
+    CXX_STANDARD_REQUIRED ON
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
+
 # --- retroplug-core: the shared emulator core, as a neutrally-named static lib. Extracted out of
 # packages/native/cli (which also compiled the CLI-only HarnessRpcService) so the host links ONLY the
 # shared core, never a CLI RPC surface it doesn't use. The legacy `retroplug-cli-core` is now a thin
@@ -58,7 +82,7 @@ add_library(retroplug-core STATIC
     ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/LsdjKitCodec.cpp
-    # risa NES-DPCM (DMC) codec — reuses the generic kit pipeline (r8brain resample + enkiTS), swaps the
+    # risa NES-DPCM (DMC) codec: reuses the generic kit pipeline (the dsp kernels + enkiTS), swaps the
     # pack stage for 1-bit delta DPCM + an 8 KB kit bank.
     ${CMAKE_CURRENT_SOURCE_DIR}/src/risa/RisaDmcCodec.cpp
     # NOTE: the LSDj sav codec + model were removed — they are now pure TS
@@ -97,7 +121,7 @@ target_link_libraries(retroplug-core PUBLIC
     sameboy
     mesen
     rpcpp     # transitively pulls reflectcpp
-    r8brain   # used by KitUtil for resampling
+    retroplug-dsp-kernels  # the kit path's gain, biquad, dither and r8brain resample (src/dsp/)
     enkiTS    # used by KitCompiler for per-sample parallel compile
     miniaudio # header-only audio decoder used by SampleCache
     miniz     # used by projectConfigToZip for --save-rplg
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index ae3dd9d9d..0ccee009b 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -38,7 +38,7 @@ if(MSVC)
     set(_rp_werror_targets
         retroplug-core retroplug-backend retroplug-host retroplug-cli retroplug-sdl
         retroplug retroplug-dsp retroplug-ui
-        retroplug-graph-exec retroplug-graph)
+        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels)
     foreach(_t IN LISTS _rp_werror_targets)
         if(TARGET ${_t})
             target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
diff --git a/packages/native/src/lsdj/Effects.cpp b/packages/native/src/lsdj/Effects.cpp
index ccc34cd6c..18902e8ed 100644
--- a/packages/native/src/lsdj/Effects.cpp
+++ b/packages/native/src/lsdj/Effects.cpp
@@ -1,329 +1,80 @@
 #include "lsdj/Effects.hpp"
 
-#include <bit>
-#include <cmath>
 #include <cstdint>
 
-#include "util/Hash.hpp"
+#include "dsp/Biquad.hpp"
+#include "dsp/Dither.hpp"
+#include "dsp/Gain.hpp"
 
 namespace rp::lsdj {
 
 namespace {
 
-// Biquad state. Coefficients are normalized to a0 = 1.0 in the calculators
-// below — the divisions happen during coefficient setup, so the inner loop
-// is a straight transposed-direct-form-1 implementation.
-struct FilterState {
-    float sampleRate = 48000.0f;
-    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
-    float a1 = 0.0f, a2 = 0.0f;
-    float x1 = 0.0f, x2 = 0.0f;
-    float y1 = 0.0f, y2 = 0.0f;
-};
-
-void calculateLowPass(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha;
-    s.b0 = (1.0f - cw) / (2.0f * a0);
-    s.b1 = (1.0f - cw) / a0;
-    s.b2 = (1.0f - cw) / (2.0f * a0);
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha) / a0;
-}
-
-void calculateHighPass(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha;
-    s.b0 =  (1.0f + cw) / (2.0f * a0);
-    s.b1 = -(1.0f + cw) / a0;
-    s.b2 =  (1.0f + cw) / (2.0f * a0);
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha) / a0;
-}
-
-void calculateBandPass(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha;
-    s.b0 =  sw / (2.0f * a0);
-    s.b1 = 0.0f;
-    s.b2 = -sw / (2.0f * a0);
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha) / a0;
-}
-
-void calculateBandStop(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha;
-    s.b0 = 1.0f / a0;
-    s.b1 = (-2.0f * cw) / a0;
-    s.b2 = 1.0f / a0;
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha) / a0;
-}
-
-void calculatePeak(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float A = std::pow(10.0f, effect.gain / 40.0f);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha / A;
-    s.b0 = (1.0f + alpha * A) / a0;
-    s.b1 = (-2.0f * cw) / a0;
-    s.b2 = (1.0f - alpha * A) / a0;
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha / A) / a0;
-}
-
-void calculateLowShelf(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float A = std::pow(10.0f, effect.gain / 40.0f);
-    const float beta = std::sqrt(A) / effect.q;
-    const float a0 = (A + 1.0f) + (A - 1.0f) * cw + beta * sw;
-    s.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + beta * sw)) / a0;
-    s.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
-    s.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - beta * sw)) / a0;
-    s.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
-    s.a2 = ((A + 1.0f) + (A - 1.0f) * cw - beta * sw) / a0;
-}
-
-void calculateHighShelf(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float A = std::pow(10.0f, effect.gain / 40.0f);
-    const float beta = std::sqrt(A) / effect.q;
-    const float a0 = (A + 1.0f) - (A - 1.0f) * cw + beta * sw;
-    s.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + beta * sw)) / a0;
-    s.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
-    s.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - beta * sw)) / a0;
-    s.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
-    s.a2 = ((A + 1.0f) - (A - 1.0f) * cw - beta * sw) / a0;
-}
-
-void calculateAllPass(const FilterEffect& effect, FilterState& s) {
-    const float omega = 2.0f * kPi * effect.frequency / s.sampleRate;
-    const float sw = std::sin(omega), cw = std::cos(omega);
-    const float alpha = sw / (2.0f * effect.q);
-    const float a0 = 1.0f + alpha;
-    s.b0 = (1.0f - alpha) / a0;
-    s.b1 = (-2.0f * cw) / a0;
-    s.b2 = (1.0f + alpha) / a0;
-    s.a1 = (-2.0f * cw) / a0;
-    s.a2 = (1.0f - alpha) / a0;
-}
-
-void updateCoefficients(const FilterEffect& effect, FilterState& s) {
-    switch (effect.filterType) {
-        case FilterType::LowPass:   calculateLowPass(effect, s);   break;
-        case FilterType::HighPass:  calculateHighPass(effect, s);  break;
-        case FilterType::BandPass:  calculateBandPass(effect, s);  break;
-        case FilterType::BandStop:  calculateBandStop(effect, s);  break;
-        case FilterType::Peak:      calculatePeak(effect, s);      break;
-        case FilterType::LowShelf:  calculateLowShelf(effect, s);  break;
-        case FilterType::HighShelf: calculateHighShelf(effect, s); break;
-        case FilterType::AllPass:   calculateAllPass(effect, s);   break;
-    }
-}
-
-// ----- Dither algorithms (ported from the legacy build's AudioDithering.h) -----
-
-void errorDiffusion(std::vector<float>& buf, int bitDepth) {
-    const int   levels    = 1 << bitDepth;
-    const float maxValue  = static_cast<float>(levels - 1);
-    const float halfScale = maxValue / 2.0f;
-    float error = 0.0f;
-    for (float& v : buf) {
-        float value = (v + 1.0f) * halfScale;
-        value += error * 0.9f;
-        value = std::clamp(value, 0.0f, maxValue);
-        const float q = std::round(value);
-        error = value - q;
-        v = q;
-    }
-}
-
-void sierraLite(std::vector<float>& buf, int bitDepth) {
-    const int   levels    = 1 << bitDepth;
-    const float maxValue  = static_cast<float>(levels - 1);
-    const float halfScale = maxValue / 2.0f;
-    const std::size_t n = buf.size();
-    std::vector<float> padded(n + 2, 0.0f);
-    for (std::size_t i = 0; i < n; ++i)
-        padded[i] = (buf[i] + 1.0f) * halfScale;
-
-    for (std::size_t i = 0; i < n; ++i) {
-        const float value = padded[i];
-        const float q = std::round(std::clamp(value, 0.0f, maxValue));
-        buf[i] = q;
-        const float err = value - q;
-        if (i + 1 < n) padded[i + 1] += err * 0.5f;
-        if (i + 2 < n) padded[i + 2] += err * 0.25f;
-    }
-}
-
-void jjnErrorDiffusion(std::vector<float>& buf, int bitDepth) {
-    const int   levels    = 1 << bitDepth;
-    const float maxValue  = static_cast<float>(levels - 1);
-    const float halfScale = maxValue / 2.0f;
-    const std::size_t n = buf.size();
-    std::vector<float> padded(n + 4, 0.0f);
-    for (std::size_t i = 0; i < n; ++i)
-        padded[i] = (buf[i] + 1.0f) * halfScale;
-
-    constexpr float w1 = 7.0f / 48.0f;
-    constexpr float w2 = 5.0f / 48.0f;
-    constexpr float w3 = 3.0f / 48.0f;
-    constexpr float w4 = 1.0f / 48.0f;
-
-    for (std::size_t i = 0; i < n; ++i) {
-        const float value = padded[i];
-        const float q = std::round(std::clamp(value, 0.0f, maxValue));
-        buf[i] = q;
-        const float err = value - q;
-        if (i + 1 < n) padded[i + 1] += err * w1;
-        if (i + 2 < n) padded[i + 2] += err * w2;
-        if (i + 3 < n) padded[i + 3] += err * w3;
-        if (i + 4 < n) padded[i + 4] += err * w4;
-    }
-}
-
-// One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
-// `DitherType`'s ordinal, so the order the enum happens to declare its algorithms in cannot re-seed a kit that uses
-// one of them.
-constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fnv1a64("HighPassTPDF")
-constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fnv1a64("ShapedTPDF")
-
-// The buffer's samples as the bytes FNV-1a hashes, least-significant byte of each float first whatever the host's
-// own byte order, so a big-endian build seeds the same as a little-endian one. A float is IEEE-754 binary32 on every
-// target; on a little-endian host the bytes are already in that order, so they are hashed in place.
-std::uint64_t hashFloats(const std::vector<float>& buf) {
-    if constexpr (std::endian::native == std::endian::little) {
-        return rp::hash::fnv1a64(reinterpret_cast<const std::uint8_t*>(buf.data()),
-                                 buf.size() * sizeof(float));
-    } else {
-        std::vector<std::uint8_t> bytes(buf.size() * sizeof(float));
-        for (std::size_t i = 0; i < buf.size(); ++i) {
-            const std::uint32_t bits = std::bit_cast<std::uint32_t>(buf[i]);
-            for (std::size_t b = 0; b < sizeof(float); ++b) {
-                bytes[i * sizeof(float) + b] = static_cast<std::uint8_t>(bits >> (8 * b));
-            }
-        }
-        return rp::hash::fnv1a64(bytes.data(), bytes.size());
+// The effect's filter list is the kernel's, value for value, so a filter type crosses by a cast: each cast the
+// effect-to-kernel mapping makes must land on the matching filter.
+static_assert(static_cast<dsp::BiquadType>(FilterType::LowPass) == dsp::BiquadType::LowPass);
+static_assert(static_cast<dsp::BiquadType>(FilterType::HighPass) == dsp::BiquadType::HighPass);
+static_assert(static_cast<dsp::BiquadType>(FilterType::BandPass) == dsp::BiquadType::BandPass);
+static_assert(static_cast<dsp::BiquadType>(FilterType::BandStop) == dsp::BiquadType::BandStop);
+static_assert(static_cast<dsp::BiquadType>(FilterType::Peak) == dsp::BiquadType::Peak);
+static_assert(static_cast<dsp::BiquadType>(FilterType::LowShelf) == dsp::BiquadType::LowShelf);
+static_assert(static_cast<dsp::BiquadType>(FilterType::HighShelf) == dsp::BiquadType::HighShelf);
+static_assert(static_cast<dsp::BiquadType>(FilterType::AllPass) == dsp::BiquadType::AllPass);
+
+constexpr int kBitDepth = 4;  // LSDJ kit samples are 4-bit
+
+// The kernel has a sixth algorithm (`None`, plain rounding) the effect cannot name, so the dither list maps by name.
+dsp::DitherAlgorithm algorithmOf(const DitherEffect* dither) {
+    if (!dither) return dsp::DitherAlgorithm::None;
+    switch (dither->ditherType) {
+        case DitherType::HighPassTPDF:   return dsp::DitherAlgorithm::HighPassTPDF;
+        case DitherType::ShapedTPDF:     return dsp::DitherAlgorithm::ShapedTPDF;
+        case DitherType::ErrorDiffusion: return dsp::DitherAlgorithm::ErrorDiffusion;
+        case DitherType::JJN:            return dsp::DitherAlgorithm::JJN;
+        case DitherType::SierraLite:     return dsp::DitherAlgorithm::SierraLite;
     }
+    return dsp::DitherAlgorithm::None;
 }
 
-// A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
-// mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
-// three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
-float uniform01(std::mt19937& rng) {
-    return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
-}
-
-// The seed both TPDF dithers draw their noise from: `hashFloats` over the buffer this pass quantises, folded to
-// `mt19937`'s 32-bit word and salted with the algorithm, so the two dithers over one buffer do not run the same
-// sequence. Drawing it from the input is what makes a compile reproducible: a kit bank is a file, and two compiles of
-// the same sample have to be the same file. The seed is a function of the buffer's contents and the algorithm only,
-// not of the bit depth or any other setting.
-//
-// The input is the buffer the dither is handed, which for `KitUtil::compileSample` is the windowed, post-effect,
-// resampled sample at the Game Boy kit rate (11468 Hz) as the dither receives it, BEFORE `frameAlign` trims it to
-// whole frames - not the source-rate window the gain and filter see, and not the trimmed bank.
-//
-// Same input, same noise on any host: the draw and the hash are both specified, so the dither's own arithmetic is
-// exact wherever a float is binary32. A whole bank is byte-identical only where the resampler and the filters agree
-// too, which is why exact kit bytes are compared on one reference target.
-std::mt19937 ditherRng(const std::vector<float>& buf, std::uint32_t salt) {
-    const std::uint64_t hash = hashFloats(buf);
-    const std::uint32_t folded = static_cast<std::uint32_t>(hash ^ (hash >> 32));
-    return std::mt19937(folded ^ salt);
-}
-
-void highPassTPDF(std::vector<float>& buf, int bitDepth) {
-    const int   levels    = 1 << bitDepth;
-    const float maxValue  = static_cast<float>(levels - 1);
-    const float halfScale = maxValue / 2.0f;
-    constexpr float quantum = 1.0f;
-
-    // A per-call RNG so two parallel sample compiles don't share state, seeded from the input so they agree.
-    std::mt19937 rng = ditherRng(buf, kHighPassTPDFSalt);
-
-    float prevDither   = 0.0f;
-    float shapingState = 0.0f;
-
-    for (float& v : buf) {
-        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
-        float shaped = dither - prevDither * 0.94f;
-        prevDither = dither;
-        shaped -= shapingState * 0.5f;
-        float value = (v + 1.0f) * halfScale + shaped;
-        value = std::clamp(value, 0.0f, maxValue);
-        const float q = std::round(value);
-        shapingState = q - value;
-        v = q;
-    }
-}
-
-void shapedTPDF2ndOrder(std::vector<float>& buf, int bitDepth) {
-    const int   levels    = 1 << bitDepth;
-    const float maxValue  = static_cast<float>(levels - 1);
-    const float halfScale = maxValue / 2.0f;
-    constexpr float quantum = 1.0f;
-
-    std::mt19937 rng = ditherRng(buf, kShapedTPDFSalt);
+} // namespace
 
-    float err1 = 0.0f, err2 = 0.0f;
-    for (float& v : buf) {
-        const float dither = (uniform01(rng) + uniform01(rng) - 1.0f) * quantum * 0.5f;
-        // Coefficients tuned for ~11 kHz target rate (LSDJ kit sample rate)
-        float value = (v + 1.0f) * halfScale + dither + (1.623f * err1) - (0.982f * err2);
-        value = std::clamp(value, 0.0f, maxValue);
-        const float q = std::round(value);
-        const float qe = q - value;
-        err2 = err1;
-        err1 = qe;
-        v = q;
-    }
+void processEffect(const GainEffect& effect,
+                   std::vector<float>& target,
+                   float /*sampleRate*/) {
+    float gain = effect.gain;
+    if (effect.normalize) gain = dsp::normalizedGain(gain, dsp::peak(target.data(), target.size()));
+    dsp::Gain kernel;
+    kernel.init(gain);
+    kernel.process(target.data(), target.data(), target.size());
 }
 
-} // namespace
-
 void processEffect(const FilterEffect& effect,
                    std::vector<float>& target,
                    float sampleRate) {
-    FilterState s;
-    s.sampleRate = sampleRate;
-    updateCoefficients(effect, s);
+    dsp::Biquad kernel;
+    kernel.init({static_cast<dsp::BiquadType>(effect.filterType), sampleRate, effect.frequency, effect.q, effect.gain});
+    kernel.process(target.data(), target.data(), target.size());
+}
 
-    for (float& sample : target) {
-        const float in = sample;
-        float out = s.b0 * in + s.b1 * s.x1 + s.b2 * s.x2
-                  - s.a1 * s.y1 - s.a2 * s.y2;
-        s.x2 = s.x1; s.x1 = in;
-        s.y2 = s.y1; s.y1 = out;
-        out = std::clamp(out, -1.0f, 1.0f);
-        sample = out;
-    }
+// The seed is the whole buffer's, hashed before the first code: the input the dither is handed, which for
+// `KitUtil::compileSample` is the windowed, post-effect, resampled sample at the Game Boy kit rate (11468 Hz) BEFORE
+// `frameAlign` trims it to whole frames - not the source-rate window the gain and filter see, and not the trimmed
+// bank. Drawing it from the input is what makes a compile reproducible: a kit bank is a file, and two compiles of the
+// same sample have to be the same file. A whole bank is byte-identical across hosts only where the resampler and the
+// filters agree too, which is why exact kit bytes are compared on one reference target.
+std::vector<std::uint16_t> quantise(const std::vector<float>& buffer, const DitherEffect* dither) {
+    std::vector<std::uint16_t> codes(buffer.size());
+    dsp::Dither kernel;
+    kernel.init(algorithmOf(dither), kBitDepth, dsp::inputSeed(buffer.data(), buffer.size()));
+    kernel.process(buffer.data(), codes.data(), buffer.size());
+    return codes;
 }
 
 void processEffect(const DitherEffect& effect,
                    std::vector<float>& target,
                    float /*sampleRate*/) {
-    constexpr int kBitDepth = 4;  // LSDJ kit samples are 4-bit
-    switch (effect.ditherType) {
-        case DitherType::HighPassTPDF:   highPassTPDF       (target, kBitDepth); break;
-        case DitherType::ShapedTPDF:     shapedTPDF2ndOrder (target, kBitDepth); break;
-        case DitherType::ErrorDiffusion: errorDiffusion     (target, kBitDepth); break;
-        case DitherType::JJN:            jjnErrorDiffusion  (target, kBitDepth); break;
-        case DitherType::SierraLite:     sierraLite         (target, kBitDepth); break;
-    }
+    const std::vector<std::uint16_t> codes = quantise(target, &effect);
+    for (std::size_t i = 0; i < target.size(); ++i) target[i] = static_cast<float>(codes[i]);
 }
 
 } // namespace rp::lsdj
diff --git a/packages/native/src/lsdj/Effects.hpp b/packages/native/src/lsdj/Effects.hpp
index 2e21a293a..aa4a36bac 100644
--- a/packages/native/src/lsdj/Effects.hpp
+++ b/packages/native/src/lsdj/Effects.hpp
@@ -1,9 +1,11 @@
 #pragma once
 
+// Neither <algorithm> nor <cmath> has a user in this header any more: the gain fold and the filter set-up moved into
+// src/dsp/, and kPi's only remaining caller is test/audio/Dither.test.cpp. They stay because host/rpc/BackendTypes.hpp
+// re-exports this header across the plugin's DSP surface, and a transitive include is not worth removing blind.
 #include <algorithm>
 #include <cmath>
 #include <cstdint>
-#include <random>
 #include <vector>
 
 #include <rfl/Literal.hpp>
@@ -13,10 +15,13 @@
 // the legacy build's Effects.h + AudioDithering.h.
 //
 // Each effect is a plain-data struct that round-trips via reflectcpp; the
-// `LsdjEffect` TaggedUnion below is the serialized effect descriptor. The
-// per-effect `processEffect(...)` overloads run in `KitUtil::compileKit`
-// against a float buffer in [-1, 1] (or, for dither, expect [-1, 1] and
-// output samples pre-quantized into [0, 2^bitDepth - 1]).
+// `LsdjEffect` TaggedUnion below is the serialized effect descriptor, which the
+// compileKit / compileDmc RPCs decode. The per-effect `processEffect(...)`
+// overloads run in `KitUtil::compileSample` and `RisaDmcCodec::encode` against a
+// float buffer in [-1, 1] (or, for dither, expect [-1, 1] and output samples
+// pre-quantized into [0, 2^bitDepth - 1]). They are whole-buffer loops over the
+// rp::dsp kernels (src/dsp/), which hold the arithmetic; this file only maps an
+// effect's fields onto a kernel's.
 
 namespace rp::lsdj {
 
@@ -32,17 +37,11 @@ struct GainEffect {
     float gain      = 1.0f;
 };
 
-inline void processEffect(const GainEffect& effect,
-                          std::vector<float>& target,
-                          float /*sampleRate*/) {
-    float gain = effect.gain;
-    if (effect.normalize) {
-        float maxAmp = 0.0f;
-        for (float v : target) maxAmp = std::max(maxAmp, std::abs(v));
-        if (maxAmp > 0.0f) gain *= 1.0f / maxAmp;
-    }
-    for (float& v : target) v *= gain;
-}
+// With `normalize`, the whole buffer's peak is measured first and folded into
+// the gain (rp::dsp::peak, rp::dsp::normalizedGain); then one multiply.
+void processEffect(const GainEffect& effect,
+                   std::vector<float>& target,
+                   float sampleRate);
 
 // -----------------------------------------------------------------------
 // FilterEffect — RBJ-cookbook biquads.
@@ -75,9 +74,8 @@ void processEffect(const FilterEffect& effect,
 // DitherEffect — bit-depth reduction with noise shaping.
 //
 // Input  : samples in [-1, 1].
-// Output : samples already quantised into [0, 2^bitDepth - 1] floats — the
-//          kit-compile pipeline passes the buffer straight to
-//          SampleUtil::convertScaledF32ToNibbles after the dither pass.
+// Output : samples already quantised into [0, 2^bitDepth - 1] floats (the
+//          codes `quantise` below gives, as floats).
 //
 // Five algorithms preserved verbatim from the legacy AudioDithering helpers.
 // `HighPassTPDF` is the LSDJ legacy default.
@@ -100,6 +98,13 @@ void processEffect(const DitherEffect& effect,
                    std::vector<float>& target,
                    float sampleRate);
 
+// LSDj's 4-bit sample codes (0..15) for a whole buffer at the kit rate: through
+// `dither`'s algorithm, or plain rounding when there is none. The TPDF
+// algorithms are seeded from the whole buffer (rp::dsp::inputSeed), so the
+// codes are a function of the buffer and the effect alone.
+std::vector<std::uint16_t> quantise(const std::vector<float>& buffer,
+                                    const DitherEffect* dither);
+
 // `LsdjEffect` is the on-disk variant; serialised under the discriminator
 // key `"type"` to match the legacy spelling.
 using LsdjEffect = rfl::TaggedUnion<"type", GainEffect, FilterEffect, DitherEffect>;
diff --git a/packages/native/src/lsdj/KitUtil.cpp b/packages/native/src/lsdj/KitUtil.cpp
index 86ea8ae31..d6785a410 100644
--- a/packages/native/src/lsdj/KitUtil.cpp
+++ b/packages/native/src/lsdj/KitUtil.cpp
@@ -6,65 +6,11 @@
 #include <cmath>
 #include <cstring>
 
-#include <CDSPResampler.h>
-
+#include "dsp/Resample.hpp"
 #include "lsdj/SampleUtil.hpp"
 
 namespace rp::lsdj::KitUtil {
 
-void convertSamplerate(double inputSampleRate,
-                       double outputSampleRate,
-                       const std::vector<float>& buffer,
-                       std::vector<float>& target) {
-    if (buffer.empty()) {
-        target.clear();
-        return;
-    }
-
-    constexpr std::size_t kInBufCapacity = 1024;
-    r8b::CFixedBuffer<double> inBuf;
-    // Sized to the fixed chunk, NOT the source length: the loop always fills/processes kInBufCapacity
-    // doubles (zero-padding a short tail), so a source shorter than one chunk would otherwise overflow.
-    inBuf.alloc(static_cast<int>(kInBufCapacity));
-
-    // MaxInLen MUST match the fixed block we actually feed (kInBufCapacity), not the source length: the
-    // loop always hands process() a full kInBufCapacity block (zero-padded), so a shorter MaxInLen would
-    // overflow r8brain's internal buffers on a source under one chunk.
-    r8b::CPtrKeeper<r8b::CDSPResampler24*> resampler =
-        new r8b::CDSPResampler24(inputSampleRate, outputSampleRate,
-                                 static_cast<int>(kInBufCapacity));
-
-    const std::size_t targetSize =
-        static_cast<std::size_t>(buffer.size() * (outputSampleRate / inputSampleRate));
-    target.resize(targetSize);
-
-    std::size_t sourcePos = 0;
-    std::size_t targetPos = 0;
-
-    while (targetPos < targetSize) {
-        std::fill_n(inBuf.getPtr(), kInBufCapacity, 0.0);
-
-        const std::size_t chunkSize =
-            std::min(kInBufCapacity, buffer.size() - sourcePos);
-        for (std::size_t i = 0; i < chunkSize; ++i) {
-            inBuf[i] = static_cast<double>(buffer[sourcePos++]);
-        }
-
-        double* targetBuffer = nullptr;
-        std::size_t writeCount = static_cast<std::size_t>(
-            resampler->process(inBuf.getPtr(),
-                               static_cast<int>(kInBufCapacity),
-                               targetBuffer));
-
-        if (targetPos + writeCount > targetSize) {
-            writeCount = targetSize - targetPos;
-        }
-        for (std::size_t i = 0; i < writeCount; ++i) {
-            target[targetPos++] = static_cast<float>(targetBuffer[i]);
-        }
-    }
-}
-
 namespace {
 
 // Trim a length to the nearest lower multiple of the 32-sample frame size,
@@ -122,28 +68,19 @@ std::vector<std::uint8_t> compileSample(const SampleInput& sample) {
 
     // Resample to the GB target rate.
     std::vector<float> resampled;
-    convertSamplerate(static_cast<double>(sample.sampleRate),
+    rp::dsp::resample(static_cast<double>(sample.sampleRate),
                       static_cast<double>(kGameboySampleRate),
-                      window, resampled);
-
-    if (pendingDither) {
-        processEffect(*pendingDither, resampled,
-                      static_cast<float>(kGameboySampleRate));
-        // dither outputs [0, 15] already.
-    } else {
-        // Manual clamp + scale + round so the buffer matches the dither
-        // path's [0, 15] convention before nibble packing.
-        for (float& v : resampled) {
-            v = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f;
-            v = std::round(v * 15.0f);
-        }
-    }
+                      window.data(), window.size(), resampled);
+
+    // Quantise to 4-bit codes at the GB rate: through the dither when one is
+    // set, plain rounding otherwise. The dither sees (and is seeded from) the
+    // whole resampled sample, before the trim below.
+    std::vector<std::uint16_t> codes = quantise(resampled, pendingDither);
 
     // Trim to whole 32-sample frames so the packer can pack them flush.
-    const std::size_t aligned = frameAlign(resampled.size());
-    resampled.resize(aligned);
+    codes.resize(frameAlign(codes.size()));
 
-    SampleUtil::convertScaledF32ToNibbles(resampled, out, sample.rotate);
+    SampleUtil::convertCodesToNibbles(codes, out, sample.rotate);
     return out;
 }
 
diff --git a/packages/native/src/lsdj/KitUtil.hpp b/packages/native/src/lsdj/KitUtil.hpp
index fbf0d7ef5..941a28517 100644
--- a/packages/native/src/lsdj/KitUtil.hpp
+++ b/packages/native/src/lsdj/KitUtil.hpp
@@ -41,14 +41,6 @@ struct Kit {
 
 namespace KitUtil {
 
-// Resample one sample buffer from `inputSampleRate` to `outputSampleRate`
-// using r8brain's CDSPResampler24. Output length is rounded down from
-// `buffer.size() * (outputSampleRate / inputSampleRate)`.
-void convertSamplerate(double inputSampleRate,
-                       double outputSampleRate,
-                       const std::vector<float>& buffer,
-                       std::vector<float>& target);
-
 // A single sample's input to the kit compiler.
 struct SampleInput {
     std::string             name;      // up to 3 chars; uppercased + '-' padded
@@ -63,10 +55,11 @@ struct SampleInput {
 // Resample + apply effects + 4-bit nibble-pack one sample. Output is the
 // raw byte stream destined for the kit bank's sample-data region (no
 // offset-table or naming concerns). Effect ordering matches legacy:
-// non-dither effects run first at the *input* sample rate; dither runs
-// after resampling, on the scaled-to-[0,15] domain. Manual scaling +
-// rounding is performed in the no-dither branch so the path always feeds
-// nibble-packing the same way.
+// non-dither effects run first at the *input* sample rate; the sample is
+// resampled to the GB rate (rp::dsp::resample); then it is quantised to 4-bit
+// codes there, through the dither when one is set and by plain rounding when
+// not (`quantise`, Effects.hpp), so the path always feeds nibble-packing the
+// same way.
 //
 // Returns the compressed bytes. Caller decides how to slot them into a
 // kit (KitUtil::buildKit below).
diff --git a/packages/native/src/lsdj/SampleUtil.hpp b/packages/native/src/lsdj/SampleUtil.hpp
index 3d708707a..7cb1dc1b5 100644
--- a/packages/native/src/lsdj/SampleUtil.hpp
+++ b/packages/native/src/lsdj/SampleUtil.hpp
@@ -71,20 +71,20 @@ inline void convertNibblesToF32WithRotation(const std::vector<std::uint8_t>& inp
     }
 }
 
-// Encode float32 input pre-scaled to [0, 15] (caller responsible for clamp +
-// scale + round) into packed nibbles WITH rotation. Sample i goes to
+// Encode 4-bit codes (0..15: the quantise pass's, KitUtil::compileSample) into
+// packed nibbles WITH rotation. Sample i goes to
 // position (i+1) % 32 inside its frame; values are inverted (stored = 0xF -
 // amp). Both quirks are needed for byte-exact compatibility with kits built
 // by the LSDJ project's own kit tool.
 // `rotate` applies the LSDJ 9.2.0+ rightward frame rotation; pass false for a pre-9.2.0 target ROM (whose
 // playback engine expects un-rotated frames), so import is byte-correct for the actual LSDj version.
-inline void convertScaledF32ToNibbles(const std::vector<float>& input,
-                                      std::vector<std::uint8_t>& output,
-                                      bool rotate = true) {
+inline void convertCodesToNibbles(const std::vector<std::uint16_t>& input,
+                                  std::vector<std::uint8_t>& output,
+                                  bool rotate = true) {
     const std::size_t numChunks = input.size() / SAMPLES_PER_FRAME;
     output.resize(numChunks * BYTES_PER_FRAME);
 
-    const float* src = input.data();
+    const std::uint16_t* src = input.data();
     std::uint8_t* dst = output.data();
 
     for (std::size_t chunk = 0; chunk < numChunks; ++chunk) {
diff --git a/packages/native/src/risa/RisaDmcCodec.cpp b/packages/native/src/risa/RisaDmcCodec.cpp
index 2e683227f..82c078b40 100644
--- a/packages/native/src/risa/RisaDmcCodec.cpp
+++ b/packages/native/src/risa/RisaDmcCodec.cpp
@@ -6,7 +6,7 @@
 #include <type_traits>
 #include <utility>
 
-#include "lsdj/KitUtil.hpp" // convertSamplerate (r8brain)
+#include "dsp/Resample.hpp"
 
 namespace rp::risa {
 namespace {
@@ -147,7 +147,7 @@ std::vector<std::uint8_t> RisaDmcCodec::encode(std::size_t i, const rp::kit::Sam
     }
 
     std::vector<float> resampled;
-    rp::lsdj::KitUtil::convertSamplerate(srcRate, targetRate, window, resampled);
+    rp::dsp::resample(srcRate, targetRate, window.data(), window.size(), resampled);
 
     std::vector<std::uint8_t> dmc = convertF32ToDpcm(resampled, spec.normalize);
     if (dmc.size() > kDmcMaxBytes) dmc.resize(kDmcMaxBytes); // 4081 = 16*255+1 (still a legal length)
diff --git a/packages/native/src/util/Hash.hpp b/packages/native/src/util/Hash.hpp
index baaff9e96..3e7b64a82 100644
--- a/packages/native/src/util/Hash.hpp
+++ b/packages/native/src/util/Hash.hpp
@@ -5,10 +5,14 @@
 
 namespace rp::hash {
 
+inline constexpr std::uint64_t kFnv1a64Basis = 0xcbf29ce484222325ULL;
+
 // FNV-1a 64-bit. Not cryptographic — used for cheap dedup / dirty tracking
-// where collisions are tolerable. ~1 cycle/byte on modern CPUs.
-inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
-    std::uint64_t h = 0xcbf29ce484222325ULL;
+// where collisions are tolerable. ~1 cycle/byte on modern CPUs. Passing the
+// hash of what came before as `h` continues it: hashing a stream in pieces
+// gives the hash of the whole.
+inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size,
+                             std::uint64_t h = kFnv1a64Basis) {
     for (std::size_t i = 0; i < size; ++i) {
         h ^= static_cast<std::uint64_t>(data[i]);
         h *= 0x100000001b3ULL;
diff --git a/packages/retroplug/test-native/lsdj-rom.test.ts b/packages/retroplug/test-native/lsdj-rom.test.ts
index 53e7049d1..42fae313a 100644
--- a/packages/retroplug/test-native/lsdj-rom.test.ts
+++ b/packages/retroplug/test-native/lsdj-rom.test.ts
@@ -133,7 +133,7 @@ test("import-sample splice: compile one sample + rebuild a kit around the existi
   if (!be.fileExists(LSDJ)) skip(`lsdj-rom import: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
   const wav = "/tmp/rp-kit-short.wav";
-  writeTestWav(be, wav, 600); // < 1024 source frames → exercises the convertSamplerate overflow fix
+  writeTestWav(be, wav, 600); // < 1024 source frames → a source shorter than one resampler chunk (rp::dsp::Resampler)
 
   // Compile the one sample (a throwaway 1-sample bank) and pull its nibble bytes.
   const oneBank = audio.compileKit({ name: "", samples: [{ path: wav, name: "NEW", effects: [] }] });
````

Run: `sed -n '3324,4092p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure creates `retroplug-dsp-kernels`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp]"`
Expected: `All tests passed (168939 assertions in 17 test cases)`: the kernels under their own cases.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (346 assertions in 1 test case)`: every hash and slot as Task 2 recorded them. This is the task's contract; a failure here means the kernel arithmetic, the chain's order or the resample chunking moved, and nothing may be re-recorded.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0. A full rebuild prints at most the pre-existing `PluginDSP.cpp:186:59: -Wformat-truncation` (it shows because `Effects.hpp` changed, which recompiles `PluginDSP.cpp` through `BackendTypes.hpp`; the code is untouched and the rebuild after the edits printed none).

- [ ] **Step 3: Show that the cases bite**

Stage 3's red is a compile error, so the kernel cases were written green and their bite is shown by mutations: each is
applied, run, reverted and rebuilt. The first two are the ones the design's finding rests on, because the kit baseline
passes both: Sierra Lite's two forward-error terms swapped (a one-ulp change) moves no 4-bit code in any of the 31
banks, and resampling the partial chunk at each push's end moves no bank either.

Run: `cp packages/native/src/dsp/Dither.cpp /tmp/d1-dither-good.cpp`
Expected: nothing printed (the file as this task placed it, for the restore below).

Run: `sed -i -e 's|if (pastCount >= 2) value += past\[1\] \* 0.25f;|D1SWAPA|' -e 's|if (pastCount >= 1) value += past\[0\] \* 0.5f;|if (pastCount >= 2) value += past[1] * 0.25f;|' -e 's|D1SWAPA|if (pastCount >= 1) value += past[0] * 0.5f;|' packages/native/src/dsp/Dither.cpp && cmake --build build --target retroplug-graph-test retroplug-audio-test -j$(nproc) && { build/bin/retroplug-graph-test "[dsp]"; build/bin/retroplug-audio-test "[kit-baseline]"; }`
Expected: the `sed` swaps Sierra Lite's two forward-error lines (the newest term first), a change of one ulp. Both binaries are rebuilt from the mutated file first, then run in turn: the `[dsp]` run now fails at least the Sierra Lite case, while `[kit-baseline]` still passes 346 assertions in 1 test case. The prototype measured `12 | 11 passed | 1 failed` and 15 assertions at the 13-case file this tag had before the review added four; report this tree's lines.

Run: `cp /tmp/d1-dither-good.cpp packages/native/src/dsp/Dither.cpp && cmake --build build --target retroplug-graph-test retroplug-audio-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp]" && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: nothing printed by the copy, then `All tests passed (168939 assertions in 17 test cases)` and `[kit-baseline]` 346 in 1: restored and green.

Run: `cp packages/native/src/dsp/Resample.cpp /tmp/d1-resample-good.cpp`
Expected: nothing printed (the resampler as this task placed it).

The second mutation is the resampler's, and it is the one that shows why the fixed chunk is part of the bytes: it
resamples whatever its chunk holds at the end of each call instead of waiting for the full `kChunk`, so a host-sized
push flushes a partial chunk. The `sed` edits the three places the fixed chunk appears: `flushChunk`'s `process` call
passes `s.fill`, and both standalone `if (s.fill == kChunk) flushChunk();` lines (in `gather` and in `pull`) become
`if (s.fill > 0) flushChunk();`. The rebuild and the two runs follow.

Run: `sed -i -e 's|static_cast<int>(kChunk), produced)|static_cast<int>(s.fill), produced)|' -e 's|if (s.fill == kChunk) flushChunk();|if (s.fill > 0) flushChunk();|g' packages/native/src/dsp/Resample.cpp && cmake --build build --target retroplug-graph-test retroplug-audio-test -j$(nproc) && { build/bin/retroplug-graph-test "[dsp]"; build/bin/retroplug-audio-test "[kit-baseline]"; }`
Expected: at least one `[dsp]` case fails and `[kit-baseline]` still passes 346 assertions in 1 test case: the prototype's version failed one case with 11 assertions and its first difference at sample 1910 of the 44100 to 16965.4 stream, and moved no kit byte. Report both lines, then restore.

Run: `cp /tmp/d1-resample-good.cpp packages/native/src/dsp/Resample.cpp && cmake --build build --target retroplug-graph-test retroplug-audio-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp]" && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: restored: `[dsp]` 168939 assertions in 17 test cases and `[kit-baseline]` 346 in 1, both green.

- [ ] **Step 4: Run the suites**

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. `retroplug-graph-test` `261505 assertions in 225 test cases` (92566 / 208 plus `[dsp]`), `retroplug-audio-test` `1235181 / 109` (unchanged), engine-graph without fixtures `267 | 258 passed | 9 skipped`, launchpad `144 / 29`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 160 native test file(s) passed`, only dsp-bench and nes-bench skipping. `lsdj-rom` `10 passed` and `risa-kit` `4 passed` (its `assembleKitBank == compileDmc` case byte-identical) are the kit path's own end-to-end cases over the moved code.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, `46 cases skipped in 10 files`, the skip check passing.

Run: `pnpm test && pnpm test:node && pnpm test:cli-ts`
Expected: `# 169 test file(s) passed (jobs=16)`; `PASS: retroplug-cli TypeScript harness`; 12 node tests, 12 pass.

Run: `pnpm typecheck`
Expected: exit 0.

- [ ] **Step 5: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `[dsp]` `168939 / 17` and `[kit-baseline]` `346 / 1`, no ThreadSanitizer report either: the `[dsp]` selection includes a case that runs eight resamplers on parallel threads through r8brain's shared filter cache, and the kit baseline is where enkiTS's fan-out runs.

Run: `cmake --build build-asan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `[dsp]` `168939 / 17` and `[kit-baseline]` `346 / 1`, no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 6: Commit**

````bash
git add -- packages/native/src/dsp/Biquad.cpp packages/native/src/dsp/Biquad.hpp packages/native/src/dsp/Dither.cpp \
  packages/native/src/dsp/Dither.hpp packages/native/src/dsp/Gain.cpp packages/native/src/dsp/Gain.hpp \
  packages/native/src/dsp/Resample.cpp packages/native/src/dsp/Resample.hpp packages/native/test/graph/Dsp.test.cpp \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake packages/native/cmake/warnings.cmake \
  packages/native/src/lsdj/Effects.cpp packages/native/src/lsdj/Effects.hpp packages/native/src/lsdj/KitUtil.cpp \
  packages/native/src/lsdj/KitUtil.hpp packages/native/src/lsdj/SampleUtil.hpp \
  packages/native/src/risa/RisaDmcCodec.cpp packages/native/src/util/Hash.hpp \
  packages/retroplug/test-native/lsdj-rom.test.ts
git commit -F - <<'EOF'
refactor(dsp): the kit DSP is a core-free library of block-streaming kernels

retroplug-dsp-kernels (packages/native/src/dsp/) holds the arithmetic the kit compilers run, as four kernels, each a
state struct with an init on the control thread and a noexcept, allocation-free process that gives the same samples
for any split of its stream into blocks:

- gain: a linear multiply, beside a whole-buffer peak and the normalising gain (gain * (1 / peak)) the kit path
  folds it into;
- biquad: the RBJ setup for all eight types in float, the transposed loop with its feedback taken before the
  output clamp; setup keeps the state for a swept filter, init clears it;
- dither: None (plain rounding) and the five algorithms, a bit depth of 1 to 16, codes out (and the same codes as
  audio levels through codeToLevel); Sierra Lite and JJN keep their last two and four errors instead of a padded
  copy, adding them oldest first so the float sums are the padded form's; the TPDF noise comes from an mt19937
  seeded at init, salted per algorithm, and inputSeed is the one hash of a buffer the kit path (and D2's driver)
  seeds from;
- resample: one CDSPResampler24 built at init and always handed whole 1024-frame chunks, the stream past the source
  silence; push gathers input, pull reads the output store, so the output is the same however the input arrives.
  resampledLength is the offline floor(n * out / in) cut and resample the whole-buffer form.

lsdj/Effects' three effects, the LSDj quantise (dither or plain rounding, now one quantise() giving codes the
nibble packer takes) and the resample in KitUtil::compileSample and RisaDmcCodec::encode are loops over the
kernels; KitUtil::convertSamplerate is gone. The rfl tagged union, the effect structs and the kit specs are
unchanged where the RPCs decode them. The target is not named retroplug-dsp: dpf_add_plugin(retroplug) already
names the plugin's DSP library that. It links r8brain PRIVATE (no header exposes it); retroplug-core links it in
r8brain's place. Threads::Threads is PRIVATE alongside it: the resampler object alone brings pthreads and RTTI
through r8brain (its filter cache is a std::mutex, its resampler classes virtual), so the library declares what it
owes a consumer that links nothing else thread-related. util/Hash.hpp's fnv1a64 takes an optional running hash, so
the seed's big-endian path hashes without a copy.

The kit baseline holds every bank to its recorded hash and slot (31 cases, none re-recorded). Dsp.test.cpp in
retroplug-graph-test ([dsp]) holds each kernel to one call's output under 1-, 7-, 64-, 1024-frame and ragged
splits; an initialised kernel's process to no allocation or free, the C library's included (r8brain's first chunk
too); seeds; Sierra Lite and JJN to written-out padded forms at four bit depths; the input seed to the codes the
audio suite pins; and the resampler to a direct fixed-chunk feed of r8brain, under any push and pull split and on
parallel threads.

The biquad, the largest block of moved arithmetic, is held the same way: the eight calculators and the loop are
written out from lsdj/Effects.cpp and biquadCoefficients is compared field for field, bit for bit, over the eight
types and four filter shapes, with the filtered buffer compared sample for sample. The normalising gain's test
pair is chosen so the two forms differ in binary32, which is what lets it tell gain * (1 / peak) from a division.
The dither's bit-depth clamp, its NaN-to-code rule, the resampler's available count under push and pull, and a
re-initialised resampler's fresh stream are cases as well.
EOF
````

### Task 4: `rp.dsp.gain`, `rp.dsp.biquad` and `rp.dsp.dither`

Design section 5's first three nodes, section 6's node cases, and section 9's fourth question for the core-free half.
Three C++ nodes in the `node.hpp` style, modelled on `rp.core.delay`: settings copied to plain data, state in
host-supplied tail memory, everything `noexcept`, each wrapping the kernel the kit path uses. Gain's param is linear
0..4 default 1, a held value is the kernel's exact multiply, a move ramps across one block from the previous block's
end to the block's last value, and the first block after activation snaps so a document's value is exact from sample 0.
Biquad's type is a setting and its frequency, Q and gain (dB) are params (20..20000 Hz, Q 0.1..10, gain -24..24 dB, the
last read by Peak and the shelves only): the coefficients are recomputed at the block start that first sees a change,
`setup` keeps the state on a sweep so it does not click, `init` clears it on activation or a type change, the kernel's
clamp is the node's and no other clamp is added (an out-of-range frequency goes to the RBJ formulas unclamped, because
a clamp would be a different kit byte). Dither's settings are the algorithm (the five, or `none`), the bit depth (1 to
16) and a `seed` (defaults `none`, 4, `0x9e3779b9`); its output is audio at the quantised levels, so it is a
bit-crusher in a live patch, and a settings commit restarts the noise. All three follow `channels` through
`adjust_layout`, keep their kernel state in tail memory sized from `sizeof` and the settings (gain's one float in a
member), and declare neither `heap` nor `threads`, so all three plan on `Target::Pico`. Their package is
`retroplug-dsp-nodes`, a target of its own: `retroplug-graph` is what every consumer links, and the package brings the
DSP library with it. The resampler's kernel also leaves `retroplug-dsp-kernels` here for `retroplug-dsp-resample`, so
the package's library has neither r8brain nor pthreads nor RTTI on its link line, even transitively, and a build that
will not have a resampler drops one target instead of one source.

**Files:**
- Create: `packages/native/src/graph/nodes/DspNodes.cpp`, `packages/native/src/graph/nodes/DspNodes.hpp`, `packages/native/test/graph/DspNodes.test.cpp`, `packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`, `packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`, `packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/graph.cmake`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/src/graph/Descriptor.cpp`, `packages/native/src/graph/Descriptor.hpp`, `packages/native/src/graph/node.hpp`, `packages/native/src/graph/nodes/CoreNodes.cpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/test/graph/GraphRig.hpp`

- [ ] **Step 1: Write the failing cases**

`DspNodes.test.cpp` (new, 550 lines, 13 cases, tag `[dsp-nodes]`) drives each node through a real plan built by the
graph rig and holds it to its kernel run directly, bit for bit, over splits of 1, 7, 64 and 1024 frames and a ragged
mix; pins the gain ramp and its activation snap, the biquad's sweep (state kept) and type change (state cleared), the
dither's per-channel state, its seed as its stream and the requested memory's size (`query_memory` at one and six
channels differ by five kernel states); counts four blocks after one warm-up block with the suite's counting allocator
(Task 5 moves the count to start before the plan's first block); checks that all three types plan on the Pico and that
each type's descriptor is its golden; and that settings outside their ranges are refused with a reason naming the
range.

Create `packages/native/test/graph/DspNodes.test.cpp`:

````cpp
// The rp.dsp package (src/graph/nodes/DspNodes.cpp, docs/modular-graph-phase-d1-design.md section 5): gain, biquad
// and dither as graph nodes, through real plans built by the graph rig, held to the rp::dsp kernels they wrap.
//
// - Each node's output through a plan equals the kernel run directly over the same signal, bit for bit, and any split
//   of the stream into blocks (1, 7, 64, 1024 frames and a ragged mix) gives the same stream: a node carries its
//   kernel's state across blocks exactly as the kernel does.
// - A gain that moved ramps across one block and lands on the new value; a held one is the kernel's exact multiply,
//   from the plan's first block on. The biquad's coefficients follow a sweep at the next block while its state, which
//   is what a reset would throw away, is kept: a change of type recomputes them and clears the state, which is a fresh
//   filter's stream.
// - A dither's settings are its stream's: a new seed restarts the noise, and the node's output is the kernel's
//   quantised levels, so a live patch and a kit compile run the same arithmetic.
// - A warmed plan block allocates and frees nothing (the suite's counting allocator), and all three types plan on the
//   Pico: they declare neither heap nor threads, and their extra memory is requested for the settings' channel count
//   (the request's size is the assertion), never for the descriptor's maximum.
// - Each type's descriptor is a golden beside golden/rp.test.gain.descriptor.json, recorded with
//   RP_GRAPH_GOLDEN_UPDATE=1.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "graph/Descriptor.hpp"
#include "graph/nodes/DspNodes.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr std::size_t kFrames = 4096;

// The packages whose types this file's descriptor goldens are. A package id registers once, so the resampler's type is a
// second package of its own (`rp.dsp.resample`) rather than more types in this one; the package that adds it is
// appended here, and this list is what a case walking "every rp.dsp type's descriptor" walks.
std::vector<const rp_package*> dspPackages() { return {rp::graph::dspPackage()}; }

// The block sizes a stream is split into, as Dsp.test.cpp's: one call, the usual host block, an odd size, one frame,
// the resampler's chunk and a ragged sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{4096}, {64}, {7}, {1}, {1024}, {1, 31, 512, 3, 1024, 77}};

/** A sine plus noise, overdriven past [-1, 1] in places, so the biquad's clamp and the dither's error terms work. */
std::vector<float> signal(std::size_t frames) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.3f;
        out[i] = noise + 1.1f * std::sin(static_cast<float>(i) * 0.031f);
    }
    return out;
}

/** The first index two streams differ at, bit for bit, or -1. One number keeps a failing CHECK readable; a length
 *  mismatch is a value no index can be, so it cannot be mistaken for a wrong first sample. */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<std::ptrdiff_t>::max();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

/** Calls `run(offset, n)` over [0, frames) in the block sizes `split` cycles through. */
template <class Run>
void inBlocks(std::size_t frames, const std::vector<std::size_t>& split, Run run) {
    std::size_t at = 0, k = 0;
    while (at < frames) {
        const std::size_t n = std::min(split[k++ % split.size()], frames - at);
        run(at, n);
        at += n;
    }
}

/** A rig whose plans may run blocks up to `maxFrames` frames, so a split can be compared with one call over the
 *  whole stream: GraphRig sizes its host output rows from the same number. */
struct DspRig : GraphRig {
    explicit DspRig(rp::graph::Target target = rp::graph::Target::Desktop, std::uint32_t maxFrames = 4096)
        : GraphRig({48000.0, maxFrames, target}, 8, nullptr, maxFrames) {}

    // An audio buffer resource holding one stream per channel.
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto id = resources.createAudioBuffer(channels, static_cast<std::uint32_t>(perChannel[0].size()), 48000.0);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    // An audio buffer resource holding `samples` on every one of its channels.
    rp::graph::ResourceTable::Id buffer(const std::vector<float>& samples, std::uint32_t channels) {
        return buffer(std::vector<std::vector<float>>(channels, samples));
    }

    static std::string res(rp::graph::ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }

    // The host output `bus` names, one port for a mono bus and a range for a stereo one.
    static std::string port(std::uint32_t channels) {
        return channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
    }

    /** Every host channel of the plan's output over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split) {
        std::vector<std::vector<float>> out(outs.size());
        for (auto& channel : out) channel.reserve(frames);
        inBlocks(frames, split, [&](std::size_t, std::size_t n) {
            run(static_cast<std::uint32_t>(n));
            for (std::size_t c = 0; c < out.size(); ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
        });
        return out;
    }

    /** One channel of the host output, this block. */
    std::vector<float> block(std::uint32_t channel, std::size_t frames) {
        return std::vector<float>(outs[channel].begin(), outs[channel].begin() + static_cast<std::ptrdiff_t>(frames));
    }

    /** The plan of a player of `sample` into `node` and on to the host output: the shape every case below uses, as the
     *  plan call's whole result, so a case can read the reason a plan was refused. */
    rp::graph::SetGraphResult plan(rp::graph::ResourceTable::Id sample, const std::string& node,
                                   const std::string& settings, std::uint32_t channels = 1) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        return set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(","loop":false},
                               "resources":{"sample":)" + res(sample) + R"(}},
                   {"id":"x","type":")" + node + R"(","settings":)" + settings + R"(},
                   {"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) + R"(]}}],
                   "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.)" + port(channels) + R"("}]})");
    }

    /** The same plan, as a pass or a fail. */
    bool play(rp::graph::ResourceTable::Id sample, const std::string& node, const std::string& settings,
              std::uint32_t channels = 1) {
        return plan(sample, node, settings, channels).ok;
    }
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("rp.dsp.gain through a plan is its kernel, whole or split any way", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    rp::dsp::Gain            kernel;
    kernel.init(0.37f);
    std::vector<float> whole(kFrames);
    kernel.process(in.data(), whole.data(), kFrames);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        DspRig     rig;
        const auto sample = rig.buffer(in, 1);
        REQUIRE(rig.play(sample, "rp.dsp.gain", R"({"channels":1},"params":{"gain":0.37})"));
        CHECK(firstDifference(whole, rig.render(kFrames, split)[0]) == -1);
    }
}

TEST_CASE("rp.dsp.gain ramps a moved value across one block and multiplies a held one exactly", "[dsp-nodes]") {
    // A constant 1.0 in, so each output sample IS the gain the node used there.
    DspRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","settings":{"value":1,"channels":1}},
                                 {"id":"x","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":0.5}},
                                 {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                       "wires":[{"from":"s.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})")
                .ok);

    // A held value is the kernel's multiply from the plan's first block on: no fade-in from the param's default.
    rig.run(64);
    std::vector<float> held = rig.block(0, 64);
    for (const float v : held) CHECK(v == 0.5f);

    // A live move ramps across the block it lands in, and its last sample is the new value itself, so the block after
    // is the held case again.
    REQUIRE(rig.host->setParam("x", "gain", 0.25));
    rig.run(64);
    const std::vector<float> ramp = rig.block(0, 64);
    CHECK(ramp[0] < 0.5f);   // it starts from the old value rather than stepping to the new one
    CHECK(ramp[0] > 0.25f);
    CHECK(ramp[63] == 0.25f);  // and arrives exactly
    for (std::size_t i = 1; i < ramp.size(); ++i) CHECK(ramp[i] < ramp[i - 1]);

    rig.run(64);
    for (const float v : rig.block(0, 64)) CHECK(v == 0.25f);

    // A change stamped inside a block is that block's target: the ramp spans the whole block rather than stepping at
    // the change's frame.
    REQUIRE(rig.host->setParam("x", "gain", 1.0, 32));
    rig.run(64);
    const std::vector<float> late = rig.block(0, 64);
    CHECK(late[0] > 0.25f);
    CHECK(late[0] < 1.0f);
    CHECK(late[31] < 1.0f);  // the change's own frame is mid-ramp, not a jump
    CHECK(late[63] == 1.0f);
}

TEST_CASE("rp.dsp.biquad through a plan is its kernel, whole or split any way", "[dsp-nodes]") {
    const std::vector<float>      in = signal(kFrames);
    const rp::dsp::BiquadSettings settings{rp::dsp::BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f};

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        DspRig     rig;
        const auto sample = rig.buffer(in, 2);
        REQUIRE(rig.play(sample, "rp.dsp.biquad",
                         R"({"channels":2,"type":"peak"},"params":{"frequency":1000,"q":1,"gainDb":6})", 2));
        const auto out = rig.render(kFrames, split);

        rp::dsp::Biquad kernel;  // from a fresh filter, over the same splits
        kernel.init(settings);
        std::vector<float> reference(kFrames);
        inBlocks(kFrames, split, [&](std::size_t at, std::size_t n) {
            kernel.process(in.data() + at, reference.data() + at, n);
        });
        // Both channels: each one is its own filter over its own copy of the signal, so each gives this stream, where
        // one state shared across them would not.
        CHECK(firstDifference(reference, out[0]) == -1);
        CHECK(firstDifference(reference, out[1]) == -1);

        // The clamp is the kernel's, so a signal driven past full scale comes out inside [-1, 1], and a peak at +6 dB
        // puts it there.
        for (const float v : out[0]) CHECK(std::abs(v) <= 1.0f);
        CHECK(std::any_of(out[0].begin(), out[0].end(), [](float v) { return std::abs(v) == 1.0f; }));
    }
}

TEST_CASE("rp.dsp state is per channel, and its memory is sized from the settings' channels", "[dsp-nodes]") {
    // Six channels of different signal through one biquad and one dither. Each channel's output is its own kernel's,
    // so neither node keeps one state for all of them, and extra memory sized for fewer channels than the settings
    // name leaves the extra channels' states unwritten here: a wrong stream, not a heap write, because the per-channel
    // loops are bounded by the span they were attached to.
    std::vector<std::vector<float>> in(6);
    for (std::size_t c = 0; c < in.size(); ++c) {
        in[c] = signal(512);
        for (auto& v : in[c]) v *= 0.1f * static_cast<float>(c + 1);
    }
    DspRig     rig;
    const auto sample = rig.buffer(in);
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":6,"layout":"5.1","loop":false},
                                             "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                                   {"id":"b","type":"rp.dsp.biquad","settings":{"channels":6,"type":"low-pass"},
                                    "params":{"frequency":1000,"q":1}},
                                   {"id":"d","type":"rp.dsp.dither","settings":{"channels":6,"algorithm":"error-diffusion","bits":6,"seed":21}},
                                   {"id":"out","type":"rp.host.audio-out","settings":{"buses":[6]}}],
                                 "wires":[{"from":"p.out","to":"b.in"},{"from":"b.out","to":"d.in"},
                                          {"from":"d.out","to":"out.out.1-6"}]})")
                .ok);
    rig.run(512);

    for (std::uint32_t c = 0; c < 6; ++c) {
        INFO("channel " << c);
        rp::dsp::Biquad biquad;  // a fresh filter and dither for this channel alone
        biquad.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
        rp::dsp::Dither dither;
        dither.init(rp::dsp::DitherAlgorithm::ErrorDiffusion, 6, 21);
        std::vector<float> filtered(512), levelled(512);
        biquad.process(in[c].data(), filtered.data(), 512);
        dither.processLevels(filtered.data(), levelled.data(), 512);
        CHECK(firstDifference(levelled, rig.block(c, 512)) == -1);
    }

    // The request is the settings' channel count and not the descriptor's maximum, which is over-sized and would run
    // every case above just as correctly while wasting 63 kernels a node: the size itself is the assertion.
    struct MemoryCase {
        const char* typeId;
        std::size_t state;
    };
    for (const MemoryCase& mc :
         {MemoryCase{"rp.dsp.biquad", sizeof(rp::dsp::Biquad)}, MemoryCase{"rp.dsp.dither", sizeof(rp::dsp::Dither)}}) {
        const auto* entry = rig.registry.find(mc.typeId);
        REQUIRE(entry);
        INFO(mc.typeId);
        const char* const settings[] = {R"({"channels":1})", R"({"channels":6})"};
        rp_memory_req      req[2]{};
        rp_error           err{};
        for (int i = 0; i < 2; ++i) {
            rp_settings s{settings[i], std::strlen(settings[i]), RP_SETTINGS_JSON};
            REQUIRE(entry->type->query_memory(entry->type, &s, &req[i], &err));
        }
        CHECK(req[1].size - req[0].size == 5 * mc.state);
    }
}

TEST_CASE("rp.dsp.biquad recomputes its coefficients on a sweep and keeps its state", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(kFrames);
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.biquad", R"({"channels":1,"type":"low-pass"},"params":{"frequency":1000,"q":1})"));

    rig.run(64);
    const std::vector<float> first = rig.block(0, 64);
    rp::dsp::Biquad reference;  // the same two blocks a filter swept between them, keeping its state
    reference.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    std::vector<float> filtered(64);
    reference.process(in.data(), filtered.data(), 64);
    CHECK(firstDifference(filtered, first) == -1);

    REQUIRE(rig.host->setParam("x", "frequency", 2000.0));
    rig.run(64);
    const std::vector<float> swept = rig.block(0, 64);
    reference.setup({rp::dsp::BiquadType::LowPass, 48000.0f, 2000.0f, 1.0f, 0.0f});
    reference.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, swept) == -1);

    // A filter reset to the new coefficients would start from silence here, which on this signal is a different
    // stream: the state the sweep kept is what the plan's second block shows.
    rp::dsp::Biquad cleared;
    cleared.init({rp::dsp::BiquadType::LowPass, 48000.0f, 2000.0f, 1.0f, 0.0f});
    cleared.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, swept) != -1);
}

TEST_CASE("rp.dsp.biquad recomputes its coefficients when the type changes", "[dsp-nodes]") {
    // The filter type is a setting and the three params stay put across the commit, so the coefficient cache cannot
    // fire: only the type check makes the next block a new filter rather than the old one's coefficients forever.
    const std::vector<float> in = signal(kFrames);
    DspRig                   rig;
    const auto               sample = rig.buffer(in, 1);
    const auto document = [&](const char* type) {
        const std::string settings = R"({"channels":1,"type":")" + std::string(type) + R"("})";
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                              "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                            {"id":"x","type":"rp.dsp.biquad","settings":)" + settings + R"(,
                             "params":{"frequency":1000,"q":1}},
                            {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                          "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})";
    };
    REQUIRE(rig.set(document("low-pass")).ok);

    rig.run(64);
    std::vector<float> filtered(64);
    rp::dsp::Biquad    reference;
    reference.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    reference.process(in.data(), filtered.data(), 64);
    CHECK(firstDifference(filtered, rig.block(0, 64)) == -1);

    // The same document as a high-pass, a commit: the next block computes the new type's coefficients and clears the
    // state, which is a fresh filter's stream over the same input. A node that kept the old coefficients would give
    // the low-pass stream here, since none of the params moved.
    REQUIRE(rig.set(document("high-pass")).ok);
    rig.run(64);
    const std::vector<float> changed = rig.block(0, 64);
    rp::dsp::Biquad          fresh;
    fresh.init({rp::dsp::BiquadType::HighPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    fresh.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, changed) == -1);

    // And it is the cleared state as well as the new coefficients: a filter that carried its state into them is a
    // different stream over the same input, so this pins the reset rather than the recompute alone.
    rp::dsp::Biquad carried;
    carried.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    std::vector<float> discard(64);
    carried.process(in.data(), discard.data(), 64);
    carried.setup({rp::dsp::BiquadType::HighPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    carried.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, changed) != -1);
}

TEST_CASE("rp.dsp.dither through a plan is its kernel's quantised levels, whole or split any way", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    struct Case {
        const char*              json;
        rp::dsp::DitherAlgorithm algorithm;
        int                      bits;
        std::uint32_t            seed;
    };
    const Case cases[] = {
        {R"({"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":12345})",
         rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 12345},
        {R"({"channels":1,"algorithm":"sierra-lite","bits":8,"seed":1})", rp::dsp::DitherAlgorithm::SierraLite, 8, 1},
        {R"({"channels":1,"algorithm":"none","bits":16})", rp::dsp::DitherAlgorithm::None, 16, 0x9e3779b9u},
    };

    for (const Case& c : cases) {
        for (const auto& split : kSplits) {
            INFO("case " << c.json << ", split starting " << split[0]);
            DspRig     rig;
            const auto sample = rig.buffer(in, 1);
            REQUIRE(rig.play(sample, "rp.dsp.dither", c.json));
            const std::vector<float> out = rig.render(kFrames, split)[0];

            rp::dsp::Dither kernel;
            kernel.init(c.algorithm, c.bits, c.seed);
            std::vector<float> reference(kFrames);
            inBlocks(kFrames, split, [&](std::size_t at, std::size_t n) {
                kernel.processLevels(in.data() + at, reference.data() + at, n);
            });
            CHECK(firstDifference(reference, out) == -1);
        }
    }
}

TEST_CASE("rp.dsp.dither's output is audio on the kernel's levels", "[dsp-nodes]") {
    // 4 bits, no dither: every sample is one of the sixteen levels, the kit decoder's, rather than a code.
    const std::vector<float> in = signal(kFrames);
    DspRig                   rig;
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"none","bits":4})"));
    const std::vector<float> out = rig.render(kFrames, {kFrames})[0];
    for (const float v : out) {
        const int code = static_cast<int>(std::round((v + 1.0f) * 0.5f * 15.0f));
        CHECK(v == rp::dsp::codeToLevel(static_cast<std::uint32_t>(code), 4));
    }
    CHECK(std::any_of(out.begin(), out.end(), [](float v) { return v < -0.9f; }));  // the overdriven parts reach the
    CHECK(std::any_of(out.begin(), out.end(), [](float v) { return v > 0.9f; }));   // outermost levels
}

TEST_CASE("rp.dsp.dither's seed setting is its noise: a new seed restarts the stream", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(kFrames);
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":7})"));
    const std::vector<float> first = rig.render(256, {256})[0];

    rp::dsp::Dither seeded;
    seeded.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 7);
    std::vector<float> reference(256);
    seeded.processLevels(in.data(), reference.data(), 256);
    CHECK(firstDifference(reference, first) == -1);

    // The same document with another seed is a commit the node restarts the noise on, so the plan's stream from there
    // is a fresh kernel's with that seed. The player keeps its place across the swap, so the input is the next frames.
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                                              "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                                    {"id":"x","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":99}},
                                    {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                                  "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})")
                .ok);
    const std::vector<float> second = rig.render(256, {256})[0];
    rp::dsp::Dither other;
    other.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 99);
    other.processLevels(in.data() + 256, reference.data(), 256);
    CHECK(firstDifference(reference, second) == -1);
    CHECK(firstDifference(first, second) != -1);  // and a different seed is different noise
}

TEST_CASE("a warmed rp.dsp plan block allocates and frees nothing", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(1024);
    const auto sample = rig.buffer(in, 2);
    const std::string doc =
        R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":2,"layout":"stereo"},
                       "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                      {"id":"g","type":"rp.dsp.gain","settings":{"channels":2}},
                      {"id":"b","type":"rp.dsp.biquad","settings":{"channels":2,"type":"peak"},
                       "params":{"frequency":1500,"q":0.9,"gainDb":5}},
                      {"id":"d","type":"rp.dsp.dither","settings":{"channels":2,"algorithm":"shaped-tpdf","bits":8,"seed":3}},
                      {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"b.in"},
                             {"from":"b.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})";
    REQUIRE(rig.set(doc).ok);
    rig.run(64);  // no warm-up beyond the first block, where the dither seeds its noise: that must not allocate either

    rp::test::beginCountingAllocations();
    rig.run(64, 4);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("each rp.dsp type's descriptor is its golden", "[dsp-nodes]") {
    const char* update = std::getenv("RP_GRAPH_GOLDEN_UPDATE");
    for (const rp_package* pkg : dspPackages()) {
        REQUIRE(pkg);
        REQUIRE(pkg->node_type_count > 0);
        for (std::uint32_t i = 0; i < pkg->node_type_count; ++i) {
            const rp_node_type* type = pkg->node_types[i];
            const std::string   json = type->descriptor(type);
            INFO(type->id);
            REQUIRE_FALSE(json.empty());
            const std::string path = std::string(RP_GRAPH_GOLDEN_DIR) + "/" + type->id + ".descriptor.json";
            if (update) std::ofstream(path) << json << "\n";  // recorded, then compared below like any other golden
            CHECK(json == readFile(path));
            REQUIRE(rp::graph::parseDescriptor(json));  // and the strict parser accepts it unchanged
        }
    }
}

TEST_CASE("the three rp.dsp nodes plan on the Pico", "[dsp-nodes]") {
    // This package's types alone: the resampler's node is the one that declares heap, and the planner refuses it on the
    // Pico.
    for (std::uint32_t i = 0; i < rp::graph::dspPackage()->node_type_count; ++i) {
        const rp_node_type* type = rp::graph::dspPackage()->node_types[i];
        const std::string   json = type->descriptor(type);
        INFO(type->id);
        CHECK(json.find(R"("requires":{"heap":false,"threads":false})") != std::string::npos);
    }

    DspRig     pico(rp::graph::Target::Pico);
    const auto sample = pico.buffer(signal(1024), 2);
    const std::string doc =
        R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":2,"layout":"stereo"},
                       "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                      {"id":"g","type":"rp.dsp.gain","settings":{"channels":2}},
                      {"id":"b","type":"rp.dsp.biquad","settings":{"channels":2,"type":"high-shelf"}},
                      {"id":"d","type":"rp.dsp.dither","settings":{"channels":2,"algorithm":"jjn","bits":6,"seed":5}},
                      {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"b.in"},
                             {"from":"b.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})";
    const auto r = pico.set(doc);
    INFO(GraphRig::codes(r));
    CHECK(r.ok);
    pico.run(64);
}

TEST_CASE("rp.dsp settings are validated with a reason", "[dsp-nodes]") {
    DspRig     rig;
    const auto sample = rig.buffer(signal(64), 1);
    // A channel count is a document's, and the extra memory is sized from it, so it is refused rather than clamped.
    for (const char* settings : {R"({"channels":0})", R"({"channels":65})"}) {
        INFO(settings);
        const auto r = rig.plan(sample, "rp.dsp.gain", settings);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.errors.empty());
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 64") != std::string::npos);
    }
    // The bit depth is the kernel's 1..16, and a label outside the schema's set is refused when the document is read.
    for (const char* settings : {R"({"channels":1,"bits":0})", R"({"channels":1,"bits":17})"}) {
        INFO(settings);
        const auto r = rig.plan(sample, "rp.dsp.dither", settings);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.errors.empty());
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 16") != std::string::npos);
    }
    CHECK_FALSE(rig.play(sample, "rp.dsp.biquad", R"({"channels":1,"type":"mellotron"})"));
    CHECK(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"bits":16})"));
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 0dd3e508d..3b2da9668 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -263,8 +263,11 @@ retroplug_add_test_binary(
              test/graph/QueueCapacity.test.cpp
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
              test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's arithmetic
+             test/graph/DspNodes.test.cpp             # the rp.dsp nodes, through real plans, against those kernels
     LIBS     retroplug-graph
-             retroplug-dsp-kernels                     # core-free: the standard library and r8brain
+             retroplug-dsp-nodes                      # the rp.dsp package under test
+             retroplug-dsp-kernels                    # core-free: the standard library
+             retroplug-dsp-resample                   # Dsp.test.cpp's resampler
              r8brain                                   # Dsp.test.cpp's reference copy of the kit path's resample
              Threads::Threads                          # SwapStress runs a real audio thread
     DEFS     RP_GRAPH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/graph/golden")
````

Run: `sed -n '4818,4834p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds the source, `retroplug-dsp-nodes` (a plain link name for now) and `retroplug-dsp-kernels` / `retroplug-dsp-resample` to `retroplug-graph-test`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: the build stops at the first missing header, `graph/nodes/DspNodes.hpp` (and no `rp::dsp` package), so Catch2 never runs. Report the compiler's first few errors.

- [ ] **Step 2: Add the three nodes, their package and their target**

`DspNodes.{hpp,cpp}` (new) declare `rp::graph::dspPackage()` and the three types with their settings in the named
`rp::graph::dsp` namespace (`GainNodeSettings`, `BiquadNodeSettings`, `DitherNodeSettings`), the labels as kebab-case
`rfl::Literal`s in the kernel enums' order with `static_assert`s tying each label's index to its enumerator, and one
kernel per channel in tail memory (constructed in `attach`, with a `static_assert` that it is trivially destructible
and a comment that tail memory is never destroyed). `node.hpp`'s hook list says the same. `checkChannels` moves from
`CoreNodes.cpp` to `Descriptor.{hpp,cpp}` and `checkChannelList` calls it, so the dither and biquad share one
spelling. `Engine.cpp` registers `dspPackage()` and reports a refused package on stderr, and `GraphRig` registers the
shared packages in one loop that `REQUIRE`s each with the refusal's reason in scope and takes a `maxFrames` that sizes
its host output rows (a 4096-frame split overran the 1024-frame rows and corrupted the heap). `CMakeLists.txt` splits
`retroplug-dsp-resample` out of `retroplug-dsp-kernels` (Resample.cpp, `PRIVATE r8brain Threads::Threads`; the kernels
keep the standard library alone), `retroplug-core` links both, and `find_package(Threads)` moves up beside them with
its comment naming every consumer. `cmake/graph.cmake` declares `retroplug-dsp-nodes` (PUBLIC `retroplug-graph`,
PRIVATE `retroplug-dsp-kernels`), `retroplug-backend` links it, and `warnings.cmake` puts both new targets on the MSVC
list.

Create `packages/native/src/graph/nodes/DspNodes.cpp`:

````cpp
#include "graph/nodes/DspNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <rfl/Literal.hpp>

#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// The settings structs live in a named namespace, not an anonymous one: a Settings struct's JSON Schema $defs key is
// its qualified name, which compilers spell differently for an anonymous namespace. Only they need the name;
// everything else below stays internal to this file. The kernels are rp::dsp (src/dsp/), so a kernel type is always
// spelled out in full here, and each settings struct carries `Node` because the kernel has a settings struct of its own
// under the same short name (`rp::dsp::BiquadSettings` is what Biquad::process builds, and it is in scope here).
namespace dsp {

// The eight filters and the six dither forms, labelled as every other enum in a document is: the kebab-case of the
// kernel enumerator it names (node.hpp's enum_param uses the same convention for a param), in the order the kernel
// declares them, so a label's index is its enumerator's value (the static_asserts below hold that).
using BiquadType = rfl::Literal<"low-pass", "high-pass", "band-pass", "band-stop", "peak", "low-shelf", "high-shelf",
                                 "all-pass">;
using DitherAlgorithm = rfl::Literal<"none", "high-pass-tpdf", "shaped-tpdf", "error-diffusion", "jjn", "sierra-lite">;

static_assert(BiquadType::value_of<"low-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::LowPass) &&
                  BiquadType::value_of<"high-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::HighPass) &&
                  BiquadType::value_of<"band-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::BandPass) &&
                  BiquadType::value_of<"band-stop">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::BandStop) &&
                  BiquadType::value_of<"peak">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::Peak) &&
                  BiquadType::value_of<"low-shelf">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::LowShelf) &&
                  BiquadType::value_of<"high-shelf">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::HighShelf) &&
                  BiquadType::value_of<"all-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::AllPass),
              "a filter label's index is its kernel enumerator's value");
static_assert(DitherAlgorithm::value_of<"none">() == static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::None) &&
                  DitherAlgorithm::value_of<"high-pass-tpdf">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::HighPassTPDF) &&
                  DitherAlgorithm::value_of<"shaped-tpdf">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::ShapedTPDF) &&
                  DitherAlgorithm::value_of<"error-diffusion">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::ErrorDiffusion) &&
                  DitherAlgorithm::value_of<"jjn">() == static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::JJN) &&
                  DitherAlgorithm::value_of<"sierra-lite">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::SierraLite),
              "a dither label's index is its kernel enumerator's value");

// The seed a live patch gets when its document names none. A kit compile fills the seed from a hash of the input, so
// every compile gives the same bytes; a running patch has no whole input to hash, and a fixed seed keeps its noise the
// same on every run, which is what makes a rendered patch reproducible. Any value does: init salts it with each TPDF
// algorithm's own constant, so the two do not draw the same noise from it.
constexpr std::uint32_t kDefaultDitherSeed = 0x9e3779b9u;

// A label as the kernel's enumerator: a cast, since the static_asserts above hold the two in step.
rp::dsp::BiquadType biquadTypeOf(const BiquadType& t) noexcept {
    return static_cast<rp::dsp::BiquadType>(t.value());
}
rp::dsp::DitherAlgorithm ditherAlgorithmOf(const DitherAlgorithm& a) noexcept {
    return static_cast<rp::dsp::DitherAlgorithm>(a.value());
}

struct GainNodeSettings {
    std::uint32_t channels = 2;
};

struct BiquadNodeSettings {
    std::uint32_t channels = 2;
    BiquadType    type = BiquadType::make<"low-pass">();
};

struct DitherNodeSettings {
    std::uint32_t   channels = 2;
    // `none` is plain rounding, the kit path's when no dither is set: the least surprising thing to insert, and the
    // depth is the kit path's 4 bits, so the node's default is a plain bit crusher rather than noise nobody asked for.
    DitherAlgorithm algorithm = DitherAlgorithm::make<"none">();
    int             bits = 4;  // 1 .. 16, the kernel's range
    std::uint32_t   seed = kDefaultDitherSeed;
};

}  // namespace dsp

namespace {

using namespace dsp;

// The kernels the two per-channel nodes build in their instance tail memory are never destroyed (node.hpp's hook list:
// nothing runs a destructor over tail memory), so their state must own nothing for that to be sound. A kernel that
// allocates or holds a handle - the resampler's, which is why its node keeps it in a member - fails to compile here
// rather than leaking one object per instance.
static_assert(std::is_trivially_destructible_v<rp::dsp::Biquad>);
static_assert(std::is_trivially_destructible_v<rp::dsp::Dither>);

// ---- rp.dsp.gain --------------------------------------------------------------------------------------------------

// A held gain is the kernel's multiply, exact: the kit path's arithmetic, so a gain node at 0.37 gives the bytes a
// kit compiled with that gain gives. A gain that moved is a ramp across one block, from the value the last block ended
// at to the block's new value, so a live move does not step. The block's value is its last one (a frame-stamped change
// included), so a change anywhere in a block is the ramp's target for that block rather than the next one.
struct Gain {
    static constexpr const char* id = "rp.dsp.gain";
    static constexpr const char* name = "Gain";
    static constexpr const char* category = "dsp/gain";
    using Settings = GainNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    // A linear multiply, because that is what the kernel is and what the kit path's bytes depend on: a level in dB
    // would need a conversion the kit path does not do, and a held value would no longer be the exact multiply.
    struct { rp::float_param<"Gain", rp::range{0.0, 4.0, 1.0}> gain; } params;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a gain"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

    rp::dsp::Gain kernel;
    float         applied = 1.0f;  // the gain the last block ended at
    bool          snap = true;     // the next block takes its value whole: a fresh instance or a fresh activation

    void activate(double, std::uint32_t) noexcept { snap = true; }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        const float target = static_cast<float>(params.gain.latest());
        if (snap || target == applied) {
            snap = false;
            applied = target;
            kernel.init(target);
            for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
                kernel.process(inputs.in[c], outputs.out[c], n);
            return;
        }
        const float delta = target - applied;
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
            const float* x = inputs.in[c];
            float*       y = outputs.out[c];
            for (std::uint32_t i = 0; i < n; ++i) {
                // The last sample lands on the target itself rather than one step short of it, so the next block is
                // the held case: an exact multiply.
                const float g = i + 1 == n ? target
                                           : applied + delta * (static_cast<float>(i + 1) / static_cast<float>(n));
                y[i] = x[i] * g;
            }
        }
        applied = target;
    }
};

// ---- rp.dsp.biquad -------------------------------------------------------------------------------------------------

// One kernel per channel, in host-supplied memory. The coefficients are the block's params at the sample rate the
// node was activated at, recomputed at the block start that first sees a different value: a sweep between blocks
// changes the coefficients and keeps the state (what `setup` does), so it does not click from a reset, while a fresh
// instance or a fresh activation starts from cleared state (what `init` does). The kernel's output clamp is part of its
// arithmetic, so the node's output is clamped to [-1, 1] as the kit path's is.
//
// A frequency or Q beyond the project's range is the document's to refuse (the params' range), and the kernel then
// computes the coefficients the RBJ formulas give there; the node clamps nothing of its own, a frequency above
// Nyquist included, because the kit path hands the spec's frequency straight to the kernel and a clamp here would be a
// different byte for the same kit.
struct Biquad {
    static constexpr const char* id = "rp.dsp.biquad";
    static constexpr const char* name = "Biquad";
    static constexpr const char* category = "dsp/filter";
    using Settings = BiquadNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Frequency", rp::range{20.0, 20000.0, 1000.0}, "Hz"> frequency;
        rp::float_param<"Q", rp::range{0.1, 10.0, 1.0}>                       q;
        rp::float_param<"Gain", rp::range{-24.0, 24.0, 0.0}, "dB">            gainDb;  // Peak and the shelves only
    } params;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a biquad"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Biquad);
    }

    std::span<rp::dsp::Biquad> filters;  // one per channel, in the instance's tail memory

    double               rate = 48000.0;  // as activate gave it
    rp::dsp::BiquadType  type = rp::dsp::BiquadType::LowPass;
    bool                 clear = true;  // the next block computes coefficients and clears the state
    // What the coefficients were last computed for, so a block that changed none of it does no trigonometry.
    float frequency = 0.0f, q = 0.0f, gainDb = 0.0f;

    void attach(std::span<std::byte> mem) noexcept {
        // The host builds these once per instance and never destroys them (node.hpp's hook list), which is sound only
        // because Biquad is trivially destructible: the static_assert above holds that.
        auto* const first = reinterpret_cast<rp::dsp::Biquad*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Biquad);
        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
        filters = std::span<rp::dsp::Biquad>(first, count);
    }

    // The filter type is a setting, so a commit changes it here (on the block thread) and the next block recomputes.
    void on_settings(const Settings& s) noexcept {
        const rp::dsp::BiquadType wanted = biquadTypeOf(s.type);
        if (wanted != type) clear = true;
        type = wanted;
    }

    void activate(double sampleRate, std::uint32_t) noexcept {
        rate = sampleRate;
        clear = true;
    }

    void process(const rp::Block& b) noexcept {
        const float f = static_cast<float>(params.frequency.latest());
        const float qq = static_cast<float>(params.q.latest());
        const float g = static_cast<float>(params.gainDb.latest());
        if (clear || f != frequency || qq != q || g != gainDb) {
            // One coefficient set for every channel: `setup`/`init` would re-run the RBJ trigonometry per channel, on
            // the block thread, for the same answer.
            const rp::dsp::BiquadCoefficients c =
                rp::dsp::biquadCoefficients({type, static_cast<float>(rate), f, qq, g});
            for (auto& filter : filters) {
                filter.c = c;
                if (clear) filter.reset();
            }
            clear = false;
            frequency = f;
            q = qq;
            gainDb = g;
        }
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < filters.size(); ++c)
            filters[c].process(inputs.in[c], outputs.out[c], b.frames());
    }
};

// ---- rp.dsp.dither ------------------------------------------------------------------------------------------------

// One kernel per channel, in host-supplied memory (the noise generator is most of it). The kernel quantises to codes
// and the node maps them back to audio at the quantised levels, so the same node is the kit path's dither and a bit
// crusher in a live patch.
//
// A settings commit restarts the noise: a new algorithm, depth or seed is a new stream from the first sample of the
// next block. A fresh activation restarts it too, so a plan that has just started gives the stream a fresh kernel
// gives, whatever the plan it replaced left in the state.
struct Dither {
    static constexpr const char* id = "rp.dsp.dither";
    static constexpr const char* name = "Dither";
    static constexpr const char* category = "dsp/quantise";
    using Settings = DitherNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a dither")) return e;
        if (s.bits < rp::dsp::kMinDitherBits || s.bits > rp::dsp::kMaxDitherBits)
            return "bits must be between " + std::to_string(rp::dsp::kMinDitherBits) + " and " +
                   std::to_string(rp::dsp::kMaxDitherBits);
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Dither);
    }

    std::span<rp::dsp::Dither> states;

    rp::dsp::DitherAlgorithm algorithm = rp::dsp::DitherAlgorithm::None;
    int                      bits = 4;
    std::uint32_t            seed = kDefaultDitherSeed;
    bool                     restart = true;  // the next block seeds the noise from the three fields above

    void attach(std::span<std::byte> mem) noexcept {
        // Never destroyed, like the biquad's states above; the static_assert holds that Dither is trivially
        // destructible, which it is with its noise generator held inline.
        auto* const first = reinterpret_cast<rp::dsp::Dither*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Dither);
        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
        states = std::span<rp::dsp::Dither>(first, count);
    }

    void on_settings(const Settings& s) noexcept {
        algorithm = ditherAlgorithmOf(s.algorithm);
        bits = s.bits;
        seed = s.seed;
        restart = true;
    }

    void activate(double, std::uint32_t) noexcept { restart = true; }

    void process(const rp::Block& b) noexcept {
        if (restart) {
            for (auto& state : states) state.init(algorithm, bits, seed);
            restart = false;
        }
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < states.size(); ++c)
            states[c].processLevels(inputs.in[c], outputs.out[c], b.frames());
    }
};

}  // namespace

const rp_package* dspPackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Gain>(),
        rp::node_type<Biquad>(),
        rp::node_type<Dither>(),
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph
````

Create `packages/native/src/graph/nodes/DspNodes.hpp`:

````cpp
#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.dsp package: the DSP library as graph nodes (docs/modular-graph-phase-d1-design.md section 5), each one a
// thin node around a block-streaming kernel of src/dsp/, so a patch and the kit compilers run the same arithmetic.
//
//   rp.dsp.gain     a linear gain                                              {"channels":2}
//   rp.dsp.biquad   the RBJ cookbook's eight filters, one per channel          {"channels":2,"type":"low-pass"}
//   rp.dsp.dither   quantise to 1..16 bits, five dither algorithms or none     {"channels":2,"bits":4,"seed":...}
//
// Every one carries a `channels` setting and follows it through adjust_layout, as rp.core.delay does: the two
// per-channel types keep their kernel state in host-supplied memory, and gain, whose kernel is one float and has no
// per-channel state, holds it as an ordinary member. None declares `heap` or `threads`, so all three plan on the Pico.
// A kernel's state is per channel and its size cannot be known from the descriptor alone, so extra_memory is sized
// from the settings' channel count, never from the descriptor's maximum.
//
// The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain (heap, pthreads and RTTI) with
// it, and a node that declares `heap` is refused on the Pico. So it is a source file and a target of its own, and a
// build without it omits that node and r8brain together: it registers from a package of its own, with its own
// manifest id (`rp.dsp.resample`, which `rp.host.io` is to `rp.host`), added to the engine's package list and to the
// graph rig's beside dspPackage(). A package id registers once - NodeRegistry refuses a second package under one - so
// the resample type can neither be a second rp.dsp nor a type this function's list names.
const rp_package* dspPackage();

}  // namespace rp::graph
````

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 82ca815db..76714da08 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -37,29 +37,40 @@ endif()
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)
 
 # --- retroplug-dsp-kernels: the DSP the kit compilers run, as block-streaming kernels (src/dsp/,
-# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, dither / quantise, and the
-# r8brain resampler. Core-free on purpose: it links the C++ standard library and, for the resampler alone,
-# r8brain, so retroplug-graph-test and the rp.dsp node package can use it with no core, txiki or backend.
-# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.) r8brain is
-# PRIVATE because no header here includes it (Resample.hpp holds it behind a pointer); a static library still
-# hands it to whatever links this one.
-# Threads is found before the target because the resampler object alone brings pthreads and RTTI through r8brain: its
-# filter cache is a std::mutex and its resampler classes are virtual. On a libc that does not fold pthreads into libc
-# this library would otherwise fail to link for a consumer that links nothing else thread-related. It is also why a
-# Pico build wants a source list without Resample.cpp.
-find_package(Threads REQUIRED)
+# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, and dither / quantise. Core-free
+# on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node package
+# can use it with no core, txiki, backend or r8brain. (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names
+# the plugin's own DSP library that.)
+#
+# The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
+# the heap, pthreads (its filter cache is a std::mutex) and RTTI (its resamplers are virtual). A build that has no use
+# for it - the Pico's, which runs rp.dsp's gain / biquad / dither alone - omits the target and never sees any of that,
+# and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for the same
+# reason.
 add_library(retroplug-dsp-kernels STATIC
     ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp)
 target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
-target_link_libraries(retroplug-dsp-kernels PRIVATE r8brain Threads::Threads)
 set_target_properties(retroplug-dsp-kernels PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
     POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
 
+# Threads: r8brain's filter cache is a std::mutex, efsw-static needs pthreads on Linux (its CMake links it to the
+# shared lib only), and the Launchpad scan thread and N8Link's serial thread do too. Found once, for this directory.
+find_package(Threads REQUIRED)
+add_library(retroplug-dsp-resample STATIC
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
+target_include_directories(retroplug-dsp-resample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
+# r8brain is PRIVATE because no header here includes it (Resample.hpp holds it behind a pointer); a static library
+# still hands it to whatever links this one.
+target_link_libraries(retroplug-dsp-resample PRIVATE r8brain Threads::Threads)
+set_target_properties(retroplug-dsp-resample PROPERTIES
+    CXX_STANDARD 20
+    CXX_STANDARD_REQUIRED ON
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
+
 # --- retroplug-core: the shared emulator core, as a neutrally-named static lib. Extracted out of
 # packages/native/cli (which also compiled the CLI-only HarnessRpcService) so the host links ONLY the
 # shared core, never a CLI RPC surface it doesn't use. The legacy `retroplug-cli-core` is now a thin
@@ -121,7 +132,8 @@ target_link_libraries(retroplug-core PUBLIC
     sameboy
     mesen
     rpcpp     # transitively pulls reflectcpp
-    retroplug-dsp-kernels  # the kit path's gain, biquad, dither and r8brain resample (src/dsp/)
+    retroplug-dsp-kernels  # the kit path's gain, biquad and dither (src/dsp/)
+    retroplug-dsp-resample # the kit path's r8brain resample
     enkiTS    # used by KitCompiler for per-sample parallel compile
     miniaudio # header-only audio decoder used by SampleCache
     miniz     # used by projectConfigToZip for --save-rplg
@@ -200,13 +212,10 @@ target_include_directories(retroplug-backend PUBLIC
     #  miniz header dir, arrive PUBLIC from retroplug-core above.)
 )
 
-find_package(Threads REQUIRED)  # efsw-static needs pthreads on Linux (its CMake links it to the shared lib only);
-                                # also the Launchpad scan thread and N8Link's serial thread, below.
-
-
 target_link_libraries(retroplug-backend PUBLIC
     retroplug-core      # SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
     retroplug-graph     # the graph executor, planner and GraphHost the engine runs on
+    retroplug-dsp-nodes # the rp.dsp package (gain, biquad, dither), which the engine registers
     tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
     efsw-static         # NativeFileWatcher's filesystem watcher (PUBLIC-brings efsw/efsw.hpp include dir)
     Threads::Threads
diff --git a/packages/native/cmake/graph.cmake b/packages/native/cmake/graph.cmake
index ee8b27807..ad7d63c9b 100644
--- a/packages/native/cmake/graph.cmake
+++ b/packages/native/cmake/graph.cmake
@@ -34,3 +34,18 @@ set_target_properties(retroplug-graph PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
     POSITION_INDEPENDENT_CODE ON)
+
+# retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad and dither, each a node
+# around a kernel of src/dsp/ (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather than a
+# source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the test
+# hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying. It links
+# retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or RTTI, and it
+# stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and runs these
+# nodes with only the graph library and the kernels.
+add_library(retroplug-dsp-nodes STATIC
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
+target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-graph PRIVATE retroplug-dsp-kernels)
+set_target_properties(retroplug-dsp-nodes PROPERTIES
+    CXX_STANDARD 20
+    CXX_STANDARD_REQUIRED ON
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-backend
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index 0ccee009b..01d1ea5aa 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -38,7 +38,7 @@ if(MSVC)
     set(_rp_werror_targets
         retroplug-core retroplug-backend retroplug-host retroplug-cli retroplug-sdl
         retroplug retroplug-dsp retroplug-ui
-        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels)
+        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes)
     foreach(_t IN LISTS _rp_werror_targets)
         if(TARGET ${_t})
             target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
diff --git a/packages/native/src/graph/Descriptor.cpp b/packages/native/src/graph/Descriptor.cpp
index 1e6319ee0..a81c33415 100644
--- a/packages/native/src/graph/Descriptor.cpp
+++ b/packages/native/src/graph/Descriptor.cpp
@@ -561,6 +561,12 @@ const ParamDesc* findParam(const Layout& layout, std::string_view id) {
     return nullptr;
 }
 
+std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what) {
+    if (n < 1 || n > kMaxChannels)
+        return std::string(what) + " must be between 1 and " + std::to_string(kMaxChannels) + " channels";
+    return std::nullopt;
+}
+
 std::optional<std::string> checkCapacity(const PortDesc& port) {
     const QueueCapacity& c = port.capacity;
     if (!c.messages && !c.bytes) return std::nullopt;
diff --git a/packages/native/src/graph/Descriptor.hpp b/packages/native/src/graph/Descriptor.hpp
index 4a15b01c4..a0dd669f2 100644
--- a/packages/native/src/graph/Descriptor.hpp
+++ b/packages/native/src/graph/Descriptor.hpp
@@ -105,6 +105,10 @@ std::string_view kindName(PortKind kind);
 // The most channels one audio port carries; the planner refuses a port that resolves to more.
 constexpr std::uint32_t kMaxChannels = 64;
 
+// Why a channel count outside 1..kMaxChannels cannot be used, or nullopt. `what` names the thing in the message ("a
+// delay", "inputs together"), which is the part of a settings refusal a document's author reads.
+std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what);
+
 // Why a port's declared capacity cannot be used (it is on a port that is not events or bytes, or on an input, or a
 // part lies outside 1 to its kMaxQueue*), or nullopt. The parser refuses the same, in the same words; the C++
 // authoring layer asks it of every layout it builds, so a capacity adjust_layout sets from settings is checked too.
diff --git a/packages/native/src/graph/node.hpp b/packages/native/src/graph/node.hpp
index 30cd58304..05eac72e8 100644
--- a/packages/native/src/graph/node.hpp
+++ b/packages/native/src/graph/node.hpp
@@ -36,7 +36,11 @@
 //                                                         commit writes, so unlike needs_rebuild it allows
 //                                                         stateless_prepare. Never both on one type
 //   static std::size_t extra_memory(const Settings&);     host-supplied tail memory (max_align_t aligned)
-//   void attach(std::span<std::byte>) noexcept;           receives it, at create
+//   void attach(std::span<std::byte>) noexcept;           receives it, at create. The objects a node builds
+//                                                         there are never destroyed - nothing runs a destructor
+//                                                         over tail memory - so a node whose state owns anything
+//                                                         (an allocation, a handle) keeps it in a member, built
+//                                                         in activate, which destroy does free
 //   void on_settings(const Settings&) noexcept;           at create (control) and each commit (audio): copy,
 //                                                         never allocate
 //   void bind_host(const rp_host*) noexcept;              at create: host services
diff --git a/packages/native/src/graph/nodes/CoreNodes.cpp b/packages/native/src/graph/nodes/CoreNodes.cpp
index f181da2e5..f58dada44 100644
--- a/packages/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/native/src/graph/nodes/CoreNodes.cpp
@@ -100,12 +100,6 @@ std::optional<std::string> checkCount(std::uint32_t n, std::string_view what) {
     return std::nullopt;
 }
 
-std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what) {
-    if (n < 1 || n > kMaxChannels)
-        return std::string(what) + " must be between 1 and " + std::to_string(kMaxChannels) + " channels";
-    return std::nullopt;
-}
-
 // A list of per-port channel counts: 1..kMaxPorts ports of at least one channel, kMaxChannels in all.
 std::optional<std::string> checkChannelList(const std::vector<std::uint32_t>& counts, std::string_view what) {
     const auto ports = static_cast<std::uint32_t>(std::min<std::size_t>(counts.size(), UINT32_MAX));
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 561b64614..6fd9ad39a 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -27,6 +27,7 @@
 #include "graph/HostNodes.hpp"
 #include "graph/Queue.hpp"
 #include "graph/nodes/CoreNodes.hpp"
+#include "graph/nodes/DspNodes.hpp"
 #include "host/dsp/ScriptCompiler.hpp"
 #include "host/devices/DeviceRegistry.hpp"
 #include "host/engine/DspCommand.hpp"
@@ -192,10 +193,11 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
     if (!graph_->script.load(scriptBundle, &why))
         std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
     // The routing, the controller and the protocol nodes are the script package's (src/script/nodes/).
-    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::sameboyPackage(),
-                                rp::host::mesenPackage(), rp::host::hostIoPackage(), rp::host::n8Package(),
-                                rp::host::launchpadPackage()})
-        graph_->registry.addPackage(p);
+    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::dspPackage(), rp::graph::hostPackage(),
+                                rp::host::sameboyPackage(), rp::host::mesenPackage(), rp::host::hostIoPackage(),
+                                rp::host::n8Package(), rp::host::launchpadPackage()})
+        if (!graph_->registry.addPackage(p, &why))
+            std::fprintf(stderr, "[engine] a node package was refused (%s); its types are not registered\n", why.c_str());
     if (!graph_->registry.addPackage(graph_->script.package(), &why))
         std::fprintf(stderr, "[engine] the script node types were refused (%s); none is registered\n", why.c_str());
     // rp.host.systems/1: a system node that builds its own core, on the control thread, inside the setGraph that
diff --git a/packages/native/test/graph/GraphRig.hpp b/packages/native/test/graph/GraphRig.hpp
index 7a7601282..76500f0bc 100644
--- a/packages/native/test/graph/GraphRig.hpp
+++ b/packages/native/test/graph/GraphRig.hpp
@@ -1,16 +1,19 @@
 #pragma once
 
-// A GraphHost wired to the core, host and test packages, plus host output buffers, for the graph-core
+// A GraphHost wired to the core, dsp, host and test packages, plus host output buffers, for the graph-core
 // tests. Quiescent by default, so setGraph flushes inline and run() is the block driver.
 
 #include <memory>
 #include <string>
 #include <vector>
 
+#include <catch2/catch_test_macros.hpp>
+
 #include "TestNodes.hpp"
 #include "graph/GraphHost.hpp"
 #include "graph/HostNodes.hpp"
 #include "graph/nodes/CoreNodes.hpp"
+#include "graph/nodes/DspNodes.hpp"
 
 namespace rp::graph::test {
 
@@ -24,15 +27,26 @@ struct GraphRig {
     std::vector<float*>        outPointers;
     rp_transport               transport{48000.0, 120.0, 0.0, 0};
 
-    // `extra` is a test's own package of node types, registered after the shared ones.
+    // `extra` is a test's own package of node types, registered after the shared ones. `maxFrames` sizes the host
+    // output rows, for a plan whose blocks are larger than the default one; `config.maxFrames` bounds the plan's
+    // block and is the caller's to keep in step.
     explicit GraphRig(HostConfig config = {48000.0, 64, Target::Desktop}, std::uint32_t hostChannels = 8,
-                      const rp_package* extra = nullptr) {
-        registry.addPackage(corePackage());
-        registry.addPackage(hostPackage());
-        registry.addPackage(testPackage());
-        if (extra) registry.addPackage(extra);
+                      const rp_package* extra = nullptr, std::uint32_t maxFrames = kHostFrames) {
+        // A package the registry refuses leaves the plan with fewer types than the test thinks it has, which shows up
+        // as an unrelated case failing; assert the registrations instead.
+        std::string why;
+        for (const rp_package* p : {corePackage(), dspPackage(), hostPackage(), testPackage()}) {
+            const bool added = registry.addPackage(p, &why);
+            INFO(why);
+            REQUIRE(added);
+        }
+        if (extra) {
+            const bool added = registry.addPackage(extra, &why);
+            INFO(why);
+            REQUIRE(added);
+        }
         host = std::make_unique<GraphHost>(registry, resources, config);
-        outs.assign(hostChannels, std::vector<float>(kHostFrames, 0.0f));  // room for blocks after a reactivate
+        outs.assign(hostChannels, std::vector<float>(maxFrames, 0.0f));  // room for blocks after a reactivate
         for (auto& o : outs) outPointers.push_back(o.data());
     }
 
````

Run: `sed -n '5231,5503p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure creates `retroplug-dsp-nodes`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: the behaviour cases pass and the descriptor case fails: `each rp.dsp type's descriptor is its golden` is one `TEST_CASE` looping over the three types, so it reports three failed CHECKs (one per type) inside one failing case, and the summary is `test cases: 13 | 12 passed | 1 failed`. Report the summary line.

- [ ] **Step 3: Record the three descriptor goldens, here and nowhere else**

Each golden is one line of canonical JSON under 1 KB (gain 593 bytes, biquad 913, dither 702), so all three are quoted
whole: a reviewer reads them, and the recording below proves the updater lands the same bytes. The updater is the case
itself under `RP_GRAPH_GOLDEN_UPDATE=1`, which is the kit baseline's precedent for a golden the existing graph goldens
had no updater for.

Create `packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`:

````json
{"category":"dsp/gain","id":"rp.dsp.gain","name":"Gain","params":[{"default":1,"id":"gain","max":4,"min":0,"name":"Gain","type":"float"}],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__GainNodeSettings":{"properties":{"channels":{"type":"integer"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__GainNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Create `packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`:

````json
{"category":"dsp/filter","id":"rp.dsp.biquad","name":"Biquad","params":[{"default":1000,"id":"frequency","max":20000,"min":20,"name":"Frequency","type":"float","unit":"Hz"},{"default":1,"id":"q","max":10,"min":0.1,"name":"Q","type":"float"},{"default":0,"id":"gainDb","max":24,"min":-24,"name":"Gain","type":"float","unit":"dB"}],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__BiquadNodeSettings":{"properties":{"channels":{"type":"integer"},"type":{"enum":["low-pass","high-pass","band-pass","band-stop","peak","low-shelf","high-shelf","all-pass"],"type":"string"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__BiquadNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Create `packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`:

````json
{"category":"dsp/quantise","id":"rp.dsp.dither","name":"Dither","params":[],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__DitherNodeSettings":{"properties":{"algorithm":{"enum":["none","high-pass-tpdf","shaped-tpdf","error-diffusion","jjn","sierra-lite"],"type":"string"},"bits":{"type":"integer"},"channels":{"type":"integer"},"seed":{"type":"integer"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__DitherNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `All tests passed (29170 assertions in 13 test cases)`: all 13 cases, the three goldens included.

Run: `RP_GRAPH_GOLDEN_UPDATE=1 build/bin/retroplug-graph-test "[dsp-nodes]" && git add -- packages/native/test/graph/golden/rp.dsp.gain.descriptor.json packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json packages/native/test/graph/golden/rp.dsp.dither.descriptor.json && git diff --cached --stat 09c57e09 -- packages/native/test/graph/golden`
Expected: nothing printed after the run: the updater rewrites the three files, and the staged diff against the prototype commit is empty, so the quoted blocks are the bytes this tree records. Note the refs' schemas: gain, biquad and dither have none, and every `$defs` key is the settings struct's name.

- [ ] **Step 4: Show that the cases bite**

The behaviour cases also passed on their first green build, so their bite is shown by mutations: each is applied, run,
restored and rebuilt. The first is the type-change mutation the review's fix added its case for; the second drops the
seed setting, which is stage 4's own first-pass mutation and the one that shows a settings commit restarts the noise.
Save `DspNodes.cpp` first.

Run: `cp packages/native/src/graph/nodes/DspNodes.cpp /tmp/d1-dspnodes-good.cpp`
Expected: nothing printed (the file as this step placed it).

Run: `sed -i 's|if (wanted != type) clear = true;|// mutated: no clear on a type change|' packages/native/src/graph/nodes/DspNodes.cpp && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: 13 cases, 12 pass and one fails: `rp.dsp.biquad recomputes its coefficients when the type changes` reports `0 == -1` from its `firstDifference` check. Report the summary line.

Run: `cp /tmp/d1-dspnodes-good.cpp packages/native/src/graph/nodes/DspNodes.cpp && sed -i 's|^        seed = s.seed;$|        // mutated: the seed setting is dropped|' packages/native/src/graph/nodes/DspNodes.cpp && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: the dither's seed case now fails, and so does every case that compares the node against a kernel `init`'ed with a document's non-default seed: `rp.dsp.dither through a plan is its kernel's quantised levels` (its first row seeds the kernel with 12345) and `rp.dsp state is per channel, and its memory is sized from the settings' channels` (21), so the rest do not all pass. The `5 | 3 passed | 2 failed` the prototype recorded is the seed case alone at the 12-case file, not a tag summary; report this tree's summary line.

Run: `cp /tmp/d1-dspnodes-good.cpp packages/native/src/graph/nodes/DspNodes.cpp`
Expected: nothing printed (the restoring copy).

Run: `cp /tmp/d1-dspnodes-good.cpp packages/native/src/graph/nodes/DspNodes.cpp && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: restored: `All tests passed (29170 assertions in 13 test cases)`.

The gain ramp's removal (`const float g = target;`) is stage 4's own first-pass mutation, measured at the 12-case file
before the review's fixes: `12 | 11 passed | 1 failed`, 66 assertions. The review's other mutation asks `extra_memory`
for `kMaxChannels` kernels rather than the settings' channels: `0 == 25280` on the six-channel case. Neither is re-run
here; every count in this step that no report states is the tree's to report.

- [ ] **Step 5: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, `no warnings at all`: the whole tree, the plugin through `retroplug-backend`'s new link line included.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. `retroplug-graph-test` `All tests passed (291234 assertions in 238 test cases)`, `retroplug-audio-test` `1235181 assertions in 109` (unchanged), engine-graph without fixtures `267 | 258 passed | 9 skipped` (unchanged), launchpad `144 / 29`.

Run: `build/bin/retroplug-graph-test "[dsp-nodes],[dsp]"`
Expected: `All tests passed (198109 assertions in 30 test cases)`: the node cases beside the kernels'.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, `46 cases skipped in 10 files`, the skip check passing.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 160 native test file(s) passed`, `dsp-bench` and `nes-bench` the only skips.

Run: `pnpm test && pnpm typecheck && pnpm test:node && pnpm test:cli-ts`
Expected: `# 169 test file(s) passed` (no TS source moved: nothing hardcodes the native node types); typecheck exit 0; `pass 12`, 0 failed; `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 73 file(s) clean`.

- [ ] **Step 6: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `29170 / 13`, no ThreadSanitizer report.

Run: `cmake --build build-asan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `29170 / 13`, no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/src/graph/nodes/DspNodes.cpp packages/native/src/graph/nodes/DspNodes.hpp \
  packages/native/test/graph/DspNodes.test.cpp packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json \
  packages/native/test/graph/golden/rp.dsp.dither.descriptor.json \
  packages/native/test/graph/golden/rp.dsp.gain.descriptor.json packages/native/CMakeLists.txt \
  packages/native/cmake/graph.cmake packages/native/cmake/tests.cmake packages/native/cmake/warnings.cmake \
  packages/native/src/graph/Descriptor.cpp packages/native/src/graph/Descriptor.hpp \
  packages/native/src/graph/node.hpp packages/native/src/graph/nodes/CoreNodes.cpp \
  packages/native/src/host/engine/Engine.cpp packages/native/test/graph/GraphRig.hpp
git commit -F - <<'EOF'
feat(dsp): rp.dsp gain, biquad and dither nodes

A new rp.dsp package of C++ nodes in the node.hpp style, each a thin node
around one block-streaming kernel of src/dsp/, so a live patch and the kit
compilers run the same arithmetic (docs/modular-graph-phase-d1-design.md
section 5):

- rp.dsp.gain: a linear Gain param. A held value is the kernel's multiply,
  exact, from the plan's first block on; a value that moved ramps across one
  block and lands on the new value at its last sample.
- rp.dsp.biquad: the type a setting, frequency, Q and gain dB params, one
  kernel per channel in host-supplied memory. The coefficients are recomputed
  at the block start that first sees a different value: a sweep keeps the
  state (setup), a fresh instance, activation or type change clears it (init),
  and the kernel's output clamp is the node's. One coefficient set is computed
  for the block and assigned to every channel, rather than the same RBJ
  trigonometry once per channel on the block thread.
- rp.dsp.dither: the algorithm (the five or none), bits 1..16 and a seed,
  the state per channel, out as audio at the quantised levels
  (processLevels). A settings commit restarts the noise, so a rendered patch
  is reproducible from its seed; a live patch's default is a fixed one.

Each carries a channels setting and follows it through adjust_layout as
rp.core.delay does, and sizes extra_memory from that setting's channel count,
never from the descriptor's maximum. None declares heap or threads, so all
three plan on the Pico.

The per-channel kernel states are built in the instance's tail memory, which
nothing destroys (node.hpp's hook list now says so): static_asserts hold the
two kernels trivially destructible, so the next kernel that owns anything
fails to build rather than leaking an object per instance, and a node whose
state owns anything keeps it in a member instead. checkChannels moves beside
kMaxChannels in Descriptor.hpp, so the two node files share one rule.

The package is a target of its own (retroplug-dsp-nodes), not a source of
retroplug-graph: every consumer of the engine links retroplug-graph, and the
package brings the DSP library with it. retroplug-graph-exec is untouched. To
make "the package pulls no r8brain" literal, the kernels split in two:
retroplug-dsp-kernels is gain, biquad and dither and links nothing else;
retroplug-dsp-resample is Resample.cpp with r8brain, pthreads and RTTI
PRIVATE. A build that has no use for the resampler omits that target. The
resampler's node cannot join this package (a package id registers once), so it
registers from a package of its own, rp.dsp.resample, as rp.host.io sits
beside rp.host; DspNodes.hpp records that seam and both registration sites
check addPackage's result rather than ignoring it (the engine to stderr, the
graph rig with a REQUIRE).

retroplug-graph-test gains [dsp-nodes]: each node's output through a real plan
equals its kernel run directly, over one block and over splits of 1, 7, 64,
1024 and a ragged mix; a gain's move ramps over one block and a held one is
exact; a biquad's sweep keeps its state, a change of type recomputes it and
clears the state, and a dither's seed is its noise; state is per channel at
six channels, and the extra memory asked for is a kernel per channel the settings
name rather than the descriptor's maximum; a warmed plan block allocates and
frees nothing; a descriptor golden per type
(RP_GRAPH_GOLDEN_UPDATE=1 records them, over a list of packages rather than
this one's type count); and all three plan on Target::Pico. GraphRig grows a
maxFrames argument, so a rig with a larger block sizes its host rows instead
of rebuilding them. The engine registers the package and so does the graph
rig.

Kit baseline hashes unmoved, strict parity bit for bit.
EOF
````

### Task 5: `rp.dsp.resample`, a buffer player that resamples to the plan rate

Design section 5's fourth node, section 8's D2 notes on it, and section 9's fourth question for the resampler alone.
Every port in a plan carries the same frame count at the plan's one rate, so a node cannot take 1024 frames in and give
256 out: `rp.dsp.resample` is a rate-aware sibling of `rp.core.buffer-player`, taking an `audio-buffer` resource as a
settings ref (`"buffer":"res:N"`) at that buffer's own rate and outputting at the plan rate, pulling input as it needs
it. It is a package of its own (`rp.dsp.resample`), with a source file and a target of its own
(`retroplug-dsp-resample-nodes`), because a node that reaches r8brain must be separable at link time and not only
refused by the planner: a Pico build omits the target and the registration, and the planner still refuses the node with
`capability` for `heap`. Its resamplers are node members built in `activate` (tail memory is never destroyed, and
building one allocates on the control thread); `loop` defaults false, so the buffer's end is silence; equal rates still
go through r8brain and are bit-exact (one path, nothing to drift); and a re-activation at a new rate rebuilds the
resamplers and carries on from `origin + floor(emitted * (inRate / oldRate))`, so r8brain's run-in is heard again and up
to one input chunk repeats. `node.hpp` gains `resource_ref::audio()` and a resource ref's schema becomes a string
inline, which the system nodes' descriptors lose their ref `$defs` entries to.

**Files:**
- Create: `packages/native/src/graph/nodes/DspResampleNodes.cpp`, `packages/native/src/graph/nodes/DspResampleNodes.hpp`, `packages/native/test/graph/DspResample.test.cpp`, `packages/native/test/graph/golden/rp.dsp.resample.descriptor.json`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/graph.cmake`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/src/graph/Descriptor.cpp`, `packages/native/src/graph/Descriptor.hpp`, `packages/native/src/graph/node.hpp`, `packages/native/src/graph/nodes/CoreNodes.cpp`, `packages/native/src/graph/nodes/DspNodes.hpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/test/engine-graph/BuildGraphGoldens.test.cpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`, `packages/native/test/graph/DspNodes.test.cpp`, `packages/native/test/graph/GraphRig.hpp`

- [ ] **Step 1: Write the failing cases**

`DspResample.test.cpp` (new, 350 lines, 12 cases, tag `[dsp-resample]`) plays buffers through real plans: 22.05 kHz
into a 48 kHz plan and 48 kHz into the kit rate, at splits of 1, 7, 64, 512 and 1024 frames and a ragged mix, each
against a `Resampler` fed directly and against `rp::dsp::resample` for the first `resampledLength` frames; a buffer
shorter than one chunk; equal rates bit for bit; `loop`, and a loop committed after the buffer has run out; a renamed
buffer as a rebuild; a re-activation at a new rate; a layout channel the buffer lacks (silence) and a node that names
no buffer; a plan's blocks counted from its first block; the Pico's `capability` refusal; and settings refused with a
reason.

Create `packages/native/test/graph/DspResample.test.cpp`:

````cpp
// rp.dsp.resample (src/graph/nodes/DspResampleNodes.cpp, docs/modular-graph-phase-d1-design.md section 5): an audio
// buffer played at the plan's rate through the resample kernel, through real plans built by the graph rig.
//
// - The node's output is the kernel's stream run directly - the buffer, then silence, fed to an rp::dsp::Resampler -
//   bit for bit, whatever blocks the plan runs in (1, 7, 64, 512 and 1024 frames and a ragged mix), and its first
//   resampledLength frames are the kit path's whole-buffer resample: 22.05 kHz into a 48 kHz plan, 48 kHz down to the
//   kit rate, and a buffer shorter than one 1024-frame chunk.
// - At equal rates the output is the buffer itself, bit for bit, though it goes through r8brain.
// - `loop` feeds the buffer again at its end, one seamless stream; a layout channel the buffer lacks is silence, and so
//   is a node that names no buffer.
// - A buffer the settings rename is a rebuild whose stream starts at the new buffer's first frame, at its own rate; a
//   re-activation at a new plan rate carries on from the buffer frame the output had reached.
// - A warmed plan block allocates and frees nothing (the suite's counting allocator): the resamplers are built when the
//   node activates, on the control thread.
// - The node declares heap, so the Pico refuses it (the planner's `capability` code). Its descriptor golden is
//   DspNodes.test.cpp's case, which walks this package beside rp.dsp and carries this file's tag too.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Resample.hpp"
#include "graph/nodes/DspResampleNodes.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr std::uint32_t kMaxBlock = 1024;

// The block sizes the brief's streams are split into, and a ragged sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {512}, {1024}, {1, 31, 512, 3, 1024, 77}};

/** A sine plus noise; `salt` makes another channel's content differ. */
std::vector<float> signal(std::size_t frames, std::uint32_t salt = 0) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u ^ salt;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.2f;
        out[i] = noise + 0.7f * std::sin(static_cast<float>(i) * (0.031f + 0.01f * static_cast<float>(salt)));
    }
    return out;
}

/** The first index two streams differ at, bit for bit, -1 for none, or a value no index can be for unequal lengths. */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<std::ptrdiff_t>::max();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

/** The kernel run directly: `source` from frame `from`, then silence (or `source` again from its first frame, when
 *  looping), resampled `inRate` -> `outRate`, `frames` frames of it. Each pull asks for everything left rather than
 *  for one block's frames as the node's feed does; the pushes are the same stream either way, since the kernel only
 *  ever hands r8brain whole chunks. `loopFrom` is the output frame a loop set mid-stream takes effect at. */
std::vector<float> kernelStream(const std::vector<float>& source, double inRate, double outRate, std::size_t frames,
                                bool loop = false, std::size_t from = 0, std::size_t loopFrom = 0) {
    rp::dsp::Resampler r;
    r.init(inRate, outRate);
    std::vector<float> out(frames);
    std::size_t        done = 0, at = from;
    while (done < frames) {
        done += r.pull(out.data() + done, frames - done);
        if (at >= source.size() && loop && done >= loopFrom) at = 0;
        if (at < source.size()) at += r.push(source.data() + at, source.size() - at);
        else r.pushSilence(rp::dsp::Resampler::kChunk);
    }
    return out;
}

/** The first `n` frames of a stream. */
std::vector<float> head(const std::vector<float>& v, std::size_t n) {
    return std::vector<float>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n));
}

/** A rig at `rate` whose plans run blocks of up to 1024 frames. */
struct ResampleRig : GraphRig {
    explicit ResampleRig(double rate, rp::graph::Target target = rp::graph::Target::Desktop)
        : GraphRig({rate, kMaxBlock, target}, 8, nullptr, kMaxBlock) {}

    // An audio buffer resource at `rate`, one stream per channel.
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel, double rate) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto id = resources.createAudioBuffer(channels, static_cast<std::uint32_t>(perChannel[0].size()), rate);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    /** A resampler of `settings` (a JSON object's members, the buffer among them) into the host output's first
     *  `channels` channels. */
    rp::graph::SetGraphResult play(const std::string& settings, std::uint32_t channels) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        const std::string port = channels == 1 ? "out.1" : "out.1-2";
        return set(R"({"nodes":[{"id":"r","type":"rp.dsp.resample","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(")" +
                   (settings.empty() ? "" : "," + settings) + R"(}},
                               {"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" +
                   std::to_string(channels) + R"(]}}],
                   "wires":[{"from":"r.out","to":"out.)" + port + R"("}]})");
    }

    static std::string ref(rp::graph::ResourceTable::Id id) { return R"("buffer":"res:)" + std::to_string(id) + "\""; }

    /** The first `channels` host channels over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split,
                                           std::uint32_t channels) {
        std::vector<std::vector<float>> out(channels);
        std::size_t                     at = 0, k = 0;
        while (at < frames) {
            const std::size_t n = std::min(split[k++ % split.size()], frames - at);
            run(static_cast<std::uint32_t>(n));
            for (std::uint32_t c = 0; c < channels; ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
            at += n;
        }
        return out;
    }
};

}  // namespace

TEST_CASE("rp.dsp.resample from 22.05 kHz into a 48 kHz plan is its kernel's stream, split any way", "[dsp-resample]") {
    constexpr std::size_t kSource = 4096, kOut = 10000;  // past the source's 8916 frames, into the silence after it
    const std::vector<std::vector<float>> source = {signal(kSource), signal(kSource, 3)};
    std::vector<std::vector<float>>       reference, kit;
    for (const auto& channel : source) {
        reference.push_back(kernelStream(channel, 22050.0, 48000.0, kOut));
        // And the stream's first resampledLength frames are the kit path's whole-buffer resample.
        kit.emplace_back();
        rp::dsp::resample(22050.0, 48000.0, channel.data(), channel.size(), kit.back());
        REQUIRE(kit.back().size() == rp::dsp::resampledLength(kSource, 22050.0, 48000.0));
        CHECK(firstDifference(head(reference.back(), kit.back().size()), kit.back()) == -1);
    }

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer(source, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);
        const auto out = rig.render(kOut, split, 2);
        for (std::size_t c = 0; c < source.size(); ++c) {
            CHECK(firstDifference(out[c], reference[c]) == -1);
            // Held to rp::dsp::resample() itself, a library call outside this file's feed loop, so the independence
            // is structural rather than through the stream that mirrors it.
            CHECK(firstDifference(head(out[c], kit[c].size()), kit[c]) == -1);
        }
    }
}

TEST_CASE("rp.dsp.resample from 48 kHz to the kit rate is its kernel's stream", "[dsp-resample]") {
    constexpr double      kKitRate = 11468.0;
    constexpr std::size_t kSource = 9000, kOut = 3000;  // the source is 2150 frames at the kit rate
    const std::vector<float> source = signal(kSource, 1);
    const std::vector<float> reference = kernelStream(source, 48000.0, kKitRate, kOut);
    std::vector<float>       kit;
    rp::dsp::resample(48000.0, kKitRate, source.data(), source.size(), kit);
    CHECK(firstDifference(head(reference, kit.size()), kit) == -1);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(kKitRate);
        const auto  buffer = rig.buffer({source}, 48000.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        const std::vector<float> out = rig.render(kOut, split, 1)[0];
        CHECK(firstDifference(out, reference) == -1);
        CHECK(firstDifference(head(out, kit.size()), kit) == -1);  // rp::dsp::resample() itself, per split
    }
}

TEST_CASE("rp.dsp.resample of a buffer shorter than one chunk is its kernel's stream", "[dsp-resample]") {
    // 600 frames, the kit tests' short source: the kernel zero-pads the one partial chunk it is, so the node must not
    // wait for a chunk the buffer cannot fill, nor resample the part it has on its own.
    const std::vector<float> source = signal(600, 2);
    const std::vector<float> reference = kernelStream(source, 22050.0, 48000.0, 4096);
    std::vector<float>       kit;
    rp::dsp::resample(22050.0, 48000.0, source.data(), source.size(), kit);
    REQUIRE(kit.size() == 1306);
    CHECK(firstDifference(head(reference, kit.size()), kit) == -1);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        CHECK(firstDifference(rig.render(4096, split, 1)[0], reference) == -1);
    }
}

TEST_CASE("rp.dsp.resample at the plan's own rate is the buffer, bit for bit", "[dsp-resample]") {
    const std::vector<float> source = signal(3000, 4);
    std::vector<float>       expected(source);
    expected.resize(5000, 0.0f);  // then silence
    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 48000.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        CHECK(firstDifference(rig.render(5000, split, 1)[0], expected) == -1);
    }
}

TEST_CASE("rp.dsp.resample loops the buffer into one seamless stream when told to", "[dsp-resample]") {
    const std::vector<float> source = signal(1500, 5);
    const std::vector<float> looped = kernelStream(source, 22050.0, 48000.0, 9000, true);
    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer) + R"(,"loop":true)", 1).ok);
        CHECK(firstDifference(rig.render(9000, split, 1)[0], looped) == -1);
    }
    // And the default is the buffer once: the stream after it is the silent one, not the loop.
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    const std::vector<float> once = rig.render(9000, {64}, 1)[0];
    CHECK(firstDifference(once, kernelStream(source, 22050.0, 48000.0, 9000)) == -1);
    CHECK(firstDifference(once, looped) != -1);
}

TEST_CASE("rp.dsp.resample takes a loop committed after the buffer has run out", "[dsp-resample]") {
    // `loop` is the one setting a commit carries into the running node, and the node promises a loop switched on after
    // the buffer's end restarts it. 1500 frames at 22.05 kHz are 3265 at 48 kHz, so 4096 output frames have run the
    // buffer out and fed the kernel silence past it.
    constexpr std::size_t    kOut = 4096;
    const std::vector<float> source = signal(1500, 10);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    CHECK(firstDifference(rig.render(kOut, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, kOut)) == -1);

    // The same ref, so this is a commit of `loop` into the instance that is already running, not a rebuild. The
    // reference is one stream over both halves of the run, with the loop switched on at the frame the commit lands
    // between.
    REQUIRE(rig.play(ResampleRig::ref(buffer) + R"(,"loop":true)", 1).ok);
    const std::vector<float> after = rig.render(kOut, {64}, 1)[0];
    const std::vector<float> both = kernelStream(source, 22050.0, 48000.0, 2 * kOut, true, 0, kOut);
    CHECK(firstDifference(after, std::vector<float>(both.begin() + kOut, both.end())) == -1);
    CHECK(firstDifference(after, std::vector<float>(kOut, 0.0f)) != -1);  // the buffer, not the silence past it
}

TEST_CASE("rp.dsp.resample sounds the buffer's channels, and silence for the rest", "[dsp-resample]") {
    const std::vector<float> source = signal(2000, 6);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 32000.0);
    // A mono buffer into a stereo layout: one resampler, and the second channel silent, as the player's.
    REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);
    const auto out = rig.render(4000, {64}, 2);
    CHECK(firstDifference(out[0], kernelStream(source, 32000.0, 48000.0, 4000)) == -1);
    CHECK(firstDifference(out[1], std::vector<float>(4000, 0.0f)) == -1);

    // No buffer named at all: it plans, and every block is silence.
    ResampleRig empty(48000.0);
    REQUIRE(empty.play("", 2).ok);
    const auto silent = empty.render(256, {64}, 2);
    CHECK(firstDifference(silent[0], std::vector<float>(256, 0.0f)) == -1);
    CHECK(firstDifference(silent[1], std::vector<float>(256, 0.0f)) == -1);
}

TEST_CASE("rp.dsp.resample of a renamed buffer starts that buffer's stream, at its own rate", "[dsp-resample]") {
    const std::vector<float> a = signal(3000, 7), b = signal(2500, 8);
    ResampleRig              rig(48000.0);
    const auto               first = rig.buffer({a}, 22050.0);
    const auto               second = rig.buffer({b}, 32000.0);
    REQUIRE(rig.play(ResampleRig::ref(first), 1).ok);
    CHECK(firstDifference(rig.render(1000, {64}, 1)[0], kernelStream(a, 22050.0, 48000.0, 1000)) == -1);

    // A ref that moved is a rebuild: the fresh instance built its resampler for 32 kHz on the control thread, and its
    // stream is the second buffer's from that buffer's first frame.
    REQUIRE(rig.play(ResampleRig::ref(second), 1).ok);
    CHECK(firstDifference(rig.render(4000, {64}, 1)[0], kernelStream(b, 32000.0, 48000.0, 4000)) == -1);
}

TEST_CASE("rp.dsp.resample re-activated at a new plan rate carries on where its output was", "[dsp-resample]") {
    const std::vector<float> source = signal(4000, 9);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    CHECK(firstDifference(rig.render(2048, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, 2048)) == -1);

    // 2048 frames at 48 kHz are 940 frames of the 22.05 kHz buffer, so the stream at 44.1 kHz is a fresh resampler's
    // from there; a block-size change alone leaves the stream as it was.
    std::string why;
    REQUIRE(rig.host->reactivate(44100.0, kMaxBlock, &why));
    const auto from = static_cast<std::size_t>(2048.0 * (22050.0 / 48000.0));
    REQUIRE(from == 940);
    const std::vector<float> after = kernelStream(source, 22050.0, 44100.0, 3072, false, from);
    CHECK(firstDifference(rig.render(2048, {64}, 1)[0], head(after, 2048)) == -1);
    REQUIRE(rig.host->reactivate(44100.0, 512, &why));
    CHECK(firstDifference(rig.render(1024, {512}, 1)[0], std::vector<float>(after.begin() + 2048, after.end())) == -1);
}

TEST_CASE("a warmed rp.dsp.resample plan block allocates and frees nothing", "[dsp-resample]") {
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({signal(2048), signal(2048, 1)}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);

    // Counted from the plan's first block, where a resampler built lazily instead of in activate would allocate: the
    // node's activate has already run, on the control thread, so a lazy build is the only thing that could show up
    // here. Ten blocks of 512 run past the source's 4458 frames at 48 kHz, so the count covers the chunks the
    // resamplers complete, the partial one at the source's end and the silent ones after it.
    rp::test::beginCountingAllocations();
    rig.run(512, 10);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("the Pico refuses rp.dsp.resample: it declares heap", "[dsp-resample]") {
    const rp_node_type* type = rp::graph::dspResamplePackage()->node_types[0];
    CHECK(std::string(type->descriptor(type)).find(R"("requires":{"heap":true,"threads":false})") != std::string::npos);

    ResampleRig pico(48000.0, rp::graph::Target::Pico);
    const auto  buffer = pico.buffer({signal(1024)}, 22050.0);
    CHECK(GraphRig::codes(pico.play(ResampleRig::ref(buffer), 1)) == "capability");
    ResampleRig desktop(48000.0);
    const auto  same = desktop.buffer({signal(1024)}, 22050.0);
    CHECK(desktop.play(ResampleRig::ref(same), 1).ok);
}

TEST_CASE("rp.dsp.resample settings are validated with a reason", "[dsp-resample]") {
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({signal(256)}, 22050.0);
    const auto  bad =
        rig.set(R"({"nodes":[{"id":"r","type":"rp.dsp.resample","settings":{"channels":2,"layout":"mono",)" +
                ResampleRig::ref(buffer) + R"(}}],"wires":[]})");
    REQUIRE_FALSE(bad.ok);
    CHECK(GraphRig::codes(bad) == "settings-invalid");
    CHECK(bad.errors[0].message.find("not 2") != std::string::npos);

    // A resource of another type is refused when the instance is created, with the field's name.
    std::string why;
    const auto  bytes = rig.resources.createBytes("rom", {1, 2, 3}, &why);
    REQUIRE(bytes != 0);
    const auto wrong = rig.play(ResampleRig::ref(bytes), 1);
    REQUIRE_FALSE(wrong.ok);
    REQUIRE_FALSE(wrong.errors.empty());
    CHECK(wrong.errors[0].message.find("buffer") != std::string::npos);
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 3b2da9668..329a1ca4a 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -264,8 +264,10 @@ retroplug_add_test_binary(
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
              test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's arithmetic
              test/graph/DspNodes.test.cpp             # the rp.dsp nodes, through real plans, against those kernels
+             test/graph/DspResample.test.cpp          # rp.dsp.resample, through real plans, against its kernel
     LIBS     retroplug-graph
              retroplug-dsp-nodes                      # the rp.dsp package under test
+             retroplug-dsp-resample-nodes             # and the rp.dsp.resample package
              retroplug-dsp-kernels                    # core-free: the standard library
              retroplug-dsp-resample                   # Dsp.test.cpp's resampler
              r8brain                                   # Dsp.test.cpp's reference copy of the kit path's resample
````

Run: `sed -n '6070,6084p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds the source and `retroplug-dsp-resample-nodes` to `retroplug-graph-test`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-resample]"`
Expected: the build stops at the first missing header, `graph/nodes/DspResampleNodes.hpp` (and no `rp::graph::dspResamplePackage()`), so Catch2 never runs. Report the compiler's first few errors.

- [ ] **Step 2: Add the node, its package and its target**

`DspResampleNodes.{hpp,cpp}` (new) hold `rp::graph::dspResamplePackage()` (manifest id `rp.dsp.resample`) and its one
type `rp.dsp.resample` (category `dsp/source`, `heap`), settings `ResampleNodeSettings` `{buffer, channels = 2, layout
= "stereo", loop = false}`, and state that is a `std::vector<rp::dsp::Resampler>` member plus the buffer handle,
`position`, `origin` and `emitted`: `activate` builds one resampler per channel the layout and the buffer share (into
a local vector, moved in, so a `bad_alloc` leaves the previous stream intact) and returns early on a missing or empty
buffer; `process` fills each channel with `pull` and feeds it from the buffer, `pushSilence` or a wrap at frame 0,
every channel with the same counts so they stay in lockstep. `node.hpp` gains `resource_ref::audio()` and the
`rfl::parsing::Parser` specialization that writes a ref as the plain string with a string schema inline, and its hook
comment follows. `checkLayout` moves to `Descriptor.{hpp,cpp}` beside `checkChannels`. `Engine.cpp` registers
`dspResamplePackage()` beside `dspPackage()` and `GraphRig`'s loop and `DspNodes.test.cpp`'s `dspPackages()` carry it
too (its descriptor case tags `[dsp-resample]`); `DspNodes.hpp`'s seam comment names the real package; the two
engine-graph helpers' comments say a settings schema is the one `$defs` entry with properties, since a ref's is a
string inline. `cmake/graph.cmake` declares `retroplug-dsp-resample-nodes` (PUBLIC `retroplug-graph`, PRIVATE
`retroplug-dsp-resample`), `retroplug-backend` links it, and `warnings.cmake` lists it. `DspNodes.test.cpp`'s
warmed-block case also starts counting before the first block, which is where a node that built something on the block
thread would allocate.

Create `packages/native/src/graph/nodes/DspResampleNodes.cpp`:

````cpp
#include "graph/nodes/DspResampleNodes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Resample.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// A named namespace for the settings struct, as DspNodes.cpp's: its JSON Schema $defs key is its qualified name.
namespace dsp {

struct ResampleNodeSettings {
    // The buffer is named by the settings, not bound to a resource port as rp.core.buffer-player's is, because the
    // resampler has to be built for the buffer's rate and building it allocates. A buffer on a port is first seen on
    // the block thread, where nothing may allocate; a buffer the settings name is pinned at create, on the control
    // thread, and a ref that moves is a rebuild (node.hpp's resource_ref), whose fresh instance builds its resamplers
    // in activate, on the control thread as well. What that buys costs ordering, and the cost is not covered: a
    // recorder (rp.core.buffer-recorder) and a resampler on the same buffer in one plan are unsupported. The planner
    // orders a port's reader after its writer and orders nothing for a settings ref, so this node resamples whichever
    // prefix of the buffer the block schedule has written when it runs: a silently truncated stream, with no
    // diagnostic. D2's driver fills a buffer before the plan that plays it, never in the same plan as the resampler.
    rp::resource_ref<"audio-buffer"> buffer;
    std::uint32_t                    channels = 2;
    std::string                      layout = "stereo";
    // Off: the buffer, then silence, which is the kernel's whole-buffer stream (the kit path's resample, before its
    // cut) continued for as long as the plan runs. On: the buffer again from its first frame at its end, as the player
    // loops, one seamless stream through the resampler. Off by default, unlike the player, because the stream the
    // resampler exists for - a kit's, D2's offline chain - is the buffer once; a document that wants a loop says so.
    bool loop = false;
};

}  // namespace dsp

namespace {

using namespace dsp;

// One resampler per channel the layout and the buffer both have, built in activate as node members: a Resampler owns
// an r8brain object and has a real destructor, and objects built in tail memory are never destroyed (node.hpp's hook
// list), so this node has no extra_memory. A layout channel past the buffer's last is silence, as the player's.
//
// At the plan's rate equal to the buffer's the samples still go through the kernel rather than around it: r8brain
// builds no stage at equal rates and hands each chunk back as it took it, and a float's round trip through double is
// exact, so the output is the buffer bit for bit, from one code path rather than a bypass that could drift from it.
//
// A re-activation at the same rate (a new block size) keeps the stream as it is. One at a new rate rebuilds the
// resamplers for it and carries on from the buffer frame the output had reached, not from the buffer's start: an
// activate preserves state (rp_node.h). That frame is the output's length in buffer time, and the frame the resampler
// has consumed trails it by the part of a chunk it is still gathering plus r8brain's filter delay, so a rate change
// mid-run repeats up to one input chunk of the buffer - 1023 frames, 46 ms at 22.05 kHz - where skipping it would
// gap instead. The resume uses the `loop` the last commit left, so a loop toggled before a rate change wraps where the
// stream would have clamped, or clamps where it would have wrapped.
struct Resample {
    static constexpr const char* id = "rp.dsp.resample";
    static constexpr const char* name = "Resample";
    static constexpr const char* category = "dsp/source";
    static constexpr bool        heap = true;  // r8brain allocates when the resamplers are built
    using Settings = ResampleNodeSettings;

    struct { rp::audio_out<rp::stereo> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a resampler")) return e;
        return checkLayout(s.layout, s.channels);
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, s.layout);
    }

    std::vector<rp::dsp::Resampler> resamplers;
    const rp_audio_buffer*          buffer = nullptr;  // pinned by the instance, so it outlives every block
    std::uint32_t                   channels = 2;
    bool                            loop = false;
    double                          rate = 0.0;     // the plan rate the resamplers were built for; 0 before the first
    std::uint32_t                   position = 0;   // the next buffer frame to feed them, 0 .. frames
    std::uint32_t                   origin = 0;     // the buffer frame their stream started at
    std::uint64_t                   emitted = 0;    // frames output since then

    // At create (control) and at each commit (audio). A commit never changes the buffer - a ref that moved is a
    // rebuild - nor the channel count, which changes the layout and so rebuilds too; only `loop` takes effect here.
    void on_settings(const Settings& s) noexcept {
        buffer = s.buffer.audio();
        channels = s.channels;
        loop = s.loop;
    }

    void activate(double sampleRate, std::uint32_t) {
        // ResourceTable::createAudioBuffer refuses a buffer of no frames, but rp_audio_buffer is a public ABI struct,
        // and this is the one place its frame count divides.
        if (!buffer || buffer->frames == 0) return;  // nothing to resample: every block is silence
        if (sampleRate == rate) return;
        std::uint32_t from = 0;
        if (rate > 0.0) {
            const auto elapsed =
                static_cast<std::uint64_t>(static_cast<double>(emitted) * (buffer->sample_rate / rate));
            from = loop ? static_cast<std::uint32_t>((origin + elapsed) % buffer->frames)
                        : static_cast<std::uint32_t>(std::min<std::uint64_t>(origin + elapsed, buffer->frames));
        }
        // Built aside and moved in, so a bad_alloc (which the authoring layer reports as the activation's failure)
        // leaves the resamplers this node had, and their stream, as they were.
        std::vector<rp::dsp::Resampler> built(std::min(channels, buffer->channels));
        for (auto& r : built) r.init(buffer->sample_rate, sampleRate);
        resamplers = std::move(built);
        rate = sampleRate;
        position = origin = from;
        emitted = 0;
    }

    // Fills `n` frames of `out` from one resampler, feeding it `source` from `at` (and silence past the end, or the
    // source again when looping) whenever it runs dry. Every channel's resampler is fed the same counts in the same
    // order, so each leaves `at` where the others do.
    void render(rp::dsp::Resampler& r, const float* source, std::uint32_t& at, float* out, std::uint32_t n) noexcept {
        const std::uint32_t frames = buffer->frames;
        std::size_t done = 0;
        for (;;) {
            done += r.pull(out + done, n - done);
            if (done == n) return;
            // The store is empty now, so a push takes at least a frame unless the resampler was never built.
            if (at >= frames && loop) at = 0;
            std::size_t took = 0;
            if (at < frames) {
                took = r.push(source + at, frames - at);
                at += static_cast<std::uint32_t>(took);
            } else {
                took = r.pushSilence(rp::dsp::Resampler::kChunk);
            }
            if (took == 0) {
                std::fill(out + done, out + n, 0.0f);
                return;
            }
        }
    }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        std::uint32_t       end = position;
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
            float* const y = outputs.out[c];
            if (c >= resamplers.size()) {
                std::fill(y, y + n, 0.0f);
                continue;
            }
            std::uint32_t at = position;
            render(resamplers[c], buffer->data[c], at, y, n);
            end = at;
        }
        position = end;
        emitted += n;
    }
};

}  // namespace

const rp_package* dspResamplePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Resample>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp.resample","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph
````

Create `packages/native/src/graph/nodes/DspResampleNodes.hpp`:

````cpp
#pragma once

#include "graph/rp_node.h"

namespace rp::graph {

// The rp.dsp.resample package (docs/modular-graph-phase-d1-design.md section 5): one type, the resampler, a package
// of its own beside rp.dsp (DspNodes.hpp says why it cannot be one of that package's types).
//
//   rp.dsp.resample   an audio buffer played at the plan's rate   {"buffer":"res:N","channels":2,"layout":"stereo"}
//
// Every port in a plan carries the same frame count at the plan's one rate, so a resampler cannot sit between two
// audio ports. It is a rate-aware sibling of rp.core.buffer-player instead: it reads an "audio-buffer" resource at
// that buffer's own sample rate and outputs at the plan's, pulling input through the resample kernel
// (src/dsp/Resample.hpp) as its output needs it. The kernel feeds r8brain fixed 1024-frame chunks, so the node's output
// stream is the kernel's, the same at any block size.
//
// The buffer is a settings ref rather than a resource port, because the resamplers are built in activate and building
// one allocates; DspResampleNodes.cpp has the threading argument. What that buys costs ordering, and the cost is not
// covered: a recorder (rp.core.buffer-recorder) and a resampler on the same buffer in one plan are unsupported. The
// planner orders a port's reader after its writer and orders nothing for a settings ref, so the node resamples
// whichever prefix of the buffer the block schedule has written when it runs: a silently truncated stream, with no
// diagnostic. D2's driver fills a buffer before the plan that plays it, never in the same plan as the resampler.
//
// This package's id and its only type's id are the same string, "rp.dsp.resample": legal, since the registry keeps
// package ids and type ids apart, but the two lists are not one list.
//
// It declares `heap`, so the planner refuses it on the Pico. Its kernel brings r8brain, pthreads and RTTI with it, so
// it is a target of its own too (retroplug-dsp-resample-nodes, linking retroplug-dsp-resample): a build for a target
// that cannot take those - the Pico's - links neither target and does not name this package in its registration list,
// so the node and r8brain are both absent from its binary, not only refused by its planner.
const rp_package* dspResamplePackage();

}  // namespace rp::graph
````

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 76714da08..6e490c778 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -216,6 +216,7 @@ target_link_libraries(retroplug-backend PUBLIC
     retroplug-core      # SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
     retroplug-graph     # the graph executor, planner and GraphHost the engine runs on
     retroplug-dsp-nodes # the rp.dsp package (gain, biquad, dither), which the engine registers
+    retroplug-dsp-resample-nodes # and the rp.dsp.resample package beside it
     tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
     efsw-static         # NativeFileWatcher's filesystem watcher (PUBLIC-brings efsw/efsw.hpp include dir)
     Threads::Threads
diff --git a/packages/native/cmake/graph.cmake b/packages/native/cmake/graph.cmake
index ad7d63c9b..169910068 100644
--- a/packages/native/cmake/graph.cmake
+++ b/packages/native/cmake/graph.cmake
@@ -49,3 +49,16 @@ set_target_properties(retroplug-dsp-nodes PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
     POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-backend
+
+# retroplug-dsp-resample-nodes: the rp.dsp.resample package (src/graph/nodes/DspResampleNodes.cpp), the resampler as a
+# node, apart from retroplug-dsp-nodes because its kernel is retroplug-dsp-resample: r8brain, and with it the heap,
+# pthreads and RTTI. A build for a target that cannot take those (the Pico's) links retroplug-dsp-nodes and
+# retroplug-dsp-kernels alone and leaves dspResamplePackage() out of its registration list, so neither this node nor
+# r8brain reaches its binary; the planner's refusal of a `heap` node there is the second line, not the only one.
+add_library(retroplug-dsp-resample-nodes STATIC
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspResampleNodes.cpp)
+target_link_libraries(retroplug-dsp-resample-nodes PUBLIC retroplug-graph PRIVATE retroplug-dsp-resample)
+set_target_properties(retroplug-dsp-resample-nodes PROPERTIES
+    CXX_STANDARD 20
+    CXX_STANDARD_REQUIRED ON
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-backend
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index 01d1ea5aa..2ebccef50 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -38,7 +38,8 @@ if(MSVC)
     set(_rp_werror_targets
         retroplug-core retroplug-backend retroplug-host retroplug-cli retroplug-sdl
         retroplug retroplug-dsp retroplug-ui
-        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes)
+        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes
+        retroplug-dsp-resample-nodes)
     foreach(_t IN LISTS _rp_werror_targets)
         if(TARGET ${_t})
             target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
diff --git a/packages/native/src/graph/Descriptor.cpp b/packages/native/src/graph/Descriptor.cpp
index a81c33415..1ac885cf8 100644
--- a/packages/native/src/graph/Descriptor.cpp
+++ b/packages/native/src/graph/Descriptor.cpp
@@ -567,6 +567,15 @@ std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what)
     return std::nullopt;
 }
 
+std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels) {
+    const auto n = layoutChannels(layout);
+    if (!n) return "unknown layout " + jsonString(layout);
+    if (*n != channels)
+        return "layout " + jsonString(layout) + " is " + std::to_string(*n) + " channels, not " +
+               std::to_string(channels);
+    return std::nullopt;
+}
+
 std::optional<std::string> checkCapacity(const PortDesc& port) {
     const QueueCapacity& c = port.capacity;
     if (!c.messages && !c.bytes) return std::nullopt;
diff --git a/packages/native/src/graph/Descriptor.hpp b/packages/native/src/graph/Descriptor.hpp
index a0dd669f2..69d35b783 100644
--- a/packages/native/src/graph/Descriptor.hpp
+++ b/packages/native/src/graph/Descriptor.hpp
@@ -127,6 +127,10 @@ bool matchesPattern(std::string_view pattern, std::string_view portId);
 // or an unknown name.
 std::optional<std::uint32_t> layoutChannels(std::string_view layout);
 
+// Why a layout name cannot carry `channels` channels (an unknown name, or one that means another count), or nullopt.
+// The settings refusal of every node that names its output's layout beside a channel count.
+std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels);
+
 // The layout name a bare channel count (at least 1) gets: mono, stereo, else discrete(n).
 std::string layoutForCount(std::uint32_t channels);
 
diff --git a/packages/native/src/graph/node.hpp b/packages/native/src/graph/node.hpp
index 05eac72e8..a5ba24e8d 100644
--- a/packages/native/src/graph/node.hpp
+++ b/packages/native/src/graph/node.hpp
@@ -77,6 +77,7 @@
 #include <cstring>
 #include <exception>
 #include <limits>
+#include <map>
 #include <new>
 #include <optional>
 #include <span>
@@ -253,8 +254,8 @@ struct link {
 // Create pins before any hook that sees the instance's settings runs, so `on_settings`, `activate`, `process`, `step` and
 // `finish` read a resolved handle (and a prepare that keeps the refs copies the instance's pins into the settings object
 // it installs). The hooks that run before create has pinned anything (`validate`, `adjust_layout`, `rebuild_key`,
-// `extra_memory`, `query_memory`) see only `ref`: a node must not read `bytes()` there. `needs_rebuild` sees handles in
-// both objects, and is skipped for a ref that moved, which is a rebuild on its own.
+// `extra_memory`, `query_memory`) see only `ref`: a node must not read `bytes()` or `audio()` there. `needs_rebuild`
+// sees handles in both objects, and is skipped for a ref that moved, which is a rebuild on its own.
 //
 //   struct Settings { rp::resource_ref<"rom"> rom; };   // JSON: {"rom": "res:7"}; absent: the node names none
 //
@@ -262,21 +263,25 @@ struct link {
 // of another type, an unknown or dropped id, or a host with no table refuses the create with the field's name.
 template <fixed_string Type>
 struct resource_ref {
-    // rfl reads and writes the ref as the plain string the document carries, never the handle it resolves to.
-    using ReflectionType = std::string;
-
+    // rfl reads and writes the ref as the plain string the document carries, never the handle it resolves to, and its
+    // schema is that string inline (the rfl::parsing::Parser specialization at the end of this file).
     static constexpr std::string_view resource_type() { return Type.view(); }
 
     std::string        ref;                 // "res:N" as the document names it (empty: none)
     const rp_resource* resource = nullptr;  // resolved by the host layer at create, and again at each prepare
 
-    std::string reflection() const { return ref; }
     // The payload of a bytes resource (every type but "audio-buffer"), or null when the field names none.
     const rp_bytes* bytes() const {
         return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") != 0
                    ? static_cast<const rp_bytes*>(resource->payload)
                    : nullptr;
     }
+    // The payload of an "audio-buffer" resource, or null when the field names none or names another type.
+    const rp_audio_buffer* audio() const {
+        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") == 0
+                   ? static_cast<const rp_audio_buffer*>(resource->payload)
+                   : nullptr;
+    }
 };
 
 // ---- params ----------------------------------------------------------------------------------------------
@@ -1190,3 +1195,26 @@ T* peer_as(rp_node* n) {
 }
 
 }  // namespace rp
+
+// A resource_ref is the plain string the document carries, read and written as one, and its schema is a string
+// INLINE rather than a $defs entry. rfl names a $defs entry after the C++ type, and a class template over a
+// fixed_string has no spelling compilers agree on (GCC writes the argument's type, Clang drops it), so the default
+// would put a different key into the descriptor under each compiler; a Settings struct is a named type for the same
+// reason (the hook list above).
+template <class R, class W, rp::fixed_string Type, class ProcessorsType>
+    requires rfl::parsing::AreReaderAndWriter<R, W, rp::resource_ref<Type>>
+struct rfl::parsing::Parser<R, W, rp::resource_ref<Type>, ProcessorsType> {
+    using InputVarType = typename R::InputVarType;
+    using String = Parser<R, W, std::string, ProcessorsType>;
+
+    static Result<rp::resource_ref<Type>> read(const R& r, const InputVarType& var) noexcept {
+        return String::read(r, var).transform([](std::string ref) { return rp::resource_ref<Type>{std::move(ref)}; });
+    }
+    template <class P>
+    static void write(const W& w, const rp::resource_ref<Type>& field, const P& parent) {
+        String::write(w, field.ref, parent);
+    }
+    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
+        return String::to_schema(definitions);
+    }
+};
diff --git a/packages/native/src/graph/nodes/CoreNodes.cpp b/packages/native/src/graph/nodes/CoreNodes.cpp
index f58dada44..12159d052 100644
--- a/packages/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/native/src/graph/nodes/CoreNodes.cpp
@@ -118,15 +118,6 @@ std::uint32_t sumOf(const std::vector<std::uint32_t>& v) {  // validated: at mos
     return n;
 }
 
-std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels) {
-    const auto n = layoutChannels(layout);
-    if (!n) return "unknown layout " + jsonString(layout);
-    if (*n != channels)
-        return "layout " + jsonString(layout) + " is " + std::to_string(*n) + " channels, not " +
-               std::to_string(channels);
-    return std::nullopt;
-}
-
 // Emit a control value at a frame, skipping one equal to what the stream already holds.
 struct ControlWriter {
     rp_control_queue& out;
diff --git a/packages/native/src/graph/nodes/DspNodes.hpp b/packages/native/src/graph/nodes/DspNodes.hpp
index 190dddf44..c881d3370 100644
--- a/packages/native/src/graph/nodes/DspNodes.hpp
+++ b/packages/native/src/graph/nodes/DspNodes.hpp
@@ -18,11 +18,12 @@ namespace rp::graph {
 // from the settings' channel count, never from the descriptor's maximum.
 //
 // The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain (heap, pthreads and RTTI) with
-// it, and a node that declares `heap` is refused on the Pico. So it is a source file and a target of its own, and a
-// build without it omits that node and r8brain together: it registers from a package of its own, with its own
-// manifest id (`rp.dsp.resample`, which `rp.host.io` is to `rp.host`), added to the engine's package list and to the
-// graph rig's beside dspPackage(). A package id registers once - NodeRegistry refuses a second package under one - so
-// the resample type can neither be a second rp.dsp nor a type this function's list names.
+// it, and a node that declares `heap` is refused on the Pico. So it is a source file and a target of its own
+// (DspResampleNodes.hpp, retroplug-dsp-resample-nodes), and a build without it omits that node and r8brain together: it
+// registers from a package of its own, with its own manifest id (`rp.dsp.resample`, which `rp.host.io` is to
+// `rp.host`), in the engine's package list and the graph rig's beside dspPackage(). A package id registers once -
+// NodeRegistry refuses a second package under one - so the resample type can neither be a second rp.dsp nor a type
+// this function's list names.
 const rp_package* dspPackage();
 
 }  // namespace rp::graph
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 6fd9ad39a..52ebd8057 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -28,6 +28,7 @@
 #include "graph/Queue.hpp"
 #include "graph/nodes/CoreNodes.hpp"
 #include "graph/nodes/DspNodes.hpp"
+#include "graph/nodes/DspResampleNodes.hpp"
 #include "host/dsp/ScriptCompiler.hpp"
 #include "host/devices/DeviceRegistry.hpp"
 #include "host/engine/DspCommand.hpp"
@@ -193,9 +194,9 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
     if (!graph_->script.load(scriptBundle, &why))
         std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
     // The routing, the controller and the protocol nodes are the script package's (src/script/nodes/).
-    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::dspPackage(), rp::graph::hostPackage(),
-                                rp::host::sameboyPackage(), rp::host::mesenPackage(), rp::host::hostIoPackage(),
-                                rp::host::n8Package(), rp::host::launchpadPackage()})
+    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::dspPackage(), rp::graph::dspResamplePackage(),
+                                rp::graph::hostPackage(), rp::host::sameboyPackage(), rp::host::mesenPackage(),
+                                rp::host::hostIoPackage(), rp::host::n8Package(), rp::host::launchpadPackage()})
         if (!graph_->registry.addPackage(p, &why))
             std::fprintf(stderr, "[engine] a node package was refused (%s); its types are not registered\n", why.c_str());
     if (!graph_->registry.addPackage(graph_->script.package(), &why))
diff --git a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
index f687e4929..b6f60ea49 100644
--- a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
+++ b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
@@ -146,7 +146,7 @@ Goldens readGoldens(const char* path) {
 }
 
 // The settings fields each system node type reads, from its descriptor's schema: the properties of the $defs entry
-// that is the settings struct itself (the others are the resource refs its fields name).
+// that is the settings struct itself, the one entry with properties (a resource ref's schema is a string inline).
 const std::map<std::string, std::set<std::string>>& systemSettingsFields() {
     static const std::map<std::string, std::set<std::string>> fields = [] {
         std::map<std::string, std::set<std::string>> out;
@@ -162,7 +162,7 @@ const std::map<std::string, std::set<std::string>>& systemSettingsFields() {
                 std::optional<rfl::Generic> settingsDef;
                 for (const auto& [name, def] : *defs)
                     if (const auto entry = def.to_object(); entry && entry->get("properties")) {
-                        REQUIRE_FALSE(settingsDef);  // one struct has properties; the rest are the refs it names
+                        REQUIRE_FALSE(settingsDef);  // one entry has properties: the settings struct
                         settingsDef = def;
                     }
                 REQUIRE(settingsDef);
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 698bcf846..896e3a3af 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -901,8 +901,8 @@ std::vector<std::string> tsValues(const std::string& name) {
     return values;
 }
 
-// The settings schema of a registered type, at its $defs entry: the one that is the settings struct (the rest are the
-// resource refs its fields name).
+// The settings schema of a registered type, at its $defs entry: the one entry with properties, the settings struct (a
+// resource ref's schema is a string inline, node.hpp).
 rfl::Generic::Object settingsSchema(NodeRig& rig, const char* type) {
     const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
     REQUIRE(descriptor);
@@ -913,7 +913,7 @@ rfl::Generic::Object settingsSchema(NodeRig& rig, const char* type) {
     std::optional<rfl::Generic::Object> settingsDef;
     for (const auto& [name, def] : *defs)
         if (const auto entry = def.to_object(); entry && entry->get("properties")) {
-            REQUIRE_FALSE(settingsDef);  // one struct has properties; the rest are the refs it names
+            REQUIRE_FALSE(settingsDef);  // one entry has properties: the settings struct
             settingsDef = *entry;
         }
     REQUIRE(settingsDef);
diff --git a/packages/native/test/graph/DspNodes.test.cpp b/packages/native/test/graph/DspNodes.test.cpp
index f9c18a926..a7eb69e50 100644
--- a/packages/native/test/graph/DspNodes.test.cpp
+++ b/packages/native/test/graph/DspNodes.test.cpp
@@ -38,6 +38,7 @@
 #include "dsp/Gain.hpp"
 #include "graph/Descriptor.hpp"
 #include "graph/nodes/DspNodes.hpp"
+#include "graph/nodes/DspResampleNodes.hpp"
 
 namespace {
 
@@ -46,9 +47,9 @@ using rp::graph::test::GraphRig;
 constexpr std::size_t kFrames = 4096;
 
 // The packages whose types this file's descriptor goldens are. A package id registers once, so the resampler's type is a
-// second package of its own (`rp.dsp.resample`) rather than more types in this one; the package that adds it is
-// appended here, and this list is what a case walking "every rp.dsp type's descriptor" walks.
-std::vector<const rp_package*> dspPackages() { return {rp::graph::dspPackage()}; }
+// second package of its own (`rp.dsp.resample`) rather than more types in this one, and this list is what a case
+// walking "every rp.dsp type's descriptor" walks.
+std::vector<const rp_package*> dspPackages() { return {rp::graph::dspPackage(), rp::graph::dspResamplePackage()}; }
 
 // The block sizes a stream is split into, as Dsp.test.cpp's: one call, the usual host block, an odd size, one frame,
 // the resampler's chunk and a ragged sequence that cycles.
@@ -470,16 +471,17 @@ TEST_CASE("a warmed rp.dsp plan block allocates and frees nothing", "[dsp-nodes]
                     "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"b.in"},
                              {"from":"b.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})";
     REQUIRE(rig.set(doc).ok);
-    rig.run(64);  // no warm-up beyond the first block, where the dither seeds its noise: that must not allocate either
 
+    // Counted from the plan's first block, which is where a node that built rather than seeded something on the block
+    // thread would allocate: the dither seeds its noise there, and that must not allocate either.
     rp::test::beginCountingAllocations();
-    rig.run(64, 4);
+    rig.run(64, 5);
     const rp::test::AllocationCount m = rp::test::endCountingAllocations();
     CHECK(m.allocations == 0);
     CHECK(m.frees == 0);
 }
 
-TEST_CASE("each rp.dsp type's descriptor is its golden", "[dsp-nodes]") {
+TEST_CASE("each rp.dsp type's descriptor is its golden", "[dsp-nodes][dsp-resample]") {
     const char* update = std::getenv("RP_GRAPH_GOLDEN_UPDATE");
     for (const rp_package* pkg : dspPackages()) {
         REQUIRE(pkg);
diff --git a/packages/native/test/graph/GraphRig.hpp b/packages/native/test/graph/GraphRig.hpp
index 76500f0bc..9f33b640a 100644
--- a/packages/native/test/graph/GraphRig.hpp
+++ b/packages/native/test/graph/GraphRig.hpp
@@ -1,7 +1,7 @@
 #pragma once
 
-// A GraphHost wired to the core, dsp, host and test packages, plus host output buffers, for the graph-core
-// tests. Quiescent by default, so setGraph flushes inline and run() is the block driver.
+// A GraphHost wired to the core, dsp, dsp.resample, host and test packages, plus host output buffers, for the
+// graph-core tests. Quiescent by default, so setGraph flushes inline and run() is the block driver.
 
 #include <memory>
 #include <string>
@@ -14,6 +14,7 @@
 #include "graph/HostNodes.hpp"
 #include "graph/nodes/CoreNodes.hpp"
 #include "graph/nodes/DspNodes.hpp"
+#include "graph/nodes/DspResampleNodes.hpp"
 
 namespace rp::graph::test {
 
@@ -35,7 +36,7 @@ struct GraphRig {
         // A package the registry refuses leaves the plan with fewer types than the test thinks it has, which shows up
         // as an unrelated case failing; assert the registrations instead.
         std::string why;
-        for (const rp_package* p : {corePackage(), dspPackage(), hostPackage(), testPackage()}) {
+        for (const rp_package* p : {corePackage(), dspPackage(), dspResamplePackage(), hostPackage(), testPackage()}) {
             const bool added = registry.addPackage(p, &why);
             INFO(why);
             REQUIRE(added);
````

Run: `sed -n '6329,6680p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure creates `retroplug-dsp-resample-nodes`.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-resample]"; build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: the behaviour cases pass and the shared descriptor case fails in both selections: `rp.dsp.resample`'s golden does not exist yet, so `[dsp-resample]` reports 13 cases with 1 failure and `[dsp-nodes]` 13 with 1. Both runs happen whatever the first returns (the `;`); report both summaries.

- [ ] **Step 3: Record its descriptor golden, here and nowhere else**

One line, 559 bytes, so it is quoted whole, and the recording below proves the updater lands the same bytes. It is the
golden that found the compiler-dependent `$defs` key: its settings name a `resource_ref`, and the inline schema Step 2
landed is what keeps the key out of every descriptor that names a resource.

Create `packages/native/test/graph/golden/rp.dsp.resample.descriptor.json`:

````json
{"category":"dsp/source","id":"rp.dsp.resample","name":"Resample","params":[],"ports":[{"channels":{"count":2,"layout":"stereo"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":true,"threads":false},"settings":{"$defs":{"rp__graph__dsp__ResampleNodeSettings":{"properties":{"buffer":{"type":"string"},"channels":{"type":"integer"},"layout":{"type":"string"},"loop":{"type":"boolean"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__ResampleNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `build/bin/retroplug-graph-test "[dsp-resample]" && build/bin/retroplug-graph-test "[dsp-resample],[dsp-nodes]"`
Expected: `All tests passed (345 assertions in 13 test cases)` (the 12 behaviour cases plus the shared descriptor case), then `All tests passed (29543 assertions in 25 test cases)`.

Run: `RP_GRAPH_GOLDEN_UPDATE=1 build/bin/retroplug-graph-test "[dsp-resample]" && git add -- packages/native/test/graph/golden && git diff --cached --stat b6cbb412 -- packages/native/test/graph/golden`
Expected: nothing printed after the run. The updater also rewrites the three Task 4 goldens, because the shared descriptor case loops every type `dspPackages()` holds and that now holds both packages; `09c57e09..b6cbb412` does not touch them, so their bytes are rewritten identically. The staged diff over the whole directory against the prototype commit is empty, so the quoted block is the byte the updater writes.

- [ ] **Step 4: Show that the cases bite**

Stage 5's recorded red is the rig without the package registered (12 cases, 0 passed, 12 failed), and the descriptor
golden red until recorded; Step 2 registers the package, so the behaviour cases are green from their first build and
their bite is shown by the lazy-build mutation. The allocation case could not see its own failure until the fix moved
its start before the plan's first block, so that is the mutation to make here: save `DspResampleNodes.cpp`, apply the
patch below (activate stops building the resamplers; the first `process` block builds them, on the block thread, from
the plan rate activate stored in `rate`), rebuild and run.

Run: `cp packages/native/src/graph/nodes/DspResampleNodes.cpp /tmp/d1-resamplenode-good.cpp`
Expected: nothing printed (the node as this step placed it).

````patch
diff --git a/packages/native/src/graph/nodes/DspResampleNodes.cpp b/packages/native/src/graph/nodes/DspResampleNodes.cpp
index 29055cf23..057b657cb 100644
--- a/packages/native/src/graph/nodes/DspResampleNodes.cpp
+++ b/packages/native/src/graph/nodes/DspResampleNodes.cpp
@@ -104,11 +104,6 @@ struct Resample {
             from = loop ? static_cast<std::uint32_t>((origin + elapsed) % buffer->frames)
                         : static_cast<std::uint32_t>(std::min<std::uint64_t>(origin + elapsed, buffer->frames));
         }
-        // Built aside and moved in, so a bad_alloc (which the authoring layer reports as the activation's failure)
-        // leaves the resamplers this node had, and their stream, as they were.
-        std::vector<rp::dsp::Resampler> built(std::min(channels, buffer->channels));
-        for (auto& r : built) r.init(buffer->sample_rate, sampleRate);
-        resamplers = std::move(built);
         rate = sampleRate;
         position = origin = from;
         emitted = 0;
@@ -140,6 +135,12 @@ struct Resample {
     }
 
     void process(const rp::Block& b) noexcept {
+        // mutated: the resamplers are built here, on the block thread, instead of in activate
+        if (resamplers.empty() && buffer) {
+            std::vector<rp::dsp::Resampler> built(std::min(channels, buffer->channels));
+            for (auto& r : built) r.init(buffer->sample_rate, rate);
+            resamplers = std::move(built);
+        }
         const std::uint32_t n = b.frames();
         std::uint32_t       end = position;
         for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
````

Run: `sed -n '6723,6751p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: this is a diff of the file the task placed, and it applies cleanly.

Run: `sed -n '6723,6751p' docs/modular-graph-phase-d1-plan.md | git apply && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-resample]"`
Expected: the patch applies, then the build and run: at least the warmed-block case fails, on `5 == 0` from `CHECK(m.allocations == 0)` (the prototype ran that case alone; report the whole selection's summary line), then restore with `cp /tmp/d1-resamplenode-good.cpp packages/native/src/graph/nodes/DspResampleNodes.cpp` and rebuild.

Run: `cp /tmp/d1-resamplenode-good.cpp packages/native/src/graph/nodes/DspResampleNodes.cpp && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-resample]"`
Expected: restored: `All tests passed (345 assertions in 13 test cases)`.

The sibling case in `DspNodes.test.cpp` was fixed the same way and bites too: a temporary `std::vector<float>
scratch(16);` in `Dither::process`'s restart branch fails it on the first block with `frees == 1`. Stage 5's other
recorded mutations (re-activation ignoring the elapsed output, channel 2 fed from channel 1's end position, always
looping at the buffer's end) each failed their case; the first measured `12 | 11 passed | 1 failed` at the 12-case
file this tag had before the loop-commit case was added.

- [ ] **Step 5: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, no warnings: the whole tree with the fourth target linked in.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. `retroplug-graph-test` `291744 / 250` (238 plus `[dsp-resample]`), `retroplug-audio-test` `1235181 / 109` (unchanged), engine-graph without fixtures `267 | 258 passed | 9 skipped` (unchanged), launchpad `144 / 29`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (37069 assertions in 267 test cases)`: the engine-graph's fixture-gated sections still pass with the refs' schemas inline.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, no file with a skip.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, `46 case(s) SKIPPED in 10 file(s)`, the skip check passing.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 160 native test file(s) passed`, only `dsp-bench` and `nes-bench` skipping.

Run: `pnpm test && pnpm typecheck && pnpm test:node && pnpm test:cli-ts`
Expected: `# 169 test file(s) passed`; typecheck exit 0; `pass 12`, 0 skipped; `PASS: retroplug-cli TypeScript harness`.

Run: `git status --short`
Expected: this task's 18 files plus ` m deps/sameboy`, nothing else: `A` for `packages/native/test/graph/golden/rp.dsp.resample.descriptor.json` (staged by the recording check), `??` for `DspResampleNodes.{hpp,cpp}` and `DspResample.test.cpp`, and ` M` for the other fourteen.

- [ ] **Step 6: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp-resample],[dsp-nodes]"`
Expected: `29543 / 25`, zero ThreadSanitizer reports.

Run: `cmake --build build-asan --target retroplug-graph-test retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp-resample],[dsp-nodes]"`
Expected: `29543 / 25`, no AddressSanitizer or LeakSanitizer report: the `Resampler` members are freed with their instance. (The report also ran the whole graph binary under ASan at an earlier commit, 291716 assertions in 249 cases; the loop-commit case added in review makes today's whole-binary totals Task 7's 291744 / 250.)

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/src/graph/nodes/DspResampleNodes.cpp packages/native/src/graph/nodes/DspResampleNodes.hpp \
  packages/native/test/graph/DspResample.test.cpp packages/native/test/graph/golden/rp.dsp.resample.descriptor.json \
  packages/native/CMakeLists.txt packages/native/cmake/graph.cmake packages/native/cmake/tests.cmake \
  packages/native/cmake/warnings.cmake packages/native/src/graph/Descriptor.cpp \
  packages/native/src/graph/Descriptor.hpp packages/native/src/graph/node.hpp \
  packages/native/src/graph/nodes/CoreNodes.cpp packages/native/src/graph/nodes/DspNodes.hpp \
  packages/native/src/host/engine/Engine.cpp packages/native/test/engine-graph/BuildGraphGoldens.test.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp packages/native/test/graph/DspNodes.test.cpp \
  packages/native/test/graph/GraphRig.hpp
git commit -F - <<'EOF'
feat(dsp): rp.dsp.resample, a buffer player that resamples to the plan rate

rp.dsp.resample is a rate-aware sibling of rp.core.buffer-player: it reads an audio-buffer resource at that buffer's
own sample rate and outputs at the plan's, pulling input through the resample kernel (rp::dsp::Resampler), which
hands r8brain fixed 1024-frame chunks, so the node's output stream is the kernel's at any block size. It registers
from a package of its own (manifest id rp.dsp.resample, beside rp.dsp as rp.host.io sits beside rp.host; the registry
refuses a second package under one id), in the engine's static list and the graph rig's. Its package id and its only
type's id are the same string, which is legal since the registry keeps the two apart, but the two lists are not one.

- The buffer is a settings field (rp::resource_ref<"audio-buffer">), not a resource port. Building a resampler
  allocates, and a buffer on a port is first seen on the block thread; a buffer the settings name is pinned at
  create, on the control thread, and a ref that moves is a rebuild. So the Resamplers are node members built in
  activate, on the control thread (never in extra_memory, whose objects are never destroyed), one per channel the
  layout and the buffer both have; a layout channel past the buffer's is silence, as the player's. A renamed buffer
  is a fresh instance whose stream starts at that buffer's first frame. The node declares heap.
- What that costs is ordering, and it is not covered: a recorder (rp.core.buffer-recorder) and a resampler on the
  same buffer in one plan are unsupported. The planner orders a port's reader after its writer and orders nothing for
  a settings ref, so the node resamples whichever prefix of the buffer the block schedule has written when it runs, a
  silently truncated stream with no diagnostic. D2's driver fills a buffer before the plan that plays it, never in the
  same plan as the resampler, and that is the shape the node supports; the node's header and this message say so
  rather than the planner gaining a rule for it.
- At the buffer's end: silence by default (the kernel's whole-buffer stream, which D2's kit chain cuts at
  resampledLength), or the buffer again from its first frame with "loop":true, one seamless stream through r8brain.
  "loop" is the one setting a commit carries into a running node, and a loop switched on after the buffer has run out
  restarts it. An rp_audio_buffer of no frames (public ABI; the resource table refuses to create one) divides nothing
  here: activate treats it as no buffer at all.
- At equal rates it still runs through the kernel: r8brain builds no stage there and hands each chunk back as it took
  it, so the output is the buffer bit for bit, from the one code path.
- A re-activation at the same rate keeps the stream; one at a new rate rebuilds the resamplers and carries on from
  the buffer frame the output had reached. That frame trails the frame the resamplers have consumed by the part of a
  chunk still being gathered plus r8brain's filter delay, so a rate change mid-run repeats up to one input chunk of
  the buffer (1023 frames, 46 ms at 22.05 kHz) where skipping it would gap; the resume takes the "loop" the last
  commit left.
- The Pico omits it at link time, not only at plan time: the node is its own target, retroplug-dsp-resample-nodes,
  which alone links retroplug-dsp-resample (Resample.cpp, r8brain, pthreads, RTTI). A Pico build links
  retroplug-dsp-nodes + retroplug-dsp-kernels and leaves dspResamplePackage() out of its registration list, so
  neither the node nor r8brain reaches its binary; the planner refusing a heap node on Target::Pico is the second line.

node.hpp: resource_ref gains audio() beside bytes(), and an rfl Parser specialization that reads and writes the ref
as a string and gives its schema as a string inline. rfl names a $defs entry after the C++ type, and GCC and Clang
spell resource_ref<fixed_string> differently, so the default put a compiler-dependent key into every descriptor
naming a resource (system nodes' rom/sram/state too); the new descriptor golden would have failed on Clang. The two
engine-graph helpers that read a system node's settings schema still find its one $defs entry with properties.
checkLayout moves beside checkChannels in Descriptor.{hpp,cpp}, shared by the player and the resampler.

Tests, tag [dsp-resample] in retroplug-graph-test (DspResample.test.cpp): 22.05 kHz into a 48 kHz plan, stereo,
equals the kernel run directly and, at every split, rp::dsp::resample() itself, bit for bit at 1-, 7-, 64-, 512-,
1024-frame and ragged blocks; 48 kHz to 11468 Hz, both references; a 600-frame buffer; equal rates; loop, and a loop
committed after the buffer has run out; a mono buffer in a stereo layout and no buffer; a renamed buffer; a
re-activation at a new rate; a warmed block allocating nothing; Target::Pico refusing it (capability); settings
refusals. That last allocation case counts from the plan's first block, not after a warm-up, so a resampler built
lazily on the block thread fails it (counted: 5 allocations) where a per-block allocation on a warm-up would have
been discarded; DspNodes.test.cpp's stage-4 case now counts from its first block too, the same two-line change, so
the dither's first-block seed is held to what its comment claimed. The descriptor golden is DspNodes.test.cpp's case,
which walks both packages.
EOF
````

### Task 6: the docs say what D1 built

Design sections 1 to 10, the specs, the main design and `AGENTS.md`, as landed (commit 6). The D1 design gets its
Status line, sections 3 to 5 corrected in place to what was built, section 7's landing list and section 8's D2 and D3
notes brought forward, and a new section 10 ("As built"): what each of the six stages settled, section 9's five
answers, what building it changed in sections 3 to 5, the findings (the baseline's coarseness, the dither state's
size, span-bounded per-channel loops, the compiler-dependent `$defs` key, `GraphRig::maxFrames`, nine static packages
plus the script pack) and the suite as built. The specs are rewritten to
what is true now: `spec/01` gains the DSP library and its nodes, `spec/02` the four targets, the two packages and a kit
reproducibility section, `spec/06` three target rows and a `[kit-baseline]` section with the re-record and target
rules, `spec/07` the `D1 built` entry with D2, D3 and the residuals, and `spec/13` the kit-compiles bullet;
`AGENTS.md` gets a shorter kit-baseline paragraph after parity and the three graph-test tags in its Catch2 bullet, and
`docs/lsdj.md` names the kernels, the seed rule and the baseline. The main design's Phase D note says D1 is built.

**Files:**
- Modify: `AGENTS.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-d1-design.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/13-subsystems.md`

- [ ] **Step 1: Land the design, the specs and the live docs**

Every file below is the prototype's diff: the design, five specs, `AGENTS.md`, `docs/lsdj.md` and the main design. No
build or test run is needed: the change is Markdown, and nothing it touches compiles differently. Apply the diff
verbatim; do not edit it. It is the commit the prototype landed and the replay checked, section 10 included, and the
three review findings the header records are code that no task here takes, so nothing in this diff should change for
them.

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 62d887321..33356bd4b 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -96,6 +96,13 @@ The rules below are the parts that don't fit those.
   A hidden bench times a script node per block, and the protocol nodes' plan, at 1, 64 and 512 frames
   (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds eight `[profile]` cases
   (`tools/run-profile.sh unit`).
+  `retroplug-graph-test` holds the DSP library and its nodes: the kernels are chunk-exact and allocation-free in
+  `process` once warmed, the resampler from its first chunk, the padded Sierra Lite and JJN references included
+  (`[dsp]`); each `rp.dsp` node matches its kernel through a real plan, a block counted from the plan's first block
+  allocating nothing, a descriptor golden per type, and gain/biquad/dither all plan on the Pico (`[dsp-nodes]`);
+  and `rp.dsp.resample` plays a 22.05 kHz buffer into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits,
+  takes a loop committed after its buffer ran out, carries on after a rate change and is refused on the Pico
+  (`[dsp-resample]`).
   `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
   plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
   `PluginExporter::reinitParameters` over a toy plugin.
@@ -410,6 +417,14 @@ own change with a re-record, as the LSDj master-sync fix and the narrow-host NES
 (every LSDj case, its MIDI-out and link-cable ones among them, and every controller, risa and GBA case is
 fixture-only), so a Phase B step is accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`.
 
+**The kit baseline** (`build/bin/retroplug-audio-test "[kit-baseline]"`, [spec/06-build-test.md](spec/06-build-test.md))
+holds the kit compilers' bytes: 31 synthesised cases through `KitCompiler` and the two codecs, each bank's FNV-1a-64,
+size and slot length against `packages/native/test/audio/golden/kit-baseline.json`. Exact bytes are compared only on
+`linux-x64-gnu13` (CI's linux-x64 job sets `RP_KIT_GOLDEN_REQUIRE_TARGET=1`), elsewhere a case must agree with itself,
+and a re-record (`RP_KIT_GOLDEN_UPDATE=1`) is refused off that host and for a case that disagrees with itself. Unlike
+parity it is a 4-bit view: a one-ulp change in a dither can leave all 31 hashes matching, which is what `[dsp]`'s
+16-bit references are for.
+
 **`retroplug-cli` as a consumer's whole test harness** (`pnpm test:cli-ts`):
 `retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `run <session.ts>` does one
 file - so a consumer repo (BlipToaster) needs **no Node, npm, esbuild or node_modules**. Its kit is the
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 8f97a41aa..41da98029 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -228,13 +228,16 @@ backend.writeFileAtomic(out, r.bytes());            // save the patched .gb
   Both decode (`kit.sampleData()`) and encode (`compileKit`'s `rotate`) gate on the ROM version, so import
   is byte-correct for any version — `LsdjRom.rotatesSamples` (true for 9.2.0+) drives both. Byte-exact with
   the LSDj tool's own kits.
-- **Sample import = native.** Resample (r8brain, anti-aliased), effects (gain/normalize, RBJ filter,
-  TPDF dither), and 4-bit pack happen in **C++** (`packages/native/src/lsdj/KitCompiler` +
-  `KitUtil`/`SampleUtil`), fanned across an enkiTS pool per sample, decoding WAV/MP3/FLAC via miniaudio.
-  Exposed as the **harness-facet** RPC `compileKit` (CLI/test-host only, never the plugin) on
-  `AudioDriver`. The pure-TS `rom` module orchestrates: whole-kit builds take the returned 16 KB bank
-  directly; single-sample imports splice the compiled bytes beside existing samples' **raw** bytes and
-  rebuild the offset table (`buildKitBank`), budget-checked against `0x3fa0`.
+- **Sample import = native.** Resample (r8brain, anti-aliased), effects (gain/normalize, RBJ filter, TPDF dither),
+  and 4-bit pack happen in **C++**: `packages/native/src/lsdj/KitCompiler` (`KitUtil` / `SampleUtil`) over the
+  block-streaming kernels in `packages/native/src/dsp/`, which the `rp.dsp` graph nodes wrap too, fanned across an
+  enkiTS pool per sample, decoding WAV/MP3/FLAC via miniaudio. The two TPDF dithers seed from a hash of the sample the
+  dither receives (salted by algorithm), so a kit compiles to the same bytes every time, and the other three
+  algorithms are deterministic already; `retroplug-audio-test`'s `[kit-baseline]` holds every compiled bank to a golden
+  (D1, [modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md)). Exposed as the **harness-facet** RPC
+  `compileKit` (CLI/test-host only, never the plugin) on `AudioDriver`. The pure-TS `rom` module orchestrates: whole-kit
+  builds take the returned 16 KB bank directly; single-sample imports splice the compiled bytes beside existing
+  samples' **raw** bytes and rebuild the offset table (`buildKitBank`), budget-checked against `0x3fa0`.
 - **Asset files (community formats, lsdpatch-compatible).** Whole assets interchange as the standard
   files: **`.lsdpal`** palettes (4-char name + the 40-byte palette body), **`.png`** fonts (an 8-tile-wide
   greyscale image — 64×72 for the 71 main tiles, 64×120 for the **extended** layout that also carries the
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index ecc45fc14..fd9851222 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1239,6 +1239,14 @@ leaves (its section 11): P7's remainder, and a cart's battery reachable only at
 - Kit compiles as offline graphs; proof is **byte-identical kit output** against today's `KitCompiler`.
 - CLI `render` and background renders on the offline executor.
 
+D1 is built ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), six commits): the kit path's DSP is a
+core-free library of block-streaming kernels (`src/dsp/`, `retroplug-dsp-kernels` + `retroplug-dsp-resample`), the kit
+compilers run it, and the graph has it as `rp.dsp.gain`, `.biquad`, `.dither` and `rp.dsp.resample`; TPDF dither is
+seeded from the sample it receives, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]`
+records 31 compiled banks. D2 (audio buffers into and out of a document, a run-to-completion driver, normalise and the
+two-rate kit chain, kit compiles as graphs byte-identical to the baseline) and D3 (the CLI `render` and the background
+renders on that driver) are left.
+
 ### Phase E: the Pico runtime
 
 - `exportPlan` for a named target; the static-arena executor built for the RP2350.
diff --git a/docs/modular-graph-phase-d1-design.md b/docs/modular-graph-phase-d1-design.md
index 71f441fc1..52ea88c60 100644
--- a/docs/modular-graph-phase-d1-design.md
+++ b/docs/modular-graph-phase-d1-design.md
@@ -1,6 +1,7 @@
 # Phase D1: the kit baseline and the DSP library
 
-**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9
+**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** built (D1, six commits); section 10 records the
+build and sections 3 to 5 are corrected in place.
 
 Phase D gives the graph DSP of its own ([modular-graph-design.md](modular-graph-design.md) section 8): library nodes
 (gain, biquad, dither, resample), kit compiles as offline graphs held byte-identical to today's `KitCompiler`, and
@@ -52,73 +53,110 @@ The user's, on 2026-11-01:
 D1's first two commits come before any DSP moves.
 
 **The seed.** The kit path seeds the two TPDF algorithms from a hash of the dither's input samples and its algorithm
-(the other three algorithms are deterministic already). A kit compiles to the same bytes every time; the noise is still
-TPDF noise. This is D1's one deliberate kit behaviour change.
+(the other three algorithms are deterministic already). The hash is FNV-1a-64 over the buffer's float bytes, folded to
+a 32-bit word and salted with a stable per-algorithm constant, and the draw is `(rng() >> 8) * 2^-24` rather than
+`std::uniform_real_distribution`, whose sequence the standard does not fix. A kit compiles to the same bytes every
+time; the noise is still TPDF noise. This is D1's one deliberate kit behaviour change.
 
 **The baseline suite.** A Catch2 suite in `retroplug-audio-test` (which links `retroplug-core`) runs today's
 `KitCompiler` over a fixed matrix of cases synthesised in the test, so it needs no fixtures and runs in CI: sines, a
-chirp and seeded noise at 22.05, 44.1 and 48 kHz, and a 600-frame source shorter than one resampler chunk; for LSDj,
-no effect, gain, normalise, each of the eight filters, each of the five dithers, rotate on and off; for risa, the DMC
-codec at several rate indices. Each case's bank is hashed into a golden JSON beside the test, re-recorded only with
-`RP_KIT_GOLDEN_UPDATE=1`. Exact bytes are compared on `linux-x64-gnu13` (as parity does); elsewhere a case must agree
-with itself across two runs. From then on a change that moves a kit byte fails this suite.
+chirp, seeded noise and silence at 22.05, 44.1 and 48 kHz, and a 600-frame source shorter than one resampler chunk;
+for LSDj, no effect, gain, normalise (a silent source too), each of the eight filters, each of the five dithers,
+rotate on and off, one filter-gain-dither chain and a short source; for risa, the DMC codec at five rate indices,
+normalise off, loop on, a silent normalise at two rates and a short source. Each case's bank is recorded in a golden
+JSON beside the test as its hash, size and slot length, re-recorded only with `RP_KIT_GOLDEN_UPDATE=1`. Exact bytes
+are compared on `linux-x64-gnu13` (as parity does), off it a case must agree with itself across two runs while its
+size and slot length are still held, and `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turns the fallback into a failure on CI's
+linux-x64 job. From then on a change that moves a kit byte fails this suite.
 
 ## 4. The DSP library
 
-**The target.** `retroplug-dsp` (`packages/native/src/dsp/`) is a static library linking the C++ standard library and,
-for the resampler kernel only, r8brain. `retroplug-core` links it for the kit path, the node package links it, and so
-does `retroplug-graph-test`, which tests the nodes with no core, txiki or backend.
+**The targets.** `retroplug-dsp-kernels` (`packages/native/src/dsp/`) is a static library linking the C++ standard
+library and nothing else: gain, peak and the normalising gain, the biquads, dither and quantise. `retroplug-dsp-resample`
+is `Resample.cpp` alone, linking r8brain and (for r8brain's filter-cache mutex) `Threads::Threads`, so a build with no
+resampler omits that one target and sees neither the heap nor pthreads nor RTTI. `retroplug-core` links both for the kit
+path, the node packages link them, and so does `retroplug-graph-test`, which tests the nodes with no core, txiki or
+backend. The design's `retroplug-dsp` name is taken: `dpf_add_plugin(retroplug ...)` creates the plugin's own DSP
+library of that name.
 
-**The kernels.** Each is a plain state struct, an `init` on the control thread (coefficients; the resampler's
-allocation), and a `process(in, out, frames)` that is `noexcept` and allocates nothing:
+**The kernels.** Each is a plain state struct and an `init` on the control thread (coefficients; the resampler's
+allocation). Gain, biquad and dither add a `process(in, out, frames)` that is `noexcept` and allocates nothing; the
+resampler's counts differ, so its streaming calls are the push/pull set below.
 
-- **gain**: a linear multiply; beside it a whole-buffer `peak` measurement, which the kit path normalises with;
+- **gain**: a linear multiply; beside it a whole-buffer `peak` measurement and the normalising gain the kit path folds
+  into it (a reciprocal multiply, not a division: the bytes depend on it);
 - **biquad**: the RBJ setup for all eight types in `float` as today, the same loop, the same clamp (part of the bytes);
+  `init` builds the coefficients and clears the state, `setup` moves the coefficients and keeps the state, so a sweep
+  does not click;
 - **dither / quantise**: the five algorithms plus `none` (today's plain rounding when no dither is set), a bit depth
-  and a seed, out as integer codes 0..2^bits-1 with a helper mapping codes to audio levels; Sierra Lite's and JJN's
-  forward error terms live in a small fixed array in the state, the same arithmetic without the padded copy;
-- **resample**: one r8brain `CDSPResampler24`, built in `init`, fed exactly as `convertSamplerate` feeds it (fixed
-  1024-frame chunks, the tail zero-padded), so its output stream is the same however the input arrives; the offline
+  (1 to 16) and a seed, out as `uint16_t` codes 0..2^bits-1 with a helper mapping codes to audio levels; `inputSeed()`
+  hashes a buffer the way the kit path seeds it and `init` applies the per-algorithm salt. Sierra Lite's and JJN's
+  forward error terms live in the state as the last two / four errors, the same arithmetic without the padded copy;
+- **resample**: one r8brain `CDSPResampler24`, built in `init`, fed exactly as `convertSamplerate` fed it (fixed
+  1024-frame chunks, the tail zero-padded), so its output stream is the same however the input arrives. Its API is
+  push/pull rather than `process(in, out, frames)`, because the counts differ: `push` fills the chunk and resamples it
+  into a fixed ring, `pushSilence` continues the stream past the source, `pull` hands out output, and the offline
   length rule (`floor(n * out / in)`) is a separate whole-buffer helper, for the kit path now and D2's driver later.
 
-**Today's code on the kernels.** `lsdj/Effects`, `KitUtil::convertSamplerate`, the no-dither quantise and the risa DMC
-path's effects and resample become loops over the kernels; `Effects.cpp`'s math moves into them, and the `rfl` tagged
-union stays where the kit specs need it. The baseline must not move by a single hash.
+**Today's code on the kernels.** `lsdj/Effects` maps its fields onto the kernels and `processEffect(GainEffect)` moves
+out of line; a new `lsdj::quantise(buffer, const DitherEffect*)` gives the 4-bit codes, seeded from `inputSeed`; the
+no-dither quantise and the risa DMC path's effects and resample become loops over the kernels. `Effects.cpp`'s math
+moves into them and the `rfl` tagged union stays where the kit specs need it. `KitUtil::convertSamplerate` is deleted,
+`SampleUtil::convertScaledF32ToNibbles` becomes `convertCodesToNibbles` over the codes, and `fnv1a64` gains an optional
+running hash so a big-endian host seeds without a copy. The baseline must not move by a single hash.
 
 ## 5. The `rp.dsp` nodes
 
 A new `rp.dsp` package of C++ nodes in the `node.hpp` style, modelled on `rp.core.delay` (state in host-supplied
 memory, settings copied to plain data, everything `noexcept`), each wrapping a kernel, registered by the engine and by
-the graph test rig:
-
-- **`rp.dsp.gain`**: audio in, audio out, any channel count; a linear `gain` param. A held value is an exact multiply
-  (the kit path's); a change ramps across one block so a live move does not click.
-- **`rp.dsp.biquad`**: state per channel; the type a setting; frequency, Q and gain (dB) params, so a patch can sweep
-  them, the coefficients recomputed at block start when one changes; the clamp kept.
-- **`rp.dsp.dither`**: settings for the algorithm (the five, or `none`), the bit depth (1 to 16) and a `seed`; out as
-  audio at the quantised levels. D2's kit compile fills `seed` from the input hash; a live patch gets a fixed default.
+the graph test rig (the resampler is a package of its own beside it):
+
+- **`rp.dsp.gain`**: audio in, audio out, any channel count; one linear `gain` param (0..4, default 1). A held value
+  is an exact multiply (the kit path's); a change ramps across one block from the previous block's end to the block's
+  last value, and the first block after activation snaps, so a document's value is exact from sample 0.
+- **`rp.dsp.biquad`**: state per channel; the type a setting; frequency, Q and gain (dB) params (20..20000 Hz, Q
+  0.1..10, gain -24..24 dB, the last read by Peak and the shelves only), so a patch can sweep them, the coefficients
+  recomputed at block start when one changes; `init` clears the state on activation and on a type change, `setup` keeps
+  it on a sweep; the clamp kept, and no other clamp added.
+- **`rp.dsp.dither`**: settings for the algorithm (the five, or `none`), the bit depth (1 to 16) and a `seed` (default
+  `none`, 4 bits and `0x9e3779b9`); out as audio at the quantised levels. A settings commit restarts the noise. D2's kit
+  compile fills `seed` from `inputSeed` over the whole pre-trim buffer; a live patch gets a fixed default.
 - **`rp.dsp.resample`**: every port in a plan carries the same frame count at the plan's one rate, so a node cannot take
-  1024 frames in and give 256 out. The resampler is a rate-aware sibling of `rp.core.buffer-player`: it reads an
-  `audio-buffer` resource at that buffer's own rate and outputs at the plan rate, pulling input as it needs it. It
-  declares `heap` (r8brain allocates), so the planner refuses it on the Pico. Running effects at the input rate before
-  resampling, as the kit chain does, takes two passes at two rates: D2's driver.
+  1024 frames in and give 256 out. The resampler is a rate-aware sibling of `rp.core.buffer-player`: it takes an
+  `audio-buffer` resource as a settings ref (`"buffer":"res:N"`) at that buffer's own rate and outputs at the plan
+  rate, pulling input as it needs it. The buffer is a settings ref, not a resource port, because the resamplers are
+  built at activation and building one allocates: a port's binding is first seen on the block thread, a settings ref is
+  pinned at create on the control thread. The cost is ordering: a `rp.core.buffer-recorder` and a resampler on one
+  buffer in one plan are unsupported (the node reads whichever prefix the block schedule wrote, silently), and D2's
+  driver fills a buffer before the plan that plays it. It declares `heap` (r8brain allocates), so the planner refuses it
+  on the Pico, and it is a package of its own, `rp.dsp.resample`, with a target of its own, so a Pico build also leaves
+  it and r8brain out of the binary. `loop` defaults false (silence past the buffer), equal rates still go through
+  r8brain and are bit-exact, and a mid-run rate change rebuilds the resamplers and carries on from the buffer position
+  the output had reached, repeating up to one input chunk. Running effects at the input rate before resampling, as the
+  kit chain does, takes two passes at two rates: D2's driver.
 
-Gain, biquad and dither declare neither `heap` nor `threads`, so they stay planable on the Pico (Phase E).
+Gain, biquad and dither declare neither `heap` nor `threads`, so they stay planable on the Pico (Phase E). Their package
+is `retroplug-dsp-nodes`, a target of its own rather than part of `retroplug-graph`, because the package brings the DSP
+library and `retroplug-graph` is what every consumer links; the resampler's package is `retroplug-dsp-resample-nodes`,
+beside it, so only a build that wants the resampler reaches r8brain.
 
 ## 6. Tests
 
 - **The kit baseline** (section 3): every hash unchanged after the kernel move.
 - **Kernels**, in `retroplug-graph-test`: one block equals the same input split into 1-, 7-, 64- and 1024-frame blocks,
-  bit for bit; a warmed `process` allocates and frees nothing (the suite's CountingAllocator); the same seed gives the
-  same codes and different seeds different noise; Sierra Lite and JJN match their old padded arithmetic; resample's
-  output is independent of the input's chunking, a source shorter than a chunk included.
+  bit for bit; a `process` allocates and frees nothing once warmed, and the resampler from its very first chunk (the
+  suite's CountingAllocator); the same seed gives the same codes and different seeds different noise; Sierra Lite and
+  JJN match their old padded arithmetic; resample's output is independent of the input's chunking, a source shorter
+  than a chunk included.
 - **Nodes**, through real plans (the graph rig): each node's output equals its kernel run directly; ragged block splits
-  give identical output; a param change ramps over one block and a held one is exact; a warmed plan block allocates
-  nothing; a descriptor golden per type; resample from a 22.05 kHz buffer into a 48 kHz plan matches the kernel's
-  stream, and the Pico target refuses it.
+  give identical output; a param change ramps over one block and a held one is exact; a plan's blocks allocate and free
+  nothing counted from its first; a descriptor golden per type; resample from a 22.05 kHz buffer into a 48 kHz plan
+  matches the kernel's stream, and the Pico target refuses it.
 - **Unchanged**: strict parity bit for bit (nothing in the live engine path changes), `pnpm test:native`'s `lsdj-rom` and
-  `risa-kit` cases plus one new case (a CLI kit compiled twice gives identical bytes), both consumer suites, and the new
-  tags under both sanitizers.
+  `risa-kit` cases plus one new case (a kit compiled twice gives identical bytes), both consumer suites, and the new
+  tags under both sanitizers. The tags as built are `[dsp]`, `[dsp-nodes]` and `[dsp-resample]` in
+  `retroplug-graph-test`, `[kit-baseline]` and `[dither]` in `retroplug-audio-test`, and `kit-dither` in
+  `test-native`.
 
 ## 7. Landing
 
@@ -126,9 +164,9 @@ Six commits, each green:
 
 1. **Dither seeded from its input** (the one deliberate kit behaviour change; nothing is re-recorded, as no baseline
    exists yet).
-2. **The kit baseline suite** and its golden hashes, recorded at commit 1's tree.
-3. **`retroplug-dsp`**: the kernels, and `lsdj/Effects`, `KitUtil`'s resample and quantise and the risa DMC path moved
-   onto them; the baseline does not move by one hash.
+2. **The kit baseline suite** and its golden hashes and slot lengths, recorded at commit 1's tree.
+3. **`retroplug-dsp-kernels` and `retroplug-dsp-resample`**: the kernels, and `lsdj/Effects`, `KitUtil`'s resample and
+   quantise and the risa DMC path moved onto them; the baseline does not move by one hash.
 4. **`rp.dsp.gain`, `rp.dsp.biquad`, `rp.dsp.dither`.**
 5. **`rp.dsp.resample`.**
 6. **Docs**: this design's as-built record, the specs, AGENTS.md.
@@ -144,9 +182,26 @@ implementers and follow-ups, Opus code reviews of tasks 3 and 5, a DeepSeek acce
   file, and one reading a buffer back), a run-to-completion driver over `GraphHost`, normalise in a graph, the two-rate
   kit chain, and kit compiles as graphs behind the same `compileKit` / `compileDmc` RPCs, byte-identical to D1's
   baseline; the throughput question (one plan per slot on N threads, as today's enkiTS fan-out, or the fan-out in the
-  driver); and whether the plugin gains a kit compile.
+  driver); and whether the plugin gains a kit compile. What D1's build adds to the work:
+  - **The dither seed is computed before the run**, from `rp::dsp::inputSeed` over the whole pre-trim buffer at the kit
+    rate; a graph that streams cannot hash its own input, so the driver passes that word in `rp.dsp.dither`'s `seed`.
+    Hashing only the first block, or the trimmed input, moves every LSDj dither kit.
+  - **Normalise must call `rp::dsp::normalizedGain`** (and `peak` over the whole buffer): it is a reciprocal multiply,
+    where a division rounds differently and moves kit bytes. The graph form still has to be decided (section 5's gain is
+    a plain streaming gain; D2 owns the offline form).
+  - **The resample node plays a buffer, it does not fill one.** A recorder and a resampler on the same buffer in one
+    plan are unsupported, so the driver fills each buffer before the plan that plays it. The node also repeats up to
+    one input chunk on a rate change, and it must not bypass `Resampler` to feed r8brain host blocks: r8brain's double
+    output is split-dependent even though the float cast hides almost all of it.
+  - **The kit chain's order is the bytes**: window, the effects at the source rate, resample to the kit rate, quantise
+    (codes), `frameAlign`, then the nibble pack. D2's graph must keep that order and the whole-buffer cut at
+    `resampledLength`; the D1 baseline is its oracle. The codes are a graph's problem: `rp.dsp.dither` outputs audio at
+    the quantised levels (section 2), so a graph compile either inverts `codeToLevel` - levels are k/(2^bits-1), so
+    `round((level + 1) * 0.5 * maxValue)` recovers the code exactly - or the node gains a codes output D1 does not
+    have; the driver decides, and either way the packer must see the same codes.
 - **D3**: the CLI `render` and the UI's background renders on the offline driver, keeping their block-size sequence and
-  per-block side effects or re-scoping their byte-identity claim.
+  per-block side effects or re-scoping their byte-identity claim. D1 changed neither path, so nothing is owed here
+  beyond D2's driver.
 
 ## 9. Prototype questions
 
@@ -156,7 +211,158 @@ implementers and follow-ups, Opus code reviews of tasks 3 and 5, a DeepSeek acce
   past a small window), and does the baseline hold through the rewrite?
 - Does r8brain allocate anywhere in `process`, or only at construction, so the resample kernel's `process` is truly
   allocation-free?
-- Can `retroplug-graph-test` link `retroplug-dsp` (with r8brain) without pulling anything the graph library must not
-  link, and do the Pico-target refusals hold for `rp.dsp.resample` alone?
+- Can `retroplug-graph-test` link the DSP libraries (`retroplug-dsp-kernels` and `retroplug-dsp-resample`) without
+  pulling anything the graph library must not link, and do the Pico-target refusals hold for `rp.dsp.resample` alone?
 - Does anything outside the kit path call `lsdj/Effects` (the CLI's effect parsing, TS's effect descriptor), and does
   the move keep the reflect-cpp shape the RPC decodes?
+
+## 10. As built
+
+Section 7's six commits landed in order, each green, and nothing on the emulated audio path moved, so strict parity held
+bit for bit at every one and no parity baseline moved. Section 3's own baseline is the new gate: the first two commits
+record it, and every later commit had to leave all its hashes and slot lengths unchanged (stage 3's kernel move, the one
+that could have moved a byte, did not). This section records what each stage decided where the design was silent, what
+section 9 turned out to be, what building it corrected in sections 3 to 5 (each correction is in place there), and the
+findings worth naming; each stage's own report is the full record.
+
+### What each stage settled
+
+- **1, the dither seed.** The hash is over the buffer the dither receives: the windowed, post-effect, r8brain-resampled
+  sample at the kit rate (11468 Hz), before `frameAlign` trims it, not the source-rate window the gain and filter see. It
+  is `rp::hash::fnv1a64` over the float bytes, canonical least-significant byte first whatever the host's byte order,
+  folded `(h ^ (h >> 32))` and salted with a per-algorithm constant (`0x8064fcc5` for HighPassTPDF, `0x262f0818` for
+  ShapedTPDF, each the FNV of the algorithm's name) so renumbering `DitherType` cannot re-seed a kit. The draw is
+  `(rng() >> 8) * 2^-24`, a uniform from raw `mt19937` output that the standard fixes, where
+  `uniform_real_distribution` is implementation-defined; negative zero still hashes apart from zero. The salt, the fold
+  and the draw were pinned by an eight-code golden over silence, which is what D2's driver can check against.
+- **2, the baseline.** It drives `KitCompiler` and the two codecs directly rather than the RPC (the RPC adds only DTO
+  mapping, and constructing it needs an Engine the kit path never touches), and it writes each synthesised source as a
+  32-bit float WAV so `SampleCache` and miniaudio stay in the loop: the test asserts the decoded buffer equals the
+  samples it computed, so the golden pins the compilers and not a quantisation of their input. The record is
+  `{hash, size, slot}` per case: `size` and `slot` are layout facts compared on every host, and `hash` (FNV-1a-64 of
+  the bank) only on `linux-x64-gnu13`; off the target a case compiles twice and must agree with itself, and
+  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` (set on CI's linux-x64 job) turns that fallback into a failure. The target is
+  `rp::buildTarget()`, the same function the RPC reports, and `golden.target` is checked against it. Recording
+  (`RP_KIT_GOLDEN_UPDATE=1`) refuses a case whose two compiles disagree and refuses to write anywhere but the target
+  host. The suite guards against a vacuous pass: every case must compile clean, the bank must carry its slot, and the
+  golden must name exactly the matrix's cases. The matrix grew from 26 to 31 cases in review: silent sources for both
+  normalise paths, risa's normalise-off and loop branches, and the slot length beside the hash.
+- **3, the kernels.** The target named `retroplug-dsp` in section 4 was taken by `dpf_add_plugin(retroplug ...)`, which
+  creates the plugin's own DSP library of that name, so the library is `retroplug-dsp-kernels` (gain, peak, the
+  normalising gain, the biquads, dither and quantise) plus `retroplug-dsp-resample` (r8brain, pthreads, RTTI). The
+  resampler's API is push/pull because its counts differ from a block's: `push` fills the 1024-frame chunk and
+  resamples it into a ring of `2 * getMaxOutLen(1024)`, `pushSilence` continues the stream past the source, `pull`
+  hands out output, and a pull larger than the ring simply takes more pushes. Its `init` is the one allocation, on the
+  control thread, and nothing after the first chunk allocates or frees. The dither seed is split into `inputSeed()`
+  (unsalted) and `init`'s salt, its codes are `uint16_t` (`0..65535` covers 16 bits), and `codeToLevel` is exactly the
+  LSDj decoder's level. Sierra Lite and JJN keep their last two / four forward errors and skip the terms before the
+  stream's first sample rather than adding zero, so the padded form's float operations and their order are reproduced.
+  `Gain::normalizedGain` is a reciprocal multiply because the bytes depend on it. `Biquad` mirrors `lsdj::FilterType`
+  by value with static_asserts, and `KitUtil::convertSamplerate` is deleted.
+- **4, gain, biquad and dither.** The package's home is `retroplug-dsp-nodes`, a target of its own: `retroplug-graph`
+  is what every consumer links, and the package brings the DSP library with it, which the graph library has no business
+  carrying. Gain's param is linear 0..4 default 1; a held value is the kernel's exact multiply, a move ramps across one
+  block from the previous block's end to the block's last value, and the first block after activation snaps. Biquad's
+  type is a setting, its frequency, Q and gain (dB) are params, `setup` keeps state on a sweep and `init` clears it on
+  activation or a type change, and the node hands the kernel an out-of-range frequency rather than clamping, because a
+  clamp would be a different kit byte. Dither's defaults are `none`, 4 bits and seed `0x9e3779b9`; a settings commit
+  restarts the noise. Its per-channel state is about 5 KB on LP64 (`std::mt19937` is 624 words of 8-byte
+  `uint_fast32_t`), sized from `sizeof` and the settings' channel count. All three follow `channels` through
+  `adjust_layout`, keep their kernel state in tail memory (gain's one float in a member), and declare neither `heap` nor
+  `threads`, so all three plan on the Pico.
+- **5, the resampler.** It is a package of its own, `rp.dsp.resample`, with a target of its own
+  (`retroplug-dsp-resample-nodes`), because a node that reaches r8brain must be separable at link time, not only
+  refused by the planner; its package id and its type id are the same string, which is legal because the registry keeps
+  the two namespaces apart. Its buffer is a settings `resource_ref<"audio-buffer">`, not a resource port: building a
+  resampler allocates, a port is first seen on the block thread and no control-thread hook sees its binding, whereas a
+  settings ref is pinned at create and its handle readable in `activate`, so the resamplers are built there (into a
+  local vector and moved in, so a throw leaves the previous stream intact). The cost is ordering: a
+  `rp.core.buffer-recorder` and a resampler on one buffer in one plan are unsupported, and D2 fills a buffer before the
+  plan that plays it. `loop` defaults false (unlike the player's true), the buffer's end is silence or a wrap into the
+  same stream, equal rates still go through r8brain and are bit-exact, and a rate change rebuilds the resamplers and
+  resumes from the output's buffer position, `origin + floor(emitted * (inRate / oldRate))`, so r8brain's run-in is
+  heard again and up to one input chunk repeats. `node.hpp` gained `resource_ref::audio()`, and a resource ref's schema
+  became an inline string (an rfl Parser specialization) so descriptors no longer carry a compiler-dependent `$defs`
+  key.
+- **6, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build.
+
+### Section 9's questions, answered
+
+- **What exactly does the dither seed hash?** The resampled input the dither sees, at the kit rate, salted with the
+  algorithm (stage 1). A streaming node cannot hash its whole input before it starts, so D2's driver must assemble the
+  buffer, hash it with `rp::dsp::inputSeed` and pass that word in `rp.dsp.dither`'s `seed`. A node that hashed only its
+  first block, or its trimmed input, moves every dither kit.
+- **Do Sierra Lite and JJN have an exact streaming form?** Yes. In the padded loop sample i is its scaled input plus the
+  errors of i-2 and i-1 (JJN: i-4 to i-1), each times its weight, added oldest first; keeping the last two / four and
+  adding them in the same order reproduces the same float operations, and the `i + k < n` guards only drop writes past
+  the end. A `[dsp]` case holds the streaming form against verbatim copies of both padded functions at lengths 0 to 5,
+  two noises and an overdriven sine, four bit depths (1, 4, 8 and 16) and five block splits. The kit baseline alone is
+  too coarse to prove it: swapping Sierra's two terms left all 31 hashes matching while the `[dsp]` case failed.
+- **Does r8brain allocate in `process`?** No, only at construction: 8 to 14 blocks in the constructor, then zero
+  allocations and frees over 200 chunks from the first `process`, at every kit-relevant rate pair. A related finding
+  is load-bearing: r8brain's double output depends on how its input is split across calls (low bits in 60 to 90% of
+  samples, though the cast to float hides almost all of it), so the fixed 1024-frame chunk rule is part of the bytes and
+  nothing may feed r8brain host blocks. Two 20000-frame `[dsp]` cases make the reference likelier to see such a flip.
+- **Can `retroplug-graph-test` link the DSP libraries without the core, and do the Pico refusals hold?** Yes to both.
+  The test links `retroplug-graph` + `retroplug-dsp-nodes` + `retroplug-dsp-kernels` (and the resample library for
+  `Dsp.test.cpp`'s reference), with no core, txiki, efsw, LVGL or backend; `nm -u` of the kernels plus r8brain shows
+  only libm, malloc/free, mem*, libstdc++ (r8brain's RTTI) and pthread mutex calls. Gain, biquad and dither plan on
+  `Target::Pico`; `rp.dsp.resample` is refused there with `capability` for `heap`, and it is also absent at link time,
+  since the Pico's build omits its target and its registration.
+- **Who else calls `lsdj/Effects`?** Only the kit path: `KitUtil::compileSample` and `RisaDmcCodec::encode` (the math),
+  as types the two codec specs and the RPC DTOs, and the tests. TS only mirrors the union in `audioDriver.ts` and
+  builds effect objects in the CLI sessions. The rfl shape is untouched: the three structs, both enums and the tagged
+  union are unchanged; only functions moved.
+
+### What building it changed in sections 3 to 5
+
+- **Section 3**: the seed is over the resampled, pre-trim buffer and salted by a stable constant, with a specified draw;
+  the baseline is 31 cases, records the slot length, uses `RP_KIT_GOLDEN_REQUIRE_TARGET` and the shared
+  `rp::buildTarget()`, and refuses a disagreeing case at recording.
+- **Section 4**: the target is `retroplug-dsp-kernels` + `retroplug-dsp-resample`, not `retroplug-dsp`; the resampler's
+  API is push/pull and its `process`-equivalent is allocation-free from the first chunk; the dither's seed is split into
+  an unsalted hash and `init`'s salt, and its codes are `uint16_t`; Sierra Lite and JJN keep the last two / four
+  errors; `normalizedGain` is a reciprocal multiply the kit path must call.
+- **Section 5**: gain ramps at block rate to the block's last value and snaps on activation; the biquad keeps state when
+  its coefficients move; dither's defaults and its settings-commit restart are stated; `rp.dsp.resample` is a package
+  (and target) of its own whose buffer is a settings ref, built in `activate`, with `loop` false, the recorder caveat,
+  and the repeat-at-a-rate-change behaviour.
+
+### Found while building
+
+- **The kit baseline is the only proof of the kernel move, and it is nearly blind to some arithmetic.** A one-ulp
+  change in Sierra Lite's term order moves no 4-bit code across all 31 cases; the `[dsp]` cases had to carry verbatim
+  references and 16-bit depths to see it. The baseline still catches anything that moves a bank, which is the contract
+  D2 needs, but it is not a substitute for the kernel tests.
+- **`sizeof(rp::dsp::Dither)` is about 5 KB on LP64**, not the 2.5 KB an MSVC 32-bit `uint_fast32_t` gives; the node's
+  `extra_memory` derives it from `sizeof`, so the platform difference is harmless, but a channel cap must count it.
+- **A node's per-channel loops are bounded by the span it was attached to, not by its port's channels.** An undersized
+  `extra_memory` therefore makes a wrong stream, not a heap write, which is how the six-channel mutation was caught in
+  a release build.
+- **A resource ref's `$defs` key was compiler-dependent.** rfl names the entry after the C++ type, GCC and Clang spell
+  it differently, and the resample node's new descriptor golden would have failed on the Clang CI job; the general fix
+  (an inline string schema) also changed the system nodes' descriptors, and nothing reads the old entries.
+- **A plan test that renders bigger blocks needs a bigger rig.** `GraphRig`'s host rows hold one default block (1024
+  frames), so a 4096-frame split overran them and corrupted the heap; `GraphRig` now takes a `maxFrames` that sizes
+  the rows.
+- **The engine registers nine static packages plus the script pack** (the `rp.dsp` and `rp.dsp.resample` pair among
+  the nine); a refused package is reported at engine construction and `REQUIRE`d by the rig, so a package id
+  registered twice cannot fail a distant case.
+
+### The suite, as built
+
+- `retroplug-audio-test` gained `[kit-baseline]` (`test/audio/KitBaseline.test.cpp`, 31 banks, one case, 346
+  assertions) and `[dither]` (`test/audio/Dither.test.cpp`, the same-twice cases and the eight-code golden), and
+  `test-native` gained `kit-dither` (`test-native/kit-dither.test.ts`).
+- `retroplug-graph-test` gained `[dsp]` (`test/graph/Dsp.test.cpp`, 17 cases: chunk-exact streaming at 1, 4, 8 and 16
+  bits and every split, a `process` allocating nothing once warmed with the resampler counted from its first chunk, the
+  padded references, the resampler's split independence, the seed golden), `[dsp-nodes]`
+  (`test/graph/DspNodes.test.cpp`, 13 cases: each node against its kernel through a real plan, ragged splits, the gain
+  ramp and snap, the biquad's sweep and type change, the dither's seed and channel sizing, a block counted from the
+  plan's first block, and the descriptor goldens) and `[dsp-resample]`
+  (`test/graph/DspResample.test.cpp`, 12 cases plus the shared descriptor case: a 22.05 kHz buffer into a 48 kHz plan
+  and a 48 kHz one into the kit rate, at six splits, a loop committed after the buffer ran out, a rate change, a block
+  counted from the plan's first block and the Pico refusal). It went from 208 cases to 250 and from 92566 assertions to
+  291744.
+- The kit baseline ran in a fresh process and under ASan, hash for hash, and the nodes and kernels under ASan and TSan
+  with no report; nothing was re-recorded.
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index 529516ef0..75358537c 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -421,6 +421,19 @@ controller's launches to a real Game Boy), through `rp.host.midi-out`, is draine
 plugin (to the DAW, each message at its frame) and the SDL standalone (to its MIDI out port, at once,
 without the frame).
 
+### The DSP library and its nodes
+
+`src/dsp/` is the kit path's gain, biquad, dither and resample as block-streaming kernels, in two targets:
+`retroplug-dsp-kernels` (core-free, the C++ standard library only) and `retroplug-dsp-resample` (r8brain, and with it
+the heap, pthreads and RTTI, so a build with no resampler omits it). `retroplug-core` links both for the kit compilers,
+and the graph has them as `rp.dsp` (`rp.dsp.gain`, `.biquad`, `.dither`) and `rp.dsp.resample`, in targets of their own
+(`retroplug-dsp-nodes`, `retroplug-dsp-resample-nodes`) so `retroplug-graph` carries neither and a Pico build links
+`retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone. The kit compilers and the nodes run the same arithmetic, so a
+patched kit and a rendered patch share one DSP. `rp.dsp.resample` takes an `audio-buffer` resource at that buffer's own
+rate and outputs at the plan's; every other node's ports carry the plan's one rate. The kit compilers' output is
+reproducible: the TPDF dither seeds from a hash of the input it sees, held by the kit baseline
+([06-build-test.md](06-build-test.md), [02-native-host.md](02-native-host.md), D1 design sections 4 and 5).
+
 ---
 
 ## 7. Threading / ownership invariants
@@ -516,6 +529,8 @@ lost mutation.
 | [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), its one `SystemFactory`, the staged host MIDI, the host outputs and the registry; `buildSystem` / `destroySystem` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
 | [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
+| [`src/dsp/`](../packages/native/src/dsp) | the core-free DSP kernels the kit compilers and the `rp.dsp` nodes share: gain and peak, the RBJ biquads, dither and quantise, and the r8brain resampler |
+| [`DspNodes.cpp`](../packages/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither) and the `rp.dsp.resample` package beside it |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
 | [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 9330d1b41..03ab4c485 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -1087,6 +1087,7 @@ its **runtime** classes:
 | [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
 | [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the one core with a [`ChannelTap`](../packages/native/src/system/ChannelTap.hpp) today (it implements the interface itself, `channelTap()` returning `this`), so it is the `SyncTap` target. The engine's tap and build check name no console: it forward-declares `ChannelTap` and includes no core header, and with C5's cart node it has no console check at all |
 | the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
+| the DSP library (`src/dsp/`: gain and peak, the biquads, dither and quantise, the r8brain resampler) | the kit path's effect and resample arithmetic as block-streaming kernels, and the same kernels the `rp.dsp` nodes wrap |
 | `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
 
 The graph the engine runs on is not in `retroplug-core`: the executor, planner and
@@ -1099,6 +1100,33 @@ and the control surface's two) and the script host (`src/host/script/`) compile
 ROM classification (the legacy build's C++ `RomSniffer`) and the LSDj sav codec are TS-owned - neither is in
 the C++ tree.
 
+### The DSP targets and the `rp.dsp` packages
+
+The kit path's DSP and the graph's DSP nodes are the same kernels, in `src/dsp/`, split across four targets:
+`retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise; the C++ standard library alone),
+`retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and RTTI), and the two node packages
+`retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`) and `retroplug-dsp-resample-nodes` (`rp.dsp.resample`), in
+[cmake/graph.cmake](../packages/native/cmake/graph.cmake). `retroplug-core` links both kernel libraries for
+`KitCompiler`, `KitUtil` and `RisaDmcCodec`; `retroplug-graph` links none of them; `retroplug-backend` links both
+packages and `Engine.cpp` registers them beside the other static packages. The split is load-bearing: `rp.dsp` never
+reaches r8brain, so a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone, leaves
+`dspResamplePackage()` out of its registration, and has neither the resampler nor r8brain in its binary, where the
+planner's `heap` refusal is only the second line. `rp.dsp.resample` reads an `audio-buffer` resource named by its
+settings at that buffer's rate and outputs at the plan's; a recorder and a resampler on one buffer in one plan are
+unsupported (a plan orders a reader of a mutable resource only through a wire from its writer, and a settings ref has
+neither), so a driver fills a buffer before the plan that plays it.
+
+### Kit reproducibility
+
+A kit compiles to the same bytes every time. The two TPDF dither algorithms seed their `std::mt19937` from
+`rp::dsp::inputSeed` over the resampled, pre-trim sample the dither receives, salted with a per-algorithm constant, and
+the other three algorithms are deterministic already. `retroplug-audio-test`'s `[kit-baseline]` suite
+([KitBaseline.test.cpp](../packages/native/test/audio/KitBaseline.test.cpp)) compiles a fixed matrix of synthesised
+sources through `KitCompiler` and the two codecs and holds each bank's FNV-1a-64, size and slot length to
+[golden/kit-baseline.json](../packages/native/test/audio/golden/kit-baseline.json): exact bytes on `linux-x64-gnu13`,
+self-agreement and the layout facts elsewhere, with `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on CI's linux-x64 job and
+`RP_KIT_GOLDEN_UPDATE=1` re-recording on that target only. [06-build-test.md](06-build-test.md) has the suite's rules.
+
 The txiki host `TjsHostRuntime` comes from
 [`deps/dpf.js`](../deps/dpf.js), not the shared core.
 
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 4d824e8ec..e5bc24d1c 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -32,6 +32,9 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 | Target | Kind | Produces | In `ALL`? |
 |---|---|---|---|
 | `retroplug-core` | static lib | The shared emulator / kit-codec core. | yes (via consumers) |
+| `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the kit compilers and the `rp.dsp` nodes share (gain and peak, the eight RBJ biquads, dither and quantise); the C++ standard library alone. | yes (via `retroplug-core` and the node packages) |
+| `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. | yes (via `retroplug-core` and the resample node package) |
+| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/native/cmake/graph.cmake), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
 | `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
 | `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
 | `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
@@ -212,6 +215,36 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`
   (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`).
 
+### The kit baseline (`[kit-baseline]`)
+
+Exact kit bytes, not structure and audibility. `KitCompiler`'s output is what LSDj and risa play, and until D1 no test
+compared a compiled bank with anything; the suite is the gate Phase D's graph compiles are held to
+([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)).
+
+- **Matrix.** `[kit-baseline]` ([KitBaseline.test.cpp](../packages/native/test/audio/KitBaseline.test.cpp), in
+  `retroplug-audio-test`) synthesises every source in the test (two sines, a chirp, seeded noise and silence at 22.05,
+  44.1 and 48 kHz, plus a 600-frame source shorter than one resampler chunk), writes each as a 32-bit float WAV in a
+  temp dir, and compiles it through `KitCompiler` over `LsdjKitCodec` / `RisaDmcCodec`, so `SampleCache` and
+  miniaudio's decode are in the loop and the suite needs no fixtures. 31 cases: LSDj has no effect, gain, normalise
+  (a silent source too), each of the eight filters, each of the five dithers, rotate on and off, a filter-gain-dither
+  chain and the short source; risa has five DMC rate indices, normalise off, loop on, a silent normalise at two rates
+  and the short source.
+- **Golden.** `golden/kit-baseline.json` holds `target` and `{hash, size, slot}` per case, sorted, read with
+  reflect-cpp. `hash` is FNV-1a-64 of the bank; `size` and `slot` are the layout facts. The suite asserts each case
+  compiles clean, the bank carries its slot and the golden names exactly the matrix's cases, so a broken case cannot
+  pin a stable empty bank.
+- **Targets.** As parity: exact bytes only on `linux-x64-gnu13` (float arithmetic is bit-stable only per compiler and
+  architecture); elsewhere a case compiles twice and must agree with itself, and `size` / `slot` are still compared.
+  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead, and CI's linux-x64 job sets it. The target is
+  `rp::buildTarget()`, the same function the RPC reports, so the two baselines name one toolchain.
+- **Re-record** with `RP_KIT_GOLDEN_UPDATE=1`. It refuses on any other host, and refuses a case whose two compiles
+  disagree (that case depends on something outside its input). A re-record on a stable tree is a byte-identical file,
+  so a diff shows only moved banks. Moving the target is one change: `kBaselineTarget` in the test, a re-record on the
+  new host, the golden's `target` line, and CI's `RP_KIT_GOLDEN_REQUIRE_TARGET` step on that host.
+- **A trap.** The baseline is coarse: a one-ulp change in a dither's arithmetic can move no 4-bit code across all 31
+  cases (stage 3's Sierra Lite mutation), which is why the kernels have `[dsp]`'s verbatim references at 16 bits.
+  Treat a kit baseline failure as a real change to the bytes, and do not re-record it to make a refactor pass.
+
 | Tier | Backend under test | Command | Runner | Test dir |
 |---|---|---|---|---|
 | **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
@@ -266,7 +299,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
 | `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 protocol and serial checks, `[cart-jobs]` and `[cart-release]` among them since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the padded Sierra Lite and JJN references and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep and type change, the dither's seed and per-channel sizing, a descriptor golden per type, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[kit-baseline]` and `[dither]` hold every compiled bank and the TPDF seed (see [the kit baseline](#the-kit-baseline-kit-baseline)). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
@@ -330,6 +363,7 @@ Pick the cheapest tier that actually exercises the code you touched. Trust-but-v
 |---|---|---|
 | **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug/src` that doesn't need a live core. |
 | **The engine's output, bit for bit**: routing, block handling, MIDI staging, sync, anything Phase B moves | `pnpm test:native parity` | The parity baseline: one changed bit in any lane, MIDI byte or RAM byte fails, which RMS-based tests never see. |
+| **A kit compile or the DSP kernels/nodes**: the kit chain's effects and resample, anything D2's offline graphs will reuse | `pnpm test:plugin` (`[kit-baseline]`, `[dsp]`, `[dsp-nodes]`, `[dsp-resample]`) | The kit baseline holds every compiled bank to its recorded bytes, and the `[dsp]` cases hold the kernels to their padded references at 16 bits where a 4-bit code cannot see a one-ulp move. |
 | **DSP behaviour / cores / the command-ring & snapshot seam**: the protocol nodes and their wires, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem` on the real engine graph, the protocol nodes running in the script host every Engine embeds. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
 | **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
 | **Anything you want to eyeball** in the standalone | `pnpm screenshot` → Read `/tmp/retroplug.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug"`. |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 46d6cafdb..5cb33aa50 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -346,6 +346,26 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     refused for a link that never went quiet.
   - **A retained generation of a script pack, the 0.5 s state publish and the N8 firmware's SD path** are the
     allocations left on the block thread, as C1/C4 list them above; C5 added none.
+- **D1 built: the kit baseline and the DSP library**
+  ([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md), six commits): the kit path's DSP is
+  `src/dsp/` as block-streaming kernels (`retroplug-dsp-kernels`, plus `retroplug-dsp-resample` for r8brain), and
+  `lsdj/Effects`, `KitUtil` and `RisaDmcCodec` run those kernels. The graph has them as `rp.dsp.gain`, `.biquad`,
+  `.dither` and `rp.dsp.resample` (`retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes`, the resampler a package and
+  target of its own so a Pico build can leave it and r8brain out). TPDF dither is seeded from a hash of the sample it
+  sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]` holds 31 compiled banks to
+  a golden. Strict parity held bit for bit at every commit, and the baseline did not move by a hash through the kernel
+  move. What it leaves:
+  - **D2**: audio buffers in and out of a document, a run-to-completion driver over `GraphHost`, normalise in a graph,
+    the two-rate kit chain, and kit compiles as graphs behind `compileKit` / `compileDmc`, byte-identical to the
+    baseline. D1's build fixes the seed rule (hash the whole pre-trim buffer and pass `rp::dsp::inputSeed` in the
+    node's `seed`), normalise (call `normalizedGain`, a reciprocal multiply), and the order (effects at the source
+    rate, resample, quantise, `frameAlign`, nibble pack).
+  - **D3**: the CLI `render` and the UI background renders on the offline driver, keeping their block-size sequence
+    and per-block side effects or re-scoping their byte-identity claim; D1 changed neither path.
+  - **Residuals.** The kit baseline is a 4-bit view and nearly blind to a one-ulp move in Sierra Lite or JJN (the
+    `[dsp]` cases carry that). `rp.dsp.resample` and a recorder on one buffer in one plan are unsupported, and a
+    mid-run rate change repeats up to one input chunk. Dither's per-channel state is about 5 KB. The plugin still
+    cannot compile a kit (the RPCs remain harness-only), which D2 owns.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 823747393..2e0612820 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -27,6 +27,11 @@ import/export paths are written once:
 - **`TRACKER_INTEGRATIONS`** — the registry that maps a system to its catalogs.
 - **`liveSav`** — reads the battery out of the *running* core rather than off disk, which is why
   the Songs menu acts on what the cart currently has.
+- **Kit compiles** are native: `KitCompiler` over `LsdjKitCodec` / `RisaDmcCodec`, with the effects and resample from
+  `src/dsp/` (the same block-streaming kernels the `rp.dsp` graph nodes wrap), exposed as the harness-facet
+  `compileKit` / `compileDmc` RPCs. TPDF dither seeds from a hash of its input, so a kit compiles to the same bytes
+  every time, and `retroplug-audio-test`'s `[kit-baseline]` holds the compiled banks (D1,
+  [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)).
 
 What it does **not** abstract is role registration; each console registers its own roles in
 [romProviders.ts](../packages/retroplug/src/romProviders.ts).
@@ -41,7 +46,8 @@ the `risa-sync` role turns transport into risa's arm / start / 24-PPQN clock / s
 the emulated EverDrive N8 FIFO. Layout support covers risa 2.3.0.
 
 - Code: `src/risa/` (codec + ROM + runtime), `src/risaRole.ts`, `src/risaSync.ts`,
-  `src/tracker/risa*Catalog.ts`. The DMC kit codec is native (`src/risa/RisaDmcCodec.cpp`).
+  `src/tracker/risa*Catalog.ts`. The DMC kit codec is native (`src/risa/RisaDmcCodec.cpp`) and shares the `src/dsp/`
+  effects and resampler with LSDj's kit path.
 - **Prove a change:** `pnpm test risa` / `pnpm test:native risa`, and `pnpm reaper:risa-sync` for
   the real-DAW drift render (not in CI).
 - Deeper: [docs/risa-integration-plan.md](../docs/risa-integration-plan.md) (design rationale; the
````

Run: `sed -n '6900,7551p' docs/modular-graph-phase-d1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

- [ ] **Step 2: Verify the docs**

Run: `pnpm check:docs`
Expected: `check-doc-links: 73 file(s) clean`: the prototype's 72 plus this plan. Every link the new text adds resolves, `src/dsp/` and the two node files included.

Run: `git diff HEAD | python3 -c "import sys; print(sum('\u2014' in l for l in sys.stdin if l.startswith('+')))"`
Expected: `0`: no emdash on any added line, as the stage report's own check found (and none in this commit's message).

Run: `git diff --check`
Expected: clean (no whitespace errors), and `git status --short` lists the 9 files this step changed plus ` m deps/sameboy`; the commit below is what leaves only that line.

- [ ] **Step 3: Commit**

````bash
git add -- AGENTS.md docs/lsdj.md docs/modular-graph-design.md docs/modular-graph-phase-d1-design.md \
  spec/01-architecture.md spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md \
  spec/13-subsystems.md
git commit -F - <<'EOF'
docs: D1 records the kit baseline and the DSP library

D1's six commits are recorded as built: the design's status and sections 3 to 5 corrected in place, section 10
adding what each stage settled, section 9's answers, the findings, and the suite's tags; section 8 now carries
what D2 and D3 inherit.

The specs pick it up: spec/01's DSP library and its nodes, spec/02's four targets, the rp.dsp and rp.dsp.resample
packages and kit reproducibility, spec/06's kit baseline and RP_KIT_GOLDEN_UPDATE plus the new graph-test tags,
spec/07's D1 entry and residuals, and spec/13's kit compiles. AGENTS.md gets the kit baseline and the tags;
docs/lsdj.md's sample-import entry and the main design's Phase D note say what the build made true.

The review's corrections are in: the resampler's rate pairs and the allocation counts as its cases make them
(counted from a plan's first block, and from the resampler's first chunk), the baseline's 31 banks told apart
from Catch2 cases, nine static packages plus the script pack, the kit matrix's short sources, the Pico's link
pair named, and the resource-ordering rule the planner actually applies.
EOF
````

### Task 7: the acceptance run

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. **There is no hardware leg**: D1 changes no console I/O, no cart path and no host device, so
nothing here needs a real NES or the N8 Pro. The counts are the tip verification at `b6cbb412` plus the first docs
commit, whose code is identical to the final tip `00c59da9` (only docs changed after); its logs are in
`/tmp/d1pro/tv-*.log` and its summary lines are in `tv-summary.txt`: every leg exit 0, including both consumers. A line
no log records says so and asks for the count instead.

- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build of every target with no warning at all (Task 6 changed Markdown only). A full build ends at `[100%] Built target retroplug-cli` (the prototype's log ends at `[100%] Built target retroplug-cli`).

Run: `git log --oneline -8`
Expected: the plan's commit first, then Task 6 down to Task 1, then 37c8fd7a's subject as the eighth line.

- [ ] **Step 2: The suites, with and without fixtures**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 169 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. Engine-graph without fixtures `test cases: 267 | 258 passed | 9 skipped`, `assertions: 36914 | 36914 passed`; graph-test `All tests passed (291744 assertions in 250 test cases)` (208 plus `[dsp]`'s 17, `[dsp-nodes]`'s 13 and `[dsp-resample]`'s 12); audio-test `All tests passed (1235181 assertions in 109 test cases)` (105 plus `[dither]`'s 3 and `[kit-baseline]`'s 1); launchpad `All tests passed (144 assertions in 29 test cases)`. The other six binaries are at their own counts: `retroplug-lottie-test` 7 / 1, `retroplug-dynparams-test` 24 / 7, `retroplug-plugin-test` 21 / 6, `retroplug-midi-test` 35 / 19, `retroplug-classid-test` 10 / 3 and `retroplug-watcher-test` 11 / 3, from the tip log's sections. Report all ten lines.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (37069 assertions in 267 test cases)`: the same 267 cases, with the fixture-gated sections running.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` for the serial binary: D1 touches no n8 code, so both are the baseline's.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, every case run, nothing re-recorded, and `git status` shows no parity baseline and no golden changed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, with `46 case(s) SKIPPED in 10 file(s)`. (The tip verification ran the strict form only; the CI form's skip line is stage 5's, on the same tree as the last code commit.)

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 160 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 160 native test file(s) passed (jobs=16)`, with `# 136 case(s) SKIPPED in 61 file(s)` and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, with the fixtures present.

Run: `pnpm test:ui`
Expected: the same 39 files (a skip does not remove a file); report whether any case skipped without the fixtures.

Run: `pnpm test:node && pnpm test:cli-ts && pnpm check:docs`
Expected: `tests 12`, `pass 12`, `fail 0`; `PASS: retroplug-cli TypeScript harness`; `check-doc-links: 73 file(s) clean` (the prototype's 72 plus this plan).

- [ ] **Step 3: The kit baseline, and nothing re-recorded**

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (346 assertions in 1 test case)`: all 31 banks, hash, size and slot, in a fresh process on the recording host.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (346 assertions in 1 test case)`: the same 31 banks with the off-target fallback turned into a failure, which is CI's linux-x64 view of the flag (Task 2 set it there).

Run: `build/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`: the seed's own three cases, as Task 1 left them (no later stage touched this file).

Run: `git status --short`
Expected: only ` m deps/sameboy`: no kit golden, no descriptor golden and no parity baseline moved during the acceptance run. A changed `test/audio/golden/kit-baseline.json` or `test/graph/golden/rp.dsp.*` here means something re-recorded, which nothing in this plan does.

- [ ] **Step 4: The sanitizers, on D1's tags**

D1's tags are `[dsp]`, `[dsp-nodes]` and `[dsp-resample]` in `retroplug-graph-test`, `[kit-baseline]` and `[dither]`
in `retroplug-audio-test`, and `kit-dither` in `test-native`. The tip verification's selections are quoted first with
the counts its logs record, then the selections D1 adds, which no log records: report those. Build each binary in its
own tree first, and note that `[resource]` and `[resources]` ride along in the TSan selection because they are the
resource cases the resampler's settings ref sits beside.

Run: `cmake --build build-tsan --target retroplug-graph-test retroplug-audio-test retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0: all three binaries in the TSan tree.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[resource],[resources]"`
Expected: `All tests passed (198748 assertions in 56 test cases)`, no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-audio-test "[kit-baseline],[dither]"`
Expected: both tags pass with no ThreadSanitizer report; report the counts. Stage 3 recorded `[kit-baseline]` alone under TSan (enkiTS's fan-out) at `346 / 1`, and the pair under ASan at `352 assertions in 4 test cases`.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag, so dropping it changes no case), then `All tests passed (12997 assertions in 93 test cases)`, no ThreadSanitizer report: Task 4 and Task 5 changed `Engine.cpp`'s package registration, so the engine-graph suite is the one that plans with the new packages on a thread.

Run: `cmake --build build-asan --target retroplug-graph-test retroplug-audio-test retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0: all three binaries in the ASan tree.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-audio-test "[kit-baseline],[dither]"`
Expected: `All tests passed (352 assertions in 4 test cases)`, no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample]"`
Expected: every selected case passes with no AddressSanitizer or LeakSanitizer report; report the counts. The prototype's ASan runs of the same tags were `[dsp]` 168939 / 17, `[dsp-nodes]` 29170 / 13 at Task 4 (its descriptor case then looped three types, not four) and `[dsp-resample],[dsp-nodes]` 29543 / 25.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (291744 assertions in 250 test cases)`: the whole graph binary under ASan, no report (the `Resampler` members are freed with their instance).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad]"`
Expected: `All tests passed (12997 assertions in 93 test cases)`, no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 5: The live hosts**

One at a time, with nothing rebuilding the plugin meanwhile. No D1 log records the Reaper checks: the prototype's
acceptance was headless, so report each line, and a failure here is a real finding about a plugin host rather than a
missing figure. (The sdl smoke is the exception: its line is quoted from `tv-sdl.log` below.) They exist because Task
4 and Task 5 changed what the engine's constructor registers: a host whose editor or parameter surface came up blank
would show here and in no Catch2 binary.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines (boot and a non-blank render, the MIDI-clock transport, multi-output audio, resize, the minimum clamp, the wheel, the close guard, the idle reclaim).

Run: `pnpm reaper:editor`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:editor-reopen`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:editor-autoload`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:two-instances-load`
Expected: the check's `PASS` line, with the heartbeat's start and end counts; report both.

Run: `pnpm reaper:params`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:params-clap`
Expected: the check's `PASS` line, and confirm it loaded the CLAP format; report it.

Then the whole Reaper leg at once. Its fixture step authors the LSDj fixtures from `../resources/roms/lsdj/...`, which
does not resolve from this worktree (the sibling resources are at `/workspaces/resources`), so author them by hand
with explicit ROM paths and run the suite with `RP_SUITE_NO_BUILD=1`, which skips the suite's own build and every
fixture, after the build above. Make sure no Reaper or jackd of yours is left first.

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight).

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then `ALL 15 REAPER CHECKS PASSED`, the suite's all-pass line (`tools/run-reaper-suite.sh`:262; a skip instead prints the inconclusive form at :264). Report the line. If a render fails, run that one check alone once (its own `pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

- [ ] **Step 6: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched. Never edit `/workspaces/bliptoaster` or `/workspaces/nesvj`: both commands copy the suite to `/tmp`, run it
there, and remove the copy, and both repositories' `git status --short` must stay empty. D1 changes nothing the CLI's
kits see (the kit RPCs' DTOs and the effect names are untouched), so both pass without an edit.

Run: `R=$PWD; rm -rf /tmp/d1-bt && mkdir -p /tmp/d1-bt/build /tmp/d1-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/d1-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/d1-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/d1-bt/retroplug-cli/ && (cd /tmp/d1-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); e=$?; rm -rf /tmp/d1-bt; exit $e`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed.

Run: `R=$PWD; rm -rf /tmp/d1-vj && mkdir -p /tmp/d1-vj/build /tmp/d1-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/d1-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/d1-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/d1-vj/retroplug-cli/ && (cd /tmp/d1-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); e=$?; rm -rf /tmp/d1-vj; exit $e`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 7: The final tree**

Run: `git status --short`
Expected: only ` m deps/sameboy`; no scratch file and no parity or golden baseline changed.

