# Phase D2: kit compiles as offline graph jobs - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Every kit compile becomes a graph run: a generic offline job seam runs a graph document to completion on a
host of its own and hands back the buffers it recorded, TypeScript builds the LSDj and risa kit chains' documents out
of `rp.core`, `rp.dsp` and `rp.dsp.resample` and packs the banks, native `KitCompiler` and everything only it used
retire, and every kit that compiles stays byte-identical to D1's 39-bank baseline. The same seam is what D3's renders
and user-authored sample graphs will run on, and because it rides a facet every host binds, the plugin can compile a
kit for the first time.

**Architecture:** Per [modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md), whose section 11 ("As
built") records the build and which Task 7 lands with its status line, sections 3 to 6 corrected in place and sections
8 and 9 carrying what D3 inherits. The seam is a pool on the `Engine` (its own immutable registry, workers started on
demand, 64 jobs, 1024-frame blocks) and a submission / drain / cancel trio in the emulator facet; a job's resource
table, host, plan and node instances are locals of the worker's run function, so it shares nothing with the live plan
and every resampler dies inside its host's life. The whole-buffer steps a streaming chain cannot do in one pass (a
normalising gain's peak, the dither's seed, the DMC encode) are nodes that read a bound buffer at activate on the
control thread, so every float the bytes depend on is native. TypeScript builds each pass's document and cuts and
packs the buffers between passes. Two mechanisms make "byte-identical" exact rather than a hope: a guard
a node's own state reset reports, and a non-finite flag on every returned buffer; the compiler refuses a slot on
either, and each job runs in the default floating-point environment whatever its submitter's.

**Tech stack:** C++20 with Catch2 (`retroplug-engine-graph-test`, `retroplug-graph-test`, `retroplug-audio-test`),
the graph layer of Phases A to C5, r8brain through `rp.dsp.resample`, reflect-cpp for the descriptors and the wire,
msgpack over rpcpp for the RPCs, TypeScript (QuickJS in every native host) for the compiler and its tests, and the
`test-native` TAP runners for the two kit goldens. No submodule moves, and `deps/enkiTS` leaves the build but stays a
submodule.

**Spec:** [modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md). D1's design and plan are
[modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md) and
[modular-graph-phase-d1-plan.md](modular-graph-phase-d1-plan.md).

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/d2-kit` in this worktree, on top of the design commit 1c403cd5: seven commits, one per task (Task 8 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A block that creates a file is that
file as the prototype commit left it; one that changes it is the prototype's diff for it, so `git apply` it from the
step's text (or apply it hunk by hunk with the Edit tool); one that deletes files runs `git rm`, because a deletion
diff would print whole translation units twice. The tip verification ran on `578d339b7`, whose code is identical to
the final tip `00676a33a` (only docs changed after), and its logs are `/tmp/d2pro/tip-*.log`. Every expected pass,
test count and figure is quoted from the stage reports in /tmp/d2pro or from those logs, which is what the generator
checks; a failing-test step's expected failure is what the missing code produces, as each step says. When this plan
was generated it was replayed in a scratch clone of 1c403cd5, every block written or applied in order, and each task
committed with the command given: each commit's tree came out exactly as the prototype's, and each commit message as
its. The prototype commits, by task:

- Task 1 `fced2f618`: `rp.dsp.gain` and `rp.dsp.biquad` cover any sane kit value.
- Task 2 `32992e257`: offline jobs run a graph document to completion on a host of their own.
- Task 3 `f45bc6898`: normalise, input-seeded dither and DPCM as nodes reading a bound buffer.
- Task 4 `7b2d5bdbb`: the kit compilers in TypeScript, as chains of offline graph jobs.
- Task 5 `dcd4bfd4c`: every kit compile runs as offline graph jobs, the plugin's included.
- Task 6 `2898eb671`: native `KitCompiler`, its codecs and the harness kit RPCs retire.
- Task 7 `00676a33a`: the docs. Task 8 is the acceptance run and commits nothing.

**Where this plan departs from the design as committed (1c403cd5).** The design was written before the prototype;
this plan follows what was built, and Task 7 lands the design with section 11's record and sections 3 to 6 corrected.
In short:
- **The pool is its own object with its own registry.** `OfflineJobs` is an `Engine` member made at the first
  submission and reset explicitly in `~Engine`, so a host that never submits has no registry and no thread. It runs
  clamp(hardware_concurrency, 1, 8) workers, started on demand while every started one is busy and never retired,
  with 64 jobs outstanding (queued, running or finished-undrained) and 64 inputs plus outputs per job. Its registry
  is filled once with `rp.core`, `rp.dsp` and `rp.dsp.resample` before any worker exists and never changed, so a
  script pack's swap of the engine's registry cannot race a job; a script, system or host I/O node is refused as
  `unknown-type`. A job's blocks are 1024 frames, the last partial; its transport is its rate, 120 bpm, ppq 0, not
  playing.
- **The seam's contract is exact.** Input `i` is `res:(i + 1)`, output `j` is `res:(inputs + j + 1)`, computed on
  the TypeScript side, and `returns` defaults to every output. A file input's cap carries its arithmetic,
  `float-ratio` (LSDj: `ceil(float(32576) * (float(rate) / float(11468)))`) or `double-scale` (risa:
  `ceil(double(32648) * double(rate) / palRate)`), because the two round differently at 464 of the integer rates
  1000..96000. An input of no frames is accepted and reported, but its handle names a placeholder freed at once (so
  later handles stay put) and a document binding it is refused; an output of no frames is refused. A node fault
  fails the job. A cancel completes a queued job at once and stops a running one at its next block boundary; a
  cancel that returned true always completes `cancelled`. `drainOfflineJobs` takes a wait of at most 1000 ms and
  answers the completions plus `inFlight`. Destroying the engine abandons its jobs in flight: queued ones are
  dropped unrun, running ones stop, every worker is joined, and undrained completions go with the pool.
- **The TypeScript poll shares the pump, not the wait.** `backend.pumpJobs()` and `pumpAllJobs()` drain offline
  completions beside capability jobs (the plugin's `useJobPump` needed no change), and `createAudioDriver` turns the
  poll's native wait on. A step drains without waiting while its chain is busy and blocks in native only after eight
  consecutive steps that settled nothing and saw no submission, so a chain that awaits through extra async layers
  keeps moving. A run made while 64 promises are pending is held in a local FIFO and submitted as room comes, so
  `offlineRun` never rejects busy; a promise native no longer holds rejects. There is no TypeScript cancel surface
  in D2 (the RPC exists and is tested).
- **`rp.dsp.normalize`'s peak is over the channels it plays**, not the whole buffer: `activate` folds `min(channels,
  buffer->channels)` channels, one gain scales them all, and an unplayed buffer channel is silence that never scales
  a played one. A settings commit re-folds a changed gain against the peak that activation measured; a missing
  buffer is `settings-invalid`; the node allocates nothing, so it plans on the Pico.
- **`rp.dsp.dpcm` encodes at activate into storage it owns and makes `normalize` its rebuild key.** A settings
  commit runs on the block thread, where a node may only copy, so a commit that toggles `normalize` rebuilds the
  instance and the new `activate` encodes on the control thread, rather than re-encoding a whole buffer between two
  blocks. The kernel writes one unpacked bit per byte and needs no scratch (two passes over the input); the node is
  mono, refuses a multi-channel buffer at activate, and declares `heap`.
- **The dither's effective seed is settled in `on_settings` as well as `activate`.** A plan replacement commits
  settings to the live instance without re-activating it, so a hash computed only in `activate` silently ignored a
  `seed` change; the existing `[dsp-nodes]` case caught it. `seedFrom`, when set, hashes the named buffer's first
  channel with `rp::dsp::inputSeed` and replaces the setting, so a committed `seed` does not change the noise.
- **A guard report is a report, not a failure.** `rp.host.guards/1` (`rp_node.h`) is a host service a node reports
  into when one of its guards replaces what its kernel would have computed: `fired(ctx, reason)`, called on the
  block thread, never blocking or allocating, the reason a string literal the reporting instance keeps alive.
  `rp.dsp.biquad` reports its reset-and-silence and `rp.dsp.dither` its channel restart. A host offering none
  answers null (the live engine does, and `Engine::graphService` is pinned to answer null for it). Each offline
  job's host offers it and its completion carries `guardsFired` and the first `guardReason` on an ok result; a job a
  guard stepped into still completes `ok`, and the caller decides.
- **Every returned offline buffer carries `nonFinite`**, read natively as the buffer is copied out. A non-finite
  value no guard touches (a normalising gain over a subnormal peak, a plain gain over samples near `FLT_MAX`) is in
  the recording, and this flag is how a caller sees it without scanning; a TypeScript scan put the 15-slot worst
  case at 467 ms against 83 ms with native's flag. The kit compiler refuses a slot on a non-zero guard count or a
  flagged recording, which is how section 2's byte-identity promise holds for intermediate values.
- **Every job runs in the default floating-point environment.** A scoped `DefaultFloatEnvironment` holds it from
  decode to copy-out and restores the caller's after: round to nearest, no trapping, and the flush bits cleared
  explicitly (x86 MXCSR FTZ and DAZ, AArch64 FPCR FZ and FZ16), because a worker inherits its submitter's mode and
  audio hosts commonly flush.
- **The compiler's result is `{bytes, errors}`, and every empty or short slot has a reason.** `errors[i]` is null
  exactly when the bank holds the sample whole; the reasons include an empty window, a window that resamples to
  under one wave frame, a sample the bank clipped or could not fit, and a sample past the slot count. Both entry
  points take a runner first. A refused risa slot is empty (its entry `0xFF`) where its empty window alone would
  have placed `{0}`, and a refused LSDj slot keeps its name at length 0; a refusal beats an empty window. Missing
  spec fields take the native structs' defaults, which is a real departure from the old RPC: its decode had no
  defaults, so any effect field left out threw for the whole kit. Samples past the bank's slot count are not
  compiled and say so.
- **`kit-edges.json` records the banks past the baseline's matrix.** The baseline is a coarse oracle: it sees 4-bit
  codes, so it can pass a one-ulp change, and the 12 edge banks, the `[dsp-buffer-nodes]` cases and the
  `[dsp][dpcm]` reference are the wider net. The edge golden is 12 banks as `{hash, size}` (no slot: `kit-compile`
  refuses slots by design), recorded once from the compiler on `linux-x64-gnu13` in a run where every bank also
  equalled native's, and held under the baseline's rules: size everywhere, hash on the target, a second compile
  agreeing elsewhere, `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turning the fallback into a failure and
  `RP_KIT_GOLDEN_UPDATE=1` refused off the target and for a disagreeing bank. `kit-baseline.json` and parity are
  never re-recorded by any task here.
- **The CLI's sessions are async.** `audioDriver.compileKit` / `compileDmc` return `Promise<KitCompileResult>` and
  run `src/kit` over `realBackend`'s one offline queue; `runSession` and `CliTool.run` accept a promise and exit on
  it; the per-slot reason reaches the read-back check, and a clipped slot with bytes warns. `createAudioDriver`
  turns the poll's native wait on, which is what settles a compile in a host with no UI tick; the plugin settles it
  from the frame tick. This exposed a latent bug: the CLI's `--filter` built a filter with no `gain`, so the old RPC
  threw `Field named 'gain' not found` for every such kit and the flag never worked; the TypeScript compiler fills
  the 0 dB default and the flag now compiles.
- **`SampleCache`'s staleness is gone.** The old RPC kept one cache for the engine's life, keyed by path and never
  invalidated, so a path rewritten in one session compiled the old audio; each job decodes through a cache of its
  own, so a compile sees the file as it is now. The behavioural cost: two by-hand comparisons must not rewrite a
  source path within one session.
- **The deletions go beyond the design's list.** `SampleUtil.hpp` goes with `KitUtil` (its helpers had no caller
  left), `LsdjEffect` (the rfl tagged union) goes with the DTOs (it existed to decode a `compileKit` effect list),
  and `KitCodec.hpp` goes with the compiler. What stays is what a node, the seam or a test runs: `src/dsp/` whole,
  `SampleCache`, `lsdj/Effects.cpp` (compiled into both test binaries as the kit path's reference) and a new slim
  `lsdj/KitBank.hpp` for the bank's numbers. `retroplug-core` links neither DSP library any more. enkiTS leaves the
  build entirely (the `add_subdirectory`, the warning suppression and the third-party credit) because no shipped
  binary can contain it now; the submodule stays for a later cleanup. The harness facet drops to 20 methods and the
  union to 90.
- **The cost is buffers crossing the RPC, not host setup.** Release build, this box (32 threads, so 8 workers), 8
  runs each:


| measurement | native `compileKit` (RPC) | TypeScript `compileKit` |
|---|---|---|
| 15 slots x 1 s at 44.1 kHz, three effects | best 1.5-1.6 ms, median 1.7-1.8 ms | best 22.0 ms, median 22.7-23.1 ms |
| 15 slots x 8192 frames, no effects | best 0.4-0.5 ms, median 0.5 ms | best 4.1-4.2 ms, median 4.2 ms |
| the 1 s kit, slot by slot (serial) | 10.5-11.1 ms | 49-51 ms |

Per job (serial round-trip means) for the 1 s kit: load 0.29-0.34, effects 0.47-0.49, the normalise pass 0.24-0.26,
resample 0.70-0.71, quantise 0.31-0.38 ms. A trivial job is 0.011-0.017 ms to 0.03 ms, about 2 ms across a kit's 75
jobs, so per-job host setup is not the cost; an echo job (buffer in, played, recorded, returned) is 0.07 ms at 8192
frames, 0.18 at 44 100 and 1.1 at 176 400, about 0.15 ms per 2 x 176 KB on the one JS thread's msgpack and copies,
about 1 MB per 1 s slot and 15 MB per kit. The worst case the switch-over survives is 15 slots of 4 s at 96 kHz, each
capped at 272 700 frames, with a filter, a normalise and a dither: native 4.9 ms median (4.8 best), TypeScript 83.2 ms
(77.9 best), where an early TypeScript-side scan for non-finite samples had made that 467 ms. About 80 ms of the UI
thread is the cost of a worst-case kit compile if a future row ever runs one.

- **The plugin's kit compile is proved on the plugin's own path.** `test-ui/kit-compile.test.ts` runs on the UI test
  host, whose registration is exactly the plugin's, proves a microtask-only stretch settles nothing (so the native
  wait is off), then settles the whole compile from `ui.pump` + `backend.pumpJobs` alone and reads the slot back
  through `KitView`. The compiler reached the CLI bundle and the render worker's session path, not the UI bundle.
- **Two things no log backs and this plan says so.** The C++ seam test no longer holds the compiler's side of an
  empty window (the TypeScript `kit-compile` EMP slots and the edges golden do), and the Windows build has not seen
  this tree: stage 6 also removed transitive includes, so the residual "confirm the removed transitive includes
  build under MSVC" sits in [07-remaining-work.md](../spec/07-remaining-work.md), not as a claim here.
- **Found while building, and left alone.** The live engine's first periodic state snapshot allocates (about block
  46 of a booting Game Boy, two allocations, with or without offline jobs): it is the 0.5 s state publish
  [07-remaining-work.md](../spec/07-remaining-work.md) already lists, seen on a Game Boy's `readStateSnapshot`
  rather than Mesen's serializer, pre-existing, and C4's no-allocation claim excludes that publish. No parity
  baseline and no kit golden was re-recorded by any task: only the `rp.dsp` descriptors (Tasks 1 and 3) and the new
  `kit-edges.json` (Task 4) are recorded, each proved against the prototype's bytes.


**Ground rules for whoever executes this:**
- This plan lives at `docs/modular-graph-phase-d2-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's links are relative to that location; every command and path in the steps is relative to the worktree
  root.
- Commit after each task (the commands are given; Task 8 commits nothing unless a step fails and is fixed, in a commit
  of its own). No `Co-Authored-By` or "generated with" trailer. Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with uncommitted
  work in the tree. Never touch a submodule. `deps/sameboy` shows as modified after any configure: that is the tracked
  audio patch, expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- **Parity holds the engine's output to the bit, and no task re-records it.** D2 changes nothing on the emulated audio
  path, so a strict parity failure is a real change: stop and find it. Never set `RP_PARITY_UPDATE`.
- **Three things are recorded in this plan, each in exactly one step, and no later task touches them**: Tasks 1 and
  3's `rp.dsp` descriptor goldens (recorded with `RP_GRAPH_GOLDEN_UPDATE=1` on the descriptor case) and Task 4's new
  `kit-edges.json` (recorded with `RP_KIT_GOLDEN_UPDATE=1` by the `test-native` `kit-compile` file, on the target,
  for that new file only). Each is quoted whole below, small enough to read and to apply, and each step then proves
  the recorder lands the same bytes with an empty `git diff --cached --stat` against the prototype commit.
  `kit-baseline.json` is never re-recorded: it is the oracle.
- Every whole-file block below (a `Create` or a `Replace`) is followed by the `sed -n` range that extracts it from
  this plan and by the blob hash the file must have: write or extract the file, then `git hash-object <path>` must
  print the hash the step quotes, which is the prototype commit's blob. A retyped slip fails there rather than in a
  test that never reads the line.
- **The kit goldens' exact bytes exist on `linux-x64-gnu13` only**, so this plan is executed on that host: recording
  refuses any other, and CI's linux-x64 job is the other place they are compared. On a foreign host Task 4's recording
  step refuses and the plan stops there; do not force it.
- Every `check:docs` count below is 75 from Task 1 on: the reports and the tip log record 74, and this plan is
  the 75th tracked Markdown file `check-doc-links` walks.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; without it their
  cases skip. A shell `export` does not survive between commands. `pnpm test:native` takes ONE filter per run (an
  exact slug like `parity/lsdj-modes`, or a dash-form prefix).
- The Catch2 binaries are `EXCLUDE_FROM_ALL`: `cmake --build build` does not relink them, so a library edit needs the
  test target built by name before a run. A new or removed test source needs `cmake -S . -B build` first.
- The sanitizer trees are their own: `build-tsan/` and `build-asan/`. Configure one only if its `CMakeCache.txt` is
  missing (`cmake -S . -B build-tsan -DRETROPLUG_SANITIZE=thread`, or `build-asan` with `=address`). Build the binary in
  that tree before running it there; a stale prototype binary proves nothing.
- Case counts are exact. A `test cases: N | P passed | S skipped` line, an `All tests passed (A assertions in N test
  cases)` line and a per-file count must match the quoted N/P/S. Assertion totals are exact too, with one exception: a
  run that includes `[capability]`, and so any whole engine-graph binary run, may report 2 or 4 more assertions than
  quoted, from the device-thread progress case (a drain that lands between the final progress store and `Done`
  legitimately yields one more entry, 2 assertions; two more entries make 4). `[offline]`, `[dsp]`,
  `[dsp-buffer-nodes]`, `[dsp-nodes]` and `[dither]` carry no such case, so their totals are exact.
- **The prototype branch `proto/d2-kit` is this plan's provenance, and three steps read it: Task 1's golden step,
  Task 3's golden step and Task 4's golden step compare a recorded golden with the prototype commit's bytes. Keep the
  branch until Task 4 is committed; after that no step reads it.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks one
  at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper; pgrep -a jackd`).
- The Windows build has not seen this tree (this plan's prototype is local and Linux-only), so an MSVC-only warning
  is a real finding to report, not a stale quote; no step below builds on Windows.

**Commit this plan before Task 1.** Before the commit, check where the tree is: `git rev-parse --abbrev-ref HEAD` must
print `feature/modularize`, `git rev-parse --short=8 HEAD` must print `1c403cd5`, and `git status --short` must show
only ` m deps/sameboy`. If any of the three differs, stop: every step below assumes the design commit on a clean tree. The
run below copies the plan to its home in the repository and commits it on its own, so every later task's `git status`
and `git add` see a clean tree:

````bash
cp /tmp/d2plan/plan.md docs/modular-graph-phase-d2-plan.md
git add -- docs/modular-graph-phase-d2-plan.md
git commit -F - <<'EOF'
docs: the D2 implementation plan

The plan for Phase D2, kit compiles as offline graph jobs, generated from the prototype's seven commits on
proto/d2-kit: the widened rp.dsp ranges, the offline job seam, the three whole-buffer nodes, the TypeScript kit
compiler and its baseline runner, the switch-over, native's retirement and the docs. Every expected count is quoted
from a stage report or the tip verification, and the code was replayed commit by commit against the prototype's
trees.
EOF
````

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/host/engine/OfflineJobs.{hpp,cpp}` | the pool, the request/completion types and `offlineWindow()` (Task 2); the guard report and the floating-point environment (Task 4) |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `submitOfflineJob`, `drainOfflineJobs(wait)`, `cancelOfflineJob`, `offlineJobsOutstanding`, the lazily made pool and `graphService` (Tasks 2 and 4) |
| `packages/native/src/host/rpc/` | the three RPCs in the emulator facet, the wire types, `guardsFired` / `guardReason` / `nonFinite` (Tasks 2 and 4) |
| `packages/native/src/graph/nodes/DspNodes.{hpp,cpp}` | the widened ranges (Task 1), `rp.dsp.normalize`, `rp.dsp.dpcm`, dither's `seedFrom` (Task 3), the guard reports (Task 4) |
| `packages/native/src/graph/rp_node.h` | `rp.host.guards/1` (Task 4) |
| `packages/native/src/dsp/Dpcm.{hpp,cpp}` | the DMC encode kernel (Task 3) |
| `packages/native/src/lsdj/KitBank.hpp` | the LSDj bank's numbers the seam and the tests still name (Task 6) |
| `packages/native/src/kit/KitCompiler.{hpp,cpp}`, `KitCodec.hpp`, `src/lsdj/KitUtil.{hpp,cpp}`, `LsdjKitCodec.{hpp,cpp}`, `SampleUtil.hpp`, `src/risa/RisaDmcCodec.{hpp,cpp}` | the retired compiler and its codecs (deleted by Task 6; `SampleCache` stays) |
| `packages/native/cmake/`, `CMakeLists.txt` | the pool's source, the new tests, `Effects.cpp` in the two test binaries, enkiTS out (Tasks 2, 3 and 6) |
| `packages/native/test/engine-graph/OfflineJobs.test.cpp` | `[offline]` (Tasks 2, 3 and 4; comments in Task 6) |
| `packages/native/test/graph/DspNodes.test.cpp`, `DspBufferNodes.test.cpp`, `Dsp.test.cpp` | `[dsp-nodes]` (Tasks 1 and 3), `[dsp-buffer-nodes]` and `[dsp][dpcm]` (Task 3) |
| `packages/native/test/graph/golden/` | the five `rp.dsp` descriptor goldens (Tasks 1 and 3) |
| `packages/native/test/audio/golden/kit-baseline.json`, `kit-edges.json` | the oracle (untouched) and the edge banks (Task 4) |
| `packages/retroplug/src/offline.ts`, `capability.ts`, `realBackend.ts`, `backend.ts`, `audioDriver.ts` | the promise side, the shared pump and the single offline queue (Tasks 2 and 4), the switch-over (Task 5) |
| `packages/retroplug/src/kit/{pack,chain,compile,index}.ts` | the compiler: the integer half, the chain documents, the two entry points (Task 4) |
| `packages/retroplug/src/lsdj/rom/`, `src/risa/rom/kit.ts` | the assemblers' native byte rules (Task 4) and the async callers' comments (Tasks 5 and 6) |
| `packages/retroplug/test-native/` | `offline-run` (Task 2), `kit-baseline`, `kit-compile` and `kit/sources.ts` (Tasks 4 to 6) |
| `packages/retroplug/test/offline/`, `test/kit/` | the mock and scripted-port cases (Tasks 2 and 4) |
| `packages/retroplug/test-ui/kit-compile.test.ts` | the plugin's kit compile (Task 5) |
| `packages/retroplug/cli/` | the async sessions and `--filter` (Task 5) |
| `docs/`, `spec/`, `AGENTS.md` | the design's as-built record and the live docs (Task 7) |

---
### Task 1: the `rp.dsp` ranges cover any sane kit value

Design sections 1, 4 and 7's first commit. The planner clamps a document's param silently and cannot see the plan's
rate, so D1's ranges were narrower than a kit spec's and a kit compile could not get its plain gain, its high-pass at Q
20 or its 30 dB shelf to the kernel. The gain becomes linear -100..100 and the biquad 1..96 000 Hz, Q 0.001..1000 and
-96..96 dB. The planner and `setParam` still clamp a document's value, a wire is still unclamped, and the biquad
clamps nothing itself, Nyquist included: the compiler Task 4 lands does the refusal, relative to the rate the source
decoded at. The two descriptor goldens re-record once; nothing on the kit path moves, and the kit baseline is
unchanged.

**Files:**

- Modify: `packages/native/src/graph/nodes/DspNodes.cpp`, `packages/native/test/audio/KitBaseline.test.cpp`, `packages/native/test/graph/DspNodes.test.cpp`, `packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`, `packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`

- [ ] **Step 1: Write the failing cases**

`DspNodes.test.cpp` (tag `[dsp-nodes]`) gains the two cases the widened ranges need. The gain case drives -1.0 and 7.4
(the baseline's values outside D1's 0..4) through a document's params and holds the node to its kernel bit for bit
over every split, then asks for 150 and -150 and requires the kernel at 100 and -100; the biquad case drives a 10 Hz
high-pass at Q 20 and a 3000 Hz high shelf at Q 0.7 and +30 dB, then asks beyond each new bound (frequency 0.5, Q
0.0005 and 2000, gain -120 and 120) and requires the kernel at the bound. Every clamp case also runs the kernel at the
asked value and requires `firstDifference(asked, bound) != -1`, so it proves the clamp rather than a coincidence at
the bound. The file header gains a bullet saying what the ranges cover.

````diff
diff --git a/packages/native/test/graph/DspNodes.test.cpp b/packages/native/test/graph/DspNodes.test.cpp
index 8ea4ad949..b6a6e497b 100644
--- a/packages/native/test/graph/DspNodes.test.cpp
+++ b/packages/native/test/graph/DspNodes.test.cpp
@@ -4,6 +4,8 @@
 // - Each node's output through a plan equals the kernel run directly over the same signal, bit for bit, and any split
 //   of the stream into blocks (1, 7, 64, 1024 frames and a ragged mix) gives the same stream: a node carries its
 //   kernel's state across blocks exactly as the kernel does.
+// - The params' ranges carry any sane kit value (a phase-flip or large plain gain, a 10 Hz high-pass at Q 20, a
+//   +30 dB shelf), and a value outside a range is still clamped to its bound, silently.
 // - A gain that moved ramps across one block and lands on the new value; a held one is the kernel's exact multiply,
 //   from the plan's first block on. The biquad's coefficients follow a sweep at the next block while its state, which
 //   is what a reset would throw away, is kept: a change of type recomputes them and clears the state, which is a fresh
@@ -221,6 +223,44 @@ TEST_CASE("rp.dsp.gain ramps a moved value across one block and multiplies a hel
     CHECK(late[63] == 1.0f);
 }
 
+TEST_CASE("rp.dsp.gain carries a kit's gain whole, and still clamps at its bounds", "[dsp-nodes]") {
+    const std::vector<float> in = signal(kFrames);
+    // A phase flip and a large plain gain: both are legal kit values, and both were outside the range D1 shipped, where
+    // the planner clamped them silently. Neither is the normalising gain rp.dsp.normalize computes from its buffer.
+    for (const float gain : {-1.0f, 7.4f}) {
+        INFO("gain " << gain);
+        rp::dsp::Gain kernel;
+        kernel.init(gain);
+        std::vector<float> reference(kFrames);
+        kernel.process(in.data(), reference.data(), kFrames);
+
+        const std::string params = R"({"channels":1},"params":{"gain":)" + std::to_string(gain) + "}";
+        for (const auto& split : kSplits) {
+            INFO("split starting " << split[0]);
+            DspRig     rig;
+            const auto sample = rig.buffer(in, 1);
+            REQUIRE(rig.play(sample, "rp.dsp.gain", params));
+            CHECK(firstDifference(reference, rig.render(kFrames, split)[0]) == -1);
+        }
+    }
+
+    // A document's value outside the range is clamped to the bound (the planner does it, silently), so the node gives
+    // the kernel's stream at that bound rather than the value asked for: the clamp the wider range must keep.
+    for (const auto& [asked, bound] : {std::pair{150.0f, 100.0f}, std::pair{-150.0f, -100.0f}}) {
+        INFO("gain " << asked << " clamped to " << bound);
+        rp::dsp::Gain kernel;
+        kernel.init(bound);
+        std::vector<float> reference(kFrames);
+        kernel.process(in.data(), reference.data(), kFrames);
+
+        DspRig     rig;
+        const auto sample = rig.buffer(in, 1);
+        REQUIRE(rig.play(sample, "rp.dsp.gain",
+                         R"({"channels":1},"params":{"gain":)" + std::to_string(asked) + "}"));
+        CHECK(firstDifference(reference, rig.render(kFrames, {4096})[0]) == -1);
+    }
+}
+
 TEST_CASE("rp.dsp.biquad through a plan is its kernel, whole or split any way", "[dsp-nodes]") {
     const std::vector<float>      in = signal(kFrames);
     const rp::dsp::BiquadSettings settings{rp::dsp::BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f};
@@ -251,6 +291,77 @@ TEST_CASE("rp.dsp.biquad through a plan is its kernel, whole or split any way",
     }
 }
 
+TEST_CASE("rp.dsp.biquad carries a kit's filter whole, and still clamps at its bounds", "[dsp-nodes]") {
+    const std::vector<float> in = signal(kFrames);
+    // A 10 Hz high-pass at Q 20 and a 3 kHz high shelf at +30 dB: legal kit specs outside the ranges D1 shipped, where
+    // the planner clamped them silently.
+    struct Spec {
+        const char*         type;
+        rp::dsp::BiquadType kernelType;
+        float               frequency, q, gainDb;
+    };
+    for (const Spec& spec : {Spec{"high-pass", rp::dsp::BiquadType::HighPass, 10.0f, 20.0f, 0.0f},
+                             Spec{"high-shelf", rp::dsp::BiquadType::HighShelf, 3000.0f, 0.7f, 30.0f}}) {
+        INFO(spec.type << " at " << spec.frequency << " Hz, Q " << spec.q << ", " << spec.gainDb << " dB");
+        rp::dsp::Biquad kernel;
+        kernel.init({spec.kernelType, 48000.0f, spec.frequency, spec.q, spec.gainDb});
+        std::vector<float> reference(kFrames);
+        kernel.process(in.data(), reference.data(), kFrames);
+
+        const std::string settings = R"({"channels":2,"type":")" + std::string(spec.type) +
+                                     R"("},"params":{"frequency":)" + std::to_string(spec.frequency) +
+                                     R"(,"q":)" + std::to_string(spec.q) +
+                                     R"(,"gainDb":)" + std::to_string(spec.gainDb) + "}";
+        for (const auto& split : kSplits) {
+            INFO("split starting " << split[0]);
+            DspRig     rig;
+            const auto sample = rig.buffer(in, 2);
+            REQUIRE(rig.play(sample, "rp.dsp.biquad", settings, 2));
+            const auto out = rig.render(kFrames, split);
+            CHECK(firstDifference(reference, out[0]) == -1);
+            CHECK(firstDifference(reference, out[1]) == -1);
+        }
+    }
+
+    // A document's value outside a range is clamped to the bound (the planner does it, silently), so each param asked
+    // beyond its range gives the kernel's stream at the bound: the clamp the wider ranges must keep. Every bound is
+    // under the 48 kHz plan's Nyquist, where the node and the kernel agree (the guard never fires). Each case also runs
+    // the kernel at the value asked, so it fails unless the bound and the asked value really do give different streams.
+    struct Clamped {
+        float frequency, q, gainDb;
+        float boundFrequency, boundQ, boundGainDb;
+    };
+    for (const Clamped& c : {Clamped{0.5f, 1.0f, 12.0f, 1.0f, 1.0f, 12.0f},
+                             Clamped{1000.0f, 0.0005f, 12.0f, 1000.0f, 0.001f, 12.0f},
+                             Clamped{1000.0f, 2000.0f, 12.0f, 1000.0f, 1000.0f, 12.0f},
+                             Clamped{1000.0f, 1.0f, 120.0f, 1000.0f, 1.0f, 96.0f},
+                             Clamped{1000.0f, 1.0f, -120.0f, 1000.0f, 1.0f, -96.0f}}) {
+        INFO("high-shelf clamped to " << c.boundFrequency << " Hz, Q " << c.boundQ << ", " << c.boundGainDb
+                                      << " dB");
+        rp::dsp::Biquad askedKernel;
+        askedKernel.init({rp::dsp::BiquadType::HighShelf, 48000.0f, c.frequency, c.q, c.gainDb});
+        std::vector<float> asked(kFrames);
+        askedKernel.process(in.data(), asked.data(), kFrames);
+
+        rp::dsp::Biquad kernel;
+        kernel.init({rp::dsp::BiquadType::HighShelf, 48000.0f, c.boundFrequency, c.boundQ, c.boundGainDb});
+        std::vector<float> reference(kFrames);
+        kernel.process(in.data(), reference.data(), kFrames);
+
+        // The case would fail without the clamp: the asked value's own stream differs from the bound's.
+        CHECK(firstDifference(asked, reference) != -1);
+
+        DspRig     rig;
+        const auto sample = rig.buffer(in, 1);
+        REQUIRE(rig.play(sample, "rp.dsp.biquad",
+                         R"({"channels":1,"type":"high-shelf"},"params":{"frequency":)" +
+                             std::to_string(c.frequency) +
+                             R"(,"q":)" + std::to_string(c.q) +
+                             R"(,"gainDb":)" + std::to_string(c.gainDb) + "}"));
+        CHECK(firstDifference(reference, rig.render(kFrames, {4096})[0]) == -1);
+    }
+}
+
 TEST_CASE("rp.dsp state is per channel, and its memory is sized from the settings' channels", "[dsp-nodes]") {
     // Six channels of different signal through one biquad and one dither. Each channel's output is its own kernel's,
     // so neither node keeps one state for all of them, and extra memory sized for fewer channels than the settings
````

Run: `sed -n '311,446p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: the two new cases fail against the old descriptors and the old clamping. The report's first pass, before the review added the per-case clamp checks, recorded `test cases: 18 | 16 passed | 2 failed` and `assertions: 35186 | 35145 passed | 41 failed`; this tree's two cases carry four clamp checks each, so report its own summary line.

- [ ] **Step 2: Widen the ranges and record the two descriptor goldens**

`DspNodes.cpp` moves the two types' ranges (the gain to -100..100, default 1; the biquad to 1..96 000 Hz, Q
0.001..1000 and -96..96 dB, defaults unchanged) and rewrites the paragraphs above them to state what is: the ranges
cover any sane kit value; the planner and the host clamp a document's or `setParam`'s value silently and a wired value
is not clamped; the biquad clamps nothing itself, Nyquist included, so its runaway is reachable at every plan rate
under 192 kHz (most of the range is above Nyquist at 48 kHz and nearly all of it at the standalone's 22 050 and 32 000
Hz); and a kit compile refuses a spec outside the ranges and a frequency at or above its source rate's Nyquist itself,
because the planner cannot see the plan's rate. The dither paragraph stops saying D2 has to decide for both guards: a
kit source can carry the regime, no baseline case does, and a kit compile refuses a source with a non-finite sample.
`KitBaseline.test.cpp` changes in comments only, wherever a comment stated one of D1's ranges as a fact; no case,
signal or golden moves.

````diff
diff --git a/packages/native/src/graph/nodes/DspNodes.cpp b/packages/native/src/graph/nodes/DspNodes.cpp
index de8914812..6c4c513cf 100644
--- a/packages/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/native/src/graph/nodes/DspNodes.cpp
@@ -121,8 +121,11 @@ struct Gain {
     struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
     struct { rp::audio_out<rp::match<"in">> out; } outputs;
     // A linear multiply, because that is what the kernel is and what the kit path's bytes depend on: a level in dB
-    // would need a conversion the kit path does not do, and a held value would no longer be the exact multiply.
-    struct { rp::float_param<"Gain", rp::range{0.0, 4.0, 1.0}> gain; } params;
+    // would need a conversion the kit path does not do, and a held value would no longer be the exact multiply. The
+    // range covers any sane kit spec's plain gain, a phase flip or a boost or cut; a normalising gain is
+    // rp.dsp.normalize's, computed from the buffer at activate and never this param's. A kit compile refuses a plain
+    // gain outside the range rather than letting the planner clamp it silently.
+    struct { rp::float_param<"Gain", rp::range{-100.0, 100.0, 1.0}> gain; } params;
 
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a gain"); }
 
@@ -172,21 +175,22 @@ struct Gain {
 // instance or a fresh activation starts from cleared state (what `init` does). The kernel's output clamp is part of its
 // arithmetic, so the node's output is clamped to [-1, 1] as the kit path's is.
 //
-// A frequency or Q beyond the project's range is not refused: the params' ranges are the node's, not the kernel's,
-// and they are narrower than a kit spec can be (20 Hz to 20 kHz, Q 0.1 to 10, -24 to 24 dB, where a kit's
-// `FilterEffect` is four plain floats). A document's or `setParam`'s value outside one is clamped silently by the
-// planner and the host, and a wired value is not clamped at all, so the kernel computes the coefficients the RBJ
-// formulas give for whatever it reads. The node clamps nothing of its own, a frequency above Nyquist included, because
-// the kit path hands the spec's frequency straight to the kernel and a clamp here would be a different byte for the
-// same kit. A kit compile therefore cannot send its spec through these params as they are: D2's design has to widen
-// them, carry the spec in settings or wire it (the D1 design, section 8).
+// A frequency or Q beyond the node's range is not refused: the params' ranges are the node's, not the kernel's. A
+// document's or `setParam`'s value outside one is clamped silently by the planner and the host, and a wired value is
+// not clamped at all, so the kernel computes the coefficients the RBJ formulas give for whatever it reads. The node
+// clamps nothing of its own, a frequency above Nyquist included, because the kit path hands the spec's frequency
+// straight to the kernel and a clamp here would be a different byte for the same kit. So the ranges cover any sane kit
+// value (1 Hz to 96 kHz, Q 0.001 to 1000, -96 to 96 dB, where a kit's `FilterEffect` is four plain floats), and a kit
+// compile refuses a spec outside them itself rather than letting the planner clamp it. It refuses a frequency at or
+// above its source rate's Nyquist too, which the planner cannot see: it knows neither the plan's rate nor the source's.
 //
 // Above Nyquist the RBJ formulas put the poles outside the unit circle, so the kernel's unclamped feedback grows until
 // the state is infinite and then NaN, and the clamp writes a NaN through (`std::clamp` of a NaN is the NaN). That is
-// reachable at the 22050 and 32000 Hz rates the standalone offers, where the frequency knob's top half is above
-// Nyquist, and nothing between the node and the host output rejects a non-finite sample. The growth is slow enough to
-// be heard: about 700 samples from a cleared state at 12 kHz on a 22.05 kHz plan, and the kernel's clamp writes
-// rail-to-rail output in every block until the guard fires, so a held above-Nyquist setting is full-scale bursts with
+// reachable at every plan rate under 192 kHz, from a document or `setParam` as well as a wire (at 48 kHz most of the
+// range is above Nyquist, at the standalone's 22050 and 32000 Hz nearly all of it), and nothing between the node and
+// the host output rejects a non-finite sample. The growth is slow enough to be heard: about 700 samples from a cleared
+// state at 12 kHz on a 22.05 kHz plan, and the kernel's clamp writes rail-to-rail output in every block until the
+// guard fires, so a held above-Nyquist setting is full-scale bursts with the silent blocks the guard writes between
 // the silent blocks the guard writes between them rather than silence. So the node guards the state and leaves the
 // coefficients alone: a channel whose filter came out of a block with any of its four state values not finite has that
 // filter reset and its block written as silence, so a sweep past Nyquist cannot poison the stream, and a frequency that
@@ -196,8 +200,9 @@ struct Gain {
 // frequency is above its source rate's Nyquist produces constant codes today (the state goes NaN, and the quantiser
 // reads every NaN as code 0), where this node recovers; and any non-finite input sample, which a decoded 32-bit float
 // WAV can carry (nothing between the sample cache and the kit compiler checks), latches the kernel for good, where this
-// node resets. No case in the kit baseline is in either regime, so none of its bytes moved; D2, whose contract is byte
-// identity, has to decide between the kernel call and this node for both.
+// node resets. No case in the kit baseline is in either regime, so none of its bytes moved. A kit compile refuses both
+// rather than choosing between the kernel call and this node: a source with a non-finite sample and a filter at or
+// above its source rate's Nyquist are errors, so it never asks this node to match the kernel there.
 struct Biquad {
     static constexpr const char* id = "rp.dsp.biquad";
     static constexpr const char* name = "Biquad";
@@ -207,9 +212,9 @@ struct Biquad {
     struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
     struct { rp::audio_out<rp::match<"in">> out; } outputs;
     struct {
-        rp::float_param<"Frequency", rp::range{20.0, 20000.0, 1000.0}, "Hz"> frequency;
-        rp::float_param<"Q", rp::range{0.1, 10.0, 1.0}>                       q;
-        rp::float_param<"Gain", rp::range{-24.0, 24.0, 0.0}, "dB">            gainDb;  // Peak and the shelves only
+        rp::float_param<"Frequency", rp::range{1.0, 96000.0, 1000.0}, "Hz"> frequency;
+        rp::float_param<"Q", rp::range{0.001, 1000.0, 1.0}>                  q;
+        rp::float_param<"Gain", rp::range{-96.0, 96.0, 0.0}, "dB">           gainDb;  // Peak and the shelves only
     } params;
 
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a biquad"); }
@@ -310,10 +315,11 @@ struct Biquad {
 // block rather than the stream. The block itself is left as the kernel quantised it, which is finite and at worst full
 // scale, so only the state is restarted and the click is one block's.
 //
-// That restart is a second place the node differs from the kernel run directly, and a kit can reach it: nothing
-// between `SampleCache` and `KitCompiler` rejects a non-finite sample, so a 32-bit float WAV carrying a NaN or an
-// infinity latches the kernel for the rest of the slot where this node recovers at the next block. No baseline case
-// carries one, so no kit byte moved; D2 has to decide for both this and the biquad's guard.
+// That restart is a second place the node differs from the kernel run directly, and a kit source can carry the regime:
+// nothing between `SampleCache` and the kit compile checks a sample for finiteness, so a 32-bit float WAV carrying a
+// NaN or an infinity latches the kernel for the rest of the slot where this node recovers at the next block. No
+// baseline case carries one, so no kit byte moved; a kit compile refuses a source with a non-finite sample, so it
+// never asks this node to match the kernel there.
 struct Dither {
     static constexpr const char* id = "rp.dsp.dither";
     static constexpr const char* name = "Dither";
diff --git a/packages/native/test/audio/KitBaseline.test.cpp b/packages/native/test/audio/KitBaseline.test.cpp
index 1eae3cc65..4604f8f86 100644
--- a/packages/native/test/audio/KitBaseline.test.cpp
+++ b/packages/native/test/audio/KitBaseline.test.cpp
@@ -10,9 +10,9 @@
 // The matrix is fixed. Sources: two sine frequencies, a quiet sine, a chirp, seeded noise (`std::mt19937`, a fixed
 // seed) and silence, each at 22.05, 44.1 and 48 kHz, plus a 600-frame source shorter than one resampler chunk. LSDj
 // cases: no effect, a non-unity gain with normalise off, the same gain with normalise on, that normalise over a
-// silent source, a negative gain, a normalise of the quiet source (whose `gain / peak` lands far above the gain
-// node's range) at every rate and through a dither, each of the eight filter types, two filter specs outside the
-// biquad node's ranges, each of the five dither algorithms, rotate on and off, one chain of filter then gain then
+// silent source, a negative gain, a normalise of the quiet source (whose `gain / peak` lands far above D1's gain
+// node range) at every rate and through a dither, each of the eight filter types, two filter specs outside D1's
+// biquad node ranges, each of the five dither algorithms, rotate on and off, one chain of filter then gain then
 // dither, the short source on its own, and one bank carrying a second slot beside the first. risa cases: the DMC codec
 // at five rate indices, with and without a pre-effect, with normalise off and with loop set, a normalise of a silent
 // source, the short source on its own, and one bank of two slots. One of the two-slot cases
@@ -168,7 +168,7 @@ std::vector<float> signalSamples(Signal signal, std::uint32_t rate) {
     case Signal::Sine1200: return sine(1200.0, rate, frames);
     // A 440 Hz sine at 0.8 scaled down by 16 (an exact power of two, so the samples are the sine's times 0.0625): a
     // peak of about 0.05, roughly 26 dB below full scale. A kit normalising it to 0.37 asks for a gain near 7.4, well
-    // past `rp.dsp.gain`'s 0 .. 4, which is what a quiet sample in an ordinary kit asks for.
+    // past D1's `rp.dsp.gain` range of 0 .. 4, which is what a quiet sample in an ordinary kit asks for.
     case Signal::Quiet: {
         std::vector<float> out = sine(440.0, rate, frames);
         for (float& v : out) v *= 0.0625f;
@@ -284,15 +284,15 @@ std::vector<KitCase> kitCases() {
         // A silent source is the only input that reaches `GainEffect`'s "maxAmp > 0" guard, the one division in the
         // effect: a non-silent source never takes that branch.
         {"lsdj/normalize-silence", KitCodec::Lsdj, Signal::Silence, 44100, {gainEffect(0.37f, true)}},
-        // The quiet source normalised: `gain / peak` is about 7.4, far above `rp.dsp.gain`'s 0 .. 4, so a graph node
-        // given the spec's gain would clamp it silently and compile a different bank. All three rates pin it through a
-        // different resampler pair, and the middle case dithers the result, so the over-range multiply reaches the
-        // seed and the quantiser as well as the multiply.
+        // The quiet source normalised: `gain / peak` is about 7.4, far above D1's `rp.dsp.gain` range of 0 .. 4, so
+        // D1's graph node given the spec's gain would have clamped it silently and compiled a different bank; now the
+        // value reaches the node whole. All three rates pin it through a different resampler pair, and the middle case
+        // dithers the result, so the over-range multiply reaches the seed and the quantiser as well as the multiply.
         {"lsdj/normalize-quiet", KitCodec::Lsdj, Signal::Quiet, 22050, {gainEffect(0.37f, true)}},
         {"lsdj/normalize-quiet-dither", KitCodec::Lsdj, Signal::Quiet, 44100,
          {gainEffect(0.37f, true), ditherEffect(DitherType::HighPassTPDF)}},
         {"lsdj/normalize-quiet-48000", KitCodec::Lsdj, Signal::Quiet, 48000, {gainEffect(0.25f, true)}},
-        // A negative gain is a phase flip: a legal kit spec and outside the gain node's range, where a clamp would keep
+        // A negative gain is a phase flip: a legal kit spec and outside D1's gain node range, where a clamp would keep
         // the magnitude and lose the flip.
         {"lsdj/gain-negative", KitCodec::Lsdj, Signal::Sine440, 44100, {gainEffect(-1.0f, false)}},
         {"lsdj/filter/low-pass", KitCodec::Lsdj, Signal::Sine440, 44100,
@@ -311,9 +311,10 @@ std::vector<KitCase> kitCases() {
          {filterEffect(FilterType::HighShelf, 3000.0f, 0.7f, 4.0f)}},
         {"lsdj/filter/all-pass", KitCodec::Lsdj, Signal::Noise, 48000,
          {filterEffect(FilterType::AllPass, 1500.0f, 1.2f)}},
-        // Filter specs the biquad node's ranges would clamp: 10 Hz is below its 20 Hz floor and a Q of 20 is past its
+        // Filter specs D1's biquad node ranges would clamp: 10 Hz is below its 20 Hz floor and a Q of 20 is past its
         // 10, and a 30 dB shelf is past its +/-24 dB. Each is a legal `FilterEffect` today, so a graph compile that
-        // sent the spec through the params would silently filter a different spec.
+        // sent the spec through D1's params would have silently filtered a different spec; now they pin that each value
+        // outside D1's ranges reaches the node whole.
         {"lsdj/filter/high-pass-outside-range", KitCodec::Lsdj, Signal::Sine440, 44100,
          {filterEffect(FilterType::HighPass, 10.0f, 20.0f)}},
         {"lsdj/filter/high-shelf-outside-range", KitCodec::Lsdj, Signal::Noise, 22050,
````

Run: `sed -n '469,622p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc)`
Expected: exit 0: the widened ranges are in the binary the recorder below runs.

Each golden is one line of canonical JSON (gain 598 bytes, biquad 916), so both are quoted whole: a reviewer reads
them, and the recording below proves the updater lands the same bytes. The updater is the descriptor case itself under
`RP_GRAPH_GOLDEN_UPDATE=1`, the precedent D1's descriptor goldens set. The only change is each param's `min` and
`max`; defaults, ports, schemas and versions are as they were. Both are recorded here, before the suite runs below, so
that run is green: the descriptor case compares against the new bytes by then, not D1's.

Replace `packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`:

````json
{"category":"dsp/gain","id":"rp.dsp.gain","name":"Gain","params":[{"default":1,"id":"gain","max":100,"min":-100,"name":"Gain","type":"float"}],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__GainNodeSettings":{"properties":{"channels":{"type":"integer"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__GainNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `mkdir -p packages/native/test/graph/golden; sed -n '640,640p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/golden/rp.dsp.gain.descriptor.json`
Expected: `7f46e1f8289e3fc98a953c594db10fd8be2420a9`: the blob fced2f618 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Replace `packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`:

````json
{"category":"dsp/filter","id":"rp.dsp.biquad","name":"Biquad","params":[{"default":1000,"id":"frequency","max":96000,"min":1,"name":"Frequency","type":"float","unit":"Hz"},{"default":1,"id":"q","max":1000,"min":0.001,"name":"Q","type":"float"},{"default":0,"id":"gainDb","max":96,"min":-96,"name":"Gain","type":"float","unit":"dB"}],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__BiquadNodeSettings":{"properties":{"channels":{"type":"integer"},"type":{"enum":["low-pass","high-pass","band-pass","band-stop","peak","low-shelf","high-shelf","all-pass"],"type":"string"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__BiquadNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `mkdir -p packages/native/test/graph/golden; sed -n '652,652p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json`
Expected: `570581d3f5497719851ad0f5d5a218775f96536a`: the blob fced2f618 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `RP_GRAPH_GOLDEN_UPDATE=1 build/bin/retroplug-graph-test "[dsp-nodes]" && git add -- packages/native/test/graph/golden/rp.dsp.gain.descriptor.json packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json && git diff --cached --stat fced2f618 -- packages/native/test/graph/golden`
Expected: nothing printed after the run: the updater rewrites the two files, and the staged diff against the prototype commit is empty, so the quoted blocks are the bytes this tree records.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample]"`
Expected: `All tests passed (209138 assertions in 51 test cases)`, the re-recorded goldens already in place.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc)`
Expected: exit 0: `KitBaseline.test.cpp` changed in comments only, and its binary is rebuilt here so the run below is this tree's rather than a stale one.

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (428 assertions in 1 test case)`, the 39 banks unchanged: no kit byte depends on a range.

- [ ] **Step 3: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, with the plugin and `retroplug-sdl` included.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-graph-test` `All tests passed (302400 assertions in 259 test cases)`; `retroplug-engine-graph-test` `test cases: 268 | 259 passed | 9 skipped`; `retroplug-audio-test` `All tests passed (1235263 assertions in 109 test cases)`; launchpad 144 / 29.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, nothing skipped, no baseline re-recorded.

Run: `pnpm test`
Expected: `# 169 test file(s) passed (jobs=16)` (unchanged: no TS source moved).

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 4: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `All tests passed (35205 assertions in 18 test cases)`, no ThreadSanitizer report.

Run: `cmake --build build-asan --target retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `All tests passed (35205 assertions in 18 test cases)`, no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 5: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/native/src/graph/nodes/DspNodes.cpp
 M packages/native/test/audio/KitBaseline.test.cpp
 M packages/native/test/graph/DspNodes.test.cpp
M  packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json
M  packages/native/test/graph/golden/rp.dsp.gain.descriptor.json
````

````bash
git add -- packages/native/src/graph/nodes/DspNodes.cpp packages/native/test/audio/KitBaseline.test.cpp \
  packages/native/test/graph/DspNodes.test.cpp packages/native/test/graph/golden/rp.dsp.biquad.descriptor.json \
  packages/native/test/graph/golden/rp.dsp.gain.descriptor.json
git commit -F - <<'EOF'
feat(dsp): rp.dsp.gain and rp.dsp.biquad cover any sane kit value

The gain range becomes -100..100 and the biquad's 1..96000 Hz, Q 0.001..1000,
-96..96 dB, so a kit spec's plain gain (a phase flip, a boost or cut) and its
filter reach a node whole instead of being clamped silently by the planner. A
normalising gain is rp.dsp.normalize's, computed from its buffer at activate, and
never this param's. The planner and the host still clamp a document's or
setParam's value to the new bounds, a wired value is still not clamped, the
biquad still clamps nothing of its own, and the guards are unchanged.

The kit compiler, which a later commit lands, refuses a spec outside the ranges
itself and a frequency at or above its source rate's Nyquist, because the planner
cannot see the plan's rate.

The two descriptor goldens re-record; only each param's min and max moved, the
defaults are unchanged. New [dsp-nodes] cases hold the node to its kernel, bit for
bit through a real plan, at the values D1's kit baseline carries outside D1's
ranges (a gain of -1.0 and of 7.4, a 10 Hz high-pass at Q 20, a 3 kHz high shelf
at Q 0.7 and +30 dB), and pin the silent clamp: a document's value beyond a new
bound gives the kernel's stream at that bound, for the frequency and Q floors,
the Q ceiling and both gain bounds, and each such case also holds the asked
value's own stream different from the bound's.
EOF
````

---

### Task 2: offline jobs run a graph document on a host of their own

Design section 3 and section 10's first and fifth questions. This is D2's seam: one RPC submits a graph document, its
inputs, the buffers to record and return, and the frames to run; a native pool runs each job on a worker with a fresh
`GraphHost` and its own `ResourceTable`; a polled, bounded completion queue carries the buffers back. The pool is apart
from the live engine, so a job never touches the plan that is playing, and a job's host is built for it and torn down
with it, so every resampler dies inside that host's life. TypeScript gets `backend.offlineRun()`, a promise over submit
and poll, whose pump is the capability jobs' and whose native wait only `createAudioDriver` turns on. The tests are
written first and cannot compile until the seam exists, which is this task's red.

**Files:**

- Create: `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/engine/OfflineJobs.hpp`, `packages/native/test/engine-graph/OfflineJobs.test.cpp`, `packages/retroplug/src/offline.ts`, `packages/retroplug/test-native/offline-run.test.ts`, `packages/retroplug/test/offline/offline.test.ts`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`, `packages/native/node/test/ops.mjs`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/rpc/BackendRpcRegistration.hpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/retroplug/src/audioDriver.ts`, `packages/retroplug/src/backend.ts`, `packages/retroplug/src/capability.ts`, `packages/retroplug/src/realBackend.ts`, `packages/retroplug/testing/mockBackend.ts`, `packages/retroplug/ui/app/useJobPump.ts`

- [ ] **Step 1: Write the failing cases**

`OfflineJobs.test.cpp` (new, `[offline]`, in `retroplug-engine-graph-test`) holds the seam to what it promises: a
job's buffers equal the same nodes run directly through a graph rig; two jobs on two workers do not interfere; the
pool's concurrency and its 64-job bound hold; a queued and a running cancel each complete `cancelled`; an engine
destroyed with jobs in flight abandons them and joins every worker; every refusal's message; the RPC's sample bytes
and handles; both windows and the cap's arithmetic against copies of each formula, over every offset, length and size
at 11 file rates and all 16 PAL rates, and every whole rate 1000..96000; a file input against `SampleCache`'s decode;
a job running beside a live plan that stays bit for bit with warmed blocks allocating nothing; and a hidden
`[offline-bench]`. `test-native/offline-run.test.ts` (new) is the promise side over the real host, and
`test/offline/offline.test.ts` (new) is the mock's rejection plus the promise side over a scripted port.
`packages/native/node/test/ops.mjs` gains a real submit and drain (samples plus a windowed file input, a refusal) in
the Node-vs-QuickJS codec matrix, and `cmake/tests.cmake` adds the new test source.

Create `packages/native/test/engine-graph/OfflineJobs.test.cpp`:

````cpp
// Offline jobs (docs/modular-graph-phase-d2-design.md section 3, host/engine/OfflineJobs.hpp): a graph document run to
// completion on a host of its own, on a worker of the engine's offline pool. A job returns exactly what the same nodes
// give through a GraphRig run directly; jobs past the pool's concurrency queue and each returns its own result; a
// cancel stops a queued job at once and a running one at its next block, with the cancel reason; an engine destroyed with
// jobs in flight abandons them and joins every worker; the outstanding bound refuses; every refusal says why, at
// submission or in the completion; a file input is SampleCache's decode, windowed exactly as the two kit compilers
// window today, the cap's two arithmetics and every edge included, with its report; and a job run while an audio thread
// plays the live plan leaves that plan's output bit for bit, its warmed blocks allocating nothing.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[offline]"`; the hidden
// "[offline-bench]" times a trivial job and a one-second resample.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "../graph/GraphRig.hpp"
#include "GraphDocs.hpp"
#include "dsp/Resample.hpp"
#include "graph/node.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/engine/OfflineJobs.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "kit/SampleCache.hpp"
#include "lsdj/KitUtil.hpp"
#include "risa/RisaDmcCodec.hpp"

using rp::host::OfflineCap;
using rp::host::OfflineCapArithmetic;
using rp::host::OfflineCompletion;
using rp::host::OfflineFile;
using rp::host::OfflineJobs;
using rp::host::OfflineOutput;
using rp::host::OfflineRequest;
using rp::host::OfflineSamples;

namespace {

// A node whose activation refuses, for the activate-failed completion; a planner refusal of the same document names it.
struct Unready {
    static constexpr const char* id = "rp.test.unready";
    struct { rp::audio_out<rp::mono> out; } outputs;
    bool activate(double, std::uint32_t) { return false; }
    void process(const rp::Block&) noexcept {}
};

const rp_package* unreadyPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Unready>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.offline","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

constexpr double kTwoPi = 6.283185307179586;

// Interleaved test audio: a sine per channel at its own frequency, plus a little seeded noise, so each channel and
// each seed is told apart, and a resampler's filter has something above the kit rate's Nyquist to remove.
std::vector<float> signal(std::size_t frames, std::uint32_t channels, std::uint32_t seed, double rate = 44100.0) {
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> noise(-0.05f, 0.05f);
    std::vector<float>                    out(frames * channels);
    for (std::size_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c)
            out[f * channels + c] =
                0.6f * static_cast<float>(std::sin(kTwoPi * (220.0 * (c + 1) + seed) * double(f) / rate)) + noise(rng);
    return out;
}

// Buffer player (or resampler) -> gain -> recorder, `channels` wide, playing res:1 into res:2.
std::string chainDocument(bool resample, double gain, std::uint32_t channels) {
    const std::string c = std::to_string(channels);
    const std::string layout = channels == 1 ? "mono" : "stereo";
    const std::string source =
        resample ? R"({"id":"src","type":"rp.dsp.resample","settings":{"buffer":"res:1","channels":)" + c +
                       R"(,"layout":")" + layout + R"("}})"
                 : R"({"id":"src","type":"rp.core.buffer-player","settings":{"channels":)" + c + R"(,"layout":")" +
                       layout + R"(","loop":false},"resources":{"sample":"res:1"}})";
    char g[64];
    std::snprintf(g, sizeof g, "%.17g", gain);
    return R"({"nodes":[)" + source + R"(,{"id":"g","type":"rp.dsp.gain","settings":{"channels":)" + c +
           R"(},"params":{"gain":)" + g + R"(}},{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":)" +
           c + R"(},"resources":{"target":"res:2"}}],
             "wires":[{"from":"src.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
}

// The same nodes through a GraphRig directly, at `planRate` in blocks of `block`: the recorder's buffer, interleaved.
std::vector<float> rigReference(const std::vector<float>& input, std::uint32_t channels, double inputRate,
                                double planRate, bool resample, double gain, std::uint32_t outFrames,
                                std::uint32_t block = OfflineJobs::kBlockFrames) {
    rp::graph::test::GraphRig rig(rp::graph::HostConfig{planRate, block, rp::graph::Target::Desktop}, 0, nullptr, block);
    const std::uint32_t frames = static_cast<std::uint32_t>(input.size() / channels);
    const auto in = rig.resources.createAudioBuffer(channels, frames, inputRate);
    const auto out = rig.resources.createAudioBuffer(channels, outFrames, planRate);
    REQUIRE(in == 1);
    REQUIRE(out == 2);
    auto* b = static_cast<rp_audio_buffer*>(rig.resources.get(in)->payload);
    for (std::uint32_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c) b->data[c][f] = input[std::size_t(f) * channels + c];
    const auto planned = rig.set(chainDocument(resample, gain, channels));
    INFO(rp::graph::test::GraphRig::codes(planned));
    REQUIRE(planned.ok);
    rig.transport.sample_rate = planRate;
    for (std::uint32_t done = 0; done < outFrames;) {
        const std::uint32_t n = std::min(block, outFrames - done);
        rig.host->processBlock(n, rig.transport);
        done += n;
    }
    const auto* r = static_cast<const rp_audio_buffer*>(rig.resources.get(out)->payload);
    std::vector<float> interleaved(std::size_t(channels) * outFrames);
    for (std::uint32_t f = 0; f < outFrames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c) interleaved[std::size_t(f) * channels + c] = r->data[c][f];
    return interleaved;
}

OfflineRequest chainRequest(std::vector<float> input, std::uint32_t channels, double inputRate, double planRate,
                            bool resample, double gain, std::uint32_t outFrames) {
    OfflineRequest r;
    r.document = chainDocument(resample, gain, channels);
    r.inputs.emplace_back(OfflineSamples{std::move(input), channels, inputRate});
    r.outputs.push_back({channels, outFrames, planRate});
    r.returns = {2};
    r.frames = outFrames;
    r.rate = planRate;
    return r;
}

// A job long enough to be running whenever the case acts on it, and to keep running until it is cancelled: a looping
// resampler through a gain for as many frames as a job may ask for (a day of audio, hours of work).
OfflineRequest longRequest() {
    OfflineRequest r;
    r.document = R"({"nodes":[{"id":"p","type":"rp.dsp.resample","settings":{"buffer":"res:1","channels":1,
                                                                         "layout":"mono","loop":true}},
                             {"id":"g","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":0.5}},
                             {"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
                              "resources":{"target":"res:2"}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
    r.inputs.emplace_back(OfflineSamples{signal(4096, 1, 7), 1, 44100.0});
    r.outputs.push_back({1, 4096, 48000.0});
    r.returns = {2};
    r.frames = std::numeric_limits<std::uint32_t>::max();
    r.rate = 48000.0;
    return r;
}

// Drain `drain` (waiting between drains) until every id in `ids` has completed, or 60 s have passed.
template <class Drain>
std::map<std::uint32_t, OfflineCompletion> collect(Drain&& drain, const std::vector<std::uint32_t>& ids) {
    std::map<std::uint32_t, OfflineCompletion> done;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (done.size() < ids.size() && std::chrono::steady_clock::now() < deadline)
        for (auto& c : drain(std::chrono::milliseconds(50))) done.emplace(c.id, std::move(c));
    return done;
}

OfflineCompletion one(OfflineJobs& jobs, std::uint32_t id) {
    auto done = collect([&](auto w) { return jobs.drain(w); }, {id});
    REQUIRE(done.count(id) == 1);
    return std::move(done[id]);
}

// The thread count of this process, from /proc on Linux; -1 elsewhere.
int threadCount() {
#if defined(__linux__)
    int n = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator("/proc/self/task")) ++n;
    return n;
#else
    return -1;
#endif
}

// A WAV file, little-endian byte by byte: 16-bit PCM (`floatSamples` false), whose decode converts, or 32-bit float.
void writeWav(const std::filesystem::path& path, const std::vector<float>& samples, std::uint32_t rate,
              bool floatSamples) {
    std::vector<std::uint8_t> bytes;
    const auto u16 = [&](std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(v >> shift));
    };
    const auto tag = [&](const char* s) { bytes.insert(bytes.end(), s, s + 4); };
    const std::uint32_t width = floatSamples ? 4 : 2;
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * width);
    tag("RIFF");
    u32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    u32(16);
    u16(floatSamples ? 3 : 1);
    u16(1);
    u32(rate);
    u32(rate * width);
    u16(static_cast<std::uint16_t>(width));
    u16(static_cast<std::uint16_t>(width * 8));
    tag("data");
    u32(dataBytes);
    for (const float v : samples) {
        if (floatSamples) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof bits);
            u32(bits);
        } else {
            const long pcm = std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
            u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(pcm)));
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A directory of the case's own under the temp dir, removed with it.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / ("rp-offline-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// The PAL DMC rates (RisaDmcCodec.cpp's table), the risa cap's rate.
constexpr double kPalRates[16] = {
    4177.40,  4696.63,  5261.41,  5579.22,  6023.94,  7044.94,  7917.18,  8397.01,
    9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
};
constexpr std::uint32_t kLsdjCapFrames = static_cast<std::uint32_t>(rp::lsdj::Kit::kMaxSampleFrames);
constexpr std::uint32_t kRisaCapFrames = 4081u * 8u;  // RisaDmcCodec's kDmcMaxBytes in bits

// Today's two windows, copied from their homes as they compute them: what the job's window must reproduce.
std::pair<std::size_t, std::size_t> lsdjWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                               std::size_t length) {
    // KitUtil::compileSample
    const float srRatio = static_cast<float>(sampleRate) / static_cast<float>(rp::lsdj::kGameboySampleRate);
    const std::size_t maxFrames = static_cast<std::size_t>(
        std::ceil(static_cast<float>(rp::lsdj::Kit::kMaxSampleFrames) * srRatio));
    const std::size_t length0 = (length == 0) ? size : length;
    const std::size_t startFrame = std::min(offset, size);
    return {startFrame, std::min(maxFrames, std::min(length0, size - startFrame))};
}

std::pair<std::size_t, std::size_t> risaWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                               std::size_t length, double targetRate) {
    // RisaDmcCodec::encode
    const double srcRate = static_cast<double>(sampleRate);
    const std::size_t maxOutFrames =
        static_cast<std::size_t>(std::ceil(static_cast<double>(4081 * 8) * srcRate / targetRate));
    const std::size_t length0 = length == 0 ? size : length;
    const std::size_t start = std::min(offset, size);
    return {start, std::min(maxOutFrames, std::min(length0, size - start))};
}

OfflineCap lsdjCap() { return {kLsdjCapFrames, double(rp::lsdj::kGameboySampleRate), OfflineCapArithmetic::FloatRatio}; }
OfflineCap risaCap(int index) { return {kRisaCapFrames, kPalRates[index], OfflineCapArithmetic::DoubleScale}; }

OfflineRequest fileRequest(const std::filesystem::path& path, std::uint32_t offset, std::uint32_t length,
                           std::optional<OfflineCap> cap) {
    OfflineRequest r;
    r.inputs.emplace_back(OfflineFile{path.string(), offset, length, cap});
    r.returns = {1};
    return r;
}

}  // namespace

TEST_CASE("an offline job runs a document to completion and returns what the same nodes give run directly",
          "[engine-graph][offline]") {
    OfflineJobs jobs;
    // A resample to the kit rate (the frames a kit cuts there) and a plain player at the source rate, mono and stereo,
    // each through a gain into a recorder: the job's bytes are the rig's, bit for bit.
    struct Case {
        bool          resample;
        std::uint32_t channels;
        double        planRate;
    };
    for (const Case k :
         {Case{true, 1, 11468.0}, Case{true, 2, 11468.0}, Case{false, 2, 44100.0}, Case{false, 1, 22050.0}}) {
        INFO("resample " << k.resample << ", " << k.channels << " channel(s) at " << k.planRate);
        const std::size_t frames = 5000;
        const auto input = signal(frames, k.channels, 3);
        const std::uint32_t outFrames =
            k.resample ? static_cast<std::uint32_t>(rp::dsp::resampledLength(frames, 44100.0, k.planRate))
                       : static_cast<std::uint32_t>(frames);
        const auto reference = rigReference(input, k.channels, 44100.0, k.planRate, k.resample, 0.75, outFrames);
        const auto s = jobs.submit(chainRequest(input, k.channels, 44100.0, k.planRate, k.resample, 0.75, outFrames));
        INFO(s.error);
        REQUIRE(s.id != 0);
        const auto c = one(jobs, s.id);
        INFO(c.error);
        REQUIRE(c.ok);
        REQUIRE(c.buffers.size() == 1);
        CHECK(c.buffers[0].handle == 2);
        CHECK(c.buffers[0].channels == k.channels);
        CHECK(c.buffers[0].frames == outFrames);
        CHECK(c.buffers[0].rate == k.planRate);
        CHECK(c.buffers[0].samples == reference);
        CHECK(c.inputs.empty());  // no file input to report
        // Not vacuous: the recorder wrote, and the gain applied.
        float peak = 0.0f;
        for (const float v : c.buffers[0].samples) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.2f);
    }
    // A run whose length is not a whole number of blocks ends on a partial one, and the recorder stops at its buffer's
    // end: a buffer shorter than the run keeps its frames, and a run shorter than the buffer leaves the rest zero.
    const auto input = signal(3000, 1, 5);
    auto shortRun = chainRequest(input, 1, 48000.0, 48000.0, false, 1.0, 3000);
    shortRun.frames = 1500;
    const auto s = jobs.submit(std::move(shortRun));
    REQUIRE(s.id != 0);
    const auto c = one(jobs, s.id);
    REQUIRE(c.ok);
    REQUIRE(c.buffers[0].samples.size() == 3000);
    CHECK(std::equal(input.begin(), input.begin() + 1500, c.buffers[0].samples.begin()));
    CHECK(std::all_of(c.buffers[0].samples.begin() + 1500, c.buffers[0].samples.end(),
                      [](float v) { return v == 0.0f; }));
    // A job with no document returns its inputs as the submission gave them, an input among its returns.
    OfflineRequest echo;
    echo.inputs.emplace_back(OfflineSamples{signal(100, 2, 9), 2, 32000.0});
    echo.outputs.push_back({1, 10, 32000.0});
    echo.returns = {2, 1};
    const auto e = one(jobs, jobs.submit(echo).id);
    REQUIRE(e.ok);
    REQUIRE(e.buffers.size() == 2);
    CHECK(e.buffers[0].handle == 2);
    CHECK(e.buffers[0].samples == std::vector<float>(10, 0.0f));
    CHECK(e.buffers[1].handle == 1);
    CHECK(e.buffers[1].channels == 2);
    CHECK(e.buffers[1].rate == 32000.0);
    CHECK(e.buffers[1].samples == std::get<OfflineSamples>(echo.inputs[0]).samples);
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("offline jobs past the pool's concurrency queue, and each returns its own result", "[engine-graph][offline]") {
    OfflineJobs jobs;
    const std::size_t count = std::min<std::size_t>(3 * OfflineJobs::concurrency() + 1, OfflineJobs::kMaxJobs);
    const auto input = signal(6000, 1, 11);
    std::vector<std::uint32_t> ids;
    std::map<std::uint32_t, double> gains;
    for (std::size_t i = 0; i < count; ++i) {
        const double gain = 0.1 + 0.05 * double(i);
        const auto s = jobs.submit(chainRequest(input, 1, 44100.0, 11468.0, true, gain, 1500));
        INFO(s.error);
        REQUIRE(s.id != 0);
        ids.push_back(s.id);
        gains[s.id] = gain;
    }
    CHECK(jobs.workers() <= OfflineJobs::concurrency());
    const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
    REQUIRE(done.size() == count);
    // Each job's result is its own gain over the same resample: the rig's run of that gain, bit for bit.
    for (const auto id : ids) {
        const auto& c = done.at(id);
        INFO(c.error);
        REQUIRE(c.ok);
        CHECK(c.buffers[0].samples == rigReference(input, 1, 44100.0, 11468.0, true, gains[id], 1500));
    }
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("two jobs submitted against one parked worker start a second one", "[engine-graph][offline]") {
    if (OfflineJobs::concurrency() < 2) SKIP("one hardware thread: the pool runs one worker");
    OfflineJobs jobs;
    // One worker, started and parked again.
    REQUIRE(one(jobs, jobs.submit(chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64)).id).ok);
    REQUIRE(jobs.workers() == 1);
    // Two long jobs back to back: the parked worker is claimed by the first, so the second starts a worker of its own
    // rather than queue behind a job that runs for hours.
    const auto a = jobs.submit(longRequest()).id;
    const auto b = jobs.submit(longRequest()).id;
    CHECK(jobs.workers() == 2);
    CHECK(jobs.cancel(a));
    CHECK(jobs.cancel(b));
    CHECK(collect([&](auto w) { return jobs.drain(w); }, {a, b}).size() == 2);
}

TEST_CASE("a cancel stops an offline job, queued or running, with the cancel reason", "[engine-graph][offline]") {
    OfflineJobs jobs;
    // Fill every worker with a long job, then one more, which queues.
    std::vector<std::uint32_t> running;
    for (unsigned i = 0; i < OfflineJobs::concurrency(); ++i) running.push_back(jobs.submit(longRequest()).id);
    const auto queued = jobs.submit(longRequest()).id;
    REQUIRE(queued != 0);
    // The queued one completes at the cancel, unrun.
    CHECK(jobs.cancel(queued));
    const auto first = jobs.drain();
    REQUIRE(first.size() == 1);
    CHECK(first[0].id == queued);
    CHECK_FALSE(first[0].ok);
    CHECK(first[0].error == "cancelled");
    CHECK_FALSE(jobs.cancel(queued));  // finished: nothing to cancel
    // The running ones stop at their next block: ten minutes of audio each, cut short within seconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto id : running) CHECK(jobs.cancel(id));
    const auto done = collect([&](auto w) { return jobs.drain(w); }, running);
    REQUIRE(done.size() == running.size());
    for (const auto& [id, c] : done) {
        CHECK_FALSE(c.ok);
        CHECK(c.error == "cancelled");
        CHECK(c.buffers.empty());
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    CHECK_FALSE(jobs.cancel(9999));  // never taken
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("an engine destroyed with offline jobs in flight abandons them and joins every worker",
          "[engine-graph][offline]") {
    const int before = threadCount();
    const auto t0 = std::chrono::steady_clock::now();
    {
        Engine e(48000.0);
        CHECK(e.offlineJobsOutstanding() == 0);
        CHECK(e.drainOfflineJobs().empty());
        CHECK_FALSE(e.cancelOfflineJob(1));
        // Running and queued long jobs, a short one that may have finished undrained.
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 2; ++i) REQUIRE(e.submitOfflineJob(longRequest()).id != 0);
        REQUIRE(e.submitOfflineJob(chainRequest(signal(500, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 500)).id != 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (before >= 0) CHECK(threadCount() > before);  // the workers are there
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    if (before >= 0) CHECK(threadCount() == before);  // and joined, every one
    // The pool alone, the same way.
    {
        OfflineJobs jobs;
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 2; ++i) REQUIRE(jobs.submit(longRequest()).id != 0);
    }
    if (before >= 0) CHECK(threadCount() == before);
}

TEST_CASE("the offline job bound refuses past it, and takes again once a drain makes room", "[engine-graph][offline]") {
    OfflineJobs jobs;
    std::vector<std::uint32_t> ids;
    for (std::size_t i = 0; i < OfflineJobs::kMaxJobs; ++i) {
        const auto s = jobs.submit(longRequest());
        REQUIRE(s.id != 0);
        ids.push_back(s.id);
    }
    CHECK(jobs.outstanding() == OfflineJobs::kMaxJobs);
    const auto refused = jobs.submit(longRequest());
    CHECK(refused.id == 0);
    CHECK(refused.error == "busy: 64 offline jobs are outstanding");
    for (const auto id : ids) CHECK(jobs.cancel(id));
    // Finished and undrained still counts: the bound holds room for every completion.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(jobs.submit(longRequest()).id == 0);
    const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
    CHECK(done.size() == OfflineJobs::kMaxJobs);
    const auto again = jobs.submit(chainRequest(signal(64, 1, 2), 1, 48000.0, 48000.0, false, 1.0, 64));
    CHECK(again.id == ids.back() + 1);  // ids count up, the refusals taking none
    CHECK(one(jobs, again.id).ok);
}

TEST_CASE("every offline refusal says why, at submission or in its completion", "[engine-graph][offline]") {
    const rp_package* extra[] = {rp::graph::test::testPackage(), unreadyPackage()};
    OfflineJobs jobs(extra);
    const auto refusal = [&](OfflineRequest r) { return jobs.submit(std::move(r)).error; };
    const auto failure = [&](OfflineRequest r) {
        const auto s = jobs.submit(std::move(r));
        INFO(s.error);
        REQUIRE(s.id != 0);
        const auto c = one(jobs, s.id);
        CHECK_FALSE(c.ok);
        return c.error;
    };
    const auto base = [] { return chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64); };

    // At submission.
    auto r = base();
    r.rate = 0.0;
    CHECK(refusal(r).find("sample rate") != std::string::npos);
    r = base();
    r.outputs.assign(OfflineJobs::kMaxResources, OfflineOutput{1, 1, 48000.0});
    CHECK(refusal(r) == "a job takes at most 64 inputs and outputs, not 65");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).channels = 0;
    CHECK(refusal(r) == "input 0 has no channels");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).channels = 3;
    CHECK(refusal(r) == "input 0 holds 64 samples, not a whole number of 3-channel frames");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).rate = std::numeric_limits<double>::quiet_NaN();
    CHECK(refusal(r) == "input 0 needs a sample rate that is a positive number");
    r = base();
    r.inputs[0] = OfflineFile{};
    CHECK(refusal(r) == "input 0 names no file");
    r = base();
    r.inputs[0] = OfflineFile{"x.wav", 0, 0, OfflineCap{10, -1.0, OfflineCapArithmetic::DoubleScale}};
    CHECK(refusal(r) == "input 0's cap needs a rate that is a positive number");
    r = base();
    r.outputs[0].frames = 0;
    CHECK(refusal(r) == "output 0 needs at least one channel and one frame");
    r = base();
    r.returns = {3};
    CHECK(refusal(r) == "a return names res:3, which is no input or output of the job");

    // In the completion: a file that will not decode, a document that is not one, the planner's refusals (a type no
    // offline host has, a script node or a system node among them, and a handle an empty input leaves naming nothing),
    // a node that will not activate, and a node that faults.
    r = base();
    r.inputs[0] = OfflineFile{"/nonexistent/rp-offline.wav", 0, 0, std::nullopt};
    CHECK(failure(r) == "input 0: /nonexistent/rp-offline.wav could not be read or decoded");
    r = base();
    r.document = "{";
    CHECK(failure(r).rfind("not a graph document: ", 0) == 0);
    r = base();
    r.document = R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb"},{"id":"route","type":"rp.midi.route"}],"wires":[]})";
    CHECK(failure(r) == "the document was refused: gb: unknown-type: no node type \"rp.sameboy.gb\" is registered; "
                        "route: unknown-type: no node type \"rp.midi.route\" is registered");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).samples.clear();  // an input of no frames: res:1 names nothing
    CHECK(failure(r).find("unknown-resource") != std::string::npos);
    r = base();
    r.document = R"({"nodes":[{"id":"u","type":"rp.test.unready"}],"wires":[]})";
    CHECK(failure(r).rfind("the document was refused: u: activate-failed", 0) == 0);
    r = base();
    r.document = R"({"nodes":[{"id":"f","type":"rp.test.faulty","settings":{"failAt":2}},
                              {"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
                               "resources":{"target":"res:2"}}],
                     "wires":[{"from":"f.out","to":"rec.in"}]})";
    r.frames = 4096;
    CHECK(failure(r) == "a node faulted: f");
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("the offline RPCs carry a job's samples as float32 bytes, and refuse an input of neither or both kinds",
          "[engine-graph][offline]") {
    Engine           e(48000.0);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    const auto input = signal(256, 2, 4);
    const auto bytesOf = [](const std::vector<float>& v) {
        const auto* p = reinterpret_cast<const std::byte*>(v.data());
        return rfl::Bytestring(p, p + v.size() * sizeof(float));
    };
    RpcOfflineRequest req;
    req.document = chainDocument(false, 0.5, 2);
    req.inputs.push_back({bytesOf(input), 2u, 48000.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    req.outputs.push_back({2, 256, 48000.0});
    req.returns = {2};
    req.frames = 256;
    req.rate = 48000.0;
    const auto s = rpc.submitOfflineJob(req);
    REQUIRE(s.id.has_value());
    CHECK_FALSE(s.error.has_value());
    std::vector<RpcOfflineCompletion> done;
    for (int i = 0; i < 100 && done.empty(); ++i) {
        auto drained = rpc.drainOfflineJobs(100.0);
        CHECK(drained.inFlight <= 1);
        done = std::move(drained.completions);
    }
    REQUIRE(done.size() == 1);
    CHECK(done[0].id == *s.id);
    INFO(done[0].error);
    REQUIRE(done[0].ok);
    REQUIRE(done[0].buffers.size() == 1);
    std::vector<float> got(done[0].buffers[0].samples.size() / sizeof(float));
    std::memcpy(got.data(), done[0].buffers[0].samples.data(), done[0].buffers[0].samples.size());
    REQUIRE(got.size() == input.size());
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i] == input[i] * 0.5f);
    const auto idle = rpc.drainOfflineJobs(std::numeric_limits<double>::quiet_NaN());  // nothing in flight: no wait
    CHECK(idle.completions.empty());
    CHECK(idle.inFlight == 0);
    // A job still running is counted in flight, so a caller can tell a job native holds from one it does not.
    const auto running = rpc.submitOfflineJob([&] {
        RpcOfflineRequest r = req;
        r.frames = std::numeric_limits<std::uint32_t>::max();
        r.document = chainDocument(false, 0.5, 2);
        return r;
    }());
    REQUIRE(running.id.has_value());
    CHECK(rpc.drainOfflineJobs(0.0).inFlight == 1);
    CHECK(rpc.cancelOfflineJob(*running.id));
    std::size_t cancelled = 0;
    for (int i = 0; i < 100 && cancelled == 0; ++i) cancelled = rpc.drainOfflineJobs(100.0).completions.size();
    CHECK(cancelled == 1);
    CHECK_FALSE(rpc.cancelOfflineJob(*s.id));

    auto bad = req;
    bad.inputs[0].path = "x.wav";
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0 must be samples (samples, channels and rate) or a file (a path and its window)");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0 must be samples (samples, channels and rate) or a file (a path and its window)");
    bad = req;
    bad.inputs[0].rate.reset();
    CHECK(rpc.submitOfflineJob(bad).error == "input 0 needs samples, channels and a rate");
    bad = req;
    bad.inputs[0].samples = rfl::Bytestring(3);
    CHECK(rpc.submitOfflineJob(bad).error == "input 0's samples are 3 bytes, not a whole number of float32 values");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, 4u, std::nullopt, std::nullopt};
    CHECK(rpc.submitOfflineJob(bad).error == "input 0 needs a path");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, "x.wav", std::nullopt, std::nullopt,
                     RpcOfflineCap{10, 11468.0, "float"}};
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0's cap arithmetic \"float\" is neither \"float-ratio\" nor \"double-scale\"");
    CHECK(e.offlineJobsOutstanding() == 0);
}

TEST_CASE("the offline window is each kit compiler's window, the cap's arithmetic included", "[engine-graph][offline]") {
    // Directly, over rates and lengths where the two arithmetics' roundings matter: every PAL rate for risa's, LSDj's
    // kit rate for its own, at decoded rates a file carries, with offsets at, past and before the end and the zero
    // length, against the windows as their homes compute them.
    for (const std::uint32_t rate :
         {8000u, 11025u, 11468u, 16000u, 22050u, 32000u, 44100u, 48000u, 88200u, 96000u, 192000u}) {
        for (const std::size_t size : {std::size_t(0), std::size_t(1), std::size_t(9999), std::size_t(400000)}) {
            for (const std::uint32_t offset : {0u, 1u, 5000u, 399999u, 400000u, 400001u}) {
                for (const std::uint32_t length : {0u, 1u, 30000u}) {
                    INFO(rate << " Hz, " << size << " frames, offset " << offset << ", length " << length);
                    const auto w = rp::host::offlineWindow(size, rate, offset, length, lsdjCap());
                    const auto [ls, lf] = lsdjWindow(size, rate, offset, length);
                    CHECK(w.start == ls);
                    CHECK(w.frames == lf);
                    for (int i = 0; i < 16; ++i) {
                        const auto r = rp::host::offlineWindow(size, rate, offset, length, risaCap(i));
                        const auto [rs, rf] = risaWindow(size, rate, offset, length, kPalRates[i]);
                        CHECK(r.start == rs);
                        CHECK(r.frames == rf);
                    }
                    const auto none = rp::host::offlineWindow(size, rate, offset, length, std::nullopt);
                    const std::size_t left = size - std::min<std::size_t>(offset, size);
                    CHECK(none.frames == std::min<std::size_t>(length ? length : size, left));
                }
            }
        }
    }
    // Every whole decoded rate a file may carry up to 96 kHz, the cap binding: the job's window and the kit's agree at
    // each one, LSDj's and all sixteen of risa's.
    int lsdjMismatches = 0, risaMismatches = 0;
    for (std::uint32_t rate = 1000; rate <= 96000; ++rate) {
        if (rp::host::offlineWindow(1u << 22, rate, 0, 0, lsdjCap()).frames != lsdjWindow(1u << 22, rate, 0, 0).second)
            ++lsdjMismatches;
        for (int i = 0; i < 16; ++i)
            if (rp::host::offlineWindow(1u << 22, rate, 0, 0, risaCap(i)).frames !=
                risaWindow(1u << 22, rate, 0, 0, kPalRates[i]).second)
                ++risaMismatches;
    }
    CHECK(lsdjMismatches == 0);
    CHECK(risaMismatches == 0);
    // The two arithmetics really differ somewhere a kit reaches: the same cap through the other one moves a frame.
    bool differs = false;
    for (std::uint32_t rate = 8000; rate <= 96000 && !differs; rate += 1)
        differs = rp::host::offlineWindow(1u << 20, rate, 0, 0, lsdjCap()).frames !=
                  rp::host::offlineWindow(1u << 20, rate, 0, 0,
                                          OfflineCap{kLsdjCapFrames, 11468.0, OfflineCapArithmetic::DoubleScale}).frames;
    CHECK(differs);
}

TEST_CASE("a file input is SampleCache's decode, windowed as today's kit compilers window it, with its report",
          "[engine-graph][offline]") {
    TempDir     dir;
    OfflineJobs jobs;
    // Two decoded rates, three seconds each: long enough that both caps bind from the start (LSDj's at its kit rate,
    // risa's at the fastest PAL rate) and neither does from late in the file. 16-bit PCM, so the decode converts.
    for (const std::uint32_t rate : {44100u, 22050u}) {
        const auto path = dir.path / ("src-" + std::to_string(rate) + ".wav");
        writeWav(path, signal(std::size_t(rate) * 3, 1, rate, double(rate)), rate, false);
        rp::kit::SampleCache cache;
        const rp::kit::SampleData* decoded = cache.getOrLoad(path.string());
        REQUIRE(decoded);
        const std::size_t size = decoded->buffer.size();
        REQUIRE(decoded->sampleRate == rate);

        // The whole file: the decode, bit for bit, at its rate, with its report.
        auto whole = one(jobs, jobs.submit(fileRequest(path, 0, 0, std::nullopt)).id);
        INFO(whole.error);
        REQUIRE(whole.ok);
        CHECK(whole.buffers[0].samples == decoded->buffer);
        CHECK(whole.buffers[0].rate == double(rate));
        CHECK(whole.buffers[0].channels == 1);
        REQUIRE(whole.inputs.size() == 1);
        CHECK(whole.inputs[0].handle == 1);
        CHECK(whole.inputs[0].decodedRate == rate);
        CHECK(whole.inputs[0].decodedFrames == size);
        CHECK(whole.inputs[0].frames == size);
        CHECK_FALSE(whole.inputs[0].nonFinite);

        struct Edge {
            const char*               name;
            std::uint32_t             offset, length;
            int                       kind;  // 0: LSDj's window, 1..: risa's at PAL index kind - 1
        };
        const auto n = static_cast<std::uint32_t>(size);
        const std::vector<Edge> edges = {
            {"LSDj, no window: the cap binds", 0, 0, 0},
            {"LSDj, offset past the end", n + 10, 0, 0},
            {"LSDj, offset at the end", n, 0, 0},
            {"LSDj, zero length from late on: to the end, under the cap", n - 1000, 0, 0},
            {"LSDj, a length past what is left", n - 300, 5000, 0},
            {"LSDj, a length under the cap", 100, 2000, 0},
            {"risa fastest, no window: the cap binds", 0, 0, 16},
            {"risa slowest, no window: the file is shorter than the cap", 0, 0, 1},
            {"risa, offset past the end", n + 1, 0, 13},
            {"risa, zero length from the middle", n / 2, 0, 16},
            {"risa, a length past what is left", n - 10, 50, 7},
        };
        for (const Edge& edge : edges) {
            INFO(rate << " Hz: " << edge.name);
            const auto cap = edge.kind == 0 ? lsdjCap() : risaCap(edge.kind - 1);
            const auto [start, frames] = edge.kind == 0 ? lsdjWindow(size, rate, edge.offset, edge.length)
                                                        : risaWindow(size, rate, edge.offset, edge.length,
                                                                     kPalRates[edge.kind - 1]);
            const auto c = one(jobs, jobs.submit(fileRequest(path, edge.offset, edge.length, cap)).id);
            INFO(c.error);
            REQUIRE(c.ok);
            REQUIRE(c.inputs.size() == 1);
            CHECK(c.inputs[0].decodedRate == rate);
            CHECK(c.inputs[0].decodedFrames == size);
            CHECK(c.inputs[0].frames == frames);
            CHECK(c.buffers[0].frames == frames);
            CHECK(c.buffers[0].rate == double(rate));
            CHECK(c.buffers[0].samples ==
                  std::vector<float>(decoded->buffer.begin() + std::ptrdiff_t(start),
                                     decoded->buffer.begin() + std::ptrdiff_t(start + frames)));
        }
        // Not vacuous: at each rate the LSDj and the fastest risa caps cut the whole file short.
        CHECK(lsdjWindow(size, rate, 0, 0).second < size);
        CHECK(risaWindow(size, rate, 0, 0, kPalRates[15]).second < size);
    }

    // A non-finite sample is reported only where the window keeps it. A float WAV, so the NaN survives the decode.
    std::vector<float> withNan = signal(2000, 1, 1);
    withNan[1500] = std::numeric_limits<float>::quiet_NaN();
    const auto nanPath = dir.path / "nan.wav";
    writeWav(nanPath, withNan, 32000, true);
    const auto kept = one(jobs, jobs.submit(fileRequest(nanPath, 1000, 0, std::nullopt)).id);
    REQUIRE(kept.ok);
    CHECK(kept.inputs[0].nonFinite);
    const auto cut = one(jobs, jobs.submit(fileRequest(nanPath, 0, 1500, std::nullopt)).id);
    REQUIRE(cut.ok);
    CHECK_FALSE(cut.inputs[0].nonFinite);

    // An empty window is an input of no frames: reported, returned as a buffer of none at the decoded rate, and named by
    // no resource. Today's compilers make of it an LSDj slot of no bytes and a one-byte risa slot (a legal 16k+1
    // length), which a TS compiler reproduces from that report.
    const auto empty = one(jobs, jobs.submit(fileRequest(nanPath, 5000, 0, lsdjCap())).id);
    REQUIRE(empty.ok);
    CHECK(empty.inputs[0].frames == 0);
    CHECK(empty.inputs[0].decodedFrames == 2000);
    CHECK(empty.buffers[0].frames == 0);
    CHECK(empty.buffers[0].samples.empty());
    CHECK(empty.buffers[0].rate == 32000.0);
    rp::lsdj::KitUtil::SampleInput lsdj;
    lsdj.data = withNan;
    lsdj.sampleRate = 32000;
    lsdj.offset = 5000;
    CHECK(rp::lsdj::KitUtil::compileSample(lsdj).empty());
    rp::risa::CompileDmcSampleSpec spec;
    spec.offset = 5000;
    const rp::risa::RisaDmcCodec risa("K", {spec});
    rp::kit::SampleData data;
    data.buffer = withNan;
    data.sampleRate = 32000;
    CHECK(risa.encode(0, data) == std::vector<std::uint8_t>{0});
}

TEST_CASE("an offline job leaves the live plan an audio thread plays bit for bit, and its warmed blocks allocate nothing",
          "[engine-graph][offline]") {
    // Two engines play the same live plan, a Game Boy booting mGB, block for block on an audio thread of their own; one
    // of them has offline jobs running (resampling, the costliest node) the whole time. Every block's output is the
    // same, and the block thread allocates and frees nothing over its warmed blocks with the jobs in flight.
    constexpr int           kBlocks = 240;
    // Warmed past the snapshot registry's first periodic state publish (SystemBase::readStateSnapshot sizes its buffer
    // there, about block 46 of a booting Game Boy: two allocations, in both runs alike), which is the engine's, not
    // this seam's.
    constexpr int           kWarm = 60;
    constexpr std::uint32_t kFrames = 512;
    const auto play = [&](bool withJobs, rp::test::AllocationCount& counted) {
        Engine e(48000.0);
        rp::host::test::Roms roms;
        std::vector<rp::host::test::DocSystem> systems{{1, rp::host::test::kGbType}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        std::vector<float> out;
        out.reserve(std::size_t(kBlocks) * kFrames * 2);
        std::atomic<int> blocks{0};
        e.setAudioThreadOwns(true);
        std::thread audio([&] {
            std::vector<float> l(kFrames), r(kFrames);
            float*             outs[2] = {l.data(), r.data()};
            for (int b = 0; b < kBlocks; ++b) {
                if (b == kWarm) rp::test::beginCountingAllocations();
                e.drainCommands();
                e.processBlock(kFrames, outs, 2);
                for (std::uint32_t i = 0; i < kFrames; ++i) {
                    out.push_back(l[i]);
                    out.push_back(r[i]);
                }
                blocks.store(b + 1, std::memory_order_release);
            }
            counted = rp::test::endCountingAllocations();
        });
        std::vector<std::uint32_t> ids;
        if (withJobs) {
            const auto input = signal(44100, 2, 6);
            while (blocks.load(std::memory_order_acquire) < kBlocks) {
                for (unsigned i = 0; i < OfflineJobs::concurrency() && ids.size() < 48; ++i) {
                    const auto s = e.submitOfflineJob(chainRequest(input, 2, 44100.0, 11468.0, true, 0.5, 11468));
                    if (s.id) ids.push_back(s.id);
                }
                e.drainOfflineJobs(std::chrono::milliseconds(5));
            }
        }
        audio.join();
        e.setAudioThreadOwns(false);
        if (withJobs) CHECK_FALSE(ids.empty());
        return out;
    };
    rp::test::AllocationCount quiet, busy;
    const auto alone = play(false, quiet);
    const auto beside = play(true, busy);
    // Not vacuous: the plan sounds.
    double energy = 0.0;
    for (const float v : alone) energy += double(v) * v;
    CHECK(energy > 0.0);
    CHECK(beside == alone);
    CHECK(busy.allocations == quiet.allocations);
    CHECK(busy.frees == quiet.frees);
    CHECK(busy.allocations == 0);
    CHECK(busy.frees == 0);
}

TEST_CASE("offline job costs", "[.][offline-bench]") {
    // What one job costs end to end (submit to completion drained): the host built, one block, torn down; and one
    // second of 44.1 kHz audio resampled to the LSDj kit rate, recorded and returned.
    OfflineJobs jobs;
    const auto time = [&](const OfflineRequest& r, int runs) {
        double best = 1e9, total = 0.0;
        for (int i = 0; i < runs; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto c = one(jobs, jobs.submit(r).id);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            REQUIRE(c.ok);
            best = std::min(best, ms);
            total += ms;
        }
        return std::pair{best, total / runs};
    };
    const auto trivial = time(chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64), 200);
    const auto oneSecond = chainRequest(signal(44100, 1, 1), 1, 44100.0, 11468.0, true, 1.0, 11468);
    const auto resample = time(oneSecond, 50);
    std::printf("[offline-bench] trivial job: best %.3f ms, mean %.3f ms\n", trivial.first, trivial.second);
    std::printf("[offline-bench] 1 s of 44.1 kHz resampled to 11468 Hz: best %.3f ms, mean %.3f ms\n", resample.first,
                resample.second);
    // A kit's fan-out: fifteen such resamples at once on the pool, the requests made before the clock starts. The first
    // round starts the workers it needs; the second finds them parked.
    std::vector<OfflineRequest> requests;
    for (int i = 0; i < 15; ++i)
        requests.push_back(chainRequest(signal(44100, 1, i), 1, 44100.0, 11468.0, true, 1.0, 11468));
    for (const char* round : {"cold", "warm"}) {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> ids;
        for (const auto& r : requests) ids.push_back(jobs.submit(r).id);
        const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
        REQUIRE(done.size() == 15);
        std::printf("[offline-bench] 15 of them at once (%u workers, %s): %.3f ms\n", OfflineJobs::concurrency(), round,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
}
````

Run: `mkdir -p packages/native/test/engine-graph; sed -n '779,1660p' docs/modular-graph-phase-d2-plan.md > packages/native/test/engine-graph/OfflineJobs.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/engine-graph/OfflineJobs.test.cpp`
Expected: `12f2a36290f1be3034e64168d2462eda74aa665d`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/offline-run.test.ts`:

````ts
// Offline jobs over the real host (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3): backend.offlineRun
// runs a graph document to completion on a worker of the engine's offline pool and `await` settles in this host, which
// fires no timer and has no frame tick, through the poll's native wait the audio driver turns on. Samples go through
// rp.dsp.gain into a recorder and come back sample for sample; a file input comes back windowed, with its report; a
// refusal and a failure reject with native's reason; several jobs run at once, a chain of them one after another, and a
// fan-out past the engine's 64 outstanding jobs all come back.
// The pool, the threading, the windows and the live plan's isolation are held by retroplug-engine-graph-test
// "[offline]".
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { OfflineRunError, offlineInputRef, offlineOutputRef, type OfflineRunRequest } from "../src/offline";

const be = createRealBackend();
createAudioDriver(); // the poll's wait: this host has no UI tick

// Player -> gain -> recorder over res:1 into res:2, `channels` wide; with `loop`, the player wraps.
function gainDocument(gain: number, channels: number, loop = false): string {
  const layout = channels === 1 ? "mono" : "stereo";
  return JSON.stringify({
    nodes: [
      { id: "p", type: "rp.core.buffer-player", settings: { channels, layout, loop }, resources: { sample: offlineInputRef(0) } },
      { id: "g", type: "rp.dsp.gain", settings: { channels }, params: { gain } },
      { id: "rec", type: "rp.core.buffer-recorder", settings: { channels }, resources: { target: offlineOutputRef(1, 0) } },
    ],
    wires: [
      { from: "p.out", to: "g.in" },
      { from: "g.out", to: "rec.in" },
    ],
  });
}

function signal(frames: number, channels: number, seed: number): Float32Array {
  const out = new Float32Array(frames * channels);
  for (let i = 0; i < out.length; i++) out[i] = Math.fround(0.8 * Math.sin((i + 1) * 0.01 * (seed + 1)));
  return out;
}

function gainJob(input: Float32Array, channels: number, gain: number): OfflineRunRequest {
  const frames = input.length / channels;
  return {
    document: gainDocument(gain, channels),
    inputs: [{ samples: input, channels, rate: 48000 }],
    outputs: [{ channels, frames, rate: 48000 }],
    frames,
    rate: 48000,
  };
}

// A 16-bit PCM mono WAV, little-endian.
function wav(samples: number[], rate: number): Uint8Array {
  const bytes = new Uint8Array(44 + samples.length * 2);
  const v = new DataView(bytes.buffer);
  const tag = (at: number, s: string) => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)));
  tag(0, "RIFF");
  v.setUint32(4, 36 + samples.length * 2, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  v.setUint32(16, 16, true);
  v.setUint16(20, 1, true);
  v.setUint16(22, 1, true);
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 2, true);
  v.setUint16(32, 2, true);
  v.setUint16(34, 16, true);
  tag(36, "data");
  v.setUint32(40, samples.length * 2, true);
  samples.forEach((s, i) => v.setInt16(44 + i * 2, s, true));
  return bytes;
}

async function rejection(p: Promise<unknown>): Promise<Error> {
  try {
    await p;
  } catch (e) {
    return e as Error;
  }
  throw new Error("expected a rejection, got a resolution");
}

test("samples through rp.dsp.gain into a recorder come back sample for sample", async () => {
  const input = signal(3000, 2, 1);
  const result = await be.offlineRun(gainJob(input, 2, 0.5));
  expect(result.inputs).toEqual([]);
  expect(result.buffers.length).toBe(1);
  const [out] = result.buffers;
  expect(out.handle).toBe(2);
  expect(out.channels).toBe(2);
  expect(out.frames).toBe(3000);
  expect(out.rate).toBe(48000);
  expect(out.samples.length).toBe(input.length);
  let mismatches = 0;
  for (let i = 0; i < input.length; i++) if (out.samples[i] !== Math.fround(input[i] * 0.5)) mismatches++;
  expect(mismatches).toBe(0);
  expect(be.pumpJobs()).toBe(0);
});

test("a file input comes back decoded and windowed, with its report", async () => {
  const path = be.configDir() + "/offline-run.wav";
  const pcm = Array.from({ length: 3000 }, (_, i) => Math.round(12000 * Math.sin(i * 0.05)));
  expect(be.writeFile(path, wav(pcm, 22050))).toBeTruthy();
  const result = await be.offlineRun({ inputs: [{ path, offset: 1000, length: 500 }], returns: [1], frames: 0, rate: 22050 });
  expect(result.inputs).toEqual([{ handle: 1, decodedRate: 22050, decodedFrames: 3000, frames: 500, nonFinite: false }]);
  const [buffer] = result.buffers;
  expect(buffer.frames).toBe(500);
  expect(buffer.rate).toBe(22050);
  // miniaudio's 16-bit to float: the sample over 32768.
  let mismatches = 0;
  for (let i = 0; i < 500; i++) if (buffer.samples[i] !== Math.fround(pcm[1000 + i] / 32768)) mismatches++;
  expect(mismatches).toBe(0);
  // Past the end, under LSDj's cap: an empty window, reported, and a buffer of no frames.
  const empty = await be.offlineRun({
    inputs: [{ path, offset: 5000, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } }],
    returns: [1],
    frames: 0,
    rate: 22050,
  });
  expect(empty.inputs[0].frames).toBe(0);
  expect(empty.buffers[0].samples.length).toBe(0);
  be.deleteFile(path);
});

test("a refusal and a failure reject with native's reason", async () => {
  const refused = await rejection(be.offlineRun({ outputs: [{ channels: 1, frames: 0, rate: 48000 }], frames: 1, rate: 48000 }));
  expect(refused.message).toBe("output 0 needs at least one channel and one frame");
  const failed = await rejection(
    be.offlineRun({ document: JSON.stringify({ nodes: [{ id: "gb", type: "rp.sameboy.gb" }], wires: [] }), frames: 64, rate: 48000 }),
  );
  expect(failed instanceof OfflineRunError).toBe(true);
  expect(failed.message).toBe('the document was refused: gb: unknown-type: no node type "rp.sameboy.gb" is registered');
  const missing = await rejection(be.offlineRun({ inputs: [{ path: "/nonexistent/rp.wav" }], frames: 0, rate: 48000 }));
  expect(missing.message).toBe("input 0: /nonexistent/rp.wav could not be read or decoded");
  expect((missing as OfflineRunError).inputs).toEqual([]);
});

test("several jobs run at once, and a chain of them one after another", async () => {
  const input = signal(2000, 1, 2);
  const gains = [0.25, 0.5, 1, 2, -1, 0.125];
  const results = await Promise.all(gains.map((g) => be.offlineRun(gainJob(input, 1, g))));
  results.forEach((r, k) => {
    let mismatches = 0;
    for (let i = 0; i < input.length; i++) if (r.buffers[0].samples[i] !== Math.fround(input[i] * gains[k])) mismatches++;
    expect(mismatches).toBe(0);
  });
  // Each job's output the next one's input: three halvings.
  let buffer = input;
  for (let i = 0; i < 3; i++) buffer = (await be.offlineRun(gainJob(buffer, 1, 0.5))).buffers[0].samples;
  expect(buffer[100]).toBe(Math.fround(input[100] * 0.125));
  expect(be.pumpJobs()).toBe(0);
});

test("a fan-out past the engine's bound of outstanding jobs is held back, and every job comes back", async () => {
  // Native refuses a 65th outstanding job as busy; offlineRun holds the rest in TS and submits them as room comes.
  // Each job runs long enough (ten minutes of a looping player, recorded for its first 256 frames) that the
  // submissions outrun the completions and the bound is reached.
  const count = 150;
  const input = signal(256, 1, 3);
  const long = (k: number): OfflineRunRequest => ({
    ...gainJob(input, 1, k / 64),
    document: gainDocument(k / 64, 1, true),
    frames: 48000 * 600,
  });
  const results = await Promise.all(Array.from({ length: count }, (_, k) => be.offlineRun(long(k))));
  expect(results.length).toBe(count);
  let mismatches = 0;
  results.forEach((r, k) => {
    for (let i = 0; i < input.length; i++) if (r.buffers[0].samples[i] !== Math.fround(input[i] * Math.fround(k / 64))) mismatches++;
  });
  expect(mismatches).toBe(0);
  expect(be.pumpJobs()).toBe(0);
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '1672,1842p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test-native/offline-run.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/offline-run.test.ts`
Expected: `4c7e7cbe8f4a7fe06be2fcc2c40041272875d4f0`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/offline/offline.test.ts`:

````ts
// Offline jobs as promises (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3): the mock backend rejects
// every job, since the mock tier runs no graph; and the promise side over a virtual-time port: a refusal rejects at
// once with native's reason, a failure with an OfflineRunError carrying the inputs' reports, several jobs submitted in
// one run are all in flight at once and settle through the poll's native wait with no timer, a chain of jobs (direct, or
// through extra async layers) keeps moving while a long job runs, a fan-out past native's bound is held back rather than
// refused, a job native no longer holds rejects rather than being polled forever, the shared pump (pumpAllJobs, as the
// UI tick's pumpJobs) settles them with the wait off, and the wire request carries the handles and the default returns.
// The native half is held by retroplug-engine-graph-test "[offline]" and test-native/offline-run.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { liveJobCount, pumpAllJobs } from "../../src/capability";
import {
  OFFLINE_MAX_JOBS,
  OfflineJobs,
  OfflineRunError,
  offlineInputRef,
  offlineOutputRef,
  setOfflineWait,
  type OfflineRunRequest,
  type OfflineSubmitResult,
  type OfflineWireCompletion,
  type OfflineWireDrain,
  type OfflineWireRequest,
} from "../../src/offline";

// A port over a virtual clock, as native behaves: a job runs for `frames` time units from its submission (0: done at
// once), a drain that waits advances the clock to the next job's end when none has finished, a drain that does not
// wait never moves it, at most OFFLINE_MAX_JOBS are outstanding (submitted and not yet drained) and one past that is
// refused busy. A job echoes its first input's samples, or fails for a document of "fail".
function clockPort() {
  let now = 0;
  let next = 1;
  const jobs: { id: number; request: OfflineWireRequest; end: number }[] = [];
  const log: string[] = [];
  const waits: number[] = [];
  const submitted: OfflineWireRequest[] = [];
  let most = 0;
  const port = {
    log,
    waits,
    submitted,
    now: () => now,
    advance: (t: number) => (now += t),
    mostInFlight: () => most,
    submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult {
      if (request.rate <= 0) return { id: null, error: "the job's sample rate must be a positive number" };
      if (jobs.length >= OFFLINE_MAX_JOBS) return { id: null, error: `busy: ${OFFLINE_MAX_JOBS} offline jobs are outstanding` };
      submitted.push(request);
      jobs.push({ id: next, request, end: now + request.frames });
      log.push(`t=${now} submit ${request.document ?? ""}`);
      most = Math.max(most, jobs.length);
      return { id: next++, error: null };
    },
    drainOfflineJobs(waitMs: number): OfflineWireDrain {
      waits.push(waitMs);
      if (waitMs > 0 && jobs.length && !jobs.some((j) => j.end <= now)) now = Math.min(...jobs.map((j) => j.end));
      const done: OfflineWireCompletion[] = [];
      for (const j of jobs.filter((x) => x.end <= now)) {
        const report = { handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true };
        if (j.request.document === "fail") {
          done.push({ id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] });
          continue;
        }
        const echoed = j.request.inputs[0]?.samples ?? new Uint8Array(0);
        log.push(`t=${now} done ${j.request.document ?? ""}`);
        done.push({
          id: j.id,
          ok: true,
          error: "",
          buffers: [{ handle: 1, channels: 1, frames: echoed.byteLength / 4, rate: 48000, samples: echoed }],
          inputs: [],
        });
      }
      for (const d of done) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
      return { completions: done, inFlight: jobs.length };
    },
  };
  return port;
}

const samples = (...v: number[]) => ({ samples: Float32Array.from(v), channels: 1, rate: 48000 });
const job = (document: string, frames: number, ...v: number[]): OfflineRunRequest => ({
  document,
  inputs: [samples(...v)],
  frames,
  rate: 48000,
});

// `body` with the poll's wait on, as a host with no UI tick runs it.
async function waiting<T>(body: () => Promise<T>): Promise<T> {
  setOfflineWait(7);
  try {
    return await body();
  } finally {
    setOfflineWait(0);
  }
}

test("the mock backend rejects every offline job: only a native host runs graphs", async () => {
  const be = new MockBackend("/cfg");
  let message = "";
  await be.offlineRun({ frames: 64, rate: 48000 }).catch((e: Error) => (message = e.message));
  expect(message).toBe("offline jobs need a native host: the mock backend cannot run graphs");
  expect(be.pumpJobs()).toBe(0);
});

test("a refusal rejects at once, and a failure rejects with its reason and the inputs' reports", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  let refused = "";
  await jobs.run({ frames: 64, rate: 0 }).catch((e: Error) => (refused = e.message));
  expect(refused).toBe("the job's sample rate must be a positive number");
  // Completed at the pump right after its submission: no wait, no tick.
  const failed = jobs.run({ document: "fail", inputs: [{ path: "/x.wav" }], frames: 0, rate: 48000 });
  let error: OfflineRunError | null = null;
  await failed.catch((e: OfflineRunError) => (error = e));
  expect(error!.message).toBe("a node faulted: f");
  expect(error! instanceof OfflineRunError).toBe(true);
  expect(error!.inputs).toEqual([{ handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true }]);
  expect(jobs.pendingCount()).toBe(0);
});

test("jobs submitted together are all in flight at once, and settle through the poll's wait with no timer", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  await waiting(async () => {
    const all = Promise.all([jobs.run(job("a", 3, 1, 2)), jobs.run(job("b", 1, 3)), jobs.run(job("c", 2, 4, 5, 6))]);
    expect(port.mostInFlight()).toBe(3); // every submission made before any wait
    const results = await all;
    expect(results.map((r) => Array.from(r.buffers[0].samples))).toEqual([[1, 2], [3], [4, 5, 6]]);
    expect(port.waits.includes(7)).toBe(true); // the poll waited in native
    expect(port.now()).toBe(3); // and only as long as the longest job
    // A chain: each job submitted as the one before it settles.
    let value = 1;
    for (let i = 0; i < 4; i++) value = (await jobs.run(job("chain", 1, value + 1))).buffers[0].samples[0];
    expect(value).toBe(5);
  });
  expect(jobs.pendingCount()).toBe(0);
  expect(liveJobCount()).toBe(0);
});

test("a chain awaiting through extra async layers keeps moving while a long job runs", async () => {
  // A slot's chain as a compiler writes it: each step an async function around offlineRun, its result awaited
  // through a further layer, so the next submission comes several microtask turns after the completion. A long job runs
  // beside it the whole time. The short chain's three jobs must all run before the long job ends, rather than one per
  // completion of something else.
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const step = async (k: number) => {
    const r = await jobs.run(job(`short-${k}`, 1, k));
    await Promise.resolve();
    return r.buffers[0].samples[0];
  };
  const slot = async () => {
    for (let k = 0; k < 3; k++) await (async () => step(k))();
  };
  await waiting(async () => {
    const long = jobs.run(job("long", 50, 0));
    await slot();
    expect(port.now() <= 3).toBe(true);
    await long;
  });
  const order = port.log.filter((l) => l.includes("done")).map((l) => l.replace(/^t=\d+ done /, ""));
  expect(order).toEqual(["short-0", "short-1", "short-2", "long"]);
  expect(port.now()).toBe(50);
});

test("a fan-out past native's bound is held back, never refused busy", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const count = OFFLINE_MAX_JOBS * 2 + 7;
  const results = await waiting(() =>
    Promise.all(Array.from({ length: count }, (_, i) => jobs.run(job(`fan-${i}`, 1 + (i % 5), i)))),
  );
  expect(results.length).toBe(count);
  expect(results.every((r, i) => r.buffers[0].samples[0] === i)).toBe(true);
  expect(port.mostInFlight()).toBe(OFFLINE_MAX_JOBS);
  expect(port.submitted.length).toBe(count);
  expect(jobs.pendingCount()).toBe(0);
  // With the wait off, the pump submits what is held as room comes.
  const later = Array.from({ length: OFFLINE_MAX_JOBS + 3 }, (_, i) => jobs.run(job("later", 1, i)));
  expect(jobs.pendingCount()).toBe(OFFLINE_MAX_JOBS + 3);
  port.advance(1);
  pumpAllJobs();
  port.advance(1);
  pumpAllJobs();
  expect((await Promise.all(later)).length).toBe(OFFLINE_MAX_JOBS + 3);
  expect(jobs.pendingCount()).toBe(0);
});

test("a job native no longer holds rejects, rather than being polled forever", async () => {
  // A port that takes a job and then holds no record of it: nothing comes back and nothing is in flight.
  const jobs = new OfflineJobs({
    submitOfflineJob: () => ({ id: 1, error: null }),
    drainOfflineJobs: () => ({ completions: [], inFlight: 0 }),
  });
  let message = "";
  await waiting(() => jobs.run(job("lost", 1)).catch((e: Error) => (message = e.message)));
  expect(message).toBe("the engine holds no record of this offline job");
  expect(liveJobCount()).toBe(0);
});

test("with the wait off, the shared pump settles offline jobs as the UI tick's does", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  let settled = false;
  const p = jobs.run(job("tick", 2, 9)).then(() => (settled = true));
  expect(liveJobCount()).toBe(1);
  port.advance(1);
  pumpAllJobs();
  expect(jobs.pendingCount()).toBe(1);
  port.advance(1);
  pumpAllJobs();
  expect(port.waits.every((w) => w === 0)).toBe(true); // never a wait: this thread may be a UI's
  await p;
  expect(settled).toBe(true);
  expect(liveJobCount()).toBe(0);
});

test("a request names its inputs then its outputs, and returns every output by default", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const settled = jobs.run({
    document: "{}",
    inputs: [samples(1), { path: "/a.wav", offset: 3, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } }],
    outputs: [{ channels: 1, frames: 8, rate: 11468 }, { channels: 2, frames: 8, rate: 11468 }],
    frames: 8,
    rate: 11468,
  });
  const wire = port.submitted[0];
  port.advance(8);
  expect(jobs.pump()).toBe(0); // settled, so no queue is left live after the last case
  await settled;
  expect(wire.returns).toEqual([3, 4]);
  expect(wire.inputs[1]).toEqual({ path: "/a.wav", offset: 3, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } });
  expect(wire.inputs[0].channels).toBe(1);
  expect(offlineInputRef(1)).toBe("res:2");
  expect(offlineOutputRef(2, 1)).toBe("res:4");
});
````

Run: `mkdir -p packages/retroplug/test/offline; sed -n '1854,2092p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test/offline/offline.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/offline/offline.test.ts`
Expected: `da10705ddae017f0e52c3b02905e1a77d29a404c`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index ddc641c18..7b6a4f227 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -86,8 +86,9 @@ retroplug_add_test_binary(
 # adapters, down to test/n8/FakeN8.hpp's serial port), TS nodes run behind the C ABI in the script host, a script
 # node pack loads beside the built-in one into runtimes of its own and reloads, the generation it replaced freed off
 # the audio thread, the protocol script nodes send what the DSP kernel they replaced
-# was recorded sending, and every document in buildGraph's goldens, its role goldens among them, plans on it.
-# A hidden bench times a script node per block.
+# was recorded sending, every document in buildGraph's goldens, its role goldens among them, plans on it, and an
+# offline job (docs/modular-graph-phase-d2-design.md section 3) runs a document to completion on a worker host of its
+# own, apart from the live plan. A hidden bench times a script node per block, another an offline job.
 # Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
 # CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
 # (the authoring layer for their toy nodes) is embedded here alone: cmake/bundles.cmake, retroplug-script-test-bundle.
@@ -101,6 +102,7 @@ retroplug_add_test_binary(
              test/engine-graph/HostIo.test.cpp
              test/engine-graph/Devices.test.cpp        # rp.host.devices/1: the links a hardware node binds by name
              test/engine-graph/Capabilities.test.cpp   # rp.system.memory/1, sram/1, rom/1 as asynchronous jobs
+             test/engine-graph/OfflineJobs.test.cpp    # a document run to completion on a worker host of its own
              test/engine-graph/N8Node.test.cpp         # rp.n8.nes: the real cart as a node, its jobs on the link
              test/engine-graph/N8Adapters.test.cpp     # rp.n8.nes through N8CartLink / N8Host to a fake serial port
              test/engine-graph/Surface.test.cpp        # rp.launchpad.surface-in / -out: the control surface as nodes
diff --git a/packages/native/node/test/ops.mjs b/packages/native/node/test/ops.mjs
index 795e30cef..0cace9e52 100644
--- a/packages/native/node/test/ops.mjs
+++ b/packages/native/node/test/ops.mjs
@@ -39,12 +39,13 @@ export function payload() {
 }
 
 /**
- * Run the host-facet operation matrix against `send` (a synchronous __rpcSend) using `tmp` as a
- * scratch directory. Returns [label, canonicalResult] pairs in a fixed order.
+ * Run the operation matrix (the host facet, and an offline job on the emulator facet) against `send` (a synchronous
+ * __rpcSend) using `tmp` as a scratch directory. Returns [label, canonicalResult] pairs in a fixed order.
  *
  * Coverage, in codec terms: string/int/bool scalars, an empty std::optional, rfl::Bytestring in BOTH
  * directions, std::vector<std::string>, std::vector<struct> with a nested Bytestring, a struct
- * parameter with a nested Bytestring, and the JSON-RPC error envelope.
+ * parameter with a nested Bytestring, a struct of vectors of structs with absent optionals and a nested optional
+ * struct (an offline job's request) and its completion's nested vectors of structs, and the JSON-RPC error envelope.
  */
 export function runOps(send, tmp) {
     let id = 0;
@@ -104,6 +105,71 @@ export function runOps(send, tmp) {
     rec("writeFile:empty", call("writeFile", emptyPath, new Uint8Array(0)));
     rec("readFile:empty", call("readFile", emptyPath));
 
+    // An offline job: a struct of vectors of structs IN (optional fields absent, a nested optional struct, interleaved
+    // float32 as a Bytestring) -> an optional-field answer OUT -> vectors of structs with nested Bytestrings OUT. A
+    // samples input through rp.dsp.gain into a recorder, beside a windowed file input (a 16-bit WAV written here).
+    const wav = new Uint8Array(44 + 64 * 2);
+    const dv = new DataView(wav.buffer);
+    [..."RIFF"].forEach((c, i) => dv.setUint8(i, c.charCodeAt(0)));
+    dv.setUint32(4, 36 + 128, true);
+    [..."WAVEfmt "].forEach((c, i) => dv.setUint8(8 + i, c.charCodeAt(0)));
+    dv.setUint32(16, 16, true);
+    dv.setUint16(20, 1, true);
+    dv.setUint16(22, 1, true);
+    dv.setUint32(24, 22050, true);
+    dv.setUint32(28, 44100, true);
+    dv.setUint16(32, 2, true);
+    dv.setUint16(34, 16, true);
+    [..."data"].forEach((c, i) => dv.setUint8(36 + i, c.charCodeAt(0)));
+    dv.setUint32(40, 128, true);
+    for (let i = 0; i < 64; i++) dv.setInt16(44 + 2 * i, (i * 997) % 20000 - 10000, true);
+    const wavPath = tmp + "/offline.wav";
+    call("writeFile", wavPath, wav);
+    const floats = new Float32Array(256);
+    for (let i = 0; i < floats.length; i++) floats[i] = ((i * 37) % 101) / 101 - 0.5;
+    const doc = JSON.stringify({
+        nodes: [
+            { id: "p", type: "rp.core.buffer-player", settings: { channels: 1, layout: "mono", loop: false }, resources: { sample: "res:1" } },
+            { id: "g", type: "rp.dsp.gain", settings: { channels: 1 }, params: { gain: 0.25 } },
+            { id: "rec", type: "rp.core.buffer-recorder", settings: { channels: 1 }, resources: { target: "res:3" } },
+        ],
+        wires: [{ from: "p.out", to: "g.in" }, { from: "g.out", to: "rec.in" }],
+    });
+    const submitted = call("submitOfflineJob", {
+        document: doc,
+        inputs: [
+            { samples: new Uint8Array(floats.buffer), channels: 1, rate: 48000 },
+            { path: wavPath, offset: 8, length: 0, cap: { frames: 40, rate: 11468, arithmetic: "float-ratio" } },
+        ],
+        outputs: [{ channels: 1, frames: 256, rate: 48000 }],
+        returns: [3, 2],
+        frames: 256,
+        rate: 48000,
+    });
+    rec("submitOfflineJob", { taken: typeof submitted.id === "number", error: submitted.error });
+    // How many drains it takes is timing, so only what holds on every host is recorded: the job was in flight until
+    // it came back, and nothing was after.
+    let completions = [];
+    const inFlight = [];
+    for (let i = 0; i < 100 && completions.length === 0; i++) {
+        const drained = call("drainOfflineJobs", 100);
+        inFlight.push(drained.inFlight);
+        completions = drained.completions;
+    }
+    rec("drainOfflineJobs", completions.map((c) => ({ ...c, id: c.id === submitted.id })));
+    rec("drainOfflineJobs:inFlight", {
+        last: inFlight[inFlight.length - 1],
+        before: inFlight.slice(0, -1).every((n) => n === 1),
+    });
+    rec("submitOfflineJob:refused", call("submitOfflineJob", {
+        inputs: [{ path: wavPath, cap: { frames: 1, rate: 1, arithmetic: "nope" } }],
+        outputs: [],
+        returns: [],
+        frames: 0,
+        rate: 48000,
+    }));
+    rec("cancelOfflineJob:none", call("cancelOfflineJob", 12345));
+
     // error envelope
     rec("error:unknownMethod", raw("noSuchMethod"));
     // wrong parameter type - the reader's cast failure, surfaced as an rpc error
````

Run: `sed -n '2102,2218p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: the configure exits 0 (it picked up the new test source), then the build stops compiling `OfflineJobs.test.cpp`: `host/engine/OfflineJobs.hpp` does not exist yet. That is this seam's red.

Run: `pnpm test offline`
Expected: every case in `offline.test.ts` errors before it runs: `src/offline` cannot be resolved, because the promise side is what Step 3 writes.

- [ ] **Step 2: The native seam**

`OfflineJobs.{hpp,cpp}` is the pool and its types: `OfflineJobs` owns the queue, the running list, the completions,
the workers and its own `NodeRegistry`; `OfflineRequest` / `OfflineCompletion` describe a job and its outcome;
`offlineWindow()` is the file window as a free function so it is testable on its own. A `Job` is popped by a worker
and owned by it until completion; its `ResourceTable`, inputs, outputs, host, plan and node instances are locals of
`OfflineJobs::run`, the table declared before the host so the host is destroyed first, on that thread; planning,
activation, every block (`GraphHost::processBlock`, quiescent), the fault check and the copy-out all run there.
`submit` validates and queues, `drain(wait, inFlight*)` moves completions out under the lock, `cancel` removes a
queued job or sets a running one's flag, and the destructor sets `stopping_`, drops queued jobs, flags running ones,
wakes and joins every worker. `Engine` gains the pool as a `std::unique_ptr` made at the first submission and reset
first in `~Engine`, plus the four forwarding methods. `BackendTypes.hpp` carries the wire types,
`EngineRpcService.{hpp,cpp}` the three methods, and `BackendRpcRegistration.hpp` registers them in
`registerEmulatorRpc` (15 -> 18, the union 89 -> 92): every host that binds that facet gets them with no host change.
`CMakeLists.txt` puts `OfflineJobs.cpp` in `retroplug-backend`.

Create `packages/native/src/host/engine/OfflineJobs.hpp`:

````cpp
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "graph/NodeRegistry.hpp"
#include "graph/rp_node.h"

// Offline jobs (docs/modular-graph-phase-d2-design.md section 3): a graph document run to completion on a host of its
// own, apart from the live engine, with the buffers it recorded handed back. A job is submitted with its inputs (audio
// the submission carries, or a file native decodes and windows), the empty buffers it records into, the buffers to
// return and the frames to run; it is taken or refused at once, runs on a worker thread, and its completion is
// collected later from a bounded queue the control plane polls, as a capability job's is (CapabilityJobs.hpp).
//
// Threads and ownership. The pool (an OfflineJobs) is the engine's, made on the control thread at the first submission
// and joined as the engine goes. Its node registry is its own, filled in its constructor before any worker exists and
// never changed after, so the workers share it read-only (NodeRegistry's rule: concurrent find() is safe once filling
// is over); it holds the built-in C++ packages a job's document may name (rp.core, rp.dsp, rp.dsp.resample) and
// nothing of the engine's, so a script pack loading, reloading or freed, which swaps the ENGINE's registry entries on
// the control thread, can never race a job's planning: no job can see that registry. A script node, a system node or
// a host I/O node in a job's document is refused by the planner as an unknown type. The packages themselves are static
// objects that outlive every engine.
//
// Each job runs on one worker thread from start to end, which is the job's control thread and its block thread at
// once: the worker builds the job's ResourceTable and its inputs and outputs in it, builds a GraphHost over the pool's
// registry and that table (quiescent: no audio thread, so setGraph flushes inline and run() is the block driver, as
// the graph tests drive it), plans the document, runs `frames` frames in blocks of kBlockFrames (the last one
// partial), checking for a cancel between blocks, tears the host down, copies the returned buffers out and frees the
// table, all on that thread. So every node a job builds, a resampler and its r8brain objects among them, is destroyed
// inside its host's life and before the pool's destructor returns, which an engine reaches before the program's
// statics go (docs/modular-graph-phase-d1-design.md section 8). A job never touches the engine's plan, its command
// ring, its resource table or its audio thread: the only state a worker shares is this pool's queue and completions,
// under its mutex.
//
// The workers are started as jobs need them, up to concurrency() (the machine's hardware threads, at least one, at
// most kMaxWorkers), so a host that never submits has no thread at all; jobs past that wait in submission order. A
// worker never retires on its own: once started it stays parked between jobs until the pool goes, so an engine that ran
// one kit compile keeps up to kMaxWorkers idle threads for the rest of its life (each a parked wait, costing no CPU).
// At most kMaxJobs are outstanding at once, queued, running, or finished and not yet drained, so every job taken has
// room for its completion and none is ever dropped.
//
// Resources. A job's table is fresh, so its handles are known before it runs: input i is res:(i + 1) and output j is
// res:(inputs + j + 1), in the order the submission lists them, which is how a document names them. An input of no
// frames (a window that comes out empty) is an empty buffer: createAudioBuffer refuses zero frames, so its handle is
// taken by a placeholder freed at once, names no resource (a document binding it is refused as unknown-resource), and
// returns as a buffer of no frames. An OUTPUT of no frames is refused at submission, so a caller whose chain meets an
// empty buffer (a window that came out empty, a resample whose length comes out 0) does not submit the jobs after it:
// it knows what they would make. Every buffer is planar inside the job, interleaved in a completion.
namespace rp::host {

// How a file input's frame cap scales a frame count at a kit rate back to the decoded rate, reproducing the two kit
// windows exactly, both of which depend on the decoded rate a submission cannot know:
//   - FloatRatio, LSDj's (KitUtil::compileSample): ceil(float(frames) * (float(decodedRate) / float(rate))), every
//     step in float, the ratio first;
//   - DoubleScale, risa's (RisaDmcCodec::encode): ceil(double(frames) * double(decodedRate) / rate), in double, the
//     product first.
enum class OfflineCapArithmetic : std::uint8_t { FloatRatio, DoubleScale };

struct OfflineCap {
    std::uint32_t        frames = 0;   // at `rate`
    double               rate = 0.0;
    OfflineCapArithmetic arithmetic = OfflineCapArithmetic::FloatRatio;
};

// Audio the submission carries: interleaved, `channels` wide, at `rate`.
struct OfflineSamples {
    std::vector<float> samples;
    std::uint32_t      channels = 0;
    double             rate = 0.0;
};

// A file decoded through SampleCache (miniaudio, mono float at the file's own rate, the kit compiler's decode) and
// windowed: from `offset` (clamped to the end), `length` frames (0: to the end), at most the cap's frames.
struct OfflineFile {
    std::string               path;
    std::uint32_t             offset = 0;
    std::uint32_t             length = 0;
    std::optional<OfflineCap> cap;
};

using OfflineInput = std::variant<OfflineSamples, OfflineFile>;

// An empty buffer for a recorder to write.
struct OfflineOutput {
    std::uint32_t channels = 0;
    std::uint32_t frames = 0;
    double        rate = 0.0;
};

struct OfflineRequest {
    std::string                document;  // a graph document; empty for none (the job returns its inputs)
    std::vector<OfflineInput>  inputs;
    std::vector<OfflineOutput> outputs;
    std::vector<std::uint32_t> returns;   // the handles whose buffers the completion carries, in this order
    std::uint32_t              frames = 0;
    double                     rate = 48000.0;  // the plan's rate
};

// The frames a window keeps of a decoded buffer, as the kit compilers cut it: `start` is the offset clamped to the
// end, and `frames` the least of the length (all of what is left when 0), what is left, and the cap at
// `decodedRate`. An offset at or past the end, or a cap of 0 frames, keeps nothing; a zero length is no limit.
struct OfflineWindow {
    std::size_t start = 0;
    std::size_t frames = 0;
};
OfflineWindow offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
                            std::uint32_t length, const std::optional<OfflineCap>& cap);

// submit's answer: the job's id (counting up from 1, never reused), or 0 with the refusal in `error`.
struct OfflineSubmit {
    std::uint32_t id = 0;
    std::string   error;
};

// A buffer a completion returns: interleaved, `channels` x `frames`.
struct OfflineBuffer {
    std::uint32_t      handle = 0;
    std::uint32_t      channels = 0;
    std::uint32_t      frames = 0;
    double             rate = 0.0;
    std::vector<float> samples;
};

// What a file input decoded to and kept: the decoded rate and length, the window's length, and whether any sample the
// window kept is not finite (a kit compile refuses such a slot).
struct OfflineInputReport {
    std::uint32_t handle = 0;
    std::uint32_t decodedRate = 0;
    std::uint32_t decodedFrames = 0;
    std::uint32_t frames = 0;
    bool          nonFinite = false;
};

// A finished job: ok with the buffers `returns` named, or the reason it failed. `inputs` reports each file input
// decoded before the job ended, a failed job's included.
struct OfflineCompletion {
    std::uint32_t                   id = 0;
    bool                            ok = false;
    std::string                     error;
    std::vector<OfflineBuffer>      buffers;
    std::vector<OfflineInputReport> inputs;
};

class OfflineJobs {
public:
    static constexpr std::size_t   kMaxJobs = 64;        // outstanding: queued, running, or finished and not drained
    static constexpr std::size_t   kMaxResources = 64;   // inputs plus outputs, per job
    static constexpr unsigned      kMaxWorkers = 8;
    static constexpr std::uint32_t kBlockFrames = 1024;  // the block every job runs in, the last one partial
    // How long one drain may wait for a completion, whatever it is asked.
    static constexpr std::chrono::milliseconds kMaxDrainWait{1000};

    // The node types a job's document may name: the built-in C++ packages, then `extra` (a test's own), registered
    // here, before any worker runs, and never changed. A package the registry refuses is logged and left out.
    explicit OfflineJobs(std::span<const rp_package* const> extra = {});
    // Cancels every job: the queued ones are dropped unrun, the running ones stop at their next block boundary; then
    // joins every worker. Completions not yet drained go with the pool.
    ~OfflineJobs();
    OfflineJobs(const OfflineJobs&) = delete;
    OfflineJobs& operator=(const OfflineJobs&) = delete;

    // The workers the pool runs jobs on at most: std::thread::hardware_concurrency(), at least 1, at most kMaxWorkers.
    static unsigned concurrency();

    // ---- any thread (the engine calls them on its control thread) ----
    // Take a job, or refuse it at once (id 0, the reason in `error`): a rate GraphHost refuses, more than kMaxResources
    // inputs and outputs, an input of samples with no channels, a sample count that is not a whole number of frames or
    // a rate that is not a positive number, a file input with no path or a cap whose rate is not a positive number, an
    // output createAudioBuffer would refuse, a return naming no handle of the job, kMaxJobs outstanding, and a worker
    // that cannot be started. What fails only once the job runs (a file that will not decode, a document the planner
    // refuses, a node that will not activate or faults, a cancel) is its completion's error.
    OfflineSubmit submit(OfflineRequest request);
    // The completions finished since the last drain, oldest first. With `wait`, and none finished, waits up to `wait`
    // (at most kMaxDrainWait) for one, returning at once when no job is queued or running; never waits otherwise.
    // `inFlight`, when given, is set to the jobs still queued or running after it, read under the same lock.
    std::vector<OfflineCompletion> drain(std::chrono::milliseconds wait = std::chrono::milliseconds(0),
                                         std::size_t* inFlight = nullptr);
    // Cancel job `id`: a queued one completes now, unrun; a running one stops at its next block boundary (or as it
    // finishes, if it is past its last). Either completes with the reason "cancelled". False for an id that is
    // neither queued nor running (finished, or never taken).
    bool cancel(std::uint32_t id);
    // Jobs outstanding: queued, running, or finished and not yet drained.
    std::size_t outstanding() const;
    // Worker threads started so far (never more than concurrency()).
    std::size_t workers() const;
    // The node types a job's document may name.
    const graph::NodeRegistry& registry() const { return registry_; }

private:
    struct Job {
        std::uint32_t     id = 0;
        OfflineRequest    request;
        std::atomic<bool> cancelled{false};
    };

    void              workerLoop();
    // Runs the job, and releases its request's samples once the job's table holds them.
    OfflineCompletion run(Job& job);

    // Filled by the constructor and never changed: the workers read it without the lock.
    graph::NodeRegistry registry_;

    mutable std::mutex                mutex_;
    std::condition_variable           work_;      // a worker waits for a job, or for the pool to stop
    std::condition_variable           finished_;  // a drain waits for a completion
    std::deque<std::unique_ptr<Job>>  queue_;
    std::vector<Job*>                 running_;   // owned by the worker running each
    std::deque<OfflineCompletion>     completions_;
    std::size_t                       outstanding_ = 0;
    std::size_t                       idle_ = 0;  // workers parked, waiting for a job
    std::vector<std::thread>          workers_;
    std::uint32_t                     nextId_ = 1;
    bool                              stopping_ = false;
};

}  // namespace rp::host
````

Run: `mkdir -p packages/native/src/host/engine; sed -n '2249,2476p' docs/modular-graph-phase-d2-plan.md > packages/native/src/host/engine/OfflineJobs.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/src/host/engine/OfflineJobs.hpp`
Expected: `150b09b960040ccaa6fed95e8c25926e8a35b121`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/src/host/engine/OfflineJobs.cpp`:

````cpp
#include "host/engine/OfflineJobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string_view>
#include <system_error>
#include <utility>

#include "graph/GraphDocument.hpp"
#include "graph/GraphHost.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/nodes/DspResampleNodes.hpp"
#include "kit/SampleCache.hpp"

namespace rp::host {

namespace {

bool positiveRate(double rate) { return rate > 0.0 && std::isfinite(rate); }

// The planner's errors as one line, each "node.port: code: message" as the engine's refusals read.
std::string describe(const std::vector<graph::PlanError>& errors) {
    std::string s;
    for (const auto& e : errors) {
        if (!s.empty()) s += "; ";
        if (!e.nodeId.empty()) s += e.nodeId + (e.port.empty() ? "" : "." + e.port) + ": ";
        s += e.code + ": " + e.message;
    }
    return s;
}

// Why createAudioBuffer would refuse this shape, or nullopt.
std::optional<std::string> bufferRefusal(std::uint64_t channels, std::uint64_t frames, double rate) {
    if (channels == 0 || frames == 0) return std::string("needs at least one channel and one frame");
    if (!positiveRate(rate)) return std::string("needs a sample rate that is a positive number");
    if (channels * frames > graph::ResourceTable::kMaxAudioSamples)
        return "is larger than " + std::to_string(graph::ResourceTable::kMaxAudioSamples) + " samples";
    return std::nullopt;
}

// A handle that names nothing: an input of no frames takes its place in the table's count, so the handles after it
// stay where the submission put them, and is freed at once (a byte resource no pin holds goes with its owner's drop).
graph::ResourceTable::Id reservePlaceholder(graph::ResourceTable& table) {
    const auto id = table.createBytes("bytes", {});
    table.drop(id);
    return id;
}

}  // namespace

OfflineWindow offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
                            std::uint32_t length, const std::optional<OfflineCap>& cap) {
    // Each step as the kit windows spell it (KitUtil::compileSample, RisaDmcCodec::encode), the casts included: the cap
    // is the one place their arithmetic differs, and a different rounding there is a different frame count.
    std::size_t maxFrames = decodedFrames;
    if (cap) {
        if (cap->arithmetic == OfflineCapArithmetic::FloatRatio) {
            const float ratio = static_cast<float>(decodedRate) / static_cast<float>(cap->rate);
            maxFrames = static_cast<std::size_t>(std::ceil(static_cast<float>(cap->frames) * ratio));
        } else {
            maxFrames = static_cast<std::size_t>(
                std::ceil(static_cast<double>(cap->frames) * static_cast<double>(decodedRate) / cap->rate));
        }
    }
    const std::size_t length0 = length == 0 ? decodedFrames : length;
    const std::size_t start = std::min<std::size_t>(offset, decodedFrames);
    return {start, std::min(maxFrames, std::min(length0, decodedFrames - start))};
}

OfflineJobs::OfflineJobs(std::span<const rp_package* const> extra) {
    const auto add = [this](const rp_package* p) {
        std::string why;
        if (!registry_.addPackage(p, &why))
            std::fprintf(stderr, "[offline] a node package was refused (%s); its types are not registered\n",
                         why.c_str());
    };
    for (const rp_package* p : {graph::corePackage(), graph::dspPackage(), graph::dspResamplePackage()}) add(p);
    for (const rp_package* p : extra) add(p);
}

OfflineJobs::~OfflineJobs() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();  // unrun: nothing of theirs was built
        for (Job* job : running_) job->cancelled.store(true, std::memory_order_release);
        workers.swap(workers_);
    }
    work_.notify_all();
    finished_.notify_all();
    for (std::thread& t : workers) t.join();
}

unsigned OfflineJobs::concurrency() {
    return std::clamp(std::thread::hardware_concurrency(), 1u, kMaxWorkers);
}

OfflineSubmit OfflineJobs::submit(OfflineRequest request) {
    const auto refuse = [](std::string why) { return OfflineSubmit{0, std::move(why)}; };
    if (auto why = graph::checkConfig(request.rate, kBlockFrames)) return refuse("the job's " + *why);
    const std::size_t handles = request.inputs.size() + request.outputs.size();
    if (handles > kMaxResources)
        return refuse("a job takes at most " + std::to_string(kMaxResources) + " inputs and outputs, not " +
                      std::to_string(handles));
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        const std::string name = "input " + std::to_string(i);
        if (const auto* s = std::get_if<OfflineSamples>(&request.inputs[i])) {
            if (s->channels == 0) return refuse(name + " has no channels");
            if (s->samples.size() % s->channels != 0)
                return refuse(name + " holds " + std::to_string(s->samples.size()) +
                              " samples, not a whole number of " + std::to_string(s->channels) + "-channel frames");
            if (!positiveRate(s->rate)) return refuse(name + " needs a sample rate that is a positive number");
            if (s->samples.size() > graph::ResourceTable::kMaxAudioSamples)
                return refuse(name + " is larger than " + std::to_string(graph::ResourceTable::kMaxAudioSamples) +
                              " samples");
        } else {
            const auto& f = std::get<OfflineFile>(request.inputs[i]);
            if (f.path.empty()) return refuse(name + " names no file");
            if (f.cap && !positiveRate(f.cap->rate))
                return refuse(name + "'s cap needs a rate that is a positive number");
        }
    }
    for (std::size_t j = 0; j < request.outputs.size(); ++j) {
        const auto& o = request.outputs[j];
        if (auto why = bufferRefusal(o.channels, o.frames, o.rate))
            return refuse("output " + std::to_string(j) + " " + *why);
    }
    for (const std::uint32_t h : request.returns)
        if (h == 0 || h > handles)
            return refuse("a return names res:" + std::to_string(h) + ", which is no input or output of the job");

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return refuse("the engine is going");
    if (outstanding_ >= kMaxJobs)
        return refuse("busy: " + std::to_string(kMaxJobs) + " offline jobs are outstanding");
    // One more worker unless more are parked than jobs already wait for one (a parked worker a notify has woken stays
    // counted until it takes the lock, so the jobs queued since have claimed it), while there is room; made here, under
    // the lock it starts by taking.
    if (idle_ <= queue_.size() && workers_.size() < concurrency()) {
        try {
            workers_.emplace_back([this] { workerLoop(); });
        } catch (const std::system_error& e) {
            if (workers_.empty()) return refuse(std::string("no worker thread could be started: ") + e.what());
        }
    }
    auto job = std::make_unique<Job>();
    job->id = nextId_++;
    job->request = std::move(request);
    const std::uint32_t id = job->id;
    queue_.push_back(std::move(job));
    ++outstanding_;
    work_.notify_one();
    return {id, {}};
}

std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait, std::size_t* inFlight) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (completions_.empty() && wait.count() > 0) {
        finished_.wait_for(lock, std::min(wait, kMaxDrainWait), [this] {
            return stopping_ || !completions_.empty() || outstanding_ == 0;
        });
    }
    std::vector<OfflineCompletion> out;
    out.reserve(completions_.size());
    for (auto& c : completions_) out.push_back(std::move(c));
    outstanding_ -= completions_.size();
    completions_.clear();
    if (inFlight) *inFlight = outstanding_;  // what is left is queued or running: every completion was just taken
    return out;
}

bool OfflineJobs::cancel(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if ((*it)->id != id) continue;
        queue_.erase(it);
        OfflineCompletion c;
        c.id = id;
        c.error = "cancelled";
        completions_.push_back(std::move(c));
        finished_.notify_all();
        return true;
    }
    for (Job* job : running_) {
        if (job->id != id) continue;
        job->cancelled.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

std::size_t OfflineJobs::outstanding() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outstanding_;
}

std::size_t OfflineJobs::workers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

void OfflineJobs::workerLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        ++idle_;
        work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        --idle_;
        if (stopping_) return;
        std::unique_ptr<Job> job = std::move(queue_.front());
        queue_.pop_front();
        running_.push_back(job.get());
        lock.unlock();
        OfflineCompletion done = run(*job);
        lock.lock();
        running_.erase(std::find(running_.begin(), running_.end(), job.get()));
        // A cancel taken while the job ran wins, even over a run that got to its end first: cancel() answered true,
        // and the caller holds that answer as the job's outcome.
        if (job->cancelled.load(std::memory_order_acquire)) {
            done.ok = false;
            done.error = "cancelled";
            done.buffers.clear();
        }
        if (stopping_) return;  // the pool is going, and its completions with it
        completions_.push_back(std::move(done));
        finished_.notify_all();
    }
}

OfflineCompletion OfflineJobs::run(Job& job) {
    OfflineCompletion out;
    out.id = job.id;
    const auto fail = [&out](std::string why) {
        out.ok = false;
        out.error = std::move(why);
        out.buffers.clear();
        return std::move(out);
    };
    const auto cancelled = [&job] { return job.cancelled.load(std::memory_order_acquire); };
    OfflineRequest& req = job.request;
    try {
        if (cancelled()) return fail("cancelled");
        // Declared before the host, which pins what its plan binds: the host goes first, at the end of its scope.
        graph::ResourceTable resources;
        struct Slot {
            bool          empty = false;  // an input of no frames: its handle names nothing
            std::uint32_t channels = 1;
            double        rate = 0.0;
        };
        std::vector<Slot> slots;  // by handle - 1
        std::string       why;
        for (std::size_t i = 0; i < req.inputs.size(); ++i) {
            const std::string name = "input " + std::to_string(i);
            graph::ResourceTable::Id id = 0;
            if (auto* s = std::get_if<OfflineSamples>(&req.inputs[i])) {
                const auto frames = static_cast<std::uint32_t>(s->samples.size() / s->channels);
                if (frames == 0) {
                    id = reservePlaceholder(resources);
                    slots.push_back({true, s->channels, s->rate});
                } else {
                    id = resources.createAudioBuffer(s->channels, frames, s->rate, &why);
                    if (id == 0) return fail(name + ": " + why);
                    auto* b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
                    for (std::uint32_t f = 0; f < frames; ++f)
                        for (std::uint32_t c = 0; c < s->channels; ++c)
                            b->data[c][f] = s->samples[std::size_t(f) * s->channels + c];
                    slots.push_back({false, s->channels, s->rate});
                }
                // The table holds the samples now: the request's copy goes, rather than stay resident for the run.
                std::vector<float>().swap(s->samples);
            } else {
                const auto& f = std::get<OfflineFile>(req.inputs[i]);
                // A cache of the job's own, so a file edited between two jobs decodes as it is now; the decode is
                // SampleCache's, the kit compiler's, so the samples cannot drift from what it decodes.
                kit::SampleCache cache;
                const kit::SampleData* d = cache.getOrLoad(f.path);
                if (!d) return fail(name + ": " + f.path + " could not be read or decoded");
                const OfflineWindow w = offlineWindow(d->buffer.size(), d->sampleRate, f.offset, f.length, f.cap);
                OfflineInputReport report;
                report.handle = static_cast<std::uint32_t>(i + 1);
                report.decodedRate = d->sampleRate;
                report.decodedFrames = static_cast<std::uint32_t>(d->buffer.size());
                report.frames = static_cast<std::uint32_t>(w.frames);
                const float* from = d->buffer.data() + w.start;
                report.nonFinite = std::any_of(from, from + w.frames, [](float v) { return !std::isfinite(v); });
                out.inputs.push_back(report);
                if (w.frames == 0) {
                    id = reservePlaceholder(resources);
                    slots.push_back({true, 1, static_cast<double>(d->sampleRate)});
                } else {
                    id = resources.createAudioBuffer(1, report.frames, d->sampleRate, &why);
                    if (id == 0) return fail(name + ": " + why);
                    auto* b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
                    std::copy(from, from + w.frames, b->data[0]);
                    slots.push_back({false, 1, static_cast<double>(d->sampleRate)});
                }
            }
            if (id != i + 1) return fail(name + " did not get its handle, res:" + std::to_string(i + 1));
        }
        for (std::size_t j = 0; j < req.outputs.size(); ++j) {
            const auto& o = req.outputs[j];
            const auto id = resources.createAudioBuffer(o.channels, o.frames, o.rate, &why);
            if (id == 0) return fail("output " + std::to_string(j) + ": " + why);
            if (id != req.inputs.size() + j + 1)
                return fail("output " + std::to_string(j) + " did not get its handle, res:" +
                            std::to_string(req.inputs.size() + j + 1));
            slots.push_back({false, o.channels, o.rate});
        }

        if (!req.document.empty()) {
            if (cancelled()) return fail("cancelled");
            auto doc = graph::parseGraphDocument(req.document);
            if (!doc) return fail(std::string("not a graph document: ") + doc.error().what());
            graph::HostConfig config;
            config.sampleRate = req.rate;
            config.maxFrames = kBlockFrames;
            graph::GraphHost host(registry_, resources, config);
            const graph::SetGraphResult planned = host.setGraph(*doc);
            if (!planned.ok) return fail("the document was refused: " + describe(planned.errors));
            const rp_transport transport{req.rate, 120.0, 0.0, 0};
            for (std::uint32_t done = 0; done < req.frames;) {
                if (cancelled()) return fail("cancelled");
                const std::uint32_t n = std::min(kBlockFrames, req.frames - done);
                host.processBlock(n, transport);
                done += n;
            }
            // A faulted node is bypassed from its fault on, so what was recorded is not what the document asked for.
            if (host.faultCount() != 0) {
                std::string faulted;
                for (const auto& n : doc->nodes)
                    if (host.isFaulted(n.id)) faulted += (faulted.empty() ? "" : ", ") + n.id;
                return fail("a node faulted: " + faulted);
            }
        }  // the host goes here, on this thread, and every node it built with it

        for (const std::uint32_t h : req.returns) {
            const Slot& slot = slots[h - 1];
            OfflineBuffer buffer;
            buffer.handle = h;
            buffer.channels = slot.channels;
            buffer.rate = slot.rate;
            if (!slot.empty) {
                const auto* b = static_cast<const rp_audio_buffer*>(resources.get(h)->payload);
                buffer.frames = b->frames;
                buffer.samples.resize(std::size_t(b->channels) * b->frames);
                for (std::uint32_t f = 0; f < b->frames; ++f)
                    for (std::uint32_t c = 0; c < b->channels; ++c)
                        buffer.samples[std::size_t(f) * b->channels + c] = b->data[c][f];
            }
            out.buffers.push_back(std::move(buffer));
        }
        out.ok = true;
        return out;
    } catch (const std::exception& e) {
        return fail(std::string("the job failed: ") + e.what());
    }
}

}  // namespace rp::host
````

Run: `mkdir -p packages/native/src/host/engine; sed -n '2488,2850p' docs/modular-graph-phase-d2-plan.md > packages/native/src/host/engine/OfflineJobs.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/src/host/engine/OfflineJobs.cpp`
Expected: `2a79731e68c0b29c5bdc0fee42198d2d9df5ed4f`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 78c97a0c7..114d94ca0 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -205,6 +205,7 @@ add_library(retroplug-backend STATIC
     src/host/engine/Engine.cpp              # the engine: the graph + the script host + the staged MIDI, runs blocks
     src/host/engine/SnapshotRegistry.cpp    # owned id-keyed frame/state/SRAM store — the control plane's one read door
     src/host/engine/EngineInvoker.cpp       # QueuedInvoker - the one mutation path to the Engine
+    src/host/engine/OfflineJobs.cpp         # offline jobs: a document run to completion on a worker host of its own
     src/host/IdleReclaimProbe.cpp           # hosts' idle-tick self-test (HostServices::reclaimOnIdle), smoke-driven
     src/host/graph/SystemNodes.cpp          # the cores as rp.sameboy.gb / rp.mesen.* nodes
     src/host/graph/HostIoNodes.cpp          # rp.host.midi-in / -out
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 52ebd8057..8a1bbb021 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -257,7 +257,25 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
                          e.nodeId.c_str(), e.port.empty() ? "" : ".", e.port.c_str(), e.message.c_str());
 }
 
-Engine::~Engine() = default;
+// The offline pool first: it cancels its jobs and joins its workers, so no job thread outlives the engine, and each
+// job's host and nodes (a resampler among them) are gone before the engine's own members start to go.
+Engine::~Engine() { offline_.reset(); }
+
+rp::host::OfflineSubmit Engine::submitOfflineJob(rp::host::OfflineRequest request) {
+    if (!offline_) offline_ = std::make_unique<rp::host::OfflineJobs>();
+    return offline_->submit(std::move(request));
+}
+
+std::vector<rp::host::OfflineCompletion> Engine::drainOfflineJobs(std::chrono::milliseconds wait,
+                                                                  std::size_t*              inFlight) {
+    if (offline_) return offline_->drain(wait, inFlight);
+    if (inFlight) *inFlight = 0;
+    return {};
+}
+
+bool Engine::cancelOfflineJob(std::uint32_t id) { return offline_ && offline_->cancel(id); }
+
+std::size_t Engine::offlineJobsOutstanding() const { return offline_ ? offline_->outstanding() : 0; }
 
 const void* Engine::graphInterface(void* ctx, const char* id) {
     auto* self = static_cast<Engine*>(ctx);
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 9564e0010..c77ea1aff 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -18,6 +18,7 @@
 #include "graph/rp_node.h"  // rp_queue
 #include "host/engine/CapabilityJobs.hpp"
 #include "host/engine/HostSyncTrace.hpp"
+#include "host/engine/OfflineJobs.hpp"
 #include "host/engine/SnapshotRegistry.hpp"
 #include "host/script/ScriptHost.hpp"
 
@@ -172,6 +173,25 @@ public:
     // dropped where it applies, as the RPC refuses one first from the published size.
     bool writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);
 
+    // --- offline jobs (host/engine/OfflineJobs.hpp, docs/modular-graph-phase-d2-design.md section 3), control
+    // thread ---
+    // A graph document run to completion on a host of its own, on a worker thread of the engine's offline pool, apart
+    // from everything above: its own node registry (the built-in C++ packages only), its own resource table, no audio
+    // thread, and nothing of this engine's plan, ring or resources. The pool is made at the first submission, so a host
+    // that never submits starts no thread, and joined first as the engine goes. Taken or refused at once
+    // (OfflineJobs::submit); its completion comes through a later drain.
+    rp::host::OfflineSubmit submitOfflineJob(rp::host::OfflineRequest request);
+    // The completions since the last drain, oldest first; with `wait`, waits up to it for one while a job is in
+    // flight (OfflineJobs::drain), which a host with no UI tick does between pumps. `inFlight`, when given, is set to
+    // the jobs still queued or running after it.
+    std::vector<rp::host::OfflineCompletion> drainOfflineJobs(
+        std::chrono::milliseconds wait = std::chrono::milliseconds(0), std::size_t* inFlight = nullptr);
+    // Cancel a queued or running job: it completes with the reason "cancelled". False for one neither queued nor
+    // running.
+    bool        cancelOfflineJob(std::uint32_t id);
+    // Offline jobs queued, running, or finished and not yet drained.
+    std::size_t offlineJobsOutstanding() const;
+
     // The owned snapshot store the control plane reads through. Claim a slot at build, release it
     // when the core is deleted; processBlock publishes into it.
     SnapshotRegistry& registry() { return registry_; }
@@ -482,6 +502,12 @@ private:
     std::array<rp::host::Job*, kMaxJobs>            applied_{};
     std::size_t                                     appliedCount_ = 0;
 
+    // The offline job pool, made at the first submission. It uses nothing of the engine's (its registry, its jobs'
+    // tables and hosts are its own), so where it sits among the members does not matter for what it touches; the
+    // destructor still resets it first, so every worker is joined, and every node a job built destroyed, before any
+    // other member goes.
+    std::unique_ptr<rp::host::OfflineJobs>          offline_;
+
     // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
     // liveCores_) as they go.
     std::unique_ptr<Graph> graph_;
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index 547ca9058..56b3b3523 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,7 +35,8 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: resources / lifecycle / script packs / live params / snapshot reads / capability jobs / input (15) ---
+// --- emulator: resources / lifecycle / script packs / live params / snapshot reads / capability jobs / offline jobs /
+// input (18) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::putResource>(e);
@@ -49,6 +50,9 @@ void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::writeRam>(e);
     s.template addMethod<&EngineRpcService::submitJob>(e);
     s.template addMethod<&EngineRpcService::drainJobs>(e);
+    s.template addMethod<&EngineRpcService::submitOfflineJob>(e);
+    s.template addMethod<&EngineRpcService::drainOfflineJobs>(e);
+    s.template addMethod<&EngineRpcService::cancelOfflineJob>(e);
     s.template addMethod<&EngineRpcService::screenshot>(e);
     s.template addMethod<&EngineRpcService::getFrame>(e);
     s.template addMethod<&EngineRpcService::pressButton>(e);
@@ -125,7 +129,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (89 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (92 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index f99f0de58..63c025f7d 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -174,3 +174,68 @@ struct RpcJobCompletion {
     std::uint32_t   progress;
     std::uint32_t   total;
 };
+
+// An offline job (docs/modular-graph-phase-d2-design.md section 3, Engine::submitOfflineJob,
+// host/engine/OfflineJobs.hpp): a graph document run to completion on a host of its own. Its resources are its inputs
+// then its outputs, in order, named in the document as res:1, res:2 and on; `returns` names the handles whose buffers
+// the completion carries. An input is EITHER audio the call carries (`samples`, interleaved float32 in the host's byte
+// order, little-endian on every target, `channels` wide, at `rate`) OR a file native decodes (`path`, through the kit
+// compiler's SampleCache decode, mono at the file's rate) and windows (`offset`, `length`, 0 or absent for all of it,
+// and a `cap`); a field of the other kind is refused. A cap is `frames` at `rate` scaled to the decoded rate by
+// `arithmetic`: "float-ratio" (LSDj's compileSample) or "double-scale" (risa's DMC encode). `document` absent or empty
+// runs no graph, so the job returns its inputs (a file's window, and its report).
+struct RpcOfflineCap { std::uint32_t frames; double rate; std::string arithmetic; };
+struct RpcOfflineInput {
+    std::optional<rfl::Bytestring> samples;
+    std::optional<std::uint32_t>   channels;
+    std::optional<double>          rate;
+    std::optional<std::string>     path;
+    std::optional<std::uint32_t>   offset;
+    std::optional<std::uint32_t>   length;
+    std::optional<RpcOfflineCap>   cap;
+};
+struct RpcOfflineOutput { std::uint32_t channels; std::uint32_t frames; double rate; };
+struct RpcOfflineRequest {
+    std::optional<std::string>    document;
+    std::vector<RpcOfflineInput>  inputs;
+    std::vector<RpcOfflineOutput> outputs;
+    std::vector<std::uint32_t>    returns;
+    std::uint32_t                 frames;
+    double                        rate;
+};
+// Its answer: `id` (taken; its completion comes through drainOfflineJobs) or `error` (refused, and why). Exactly one is
+// present.
+struct RpcOfflineSubmitResult {
+    std::optional<std::uint32_t> id;
+    std::optional<std::string>   error;
+};
+// A returned buffer, `samples` interleaved float32 as an input's are; and a file input's report: the decoded rate and
+// length, the window's length, and whether a sample the window kept is not finite.
+struct RpcOfflineBuffer {
+    std::uint32_t   handle;
+    std::uint32_t   channels;
+    std::uint32_t   frames;
+    double          rate;
+    rfl::Bytestring samples;
+};
+struct RpcOfflineInputReport {
+    std::uint32_t handle;
+    std::uint32_t decodedRate;
+    std::uint32_t decodedFrames;
+    std::uint32_t frames;
+    bool          nonFinite;
+};
+// A finished job: `ok` with its `buffers`, or `error`; `inputs` reports every file input decoded before it ended.
+struct RpcOfflineCompletion {
+    std::uint32_t                      id;
+    bool                               ok;
+    std::string                        error;
+    std::vector<RpcOfflineBuffer>      buffers;
+    std::vector<RpcOfflineInputReport> inputs;
+};
+// What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running after
+// them (so a caller holding a promise for a job native no longer holds learns so, rather than poll for it forever).
+struct RpcOfflineDrain {
+    std::vector<RpcOfflineCompletion> completions;
+    std::uint32_t                     inFlight;
+};
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index aa11311b3..c38c0b137 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -4,6 +4,8 @@
 #include <cmath>
 #include <cstdio>
 #include <cstddef>
+#include <cstring>
+#include <chrono>
 #include <span>
 #include <string>
 #include <utility>
@@ -220,6 +222,95 @@ std::vector<RpcJobCompletion> EngineRpcService::drainJobs() {
     return out;
 }
 
+RpcOfflineSubmitResult EngineRpcService::submitOfflineJob(RpcOfflineRequest request) {
+    RpcOfflineSubmitResult out{std::nullopt, std::nullopt};
+    rp::host::OfflineRequest r;
+    r.document = request.document.value_or("");
+    r.frames = request.frames;
+    r.rate = request.rate;
+    r.returns = std::move(request.returns);
+    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
+        RpcOfflineInput& in = request.inputs[i];
+        const std::string name = "input " + std::to_string(i);
+        const bool samples = in.samples || in.channels || in.rate;
+        const bool file = in.path || in.offset || in.length || in.cap;
+        if (samples == file) {
+            out.error = name + " must be samples (samples, channels and rate) or a file (a path and its window)";
+            return out;
+        }
+        if (samples) {
+            if (!in.samples || !in.channels || !in.rate) {
+                out.error = name + " needs samples, channels and a rate";
+                return out;
+            }
+            if (in.samples->size() % sizeof(float) != 0) {
+                out.error = name + "'s samples are " + std::to_string(in.samples->size()) +
+                            " bytes, not a whole number of float32 values";
+                return out;
+            }
+            rp::host::OfflineSamples s;
+            s.samples.resize(in.samples->size() / sizeof(float));
+            if (!s.samples.empty()) std::memcpy(s.samples.data(), in.samples->data(), in.samples->size());
+            s.channels = *in.channels;
+            s.rate = *in.rate;
+            r.inputs.emplace_back(std::move(s));
+            continue;
+        }
+        if (!in.path) {
+            out.error = name + " needs a path";
+            return out;
+        }
+        rp::host::OfflineFile f;
+        f.path = std::move(*in.path);
+        f.offset = in.offset.value_or(0);
+        f.length = in.length.value_or(0);
+        if (in.cap) {
+            rp::host::OfflineCap cap;
+            cap.frames = in.cap->frames;
+            cap.rate = in.cap->rate;
+            if (in.cap->arithmetic == "float-ratio") cap.arithmetic = rp::host::OfflineCapArithmetic::FloatRatio;
+            else if (in.cap->arithmetic == "double-scale") cap.arithmetic = rp::host::OfflineCapArithmetic::DoubleScale;
+            else {
+                out.error = name + "'s cap arithmetic \"" + in.cap->arithmetic +
+                            "\" is neither \"float-ratio\" nor \"double-scale\"";
+                return out;
+            }
+            f.cap = cap;
+        }
+        r.inputs.emplace_back(std::move(f));
+    }
+    for (const RpcOfflineOutput& o : request.outputs) r.outputs.push_back({o.channels, o.frames, o.rate});
+    rp::host::OfflineSubmit submitted = engine_.submitOfflineJob(std::move(r));
+    if (submitted.id != 0) out.id = submitted.id;
+    else out.error = std::move(submitted.error);
+    return out;
+}
+
+RpcOfflineDrain EngineRpcService::drainOfflineJobs(double waitMs) {
+    // A NaN or a negative wait waits not at all; the engine bounds a long one.
+    const double ms = std::isfinite(waitMs) ? std::clamp(waitMs, 0.0, 1000.0) : (waitMs > 0 ? 1000.0 : 0.0);
+    RpcOfflineDrain out{{}, 0};
+    std::size_t     inFlight = 0;
+    for (auto& c : engine_.drainOfflineJobs(std::chrono::milliseconds(static_cast<std::int64_t>(ms)), &inFlight)) {
+        RpcOfflineCompletion rc{c.id, c.ok, std::move(c.error), {}, {}};
+        for (auto& b : c.buffers) {
+            rfl::Bytestring bytes;
+            if (!b.samples.empty()) {
+                const auto* p = reinterpret_cast<const std::byte*>(b.samples.data());
+                bytes = rfl::Bytestring(p, p + b.samples.size() * sizeof(float));
+            }
+            rc.buffers.push_back({b.handle, b.channels, b.frames, b.rate, std::move(bytes)});
+        }
+        for (const auto& i : c.inputs)
+            rc.inputs.push_back({i.handle, i.decodedRate, i.decodedFrames, i.frames, i.nonFinite});
+        out.completions.push_back(std::move(rc));
+    }
+    out.inFlight = static_cast<std::uint32_t>(inFlight);
+    return out;
+}
+
+bool EngineRpcService::cancelOfflineJob(std::uint32_t id) { return engine_.cancelOfflineJob(id); }
+
 bool EngineRpcService::screenshot(std::uint32_t id, std::string path) {
     // Encodes the owned registry frame (a published copy) — safe while the audio thread runs, no guard.
     return engine_.screenshot(id, path);
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index c064bc70f..a1fad2038 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -62,6 +62,18 @@ public:
     std::vector<RpcJobCompletion> drainJobs();
     bool screenshot(std::uint32_t id, std::string path);
     RpcFrame getFrame(std::uint32_t id);
+    // An offline job (Engine::submitOfflineJob, docs/modular-graph-phase-d2-design.md section 3): its id, whose
+    // completion a later drainOfflineJobs hands back, or why it was refused. Refused here too for an input that is not
+    // exactly one kind (samples, channels and rate; or a path, with its window), samples that are not whole float32
+    // values, and a cap arithmetic that is neither "float-ratio" nor "double-scale".
+    RpcOfflineSubmitResult            submitOfflineJob(RpcOfflineRequest request);
+    // The completions since the last drain, oldest first, and the jobs still queued or running after them. `waitMs`
+    // above 0 waits up to that long (at most a second) for one while a job is in flight: what a host with no UI tick
+    // does between pumps (src/offline.ts). A host that pumps from its UI tick passes 0, and never blocks the thread its
+    // UI runs on.
+    RpcOfflineDrain                   drainOfflineJobs(double waitMs);
+    // Cancel a queued or running offline job; it completes with the reason "cancelled". False for one that is neither.
+    bool                              cancelOfflineJob(std::uint32_t id);
 
     // (Live-core debug/inspection — getApuState/readCpu/breakpoints/trace/profiler/… — moved to
     // DebugRpcService; only the CLI binds that facet.)
````

Run: `sed -n '2860,3191p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds `OfflineJobs.cpp` to `retroplug-backend`.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29127 assertions in 11 test cases)`.

- [ ] **Step 3: The TypeScript promise side**

`src/offline.ts` (new) is the types, `OfflineJobs` (the promise side), `OfflineRunError` (which carries the file
inputs' reports), `setOfflineWait`, `offlineInputRef` / `offlineOutputRef` and the pump: each step drains with the
native wait while the chain is quiet, queues the next step behind whatever settled promises queued, blocks in native
only after eight consecutive steps that settled nothing and saw no submission, holds runs past 64 in a local FIFO, and
rejects a promise whose job `inFlight` says native no longer holds. `capability.ts`'s live set takes any `JobQueue`,
so `pumpAllJobs` visits offline queues; `realBackend.ts` owns the runtime's one offline queue and pumps both kinds;
`backend.ts` declares `offlineRun`; `audioDriver.ts`'s `createAudioDriver` turns the poll's native wait on (at
creation, not at `startAudio`); `mockBackend.ts` rejects ("offline jobs need a native host: the mock backend cannot
run graphs"); `useJobPump.ts`'s header mentions offline jobs.

Create `packages/retroplug/src/offline.ts`:

````ts
// Offline jobs (docs/modular-graph-phase-d2-design.md section 3): a graph document run to completion on a host of its
// own, apart from the live engine, with the buffers it recorded handed back as a promise. Native takes or refuses a job
// at once (submitOfflineJob), runs it on a worker thread of the engine's offline pool, and hands its completion back
// later (drainOfflineJobs, from a bounded queue). This is the promise side of that; only the real backend has it, since
// only a native host runs graphs (the mock rejects).
//
// The poll follows the capability jobs' (src/capability.ts), whose live set it joins, so one pump settles both kinds:
// once right after each submission, from the UI's frame tick (ui/app/useJobPump.ts, through backend.pumpJobs) while
// anything is pending, and after each audio-driver call that lets time pass (pumpAllJobs). What it cannot share is the
// capability jobs' wait: a capability submission blocks until its job settles, which would run offline jobs one at a
// time, where a kit compile wants its slots in parallel. So in a host with no UI tick (the CLI, the Node addon, the
// test host: the audio driver turns the wait on as it is made, setOfflineWait) a submission starts a poll that runs as
// a microtask chain, one drain a step, each step queued behind whatever microtasks the settled promises queued. A step
// drains without waiting while the chain is busy, and only after QUIET_STEPS steps in a row that settled nothing and
// saw no submission does one block in native (drainOfflineJobs waits up to the wait for a completion while a job is in
// flight): a caller that awaits through a few async layers before submitting its next job reaches the submission in
// those quiet steps, rather than finding the chain already asleep until some other job completes. Everything submitted
// in one synchronous run is in flight at once, a chain of jobs keeps moving, and `await offlineRun(...)` settles with
// no timer at all. A host whose UI tick pumps never turns the wait on, so no step ever blocks its UI thread.
//
// Native holds at most OFFLINE_MAX_JOBS outstanding per engine and refuses a submission past that as busy. The queue
// never surfaces that: one runtime's pending promises are exactly its engine's outstanding jobs (each pending id is one
// job native holds until a drain hands it back), so a run made while OFFLINE_MAX_JOBS are pending is held here, in
// submission order, and submitted as completions make room. A held job is refused (its other refusals) only when it is
// submitted.

import { setQueueLive, type JobQueue } from "./capability";

/** How a file input's frame cap scales a frame count at a kit rate to the decoded rate: LSDj's window computes it in
 *  float with the ratio first ("float-ratio"), risa's in double with the product first ("double-scale"). */
export type OfflineCapArithmetic = "float-ratio" | "double-scale";

/** A file input's cap: at most `frames` frames at `rate`, scaled to the decoded rate as `arithmetic` says. */
export interface OfflineCap {
  frames: number;
  rate: number;
  arithmetic: OfflineCapArithmetic;
}

/** Audio the call carries: interleaved, `channels` wide, at `rate`. */
export interface OfflineSamplesInput {
  samples: Float32Array;
  channels: number;
  rate: number;
}

/** A file native decodes (the kit compiler's decode: mono, at the file's own rate) and windows: from `offset`
 *  (clamped to the end), `length` frames (absent or 0: to the end), at most the cap. */
export interface OfflineFileInput {
  path: string;
  offset?: number;
  length?: number;
  cap?: OfflineCap;
}

export type OfflineInput = OfflineSamplesInput | OfflineFileInput;

/** An empty buffer the document's recorder writes. */
export interface OfflineOutput {
  channels: number;
  frames: number;
  rate: number;
}

/** A job: `document` (a graph document's JSON, as setGraph takes it; absent for none, so the job returns its inputs)
 *  run for `frames` frames at `rate`, over its inputs then its outputs, which the document names res:1, res:2 and on
 *  (offlineInputRef, offlineOutputRef). `returns` names the handles whose buffers come back, every output by
 *  default. */
export interface OfflineRunRequest {
  document?: string;
  inputs?: OfflineInput[];
  outputs?: OfflineOutput[];
  returns?: number[];
  frames: number;
  rate: number;
}

/** A returned buffer: interleaved, `channels` x `frames` at `rate`. */
export interface OfflineBuffer {
  handle: number;
  channels: number;
  frames: number;
  rate: number;
  samples: Float32Array;
}

/** What a file input decoded to and kept: the decoded rate and length, the window's length (0: an empty window, an
 *  input no resource names), and whether a sample the window kept is not finite. */
export interface OfflineInputReport {
  handle: number;
  decodedRate: number;
  decodedFrames: number;
  frames: number;
  nonFinite: boolean;
}

/** A finished job: the buffers `returns` named, in that order, and the file inputs' reports. */
export interface OfflineRunResult {
  buffers: OfflineBuffer[];
  inputs: OfflineInputReport[];
}

/** A job that failed: native's reason, and the reports of the file inputs it decoded before it stopped. */
export class OfflineRunError extends Error {
  constructor(message: string, readonly inputs: OfflineInputReport[]) {
    super(message);
    this.name = "OfflineRunError";
  }
}

/** The handle input `i` gets in a document, as a resource reference ("res:<n>"). */
export function offlineInputRef(i: number): string {
  return `res:${i + 1}`;
}

/** The handle output `j` gets in a document of `inputCount` inputs, as a resource reference. */
export function offlineOutputRef(inputCount: number, j: number): string {
  return `res:${inputCount + j + 1}`;
}

// The wire shapes (native BackendTypes.hpp: RpcOfflineRequest, RpcOfflineSubmitResult, RpcOfflineCompletion). An
// absent optional is left out, which native reads as nullopt.
export interface OfflineWireInput {
  samples?: Uint8Array;
  channels?: number;
  rate?: number;
  path?: string;
  offset?: number;
  length?: number;
  cap?: OfflineCap;
}
export interface OfflineWireRequest {
  document?: string;
  inputs: OfflineWireInput[];
  outputs: OfflineOutput[];
  returns: number[];
  frames: number;
  rate: number;
}
export interface OfflineSubmitResult {
  id?: number | null;
  error?: string | null;
}
export interface OfflineWireCompletion {
  id: number;
  ok: boolean;
  error: string;
  buffers: { handle: number; channels: number; frames: number; rate: number; samples: Uint8Array }[];
  inputs: OfflineInputReport[];
}

/** What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running. */
export interface OfflineWireDrain {
  completions: OfflineWireCompletion[];
  inFlight: number;
}

/** The two native calls the jobs ride: what a backend hands OfflineJobs. */
export interface OfflinePort {
  submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult;
  drainOfflineJobs(waitMs: number): OfflineWireDrain;
}

/** Native's bound on one engine's outstanding offline jobs (OfflineJobs::kMaxJobs): past it the queue holds runs
 *  back. */
export const OFFLINE_MAX_JOBS = 64;

// How many quiet steps in a row (nothing settled, nothing submitted) the poll takes before one waits in native.
const QUIET_STEPS = 8;

// How long one poll step may wait in native for a completion, or 0 for no poll at all (a host whose UI tick pumps).
let offlineWaitMs = 0;

/** Turn the poll on (a wait per step, in ms) or off (0): the audio driver turns it on as it is made, in the hosts
 *  with no UI tick. */
export function setOfflineWait(ms: number): void {
  offlineWaitMs = ms > 0 ? ms : 0;
}

function toWire(request: OfflineRunRequest): OfflineWireRequest {
  const inputs = (request.inputs ?? []).map((input): OfflineWireInput => {
    if ("samples" in input) {
      const s = input.samples;
      const bytes = new Uint8Array(s.buffer, s.byteOffset, s.byteLength);
      return { samples: bytes, channels: input.channels, rate: input.rate };
    }
    const w: OfflineWireInput = { path: input.path };
    if (input.offset !== undefined) w.offset = input.offset;
    if (input.length !== undefined) w.length = input.length;
    if (input.cap !== undefined) w.cap = input.cap;
    return w;
  });
  const outputs = request.outputs ?? [];
  const wire: OfflineWireRequest = {
    inputs,
    outputs,
    returns: request.returns ?? outputs.map((_, j) => inputs.length + j + 1),
    frames: request.frames,
    rate: request.rate,
  };
  if (request.document !== undefined) wire.document = request.document;
  return wire;
}

function floats(bytes: Uint8Array): Float32Array {
  // A copy, so the floats sit on a 4-byte boundary whatever offset the bytes arrived at.
  return bytes.byteLength ? new Float32Array(bytes.slice().buffer) : new Float32Array(0);
}

interface Pending {
  resolve: (value: OfflineRunResult) => void;
  reject: (error: Error) => void;
}

/** The promises over one queue of offline jobs: native's, which is one per engine. */
export class OfflineJobs implements JobQueue {
  private readonly pending = new Map<number, Pending>();
  // Runs made while OFFLINE_MAX_JOBS were pending, oldest first, submitted as completions make room.
  private readonly held: { wire: OfflineWireRequest; waiter: Pending }[] = [];
  private polling = false;
  private quiet = 0;          // the poll's steps in a row that settled nothing and saw no submission
  private submitted = false;  // a submission since the poll's last step

  constructor(private readonly port: OfflinePort) {}

  /** Run a job. Rejects with native's reason when it is refused (at once, or when a held run is submitted); otherwise
   *  settles with its completion, or rejects with an OfflineRunError carrying native's reason. Never rejects because
   *  native's bound is full: the run waits here for room instead. */
  run(request: OfflineRunRequest): Promise<OfflineRunResult> {
    const wire = toWire(request);
    let waiter: Pending | null = null;
    const promise = new Promise<OfflineRunResult>((resolve, reject) => (waiter = { resolve, reject }));
    if (this.pending.size >= OFFLINE_MAX_JOBS) this.held.push({ wire, waiter: waiter! });
    else this.submit(wire, waiter!);
    if (this.outstanding() > 0) setQueueLive(this, true);
    this.pump();
    this.poll();
    return promise;
  }

  /** Drain native's completions without waiting and settle their promises; nothing (no native call) while none is
   *  pending. Returns how many are still pending, held runs included. */
  pump(): number {
    return this.settle(0);
  }

  /** How many promises are waiting on a completion, held runs included. */
  pendingCount(): number {
    return this.outstanding();
  }

  private outstanding(): number {
    return this.pending.size + this.held.length;
  }

  private submit(wire: OfflineWireRequest, waiter: Pending): void {
    this.submitted = true;
    const submitted = this.port.submitOfflineJob(wire);
    if (submitted.id == null) waiter.reject(new Error(submitted.error ?? "the offline job was refused"));
    else this.pending.set(submitted.id, waiter);
  }

  private settle(waitMs: number): number {
    if (this.pending.size === 0) {
      if (this.held.length === 0) setQueueLive(this, false);
      return this.outstanding();
    }
    const drained = this.port.drainOfflineJobs(waitMs);
    for (const c of drained.completions) {
      const p = this.pending.get(c.id);
      if (!p) continue;
      this.pending.delete(c.id);
      if (!c.ok) {
        p.reject(new OfflineRunError(c.error, c.inputs));
        continue;
      }
      p.resolve({
        buffers: c.buffers.map((b) => ({
          handle: b.handle,
          channels: b.channels,
          frames: b.frames,
          rate: b.rate,
          samples: floats(b.samples),
        })),
        inputs: c.inputs,
      });
    }
    // A promise whose job native no longer holds (none came back, and nothing is in flight) would otherwise be polled
    // forever: it cannot complete, so it fails. One runtime binding one engine never gets here.
    if (this.pending.size > 0 && drained.inFlight === 0) {
      const lost = [...this.pending.values()];
      this.pending.clear();
      for (const p of lost) p.reject(new Error("the engine holds no record of this offline job"));
    }
    while (this.held.length && this.pending.size < OFFLINE_MAX_JOBS) {
      const next = this.held.shift()!;
      this.submit(next.wire, next.waiter);
    }
    if (this.outstanding() === 0) setQueueLive(this, false);
    return this.outstanding();
  }

  // The no-UI-tick hosts' poll: a microtask chain, one step per turn, until nothing is pending. A step drains without
  // waiting unless the chain has been quiet for QUIET_STEPS steps; a step that waited starts the count again, so the
  // promises it settled get their quiet steps to submit what follows them. One chain per queue: a submission while one
  // runs joins it.
  private poll(): void {
    if (this.polling || offlineWaitMs === 0 || this.outstanding() === 0) return;
    this.polling = true;
    this.quiet = 0;
    const step = (): void => {
      if (offlineWaitMs === 0) {
        this.polling = false;
        return;
      }
      const wait = this.submitted || this.quiet < QUIET_STEPS ? 0 : offlineWaitMs;
      const before = this.pending.size;
      this.submitted = false;
      const left = this.settle(wait);
      if (left === 0) {
        this.polling = false;
        return;
      }
      if (wait > 0 || this.submitted || this.pending.size !== before) this.quiet = 0;
      else this.quiet++;
      void Promise.resolve().then(step);
    };
    void Promise.resolve().then(step);
  }
}
````

Run: `mkdir -p packages/retroplug/src; sed -n '3218,3547p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/src/offline.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/offline.ts`
Expected: `e1a4f0feccb81916e6ad9b3f379d6588a19634a5`: the blob 32992e257 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/src/audioDriver.ts b/packages/retroplug/src/audioDriver.ts
index ccef745f8..8ceff1976 100644
--- a/packages/retroplug/src/audioDriver.ts
+++ b/packages/retroplug/src/audioDriver.ts
@@ -6,6 +6,7 @@
 
 
 import { pumpAllJobs, setJobWait } from "./capability";
+import { setOfflineWait } from "./offline";
 import { makeCall } from "./rpcClient";
 import type { EngineGraph } from "./engineGraph";
 import type { HostShape } from "./graph/buildGraph";
@@ -231,6 +232,10 @@ export interface AudioDriver {
   drainReleased(): number;
 }
 
+// How long one offline poll step waits in native for a completion before the next microtask turn: long enough that a
+// waiting chain costs nothing, short enough that a step never holds a turn for long.
+const OFFLINE_WAIT_MS = 100;
+
 /** Build an audio driver backed by the native host. Throws if no RPC surface is bound.
  *
  *  `graph` is the engine's graph whoever builds the systems publishes (docs/modular-graph-phase-b2-design.md
@@ -239,6 +244,9 @@ export interface AudioDriver {
  *  the calls render whatever shape is planned, and native refuses buffers that do not match it. */
 export function createAudioDriver(graph?: EngineGraph): AudioDriver {
   const call = makeCall();
+  // A host that makes an audio driver has no UI tick to pump offline jobs (the CLI, the Node addon, the test host), so
+  // their promises settle through the poll's native wait (src/offline.ts) instead.
+  setOfflineWait(OFFLINE_WAIT_MS);
 
   const ints = (b: Uint8Array | number[]): number[] => Array.from(b);
   const STEREO: HostShape = { kind: "multi", channels: 2 };
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index fba669248..8dcce84fd 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -18,6 +18,7 @@
 // belongs in the application layer, not here.
 
 import type { CapabilityArgs, CapabilityOp, CapabilityProgress, CapabilityResults } from "./capability";
+import type { OfflineRunRequest, OfflineRunResult } from "./offline";
 import type { Platform, Core } from "./platform";
 import type { TapArm } from "./graph/buildGraph";
 
@@ -237,11 +238,29 @@ export interface Backend {
     onProgress?: (p: CapabilityProgress) => void,
   ): Promise<CapabilityResults[O]>;
 
-  /** Settle what has completed since the last look: the control plane's tick calls it, and it still drains the
-   *  completions a close left owed (a no-op only when nothing is pending and nothing is owed). Returns how many are
-   *  still pending. */
+  /** Settle what has completed since the last look, capability and offline jobs both: the control plane's tick calls
+   *  it, and it still drains the completions a close left owed (a no-op only when nothing is pending and nothing is
+   *  owed). Returns how many are still pending. */
   pumpJobs(): number;
 
+  // --- Offline jobs (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3) ------------------------------
+
+  /** Run a graph document to completion on a host of its own, apart from the running graph: its inputs (audio, or a
+   *  file native decodes and windows) then its outputs are the document's res:1, res:2 and on, and the buffers
+   *  `returns` names come back. Rejects with native's reason when the job is refused or fails (an OfflineRunError,
+   *  carrying the file inputs' reports, for a failure); never for native's bound on outstanding jobs, past which runs
+   *  wait in TS for room. The mock rejects every job: only a native host runs graphs.
+   *
+   *  What settles it: in a host with a UI tick, the tick's pumpJobs. In a host with none (the CLI, the Node addon, the
+   *  test host) the poll's native wait, which the audio driver turns on as it is made (createAudioDriver, or
+   *  setOfflineWait): a host of that kind that makes no audio driver and calls no pump leaves the promise pending.
+   *
+   *  Zero frames: an input whose window comes out empty is accepted (its report says `frames: 0`, its buffer comes back
+   *  empty) but names no resource, so a document cannot bind it; an output of no frames is refused. A caller whose
+   *  chain meets an empty buffer (an empty window, a resample whose length comes out 0) submits nothing after it and
+   *  makes what those jobs would have made itself. */
+  offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult>;
+
   /** Reject every pending capability promise with `reason`: the project closed, and the nodes they named with it. A
    *  real client's queue is its engine's, shared by every emulator client of a runtime, so this reaches theirs too. */
   rejectPendingJobs(reason: string): void;
@@ -442,7 +461,7 @@ export type EmulatorBackend = Pick<
   | "putResource" | "releaseResource"
   | "setGraph" | "loadScriptPack" | "setParam"
   | "pressButton" | "stageNodeMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
-  | "capability" | "pumpJobs" | "rejectPendingJobs"
+  | "capability" | "pumpJobs" | "rejectPendingJobs" | "offlineRun"
 >;
 
 /** Live-core inspection / stepping / breakpoints / profiler — the CLI-only debug facet (spec/09). */
diff --git a/packages/retroplug/src/capability.ts b/packages/retroplug/src/capability.ts
index ca4349848..14960fdfd 100644
--- a/packages/retroplug/src/capability.ts
+++ b/packages/retroplug/src/capability.ts
@@ -141,11 +141,23 @@ interface Pending {
   onProgress?: (p: CapabilityProgress) => void;
 }
 
-// Every CapabilityJobs a time-advancing pump should visit: the ones with a promise waiting, or a completion a close
-// left owed. A queue that owes nothing leaves the set, so a runtime that makes backends repeatedly bounds it; one real
-// client's queue serves every client of a runtime, since native's completion queue is one per engine and a runtime
-// binds one engine (realBackend.ts), while the mock has one per mock, over its own model.
-const liveJobs = new Set<CapabilityJobs>();
+/** A queue of jobs a time-advancing pump visits: the capability jobs here, and the offline jobs (src/offline.ts), so
+ *  one pump settles both kinds. `pump` drains what has completed and answers how many are still pending. */
+export interface JobQueue {
+  pump(): number;
+}
+
+// Every queue a time-advancing pump should visit: the ones with a promise waiting, or a completion a close left owed.
+// A queue that owes nothing leaves the set, so a runtime that makes backends repeatedly bounds it; one real client's
+// queue serves every client of a runtime, since native's completion queue is one per engine and a runtime binds one
+// engine (realBackend.ts), while the mock has one per mock, over its own model.
+const liveJobs = new Set<JobQueue>();
+
+/** Put a queue in, or take it out of, the set pumpAllJobs visits: a queue joins while it has a promise waiting. */
+export function setQueueLive(queue: JobQueue, live: boolean): void {
+  if (live) liveJobs.add(queue);
+  else liveJobs.delete(queue);
+}
 
 // How to let time pass while a submitted job waits, or null to return its promise pending: the audio driver installs
 // a native sleep while its audio thread runs (startAudio to stopAudio), where nothing else would pump, and clears it on
@@ -157,8 +169,8 @@ export function setJobWait(wait: ((ms: number) => void) | null): void {
   jobWait = wait;
 }
 
-/** Pump every live job queue: the audio driver calls it after the calls that advance time. A real client's queue is its
- *  engine's, so one visit can settle more than one backend's promises. */
+/** Pump every live job queue, capability and offline: the audio driver calls it after the calls that advance time. A
+ *  real client's queue is its engine's, so one visit can settle more than one backend's promises. */
 export function pumpAllJobs(): void {
   for (const jobs of liveJobs) jobs.pump();
 }
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 313025db1..923f56030 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -18,6 +18,7 @@ import type { OpenSerialPort, SerialClient, SerialPortInfo } from "./n8/transpor
 import { savFromJson as savFromJsonTs } from "./lsdj";
 import { makeCall } from "./rpcClient";
 import { CapabilityJobs, type JobCompletion, type JobRequest, type JobSubmitResult } from "./capability";
+import { OfflineJobs, type OfflineSubmitResult, type OfflineWireDrain, type OfflineWireRequest } from "./offline";
 
 
 // --- file dialog (async, UI-direct) ------------------------------------------------------------------
@@ -116,10 +117,23 @@ function sharedJobs(): CapabilityJobs {
   return realJobs;
 }
 
+// The offline jobs' queue, shared by every emulator client of a runtime for the capability jobs' reason: native's
+// completion queue is one per engine.
+let realOffline: OfflineJobs | null = null;
+
+function sharedOffline(): OfflineJobs {
+  realOffline ??= new OfflineJobs({
+    submitOfflineJob: (request: OfflineWireRequest) => makeCall()("submitOfflineJob", request) as OfflineSubmitResult,
+    drainOfflineJobs: (waitMs: number) => makeCall()("drainOfflineJobs", waitMs) as OfflineWireDrain,
+  });
+  return realOffline;
+}
+
 /** The emulator lifecycle / live params / input / snapshot-reads facet. Throws if no RPC surface is bound. */
 export function createEmulatorClient(): EmulatorBackend {
   const call = makeCall();
   const jobs = sharedJobs();
+  const offline = sharedOffline();
   return {
     // Native answers { id } or { error }: one optional each way, so a refused put has no id to name.
     putResource: (spec: ResourceSpec): PutResourceResult => {
@@ -138,7 +152,9 @@ export function createEmulatorClient(): EmulatorBackend {
     readRam: (id) => bytesOrNull(call("readRam", id)),
     writeRam: (id, offset, bytes) => call("writeRam", id, offset, bytes) as boolean,
     capability: (nodeId, op, args, onProgress) => jobs.run(nodeId, op, args, onProgress),
-    pumpJobs: () => jobs.pump(),
+    // One pump for both kinds: the UI tick's, and the one after each submission.
+    pumpJobs: () => jobs.pump() + offline.pump(),
+    offlineRun: (request) => offline.run(request),
     rejectPendingJobs: (reason) => jobs.rejectAll(reason),
     getFrame: (id): FrameData | null => {
       const r = call("getFrame", id) as { width: number; height: number; published: boolean; data?: Uint8Array } | null;
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index b4347b9bc..9130df88a 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -6,6 +6,7 @@
 
 import type { ApuState, ApuSquareState, Backend, BreakHitBatch, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, PutResourceResult, ResourceSpec, ResourceType, TraceLine, ZipEntry } from "../src/backend";
 import { savFromJson } from "../src/lsdj";
+import type { OfflineRunRequest, OfflineRunResult } from "../src/offline";
 import type { GraphDocument, NodeDoc } from "../src/graph/graphDocument";
 import { rebuildKeyOf } from "../src/graph/buildGraph";
 import { canonicalJson } from "../src/graph/canonicalJson";
@@ -802,6 +803,13 @@ export class MockBackend implements Backend {
     this.jobs.rejectAll(reason);
   }
 
+  // The mock tier has no graph executor, so it runs no offline job: a caller learns so rather than getting buffers no
+  // graph produced.
+  offlineRun(_request: OfflineRunRequest): Promise<OfflineRunResult> {
+    this.log.push("offlineRun");
+    return Promise.reject(new Error("offline jobs need a native host: the mock backend cannot run graphs"));
+  }
+
   getFrame(id: number): FrameData | null {
     this.log.push("getFrame");
     // The mock never advances a real core, so a live system reports GB dimensions but no published
diff --git a/packages/retroplug/ui/app/useJobPump.ts b/packages/retroplug/ui/app/useJobPump.ts
index 5cc44119c..7d4d468ae 100644
--- a/packages/retroplug/ui/app/useJobPump.ts
+++ b/packages/retroplug/ui/app/useJobPump.ts
@@ -1,8 +1,9 @@
-// useJobPump - settle capability jobs (src/capability.ts) as their completions arrive.
+// useJobPump - settle capability jobs (src/capability.ts) and offline jobs (src/offline.ts) as they complete.
 //
 // A job's promise settles when the backend drains its completion. Everything that completes at once (a read of an
 // emulated system, a write while no audio thread runs) settles at its submission; a write while the audio thread runs
-// completes after the next block, and a device's job seconds later, so something has to look again. That is this
+// completes after the next block, a device's job seconds later, and an offline job when its worker finishes it, so
+// something has to look again. That is this
 // hook, on the render frame the other watchers already count, rather than a timer of its own: pumpJobs makes no native
 // call while nothing is pending, so the steady state costs one size check a frame.
 //
````

Run: `sed -n '3557,3776p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test offline`
Expected: 8 passed: the offline file alone, as the report counts it.

Run: `pnpm test:native offline-run`
Expected: 5 passed and `# 1 native test file(s) passed`.

- [ ] **Step 4: Show that the window's arithmetic bites**

The window cases were written beside the code, so their bite is shown by a mutation: LSDj's cap computed in double
with the product first, which is risa's form, instead of in float with the ratio first. It is applied, run, reversed
and rebuilt.

````patch
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -61,4 +61,5 @@
         if (cap->arithmetic == OfflineCapArithmetic::FloatRatio) {
-            const float ratio = static_cast<float>(decodedRate) / static_cast<float>(cap->rate);
-            maxFrames = static_cast<std::size_t>(std::ceil(static_cast<float>(cap->frames) * ratio));
+            // mutated: LSDj's cap in double, the product first, as risa's spells it
+            maxFrames = static_cast<std::size_t>(
+                std::ceil(static_cast<double>(cap->frames) * static_cast<double>(decodedRate) / cap->rate));
         } else {
````

Run: `sed -n '3795,3805p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the mutation applies cleanly to the tree Step 2 placed.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[offline]"`
Expected: the window case fails: its `CHECK(lsdjMismatches == 0)` sees 464 mismatched rates and `CHECK(differs)` fails too, because the two arithmetics now agree everywhere. Report the summary line.

Run: `sed -n '3795,3805p' docs/modular-graph-phase-d2-plan.md | git apply -R`
Expected: nothing: the mutation is reversed and the file is Step 2's again.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29127 assertions in 11 test cases)` again, the restored tree.

- [ ] **Step 5: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, with the plugin and `retroplug-sdl` included.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 279 | 270 passed | 9 skipped`; `retroplug-graph-test` `All tests passed (302400 assertions in 259 test cases)`; `retroplug-audio-test` `All tests passed (1235263 assertions in 109 test cases)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: the whole suite passes with the fixtures present, nothing skipped: `All tests passed (N assertions in 279 test cases)`, the 279 deterministic from the run above (270 passed plus the 9 fixture-gated cases that now run). The report's first pass recorded `All tests passed (66187 assertions in 278 test cases)`, before the fix pass's second parked-worker case; report this tree's assertion total N.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, nothing skipped, no baseline re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 161 native test file(s) passed (jobs=16)`, with only dsp-bench and nes-bench skipping.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native offline-run`
Expected: 5 passed, 0 skipped, and `# 1 native test file(s) passed`: the new file needs no fixture. The count is the fix pass's five cases; the no-fixtures run itself is inferred, not logged, since the report's only CI-view figure is the first pass's 4 passed, before the fifth case. Report this run's line.

Run: `pnpm test`
Expected: `# 170 test file(s) passed (jobs=16)` (the baseline's 169 plus offline).

Run: `pnpm typecheck`
Expected: clean: `pnpm typecheck` -> clean.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

Run: `pnpm test:node`
Expected: 12 tests, 12 pass, 0 fail: the codec matrix now carries an offline job, and the report checked by hand that it completes `ok` on Node rather than agreeing on two failures.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines.

- [ ] **Step 6: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29127 assertions in 11 test cases)`, 0 ThreadSanitizer reports: 25 jobs in parallel and beside a live audio thread were the point of this tree.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29127 assertions in 11 test cases)`, 0 Address/LeakSanitizer reports (leak detection on: a job's table, host and resamplers must all die on its worker).

- [ ] **Step 7: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/native/CMakeLists.txt
 M packages/native/cmake/tests.cmake
 M packages/native/node/test/ops.mjs
 M packages/native/src/host/engine/Engine.cpp
 M packages/native/src/host/engine/Engine.hpp
?? packages/native/src/host/engine/OfflineJobs.cpp
?? packages/native/src/host/engine/OfflineJobs.hpp
 M packages/native/src/host/rpc/BackendRpcRegistration.hpp
 M packages/native/src/host/rpc/BackendTypes.hpp
 M packages/native/src/host/rpc/EngineRpcService.cpp
 M packages/native/src/host/rpc/EngineRpcService.hpp
?? packages/native/test/engine-graph/OfflineJobs.test.cpp
 M packages/retroplug/src/audioDriver.ts
 M packages/retroplug/src/backend.ts
 M packages/retroplug/src/capability.ts
?? packages/retroplug/src/offline.ts
 M packages/retroplug/src/realBackend.ts
?? packages/retroplug/test-native/offline-run.test.ts
?? packages/retroplug/test/offline/offline.test.ts
 M packages/retroplug/testing/mockBackend.ts
 M packages/retroplug/ui/app/useJobPump.ts
````

````bash
git add -- packages/native/src/host/engine/OfflineJobs.cpp packages/native/src/host/engine/OfflineJobs.hpp \
  packages/native/test/engine-graph/OfflineJobs.test.cpp packages/retroplug/src/offline.ts \
  packages/retroplug/test-native/offline-run.test.ts packages/retroplug/test/offline/offline.test.ts \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake packages/native/node/test/ops.mjs \
  packages/native/src/host/engine/Engine.cpp packages/native/src/host/engine/Engine.hpp \
  packages/native/src/host/rpc/BackendRpcRegistration.hpp packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/retroplug/src/audioDriver.ts packages/retroplug/src/backend.ts packages/retroplug/src/capability.ts \
  packages/retroplug/src/realBackend.ts packages/retroplug/testing/mockBackend.ts \
  packages/retroplug/ui/app/useJobPump.ts
git commit -F - <<'EOF'
feat(engine): offline jobs run a graph document to completion on a host of their own

An offline job (docs/modular-graph-phase-d2-design.md section 3) is a graph document, its inputs, the empty buffers
it records into, the buffers to return and the frames to run. Engine::submitOfflineJob takes or refuses it at once;
it runs on a worker of the engine's offline pool (host/engine/OfflineJobs), and its completion is drained later from
a bounded queue, as a capability job's is.

The pool is made at the first submission and joined first as the engine goes. Its workers start as jobs need them,
up to the machine's hardware threads (at most 8), the rest queued, and stay parked until the pool goes; at most 64
jobs are outstanding, queued, running or finished and undrained. Its node registry is its own, filled with the built-in C++ packages (rp.core, rp.dsp,
rp.dsp.resample) before any worker exists and never changed, so the workers share it read-only and a script pack
loading or reloading, which swaps the engine's registry, can never race a job's planning; a script, system or host
node in a job's document is refused as an unknown type. Each job runs on one worker from start to end: its own
ResourceTable, a quiescent GraphHost over the pool's registry (setGraph flushes inline, run() is the block driver),
the frames in blocks of 1024 with the last partial, a cancel checked between blocks, the host torn down on that
thread so every resampler dies inside its life. A job never touches the engine's plan, ring, resources or audio
thread.

Inputs then outputs are the document's res:1, res:2 and on. An input is interleaved float32 samples, or a file
decoded through SampleCache (the kit compiler's decode) and windowed by an offset, a length and a cap whose
arithmetic reproduces LSDj's (float, the ratio first) or risa's (double, the product first) window exactly, edges
included; a file input reports its decoded rate and length, the window's length and whether a kept sample is not
finite. An input of no frames is an empty buffer whose handle names nothing; an output of no frames is refused. A planner refusal, an activate error, a
fault, a refused input or a cancel completes the job with the reason.

The emulator facet, which the plugin, the SDL standalone, the CLI and the Node addon all bind, gains
submitOfflineJob, drainOfflineJobs (with a bounded native wait, answering the jobs still in flight beside the
completions) and cancelOfflineJob. backend.offlineRun is a promise over them: the capability jobs' pump settles both
kinds (the UI frame tick in the plugin), and in a host with no UI tick the audio driver turns on a microtask poll
that blocks in the native wait only after several steps that settled nothing and saw no submission, so jobs
submitted together run in parallel, a chain awaiting through async layers keeps moving, and `await` settles with no
timer. Runs past native's 64 are held in TS and submitted as room comes, so offlineRun never rejects busy; a promise
whose job native no longer holds rejects. The mock backend rejects: it runs no graph.

Tests: retroplug-engine-graph-test "[offline]" (a job equals the same nodes through a GraphRig, parallel and queued
jobs, cancel, an engine destroyed with jobs in flight joining every worker, the bound, every refusal, file inputs
and both windows at every edge, and a job beside a live plan that stays bit for bit with warmed blocks allocating
nothing, and a second worker started for a job behind a claimed parked one), under TSan and ASan;
test-native/offline-run (a fan-out of 150 included); the mock's rejection and the promise side in pnpm test (a
wrapped chain beside a long job, a fan-out past the bound, a lost job); the Node-vs-QuickJS codec matrix carries an
offline job. A hidden "[offline-bench]" times a job.
EOF
````

---

### Task 3: normalise, input-seeded dither and DPCM as nodes reading a bound buffer

Design section 4 and section 10's second question. Three whole-buffer steps a streaming chain cannot do in one pass
become nodes that read a bound `audio-buffer` at activate, on the control thread, where a whole-buffer pass is
allowed: `rp.dsp.normalize` measures its own peak and streams the buffer times the normalising gain, `rp.dsp.dither`
gains `seedFrom` and hashes the buffer the kit path hashes, and `rp.dsp.dpcm` runs the DMC encoder. The encode moves
out of `RisaDmcCodec` into `retroplug-dsp-kernels`, so the codec's bytes are unchanged and the kit baseline is still
the oracle. All three types join `rp.dsp`, which the engine, the graph rig and Task 2's pool already register, so no
registration changes; the descriptor goldens re-record (dither) or are added (the two new types).

**Files:**

- Create: `packages/native/src/dsp/Dpcm.cpp`, `packages/native/src/dsp/Dpcm.hpp`, `packages/native/test/graph/DspBufferNodes.test.cpp`, `packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json`, `packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/graph.cmake`, `packages/native/cmake/tests.cmake`, `packages/native/src/graph/nodes/DspNodes.cpp`, `packages/native/src/graph/nodes/DspNodes.hpp`, `packages/native/src/risa/RisaDmcCodec.cpp`, `packages/native/test/audio/KitBaseline.test.cpp`, `packages/native/test/engine-graph/EngineGraph.test.cpp`, `packages/native/test/engine-graph/OfflineJobs.test.cpp`, `packages/native/test/graph/Dsp.test.cpp`, `packages/native/test/graph/DspNodes.test.cpp`, `packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`

- [ ] **Step 1: Write the failing cases**

`DspBufferNodes.test.cpp` (new, `[dsp-buffer-nodes]`, 12 cases after the fix pass) holds each node to the kit path:
normalize whole and split, its peak over the channels it plays, its silence past the buffer, its committed gain and
the block after a commit allocating nothing; `seedFrom`'s channel 0 against `inputSeed` and the other channels'
derived seeds, a committed seed restarting from the buffer's hash, and the same document without `seedFrom` as a
control; the DPCM's bits against `convertF32ToDpcm`'s, its `normalize` rebuild, a block counted from the plan's first
block allocating nothing, the Pico refusal and the settings refusals. `Dsp.test.cpp` gains a verbatim copy of the old
`convertF32ToDpcm` and `[dsp][dpcm]`, and counts `encodeDpcm` in its `[dsp][noalloc]` loop. `DspNodes.test.cpp`'s Pico
case asserts `heap` for `rp.dsp.dpcm` alone and keeps planning the three processors. `EngineGraph.test.cpp` submits
one document per new type and requires its own settings refusal (a registered type's `settings-invalid`, not an
unregistered `unknown-type`). `OfflineJobs.test.cpp` gains the kit-shaped job that names one buffer twice. `cmake/
tests.cmake` adds the new source and compiles `src/lsdj/Effects.cpp` into `retroplug-graph-test`, so the nodes are
held to `rp::lsdj::processEffect` rather than to a kernel the kit path also calls.

Create `packages/native/test/graph/DspBufferNodes.test.cpp`:

````cpp
// The rp.dsp nodes that read a whole buffer before they play one (src/graph/nodes/DspNodes.cpp,
// docs/modular-graph-phase-d2-design.md section 4): rp.dsp.normalize, rp.dsp.dpcm and rp.dsp.dither's `seedFrom`,
// through real plans built by the graph rig, held to the kit path they replace.
//
// - rp.dsp.normalize's output is today's normalising `GainEffect` through `rp::lsdj::processEffect`, bit for bit, at
//   block sizes of 1, 7, 64 and 1024 frames: a sine, a source so quiet its normalised gain is far above any param's
//   range, a silence (the gain alone) and a negative gain. Its peak is every channel's, and one gain scales them all.
// - A dither's `seedFrom` gives channel 0 the seed `rp::lsdj::processEffect(DitherEffect)` hashes out of the same
//   buffer, for each of the five algorithms, and the other channels their own derivation of it; without one the seed
//   setting stands as it did.
// - rp.dsp.dpcm streams the kernel's delta bits as +1 / -1 audio, sample for sample, normalising or not, at every
//   split.
// - A warmed plan block allocates and frees nothing counted from the plan's first block, the dither's hash and the
//   DPCM bits being built at activate, on the control thread.
// - A settings commit is each node's own: a normalise re-folds a new gain against the peak activate measured, a dither
//   with `seedFrom` restarts a committed seed from the buffer's hash rather than from the setting, and a DPCM
//   `normalize` toggle is a rebuild that re-encodes from the first bit.
// - Each type refuses a document it cannot honour (no buffer named, a resource that is not a live audio buffer), and
//   rp.dsp.dpcm refuses a multi-channel buffer at activate. The DPCM node is the package's one type that declares
//   `heap`, so the Pico refuses it; rp.dsp.normalize allocates nothing and plans there.
//
// The two types' descriptor goldens are DspNodes.test.cpp's case, which walks every type of this package and carries
// the tag `[dsp-nodes]`; this file's tag names only the cases below.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Dpcm.hpp"
#include "dsp/Gain.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "lsdj/Effects.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr double kRate = 48000.0;

// The block sizes a stream is split into: one frame, an odd size, a host block, and the largest block these plans run.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {1024}};

/** A sine, overdriven past [-1, 1] when `amplitude` says so, with a little noise (scaled with it) so no two samples
 *  repeat a code. */
std::vector<float> signal(std::size_t frames, float amplitude = 1.1f) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.05f * amplitude;
        out[i] = noise + amplitude * std::sin(static_cast<float>(i) * 0.031f);
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

/** A float written into a document exactly: `std::to_string` prints six decimals, which would round a gain like
 *  1e-7 to 0, and a setting must reach the node as the case wrote it. */
std::string number(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", static_cast<double>(v));
    return text;
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

/** A rig whose plans may run blocks up to `maxFrames` frames, with a helper for the audio buffers these nodes name. */
struct BufferRig : GraphRig {
    explicit BufferRig(rp::graph::Target target = rp::graph::Target::Desktop, std::uint32_t maxFrames = 2048)
        : GraphRig({kRate, maxFrames, target}, 8, nullptr, maxFrames) {}

    // An audio buffer resource holding one stream per channel, at the plan's rate (these nodes ignore a buffer's rate:
    // they play frames).
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto frames = static_cast<std::uint32_t>(perChannel[0].size());
        const auto id = resources.createAudioBuffer(channels, frames, kRate);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    // An audio buffer resource holding `samples` on every one of its channels.
    rp::graph::ResourceTable::Id buffer(const std::vector<float>& samples, std::uint32_t channels = 1) {
        return buffer(std::vector<std::vector<float>>(channels, samples));
    }

    static std::string res(rp::graph::ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }

    /** Every host channel of the plan's output over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split,
                                           std::uint32_t channels) {
        std::vector<std::vector<float>> out(channels);
        for (auto& channel : out) channel.reserve(frames);
        inBlocks(frames, split, [&](std::size_t, std::size_t n) {
            run(static_cast<std::uint32_t>(n));
            for (std::uint32_t c = 0; c < channels; ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
        });
        return out;
    }

    /** One channel of the host output, this block. */
    std::vector<float> block(std::uint32_t channel, std::size_t frames) {
        return std::vector<float>(outs[channel].begin(), outs[channel].begin() + static_cast<std::ptrdiff_t>(frames));
    }

    /** The plan naming `node` with `settings`, its output on the host's first `channels` channels. */
    rp::graph::SetGraphResult source(const std::string& node, const std::string& settings, std::uint32_t channels) {
        const std::string port = channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
        return set(R"({"nodes":[{"id":"x","type":")" + node + R"(","settings":)" + settings +
                   R"(},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) +
                   R"(]}}],"wires":[{"from":"x.out","to":"out.)" + port + R"("}]})");
    }

    /** The plan playing `sample` into `node` with `settings`, on to the host's first `channels` channels. */
    rp::graph::SetGraphResult play(rp::graph::ResourceTable::Id sample, const std::string& node,
                                   const std::string& settings, std::uint32_t channels) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        const std::string port = channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
        return set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(","loop":false},
                               "resources":{"sample":)" + res(sample) + R"(}},
                   {"id":"x","type":")" + node + R"(","settings":)" + settings +
                   R"(},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) +
                   R"(]}}],"wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.)" + port + R"("}]})");
    }
};

/** `count` frames of the normalising `GainEffect` over `in`, then `tail` frames of silence: what a normalising player
 *  of the same buffer, run `count + tail` frames, has to give. */
std::vector<float> normalised(const std::vector<float>& in, float gain, std::size_t tail) {
    std::vector<float> out = in;
    rp::lsdj::processEffect(rp::lsdj::GainEffect{true, gain}, out, static_cast<float>(kRate));
    out.resize(out.size() + tail, 0.0f);
    return out;
}

}  // namespace

TEST_CASE("rp.dsp.normalize is the kit path's normalising GainEffect, whole or split any way", "[dsp-buffer-nodes]") {
    // A sine; a source so quiet that normalising it multiplies by about 700, far above any param range a document's
    // value would survive; silence, where only the scale shows at all; and a phase flip. The silence case's gain is
    // negative on purpose: 0 * -0.7 is -0.0, which differs from +0 bit for bit, so the case pins the scale where any
    // positive one would give the same +0.
    struct Case {
        const char*        what;
        std::vector<float> in;
        float              gain;
    };
    const std::vector<Case> cases = {
        {"a sine", signal(1024), 0.5f},
        {"a quiet source", signal(1024, 0.0013f), 1.0f},
        {"silence", std::vector<float>(1024, 0.0f), -0.7f},
        {"a negative gain", signal(1024), -0.8f},
    };
    constexpr std::size_t kTail = 137;

    for (const Case& c : cases) {
        INFO(c.what << ", gain " << c.gain);
        // The kit path's own call: the whole buffer, the peak folded into the gain by rp::dsp::normalizedGain.
        const std::vector<float> reference = normalised(c.in, c.gain, kTail);
        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            BufferRig  rig;
            const auto sample = rig.buffer(c.in);
            REQUIRE(rig.source("rp.dsp.normalize",
                               R"({"buffer":)" + BufferRig::res(sample) + R"(,"gain":)" + number(c.gain) +
                                   R"(,"channels":1})",
                               1)
                        .ok);
            const std::vector<float> out = rig.render(c.in.size() + kTail, split, 1)[0];
            CHECK(firstDifference(reference, out) == -1);
        }
    }

    // The quiet source really is past every param's range once normalised, so this is not a case rp.dsp.gain could have
    // carried: 1.0 / peak of a 0.0013-amplitude sine, where the widest param bound is 100.
    CHECK(rp::dsp::normalizedGain(1.0f, rp::dsp::peak(cases[1].in.data(), cases[1].in.size())) > 100.0f);
}

TEST_CASE("rp.dsp.normalize's peak is every channel's, and one gain scales them alike", "[dsp-buffer-nodes]") {
    const std::vector<float> left = signal(512, 0.2f), right = signal(512, 0.7f);
    const float              peak = std::max(rp::dsp::peak(left.data(), left.size()),
                                             rp::dsp::peak(right.data(), right.size()));
    // The louder channel's peak, not each channel's own: a per-channel normalise would put both channels at the same
    // amplitude, where one gain over the mix keeps their balance.
    const float scale = rp::dsp::normalizedGain(0.6f, peak);
    REQUIRE(scale == 0.6f * (1.0f / rp::dsp::peak(right.data(), right.size())));

    BufferRig  rig;
    const auto sample = rig.buffer({left, right});
    REQUIRE(rig.source("rp.dsp.normalize",
                       R"({"buffer":)" + BufferRig::res(sample) + R"(,"gain":0.6,"channels":2})", 2)
                .ok);
    const std::vector<std::vector<float>> out = rig.render(512, {64}, 2);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        INFO("channel " << channel);
        const std::vector<float>& source = channel == 0 ? left : right;
        rp::dsp::Gain             kernel;
        kernel.init(scale);
        std::vector<float> reference(source.size());
        kernel.process(source.data(), reference.data(), source.size());
        CHECK(firstDifference(reference, out[channel]) == -1);
    }

    // A layout channel the buffer lacks is silence, as the player's.
    BufferRig  mono;
    const std::vector<float> quiet = signal(256, 0.4f);
    const auto one = mono.buffer(quiet, 1);
    REQUIRE(mono.source("rp.dsp.normalize",
                        R"({"buffer":)" + BufferRig::res(one) + R"(,"gain":1,"channels":2})", 2)
                .ok);
    const auto pair = mono.render(256, {64}, 2);
    rp::dsp::Gain alone;
    alone.init(rp::dsp::normalizedGain(1.0f, rp::dsp::peak(quiet.data(), quiet.size())));
    std::vector<float> reference(256);
    alone.process(quiet.data(), reference.data(), 256);
    CHECK(firstDifference(reference, pair[0]) == -1);
    CHECK(firstDifference(pair[1], std::vector<float>(256, 0.0f)) == -1);

    // Only the channels the node plays are in the peak: `channels: 1` over a stereo buffer takes the played channel's
    // own, so a quiet channel is not scaled by the loud one it never plays.
    BufferRig  stereoRig;
    const auto wide = stereoRig.buffer({left, right});
    REQUIRE(stereoRig.source("rp.dsp.normalize",
                             R"({"buffer":)" + BufferRig::res(wide) + R"(,"gain":0.6,"channels":1})", 1)
                .ok);
    const std::vector<float> narrow = stereoRig.render(512, {64}, 1)[0];
    rp::dsp::Gain             played;
    played.init(rp::dsp::normalizedGain(0.6f, rp::dsp::peak(left.data(), left.size())));
    std::vector<float> playedReference(left.size());
    played.process(left.data(), playedReference.data(), left.size());
    CHECK(firstDifference(playedReference, narrow) == -1);
    // The unplayed channel's scale is the other answer, and it is a different stream.
    rp::dsp::Gain whole;
    whole.init(scale);
    whole.process(left.data(), playedReference.data(), left.size());
    CHECK(firstDifference(playedReference, narrow) != -1);
}

TEST_CASE("rp.dsp.normalize re-folds a committed gain against the peak its activation measured", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const float              peak = rp::dsp::peak(in.data(), in.size());
    const auto               document = [](const std::string& buffer, float gain) {
        return R"({"nodes":[{"id":"x","type":"rp.dsp.normalize","settings":{"buffer":)" + buffer +
               R"(,"gain":)" + number(gain) + R"(,"channels":1}},
                          {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                 "wires":[{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, 0.5f)).ok);
    rp_node* const instance = rig.host->node("x");
    REQUIRE(instance);
    const std::vector<float> first = rig.render(128, {64}, 1)[0];

    // A plan replacement commits the new gain to the live instance rather than re-activating it, so the peak activate
    // measured stands and only the fold is redone; the same instance proves it was a swap and not a rebuild, and the
    // position carries on, so the stream continues at frame 128.
    REQUIRE(rig.set(document(buffer, -3.5f)).ok);
    CHECK(rig.host->node("x") == instance);
    rp::test::beginCountingAllocations();
    rig.run(64);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
    const std::vector<float> second = rig.block(0, 64);

    const float        scale = rp::dsp::normalizedGain(-3.5f, peak);
    const float        oldScale = rp::dsp::normalizedGain(0.5f, peak);
    std::vector<float> expected(64), unchanged(64);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = in[128 + i] * scale;
        unchanged[i] = in[128 + i] * oldScale;
    }
    CHECK(firstDifference(expected, second) == -1);
    CHECK(firstDifference(unchanged, second) != -1);  // the committed gain really moved the scale
    CHECK(firstDifference(first, second) != -1);
}

TEST_CASE("a dither's seedFrom gives channel 0 the kit path's seed, for every algorithm", "[dsp-buffer-nodes]") {
    struct Case {
        const char*          label;
        rp::lsdj::DitherType type;
        bool                 tpdf;  // one of the two forms that draw noise, so `seedFrom` decides its codes
    };
    const Case cases[] = {
        {"high-pass-tpdf", rp::lsdj::DitherType::HighPassTPDF, true},
        {"shaped-tpdf", rp::lsdj::DitherType::ShapedTPDF, true},
        {"error-diffusion", rp::lsdj::DitherType::ErrorDiffusion, false},
        {"jjn", rp::lsdj::DitherType::JJN, false},
        {"sierra-lite", rp::lsdj::DitherType::SierraLite, false},
    };
    const std::vector<float> in = signal(1024);

    for (const Case& c : cases) {
        INFO(c.label);
        // The kit path's quantise: the same buffer, hashed whole by rp::dsp::inputSeed and quantised at 4 bits.
        std::vector<float> reference = in;
        rp::lsdj::processEffect(rp::lsdj::DitherEffect{c.type}, reference, static_cast<float>(kRate));

        BufferRig  rig;
        const auto sample = rig.buffer(in);  // the buffer the dither plays and hashes: one resource, named twice
        // The seed setting is a different word on purpose: with `seedFrom` set it is not what seeds the noise.
        const std::string settings = R"({"channels":1,"algorithm":")" + std::string(c.label) +
                                     R"(","bits":4,"seed":17,"seedFrom":)" + BufferRig::res(sample) + "}";
        REQUIRE(rig.play(sample, "rp.dsp.dither", settings, 1).ok);
        const std::vector<float> out = rig.render(in.size(), {64}, 1)[0];
        REQUIRE(out.size() == reference.size());

        // The node's levels are the codes' levels (rp::dsp::codeToLevel), and the code recovered from a level is the
        // code itself, so this is the kit path's code stream and not merely another one at the same levels.
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto code = static_cast<std::uint32_t>(reference[i]);
            INFO("frame " << i);
            REQUIRE(out[i] == rp::dsp::codeToLevel(code, 4));
            CHECK(static_cast<std::uint32_t>(std::round((out[i] + 1.0f) * 0.5f * 15.0f)) == code);
        }

        // Three of the five forms draw no noise at all (the kernel's init salts only the TPDF forms), so for those
        // whatever seeds them they give the same codes and the levels above are all the case can pin. The two that do
        // draw noise are held to the hash by a control: the same document without `seedFrom`, seeded with the 17 the
        // settings name, gives a different stream.
        if (!c.tpdf) continue;
        BufferRig        seeded;
        const auto       seededSample = seeded.buffer(in);
        const std::string withoutSeedFrom = R"({"channels":1,"algorithm":")" + std::string(c.label) +
                                            R"(","bits":4,"seed":17})";
        REQUIRE(seeded.play(seededSample, "rp.dsp.dither", withoutSeedFrom, 1).ok);
        const std::vector<float> other = seeded.render(in.size(), {64}, 1)[0];
        CHECK(firstDifference(out, other) != -1);
    }
}

TEST_CASE("a dither's seedFrom seeds the other channels the way the seed setting does", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    BufferRig                rig;
    const auto               sample = rig.buffer(in, 2);
    REQUIRE(rig.play(sample, "rp.dsp.dither",
                     R"({"channels":2,"algorithm":"high-pass-tpdf","bits":4,"seedFrom":)" + BufferRig::res(sample) +
                         "}",
                     2)
                .ok);
    const std::vector<std::vector<float>> out = rig.render(512, {64}, 2);

    // Channel 0 is the hash itself; channel 1 is that seed plus the node's golden-ratio step, as every channel past 0
    // is seeded from whatever word the stream started from.
    const std::uint32_t hashed = rp::dsp::inputSeed(in.data(), in.size());
    rp::dsp::Dither first;
    first.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, hashed);
    std::vector<float> reference(512);
    first.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[0]) == -1);

    rp::dsp::Dither second;
    second.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, hashed + 0x9e3779b9u);
    second.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[1]) == -1);
    CHECK(firstDifference(out[0], out[1]) != -1);
}

TEST_CASE("a dither without seedFrom keeps its seed setting", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    BufferRig                rig;
    const auto               sample = rig.buffer(in);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"sierra-lite","bits":4,"seed":12345})", 1)
                .ok);
    const std::vector<float> out = rig.render(512, {64}, 1)[0];

    rp::dsp::Dither kernel;
    kernel.init(rp::dsp::DitherAlgorithm::SierraLite, 4, 12345);
    std::vector<float> reference(512);
    kernel.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out) == -1);
}

TEST_CASE("a dither with seedFrom restarts a committed seed from the buffer's hash", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const auto document = [](const std::string& buffer, std::uint32_t seed) {
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,
                             "layout":"mono","loop":false},"resources":{"sample":)" + buffer + R"(}},
                         {"id":"x","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf",
                          "bits":4,"seed":)" + std::to_string(seed) + R"(,"seedFrom":)" + buffer + R"(}},
                         {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, 7)).ok);
    const std::vector<float> first = rig.render(256, {64}, 1)[0];

    // The commit restarts the noise, and `seedFrom` still stands in the seed's place: the stream from there is a fresh
    // kernel's from the buffer's hash over the frames from 256, not one from 99 and not the old sequence carried on.
    // The player keeps its place across the swap, as it does without a seed change.
    REQUIRE(rig.set(document(buffer, 99)).ok);
    const std::vector<float> second = rig.render(256, {64}, 1)[0];

    std::vector<float> expected(256);
    rp::dsp::Dither     fromHash;
    fromHash.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, rp::dsp::inputSeed(in.data(), in.size()));
    fromHash.processLevels(in.data() + 256, expected.data(), expected.size());
    CHECK(firstDifference(expected, second) == -1);

    rp::dsp::Dither fromSetting;
    fromSetting.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 99);
    fromSetting.processLevels(in.data() + 256, expected.data(), expected.size());
    CHECK(firstDifference(expected, second) != -1);
    CHECK(firstDifference(first, second) != -1);
}

TEST_CASE("rp.dsp.dpcm streams the kernel's delta bits as plus and minus one", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(1024);
    constexpr std::size_t    kTail = 71;

    for (const bool normalize : {true, false}) {
        std::vector<std::uint8_t> bits(in.size());
        rp::dsp::encodeDpcm(in.data(), in.size(), normalize, bits.data());
        std::vector<float> reference(in.size() + kTail, 0.0f);
        for (std::size_t i = 0; i < in.size(); ++i) reference[i] = bits[i] ? 1.0f : -1.0f;
        // Both polarities are in the stream, so a node that wrote one level throughout would not pass by matching
        // the kernel's bits alone.
        REQUIRE(std::count(bits.begin(), bits.end(), std::uint8_t{1}) > 0);
        REQUIRE(std::count(bits.begin(), bits.end(), std::uint8_t{0}) > 0);

        for (const auto& split : kSplits) {
            INFO("normalize " << normalize << ", split starting " << split[0]);
            BufferRig  rig;
            const auto sample = rig.buffer(in);
            REQUIRE(rig.source("rp.dsp.dpcm",
                               R"({"buffer":)" + BufferRig::res(sample) + R"(,"normalize":)" +
                                   (normalize ? "true" : "false") + "}",
                               1)
                        .ok);
            CHECK(firstDifference(reference, rig.render(in.size() + kTail, split, 1)[0]) == -1);
        }
    }
}

TEST_CASE("rp.dsp.dpcm's normalize toggle rebuilds and plays the other encoding from the first bit",
          "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const auto document = [](const std::string& buffer, bool normalize) {
        return R"({"nodes":[{"id":"x","type":"rp.dsp.dpcm","settings":{"buffer":)" + buffer +
               R"(,"normalize":)" + (normalize ? "true" : "false") + R"(}},
                          {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                 "wires":[{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, true)).ok);
    const std::vector<float> first = rig.render(512, {64}, 1)[0];

    // Re-encoding is a whole-buffer pass on the control thread, so a change of `normalize` is a rebuild: the fresh
    // instance encodes in activate and plays the new bit stream from its first bit, not the old stream carried on.
    REQUIRE(rig.set(document(buffer, false)).ok);
    const std::vector<float> second = rig.render(512, {64}, 1)[0];

    std::vector<std::uint8_t> bits(in.size());
    rp::dsp::encodeDpcm(in.data(), in.size(), false, bits.data());
    std::vector<float> expected(in.size());
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = bits[i] ? 1.0f : -1.0f;
    CHECK(firstDifference(expected, second) == -1);
    CHECK(firstDifference(first, second) != -1);  // and the two encodings are different streams
}

TEST_CASE("the buffer-reading nodes' plan allocates and frees nothing from its first block", "[dsp-buffer-nodes]") {
    BufferRig  rig;
    const auto normalisedSource = rig.buffer(signal(1024, 0.9f));
    const auto dpcmSource = rig.buffer(signal(1024, 0.5f));
    const auto ditherSource = rig.buffer(signal(1024));
    REQUIRE(rig.set(R"({"nodes":[
                        {"id":"n","type":"rp.dsp.normalize","settings":{"buffer":)" +
                    BufferRig::res(normalisedSource) + R"(,"gain":0.3,"channels":1}},
                        {"id":"c","type":"rp.dsp.dpcm","settings":{"buffer":)" + BufferRig::res(dpcmSource) +
                    R"(}},
                        {"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                         "resources":{"sample":)" + BufferRig::res(ditherSource) + R"(}},
                        {"id":"d","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,
                         "seedFrom":)" + BufferRig::res(ditherSource) + R"(}},
                        {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1,1]}}],
                      "wires":[{"from":"n.out","to":"out.out.1"},{"from":"c.out","to":"out.out.2"},
                               {"from":"p.out","to":"d.in"},{"from":"d.out","to":"out.out.3"}]})")
                .ok);

    // Counted from the plan's first block, where a node that hashed or encoded inside `process` would allocate: the
    // dither's hash and the DPCM bits are built at activate, on the control thread.
    rp::test::beginCountingAllocations();
    rig.run(64, 6);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("the buffer-reading nodes refuse a document they cannot honour", "[dsp-buffer-nodes]") {
    BufferRig rig;

    struct Refusal {
        const char* node;
        const char* noBuffer;
        const char* missingBuffer;
    };
    for (const Refusal& c : {Refusal{"rp.dsp.normalize", R"({"channels":1})", R"({"buffer":"res:9999","channels":1})"},
                             Refusal{"rp.dsp.dpcm", "{}", R"({"buffer":"res:9999"})"}}) {
        INFO(c.node);
        // No buffer named: a settings error with a reason, since a normalise or a DPCM without one is silence nobody
        // asked for rather than a document the node can play.
        const auto none = rig.source(c.node, c.noBuffer, 1);
        REQUIRE_FALSE(none.ok);
        REQUIRE_FALSE(none.errors.empty());
        CHECK(none.errors[0].code == "settings-invalid");
        CHECK(none.errors[0].message.find("names an audio buffer") != std::string::npos);

        // A ref that names nothing live is refused when the instance is created, with the field's name.
        const auto missing = rig.source(c.node, c.missingBuffer, 1);
        REQUIRE_FALSE(missing.ok);
        REQUIRE_FALSE(missing.errors.empty());
        CHECK(missing.errors[0].message.find("buffer") != std::string::npos);
    }

    // A resource of another type is refused the same way.
    std::string why;
    const auto  bytes = rig.resources.createBytes("rom", {1, 2, 3}, &why);
    REQUIRE(bytes != 0);
    const auto wrong = rig.source("rp.dsp.normalize", R"({"buffer":)" + BufferRig::res(bytes) + R"(,"channels":1})", 1);
    REQUIRE_FALSE(wrong.ok);
    CHECK(wrong.errors[0].message.find("buffer") != std::string::npos);

    // A channel count is a document's, and the port's layout is labelled from it, so an impossible one is refused
    // rather than clamped.
    const auto live = rig.buffer(signal(64));
    for (const std::string& settings :
         {R"({"buffer":)" + BufferRig::res(live) + R"(,"channels":0})",
          R"({"buffer":)" + BufferRig::res(live) + R"(,"channels":65})"}) {
        INFO(settings);
        const auto r = rig.source("rp.dsp.normalize", settings, 1);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 64") != std::string::npos);
    }

    // A DPCM node is one DMC channel's: a stereo buffer fails the activation, with a reason, rather than mixing or
    // dropping a channel silently.
    const auto stereo = rig.buffer({signal(64), signal(64, 0.5f)});
    const auto refused =
        rig.source("rp.dsp.dpcm", R"({"buffer":)" + BufferRig::res(stereo) + R"(,"normalize":true})", 1);
    REQUIRE_FALSE(refused.ok);
    REQUIRE_FALSE(refused.errors.empty());
    CHECK(refused.errors[0].code == "activate-failed");
    CHECK(refused.errors[0].message.find("mono") != std::string::npos);
}

TEST_CASE("the Pico refuses rp.dsp.dpcm, which builds storage, and plans rp.dsp.normalize", "[dsp-buffer-nodes]") {
    // The DPCM node's bit storage is built at activate, so the type declares heap and the planner refuses it there.
    const rp_node_type* type = nullptr;
    for (std::uint32_t i = 0; i < rp::graph::dspPackage()->node_type_count; ++i) {
        const rp_node_type* candidate = rp::graph::dspPackage()->node_types[i];
        if (std::string(candidate->id) == "rp.dsp.dpcm") type = candidate;
    }
    REQUIRE(type);
    CHECK(std::string(type->descriptor(type)).find(R"("requires":{"heap":true,"threads":false})") != std::string::npos);

    BufferRig  pico(rp::graph::Target::Pico);
    const auto dpcmSource = pico.buffer(signal(256));
    CHECK(GraphRig::codes(pico.source("rp.dsp.dpcm", R"({"buffer":)" + BufferRig::res(dpcmSource) + "}", 1)) ==
          "capability");

    // A normalising player allocates nothing, so it plans and runs there.
    BufferRig  pico2(rp::graph::Target::Pico);
    const auto source = pico2.buffer(signal(256));
    REQUIRE(pico2.source("rp.dsp.normalize",
                         R"({"buffer":)" + BufferRig::res(source) + R"(,"gain":0.5,"channels":1})", 1)
                .ok);
    pico2.run(64);
    const std::vector<float> played = pico2.block(0, 64);
    CHECK(std::any_of(played.begin(), played.end(), [](float v) { return v != 0.0f; }));
}
````

Run: `mkdir -p packages/native/test/graph; sed -n '3990,4599p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/DspBufferNodes.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/DspBufferNodes.test.cpp`
Expected: `d7b629e734f5aa24e24a89666bd60b8a95e08510`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 7b6a4f227..d2d1e35ee 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -266,7 +266,12 @@ retroplug_add_test_binary(
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
              test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's math
              test/graph/DspNodes.test.cpp             # the rp.dsp nodes, through real plans, against those kernels
+             test/graph/DspBufferNodes.test.cpp       # the rp.dsp nodes that read a whole buffer: normalize, DPCM, seedFrom
              test/graph/DspResample.test.cpp          # rp.dsp.resample, through real plans, against its kernel
+             # The kit path's effect wrappers, so the two buffer-reading nodes are held to what
+             # rp::lsdj::processEffect does to a buffer, not only to the kernels underneath it. They need the kernels
+             # and reflect-cpp (through retroplug-graph), nothing of the core.
+             ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     LIBS     retroplug-graph
              retroplug-dsp-nodes                      # the rp.dsp package under test
              retroplug-dsp-resample-nodes             # and the rp.dsp.resample package
diff --git a/packages/native/test/audio/KitBaseline.test.cpp b/packages/native/test/audio/KitBaseline.test.cpp
index 4604f8f86..a181fee3d 100644
--- a/packages/native/test/audio/KitBaseline.test.cpp
+++ b/packages/native/test/audio/KitBaseline.test.cpp
@@ -354,7 +354,7 @@ std::vector<KitCase> kitCases() {
         // `loop.value_or(false)`): the non-normalising 7-bit map and the loop flag in the index entry's byte 3.
         {"risa/no-normalize-rate-12", KitCodec::Risa, Signal::Noise, 44100, {}, true, 12, false},
         {"risa/loop-rate-4", KitCodec::Risa, Signal::Sine440, 44100, {}, true, 4, true, true},
-        // A silent source pins `convertF32ToDpcm`'s peak floor of 1 at both remaining rates, and with it the
+        // A silent source pins the DPCM kernel's peak floor of 1 at both remaining rates, and with it the
         // no-peak branch of the DMC normalise.
         {"risa/normalize-silence-rate-12", KitCodec::Risa, Signal::Silence, 22050, {}, true, 12},
         {"risa/normalize-silence-rate-4", KitCodec::Risa, Signal::Silence, 48000, {}, true, 4},
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 699c59bec..7a627966b 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -1151,8 +1151,9 @@ TEST_CASE("the engine registers both DSP packages, so a document naming every ty
     // A package the engine refuses is a stderr line at construction and nothing else, and every `[dsp-nodes]` and
     // `[dsp-resample]` case runs on the graph rig, which registers its own copy: without this, a registration that
     // broke would leave the types unplannable on the one host that has them and fail no test. One document names all
-    // four, chained from a resampler with no buffer (silence) to the host output, so planning it is the proof; the
-    // inputs are all silence, so the only thing to hold the block to is that it runs with no fault.
+    // four of the nodes that need no buffer, chained from a resampler with no buffer (silence) to the host output, so
+    // planning it is the proof; the inputs are all silence, so the only thing to hold the block to is that it runs
+    // with no fault.
     Engine e(kRate);
     const std::string document =
         R"({"nodes":[)"
@@ -1175,6 +1176,20 @@ TEST_CASE("the engine registers both DSP packages, so a document naming every ty
     float*             outs[2] = {&lanes[0], &lanes[256]};
     e.processBlock(256, outs, 2);
     CHECK(e.faultCount() == 0);
+
+    // The two types that read a whole buffer are in the same packages, but their nodes cannot plan here: an
+    // "audio-buffer" resource is a resource table's createAudioBuffer, which only a job's table does, so these
+    // documents name none. They are still the proof of registration: each is refused by its own settings check, where
+    // a type the engine did not register is refused as unknown-type.
+    for (const char* type : {"rp.dsp.normalize", "rp.dsp.dpcm"}) {
+        INFO(type);
+        const auto r = e.setGraph(rp::host::test::graphRequest(
+            R"({"nodes":[{"id":"x","type":")" + std::string(type) + R"(","settings":{}}],"wires":[]})", {}));
+        REQUIRE_FALSE(r.ok);
+        REQUIRE_FALSE(r.errors.empty());
+        CHECK(r.errors[0].code == "settings-invalid");
+        CHECK(r.errors[0].message.find("names an audio buffer") != std::string::npos);
+    }
 }
 
 TEST_CASE("the idle probe a host's smoke arms publishes a plan and the one that replaces it, and reports the idle tick "
diff --git a/packages/native/test/engine-graph/OfflineJobs.test.cpp b/packages/native/test/engine-graph/OfflineJobs.test.cpp
index 12f2a3629..fc3be6ca0 100644
--- a/packages/native/test/engine-graph/OfflineJobs.test.cpp
+++ b/packages/native/test/engine-graph/OfflineJobs.test.cpp
@@ -4,8 +4,10 @@
 // cancel stops a queued job at once and a running one at its next block, with the cancel reason; an engine destroyed with
 // jobs in flight abandons them and joins every worker; the outstanding bound refuses; every refusal says why, at
 // submission or in the completion; a file input is SampleCache's decode, windowed exactly as the two kit compilers
-// window today, the cap's two arithmetics and every edge included, with its report; and a job run while an audio thread
-// plays the live plan leaves that plan's output bit for bit, its warmed blocks allocating nothing.
+// window today, the cap's two arithmetics and every edge included, with its report; a kit-shaped job names one buffer
+// twice - as the normalising source's and as the dither's `seedFrom` - beside the DPCM encode, and gives the kit
+// arithmetic for all three; and a job run while an audio thread plays the live plan leaves that plan's output bit for
+// bit, its warmed blocks allocating nothing.
 //
 // Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[offline]"`; the hidden
 // "[offline-bench]" times a trivial job and a one-second resample.
@@ -29,6 +31,9 @@
 #include "../graph/CountingAllocator.hpp"
 #include "../graph/GraphRig.hpp"
 #include "GraphDocs.hpp"
+#include "dsp/Dither.hpp"
+#include "dsp/Dpcm.hpp"
+#include "dsp/Gain.hpp"
 #include "dsp/Resample.hpp"
 #include "graph/node.hpp"
 #include "host/engine/Engine.hpp"
@@ -348,6 +353,73 @@ TEST_CASE("an offline job runs a document to completion and returns what the sam
     CHECK(jobs.outstanding() == 0);
 }
 
+TEST_CASE("a kit-shaped job names one buffer twice and runs the buffer-reading nodes on its worker",
+          "[engine-graph][offline]") {
+    // The shape a kit compile submits: one input named by a normalising source and by the dither's `seedFrom`, and the
+    // same buffer encoded to DMC bits. The worker is the job's control and block thread at once, so this is where the
+    // DPCM node's storage is allocated and the dither's hash is taken; the pool's own registry has to hold every
+    // rp.dsp type, and one handle named twice has to resolve to the one resource.
+    const std::vector<float> input = signal(2048, 1, 5);
+    const std::uint32_t      frames = static_cast<std::uint32_t>(input.size());
+    OfflineJobs              jobs;
+    OfflineRequest           r;
+    r.document = R"({"nodes":[
+        {"id":"n","type":"rp.dsp.normalize","settings":{"buffer":"res:1","gain":-0.7,"channels":1}},
+        {"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
+         "resources":{"sample":"res:1"}},
+        {"id":"d","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,
+         "seedFrom":"res:1"}},
+        {"id":"c","type":"rp.dsp.dpcm","settings":{"buffer":"res:1","normalize":true}},
+        {"id":"rn","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:2"}},
+        {"id":"rd","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:3"}},
+        {"id":"rc","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:4"}}],
+        "wires":[{"from":"n.out","to":"rn.in"},{"from":"p.out","to":"d.in"},{"from":"d.out","to":"rd.in"},
+                 {"from":"c.out","to":"rc.in"}]})";
+    r.inputs.emplace_back(OfflineSamples{input, 1, 48000.0});
+    for (int i = 0; i < 3; ++i) r.outputs.push_back({1, frames, 48000.0});
+    r.returns = {2, 3, 4};
+    r.frames = frames;
+    r.rate = 48000.0;
+    const auto s = jobs.submit(r);
+    INFO(s.error);
+    REQUIRE(s.id != 0);
+    const auto c = one(jobs, s.id);
+    INFO(c.error);
+    REQUIRE(c.ok);
+    REQUIRE(c.buffers.size() == 3);
+    CHECK(c.buffers[0].handle == 2);
+    CHECK(c.buffers[1].handle == 3);
+    CHECK(c.buffers[2].handle == 4);
+
+    // The normalising gain, as the kit path folds it.
+    std::vector<float> normalised(frames);
+    {
+        rp::dsp::Gain gain;
+        gain.init(rp::dsp::normalizedGain(-0.7f, rp::dsp::peak(input.data(), input.size())));
+        gain.process(input.data(), normalised.data(), input.size());
+    }
+    CHECK(c.buffers[0].samples == normalised);
+
+    // The dither's levels, from the hash of the whole buffer the job hands both the player and `seedFrom`.
+    std::vector<std::uint16_t> codes(frames);
+    {
+        rp::dsp::Dither dither;
+        dither.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, rp::dsp::inputSeed(input.data(), input.size()));
+        dither.process(input.data(), codes.data(), frames);
+    }
+    std::vector<float> levels(frames);
+    for (std::size_t i = 0; i < frames; ++i) levels[i] = rp::dsp::codeToLevel(codes[i], 4);
+    CHECK(c.buffers[1].samples == levels);
+
+    // The DMC bits as levels, the same encode the codec's bytes come from.
+    std::vector<std::uint8_t> bits(frames);
+    rp::dsp::encodeDpcm(input.data(), input.size(), true, bits.data());
+    std::vector<float> dpcm(frames);
+    for (std::size_t i = 0; i < frames; ++i) dpcm[i] = bits[i] ? 1.0f : -1.0f;
+    CHECK(c.buffers[2].samples == dpcm);
+    CHECK(jobs.outstanding() == 0);
+}
+
 TEST_CASE("offline jobs past the pool's concurrency queue, and each returns its own result", "[engine-graph][offline]") {
     OfflineJobs jobs;
     const std::size_t count = std::min<std::size_t>(3 * OfflineJobs::concurrency() + 1, OfflineJobs::kMaxJobs);
diff --git a/packages/native/test/graph/Dsp.test.cpp b/packages/native/test/graph/Dsp.test.cpp
index 78f5c794a..19da8f4e2 100644
--- a/packages/native/test/graph/Dsp.test.cpp
+++ b/packages/native/test/graph/Dsp.test.cpp
@@ -17,6 +17,9 @@
 // - Resample: the output stream is independent of how the input arrives (a source shorter than one chunk included),
 //   and equals a direct fixed-chunk feed of r8brain, written out here as a reference; a failed allocation in `init`
 //   throws and leaves the resampler unbuilt.
+// - DPCM: the kernel's one bit per sample, packed the way the risa codec packs them, is the codec's old whole-buffer
+//   function's bytes, written out here verbatim: normalising or not, a short buffer, a long one, an overdriven one and
+//   silence.
 //
 // The kit baseline (test/audio/KitBaseline.test.cpp) holds the same kernels to whole kit banks' bytes.
 #include <catch2/catch_test_macros.hpp>
@@ -31,11 +34,13 @@
 #include <random>
 #include <thread>
 #include <type_traits>
+#include <utility>
 #include <vector>
 
 #include "CountingAllocator.hpp"
 #include "dsp/Biquad.hpp"
 #include "dsp/Dither.hpp"
+#include "dsp/Dpcm.hpp"
 #include "dsp/Gain.hpp"
 #include "dsp/R8bAllocator.hpp"  // r8brain, for the reference resample below
 #include "dsp/Resample.hpp"
@@ -459,6 +464,92 @@ std::vector<float> streamResample(double inRate, double outRate, const std::vect
     return out;
 }
 
+// ---- risa's DMC encoder as it was before the kernel ---------------------------------------------------------------
+// The codec's whole-buffer function, verbatim, with the byte packing and the pad it did around the bits. The kernel
+// outputs one bit per sample instead, so a case packs its bits the same way before comparing the two.
+
+constexpr std::size_t kDmcLengthStep = 16;
+
+// Floor-division toward -infinity (b > 0). C integer division truncates toward zero, which diverges from wav2dmc's
+// Python `//` on negative numerators: load-bearing for the 7-bit map.
+long referenceFloorDiv(long a, long b) {
+    long q = a / b;
+    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
+    return q;
+}
+
+// Pad a bit-packed DMC stream to a legal 16k+1 length with 0x00 bytes.
+std::vector<std::uint8_t> referencePadToDmcLength(std::vector<std::uint8_t> data) {
+    if (data.empty()) return { 0 };
+    const std::size_t rem = (data.size() - 1) % kDmcLengthStep;
+    if (rem != 0) data.resize(data.size() + (kDmcLengthStep - rem), 0);
+    return data;
+}
+
+// Encode resampled float audio into NES DMC bytes: wav2dmc.py's post-resample pipeline (DC blocker, 7-bit map centered
+// on 64, 1-bit plus/minus 2 delta, LSB-first packing, pad), in the int16 domain.
+std::vector<std::uint8_t> referenceConvertF32ToDpcm(const std::vector<float>& f32, bool normalize) {
+    const std::size_t n = f32.size();
+    if (n == 0) return { 0 };
+
+    std::vector<std::int32_t> s(n);
+    for (std::size_t i = 0; i < n; ++i) {
+        const float v = std::clamp(f32[i] * 32767.0f, -32768.0f, 32767.0f);
+        s[i] = static_cast<std::int32_t>(std::lround(v));
+    }
+
+    // DC blocker: one-pole, R = 255/256, truncate toward zero, clamp int16. State x1 = y1 = 0.
+    const double R = 255.0 / 256.0;
+    double x1 = 0.0, y1 = 0.0;
+    for (std::size_t i = 0; i < n; ++i) {
+        const double x = static_cast<double>(s[i]);
+        const double y = x - x1 + R * y1;
+        x1 = x;
+        y1 = y;
+        long v = static_cast<long>(y); // truncates toward zero
+        v = std::clamp(v, -32768L, 32767L);
+        s[i] = static_cast<std::int32_t>(v);
+    }
+
+    // int16 -> 7-bit unsigned centered on 64.
+    std::vector<std::uint8_t> u7(n);
+    if (normalize) {
+        long peak = 1;
+        for (auto v : s) peak = std::max(peak, static_cast<long>(std::abs(v)));
+        for (std::size_t i = 0; i < n; ++i) {
+            const long q = 64 + referenceFloorDiv(static_cast<long>(s[i]) * 45, peak);
+            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
+        }
+    } else {
+        for (std::size_t i = 0; i < n; ++i) {
+            const long q = 64 + referenceFloorDiv(static_cast<long>(s[i]) * 63, 32768);
+            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
+        }
+    }
+
+    // 1-bit plus/minus 2 delta encode, LSB-first packing. Counter starts 64; guards <127 / >0 (the deliberate
+    // plus/minus 2 clamp quirk the decoder mirrors, NOT a clamp(0,127)).
+    std::vector<std::uint8_t> out((n + 7) / 8, 0);
+    int counter = 64;
+    for (std::size_t i = 0; i < n; ++i) {
+        if (static_cast<int>(u7[i]) >= counter) {
+            out[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
+            if (counter < 127) counter += 2;
+        } else {
+            if (counter > 0) counter -= 2;
+        }
+    }
+    return referencePadToDmcLength(std::move(out));
+}
+
+/** The kernel's one-byte-per-bit output packed and padded the way the codec packs and pads it. */
+std::vector<std::uint8_t> packDpcmBits(const std::vector<std::uint8_t>& bits) {
+    std::vector<std::uint8_t> out((bits.size() + 7) / 8, 0);
+    for (std::size_t i = 0; i < bits.size(); ++i)
+        if (bits[i]) out[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
+    return referencePadToDmcLength(std::move(out));
+}
+
 } // namespace
 
 TEST_CASE("the gain kernel multiplies exactly and a block split changes nothing", "[dsp][gain]") {
@@ -788,6 +879,26 @@ TEST_CASE("the dithers without a padded reference give the old whole-buffer loop
     }
 }
 
+TEST_CASE("the DPCM kernel's bits are the kit codec's bytes", "[dsp][dpcm]") {
+    // A short buffer; one longer than any kit slot resamples to; an overdriven source, where the DC blocker's clamp and
+    // the delta counter's ends both do work; and silence, which pins the peak floor of 1 and the sign of every step.
+    const std::vector<std::vector<float>> sources = {signal(37), signal(20000), noise(1000, 4.0f, 0x51ee7u),
+                                                     std::vector<float>(500, 0.0f)};
+    for (const auto& source : sources) {
+        for (const bool normalize : {false, true}) {
+            INFO(source.size() << " frames, normalize " << normalize);
+            std::vector<std::uint8_t> bits(source.size());
+            rp::dsp::encodeDpcm(source.data(), source.size(), normalize, bits.data());
+            for (const auto bit : bits) REQUIRE((bit == 0 || bit == 1));
+            CHECK(packDpcmBits(bits) == referenceConvertF32ToDpcm(source, normalize));
+        }
+    }
+
+    // No samples: no bits, and the one-byte slot the codec's pad gives an empty stream.
+    CHECK(packDpcmBits({}) == std::vector<std::uint8_t>{0});
+    CHECK(referenceConvertF32ToDpcm({}, true) == std::vector<std::uint8_t>{0});
+}
+
 TEST_CASE("the resampler's output stream is independent of how its input arrives", "[dsp][resample]") {
     struct Case {
         double inRate, outRate;
@@ -968,6 +1079,7 @@ TEST_CASE("an initialised kernel processes without allocating or freeing", "[dsp
     biquad.init({BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f});
     rp::dsp::Dither dithers[std::size(kAlgorithms)];
     for (std::size_t a = 0; a < std::size(kAlgorithms); ++a) dithers[a].init(kAlgorithms[a], 4, 99);
+    std::vector<std::uint8_t> dpcmBits(1024);
     // The resampler is counted from its first chunk, not warmed: r8brain sizes every buffer it uses in its
     // constructor, which `init` runs, so not even the first `process` may allocate.
     rp::dsp::Resampler resampler;
@@ -984,6 +1096,8 @@ TEST_CASE("an initialised kernel processes without allocating or freeing", "[dsp
             d.processLevels(src, out.data(), 1024);
         }
         (void)rp::dsp::inputSeed(src, 1024);
+        rp::dsp::encodeDpcm(src, 1024, true, dpcmBits.data());
+        rp::dsp::encodeDpcm(src, 1024, false, dpcmBits.data());
         std::size_t taken = 0;
         while (taken < 1024) {
             taken += resampler.push(src + taken, 1024 - taken);
diff --git a/packages/native/test/graph/DspNodes.test.cpp b/packages/native/test/graph/DspNodes.test.cpp
index b6a6e497b..7846aa3b8 100644
--- a/packages/native/test/graph/DspNodes.test.cpp
+++ b/packages/native/test/graph/DspNodes.test.cpp
@@ -13,8 +13,10 @@
 // - A dither's settings are its stream's: a new seed restarts the noise, and the node's output is the kernel's
 //   quantised levels, so a live patch and a kit compile run the same arithmetic.
 // - A plan block allocates and frees nothing counted from the plan's first block (the suite's counting allocator), and
-//   all three types plan on the Pico: they declare neither heap nor threads, and their extra memory is requested for
-//   the settings' channel count (the request's size is the assertion), never for the descriptor's maximum.
+//   gain, biquad and dither plan on the Pico: they declare neither heap nor threads, and their extra memory is
+//   requested for the settings' channel count (the request's size is the assertion), never for the descriptor's
+//   maximum. (The package's two buffer-reading types are DspBufferNodes.test.cpp's, which also holds the DPCM node's
+//   `heap` and the Pico's refusal of it.)
 // - Each type's descriptor is a golden beside golden/rp.test.gain.descriptor.json, recorded with
 //   RP_GRAPH_GOLDEN_UPDATE=1.
 #include <catch2/catch_test_macros.hpp>
@@ -772,7 +774,9 @@ TEST_CASE("the three rp.dsp nodes plan on the Pico", "[dsp-nodes]") {
         INFO(type->id);
         auto parsed = rp::graph::parseDescriptor(type->descriptor(type));
         REQUIRE(parsed);
-        CHECK_FALSE(parsed->heap);
+        // rp.dsp.dpcm builds its bit storage at activate, so it is this package's one type the Pico refuses;
+        // DspBufferNodes.test.cpp plans it there for the refusal.
+        CHECK(parsed->heap == (std::string(type->id) == "rp.dsp.dpcm"));
         CHECK_FALSE(parsed->threads);
     }
 
````

Run: `sed -n '4609,4969p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build && cmake --build build --target retroplug-graph-test -j$(nproc)`
Expected: the configure exits 0, then the build stops compiling `DspBufferNodes.test.cpp`: `dsp/Dpcm.hpp` does not exist yet. That is this task's red.

- [ ] **Step 2: The DMC kernel and the codec on it**

`rp::dsp::encodeDpcm(const float*, frames, bool normalize, uint8_t* bits)` is the codec's int16 conversion, double
one-pole DC blocker, 7-bit map (the 45-swing normalised or 63/32768) and 1-bit delta as one whole-buffer call writing
one byte per bit into caller storage. It keeps no scratch: pass 1 computes the DC-blocked peak and pass 2 recomputes
the same samples and maps them straight into the delta loop, so the function never allocates. `Dpcm.cpp` joins
`retroplug-dsp-kernels`; `RisaDmcCodec::encode` calls the kernel and keeps packing the bits LSB first, padding to `16k
+ 1` and truncating to 4081, so its bytes do not move.

Create `packages/native/src/dsp/Dpcm.hpp`:

````cpp
#pragma once

// risa's NES DMC encoder, as a whole-buffer kernel (docs/modular-graph-phase-d2-design.md section 4).
//
// wav2dmc.py's pipeline, which is what the risa kit codec has always run: the float scaled to the int16 domain and
// rounded, a one-pole DC blocker in double with its state carried from sample to sample, the 7-bit map into 0..127
// around 64 (peak-normalised to a 45 swing, or scaled by 63 / 32768), and the 1-bit delta that walks a counter between
// 0 and 127 two steps at a time. The delta bits are the encoder's output: one per sample, in stream order. The caller
// packs them least-significant bit first and pads the result to a legal DMC length, because those two are the risa
// bank's format rather than the encoder's arithmetic (RisaDmcCodec::encode).
//
// One call over a whole buffer, with no scratch of its own: the DC blocker's two doubles and a running peak are the
// whole state, and the second pass recomputes the first pass's samples rather than keeping one int per sample, so a
// caller that sizes `bits` from the frame count gets an encoder that allocates nothing. rp.dsp.dpcm runs it at
// activate, on the control thread, into storage it owns for its lifetime; nothing here touches the heap.
//
// A different shape from the other kernels in src/dsp/: those stream a block at a time, because a plan feeds them
// blocks, where this one needs every sample before it writes its first bit (the peak normalises the whole buffer). A
// node that plays the bits out one block at a time runs the encoder once, ahead of the stream, which is the whole of
// why rp.dsp.dpcm declares `heap`.

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

// Writes `frames` bytes, one per sample, into caller-owned `bits`: 1 where the delta stepped the counter up, 0 where
// it stepped it down. `in` and `bits` must not overlap. The bytes agree with the risa codec's whatever the host: the
// int16 conversion, the 7-bit map and the delta are integer arithmetic, and the DC blocker is one expression evaluated
// in double, sample for sample.
//
// `normalize` is risa's per-sample flag: on, the DC-blocked buffer's peak (never below 1) divides the 7-bit map's
// swing, so the loudest sample lands near full scale; off, the map is a plain 63 / 32768 scale.
void encodeDpcm(const float* in, std::size_t frames, bool normalize, std::uint8_t* bits) noexcept;

} // namespace rp::dsp
````

Run: `mkdir -p packages/native/src/dsp; sed -n '4990,5025p' docs/modular-graph-phase-d2-plan.md > packages/native/src/dsp/Dpcm.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/src/dsp/Dpcm.hpp`
Expected: `c450cbb365c9c4e8dc8f0a85778cbc193ae3eb70`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/src/dsp/Dpcm.cpp`:

````cpp
#include "dsp/Dpcm.hpp"

#include <algorithm>
#include <cmath>

namespace rp::dsp {
namespace {

// 63 * 10^(-3dB/20), rounded: wav2dmc's NORMALIZE_TARGET_SWING, the 7-bit map's swing when normalising.
constexpr long kNormalizeSwing = 45;

// The DC blocker's pole, one minus one 256th: a high-pass at a fraction of the rate, which is what wav2dmc uses.
constexpr double kDcBlockR = 255.0 / 256.0;

// Floor division toward negative infinity (b > 0). C integer division truncates toward zero, which diverges from
// wav2dmc's Python `//` on a negative numerator: load-bearing for the 7-bit map.
long floorDiv(long a, long b) noexcept {
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// The int16 domain sample: the float scaled by 32767, clamped to the int16 range and rounded half away from zero.
// wav2dmc works on int16 and risa's codec has always converted through this exact expression, so it is the encoder's
// first stage rather than an approximation of it.
std::int32_t toInt16Domain(float sample) noexcept {
    const float v = std::clamp(sample * 32767.0f, -32768.0f, 32767.0f);
    return static_cast<std::int32_t>(std::lround(v));
}

// The DC blocker, one sample per call: `y = x - x1 + R * y1`, truncated toward zero and clamped to int16. Its state is
// the last input and the last unclamped output, which is what makes the second pass over the buffer reproduce the
// first one's samples exactly: both loops inline this one expression, so they agree unless a compiler contracts the
// multiply into a fused operation in one inlined copy and not the other (GCC's default -ffp-contract=fast allows that
// on a target with an FMA). That identity is the assumption the two-pass shape rests on, and exact kit bytes are
// compared on one reference target only.
struct DcBlocker {
    double x1 = 0.0;
    double y1 = 0.0;

    long operator()(std::int32_t s) noexcept {
        const double x = static_cast<double>(s);
        const double y = x - x1 + kDcBlockR * y1;
        x1 = x;
        y1 = y;
        return std::clamp(static_cast<long>(y), -32768L, 32767L);  // the cast truncates toward zero
    }
};

} // namespace

void encodeDpcm(const float* in, std::size_t frames, bool normalize, std::uint8_t* bits) noexcept {
    // The peak over the DC-blocked samples, which the normalising map divides by. A floor of 1 is the codec's: silence
    // still divides by something, and by the same 1 on every host.
    long peak = 1;
    {
        DcBlocker dc;
        for (std::size_t i = 0; i < frames; ++i) {
            const long v = dc(toInt16Domain(in[i]));
            peak = std::max(peak, std::abs(v));
        }
    }

    // The 7-bit map straight into the 1-bit delta: the counter starts at 64, a sample at or above it is a step up and
    // moves it two closer to 127, one below it a step down and two closer to 0. Both stops are the hardware's, and the
    // guards rather than a clamp are the deliberate quirk risa's decoder mirrors.
    DcBlocker dc;
    int       counter = 64;
    for (std::size_t i = 0; i < frames; ++i) {
        const long v = dc(toInt16Domain(in[i]));
        const long q = 64 + (normalize ? floorDiv(v * kNormalizeSwing, peak) : floorDiv(v * 63, 32768));
        const long u7 = std::clamp(q, 0L, 127L);
        if (u7 >= counter) {
            bits[i] = 1;
            if (counter < 127) counter += 2;
        } else {
            bits[i] = 0;
            if (counter > 0) counter -= 2;
        }
    }
}

} // namespace rp::dsp
````

Run: `mkdir -p packages/native/src/dsp; sed -n '5037,5119p' docs/modular-graph-phase-d2-plan.md > packages/native/src/dsp/Dpcm.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/src/dsp/Dpcm.cpp`
Expected: `538209a57e38478e87ffca645a9824d8357f0a3e`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 114d94ca0..1280c34e0 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -36,21 +36,23 @@ endif()
 # The shape shared by every native test binary (see the file for why).
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)
 
-# --- retroplug-dsp-kernels: the DSP the kit compilers run, as block-streaming kernels (src/dsp/,
-# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, and dither / quantise. Core-free
-# on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node package
-# can use it with no core, txiki, backend or r8brain. (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names
-# the plugin's own DSP library that.)
+# --- retroplug-dsp-kernels: the DSP the kit compilers run, as kernels (src/dsp/,
+# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, dither / quantise, and risa's DMC
+# encoder (a whole-buffer kernel, Dpcm.hpp says why; the others stream a block at a time). Core-free on purpose: it
+# links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node package can use it with
+# no core, txiki, backend or r8brain.
+# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.)
 #
 # The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
 # the heap, pthreads (its filter cache is a std::mutex) and RTTI (its resamplers are virtual). A build that has no use
-# for it - the Pico's, which runs rp.dsp's gain / biquad / dither alone - omits the target and never sees any of that,
-# and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for the same
-# reason.
+# for it - the Pico's, which runs rp.dsp's gain / biquad / dither / normalize alone - omits the target and never sees
+# any of that, and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for
+# the same reason.
 add_library(retroplug-dsp-kernels STATIC
     ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp)
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dpcm.cpp)
 target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
 set_target_properties(retroplug-dsp-kernels PROPERTIES
     CXX_STANDARD 20
diff --git a/packages/native/src/risa/RisaDmcCodec.cpp b/packages/native/src/risa/RisaDmcCodec.cpp
index 82c078b40..eb1ed9175 100644
--- a/packages/native/src/risa/RisaDmcCodec.cpp
+++ b/packages/native/src/risa/RisaDmcCodec.cpp
@@ -6,6 +6,7 @@
 #include <type_traits>
 #include <utility>
 
+#include "dsp/Dpcm.hpp"
 #include "dsp/Resample.hpp"
 
 namespace rp::risa {
@@ -27,7 +28,6 @@ constexpr std::uint8_t kFlagLoop       = 0x01;
 constexpr int          kSampleAlign    = 64;
 constexpr int          kLengthStep     = 16;
 constexpr std::size_t  kDmcMaxBytes    = 4081;
-constexpr int          kNormalizeSwing = 45; // 63 * 10^(-3dB/20), rounded (wav2dmc NORMALIZE_TARGET_SWING)
 
 // PAL DPCM playback rates (Hz), index 0..15 (wav2dmc.py PAL_DPCM_RATES_HZ).
 constexpr double kPalRates[16] = {
@@ -35,14 +35,6 @@ constexpr double kPalRates[16] = {
     9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
 };
 
-// Floor-division toward -infinity (b > 0). C integer division truncates toward zero, which diverges from
-// wav2dmc's Python `//` on negative numerators — load-bearing for the 7-bit map.
-long floorDiv(long a, long b) {
-    long q = a / b;
-    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
-    return q;
-}
-
 // Pad a bit-packed DMC stream to a legal 16k+1 length with 0x00 bytes.
 std::vector<std::uint8_t> padToDmcLength(std::vector<std::uint8_t> data) {
     if (data.empty()) return { 0 };
@@ -51,63 +43,6 @@ std::vector<std::uint8_t> padToDmcLength(std::vector<std::uint8_t> data) {
     return data;
 }
 
-// Encode resampled float audio into NES DMC bytes — wav2dmc.py's post-resample pipeline: DC blocker ->
-// 7-bit map centered on 64 -> 1-bit ±2 delta (LSB-first) -> pad. Operates in the int16 domain (wav2dmc
-// works on int16), so f32 is rounded/clamped to int16 first.
-std::vector<std::uint8_t> convertF32ToDpcm(const std::vector<float>& f32, bool normalize) {
-    const std::size_t n = f32.size();
-    if (n == 0) return { 0 };
-
-    std::vector<std::int32_t> s(n);
-    for (std::size_t i = 0; i < n; ++i) {
-        const float v = std::clamp(f32[i] * 32767.0f, -32768.0f, 32767.0f);
-        s[i] = static_cast<std::int32_t>(std::lround(v));
-    }
-
-    // DC blocker: one-pole, R = 255/256, truncate toward zero, clamp int16. State x1 = y1 = 0.
-    const double R = 255.0 / 256.0;
-    double x1 = 0.0, y1 = 0.0;
-    for (std::size_t i = 0; i < n; ++i) {
-        const double x = static_cast<double>(s[i]);
-        const double y = x - x1 + R * y1;
-        x1 = x;
-        y1 = y;
-        long v = static_cast<long>(y); // truncates toward zero
-        v = std::clamp(v, -32768L, 32767L);
-        s[i] = static_cast<std::int32_t>(v);
-    }
-
-    // int16 -> 7-bit unsigned centered on 64.
-    std::vector<std::uint8_t> u7(n);
-    if (normalize) {
-        long peak = 1;
-        for (auto v : s) peak = std::max(peak, static_cast<long>(std::abs(v)));
-        for (std::size_t i = 0; i < n; ++i) {
-            const long q = 64 + floorDiv(static_cast<long>(s[i]) * kNormalizeSwing, peak);
-            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
-        }
-    } else {
-        for (std::size_t i = 0; i < n; ++i) {
-            const long q = 64 + floorDiv(static_cast<long>(s[i]) * 63, 32768);
-            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
-        }
-    }
-
-    // 1-bit ±2 delta encode, LSB-first packing. Counter starts 64; guards <127 / >0 (the deliberate ±2
-    // clamp quirk the decoder mirrors — NOT a clamp(0,127)).
-    std::vector<std::uint8_t> out((n + 7) / 8, 0);
-    int counter = 64;
-    for (std::size_t i = 0; i < n; ++i) {
-        if (static_cast<int>(u7[i]) >= counter) {
-            out[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
-            if (counter < 127) counter += 2;
-        } else {
-            if (counter > 0) counter -= 2;
-        }
-    }
-    return padToDmcLength(std::move(out));
-}
-
 // A 3-char, uppercased, right-space-padded sample name.
 void writeSampleName(std::uint8_t* dst, const std::string& name) {
     for (std::size_t i = 0; i < kSampleNameLen; ++i) {
@@ -149,7 +84,15 @@ std::vector<std::uint8_t> RisaDmcCodec::encode(std::size_t i, const rp::kit::Sam
     std::vector<float> resampled;
     rp::dsp::resample(srcRate, targetRate, window.data(), window.size(), resampled);
 
-    std::vector<std::uint8_t> dmc = convertF32ToDpcm(resampled, spec.normalize);
+    // The encoder's delta bits, packed least-significant bit first into DMC bytes and padded to a legal length. The
+    // arithmetic is rp::dsp::encodeDpcm's; the packing and the pad are this bank's format, not the encoder's.
+    std::vector<std::uint8_t> bits(resampled.size());
+    rp::dsp::encodeDpcm(resampled.data(), resampled.size(), spec.normalize, bits.data());
+    std::vector<std::uint8_t> packed((bits.size() + 7) / 8, 0);
+    for (std::size_t bit = 0; bit < bits.size(); ++bit)
+        if (bits[bit]) packed[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7));
+
+    std::vector<std::uint8_t> dmc = padToDmcLength(std::move(packed));
     if (dmc.size() > kDmcMaxBytes) dmc.resize(kDmcMaxBytes); // 4081 = 16*255+1 (still a legal length)
     return dmc;
 }
````

Run: `sed -n '5129,5281p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the configure adds `Dpcm.cpp` to `retroplug-dsp-kernels`.

Run: `cmake --build build --target retroplug-audio-test -j$(nproc) && build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (428 assertions in 1 test case)`, the 39 banks unchanged: the move moved no byte.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp][dpcm]"`
Expected: the new kernel case passes. Report its summary line: the report quotes it only inside the whole binary's count.

- [ ] **Step 3: The three nodes, and the goldens they need**

`DspNodes.{hpp,cpp}` gains `NormalizeNodeSettings` (`buffer`, `gain`, `channels`), `DpcmNodeSettings` (`buffer`,
`normalize`) and dither's `seedFrom`, and registers the two new types after gain, biquad and dither. Normalize's port
is `channels` wide, it plays frames from frame 0 and keeps its position across a re-activation, and `activate` folds
`rp::dsp::normalizedGain` against the peak over `min(channels, buffer->channels)` channels; a settings commit re-folds
a changed gain against that activation's peak. Dither hashes `seedFrom`'s first channel at activate and in
`on_settings`, because a plan replacement does not re-activate; the non-finite-state guard restarts from that seed
too. DPCM encodes at activate into a member `std::vector<std::uint8_t>`, emits +1 / -1 per bit and silence past the
end, refuses a multi-channel buffer at activate, declares `heap`, and makes `normalize` its `rebuild_key` so a toggle
rebuilds the instance and re-encodes on the control thread instead of re-encoding inside a block-thread commit. Both
new types refuse "no buffer named" as `settings-invalid`; all three document the same-plan recorder hazard the
resampler documents. `cmake/graph.cmake`'s type list names the five types.

````diff
diff --git a/packages/native/cmake/graph.cmake b/packages/native/cmake/graph.cmake
index 169910068..34fc3d847 100644
--- a/packages/native/cmake/graph.cmake
+++ b/packages/native/cmake/graph.cmake
@@ -35,13 +35,13 @@ set_target_properties(retroplug-graph PROPERTIES
     CXX_STANDARD_REQUIRED ON
     POSITION_INDEPENDENT_CODE ON)
 
-# retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad and dither, each a node
-# around a kernel of src/dsp/ (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather than a
-# source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the test
-# hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying. It links
-# retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or RTTI, and it
-# stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and runs these
-# nodes with only the graph library and the kernels.
+# retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
+# each a node around the kernels of src/dsp/ (docs/modular-graph-phase-d1-design.md section 5). A target of its own
+# rather than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the
+# CLI, the test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
+# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
+# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
+# runs these nodes with only the graph library and the kernels.
 add_library(retroplug-dsp-nodes STATIC
     ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
 target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-graph PRIVATE retroplug-dsp-kernels)
diff --git a/packages/native/src/graph/nodes/DspNodes.cpp b/packages/native/src/graph/nodes/DspNodes.cpp
index 6c4c513cf..5f6d715e5 100644
--- a/packages/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/native/src/graph/nodes/DspNodes.cpp
@@ -8,14 +8,17 @@
 #include <memory>
 #include <optional>
 #include <span>
+#include <stdexcept>
 #include <string>
 #include <string_view>
 #include <type_traits>
+#include <vector>
 
 #include <rfl/Literal.hpp>
 
 #include "dsp/Biquad.hpp"
 #include "dsp/Dither.hpp"
+#include "dsp/Dpcm.hpp"
 #include "dsp/Gain.hpp"
 #include "graph/node.hpp"
 
@@ -91,6 +94,35 @@ struct DitherNodeSettings {
     DitherAlgorithm algorithm = DitherAlgorithm::make<"none">();
     int             bits = 4;  // 1 .. 16, the kernel's range
     std::uint32_t   seed = kDefaultDitherSeed;
+    // A buffer whose hash is the seed instead: the kit path's, `rp::dsp::inputSeed` over the whole buffer the dither
+    // quantises (a kit's resampled sample at the bank rate, before it is trimmed to whole frames). Empty, the seed
+    // above is used and nothing about the node changes. A settings ref rather than a port, for the reason
+    // rp.dsp.resample's buffer is one (DspResampleNodes.cpp has the threading argument): the hash is taken at activate,
+    // on the control thread, from a resource create has pinned. What that costs is ordering: an rp.core.buffer-recorder
+    // writing this buffer in the same plan is not ordered before this node, so the hash is of whatever the buffer held
+    // when activate ran. D2's driver records a buffer in one job and reads it in the next.
+    rp::resource_ref<"audio-buffer"> seedFrom;
+};
+
+struct NormalizeNodeSettings {
+    // The buffer is a settings ref, as rp.dsp.resample's is: the peak is measured at activate, on the control thread,
+    // over a resource create has pinned, and the node streams that buffer rather than reading a handle on the block
+    // thread. Empty names none, which a document is refused for: a normalise without a buffer is silence with no
+    // reason to play it.
+    rp::resource_ref<"audio-buffer"> buffer;
+    // The kit spec's gain, a setting and not a param. A normalising gain is `gain / peak` (the kit's `GainEffect`
+    // folds the peak in exactly that way): about 7.4 for a source peaking at -26 dBFS, and negative for a phase flip,
+    // where a param's range would clamp a document's value silently. A setting carries any float the spec has.
+    float                            gain = 1.0f;
+    std::uint32_t                    channels = 2;
+};
+
+struct DpcmNodeSettings {
+    rp::resource_ref<"audio-buffer"> buffer;
+    // The 7-bit map's peak normalisation, risa's per-sample flag: on, the buffer's DC-blocked peak divides a 45 swing
+    // (its loudest sample lands near full scale); off, the map is a plain 63 / 32768 scale. A setting, so no param
+    // range clamps it.
+    bool normalize = true;
 };
 
 }  // namespace dsp
@@ -313,7 +345,9 @@ struct Biquad {
 // their state from it, so an infinity there only quantises that one sample to full scale). A channel that comes out of
 // a block with any of its state not finite therefore has its state restarted from the settings: the latch lasts one
 // block rather than the stream. The block itself is left as the kernel quantised it, which is finite and at worst full
-// scale, so only the state is restarted and the click is one block's.
+// scale, so only the state is restarted and the click is one block's. A `seedFrom` buffer restarts it from the word
+// the stream started from: the buffer's hash, not a live patch's seed, so a recovered channel draws that seed's
+// sequence again from its first draw where the kit kernel would still be latching.
 //
 // That restart is a second place the node differs from the kernel run directly, and a kit source can carry the regime:
 // nothing between `SampleCache` and the kit compile checks a sample for finiteness, so a 32-bit float WAV carrying a
@@ -349,8 +383,12 @@ struct Dither {
 
     rp::dsp::DitherAlgorithm algorithm = rp::dsp::DitherAlgorithm::None;
     int                      bits = 4;
-    std::uint32_t            seed = kDefaultDitherSeed;
-    bool                     restart = true;  // the next block seeds the noise from the three fields above
+    std::uint32_t            seed = kDefaultDitherSeed;        // the setting, when `seedFrom` names nothing
+    std::uint32_t            hashedSeed = kDefaultDitherSeed;  // `seedFrom`'s hash, taken at activate
+    std::uint32_t            activeSeed = kDefaultDitherSeed;  // what the noise is seeded from: the hash, or `seed`
+    // The buffer a `seedFrom` names, pinned by the instance (so it outlives every block) and read only at activate.
+    const rp_audio_buffer* seedFrom = nullptr;
+    bool                   restart = true;  // the next block seeds the noise from the fields above
 
     void attach(std::span<std::byte> mem) noexcept {
         // Never destroyed, like the biquad's states above; the static_assert holds that Dither is trivially
@@ -365,13 +403,26 @@ struct Dither {
         algorithm = ditherAlgorithmOf(s.algorithm);
         bits = s.bits;
         seed = s.seed;
+        seedFrom = s.seedFrom.audio();
+        // A commit restarts the noise, from whichever word the stream was seeded by: a `seedFrom` buffer's hash is
+        // taken once, at activate, so a commit that changes `seed` while one is named changes nothing about the noise
+        // (the hash replaces the setting), and a commit without one takes the new value.
+        activeSeed = seedFrom ? hashedSeed : seed;
         restart = true;
     }
 
-    void activate(double, std::uint32_t) noexcept { restart = true; }
+    // The seed the noise starts from is settled here, on the control thread: a `seedFrom` buffer is hashed whole
+    // (`rp::dsp::inputSeed`), and without one the `seed` setting stands. The hash is the kit path's: over the first
+    // channel's samples, which are all a kit's mono buffer has, and over every one of them (the trimmed input would be
+    // a different word). A re-activation hashes the buffer again, which is the same word.
+    void activate(double, std::uint32_t) noexcept {
+        if (seedFrom) hashedSeed = rp::dsp::inputSeed(seedFrom->data[0], seedFrom->frames);
+        activeSeed = seedFrom ? hashedSeed : seed;
+        restart = true;
+    }
 
     void seedState(rp::dsp::Dither& state, std::uint32_t channel) const noexcept {
-        state.init(algorithm, bits, seed + channel * kChannelSeedStep);
+        state.init(algorithm, bits, activeSeed + channel * kChannelSeedStep);
     }
 
     void process(const rp::Block& b) noexcept {
@@ -391,6 +442,170 @@ struct Dither {
     }
 };
 
+// ---- rp.dsp.normalize ---------------------------------------------------------------------------------------------
+
+// A buffer player that normalises, for a kit chain's normalising gain (`GainEffect{normalize: true}`): the whole
+// buffer's peak is measured at activate, `normalizedGain(gain, peak)` folds it into the gain exactly as
+// lsdj/Effects.cpp does, and the buffer is streamed times that gain, from its first frame, silence past its end. It
+// plays frames rather than seconds: the plan's rate is the buffer's in a kit job, and a buffer player's position is a
+// buffer frame.
+//
+// The peak is measured across the channels this node plays: each played channel's `rp::dsp::peak`, then the greatest
+// of those, which is the same fold the mono path makes, continued across the channels. A channel past the buffer's
+// last is silence and contributes nothing, so `channels: 1` over a stereo buffer is normalised by the channel it
+// plays and the unplayed one never scales it. One gain scales every channel, as the kit path's single multiply does
+// to its mono buffer, and no channel is normalised on its own. Kit sources are mono, so a kit compile never asks the
+// question.
+//
+// The peak is read where activation finds the buffer. An rp.core.buffer-recorder writing that buffer in the same plan
+// is not ordered before this node (a settings ref is not a binding the planner sees), so the peak is taken over
+// whatever the buffer held then, silence for a fresh one, and a normalise of zeros is the bare gain, with no
+// diagnostic. D2's driver records a buffer in one job and reads it in the next.
+//
+// Nothing is allocated, here or in `process`: the buffer is read where the resource table holds it, and the only state
+// is the scale and the position. So the node declares no `heap` and the planner plans it on the Pico, unlike
+// rp.dsp.resample, whose buffer needs r8brain built at activate.
+struct Normalize {
+    static constexpr const char* id = "rp.dsp.normalize";
+    static constexpr const char* name = "Normalize";
+    static constexpr const char* category = "dsp/source";
+    using Settings = NormalizeNodeSettings;
+
+    struct { rp::audio_out<rp::channels<2, "any">> out; } outputs;
+
+    static std::optional<std::string> validate(const Settings& s) {
+        if (auto e = checkChannels(s.channels, "a normalise")) return e;
+        if (s.buffer.ref.empty()) return "a normalise names an audio buffer";
+        return std::nullopt;
+    }
+    static void adjust_layout(Layout& l, const Settings& s) {
+        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
+    }
+
+    const rp_audio_buffer* buffer = nullptr;  // pinned by the instance, so it outlives every block
+    float                  gain = 1.0f;       // the setting, as the last commit left it
+    std::uint32_t          channels = 2;      // the output's, which is how many of the buffer's the peak folds
+    float                  peak = 0.0f;       // the played channels', measured at activate
+    float                  scale = 1.0f;      // normalizedGain(gain, peak)
+    std::uint32_t          position = 0;      // the next buffer frame to play, 0 .. frames
+
+    // A commit never moves the buffer (a ref that moved is a rebuild) and never changes the channel count (a new
+    // layout rebuilds), and the peak is the last activate's, so a gain that changed takes effect from the next block
+    // with no block-thread work beyond the multiply.
+    void on_settings(const Settings& s) noexcept {
+        buffer = s.buffer.audio();
+        gain = s.gain;
+        channels = s.channels;
+        scale = rp::dsp::normalizedGain(gain, peak);
+    }
+
+    // The peak is measured here, on the control thread. The position is not reset: a re-activation (a new block size,
+    // a new plan) carries the stream on, as the player does, where a fresh instance starts at the buffer's first frame.
+    void activate(double, std::uint32_t) noexcept {
+        peak = 0.0f;
+        if (buffer)
+            for (std::uint32_t c = 0; c < channels && c < buffer->channels; ++c)
+                peak = std::max(peak, rp::dsp::peak(buffer->data[c], buffer->frames));
+        scale = rp::dsp::normalizedGain(gain, peak);
+    }
+
+    void process(const rp::Block& b) noexcept {
+        const std::uint32_t n = b.frames();
+        const std::uint32_t frames = buffer ? buffer->frames : 0;
+        const std::uint32_t at = position < frames ? position : frames;
+        const std::uint32_t available = frames - at;
+        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
+            float* const y = outputs.out[c];
+            if (!buffer || c >= buffer->channels) {
+                std::fill(y, y + n, 0.0f);  // a layout channel the buffer lacks, as the player's
+                continue;
+            }
+            const float* const x = buffer->data[c];
+            for (std::uint32_t i = 0; i < n; ++i) y[i] = i < available ? x[at + i] * scale : 0.0f;
+        }
+        position = n < available ? at + n : frames;
+    }
+};
+
+// ---- rp.dsp.dpcm --------------------------------------------------------------------------------------------------
+
+// risa's DMC encoder as a node: at activate, on the control thread, the kernel (src/dsp/Dpcm.hpp) runs over the whole
+// bound buffer into storage this instance owns, and every block after that streams one delta bit per frame as an audio
+// level: +1 for a one, -1 for a zero, silence past the buffer's end. The bits are the risa bank's payload rather than
+// audio, so a document records them through rp.core.buffer-recorder and the packer that turns each level back into
+// its bit (`level > 0`) runs in TypeScript.
+//
+// Mono, with no `channels` setting: the encoder is the NES's single DMC channel, and a multi-channel buffer has no
+// meaning it could define without mixing or dropping a channel nobody asked it to. A buffer of another channel count
+// therefore fails the activation with a reason, rather than being silently taken apart; a kit chain is mono.
+//
+// The storage is a member, built at activate and freed with the instance on the control thread, as rp.dsp.resample's
+// resamplers are, which is why the type declares `heap` and the planner refuses it on the Pico.
+//
+// `normalize` is the type's rebuild key, and it is one because re-encoding is a whole-buffer pass: a settings commit
+// runs on the block thread, where a node may do no more than copy, and a re-encode there would cost the buffer's
+// length in the middle of a block. A commit that changed it therefore builds a fresh instance, which encodes in
+// activate on the control thread; the stream restarts at the first bit, which is what a newly encoded stream means
+// anyway. Every other setting is a copy in `on_settings`.
+//
+// The bits are read where activation finds the buffer. An rp.core.buffer-recorder writing it in the same plan is not
+// ordered before this node (a settings ref is not a binding the planner sees), so a DMC encode of a fresh buffer is
+// the encode of silence, with no diagnostic. D2's driver records a buffer in one job and reads it in the next.
+struct Dpcm {
+    static constexpr const char* id = "rp.dsp.dpcm";
+    static constexpr const char* name = "DPCM";
+    static constexpr const char* category = "dsp/codec";
+    static constexpr bool        heap = true;  // the bit storage is built at activate
+    using Settings = DpcmNodeSettings;
+
+    struct { rp::audio_out<rp::mono> out; } outputs;
+
+    static std::optional<std::string> validate(const Settings& s) {
+        if (s.buffer.ref.empty()) return "a DPCM node names an audio buffer";
+        return std::nullopt;
+    }
+
+    // A change of `normalize` re-encodes, so it is a rebuild rather than a commit (the comment above says why).
+    static bool rebuild_key(const Settings& s) noexcept { return s.normalize; }
+
+    const rp_audio_buffer*    buffer = nullptr;  // pinned by the instance, so it outlives every block
+    bool                      normalize = true;  // the setting, as activate read it
+    std::vector<std::uint8_t> bits;              // one per buffer frame, built at activate
+    std::uint32_t             position = 0;      // the next bit to play, 0 .. bits.size()
+
+    void on_settings(const Settings& s) noexcept {
+        buffer = s.buffer.audio();
+        normalize = s.normalize;
+    }
+
+    // The encode is here, on the control thread: a fresh instance, or the one a re-activation keeps, builds its bits
+    // before the first block.
+    void activate(double, std::uint32_t) {
+        if (!buffer) {
+            bits.clear();
+            position = 0;
+            return;
+        }
+        if (buffer->channels != 1)
+            throw std::runtime_error("a DPCM node takes a mono buffer, and this one has " +
+                                     std::to_string(buffer->channels) + " channels");
+        // Built aside and moved in, so a failed allocation leaves the previous stream's bits as they were.
+        std::vector<std::uint8_t> encoded(buffer->frames);
+        rp::dsp::encodeDpcm(buffer->data[0], buffer->frames, normalize, encoded.data());
+        bits = std::move(encoded);
+    }
+
+    void process(const rp::Block& b) noexcept {
+        const std::uint32_t n = b.frames();
+        const std::uint32_t total = static_cast<std::uint32_t>(bits.size());
+        const std::uint32_t at = position < total ? position : total;
+        const std::uint32_t available = total - at;
+        float* const        y = outputs.out[0];
+        for (std::uint32_t i = 0; i < n; ++i) y[i] = i < available ? (bits[at + i] ? 1.0f : -1.0f) : 0.0f;
+        position = n < available ? at + n : total;
+    }
+};
+
 }  // namespace
 
 const rp_package* dspPackage() {
@@ -398,6 +613,8 @@ const rp_package* dspPackage() {
         rp::node_type<Gain>(),
         rp::node_type<Biquad>(),
         rp::node_type<Dither>(),
+        rp::node_type<Normalize>(),
+        rp::node_type<Dpcm>(),
     };
     static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                      static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
diff --git a/packages/native/src/graph/nodes/DspNodes.hpp b/packages/native/src/graph/nodes/DspNodes.hpp
index c881d3370..64c92b4cc 100644
--- a/packages/native/src/graph/nodes/DspNodes.hpp
+++ b/packages/native/src/graph/nodes/DspNodes.hpp
@@ -5,25 +5,36 @@
 namespace rp::graph {
 
 // The rp.dsp package: the DSP library as graph nodes (docs/modular-graph-phase-d1-design.md section 5), each one a
-// thin node around a block-streaming kernel of src/dsp/, so a patch and the kit compilers run the same arithmetic.
+// thin node around the kernels of src/dsp/, so a patch and the kit compilers run the same arithmetic. rp.dsp.normalize
+// is the one that is not: a buffer player, whose normalising multiply is the gain kernel's, applied to a scale the
+// buffer's own peak sets.
 //
-//   rp.dsp.gain     a linear gain                                              {"channels":2}
-//   rp.dsp.biquad   the RBJ cookbook's eight filters, one per channel          {"channels":2,"type":"low-pass"}
-//   rp.dsp.dither   quantise to 1..16 bits, five dither algorithms or none     {"channels":2,"bits":4,"seed":...}
+//   rp.dsp.gain       a linear gain                                              {"channels":2}
+//   rp.dsp.biquad     the RBJ cookbook's eight filters, one per channel          {"channels":2,"type":"low-pass"}
+//   rp.dsp.dither     quantise to 1..16 bits, five dither algorithms or none     {"channels":2,"bits":4,"seed":...}
+//   rp.dsp.normalize  a buffer player, normalised by its own peak                {"buffer":"res:N","gain":1,"channels":1}
+//   rp.dsp.dpcm       risa's DMC encoder, its delta bits as +1/-1 audio          {"buffer":"res:N","normalize":true}
 //
-// Every one carries a `channels` setting and follows it through adjust_layout, as rp.core.delay does: the two
-// per-channel types keep their kernel state in host-supplied memory, and gain, whose kernel is one float and has no
-// per-channel state, holds it as an ordinary member. None declares `heap` or `threads`, so all three plan on the Pico.
-// A kernel's state is per channel and its size cannot be known from the descriptor alone, so extra_memory is sized
-// from the settings' channel count, never from the descriptor's maximum.
+// The three processor types carry a `channels` setting and follow it through adjust_layout, as rp.core.delay does: the
+// two per-channel types keep their kernel state in host-supplied memory, and gain, whose kernel is one float and has no
+// per-channel state, holds it as an ordinary member. A kernel's state is per channel and its size cannot be known from
+// the descriptor alone, so extra_memory is sized from the settings' channel count, never from the descriptor's maximum.
 //
-// The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain (heap, pthreads and RTTI) with
-// it, and a node that declares `heap` is refused on the Pico. So it is a source file and a target of its own
-// (DspResampleNodes.hpp, retroplug-dsp-resample-nodes), and a build without it omits that node and r8brain together: it
-// registers from a package of its own, with its own manifest id (`rp.dsp.resample`, which `rp.host.io` is to
-// `rp.host`), in the engine's package list and the graph rig's beside dspPackage(). A package id registers once -
-// NodeRegistry refuses a second package under one - so the resample type can neither be a second rp.dsp nor a type
-// this function's list names.
+// The last two name an "audio-buffer" in their settings, as rp.dsp.resample does, because what they need of it must be
+// known before a block runs: the normalize node's peak, measured at activate, and the DPCM node's bits, encoded whole
+// at activate. Nothing is pinned on the block thread and nothing allocates there either, but the DPCM node's storage is
+// built at activate, so it alone declares `heap` and is refused on the Pico. Dither's `seedFrom` is a buffer the same
+// way: its hash is taken at activate.
+//
+// The resampler is the one rp.dsp node that cannot be here: its kernel brings r8brain with it - the heap, pthreads
+// (r8brain's filter cache is a std::mutex) and RTTI (its resamplers are virtual) - so a build that has no use for it
+// must be able to leave it out at link time rather than refuse it at the planner, which is all a `heap` type like
+// rp.dsp.dpcm costs the Pico. So it is a source file and a target of its own (DspResampleNodes.hpp,
+// retroplug-dsp-resample-nodes), and a build without it omits that node and r8brain together: it registers from a
+// package of its own, with its own manifest id (`rp.dsp.resample`, which `rp.host.io` is to `rp.host`), in the
+// engine's package list and the graph rig's beside dspPackage(). A package id registers once - NodeRegistry refuses a
+// second package under one - so the resample type can neither be a second rp.dsp nor a type this function's list
+// names.
 const rp_package* dspPackage();
 
 }  // namespace rp::graph
````

Run: `sed -n '5311,5685p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

The dither's golden re-records for the one added `"seedFrom":{"type":"string"}`; the other two are new. All three are
one line each (dither 731 bytes, DPCM 488, normalize 533), so all three are quoted whole.

Replace `packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`:

````json
{"category":"dsp/quantise","id":"rp.dsp.dither","name":"Dither","params":[],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"in","id":"in","kind":"audio"},{"channels":{"match":"in"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__DitherNodeSettings":{"properties":{"algorithm":{"enum":["none","high-pass-tpdf","shaped-tpdf","error-diffusion","jjn","sierra-lite"],"type":"string"},"bits":{"type":"integer"},"channels":{"type":"integer"},"seed":{"type":"integer"},"seedFrom":{"type":"string"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__DitherNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `mkdir -p packages/native/test/graph/golden; sed -n '5697,5697p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/golden/rp.dsp.dither.descriptor.json`
Expected: `cbd8cd77bab0bd7c63c4e33394b0fa689ca5a35b`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json`:

````json
{"category":"dsp/codec","id":"rp.dsp.dpcm","name":"DPCM","params":[],"ports":[{"channels":{"count":1,"layout":"mono"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":true,"threads":false},"settings":{"$defs":{"rp__graph__dsp__DpcmNodeSettings":{"properties":{"buffer":{"type":"string"},"normalize":{"type":"boolean"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__DpcmNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `mkdir -p packages/native/test/graph/golden; sed -n '5709,5709p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json`
Expected: `0e461da1a6e288fef97875386b72dbfc23b3c546`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json`:

````json
{"category":"dsp/source","id":"rp.dsp.normalize","name":"Normalize","params":[],"ports":[{"channels":{"count":2,"layout":"any"},"dir":"out","id":"out","kind":"audio"}],"requires":{"heap":false,"threads":false},"settings":{"$defs":{"rp__graph__dsp__NormalizeNodeSettings":{"properties":{"buffer":{"type":"string"},"channels":{"type":"integer"},"gain":{"type":"number"}},"required":[],"type":"object"}},"$ref":"#/$defs/rp__graph__dsp__NormalizeNodeSettings","$schema":"https://json-schema.org/draft/2020-12/schema"},"version":"1.0.0"}
````

Run: `mkdir -p packages/native/test/graph/golden; sed -n '5721,5721p' docs/modular-graph-phase-d2-plan.md > packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json`
Expected: `c5e12205541c0bb918853870e4836b80e1f26102`: the blob f45bc6898 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[dsp-buffer-nodes]"`
Expected: every new case passes; the descriptors already match the quoted goldens. Report the summary line.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test`
Expected: `All tests passed (356087 assertions in 272 test cases)`.

Run: `RP_GRAPH_GOLDEN_UPDATE=1 build/bin/retroplug-graph-test "[dsp-nodes]" && git add -- packages/native/test/graph/golden && git diff --cached --stat f45bc6898 -- packages/native/test/graph/golden`
Expected: nothing printed after the run: the updater rewrites the three files, and the staged diff against the prototype commit is empty, so the quoted blocks are the bytes this tree records.

- [ ] **Step 4: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, with the plugin and `retroplug-sdl` included.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-graph-test` `All tests passed (356087 assertions in 272 test cases)`; `retroplug-audio-test` `1235263 assertions in 109 test cases`; `retroplug-engine-graph-test` `280 | 271 passed | 9 skipped`; launchpad 144 / 29.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 161 native test file(s) passed (jobs=16)`, only dsp-bench and nes-bench skipping (risa-kit, lsdj-rom and kit-dither included).

Run: `pnpm test`
Expected: `# 170 test file(s) passed (jobs=16)`.

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 5: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp][dpcm],[dsp-buffer-nodes]"`
Expected: `All tests passed (53675 assertions in 13 test cases)`, 0 ThreadSanitizer reports.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29138 assertions in 12 test cases)`, 0 reports: the kit-shaped job runs the three new nodes on a worker.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp][dpcm],[dsp-buffer-nodes]"`
Expected: `All tests passed (53675 assertions in 13 test cases)`, 0 Address/LeakSanitizer reports.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29138 assertions in 12 test cases)`, 0 reports.

- [ ] **Step 6: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/native/CMakeLists.txt
 M packages/native/cmake/graph.cmake
 M packages/native/cmake/tests.cmake
?? packages/native/src/dsp/Dpcm.cpp
?? packages/native/src/dsp/Dpcm.hpp
 M packages/native/src/graph/nodes/DspNodes.cpp
 M packages/native/src/graph/nodes/DspNodes.hpp
 M packages/native/src/risa/RisaDmcCodec.cpp
 M packages/native/test/audio/KitBaseline.test.cpp
 M packages/native/test/engine-graph/EngineGraph.test.cpp
 M packages/native/test/engine-graph/OfflineJobs.test.cpp
 M packages/native/test/graph/Dsp.test.cpp
?? packages/native/test/graph/DspBufferNodes.test.cpp
 M packages/native/test/graph/DspNodes.test.cpp
M  packages/native/test/graph/golden/rp.dsp.dither.descriptor.json
A  packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json
A  packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json
````

````bash
git add -- packages/native/src/dsp/Dpcm.cpp packages/native/src/dsp/Dpcm.hpp \
  packages/native/test/graph/DspBufferNodes.test.cpp packages/native/test/graph/golden/rp.dsp.dpcm.descriptor.json \
  packages/native/test/graph/golden/rp.dsp.normalize.descriptor.json packages/native/CMakeLists.txt \
  packages/native/cmake/graph.cmake packages/native/cmake/tests.cmake packages/native/src/graph/nodes/DspNodes.cpp \
  packages/native/src/graph/nodes/DspNodes.hpp packages/native/src/risa/RisaDmcCodec.cpp \
  packages/native/test/audio/KitBaseline.test.cpp packages/native/test/engine-graph/EngineGraph.test.cpp \
  packages/native/test/engine-graph/OfflineJobs.test.cpp packages/native/test/graph/Dsp.test.cpp \
  packages/native/test/graph/DspNodes.test.cpp packages/native/test/graph/golden/rp.dsp.dither.descriptor.json
git commit -F - <<'EOF'
feat(dsp): normalise, input-seeded dither and DPCM as nodes reading a bound buffer

The three whole-buffer steps a kit chain needs as graph nodes, each held to the
arithmetic the kit path runs today.

rp.dsp.normalize is a buffer player that normalises: it names an audio-buffer in
its settings, measures the peak of the channels it plays at activate on the
control thread, folds it into the kit spec's gain with rp::dsp::normalizedGain (a
setting, not a param, so no range clamps it) and streams the buffer times that
gain, from frame 0, silence past the end. One gain scales every channel it plays,
and a buffer channel it does not play is silence that never scales one it does.
It allocates nothing, so it plans on the Pico. A committed gain re-folds against
that activation's peak, so a gain change is a copy and one multiply, never a fresh
measurement on the block thread.

rp.dsp.dither gains `seedFrom`: an optional audio-buffer ref whose whole first
channel activate hashes with rp::dsp::inputSeed, in place of the seed setting, so
channel 0 gets exactly the seed the kit path's quantise computes and the other
channels derive theirs from it as they do from a seed. Unset, nothing changes.

convertF32ToDpcm's int16 conversion, DC blocker, 7-bit map and 1-bit delta move
into retroplug-dsp-kernels as rp::dsp::encodeDpcm, a whole-buffer kernel writing
one delta bit per sample into caller-owned storage. It recomputes its samples in
a second pass rather than keeping an int per frame, so it needs no scratch and
allocates nothing. RisaDmcCodec::encode still packs the bits LSB first, pads to a
legal length and truncates to 4081 bytes, so its bytes do not move: the kit
baseline is unchanged, hash for hash.

rp.dsp.dpcm runs that kernel over its bound buffer at activate into storage the
instance owns and streams each bit as +1 / -1 audio, silence past the end; it is
mono and refuses a multi-channel buffer at activate with a reason. It declares
heap, so the Pico refuses it as it refuses rp.dsp.resample. `normalize` is its
rebuild key: re-encoding is a whole-buffer pass and a settings commit runs on the
block thread, where a node may only copy, so a commit that toggles it builds a
fresh instance, whose activate does the encode on the control thread, rather
than re-encoding between two blocks. on_settings only copies the buffer and the
flags; the encode never leaves activate.

Both new types join the rp.dsp package, which the engine, the graph rig and the
offline job pool already register, and carry descriptor goldens; the dither's
re-records for the new field alone. Each node's settings commit has its own case,
and a kit-shaped offline job runs all three on a worker, naming one buffer twice.
EOF
````

---

### Task 4: the kit compilers in TypeScript, as chains of offline jobs

Design sections 5 and 6, and section 10's third and fourth questions. `compileKit` and `compileDmc` become TypeScript:
each slot's chain is one offline job per pass, every slot's chain starts at once so the pool runs them in parallel, and
TypeScript cuts, packs and assembles the banks. Native decodes, windows and does every float the bytes depend on. The
result is `{bytes, errors}`. The 39-case baseline is the oracle, and this task proves it through the graph compile
before any caller moves. Two mechanisms, both native, make byte identity exact for intermediates a kit never keeps: a
guard a node's own reset reports through `rp.host.guards/1`, and a `nonFinite` flag on every returned buffer. A new
golden, `kit-edges.json`, records the banks past the baseline's matrix, and CI's linux-x64 job now requires exact kit
bytes on `test:native`.

**Files:**

- Create: `packages/native/test/audio/golden/kit-edges.json`, `packages/retroplug/src/kit/chain.ts`, `packages/retroplug/src/kit/compile.ts`, `packages/retroplug/src/kit/index.ts`, `packages/retroplug/src/kit/pack.ts`, `packages/retroplug/test-native/kit-baseline.test.ts`, `packages/retroplug/test-native/kit-compile.test.ts`, `packages/retroplug/test-native/kit/sources.ts`, `packages/retroplug/test/kit/kit.test.ts`
- Modify: `.github/workflows/build.yml`, `packages/native/src/graph/nodes/DspNodes.cpp`, `packages/native/src/graph/rp_node.h`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/engine/OfflineJobs.hpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/test/engine-graph/OfflineJobs.test.cpp`, `packages/retroplug/scripts/run-native-tests.mjs`, `packages/retroplug/src/lsdj/rom/buildKit.ts`, `packages/retroplug/src/lsdj/rom/index.ts`, `packages/retroplug/src/offline.ts`, `packages/retroplug/src/risa/rom/kit.ts`, `packages/retroplug/test/offline/offline.test.ts`

- [ ] **Step 1: Write the failing native cases**

`OfflineJobs.test.cpp` gains the cases the new reports need: a +96 dB peak over samples near `FLT_MAX`, a low-pass
above Nyquist and JJN over samples near `FLT_MAX` each complete `ok` with their buffer, `guardsFired > 0` and the
exact reason (and with the trigger removed, `guardsFired == 0` and an empty reason); a plain gain over huge samples
completes with `nonFinite` true; the engine's `graphService` answers null for `rp.host.guards/1` and non-null for
`rp.host.systems/1`, so the lookup reaches the engine's real answer; and a job submitted from a thread that set FTZ
and DAZ (or FPCR.FZ) gives back a buffer of subnormals through a unity gain bit for bit.

````diff
diff --git a/packages/native/test/engine-graph/OfflineJobs.test.cpp b/packages/native/test/engine-graph/OfflineJobs.test.cpp
index fc3be6ca0..e952618d2 100644
--- a/packages/native/test/engine-graph/OfflineJobs.test.cpp
+++ b/packages/native/test/engine-graph/OfflineJobs.test.cpp
@@ -12,6 +12,7 @@
 // Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[offline]"`; the hidden
 // "[offline-bench]" times a trivial job and a one-second resample.
 
+#include <algorithm>
 #include <atomic>
 #include <chrono>
 #include <cmath>
@@ -28,6 +29,10 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
+#include <xmmintrin.h>
+#endif
+
 #include "../graph/CountingAllocator.hpp"
 #include "../graph/GraphRig.hpp"
 #include "GraphDocs.hpp"
@@ -39,6 +44,7 @@
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
 #include "host/engine/OfflineJobs.hpp"
+#include "host/graph/HostServices.hpp"
 #include "host/rpc/EngineRpcService.hpp"
 #include "kit/SampleCache.hpp"
 #include "lsdj/KitUtil.hpp"
@@ -177,6 +183,22 @@ OfflineCompletion one(OfflineJobs& jobs, std::uint32_t id) {
     return std::move(done[id]);
 }
 
+// Sets flush-to-zero and denormals-are-zero on the calling thread, where the target has them; false where it does not.
+bool setFlushToZero() {
+#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
+    _mm_setcsr(_mm_getcsr() | 0x8040u);  // MXCSR FTZ (bit 15) and DAZ (bit 6)
+    return true;
+#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
+    std::uint64_t fpcr = 0;
+    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
+    fpcr |= std::uint64_t(1) << 24;  // FPCR.FZ
+    asm volatile("msr fpcr, %0" : : "r"(fpcr));
+    return true;
+#else
+    return false;
+#endif
+}
+
 // The thread count of this process, from /proc on Linux; -1 elsewhere.
 int threadCount() {
 #if defined(__linux__)
@@ -420,6 +442,129 @@ TEST_CASE("a kit-shaped job names one buffer twice and runs the buffer-reading n
     CHECK(jobs.outstanding() == 0);
 }
 
+TEST_CASE("a guard that fires in an offline job is counted in its completion; a non-finite recording is flagged",
+          "[engine-graph][offline]") {
+    // rp.dsp.biquad resets a filter whose state went non-finite and rp.dsp.dither restarts a channel whose state did,
+    // where the kernels latch. A job's host takes their reports (rp.host.guards/1), and the job still completes, ok,
+    // carrying how many fired and the first one's reason: native reports, the caller decides (a kit compile refuses
+    // the pass, a render can accept a filter that recovered). Each route here starts from finite samples, which a kit
+    // compile's own refusals let through: a +96 dB peak over samples near the top of the float range overflows the
+    // filter's state, a filter above the plan's Nyquist runs away on an ordinary signal, and an error diffusion (JJN's)
+    // over samples near the top of the range overflows the scaled value its fed-back errors are taken from. The same
+    // jobs with the guard's trigger taken away count none, so the count is the guard's. A live host offers no report
+    // (checked against the engine below) and its nodes recover as before. A value no guard touches is the recording's,
+    // and a returned buffer says whether it holds one.
+    OfflineJobs jobs;
+    const std::uint32_t frames = 4096;
+    const auto run = [&](const char* node, const std::vector<float>& input) {
+        OfflineRequest r;
+        r.document = std::string(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,
+            "layout":"mono","loop":false},"resources":{"sample":"res:1"}},)") + node +
+                     R"(,{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
+            "resources":{"target":"res:2"}}],"wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"rec.in"}]})";
+        r.inputs.emplace_back(OfflineSamples{input, 1, 48000.0});
+        r.outputs.push_back({1, frames, 48000.0});
+        r.returns = {2};
+        r.frames = frames;
+        r.rate = 48000.0;
+        const auto s = jobs.submit(r);
+        INFO(s.error);
+        REQUIRE(s.id != 0);
+        return one(jobs, s.id);
+    };
+    std::vector<float> huge = signal(frames, 1, 3);
+    for (float& v : huge) v *= 3.0e38f;  // every sample finite, the loudest near FLT_MAX
+    const std::vector<float> ordinary = signal(frames, 1, 4);
+
+    const char* peak = R"({"id":"x","type":"rp.dsp.biquad","settings":{"channels":1,"type":"peak"},
+                           "params":{"frequency":1000,"q":1,"gainDb":96}})";
+    const char* runaway = R"({"id":"x","type":"rp.dsp.biquad","settings":{"channels":1,"type":"low-pass"},
+                              "params":{"frequency":30000,"q":1}})";
+    const char* diffusion = R"({"id":"x","type":"rp.dsp.dither","settings":{"channels":1,
+                                "algorithm":"jjn","bits":4}})";
+    const std::string biquadReason = "rp.dsp.biquad reset a filter whose state went non-finite";
+    const std::string ditherReason = "rp.dsp.dither restarted a channel whose state went non-finite";
+
+    const auto guarded = [](const OfflineCompletion& c, const std::string& reason) {
+        INFO(c.error);
+        REQUIRE(c.ok);
+        CHECK(c.guardsFired > 0);
+        CHECK(c.guardReason == reason);
+        REQUIRE(c.buffers.size() == 1);  // the recording comes back too: what to make of it is the caller's
+    };
+    guarded(run(peak, huge), biquadReason);
+    guarded(run(runaway, ordinary), biquadReason);
+    guarded(run(diffusion, huge), ditherReason);
+
+    // The triggers taken away: the +96 dB peak and the error diffusion over ordinary samples.
+    const auto peakOrdinary = run(peak, ordinary);
+    INFO(peakOrdinary.error);
+    CHECK(peakOrdinary.ok);
+    CHECK(peakOrdinary.guardsFired == 0);
+    CHECK(peakOrdinary.guardReason.empty());
+    REQUIRE(peakOrdinary.buffers.size() == 1);
+    CHECK_FALSE(peakOrdinary.buffers[0].nonFinite);
+
+    // A value no guard touches reaches the recording, and the returned buffer says so: a plain gain over the huge
+    // samples overflows to infinities, which the job completes with (nothing in it misbehaved) and reports.
+    const char* loudGain = R"({"id":"x","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":50}})";
+    const auto  overflowed = run(loudGain, huge);
+    INFO(overflowed.error);
+    REQUIRE(overflowed.ok);
+    REQUIRE(overflowed.buffers.size() == 1);
+    CHECK(overflowed.buffers[0].nonFinite);
+    CHECK(std::any_of(overflowed.buffers[0].samples.begin(), overflowed.buffers[0].samples.end(),
+                      [](float v) { return std::isinf(v); }));
+    const auto ditherOrdinary = run(diffusion, ordinary);
+    INFO(ditherOrdinary.error);
+    CHECK(ditherOrdinary.ok);
+    CHECK(ditherOrdinary.guardsFired == 0);
+    CHECK(jobs.outstanding() == 0);
+
+    // The live engine offers its nodes no guard report, so a live guard recovers with nobody told; the systems service
+    // answers, so the null is the engine's answer for this id and not a lookup that reaches nothing.
+    Engine engine(48000.0);
+    CHECK(engine.graphService(RP_HOST_GUARDS_INTERFACE) == nullptr);
+    CHECK(engine.graphService(RP_HOST_SYSTEMS_INTERFACE) != nullptr);
+}
+
+TEST_CASE("an offline job computes in the default floating-point mode whatever its submitter's mode",
+          "[engine-graph][offline]") {
+    // A worker starts with the floating-point environment of the thread whose submission started it, and an audio host
+    // commonly sets flush-to-zero on its threads. A job pins the default mode for its run, so a buffer of subnormals
+    // through a unity gain comes back as it went in, where under denormals-are-zero it would come back zeros.
+    std::vector<float> subnormal(1024);
+    for (std::size_t i = 0; i < subnormal.size(); ++i) subnormal[i] = 1.0e-39f * static_cast<float>(i % 7 + 1);
+    REQUIRE(std::fpclassify(subnormal[0]) == FP_SUBNORMAL);
+    OfflineRequest r;
+    r.document = R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono",
+        "loop":false},"resources":{"sample":"res:1"}},{"id":"g","type":"rp.dsp.gain","settings":{"channels":1},
+        "params":{"gain":1}},{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
+        "resources":{"target":"res:2"}}],"wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
+    r.inputs.emplace_back(OfflineSamples{subnormal, 1, 48000.0});
+    r.outputs.push_back({1, 1024, 48000.0});
+    r.returns = {2};
+    r.frames = 1024;
+    r.rate = 48000.0;
+
+    // The pool and its first worker are made from a thread that flushes subnormals, so the worker inherits that mode.
+    OfflineJobs              jobs;
+    OfflineCompletion        c;
+    bool                     flushing = false;
+    std::thread submitter([&] {
+        flushing = setFlushToZero();
+        const auto s = jobs.submit(r);
+        REQUIRE(s.id != 0);
+        c = one(jobs, s.id);
+    });
+    submitter.join();
+    if (!flushing) SKIP("this target has no flush-to-zero mode this test knows how to set");
+    INFO(c.error);
+    REQUIRE(c.ok);
+    REQUIRE(c.buffers.size() == 1);
+    CHECK(c.buffers[0].samples == subnormal);
+}
+
 TEST_CASE("offline jobs past the pool's concurrency queue, and each returns its own result", "[engine-graph][offline]") {
     OfflineJobs jobs;
     const std::size_t count = std::min<std::size_t>(3 * OfflineJobs::concurrency() + 1, OfflineJobs::kMaxJobs);
````

Run: `sed -n '5885,6068p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: the build stops compiling `OfflineJobs.test.cpp`: `guardsFired`, `guardReason`, `nonFinite` and `graphService` do not exist yet. That is this half's red.

- [ ] **Step 2: The guard service, the recording flag and the floating-point environment**

`rp_node.h` declares `rp.host.guards/1`: `rp_host_guards { ctx; fired(ctx, reason) }`, called on the block thread,
never blocking or allocating, the reason a string literal its instance keeps alive, and a host that offers none
answers null. `rp.dsp.biquad` and `rp.dsp.dither` take it in `bind_host` and call it exactly where each guard replaces
the kernel's result. `OfflineJobs::run` gives each job's host a `GuardReport` (an atomic count and the first reason)
through `HostConfig.interfaces`, copies the reason out while the reporting nodes live, and its completion carries
`guardsFired` and `guardReason`; a job a guard stepped into still completes `ok`. Each returned buffer carries
`nonFinite`, computed as native copies it out. `Engine::graphService(id)` becomes public so the live null is pinned. A
scoped `DefaultFloatEnvironment` is held for each job's whole run, decode to copy-out, and restores the caller's
environment after: `fegetenv` / `fesetenv(FE_DFL_ENV)`, then MXCSR FTZ and DAZ or AArch64 FPCR FZ and FZ16 cleared
explicitly, because a worker inherits its submitter's mode and audio hosts commonly flush. The TypeScript wire gains
the same three fields, and the pool's comments say what a guard report is and who decides.

````diff
diff --git a/packages/native/src/graph/nodes/DspNodes.cpp b/packages/native/src/graph/nodes/DspNodes.cpp
index 5f6d715e5..0f279189c 100644
--- a/packages/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/native/src/graph/nodes/DspNodes.cpp
@@ -223,10 +223,10 @@ struct Gain {
 // the host output rejects a non-finite sample. The growth is slow enough to be heard: about 700 samples from a cleared
 // state at 12 kHz on a 22.05 kHz plan, and the kernel's clamp writes rail-to-rail output in every block until the
 // guard fires, so a held above-Nyquist setting is full-scale bursts with the silent blocks the guard writes between
-// the silent blocks the guard writes between them rather than silence. So the node guards the state and leaves the
-// coefficients alone: a channel whose filter came out of a block with any of its four state values not finite has that
-// filter reset and its block written as silence, so a sweep past Nyquist cannot poison the stream, and a frequency that
-// comes back into range is a fresh filter's from the next block on.
+// them rather than silence. So the node guards the state and leaves the coefficients alone: a channel whose filter
+// came out of a block with any of its four state values not finite has that filter reset and its block written as
+// silence, so a sweep past Nyquist cannot poison the stream, and a frequency that comes back into range is a fresh
+// filter's from the next block on.
 //
 // That guard differs from the kernel run directly in two regimes, and it is byte-visible in both: a kit filter whose
 // frequency is above its source rate's Nyquist produces constant codes today (the state goes NaN, and the quantiser
@@ -234,7 +234,11 @@ struct Gain {
 // WAV can carry (nothing between the sample cache and the kit compiler checks), latches the kernel for good, where this
 // node resets. No case in the kit baseline is in either regime, so none of its bytes moved. A kit compile refuses both
 // rather than choosing between the kernel call and this node: a source with a non-finite sample and a filter at or
-// above its source rate's Nyquist are errors, so it never asks this node to match the kernel there.
+// above its source rate's Nyquist are errors, so it never asks this node to match the kernel there. A finite source
+// can still reach the regime (a filter whose gain overflows on huge samples, a non-finite value an earlier node in the
+// same pass made), so the node reports every reset through the host's rp.host.guards/1 when the host offers it: an
+// offline job counts the reports in its completion and a kit compile refuses the pass, so a kit that compiles never
+// carried a reset; a live host offers nothing, and the guard works as it always has.
 struct Biquad {
     static constexpr const char* id = "rp.dsp.biquad";
     static constexpr const char* name = "Biquad";
@@ -261,12 +265,18 @@ struct Biquad {
 
     std::span<rp::dsp::Biquad> filters;  // one per channel, in the instance's tail memory
 
+    const rp_host_guards* guards = nullptr;  // where a reset is reported, when the host offers it
     double               rate = 48000.0;  // as activate gave it
     rp::dsp::BiquadType  type = rp::dsp::BiquadType::LowPass;
     bool                 clear = true;  // the next block computes coefficients and clears the state
     // What the coefficients were last computed for, so a block that changed none of it does no trigonometry.
     float frequency = 0.0f, q = 0.0f, gainDb = 0.0f;
 
+    // The host's guard report, taken once at create: null on a host that offers none, the live engine's included.
+    void bind_host(const rp_host* host) noexcept {
+        guards = static_cast<const rp_host_guards*>(host->get_interface(host, RP_HOST_GUARDS_INTERFACE));
+    }
+
     void attach(std::span<std::byte> mem) noexcept {
         // The host builds these once per instance and never destroys them (node.hpp's hook list), which is sound only
         // because Biquad is trivially destructible: the static_assert above holds that.
@@ -316,6 +326,7 @@ struct Biquad {
                 // and the block may already hold the NaNs it wrote, which the clamp passed through.
                 filter.reset();
                 std::fill(y, y + b.frames(), 0.0f);
+                if (guards) guards->fired(guards->ctx, "rp.dsp.biquad reset a filter whose state went non-finite");
             }
         }
     }
@@ -353,7 +364,9 @@ struct Biquad {
 // nothing between `SampleCache` and the kit compile checks a sample for finiteness, so a 32-bit float WAV carrying a
 // NaN or an infinity latches the kernel for the rest of the slot where this node recovers at the next block. No
 // baseline case carries one, so no kit byte moved; a kit compile refuses a source with a non-finite sample, so it
-// never asks this node to match the kernel there.
+// never asks this node to match the kernel there. A finite input can still overflow the state (a sample near the
+// float range's top, scaled and fed back), so the node reports every restart through the host's rp.host.guards/1 when
+// the host offers it, as the biquad reports its resets: an offline job counts it, a live host hears nothing.
 struct Dither {
     static constexpr const char* id = "rp.dsp.dither";
     static constexpr const char* name = "Dither";
@@ -389,6 +402,12 @@ struct Dither {
     // The buffer a `seedFrom` names, pinned by the instance (so it outlives every block) and read only at activate.
     const rp_audio_buffer* seedFrom = nullptr;
     bool                   restart = true;  // the next block seeds the noise from the fields above
+    const rp_host_guards*  guards = nullptr;  // where a restart is reported, when the host offers it
+
+    // The host's guard report, taken once at create: null on a host that offers none, the live engine's included.
+    void bind_host(const rp_host* host) noexcept {
+        guards = static_cast<const rp_host_guards*>(host->get_interface(host, RP_HOST_GUARDS_INTERFACE));
+    }
 
     void attach(std::span<std::byte> mem) noexcept {
         // Never destroyed, like the biquad's states above; the static_assert holds that Dither is trivially
@@ -436,8 +455,10 @@ struct Dither {
             if (!std::isfinite(state.prevDither) || !std::isfinite(state.shapingState) ||
                 !std::isfinite(state.err1) || !std::isfinite(state.err2) || !std::isfinite(state.error) ||
                 !std::isfinite(state.past[0]) || !std::isfinite(state.past[1]) || !std::isfinite(state.past[2]) ||
-                !std::isfinite(state.past[3]))
+                !std::isfinite(state.past[3])) {
                 seedState(state, c);
+                if (guards) guards->fired(guards->ctx, "rp.dsp.dither restarted a channel whose state went non-finite");
+            }
         }
     }
 };
diff --git a/packages/native/src/graph/rp_node.h b/packages/native/src/graph/rp_node.h
index 52b8a6954..b631aecc9 100644
--- a/packages/native/src/graph/rp_node.h
+++ b/packages/native/src/graph/rp_node.h
@@ -169,6 +169,19 @@ typedef struct rp_host_resources {
     bool (*release)(void* ctx, uint32_t id);
 } rp_host_resources;
 
+/* Where a node reports that one of its guards replaced what its kernel would have computed: a biquad whose state ran
+ * away is reset and its block silenced, a dither whose state went non-finite is restarted. The guard is the node's own
+ * recovery and acts the same whether or not a host takes the report; the report only tells a host that asks (see
+ * docs/modular-graph-phase-d2-design.md). A host that offers no such service answers null, and the node reports
+ * nothing. `fired` is [audio]: it must not block or allocate. The node's `reason` stays valid while the reporting
+ * instance lives (a literal of its own does, for as long as its code is loaded); a host that keeps it longer copies it
+ * first. A node takes the interface in bind_host. */
+#define RP_HOST_GUARDS_INTERFACE "rp.host.guards/1"
+typedef struct rp_host_guards {
+    void* ctx;
+    void (*fired)(void* ctx, const char* reason);
+} rp_host_guards;
+
 typedef enum rp_port_kind {
     RP_PORT_AUDIO    = 0,
     RP_PORT_EVENTS   = 1,
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index c77ea1aff..4ed515bc8 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -192,6 +192,11 @@ public:
     // Offline jobs queued, running, or finished and not yet drained.
     std::size_t offlineJobsOutstanding() const;
 
+    // The host service the live graph's nodes get for `id` from this engine (what rp_host.get_interface answers past
+    // GraphHost's own ids), or null for one it does not offer. A guard report (rp.host.guards/1) is one it does not:
+    // live nodes recover from their guards with nobody told.
+    const void* graphService(const char* id) { return graphInterface(this, id); }
+
     // The owned snapshot store the control plane reads through. Claim a slot at build, release it
     // when the core is deleted; processBlock publishes into it.
     SnapshotRegistry& registry() { return registry_; }
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index 2a79731e6..f2bc60615 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -1,8 +1,11 @@
 #include "host/engine/OfflineJobs.hpp"
 
 #include <algorithm>
+#include <atomic>
 #include <cmath>
 #include <cstdio>
+#include <cstring>
+#include <cfenv>
 #include <exception>
 #include <string_view>
 #include <system_error>
@@ -16,6 +19,13 @@
 #include "graph/nodes/DspResampleNodes.hpp"
 #include "kit/SampleCache.hpp"
 
+#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
+#include <xmmintrin.h>
+#define RP_OFFLINE_FP_MXCSR 1
+#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
+#define RP_OFFLINE_FP_FPCR 1
+#endif
+
 namespace rp::host {
 
 namespace {
@@ -50,6 +60,74 @@ graph::ResourceTable::Id reservePlaceholder(graph::ResourceTable& table) {
     return id;
 }
 
+// The default floating-point environment, held for a job's run and the caller's put back after: round to nearest, no
+// exception trapping, and subnormals kept (no flush-to-zero, no denormals-are-zero). A worker thread starts with the
+// environment of the thread that submitted the job which started it, and an audio host commonly sets flush-to-zero on
+// its threads; a job's floats are a kit's bytes, which the kernels compute in the default mode (a biquad's and the
+// resampler's tails pass through subnormals, and a normalise over a quiet source divides by one), so a job must not
+// compute them in whatever mode its submitter happened to run in. fesetenv(FE_DFL_ENV) gives the standard part; the
+// flush bits are set explicitly as well, since not every C library's default environment clears them (x86's MXCSR
+// FTZ and DAZ, AArch64's FPCR FZ and FZ16).
+class DefaultFloatEnvironment {
+public:
+    DefaultFloatEnvironment() {
+        std::fegetenv(&saved_);
+        std::fesetenv(FE_DFL_ENV);
+#if defined(RP_OFFLINE_FP_MXCSR)
+        savedMxcsr_ = _mm_getcsr();
+        _mm_setcsr(savedMxcsr_ & ~(kFtz | kDaz));
+#elif defined(RP_OFFLINE_FP_FPCR)
+        asm volatile("mrs %0, fpcr" : "=r"(savedFpcr_));
+        const std::uint64_t fpcr = savedFpcr_ & ~(kFz | kFz16);
+        asm volatile("msr fpcr, %0" : : "r"(fpcr));
+#endif
+    }
+    ~DefaultFloatEnvironment() {
+        std::fesetenv(&saved_);
+#if defined(RP_OFFLINE_FP_MXCSR)
+        _mm_setcsr(savedMxcsr_);
+#elif defined(RP_OFFLINE_FP_FPCR)
+        asm volatile("msr fpcr, %0" : : "r"(savedFpcr_));
+#endif
+    }
+    DefaultFloatEnvironment(const DefaultFloatEnvironment&) = delete;
+    DefaultFloatEnvironment& operator=(const DefaultFloatEnvironment&) = delete;
+
+private:
+    std::fenv_t saved_{};
+#if defined(RP_OFFLINE_FP_MXCSR)
+    static constexpr unsigned kFtz = 0x8000u;  // MXCSR bit 15, flush-to-zero
+    static constexpr unsigned kDaz = 0x0040u;  // MXCSR bit 6, denormals-are-zero
+    unsigned savedMxcsr_ = 0;
+#elif defined(RP_OFFLINE_FP_FPCR)
+    static constexpr std::uint64_t kFz = std::uint64_t(1) << 24;    // FPCR.FZ
+    static constexpr std::uint64_t kFz16 = std::uint64_t(1) << 19;  // FPCR.FZ16
+    std::uint64_t savedFpcr_ = 0;
+#endif
+};
+
+// The guard report a job's host offers its nodes (rp.host.guards/1): how many guards fired, and the first one's reason.
+// A node whose guard replaces what its kernel would have computed reports it, and the completion carries the count
+// and the reason on an ok result, as each returned buffer carries whether it holds a non-finite sample: native
+// reports, the caller decides (a kit compile refuses the pass; a render can accept a recovering filter). Nodes report
+// on the block thread, which for a job is its worker; the counters are atomic so that stays true of any driver.
+struct GuardReport {
+    std::atomic<std::uint32_t> count{0};
+    std::atomic<const char*>   first{nullptr};
+    rp_host_guards             service{this, &GuardReport::fired};
+
+    static void fired(void* ctx, const char* reason) {
+        auto* const self = static_cast<GuardReport*>(ctx);
+        const char* none = nullptr;
+        self->first.compare_exchange_strong(none, reason, std::memory_order_relaxed);
+        self->count.fetch_add(1, std::memory_order_relaxed);
+    }
+
+    static const void* interfaces(void* ctx, const char* id) {
+        return std::strcmp(id, RP_HOST_GUARDS_INTERFACE) == 0 ? &static_cast<GuardReport*>(ctx)->service : nullptr;
+    }
+};
+
 }  // namespace
 
 OfflineWindow offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
@@ -241,6 +319,7 @@ OfflineCompletion OfflineJobs::run(Job& job) {
         return std::move(out);
     };
     const auto cancelled = [&job] { return job.cancelled.load(std::memory_order_acquire); };
+    const DefaultFloatEnvironment floatEnvironment;  // the whole run, decode to copy-out, in the default mode
     OfflineRequest& req = job.request;
     try {
         if (cancelled()) return fail("cancelled");
@@ -315,9 +394,12 @@ OfflineCompletion OfflineJobs::run(Job& job) {
             if (cancelled()) return fail("cancelled");
             auto doc = graph::parseGraphDocument(req.document);
             if (!doc) return fail(std::string("not a graph document: ") + doc.error().what());
+            GuardReport       guards;  // before the host, whose nodes report into it until they go with it
             graph::HostConfig config;
             config.sampleRate = req.rate;
             config.maxFrames = kBlockFrames;
+            config.interfaces = &GuardReport::interfaces;
+            config.interfacesCtx = &guards;
             graph::GraphHost host(registry_, resources, config);
             const graph::SetGraphResult planned = host.setGraph(*doc);
             if (!planned.ok) return fail("the document was refused: " + describe(planned.errors));
@@ -335,6 +417,9 @@ OfflineCompletion OfflineJobs::run(Job& job) {
                     if (host.isFaulted(n.id)) faulted += (faulted.empty() ? "" : ", ") + n.id;
                 return fail("a node faulted: " + faulted);
             }
+            // Copied while the nodes that own the reason are still alive: the host and they go at this scope's end.
+            out.guardsFired = guards.count.load(std::memory_order_relaxed);
+            if (out.guardsFired != 0) out.guardReason = guards.first.load(std::memory_order_relaxed);
         }  // the host goes here, on this thread, and every node it built with it
 
         for (const std::uint32_t h : req.returns) {
@@ -350,6 +435,8 @@ OfflineCompletion OfflineJobs::run(Job& job) {
                 for (std::uint32_t f = 0; f < b->frames; ++f)
                     for (std::uint32_t c = 0; c < b->channels; ++c)
                         buffer.samples[std::size_t(f) * b->channels + c] = b->data[c][f];
+                buffer.nonFinite = std::any_of(buffer.samples.begin(), buffer.samples.end(),
+                                               [](float v) { return !std::isfinite(v); });
             }
             out.buffers.push_back(std::move(buffer));
         }
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
index 150b09b96..be0e54440 100644
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -124,13 +124,16 @@ struct OfflineSubmit {
     std::string   error;
 };
 
-// A buffer a completion returns: interleaved, `channels` x `frames`.
+// A buffer a completion returns: interleaved, `channels` x `frames`, and whether any sample is not finite (read as the
+// samples are copied out, so a caller that refuses a non-finite recording, as a kit compile does, need not scan them
+// itself, which in a script runtime costs more than the job).
 struct OfflineBuffer {
     std::uint32_t      handle = 0;
     std::uint32_t      channels = 0;
     std::uint32_t      frames = 0;
     double             rate = 0.0;
     std::vector<float> samples;
+    bool               nonFinite = false;
 };
 
 // What a file input decoded to and kept: the decoded rate and length, the window's length, and whether any sample the
@@ -144,13 +147,19 @@ struct OfflineInputReport {
 };
 
 // A finished job: ok with the buffers `returns` named, or the reason it failed. `inputs` reports each file input
-// decoded before the job ended, a failed job's included.
+// decoded before the job ended, a failed job's included. `guardsFired` counts the times a node's guard replaced what
+// its kernel would have computed during the run (rp.host.guards/1, which each job's host offers its nodes), and
+// `guardReason` is the first one's reason, empty when none fired: a job a guard stepped into still completes, and what
+// it means is the caller's to decide (a kit compile refuses the pass, since its bytes are the kernels'; a render can
+// accept a recovering filter).
 struct OfflineCompletion {
     std::uint32_t                   id = 0;
     bool                            ok = false;
     std::string                     error;
     std::vector<OfflineBuffer>      buffers;
     std::vector<OfflineInputReport> inputs;
+    std::uint32_t                   guardsFired = 0;
+    std::string                     guardReason;
 };
 
 class OfflineJobs {
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index 63c025f7d..56be281da 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -209,14 +209,15 @@ struct RpcOfflineSubmitResult {
     std::optional<std::uint32_t> id;
     std::optional<std::string>   error;
 };
-// A returned buffer, `samples` interleaved float32 as an input's are; and a file input's report: the decoded rate and
-// length, the window's length, and whether a sample the window kept is not finite.
+// A returned buffer, `samples` interleaved float32 as an input's are, and whether any of them is not finite; and a file
+// input's report: the decoded rate and length, the window's length, and whether a sample the window kept is not finite.
 struct RpcOfflineBuffer {
     std::uint32_t   handle;
     std::uint32_t   channels;
     std::uint32_t   frames;
     double          rate;
     rfl::Bytestring samples;
+    bool            nonFinite;
 };
 struct RpcOfflineInputReport {
     std::uint32_t handle;
@@ -225,13 +226,17 @@ struct RpcOfflineInputReport {
     std::uint32_t frames;
     bool          nonFinite;
 };
-// A finished job: `ok` with its `buffers`, or `error`; `inputs` reports every file input decoded before it ended.
+// A finished job: `ok` with its `buffers`, or `error`; `inputs` reports every file input decoded before it ended;
+// `guardsFired` counts the node guards that stepped in during the run, `guardReason` the first one's reason (empty for
+// none).
 struct RpcOfflineCompletion {
     std::uint32_t                      id;
     bool                               ok;
     std::string                        error;
     std::vector<RpcOfflineBuffer>      buffers;
     std::vector<RpcOfflineInputReport> inputs;
+    std::uint32_t                      guardsFired;
+    std::string                        guardReason;
 };
 // What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running after
 // them (so a caller holding a promise for a job native no longer holds learns so, rather than poll for it forever).
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index c38c0b137..68554b0a7 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -292,14 +292,14 @@ RpcOfflineDrain EngineRpcService::drainOfflineJobs(double waitMs) {
     RpcOfflineDrain out{{}, 0};
     std::size_t     inFlight = 0;
     for (auto& c : engine_.drainOfflineJobs(std::chrono::milliseconds(static_cast<std::int64_t>(ms)), &inFlight)) {
-        RpcOfflineCompletion rc{c.id, c.ok, std::move(c.error), {}, {}};
+        RpcOfflineCompletion rc{c.id, c.ok, std::move(c.error), {}, {}, c.guardsFired, std::move(c.guardReason)};
         for (auto& b : c.buffers) {
             rfl::Bytestring bytes;
             if (!b.samples.empty()) {
                 const auto* p = reinterpret_cast<const std::byte*>(b.samples.data());
                 bytes = rfl::Bytestring(p, p + b.samples.size() * sizeof(float));
             }
-            rc.buffers.push_back({b.handle, b.channels, b.frames, b.rate, std::move(bytes)});
+            rc.buffers.push_back({b.handle, b.channels, b.frames, b.rate, std::move(bytes), b.nonFinite});
         }
         for (const auto& i : c.inputs)
             rc.inputs.push_back({i.handle, i.decodedRate, i.decodedFrames, i.frames, i.nonFinite});
diff --git a/packages/retroplug/src/offline.ts b/packages/retroplug/src/offline.ts
index e1a4f0fec..db732b2e7 100644
--- a/packages/retroplug/src/offline.ts
+++ b/packages/retroplug/src/offline.ts
@@ -75,13 +75,15 @@ export interface OfflineRunRequest {
   rate: number;
 }
 
-/** A returned buffer: interleaved, `channels` x `frames` at `rate`. */
+/** A returned buffer: interleaved, `channels` x `frames` at `rate`, and whether any sample is not finite (native reads
+ *  it as it copies the samples out, which is far cheaper than a scan here). */
 export interface OfflineBuffer {
   handle: number;
   channels: number;
   frames: number;
   rate: number;
   samples: Float32Array;
+  nonFinite: boolean;
 }
 
 /** What a file input decoded to and kept: the decoded rate and length, the window's length (0: an empty window, an
@@ -98,6 +100,11 @@ export interface OfflineInputReport {
 export interface OfflineRunResult {
   buffers: OfflineBuffer[];
   inputs: OfflineInputReport[];
+  /** How many times a node's guard replaced what its kernel would have computed during the run (a biquad reset, a
+   *  dither restart; native's rp.host.guards/1), and the first one's reason ("" for none). The job completed either
+   *  way: whether a guard's output is acceptable is the caller's call (a kit compile refuses it). */
+  guardsFired: number;
+  guardReason: string;
 }
 
 /** A job that failed: native's reason, and the reports of the file inputs it decoded before it stopped. */
@@ -141,12 +148,22 @@ export interface OfflineSubmitResult {
   id?: number | null;
   error?: string | null;
 }
+export interface OfflineWireBuffer {
+  handle: number;
+  channels: number;
+  frames: number;
+  rate: number;
+  samples: Uint8Array;
+  nonFinite: boolean;
+}
 export interface OfflineWireCompletion {
   id: number;
   ok: boolean;
   error: string;
-  buffers: { handle: number; channels: number; frames: number; rate: number; samples: Uint8Array }[];
+  buffers: OfflineWireBuffer[];
   inputs: OfflineInputReport[];
+  guardsFired: number;
+  guardReason: string;
 }
 
 /** What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running. */
@@ -281,8 +298,11 @@ export class OfflineJobs implements JobQueue {
           frames: b.frames,
           rate: b.rate,
           samples: floats(b.samples),
+          nonFinite: b.nonFinite,
         })),
         inputs: c.inputs,
+        guardsFired: c.guardsFired,
+        guardReason: c.guardReason,
       });
     }
     // A promise whose job native no longer holds (none came back, and nothing is in flight) would otherwise be polled
````

Run: `sed -n '6092,6545p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29182 assertions in 14 test cases)`.

- [ ] **Step 3: Write the failing compiler cases**

`test-native/kit/sources.ts` (new) is the baseline's 19 synthesised sources, transcribed at the kit path's precision:
`Mt19937`, the float WAV writer, FNV-1a-64, every binary32 rounding a `Math.fround`, and `SOURCE_HASHES`, each
source's FNV over its float bytes as the retired C++ suite generated them. `kit-baseline.test.ts` (new) runs the
TypeScript compiler over the 39-case matrix against `kit-baseline.json` under the native suite's rules: size and slot
everywhere, hash on `linux-x64-gnu13`, a second compile agreeing elsewhere, `RP_KIT_GOLDEN_REQUIRE_TARGET=1` failing a
foreign host, and `RP_KIT_GOLDEN_UPDATE=1` refused off the target and for a disagreeing case. `kit-compile.test.ts`
(new) holds the refusals, the overflows and every window and DMC edge to `kit-edges.json` (and, until native retires,
to native's own banks). `test/kit/kit.test.ts` (new) covers the pure half: the integer conversions, the refusals'
messages, the segment split and the documents, the range constants against the descriptor goldens, the assemblers'
names and placements, and a slot's chain over a scripted runner. The scripted port's completions in
`test/offline/offline.test.ts` gain `nonFinite`, `guardsFired` and `guardReason`, which `test/kit/kit.test.ts`'s
refusal case drives: a pass that records a non-finite sample, or that a node's guard stepped into, refuses its slot.

Create `packages/retroplug/test-native/kit/sources.ts`:

````ts
// The kit baseline's synthesised sources, bit for bit the ones packages/native/test/audio/KitBaseline.test.cpp
// computes, and what writing and hashing them takes. Every expression is the C++ one in the same order: the double
// arithmetic is JavaScript's own (both are IEEE doubles, and Math.sin on this host is the C library's sin, which
// std::sin is too), and every place the C++ rounds to float is a Math.fround here. A binary32 product rounded once is
// exactly binary32 multiplication (the double product of two floats is exact), and a double difference of two floats
// rounded once is binary32 subtraction, so each fround below is one C++ float operation.
//
// The proof that these are the C++ sources is SOURCE_HASHES: each source's FNV-1a-64 over its float bytes, recorded
// from the C++ suite, which kit-baseline.test.ts holds every source to before it compiles anything.

/** The source signals, as the C++ suite names them. */
export type Signal = "sine440" | "sine1200" | "quiet" | "chirp" | "noise" | "silence" | "short";

/** Each signal's length in frames: 8192, and 600 for the source shorter than one resampler chunk. */
export function signalFrames(signal: Signal): number {
  return signal === "short" ? 600 : 8192;
}

const F = Math.fround;

// 0.8f * float(sin(2 * pi * hz * t)), t = i / rate in double.
function sine(hz: number, rate: number, frames: number): Float32Array {
  const out = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const t = i / rate;
    out[i] = F(F(0.8) * F(Math.sin(2.0 * Math.PI * hz * t)));
  }
  return out;
}

// A 200 Hz to 4000 Hz linear sweep, its phase integrated in double: 0.7f * float(sin(phase)).
function chirp(rate: number, frames: number): Float32Array {
  const out = new Float32Array(frames);
  let phase = 0.0;
  for (let i = 0; i < frames; i++) {
    const f = 200.0 + ((4000.0 - 200.0) * i) / frames;
    phase += (2.0 * Math.PI * f) / rate;
    out[i] = F(F(0.7) * F(Math.sin(phase)));
  }
  return out;
}

/** std::mt19937's 32-bit Mersenne Twister, its output sequence as the standard specifies it (the C++ noise source's
 *  generator, seeded by its one-word constructor). */
export class Mt19937 {
  private readonly mt = new Uint32Array(624);
  private index = 624;

  constructor(seed: number) {
    this.mt[0] = seed >>> 0;
    for (let i = 1; i < 624; i++) {
      const prev = this.mt[i - 1] ^ (this.mt[i - 1] >>> 30);
      this.mt[i] = (Math.imul(1812433253, prev) + i) >>> 0;
    }
  }

  next(): number {
    if (this.index >= 624) {
      for (let i = 0; i < 624; i++) {
        const y = (this.mt[i] & 0x80000000) | (this.mt[(i + 1) % 624] & 0x7fffffff);
        let v = this.mt[(i + 397) % 624] ^ (y >>> 1);
        if (y & 1) v ^= 0x9908b0df;
        this.mt[i] = v >>> 0;
      }
      this.index = 0;
    }
    let y = this.mt[this.index++];
    y ^= y >>> 11;
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= y >>> 18;
    return y >>> 0;
  }
}

// Uniform noise in [-0.6, 0.6): unit = float(rng() >> 8) * 2^-24 (exact), then 1.2f * unit - 0.6f in binary32.
function noise(frames: number): Float32Array {
  const rng = new Mt19937(0x5eed1234);
  const out = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const unit = F((rng.next() >>> 8) * (1 / 16777216));
    out[i] = F(F(F(1.2) * unit) - F(0.6));
  }
  return out;
}

/** A signal's samples at `rate`, as the C++ suite's signalSamples computes them. */
export function signalSamples(signal: Signal, rate: number): Float32Array {
  const frames = signalFrames(signal);
  switch (signal) {
    case "sine440":
    case "short":
      return sine(440.0, rate, frames);
    case "sine1200":
      return sine(1200.0, rate, frames);
    case "quiet": {
      // The sine scaled by 1/16, an exact power of two: v *= 0.0625f.
      const out = sine(440.0, rate, frames);
      for (let i = 0; i < out.length; i++) out[i] = F(out[i] * 0.0625);
      return out;
    }
    case "chirp":
      return chirp(rate, frames);
    case "noise":
      return noise(frames);
    case "silence":
      return new Float32Array(frames);
  }
}

/** The C++ suite's file name stem for a source: `<signal>-<rate>`. */
export function sourceKey(signal: Signal, rate: number): string {
  return `${signal}-${rate}`;
}

/** Each source's FNV-1a-64 over its float bytes (little-endian), recorded from KitBaseline.test.cpp's sources on
 *  linux-x64-gnu13. The noise and the silence do not depend on the rate, so their three hashes are one. */
export const SOURCE_HASHES: Readonly<Record<string, string>> = {
  "chirp-22050": "0425491f935aed01",
  "chirp-44100": "735870598c6d9d72",
  "chirp-48000": "a595850389c0d047",
  "noise-22050": "08ca2ba2e1299154",
  "noise-44100": "08ca2ba2e1299154",
  "noise-48000": "08ca2ba2e1299154",
  "quiet-22050": "42161ba4d3471d02",
  "quiet-44100": "afedb2e5342405a5",
  "quiet-48000": "91ea2ea83ee3d194",
  "short-44100": "0cf632f5372f90b2",
  "silence-22050": "8f6955bf94ec2325",
  "silence-44100": "8f6955bf94ec2325",
  "silence-48000": "8f6955bf94ec2325",
  "sine1200-22050": "5bb6e9a74e070fbe",
  "sine1200-44100": "d24e7b47a71f64ab",
  "sine1200-48000": "c1f44b47987b2501",
  "sine440-22050": "2d851d6783d27970",
  "sine440-44100": "882a8e6d90d79c07",
  "sine440-48000": "331dabe7b2984a0a",
};

/** A 32-bit float, mono WAV (fmt tag 3), every field little-endian: the C++ suite's writeFloatWav, byte for byte. */
export function floatWav(samples: Float32Array, rate: number): Uint8Array {
  const bytes = new Uint8Array(44 + samples.length * 4);
  const v = new DataView(bytes.buffer);
  const tag = (at: number, s: string): void => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)));
  tag(0, "RIFF");
  v.setUint32(4, 36 + samples.length * 4, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  v.setUint32(16, 16, true);
  v.setUint16(20, 3, true); // IEEE float
  v.setUint16(22, 1, true); // mono
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 4, true);
  v.setUint16(32, 4, true);
  v.setUint16(34, 32, true);
  tag(36, "data");
  v.setUint32(40, samples.length * 4, true);
  samples.forEach((s, i) => v.setFloat32(44 + i * 4, s, true));
  return bytes;
}

/** FNV-1a-64 of `bytes` as 16 lowercase hex digits (rp::hash::fnv1a64, the kit baseline's bank hash). The 64-bit
 *  state is two 32-bit halves: the prime is 2^40 + 0x1b3, so a multiply is the state times 0x1b3 plus the state
 *  shifted left 40, each half exact in a double before it is reduced. */
export function fnv1a64Hex(bytes: Uint8Array): string {
  let hi = 0xcbf29ce4;
  let lo = 0x84222325;
  for (let i = 0; i < bytes.length; i++) {
    lo = (lo ^ bytes[i]) >>> 0;
    const loProduct = lo * 0x1b3;
    const carry = Math.floor(loProduct / 0x100000000);
    const newHi = (Math.imul(hi, 0x1b3) >>> 0) + carry + (((lo & 0xffffff) << 8) >>> 0);
    lo = loProduct >>> 0;
    hi = newHi >>> 0;
  }
  return hi.toString(16).padStart(8, "0") + lo.toString(16).padStart(8, "0");
}

/** The float bytes of `samples`, little-endian (the C++ hash reads a float's bytes in memory order on this host). */
export function floatBytes(samples: Float32Array): Uint8Array {
  const out = new Uint8Array(samples.length * 4);
  const v = new DataView(out.buffer);
  samples.forEach((s, i) => v.setFloat32(i * 4, s, true));
  return out;
}
````

Run: `mkdir -p packages/retroplug/test-native/kit; sed -n '6572,6756p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test-native/kit/sources.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/kit/sources.ts`
Expected: `ff1f286922135bab98ea44b3af005f3e76327e5f`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/kit-baseline.test.ts`:

````ts
// The kit compilers' byte baseline through the TypeScript compiler (src/kit/, docs/modular-graph-phase-d2-design.md
// sections 5 and 6): every case of packages/native/test/audio/golden/kit-baseline.json compiled as chains of offline
// graph jobs on this host's real engine, its bank held to the record native KitCompiler made. The matrix, its sources
// and its rules are KitBaseline.test.cpp's (retroplug-audio-test "[kit-baseline]"), transcribed: one record, and two
// compilers held to it until native's retires, when this file is the one that keeps it.
//
// The sources are synthesised (test-native/kit/sources.ts), written as 32-bit float WAVs into this run's config dir and
// decoded natively by the offline jobs' file input, which is the native suite's SampleCache decode, so nothing here
// reads a fixture and CI runs it. Before any case compiles, each source is decoded back through a job to the samples
// written, and on the target held to the C++ suite's own floats (their FNV-1a-64, SOURCE_HASHES), so a bank that
// matches is the C++ suite's input compiled, not a coincidence of two different inputs. Only on the target: the hashes
// were recorded from glibc's sin, and Math.sin is the host's own libm (Apple's on macOS, the UCRT on Windows), whose
// last bit may differ now and then; off the target a case only has to agree with itself, so a source one ulp away
// there changes no verdict.
//
// The rules are the native suite's. A record is {hash, size, slot}: the bank's FNV-1a-64 in lowercase hex, its size,
// and slot 0's length as the bank records it. The size and the slot length are compared on every host; the hash only
// on the host the golden was recorded on (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic is
// bit-stable only for one compiler on one architecture. Elsewhere a case compiles twice and must agree with itself,
// and RP_KIT_GOLDEN_REQUIRE_TARGET=1 makes a foreign host fail instead (CI's linux-x64 job sets it on its test:native
// step). The case names must be the golden's keys exactly, none missing and none extra.
//
// RP_KIT_GOLDEN_UPDATE=1 re-records the golden from this compiler, refused anywhere but the target and for any case
// whose two compiles disagree, and written through a sibling temp file, the native suite's refusals. Without the
// variable the file is only ever read.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver, type KitEffect } from "../src/audioDriver";
import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
import {
  SOURCE_HASHES,
  fnv1a64Hex,
  floatBytes,
  floatWav,
  signalFrames,
  signalSamples,
  sourceKey,
  type Signal,
} from "./kit/sources";

declare const __KIT_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const env = tjs.env;
const UPDATE = env.RP_KIT_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";

// ---- the matrix (KitBaseline.test.cpp's kitCases, in its order) -----------------------------------------------------

type Codec = "lsdj" | "risa";

interface ExtraSlot {
  name: string;
  effects: KitEffect[];
  dmcRate?: number;
}

interface KitCase {
  name: string;
  codec: Codec;
  signal: Signal;
  rate: number;
  effects: KitEffect[];
  rotate?: boolean;
  dmcRate?: number;
  normalize?: boolean;
  loop?: boolean;
  extraSlots?: ExtraSlot[];
  kitName?: string;
}

const gain = (g: number, normalize: boolean): KitEffect => ({ type: "gain", normalize, gain: g });
type FilterType = NonNullable<Extract<KitEffect, { type: "filter" }>["filterType"]>;
const filter = (filterType: FilterType, frequency: number, q: number, gainDb = 0): KitEffect => ({
  type: "filter",
  filterType,
  frequency,
  q,
  gain: gainDb,
});
type DitherType = NonNullable<Extract<KitEffect, { type: "dither" }>["ditherType"]>;
const dither = (ditherType: DitherType): KitEffect => ({ type: "dither", ditherType });

const CASES: KitCase[] = [
  { name: "lsdj/no-effect", codec: "lsdj", signal: "sine440", rate: 44100, effects: [] },
  { name: "lsdj/gain", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(0.37, false)] },
  { name: "lsdj/normalize", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(0.37, true)] },
  { name: "lsdj/normalize-silence", codec: "lsdj", signal: "silence", rate: 44100, effects: [gain(0.37, true)] },
  { name: "lsdj/normalize-quiet", codec: "lsdj", signal: "quiet", rate: 22050, effects: [gain(0.37, true)] },
  {
    name: "lsdj/normalize-quiet-dither",
    codec: "lsdj",
    signal: "quiet",
    rate: 44100,
    effects: [gain(0.37, true), dither("HighPassTPDF")],
  },
  { name: "lsdj/normalize-quiet-48000", codec: "lsdj", signal: "quiet", rate: 48000, effects: [gain(0.25, true)] },
  { name: "lsdj/gain-negative", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(-1.0, false)] },
  {
    name: "lsdj/filter/low-pass",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [filter("LowPass", 2000, 0.9)],
  },
  {
    name: "lsdj/filter/high-pass",
    codec: "lsdj",
    signal: "sine440",
    rate: 22050,
    effects: [filter("HighPass", 800, 1.4)],
  },
  {
    name: "lsdj/filter/band-pass",
    codec: "lsdj",
    signal: "sine1200",
    rate: 48000,
    effects: [filter("BandPass", 1200, 2.0)],
  },
  {
    name: "lsdj/filter/band-stop",
    codec: "lsdj",
    signal: "chirp",
    rate: 48000,
    effects: [filter("BandStop", 4000, 0.7)],
  },
  {
    name: "lsdj/filter/peak",
    codec: "lsdj",
    signal: "sine1200",
    rate: 44100,
    effects: [filter("Peak", 1000, 1.0, 6.0)],
  },
  {
    name: "lsdj/filter/low-shelf",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("LowShelf", 800, 0.7, -6.0)],
  },
  {
    name: "lsdj/filter/high-shelf",
    codec: "lsdj",
    signal: "chirp",
    rate: 22050,
    effects: [filter("HighShelf", 3000, 0.7, 4.0)],
  },
  {
    name: "lsdj/filter/all-pass",
    codec: "lsdj",
    signal: "noise",
    rate: 48000,
    effects: [filter("AllPass", 1500, 1.2)],
  },
  {
    name: "lsdj/filter/high-pass-outside-range",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [filter("HighPass", 10, 20)],
  },
  {
    name: "lsdj/filter/high-shelf-outside-range",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("HighShelf", 3000, 0.7, 30.0)],
  },
  {
    name: "lsdj/dither/high-pass-tpdf",
    codec: "lsdj",
    signal: "sine1200",
    rate: 22050,
    effects: [dither("HighPassTPDF")],
  },
  { name: "lsdj/dither/shaped-tpdf", codec: "lsdj", signal: "noise", rate: 44100, effects: [dither("ShapedTPDF")] },
  {
    name: "lsdj/dither/error-diffusion",
    codec: "lsdj",
    signal: "chirp",
    rate: 44100,
    effects: [dither("ErrorDiffusion")],
  },
  { name: "lsdj/dither/jjn", codec: "lsdj", signal: "sine440", rate: 48000, effects: [dither("JJN")] },
  { name: "lsdj/dither/sierra-lite", codec: "lsdj", signal: "sine1200", rate: 44100, effects: [dither("SierraLite")] },
  { name: "lsdj/rotate-off", codec: "lsdj", signal: "chirp", rate: 44100, effects: [], rotate: false },
  { name: "lsdj/rotate-on", codec: "lsdj", signal: "chirp", rate: 44100, effects: [], rotate: true },
  {
    name: "lsdj/chain/filter-gain-dither",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("LowPass", 3000, 1.0), gain(0.5, false), dither("HighPassTPDF")],
  },
  { name: "lsdj/short-source", codec: "lsdj", signal: "short", rate: 44100, effects: [] },
  {
    name: "lsdj/two-slots",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [],
    extraSlots: [{ name: "SNR", effects: [gain(0.5, false)] }],
    kitName: "baseline-kit",
  },

  { name: "risa/no-effect-rate-4", codec: "risa", signal: "sine440", rate: 44100, effects: [], dmcRate: 4 },
  { name: "risa/no-effect-rate-12", codec: "risa", signal: "chirp", rate: 48000, effects: [], dmcRate: 12 },
  { name: "risa/no-effect-rate-15", codec: "risa", signal: "noise", rate: 22050, effects: [], dmcRate: 15 },
  {
    name: "risa/gain-rate-10",
    codec: "risa",
    signal: "sine1200",
    rate: 44100,
    effects: [gain(0.4, false)],
    dmcRate: 10,
  },
  {
    name: "risa/filter-rate-8",
    codec: "risa",
    signal: "noise",
    rate: 48000,
    effects: [filter("LowPass", 3000, 1.0)],
    dmcRate: 8,
  },
  { name: "risa/short-source-rate-12", codec: "risa", signal: "short", rate: 44100, effects: [], dmcRate: 12 },
  {
    name: "risa/no-normalize-rate-12",
    codec: "risa",
    signal: "noise",
    rate: 44100,
    effects: [],
    dmcRate: 12,
    normalize: false,
  },
  { name: "risa/loop-rate-4", codec: "risa", signal: "sine440", rate: 44100, effects: [], dmcRate: 4, loop: true },
  { name: "risa/normalize-silence-rate-12", codec: "risa", signal: "silence", rate: 22050, effects: [], dmcRate: 12 },
  { name: "risa/normalize-silence-rate-4", codec: "risa", signal: "silence", rate: 48000, effects: [], dmcRate: 4 },
  {
    name: "risa/two-slots-rate-12",
    codec: "risa",
    signal: "chirp",
    rate: 48000,
    effects: [],
    dmcRate: 12,
    extraSlots: [{ name: "SNR", effects: [gain(0.4, false)], dmcRate: 4 }],
  },
];

// ---- the sources ----------------------------------------------------------------------------------------------------

const SIGNALS: Signal[] = ["sine440", "sine1200", "quiet", "chirp", "noise", "silence", "short"];
const RATES = [22050, 44100, 48000];

interface SourceFile {
  path: string;
  samples: Float32Array;
}

const sources = new Map<string, SourceFile>();
for (const c of CASES) {
  const key = sourceKey(c.signal, c.rate);
  if (sources.has(key)) continue;
  const samples = signalSamples(c.signal, c.rate);
  const path = `${be.configDir()}/kit-baseline-${key}.wav`;
  if (!be.writeFile(path, floatWav(samples, c.rate))) throw new Error(`cannot write ${path}`);
  sources.set(key, { path, samples });
}

// ---- compiling a case, and reading its bank ---------------------------------------------------------------------

// A case's kit as its RPC spec, compiled by the TypeScript compiler: slot 0 is KIK with the case's effects, each extra
// slot the same sample under its own name and effects (and, on risa, its own rate index when it has one).
function compileCase(c: KitCase): Promise<KitCompileResult> {
  const path = sources.get(sourceKey(c.signal, c.rate))!.path;
  const kitName = c.kitName ?? (c.codec === "lsdj" ? "BASELINE" : "RISA");
  if (c.codec === "lsdj") {
    const slot = { path, name: "KIK", effects: c.effects };
    const extra = (c.extraSlots ?? []).map((x) => ({ ...slot, name: x.name, effects: x.effects }));
    return compileKit(be, { name: kitName, samples: [slot, ...extra], rotate: c.rotate ?? true });
  }
  const slot = {
    path,
    name: "KIK",
    effects: c.effects,
    rate: c.dmcRate ?? 12,
    normalize: c.normalize ?? true,
    loop: c.loop ?? false,
  };
  const extra = (c.extraSlots ?? []).map((x) => ({
    ...slot,
    name: x.name,
    effects: x.effects,
    rate: x.dmcRate ?? slot.rate,
  }));
  return compileDmc(be, { name: kitName, samples: [slot, ...extra] });
}

const LSDJ_BANK = 0x4000;
const RISA_BANK = 0x2000;
const RISA_INDEX = 0x1f00;

// Whether the bank carries every slot the case compiles: without it a case whose slots all came out empty would pin a
// stable empty bank and pass. LSDj: the offset table only moves forward, so slot i is present when entry i + 1 is past
// entry i. risa: a slot's index address byte is 0xFF when it is empty.
function bankHasSlots(c: KitCase, bank: Uint8Array): boolean {
  const slots = 1 + (c.extraSlots?.length ?? 0);
  if (c.codec === "lsdj") {
    if (bank.length !== LSDJ_BANK) return false;
    const entry = (i: number): number => bank[i * 2] | (bank[i * 2 + 1] << 8);
    if (entry(0) !== 0x4060) return false;
    for (let i = 0; i < slots; i++) if (entry(i + 1) <= entry(i)) return false;
    return true;
  }
  if (bank.length !== RISA_BANK) return false;
  for (let i = 0; i < slots; i++) if (bank[RISA_INDEX + i * 4] === 0xff) return false;
  return true;
}

// Slot 0's length as the bank records it: LSDj's offset table gives the packed bytes exactly; risa's length register
// gives (reg + 1) * 16, the $4013 playback span.
function slotLength(c: KitCase, bank: Uint8Array): number {
  if (c.codec === "lsdj") return (bank[2] | (bank[3] << 8)) - (bank[0] | (bank[1] << 8));
  return (bank[RISA_INDEX + 1] + 1) * 16;
}

interface BankGolden {
  hash: string;
  size: number;
  slot: number;
}
interface Golden {
  target: string;
  cases: Record<string, BankGolden>;
}

function readGolden(): Golden {
  const bytes = be.readFile(__KIT_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__KIT_GOLDEN__}; record it with RP_KIT_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as Golden;
}

// The golden as the native suite's writeGolden writes it, keys in byte order, so a recording from either suite gives
// one file.
function goldenText(golden: Golden): string {
  const names = Object.keys(golden.cases).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const rows = names.map((n) => {
    const b = golden.cases[n];
    return `    "${n}": { "hash": "${b.hash}", "size": ${b.size}, "slot": ${b.slot} }`;
  });
  return `{\n  "target": "${golden.target}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
}

function firstDifference(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}

// ---- the cases --------------------------------------------------------------------------------------------------

test("the matrix is the golden's: sources for every signal at every rate, and its names are the golden's keys", () => {
  for (const signal of SIGNALS)
    for (const rate of RATES) {
      if (signal === "short" && rate !== 44100) continue;
      expect(sources.has(sourceKey(signal, rate)), `${signal} at ${rate} Hz`).toBe(true);
    }
  expect(new Set(CASES.map((c) => c.name)).size).toBe(CASES.length);
  if (UPDATE) return; // a recording writes the keys this matrix has
  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  expect(CASES.map((c) => c.name).sort()).toEqual(Object.keys(golden.cases).sort());
});

test("every source decodes through a job to the samples written, and on the target is the C++ suite's", async () => {
  expect(sources.size).toBe(Object.keys(SOURCE_HASHES).length);
  const onTarget = audio.buildTarget() === BASELINE_TARGET;
  for (const [key, source] of sources) {
    expect(source.samples.length, key).toBe(signalFrames(key.split("-")[0] as Signal));
    if (onTarget) expect(fnv1a64Hex(floatBytes(source.samples)), `${key}'s floats`).toBe(SOURCE_HASHES[key]);
  }
  const loads = [...sources].map(async ([key, source]) => {
    const r = await be.offlineRun({ inputs: [{ path: source.path }], returns: [1], frames: 0, rate: 48000 });
    return { key, source, r };
  });
  for (const { key, source, r } of await Promise.all(loads)) {
    const rate = Number(key.split("-")[1]);
    expect(r.inputs[0].decodedRate, key).toBe(rate);
    expect(r.inputs[0].nonFinite, key).toBe(false);
    expect(r.buffers[0].frames, key).toBe(source.samples.length);
    expect(firstDifference(floatBytes(r.buffers[0].samples), floatBytes(source.samples)), `${key} decoded`).toBe(-1);
  }
});

const COMPILE_CASE = UPDATE
  ? "record every case's bank to the golden"
  : "every kit case compiles to the bank the golden records";

test(COMPILE_CASE, async () => {
  const host = audio.buildTarget();
  const onTarget = host === BASELINE_TARGET;

  // Every case at once: the engine's pool runs their jobs side by side, and each chain is the case's own.
  const first = await Promise.all(CASES.map(compileCase));
  const second = UPDATE || !onTarget ? await Promise.all(CASES.map(compileCase)) : null;

  if (UPDATE) {
    if (!onTarget)
      throw new Error(
        `a recording only happens on ${BASELINE_TARGET}, and this host is ${host}; ` +
          "a digest recorded here would be a fallback, not the baseline",
      );
    const recorded: Golden = { target: BASELINE_TARGET, cases: {} };
    CASES.forEach((c, k) => {
      const kit = first[k];
      expect(kit.errors, c.name).toEqual(kit.errors.map(() => null));
      expect(bankHasSlots(c, kit.bytes), `${c.name} carries its slots`).toBe(true);
      // A case whose two compiles disagree depends on something outside its input: its digest would mean nothing.
      expect(firstDifference(second![k].bytes, kit.bytes), `${c.name} compiles twice to one bank`).toBe(-1);
      recorded.cases[c.name] = { hash: fnv1a64Hex(kit.bytes), size: kit.bytes.length, slot: slotLength(c, kit.bytes) };
    });
    expect(Object.keys(recorded.cases).length).toBe(CASES.length);
    const text = new TextEncoder().encode(goldenText(recorded));
    expect(be.writeFileAtomic(__KIT_GOLDEN__, text), `write ${__KIT_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${CASES.length} banks to ${__KIT_GOLDEN__}`);
    return;
  }

  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  if (REQUIRE_TARGET && !onTarget)
    throw new Error(
      `this host is ${host} but the baseline was recorded on ${golden.target}, ` +
        "so exact kit bytes would not be compared",
    );

  let matched = 0;
  CASES.forEach((c, k) => {
    const kit = first[k];
    const expected = golden.cases[c.name];
    expect(expected !== undefined, `${c.name} is in the golden`).toBe(true);
    expect(kit.errors, `${c.name}'s slot errors`).toEqual(kit.errors.map(() => null));
    expect(bankHasSlots(c, kit.bytes), `${c.name} carries its slots`).toBe(true);
    expect(kit.bytes.length, `${c.name}'s size`).toBe(expected.size);
    expect(slotLength(c, kit.bytes), `${c.name}'s slot length`).toBe(expected.slot);
    if (onTarget) {
      expect(fnv1a64Hex(kit.bytes), `${c.name}'s bank hash`).toBe(expected.hash);
    } else {
      // Off the target only self-agreement is meaningful: the case must be reproducible from its input alone.
      expect(firstDifference(second![k].bytes, kit.bytes), `${c.name} compiles twice to one bank`).toBe(-1);
    }
    matched++;
  });
  expect(matched).toBe(CASES.length);
  console.log(`# ${matched} banks ${onTarget ? "match the golden's hashes" : `agree with themselves on ${host}`}`);
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '6768,7227p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test-native/kit-baseline.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/kit-baseline.test.ts`
Expected: `e1ae943dfb816ca35d0379d92c7a9e3bad4dfdc1`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/kit-compile.test.ts`:

````ts
// The TypeScript kit compiler (src/kit/) past what the 39-case baseline covers (test-native/kit-baseline holds those),
// on this host's real engine. A slot it refuses (a source with a NaN, a filter at Nyquist, a gain or a Q outside its
// node's range, a pass that overflows a finite source to a value that is not finite, a job a node's guard fails)
// leaves the rest of the kit compiling, says why, and leaves the bank native builds when that slot's file fails to
// load. Every edge native's compileSample and DMC encode have - an empty window, a window cut by offset and length, a
// window that resamples to less than one wave frame or to nothing, a silent source, a source past the frame cap, a bank
// whose space runs out, a DMC slot that does not fit, samples past the slot count, the struct defaults a spec leaves
// out - comes out byte for byte as native compiles it, in full 15- and 16-slot kits; and a slot the bank does not hold
// whole says why.
//
// Each bank is held two ways. Against native's compileKit / compileDmc on this host, while native's compiler exists:
// both share every kernel, so that comparison is exact on every host. And against kit-edges.json (beside
// kit-baseline.json), recorded from this compiler on the target when native and it agreed, under the baseline's
// rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile agreeing with the first;
// RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records it, refused off the
// target or for a bank whose two compiles disagree. The golden is what keeps these edges pinned once native's compiler
// retires. The sources are synthesised, so it needs no fixtures.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver, type KitEffect, type KitSampleSpec, type RisaDmcSampleSpec } from "../src/audioDriver";
import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
import { fnv1a64Hex, floatWav, signalSamples } from "./kit/sources";

declare const __KIT_EDGES_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const UPDATE = tjs.env.RP_KIT_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = tjs.env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";
const HOST = audio.buildTarget();
const ON_TARGET = HOST === BASELINE_TARGET;

interface EdgeGolden {
  target: string;
  cases: Record<string, { hash: string; size: number }>;
}

function readGolden(): EdgeGolden {
  const bytes = be.readFile(__KIT_EDGES_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__KIT_EDGES_GOLDEN__}; record it with RP_KIT_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as EdgeGolden;
}

const golden = UPDATE ? null : readGolden();
const recorded = new Map<string, { hash: string; size: number }>();

// The cases that ran to their end: a recording writes only when every one did, so a failing case cannot leave a
// golden recorded from a run that went wrong (or that skipped the banks after its failure).
let completed = 0;
const EDGE_CASES = 8;
function edgeCase(name: string, body: () => Promise<void>): void {
  test(name, async () => {
    await body();
    completed++;
  });
}

// A bank compiled and held to kit-edges.json under the baseline's rules (its size everywhere, its hash on the target, a
// second compile agreeing elsewhere), or recorded under RP_KIT_GOLDEN_UPDATE=1. Returns the first compile.
async function held(name: string, compile: () => Promise<KitCompileResult>): Promise<KitCompileResult> {
  if (UPDATE && !ON_TARGET)
    throw new Error(`a recording only happens on ${BASELINE_TARGET}, and this host is ${HOST}`);
  if (!UPDATE && REQUIRE_TARGET && !ON_TARGET)
    throw new Error(`this host is ${HOST} but kit-edges.json was recorded on ${BASELINE_TARGET}`);
  expect(recorded.has(name), `${name} is held once`).toBe(false);
  const kit = await compile();
  if (UPDATE || !ON_TARGET) {
    const again = await compile();
    expect(firstDifference(again.bytes, kit.bytes), `${name} compiles twice to one bank`).toBe(-1);
  }
  const entry = { hash: fnv1a64Hex(kit.bytes), size: kit.bytes.length };
  recorded.set(name, entry);
  if (golden) {
    const expected = golden.cases[name];
    expect(expected !== undefined, `${name} is in kit-edges.json`).toBe(true);
    expect(entry.size, `${name}'s size`).toBe(expected.size);
    if (ON_TARGET) expect(entry.hash, `${name}'s bank hash`).toBe(expected.hash);
  }
  return kit;
}

function write(name: string, samples: Float32Array, rate: number): string {
  const path = `${be.configDir()}/${name}.wav`;
  if (!be.writeFile(path, floatWav(samples, rate))) throw new Error(`cannot write ${path}`);
  return path;
}

function sine(hz: number, rate: number, frames: number, level = 0.8): Float32Array {
  return Float32Array.from({ length: frames }, (_, i) => level * Math.sin((2 * Math.PI * hz * i) / rate));
}

const SINE = write("sine", signalSamples("sine440", 44100), 44100);
const CHIRP = write("chirp", signalSamples("chirp", 48000), 48000);
const NOISE = write("noise", signalSamples("noise", 22050), 22050);
const SILENCE = write("silence", new Float32Array(8192), 44100);
const SHORT = write("short", sine(440, 44100, 100), 44100); // 26 frames at the kit rate: under one wave frame
const TINY = write("tiny", sine(440, 48000, 3), 48000); // no frames at all at the slowest DMC rate
// 4.5 seconds: past LSDj's cap (a kit's 32576 frames at 11468 Hz is 125271 frames at 44.1 kHz), so the window caps it.
const LONG = write("long", sine(220, 44100, 200000, 0.6), 44100);
// 8.2 seconds: past every DMC rate's cap too (4081 bytes of bits at 4177.4 Hz is 344659 frames at 44.1 kHz).
const LONGER = write("longer", sine(220, 44100, 360000, 0.6), 44100);
const NAN = write(
  "nan",
  (() => {
    const s = signalSamples("sine440", 44100);
    s[1000] = NaN;
    return s;
  })(),
  44100,
);
const MISSING = `${be.configDir()}/no-such-file.wav`;
// Finite sources the chain overflows: a subnormal peak (whose reciprocal is past binary32's range), samples near the
// top of the range (which a gain or a filter's gain carries past it), and samples a dither's scaled value overflows.
const SUBNORMAL = write("subnormal", sine(440, 44100, 8192, 1e-39), 44100);
const HUGE = write("huge", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 3e38), 44100);
const LOUD = write("loud", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 1e38), 44100);

// Effects with every field written: the native RPC's decode has no defaults, so a field left out refuses the whole
// kit there, where the TypeScript compiler fills the struct's default.
type FilterType = NonNullable<Extract<KitEffect, { type: "filter" }>["filterType"]>;
type DitherType = NonNullable<Extract<KitEffect, { type: "dither" }>["ditherType"]>;
const gain = (g: number, normalize = true): KitEffect => ({ type: "gain", normalize, gain: g });
const filter = (filterType: FilterType, frequency: number, q: number, gainDb = 0): KitEffect => ({
  type: "filter",
  filterType,
  frequency,
  q,
  gain: gainDb,
});
const dither = (ditherType: DitherType): KitEffect => ({ type: "dither", ditherType });

function lsdj(path: string, name: string, effects: KitEffect[] = [],
              window: Partial<KitSampleSpec> = {}): KitSampleSpec {
  return { path, name, effects, ...window };
}

function firstDifference(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}

const NONFINITE = `${NAN} has a sample that is not finite in the window`;
const NYQUIST =
  "effect 0 (filter): frequency 22050 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)";
const GAIN = "effect 0 (gain): gain 150 is outside rp.dsp.gain's range -100 .. 100";
const Q = "effect 1 (filter): q 2000 is outside rp.dsp.biquad's range 0.001 .. 1000";
const UNREADABLE = `${MISSING} could not be read or decoded`;

edgeCase("a refused LSDj slot leaves the rest compiling, says why, and is native's for a file that fails", async () => {
  const refused = [
    lsdj(NAN, "NAN"),
    lsdj(SINE, "NYQ", [filter("HighPass", 22050, 1)]),
    lsdj(NOISE, "GAN", [gain(150, false)]),
    lsdj(NOISE, "Q", [gain(0.5), filter("Peak", 1000, 2000)]),
  ];
  const samples = [
    lsdj(SINE, "A", [filter("LowPass", 2000, 0.9)]),
    refused[0],
    lsdj(CHIRP, "C", [gain(0.5), dither("JJN")]),
    refused[1],
    refused[2],
    lsdj(NOISE, "N", [filter("LowShelf", 800, 0.7, -6)]),
    refused[3],
  ];
  const kit = await held("lsdj/refused", () => compileKit(be, { name: "REFUSE", samples }));
  expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST, GAIN, null, Q]);

  // Each refused slot is empty: no bytes, its name kept, as native's assembler leaves a slot whose file failed to load.
  // So the bank is native's for the same kit with those slots' files missing, and the TypeScript compile of that kit.
  const missing = samples.map((s) => (refused.includes(s) ? { ...s, path: MISSING } : s));
  const native = audio.compileKit({ name: "REFUSE", samples: missing });
  expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
  const again = await compileKit(be, { name: "REFUSE", samples: missing });
  expect(again.errors).toEqual([null, UNREADABLE, null, UNREADABLE, UNREADABLE, null, UNREADABLE]);
  expect(firstDifference(again.bytes, kit.bytes)).toBe(-1);
  // Not the kit without those slots, though: an LSDj slot with no bytes is named, with a zero length, where a slot the
  // spec does not have is the empty-slot marker. So a refused last slot differs from the kit one shorter in exactly
  // that entry and that name.
  const shorter = await compileKit(be, { name: "REFUSE", samples: samples.slice(0, 6) });
  const entry = (bank: Uint8Array, i: number): number => bank[i * 2] | (bank[i * 2 + 1] << 8);
  expect([entry(kit.bytes, 7), entry(shorter.bytes, 7)]).toEqual([entry(kit.bytes, 6), 0]);
  expect([kit.bytes[0x22 + 6 * 3], shorter.bytes[0x22 + 6 * 3]]).toEqual(["Q".charCodeAt(0), 0]);
});

edgeCase("a refused risa slot leaves the rest compiling, its entry empty: a missing file's, or no slot's", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: CHIRP, name: "A", effects: [filter("LowPass", 3000, 1)], rate: 8 },
    { path: NAN, name: "NAN", effects: [], rate: 12 },
    { path: NOISE, name: "N", effects: [gain(0.4, false)], rate: 15, loop: true },
    { path: SINE, name: "NYQ", effects: [filter("HighPass", 22050, 1)], rate: 4 },
  ];
  const kit = await held("risa/refused", () => compileDmc(be, { name: "REFUSE", samples }));
  expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST]);
  expect([kit.bytes[0x1f04], kit.bytes[0x1f0c]]).toEqual([0xff, 0xff]);
  const missing = samples.map((s, i) => (i === 1 || i === 3 ? { ...s, path: MISSING } : s));
  const native = audio.compileDmc({ name: "REFUSE", samples: missing });
  expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
  // risa's assembler leaves an empty slot where it is and moves no cursor, so a refused LAST slot is the kit without
  // it.
  const shorter = await compileDmc(be, { name: "REFUSE", samples: samples.slice(0, 3) });
  expect(firstDifference(kit.bytes, shorter.bytes), "the kit without its refused last slot").toBe(-1);
});

edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for byte", async () => {
  const dithers = ["HighPassTPDF", "ShapedTPDF", "ErrorDiffusion", "JJN", "SierraLite"] as const;
  const samples = [
    lsdj(SINE, "EMP", [gain(0.5)], { offset: 9000 }), // an offset past the end: an empty window
    lsdj(SINE, "WIN", [dither("JJN")], { offset: 1000, length: 3000 }),
    lsdj(SHORT, "SHO", [filter("LowPass", 1000, 1)]), // resamples to 26 frames: trimmed to nothing
    lsdj(SILENCE, "SIL", [gain(0.37)]), // normalising silence: no peak to divide by
    lsdj(SILENCE, "SI2", [dither("ShapedTPDF")]),
    ...dithers.map((d, i) =>
      lsdj(i % 2 ? CHIRP : NOISE, `D${i}`, [filter("BandPass", 1500, 2), dither(d)]),
    ),
    lsdj(NOISE, "CH", [
      gain(0.25),
      filter("HighShelf", 3000, 0.7, 30),
      gain(-1, false),
      gain(0.9),
      filter("HighPass", 10, 20),
    ]),
    lsdj(SINE, "LEN", [], { length: 4000 }),
    lsdj(CHIRP, "AP", [filter("AllPass", 1500, 1.2)]),
    lsdj(NOISE, "NS"),
    lsdj(LONG, "LNG", [gain(1)]), // past the cap, and after the bank's space has run out: no room at all
  ];
  expect(samples.length).toBe(15);
  for (const rotate of [true, false]) {
    const spec = { name: "edges", samples, rotate };
    const kit = await held(`lsdj/edges-rotate-${rotate ? "on" : "off"}`, () => compileKit(be, spec));
    expect(firstDifference(kit.bytes, audio.compileKit(spec)), `rotate ${rotate}`).toBe(-1);
    // Every slot the bank does not hold whole says why: the empty window, the window under one wave frame, the slot
    // the bank's space ran out on part way (the noise, 2128 bytes, before the capped source), and the capped source,
    // which finds none left.
    const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
    expect(kit.errors).toEqual(
      samples.map((s, i) =>
        s.name === "EMP"
          ? "the window holds no frames (offset 9000, at or past the source's 8192 frames)"
          : s.name === "SHO"
            ? "the window resamples to 26 frames at 11468 Hz, under one 32-sample wave frame"
            : s.name === "NS"
              ? `the kit's sample space ran out, so ${entry(i + 1) - entry(i)} of this sample's 2128 bytes fit`
              : s.name === "LNG"
                ? "the kit's 16288 bytes of sample space are full, so none of this sample's 16288 bytes fit"
                : null,
      ),
    );
  }
  // A source past the cap fills the whole sample space on its own, and a slot after it finds none: the empty-slot
  // marker, which native writes for a slot with no room as for one the spec does not have.
  const full = { name: "FULL", samples: [lsdj(LONG, "LNG"), lsdj(SINE, "AFT")] };
  const kit = await held("lsdj/full", () => compileKit(be, full));
  expect(firstDifference(kit.bytes, audio.compileKit(full))).toBe(-1);
  const noRoom = "the kit's 16288 bytes of sample space are full, so none of this sample's 1056 bytes fit";
  expect(kit.errors).toEqual([null, noRoom]);
  const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
  expect([entry(1), entry(2), kit.bytes[0x25]]).toEqual([0x4060 + 0x3fa0, 0, 0]);
});

edgeCase("a full risa kit of every DMC encode edge is native's bank byte for byte", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: TINY, name: "TNY", effects: [], rate: 0 }, // resamples to no frames: the one-byte {0}
    { path: SINE, name: "EMP", effects: [], offset: 9000 }, // an empty window: {0} too
    { path: LONGER, name: "L0", effects: [], rate: 0 }, // past the cap at the slowest rate: 4081 bytes
    { path: LONGER, name: "L15", effects: [gain(0.5)], rate: 15 }, // no room after L0: left empty
    { path: SILENCE, name: "SIL", effects: [], rate: 12 },
    { path: SILENCE, name: "SIN", effects: [], rate: 12, normalize: false },
    { path: NOISE, name: "N", effects: [dither("JJN")], rate: 3, normalize: false },
    { path: CHIRP, name: "C", effects: [filter("Peak", 1000, 1, 6)], rate: 7, loop: true },
    { path: SINE, name: "W", effects: [], rate: 9, offset: 500, length: 2000 },
    { path: SHORT, name: "S", effects: [], rate: 1 },
    { path: NOISE, name: "N2", effects: [gain(-0.7, false)], rate: 2 },
    { path: CHIRP, name: "C2", effects: [], rate: 21 }, // an index past 15 is masked to its low four bits
    { path: SINE, name: "S2", effects: [gain(2)], rate: 5 },
    { path: NOISE, name: "N3", effects: [], rate: 6 },
    { path: CHIRP, name: "C3", effects: [], rate: 10 },
    { path: SINE, name: "S3", effects: [], rate: 11 },
  ];
  const kit = await held("risa/edges", () => compileDmc(be, { name: "edges-kit", samples }));
  // A {0} slot is a placed sample; L15 found no room after L0, and says so.
  const noFit = "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it";
  expect(kit.errors).toEqual(samples.map((s) => (s.name === "L15" ? noFit : null)));
  expect(firstDifference(kit.bytes, audio.compileDmc({ name: "edges-kit", samples }))).toBe(-1);
  // The {0} slots are one byte at their own 64-byte boundary; L0 is the longest legal stream; L15 found no room.
  expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f04], kit.bytes[0x1f05]]).toEqual([0, 0, 1, 0]);
  expect([kit.bytes[0x1f08], kit.bytes[0x1f09], kit.bytes[0x1f0c]]).toEqual([2, 255, 0xff]);
});

edgeCase("a finite source the chain overflows refuses its slot with the reason, the rest compiling", async () => {
  // Each route reaches a value that is not finite from a source that is: a normalise over a subnormal peak and a plain
  // gain over samples near the float range's top make one the pass records; a +96 dB peak over those samples overflows
  // the filter, whose guard resets it and fails the job; a dither (JJN) over loud samples overflows its scaled value,
  // whose guard restarts it and fails the job. Native compiles all four to whatever its kernels latch; the TypeScript
  // compiler refuses them, so no kit it compiles carries a guard's output or a non-finite intermediate.
  const samples = [
    lsdj(SUBNORMAL, "SUB", [gain(0.9)]),
    lsdj(SINE, "A", [filter("LowPass", 2000, 0.9)]),
    lsdj(HUGE, "GAN", [gain(50, false)]),
    lsdj(HUGE, "BIQ", [filter("Peak", 1000, 1, 96)]),
    lsdj(LOUD, "DIT", [dither("JJN")]),
  ];
  const kit = await held("lsdj/overflow", () => compileKit(be, { name: "OVER", samples }));
  expect(kit.errors[0]).toBe("the pass running effect 0 made a sample that is not finite");
  expect(kit.errors[1]).toBe(null);
  expect(kit.errors[2]).toBe("the pass running effect 0 made a sample that is not finite");
  // A guard's count depends on how many blocks it fired in, so the reason is held up to the count.
  const guard = (e: string | null, words: string): boolean => e !== null && e.startsWith(words) && e.endsWith(")");
  const filterWords = "the pass running effect 0 overflowed a filter, which rp.dsp.biquad reset (";
  expect(guard(kit.errors[3], filterWords), String(kit.errors[3])).toBe(true);
  const ditherWords = "the quantise overflowed the dither's state, which rp.dsp.dither restarted (";
  expect(guard(kit.errors[4], ditherWords), String(kit.errors[4])).toBe(true);
  // The refused slots are the slots native leaves when their files fail to load.
  const missing = samples.map((s) => (s.name === "A" ? s : { ...s, path: MISSING }));
  expect(firstDifference(kit.bytes, audio.compileKit({ name: "OVER", samples: missing }))).toBe(-1);
});

edgeCase("samples past the slot count are dropped as native drops them, each saying so", async () => {
  const sixteen = Array.from({ length: 16 }, (_, i) => lsdj(i % 2 ? SINE : SHORT, `S${i}`, i % 3 ? [] : [gain(0.5)]));
  const kit = await held("lsdj/sixteen", () => compileKit(be, { name: "SIXTEEN", samples: sixteen }));
  expect(firstDifference(kit.bytes, audio.compileKit({ name: "SIXTEEN", samples: sixteen }))).toBe(-1);
  expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
  const seventeen: RisaDmcSampleSpec[] = Array.from({ length: 17 }, (_, i) => ({
    path: i % 2 ? SINE : SHORT,
    name: `S${i}`,
    effects: [],
    rate: i % 16,
  }));
  const dmc = await held("risa/seventeen", () => compileDmc(be, { name: "SEVENTEEN", samples: seventeen }));
  expect(firstDifference(dmc.bytes, audio.compileDmc({ name: "SEVENTEEN", samples: seventeen }))).toBe(-1);
  expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
});

edgeCase("a refusal beats an empty window: a refused risa slot is empty, where native encodes {0}", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: SINE, name: "A", effects: [], rate: 12 },
    { path: SINE, name: "REF", effects: [gain(150, false)], rate: 12, offset: 9000 },
  ];
  const kit = await held("risa/refusal-over-empty-window", () => compileDmc(be, { name: "R", samples }));
  expect(kit.errors).toEqual([null, GAIN]);
  expect(kit.bytes[0x1f04]).toBe(0xff);
  // Native never refuses: the same spec compiles its empty window to the one-byte {0}, a placed slot. The refused slot
  // is the slot native leaves for a file that fails to load.
  const native = audio.compileDmc({ name: "R", samples });
  expect(native[0x1f04] !== 0xff).toBe(true);
  const missing = audio.compileDmc({ name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
  expect(firstDifference(kit.bytes, missing)).toBe(-1);
});

edgeCase("a field the spec leaves out takes the native struct's default, the spelled-out spec's bank", async () => {
  // Native's RPC refuses a spec with a field left out, so it is held to the spelled-out defaults: LowPass at 5734 Hz,
  // Q 1 and 0 dB; a normalising gain of 1; HighPassTPDF.
  const sparse: KitEffect[] = [{ type: "filter" }, { type: "gain" }, { type: "dither" }];
  const spelled: KitEffect[] = [filter("LowPass", 5734, 1, 0), gain(1, true), dither("HighPassTPDF")];
  const kit = await held("lsdj/defaults", () => compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", sparse)] }));
  expect(kit.errors).toEqual([null]);
  const native = audio.compileKit({ name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
  expect(firstDifference(kit.bytes, native)).toBe(-1);
  const dmcSparse = [{ path: CHIRP, name: "SP", effects: sparse.slice(0, 2) }];
  const dmc = await held("risa/defaults", () => compileDmc(be, { name: "DEF", samples: dmcSparse }));
  expect(dmc.errors).toEqual([null]);
  const dmcNative = audio.compileDmc({
    name: "DEF",
    samples: [{ path: CHIRP, name: "SP", effects: spelled.slice(0, 2), rate: 12, loop: false, normalize: true }],
  });
  expect(firstDifference(dmc.bytes, dmcNative)).toBe(-1);
});

test(UPDATE ? "write kit-edges.json" : "kit-edges.json holds exactly the banks held here", () => {
  if (UPDATE) {
    expect(completed, "every case ran to its end, so the recording is whole").toBe(EDGE_CASES);
    const names = [...recorded.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
    const rows = names.map((n) => {
      const b = recorded.get(n)!;
      return `    "${n}": { "hash": "${b.hash}", "size": ${b.size} }`;
    });
    const text = `{\n  "target": "${BASELINE_TARGET}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
    const written = be.writeFileAtomic(__KIT_EDGES_GOLDEN__, new TextEncoder().encode(text));
    expect(written, `write ${__KIT_EDGES_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${names.length} banks to ${__KIT_EDGES_GOLDEN__}`);
    return;
  }
  expect(golden!.target).toBe(BASELINE_TARGET);
  expect([...recorded.keys()].sort()).toEqual(Object.keys(golden!.cases).sort());
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '7239,7628p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test-native/kit-compile.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/kit-compile.test.ts`
Expected: `b4abd3800531c3bc156563ae83814ae453f068be`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/kit/kit.test.ts`:

````ts
// The TypeScript kit compiler's pure parts (src/kit/, docs/modular-graph-phase-d2-design.md section 5), with no native
// host: the 4-bit level-to-code inversion over every level the dither node produces, the trim, the nibble pack with and
// without rotate against a transcription of native convertCodesToNibbles, the DMC bit pack, pad and truncation, the
// spec's refusals and their messages, the segment split at normalising gains and the documents each pass runs, the
// range constants held to the two descriptor goldens, the bank assemblers' names byte for byte as native writes them,
// and a slot's chain of jobs over a scripted runner (what it submits, where it stops short, what it does with a
// failure). The banks themselves are held by test-native/kit-baseline (all 39 golden cases) and
// test-native/kit-compile.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import {
  LSDJ_MAX_SAMPLE_FRAMES,
  PAL_DMC_RATES,
  levelToCode,
  packDmc,
  packNibbles,
  palDmcRate,
  resampledLength,
  trimToFrames,
} from "../../src/kit/pack";
import {
  BIQUAD_FREQUENCY_RANGE,
  BIQUAD_GAIN_DB_RANGE,
  BIQUAD_Q_RANGE,
  GAIN_RANGE,
  dpcmDocument,
  effectsDocument,
  quantiseDocument,
  resampleDocument,
  resolveEffects,
  type ParamRange,
} from "../../src/kit/chain";
import { compileDmc, compileKit, type KitJobRunner } from "../../src/kit";
import { buildKitBank } from "../../src/lsdj/rom/buildKit";
import { assembleKitBank } from "../../src/risa/rom/kit";
import type { KitEffect } from "../../src/audioDriver";
import { OfflineRunError, type OfflineRunRequest, type OfflineRunResult } from "../../src/offline";
import gainDescriptor from "../../../native/test/graph/golden/rp.dsp.gain.descriptor.json";
import biquadDescriptor from "../../../native/test/graph/golden/rp.dsp.biquad.descriptor.json";

const F = Math.fround;

// rp::dsp::codeToLevel at 4 bits, in binary32 as the node computes it: (float(code) / 15.0f) * 2.0f - 1.0f. A float
// quotient rounded once from the double quotient is the binary32 quotient, and so for the multiply and the subtract.
const levelOf = (code: number): number => F(F(F(code / 15) * 2) - 1);

// Native SampleUtil::convertCodesToNibbles, transcribed.
function nativeNibbles(codes: number[], rotate: boolean): number[] {
  const out: number[] = [];
  for (let chunk = 0; chunk + 32 <= codes.length; chunk += 32) {
    const s = new Array<number>(32);
    if (rotate) {
      s[0] = 0xf - codes[chunk + 31];
      for (let i = 1; i < 32; i++) s[i] = 0xf - codes[chunk + i - 1];
    } else {
      for (let i = 0; i < 32; i++) s[i] = 0xf - codes[chunk + i];
    }
    for (let i = 0; i < 16; i++) out.push((s[i * 2] << 4) | s[i * 2 + 1]);
  }
  return out;
}

function codesOf(n: number, seed: number): number[] {
  let x = seed;
  return Array.from({ length: n }, () => {
    x = (Math.imul(x, 1103515245) + 12345) >>> 0;
    return (x >>> 16) & 0xf;
  });
}

test("every 4-bit level the dither node outputs inverts to its own code, with room to spare", () => {
  for (let code = 0; code < 16; code++) {
    const level = levelOf(code);
    expect(levelToCode(level), `code ${code}`).toBe(code);
    // The inversion's error is float rounding, about 1e-6 of a step: nowhere near the half step a neighbour needs.
    expect(Math.abs((level + 1) * 0.5 * 15 - code), `code ${code}'s error`).toBeLessThan(1e-5);
  }
  expect(levelOf(0)).toBe(-1);
  expect(levelOf(15)).toBe(1);
});

test("the trim keeps whole 32-sample frames, and the nibble pack is native's with and without rotate", () => {
  expect([0, 31, 32, 33, 63, 64, 1000].map(trimToFrames)).toEqual([0, 0, 32, 32, 32, 64, 992]);
  for (const n of [0, 5, 31, 32, 33, 64, 96 + 17, 1056 * 2]) {
    const codes = codesOf(n, n + 1);
    const levels = codes.map(levelOf);
    for (const rotate of [true, false]) {
      const packed = packNibbles(levels, rotate);
      expect(packed.length, `${n} codes`).toBe(trimToFrames(n) / 2);
      expect([...packed], `${n} codes, rotate ${rotate}`).toEqual(nativeNibbles(codes, rotate));
    }
  }
  // Rotate: a frame's last sample opens it, inverted (stored = 15 - code).
  const ramp = Array.from({ length: 32 }, (_, i) => i % 16);
  expect(packNibbles(ramp.map(levelOf), true)[0]).toBe(((0xf - 15) << 4) | (0xf - 0));
  expect(packNibbles(ramp.map(levelOf), false)[0]).toBe(((0xf - 0) << 4) | (0xf - 1));
});

test("the resampled length is the kit path's floor(frames * (out / in)), and the DMC rates are native's table", () => {
  expect(resampledLength(8192, 44100, 11468)).toBe(Math.floor(8192 * (11468 / 44100)));
  expect(resampledLength(600, 44100, 11468)).toBe(156);
  expect(resampledLength(0, 22050, 11468)).toBe(0);
  expect(resampledLength(3, 48000, 4177.4)).toBe(0);
  expect(PAL_DMC_RATES.length).toBe(16);
  expect(palDmcRate(12)).toBe(16965.4); // the compilers' default index
  expect(palDmcRate(17)).toBe(4696.63); // native masks the index to its low four bits
  expect(LSDJ_MAX_SAMPLE_FRAMES).toBe(32576);
});

test("the DMC pack is LSB first, padded to 16k + 1 bytes and cut at 4081, as RisaDmcCodec packs it", () => {
  const bitsOf = (bits: number[]): number[] => bits.map((b) => (b ? 1 : -1));
  expect([...packDmc([])]).toEqual([0]); // the encode of nothing: the one-byte legal stream
  expect([...packDmc(bitsOf([1, 0, 0, 0, 0, 0, 0, 0]))]).toEqual([0x01]);
  expect([...packDmc(bitsOf([0, 1, 1, 0, 0, 0, 0, 1]))]).toEqual([0x86]);
  const nine = packDmc(bitsOf([1, 1, 1, 1, 1, 1, 1, 1, 1]));
  expect(nine.length).toBe(17); // two bytes padded to 16 * 1 + 1
  expect([...nine.subarray(0, 2)]).toEqual([0xff, 0x01]);
  expect(nine.subarray(2).every((b) => b === 0)).toBe(true);
  expect(packDmc(new Array(17 * 8).fill(1)).length).toBe(17); // already legal: no pad
  expect(packDmc(new Array(18 * 8).fill(1)).length).toBe(33);
  // Past the hardware's 4081 bytes the stream is cut there, still a legal length, whether it was padded or not.
  expect(packDmc(new Array(4082 * 8).fill(1)).length).toBe(4081);
  const cut = packDmc(new Array(4081 * 8 + 3).fill(1));
  expect(cut.length).toBe(4081);
  expect(cut[4080]).toBe(0xff);
  // A level is a bit by its sign: the node writes exactly +1 or -1.
  expect([...packDmc([1, -1, 1, -1, 1, -1, 1, -1])]).toEqual([0x55]);
});

test("the range constants are the two nodes' descriptors", () => {
  const param = (d: { params: { id: string; min: number; max: number }[] }, id: string): ParamRange => {
    const p = d.params.find((x) => x.id === id)!;
    return { min: p.min, max: p.max };
  };
  expect(GAIN_RANGE).toEqual(param(gainDescriptor, "gain"));
  expect(BIQUAD_FREQUENCY_RANGE).toEqual(param(biquadDescriptor, "frequency"));
  expect(BIQUAD_Q_RANGE).toEqual(param(biquadDescriptor, "q"));
  expect(BIQUAD_GAIN_DB_RANGE).toEqual(param(biquadDescriptor, "gainDb"));
});

function refusal(fn: () => unknown): string {
  try {
    fn();
  } catch (e) {
    return (e as Error).message;
  }
  throw new Error("expected a refusal");
}

test("a spec the nodes cannot carry is refused with the reason, checked on its binary32 value", () => {
  const at = (effects: KitEffect[], rate = 44100) => () => resolveEffects(effects, rate, true);
  expect(refusal(at([{ type: "gain", normalize: false, gain: 150 }]))).toBe(
    "effect 0 (gain): gain 150 is outside rp.dsp.gain's range -100 .. 100",
  );
  expect(refusal(at([{ type: "gain", normalize: false, gain: NaN }]))).toBe(
    "effect 0 (gain): gain NaN is outside rp.dsp.gain's range -100 .. 100",
  );
  expect(refusal(at([{ type: "filter", filterType: "LowPass", frequency: 22050, q: 1 }]))).toBe(
    "effect 0 (filter): frequency 22050 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)",
  );
  expect(refusal(at([{ type: "filter", frequency: 6000 }], 11025))).toBe(
    "effect 0 (filter): frequency 6000 Hz is at or above the source's Nyquist frequency (5512.5 Hz at 11025 Hz)",
  );
  // The default frequency (5734 Hz, the kit rate's Nyquist) is above an 11025 Hz source's.
  expect(refusal(at([{ type: "filter" }], 11025))).toBe(
    "effect 0 (filter): frequency 5734 Hz is at or above the source's Nyquist frequency (5512.5 Hz at 11025 Hz)",
  );
  expect(refusal(at([{ type: "gain", normalize: true }, { type: "filter", filterType: "Peak", q: 2000 }]))).toBe(
    "effect 1 (filter): q 2000 is outside rp.dsp.biquad's range 0.001 .. 1000",
  );
  expect(refusal(at([{ type: "filter", q: 0.0005 }]))).toBe(
    "effect 0 (filter): q 0.0005 is outside rp.dsp.biquad's range 0.001 .. 1000",
  );
  expect(refusal(at([{ type: "filter", filterType: "HighShelf", gain: -97 }]))).toBe(
    "effect 0 (filter): gain -97 is outside rp.dsp.biquad's range -96 .. 96",
  );
  expect(refusal(at([{ type: "filter", frequency: 0.5 }]))).toBe(
    "effect 0 (filter): frequency 0.5 is outside rp.dsp.biquad's range 1 .. 96000",
  );
  // A normalising gain is a setting, refused only when no document can carry it, and the reason says which way.
  expect(refusal(at([{ type: "gain", normalize: true, gain: Infinity }]))).toBe(
    "effect 0 (gain): a normalising gain of Infinity is not finite",
  );
  expect(refusal(at([{ type: "gain", normalize: true, gain: NaN }]))).toBe(
    "effect 0 (gain): a normalising gain of NaN is not a number",
  );
  expect(refusal(at([{ type: "gain", normalize: true, gain: 1e39 }]))).toBe(
    "effect 0 (gain): a normalising gain of 1e+39 is past binary32's largest value (about 3.4e38)",
  );
  expect(refusal(at([{ type: "reverb" } as unknown as KitEffect]))).toBe('effect 0: unknown effect type "reverb"');
  expect(refusal(at([{ type: "filter", filterType: "Comb" } as unknown as KitEffect]))).toBe(
    'effect 0 (filter): unknown filter type "Comb"',
  );
  expect(refusal(at([{ type: "dither", ditherType: "Floyd" } as unknown as KitEffect]))).toBe(
    'effect 0 (dither): unknown dither type "Floyd"',
  );
  const stringGain = { type: "gain", gain: "1" } as unknown as KitEffect;
  expect(refusal(at([stringGain]))).toBe("effect 0 (gain): gain is not a number");
  // Inside the ranges, the bounds included; just under Nyquist; a normalising gain far past the plain gain's range.
  const bounds = resolveEffects(
    [
      { type: "gain", normalize: false, gain: -100 },
      { type: "gain", normalize: false, gain: 100 },
      { type: "filter", frequency: 22049, q: 1000, gain: 96 },
      { type: "filter", frequency: 1, q: 0.001, gain: -96 },
    ],
    44100,
    true,
  );
  expect(bounds.segments[0].nodes.length).toBe(4);
  expect(resolveEffects([{ type: "gain", normalize: true, gain: 150 }], 44100, true).segments[0].normalize).toBe(150);
  // risa skips a dither unread, as RisaDmcCodec does.
  const unknownDither = { type: "dither", ditherType: "Floyd" } as unknown as KitEffect;
  expect(resolveEffects([unknownDither], 44100, false).dither).toBe(null);
});

test("the effect list is cut at each normalising gain, the last dither winning, and values go in as binary32", () => {
  const chain = resolveEffects(
    [
      { type: "filter", filterType: "HighPass", frequency: 10, q: 20 },
      { type: "dither", ditherType: "JJN" },
      { type: "gain", gain: 0.37 }, // normalise defaults on, as the native struct's
      { type: "gain", normalize: false, gain: -1 },
      { type: "gain", normalize: true, gain: 0.5 },
      { type: "filter", filterType: "Peak", frequency: 1000, q: 0.7, gain: 6 },
      { type: "dither" }, // HighPassTPDF by default, and the last one wins
    ],
    44100,
    true,
  );
  expect(chain.dither).toBe("high-pass-tpdf");
  expect(chain.segments).toEqual([
    { normalize: null, nodes: [{ kind: "biquad", type: "high-pass", frequency: 10, q: 20, gainDb: 0 }], effects: [0] },
    { normalize: F(0.37), nodes: [{ kind: "gain", gain: -1 }], effects: [2, 3] },
    {
      normalize: 0.5,
      nodes: [{ kind: "biquad", type: "peak", frequency: 1000, q: F(0.7), gainDb: 6 }],
      effects: [4, 5],
    },
  ]);
  // A list with nothing to stream has no pass at all: no job.
  expect(resolveEffects([], 44100, true)).toEqual({ segments: [], dither: null });
  expect(resolveEffects([{ type: "dither", ditherType: "SierraLite" }], 44100, true)).toEqual({
    segments: [],
    dither: "sierra-lite",
  });
  // A normalising gain alone is a pass of its own: the normalise is the work.
  expect(resolveEffects([{ type: "gain", gain: 2 }], 44100, true).segments).toEqual([
    { normalize: 2, nodes: [], effects: [0] },
  ]);
});

type Doc = {
  nodes: {
    id: string;
    type: string;
    settings?: Record<string, unknown>;
    params?: Record<string, number>;
    resources?: Record<string, string>;
  }[];
  wires: { from: string; to: string }[];
};

test("each pass's document: its buffer in at res:1, a mono chain of nodes, recorded to res:2", () => {
  const plain = JSON.parse(
    effectsDocument({
      normalize: null,
      nodes: [
        { kind: "gain", gain: F(0.37) },
        { kind: "biquad", type: "low-pass", frequency: 2000, q: F(0.9), gainDb: 0 },
      ],
      effects: [0, 1],
    }),
  ) as Doc;
  expect(plain.nodes.map((n) => n.type)).toEqual([
    "rp.core.buffer-player",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.core.buffer-recorder",
  ]);
  expect(plain.nodes[0].resources).toEqual({ sample: "res:1" });
  expect(plain.nodes[0].settings).toEqual({ channels: 1, layout: "mono", loop: false });
  expect(plain.nodes[1].params).toEqual({ gain: F(0.37) });
  expect(plain.nodes[2].settings).toEqual({ channels: 1, type: "low-pass" });
  expect(plain.nodes[2].params).toEqual({ frequency: 2000, q: F(0.9), gainDb: 0 });
  expect(plain.nodes[3]).toEqual({
    id: "rec",
    type: "rp.core.buffer-recorder",
    settings: { channels: 1 },
    resources: { target: "res:2" },
  });
  expect(plain.wires).toEqual([
    { from: "src.out", to: "n0.in" },
    { from: "n0.out", to: "n1.in" },
    { from: "n1.out", to: "rec.in" },
  ]);

  const normalised = JSON.parse(effectsDocument({ normalize: F(0.37), nodes: [], effects: [0] })) as Doc;
  expect(normalised.nodes[0]).toEqual({
    id: "src",
    type: "rp.dsp.normalize",
    settings: { buffer: "res:1", gain: F(0.37), channels: 1 },
  });
  expect(normalised.wires).toEqual([{ from: "src.out", to: "rec.in" }]);

  const resample = JSON.parse(resampleDocument()) as Doc;
  expect(resample.nodes[0].settings).toEqual({ buffer: "res:1", channels: 1, layout: "mono", loop: false });

  const quantise = JSON.parse(quantiseDocument("jjn", 4)) as Doc;
  const quantiseTypes = quantise.nodes.map((n) => n.type);
  expect(quantiseTypes).toEqual(["rp.core.buffer-player", "rp.dsp.dither", "rp.core.buffer-recorder"]);
  // The seed is the hash of the very buffer the dither plays, whole: res:1 named twice.
  expect(quantise.nodes[1].settings).toEqual({ channels: 1, algorithm: "jjn", bits: 4, seedFrom: "res:1" });

  const dpcm = JSON.parse(dpcmDocument(false)) as Doc;
  expect(dpcm.nodes[0]).toEqual({ id: "src", type: "rp.dsp.dpcm", settings: { buffer: "res:1", normalize: false } });
});

test("the LSDj assembler writes names as native's does: the UTF-8 bytes, a-z uppercased, nothing else changed", () => {
  const name = (bank: Uint8Array, off: number, n: number): number[] => [...bank.subarray(off, off + n)];
  const bank = buildKitBank("baseline-kit", [
    { name: "kik", bytes: new Uint8Array(16) },
    { name: "é", bytes: new Uint8Array(0) },
  ]);
  expect(name(bank, 0x52, 6)).toEqual([..."BASELI"].map((c) => c.charCodeAt(0)));
  expect(name(bank, 0x22, 3)).toEqual([..."KIK"].map((c) => c.charCodeAt(0)));
  // A non-ASCII name is its bytes (0xC3 0xA9), as a native string of it holds, then the fill; native's toupper leaves
  // bytes past ASCII alone. The slot is present with no bytes: a named, zero-length entry, not the empty-slot marker.
  expect(name(bank, 0x25, 3)).toEqual([0xc3, 0xa9, 0x2d]);
  expect(bank[2] | (bank[3] << 8)).toBe(0x4060 + 16);
  expect(bank[4] | (bank[5] << 8)).toBe(0x4060 + 16);
  expect(bank[6] | (bank[7] << 8)).toBe(0); // slot 2: the empty-slot marker
  // A short kit name is space-padded.
  expect(name(buildKitBank("ab", []), 0x52, 6)).toEqual([0x41, 0x42, 0x20, 0x20, 0x20, 0x20]);
});

test("the risa assembler filters names byte by byte as native does", () => {
  const bank = assembleKitBank("straße", [{ dpcm: new Uint8Array(17), rate: 12, loop: false, name: "ßk" }]);
  // Native keeps A-Z, 0-9 and '-' of the name's uppercased bytes, so the two bytes of the eszett are dropped whole
  // (JavaScript's toUpperCase would have made it "SS").
  expect([...bank.subarray(0x1ec0, 0x1ec0 + 7)]).toEqual([..."STRAE"].map((c) => c.charCodeAt(0)).concat([0, 0]));
  // A sample name is its first three bytes, a-z uppercased, others as they are.
  expect([...bank.subarray(0x1ed0, 0x1ed3)]).toEqual([0xc3, 0x9f, 0x4b]);
  const short = assembleKitBank("drums", [{ dpcm: new Uint8Array(17), rate: 12, loop: false, name: "sn" }]);
  expect([...short.subarray(0x1ed0, 0x1ed3)]).toEqual([0x53, 0x4e, 0x20]);
});

// ---- a slot's chain of jobs, over a scripted runner ---------------------------------------------------------------

interface Script {
  // What a load job's file decodes to, by path: its samples, rate and whether the window holds a non-finite sample,
  // or a string to fail with.
  files: Record<string, { samples: Float32Array; rate: number; nonFinite?: boolean } | string>;
}

// A runner that records each request and answers it: a load with the scripted window (the whole file: these tests
// pass no offset or length), a pass by echoing its input resized to the output's frames (an effects pass keeps the
// samples; a quantise pass hands back levels, which the echo keeps as they are, so its input is levels already).
function scriptedRunner(script: Script): KitJobRunner & { requests: OfflineRunRequest[] } {
  const requests: OfflineRunRequest[] = [];
  return {
    requests,
    async offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult> {
      requests.push(request);
      const input = request.inputs![0];
      if ("path" in input) {
        const file = script.files[input.path];
        if (typeof file === "string") throw new OfflineRunError(file, []);
        return {
          buffers: [
            {
              handle: 1,
              channels: 1,
              frames: file.samples.length,
              rate: file.rate,
              samples: file.samples,
              nonFinite: false,
            },
          ],
          inputs: [
            {
              handle: 1,
              decodedRate: file.rate,
              decodedFrames: file.samples.length,
              frames: file.samples.length,
              nonFinite: file.nonFinite ?? false,
            },
          ],
          guardsFired: 0,
          guardReason: "",
        };
      }
      const out = request.outputs![0];
      const samples = new Float32Array(out.frames);
      samples.set(input.samples.subarray(0, out.frames));
      const nonFinite = samples.some((v) => !Number.isFinite(v));
      const buffer = { handle: 2, channels: 1, frames: out.frames, rate: out.rate, samples, nonFinite };
      return { buffers: [buffer], inputs: [], guardsFired: 0, guardReason: "" };
    },
  };
}

// A request's node types, short ("load" for a job with no document).
const typesOf = (r: OfflineRunRequest): string[] =>
  r.document === undefined
    ? ["load"]
    : (JSON.parse(r.document) as Doc).nodes.map((n) => n.type.replace(/^rp\.(core|dsp)\./, ""));

test("an LSDj slot is a load, its effects passes, the resample cut at its length, the quantise, the pack", async () => {
  const levels = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({ files: { "/a.wav": { samples: levels, rate: 44100 } } });
  const effects: KitEffect[] = [
    { type: "filter", frequency: 3000 },
    { type: "gain", gain: 0.5 },
    { type: "dither", ditherType: "ShapedTPDF" },
  ];
  const kit = await compileKit(runner, { name: "K", samples: [{ path: "/a.wav", name: "A", effects }] });
  expect(kit.errors).toEqual([null]);
  expect(runner.requests.map(typesOf)).toEqual([
    ["load"],
    ["buffer-player", "biquad", "buffer-recorder"],
    ["normalize", "buffer-recorder"],
    ["resample", "buffer-recorder"],
    ["buffer-player", "dither", "buffer-recorder"],
  ]);
  const [load, filter, normalize, resample, quantise] = runner.requests;
  const lsdjCap = { frames: 32576, rate: 11468, arithmetic: "float-ratio" };
  expect(load.inputs).toEqual([{ path: "/a.wav", offset: 0, length: 0, cap: lsdjCap }]);
  expect([filter.rate, filter.frames, normalize.rate, normalize.frames]).toEqual([44100, 4410, 44100, 4410]);
  const length = resampledLength(4410, 44100, 11468);
  expect([resample.rate, resample.frames, resample.outputs![0].frames]).toEqual([11468, length, length]);
  expect([quantise.rate, quantise.frames]).toEqual([11468, length]);
  expect((JSON.parse(quantise.document!) as Doc).nodes[1].settings!.algorithm).toBe("shaped-tpdf");
  // The echo's levels (the first `length` of the source's ramp) packed and assembled.
  const packed = packNibbles(levels.subarray(0, length), true);
  expect(kit.bytes[2] | (kit.bytes[3] << 8)).toBe(0x4060 + packed.length);
  expect([...kit.bytes.subarray(0x60, 0x60 + packed.length)]).toEqual([...packed]);
});

test("an LSDj slot that resamples to under one wave frame, or is empty, submits nothing after its load and says why",
  async () => {
  const runner = scriptedRunner({
    files: {
      "/short.wav": { samples: new Float32Array(100), rate: 44100 },
      "/empty.wav": { samples: new Float32Array(0), rate: 44100 },
    },
  });
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/short.wav", name: "S", effects: [{ type: "gain", gain: 1 }] },
      { path: "/empty.wav", name: "E", effects: [] },
    ],
  });
  expect(kit.errors).toEqual([
    "the window resamples to 26 frames at 11468 Hz, under one 32-sample wave frame",
    "the window holds no frames (offset 0, at or past the source's 0 frames)",
  ]);
  expect(runner.requests.map(typesOf)).toEqual([["load"], ["load"]]);
  // Both are named, zero-length slots, as native's compileSample leaves them.
  expect([kit.bytes[2] | (kit.bytes[3] << 8), kit.bytes[4] | (kit.bytes[5] << 8)]).toEqual([0x4060, 0x4060]);
  expect(String.fromCharCode(kit.bytes[0x22], kit.bytes[0x25])).toBe("SE");
});

test("a risa slot that resamples to no frames is the one-byte {0}, and one that does takes the DMC pass", async () => {
  const levels = Float32Array.from({ length: 2000 }, (_, i) => (i % 3 ? 1 : -1));
  const runner = scriptedRunner({
    files: { "/tiny.wav": { samples: new Float32Array(3), rate: 48000 }, "/b.wav": { samples: levels, rate: 22050 } },
  });
  const kit = await compileDmc(runner, {
    name: "R",
    samples: [
      { path: "/tiny.wav", name: "T", effects: [], rate: 0 },
      { path: "/b.wav", name: "B", effects: [{ type: "dither" }], rate: 15, normalize: false, loop: true },
    ],
  });
  expect(kit.errors).toEqual([null, null]);
  expect(runner.requests.map(typesOf)).toEqual([
    ["load"],
    ["load"],
    ["resample", "buffer-recorder"],
    ["dpcm", "buffer-recorder"],
  ]);
  const loadB = runner.requests[1];
  const dmcCap = { frames: 4081 * 8, rate: 33252.09, arithmetic: "double-scale" };
  expect(loadB.inputs![0]).toEqual({ path: "/b.wav", offset: 0, length: 0, cap: dmcCap });
  const dpcm = (JSON.parse(runner.requests[3].document!) as Doc).nodes[0];
  expect(dpcm.settings).toEqual({ buffer: "res:1", normalize: false });
  // Slot 0: one zero byte at address 0, length register 0, rate index 0.
  expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f02], kit.bytes[0]]).toEqual([0, 0, 0, 0]);
  // Slot 1: 64-byte aligned after it, its rate index and loop flag.
  // The echo pads the 2000 levels with zeros to the resampled length, and a zero is a 0 bit.
  const padded = new Float32Array(resampledLength(2000, 22050, 33252.09));
  padded.set(levels);
  const expected = packDmc(padded);
  const index1 = [...kit.bytes.subarray(0x1f04, 0x1f08)];
  expect(index1).toEqual([1, (expected.length - 1) / 16, 15, 1]);
  expect([...kit.bytes.subarray(64, 64 + expected.length)]).toEqual([...expected]);
});

test("a failed slot is left empty with its reason, and the rest of the kit compiles", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({
    files: {
      "/ok.wav": { samples: ok, rate: 44100 },
      "/nan.wav": { samples: ok, rate: 44100, nonFinite: true },
      "/missing.wav": "input 0: /missing.wav could not be read or decoded",
    },
  });
  const samples = [
    { path: "/missing.wav", name: "M", effects: [] },
    { path: "/nan.wav", name: "N", effects: [] },
    { path: "/ok.wav", name: "F", effects: [{ type: "filter", frequency: 30000 } as KitEffect] },
    { path: "/ok.wav", name: "OK", effects: [] },
  ];
  const kit = await compileKit(runner, { name: "K", samples });
  expect(kit.errors).toEqual([
    "/missing.wav could not be read or decoded", // native's job error, without the job's input numbering
    "/nan.wav has a sample that is not finite in the window",
    "effect 0 (filter): frequency 30000 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)",
    null,
  ]);
  // The failed slots are named with no bytes; the good one carries its sample.
  const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
  expect([entry(1), entry(2), entry(3)]).toEqual([0x4060, 0x4060, 0x4060]);
  expect(entry(4)).toBeGreaterThan(0x4060);
  // risa leaves a failed slot's index entry empty instead.
  const dmc = await compileDmc(runner, { name: "R", samples: [samples[0], samples[3]] });
  expect(dmc.errors).toEqual(["/missing.wav could not be read or decoded", null]);
  expect([dmc.bytes[0x1f00], dmc.bytes[0x1f04]]).toEqual([0xff, 0]);
});

test("a pass that records a non-finite sample, or that a node's guard stepped into, refuses its slot", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const base = scriptedRunner({ files: { "/ok.wav": { samples: ok, rate: 44100 } } });
  // The effects pass's recording carries an infinity (a normalise over a subnormal peak, say), or its job fails with
  // the count and first reason the offline host gives when guards fire; every other job is the scripted one.
  const runner = (fault: "infinity" | "guard"): KitJobRunner => ({
    async offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult> {
      const result = await base.offlineRun(request);
      const types = typesOf(request);
      if (types[0] === "normalize" && fault === "infinity") {
        result.buffers[0].samples[100] = Infinity;
        result.buffers[0].nonFinite = true; // as native reports it
      }
      if (types[0] === "normalize" && fault === "guard") {
        result.guardsFired = 2;
        result.guardReason = "rp.dsp.biquad reset a filter whose state went non-finite";
      }
      return result;
    },
  });
  const samples = [
    { path: "/ok.wav", name: "A", effects: [{ type: "gain", gain: 0.5 }, { type: "filter" }] as KitEffect[] },
    { path: "/ok.wav", name: "B", effects: [] },
  ];
  const infinity = await compileKit(runner("infinity"), { name: "K", samples });
  expect(infinity.errors).toEqual(["the pass running effects 0, 1 made a sample that is not finite", null]);
  const guard = await compileKit(runner("guard"), { name: "K", samples });
  expect(guard.errors[0]).toBe(
    "the pass running effects 0, 1 overflowed a filter, which rp.dsp.biquad reset (2 times)",
  );
  expect(guard.errors[1]).toBe(null);
});

test("a sample the bank does not hold whole says so: clipped, no room, or not fitting the DMC region", async () => {
  // The scripted runner applies no cap, so these sources pack past the space a bank has.
  const long = Float32Array.from({ length: 130000 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({
    files: { "/long.wav": { samples: long, rate: 44100 }, "/dmc.wav": { samples: long.map(() => 1), rate: 22050 } },
  });
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/long.wav", name: "A", effects: [] },
      { path: "/long.wav", name: "B", effects: [] },
    ],
  });
  const packed = packNibbles(long.subarray(0, resampledLength(130000, 44100, 11468)), true).length;
  expect(kit.errors).toEqual([
    `the kit's sample space ran out, so 16288 of this sample's ${packed} bytes fit`,
    `the kit's 16288 bytes of sample space are full, so none of this sample's ${packed} bytes fit`,
  ]);
  // risa: the encode at the slowest rate is cut at 4081 bytes, and a second one finds no room after it.
  const dmc = await compileDmc(runner, {
    name: "R",
    samples: [
      { path: "/dmc.wav", name: "A", effects: [], rate: 15 },
      { path: "/dmc.wav", name: "B", effects: [], rate: 15 },
    ],
  });
  expect(dmc.errors).toEqual([
    null,
    "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it",
  ]);
});

test("samples past the bank's slot count are not compiled, and say so", async () => {
  const runner = scriptedRunner({ files: { "/e.wav": { samples: new Float32Array(0), rate: 44100 } } });
  const sixteen = Array.from({ length: 16 }, (_, i) => ({ path: "/e.wav", name: `S${i}`, effects: [] }));
  const kit = await compileKit(runner, { name: "K", samples: sixteen });
  expect(kit.errors.length).toBe(16);
  expect(kit.errors[14]).toBe("the window holds no frames (offset 0, at or past the source's 0 frames)");
  expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
  expect(runner.requests.length).toBe(15);
  const seventeen = [...sixteen, { path: "/e.wav", name: "X", effects: [] }];
  const dmc = await compileDmc(runner, { name: "R", samples: seventeen });
  expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
});

test("on the mock backend every slot fails with its reason, since only a native host runs graphs", async () => {
  const kit = await compileKit(new MockBackend(), { name: "K", samples: [{ path: "/a.wav", name: "A", effects: [] }] });
  expect(kit.errors).toEqual(["offline jobs need a native host: the mock backend cannot run graphs"]);
  expect(kit.bytes.length).toBe(0x4000);
});
````

Run: `mkdir -p packages/retroplug/test/kit; sed -n '7640,8254p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test/kit/kit.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/kit/kit.test.ts`
Expected: `08eb03f9157a68cf8e52fa3bf96d64b137a36934`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test/offline/offline.test.ts b/packages/retroplug/test/offline/offline.test.ts
index da10705dd..7dd32b65e 100644
--- a/packages/retroplug/test/offline/offline.test.ts
+++ b/packages/retroplug/test/offline/offline.test.ts
@@ -58,7 +58,8 @@ function clockPort() {
       for (const j of jobs.filter((x) => x.end <= now)) {
         const report = { handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true };
         if (j.request.document === "fail") {
-          done.push({ id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] });
+          const failed = { id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] };
+          done.push({ ...failed, guardsFired: 0, guardReason: "" });
           continue;
         }
         const echoed = j.request.inputs[0]?.samples ?? new Uint8Array(0);
@@ -67,8 +68,12 @@ function clockPort() {
           id: j.id,
           ok: true,
           error: "",
-          buffers: [{ handle: 1, channels: 1, frames: echoed.byteLength / 4, rate: 48000, samples: echoed }],
+          buffers: [
+            { handle: 1, channels: 1, frames: echoed.byteLength / 4, rate: 48000, samples: echoed, nonFinite: false },
+          ],
           inputs: [],
+          guardsFired: 0,
+          guardReason: "",
         });
       }
       for (const d of done) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
````

Run: `sed -n '8264,8291p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:native kit-baseline`
Expected: the file cannot load: `../src/kit` cannot be resolved, because the compiler does not exist yet. That is this half's red.

Run: `pnpm test kit`
Expected: the same, over the mock tier: the compiler's modules cannot be resolved.

- [ ] **Step 4: The compiler**

`src/kit/pack.ts` is the integer half: the kit rate 11468, the 0x3fa0-byte sample space (0x3fa0 * 2 frames), risa's
4081-byte cut and its PAL table digit for digit, `resampledLength` (`Math.floor(frames * (outRate / inRate))`, the
native double expression operation for operation), `trimToFrames`, `levelToCode` (`round((level + 1) * 0.5 * 15)`),
`packNibbles` and `packDmc`. `src/kit/chain.ts` is the four range constants, `resolveEffects` (validation, the
refusals, the last dither winning, the split at each normalising gain), the four documents and the five job builders.
`src/kit/compile.ts` is `compileKit(runner, spec)` and `compileDmc(runner, spec)`, returning `{bytes, errors}`; a slot
is a load job, one effects pass per segment, a resample for exactly `resampledLength` frames, a quantise or DPCM pass,
then the pack and the assemble. `index.ts` re-exports. `buildKit.ts`'s `writeField`, `risa/rom/kit.ts`'s
`writeSampleName` and kit-name filter move to native's byte rules (only `a-z` uppercased over the UTF-8 bytes),
because JavaScript's Unicode `toUpperCase` can change a name's length; no baseline bank moves, and non-ASCII names now
agree too. A pass a guard stepped into or a recording flagged non-finite refuses its slot, naming the pass; a source
whose window is empty or resamples to nothing produces native's bytes without a job; every short or empty slot carries
a reason. `scripts/run-native-tests.mjs` adds the `__KIT_GOLDEN__` and `__KIT_EDGES_GOLDEN__` defines.

Create `packages/retroplug/src/kit/pack.ts`:

````ts
// The kit compilers' integer half (docs/modular-graph-phase-d2-design.md section 5): what TypeScript does to the
// buffers the offline jobs record, after native has done every float the bytes depend on. LSDj's levels become 4-bit
// codes, trimmed to whole wave frames and packed two to a byte (native KitUtil::compileSample's tail, SampleUtil's
// convertCodesToNibbles); risa's DPCM levels become the delta bits, packed and padded to a legal DMC length
// (RisaDmcCodec::encode's tail). Nothing here touches a float beyond reading which side of zero, or which level, a
// recorded sample is, so every step is exact by construction and the tests hold each to its native twin.
//
// The constants the chain needs to cut and cap a slot live here too, spelled as the native code spells them: the
// kit rates, the frame caps and the PAL DPCM rate table, whose literals are the native table's digit for digit.

/** LSDj's kit rate, the Game Boy's sample rate as the kit encoder resamples to (native kGameboySampleRate). */
export const LSDJ_KIT_RATE = 11468;

/** The 4-bit depth an LSDj kit sample is quantised to. */
export const LSDJ_KIT_BITS = 4;

/** The samples one wave frame holds, and the bytes it packs to (native SampleUtil). */
export const LSDJ_FRAME_SAMPLES = 32;
export const LSDJ_FRAME_BYTES = 16;

/** The most frames a kit can hold at the kit rate: its 0x3fa0 bytes of sample space, two 4-bit samples a byte
 *  (native Kit::kMaxSampleFrames). The window caps a slot's source to this many frames at the kit rate. */
export const LSDJ_MAX_SAMPLE_FRAMES = 0x3fa0 * 2;

/** risa's DMC byte limit: 4081 = 16 * 255 + 1, the longest legal $4013 length (native kDmcMaxBytes). */
export const DMC_MAX_BYTES = 4081;

/** A legal DMC length is 16k + 1 bytes (native kLengthStep). */
export const DMC_LENGTH_STEP = 16;

/** PAL DPCM playback rates (Hz), index 0..15, the literals native RisaDmcCodec.cpp's kPalRates spells (wav2dmc.py
 *  PAL_DPCM_RATES_HZ). A slot resamples to its index's rate, and its window's cap scales by it. */
export const PAL_DMC_RATES: readonly number[] = [
  4177.40, 4696.63, 5261.41, 5579.22, 6023.94, 7044.94, 7917.18, 8397.01,
  9446.63, 11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
];

/** The PAL DPCM rate a slot's index names: native masks the index to its low four bits (`rate & 0x0F`) at the RPC
 *  and again in the codec, so an index of 17 is rate 1, never an error. */
export function palDmcRate(index: number): number {
  return PAL_DMC_RATES[index & 0x0f];
}

/** A whole buffer's resampled length as the kit path cuts it (rp::dsp::resampledLength): `floor(frames * (outRate /
 *  inRate))`, the ratio taken first. JavaScript's numbers are the same doubles, so this is the native expression
 *  operation for operation: one division and one multiply, each correctly rounded, then a truncation (the operands
 *  are positive, so `Math.floor` is the cast). */
export function resampledLength(frames: number, inRate: number, outRate: number): number {
  return Math.floor(frames * (outRate / inRate));
}

/** `n` trimmed to whole 32-sample wave frames (native frameAlign): the packer writes whole frames only. */
export function trimToFrames(n: number): number {
  return n - (n % LSDJ_FRAME_SAMPLES);
}

/**
 * A 4-bit level back to its code. `rp.dsp.dither` outputs each code as the level the LSDj decoder gives it,
 * `float(code) / 15 * 2 - 1` in binary32 (rp::dsp::codeToLevel), so `(level + 1) * 0.5 * 15` lands within a few
 * float ulps of the code and `Math.round` recovers it: the rounding step is a whole unit and the error is about 1e-6,
 * so no level can round to its neighbour (the mock-tier case checks every one of the 16). A level the dither cannot
 * produce has no code; the clamp only keeps a stray one inside the nibble rather than wrapping it.
 */
export function levelToCode(level: number): number {
  const code = Math.round((level + 1) * 0.5 * 15);
  return code < 0 ? 0 : code > 15 ? 15 : code;
}

/**
 * Recorded 4-bit levels to the slot's packed nibbles: each level to its code, the run trimmed to whole wave frames,
 * then native convertCodesToNibbles - inverted (stored = 15 - code), and with `rotate` (LSDj 9.2.0+) each frame's
 * sample i at position (i + 1) % 32, so the frame's last sample opens it; then two samples a byte, high nibble first.
 */
export function packNibbles(levels: ArrayLike<number>, rotate: boolean): Uint8Array {
  const frames = trimToFrames(levels.length) / LSDJ_FRAME_SAMPLES;
  const out = new Uint8Array(frames * LSDJ_FRAME_BYTES);
  const frame = new Uint8Array(LSDJ_FRAME_SAMPLES);
  for (let f = 0; f < frames; f++) {
    const base = f * LSDJ_FRAME_SAMPLES;
    for (let i = 0; i < LSDJ_FRAME_SAMPLES; i++) {
      const at = rotate ? (i + 1) % LSDJ_FRAME_SAMPLES : i;
      frame[at] = 0xf - levelToCode(levels[base + i]);
    }
    for (let i = 0; i < LSDJ_FRAME_BYTES; i++) out[f * LSDJ_FRAME_BYTES + i] = (frame[i * 2] << 4) | frame[i * 2 + 1];
  }
  return out;
}

/**
 * Recorded DPCM levels to a slot's DMC bytes, as RisaDmcCodec::encode packs them: `rp.dsp.dpcm` writes each delta bit
 * as exactly +1 or -1, so `level > 0` is the bit; bits go least-significant first, eight to a byte; the stream is
 * padded with zero bytes to a legal 16k + 1 length (an empty one is the one-byte `{0}`, itself legal); and a stream
 * past the hardware's 4081 bytes is cut there, still a legal length.
 */
export function packDmc(levels: ArrayLike<number>): Uint8Array {
  const packedLength = Math.ceil(levels.length / 8);
  if (packedLength === 0) return Uint8Array.of(0);
  const rem = (packedLength - 1) % DMC_LENGTH_STEP;
  const padded = rem === 0 ? packedLength : packedLength + (DMC_LENGTH_STEP - rem);
  const out = new Uint8Array(Math.min(padded, DMC_MAX_BYTES));
  const bits = Math.min(levels.length, out.length * 8);
  for (let bit = 0; bit < bits; bit++) if (levels[bit] > 0) out[bit >> 3] |= 1 << (bit & 7);
  return out;
}
````

Run: `mkdir -p packages/retroplug/src/kit; sed -n '8322,8425p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/src/kit/pack.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/kit/pack.ts`
Expected: `b58869e66b7c50c5c377a73a9546bd001dcc65bd`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/src/kit/chain.ts`:

````ts
// The kit chain's documents (docs/modular-graph-phase-d2-design.md section 5): a slot's effect list resolved and
// checked against the nodes that will run it, then each pass of the chain as one offline job. A pass ends wherever the
// next step needs the whole of what came before: a normalising gain measures its input's peak, the resample cuts a
// whole buffer to its length, and the dither seeds from the whole resampled buffer and the DPCM encode normalises over
// it. Each of those is a node that reads its buffer at activate (rp.dsp.normalize, rp.dsp.resample, rp.dsp.dither's
// `seedFrom`, rp.dsp.dpcm), so each pass records into a buffer the next pass is handed as its input.
//
// The order is native KitUtil::compileSample's (and RisaDmcCodec::encode's), because the order is the bytes: the
// effects at the source rate in list order, the resample, then the quantise (LSDj) or the DMC encode (risa).
//
// Every spec value goes into a document as its binary32 value (`Math.fround`), which is what native decodes a spec's
// float field to and what each node casts its param or setting to, so the node computes on the kit path's float. The
// checks are on that same value, as a double against the descriptor's double bounds, the comparison the planner
// clamps with: anything this file accepts reaches its node unclamped, and anything the planner would clamp is refused
// here with a reason instead of compiling a different spec.

import type { KitEffect } from "../audioDriver";
import { offlineInputRef, offlineOutputRef, type OfflineCap, type OfflineRunRequest } from "../offline";

/** A param's bounds, as its node's descriptor declares them. */
export interface ParamRange {
  min: number;
  max: number;
}

// The param ranges of the two nodes a spec's values reach (DspNodes.cpp's float_params, recorded in
// packages/native/test/graph/golden/rp.dsp.{gain,biquad}.descriptor.json, which a pnpm test case holds these to).
export const GAIN_RANGE: ParamRange = { min: -100, max: 100 };
export const BIQUAD_FREQUENCY_RANGE: ParamRange = { min: 1, max: 96000 };
export const BIQUAD_Q_RANGE: ParamRange = { min: 0.001, max: 1000 };
export const BIQUAD_GAIN_DB_RANGE: ParamRange = { min: -96, max: 96 };

/** A spec the kit compiler refuses: the reason is the slot's error. */
export class KitSpecError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "KitSpecError";
  }
}

// The effect's enums as the nodes label them (the kebab-case of the kernel enumerator, rp.dsp.biquad's `type` and
// rp.dsp.dither's `algorithm`), keyed by the spec's spelling (the rp::lsdj enum names reflect-cpp decodes).
const FILTER_TYPES: Readonly<Record<string, string>> = {
  LowPass: "low-pass",
  HighPass: "high-pass",
  BandPass: "band-pass",
  BandStop: "band-stop",
  Peak: "peak",
  LowShelf: "low-shelf",
  HighShelf: "high-shelf",
  AllPass: "all-pass",
};
const DITHER_TYPES: Readonly<Record<string, string>> = {
  HighPassTPDF: "high-pass-tpdf",
  ShapedTPDF: "shaped-tpdf",
  ErrorDiffusion: "error-diffusion",
  JJN: "jjn",
  SierraLite: "sierra-lite",
};

/** One streaming step of an effects pass: a plain gain, or a biquad. Values are binary32. */
export type ChainNode =
  | { kind: "gain"; gain: number }
  | { kind: "biquad"; type: string; frequency: number; q: number; gainDb: number };

/** One effects pass: its buffer played (through rp.dsp.normalize at `normalize`'s gain when the pass starts at a
 *  normalising gain, else plainly), then its nodes in list order. `effects` are the spec indices of the effects it
 *  runs, the normalising gain's included, for a reason that names them. */
export interface ChainSegment {
  normalize: number | null;
  nodes: ChainNode[];
  effects: number[];
}

/** A slot's effect list, resolved: the effects passes in order (none for a list with nothing to run), and the dither
 *  the quantise runs, the last dither effect winning, or null for plain rounding. */
export interface SlotChain {
  segments: ChainSegment[];
  dither: string | null;
}

// A spec field's value: its binary32 value, with the value as the spec wrote it for a message, or the rp::lsdj
// struct's default when the field is absent (audioDriver.ts's KitEffect declares every field optional; native's RPC
// decode has no defaults and refuses the whole kit for a field left out, so here is the more lenient of the two). A
// non-number is refused: native would not decode the spec at all.
interface Field {
  value: number;
  written: number;
}

function field(effect: KitEffect, key: string, fallback: number, where: string): Field {
  const v = (effect as unknown as Record<string, unknown>)[key];
  if (v === undefined) return { value: Math.fround(fallback), written: fallback };
  if (typeof v !== "number") throw new KitSpecError(`${where}: ${key} is not a number`);
  return { value: Math.fround(v), written: v };
}

function flag(effect: KitEffect, key: string, fallback: boolean, where: string): boolean {
  const v = (effect as unknown as Record<string, unknown>)[key];
  if (v === undefined) return fallback;
  if (typeof v !== "boolean") throw new KitSpecError(`${where}: ${key} is not a boolean`);
  return v;
}

function enumLabel(table: Readonly<Record<string, string>>, value: unknown, fallback: string, what: string,
                   where: string): string {
  const key = value === undefined ? fallback : value;
  const label = typeof key === "string" && Object.prototype.hasOwnProperty.call(table, key) ? table[key] : undefined;
  if (label === undefined) throw new KitSpecError(`${where}: unknown ${what} ${JSON.stringify(value)}`);
  return label;
}

// The field's binary32 value, if the node's range holds it; refused otherwise. The comparison is the planner's (a
// double against the double bounds), on the value the document will carry. A NaN is inside no range.
function inRange(f: Field, range: ParamRange, what: string, node: string, where: string): number {
  if (!(f.value >= range.min && f.value <= range.max))
    throw new KitSpecError(`${where}: ${what} ${f.written} is outside ${node}'s range ${range.min} .. ${range.max}`);
  return f.value;
}

/**
 * A slot's effect list resolved against the nodes that run it, at the source's `sourceRate`: refused (a KitSpecError
 * naming the effect) for an effect type, filter type or dither type the spec cannot name, a plain gain outside
 * rp.dsp.gain's range, a filter value outside rp.dsp.biquad's, or a filter at or above the source's Nyquist frequency
 * (the planner cannot see the plan's rate, and a biquad there is unstable, where the node's guard would part from the
 * kernel). A normalising gain is not range-checked: rp.dsp.normalize folds the peak into it as a setting, which no
 * range clamps, so it is refused only when a document cannot carry it (not finite). `withDither` false (risa) skips a
 * dither effect unread, as RisaDmcCodec does.
 *
 * The list is cut at each normalising gain; a pass with nothing to run (the stretch before the first normalising gain,
 * when it holds no node) is dropped, so a list of nothing but dithers has no passes at all.
 */
export function resolveEffects(effects: readonly KitEffect[], sourceRate: number, withDither: boolean): SlotChain {
  const segments: ChainSegment[] = [{ normalize: null, nodes: [], effects: [] }];
  const last = (): ChainSegment => segments[segments.length - 1];
  let dither: string | null = null;
  effects.forEach((effect, i) => {
    const type = (effect as { type?: unknown }).type;
    const where = `effect ${i} (${String(type)})`;
    if (type === "gain") {
      const gain = field(effect, "gain", 1, where);
      if (flag(effect, "normalize", true, where)) {
        // A setting carries any binary32 value but a non-finite one, which JSON cannot: refused, saying which kind.
        if (Number.isNaN(gain.written)) throw new KitSpecError(`${where}: a normalising gain of NaN is not a number`);
        if (!Number.isFinite(gain.written))
          throw new KitSpecError(`${where}: a normalising gain of ${gain.written} is not finite`);
        if (!Number.isFinite(gain.value))
          throw new KitSpecError(
            `${where}: a normalising gain of ${gain.written} is past binary32's largest value (about 3.4e38)`,
          );
        segments.push({ normalize: gain.value, nodes: [], effects: [i] });
      } else {
        const linear = inRange(gain, GAIN_RANGE, "gain", "rp.dsp.gain", where);
        last().nodes.push({ kind: "gain", gain: linear });
        last().effects.push(i);
      }
    } else if (type === "filter") {
      const kind = (effect as { filterType?: unknown }).filterType;
      const filterType = enumLabel(FILTER_TYPES, kind, "LowPass", "filter type", where);
      const freq = field(effect, "frequency", 5734, where);
      const frequency = inRange(freq, BIQUAD_FREQUENCY_RANGE, "frequency", "rp.dsp.biquad", where);
      const q = inRange(field(effect, "q", 1, where), BIQUAD_Q_RANGE, "q", "rp.dsp.biquad", where);
      const gainDb = inRange(field(effect, "gain", 0, where), BIQUAD_GAIN_DB_RANGE, "gain", "rp.dsp.biquad", where);
      const nyquist = sourceRate / 2;
      if (frequency >= nyquist)
        throw new KitSpecError(
          `${where}: frequency ${freq.written} Hz is at or above the source's Nyquist frequency ` +
            `(${nyquist} Hz at ${sourceRate} Hz)`,
        );
      last().nodes.push({ kind: "biquad", type: filterType, frequency, q, gainDb });
      last().effects.push(i);
    } else if (type === "dither") {
      const kind = (effect as { ditherType?: unknown }).ditherType;
      if (withDither) dither = enumLabel(DITHER_TYPES, kind, "HighPassTPDF", "dither type", where);
    } else {
      throw new KitSpecError(`effect ${i}: unknown effect type ${JSON.stringify(type)}`);
    }
  });
  return { segments: segments.filter((s) => s.normalize !== null || s.nodes.length > 0), dither };
}

// ---- the documents ------------------------------------------------------------------------------------------------

// Every document here is mono: each node with a `channels` setting says 1, since several default to 2. A pass's input
// is res:1 and its recording res:2 (offlineInputRef, offlineOutputRef).
const IN = offlineInputRef(0);
const OUT = offlineOutputRef(1, 0);

interface DocNode {
  id: string;
  type: string;
  settings?: Record<string, unknown>;
  params?: Record<string, number>;
  resources?: Record<string, string>;
}

// A chain of nodes, each one's `out` wired to the next one's `in`, the last into a recorder of res:2.
function chainDocument(nodes: DocNode[]): string {
  const recorder: DocNode = {
    id: "rec",
    type: "rp.core.buffer-recorder",
    settings: { channels: 1 },
    resources: { target: OUT },
  };
  const all = [...nodes, recorder];
  const wires = all.slice(1).map((n, i) => ({ from: `${all[i].id}.out`, to: `${n.id}.in` }));
  return JSON.stringify({ nodes: all, wires });
}

// res:1 played from its first frame, once.
function player(): DocNode {
  return {
    id: "src",
    type: "rp.core.buffer-player",
    settings: { channels: 1, layout: "mono", loop: false },
    resources: { sample: IN },
  };
}

/** An effects pass's document: res:1 played (normalised, when the pass starts at a normalising gain), its nodes in
 *  order, recorded to res:2. */
export function effectsDocument(segment: ChainSegment): string {
  const source: DocNode =
    segment.normalize === null
      ? player()
      : { id: "src", type: "rp.dsp.normalize", settings: { buffer: IN, gain: segment.normalize, channels: 1 } };
  const nodes = segment.nodes.map((n, i): DocNode =>
    n.kind === "gain"
      ? { id: `n${i}`, type: "rp.dsp.gain", settings: { channels: 1 }, params: { gain: n.gain } }
      : {
          id: `n${i}`,
          type: "rp.dsp.biquad",
          settings: { channels: 1, type: n.type },
          params: { frequency: n.frequency, q: n.q, gainDb: n.gainDb },
        },
  );
  return chainDocument([source, ...nodes]);
}

/** The resample pass's document: res:1 through rp.dsp.resample to the plan's rate, recorded to res:2. */
export function resampleDocument(): string {
  const settings = { buffer: IN, channels: 1, layout: "mono", loop: false };
  return chainDocument([{ id: "src", type: "rp.dsp.resample", settings }]);
}

/** The quantise pass's document: res:1 played into rp.dsp.dither at 4 bits, seeded from res:1 itself (the whole
 *  resampled buffer, before any trim: the kit path's seed), its levels recorded to res:2. `algorithm` is the
 *  remembered dither's, or "none" for plain rounding (which reads no seed, as the kit path's rounding does not). */
export function quantiseDocument(algorithm: string, bits: number): string {
  const settings = { channels: 1, algorithm, bits, seedFrom: IN };
  return chainDocument([player(), { id: "q", type: "rp.dsp.dither", settings }]);
}

/** The DMC pass's document: res:1 encoded whole by rp.dsp.dpcm at activate, its delta bits recorded to res:2 as
 *  levels of plus and minus one. */
export function dpcmDocument(normalize: boolean): string {
  return chainDocument([{ id: "src", type: "rp.dsp.dpcm", settings: { buffer: IN, normalize } }]);
}

// ---- the jobs -----------------------------------------------------------------------------------------------------

/** A slot's source decoded and windowed natively, returned with no document run: the window's buffer comes back with
 *  its decoded rate, and the input's report says how long the window is and whether it holds a non-finite sample. */
export function loadJob(path: string, offset: number, length: number, cap: OfflineCap): OfflineRunRequest {
  return { inputs: [{ path, offset, length, cap }], returns: [1], frames: 0, rate: cap.rate };
}

// A mono pass over `samples` at `inRate`, recording `frames` frames at the plan's `rate`.
function pass(document: string, samples: Float32Array, inRate: number, frames: number,
              rate: number): OfflineRunRequest {
  return {
    document,
    inputs: [{ samples, channels: 1, rate: inRate }],
    outputs: [{ channels: 1, frames, rate }],
    frames,
    rate,
  };
}

/** An effects pass over the whole buffer, at the source rate (a biquad's coefficients are the plan rate's). */
export function effectsJob(samples: Float32Array, rate: number, segment: ChainSegment): OfflineRunRequest {
  return pass(effectsDocument(segment), samples, rate, samples.length, rate);
}

/** The resample from `inRate` to `outRate`, run for exactly `frames` frames: the kit path's whole-buffer cut,
 *  resampledLength. */
export function resampleJob(samples: Float32Array, inRate: number, outRate: number, frames: number): OfflineRunRequest {
  return pass(resampleDocument(), samples, inRate, frames, outRate);
}

/** The quantise of the whole resampled buffer at the kit rate. */
export function quantiseJob(samples: Float32Array, rate: number, algorithm: string, bits: number): OfflineRunRequest {
  return pass(quantiseDocument(algorithm, bits), samples, rate, samples.length, rate);
}

/** The DMC encode of the whole resampled buffer, one bit a frame. */
export function dpcmJob(samples: Float32Array, rate: number, normalize: boolean): OfflineRunRequest {
  return pass(dpcmDocument(normalize), samples, rate, samples.length, rate);
}
````

Run: `mkdir -p packages/retroplug/src/kit; sed -n '8437,8735p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/src/kit/chain.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/kit/chain.ts`
Expected: `b2147bb66ad212a1279df631a3aeb9912fa2d6af`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/src/kit/compile.ts`:

````ts
// The kit compilers (docs/modular-graph-phase-d2-design.md section 5): an LSDj kit or a risa DMC kit compiled from
// its spec as chains of offline graph jobs, one chain per slot, every slot's first job submitted at once so the
// engine's offline pool runs them in parallel. Native decodes, windows and does every float the bytes depend on (the
// effects, the resample, the dither, the DMC encode, each a node in a job's document, chain.ts); TypeScript builds the
// documents, cuts the buffers to their lengths, packs the codes and bits (pack.ts) and assembles the bank
// (lsdj/rom/buildKit.ts, risa/rom/kit.ts). A kit compiled here is the bank native KitCompiler builds from the same
// spec, byte for byte: test-native/kit-baseline.test.ts holds it to kit-baseline.json's 39 banks, and
// test-native/kit-compile.test.ts to kit-edges.json's banks past them.
//
// A slot that fails is left empty and the kit still compiles, as KitCompiler leaves a slot whose file fails to load:
// a source that cannot be read or decoded, a window with a sample that is not finite, a spec the nodes cannot carry
// (chain.ts's refusals), a pass that makes a sample that is not finite, or a pass a node's guard stepped into
// (`recorded` says why those two keep every compiled kit byte-identical). Empty means no bytes: LSDj's assembler still
// names the slot, with a length of zero; risa's leaves its index entry empty.
//
// What a slot is when a stage comes out empty is the native compiler's too, found by reading it and reproduced here
// rather than run, since a job can neither take an input of no frames nor record one: a window that is empty (an
// offset at or past the source's end) or resamples to fewer frames than one wave frame is an LSDj slot of no bytes,
// which is also what compileSample's trim makes of it; and a window that resamples to no frames at all is risa's
// one-byte `{0}`, the encode of nothing padded to its shortest legal length.
//
// Every slot the bank does not hold whole has a reason in the result, a failure or not: the empty LSDj slots above,
// an LSDj sample the bank's sample space ran out on (clipped, or with no room at all), and a risa sample that does not
// fit the sample region after the slots before it. The placements are the assemblers' own (buildKitBankPlaced,
// assembleKitBankPlaced), so the reasons cannot drift from the fit rules. A risa `{0}` is a placed sample, not an
// empty slot, and has none.

import type { KitCompileSpec, KitSampleSpec, RisaKitCompileSpec, RisaDmcSampleSpec } from "../audioDriver";
import type { Backend } from "../backend";
import { OfflineRunError, type OfflineCap, type OfflineRunRequest } from "../offline";
import { buildKitBankPlaced } from "../lsdj/rom/buildKit";
import { KIT_MAX_SAMPLES, KIT_MAX_SAMPLE_SPACE } from "../lsdj/rom/constants";
import { assembleKitBankPlaced, type AssembleSlot } from "../risa/rom/kit";
import { KIT_SAMPLE_REGION, KIT_SLOT_COUNT } from "../risa/rom/constants";
import {
  KitSpecError,
  dpcmJob,
  effectsJob,
  loadJob,
  quantiseJob,
  resampleJob,
  resolveEffects,
  type SlotChain,
} from "./chain";
import {
  DMC_MAX_BYTES,
  LSDJ_KIT_BITS,
  LSDJ_KIT_RATE,
  LSDJ_MAX_SAMPLE_FRAMES,
  packDmc,
  packNibbles,
  palDmcRate,
  resampledLength,
  trimToFrames,
} from "./pack";

/** What the compilers run their jobs on: a backend's offline jobs (every native host has them; the mock rejects).
 *  The promises settle only in a host that pumps or waits for its offline jobs (src/offline.ts): one with a UI tick
 *  pumps from it, and one without needs the poll's native wait, which createAudioDriver turns on as it is made (or
 *  setOfflineWait). In a host with neither, a compile never settles. */
export type KitJobRunner = Pick<Backend, "offlineRun">;

/** A compiled kit: the bank, and each spec sample's reason in spec order, null exactly when the bank holds that
 *  sample whole. A reason is a failure (the slot is empty), or why a slot that compiled is still empty or short (an
 *  LSDj window with nothing to pack, a bank that ran out of room); a sample past the bank's slot count is not
 *  compiled, and its reason says so. */
export interface KitCompileResult {
  bytes: Uint8Array;
  errors: (string | null)[];
}

// The window caps: at most a kit's worth of frames at the kit rate, scaled to the decoded rate in each compiler's own
// arithmetic (LSDj in float with the ratio first, risa in double with the product first: OfflineJobs' offlineWindow).
const LSDJ_CAP: OfflineCap = { frames: LSDJ_MAX_SAMPLE_FRAMES, rate: LSDJ_KIT_RATE, arithmetic: "float-ratio" };
const dmcCap = (rate: number): OfflineCap => ({ frames: DMC_MAX_BYTES * 8, rate, arithmetic: "double-scale" });

interface Source {
  samples: Float32Array;
  rate: number;
  decodedFrames: number;
}

// The slot's source, decoded and windowed natively. A window holding a sample that is not finite is refused: a NaN or
// an infinity is where a node's guard and the kit kernel part ways, so no compile could promise the kit path's bytes.
// A file that cannot be read or decoded is the job's failure, said without the job's own input numbering.
async function load(runner: KitJobRunner, spec: KitSampleSpec, cap: OfflineCap): Promise<Source> {
  let result;
  try {
    result = await runner.offlineRun(loadJob(spec.path, spec.offset ?? 0, spec.length ?? 0, cap));
  } catch (e) {
    if (e instanceof OfflineRunError) throw new Error(e.message.replace(/^input 0: /, ""));
    throw e;
  }
  const [report] = result.inputs;
  if (report.nonFinite) throw new KitSpecError(`${spec.path} has a sample that is not finite in the window`);
  return { samples: result.buffers[0].samples, rate: report.decodedRate, decodedFrames: report.decodedFrames };
}

// What a guard's report says, in a slot error's words: which node stepped in, and what that means for the pass.
function guardWords(reason: string, times: number): string {
  const count = `${times} time${times === 1 ? "" : "s"}`;
  if (reason.startsWith("rp.dsp.biquad")) return `overflowed a filter, which rp.dsp.biquad reset (${count})`;
  if (reason.startsWith("rp.dsp.dither"))
    return `overflowed the dither's state, which rp.dsp.dither restarted (${count})`;
  return `needed a node's guard (${count}: ${reason})`;
}

// A pass run, and the one buffer it recorded; refused (naming the pass, `what`) when a node's guard stepped in during
// the run or the recording holds a sample that is not finite.
//
// The two refusals keep a compiled kit byte-identical to the kit kernels when a finite source overflows somewhere in
// the chain. Every pass's input is finite: the window's, checked at the load, or an earlier pass's recording, checked
// here. In an effects pass, a gain or a normalise has neither a guard nor a clamp, so a non-finite value it makes goes
// on to the recording or to a biquad; the biquad clamps its output, the one place an effects pass could hide such a
// value, but a non-finite input or output leaves its state non-finite, which its guard resets and reports. The
// resample has no guard or clamp either, so what it makes is in its recording. The quantise and the DMC encode only
// ever get checked inputs: the dither reports any restart, and on finite input the dither's quantiser and the DPCM
// encoder are their kernels. So a node departs from its kernel only where a guard reports it (rp.host.guards/1, counted
// in the completion), and every other non-finite value is in a recording; refusing both refuses every way a kit could
// part from the kernels, as a source with a non-finite sample is refused. Native reads the non-finite flag as it copies
// the buffer out; a scan here would cost several times the job in a script runtime (380 ms of a 450 ms compile of
// fifteen capped 96 kHz slots).
async function recorded(runner: KitJobRunner, request: OfflineRunRequest, what: string): Promise<Float32Array> {
  const result = await runner.offlineRun(request);
  if (result.guardsFired > 0) throw new KitSpecError(`${what} ${guardWords(result.guardReason, result.guardsFired)}`);
  const [buffer] = result.buffers;
  if (buffer.nonFinite) throw new KitSpecError(`${what} made a sample that is not finite`);
  return buffer.samples;
}

// The effects passes in order, each recording the whole buffer for the next.
async function runEffects(runner: KitJobRunner, source: Source, chain: SlotChain): Promise<Float32Array> {
  let buffer = source.samples;
  for (const segment of chain.segments) {
    const what = `the pass running effect${segment.effects.length === 1 ? "" : "s"} ${segment.effects.join(", ")}`;
    buffer = await recorded(runner, effectsJob(buffer, source.rate, segment), what);
  }
  return buffer;
}

// A slot's compiled bytes, and why they are empty when they are without a failure.
interface Compiled {
  bytes: Uint8Array;
  empty: string | null;
}

async function lsdjSlot(runner: KitJobRunner, spec: KitSampleSpec, rotate: boolean): Promise<Compiled> {
  const source = await load(runner, spec, LSDJ_CAP);
  const chain = resolveEffects(spec.effects ?? [], source.rate, true);
  if (source.samples.length === 0) {
    const where = `offset ${spec.offset ?? 0}, at or past the source's ${source.decodedFrames} frames`;
    const empty = `the window holds no frames (${where})`;
    return { bytes: new Uint8Array(0), empty };
  }
  const length = resampledLength(source.samples.length, source.rate, LSDJ_KIT_RATE);
  if (trimToFrames(length) === 0) {
    const empty = `the window resamples to ${length} frames at ${LSDJ_KIT_RATE} Hz, under one 32-sample wave frame`;
    return { bytes: new Uint8Array(0), empty };
  }
  const effected = await runEffects(runner, source, chain);
  const resampled = await recorded(runner, resampleJob(effected, source.rate, LSDJ_KIT_RATE, length), "the resample");
  const quantise = quantiseJob(resampled, LSDJ_KIT_RATE, chain.dither ?? "none", LSDJ_KIT_BITS);
  const levels = await recorded(runner, quantise, "the quantise");
  return { bytes: packNibbles(levels, rotate), empty: null };
}

async function dmcSlot(runner: KitJobRunner, spec: RisaDmcSampleSpec): Promise<Compiled> {
  const rate = palDmcRate(spec.rate ?? 12);
  const source = await load(runner, spec, dmcCap(rate));
  const chain = resolveEffects(spec.effects ?? [], source.rate, false);
  const length = resampledLength(source.samples.length, source.rate, rate);
  if (length === 0) return { bytes: packDmc([]), empty: null };
  const effected = await runEffects(runner, source, chain);
  const resampled = await recorded(runner, resampleJob(effected, source.rate, rate, length), "the resample");
  const levels = await recorded(runner, dpcmJob(resampled, rate, spec.normalize ?? true), "the DMC encode");
  return { bytes: packDmc(levels), empty: null };
}

interface SlotResult {
  bytes: Uint8Array;
  error: string | null;
}

async function settle(compile: Promise<Compiled>): Promise<SlotResult> {
  try {
    const { bytes, empty } = await compile;
    return { bytes, error: empty };
  } catch (e) {
    return { bytes: new Uint8Array(0), error: e instanceof Error ? e.message : String(e) };
  }
}

// Each honoured slot's chain started at once; a sample past the bank's `capacity` is not compiled (native's assembler
// drops it too), and its reason says so.
async function compileSlots<S>(samples: readonly S[], capacity: number, compile: (s: S) => Promise<Compiled>,
                               bankName: string): Promise<{ results: SlotResult[]; dropped: string[] }> {
  const results = await Promise.all(samples.slice(0, capacity).map((s) => settle(compile(s))));
  const why = `the ${bankName} bank holds ${capacity} samples, so this one is dropped`;
  return { results, dropped: samples.slice(capacity).map(() => why) };
}

/** An LSDj kit: the 16 KB bank native compileKit builds from `spec` (its 15 slots, the rest dropped), and each
 *  sample's reason. */
export async function compileKit(runner: KitJobRunner, spec: KitCompileSpec): Promise<KitCompileResult> {
  const rotate = spec.rotate ?? true;
  const compile = (s: KitSampleSpec): Promise<Compiled> => lsdjSlot(runner, s, rotate);
  const { results, dropped } = await compileSlots(spec.samples, KIT_MAX_SAMPLES, compile, "LSDj kit");
  const samples = results.map((r, i) => ({ name: spec.samples[i].name, bytes: r.bytes }));
  const { bank, written } = buildKitBankPlaced(spec.name, samples);
  const errors = results.map((r, i): string | null => {
    if (r.error !== null) return r.error;
    const n = r.bytes.length;
    const landed = written[i];
    if (landed === null)
      return `the kit's ${KIT_MAX_SAMPLE_SPACE} bytes of sample space are full, so none of this sample's ${n} ` +
        "bytes fit";
    if (landed < n) return `the kit's sample space ran out, so ${landed} of this sample's ${n} bytes fit`;
    return null;
  });
  return { bytes: bank, errors: [...errors, ...dropped] };
}

/** A risa DMC kit: the 8 KB bank native compileDmc builds from `spec` (its 16 slots, the rest dropped), and each
 *  sample's reason. */
export async function compileDmc(runner: KitJobRunner, spec: RisaKitCompileSpec): Promise<KitCompileResult> {
  const compile = (s: RisaDmcSampleSpec): Promise<Compiled> => dmcSlot(runner, s);
  const { results, dropped } = await compileSlots(spec.samples, KIT_SLOT_COUNT, compile, "risa kit");
  const slots = results.map((r, i): AssembleSlot | null => {
    const s = spec.samples[i];
    return r.bytes.length ? { dpcm: r.bytes, rate: (s.rate ?? 12) & 0x0f, loop: s.loop ?? false, name: s.name } : null;
  });
  const { bank, placed } = assembleKitBankPlaced(spec.name, slots);
  const errors = results.map((r, i): string | null => {
    if (r.error !== null) return r.error;
    if (placed[i]) return null;
    return `this sample's ${r.bytes.length} bytes do not fit the ${KIT_SAMPLE_REGION}-byte sample region after the ` +
      "slots before it";
  });
  return { bytes: bank, errors: [...errors, ...dropped] };
}
````

Run: `mkdir -p packages/retroplug/src/kit; sed -n '8747,8986p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/src/kit/compile.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/kit/compile.ts`
Expected: `1529124057aedf066857b53bc3e8bb57cbb52b4a`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/src/kit/index.ts`:

````ts
// The kit compilers as offline graph jobs (docs/modular-graph-phase-d2-design.md section 5): compile.ts runs a kit's
// slots, chain.ts builds each pass's document, pack.ts packs what the passes recorded.
export { compileKit, compileDmc, type KitCompileResult, type KitJobRunner } from "./compile";
export { KitSpecError } from "./chain";
````

Run: `mkdir -p packages/retroplug/src/kit; sed -n '8998,9001p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/src/kit/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/kit/index.ts`
Expected: `a593c933fb5f3ec83fc2494a438b7e080b160980`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/run-native-tests.mjs b/packages/retroplug/scripts/run-native-tests.mjs
index c6570eacc..059707708 100644
--- a/packages/retroplug/scripts/run-native-tests.mjs
+++ b/packages/retroplug/scripts/run-native-tests.mjs
@@ -102,6 +102,15 @@ async function runOne({ file, slug }) {
         // The parity suite's baselines live beside its cases; a recording re-runs its own bundle per case.
         __PARITY_DIR__: JSON.stringify(join(TEST_DIR, "parity").replaceAll("\\", "/")),
         __SELF_BUNDLE__: JSON.stringify(outFile.replaceAll("\\", "/")),
+        // The kit compilers' byte baseline, shared with retroplug-audio-test's "[kit-baseline]" (one record, two
+        // compilers held to it); kit-baseline.test.ts reads it and, under RP_KIT_GOLDEN_UPDATE=1, records it.
+        __KIT_GOLDEN__: JSON.stringify(
+          join(REPO, "packages/native/test/audio/golden/kit-baseline.json").replaceAll("\\", "/"),
+        ),
+        // The edge banks kit-compile.test.ts holds the compiler to, recorded beside the baseline under its rules.
+        __KIT_EDGES_GOLDEN__: JSON.stringify(
+          join(REPO, "packages/native/test/audio/golden/kit-edges.json").replaceAll("\\", "/"),
+        ),
       },
     });
   } catch (e) {
diff --git a/packages/retroplug/src/lsdj/rom/buildKit.ts b/packages/retroplug/src/lsdj/rom/buildKit.ts
index 19eecd76f..7bf39432f 100644
--- a/packages/retroplug/src/lsdj/rom/buildKit.ts
+++ b/packages/retroplug/src/lsdj/rom/buildKit.ts
@@ -1,7 +1,7 @@
-// Pure-TS kit-bank assembly — the port of native KitUtil::buildKit, used to SPLICE a kit (single-sample
-// import / remove) without re-encoding untouched samples: read existing samples as raw nibble bytes
-// (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank here. Whole-kit builds go through the
-// native compileKit instead (it does resample + assembly in parallel). Layout matches kit.ts / KitUtil:
+// Pure-TS kit-bank assembly: the port of native KitUtil::buildKit, byte for byte, used to SPLICE a kit
+// (single-sample import / remove) without re-encoding untouched samples: read existing samples as raw nibble
+// bytes (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank here. The TypeScript kit compiler
+// (src/kit/) assembles its banks here too. Layout matches kit.ts / KitUtil:
 // a 16-entry u16le offset table (entry 0 = 0x4060; entries i+1 = each sample's end GB-addr; 0 terminates),
 // 15 × 3-char sample names at 0x22, a 6-char kit name at 0x52, sample data from 0x60 (≤ 0x3fa0 bytes).
 import {
@@ -21,12 +21,18 @@ export interface KitSample {
   bytes: Uint8Array; // raw 4-bit nibble stream (already frame-aligned by the compiler)
 }
 
-// Write a fixed-width name: uppercase ASCII, truncated to `size`, padded with `fill` to the end.
+const utf8 = new TextEncoder();
+
+// Write a fixed-width name as native KitUtil::buildKit's writeString does: the name's bytes (its UTF-8, which is what
+// a native string of it holds), each a-z uppercased and every other byte as it is, truncated to `size` and padded with
+// `fill`. Byte for byte, so this assembler and native's give one bank for one name: a name of printable ASCII (all a
+// kit read back from a ROM can have, KitView's readName) is the same under any rule, and for anything else native is
+// the reference the kit compiler is held to, so its bytes are copied rather than "corrected" here.
 function writeField(bank: Uint8Array, off: number, size: number, name: string, fill: number): void {
-  const up = name.toUpperCase();
+  const bytes = utf8.encode(name);
   for (let i = 0; i < size; i++) {
-    const c = i < up.length ? up.charCodeAt(i) : fill;
-    bank[off + i] = c >= 0x20 && c <= 0x7e ? c : fill;
+    const c = i < bytes.length ? bytes[i] : fill;
+    bank[off + i] = c >= 0x61 && c <= 0x7a ? c - 0x20 : c;
   }
 }
 
@@ -35,10 +41,25 @@ export function kitSampleSpace(samples: readonly KitSample[]): number {
   return samples.slice(0, KIT_MAX_SAMPLES).reduce((n, s) => n + s.bytes.length, 0);
 }
 
+/** A bank and where each sample landed in it: `written[i]` is how many of sample i's bytes the bank holds (fewer
+ *  than it has when the 0x3fa0-byte budget ran out part way through it), or null for a sample that found no room at
+ *  all and got the empty-slot marker. A sample past the 15 slots is not in `written`. */
+export interface PlacedKitBank {
+  bank: Uint8Array;
+  written: (number | null)[];
+}
+
 /** Assemble a 16 KB kit bank from a kit name + samples (≤15 honored). Sample data past the 0x3fa0 budget
  *  is clipped (matches native KitUtil::buildKit) — callers pre-check with kitSampleSpace to fail loudly. */
 export function buildKitBank(kitName: string, samples: readonly KitSample[]): Uint8Array {
+  return buildKitBankPlaced(kitName, samples).bank;
+}
+
+/** buildKitBank, saying where each sample landed: the kit compiler's per-slot reason for a sample the bank does not
+ *  hold whole comes from here, so it cannot drift from the assembler's own fit rule. */
+export function buildKitBankPlaced(kitName: string, samples: readonly KitSample[]): PlacedKitBank {
   const bank = new Uint8Array(BANK_SIZE);
+  const written: (number | null)[] = [];
   writeField(bank, KIT_NAME_OFFSET, KIT_NAME_SIZE, kitName, 0x20); // kit name, space-padded
 
   const setEntry = (i: number, gbAddr: number): void => {
@@ -59,14 +80,16 @@ export function buildKitBank(kitName: string, samples: readonly KitSample[]): Ui
       bank.set(src.subarray(0, writeSize), KIT_SAMPLE_DATA_OFFSET + cursor);
       cursor += writeSize;
       setEntry(i + 1, KIT_VALID_MAGIC + cursor); // end GB-addr
+      written.push(writeSize);
     } else {
       bank[nameOff] = 0; // empty-slot sentinel: name[0]=0, then '--'
       bank[nameOff + 1] = 0x2d;
       bank[nameOff + 2] = 0x2d;
       setEntry(i + 1, 0);
+      if (i < samples.length) written.push(null);
     }
   }
-  return bank;
+  return { bank, written };
 }
 
 /** Extract sample `i`'s raw nibble bytes from a standalone 16 KB bank (base 0) — e.g. a 1-sample bank
diff --git a/packages/retroplug/src/lsdj/rom/index.ts b/packages/retroplug/src/lsdj/rom/index.ts
index 0760fe5f2..26f30169b 100644
--- a/packages/retroplug/src/lsdj/rom/index.ts
+++ b/packages/retroplug/src/lsdj/rom/index.ts
@@ -5,7 +5,14 @@ export * from "./types";
 export * from "./constants";
 export { LsdjRom } from "./rom";
 export { KitView, decodeNibbles } from "./kit";
-export { buildKitBank, sampleBytesFromBank, kitSampleSpace, type KitSample } from "./buildKit";
+export {
+  buildKitBank,
+  buildKitBankPlaced,
+  sampleBytesFromBank,
+  kitSampleSpace,
+  type KitSample,
+  type PlacedKitBank,
+} from "./buildKit";
 export { PaletteView, findPaletteBase, unpackRgb555, packRgb555, decodeLsdpal, encodeLsdpal } from "./palette";
 export { FontView, findFontBase } from "./font";
 export { findGrayscaleNames, paletteCount, paletteNames, fontNames, setPaletteName, setFontName } from "./names";
diff --git a/packages/retroplug/src/risa/rom/kit.ts b/packages/retroplug/src/risa/rom/kit.ts
index f789986a9..5aabb3e22 100644
--- a/packages/retroplug/src/risa/rom/kit.ts
+++ b/packages/retroplug/src/risa/rom/kit.ts
@@ -1,5 +1,6 @@
-// risa DPCM kit-bank codec (read side) + the metadata-mirror derivation + the DMC decoder. The 8 KB kit
-// bank is produced natively (audioDriver.compileDmc); this reads one back (bankToModel), derives the
+// risa DPCM kit-bank codec (read side) + the metadata-mirror derivation + the DMC decoder + the bank
+// assembler. The 8 KB kit bank is produced natively (audioDriver.compileDmc) or by the TypeScript kit
+// compiler (src/kit/, through assembleKitBank); this reads one back (bankToModel), derives the
 // resident-mirror rows from a bank's own bytes (deriveMetaFromBank — the model-free path from risa's
 // rom_upgrade.js), and decodes a DPCM stream to audio (dpcmDecode) for round-trip verification. Ported
 // from risa's tools/rom_patcher/src/{kit_bank_parser.js, kit_editor/decoder.js, rom_upgrade.js}.
@@ -108,12 +109,20 @@ export interface AssembleSlot {
   name: string; // 3-char sample name
 }
 
+const utf8 = new TextEncoder();
+
+// A name's bytes as native holds them (its UTF-8), with a-z uppercased and every other byte left as it is: the
+// ASCII-only uppercase RisaDmcCodec applies, byte by byte, so one name gives native's bank bytes here.
+function upperAsciiBytes(name: string): Uint8Array {
+  const bytes = utf8.encode(name);
+  for (let i = 0; i < bytes.length; i++) if (bytes[i] >= 0x61 && bytes[i] <= 0x7a) bytes[i] -= 0x20;
+  return bytes;
+}
+
 // A 3-char, uppercased, right-space-padded sample name (native RisaDmcCodec writeSampleName).
 function writeSampleName(bank: Uint8Array, off: number, name: string): void {
-  for (let i = 0; i < KIT_SAMPLE_NAME_LEN; i++) {
-    const c = (i < name.length ? name[i] : " ").toUpperCase();
-    bank[off + i] = c.charCodeAt(0) & 0xff;
-  }
+  const bytes = upperAsciiBytes(name);
+  for (let i = 0; i < KIT_SAMPLE_NAME_LEN; i++) bank[off + i] = i < bytes.length ? bytes[i] : 0x20;
 }
 
 /** Pack DPCM slots into an 8 KB kit bank — the TS port of native `RisaDmcCodec::assemble`. Greedy 64-byte-
@@ -123,6 +132,20 @@ function writeSampleName(bank: Uint8Array, off: number, name: string): void {
  *  the native per-sample encode carries no cross-sample state, this is byte-identical to compiling the same
  *  samples together via compileDmc — so it losslessly re-packs existing slots' DPCM alongside a new one. */
 export function assembleKitBank(kitName: string, slots: (AssembleSlot | null)[]): Uint8Array {
+  return assembleKitBankPlaced(kitName, slots).bank;
+}
+
+/** A bank and which slots landed in it: `placed[i]` is false for a slot given no bytes (null or empty) and for one
+ *  that did not fit the sample region after the slots before it. Only the first KIT_SLOT_COUNT slots are listed. */
+export interface PlacedRisaKitBank {
+  bank: Uint8Array;
+  placed: boolean[];
+}
+
+/** assembleKitBank, saying which slots landed: the kit compiler's per-slot reason for a slot that did not fit comes
+ *  from here, so it cannot drift from the assembler's own fit rule. */
+export function assembleKitBankPlaced(kitName: string, slots: (AssembleSlot | null)[]): PlacedRisaKitBank {
+  const placed: boolean[] = [];
   const bank = new Uint8Array(KIT_BANK_SIZE);
   // Sample-name region defaults to spaces (kit_bank_parser modelToBank).
   bank.fill(0x20, KIT_SAMPLE_NAMES, KIT_SAMPLE_NAMES + KIT_SLOT_COUNT * KIT_SAMPLE_NAME_LEN);
@@ -133,10 +156,12 @@ export function assembleKitBank(kitName: string, slots: (AssembleSlot | null)[])
     bank[idx] = KIT_SLOT_EMPTY; // default: empty entry [0xFF, 0, 0, 0]
 
     const s = slot < slots.length ? slots[slot] : null;
+    if (slot < slots.length) placed.push(false);
     if (!s || s.dpcm.length === 0) continue;
 
     if (cursor % SAMPLE_ALIGN) cursor += SAMPLE_ALIGN - (cursor % SAMPLE_ALIGN);
     if (cursor + s.dpcm.length > KIT_SAMPLE_REGION) continue; // doesn't fit — leave the slot empty
+    placed[slot] = true;
 
     bank.set(s.dpcm, cursor);
     bank[idx] = Math.floor(cursor / SAMPLE_ALIGN) & 0xff; // $4012 address
@@ -147,17 +172,17 @@ export function assembleKitBank(kitName: string, slots: (AssembleSlot | null)[])
     cursor += s.dpcm.length;
   }
 
-  // Kit name: uppercased, keep A-Z 0-9 '-', up to 6 chars, NUL-padded to KIT_NAME_SIZE.
+  // Kit name: uppercased, keep A-Z 0-9 '-', up to 6 chars, NUL-padded to KIT_NAME_SIZE. Byte by byte over the
+  // name's UTF-8, as native filters it, so a non-ASCII character is dropped whole rather than uppercased into a letter.
   let w = 0;
-  for (const ch of kitName) {
+  for (const c of upperAsciiBytes(kitName)) {
     if (w >= KIT_NAME_MAX_CHARS) break;
-    const c = ch.toUpperCase();
-    if (/[A-Z0-9-]/.test(c)) bank[KIT_NAME_OFFSET + w++] = c.charCodeAt(0);
+    if ((c >= 0x41 && c <= 0x5a) || (c >= 0x30 && c <= 0x39) || c === 0x2d) bank[KIT_NAME_OFFSET + w++] = c;
   }
   void KIT_NAME_SIZE;
 
   bank[KIT_MAGIC_OFFSET] = KIT_MAGIC; // populated marker, stamped last
-  return bank;
+  return { bank, placed };
 }
 
 /** Decode a DPCM byte stream to float32 audio in ~[-1, 1] — the exact inverse of the native ±2 delta
````

Run: `sed -n '9011,9228p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`; 3 cases pass: the headline of this task, on the first run, with nothing re-recorded.

Run: `pnpm test kit`
Expected: 18 passed: the kit file alone.

- [ ] **Step 5: Record the edge golden, and make CI require it**

`kit-edges.json` is 12 banks of `{hash, size}` (no slot: `kit-compile` refuses slots by design), 885 bytes over 17
lines, so it is quoted whole. It is recorded once on this host from the TypeScript compiler, in a run where every bank
also equalled native's; the recorder writes only when all eight edge cases ran to their end, refuses off the target
and for a disagreeing bank, and a final case requires the recorded names to equal the golden's keys exactly. CI's
linux-x64 `test:native` step sets `RP_KIT_GOLDEN_REQUIRE_TARGET: "1"` beside `RP_PARITY_REQUIRE_TARGET`, so the
off-target self-agreement fallback cannot pass there. The Catch2 step keeps its own flag until Task 6 drops it with
the suite it guarded.

Create `packages/native/test/audio/golden/kit-edges.json`:

````json
{
  "target": "linux-x64-gnu13",
  "cases": {
    "lsdj/defaults": { "hash": "d7675b3b7dce86e9", "size": 16384 },
    "lsdj/edges-rotate-off": { "hash": "d4b4cc0f98d4ed5e", "size": 16384 },
    "lsdj/edges-rotate-on": { "hash": "3f961c8916423483", "size": 16384 },
    "lsdj/full": { "hash": "a372b194ae8e3845", "size": 16384 },
    "lsdj/overflow": { "hash": "f073fc123cb93d36", "size": 16384 },
    "lsdj/refused": { "hash": "a85b8e9f069a5fb0", "size": 16384 },
    "lsdj/sixteen": { "hash": "d02047595046f2c6", "size": 16384 },
    "risa/defaults": { "hash": "39395a29c0efcc37", "size": 8192 },
    "risa/edges": { "hash": "8ce2632d43b623da", "size": 8192 },
    "risa/refusal-over-empty-window": { "hash": "7bc0c884920bafc4", "size": 8192 },
    "risa/refused": { "hash": "7ae41ed559b334c7", "size": 8192 },
    "risa/seventeen": { "hash": "da1afe7f0ef6060c", "size": 8192 }
  }
}
````

Run: `mkdir -p packages/native/test/audio/golden; sed -n '9253,9269p' docs/modular-graph-phase-d2-plan.md > packages/native/test/audio/golden/kit-edges.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/audio/golden/kit-edges.json`
Expected: `5097534eddd1b9b43106c8fca2c8bd5a263c605f`: the blob 7b2d5bdbb holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index cd9f013bb..eb326cc02 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -81,9 +81,12 @@ jobs:
         run: pnpm test
 
       - name: Native tests (real host + emulator cores)
-        # This job is the parity baseline's target: exact audio checking must run here, not fall back.
+        # This job is the parity baseline's target: exact audio checking must run here, not fall back. It is the kit
+        # baselines' target too: test-native's kit-baseline and kit-compile compare exact bank bytes only on the host
+        # that recorded them, so this job demands that it is that host rather than falling back to self-agreement.
         env:
           RP_PARITY_REQUIRE_TARGET: "1"
+          RP_KIT_GOLDEN_REQUIRE_TARGET: "1"
         run: pnpm test:native
 
       - name: Plugin unit tests (Catch2)
````

Run: `sed -n '9279,9296p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RP_KIT_GOLDEN_UPDATE=1 pnpm test:native kit-compile && git add -- packages/native/test/audio/golden/kit-edges.json && git diff --cached --stat 7b2d5bdbb -- packages/native/test/audio/golden/kit-edges.json`
Expected: the ninth case, `write kit-edges.json`, prints `# recorded 12 banks to packages/native/test/audio/golden/kit-edges.json`, and the run ends `# 9 passed, 0 skipped, 0 failed` (`s4f-edges-record.log`); then nothing printed by the staged diff: the recorder rewrote the file and it is the prototype's bytes.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, 3 passed, the target requirement turned into a failure mode.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed: the same banks, with a foreign host now a failure.

- [ ] **Step 6: Show that the baseline and the edge golden bite**

The compiler was written after its tests, and both goldens matched on the first run, so their bite is shown by a
mutation: every quantise pass runs `none`, whatever dither the spec named. It is applied, run, reversed.

````patch
diff --git a/packages/retroplug/src/kit/compile.ts b/packages/retroplug/src/kit/compile.ts
--- a/packages/retroplug/src/kit/compile.ts
+++ b/packages/retroplug/src/kit/compile.ts
@@ -160,5 +160,5 @@
   const effected = await runEffects(runner, source, chain);
   const resampled = await recorded(runner, resampleJob(effected, source.rate, LSDJ_KIT_RATE, length), "the resample");
-  const quantise = quantiseJob(resampled, LSDJ_KIT_RATE, chain.dither ?? "none", LSDJ_KIT_BITS);
+  const quantise = quantiseJob(resampled, LSDJ_KIT_RATE, "none", LSDJ_KIT_BITS);
   const levels = await recorded(runner, quantise, "the quantise");
   return { bytes: packNibbles(levels, rotate), empty: null };
````

Run: `sed -n '9317,9326p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the mutation applies cleanly to the tree Step 4 placed.

Run: `pnpm test:native kit-compile`
Expected: the edge cases that carry a dither fail on their hashes. The report's fix pass puts it as forcing the quantise to `none` fails `lsdj/refused`, `lsdj/edges-rotate-on` and `lsdj/overflow` on their hashes. Report the summary line.

Run: `pnpm test:native kit-baseline`
Expected: the baseline's dither cases fail too; the report's first pass recorded case 3's message as expected `expected "994b994a43e8b526", got "62335f096ad12a03"`. Report this tree's hashes.

Run: `sed -n '9317,9326p' docs/modular-graph-phase-d2-plan.md | git apply -R`
Expected: nothing: the mutation is reversed and Step 4's compiler is back.

Run: `pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes` and 3 passed again, the restored tree.

- [ ] **Step 7: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, with the plugin and `retroplug-sdl` included.

Run: `build/bin/retroplug-engine-graph-test "[offline]"`
Expected: `All tests passed (29182 assertions in 14 test cases)`.

Run: `pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`; 3 passed, 0 skipped, 0 failed.

Run: `pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed, against `kit-edges.json` and native's banks.

Run: `pnpm test:native offline-run`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed`; `retroplug-graph-test` `graph 356087 / 272`; `retroplug-audio-test` `audio 1235263 / 109`; `retroplug-engine-graph-test` `282 / 273 passed / 9 skipped`; launchpad 144 / 29.

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (428 assertions in 1 test case)`, unchanged: the C++ suite still watches the same file until Task 6 retires it with the compiler.

Run: `pnpm test`
Expected: `# 171 test file(s) passed`; kit alone 18 passed.

Run: `pnpm test:ui`
Expected: the 39 files the baseline lists pass: stage 4 ran no `test:ui`, so report this run's line rather than a quoted count.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, nothing skipped, nothing re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 163 native test file(s) passed`; only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: exit 0, 163 native test files passed; `kit-baseline` and `kit-compile` skip nothing and need no `skip-baseline.json` entry, and the skip check passes.

Run: `pnpm test:node`
Expected: `pass 12, fail 0`: the offline wire gained `nonFinite` and then `guardsFired` / `guardReason`.

Run: `pnpm test:cli-ts`
Expected: `PASS`.

Run: `pnpm typecheck`
Expected: clean: exit 0.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 8: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[offline]"`
Expected: `[offline]` 29182 / 14, 0 ThreadSanitizer reports.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: `[dsp-nodes]` 35217 / 18, 0 reports.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[offline]"`
Expected: 29182 assertions in 14 test cases, 0 reports.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[dsp-nodes]"`
Expected: 35217 assertions in 18 test cases, 0 Address/LeakSanitizer reports.

- [ ] **Step 9: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M .github/workflows/build.yml
 m deps/sameboy
 M packages/native/src/graph/nodes/DspNodes.cpp
 M packages/native/src/graph/rp_node.h
 M packages/native/src/host/engine/Engine.hpp
 M packages/native/src/host/engine/OfflineJobs.cpp
 M packages/native/src/host/engine/OfflineJobs.hpp
 M packages/native/src/host/rpc/BackendTypes.hpp
 M packages/native/src/host/rpc/EngineRpcService.cpp
A  packages/native/test/audio/golden/kit-edges.json
 M packages/native/test/engine-graph/OfflineJobs.test.cpp
 M packages/retroplug/scripts/run-native-tests.mjs
?? packages/retroplug/src/kit/chain.ts
?? packages/retroplug/src/kit/compile.ts
?? packages/retroplug/src/kit/index.ts
?? packages/retroplug/src/kit/pack.ts
 M packages/retroplug/src/lsdj/rom/buildKit.ts
 M packages/retroplug/src/lsdj/rom/index.ts
 M packages/retroplug/src/offline.ts
 M packages/retroplug/src/risa/rom/kit.ts
?? packages/retroplug/test-native/kit-baseline.test.ts
?? packages/retroplug/test-native/kit-compile.test.ts
?? packages/retroplug/test-native/kit/sources.ts
?? packages/retroplug/test/kit/kit.test.ts
 M packages/retroplug/test/offline/offline.test.ts
````

````bash
git add -- packages/native/test/audio/golden/kit-edges.json packages/retroplug/src/kit/chain.ts \
  packages/retroplug/src/kit/compile.ts packages/retroplug/src/kit/index.ts packages/retroplug/src/kit/pack.ts \
  packages/retroplug/test-native/kit-baseline.test.ts packages/retroplug/test-native/kit-compile.test.ts \
  packages/retroplug/test-native/kit/sources.ts packages/retroplug/test/kit/kit.test.ts .github/workflows/build.yml \
  packages/native/src/graph/nodes/DspNodes.cpp packages/native/src/graph/rp_node.h \
  packages/native/src/host/engine/Engine.hpp packages/native/src/host/engine/OfflineJobs.cpp \
  packages/native/src/host/engine/OfflineJobs.hpp packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/test/engine-graph/OfflineJobs.test.cpp \
  packages/retroplug/scripts/run-native-tests.mjs packages/retroplug/src/lsdj/rom/buildKit.ts \
  packages/retroplug/src/lsdj/rom/index.ts packages/retroplug/src/offline.ts packages/retroplug/src/risa/rom/kit.ts \
  packages/retroplug/test/offline/offline.test.ts
git commit -F - <<'EOF'
feat(kit): the kit compilers in TypeScript, as chains of offline graph jobs

compileKit and compileDmc (packages/retroplug/src/kit/) compile an LSDj kit and a risa DMC kit from the spec shapes
the native RPCs take, on a backend's offline jobs: one chain of jobs per slot, every slot's chain started at once so
the engine's offline pool runs them in parallel. Native decodes, windows and does every float the bytes depend on;
TypeScript builds each pass's document, cuts the buffers to their lengths, packs and assembles the bank. The result is
the bank and a reason per spec sample, null exactly when the bank holds that sample whole. No caller moves: the CLI
sessions, audioDriver and the harness RPCs stay on native KitCompiler.

A slot is: a load (the file decoded and windowed natively, the LSDj or risa cap form); its effects at the source rate,
one pass per stretch of the list cut at each normalising gain (rp.dsp.normalize or a buffer player, then the gains
and biquads in list order, into a recorder); rp.dsp.resample to the kit rate run for exactly resampledLength frames;
then rp.dsp.dither at 4 bits seeded from the whole resampled buffer (seedFrom), or rp.dsp.dpcm for risa. The levels
invert to codes (round((level + 1) * 0.5 * 15), exact for all 16), trim to whole wave frames and pack with rotate;
the DMC levels' signs pack LSB first, pad to 16k + 1 and cut at 4081 bytes.

A slot that fails is left empty, as native leaves a slot whose file fails to load, with the reason: a source that
cannot be read, a window with a sample that is not finite, a spec the nodes cannot carry, a pass that records a sample
that is not finite, or a job a node's guard failed. Each spec value goes into a document as its binary32 value and is
checked as the planner clamps: a plain gain outside rp.dsp.gain's range, a filter value outside rp.dsp.biquad's or a
filter at or above the source's Nyquist frequency is refused; a normalising gain is a setting no range clamps and is
refused only when not finite. A field the spec leaves out takes the native struct's default (native's RPC refuses the
whole kit instead). What native makes of an empty stage is reproduced without a job: an empty window or one that
resamples to under one wave frame is an LSDj slot of no bytes, and one that resamples to nothing is risa's {0}. Every
slot the bank does not hold whole has a reason: those empty LSDj slots, a sample the bank's space ran out on (clipped,
or no room), and a risa sample that does not fit; the assemblers report their placements (buildKitBankPlaced,
assembleKitBankPlaced) so the reasons follow their own fit rules.

A finite source can still overflow in the chain, so a guard of rp.dsp.biquad or rp.dsp.dither that replaces what its
kernel computes now reports itself through a new host service, rp.host.guards/1 (rp_node.h), taken in bind_host. An
offline job's host offers it and counts the reports into the completion (guardsFired and the first reason, on an ok
result); a live host offers none (the engine answers null, checked) and the guards behave as before. A non-finite value
no guard touches is in the pass's recording, which each returned offline buffer flags (nonFinite, read natively as it
is copied out). Native reports both and the caller decides: the kit compiler refuses a pass on either, naming the pass
and the guard. Between them, every kit that compiles is byte-identical to the kit kernels.

Each offline job also runs in the default floating-point environment (round to nearest, subnormals kept: fesetenv
plus MXCSR FTZ/DAZ or FPCR FZ/FZ16 cleared), restored after: a worker inherits its submitter's environment, an audio
host's thread may flush subnormals, and the kit's floats are the kernels' in the default mode.

buildKitBank and assembleKitBank write names as native does, byte for byte over the name's UTF-8 with only a-z
uppercased; the two already agreed with native for every printable-ASCII name, which is every baseline case and every
name a kit read back from a ROM can have.

test-native/kit-baseline runs the TypeScript compiler over KitBaseline.test.cpp's 39-case matrix, transcribed with its
sources (held to the C++ suite's floats by FNV-1a-64 on the target, and decoded back through a job everywhere), against
kit-baseline.json under the native suite's rules, with the same re-record mode and refusals; all 39 match on
linux-x64-gnu13. test-native/kit-compile holds refusals, overflows and every compileSample and DMC edge to native's
banks and to a new sibling golden, kit-edges.json, recorded on the target under the same rules, so the edges stay
pinned when native retires. CI's test:native step now sets RP_KIT_GOLDEN_REQUIRE_TARGET. test/kit covers the pure
parts, the refusals' messages, the segment split and documents, the range constants against the descriptor goldens,
the assemblers' names and placements, and a slot's chain over a scripted runner; "[offline]" holds the guard count,
the buffer flag, the engine's null for the guard service, and a job submitted from a flush-to-zero thread.
EOF
````

---

### Task 5: every kit compile runs as offline graph jobs

Design section 7's fifth commit and section 10's fifth question. `audioDriver.compileKit` / `compileDmc` run `src/kit`
over the runtime's one offline queue, so the CLI's sessions, the render worker's session path and any host that makes
an audio driver compile through the jobs; `createAudioDriver` turns the poll's native wait on, which settles a compile
with no UI tick, while the plugin (Task 4's test) settles it from the frame tick. `runSession` and `CliTool.run` accept
a promise, a slot's own reason reaches the CLI's read-back check, a clipped slot with bytes warns, and the binary's
async exit path is pinned by `test:cli-ts`. This stage also fixes a latent CLI bug: `--filter` built a filter with no
`gain`, and the old RPC's decode had no defaults, so every `--filter` kit threw `Field named 'gain' not found`; the
TypeScript compiler fills the 0 dB default and the flag compiles now.

The prototype also compared seven by-hand sessions against native's compiler through a scratch `.js` session and
`cmp` (build-kit, `--filter`, risa, BlipToaster, and three import-sample-then-export cases), all identical, and read
the missing-source reason out of the real binary. Those scratch scripts and WAVs live only in `/tmp/d2pro/s5-hand`,
so they are evidence, not steps: the reproducible checks are `test-native`'s kit files, `test:ui`'s plugin compile and
`test:cli-ts`'s binary blocks.

**Files:**

- Create: `packages/retroplug/test-ui/kit-compile.test.ts`
- Modify: `packages/retroplug/cli/session.ts`, `packages/retroplug/cli/sessions/bliptoaster-rom.ts`, `packages/retroplug/cli/sessions/lsdj-rom.ts`, `packages/retroplug/cli/sessions/risa-rom.ts`, `packages/retroplug/cli/tools.ts`, `packages/retroplug/src/audioDriver.ts`, `packages/retroplug/src/kit/compile.ts`, `packages/retroplug/src/lsdj/rom/buildKit.ts`, `packages/retroplug/src/lsdj/rom/rom.ts`, `packages/retroplug/src/realBackend.ts`, `packages/retroplug/src/risa/rom/kit.ts`, `packages/retroplug/src/risaAssetsRole.ts`, `packages/retroplug/src/systemRoles.ts`, `packages/retroplug/test-native/bliptoaster-rom-cli.test.ts`, `packages/retroplug/test-native/kit-compile.test.ts`, `packages/retroplug/test-native/kit-dither.test.ts`, `packages/retroplug/test-native/lsdj-rom.test.ts`, `packages/retroplug/test-native/risa-kit.test.ts`, `packages/retroplug/test-native/risa-rom-cli.test.ts`, `packages/retroplug/ui/screens/menu/menuDefs.ts`, `tools/run-cli-ts-tests.sh`

- [ ] **Step 1: Write the failing CLI cases**

`test-native/lsdj-rom.test.ts` gains one case: a missing source through `lsdjRomTool.run` must throw with the slot's
own reason (`sample <i> (<file>) did not compile: ...`), where the old read-back check knew no reason. The other kit
suites await the compiles and the kit verbs, every existing assertion kept: `risa-kit`, `kit-dither`, `risa-rom-cli`
and `bliptoaster-rom-cli`. `kit-compile.test.ts` keeps its native comparison by calling the two harness RPCs directly
through `makeCall`, since the audio driver's `compileKit` is about to run TypeScript. New
`test-ui/kit-compile.test.ts` is the plugin's compile: writes a WAV, asserts the two harness RPCs and `renderAudio`
are not bound on the UI test host, runs `src/kit`'s `compileKit` over `createRealBackend()` (no audio driver), proves
a microtask-only stretch settles nothing, then settles it from `ui.pump` + `backend.pumpJobs` alone and reads the slot
back through `KitView`. `tools/run-cli-ts-tests.sh` gains a block that writes a canonical WAV and drives the real
binary: `build-kit` exits 0 only once the 16384-byte bank is on disk, and a missing source exits 1 with `did not
compile` on stderr.

Create `packages/retroplug/test-ui/kit-compile.test.ts`:

````ts
// The plugin compiles a kit (docs/modular-graph-phase-d2-design.md section 6's "New"): the TypeScript kit compiler
// runs end to end over the facet set the PLUGIN binds, with no harness facet behind it, and over the settle path the
// plugin has and no other.
//
// The harness this suite runs on (packages/native/test/ui/UiHarness.cpp) mounts exactly what
// packages/native/plugin/PluginDSP.cpp does - registerHostRpc + registerEmulatorRpc, nothing else - so it is the
// narrowest honest host for this: the CLI and the native test host also mount the harness facet, and a Catch2 case
// composing the plugin's registration could not run TypeScript at all. The offline jobs the compile is made of ride
// the emulator facet (submitOfflineJob / drainOfflineJobs), which is why the plugin can compile a kit at all; the two
// harness RPCs that compile a kit on the harness hosts (compileKit / compileDmc) are checked to be absent, since a
// compile that needed them would pass here only by accident of some other host's registration.
//
// The compile is src/kit's entry point over the real backend, never an audio driver: making a driver turns the
// offline poll's native wait on (src/offline.ts), and that is the settle path of a host with NO UI tick. The plugin
// has a tick, and its only mover of these promises is useJobPump's backend.pumpJobs on a frame. So the promise below
// is settled by ui.pump + be.pumpJobs alone, and the microtask-only stretch before that loop is what proves no
// native wait is running underneath it.
import { test, expect, ui } from "ui-harness";
import { createRealBackend } from "../src/realBackend";
import { makeCall } from "../src/rpcClient";
import { compileKit, type KitCompileResult } from "../src/kit";
import { encodeWav } from "../cli/wav";
import { KitView } from "../src/lsdj/rom/kit";
import { BANK_SIZE, KIT_LOOKUP, ROM_SIZE } from "../src/lsdj/rom/constants";

test("the plugin's facet set compiles a kit, with no harness facet bound", async () => {
  expect(ui.boot()).toBeTruthy();
  const be = createRealBackend();
  const call = makeCall();
  const wav = ui.romDir() + "/rp-plugin-kit-src.wav";

  // A decaying 220 Hz tone: a real sample, so the slot's bytes cannot be an empty slot's.
  const frames = 6000;
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 220) * Math.exp(-i / 3000);
  expect(be.writeFile(wav, encodeWav(pcm, 44100, 1))).toBeTruthy();

  // The harness facet is NOT bound on this host: the RPCs the harness hosts compile a kit through are refused, and
  // the render harness beside them is too.
  expect(() => call("compileKit", { name: "", samples: [] })).toThrow();
  expect(() => call("compileDmc", { name: "", samples: [] })).toThrow();
  expect(() => call("renderAudio", 1)).toThrow();

  let result: KitCompileResult | null = null;
  let failure: unknown = null;
  let settled = false;
  void compileKit(be, {
    name: "PLUG",
    samples: [{ path: wav, name: "SIN", effects: [{ type: "dither", ditherType: "HighPassTPDF" }] }],
  }).then(
    (r) => {
      result = r;
      settled = true;
    },
    (e) => {
      failure = e;
      settled = true;
    },
  );

  // No pump yet: microtasks alone must settle nothing. A native wait in the poll would settle the compile here,
  // which is exactly the settle path the plugin does not have.
  for (let i = 0; i < 16; i++) await Promise.resolve();
  expect(settled).toBe(false);

  // The plugin's frame tick: pump the jobs, let the settled promises queue their next step, repeat. Bounded, so a
  // compile that cannot finish without a native wait fails here instead of hanging.
  let steps = 0;
  while (!settled && steps < 2000) {
    ui.pump();
    be.pumpJobs();
    await Promise.resolve();
    steps++;
  }
  expect(settled).toBe(true);
  if (failure !== null) throw failure;
  const { bytes, errors } = result!;

  // The bank is whole, the slot landed, and it decodes to a varied sample in both polarities (LSDj stores inverted
  // nibbles, so an empty data region would decode to a constant +1.0 and satisfy a peak alone).
  expect(bytes.length).toBe(BANK_SIZE);
  expect(errors[0]).toBe(null);
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bytes, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.name()).toBe("PLUG");
  expect(kit.sampleName(0)).toBe("SIN");
  let lo = Infinity;
  let hi = -Infinity;
  for (const v of kit.sampleData(0)) {
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  expect(hi - lo).toBeGreaterThan(0.5);
  expect(lo).toBeLessThan(0);
  expect(hi).toBeGreaterThan(0);
  console.log(`[kit-compile] the plugin's facets compiled PLUG/SIN to a ${bytes.length}B bank in ${steps} frame pump(s)`);
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '9551,9648p' docs/modular-graph-phase-d2-plan.md > packages/retroplug/test-ui/kit-compile.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/kit-compile.test.ts`
Expected: `cba4c7d40272e4e6df2b6c532ebde183cba49198`: the blob dcd4bfd4c holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts b/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
index 00e2f0b1a..098f6e276 100644
--- a/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
+++ b/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
@@ -1,7 +1,7 @@
 // End-to-end for the `retroplug-cli bliptoaster-rom` verbs, driven through the real tool against a real backend +
-// audio driver (mirrors test-native/risa-rom-cli.test.ts). The compile path (build-kit) uses the REAL native
-// compileDmc RPC and needs no ROM, so it always runs; the ROM-splice verbs are gated on the built FME-7
-// banking ROM (16 kit banks) and SKIP when absent. The tool only touches s.backend + s.audio, so a minimal
+// audio driver (mirrors test-native/risa-rom-cli.test.ts). The compile path (build-kit) runs the TypeScript kit
+// compiler over the real backend and needs no ROM, so it always runs; the ROM-splice verbs are gated on the built
+// FME-7 banking ROM (16 kit banks) and SKIP when absent. The tool only touches s.backend + s.audio, so a minimal
 // Session over the real backend/driver suffices.
 import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
@@ -42,12 +42,12 @@ const requireRom = (be: ReturnType<typeof createRealBackend>, what: string): voi
   if (!be.fileExists(BLIPTOASTER_ROM)) skip(`bliptoaster-rom ${what}: no ROM at ${BLIPTOASTER_ROM}`);
 };
 
-test("bliptoaster-rom build-kit compiles a WAV into a populated 8 KB .rkit (no ROM needed)", () => {
+test("bliptoaster-rom build-kit compiles a WAV into a populated 8 KB .rkit (no ROM needed)", async () => {
   const { be, s } = toolSession();
   writeSine(be, "/tmp/rp-em-src.wav", 220);
   expect(be.writeFile("/tmp/rp-em-spec.json", jenc({ name: "MYDR", build: [{ file: "/tmp/rp-em-src.wav", name: "BD" }] }))).toBeTruthy();
 
-  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-spec.json", "/tmp/rp-em.rkit"]);
+  await blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-spec.json", "/tmp/rp-em.rkit"]);
   const kit = be.readFile("/tmp/rp-em.rkit")!;
   expect(kit.length).toBe(0x2000); // a raw 8 KB bank, not a ROM
   expect(isBankPopulated(kit)).toBe(true);
@@ -57,12 +57,12 @@ test("bliptoaster-rom build-kit compiles a WAV into a populated 8 KB .rkit (no R
   console.log(`[bliptoaster-rom] build-kit compiled WAV → 8 KB .rkit (MYDR/BD)`);
 });
 
-test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-identical; the ROM boots", () => {
+test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-identical; the ROM boots", async () => {
   const { be, audio, s } = toolSession();
   requireRom(be, "import-kit");
   writeSine(be, "/tmp/rp-em-ik.wav", 300);
   expect(be.writeFile("/tmp/rp-em-ik-spec.json", jenc({ name: "HATS", build: [{ file: "/tmp/rp-em-ik.wav", name: "HH" }] }))).toBeTruthy();
-  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-ik-spec.json", "/tmp/rp-em-ik.rkit"]);
+  await blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-ik-spec.json", "/tmp/rp-em-ik.rkit"]);
 
   // Every one of the 16 banks ships POPULATED (the cart bakes all 16 of its .rkit assets), so an import is a
   // replace, not a fill. It used to assume slot 3 was reserved and empty - true of an earlier build.
@@ -74,7 +74,7 @@ test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-
   expect(before === "HATS").toBe(false); // the baked kit must not already be the one we import
 
   const nes = copyRom(be, "/tmp/rp-em-ik.nes");
-  blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-ik.rkit", String(SLOT), "--out", nes]);
+  await blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-ik.rkit", String(SLOT), "--out", nes]);
 
   const rom = BlipToasterRom.fromBytes(be.readFile(nes)!);
   expect(rom.isKitPopulated(SLOT)).toBe(true);
@@ -92,24 +92,24 @@ test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-
   console.log(`[bliptoaster-rom] import-kit replaced bank ${SLOT} (${before} -> HATS); patched ROM boots`);
 });
 
-test("bliptoaster-rom import-sample splices into a kit, remove-sample empties a slot (index preserved)", () => {
+test("bliptoaster-rom import-sample splices into a kit, remove-sample empties a slot (index preserved)", async () => {
   const { be, s } = toolSession();
   requireRom(be, "import-sample");
   writeSine(be, "/tmp/rp-em-a.wav", 200);
   writeSine(be, "/tmp/rp-em-b.wav", 500);
   expect(be.writeFile("/tmp/rp-em-is-spec.json", jenc({ name: "KT", build: [{ file: "/tmp/rp-em-a.wav", name: "AAA" }] }))).toBeTruthy();
-  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-is-spec.json", "/tmp/rp-em-is.rkit"]);
+  await blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-is-spec.json", "/tmp/rp-em-is.rkit"]);
   const nes = copyRom(be, "/tmp/rp-em-is.nes");
-  blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-is.rkit", "2", "--out", nes]);
+  await blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-is.rkit", "2", "--out", nes]);
 
   // import-sample lands in the first empty slot (1).
-  blipToasterRomTool.run(s, ["import-sample", nes, "2", "/tmp/rp-em-b.wav", "--name", "BBB", "--out", nes]);
+  await blipToasterRomTool.run(s, ["import-sample", nes, "2", "/tmp/rp-em-b.wav", "--name", "BBB", "--out", nes]);
   let model = bankToModel(BlipToasterRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
   expect(model.slots[0]?.name).toBe("AAA");
   expect(model.slots[1]?.name).toBe("BBB");
 
   // remove-sample empties slot 0; slot 1 stays put (index preserved).
-  blipToasterRomTool.run(s, ["remove-sample", nes, "2", "0", "--out", nes]);
+  await blipToasterRomTool.run(s, ["remove-sample", nes, "2", "0", "--out", nes]);
   model = bankToModel(BlipToasterRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
   expect(model.slots[0]).toBe(null);
   expect(model.slots[1]?.name).toBe("BBB");
@@ -309,12 +309,12 @@ test("the baked font byte reaches the screen: two ROMs differing only in it rend
   expect(pixDiff).toBeGreaterThan(0);
 });
 
-test("bliptoaster-rom patch realizes a mixed manifest (build kit + import theme + font) and boots", () => {
+test("bliptoaster-rom patch realizes a mixed manifest (build kit + import theme + font) and boots", async () => {
   const { be, audio, s } = toolSession();
   requireRom(be, "patch");
   writeSine(be, "/tmp/rp-em-pt.wav", 300);
-  blipToasterRomTool.run(s, ["export-theme", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.rit"]);
-  blipToasterRomTool.run(s, ["export-font", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.chr"]);
+  await blipToasterRomTool.run(s, ["export-theme", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.rit"]);
+  await blipToasterRomTool.run(s, ["export-font", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.chr"]);
   const manifest = {
     kits: [{ slot: 4, name: "PT", build: [{ file: "/tmp/rp-em-pt.wav", name: "KIK" }] }],
     themes: [{ slot: 0, file: "/tmp/rp-em-pt.rit" }],
@@ -323,7 +323,7 @@ test("bliptoaster-rom patch realizes a mixed manifest (build kit + import theme
   expect(be.writeFile("/tmp/rp-em-pt-manifest.json", jenc(manifest))).toBeTruthy();
 
   const src = copyRom(be, "/tmp/rp-em-pt-src.nes");
-  blipToasterRomTool.run(s, ["patch", src, "/tmp/rp-em-pt-manifest.json", "/tmp/rp-em-pt-out.nes"]);
+  await blipToasterRomTool.run(s, ["patch", src, "/tmp/rp-em-pt-manifest.json", "/tmp/rp-em-pt-out.nes"]);
   const rom = BlipToasterRom.fromBytes(be.readFile("/tmp/rp-em-pt-out.nes")!);
   expect(rom.kits().some((k) => k.slot === 4 && k.name === "PT")).toBeTruthy();
 
diff --git a/packages/retroplug/test-native/kit-compile.test.ts b/packages/retroplug/test-native/kit-compile.test.ts
index b4abd3800..217ffdc80 100644
--- a/packages/retroplug/test-native/kit-compile.test.ts
+++ b/packages/retroplug/test-native/kit-compile.test.ts
@@ -9,15 +9,18 @@
 // whole says why.
 //
 // Each bank is held two ways. Against native's compileKit / compileDmc on this host, while native's compiler exists:
-// both share every kernel, so that comparison is exact on every host. And against kit-edges.json (beside
-// kit-baseline.json), recorded from this compiler on the target when native and it agreed, under the baseline's
-// rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile agreeing with the first;
-// RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records it, refused off the
-// target or for a bank whose two compiles disagree. The golden is what keeps these edges pinned once native's compiler
-// retires. The sources are synthesised, so it needs no fixtures.
+// both share every kernel, so that comparison is exact on every host. Those two are the harness RPCs
+// (nativeCompileKit / nativeCompileDmc below), the only callers left that reach native's compiler. And
+// against kit-edges.json (beside kit-baseline.json), recorded from this compiler on the target when native and it
+// agreed, under the baseline's rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile
+// agreeing with the first; RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records
+// it, refused off the target or for a bank whose two compiles disagree. The golden is what keeps these edges pinned
+// once native's compiler retires. The sources are synthesised, so it needs no fixtures.
 import { test, expect } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
-import { createAudioDriver, type KitEffect, type KitSampleSpec, type RisaDmcSampleSpec } from "../src/audioDriver";
+import { createAudioDriver, type KitCompileSpec, type KitEffect, type KitSampleSpec } from "../src/audioDriver";
+import type { RisaDmcSampleSpec, RisaKitCompileSpec } from "../src/audioDriver";
+import { makeCall } from "../src/rpcClient";
 import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
 import { fnv1a64Hex, floatWav, signalSamples } from "./kit/sources";
 
@@ -27,6 +30,13 @@ declare const tjs: { env: Record<string, string> };
 const be = createRealBackend();
 const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick
 
+// Native's compiler, over the harness facet's two RPCs (this host binds it). These go with the two harness RPCs when
+// native's compiler retires; while it is here, this is what makes each bank exact against the compiler the kernels
+// came from.
+const nativeCall = makeCall();
+const nativeCompileKit = (spec: KitCompileSpec): Uint8Array => nativeCall("compileKit", spec) as Uint8Array;
+const nativeCompileDmc = (spec: RisaKitCompileSpec): Uint8Array => nativeCall("compileDmc", spec) as Uint8Array;
+
 const BASELINE_TARGET = "linux-x64-gnu13";
 const UPDATE = tjs.env.RP_KIT_GOLDEN_UPDATE === "1";
 const REQUIRE_TARGET = tjs.env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";
@@ -173,7 +183,7 @@ edgeCase("a refused LSDj slot leaves the rest compiling, says why, and is native
   // Each refused slot is empty: no bytes, its name kept, as native's assembler leaves a slot whose file failed to load.
   // So the bank is native's for the same kit with those slots' files missing, and the TypeScript compile of that kit.
   const missing = samples.map((s) => (refused.includes(s) ? { ...s, path: MISSING } : s));
-  const native = audio.compileKit({ name: "REFUSE", samples: missing });
+  const native = nativeCompileKit({ name: "REFUSE", samples: missing });
   expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
   const again = await compileKit(be, { name: "REFUSE", samples: missing });
   expect(again.errors).toEqual([null, UNREADABLE, null, UNREADABLE, UNREADABLE, null, UNREADABLE]);
@@ -198,7 +208,7 @@ edgeCase("a refused risa slot leaves the rest compiling, its entry empty: a miss
   expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST]);
   expect([kit.bytes[0x1f04], kit.bytes[0x1f0c]]).toEqual([0xff, 0xff]);
   const missing = samples.map((s, i) => (i === 1 || i === 3 ? { ...s, path: MISSING } : s));
-  const native = audio.compileDmc({ name: "REFUSE", samples: missing });
+  const native = nativeCompileDmc({ name: "REFUSE", samples: missing });
   expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
   // risa's assembler leaves an empty slot where it is and moves no cursor, so a refused LAST slot is the kit without
   // it.
@@ -233,7 +243,7 @@ edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for
   for (const rotate of [true, false]) {
     const spec = { name: "edges", samples, rotate };
     const kit = await held(`lsdj/edges-rotate-${rotate ? "on" : "off"}`, () => compileKit(be, spec));
-    expect(firstDifference(kit.bytes, audio.compileKit(spec)), `rotate ${rotate}`).toBe(-1);
+    expect(firstDifference(kit.bytes, nativeCompileKit(spec)), `rotate ${rotate}`).toBe(-1);
     // Every slot the bank does not hold whole says why: the empty window, the window under one wave frame, the slot
     // the bank's space ran out on part way (the noise, 2128 bytes, before the capped source), and the capped source,
     // which finds none left.
@@ -256,7 +266,7 @@ edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for
   // marker, which native writes for a slot with no room as for one the spec does not have.
   const full = { name: "FULL", samples: [lsdj(LONG, "LNG"), lsdj(SINE, "AFT")] };
   const kit = await held("lsdj/full", () => compileKit(be, full));
-  expect(firstDifference(kit.bytes, audio.compileKit(full))).toBe(-1);
+  expect(firstDifference(kit.bytes, nativeCompileKit(full))).toBe(-1);
   const noRoom = "the kit's 16288 bytes of sample space are full, so none of this sample's 1056 bytes fit";
   expect(kit.errors).toEqual([null, noRoom]);
   const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
@@ -286,7 +296,7 @@ edgeCase("a full risa kit of every DMC encode edge is native's bank byte for byt
   // A {0} slot is a placed sample; L15 found no room after L0, and says so.
   const noFit = "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it";
   expect(kit.errors).toEqual(samples.map((s) => (s.name === "L15" ? noFit : null)));
-  expect(firstDifference(kit.bytes, audio.compileDmc({ name: "edges-kit", samples }))).toBe(-1);
+  expect(firstDifference(kit.bytes, nativeCompileDmc({ name: "edges-kit", samples }))).toBe(-1);
   // The {0} slots are one byte at their own 64-byte boundary; L0 is the longest legal stream; L15 found no room.
   expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f04], kit.bytes[0x1f05]]).toEqual([0, 0, 1, 0]);
   expect([kit.bytes[0x1f08], kit.bytes[0x1f09], kit.bytes[0x1f0c]]).toEqual([2, 255, 0xff]);
@@ -317,13 +327,13 @@ edgeCase("a finite source the chain overflows refuses its slot with the reason,
   expect(guard(kit.errors[4], ditherWords), String(kit.errors[4])).toBe(true);
   // The refused slots are the slots native leaves when their files fail to load.
   const missing = samples.map((s) => (s.name === "A" ? s : { ...s, path: MISSING }));
-  expect(firstDifference(kit.bytes, audio.compileKit({ name: "OVER", samples: missing }))).toBe(-1);
+  expect(firstDifference(kit.bytes, nativeCompileKit({ name: "OVER", samples: missing }))).toBe(-1);
 });
 
 edgeCase("samples past the slot count are dropped as native drops them, each saying so", async () => {
   const sixteen = Array.from({ length: 16 }, (_, i) => lsdj(i % 2 ? SINE : SHORT, `S${i}`, i % 3 ? [] : [gain(0.5)]));
   const kit = await held("lsdj/sixteen", () => compileKit(be, { name: "SIXTEEN", samples: sixteen }));
-  expect(firstDifference(kit.bytes, audio.compileKit({ name: "SIXTEEN", samples: sixteen }))).toBe(-1);
+  expect(firstDifference(kit.bytes, nativeCompileKit({ name: "SIXTEEN", samples: sixteen }))).toBe(-1);
   expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
   const seventeen: RisaDmcSampleSpec[] = Array.from({ length: 17 }, (_, i) => ({
     path: i % 2 ? SINE : SHORT,
@@ -332,7 +342,7 @@ edgeCase("samples past the slot count are dropped as native drops them, each say
     rate: i % 16,
   }));
   const dmc = await held("risa/seventeen", () => compileDmc(be, { name: "SEVENTEEN", samples: seventeen }));
-  expect(firstDifference(dmc.bytes, audio.compileDmc({ name: "SEVENTEEN", samples: seventeen }))).toBe(-1);
+  expect(firstDifference(dmc.bytes, nativeCompileDmc({ name: "SEVENTEEN", samples: seventeen }))).toBe(-1);
   expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
 });
 
@@ -346,9 +356,9 @@ edgeCase("a refusal beats an empty window: a refused risa slot is empty, where n
   expect(kit.bytes[0x1f04]).toBe(0xff);
   // Native never refuses: the same spec compiles its empty window to the one-byte {0}, a placed slot. The refused slot
   // is the slot native leaves for a file that fails to load.
-  const native = audio.compileDmc({ name: "R", samples });
+  const native = nativeCompileDmc({ name: "R", samples });
   expect(native[0x1f04] !== 0xff).toBe(true);
-  const missing = audio.compileDmc({ name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
+  const missing = nativeCompileDmc({ name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
   expect(firstDifference(kit.bytes, missing)).toBe(-1);
 });
 
@@ -359,12 +369,12 @@ edgeCase("a field the spec leaves out takes the native struct's default, the spe
   const spelled: KitEffect[] = [filter("LowPass", 5734, 1, 0), gain(1, true), dither("HighPassTPDF")];
   const kit = await held("lsdj/defaults", () => compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", sparse)] }));
   expect(kit.errors).toEqual([null]);
-  const native = audio.compileKit({ name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
+  const native = nativeCompileKit({ name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
   expect(firstDifference(kit.bytes, native)).toBe(-1);
   const dmcSparse = [{ path: CHIRP, name: "SP", effects: sparse.slice(0, 2) }];
   const dmc = await held("risa/defaults", () => compileDmc(be, { name: "DEF", samples: dmcSparse }));
   expect(dmc.errors).toEqual([null]);
-  const dmcNative = audio.compileDmc({
+  const dmcNative = nativeCompileDmc({
     name: "DEF",
     samples: [{ path: CHIRP, name: "SP", effects: spelled.slice(0, 2), rate: 12, loop: false, normalize: true }],
   });
diff --git a/packages/retroplug/test-native/kit-dither.test.ts b/packages/retroplug/test-native/kit-dither.test.ts
index 78ac956a2..01dd3a4f6 100644
--- a/packages/retroplug/test-native/kit-dither.test.ts
+++ b/packages/retroplug/test-native/kit-dither.test.ts
@@ -1,10 +1,10 @@
 // The kit compiler's two TPDF dithers are seeded from the samples they quantise, so two compiles of one sample give
-// one bank. The callers that compile a kit from scratch are the CLI sessions (`cli/sessions/lsdj-rom.ts`), and today
-// that is the only path that reaches the LSDj `compileKit` RPC. (`compileDmc` shares the `KitCompiler` instance, but
-// risa's DMC encode skips the dither, so `compileKit` is the only path that reaches a dither.)
+// one bank. A risa kit's DMC encode skips the dither, so an LSDj kit compile is the only path that reaches one; its
+// callers are the CLI sessions (`cli/sessions/lsdj-rom.ts`) and the tests.
 //
-// Everything here goes through the path a session uses (audioDriver.compileKit -> the harness RPC -> KitCompiler ->
-// LsdjKitCodec), not the codec directly, so the case covers the effect descriptor decoding and the slot queue as well.
+// Everything here goes through the path a session uses (audioDriver.compileKit -> the TypeScript kit compiler -> its
+// offline jobs -> rp.dsp.dither), not the kernel directly, so the case covers the effect settings decoding and the
+// node's seeded quantise as well.
 // The bank is read back through `KitView` before it is compared: an empty or undecoded slot would make two compiles
 // agree byte for byte for the wrong reason, so the readback holds the slot to a real sample's shape (both polarities
 // and a span no constant gives) and not only to a peak. LSDj stores inverted nibbles, so an all-zero data region
@@ -50,14 +50,14 @@ function firstDifference(a: Uint8Array, b: Uint8Array): number {
   return -1;
 }
 
-test("compileKit gives identical bytes twice for each TPDF dither", () => {
+test("compileKit gives identical bytes twice for each TPDF dither", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   writeSource(be);
 
   // The control: the same source with no effect at all. Its slot is a real, varied sample too, so a difference from
   // the dithered banks below is the dither's and not an empty slot's.
-  const plain = audio.compileKit({ name: "DITH", samples: [{ path: WAV, name: "SIN", effects: [] }] });
+  const plain = (await audio.compileKit({ name: "DITH", samples: [{ path: WAV, name: "SIN", effects: [] }] })).bytes;
   const plainRead = readBack(plain);
   expect(plainRead.frames).toBeGreaterThan(0);
   expect(plainRead.hi - plainRead.lo).toBeGreaterThan(0.5);
@@ -66,8 +66,8 @@ test("compileKit gives identical bytes twice for each TPDF dither", () => {
     const effects: KitEffect[] = [{ type: "dither", ditherType }];
     const spec = { name: "DITH", samples: [{ path: WAV, name: "SIN", effects }] };
 
-    const first = audio.compileKit(spec);
-    const second = audio.compileKit(spec);
+    const first = (await audio.compileKit(spec)).bytes;
+    const second = (await audio.compileKit(spec)).bytes;
 
     // The compared bank is a whole 16 KB bank whose slot decoded to a varying sample in both polarities (a decaying
     // sine keeps its energy through the resample and the 4-bit quantise), so "identical" can never mean "identically
diff --git a/packages/retroplug/test-native/lsdj-rom.test.ts b/packages/retroplug/test-native/lsdj-rom.test.ts
index 42fae313a..9fceecd06 100644
--- a/packages/retroplug/test-native/lsdj-rom.test.ts
+++ b/packages/retroplug/test-native/lsdj-rom.test.ts
@@ -102,14 +102,16 @@ test("LsdjRom reads real kit/palette/font assets and round-trips a surgical kit-
   console.log(`[lsdj-rom] TR-606 kit(7 samples) + 6 palettes + 3 fonts read; kit rename round-trips; patched ROM boots`);
 });
 
-test("compileKit renders an authored WAV into a 16 KB kit bank a KitView can read", () => {
+test("compileKit renders an authored WAV into a 16 KB kit bank a KitView can read", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   const wav = "/tmp/rp-kit-src.wav";
   writeTestWav(be, wav);
 
-  const bank = audio.compileKit({ name: "TEST", samples: [{ path: wav, name: "SIN", effects: [] }] });
+  const spec = { name: "TEST", samples: [{ path: wav, name: "SIN", effects: [] }] };
+  const { bytes: bank, errors } = await audio.compileKit(spec);
   expect(bank.length).toBe(BANK_SIZE); // a whole 16 KB kit bank
+  expect(errors[0]).toBe(null); // the bank holds the whole sample
 
   // Read it back by dropping the bank into a fresh ROM's slot 0 (bank 8) and using KitView.
   const rom = new Uint8Array(ROM_SIZE);
@@ -128,7 +130,7 @@ test("compileKit renders an authored WAV into a 16 KB kit bank a KitView can rea
   console.log(`[lsdj-rom] compileKit: TEST/SIN → ${bank.length}B bank, sample ${pcm.length} frames, peak ${peak.toFixed(2)}`);
 });
 
-test("import-sample splice: compile one sample + rebuild a kit around the existing raw samples", () => {
+test("import-sample splice: compile one sample + rebuild a kit around the existing raw samples", async () => {
   const be = createRealBackend();
   if (!be.fileExists(LSDJ)) skip(`lsdj-rom import: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
@@ -136,8 +138,8 @@ test("import-sample splice: compile one sample + rebuild a kit around the existi
   writeTestWav(be, wav, 600); // < 1024 source frames → a source shorter than one resampler chunk (rp::dsp::Resampler)
 
   // Compile the one sample (a throwaway 1-sample bank) and pull its nibble bytes.
-  const oneBank = audio.compileKit({ name: "", samples: [{ path: wav, name: "NEW", effects: [] }] });
-  const bytes = sampleBytesFromBank(oneBank, 0);
+  const oneBank = await audio.compileKit({ name: "", samples: [{ path: wav, name: "NEW", effects: [] }] });
+  const bytes = sampleBytesFromBank(oneBank.bytes, 0);
   expect(bytes.length > 0).toBeTruthy(); // short-sample compile no longer crashes / drops
 
   // Splice into the stock TR-606, replacing slot 0 (keeps us within the 0x3fa0 budget).
@@ -171,17 +173,18 @@ test("import-sample splice: compile one sample + rebuild a kit around the existi
   console.log(`[lsdj-rom] import-sample: TR-606 slot0 → NEW (${bytes.length}B), others preserved, ROM boots`);
 });
 
-test("compileKit rotate flag: rotated vs un-rotated banks differ but each round-trips per its mode", () => {
+test("compileKit rotate flag: rotated vs un-rotated banks differ but each round-trips per its mode", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   const wav = "/tmp/rp-kit-rot.wav";
   writeTestWav(be, wav, 4000);
 
-  const rotBank = audio.compileKit({ name: "R", rotate: true, samples: [{ path: wav, name: "S", effects: [] }] });
-  const flatBank = audio.compileKit({ name: "F", rotate: false, samples: [{ path: wav, name: "S", effects: [] }] });
+  const rotBank = await audio.compileKit({ name: "R", rotate: true, samples: [{ path: wav, name: "S", effects: [] }] });
+  const flatSpec = { name: "F", rotate: false, samples: [{ path: wav, name: "S", effects: [] }] };
+  const flatBank = await audio.compileKit(flatSpec);
 
-  const rotBytes = sampleBytesFromBank(rotBank, 0);
-  const flatBytes = sampleBytesFromBank(flatBank, 0);
+  const rotBytes = sampleBytesFromBank(rotBank.bytes, 0);
+  const flatBytes = sampleBytesFromBank(flatBank.bytes, 0);
   expect(rotBytes.length).toBe(flatBytes.length);
   expect(rotBytes.length > 0).toBeTruthy();
   // The rotation shifts nibbles within each 32-sample frame → the packed bytes differ.
@@ -198,6 +201,26 @@ test("compileKit rotate flag: rotated vs un-rotated banks differ but each round-
   console.log(`[lsdj-rom] rotate flag: rotated≠flat bytes, but matched decode identical (${decRot.length} frames)`);
 });
 
+// A slot the compiler refused reaches the CLI's read-back check as its reason, not as a generic failure: a source
+// file that does not exist is the simplest refusal, and the message names what the compiler said about it.
+test("CLI build-kit reports the compiler's reason for a slot that did not land", async () => {
+  const { be, s } = toolSession();
+  const missing = "/tmp/rp-bk-missing-src.wav";
+  be.deleteFile(missing); // a leftover from an earlier run would make the slot load
+  const spec = { name: "BAD", build: [{ file: missing, name: "BD" }] };
+  expect(be.writeFile("/tmp/rp-bk-bad-spec.json", jenc(spec))).toBeTruthy();
+
+  let message = "";
+  try {
+    await lsdjRomTool.run(s, ["build-kit", "/tmp/rp-bk-bad-spec.json", "/tmp/rp-bk-bad.kit"]);
+  } catch (e) {
+    message = (e as Error).message;
+  }
+  expect(message.includes(`sample 0 (${missing}) did not compile`), message).toBeTruthy();
+  expect(message.includes("could not be read or decoded"), message).toBeTruthy(); // the compiler's own words
+  console.log(`[lsdj-rom] build-kit refused slot: ${message}`);
+});
+
 const TRIP = __RESOURCES_DIR__ + "/roms/tripledipper942.gbc";
 
 test("custom-font ROM: fonts detect via the header anchor + palette/font names resolve", () => {
@@ -302,14 +325,14 @@ function toolSession(): { be: ReturnType<typeof createRealBackend>; audio: Retur
 }
 const jenc = (o: unknown): Uint8Array => new TextEncoder().encode(JSON.stringify(o));
 
-test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", () => {
+test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", async () => {
   const { be, audio, s } = toolSession();
   if (!be.fileExists(LSDJ)) skip(`lsdj-rom build-kit: ROM not found`);
   const wav = "/tmp/rp-bk-src.wav";
   writeTestWav(be, wav);
   expect(be.writeFile("/tmp/rp-bk-spec.json", jenc({ name: "MYDR", build: [{ file: wav, name: "BD" }] }))).toBeTruthy();
 
-  lsdjRomTool.run(s, ["build-kit", "/tmp/rp-bk-spec.json", "/tmp/rp-bk.kit"]);
+  await lsdjRomTool.run(s, ["build-kit", "/tmp/rp-bk-spec.json", "/tmp/rp-bk.kit"]);
   const kitBytes = be.readFile("/tmp/rp-bk.kit")!;
   expect(kitBytes.length).toBe(BANK_SIZE); // a raw 16 KB bank, not a ROM
 
@@ -318,7 +341,7 @@ test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", (
   lsdjRomTool.run(s, ["import-kit", "/tmp/rp-bk.gb", "/tmp/rp-bk.kit", "20", "--out", "/tmp/rp-bk.gb"]);
   const rom = LsdjRom.fromBytes(be.readFile("/tmp/rp-bk.gb")!);
   expect(rom.kit(20).name()).toBe("MYDR");
-  expect(rom.kit(20).sampleName(0)).toBe("BD-"); // the native compiler pads a short name to 3 chars with '-'
+  expect(rom.kit(20).sampleName(0)).toBe("BD-"); // the kit compiler pads a short name to 3 chars with '-'
 
   expect(graph.add({ romPath: "/tmp/rp-bk.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 5)).toBeTruthy();
   audio.renderAudio(300);
@@ -326,7 +349,7 @@ test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", (
   console.log(`[lsdj-rom] build-kit → .kit (16KB) → import-kit slot 20 (MYDR/BD), ROM boots`);
 });
 
-test("CLI patch realizes a mixed manifest (build + .kit + rename + palette + font) and boots", () => {
+test("CLI patch realizes a mixed manifest (build + .kit + rename + palette + font) and boots", async () => {
   const { be, audio, s } = toolSession();
   if (!be.fileExists(LSDJ) || !be.fileExists(KIT_FILE) || !be.fileExists(LSDPAL) || !be.fileExists(FONT_EXT_PNG)) skip(`lsdj-rom patch: ROM or asset files not found`);
   const wav = "/tmp/rp-ap-src.wav";
@@ -346,7 +369,7 @@ test("CLI patch realizes a mixed manifest (build + .kit + rename + palette + fon
   expect(be.writeFile("/tmp/rp-ap-manifest.json", jenc(manifest))).toBeTruthy();
   expect(be.writeFile("/tmp/rp-ap.gb", be.readFile(LSDJ)!)).toBeTruthy();
 
-  lsdjRomTool.run(s, ["patch", "/tmp/rp-ap.gb", "/tmp/rp-ap-manifest.json", "/tmp/rp-ap-out.gb"]);
+  await lsdjRomTool.run(s, ["patch", "/tmp/rp-ap.gb", "/tmp/rp-ap-manifest.json", "/tmp/rp-ap-out.gb"]);
 
   const rom = LsdjRom.fromBytes(be.readFile("/tmp/rp-ap-out.gb")!);
   expect(rom.kit(21).name()).toBe("BILT"); // built
diff --git a/packages/retroplug/test-native/risa-kit.test.ts b/packages/retroplug/test-native/risa-kit.test.ts
index 8c364fbd7..d46895e65 100644
--- a/packages/retroplug/test-native/risa-kit.test.ts
+++ b/packages/retroplug/test-native/risa-kit.test.ts
@@ -1,4 +1,4 @@
-// M5 native proof for the risa NES-DPCM (DMC) kit compiler: author a WAV → compileDmc → an 8 KB DMC kit
+// M5 proof for the risa NES-DPCM (DMC) kit compile: author a WAV -> compileDmc -> an 8 KB DMC kit
 // bank → decode the DPCM back to audio (round-trip, the r8brain path is not byte-parity with wav2dmc so we
 // gate on the delta/pack/decode invariants) → splice it into a real risa ROM via setKit (bank + mirror) →
 // the patched ROM boots. SKIPs when the built risa ROM is absent.
@@ -33,13 +33,15 @@ function peakOf(a: Float32Array): number {
   return p;
 }
 
-test("compileDmc renders a WAV into an 8 KB DMC kit bank that decodes back to audio", () => {
+test("compileDmc renders a WAV into an 8 KB DMC kit bank that decodes back to audio", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   writeTestWav(be, WAV);
 
-  const bank = audio.compileDmc({ name: "TEST", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
+  const spec = { name: "TEST", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] };
+  const { bytes: bank, errors } = await audio.compileDmc(spec);
   expect(bank.length).toBe(0x2000);
+  expect(errors[0]).toBe(null); // the bank holds the whole sample
   expect(isBankPopulated(bank)).toBe(true); // 0xA5 magic
 
   const model = bankToModel(bank);
@@ -60,13 +62,14 @@ test("compileDmc renders a WAV into an 8 KB DMC kit bank that decodes back to au
   expect(peak > 0.1).toBe(true);
 });
 
-test("a compiled DMC kit splices into a real risa ROM (bank + mirror) and the ROM boots", () => {
+test("a compiled DMC kit splices into a real risa ROM (bank + mirror) and the ROM boots", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   if (!be.fileExists(RISA_ROM)) skip(`risa-kit boot: no ROM at ${RISA_ROM}`);
   writeTestWav(be, WAV);
 
-  const bank = audio.compileDmc({ name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
+  const spec = { name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] };
+  const bank = (await audio.compileDmc(spec)).bytes;
   const rom = RisaRom.fromBytes(be.readFile(RISA_ROM)!);
   expect(rom.isRisa).toBe(true);
   expect(rom.hasKitMeta).toBe(true); // the mirror is located (needed for the dual-write)
@@ -88,7 +91,7 @@ test("a compiled DMC kit splices into a real risa ROM (bank + mirror) and the RO
   console.log(`[risa-kit] compiled kit spliced into risa ROM; patched image boots; on-disk .nes unchanged`);
 });
 
-test("assembleKitBank re-packs separately-compiled DPCM byte-identically to a whole-kit compileDmc", () => {
+test("assembleKitBank re-packs separately-compiled DPCM byte-identically to a whole-kit compileDmc", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   const WAV_A = "/tmp/rp-risa-parity-a.wav";
@@ -96,38 +99,43 @@ test("assembleKitBank re-packs separately-compiled DPCM byte-identically to a wh
   writeSine(be, WAV_A, 220, 6000);
   writeSine(be, WAV_B, 440, 4000);
 
-  // A whole-kit compile of both samples together (native assemble).
-  const both = audio.compileDmc({
+  // A whole-kit compile of both samples together.
+  const both = (await audio.compileDmc({
     name: "DRUMS",
     samples: [
       { path: WAV_A, name: "AAA", rate: 12, effects: [] },
       { path: WAV_B, name: "BBB", rate: 8, loop: true, effects: [] },
     ],
-  });
+  })).bytes;
   expect(both.length).toBe(0x2000);
 
-  // Compile each sample ALONE, pull its DPCM, and re-pack with the TS assembler. Since encode() carries no
-  // cross-sample state, this must be byte-identical to the whole-kit compile above.
-  const a = bankToModel(audio.compileDmc({ name: "", samples: [{ path: WAV_A, name: "AAA", rate: 12, effects: [] }] })).slots[0]!;
-  const b = bankToModel(audio.compileDmc({ name: "", samples: [{ path: WAV_B, name: "BBB", rate: 8, loop: true, effects: [] }] })).slots[0]!;
+  // Compile each sample ALONE, pull its DPCM, and re-pack with the assembler. The DMC encode keeps no cross-sample
+  // state, so re-assembling the two alone-compiled slots must give the whole-kit bank byte for byte. That is the
+  // slots' chains being independent of each other; what the bytes are held to is kit-baseline's and kit-edges'
+  // record.
+  const aSpec = { name: "", samples: [{ path: WAV_A, name: "AAA", rate: 12, effects: [] }] };
+  const bSpec = { name: "", samples: [{ path: WAV_B, name: "BBB", rate: 8, loop: true, effects: [] }] };
+  const a = bankToModel((await audio.compileDmc(aSpec)).bytes).slots[0]!;
+  const b = bankToModel((await audio.compileDmc(bSpec)).bytes).slots[0]!;
   const reassembled = assembleKitBank("DRUMS", [
     { dpcm: a.dpcm, rate: a.rate, loop: a.loop, name: "AAA" },
     { dpcm: b.dpcm, rate: b.rate, loop: b.loop, name: "BBB" },
   ]);
 
-  expect([...reassembled]).toEqual([...both]); // the TS packer matches native RisaDmcCodec::assemble
-  console.log(`[risa-kit] assembleKitBank == compileDmc (${both.length}B, 2 samples) — byte-identical`);
+  expect([...reassembled]).toEqual([...both]); // the slots' chains carry no cross-slot state
+  console.log(`[risa-kit] assembleKitBank == compileDmc (${both.length}B, 2 samples) - byte-identical`);
 });
 
-test("the risa-assets kit override links a pre-built .rkit at construct (offline compile) and boots", () => {
+test("the risa-assets kit override links a pre-built .rkit at construct (offline compile) and boots", async () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   if (!be.fileExists(RISA_ROM)) skip(`risa-kit link: no ROM at ${RISA_ROM}`);
   writeTestWav(be, WAV);
 
-  // Compile a bank OFFLINE (the plugin can't reach compileDmc) and persist it as a .rkit — exactly what the
-  // Kits menu's "Export..." writes and "Replace..." later links.
-  const bank = audio.compileDmc({ name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
+  // Compile a bank and persist it as a .rkit - exactly what the Kits menu's "Export..." writes and "Replace..."
+  // later links.
+  const spec = { name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] };
+  const bank = (await audio.compileDmc(spec)).bytes;
   const KIT = "/tmp/rp-risa-kit.rkit";
   expect(be.writeFileAtomic(KIT, bank)).toBe(true);
 
diff --git a/packages/retroplug/test-native/risa-rom-cli.test.ts b/packages/retroplug/test-native/risa-rom-cli.test.ts
index e94b40300..f7d5895de 100644
--- a/packages/retroplug/test-native/risa-rom-cli.test.ts
+++ b/packages/retroplug/test-native/risa-rom-cli.test.ts
@@ -35,19 +35,19 @@ const requireRom = (be: ReturnType<typeof createRealBackend>, what: string): voi
   if (!be.fileExists(RISA_ROM)) skip(`risa-rom ${what}: no ROM at ${RISA_ROM}`);
 };
 
-test("risa-rom build-kit writes a bootable .rkit that import-kit places into a ROM", () => {
+test("risa-rom build-kit writes a bootable .rkit that import-kit places into a ROM", async () => {
   const { be, audio, s } = toolSession();
   requireRom(be, "build-kit");
   writeSine(be, "/tmp/rp-rk-src.wav", 220);
   expect(be.writeFile("/tmp/rp-rk-spec.json", jenc({ name: "MYDR", build: [{ file: "/tmp/rp-rk-src.wav", name: "BD" }] }))).toBeTruthy();
 
-  risaRomTool.run(s, ["build-kit", "/tmp/rp-rk-spec.json", "/tmp/rp-rk.rkit"]);
+  await risaRomTool.run(s, ["build-kit", "/tmp/rp-rk-spec.json", "/tmp/rp-rk.rkit"]);
   const kit = be.readFile("/tmp/rp-rk.rkit")!;
   expect(kit.length).toBe(0x2000); // a raw 8 KB bank, not a ROM
   expect(bankToModel(kit).name).toBe("MYDR");
 
   const rom = copyRom(be, "/tmp/rp-rk.nes");
-  risaRomTool.run(s, ["import-kit", rom, "/tmp/rp-rk.rkit", "0", "--out", rom]);
+  await risaRomTool.run(s, ["import-kit", rom, "/tmp/rp-rk.rkit", "0", "--out", rom]);
   const patched = RisaRom.fromBytes(be.readFile(rom)!);
   expect(patched.isKitPopulated(0)).toBe(true);
   expect(patched.kits().some((k) => k.slot === 0 && k.name === "MYDR")).toBe(true);
@@ -88,38 +88,38 @@ test("risa-rom export-font → import-font round-trips a .chr", () => {
   expect([...RisaRom.fromBytes(be.readFile(nes)!).getChrFontSlot(1)!]).toEqual([...want]);
 });
 
-test("risa-rom import-sample splices a sample into a kit, remove-sample empties a slot", () => {
+test("risa-rom import-sample splices a sample into a kit, remove-sample empties a slot", async () => {
   const { be, s } = toolSession();
   requireRom(be, "import-sample");
   // Start from a 1-sample kit in slot 0.
   writeSine(be, "/tmp/rp-is-a.wav", 200);
   writeSine(be, "/tmp/rp-is-b.wav", 500);
   expect(be.writeFile("/tmp/rp-is-spec.json", jenc({ name: "KT", build: [{ file: "/tmp/rp-is-a.wav", name: "AAA" }] }))).toBeTruthy();
-  risaRomTool.run(s, ["build-kit", "/tmp/rp-is-spec.json", "/tmp/rp-is.rkit"]);
+  await risaRomTool.run(s, ["build-kit", "/tmp/rp-is-spec.json", "/tmp/rp-is.rkit"]);
   const nes = copyRom(be, "/tmp/rp-is.nes");
-  risaRomTool.run(s, ["import-kit", nes, "/tmp/rp-is.rkit", "0", "--out", nes]);
+  await risaRomTool.run(s, ["import-kit", nes, "/tmp/rp-is.rkit", "0", "--out", nes]);
 
   // import-sample lands in the first empty slot (1).
-  risaRomTool.run(s, ["import-sample", nes, "0", "/tmp/rp-is-b.wav", "--name", "BBB", "--out", nes]);
+  await risaRomTool.run(s, ["import-sample", nes, "0", "/tmp/rp-is-b.wav", "--name", "BBB", "--out", nes]);
   let model = bankToModel(RisaRom.fromBytes(be.readFile(nes)!).getKitBank(0)!);
   expect(model.slots[0]?.name).toBe("AAA");
   expect(model.slots[1]?.name).toBe("BBB");
 
   // remove-sample empties slot 0; slot 1 stays put (index preserved).
-  risaRomTool.run(s, ["remove-sample", nes, "0", "0", "--out", nes]);
+  await risaRomTool.run(s, ["remove-sample", nes, "0", "0", "--out", nes]);
   model = bankToModel(RisaRom.fromBytes(be.readFile(nes)!).getKitBank(0)!);
   expect(model.slots[0]).toBe(null);
   expect(model.slots[1]?.name).toBe("BBB");
   console.log(`[risa-rom] import-sample (slot 1) + remove-sample (slot 0) — index-addressed splice`);
 });
 
-test("risa-rom patch realizes a mixed manifest (build kit + import theme + import font) and boots", () => {
+test("risa-rom patch realizes a mixed manifest (build kit + import theme + import font) and boots", async () => {
   const { be, audio, s } = toolSession();
   requireRom(be, "patch");
   writeSine(be, "/tmp/rp-pt.wav", 300);
   // Seed a .rit + .chr by exporting from the base ROM.
-  risaRomTool.run(s, ["export-theme", RISA_ROM, "0", "/tmp/rp-pt.rit"]);
-  risaRomTool.run(s, ["export-font", RISA_ROM, "0", "/tmp/rp-pt.chr"]);
+  await risaRomTool.run(s, ["export-theme", RISA_ROM, "0", "/tmp/rp-pt.rit"]);
+  await risaRomTool.run(s, ["export-font", RISA_ROM, "0", "/tmp/rp-pt.chr"]);
   const manifest = {
     kits: [{ slot: 1, name: "PT", build: [{ file: "/tmp/rp-pt.wav", name: "KIK" }] }],
     themes: [{ slot: 3, file: "/tmp/rp-pt.rit" }],
@@ -128,7 +128,7 @@ test("risa-rom patch realizes a mixed manifest (build kit + import theme + impor
   expect(be.writeFile("/tmp/rp-pt-manifest.json", jenc(manifest))).toBeTruthy();
 
   const src = copyRom(be, "/tmp/rp-pt-src.nes");
-  risaRomTool.run(s, ["patch", src, "/tmp/rp-pt-manifest.json", "/tmp/rp-pt-out.nes"]);
+  await risaRomTool.run(s, ["patch", src, "/tmp/rp-pt-manifest.json", "/tmp/rp-pt-out.nes"]);
   const rom = RisaRom.fromBytes(be.readFile("/tmp/rp-pt-out.nes")!);
   expect(rom.kits().some((k) => k.slot === 1 && k.name === "PT")).toBeTruthy();
 
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index 7a19fe28c..b3e4dec5d 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -170,6 +170,37 @@ check "run under an unwritable sibling still exits 0" 0 $code
 contains "the fallback is announced" "$out" "sibling build dir is not writable"
 contains "the session ran from the fallback" "$out" "a stripped sibling helper is importable"
 
+# 6c. The kit sessions' async exit. A kit verb awaits its compile (offline graph jobs, settled by the launcher's
+#     job loop), so the process must exit 0 only AFTER the bank is on disk, and a rejected compile must still exit 1
+#     with the reason. Both go through the REAL binary and its runSession, which a direct tool call in test-native
+#     does not: if runSession exited before the await settled, the exit-0 case would race the file and the rejected
+#     case would exit 0 with nothing on stderr.
+#
+#     The source is a canonical 16-bit mono WAV written here rather than committed: a 4400-frame square wave at
+#     44100 Hz, so 8800 data bytes after a fixed 44-byte header.
+wav="$TMP/tone.wav"
+{
+	printf 'RIFF\x84\x22\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x44\xac\x00\x00\x88\x58\x01\x00\x02\x00\x10\x00data\x60\x22\x00\x00'
+	for ((i = 0; i < 44; i++)); do
+		printf '\x40\x1f%.0s' {1..50} # 50 frames at about +8000
+		printf '\xc0\xe0%.0s' {1..50} # 50 frames at about -8000
+	done
+} >"$wav"
+echo '{ "name": "TST", "build": [{ "file": "tone.wav", "name": "SQR" }] }' >"$TMP/kit.json"
+out="$TMP/build-kit.log"
+"$CLI" lsdj-rom build-kit "$TMP/kit.json" "$TMP/out.kit" >"$out" 2>&1
+check "lsdj-rom build-kit exits 0 once its compile settled" 0 $?
+if [ "$(wc -c <"$TMP/out.kit" 2>/dev/null)" = "16384" ]; then
+	echo "ok   - the bank was on disk when the process exited (16384 B)"
+else
+	echo "FAIL - no 16384-byte bank after build-kit exited 0"
+	fails=$((fails + 1))
+fi
+echo '{ "name": "TST", "build": [{ "file": "nope.wav", "name": "SQR" }] }' >"$TMP/kit-missing.json"
+"$CLI" lsdj-rom build-kit "$TMP/kit-missing.json" "$TMP/out-missing.kit" >"$TMP/build-kit-missing.out" 2>"$TMP/build-kit-missing.err"
+check "a rejected compile exits 1" 1 $?
+contains "the refusal carries the slot's reason" "$TMP/build-kit-missing.err" "did not compile"
+
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
 for f in pass.test fail.test helper; do
````

Run: `sed -n '9658,10329p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:native lsdj-rom`
Expected: 4 of 11 cases fail: `compileKit renders an authored WAV into a 16 KB kit bank a KitView can read`, `import-sample splice: compile one sample + rebuild a kit around the existing raw samples` and `compileKit rotate flag: rotated vs un-rotated banks differ but each round-trips per its mode` on the result shape the switch-over brings (`audio.compileKit` still returns a `Uint8Array` synchronously, so `.bytes` and `errors` are undefined), and `CLI build-kit reports the compiler's reason for a slot that did not land` on the missing reason (the old read-back check's message says `failed to compile` and `check the path`, not `did not compile`). Report the summary line.

Run: `pnpm test:cli-ts`
Expected: the new block prints three `ok` and one `FAIL - the refusal carries the slot's reason`: the old `build-kit` compiles synchronously through the native RPC and writes the bank before it exits, so `lsdj-rom build-kit exits 0 once its compile settled`, `the bank was on disk when the process exited (16384 B)` and `a rejected compile exits 1` all pass already, and only the refusal's message is missing (`failed to compile` rather than `did not compile`). The async-exit assertions gain their teeth once Step 2 makes the verb async. Report the block's lines.

Run: `pnpm test:ui kit-compile`
Expected: this case passes already, and that is the point of section 10's fifth question: the plugin's facet set has carried the seam since Task 2, so the compiler runs there with no audio driver. Report the line; it prints the pump count.

- [ ] **Step 2: The callers move, and the sessions await**

`audioDriver.compileKit` / `compileDmc` return `Promise<KitCompileResult>` and call `src/kit`'s entry points; their
runner is `realBackend`'s new `offlineRunner()`, the runtime's ONE offline queue (a second `OfflineJobs` would drain
the same native completion queue behind the first). `cli/session.ts`'s `runSession` takes `(s) => void |
Promise<void>`, a promise's settle calls `tjs.exit` and a rejection is reported as a throw; `runLongSession` reports a
rejected async setup; `CliTool.run` returns `void | Promise<void>`; the three sessions' compiles and kit verbs are
async and awaited, and each `compileKitBank` still asserts the bank size and that the source's bytes landed, then
throws the slot's own reason. The one-sample compiles say `failed to compile <audio>: <errors[0]>`. `src/kit/
compile.ts`'s docs describe the bytes by the two goldens, not by the compiler on its way out; the assemblers' comments
say what they write.

````diff
diff --git a/packages/retroplug/cli/session.ts b/packages/retroplug/cli/session.ts
index ae9e422f9..e0be5e02f 100644
--- a/packages/retroplug/cli/session.ts
+++ b/packages/retroplug/cli/session.ts
@@ -32,19 +32,30 @@ export function hostArgs(): string[] {
 }
 
 /** Boot a session, run `main` against it, and exit 0. Any throw is reported and exits 1. This is the
- *  entry every session file wraps its body in. */
-export function runSession(main: (s: Session) => void): void {
-  try {
-    main(bootSession());
-    tjs.exit(0);
-  } catch (e) {
-    // Report just the message — these are user-facing usage / IO errors (bad flags, missing ROM, unreadable
+ *  entry every session file wraps its body in. A `main` that awaits (a kit compile's offline jobs, say)
+ *  returns a promise: the exit then happens when it settles, and a rejection is reported as a throw is.
+ *  The launcher keeps pumping the JS job loop until `tjs.exit`, which is what advances those awaits. */
+export function runSession(main: (s: Session) => void | Promise<void>): void {
+  const fail = (e: unknown): void => {
+    // Report just the message - these are user-facing usage / IO errors (bad flags, missing ROM, unreadable
     // file), not crashes to debug, so no stack trace. (QuickJS's Error.stack omits the message anyway, so we
     // read err.message directly rather than printing `.stack`.)
     const err = e as Error;
     console.error(`ERROR: ${err?.message ?? e}`);
     tjs.exit(1);
+  };
+  let out: void | Promise<void>;
+  try {
+    out = main(bootSession());
+  } catch (e) {
+    fail(e);
+    return;
+  }
+  if (out && typeof (out as Promise<void>).then === "function") {
+    (out as Promise<void>).then(() => tjs.exit(0), fail);
+    return;
   }
+  tjs.exit(0);
 }
 
 /** Opt this session out of the bounded batch pump: the native launcher then pumps until tjs.exit / Ctrl-C.
@@ -57,9 +68,16 @@ export function keepAlive(): void {
 /** Boot a session and run `main` WITHOUT auto-exiting on success — for a long-running tool that sets up an
  *  event-driven poll loop (setInterval) + keepAlive() and returns, leaving the native pump to run it until
  *  tjs.exit / Ctrl-C. Any throw during setup is reported and exits 1 (mirrors runSession's catch). */
-export function runLongSession(main: (s: Session) => void): void {
+export function runLongSession(main: (s: Session) => void | Promise<void>): void {
   try {
-    main(bootSession());
+    const out = main(bootSession());
+    // A long-running session sets up and returns; if its setup is async, a rejection is still a setup failure.
+    if (out && typeof (out as Promise<void>).then === "function") {
+      (out as Promise<void>).catch((e: unknown) => {
+        console.error(`ERROR: ${(e as Error)?.message ?? e}`);
+        tjs.exit(1);
+      });
+    }
     // No tjs.exit here: the native keep-alive loop pumps until the tool exits or the user hits Ctrl-C.
   } catch (e) {
     const err = e as Error;
diff --git a/packages/retroplug/cli/sessions/bliptoaster-rom.ts b/packages/retroplug/cli/sessions/bliptoaster-rom.ts
index 149bcc993..c3ef21a38 100644
--- a/packages/retroplug/cli/sessions/bliptoaster-rom.ts
+++ b/packages/retroplug/cli/sessions/bliptoaster-rom.ts
@@ -1,9 +1,9 @@
 // `retroplug-cli bliptoaster-rom` — inspect, extract and edit the static assets (DPCM sample kits, the theme,
 // the CHR font) inside a BlipToaster `.nes` ROM, headlessly. The BlipToaster twin of ./risa-rom.ts (BlipToaster is
 // also NES/DMC, so it reuses the exact same risa asset codecs), backed by the pure-TS src/bliptoaster/rom
-// module; this is just the CLI surface (arg parsing + file I/O + WAV/JSON output). Kit compile is native
-// (s.audio.compileDmc — the generic DMC compiler); sample splicing re-packs existing DPCM + a freshly-
-// compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
+// module; this is just the CLI surface (arg parsing + file I/O + WAV/JSON output). Kit compile is the
+// TypeScript kit compiler (s.audio.compileDmc - the generic DMC compiler); sample splicing re-packs existing
+// DPCM + a freshly-compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
 //
 // BlipToaster has up to 16 SWITCHABLE kit banks on the banking builds (VRC6/VRC7/S5B/FME-7/N163) and a single
 // kit on NROM — kit indices are bounded by rom.kitBankCapacity(). It also bakes 16 themes (bounded by
@@ -13,7 +13,7 @@
 //   retroplug-cli bliptoaster-rom info          <rom> [--json]
 //   retroplug-cli bliptoaster-rom extract       <rom> <outDir> [--rate N]
 //   retroplug-cli bliptoaster-rom patch         <rom> <manifest.json> <out>       (the whole-ROM manifest)
-//   retroplug-cli bliptoaster-rom build-kit     <kit.json> <out.rkit> [flags]     (native compile → .rkit file)
+//   retroplug-cli bliptoaster-rom build-kit     <kit.json> <out.rkit> [flags]     (compile → .rkit file)
 //   retroplug-cli bliptoaster-rom import-sample <rom> <kit> <audio> [flags]       (compile one + splice)
 //   retroplug-cli bliptoaster-rom remove-sample <rom> <kit> <slot> [--out rom]
 //   retroplug-cli bliptoaster-rom export-theme  / import-theme   (.rit palette-role JSON)
@@ -47,6 +47,10 @@ import {
   type AssembleSlot,
 } from "../../src/risa/rom";
 
+// What a slot the compiler left empty with no reason is: the compile result's contract makes that a compiler bug,
+// not a full bank, so the message says so instead of guessing at a cause.
+const NO_SLOT_REASON = "the compiler left the slot empty without saying why";
+
 const enc = new TextEncoder();
 const dec = new TextDecoder();
 const sanitize = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "_";
@@ -97,7 +101,7 @@ function readOrThrow(s: Session, path: string, what: string): Uint8Array {
   return data;
 }
 
-// --- kit compile (native compileDmc) --------------------------------------------------------------------
+// --- kit compile (the TypeScript kit compiler, s.audio.compileDmc) ----------------------------------------
 
 // Per-sample shaping. The DMC codec skips dither (1-bit only) — so gain/filter only. --no-normalize maps
 // to the DMC 7-bit normalize (spec.normalize) rather than a gain effect, so there's no double-normalize.
@@ -146,9 +150,10 @@ function normalizeBuild(build: BuildSource[]): Exclude<BuildSource, string>[] {
   return build.map((b) => (typeof b === "string" ? { file: b } : b));
 }
 
-// Compile a whole 8 KB DPCM bank from `build` sources, asserting every source landed (a bad path silently
-// leaves an empty slot natively). Shared by build-kit + patch.
-function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, fb: Fallbacks): Uint8Array {
+// Compile a whole 8 KB DPCM bank from `build` sources, asserting every source landed. Shared by build-kit +
+// patch. The compiler leaves a refused slot empty and says why, so a slot that did not land is reported with
+// its reason rather than a generic failure.
+async function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, fb: Fallbacks): Promise<Uint8Array> {
   const sources = normalizeBuild(build);
   const samples: RisaDmcSampleSpec[] = sources.map((sm) => ({
     path: resolvePath(baseFile, sm.file),
@@ -160,11 +165,14 @@ function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile
     loop: sm.loop ?? fb.loop,
     normalize: sm.normalize ?? fb.normalize,
   }));
-  const bank = s.audio.compileDmc({ name, samples });
-  if (bank.length !== KIT_BANK_SIZE) throw new Error("compileDmc returned an unexpected bank size");
+  const { bytes: bank, errors } = await s.audio.compileDmc({ name, samples });
+  if (bank.length !== KIT_BANK_SIZE) throw new Error("the kit compiler returned an unexpected bank size");
   const model = bankToModel(bank);
   sources.forEach((sm, i) => {
-    if (!model.slots[i]) throw new Error(`sample ${i} (${sm.file}) failed to compile — check the path/format`);
+    if (!model.slots[i])
+      throw new Error(
+        `sample ${i} (${sm.file}) did not compile: ${errors[i] ?? NO_SLOT_REASON}`,
+      );
   });
   return bank;
 }
@@ -377,17 +385,17 @@ function extract(s: Session, args: string[]): void {
   console.log(`extracted ${wavs} sample WAVs + ${rom.themes().length} themes + ${rom.fonts().length} fonts + rom.json to ${outDir}`);
 }
 
-function buildKit(s: Session, args: string[]): void {
+async function buildKit(s: Session, args: string[]): Promise<void> {
   const [specPath, outKit] = positionals(args);
   if (!specPath || !outKit) throw new Error("usage: bliptoaster-rom build-kit <kit.json> <out.rkit> [effect flags]");
   const spec = JSON.parse(dec.decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
   if (!spec.build || spec.build.length === 0) throw new Error('kit spec needs a "build" array of source audio files');
-  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
+  const bank = await compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
   if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
   console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples); wrote ${outKit}`);
 }
 
-function importSample(s: Session, args: string[]): void {
+async function importSample(s: Session, args: string[]): Promise<void> {
   const [romPath, kitStr, audio] = positionals(args);
   if (!romPath || kitStr == null || !audio) throw new Error("usage: bliptoaster-rom import-sample <rom> <kit> <audio> [--slot N] [--name X] [--out rom] [flags]");
   const rom = openRom(s, romPath);
@@ -395,10 +403,13 @@ function importSample(s: Session, args: string[]): void {
   const name = flag(args, "--name") ?? sampleName(audio);
   const fb = fallbacks(args);
 
-  // Compile the one sample natively (a throwaway 1-sample bank) and pull its DPCM back out.
-  const oneBank = s.audio.compileDmc({ name: "", samples: [{ path: audio, name, effects: fb.effects, rate: fb.rate, loop: fb.loop, normalize: fb.normalize }] });
-  const one = bankToModel(oneBank).slots[0];
-  if (!one) throw new Error(`failed to compile ${audio} — check the path/format`);
+  // Compile the one sample (a throwaway 1-sample bank) and pull its DPCM back out.
+  const one = await s.audio.compileDmc({
+    name: "",
+    samples: [{ path: audio, name, effects: fb.effects, rate: fb.rate, loop: fb.loop, normalize: fb.normalize }],
+  });
+  const compiled = bankToModel(one.bytes).slots[0];
+  if (!compiled) throw new Error(`failed to compile ${audio}: ${one.errors[0] ?? NO_SLOT_REASON}`);
 
   const { name: kitName, slots } = kitSlots(rom, kitIndex);
   const slotStr = flag(args, "--slot");
@@ -410,7 +421,7 @@ function importSample(s: Session, args: string[]): void {
     slot = parseInt(slotStr, 10);
     if (slot < 0 || slot >= KIT_SLOT_COUNT) throw new Error(`--slot ${slot} out of range (0..${KIT_SLOT_COUNT - 1})`);
   }
-  slots[slot] = { dpcm: one.dpcm, rate: one.rate, loop: one.loop, name };
+  slots[slot] = { dpcm: compiled.dpcm, rate: compiled.rate, loop: compiled.loop, name };
 
   rom.setKit(kitIndex, assembleKitBank(kitName, slots));
   const out = flag(args, "--out") ?? romPath;
@@ -511,7 +522,7 @@ function importKit(s: Session, args: string[]): void {
   console.log(`imported ${file} into kit ${idx}; wrote ${out}`);
 }
 
-function patchManifest(s: Session, args: string[]): void {
+async function patchManifest(s: Session, args: string[]): Promise<void> {
   const [romPath, manifestPath, outRom] = positionals(args);
   if (!romPath || !manifestPath || !outRom) throw new Error("usage: bliptoaster-rom patch <rom> <manifest.json> <out>");
   const rom = openRom(s, romPath);
@@ -523,7 +534,8 @@ function patchManifest(s: Session, args: string[]): void {
     if (ke.slot == null) throw new Error('a manifest kit entry needs a "slot"');
     kitIndexInRange(rom, ke.slot);
     if (ke.build) {
-      rom.setKit(ke.slot, compileKitBank(s, ke.name ?? "", ke.build, manifestPath, fb));
+      const bank = await compileKitBank(s, ke.name ?? "", ke.build, manifestPath, fb);
+      rom.setKit(ke.slot, bank);
     } else if (ke.file) {
       const bank = readOrThrow(s, resolvePath(manifestPath, ke.file), "kit");
       if (bank.length !== KIT_BANK_SIZE || !isBankPopulated(bank)) throw new Error(`kit file ${ke.file}: not a populated 8 KB .rkit`);
@@ -621,7 +633,7 @@ export const blipToasterRomTool: CliTool = {
   name: "bliptoaster-rom",
   summary: "inspect / extract / edit BlipToaster ROM kits, theme and font (+ compile WAV → .rkit)",
   help: BLIPTOASTER_ROM_HELP,
-  run(s: Session, args: string[]): void {
+  run(s: Session, args: string[]): void | Promise<void> {
     const sub = args[0];
     const rest = args.slice(1);
     if (sub === "info") return info(s, rest);
diff --git a/packages/retroplug/cli/sessions/lsdj-rom.ts b/packages/retroplug/cli/sessions/lsdj-rom.ts
index 6c1483283..f524db0fe 100644
--- a/packages/retroplug/cli/sessions/lsdj-rom.ts
+++ b/packages/retroplug/cli/sessions/lsdj-rom.ts
@@ -9,7 +9,7 @@
 //   retroplug-cli lsdj-rom info          <rom> [--json]
 //   retroplug-cli lsdj-rom extract       <rom> <outDir> [--rate N]
 //   retroplug-cli lsdj-rom patch         <rom> <manifest.json> <out>         (the whole-ROM manifest)
-//   retroplug-cli lsdj-rom build-kit     <kit.json> <out.kit> [--no-rotate]  (native compile → .kit file)
+//   retroplug-cli lsdj-rom build-kit     <kit.json> <out.kit> [--no-rotate]  (compile → .kit file)
 //   retroplug-cli lsdj-rom import-sample <rom> <kit> <audio> [flags]         (compile one + splice)
 //   retroplug-cli lsdj-rom remove-sample <rom> <kit> <slot> [--out rom]
 //   retroplug-cli lsdj-rom export-palette / import-palette   (.lsdpal community palette files)
@@ -25,6 +25,10 @@ import { LsdjRom, buildKitBank, sampleBytesFromBank, kitSampleSpace, KIT_MAX_SAM
 // reference rate — overridable with --rate. 11468 Hz is LSDj's middle-note playback rate.
 const DEFAULT_SAMPLE_RATE = 11468;
 
+// What a slot the compiler left empty with no reason is: the compile result's contract makes that a compiler bug,
+// not a full bank, so the message says so instead of guessing at a cause.
+const NO_SLOT_REASON = "the compiler left the slot empty without saying why";
+
 const enc = new TextEncoder();
 const sanitize = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "_";
 const hex2 = (n: number): string => n.toString(16).toUpperCase().padStart(2, "0");
@@ -103,7 +107,7 @@ function extract(s: Session, args: string[]): void {
   console.log(`extracted ${wavs} sample WAVs (@${rate}Hz) + rom.json to ${outDir}`);
 }
 
-// --- sample import (native compileKit) ---
+// --- sample import (the TypeScript kit compiler, s.audio.compileKit) ---
 
 const flag = (args: string[], name: string): string | undefined => {
   const i = args.indexOf(name);
@@ -178,26 +182,31 @@ function normalizeBuild(build: BuildSource[]): { file: string; name?: string; of
   return build.map((b) => (typeof b === "string" ? { file: b } : b));
 }
 
-// Compile a kit bank from `build` sources, asserting a 16 KB bank and that every source landed (a bad path
-// silently leaves an empty slot natively). Shared by build-kit + apply. `fallbackEffects` (the CLI effect
-// flags) applies to sources that don't carry their own `effects`.
-function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, rotate: boolean, fallbackEffects: KitEffect[]): Uint8Array {
+// Compile a kit bank from `build` sources, asserting a 16 KB bank and that every source landed. Shared by
+// build-kit + apply. `fallbackEffects` (the CLI effect flags) applies to sources that don't carry their own
+// `effects`. The compiler leaves a refused slot empty and says why, so a slot that did not land is reported
+// with its reason rather than a generic failure; a sample the bank ran out of room for part way keeps its bytes,
+// so it is warned about instead (a read-back cannot see a clipped sample).
+async function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, rotate: boolean, fallbackEffects: KitEffect[]): Promise<Uint8Array> {
   const sources = normalizeBuild(build);
-  const bank = s.audio.compileKit({
+  const { bytes: bank, errors } = await s.audio.compileKit({
     name,
     rotate,
     samples: sources.map((sm) => ({ path: resolvePath(baseFile, sm.file), name: sm.name ?? sampleName(sm.file), offset: sm.offset, length: sm.length, effects: sm.effects ?? fallbackEffects })),
   });
-  if (bank.length !== 0x4000) throw new Error("compileKit returned an unexpected bank size");
+  if (bank.length !== 0x4000) throw new Error("the kit compiler returned an unexpected bank size");
   sources.forEach((sm, i) => {
-    if (sampleBytesFromBank(bank, i).length === 0) throw new Error(`sample ${i} (${sm.file}) failed to compile — check the path`);
+    const reason = errors[i];
+    if (sampleBytesFromBank(bank, i).length === 0)
+      throw new Error(`sample ${i} (${sm.file}) did not compile: ${reason ?? NO_SLOT_REASON}`);
+    if (reason != null) console.error(`warning: sample ${i} (${sm.file}): ${reason}`);
   });
   return bank;
 }
 
 // build-kit reads a single (slotless) kit entry — the same shape as one `kits[]` entry in a manifest — and
 // compiles its `build` sources to a .kit file.
-function buildKit(s: Session, args: string[]): void {
+async function buildKit(s: Session, args: string[]): Promise<void> {
   const [specPath, outKit] = args.filter((a) => !a.startsWith("--"));
   if (!specPath || !outKit) throw new Error("usage: lsdj-rom build-kit <kit.json> <out.kit> [--no-rotate] [effect flags]");
   const spec = JSON.parse(new TextDecoder().decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
@@ -206,12 +215,13 @@ function buildKit(s: Session, args: string[]): void {
   // No target ROM here, so rotation is a build-time choice: default ON (LSDj 9.2.0+), --no-rotate for older
   // targets. Importing this .kit into a mismatched-version ROM is the straight-copy caveat (see import-kit).
   const rotate = !has(args, "--no-rotate");
-  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, rotate, effectsFromFlags(args));
+  const effects = effectsFromFlags(args);
+  const bank = await compileKitBank(s, spec.name ?? "", spec.build, specPath, rotate, effects);
   if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
   console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples${rotate ? "" : ", un-rotated"}); wrote ${outKit}`);
 }
 
-function importSample(s: Session, args: string[]): void {
+async function importSample(s: Session, args: string[]): Promise<void> {
   const [romPath, kitStr, audio] = args.filter((a) => !a.startsWith("--"));
   if (!romPath || kitStr == null || !audio) throw new Error("usage: lsdj-rom import-sample <rom> <kitIndex> <audio> [--name X] [--slot N] [--out rom] [effect flags]");
   const kitIndex = parseInt(kitStr, 10);
@@ -220,11 +230,15 @@ function importSample(s: Session, args: string[]): void {
   const name = flag(args, "--name") ?? audio.slice(audio.lastIndexOf("/") + 1).replace(/\.[^.]*$/, "");
   const out = flag(args, "--out") ?? romPath;
 
-  // Compile the one sample natively (a throwaway 1-sample bank) and pull its nibble bytes out. Encode to
+  // Compile the one sample (a throwaway 1-sample bank) and pull its nibble bytes out. Encode to
   // match the target ROM's version so splicing into a pre-9.2.0 kit stays byte-correct.
-  const oneBank = s.audio.compileKit({ name: "", rotate: rom.rotatesSamples, samples: [{ path: audio, name, effects: effectsFromFlags(args) }] });
-  const bytes = sampleBytesFromBank(oneBank, 0);
-  if (bytes.length === 0) throw new Error(`failed to compile ${audio} — check the path/format`);
+  const one = await s.audio.compileKit({
+    name: "",
+    rotate: rom.rotatesSamples,
+    samples: [{ path: audio, name, effects: effectsFromFlags(args) }],
+  });
+  const bytes = sampleBytesFromBank(one.bytes, 0);
+  if (bytes.length === 0) throw new Error(`failed to compile ${audio}: ${one.errors[0] ?? NO_SLOT_REASON}`);
 
   // Splice beside the kit's existing samples (raw, un-re-encoded).
   const samples: KitSample[] = kit.samplesRaw();
@@ -343,7 +357,7 @@ function readOrThrow(s: Session, path: string, what: string): Uint8Array {
   return data;
 }
 
-function patchManifest(s: Session, args: string[]): void {
+async function patchManifest(s: Session, args: string[]): Promise<void> {
   const [romPath, manifestPath, outRom] = args.filter((a) => !a.startsWith("--"));
   if (!romPath || !manifestPath || !outRom) throw new Error("usage: lsdj-rom patch <rom> <manifest.json> <out>");
   const rom = openRom(s, romPath);
@@ -353,7 +367,10 @@ function patchManifest(s: Session, args: string[]): void {
   for (const ke of m.kits ?? []) {
     if (ke.slot == null) throw new Error('a manifest kit entry needs a "slot"');
     if (ke.build) {
-      rom.setKitBank(ke.slot, compileKitBank(s, ke.name ?? "", ke.build, manifestPath, rom.rotatesSamples, effectsFromFlags(args)));
+      const bank = await compileKitBank(
+        s, ke.name ?? "", ke.build, manifestPath, rom.rotatesSamples, effectsFromFlags(args),
+      );
+      rom.setKitBank(ke.slot, bank);
     } else if (ke.file) {
       rom.importKitFile(ke.slot, readOrThrow(s, resolvePath(manifestPath, ke.file), "kit"));
     }
@@ -433,7 +450,7 @@ export const lsdjRomTool: CliTool = {
   name: "lsdj-rom",
   summary: "inspect / extract / metadata-patch LSDj ROM kits, palettes and fonts",
   help: LSDJ_ROM_HELP,
-  run(s: Session, args: string[]): void {
+  run(s: Session, args: string[]): void | Promise<void> {
     const sub = args[0];
     const rest = args.slice(1);
     if (sub === "info") return info(s, rest);
diff --git a/packages/retroplug/cli/sessions/risa-rom.ts b/packages/retroplug/cli/sessions/risa-rom.ts
index 1a868b06f..2c866bfce 100644
--- a/packages/retroplug/cli/sessions/risa-rom.ts
+++ b/packages/retroplug/cli/sessions/risa-rom.ts
@@ -1,8 +1,8 @@
 // `retroplug-cli risa-rom` — inspect, extract and edit the static assets (DPCM sample kits, themes, fonts)
 // inside a risa (NES/MMC5) `.nes` ROM, headlessly. The risa twin of ./lsdj-rom.ts, backed by the pure-TS
 // src/risa/rom module; this is just the CLI surface (arg parsing + file I/O + WAV/JSON output). Kit compile
-// is native (s.audio.compileDmc — the risa analog of compileKit); sample splicing re-packs existing DPCM +
-// a freshly-compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
+// is the TypeScript kit compiler (s.audio.compileDmc - the risa analog of compileKit); sample splicing
+// re-packs existing DPCM + a freshly-compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
 //
 // ONE JSON schema drives the editing verbs (see RISA_ROM_HELP): a manifest of kit/theme/font entries, each
 // of which BUILDS/IMPORTS from a file or TWEAKS metadata; `build-kit` reads a single, slotless kit entry.
@@ -10,7 +10,7 @@
 //   retroplug-cli risa-rom info          <rom> [--json]
 //   retroplug-cli risa-rom extract       <rom> <outDir> [--rate N]
 //   retroplug-cli risa-rom patch         <rom> <manifest.json> <out>       (the whole-ROM manifest)
-//   retroplug-cli risa-rom build-kit     <kit.json> <out.rkit> [flags]     (native compile → .rkit file)
+//   retroplug-cli risa-rom build-kit     <kit.json> <out.rkit> [flags]     (compile → .rkit file)
 //   retroplug-cli risa-rom import-sample <rom> <kit> <audio> [flags]       (compile one + splice)
 //   retroplug-cli risa-rom remove-sample <rom> <kit> <slot> [--out rom]
 //   retroplug-cli risa-rom export-theme  / import-theme   (.rit palette-role JSON)
@@ -39,6 +39,10 @@ import {
   type AssembleSlot,
 } from "../../src/risa/rom";
 
+// What a slot the compiler left empty with no reason is: the compile result's contract makes that a compiler bug,
+// not a full bank, so the message says so instead of guessing at a cause.
+const NO_SLOT_REASON = "the compiler left the slot empty without saying why";
+
 const enc = new TextEncoder();
 const dec = new TextDecoder();
 const sanitize = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "_";
@@ -77,7 +81,7 @@ function readOrThrow(s: Session, path: string, what: string): Uint8Array {
   return data;
 }
 
-// --- kit compile (native compileDmc) --------------------------------------------------------------------
+// --- kit compile (the TypeScript kit compiler, s.audio.compileDmc) ---------------------------------------
 
 // Per-sample shaping. risa's DMC codec skips dither (4-bit only) — so gain/filter only. --no-normalize maps
 // to the DMC 7-bit normalize (spec.normalize) rather than a gain effect, so there's no double-normalize.
@@ -126,9 +130,10 @@ function normalizeBuild(build: BuildSource[]): Exclude<BuildSource, string>[] {
   return build.map((b) => (typeof b === "string" ? { file: b } : b));
 }
 
-// Compile a whole 8 KB DPCM bank from `build` sources, asserting every source landed (a bad path silently
-// leaves an empty slot natively). Shared by build-kit + patch.
-function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, fb: Fallbacks): Uint8Array {
+// Compile a whole 8 KB DPCM bank from `build` sources, asserting every source landed. Shared by build-kit +
+// patch. The compiler leaves a refused slot empty and says why, so a slot that did not land is reported with
+// its reason rather than a generic failure.
+async function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, fb: Fallbacks): Promise<Uint8Array> {
   const sources = normalizeBuild(build);
   const samples: RisaDmcSampleSpec[] = sources.map((sm) => ({
     path: resolvePath(baseFile, sm.file),
@@ -140,11 +145,14 @@ function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile
     loop: sm.loop ?? fb.loop,
     normalize: sm.normalize ?? fb.normalize,
   }));
-  const bank = s.audio.compileDmc({ name, samples });
-  if (bank.length !== KIT_BANK_SIZE) throw new Error("compileDmc returned an unexpected bank size");
+  const { bytes: bank, errors } = await s.audio.compileDmc({ name, samples });
+  if (bank.length !== KIT_BANK_SIZE) throw new Error("the kit compiler returned an unexpected bank size");
   const model = bankToModel(bank);
   sources.forEach((sm, i) => {
-    if (!model.slots[i]) throw new Error(`sample ${i} (${sm.file}) failed to compile — check the path/format`);
+    if (!model.slots[i])
+      throw new Error(
+        `sample ${i} (${sm.file}) did not compile: ${errors[i] ?? NO_SLOT_REASON}`,
+      );
   });
   return bank;
 }
@@ -251,17 +259,17 @@ function extract(s: Session, args: string[]): void {
   console.log(`extracted ${wavs} sample WAVs + ${rom.themes().length} themes + ${rom.fonts().length} fonts + rom.json to ${outDir}`);
 }
 
-function buildKit(s: Session, args: string[]): void {
+async function buildKit(s: Session, args: string[]): Promise<void> {
   const [specPath, outKit] = positionals(args);
   if (!specPath || !outKit) throw new Error("usage: risa-rom build-kit <kit.json> <out.rkit> [effect flags]");
   const spec = JSON.parse(dec.decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
   if (!spec.build || spec.build.length === 0) throw new Error('kit spec needs a "build" array of source audio files');
-  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
+  const bank = await compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
   if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
   console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples); wrote ${outKit}`);
 }
 
-function importSample(s: Session, args: string[]): void {
+async function importSample(s: Session, args: string[]): Promise<void> {
   const [romPath, kitStr, audio] = positionals(args);
   if (!romPath || kitStr == null || !audio) throw new Error("usage: risa-rom import-sample <rom> <kit> <audio> [--slot N] [--name X] [--out rom] [flags]");
   const kitIndex = parseInt(kitStr, 10);
@@ -269,10 +277,13 @@ function importSample(s: Session, args: string[]): void {
   const name = flag(args, "--name") ?? sampleName(audio);
   const fb = fallbacks(args);
 
-  // Compile the one sample natively (a throwaway 1-sample bank) and pull its DPCM back out.
-  const oneBank = s.audio.compileDmc({ name: "", samples: [{ path: audio, name, effects: fb.effects, rate: fb.rate, loop: fb.loop, normalize: fb.normalize }] });
-  const one = bankToModel(oneBank).slots[0];
-  if (!one) throw new Error(`failed to compile ${audio} — check the path/format`);
+  // Compile the one sample (a throwaway 1-sample bank) and pull its DPCM back out.
+  const one = await s.audio.compileDmc({
+    name: "",
+    samples: [{ path: audio, name, effects: fb.effects, rate: fb.rate, loop: fb.loop, normalize: fb.normalize }],
+  });
+  const compiled = bankToModel(one.bytes).slots[0];
+  if (!compiled) throw new Error(`failed to compile ${audio}: ${one.errors[0] ?? NO_SLOT_REASON}`);
 
   const { name: kitName, slots } = kitSlots(rom, kitIndex);
   const slotStr = flag(args, "--slot");
@@ -284,7 +295,7 @@ function importSample(s: Session, args: string[]): void {
     slot = parseInt(slotStr, 10);
     if (slot < 0 || slot >= KIT_SLOT_COUNT) throw new Error(`--slot ${slot} out of range (0..${KIT_SLOT_COUNT - 1})`);
   }
-  slots[slot] = { dpcm: one.dpcm, rate: one.rate, loop: one.loop, name };
+  slots[slot] = { dpcm: compiled.dpcm, rate: compiled.rate, loop: compiled.loop, name };
 
   rom.setKit(kitIndex, assembleKitBank(kitName, slots));
   const out = flag(args, "--out") ?? romPath;
@@ -374,7 +385,7 @@ function importKit(s: Session, args: string[]): void {
   console.log(`imported ${file} into kit ${idxStr}; wrote ${out}`);
 }
 
-function patchManifest(s: Session, args: string[]): void {
+async function patchManifest(s: Session, args: string[]): Promise<void> {
   const [romPath, manifestPath, outRom] = positionals(args);
   if (!romPath || !manifestPath || !outRom) throw new Error("usage: risa-rom patch <rom> <manifest.json> <out>");
   const rom = openRom(s, romPath);
@@ -385,7 +396,8 @@ function patchManifest(s: Session, args: string[]): void {
   for (const ke of m.kits ?? []) {
     if (ke.slot == null) throw new Error('a manifest kit entry needs a "slot"');
     if (ke.build) {
-      rom.setKit(ke.slot, compileKitBank(s, ke.name ?? "", ke.build, manifestPath, fb));
+      const bank = await compileKitBank(s, ke.name ?? "", ke.build, manifestPath, fb);
+      rom.setKit(ke.slot, bank);
     } else if (ke.file) {
       const bank = readOrThrow(s, resolvePath(manifestPath, ke.file), "kit");
       if (bank.length !== KIT_BANK_SIZE || !isBankPopulated(bank)) throw new Error(`kit file ${ke.file}: not a populated 8 KB .rkit`);
@@ -455,7 +467,7 @@ export const risaRomTool: CliTool = {
   name: "risa-rom",
   summary: "inspect / extract / metadata-patch risa ROM kits, themes and fonts",
   help: RISA_ROM_HELP,
-  run(s: Session, args: string[]): void {
+  run(s: Session, args: string[]): void | Promise<void> {
     const sub = args[0];
     const rest = args.slice(1);
     if (sub === "info") return info(s, rest);
diff --git a/packages/retroplug/cli/tools.ts b/packages/retroplug/cli/tools.ts
index f29bffd27..76f459972 100644
--- a/packages/retroplug/cli/tools.ts
+++ b/packages/retroplug/cli/tools.ts
@@ -17,8 +17,10 @@ export interface CliTool {
    *  returns, and the dispatcher does NOT auto-exit it (runLongSession) — the native pump runs it until
    *  tjs.exit / Ctrl-C. Omit / false for a normal batch command. */
   longRunning?: boolean;
-  /** Run the tool against a booted control plane, given its own args (everything after the command name). */
-  run(s: Session, args: string[]): void;
+  /** Run the tool against a booted control plane, given its own args (everything after the command name). A tool
+   *  that awaits (a kit compile's offline jobs) returns a promise; the dispatcher's runSession waits for it before
+   *  exiting, so a rejection is still reported as a failed command. */
+  run(s: Session, args: string[]): void | Promise<void>;
 }
 
 import { testTool } from "./sessions/test";
diff --git a/packages/retroplug/src/audioDriver.ts b/packages/retroplug/src/audioDriver.ts
index 8ceff1976..9bcb0807f 100644
--- a/packages/retroplug/src/audioDriver.ts
+++ b/packages/retroplug/src/audioDriver.ts
@@ -6,8 +6,10 @@
 
 
 import { pumpAllJobs, setJobWait } from "./capability";
+import { compileDmc, compileKit, type KitCompileResult } from "./kit";
 import { setOfflineWait } from "./offline";
 import { makeCall } from "./rpcClient";
+import { offlineRunner } from "./realBackend";
 import type { EngineGraph } from "./engineGraph";
 import type { HostShape } from "./graph/buildGraph";
 
@@ -43,9 +45,9 @@ export interface DspTraceSpan {
   t1: number;
 }
 
-// LSDJ kit-compile effects (mirror the native rp::lsdj tagged union; discriminated on `type`). Applied
-// per sample: gain (normalize/scale) + filter (RBJ biquad) run before resampling, dither after — matching
-// the native ordering. Passed verbatim to the compileKit RPC where reflect-cpp decodes the tagged union.
+// LSDJ kit-compile effects (the kit chain's rp.dsp nodes' settings; discriminated on `type`). Applied
+// per sample: gain (normalize/scale) + filter (RBJ biquad) run before resampling, dither after - matching
+// the compile order. A field a spec leaves out takes the node's default (src/kit/chain.ts).
 export type KitGainEffect = { type: "gain"; normalize?: boolean; gain?: number };
 export type KitFilterEffect = {
   type: "filter";
@@ -136,14 +138,15 @@ export interface AudioDriver {
    *  buffer per stream in its `channelLayout()` (Game Boy = 4: Pulse 1, Pulse 2, Wave, Noise). Empty
    *  unless the project holds exactly ONE system (the PerChannel shape fans a single core's channels). */
   renderAudioPerChannel(id: number, ms: number): Float32Array[];
-  /** Compile an LSDJ sample kit from source audio files into a 16 KB kit bank (resample to the GB rate +
-   *  effects + 4-bit pack, per-sample in parallel natively). A per-sample load failure leaves that slot
-   *  empty rather than failing the whole kit — the caller validates by reading the bank back. */
-  compileKit(spec: KitCompileSpec): Uint8Array;
-  /** Compile a risa NES-DPCM sample kit into an 8 KB kit bank (resample to the PAL DPCM rate + effects +
-   *  1-bit ±2 delta pack, per-sample in parallel natively). Same behaviour as compileKit — an unloadable
-   *  slot is left empty; the caller validates by reading the bank back. */
-  compileDmc(spec: RisaKitCompileSpec): Uint8Array;
+  /** Compile an LSDJ sample kit from source audio files into a 16 KB kit bank: the TypeScript compiler's chain of
+   *  offline graph jobs per slot, slots in parallel (src/kit/). The promise settles where this host pumps or waits
+   *  for offline jobs: createAudioDriver turns the wait on, and a host with a UI tick pumps from it. A per-sample
+   *  failure leaves that slot empty rather than failing the whole kit, and the slot's reason comes back beside the
+   *  bank - the caller validates by reading the bank back, and can say why a slot did not land. */
+  compileKit(spec: KitCompileSpec): Promise<KitCompileResult>;
+  /** Compile a risa NES-DPCM sample kit into an 8 KB kit bank (the same chain, resampled to the PAL DPCM rate and
+   *  encoded by rp.dsp.dpcm). Same settling and failure behaviour as compileKit. */
+  compileDmc(spec: RisaKitCompileSpec): Promise<KitCompileResult>;
   /** The engine's audio sample rate (Hz) — thread into encodeWav so WAV headers are labelled correctly. */
   sampleRate(): number;
   /** Set the host sample rate (Hz). Baked into cores at construct, so it only takes effect BEFORE any
@@ -247,6 +250,9 @@ export function createAudioDriver(graph?: EngineGraph): AudioDriver {
   // A host that makes an audio driver has no UI tick to pump offline jobs (the CLI, the Node addon, the test host), so
   // their promises settle through the poll's native wait (src/offline.ts) instead.
   setOfflineWait(OFFLINE_WAIT_MS);
+  // The kit compiles run on the runtime's one offline queue, the emulator client's, so a compile and a live host never
+  // drain each other's completions (realBackend's offlineRunner).
+  const kitRunner = offlineRunner();
 
   const ints = (b: Uint8Array | number[]): number[] => Array.from(b);
   const STEREO: HostShape = { kind: "multi", channels: 2 };
@@ -305,8 +311,8 @@ export function createAudioDriver(graph?: EngineGraph): AudioDriver {
         },
         () => [],
       ),
-    compileKit: (spec) => call("compileKit", spec) as Uint8Array, // the 16 KB kit bank, straight through
-    compileDmc: (spec) => call("compileDmc", spec) as Uint8Array, // the 8 KB risa DMC kit bank
+    compileKit: (spec) => compileKit(kitRunner, spec), // the 16 KB kit bank and each slot's reason
+    compileDmc: (spec) => compileDmc(kitRunner, spec), // the 8 KB risa DMC kit bank, and each slot's reason
     sampleRate: () => call("sampleRate") as number,
     setSampleRate: (sampleRate) => call("setSampleRate", sampleRate) as boolean,
     setTransport: (running) => call("setTransport", running) as boolean,
diff --git a/packages/retroplug/src/kit/compile.ts b/packages/retroplug/src/kit/compile.ts
index 152912405..ba1849dd0 100644
--- a/packages/retroplug/src/kit/compile.ts
+++ b/packages/retroplug/src/kit/compile.ts
@@ -3,21 +3,22 @@
 // engine's offline pool runs them in parallel. Native decodes, windows and does every float the bytes depend on (the
 // effects, the resample, the dither, the DMC encode, each a node in a job's document, chain.ts); TypeScript builds the
 // documents, cuts the buffers to their lengths, packs the codes and bits (pack.ts) and assembles the bank
-// (lsdj/rom/buildKit.ts, risa/rom/kit.ts). A kit compiled here is the bank native KitCompiler builds from the same
-// spec, byte for byte: test-native/kit-baseline.test.ts holds it to kit-baseline.json's 39 banks, and
-// test-native/kit-compile.test.ts to kit-edges.json's banks past them.
+// (lsdj/rom/buildKit.ts, risa/rom/kit.ts). A kit compiled here is the bank those goldens record, byte for byte:
+// test-native/kit-baseline.test.ts holds it to kit-baseline.json's 39 banks and test-native/kit-compile.test.ts to
+// kit-edges.json's banks past them, both under the baseline's rules (the exact bytes only on the host that recorded
+// them, a foreign host demanding each case agree with itself).
 //
-// A slot that fails is left empty and the kit still compiles, as KitCompiler leaves a slot whose file fails to load:
-// a source that cannot be read or decoded, a window with a sample that is not finite, a spec the nodes cannot carry
-// (chain.ts's refusals), a pass that makes a sample that is not finite, or a pass a node's guard stepped into
-// (`recorded` says why those two keep every compiled kit byte-identical). Empty means no bytes: LSDj's assembler still
-// names the slot, with a length of zero; risa's leaves its index entry empty.
+// A slot that fails is left empty and the kit still compiles, never refusing the kit as a whole: a source that cannot
+// be read or decoded, a window with a sample that is not finite, a spec the nodes cannot carry (chain.ts's refusals),
+// a pass that makes a sample that is not finite, or a pass a node's guard stepped into (`recorded` says why that last
+// one keeps every compiled kit byte-identical). Empty means no bytes: LSDj's assembler still names the slot, with a
+// length of zero; risa's leaves its index entry empty.
 //
-// What a slot is when a stage comes out empty is the native compiler's too, found by reading it and reproduced here
-// rather than run, since a job can neither take an input of no frames nor record one: a window that is empty (an
-// offset at or past the source's end) or resamples to fewer frames than one wave frame is an LSDj slot of no bytes,
-// which is also what compileSample's trim makes of it; and a window that resamples to no frames at all is risa's
-// one-byte `{0}`, the encode of nothing padded to its shortest legal length.
+// What a slot is when a chain comes out empty is settled here rather than run, since a job can neither take an input
+// of no frames nor record one: a window that is empty (an offset at or past the source's end) or resamples to fewer
+// frames than one wave frame is an LSDj slot of no bytes, which is also what trimming to whole 32-sample frames leaves
+// of it; and a window that resamples to no frames at all is risa's one-byte `{0}`, the encode of nothing padded to its
+// shortest legal length.
 //
 // Every slot the bank does not hold whole has a reason in the result, a failure or not: the empty LSDj slots above,
 // an LSDj sample the bank's sample space ran out on (clipped, or with no room at all), and a risa sample that does not
@@ -199,8 +200,7 @@ async function compileSlots<S>(samples: readonly S[], capacity: number, compile:
   return { results, dropped: samples.slice(capacity).map(() => why) };
 }
 
-/** An LSDj kit: the 16 KB bank native compileKit builds from `spec` (its 15 slots, the rest dropped), and each
- *  sample's reason. */
+/** An LSDj kit: the 16 KB bank (its 15 slots, the rest dropped), and each sample's reason. */
 export async function compileKit(runner: KitJobRunner, spec: KitCompileSpec): Promise<KitCompileResult> {
   const rotate = spec.rotate ?? true;
   const compile = (s: KitSampleSpec): Promise<Compiled> => lsdjSlot(runner, s, rotate);
@@ -220,8 +220,7 @@ export async function compileKit(runner: KitJobRunner, spec: KitCompileSpec): Pr
   return { bytes: bank, errors: [...errors, ...dropped] };
 }
 
-/** A risa DMC kit: the 8 KB bank native compileDmc builds from `spec` (its 16 slots, the rest dropped), and each
- *  sample's reason. */
+/** A risa DMC kit: the 8 KB bank (its 16 slots, the rest dropped), and each sample's reason. */
 export async function compileDmc(runner: KitJobRunner, spec: RisaKitCompileSpec): Promise<KitCompileResult> {
   const compile = (s: RisaDmcSampleSpec): Promise<Compiled> => dmcSlot(runner, s);
   const { results, dropped } = await compileSlots(spec.samples, KIT_SLOT_COUNT, compile, "risa kit");
diff --git a/packages/retroplug/src/lsdj/rom/buildKit.ts b/packages/retroplug/src/lsdj/rom/buildKit.ts
index 7bf39432f..d39328293 100644
--- a/packages/retroplug/src/lsdj/rom/buildKit.ts
+++ b/packages/retroplug/src/lsdj/rom/buildKit.ts
@@ -1,7 +1,7 @@
 // Pure-TS kit-bank assembly: the port of native KitUtil::buildKit, byte for byte, used to SPLICE a kit
 // (single-sample import / remove) without re-encoding untouched samples: read existing samples as raw nibble
 // bytes (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank here. The TypeScript kit compiler
-// (src/kit/) assembles its banks here too. Layout matches kit.ts / KitUtil:
+// (src/kit/) assembles every bank it compiles here too, whatever host it runs in. Layout matches kit.ts / KitUtil:
 // a 16-entry u16le offset table (entry 0 = 0x4060; entries i+1 = each sample's end GB-addr; 0 terminates),
 // 15 × 3-char sample names at 0x22, a 6-char kit name at 0x52, sample data from 0x60 (≤ 0x3fa0 bytes).
 import {
@@ -92,8 +92,8 @@ export function buildKitBankPlaced(kitName: string, samples: readonly KitSample[
   return { bank, written };
 }
 
-/** Extract sample `i`'s raw nibble bytes from a standalone 16 KB bank (base 0) — e.g. a 1-sample bank
- *  returned by compileKit, to splice into an existing kit. Empty when the slot is unused. */
+/** Extract sample `i`'s raw nibble bytes from a standalone 16 KB bank (base 0) - e.g. a 1-sample bank
+ *  from compileKit's result, to splice into an existing kit. Empty when the slot is unused. */
 export function sampleBytesFromBank(bank: Uint8Array, i: number): Uint8Array {
   const entry = (k: number): number => bank[k * 2] | (bank[k * 2 + 1] << 8);
   if (i < 0 || i >= KIT_MAX_SAMPLES) return new Uint8Array(0);
diff --git a/packages/retroplug/src/lsdj/rom/rom.ts b/packages/retroplug/src/lsdj/rom/rom.ts
index b6c4563ac..39106eeeb 100644
--- a/packages/retroplug/src/lsdj/rom/rom.ts
+++ b/packages/retroplug/src/lsdj/rom/rom.ts
@@ -54,8 +54,8 @@ function versionRotatesSamples(v: LsdjVersion | null): boolean {
 
 export class LsdjRom {
   readonly version: LsdjVersion | null;
-  /** Whether this ROM's LSDj version (9.2.0+) uses the per-frame sample rotation — for both decoding its
-   *  kits (kit.ts) and encoding imported samples to match (pass to compileKit's `rotate`). */
+  /** Whether this ROM's LSDj version (9.2.0+) uses the per-frame sample rotation - for both decoding its
+   *  kits (kit.ts) and encoding imported samples to match (the `rotate` a kit compile takes). */
   readonly rotatesSamples: boolean;
 
   private constructor(private readonly rom: Uint8Array) {
@@ -86,7 +86,7 @@ export class LsdjRom {
     return Array.from({ length: KIT_COUNT }, (_, i) => new KitView(this.rom, i, this.rotatesSamples));
   }
 
-  /** Overwrite a kit slot with a whole 16 KB bank (e.g. from compileKit / buildKitBank). */
+  /** Overwrite a kit slot with a whole 16 KB bank (the bank a kit compile returns, or buildKitBank's). */
   setKitBank(index: number, bank: Uint8Array): void {
     if (index < 0 || index >= KIT_COUNT || bank.length !== BANK_SIZE) return;
     this.rom.set(bank, KIT_LOOKUP[index] * BANK_SIZE);
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 923f56030..912e94ac4 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -129,6 +129,15 @@ function sharedOffline(): OfflineJobs {
   return realOffline;
 }
 
+/** This runtime's offline queue as the `offlineRun` runner the kit compiles take. It is the queue of the emulator
+ *  client above, not a second one over the same engine: native's completion queue is one per engine, so two queues
+ *  would each drain the other's completions. Nothing throws here with no RPC surface bound: the channel is resolved
+ *  inside the queue's port lambdas, so it is the first run's submission that fails, not this call. */
+export function offlineRunner(): Pick<Backend, "offlineRun"> {
+  const offline = sharedOffline();
+  return { offlineRun: (request) => offline.run(request) };
+}
+
 /** The emulator lifecycle / live params / input / snapshot-reads facet. Throws if no RPC surface is bound. */
 export function createEmulatorClient(): EmulatorBackend {
   const call = makeCall();
diff --git a/packages/retroplug/src/risa/rom/kit.ts b/packages/retroplug/src/risa/rom/kit.ts
index 5aabb3e22..176fa0ee0 100644
--- a/packages/retroplug/src/risa/rom/kit.ts
+++ b/packages/retroplug/src/risa/rom/kit.ts
@@ -1,9 +1,10 @@
 // risa DPCM kit-bank codec (read side) + the metadata-mirror derivation + the DMC decoder + the bank
-// assembler. The 8 KB kit bank is produced natively (audioDriver.compileDmc) or by the TypeScript kit
-// compiler (src/kit/, through assembleKitBank); this reads one back (bankToModel), derives the
-// resident-mirror rows from a bank's own bytes (deriveMetaFromBank — the model-free path from risa's
-// rom_upgrade.js), and decodes a DPCM stream to audio (dpcmDecode) for round-trip verification. Ported
-// from risa's tools/rom_patcher/src/{kit_bank_parser.js, kit_editor/decoder.js, rom_upgrade.js}.
+// assembler. The 8 KB kit bank is produced by the TypeScript kit compiler (src/kit/, through
+// audioDriver.compileDmc over the offline jobs) or re-packed by assembleKitBank; this reads one back
+// (bankToModel), derives the resident-mirror rows from a bank's own bytes (deriveMetaFromBank, the
+// model-free path from risa's rom_upgrade.js), and decodes a DPCM stream to audio (dpcmDecode) for
+// round-trip verification. Ported from risa's tools/rom_patcher/src/{kit_bank_parser.js, kit_editor/decoder.js,
+// rom_upgrade.js}.
 
 import {
   KIT_BANK_SIZE,
@@ -129,8 +130,8 @@ function writeSampleName(bank: Uint8Array, off: number, name: string): void {
  *  aligned packing into the 7872 B sample region, a 4-byte index entry per slot ([addr, lenReg, rate, flags]),
  *  3-char sample names, the ≤6-char kit name, and the 0xA5 populated magic. A null slot stays EMPTY with its
  *  index preserved (NOT compacted); a slot whose DPCM won't fit the remaining region is left empty. Because
- *  the native per-sample encode carries no cross-sample state, this is byte-identical to compiling the same
- *  samples together via compileDmc — so it losslessly re-packs existing slots' DPCM alongside a new one. */
+ *  a slot's encode carries no cross-sample state, this is byte-identical to compiling the same samples together
+ *  via compileDmc - so it losslessly re-packs existing slots' DPCM alongside a new one. */
 export function assembleKitBank(kitName: string, slots: (AssembleSlot | null)[]): Uint8Array {
   return assembleKitBankPlaced(kitName, slots).bank;
 }
````

Run: `sed -n '10357,11111p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`, including the new `lsdj-rom build-kit exits 0 once its compile settled`, `the bank was on disk when the process exited (16384 B)`, `a rejected compile exits 1` and `the refusal carries the slot's reason`.

Run: `pnpm test:native lsdj-rom`
Expected: 11 passed, 0 skipped, 0 failed: the new reason case included.

Run: `pnpm test:native risa-kit`
Expected: 4 passed, 0 skipped, 0 failed.

Run: `pnpm test:native kit-dither`
Expected: 1 passed, 0 skipped, 0 failed.

Run: `pnpm test:native risa-rom-cli`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `pnpm test:native bliptoaster-rom-cli`
Expected: 10 passed, 0 skipped, 0 failed.

Run: `pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed, the native comparison still in place through `makeCall`.

- [ ] **Step 3: The comments that named the old path**

`systemRoles.ts` says `lsdj-assets` is a role with no protocol node that splices pre-built kit, palette and font files
into the ROM on the control plane (it compiles nothing); `risaAssetsRole.ts` and `ui/screens/menu/menuDefs.ts` say a
`.rkit` comes from risa's Export or the CLI's build-kit, and note that `src/kit` can compile one in the plugin though
no row does. No behaviour changes.

````diff
diff --git a/packages/retroplug/src/risaAssetsRole.ts b/packages/retroplug/src/risaAssetsRole.ts
index 2dd1bf86b..82361b771 100644
--- a/packages/retroplug/src/risaAssetsRole.ts
+++ b/packages/retroplug/src/risaAssetsRole.ts
@@ -5,9 +5,10 @@
 // manifest is the persisted source of truth (it round-trips through the project's role config). THEMES are
 // just palette indices, so they're stored INLINE as readable JSON (no file, no base64); FONTS are binary,
 // so they LINK to the `.chr` bank file on disk by path (read at construct). KITS are likewise binary, so
-// they LINK a pre-built 8 KB DMC bank (`.rkit`) by path — compilation is offline (the compileDmc RPC is on
-// the harness facet, unreachable from the plugin), so a kit override just splices a ready-made bank with
-// rom.setKit (which dual-writes the resident metadata mirror). Applying reuses the pure-TS patcher (src/risa/rom).
+// they LINK a pre-built 8 KB DMC bank (`.rkit`) by path - risa's Export writes one out of a ROM and the CLI's
+// build-kit compiles one, and a kit override just splices that ready-made bank with rom.setKit (which
+// dual-writes the resident metadata mirror). src/kit can compile a .rkit in the plugin too, though no UI row does.
+// Applying reuses the pure-TS patcher (src/risa/rom).
 import type { RoleRegistry, ConstructCaps } from "./systemRoles";
 import type { ConstructSpec } from "./backend";
 import { z } from "./configSchema";
diff --git a/packages/retroplug/src/systemRoles.ts b/packages/retroplug/src/systemRoles.ts
index 9c168d8fb..fe526a95b 100644
--- a/packages/retroplug/src/systemRoles.ts
+++ b/packages/retroplug/src/systemRoles.ts
@@ -62,7 +62,8 @@ export interface RoleType {
    *  role's own config (e.g. an LSDj asset-override list). */
   onConstruct?(spec: ConstructSpec, caps: ConstructCaps, config: Record<string, unknown>): ConstructSpec;
   /** DEFERRED: a role-owned settings UI + its render descriptor. (Kit patching does NOT need this -
-   *  `lsdj-assets` is a role with no protocol node that compiles on the control plane via the compileKit RPC.) */
+   *  `lsdj-assets` is a role with no protocol node that splices pre-built kit, palette and font files into
+   *  the ROM on the control plane.) */
   ui?: unknown;
 }
 
diff --git a/packages/retroplug/ui/screens/menu/menuDefs.ts b/packages/retroplug/ui/screens/menu/menuDefs.ts
index 5329995b2..68c79781e 100644
--- a/packages/retroplug/ui/screens/menu/menuDefs.ts
+++ b/packages/retroplug/ui/screens/menu/menuDefs.ts
@@ -1267,8 +1267,9 @@ function replaceAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, sl
 
 // --- risa asset file actions (Export/Replace own the .rit/.chr/.rkit formats) -------------------------
 // THEMES are palette indices stored INLINE (readable JSON `.rit`, no file); FONTS LINK a `.chr` bank by path;
-// KITS LINK a pre-built 8 KB `.rkit` DMC bank by path (compilation is offline — the plugin can't reach
-// compileDmc — so a kit override just splices a ready-made bank, as risa's Export produces).
+// KITS LINK a pre-built 8 KB `.rkit` DMC bank by path (risa's Export writes one out of a ROM and the CLI's
+// build-kit compiles one; src/kit can compile one in the plugin, though no row does - so a kit override just
+// splices a ready-made bank).
 
 const readRisaRomFor = (be: HostBackend, romPath: string): RisaRom | null => {
   const bytes = romPath ? be.readFile(romPath) : null;
````

Run: `sed -n '11146,11193p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm typecheck`
Expected: Done (both `tsconfig.json` and `tsconfig.ui.json`).

- [ ] **Step 4: Run the suites**

Run: `pnpm test`
Expected: `# 171 test file(s) passed (jobs=16)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 163 native test file(s) passed (jobs=16)`, only dsp-bench and nes-bench skipping.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: exit 0, `# 163 native test file(s) passed`, and the skip check passes: `136 skips in 61 files`, all baseline.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: `All tests passed (428 assertions in 1 test case)`, unchanged.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:ui`
Expected: `# 40 UI test file(s) passed (jobs=16)`, the two baseline skips; the kit case logs `... a 16384B bank in 15 frame pump(s)`.

Run: `pnpm test:node`
Expected: `pass 12, fail 0`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`; graph 356087 / 272, audio 1235263 / 109, engine-graph 282 cases (273 passed, 9 skipped), launchpad 144 / 29, all unchanged (no native file changed).

Run: `pnpm typecheck`
Expected: Done (both `tsconfig.json` and `tsconfig.ui.json`).

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

Run: `./build.sh`
Expected: exit 0; the one pre-existing GCC warning is `PluginDSP.cpp`'s `-Wformat-truncation`, on a file this task did not touch.

- [ ] **Step 5: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/retroplug/cli/session.ts
 M packages/retroplug/cli/sessions/bliptoaster-rom.ts
 M packages/retroplug/cli/sessions/lsdj-rom.ts
 M packages/retroplug/cli/sessions/risa-rom.ts
 M packages/retroplug/cli/tools.ts
 M packages/retroplug/src/audioDriver.ts
 M packages/retroplug/src/kit/compile.ts
 M packages/retroplug/src/lsdj/rom/buildKit.ts
 M packages/retroplug/src/lsdj/rom/rom.ts
 M packages/retroplug/src/realBackend.ts
 M packages/retroplug/src/risa/rom/kit.ts
 M packages/retroplug/src/risaAssetsRole.ts
 M packages/retroplug/src/systemRoles.ts
 M packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
 M packages/retroplug/test-native/kit-compile.test.ts
 M packages/retroplug/test-native/kit-dither.test.ts
 M packages/retroplug/test-native/lsdj-rom.test.ts
 M packages/retroplug/test-native/risa-kit.test.ts
 M packages/retroplug/test-native/risa-rom-cli.test.ts
?? packages/retroplug/test-ui/kit-compile.test.ts
 M packages/retroplug/ui/screens/menu/menuDefs.ts
 M tools/run-cli-ts-tests.sh
````

````bash
git add -- packages/retroplug/test-ui/kit-compile.test.ts packages/retroplug/cli/session.ts \
  packages/retroplug/cli/sessions/bliptoaster-rom.ts packages/retroplug/cli/sessions/lsdj-rom.ts \
  packages/retroplug/cli/sessions/risa-rom.ts packages/retroplug/cli/tools.ts packages/retroplug/src/audioDriver.ts \
  packages/retroplug/src/kit/compile.ts packages/retroplug/src/lsdj/rom/buildKit.ts \
  packages/retroplug/src/lsdj/rom/rom.ts packages/retroplug/src/realBackend.ts \
  packages/retroplug/src/risa/rom/kit.ts packages/retroplug/src/risaAssetsRole.ts \
  packages/retroplug/src/systemRoles.ts packages/retroplug/test-native/bliptoaster-rom-cli.test.ts \
  packages/retroplug/test-native/kit-compile.test.ts packages/retroplug/test-native/kit-dither.test.ts \
  packages/retroplug/test-native/lsdj-rom.test.ts packages/retroplug/test-native/risa-kit.test.ts \
  packages/retroplug/test-native/risa-rom-cli.test.ts packages/retroplug/ui/screens/menu/menuDefs.ts \
  tools/run-cli-ts-tests.sh
git commit -F - <<'EOF'
refactor(kit): every kit compile runs as offline graph jobs, the plugin's included

audioDriver's compileKit / compileDmc run the TypeScript kit compiler (src/kit/):
each returns a promise of the bank and one reason per spec sample, and runs on
the runtime's one offline queue (realBackend's offlineRunner), so a compile and
a live host never drain each other's completions. createAudioDriver still turns
the poll's native wait on, which is what settles them in the hosts with no UI
tick.

The CLI sessions await them: lsdj-rom / risa-rom / bliptoaster-rom's build-kit,
import-sample and patch, with CliTool.run and runSession taking a promise and
exiting on it (the launcher's pump is what advances the awaits). A session's
read-back check reports the compiler's own reason for a slot that did not land
instead of a generic failure, warns with the reason when a sample only partly
fits, and says a slot the compiler left empty with no reason at all is a
compiler bug rather than guessing a cause. Test-native holds one refusal end to
end (a missing source through build-kit), and tools/run-cli-ts-tests.sh holds
the binary's own exit path: build-kit exits 0 only once the bank is on disk, and
a rejected compile exits 1 with "did not compile" on stderr.

The test-native files that compiled through the driver await it (lsdj-rom,
risa-kit, kit-dither, the two CLI suites); kit-compile.test.ts keeps its
comparison against native's compiler by calling the two harness RPCs directly
(makeCall), which go when the RPCs go. src/kit/compile.ts describes the bytes it
produces by what the kit-baseline and kit-edges goldens record, not by the
compiler that is on its way out.

The plugin can compile a kit: the offline jobs ride the emulator facet, which
the plugin binds. New test-ui/kit-compile.test.ts proves it end to end on the UI
test host, whose facet registration is exactly the plugin's (HostRpcService +
EngineRpcService, no harness) and which checks the two harness RPCs are not
bound there. The compile is src/kit's over the real backend, settled only by the
host's frame tick (ui.pump + backend.pumpJobs) and never by an audio driver's
native wait, with a microtask-only stretch first proving the wait is off. The UI
bundle itself still reaches neither the compiler nor an audio driver; the CLI
bundle and the render worker's session path do.

The mock tier cannot reach it: only a native host runs graphs (the existing mock
case pins that rejection), and createAudioDriver needs the RPC channel.

No native file changed: native's compiler and the harness RPCs stay in the
tree, unreferenced from TypeScript.
EOF
````

---

### Task 6: native `KitCompiler` and its codecs retire

Design section 5's retention list and section 7's sixth commit. Every caller has compiled through TypeScript since Task
5, so native's own compile goes with the surface only it used: `KitCompiler`, `KitCodec`, `LsdjKitCodec`,
`RisaDmcCodec`, `KitUtil`'s `compileSample` and `buildKit`, `SampleUtil`'s nibble helpers, the harness `compileKit` /
`compileDmc` methods and their DTOs, and the C++ baseline suite. What stays is what a node, the seam or a test runs:
`src/dsp/` whole, `SampleCache`, `lsdj/Effects.cpp` (now compiled into both test binaries as the kit path's reference)
and a new slim `lsdj/KitBank.hpp` for the bank's numbers the seam and the tests still name. `retroplug-core` links
neither DSP library, and enkiTS leaves the build entirely: no shipped binary can contain it, so its `add_subdirectory`,
its warning suppression and its third-party credit go; the submodule stays. CI's Catch2 step drops
`RP_KIT_GOLDEN_REQUIRE_TARGET` with the suite it guarded, and `test:native` keeps it.

**Files:**

- Create: `packages/native/src/lsdj/KitBank.hpp`
- Modify: `.github/workflows/build.yml`, `CMakeLists.txt`, `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`, `packages/native/node/README.md`, `packages/native/node/napi/Writer.hpp`, `packages/native/src/dsp/Dpcm.hpp`, `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/engine/OfflineJobs.hpp`, `packages/native/src/host/rpc/BackendRpcRegistration.hpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/native/src/kit/SampleCache.hpp`, `packages/native/src/lsdj/Effects.cpp`, `packages/native/src/lsdj/Effects.hpp`, `packages/native/test/audio/Dither.test.cpp`, `packages/native/test/engine-graph/OfflineJobs.test.cpp`, `packages/native/test/graph/Dsp.test.cpp`, `packages/retroplug/scripts/run-native-tests.mjs`, `packages/retroplug/src/kit/chain.ts`, `packages/retroplug/src/kit/compile.ts`, `packages/retroplug/src/kit/pack.ts`, `packages/retroplug/src/lsdj/rom/buildKit.ts`, `packages/retroplug/src/lsdj/rom/constants.ts`, `packages/retroplug/src/lsdj/rom/kit.ts`, `packages/retroplug/src/risa/rom/kit.ts`, `packages/retroplug/test-native/kit-baseline.test.ts`, `packages/retroplug/test-native/kit-compile.test.ts`, `packages/retroplug/test-native/kit/sources.ts`, `packages/retroplug/test-ui/kit-compile.test.ts`, `packages/retroplug/test/kit/kit.test.ts`, `packaging/licenses/THIRD-PARTY-NOTICES.txt`, `spec/02-native-host.md`, `spec/06-build-test.md`
- Delete: `packages/native/src/kit/KitCodec.hpp`, `packages/native/src/kit/KitCompiler.cpp`, `packages/native/src/kit/KitCompiler.hpp`, `packages/native/src/lsdj/KitUtil.cpp`, `packages/native/src/lsdj/KitUtil.hpp`, `packages/native/src/lsdj/LsdjKitCodec.cpp`, `packages/native/src/lsdj/LsdjKitCodec.hpp`, `packages/native/src/lsdj/SampleUtil.hpp`, `packages/native/src/risa/RisaDmcCodec.cpp`, `packages/native/src/risa/RisaDmcCodec.hpp`, `packages/native/test/audio/KitBaseline.test.cpp`

- [ ] **Step 1: Native's compile surface retires**

Delete the eleven files whose only reader was the compiler, with `git rm`:

````bash
git rm -q -- packages/native/src/kit/KitCodec.hpp packages/native/src/kit/KitCompiler.cpp \
  packages/native/src/kit/KitCompiler.hpp packages/native/src/lsdj/KitUtil.cpp packages/native/src/lsdj/KitUtil.hpp \
  packages/native/src/lsdj/LsdjKitCodec.cpp packages/native/src/lsdj/LsdjKitCodec.hpp \
  packages/native/src/lsdj/SampleUtil.hpp packages/native/src/risa/RisaDmcCodec.cpp \
  packages/native/src/risa/RisaDmcCodec.hpp packages/native/test/audio/KitBaseline.test.cpp
````

Run: `git status --short --untracked-files=all`
Expected: one `D  <path>` line per file just removed (staged, worktree gone) and ` m deps/sameboy`; nothing else.

`src/lsdj/KitBank.hpp` keeps the three numbers two tests still name (the kit rate 11 468, `0x3fa0` and its frame
count) and documents them as the format TypeScript packs. `retroplug-core` no longer compiles a kit-path source except
`SampleCache.cpp`, and links neither `retroplug-dsp-kernels` nor `retroplug-dsp-resample` nor enkiTS;
`retroplug-audio-test` compiles `src/lsdj/Effects.cpp` itself (`retroplug-graph-test` already did) and names
`retroplug-dsp-kernels` in its own LIBS. `BackendRpcRegistration.hpp` drops the two methods (the harness facet 22 ->
20, the union 92 -> 90, recounted from the `addMethod` lines); `BackendTypes.hpp` drops their DTOs and
`EngineRpcService` drops the methods and its lazy compiler member with the out-of-line destructor that existed only
for it. `Effects.hpp` drops `LsdjEffect` and its `rfl::Literal` tags; the effect structs and `processEffect` stay. The
root `CMakeLists.txt` drops `add_subdirectory(deps/enkiTS)` and its warning suppression, and
`packaging/licenses/THIRD-PARTY-NOTICES.txt` drops the credit. CI's Catch2 step drops `RP_KIT_GOLDEN_REQUIRE_TARGET`;
`test:native` keeps it. Two spec links are repointed here, because deleting `KitBaseline.test.cpp` would otherwise
make `check:docs` fail one commit before the docs task; their prose is Task 7's. Comments in `Dpcm.hpp`,
`SampleCache.hpp`, `Effects.{hpp,cpp}`, `OfflineJobs.{hpp,cpp}`, the two test files and the Node README and writer now
describe what is instead of naming a deleted symbol.

Create `packages/native/src/lsdj/KitBank.hpp`:

````cpp
#pragma once

#include <cstddef>
#include <cstdint>

// The LSDj kit bank's numbers: a 16 KB bank whose data region is up to 0x3fa0 bytes of packed 4-bit nibbles (two
// samples a byte, stored inverted), resampled to kGameboySampleRate. The TypeScript kit compiler packs one
// (packages/retroplug/src/kit/pack.ts), and native reads or writes none: the tests name these numbers, Dither.test.cpp
// for the kit rate and OfflineJobs.test.cpp for the seam's float-ratio cap, which is measured in kit frames.

namespace rp::lsdj {

inline constexpr std::uint32_t kGameboySampleRate = 11468;

inline constexpr std::size_t kMaxSampleBytes  = 0x3fa0;                 // packed nibbles, 2 samples a byte
inline constexpr std::size_t kMaxSampleFrames = kMaxSampleBytes * 2;    // 32576 frames

} // namespace rp::lsdj
````

Run: `mkdir -p packages/native/src/lsdj; sed -n '11382,11399p' docs/modular-graph-phase-d2-plan.md > packages/native/src/lsdj/KitBank.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/src/lsdj/KitBank.hpp`
Expected: `c00b975c523810a5469c1eef2f75470b86a01241`: the blob 2898eb671 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index eb326cc02..1ad6e968a 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -90,11 +90,6 @@ jobs:
         run: pnpm test:native
 
       - name: Plugin unit tests (Catch2)
-        # The kit baseline compares exact bank bytes only on the host it recorded them on, so this job demands that
-        # this is that host, as the parity step above demands it of the audio baseline; the arm64, macOS and Windows
-        # jobs run the same step without it and fall back to each case agreeing with itself.
-        env:
-          RP_KIT_GOLDEN_REQUIRE_TARGET: "1"
         run: pnpm test:plugin
 
       - name: UI tests (headless LVGL)
diff --git a/CMakeLists.txt b/CMakeLists.txt
index 80da8d0b2..83f0f8ee3 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -84,7 +84,7 @@ option(RETROPLUG_MESEN_LTO "LTO the Mesen NES core (~10% faster, much slower lin
 set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/packages/native/src/lv_conf.h" CACHE PATH "")
 
 # <windows.h> defines min()/max() macros that break std::numeric_limits<T>::max()
-# and std::min/max (e.g. lsdj/KitUtil.cpp). NOMINMAX suppresses them — safe and
+# and std::min/max. NOMINMAX suppresses them - safe and
 # standard; unlike WIN32_LEAN_AND_MEAN it removes no headers, so it's global.
 # (The earlier /FI cpp_stdlib_preinclude.h workaround for the extern "C" / STL
 # C2894 issue is gone — tommitytom/txiki.js's self-guarding headers + dropping
@@ -280,14 +280,11 @@ add_subdirectory(deps/mesen)
 # Headers only: packages/native/src/dsp/R8bBase.cpp compiles its one source (r8bbase.cpp) on the throwing allocator, so
 # the tree declares no target for the vendored library and there is none to link.
 
-# --- enkiTS: task scheduler. Drives per-sample parallel work in the LSDJ
-# kit compiler so a 16-sample kit can recompile across cores instead of
-# blocking the UI rpcpp thread on a serial loop.
-add_subdirectory(deps/enkiTS)
-retroplug_silence_dep_warnings(enkiTS)
+# --- enkiTS: a task scheduler, vendored but not built. No RetroPlug code uses it, so the tree adds no target for it
+# and links none; the submodule is kept.
 
 # --- miniaudio: single-header audio file decoder (WAV / MP3 / FLAC).
-# Used by SampleCache to load source samples for kit compilation.
+# Used by SampleCache, the decode an offline job's file input takes.
 # Header-only — no library, just an include path.
 add_library(miniaudio INTERFACE)
 target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniaudio)
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 1280c34e0..cca97ea4f 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -36,11 +36,11 @@ endif()
 # The shape shared by every native test binary (see the file for why).
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)
 
-# --- retroplug-dsp-kernels: the DSP the kit compilers run, as kernels (src/dsp/,
-# docs/modular-graph-phase-d1-design.md section 4): gain and peak, the RBJ biquads, dither / quantise, and risa's DMC
-# encoder (a whole-buffer kernel, Dpcm.hpp says why; the others stream a block at a time). Core-free on purpose: it
-# links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node package can use it with
-# no core, txiki, backend or r8brain.
+# --- retroplug-dsp-kernels: the DSP the rp.dsp nodes run (src/dsp/, docs/modular-graph-phase-d1-design.md section 4):
+# gain and peak, the RBJ biquads, dither / quantise, and risa's DMC encoder (a whole-buffer kernel, Dpcm.hpp says why;
+# the others stream a block at a time). A kit compile is a chain of those nodes, so it reaches this DSP through them.
+# Core-free on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node
+# package can use it with no core, txiki, backend or r8brain.
 # (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.)
 #
 # The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
@@ -57,7 +57,7 @@ target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_D
 set_target_properties(retroplug-dsp-kernels PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp node package
 
 # r8brain's static members are vendored code, so they get a target of their own rather than riding in one of ours:
 # /W4 /WX is scoped to RetroPlug's own targets (cmake/warnings.cmake), which this one is deliberately not on, and
@@ -72,7 +72,7 @@ target_include_directories(retroplug-r8brain-base PRIVATE
 set_target_properties(retroplug-r8brain-base PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp.resample node package
 retroplug_silence_dep_warnings(retroplug-r8brain-base)
 
 # Threads: r8brain's filter cache is a std::mutex, efsw-static needs pthreads on Linux (its CMake links it to the
@@ -93,7 +93,7 @@ target_link_libraries(retroplug-dsp-resample PRIVATE Threads::Threads retroplug-
 set_target_properties(retroplug-dsp-resample PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-core
+    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp.resample node package
 
 # --- retroplug-core: the shared emulator core, as a neutrally-named static lib. Extracted out of
 # packages/native/cli (which also compiled the CLI-only HarnessRpcService) so the host links ONLY the
@@ -109,20 +109,9 @@ else()
 endif()
 
 add_library(retroplug-core STATIC
-    # Console-neutral kit pipeline: the async compiler + decoded-audio cache are
-    # codec-agnostic (KitCodec.hpp). Per-console packing lives in a codec (LSDj
-    # below; risa NES-DPCM planned).
+    # The decoded-audio cache: an offline job's file inputs decode through it (host/engine/OfflineJobs.cpp), which is
+    # how every kit compile reads a source. Console- and format-neutral.
     ${CMAKE_CURRENT_SOURCE_DIR}/src/kit/SampleCache.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/kit/KitCompiler.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/LsdjKitCodec.cpp
-    # risa NES-DPCM (DMC) codec: reuses the generic kit pipeline (the dsp kernels + enkiTS), swaps the
-    # pack stage for 1-bit delta DPCM + an 8 KB kit bank.
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/risa/RisaDmcCodec.cpp
-    # NOTE: the LSDj sav codec + model were removed — they are now pure TS
-    # (packages/retroplug/src/lsdj), validated by frozen golden vectors. Only the
-    # kit/sample pipeline (kit/* generic + lsdj/{Effects,KitUtil,LsdjKitCodec}) remains in C++.
     # processAlive only, so <windows.h> stays in one TU rather than in every includer of the header
     # (NesEverdriveFifo.hpp pulls it in, which is the whole NES core).
     ${CMAKE_CURRENT_SOURCE_DIR}/src/util/ProcessId.cpp
@@ -156,9 +145,6 @@ target_link_libraries(retroplug-core PUBLIC
     sameboy
     mesen
     rpcpp     # transitively pulls reflectcpp
-    retroplug-dsp-kernels  # the kit path's gain, biquad and dither (src/dsp/)
-    retroplug-dsp-resample # the kit path's r8brain resample
-    enkiTS    # used by KitCompiler for per-sample parallel compile
     miniaudio # header-only audio decoder used by SampleCache
     miniz     # used by projectConfigToZip for --save-rplg
     mgb-rom   # embedded mGB ROM (SameBoyBackend supplies it on load)
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index d2d1e35ee..a143b7a3e 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -69,15 +69,18 @@ retroplug_add_test_binary(
              test/audio/SameBoyModels.test.cpp         # every SameBoyModel (incl. SGB variants) boots + renders
              test/audio/EngineInvoker.test.cpp         # the teardown discards a staged-message payload
              test/audio/Dither.test.cpp                # the kit TPDF dithers are seeded from their input
-             test/audio/KitBaseline.test.cpp           # every kit the compilers build is held to its recorded bank hash
              test/graph/CountingAllocator.cpp          # replaces operator new / delete: the N8 FIFO's [alloc] case
+             # The kit path's effect wrappers, so the dither cases are held to what rp::lsdj::processEffect does to a
+             # buffer, not only to the kernels underneath it (retroplug-graph-test compiles it too, for the two
+             # buffer-reading nodes). It needs the kernels, named in LIBS below.
+             ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
+             retroplug-dsp-kernels                     # Effects.cpp's wrappers call these
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
              RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
-             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg"
-             RP_KIT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/audio/golden")
+             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")
 
 # Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md,
 # -c2-design.md): each system node renders exactly what its core renders driven directly, its inputs and its settings'
@@ -269,8 +272,8 @@ retroplug_add_test_binary(
              test/graph/DspBufferNodes.test.cpp       # the rp.dsp nodes that read a whole buffer: normalize, DPCM, seedFrom
              test/graph/DspResample.test.cpp          # rp.dsp.resample, through real plans, against its kernel
              # The kit path's effect wrappers, so the two buffer-reading nodes are held to what
-             # rp::lsdj::processEffect does to a buffer, not only to the kernels underneath it. They need the kernels
-             # and reflect-cpp (through retroplug-graph), nothing of the core.
+             # rp::lsdj::processEffect does to a buffer, not only to the kernels underneath it. They need the kernels,
+             # named in LIBS below, and nothing of the core.
              ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     LIBS     retroplug-graph
              retroplug-dsp-nodes                      # the rp.dsp package under test
diff --git a/packages/native/node/README.md b/packages/native/node/README.md
index f70677b91..7dcbb9a54 100644
--- a/packages/native/node/README.md
+++ b/packages/native/node/README.md
@@ -59,7 +59,7 @@ vector and the Writer has no steal-on-attach rule.
 The codec opts into rpcpp's `NativeAstCodec` via `ast_view_t`. That is load-bearing: it lets
 `RpcServer` decode method params straight into their typed tuple instead of through `rfl::Generic`,
 which is what keeps an `rfl::Bytestring` crossing as a live `Uint8Array` rather than degrading to an
-array of numbers. `renderAudio` returns raw interleaved f32 and `compileKit` a 16 KB bank; neither
+array of numbers. `renderAudio` returns raw interleaved f32 and `readSram` a battery dump; neither
 should be serialized.
 
 ## Building
diff --git a/packages/native/node/napi/Writer.hpp b/packages/native/node/napi/Writer.hpp
index 6fc55fe3c..7722ca9b2 100644
--- a/packages/native/node/napi/Writer.hpp
+++ b/packages/native/node/napi/Writer.hpp
@@ -196,7 +196,7 @@ class Writer {
         } else if constexpr (std::is_same_v<U, rfl::Bytestring>) {
             // Binary buffers (rfl::Bytestring) cross the bridge as a JS Uint8Array, matching the
             // QuickJS codec's JS_NewUint8ArrayCopy path - NOT a JS array of numbers. This is what
-            // keeps renderAudio / compileKit / readFile off the number-per-byte path.
+            // keeps renderAudio / readSram / readFile off the number-per-byte path.
             napi_value ab   = nullptr;
             void*      dest = nullptr;
             if (napi_create_arraybuffer(env_, _var.size(), &dest, &ab) != napi_ok) {
diff --git a/packages/native/src/dsp/Dpcm.hpp b/packages/native/src/dsp/Dpcm.hpp
index c450cbb36..a8015a616 100644
--- a/packages/native/src/dsp/Dpcm.hpp
+++ b/packages/native/src/dsp/Dpcm.hpp
@@ -2,12 +2,12 @@
 
 // risa's NES DMC encoder, as a whole-buffer kernel (docs/modular-graph-phase-d2-design.md section 4).
 //
-// wav2dmc.py's pipeline, which is what the risa kit codec has always run: the float scaled to the int16 domain and
-// rounded, a one-pole DC blocker in double with its state carried from sample to sample, the 7-bit map into 0..127
-// around 64 (peak-normalised to a 45 swing, or scaled by 63 / 32768), and the 1-bit delta that walks a counter between
-// 0 and 127 two steps at a time. The delta bits are the encoder's output: one per sample, in stream order. The caller
-// packs them least-significant bit first and pads the result to a legal DMC length, because those two are the risa
-// bank's format rather than the encoder's arithmetic (RisaDmcCodec::encode).
+// wav2dmc.py's pipeline, which is what the risa kit compiler runs through rp.dsp.dpcm: the float scaled to the int16
+// domain and rounded, a one-pole DC blocker in double with its state carried from sample to sample, the 7-bit map into
+// 0..127 around 64 (peak-normalised to a 45 swing, or scaled by 63 / 32768), and the 1-bit delta that walks a counter
+// between 0 and 127 two steps at a time. The delta bits are the encoder's output: one per sample, in stream order. The
+// caller packs them least-significant bit first and pads the result to a legal DMC length, because those two are the
+// risa bank's format rather than the encoder's arithmetic (packages/retroplug/src/kit/pack.ts).
 //
 // One call over a whole buffer, with no scratch of its own: the DC blocker's two doubles and a running peak are the
 // whole state, and the second pass recomputes the first pass's samples rather than keeping one int per sample, so a
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index f2bc60615..07e1ebf14 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -132,8 +132,8 @@ struct GuardReport {
 
 OfflineWindow offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
                             std::uint32_t length, const std::optional<OfflineCap>& cap) {
-    // Each step as the kit windows spell it (KitUtil::compileSample, RisaDmcCodec::encode), the casts included: the cap
-    // is the one place their arithmetic differs, and a different rounding there is a different frame count.
+    // Each step as the two kit windows spell it, the casts included: the cap is the one place their arithmetic differs,
+    // and a different rounding there is a different frame count (test/engine-graph/OfflineJobs.test.cpp's refs).
     std::size_t maxFrames = decodedFrames;
     if (cap) {
         if (cap->arithmetic == OfflineCapArithmetic::FloatRatio) {
@@ -354,7 +354,7 @@ OfflineCompletion OfflineJobs::run(Job& job) {
             } else {
                 const auto& f = std::get<OfflineFile>(req.inputs[i]);
                 // A cache of the job's own, so a file edited between two jobs decodes as it is now; the decode is
-                // SampleCache's, the kit compiler's, so the samples cannot drift from what it decodes.
+                // SampleCache's, so the samples are what every kit compile reads from this path.
                 kit::SampleCache cache;
                 const kit::SampleData* d = cache.getOrLoad(f.path);
                 if (!d) return fail(name + ": " + f.path + " could not be read or decoded");
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
index be0e54440..acb295304 100644
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -62,10 +62,10 @@ namespace rp::host {
 
 // How a file input's frame cap scales a frame count at a kit rate back to the decoded rate, reproducing the two kit
 // windows exactly, both of which depend on the decoded rate a submission cannot know:
-//   - FloatRatio, LSDj's (KitUtil::compileSample): ceil(float(frames) * (float(decodedRate) / float(rate))), every
-//     step in float, the ratio first;
-//   - DoubleScale, risa's (RisaDmcCodec::encode): ceil(double(frames) * double(decodedRate) / rate), in double, the
-//     product first.
+//   - FloatRatio, the LSDj window's: ceil(float(frames) * (float(decodedRate) / float(rate))), every step in float,
+//     the ratio first;
+//   - DoubleScale, risa's DMC window: ceil(double(frames) * double(decodedRate) / rate), in double, the product
+//     first.
 enum class OfflineCapArithmetic : std::uint8_t { FloatRatio, DoubleScale };
 
 struct OfflineCap {
@@ -81,8 +81,9 @@ struct OfflineSamples {
     double             rate = 0.0;
 };
 
-// A file decoded through SampleCache (miniaudio, mono float at the file's own rate, the kit compiler's decode) and
-// windowed: from `offset` (clamped to the end), `length` frames (0: to the end), at most the cap's frames.
+// A file decoded through SampleCache (miniaudio, mono float at the file's own rate, the decode a kit compile reads its
+// sources through) and windowed: from `offset` (clamped to the end), `length` frames (0: to the end), at most the
+// cap's frames.
 struct OfflineFile {
     std::string               path;
     std::uint32_t             offset = 0;
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index 56b3b3523..acf424ad1 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -59,14 +59,12 @@ void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::stageNodeMidi>(e);
 }
 
-// --- harness: audio render / transport / MIDI + DSP profiling + kit compile + graph drops (22; CLI + tests only) ---
+// --- harness: audio render / transport / MIDI + DSP profiling + graph drops (20; CLI + tests only) ---
 template <class Server>
 void registerHarnessRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::renderAudio>(e);
     s.template addMethod<&EngineRpcService::renderAudioPerSystem>(e);
     s.template addMethod<&EngineRpcService::renderAudioPerChannel>(e);
-    s.template addMethod<&EngineRpcService::compileKit>(e);
-    s.template addMethod<&EngineRpcService::compileDmc>(e);
     s.template addMethod<&EngineRpcService::sampleRate>(e);
     s.template addMethod<&EngineRpcService::setSampleRate>(e);
     s.template addMethod<&EngineRpcService::setTransport>(e);
@@ -129,7 +127,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (92 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (90 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index 56be281da..d88b0e7c0 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -7,8 +7,6 @@
 
 #include <rfl/Bytestring.hpp>
 
-#include "lsdj/Effects.hpp"  // rp::lsdj::LsdjEffect (tagged union: gain/filter/dither)
-
 // Shared DTOs for the Backend RPC surface (packages/retroplug/src/backend.ts).
 // Binary crosses as rfl::Bytestring in BOTH directions (a JS Uint8Array): the qjs codec decodes a
 // typed byte param straight into rfl::Bytestring (rpcpp's NativeAstCodec path), not a JSON int-array.
@@ -22,36 +20,6 @@ struct BackendZipInput { std::string name; rfl::Bytestring bytes; };  // zip inp
 // generic codec crossing so font import/export works from the plugin as well as the CLI.
 struct PngImage { std::uint32_t width; std::uint32_t height; rfl::Bytestring rgba; };
 
-// One source sample for the LSDJ kit compiler (harness-facet compileKit): a source audio file (WAV/MP3/
-// FLAC, decoded natively by SampleCache/miniaudio), a 3-char slot name, an optional source window, and an
-// effect chain (gain/filter/dither) applied during compilation. Mirrors rp::lsdj::CompileSampleSpec.
-struct KitSampleSpec {
-    std::string                       path;
-    std::string                       name;
-    std::optional<std::uint32_t>      offset;
-    std::optional<std::uint32_t>      length;
-    std::vector<rp::lsdj::LsdjEffect> effects;
-};
-// A whole-kit compile request: a 6-char kit name + up to 15 samples (extra dropped). `rotate` is the LSDJ
-// 9.2.0+ frame rotation, derived from the TARGET ROM's version (absent → true). Output = a 16 KB kit bank
-// (rfl::Bytestring) ready to drop into a ROM slot.
-struct KitCompileSpec { std::string name; std::vector<KitSampleSpec> samples; std::optional<bool> rotate; };
-
-// One source sample for the risa NES-DPCM kit compiler (harness-facet compileDmc): the same source/window/
-// effects as a kit sample, plus the DMC playback-rate index (0..15 into the PAL rate table), a loop flag,
-// and a 7-bit peak-normalize flag. Mirrors rp::risa::CompileDmcSampleSpec.
-struct RisaDmcSampleSpec {
-    std::string                       path;
-    std::string                       name;
-    std::optional<std::uint32_t>      offset;
-    std::optional<std::uint32_t>      length;
-    std::vector<rp::lsdj::LsdjEffect> effects;
-    std::optional<std::uint32_t>      rate;      // PAL DPCM rate index 0..15 (default 12)
-    std::optional<bool>               loop;
-    std::optional<bool>               normalize; // 7-bit peak-normalize (default true)
-};
-struct RisaKitCompileSpec { std::string name; std::vector<RisaDmcSampleSpec> samples; };
-
 // Monotonic audio-capture snapshot published by the background audio thread (energy = sum of
 // squared samples, frames = total). A control-thread reader diffs two snapshots for a windowed RMS.
 struct AudioCaptured { double energy; std::uint64_t frames; };
@@ -179,10 +147,10 @@ struct RpcJobCompletion {
 // host/engine/OfflineJobs.hpp): a graph document run to completion on a host of its own. Its resources are its inputs
 // then its outputs, in order, named in the document as res:1, res:2 and on; `returns` names the handles whose buffers
 // the completion carries. An input is EITHER audio the call carries (`samples`, interleaved float32 in the host's byte
-// order, little-endian on every target, `channels` wide, at `rate`) OR a file native decodes (`path`, through the kit
-// compiler's SampleCache decode, mono at the file's rate) and windows (`offset`, `length`, 0 or absent for all of it,
+// order, little-endian on every target, `channels` wide, at `rate`) OR a file native decodes (`path`, through
+// SampleCache's miniaudio decode, mono at the file's rate) and windows (`offset`, `length`, 0 or absent for all of it,
 // and a `cap`); a field of the other kind is refused. A cap is `frames` at `rate` scaled to the decoded rate by
-// `arithmetic`: "float-ratio" (LSDj's compileSample) or "double-scale" (risa's DMC encode). `document` absent or empty
+// `arithmetic`: "float-ratio" (the LSDj window's) or "double-scale" (risa's DMC one). `document` absent or empty
 // runs no graph, so the job returns its inputs (a file's window, and its report).
 struct RpcOfflineCap { std::uint32_t frames; double rate; std::string arithmetic; };
 struct RpcOfflineInput {
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 68554b0a7..dd4be39bf 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -16,10 +16,6 @@
 
 #include "EmbeddedRoms.hpp"
 #include "system/SystemBase.hpp"
-
-#include "kit/KitCompiler.hpp"       // rp::kit::KitCompiler (generic, compileKit) - heavy, so only in this TU
-#include "lsdj/LsdjKitCodec.hpp"     // rp::lsdj::LsdjKitCodec (the LSDJ nibble/16KB-bank codec)
-#include "risa/RisaDmcCodec.hpp"     // rp::risa::RisaDmcCodec (the risa NES-DPCM/8KB-bank codec)
 #include "util/BuildTarget.hpp"      // rp::buildTarget (shared with the kit baseline's golden)
 #include "util/SlurpFile.hpp"
 
@@ -53,9 +49,6 @@ bool EngineRpcService::rendersInto(std::size_t channels) const {
     return false;
 }
 
-// Out-of-line so unique_ptr<KitCompiler> can destroy a complete type (fwd-declared in the header).
-EngineRpcService::~EngineRpcService() = default;
-
 RpcPutResourceResult EngineRpcService::putResource(BackendResourceSpec spec) {
     RpcPutResourceResult out{std::nullopt, std::nullopt};
     // A node reads a resource as one of three things (a system's ROM, its battery, a savestate), so the wire takes
@@ -456,57 +449,6 @@ std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerChannel(std::uint32
     return result;
 }
 
-rfl::Bytestring EngineRpcService::compileKit(KitCompileSpec spec) {
-    // Lazily build the compiler on first use — it spins up an enkiTS pool + a SampleCache, which the
-    // plugin (which also constructs this service) must never pay for.
-    if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::kit::KitCompiler>();
-
-    const bool rotate = spec.rotate.value_or(true);   // per the target ROM's LSDj version (9.2.0+ rotates)
-    std::vector<rp::lsdj::CompileSampleSpec> specs;
-    specs.reserve(spec.samples.size());
-    for (auto& s : spec.samples) {
-        rp::lsdj::CompileSampleSpec cs;
-        cs.path    = s.path;
-        cs.name    = s.name;
-        cs.offset  = s.offset.value_or(0);
-        cs.length  = s.length.value_or(0);
-        cs.effects = s.effects;
-        cs.rotate  = rotate;
-        specs.push_back(std::move(cs));
-    }
-
-    // The generic compiler drives the LSDJ codec (nibble pack + 16 KB bank). Always returns a bank; a
-    // per-sample load failure just leaves that slot empty. The caller (CLI) validates by reading the
-    // bank back, so no exception crosses the RPC boundary here.
-    rp::lsdj::LsdjKitCodec codec(spec.name, std::move(specs));
-    rp::kit::CompiledKit kit = kitCompiler_->compile(codec);
-    return toBytestring(kit.bytes);
-}
-
-rfl::Bytestring EngineRpcService::compileDmc(RisaKitCompileSpec spec) {
-    // Reuse the same lazy compiler as compileKit — it's codec-agnostic (enkiTS pool + SampleCache).
-    if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::kit::KitCompiler>();
-
-    std::vector<rp::risa::CompileDmcSampleSpec> specs;
-    specs.reserve(spec.samples.size());
-    for (auto& s : spec.samples) {
-        rp::risa::CompileDmcSampleSpec cs;
-        cs.path      = s.path;
-        cs.name      = s.name;
-        cs.offset    = s.offset.value_or(0);
-        cs.length    = s.length.value_or(0);
-        cs.effects   = s.effects;
-        cs.rate      = static_cast<std::uint8_t>(s.rate.value_or(12) & 0x0F);
-        cs.loop      = s.loop.value_or(false);
-        cs.normalize = s.normalize.value_or(true);
-        specs.push_back(std::move(cs));
-    }
-
-    rp::risa::RisaDmcCodec codec(spec.name, std::move(specs));
-    rp::kit::CompiledKit kit = kitCompiler_->compile(codec);
-    return toBytestring(kit.bytes);
-}
-
 double EngineRpcService::sampleRate() const {
     return engine_.sampleRate();
 }
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index a1fad2038..caa5d3f64 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -1,7 +1,6 @@
 #pragma once
 
 #include <cstdint>
-#include <memory>
 #include <optional>
 #include <string>
 #include <vector>
@@ -14,7 +13,6 @@
 
 class Engine;
 class QueuedInvoker;
-namespace rp::kit { class KitCompiler; }  // fwd: lazy, kept out of the header (enkiTS/r8brain heavy)
 
 // The emulator surface (lifecycle / reads / MIDI / transport / profiling) as a THIN RPC layer over
 // (Engine + the one Invoker). No threading branches: every mutation just pushes onto
@@ -23,7 +21,6 @@ namespace rp::kit { class KitCompiler; }  // fwd: lazy, kept out of the header (
 class EngineRpcService {
 public:
     EngineRpcService(Engine& engine, QueuedInvoker& invoker);
-    ~EngineRpcService();   // out-of-line: kitCompiler_ holds a fwd-declared KitCompiler
 
     // --- emulator lifecycle / reads ---
     // (duplicate, reload and remove live in the TS SystemsStore over the resource calls and setGraph.)
@@ -94,14 +91,6 @@ public:
     // PCM (Game Boy = 4: Pulse 1/Pulse 2/Wave/Noise). Single-system only (empty if systemCount() != 1 or
     // `id` is unknown) - the PerChannel shape keys off the stream, not the slot. Marshals to Uint8Array[].
     std::vector<rfl::Bytestring> renderAudioPerChannel(std::uint32_t id, double ms);
-    // Compile an LSDJ sample kit from source audio files → a 16 KB kit bank (harness/tooling only; the
-    // plugin never binds this). Resample (r8brain) + effects + 4-bit nibble-pack per sample, fanned across
-    // an enkiTS pool by the owned KitCompiler; a per-sample load failure just leaves that slot empty.
-    rfl::Bytestring compileKit(KitCompileSpec spec);
-    // Compile a risa NES-DPCM sample kit → an 8 KB kit bank (harness/tooling only). Same generic pipeline
-    // as compileKit (reuses the lazy kitCompiler_) but drives the DMC codec: r8brain resample to the PAL
-    // DPCM rate + 1-bit ±2 delta encode + 64-byte-aligned pack.
-    rfl::Bytestring compileDmc(RisaKitCompileSpec spec);
     // The engine's audio sample rate (Hz), so callers can label WAV output correctly.
     double          sampleRate() const;
     // Set the host sample rate (Hz). Baked into each core at construct, so it only takes effect BEFORE any
@@ -172,6 +161,5 @@ private:
     std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
     std::vector<float>       scratchR_;
     std::vector<RpcMidiOut> accumMidiOut_;  // host MIDI out gathered across a render window (drainMidiOut)
-    std::unique_ptr<rp::kit::KitCompiler> kitCompiler_;  // lazy: built on first compileKit (enkiTS pool)
     rp::host::HarnessSurfaceLink* harnessSurface_ = nullptr;  // the headless host's own surface, if it has one
 };
diff --git a/packages/native/src/kit/SampleCache.hpp b/packages/native/src/kit/SampleCache.hpp
index 6ed197da0..1f6d594cb 100644
--- a/packages/native/src/kit/SampleCache.hpp
+++ b/packages/native/src/kit/SampleCache.hpp
@@ -8,15 +8,12 @@
 #include <unordered_map>
 #include <vector>
 
-// Decoded-audio cache used by the kit compiler. Loads source files via
-// miniaudio (WAV / MP3 / FLAC), keeps them in memory as mono float32, and
-// dedupes by content hash (so two paths to identical bytes share one entry,
+// Decoded-audio cache used by the offline job seam's file inputs. Loads source files via miniaudio (WAV / MP3 / FLAC),
+// keeps them in memory as mono float32, and dedupes by content hash (so two paths to identical bytes share one entry,
 // and editing a file in place re-loads on cache miss).
 //
-// Console-neutral: nothing here is LSDj- or NES-specific. The per-format
-// resample + pack lives behind a `KitCodec` (see KitCodec.hpp); this cache is
-// shared across every codec so a live batch re-patch (moving a slider) only
-// re-decodes sources that actually changed.
+// Console-neutral: nothing here is LSDj- or NES-specific. The per-format resample + pack lives in the TypeScript kit
+// compiler, whose every source is decoded through this cache and whose window native applies at input creation.
 
 namespace rp::kit {
 
diff --git a/packages/native/src/lsdj/Effects.cpp b/packages/native/src/lsdj/Effects.cpp
index 18902e8ed..c711cb9eb 100644
--- a/packages/native/src/lsdj/Effects.cpp
+++ b/packages/native/src/lsdj/Effects.cpp
@@ -56,12 +56,12 @@ void processEffect(const FilterEffect& effect,
     kernel.process(target.data(), target.data(), target.size());
 }
 
-// The seed is the whole buffer's, hashed before the first code: the input the dither is handed, which for
-// `KitUtil::compileSample` is the windowed, post-effect, resampled sample at the Game Boy kit rate (11468 Hz) BEFORE
-// `frameAlign` trims it to whole frames - not the source-rate window the gain and filter see, and not the trimmed
-// bank. Drawing it from the input is what makes a compile reproducible: a kit bank is a file, and two compiles of the
-// same sample have to be the same file. A whole bank is byte-identical across hosts only where the resampler and the
-// filters agree too, which is why exact kit bytes are compared on one reference target.
+// The seed is the whole buffer's, hashed before the first code: the input the dither is handed, which for the kit path
+// is the windowed, post-effect, resampled sample at the Game Boy kit rate (11468 Hz) BEFORE it is trimmed to whole
+// 32-sample frames - not the source-rate window the gain and filter see, and not the trimmed bank. Drawing it from the
+// input is what makes a compile reproducible: a kit bank is a file, and two compiles of the same sample have to be the
+// same file. A whole bank is byte-identical across hosts only where the resampler and the filters agree too, which is
+// why exact kit bytes are compared on one reference target.
 std::vector<std::uint16_t> quantise(const std::vector<float>& buffer, const DitherEffect* dither) {
     std::vector<std::uint16_t> codes(buffer.size());
     dsp::Dither kernel;
diff --git a/packages/native/src/lsdj/Effects.hpp b/packages/native/src/lsdj/Effects.hpp
index aa63c79c3..999975bc5 100644
--- a/packages/native/src/lsdj/Effects.hpp
+++ b/packages/native/src/lsdj/Effects.hpp
@@ -1,27 +1,17 @@
 #pragma once
 
-// <algorithm> and <cmath> are kept for whoever reaches them through this header: host/rpc/BackendTypes.hpp re-exports
-// it across the plugin's DSP surface, and kPi's only caller is test/audio/Dither.test.cpp.
-#include <algorithm>
-#include <cmath>
 #include <cstdint>
 #include <vector>
 
-#include <rfl/Literal.hpp>
-#include <rfl/TaggedUnion.hpp>
-
-// Sample-processing chain for LSDJ kit compilation. Ported from
-// the legacy build's Effects.h + AudioDithering.h.
+// The kit path's sample-processing effects, as whole-buffer wrappers. Ported from the legacy build's Effects.h +
+// AudioDithering.h.
 //
-// Each effect is a plain-data struct that round-trips via reflectcpp; the
-// `LsdjEffect` TaggedUnion below is the serialized effect descriptor, which the
-// compileKit / compileDmc RPCs decode. The gain and filter `processEffect(...)`
-// overloads run in `KitUtil::compileSample` and `RisaDmcCodec::encode` against a
-// float buffer in [-1, 1]; the dither one is the 4-bit codes as floats, which
-// `KitUtil::compileSample` gets from `quantise` instead, so its only caller is
-// test/audio/Dither.test.cpp. All of them are whole-buffer loops over the
-// rp::dsp kernels (src/dsp/), which hold the arithmetic; this file only maps an
-// effect's fields onto a kernel's.
+// The gain and filter `processEffect(...)` overloads take a float buffer in [-1, 1] at the rate the chain runs them
+// at; the dither one takes the kit rate's buffer and leaves the 4-bit codes as floats, through `quantise` below. All
+// of them are whole-buffer loops over the rp::dsp kernels (src/dsp/), which hold the arithmetic; this file only maps
+// an effect's fields onto a kernel's. Nothing serialises these structs: a kit compile is a chain of `rp.dsp` nodes, and
+// the wrappers are the kit-path reference the tests hold to, bit for bit: test/graph/DspBufferNodes.test.cpp holds the
+// nodes to them, and test/audio/Dither.test.cpp holds the dither effect's seed.
 
 namespace rp::lsdj {
 
@@ -31,8 +21,6 @@ inline constexpr float kPi = 3.14159265358979323846f;
 // GainEffect — uniform amplification with optional peak-normalisation.
 // -----------------------------------------------------------------------
 struct GainEffect {
-    using Tag = rfl::Literal<"gain">;
-
     bool  normalize = true;
     float gain      = 1.0f;
 };
@@ -58,8 +46,6 @@ enum class FilterType : std::uint32_t {
 };
 
 struct FilterEffect {
-    using Tag = rfl::Literal<"filter">;
-
     FilterType filterType = FilterType::LowPass;
     float      frequency  = 5734.0f;   // GAMEBOY_SAMPLE_RATE / 2; nyquist for 11468 Hz
     float      q          = 1.0f;
@@ -89,8 +75,6 @@ enum class DitherType : std::uint32_t {
 };
 
 struct DitherEffect {
-    using Tag = rfl::Literal<"dither">;
-
     DitherType ditherType = DitherType::HighPassTPDF;
 };
 
@@ -105,8 +89,4 @@ void processEffect(const DitherEffect& effect,
 std::vector<std::uint16_t> quantise(const std::vector<float>& buffer,
                                     const DitherEffect* dither);
 
-// `LsdjEffect` is the on-disk variant; serialised under the discriminator
-// key `"type"` to match the legacy spelling.
-using LsdjEffect = rfl::TaggedUnion<"type", GainEffect, FilterEffect, DitherEffect>;
-
 } // namespace rp::lsdj
diff --git a/packages/native/test/audio/Dither.test.cpp b/packages/native/test/audio/Dither.test.cpp
index 667f2c501..ba6555971 100644
--- a/packages/native/test/audio/Dither.test.cpp
+++ b/packages/native/test/audio/Dither.test.cpp
@@ -1,14 +1,14 @@
 // The kit path's two TPDF dithers, seeded from the samples they quantise.
 //
-// `KitUtil::compileSample` hands the dither the WINDOWED, POST-EFFECT, RESAMPLED buffer at the Game Boy kit rate
-// (11468 Hz), before `frameAlign` trims it to whole 32-sample frames, so that is the input the seed is drawn from.
-// Every case here calls the effect directly on a buffer at that rate, the way the kit path does, and holds what the
-// seed has to give: a compile gives the same bytes for the same input, two different samples do not share one noise
-// sequence (which a single fixed seed would), and the noise is a specified function of the input and the algorithm,
-// so it is the same on any host whose float arithmetic is IEEE-754 - the draw comes from raw `mt19937` output and the
-// hash reads the floats' bytes little-endian, neither of which depends on the standard library's or the host's choices.
-// A whole kit bank is byte-identical only where the resampler and the filters agree as well, which is why the exact
-// bank bytes are compared on one reference target.
+// The kit compiler hands the dither the WINDOWED, POST-EFFECT, RESAMPLED buffer at the Game Boy kit rate (11468 Hz),
+// before it trims it to whole 32-sample frames, so that is the input the seed is drawn from. Every case here calls the
+// effect directly on a buffer at that rate, the way the kit path does, and holds what the seed has to give: a compile
+// gives the same bytes for the same input, two different samples do not share one noise sequence (which a single fixed
+// seed would), and the noise is a specified function of the input and the algorithm, so it is the same on any host
+// whose float arithmetic is IEEE-754 - the draw comes from raw `mt19937` output and the hash reads the floats' bytes
+// little-endian, neither of which depends on the standard library's or the host's choices. A whole kit bank is
+// byte-identical only where the resampler and the filters agree as well, which is why the exact bank bytes are
+// compared on one reference target (test-native/kit-baseline.test.ts, over the graph jobs).
 //
 // Run via `pnpm test:plugin`.
 
@@ -20,11 +20,11 @@
 #include <catch2/catch_test_macros.hpp>
 
 #include "lsdj/Effects.hpp"
-#include "lsdj/KitUtil.hpp"
+#include "lsdj/KitBank.hpp"
 
 namespace {
 
-// The rate the dither runs at: the kit's target rate, which is also its input rate once `compileSample` has resampled.
+// The rate the dither runs at: the kit's target rate, which is also its input rate once the kit compiler has resampled.
 constexpr float kKitRate = static_cast<float>(rp::lsdj::kGameboySampleRate);
 
 /** A decaying tone as a source sample: something with real 4-bit structure for the dither to quantise. */
diff --git a/packages/native/test/engine-graph/OfflineJobs.test.cpp b/packages/native/test/engine-graph/OfflineJobs.test.cpp
index e952618d2..4202529bb 100644
--- a/packages/native/test/engine-graph/OfflineJobs.test.cpp
+++ b/packages/native/test/engine-graph/OfflineJobs.test.cpp
@@ -1,13 +1,13 @@
 // Offline jobs (docs/modular-graph-phase-d2-design.md section 3, host/engine/OfflineJobs.hpp): a graph document run to
 // completion on a host of its own, on a worker of the engine's offline pool. A job returns exactly what the same nodes
 // give through a GraphRig run directly; jobs past the pool's concurrency queue and each returns its own result; a
-// cancel stops a queued job at once and a running one at its next block, with the cancel reason; an engine destroyed with
-// jobs in flight abandons them and joins every worker; the outstanding bound refuses; every refusal says why, at
-// submission or in the completion; a file input is SampleCache's decode, windowed exactly as the two kit compilers
-// window today, the cap's two arithmetics and every edge included, with its report; a kit-shaped job names one buffer
-// twice - as the normalising source's and as the dither's `seedFrom` - beside the DPCM encode, and gives the kit
-// arithmetic for all three; and a job run while an audio thread plays the live plan leaves that plan's output bit for
-// bit, its warmed blocks allocating nothing.
+// cancel stops a queued job at once and a running one at its next block, with the cancel reason; an engine destroyed
+// with jobs in flight abandons them and joins every worker; the outstanding bound refuses; every refusal says why, at
+// submission or in the completion; a file input is SampleCache's decode, windowed exactly as the two kit windows
+// spell it (LSDj's and risa's), the cap's two arithmetics and every edge included, with its report; a kit-shaped job
+// names one buffer twice - as the normalising source's and as the dither's `seedFrom` - beside the DPCM encode, and
+// gives the kit arithmetic for all three; and a job run while an audio thread plays the live plan leaves that plan's
+// output bit for bit, its warmed blocks allocating nothing.
 //
 // Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[offline]"`; the hidden
 // "[offline-bench]" times a trivial job and a one-second resample.
@@ -47,8 +47,7 @@
 #include "host/graph/HostServices.hpp"
 #include "host/rpc/EngineRpcService.hpp"
 #include "kit/SampleCache.hpp"
-#include "lsdj/KitUtil.hpp"
-#include "risa/RisaDmcCodec.hpp"
+#include "lsdj/KitBank.hpp"
 
 using rp::host::OfflineCap;
 using rp::host::OfflineCapArithmetic;
@@ -264,21 +263,21 @@ struct TempDir {
     }
 };
 
-// The PAL DMC rates (RisaDmcCodec.cpp's table), the risa cap's rate.
+// The PAL DMC rates (wav2dmc.py's table, which the risa kit compiler's rate index picks from), the risa cap's rate.
 constexpr double kPalRates[16] = {
     4177.40,  4696.63,  5261.41,  5579.22,  6023.94,  7044.94,  7917.18,  8397.01,
     9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
 };
-constexpr std::uint32_t kLsdjCapFrames = static_cast<std::uint32_t>(rp::lsdj::Kit::kMaxSampleFrames);
-constexpr std::uint32_t kRisaCapFrames = 4081u * 8u;  // RisaDmcCodec's kDmcMaxBytes in bits
+constexpr std::uint32_t kLsdjCapFrames = static_cast<std::uint32_t>(rp::lsdj::kMaxSampleFrames);
+constexpr std::uint32_t kRisaCapFrames = 4081u * 8u;  // the DMC's longest stream (4081 bytes) in bits
 
-// Today's two windows, copied from their homes as they compute them: what the job's window must reproduce.
+// The two kit windows, written out as references: what the job's window must reproduce.
 std::pair<std::size_t, std::size_t> lsdjWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                                std::size_t length) {
-    // KitUtil::compileSample
+    // LSDj's: the cap is 32576 kit frames at 11468 Hz, measured back to the decoded rate in float, ratio first.
     const float srRatio = static_cast<float>(sampleRate) / static_cast<float>(rp::lsdj::kGameboySampleRate);
     const std::size_t maxFrames = static_cast<std::size_t>(
-        std::ceil(static_cast<float>(rp::lsdj::Kit::kMaxSampleFrames) * srRatio));
+        std::ceil(static_cast<float>(rp::lsdj::kMaxSampleFrames) * srRatio));
     const std::size_t length0 = (length == 0) ? size : length;
     const std::size_t startFrame = std::min(offset, size);
     return {startFrame, std::min(maxFrames, std::min(length0, size - startFrame))};
@@ -286,7 +285,7 @@ std::pair<std::size_t, std::size_t> lsdjWindow(std::size_t size, std::uint32_t s
 
 std::pair<std::size_t, std::size_t> risaWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                                std::size_t length, double targetRate) {
-    // RisaDmcCodec::encode
+    // risa's: the cap is 4081 DMC bytes (32648 bits) at the slot's PAL rate, measured back in double, product first.
     const double srcRate = static_cast<double>(sampleRate);
     const std::size_t maxOutFrames =
         static_cast<std::size_t>(std::ceil(static_cast<double>(4081 * 8) * srcRate / targetRate));
@@ -886,7 +885,7 @@ TEST_CASE("the offline window is each kit compiler's window, the cap's arithmeti
     CHECK(differs);
 }
 
-TEST_CASE("a file input is SampleCache's decode, windowed as today's kit compilers window it, with its report",
+TEST_CASE("a file input is SampleCache's decode, windowed as the two kit windows spell it, with its report",
           "[engine-graph][offline]") {
     TempDir     dir;
     OfflineJobs jobs;
@@ -971,8 +970,8 @@ TEST_CASE("a file input is SampleCache's decode, windowed as today's kit compile
     CHECK_FALSE(cut.inputs[0].nonFinite);
 
     // An empty window is an input of no frames: reported, returned as a buffer of none at the decoded rate, and named by
-    // no resource. Today's compilers make of it an LSDj slot of no bytes and a one-byte risa slot (a legal 16k+1
-    // length), which a TS compiler reproduces from that report.
+    // no resource. The kit compiler makes of it an LSDj slot of no bytes and a one-byte risa slot (a legal 16k+1
+    // length) from that report; test-native/kit-compile.test.ts holds both banks (its "EMP" slots).
     const auto empty = one(jobs, jobs.submit(fileRequest(nanPath, 5000, 0, lsdjCap())).id);
     REQUIRE(empty.ok);
     CHECK(empty.inputs[0].frames == 0);
@@ -980,18 +979,6 @@ TEST_CASE("a file input is SampleCache's decode, windowed as today's kit compile
     CHECK(empty.buffers[0].frames == 0);
     CHECK(empty.buffers[0].samples.empty());
     CHECK(empty.buffers[0].rate == 32000.0);
-    rp::lsdj::KitUtil::SampleInput lsdj;
-    lsdj.data = withNan;
-    lsdj.sampleRate = 32000;
-    lsdj.offset = 5000;
-    CHECK(rp::lsdj::KitUtil::compileSample(lsdj).empty());
-    rp::risa::CompileDmcSampleSpec spec;
-    spec.offset = 5000;
-    const rp::risa::RisaDmcCodec risa("K", {spec});
-    rp::kit::SampleData data;
-    data.buffer = withNan;
-    data.sampleRate = 32000;
-    CHECK(risa.encode(0, data) == std::vector<std::uint8_t>{0});
 }
 
 TEST_CASE("an offline job leaves the live plan an audio thread plays bit for bit, and its warmed blocks allocate nothing",
diff --git a/packages/native/test/graph/Dsp.test.cpp b/packages/native/test/graph/Dsp.test.cpp
index 19da8f4e2..42e59120c 100644
--- a/packages/native/test/graph/Dsp.test.cpp
+++ b/packages/native/test/graph/Dsp.test.cpp
@@ -6,22 +6,23 @@
 //   whole of it gives, bit for bit: every kernel carries its state across the split.
 // - A warmed process allocates and frees nothing (the counting allocator; the C library's blocks are counted too
 //   where this build can see them, which matters for r8brain, whose own allocator is malloc).
-// - Biquad: the eight coefficient calculators and the loop, written out here from lsdj/Effects.cpp, give the
-//   kernel's coefficients (every field bit for bit) and its filtered buffer sample for sample.
+// - Biquad: the eight coefficient calculators and the loop, written out here from 12905a59's lsdj/Effects.cpp before
+//   the arithmetic moved into src/dsp/, give the kernel's coefficients (every field bit for bit) and its filtered
+//   buffer sample for sample.
 // - Dither: the same seed gives the same codes and a different seed different noise; the four algorithms without a
-//   padded reference (HighPassTPDF, ShapedTPDF, ErrorDiffusion, None) give the old whole-buffer loops' codes; Sierra
-//   Lite and JJN, kept as a few past errors, give the codes their padded whole-buffer arithmetic gives; and the seed
-//   drawn from a buffer gives the codes test/audio/Dither.test.cpp pins through the kit effect. (inputSeed's
+//   padded reference (HighPassTPDF, ShapedTPDF, ErrorDiffusion, None) give the kit path's whole-buffer loops' codes;
+//   Sierra Lite and JJN, kept as a few past errors, give the codes their padded whole-buffer arithmetic gives; and the
+//   seed drawn from a buffer gives the codes test/audio/Dither.test.cpp pins through the kit effect. (inputSeed's
 //   big-endian half cannot be exercised on a little-endian host; the two halves are held to one value by construction,
 //   not by a case here.)
 // - Resample: the output stream is independent of how the input arrives (a source shorter than one chunk included),
 //   and equals a direct fixed-chunk feed of r8brain, written out here as a reference; a failed allocation in `init`
 //   throws and leaves the resampler unbuilt.
-// - DPCM: the kernel's one bit per sample, packed the way the risa codec packs them, is the codec's old whole-buffer
-//   function's bytes, written out here verbatim: normalising or not, a short buffer, a long one, an overdriven one and
-//   silence.
+// - DPCM: the kernel's one bit per sample, packed the way the risa bank packs them, is the whole-buffer encoder's bytes
+//   written out here verbatim: normalising or not, a short buffer, a long one, an overdriven one and silence.
 //
-// The kit baseline (test/audio/KitBaseline.test.cpp) holds the same kernels to whole kit banks' bytes.
+// The kit baseline (test-native/kit-baseline.test.ts) holds the same kernels to whole kit banks' bytes, through the
+// rp.dsp nodes' offline graph jobs.
 #include <catch2/catch_test_macros.hpp>
 
 #include <algorithm>
@@ -155,11 +156,11 @@ std::vector<float> paddedJjn(std::vector<float> buf, int bitDepth) {
     return buf;
 }
 
-// ---- the kit path's dithers as they were before the kernel ---------------------------------------------------------
-// Verbatim from 12905a59's lsdj/Effects.cpp (the two TPDF algorithms and error diffusion) and KitUtil.cpp's no-dither
-// branch. The RNG is handed in rather than drawn from the buffer here: the kernel salts `inputSeed` itself, so the
-// reference starts from `mt19937(inputSeed ^ salt)`, the word the kernel's `init` seeds. Codes as floats in [0, 2^bits
-// - 1], the same shape the padded references give.
+// ---- the kit path's dithers, written out as independent references --------------------------------------------------
+// The two TPDF algorithms and error diffusion, verbatim from 12905a59's lsdj/Effects.cpp before the arithmetic moved
+// into src/dsp/, with the kit path's no-dither branch beside them. The RNG is handed in rather than drawn from the
+// buffer here: the kernel salts `inputSeed` itself, so the reference starts from `mt19937(inputSeed ^ salt)`, the word
+// the kernel's `init` seeds. Codes as floats in [0, 2^bits - 1], the same shape the padded references give.
 
 constexpr std::uint32_t kHighPassTPDFSalt = 0x8064fcc5u; // fold(fnv1a64("HighPassTPDF"))
 constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fold(fnv1a64("ShapedTPDF"))
@@ -234,8 +235,8 @@ std::vector<float> errorDiffusion(std::vector<float>& buf, int bitDepth) {
     return buf;
 }
 
-/** The no-dither branch of the old `KitUtil::compileSample` (clamp, scale to [0, 1], round to the code), with the kit
- *  path's hard-coded 15.0f written as the maximum any depth gives. */
+/** The kit path's no-dither quantiser (clamp, scale to [0, 1], round to the code): rp.dsp.dither's None at 4 bits,
+ *  with the kit's hard-coded 15.0f written as the maximum any depth gives. */
 std::vector<float> noDither(std::vector<float>& buf, int bitDepth) {
     const float maxValue = static_cast<float>((1 << bitDepth) - 1);
     for (float& v : buf) {
@@ -711,8 +712,8 @@ TEST_CASE("codes map back to the quantised levels", "[dsp][dither]") {
     CHECK(rp::dsp::codeToLevel(15, 4) == 1.0f);
     CHECK(rp::dsp::codeToLevel(1, 1) == 1.0f);
     CHECK(rp::dsp::codeToLevel(65535, 16) == 1.0f);
-    // The LSDj kit decoder's level for a 4-bit code (SampleUtil::convertNibblesToF32), so a dither node's audio is
-    // what the Game Boy would play.
+    // The LSDj kit decoder's level for a 4-bit code (code / 15 * 2 - 1), so a dither node's audio is what the Game Boy
+    // would play.
     for (std::uint32_t c = 0; c < 16; ++c) {
         CHECK(rp::dsp::codeToLevel(c, 4) == (static_cast<float>(c) / 15.0f) * 2.0f - 1.0f);
     }
diff --git a/packaging/licenses/THIRD-PARTY-NOTICES.txt b/packaging/licenses/THIRD-PARTY-NOTICES.txt
index 09c5e1b15..4b6478852 100644
--- a/packaging/licenses/THIRD-PARTY-NOTICES.txt
+++ b/packaging/licenses/THIRD-PARTY-NOTICES.txt
@@ -39,7 +39,6 @@ rpcpp             MIT               (c) 2026 Tom Yaxley                      htt
 reflect-cpp       MIT               (c) 2023 Code17 GmbH                     https://github.com/getml/reflect-cpp
 concurrentqueue   Boost-1.0         (c) 2013-2020 Cameron Desrochers        https://github.com/cameron314/concurrentqueue
 r8brain-free-src  MIT               (c) 2013-2022 Aleksey Vaneev            https://github.com/avaneev/r8brain-free-src
-enkiTS            Zlib              (c) 2013 Doug Binks                      https://github.com/dougbinks/enkiTS
 miniz             MIT               (c) 2013-2014 RAD Game Tools, R.Geldreich https://github.com/richgel999/miniz
 miniaudio         public domain / MIT-0  (c) David Reid                     https://miniaud.io
 lodepng           Zlib              (c) 2005-2020 Lode Vandevenne           https://lodev.org/lodepng
@@ -97,7 +96,7 @@ THIS SOFTWARE.
 
 --------------------------------------------------------------------------------
 The zlib/libpng License
-  Applies to: SDL2, enkiTS, lodepng — (c) their respective holders above.
+  Applies to: SDL2, lodepng - (c) their respective holders above.
 --------------------------------------------------------------------------------
 This software is provided 'as-is', without any express or implied warranty. In
 no event will the authors be held liable for any damages arising from the use of
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 40a854f3a..286c2b8c9 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -1142,8 +1142,8 @@ byte.
 
 A kit compiles to the same bytes every time. The two TPDF dither algorithms seed their `std::mt19937` from
 `rp::dsp::inputSeed` over the resampled, pre-trim sample the dither receives, salted with a per-algorithm constant, and
-the other three algorithms are deterministic already. `retroplug-audio-test`'s `[kit-baseline]` suite
-([KitBaseline.test.cpp](../packages/native/test/audio/KitBaseline.test.cpp)) compiles a fixed matrix of synthesised
+the other three algorithms are deterministic already. The kit baseline suite
+([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts)) compiles a fixed matrix of synthesised
 sources through `KitCompiler` and the two codecs and holds each bank's FNV-1a-64, size and slot length to
 [golden/kit-baseline.json](../packages/native/test/audio/golden/kit-baseline.json): exact bytes on `linux-x64-gnu13`,
 self-agreement and the layout facts elsewhere, with `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on CI's linux-x64 job and
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 7df1b2d72..b51a499e9 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -223,9 +223,9 @@ Exact kit bytes, not structure and audibility. `KitCompiler`'s output is what LS
 compared a compiled bank with anything; the suite is the gate Phase D's graph compiles are held to
 ([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)).
 
-- **Matrix.** `[kit-baseline]` ([KitBaseline.test.cpp](../packages/native/test/audio/KitBaseline.test.cpp), in
-  `retroplug-audio-test`; it carries `[kit]` beside the tag, which `[dither]` carries too, so one `"[kit]"` run
-  covers the kit path's whole byte surface) synthesises every source in the test (two sines, a quiet sine, a chirp,
+- **Matrix.** The kit baseline
+  ([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts), run by `pnpm test:native`)
+  synthesises every source in the test (two sines, a quiet sine, a chirp,
   seeded noise and silence at 22.05, 44.1 and 48 kHz, plus a 600-frame source shorter than one resampler chunk), writes
   each as a 32-bit float WAV in a temp dir, and compiles it through `KitCompiler` over `LsdjKitCodec` / `RisaDmcCodec`,
   so `SampleCache` and miniaudio's decode are in the loop and the suite needs no fixtures. 39 cases: LSDj has no
````

Run: `sed -n '11409,12308p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: exit 0; the only warning is the pre-existing `-Wformat-truncation` in `PluginDSP.cpp:186`, because this build recompiles `PluginDSP.cpp` after the RPC headers and the kit sources changed, as Task 5's build records.

Run: `cmake --build build --target help | grep -c enkiTS`
Expected: `0`: no target of that name. `nm -C build/bin/retroplug-host | grep -c enki` is 0 too, so no shipped binary carries it.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed`; `retroplug-audio-test` `1234835 assertions in 108 cases` (the `[kit-baseline]` case's 428 assertions gone); graph 356087 / 272 and engine-graph 282 | 273 passed | 9 skipped, unchanged; launchpad 144 / 29.

Run: `build/bin/retroplug-audio-test "[kit-baseline]"`
Expected: exit 2 and Catch2's `No tests ran`: the tag this task deleted, and Catch2 treats `No tests ran` as a failure, so a stale script fails loudly.

- [ ] **Step 2: The TypeScript side says what is**

`test-native/kit-compile.test.ts` loses its native half (`makeCall`, `nativeCompileKit`, `nativeCompileDmc`); its 9
cases stay, every bank is still held to `kit-edges.json`, each refusal's shape is held against the same kit compiled
with that slot's file missing, and the empty-window and defaults cases hold their own bytes.
`test-ui/kit-compile.test.ts` drops the two absence probes (the names are no method anywhere now) and keeps
`renderAudio`'s refusal as the proof its facet is unbound. Comments across `src/kit/{chain,pack,compile}.ts`, the LSDj
and risa ROM modules, `test/kit`, `test-native/kit/sources.ts`, `kit-baseline.test.ts` and `run-native-tests.mjs` now
name the format, the golden and the independent transcription instead of a deleted symbol; the test names that said
"native" say the byte rule or the reference.

````diff
diff --git a/packages/retroplug/scripts/run-native-tests.mjs b/packages/retroplug/scripts/run-native-tests.mjs
index 059707708..cbd4152b7 100644
--- a/packages/retroplug/scripts/run-native-tests.mjs
+++ b/packages/retroplug/scripts/run-native-tests.mjs
@@ -102,8 +102,8 @@ async function runOne({ file, slug }) {
         // The parity suite's baselines live beside its cases; a recording re-runs its own bundle per case.
         __PARITY_DIR__: JSON.stringify(join(TEST_DIR, "parity").replaceAll("\\", "/")),
         __SELF_BUNDLE__: JSON.stringify(outFile.replaceAll("\\", "/")),
-        // The kit compilers' byte baseline, shared with retroplug-audio-test's "[kit-baseline]" (one record, two
-        // compilers held to it); kit-baseline.test.ts reads it and, under RP_KIT_GOLDEN_UPDATE=1, records it.
+        // The kit compiler's byte baseline, the one record it is held to: kit-baseline.test.ts reads it and, under
+        // RP_KIT_GOLDEN_UPDATE=1, records it.
         __KIT_GOLDEN__: JSON.stringify(
           join(REPO, "packages/native/test/audio/golden/kit-baseline.json").replaceAll("\\", "/"),
         ),
diff --git a/packages/retroplug/src/kit/chain.ts b/packages/retroplug/src/kit/chain.ts
index b2147bb66..237287a1d 100644
--- a/packages/retroplug/src/kit/chain.ts
+++ b/packages/retroplug/src/kit/chain.ts
@@ -5,14 +5,13 @@
 // it. Each of those is a node that reads its buffer at activate (rp.dsp.normalize, rp.dsp.resample, rp.dsp.dither's
 // `seedFrom`, rp.dsp.dpcm), so each pass records into a buffer the next pass is handed as its input.
 //
-// The order is native KitUtil::compileSample's (and RisaDmcCodec::encode's), because the order is the bytes: the
-// effects at the source rate in list order, the resample, then the quantise (LSDj) or the DMC encode (risa).
+// The order is the kit formats' (and the golden's), because the order is the bytes: the effects at the source rate in
+// list order, the resample, then the quantise (LSDj) or the DMC encode (risa).
 //
-// Every spec value goes into a document as its binary32 value (`Math.fround`), which is what native decodes a spec's
-// float field to and what each node casts its param or setting to, so the node computes on the kit path's float. The
-// checks are on that same value, as a double against the descriptor's double bounds, the comparison the planner
-// clamps with: anything this file accepts reaches its node unclamped, and anything the planner would clamp is refused
-// here with a reason instead of compiling a different spec.
+// Every spec value goes into a document as its binary32 value (`Math.fround`), which is what each node casts its param
+// or setting to, so the node computes on the kit path's float. The checks are on that same value, as a double against
+// the descriptor's double bounds, the comparison the planner clamps with: anything this file accepts reaches its node
+// unclamped, and anything the planner would clamp is refused here with a reason instead of compiling a different spec.
 
 import type { KitEffect } from "../audioDriver";
 import { offlineInputRef, offlineOutputRef, type OfflineCap, type OfflineRunRequest } from "../offline";
@@ -79,10 +78,9 @@ export interface SlotChain {
   dither: string | null;
 }
 
-// A spec field's value: its binary32 value, with the value as the spec wrote it for a message, or the rp::lsdj
-// struct's default when the field is absent (audioDriver.ts's KitEffect declares every field optional; native's RPC
-// decode has no defaults and refuses the whole kit for a field left out, so here is the more lenient of the two). A
-// non-number is refused: native would not decode the spec at all.
+// A spec field's value: its binary32 value, with the value as the spec wrote it for a message, or the effect's
+// documented default when the field is absent (audioDriver.ts's KitEffect declares every field optional). A non-number
+// is refused: no document can carry it.
 interface Field {
   value: number;
   written: number;
@@ -125,7 +123,7 @@ function inRange(f: Field, range: ParamRange, what: string, node: string, where:
  * (the planner cannot see the plan's rate, and a biquad there is unstable, where the node's guard would part from the
  * kernel). A normalising gain is not range-checked: rp.dsp.normalize folds the peak into it as a setting, which no
  * range clamps, so it is refused only when a document cannot carry it (not finite). `withDither` false (risa) skips a
- * dither effect unread, as RisaDmcCodec does.
+ * dither effect unread: DMC is 1-bit, so a 4-bit dither has nothing to apply to.
  *
  * The list is cut at each normalising gain; a pass with nothing to run (the stretch before the first normalising gain,
  * when it holds no node) is dropped, so a list of nothing but dithers has no passes at all.
diff --git a/packages/retroplug/src/kit/compile.ts b/packages/retroplug/src/kit/compile.ts
index ba1849dd0..febdb2216 100644
--- a/packages/retroplug/src/kit/compile.ts
+++ b/packages/retroplug/src/kit/compile.ts
@@ -191,8 +191,8 @@ async function settle(compile: Promise<Compiled>): Promise<SlotResult> {
   }
 }
 
-// Each honoured slot's chain started at once; a sample past the bank's `capacity` is not compiled (native's assembler
-// drops it too), and its reason says so.
+// Each honoured slot's chain started at once; a sample past the bank's `capacity` is not compiled (the bank layout
+// holds that many), and its reason says so.
 async function compileSlots<S>(samples: readonly S[], capacity: number, compile: (s: S) => Promise<Compiled>,
                                bankName: string): Promise<{ results: SlotResult[]; dropped: string[] }> {
   const results = await Promise.all(samples.slice(0, capacity).map((s) => settle(compile(s))));
diff --git a/packages/retroplug/src/kit/pack.ts b/packages/retroplug/src/kit/pack.ts
index b58869e66..0803db0a5 100644
--- a/packages/retroplug/src/kit/pack.ts
+++ b/packages/retroplug/src/kit/pack.ts
@@ -1,42 +1,42 @@
 // The kit compilers' integer half (docs/modular-graph-phase-d2-design.md section 5): what TypeScript does to the
 // buffers the offline jobs record, after native has done every float the bytes depend on. LSDj's levels become 4-bit
-// codes, trimmed to whole wave frames and packed two to a byte (native KitUtil::compileSample's tail, SampleUtil's
-// convertCodesToNibbles); risa's DPCM levels become the delta bits, packed and padded to a legal DMC length
-// (RisaDmcCodec::encode's tail). Nothing here touches a float beyond reading which side of zero, or which level, a
-// recorded sample is, so every step is exact by construction and the tests hold each to its native twin.
+// codes, trimmed to whole wave frames and packed two to a byte (inverted, and rotated for LSDj 9.2.0+); risa's DPCM
+// levels become the delta bits, packed and padded to a legal DMC length. Nothing here touches a float beyond reading
+// which side of zero, or which level, a recorded sample is, so every step is exact by construction and the tests hold
+// each to the bank layout it writes.
 //
-// The constants the chain needs to cut and cap a slot live here too, spelled as the native code spells them: the
-// kit rates, the frame caps and the PAL DPCM rate table, whose literals are the native table's digit for digit.
+// The constants the chain needs to cut and cap a slot live here too, spelled as the formats spell them: the kit rates,
+// the frame caps and the PAL DPCM rate table, whose literals are wav2dmc.py's digit for digit.
 
-/** LSDj's kit rate, the Game Boy's sample rate as the kit encoder resamples to (native kGameboySampleRate). */
+/** LSDj's kit rate: the Game Boy wave channel's, which every kit sample is resampled to. */
 export const LSDJ_KIT_RATE = 11468;
 
 /** The 4-bit depth an LSDj kit sample is quantised to. */
 export const LSDJ_KIT_BITS = 4;
 
-/** The samples one wave frame holds, and the bytes it packs to (native SampleUtil). */
+/** The samples one wave frame holds, and the bytes it packs to: the GB wave format's 32-sample frame. */
 export const LSDJ_FRAME_SAMPLES = 32;
 export const LSDJ_FRAME_BYTES = 16;
 
-/** The most frames a kit can hold at the kit rate: its 0x3fa0 bytes of sample space, two 4-bit samples a byte
- *  (native Kit::kMaxSampleFrames). The window caps a slot's source to this many frames at the kit rate. */
+/** The most frames a kit can hold at the kit rate: its 0x3fa0 bytes of sample space, two 4-bit samples a byte. The
+ *  window caps a slot's source to this many frames at the kit rate. */
 export const LSDJ_MAX_SAMPLE_FRAMES = 0x3fa0 * 2;
 
-/** risa's DMC byte limit: 4081 = 16 * 255 + 1, the longest legal $4013 length (native kDmcMaxBytes). */
+/** risa's DMC byte limit: 4081 = 16 * 255 + 1, the longest legal $4013 length. */
 export const DMC_MAX_BYTES = 4081;
 
-/** A legal DMC length is 16k + 1 bytes (native kLengthStep). */
+/** A legal DMC length is 16k + 1 bytes. */
 export const DMC_LENGTH_STEP = 16;
 
-/** PAL DPCM playback rates (Hz), index 0..15, the literals native RisaDmcCodec.cpp's kPalRates spells (wav2dmc.py
- *  PAL_DPCM_RATES_HZ). A slot resamples to its index's rate, and its window's cap scales by it. */
+/** PAL DPCM playback rates (Hz), index 0..15, wav2dmc.py's PAL_DPCM_RATES_HZ digit for digit. A slot resamples to
+ *  its index's rate, and its window's cap scales by it. */
 export const PAL_DMC_RATES: readonly number[] = [
   4177.40, 4696.63, 5261.41, 5579.22, 6023.94, 7044.94, 7917.18, 8397.01,
   9446.63, 11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
 ];
 
-/** The PAL DPCM rate a slot's index names: native masks the index to its low four bits (`rate & 0x0F`) at the RPC
- *  and again in the codec, so an index of 17 is rate 1, never an error. */
+/** The PAL DPCM rate a slot's index names: the index is masked to its low four bits (`rate & 0x0F`), so an index of
+ *  17 is rate 1, never an error. */
 export function palDmcRate(index: number): number {
   return PAL_DMC_RATES[index & 0x0f];
 }
@@ -49,7 +49,7 @@ export function resampledLength(frames: number, inRate: number, outRate: number)
   return Math.floor(frames * (outRate / inRate));
 }
 
-/** `n` trimmed to whole 32-sample wave frames (native frameAlign): the packer writes whole frames only. */
+/** `n` trimmed down to whole 32-sample wave frames: the packer writes whole frames only. */
 export function trimToFrames(n: number): number {
   return n - (n % LSDJ_FRAME_SAMPLES);
 }
@@ -68,8 +68,8 @@ export function levelToCode(level: number): number {
 
 /**
  * Recorded 4-bit levels to the slot's packed nibbles: each level to its code, the run trimmed to whole wave frames,
- * then native convertCodesToNibbles - inverted (stored = 15 - code), and with `rotate` (LSDj 9.2.0+) each frame's
- * sample i at position (i + 1) % 32, so the frame's last sample opens it; then two samples a byte, high nibble first.
+ * then the nibble pack - inverted (stored = 15 - code), and with `rotate` (LSDj 9.2.0+) each frame's sample i at
+ * position (i + 1) % 32, so the frame's last sample opens it; then two samples a byte, high nibble first.
  */
 export function packNibbles(levels: ArrayLike<number>, rotate: boolean): Uint8Array {
   const frames = trimToFrames(levels.length) / LSDJ_FRAME_SAMPLES;
@@ -87,7 +87,7 @@ export function packNibbles(levels: ArrayLike<number>, rotate: boolean): Uint8Ar
 }
 
 /**
- * Recorded DPCM levels to a slot's DMC bytes, as RisaDmcCodec::encode packs them: `rp.dsp.dpcm` writes each delta bit
+ * Recorded DPCM levels to a slot's DMC bytes, as the risa bank packs them: `rp.dsp.dpcm` writes each delta bit
  * as exactly +1 or -1, so `level > 0` is the bit; bits go least-significant first, eight to a byte; the stream is
  * padded with zero bytes to a legal 16k + 1 length (an empty one is the one-byte `{0}`, itself legal); and a stream
  * past the hardware's 4081 bytes is cut there, still a legal length.
diff --git a/packages/retroplug/src/lsdj/rom/buildKit.ts b/packages/retroplug/src/lsdj/rom/buildKit.ts
index d39328293..fdabcd05e 100644
--- a/packages/retroplug/src/lsdj/rom/buildKit.ts
+++ b/packages/retroplug/src/lsdj/rom/buildKit.ts
@@ -1,7 +1,7 @@
-// Pure-TS kit-bank assembly: the port of native KitUtil::buildKit, byte for byte, used to SPLICE a kit
-// (single-sample import / remove) without re-encoding untouched samples: read existing samples as raw nibble
-// bytes (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank here. The TypeScript kit compiler
-// (src/kit/) assembles every bank it compiles here too, whatever host it runs in. Layout matches kit.ts / KitUtil:
+// Pure-TS kit-bank assembly, used to SPLICE a kit (single-sample import / remove) without re-encoding untouched
+// samples: read existing samples as raw nibble bytes (KitView.samplesRaw), mutate the list, and rebuild the 16 KB bank
+// here. The TypeScript kit compiler (src/kit/) assembles every bank it compiles here too, whatever host it runs in.
+// The layout matches kit.ts's:
 // a 16-entry u16le offset table (entry 0 = 0x4060; entries i+1 = each sample's end GB-addr; 0 terminates),
 // 15 × 3-char sample names at 0x22, a 6-char kit name at 0x52, sample data from 0x60 (≤ 0x3fa0 bytes).
 import {
@@ -23,11 +23,10 @@ export interface KitSample {
 
 const utf8 = new TextEncoder();
 
-// Write a fixed-width name as native KitUtil::buildKit's writeString does: the name's bytes (its UTF-8, which is what
-// a native string of it holds), each a-z uppercased and every other byte as it is, truncated to `size` and padded with
-// `fill`. Byte for byte, so this assembler and native's give one bank for one name: a name of printable ASCII (all a
-// kit read back from a ROM can have, KitView's readName) is the same under any rule, and for anything else native is
-// the reference the kit compiler is held to, so its bytes are copied rather than "corrected" here.
+// Write a fixed-width name: the name's bytes (its UTF-8), each a-z uppercased and every other byte as it is, truncated
+// to `size` and padded with `fill`. The byte rule is the bank layout's, not a locale's, so one name gives one bank: a
+// name of printable ASCII (all a kit read back from a ROM can have, KitView's readName) is the same under any rule, and
+// test/kit/kit.test.ts's non-ASCII case pins what these bytes are for anything else.
 function writeField(bank: Uint8Array, off: number, size: number, name: string, fill: number): void {
   const bytes = utf8.encode(name);
   for (let i = 0; i < size; i++) {
@@ -50,7 +49,7 @@ export interface PlacedKitBank {
 }
 
 /** Assemble a 16 KB kit bank from a kit name + samples (≤15 honored). Sample data past the 0x3fa0 budget
- *  is clipped (matches native KitUtil::buildKit) — callers pre-check with kitSampleSpace to fail loudly. */
+ *  is clipped - callers pre-check with kitSampleSpace to fail loudly. */
 export function buildKitBank(kitName: string, samples: readonly KitSample[]): Uint8Array {
   return buildKitBankPlaced(kitName, samples).bank;
 }
diff --git a/packages/retroplug/src/lsdj/rom/constants.ts b/packages/retroplug/src/lsdj/rom/constants.ts
index 4223830c5..fa54a904f 100644
--- a/packages/retroplug/src/lsdj/rom/constants.ts
+++ b/packages/retroplug/src/lsdj/rom/constants.ts
@@ -32,7 +32,7 @@ export const KIT_OFFSET_ENTRIES = 16;
 export const KIT_VALID_MAGIC = 0x4000 + KIT_SAMPLE_DATA_OFFSET; // 0x4060 (first offset entry, LE 60 40)
 export const KIT_EMPTY_MAGIC = 0xffff; // first offset entry of an empty kit
 
-// Sample nibble encoding (the GB wave format LSDj packs kit audio into; see native SampleUtil.hpp):
+// Sample nibble encoding (the GB wave format LSDj packs kit audio into):
 //  - stored 4-bit value is INVERTED: stored = 0xF - amplitude (decode: amp = 0xF - stored).
 //  - LSDj 9.2.0+ ROTATES each 32-sample frame right by one (encoded position i holds source sample i-1,
 //    position 0 the last) to dodge a GB wave-refresh bug. Older builds store un-rotated.
diff --git a/packages/retroplug/src/lsdj/rom/kit.ts b/packages/retroplug/src/lsdj/rom/kit.ts
index 98c104e8c..1748f035c 100644
--- a/packages/retroplug/src/lsdj/rom/kit.ts
+++ b/packages/retroplug/src/lsdj/rom/kit.ts
@@ -33,8 +33,7 @@ function readName(view: BitView, off: number, size: number): string {
 
 // Decode packed 4-bit kit nibbles → PCM in [-1, 1]. Nibbles are stored INVERTED (amp = 0xF - stored); a
 // byte holds two samples (high nibble first). When `rotate` (LSDj 9.2.0+), each 32-sample frame was
-// stored rotated right by one — undo it by reading encoded position (i+1)%32. Port of native
-// SampleUtil.hpp's convertNibblesToF32 / convertNibblesToF32WithRotation.
+// stored rotated right by one - undo it by reading encoded position (i+1)%32.
 export function decodeNibbles(bytes: Uint8Array, rotate: boolean): Float32Array {
   const amp = (n: number): number => ((0xf - n) / 15) * 2 - 1;
   if (!rotate) {
@@ -131,7 +130,7 @@ export class KitView {
   }
 
   /** The i-th sample's PCM in [-1, 1], decoded from its 4-bit nibbles: inverted (amp = 0xF - stored) and,
-   *  for LSDj 9.2.0+ ROMs, un-rotating each 32-sample frame. Matches native SampleUtil.hpp. */
+   *  for LSDj 9.2.0+ ROMs, each 32-sample frame un-rotated. */
   sampleData(i: number): Float32Array {
     return decodeNibbles(this.rawSampleBytes(i), this.rotate);
   }
diff --git a/packages/retroplug/src/risa/rom/kit.ts b/packages/retroplug/src/risa/rom/kit.ts
index 176fa0ee0..cb7be5bea 100644
--- a/packages/retroplug/src/risa/rom/kit.ts
+++ b/packages/retroplug/src/risa/rom/kit.ts
@@ -112,26 +112,26 @@ export interface AssembleSlot {
 
 const utf8 = new TextEncoder();
 
-// A name's bytes as native holds them (its UTF-8), with a-z uppercased and every other byte left as it is: the
-// ASCII-only uppercase RisaDmcCodec applies, byte by byte, so one name gives native's bank bytes here.
+// A name's bytes (its UTF-8), with a-z uppercased and every other byte left as it is: the bank's ASCII-only uppercase,
+// byte by byte, so one name gives one bank.
 function upperAsciiBytes(name: string): Uint8Array {
   const bytes = utf8.encode(name);
   for (let i = 0; i < bytes.length; i++) if (bytes[i] >= 0x61 && bytes[i] <= 0x7a) bytes[i] -= 0x20;
   return bytes;
 }
 
-// A 3-char, uppercased, right-space-padded sample name (native RisaDmcCodec writeSampleName).
+// A 3-char, uppercased, right-space-padded sample name, as the bank's sample-name field stores it.
 function writeSampleName(bank: Uint8Array, off: number, name: string): void {
   const bytes = upperAsciiBytes(name);
   for (let i = 0; i < KIT_SAMPLE_NAME_LEN; i++) bank[off + i] = i < bytes.length ? bytes[i] : 0x20;
 }
 
-/** Pack DPCM slots into an 8 KB kit bank — the TS port of native `RisaDmcCodec::assemble`. Greedy 64-byte-
- *  aligned packing into the 7872 B sample region, a 4-byte index entry per slot ([addr, lenReg, rate, flags]),
- *  3-char sample names, the ≤6-char kit name, and the 0xA5 populated magic. A null slot stays EMPTY with its
- *  index preserved (NOT compacted); a slot whose DPCM won't fit the remaining region is left empty. Because
- *  a slot's encode carries no cross-sample state, this is byte-identical to compiling the same samples together
- *  via compileDmc - so it losslessly re-packs existing slots' DPCM alongside a new one. */
+/** Pack DPCM slots into an 8 KB kit bank. Greedy 64-byte-aligned packing into the 7872 B sample region, a 4-byte
+ *  index entry per slot ([addr, lenReg, rate, flags]), 3-char sample names, the ≤6-char kit name, and the 0xA5
+ *  populated magic. A null slot stays EMPTY with its index preserved (NOT compacted); a slot whose DPCM won't fit the
+ *  remaining region is left empty. Because a slot's encode carries no cross-sample state, this is byte-identical to
+ *  compiling the same samples together via compileDmc - so it losslessly re-packs existing slots' DPCM alongside a
+ *  new one. */
 export function assembleKitBank(kitName: string, slots: (AssembleSlot | null)[]): Uint8Array {
   return assembleKitBankPlaced(kitName, slots).bank;
 }
@@ -174,7 +174,7 @@ export function assembleKitBankPlaced(kitName: string, slots: (AssembleSlot | nu
   }
 
   // Kit name: uppercased, keep A-Z 0-9 '-', up to 6 chars, NUL-padded to KIT_NAME_SIZE. Byte by byte over the
-  // name's UTF-8, as native filters it, so a non-ASCII character is dropped whole rather than uppercased into a letter.
+  // name's UTF-8 by that rule, so a non-ASCII character is dropped whole rather than uppercased into a letter.
   let w = 0;
   for (const c of upperAsciiBytes(kitName)) {
     if (w >= KIT_NAME_MAX_CHARS) break;
diff --git a/packages/retroplug/test-native/kit-baseline.test.ts b/packages/retroplug/test-native/kit-baseline.test.ts
index e1ae943df..f3e57420b 100644
--- a/packages/retroplug/test-native/kit-baseline.test.ts
+++ b/packages/retroplug/test-native/kit-baseline.test.ts
@@ -1,28 +1,28 @@
 // The kit compilers' byte baseline through the TypeScript compiler (src/kit/, docs/modular-graph-phase-d2-design.md
 // sections 5 and 6): every case of packages/native/test/audio/golden/kit-baseline.json compiled as chains of offline
-// graph jobs on this host's real engine, its bank held to the record native KitCompiler made. The matrix, its sources
-// and its rules are KitBaseline.test.cpp's (retroplug-audio-test "[kit-baseline]"), transcribed: one record, and two
-// compilers held to it until native's retires, when this file is the one that keeps it.
+// graph jobs on this host's real engine, its bank held to the golden's record. The matrix is this file's CASES and the
+// sources are test-native/kit/sources.ts; this file is the baseline's only runner, and the golden holds the target and
+// each case's record.
 //
 // The sources are synthesised (test-native/kit/sources.ts), written as 32-bit float WAVs into this run's config dir and
-// decoded natively by the offline jobs' file input, which is the native suite's SampleCache decode, so nothing here
+// decoded natively by the offline jobs' file input, which is SampleCache's decode (the seam's own), so nothing here
 // reads a fixture and CI runs it. Before any case compiles, each source is decoded back through a job to the samples
-// written, and on the target held to the C++ suite's own floats (their FNV-1a-64, SOURCE_HASHES), so a bank that
-// matches is the C++ suite's input compiled, not a coincidence of two different inputs. Only on the target: the hashes
-// were recorded from glibc's sin, and Math.sin is the host's own libm (Apple's on macOS, the UCRT on Windows), whose
-// last bit may differ now and then; off the target a case only has to agree with itself, so a source one ulp away
-// there changes no verdict.
+// written, and on the target held to SOURCE_HASHES (each source's FNV-1a-64 over its float bytes, recorded from the
+// retired C++ suite), so a bank that matches is the recorded input compiled, not a coincidence of two different inputs.
+// Only on the target: the hashes were recorded from glibc's sin, and Math.sin is the host's own libm (Apple's on macOS,
+// the UCRT on Windows), whose last bit may differ now and then; off the target a case only has to agree with itself, so
+// a source one ulp away there changes no verdict.
 //
-// The rules are the native suite's. A record is {hash, size, slot}: the bank's FNV-1a-64 in lowercase hex, its size,
-// and slot 0's length as the bank records it. The size and the slot length are compared on every host; the hash only
-// on the host the golden was recorded on (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic is
-// bit-stable only for one compiler on one architecture. Elsewhere a case compiles twice and must agree with itself,
+// The rules the golden records under. A record is {hash, size, slot}: the bank's FNV-1a-64 in lowercase hex, its
+// size, and slot 0's length as the bank records it. The size and the slot length are compared on every host; the hash
+// only on the host the golden was recorded on (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic
+// is bit-stable only for one compiler on one architecture. Elsewhere a case compiles twice and must agree with itself,
 // and RP_KIT_GOLDEN_REQUIRE_TARGET=1 makes a foreign host fail instead (CI's linux-x64 job sets it on its test:native
 // step). The case names must be the golden's keys exactly, none missing and none extra.
 //
 // RP_KIT_GOLDEN_UPDATE=1 re-records the golden from this compiler, refused anywhere but the target and for any case
-// whose two compiles disagree, and written through a sibling temp file, the native suite's refusals. Without the
-// variable the file is only ever read.
+// whose two compiles disagree, and written through a sibling temp file. Without the variable the file is only ever
+// read.
 import { test, expect } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver, type KitEffect } from "../src/audioDriver";
@@ -49,7 +49,7 @@ const env = tjs.env;
 const UPDATE = env.RP_KIT_GOLDEN_UPDATE === "1";
 const REQUIRE_TARGET = env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";
 
-// ---- the matrix (KitBaseline.test.cpp's kitCases, in its order) -----------------------------------------------------
+// ---- the matrix (the golden's cases, in its order) ------------------------------------------------------------------
 
 type Codec = "lsdj" | "risa";
 
@@ -344,8 +344,7 @@ function readGolden(): Golden {
   return JSON.parse(new TextDecoder().decode(bytes)) as Golden;
 }
 
-// The golden as the native suite's writeGolden writes it, keys in byte order, so a recording from either suite gives
-// one file.
+// The golden's format: keys in byte order, so a recording of the same banks is the same file.
 function goldenText(golden: Golden): string {
   const names = Object.keys(golden.cases).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
   const rows = names.map((n) => {
@@ -376,7 +375,7 @@ test("the matrix is the golden's: sources for every signal at every rate, and it
   expect(CASES.map((c) => c.name).sort()).toEqual(Object.keys(golden.cases).sort());
 });
 
-test("every source decodes through a job to the samples written, and on the target is the C++ suite's", async () => {
+test("every source decodes through a job to the samples written, and on the target matches SOURCE_HASHES", async () => {
   expect(sources.size).toBe(Object.keys(SOURCE_HASHES).length);
   const onTarget = audio.buildTarget() === BASELINE_TARGET;
   for (const [key, source] of sources) {
diff --git a/packages/retroplug/test-native/kit-compile.test.ts b/packages/retroplug/test-native/kit-compile.test.ts
index 217ffdc80..ba8f856c4 100644
--- a/packages/retroplug/test-native/kit-compile.test.ts
+++ b/packages/retroplug/test-native/kit-compile.test.ts
@@ -1,26 +1,22 @@
 // The TypeScript kit compiler (src/kit/) past what the 39-case baseline covers (test-native/kit-baseline holds those),
 // on this host's real engine. A slot it refuses (a source with a NaN, a filter at Nyquist, a gain or a Q outside its
 // node's range, a pass that overflows a finite source to a value that is not finite, a job a node's guard fails)
-// leaves the rest of the kit compiling, says why, and leaves the bank native builds when that slot's file fails to
-// load. Every edge native's compileSample and DMC encode have - an empty window, a window cut by offset and length, a
-// window that resamples to less than one wave frame or to nothing, a silent source, a source past the frame cap, a bank
-// whose space runs out, a DMC slot that does not fit, samples past the slot count, the struct defaults a spec leaves
-// out - comes out byte for byte as native compiles it, in full 15- and 16-slot kits; and a slot the bank does not hold
-// whole says why.
+// leaves the rest of the kit compiling, says why, and leaves a slot of no bytes, the shape a slot whose file fails to
+// load has. Every edge a slot has - an empty window, a window cut by offset and length, a window that resamples to less
+// than one wave frame or to nothing, a silent source, a source past the frame cap, a bank whose space runs out, a DMC
+// slot that does not fit, samples past the slot count, the defaults a spec leaves out - is recorded, in full 15- and
+// 16-slot kits; and a slot the bank does not hold whole says why.
 //
-// Each bank is held two ways. Against native's compileKit / compileDmc on this host, while native's compiler exists:
-// both share every kernel, so that comparison is exact on every host. Those two are the harness RPCs
-// (nativeCompileKit / nativeCompileDmc below), the only callers left that reach native's compiler. And
-// against kit-edges.json (beside kit-baseline.json), recorded from this compiler on the target when native and it
-// agreed, under the baseline's rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile
-// agreeing with the first; RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records
-// it, refused off the target or for a bank whose two compiles disagree. The golden is what keeps these edges pinned
-// once native's compiler retires. The sources are synthesised, so it needs no fixtures.
+// Every bank is held to kit-edges.json (beside kit-baseline.json), recorded from this compiler on the target under the
+// baseline's rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile agreeing with the first;
+// RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records it, refused off the target
+// or for a bank whose two compiles disagree. Every case still compiles its bank, so the golden holds the bank each one
+// describes, and the refusals' shape is held against the same kit compiled with those slots' files missing. The sources
+// are synthesised, so it needs no fixtures.
 import { test, expect } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
-import { createAudioDriver, type KitCompileSpec, type KitEffect, type KitSampleSpec } from "../src/audioDriver";
-import type { RisaDmcSampleSpec, RisaKitCompileSpec } from "../src/audioDriver";
-import { makeCall } from "../src/rpcClient";
+import { createAudioDriver, type KitEffect, type KitSampleSpec } from "../src/audioDriver";
+import type { RisaDmcSampleSpec } from "../src/audioDriver";
 import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
 import { fnv1a64Hex, floatWav, signalSamples } from "./kit/sources";
 
@@ -30,13 +26,6 @@ declare const tjs: { env: Record<string, string> };
 const be = createRealBackend();
 const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick
 
-// Native's compiler, over the harness facet's two RPCs (this host binds it). These go with the two harness RPCs when
-// native's compiler retires; while it is here, this is what makes each bank exact against the compiler the kernels
-// came from.
-const nativeCall = makeCall();
-const nativeCompileKit = (spec: KitCompileSpec): Uint8Array => nativeCall("compileKit", spec) as Uint8Array;
-const nativeCompileDmc = (spec: RisaKitCompileSpec): Uint8Array => nativeCall("compileDmc", spec) as Uint8Array;
-
 const BASELINE_TARGET = "linux-x64-gnu13";
 const UPDATE = tjs.env.RP_KIT_GOLDEN_UPDATE === "1";
 const REQUIRE_TARGET = tjs.env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";
@@ -129,8 +118,8 @@ const SUBNORMAL = write("subnormal", sine(440, 44100, 8192, 1e-39), 44100);
 const HUGE = write("huge", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 3e38), 44100);
 const LOUD = write("loud", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 1e38), 44100);
 
-// Effects with every field written: the native RPC's decode has no defaults, so a field left out refuses the whole
-// kit there, where the TypeScript compiler fills the struct's default.
+// Effects with every field written, so a case says exactly what it applies; the defaults case below is the one that
+// leaves fields out and holds them to the spelled-out spec.
 type FilterType = NonNullable<Extract<KitEffect, { type: "filter" }>["filterType"]>;
 type DitherType = NonNullable<Extract<KitEffect, { type: "dither" }>["ditherType"]>;
 const gain = (g: number, normalize = true): KitEffect => ({ type: "gain", normalize, gain: g });
@@ -161,7 +150,7 @@ const GAIN = "effect 0 (gain): gain 150 is outside rp.dsp.gain's range -100 .. 1
 const Q = "effect 1 (filter): q 2000 is outside rp.dsp.biquad's range 0.001 .. 1000";
 const UNREADABLE = `${MISSING} could not be read or decoded`;
 
-edgeCase("a refused LSDj slot leaves the rest compiling, says why, and is native's for a file that fails", async () => {
+edgeCase("a refused LSDj slot leaves the rest compiling, says why, and empties its slot", async () => {
   const refused = [
     lsdj(NAN, "NAN"),
     lsdj(SINE, "NYQ", [filter("HighPass", 22050, 1)]),
@@ -180,14 +169,12 @@ edgeCase("a refused LSDj slot leaves the rest compiling, says why, and is native
   const kit = await held("lsdj/refused", () => compileKit(be, { name: "REFUSE", samples }));
   expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST, GAIN, null, Q]);
 
-  // Each refused slot is empty: no bytes, its name kept, as native's assembler leaves a slot whose file failed to load.
-  // So the bank is native's for the same kit with those slots' files missing, and the TypeScript compile of that kit.
+  // Each refused slot is empty: no bytes, its name kept, which is how a slot whose file fails to load is left. So the
+  // bank is the same kit compiled with those slots' files missing, byte for byte.
   const missing = samples.map((s) => (refused.includes(s) ? { ...s, path: MISSING } : s));
-  const native = nativeCompileKit({ name: "REFUSE", samples: missing });
-  expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
   const again = await compileKit(be, { name: "REFUSE", samples: missing });
   expect(again.errors).toEqual([null, UNREADABLE, null, UNREADABLE, UNREADABLE, null, UNREADABLE]);
-  expect(firstDifference(again.bytes, kit.bytes)).toBe(-1);
+  expect(firstDifference(kit.bytes, again.bytes), "the bank with the refused slots' files missing").toBe(-1);
   // Not the kit without those slots, though: an LSDj slot with no bytes is named, with a zero length, where a slot the
   // spec does not have is the empty-slot marker. So a refused last slot differs from the kit one shorter in exactly
   // that entry and that name.
@@ -208,15 +195,14 @@ edgeCase("a refused risa slot leaves the rest compiling, its entry empty: a miss
   expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST]);
   expect([kit.bytes[0x1f04], kit.bytes[0x1f0c]]).toEqual([0xff, 0xff]);
   const missing = samples.map((s, i) => (i === 1 || i === 3 ? { ...s, path: MISSING } : s));
-  const native = nativeCompileDmc({ name: "REFUSE", samples: missing });
-  expect(firstDifference(kit.bytes, native), "the native bank with the refused slots' files missing").toBe(-1);
-  // risa's assembler leaves an empty slot where it is and moves no cursor, so a refused LAST slot is the kit without
-  // it.
+  const again = await compileDmc(be, { name: "REFUSE", samples: missing });
+  expect(firstDifference(kit.bytes, again.bytes), "the bank with the refused slots' files missing").toBe(-1);
+  // risa's bank leaves an empty slot where it is and moves no cursor, so a refused LAST slot is the kit without it.
   const shorter = await compileDmc(be, { name: "REFUSE", samples: samples.slice(0, 3) });
   expect(firstDifference(kit.bytes, shorter.bytes), "the kit without its refused last slot").toBe(-1);
 });
 
-edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for byte", async () => {
+edgeCase("a full LSDj kit of every slot edge is the bank kit-edges.json records", async () => {
   const dithers = ["HighPassTPDF", "ShapedTPDF", "ErrorDiffusion", "JJN", "SierraLite"] as const;
   const samples = [
     lsdj(SINE, "EMP", [gain(0.5)], { offset: 9000 }), // an offset past the end: an empty window
@@ -243,7 +229,6 @@ edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for
   for (const rotate of [true, false]) {
     const spec = { name: "edges", samples, rotate };
     const kit = await held(`lsdj/edges-rotate-${rotate ? "on" : "off"}`, () => compileKit(be, spec));
-    expect(firstDifference(kit.bytes, nativeCompileKit(spec)), `rotate ${rotate}`).toBe(-1);
     // Every slot the bank does not hold whole says why: the empty window, the window under one wave frame, the slot
     // the bank's space ran out on part way (the noise, 2128 bytes, before the capped source), and the capped source,
     // which finds none left.
@@ -263,17 +248,16 @@ edgeCase("a full LSDj kit of every compileSample edge is native's bank byte for
     );
   }
   // A source past the cap fills the whole sample space on its own, and a slot after it finds none: the empty-slot
-  // marker, which native writes for a slot with no room as for one the spec does not have.
+  // marker, which the bank writes for a slot with no room as for one the spec does not have.
   const full = { name: "FULL", samples: [lsdj(LONG, "LNG"), lsdj(SINE, "AFT")] };
   const kit = await held("lsdj/full", () => compileKit(be, full));
-  expect(firstDifference(kit.bytes, nativeCompileKit(full))).toBe(-1);
   const noRoom = "the kit's 16288 bytes of sample space are full, so none of this sample's 1056 bytes fit";
   expect(kit.errors).toEqual([null, noRoom]);
   const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
   expect([entry(1), entry(2), kit.bytes[0x25]]).toEqual([0x4060 + 0x3fa0, 0, 0]);
 });
 
-edgeCase("a full risa kit of every DMC encode edge is native's bank byte for byte", async () => {
+edgeCase("a full risa kit of every DMC slot edge is the bank kit-edges.json records", async () => {
   const samples: RisaDmcSampleSpec[] = [
     { path: TINY, name: "TNY", effects: [], rate: 0 }, // resamples to no frames: the one-byte {0}
     { path: SINE, name: "EMP", effects: [], offset: 9000 }, // an empty window: {0} too
@@ -296,7 +280,6 @@ edgeCase("a full risa kit of every DMC encode edge is native's bank byte for byt
   // A {0} slot is a placed sample; L15 found no room after L0, and says so.
   const noFit = "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it";
   expect(kit.errors).toEqual(samples.map((s) => (s.name === "L15" ? noFit : null)));
-  expect(firstDifference(kit.bytes, nativeCompileDmc({ name: "edges-kit", samples }))).toBe(-1);
   // The {0} slots are one byte at their own 64-byte boundary; L0 is the longest legal stream; L15 found no room.
   expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f04], kit.bytes[0x1f05]]).toEqual([0, 0, 1, 0]);
   expect([kit.bytes[0x1f08], kit.bytes[0x1f09], kit.bytes[0x1f0c]]).toEqual([2, 255, 0xff]);
@@ -306,8 +289,8 @@ edgeCase("a finite source the chain overflows refuses its slot with the reason,
   // Each route reaches a value that is not finite from a source that is: a normalise over a subnormal peak and a plain
   // gain over samples near the float range's top make one the pass records; a +96 dB peak over those samples overflows
   // the filter, whose guard resets it and fails the job; a dither (JJN) over loud samples overflows its scaled value,
-  // whose guard restarts it and fails the job. Native compiles all four to whatever its kernels latch; the TypeScript
-  // compiler refuses them, so no kit it compiles carries a guard's output or a non-finite intermediate.
+  // whose guard restarts it and fails the job. A live plan streams on through both guards; the kit compile refuses the
+  // slot, so no kit it builds carries a guard's output or a non-finite intermediate.
   const samples = [
     lsdj(SUBNORMAL, "SUB", [gain(0.9)]),
     lsdj(SINE, "A", [filter("LowPass", 2000, 0.9)]),
@@ -325,15 +308,15 @@ edgeCase("a finite source the chain overflows refuses its slot with the reason,
   expect(guard(kit.errors[3], filterWords), String(kit.errors[3])).toBe(true);
   const ditherWords = "the quantise overflowed the dither's state, which rp.dsp.dither restarted (";
   expect(guard(kit.errors[4], ditherWords), String(kit.errors[4])).toBe(true);
-  // The refused slots are the slots native leaves when their files fail to load.
+  // The refused slots are left as the slots whose files fail to load.
   const missing = samples.map((s) => (s.name === "A" ? s : { ...s, path: MISSING }));
-  expect(firstDifference(kit.bytes, nativeCompileKit({ name: "OVER", samples: missing }))).toBe(-1);
+  const bank = await compileKit(be, { name: "OVER", samples: missing });
+  expect(firstDifference(kit.bytes, bank.bytes), "the bank with the refused slots' files missing").toBe(-1);
 });
 
-edgeCase("samples past the slot count are dropped as native drops them, each saying so", async () => {
+edgeCase("samples past the slot count are dropped, each saying so", async () => {
   const sixteen = Array.from({ length: 16 }, (_, i) => lsdj(i % 2 ? SINE : SHORT, `S${i}`, i % 3 ? [] : [gain(0.5)]));
   const kit = await held("lsdj/sixteen", () => compileKit(be, { name: "SIXTEEN", samples: sixteen }));
-  expect(firstDifference(kit.bytes, nativeCompileKit({ name: "SIXTEEN", samples: sixteen }))).toBe(-1);
   expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
   const seventeen: RisaDmcSampleSpec[] = Array.from({ length: 17 }, (_, i) => ({
     path: i % 2 ? SINE : SHORT,
@@ -342,11 +325,10 @@ edgeCase("samples past the slot count are dropped as native drops them, each say
     rate: i % 16,
   }));
   const dmc = await held("risa/seventeen", () => compileDmc(be, { name: "SEVENTEEN", samples: seventeen }));
-  expect(firstDifference(dmc.bytes, nativeCompileDmc({ name: "SEVENTEEN", samples: seventeen }))).toBe(-1);
   expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
 });
 
-edgeCase("a refusal beats an empty window: a refused risa slot is empty, where native encodes {0}", async () => {
+edgeCase("a refusal beats an empty window: a refused slot is empty, an empty window's {0}", async () => {
   const samples: RisaDmcSampleSpec[] = [
     { path: SINE, name: "A", effects: [], rate: 12 },
     { path: SINE, name: "REF", effects: [gain(150, false)], rate: 12, offset: 9000 },
@@ -354,31 +336,35 @@ edgeCase("a refusal beats an empty window: a refused risa slot is empty, where n
   const kit = await held("risa/refusal-over-empty-window", () => compileDmc(be, { name: "R", samples }));
   expect(kit.errors).toEqual([null, GAIN]);
   expect(kit.bytes[0x1f04]).toBe(0xff);
-  // Native never refuses: the same spec compiles its empty window to the one-byte {0}, a placed slot. The refused slot
-  // is the slot native leaves for a file that fails to load.
-  const native = nativeCompileDmc({ name: "R", samples });
-  expect(native[0x1f04] !== 0xff).toBe(true);
-  const missing = nativeCompileDmc({ name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
-  expect(firstDifference(kit.bytes, missing)).toBe(-1);
+  // An empty window is not a refusal: the same slot without the out-of-range gain compiles to the one-byte {0}, a
+  // placed slot. The refused slot is the slot left for a file that fails to load.
+  const window = await compileDmc(be, {
+    name: "R",
+    samples: [samples[0], { ...samples[1], effects: [], offset: 9000 }],
+  });
+  expect(window.errors).toEqual([null, null]);
+  expect(window.bytes[0x1f04] !== 0xff).toBe(true);
+  const missing = await compileDmc(be, { name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
+  expect(firstDifference(kit.bytes, missing.bytes)).toBe(-1);
 });
 
-edgeCase("a field the spec leaves out takes the native struct's default, the spelled-out spec's bank", async () => {
-  // Native's RPC refuses a spec with a field left out, so it is held to the spelled-out defaults: LowPass at 5734 Hz,
-  // Q 1 and 0 dB; a normalising gain of 1; HighPassTPDF.
+edgeCase("a field the spec leaves out takes the effect's default, the spelled-out spec's bank", async () => {
+  // A spec may leave an effect's fields out, and the compiler fills the defaults: LowPass at 5734 Hz, Q 1 and 0 dB; a
+  // normalising gain of 1; HighPassTPDF. The sparse spec must compile to the spelled-out one's bank.
   const sparse: KitEffect[] = [{ type: "filter" }, { type: "gain" }, { type: "dither" }];
   const spelled: KitEffect[] = [filter("LowPass", 5734, 1, 0), gain(1, true), dither("HighPassTPDF")];
   const kit = await held("lsdj/defaults", () => compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", sparse)] }));
   expect(kit.errors).toEqual([null]);
-  const native = nativeCompileKit({ name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
-  expect(firstDifference(kit.bytes, native)).toBe(-1);
+  const lsdjSpelled = await compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
+  expect(firstDifference(kit.bytes, lsdjSpelled.bytes), "the spelled-out LSDj spec's bank").toBe(-1);
   const dmcSparse = [{ path: CHIRP, name: "SP", effects: sparse.slice(0, 2) }];
   const dmc = await held("risa/defaults", () => compileDmc(be, { name: "DEF", samples: dmcSparse }));
   expect(dmc.errors).toEqual([null]);
-  const dmcNative = nativeCompileDmc({
+  const dmcSpelled = await compileDmc(be, {
     name: "DEF",
     samples: [{ path: CHIRP, name: "SP", effects: spelled.slice(0, 2), rate: 12, loop: false, normalize: true }],
   });
-  expect(firstDifference(dmc.bytes, dmcNative)).toBe(-1);
+  expect(firstDifference(dmc.bytes, dmcSpelled.bytes), "the spelled-out risa spec's bank").toBe(-1);
 });
 
 test(UPDATE ? "write kit-edges.json" : "kit-edges.json holds exactly the banks held here", () => {
diff --git a/packages/retroplug/test-native/kit/sources.ts b/packages/retroplug/test-native/kit/sources.ts
index ff1f28692..0265b1011 100644
--- a/packages/retroplug/test-native/kit/sources.ts
+++ b/packages/retroplug/test-native/kit/sources.ts
@@ -1,14 +1,14 @@
-// The kit baseline's synthesised sources, bit for bit the ones packages/native/test/audio/KitBaseline.test.cpp
-// computes, and what writing and hashing them takes. Every expression is the C++ one in the same order: the double
-// arithmetic is JavaScript's own (both are IEEE doubles, and Math.sin on this host is the C library's sin, which
-// std::sin is too), and every place the C++ rounds to float is a Math.fround here. A binary32 product rounded once is
-// exactly binary32 multiplication (the double product of two floats is exact), and a double difference of two floats
-// rounded once is binary32 subtraction, so each fround below is one C++ float operation.
+// The kit baseline's synthesised sources, and what writing and hashing them takes. Every expression is at the kit
+// path's precision: the double arithmetic is JavaScript's own (both are IEEE doubles, and Math.sin on this host is the
+// C library's sin), and every place the kit path rounds to float is a Math.fround here. A binary32 product rounded once
+// is exactly binary32 multiplication (the double product of two floats is exact), and a double difference of two floats
+// rounded once is binary32 subtraction, so each fround below is one float operation.
 //
-// The proof that these are the C++ sources is SOURCE_HASHES: each source's FNV-1a-64 over its float bytes, recorded
-// from the C++ suite, which kit-baseline.test.ts holds every source to before it compiles anything.
+// The proof that these are the sources the baseline was recorded from is SOURCE_HASHES: each source's FNV-1a-64 over
+// its float bytes, recorded from the retired C++ suite, which kit-baseline.test.ts holds every source to on the target
+// before it compiles anything.
 
-/** The source signals, as the C++ suite names them. */
+/** The source signals the baseline's matrix uses. */
 export type Signal = "sine440" | "sine1200" | "quiet" | "chirp" | "noise" | "silence" | "short";
 
 /** Each signal's length in frames: 8192, and 600 for the source shorter than one resampler chunk. */
@@ -84,7 +84,7 @@ function noise(frames: number): Float32Array {
   return out;
 }
 
-/** A signal's samples at `rate`, as the C++ suite's signalSamples computes them. */
+/** A signal's samples at `rate`, as the baseline's matrix defines them. */
 export function signalSamples(signal: Signal, rate: number): Float32Array {
   const frames = signalFrames(signal);
   switch (signal) {
@@ -108,13 +108,13 @@ export function signalSamples(signal: Signal, rate: number): Float32Array {
   }
 }
 
-/** The C++ suite's file name stem for a source: `<signal>-<rate>`. */
+/** A source's file name stem: `<signal>-<rate>`. */
 export function sourceKey(signal: Signal, rate: number): string {
   return `${signal}-${rate}`;
 }
 
-/** Each source's FNV-1a-64 over its float bytes (little-endian), recorded from KitBaseline.test.cpp's sources on
- *  linux-x64-gnu13. The noise and the silence do not depend on the rate, so their three hashes are one. */
+/** Each source's FNV-1a-64 over its float bytes (little-endian), recorded with the golden on linux-x64-gnu13. The
+ *  noise and the silence do not depend on the rate, so their three hashes are one. */
 export const SOURCE_HASHES: Readonly<Record<string, string>> = {
   "chirp-22050": "0425491f935aed01",
   "chirp-44100": "735870598c6d9d72",
@@ -137,7 +137,7 @@ export const SOURCE_HASHES: Readonly<Record<string, string>> = {
   "sine440-48000": "331dabe7b2984a0a",
 };
 
-/** A 32-bit float, mono WAV (fmt tag 3), every field little-endian: the C++ suite's writeFloatWav, byte for byte. */
+/** A 32-bit float, mono WAV (fmt tag 3), every field little-endian: the layout of the written sources. */
 export function floatWav(samples: Float32Array, rate: number): Uint8Array {
   const bytes = new Uint8Array(44 + samples.length * 4);
   const v = new DataView(bytes.buffer);
diff --git a/packages/retroplug/test-ui/kit-compile.test.ts b/packages/retroplug/test-ui/kit-compile.test.ts
index cba4c7d40..3ca71073c 100644
--- a/packages/retroplug/test-ui/kit-compile.test.ts
+++ b/packages/retroplug/test-ui/kit-compile.test.ts
@@ -6,9 +6,9 @@
 // packages/native/plugin/PluginDSP.cpp does - registerHostRpc + registerEmulatorRpc, nothing else - so it is the
 // narrowest honest host for this: the CLI and the native test host also mount the harness facet, and a Catch2 case
 // composing the plugin's registration could not run TypeScript at all. The offline jobs the compile is made of ride
-// the emulator facet (submitOfflineJob / drainOfflineJobs), which is why the plugin can compile a kit at all; the two
-// harness RPCs that compile a kit on the harness hosts (compileKit / compileDmc) are checked to be absent, since a
-// compile that needed them would pass here only by accident of some other host's registration.
+// the emulator facet (submitOfflineJob / drainOfflineJobs), which is why the plugin can compile a kit at all. The
+// harness facet is checked to be absent (its renderAudio is refused), so a compile that needed one of its methods would
+// pass here only by accident of some other host's registration.
 //
 // The compile is src/kit's entry point over the real backend, never an audio driver: making a driver turns the
 // offline poll's native wait on (src/offline.ts), and that is the settle path of a host with NO UI tick. The plugin
@@ -35,10 +35,7 @@ test("the plugin's facet set compiles a kit, with no harness facet bound", async
   for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 220) * Math.exp(-i / 3000);
   expect(be.writeFile(wav, encodeWav(pcm, 44100, 1))).toBeTruthy();
 
-  // The harness facet is NOT bound on this host: the RPCs the harness hosts compile a kit through are refused, and
-  // the render harness beside them is too.
-  expect(() => call("compileKit", { name: "", samples: [] })).toThrow();
-  expect(() => call("compileDmc", { name: "", samples: [] })).toThrow();
+  // The harness facet is NOT bound on this host: its renderAudio is refused.
   expect(() => call("renderAudio", 1)).toThrow();
 
   let result: KitCompileResult | null = null;
diff --git a/packages/retroplug/test/kit/kit.test.ts b/packages/retroplug/test/kit/kit.test.ts
index 08eb03f91..662254d7c 100644
--- a/packages/retroplug/test/kit/kit.test.ts
+++ b/packages/retroplug/test/kit/kit.test.ts
@@ -1,10 +1,10 @@
-// The TypeScript kit compiler's pure parts (src/kit/, docs/modular-graph-phase-d2-design.md section 5), with no native
-// host: the 4-bit level-to-code inversion over every level the dither node produces, the trim, the nibble pack with and
-// without rotate against a transcription of native convertCodesToNibbles, the DMC bit pack, pad and truncation, the
-// spec's refusals and their messages, the segment split at normalising gains and the documents each pass runs, the
-// range constants held to the two descriptor goldens, the bank assemblers' names byte for byte as native writes them,
-// and a slot's chain of jobs over a scripted runner (what it submits, where it stops short, what it does with a
-// failure). The banks themselves are held by test-native/kit-baseline (all 39 golden cases) and
+// The TypeScript kit compiler's pure parts (src/kit/, docs/modular-graph-phase-d2-design.md section 5), with no host:
+// the 4-bit level-to-code inversion over every level the dither node produces, the trim, the nibble pack with and
+// without rotate against an independent transcription of the LSDj nibble format, the DMC bit pack, pad and truncation,
+// the spec's refusals and their messages, the segment split at normalising gains and the documents each pass runs, the
+// range constants held to the two descriptor goldens, the bank assemblers' names byte for byte as the bank layouts
+// store them, and a slot's chain of jobs over a scripted runner (what it submits, where it stops short, what it does
+// with a failure). The banks themselves are held by test-native/kit-baseline (all 39 golden cases) and
 // test-native/kit-compile.
 import { test, expect } from "../../testing/harness";
 import { MockBackend } from "../../testing/mockBackend";
@@ -44,8 +44,9 @@ const F = Math.fround;
 // quotient rounded once from the double quotient is the binary32 quotient, and so for the multiply and the subtract.
 const levelOf = (code: number): number => F(F(F(code / 15) * 2) - 1);
 
-// Native SampleUtil::convertCodesToNibbles, transcribed.
-function nativeNibbles(codes: number[], rotate: boolean): number[] {
+// The LSDj nibble pack, transcribed independently of src/kit: stored = 0xF - code, and each frame rotated right by
+// one for 9.2.0+.
+function referenceNibbles(codes: number[], rotate: boolean): number[] {
   const out: number[] = [];
   for (let chunk = 0; chunk + 32 <= codes.length; chunk += 32) {
     const s = new Array<number>(32);
@@ -79,7 +80,7 @@ test("every 4-bit level the dither node outputs inverts to its own code, with ro
   expect(levelOf(15)).toBe(1);
 });
 
-test("the trim keeps whole 32-sample frames, and the nibble pack is native's with and without rotate", () => {
+test("the trim keeps whole 32-sample frames, and the nibble pack matches the independent transcription", () => {
   expect([0, 31, 32, 33, 63, 64, 1000].map(trimToFrames)).toEqual([0, 0, 32, 32, 32, 64, 992]);
   for (const n of [0, 5, 31, 32, 33, 64, 96 + 17, 1056 * 2]) {
     const codes = codesOf(n, n + 1);
@@ -87,7 +88,7 @@ test("the trim keeps whole 32-sample frames, and the nibble pack is native's wit
     for (const rotate of [true, false]) {
       const packed = packNibbles(levels, rotate);
       expect(packed.length, `${n} codes`).toBe(trimToFrames(n) / 2);
-      expect([...packed], `${n} codes, rotate ${rotate}`).toEqual(nativeNibbles(codes, rotate));
+      expect([...packed], `${n} codes, rotate ${rotate}`).toEqual(referenceNibbles(codes, rotate));
     }
   }
   // Rotate: a frame's last sample opens it, inverted (stored = 15 - code).
@@ -96,18 +97,18 @@ test("the trim keeps whole 32-sample frames, and the nibble pack is native's wit
   expect(packNibbles(ramp.map(levelOf), false)[0]).toBe(((0xf - 0) << 4) | (0xf - 1));
 });
 
-test("the resampled length is the kit path's floor(frames * (out / in)), and the DMC rates are native's table", () => {
+test("the resampled length is the kit path's floor(frames * (out / in)), and the DMC rates wav2dmc.py's", () => {
   expect(resampledLength(8192, 44100, 11468)).toBe(Math.floor(8192 * (11468 / 44100)));
   expect(resampledLength(600, 44100, 11468)).toBe(156);
   expect(resampledLength(0, 22050, 11468)).toBe(0);
   expect(resampledLength(3, 48000, 4177.4)).toBe(0);
   expect(PAL_DMC_RATES.length).toBe(16);
   expect(palDmcRate(12)).toBe(16965.4); // the compilers' default index
-  expect(palDmcRate(17)).toBe(4696.63); // native masks the index to its low four bits
+  expect(palDmcRate(17)).toBe(4696.63); // the index is masked to its low four bits
   expect(LSDJ_MAX_SAMPLE_FRAMES).toBe(32576);
 });
 
-test("the DMC pack is LSB first, padded to 16k + 1 bytes and cut at 4081, as RisaDmcCodec packs it", () => {
+test("the DMC pack is LSB first, padded to 16k + 1 bytes and cut at 4081, as the risa bank packs it", () => {
   const bitsOf = (bits: number[]): number[] => bits.map((b) => (b ? 1 : -1));
   expect([...packDmc([])]).toEqual([0]); // the encode of nothing: the one-byte legal stream
   expect([...packDmc(bitsOf([1, 0, 0, 0, 0, 0, 0, 0]))]).toEqual([0x01]);
@@ -209,7 +210,7 @@ test("a spec the nodes cannot carry is refused with the reason, checked on its b
   );
   expect(bounds.segments[0].nodes.length).toBe(4);
   expect(resolveEffects([{ type: "gain", normalize: true, gain: 150 }], 44100, true).segments[0].normalize).toBe(150);
-  // risa skips a dither unread, as RisaDmcCodec does.
+  // risa skips a dither unread: DMC is 1-bit, so a 4-bit dither has nothing to apply to.
   const unknownDither = { type: "dither", ditherType: "Floyd" } as unknown as KitEffect;
   expect(resolveEffects([unknownDither], 44100, false).dither).toBe(null);
 });
@@ -316,7 +317,7 @@ test("each pass's document: its buffer in at res:1, a mono chain of nodes, recor
   expect(dpcm.nodes[0]).toEqual({ id: "src", type: "rp.dsp.dpcm", settings: { buffer: "res:1", normalize: false } });
 });
 
-test("the LSDj assembler writes names as native's does: the UTF-8 bytes, a-z uppercased, nothing else changed", () => {
+test("the LSDj assembler writes names by the bank layout's byte rule: the UTF-8 bytes, a-z uppercased", () => {
   const name = (bank: Uint8Array, off: number, n: number): number[] => [...bank.subarray(off, off + n)];
   const bank = buildKitBank("baseline-kit", [
     { name: "kik", bytes: new Uint8Array(16) },
@@ -324,8 +325,8 @@ test("the LSDj assembler writes names as native's does: the UTF-8 bytes, a-z upp
   ]);
   expect(name(bank, 0x52, 6)).toEqual([..."BASELI"].map((c) => c.charCodeAt(0)));
   expect(name(bank, 0x22, 3)).toEqual([..."KIK"].map((c) => c.charCodeAt(0)));
-  // A non-ASCII name is its bytes (0xC3 0xA9), as a native string of it holds, then the fill; native's toupper leaves
-  // bytes past ASCII alone. The slot is present with no bytes: a named, zero-length entry, not the empty-slot marker.
+  // A non-ASCII name is its UTF-8 bytes (0xC3 0xA9), then the fill; the rule uppercases a-z only, so bytes past ASCII
+  // are left alone. The slot is present with no bytes: a named, zero-length entry, not the empty-slot marker.
   expect(name(bank, 0x25, 3)).toEqual([0xc3, 0xa9, 0x2d]);
   expect(bank[2] | (bank[3] << 8)).toBe(0x4060 + 16);
   expect(bank[4] | (bank[5] << 8)).toBe(0x4060 + 16);
@@ -334,9 +335,9 @@ test("the LSDj assembler writes names as native's does: the UTF-8 bytes, a-z upp
   expect(name(buildKitBank("ab", []), 0x52, 6)).toEqual([0x41, 0x42, 0x20, 0x20, 0x20, 0x20]);
 });
 
-test("the risa assembler filters names byte by byte as native does", () => {
+test("the risa assembler filters names byte by byte by the bank layout's rule", () => {
   const bank = assembleKitBank("straße", [{ dpcm: new Uint8Array(17), rate: 12, loop: false, name: "ßk" }]);
-  // Native keeps A-Z, 0-9 and '-' of the name's uppercased bytes, so the two bytes of the eszett are dropped whole
+  // The rule keeps A-Z, 0-9 and '-' of the name's uppercased bytes, so the two bytes of the eszett are dropped whole
   // (JavaScript's toUpperCase would have made it "SS").
   expect([...bank.subarray(0x1ec0, 0x1ec0 + 7)]).toEqual([..."STRAE"].map((c) => c.charCodeAt(0)).concat([0, 0]));
   // A sample name is its first three bytes, a-z uppercased, others as they are.
@@ -457,7 +458,7 @@ test("an LSDj slot that resamples to under one wave frame, or is empty, submits
     "the window holds no frames (offset 0, at or past the source's 0 frames)",
   ]);
   expect(runner.requests.map(typesOf)).toEqual([["load"], ["load"]]);
-  // Both are named, zero-length slots, as native's compileSample leaves them.
+  // Both are named, zero-length slots: the bank keeps a slot's name with no bytes.
   expect([kit.bytes[2] | (kit.bytes[3] << 8), kit.bytes[4] | (kit.bytes[5] << 8)]).toEqual([0x4060, 0x4060]);
   expect(String.fromCharCode(kit.bytes[0x22], kit.bytes[0x25])).toBe("SE");
 });
````

Run: `sed -n '12338,13170p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:native kit-baseline`
Expected: kit-baseline 3 passed and `# 39 banks match the golden's hashes`: the only runner now.

Run: `pnpm test:native kit-compile`
Expected: kit-compile 9 passed, every bank held to `kit-edges.json` alone.

Run: `pnpm test:ui`
Expected: `# 40 UI test file(s) passed (jobs=16)`.

- [ ] **Step 3: Run the suites**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 163 native test file(s) passed (jobs=16)`; only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: exit 0, `# 163 native test file(s) passed`; `136 skips in 61 files`, the baseline, and the skip check passes.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test`
Expected: `# 171 test file(s) passed (jobs=16)`.

Run: `pnpm typecheck`
Expected: `Done (both tsconfigs)`.

Run: `pnpm test:ui`
Expected: `# 40 UI test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `pass 12, fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:n8`
Expected: the n8 binary 529 / 57 and the serial binary 7 / 1, as the report records them; report both lines.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK`.

- [ ] **Step 4: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad]"`
Expected: `No test cases matched '[n8]'` first (`[n8]` is not an engine-graph tag, so dropping it changes no case), then `engine-graph 12995 / 93`, no ThreadSanitizer report (the prototype ran this selection with the fixtures present; 12995 is the lower figure of the two trees' runs, so 12997 is within the rule). This selection includes `[capability]`, so a run may report 2 or 4 more assertions than this quote, from the device-thread progress case, as the ground rules say.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[resource],[resources]"`
Expected: graph `252500 assertions in 66 cases`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad]"`
Expected: `No test cases matched '[n8]'` first, then the engine-graph binary passes (`engine-graph 12995 / 93`), no report: this selection includes `[capability]` too, so the same 2 or 4 more assertions than quoted are normal, and it passes in both trees.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: the whole graph binary passes (`graph whole suite 356087 / 272`), no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 5: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M .github/workflows/build.yml
 M CMakeLists.txt
 m deps/sameboy
 M packages/native/CMakeLists.txt
 M packages/native/cmake/tests.cmake
 M packages/native/node/README.md
 M packages/native/node/napi/Writer.hpp
 M packages/native/src/dsp/Dpcm.hpp
 M packages/native/src/host/engine/OfflineJobs.cpp
 M packages/native/src/host/engine/OfflineJobs.hpp
 M packages/native/src/host/rpc/BackendRpcRegistration.hpp
 M packages/native/src/host/rpc/BackendTypes.hpp
 M packages/native/src/host/rpc/EngineRpcService.cpp
 M packages/native/src/host/rpc/EngineRpcService.hpp
D  packages/native/src/kit/KitCodec.hpp
D  packages/native/src/kit/KitCompiler.cpp
D  packages/native/src/kit/KitCompiler.hpp
 M packages/native/src/kit/SampleCache.hpp
 M packages/native/src/lsdj/Effects.cpp
 M packages/native/src/lsdj/Effects.hpp
?? packages/native/src/lsdj/KitBank.hpp
D  packages/native/src/lsdj/KitUtil.cpp
D  packages/native/src/lsdj/KitUtil.hpp
D  packages/native/src/lsdj/LsdjKitCodec.cpp
D  packages/native/src/lsdj/LsdjKitCodec.hpp
D  packages/native/src/lsdj/SampleUtil.hpp
D  packages/native/src/risa/RisaDmcCodec.cpp
D  packages/native/src/risa/RisaDmcCodec.hpp
 M packages/native/test/audio/Dither.test.cpp
D  packages/native/test/audio/KitBaseline.test.cpp
 M packages/native/test/engine-graph/OfflineJobs.test.cpp
 M packages/native/test/graph/Dsp.test.cpp
 M packages/retroplug/scripts/run-native-tests.mjs
 M packages/retroplug/src/kit/chain.ts
 M packages/retroplug/src/kit/compile.ts
 M packages/retroplug/src/kit/pack.ts
 M packages/retroplug/src/lsdj/rom/buildKit.ts
 M packages/retroplug/src/lsdj/rom/constants.ts
 M packages/retroplug/src/lsdj/rom/kit.ts
 M packages/retroplug/src/risa/rom/kit.ts
 M packages/retroplug/test-native/kit-baseline.test.ts
 M packages/retroplug/test-native/kit-compile.test.ts
 M packages/retroplug/test-native/kit/sources.ts
 M packages/retroplug/test-ui/kit-compile.test.ts
 M packages/retroplug/test/kit/kit.test.ts
 M packaging/licenses/THIRD-PARTY-NOTICES.txt
 M spec/02-native-host.md
 M spec/06-build-test.md
````

````bash
git add -- packages/native/src/lsdj/KitBank.hpp .github/workflows/build.yml CMakeLists.txt \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake packages/native/node/README.md \
  packages/native/node/napi/Writer.hpp packages/native/src/dsp/Dpcm.hpp \
  packages/native/src/host/engine/OfflineJobs.cpp packages/native/src/host/engine/OfflineJobs.hpp \
  packages/native/src/host/rpc/BackendRpcRegistration.hpp packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/src/kit/SampleCache.hpp packages/native/src/lsdj/Effects.cpp packages/native/src/lsdj/Effects.hpp \
  packages/native/test/audio/Dither.test.cpp packages/native/test/engine-graph/OfflineJobs.test.cpp \
  packages/native/test/graph/Dsp.test.cpp packages/retroplug/scripts/run-native-tests.mjs \
  packages/retroplug/src/kit/chain.ts packages/retroplug/src/kit/compile.ts packages/retroplug/src/kit/pack.ts \
  packages/retroplug/src/lsdj/rom/buildKit.ts packages/retroplug/src/lsdj/rom/constants.ts \
  packages/retroplug/src/lsdj/rom/kit.ts packages/retroplug/src/risa/rom/kit.ts \
  packages/retroplug/test-native/kit-baseline.test.ts packages/retroplug/test-native/kit-compile.test.ts \
  packages/retroplug/test-native/kit/sources.ts packages/retroplug/test-ui/kit-compile.test.ts \
  packages/retroplug/test/kit/kit.test.ts packaging/licenses/THIRD-PARTY-NOTICES.txt spec/02-native-host.md \
  spec/06-build-test.md
git commit -F - <<'EOF'
refactor(kit): native KitCompiler, its codecs and the harness kit RPCs retire

A kit compile is TypeScript's: src/kit builds each slot's chain of offline graph jobs, the nodes run the DSP and
pack the bank, and every caller (the CLI sessions, audioDriver, the plugin) has gone through it since.
So native's own compile goes, with the surface only it used: KitCompiler and KitCodec, LsdjKitCodec, RisaDmcCodec,
KitUtil's compileSample and buildKit, SampleUtil's nibble helpers, the harness facet's compileKit / compileDmc and
their DTOs in BackendTypes.hpp, and EngineRpcService's lazy compiler. The facet drops from 22 methods to 20, the full
union from 92 to 90.

What stays is what a node, the seam or a test still runs: src/dsp/ whole, SampleCache (an offline job decodes its file
inputs through it), and lsdj/Effects, whose processEffect / quantise are the kit-path reference the graph and audio
tests hold rp.dsp's nodes to. The LSDj bank's numbers move to lsdj/KitBank.hpp, which is what the seam's file-input
cap and the tests that hold it to the kit window name.

The C++ kit baseline suite goes with the compiler it held: test-native's kit-baseline runner and the shared golden are
the oracle now, and kit-compile.test.ts keeps every case, holding each bank to kit-edges.json alone and each refused
slot against the same kit compiled with that slot's file missing. No golden is re-recorded.

retroplug-core no longer links the DSP kernels, which only the compile reached through it, and retroplug-audio-test
names retroplug-dsp-kernels in its own LIBS for the Effects.cpp wrappers it compiles. No RetroPlug target builds or
links enkiTS, so its add_subdirectory and its third-party credit go; the vendored submodule stays. CI's Catch2 run
drops RP_KIT_GOLDEN_REQUIRE_TARGET, which only the deleted suite read.
EOF
````

---

### Task 7: the docs say what D2 built

Design section 7's seventh commit and section 11. The design's status line says built; sections 3 to 6 are corrected
where the build made them false; sections 8 and 9 carry what D3 inherits and what is left beyond D2; and a new section
11 records the build in D1's style: each stage's decisions, section 10's five questions answered (with Task 4's
throughput table), the guards, the non-finite flag and the pinned floating-point environment, what building it found,
and the suite as built. D1 keeps its record and gains only pointers where it stated the native compiler and the C++
baseline as current. The specs and `AGENTS.md` follow, and `docs/lsdj.md` and `docs/modular-graph-design.md` describe
the compiler that exists.

**Files:**

- Modify: `AGENTS.md`, `docs/lsdj-sav-ts-design.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-d1-design.md`, `docs/modular-graph-phase-d2-design.md`, `spec/00-overview.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/04-roles-protocol-nodes.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/13-subsystems.md`

- [ ] **Step 1: The design's record**

````diff
diff --git a/docs/modular-graph-phase-d1-design.md b/docs/modular-graph-phase-d1-design.md
index d3a900fc9..26762f87d 100644
--- a/docs/modular-graph-phase-d1-design.md
+++ b/docs/modular-graph-phase-d1-design.md
@@ -4,10 +4,11 @@
 build and sections 3 to 5 are corrected in place.
 
 Phase D gives the graph DSP of its own ([modular-graph-design.md](modular-graph-design.md) section 8): library nodes
-(gain, biquad, dither, resample), kit compiles as offline graphs held byte-identical to today's `KitCompiler`, and
-renders on the offline executor. It is split in three. D1 (this design) makes kit output reproducible, records it as a
-baseline, moves the kit path's DSP into a core-free library of block-streaming kernels, and puts that library in the
-graph as `rp.dsp` nodes. D2 builds the offline driver (audio buffers in and out of a document, a run to completion,
+(gain, biquad, dither, resample), kit compiles as offline graphs held byte-identical to native `KitCompiler` (retired in
+D2; its [section 11](modular-graph-phase-d2-design.md#11-as-built) has the build), and renders on the offline executor.
+It is split in three. D1 (this design) makes kit output reproducible, records it as a baseline, moves the kit path's DSP
+into a core-free library of block-streaming kernels, and puts that library in
+the graph as `rp.dsp` nodes. D2 builds the offline driver (audio buffers in and out of a document, a run to completion,
 normalise and the two-rate kit chain) and compiles kits as graphs, held to D1's baseline. D3 moves the CLI and
 background renders onto that driver.
 
@@ -71,7 +72,8 @@ Each case's bank is recorded in a golden JSON beside the test as its hash, size
 re-recorded only with `RP_KIT_GOLDEN_UPDATE=1`. Exact bytes are compared on `linux-x64-gnu13` (as parity does), off it
 a case must agree with itself across two runs while its size and slot length are still held, and
 `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turns the fallback into a failure on CI's linux-x64 job. From then on a change that
-moves a kit byte fails this suite.
+moves a kit byte fails this suite. D2 retired this suite with the compiler it drove: the matrix and the golden are
+unchanged, and `test-native/kit-baseline.test.ts` is their runner now (D2 design section 11).
 
 ## 4. The DSP library
 
@@ -196,7 +198,8 @@ beside it, so only a build that wants the resampler reaches r8brain.
   tags under both sanitizers. The tags as built are `[dsp]`, `[dsp-nodes]` and `[dsp-resample]` in
   `retroplug-graph-test`, `[kit-baseline]` and `[dither]` in `retroplug-audio-test` (both carry `[kit]` too, so one
   run covers the kit path's whole byte surface), and `kit-dither` in
-  `test-native`.
+  `test-native`. (`[kit-baseline]` and its suite went with the compiler in D2; the other tags stand, and D2 added
+  `[dsp-buffer-nodes]` and `[offline]`.)
 
 ## 7. Landing
 
@@ -243,8 +246,10 @@ implementers and follow-ups, Opus code reviews of tasks 3 and 5, a DeepSeek acce
     filter r8brain is FIR, so an isolated NaN poisons a few blocks of its output and the dither kernel latches (a NaN's
     code is 0 for the rest of the slot), where `rp.dsp.dither` restarts and recovers; with a filter the biquad kernel
     latches for good, where `rp.dsp.biquad` resets. A kit's byte identity is at stake in both regimes, so D2 decides
-    for both. Above Nyquist the biquad's guard also only bounds the damage: until it fires the kernel's clamp writes
-    rail-to-rail output, so a held above-Nyquist setting is full-scale bursts between the guard's silent blocks.
+    for both (as built: a source with a non-finite sample is refused, and a pass a guard stepped into refuses its
+    slot; D2 design section 11). Above Nyquist the biquad's guard also only bounds the damage: until it fires the
+    kernel's clamp writes rail-to-rail output, so a held above-Nyquist setting is full-scale bursts between the
+    guard's silent blocks.
   - **A resampler must not outlive r8brain's static members.** `rp.dsp.resample` keeps its `rp::dsp::Resampler`s as
     instance members, and a `Resampler`'s destructor calls r8brain's `unref`, which locks a static mutex and writes into
     cache-owned objects (`CDSPFIRFilter`, `CDSPFracDelayFilterBank`): destroyed after `R8bBase.cpp`'s statics, it is a
diff --git a/docs/modular-graph-phase-d2-design.md b/docs/modular-graph-phase-d2-design.md
index 35c0ca468..e3be8fc9f 100644
--- a/docs/modular-graph-phase-d2-design.md
+++ b/docs/modular-graph-phase-d2-design.md
@@ -1,6 +1,7 @@
 # Phase D2: kit compiles as offline graphs
 
-**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 10
+**Date:** 2026-11-01 · **Branch:** `feature/modularize` · **Status:** built (D2, seven commits); section 11 records
+the build, sections 3 to 6 are corrected in place, and sections 8 and 9 carry what it learned.
 
 D1 ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md)) made kit output reproducible, recorded it as
 a byte baseline, moved the kit DSP into core-free kernels and put those kernels in the graph as `rp.dsp` nodes. D2 runs
@@ -54,26 +55,31 @@ The user's, on 2026-11-01:
 **A job.** One engine RPC submits a job: a graph document, its input buffers (interleaved float samples with a rate and
 a channel count, or a file path native decodes, below), which recorded buffers to return, and the frames to run. It
 returns a job id, or a refusal; completions (the job id, ok or the reason, the returned buffers) drain from a polled
-queue, the C5 capability-job pattern. TypeScript gets `backend.offlineRun(doc, inputs, outputs, frames)`, a promise
-over submit and poll.
+queue, the C5 capability-job pattern. TypeScript gets `backend.offlineRun(request)`, a promise over submit and poll.
 
-**Where it runs.** A small native worker pool (one thread per running job, sized to the machine as `RenderJobRegistry`
-is). Each job gets a fresh offline `GraphHost`: its own resource table, no audio thread, the quiescent block driver,
-running `GraphHost::run` in fixed blocks for the requested frames (no second block driver). The pool is apart from the
-live engine, so a job never touches the plan that is playing; a job's host is built for it and torn down with it, so
-every resampler dies inside that host's life (D1's teardown rule for r8brain's statics).
+**Where it runs.** A small native worker pool (one thread per running job, at most clamp(hardware_concurrency, 1, 8)
+workers, started on demand and parked when idle). Each job gets a fresh offline `GraphHost`: its own resource table, no
+audio thread, the quiescent block driver, calling `GraphHost::processBlock` (drain, then run) in fixed blocks for the
+requested frames (no second block driver). The pool is apart from the live engine, so a job never touches the plan that
+is playing; a job's host is built for it and torn down with it, so every resampler dies inside that host's life (D1's
+teardown rule for r8brain's statics).
 
 **Inputs and outputs.** An input becomes an `audio-buffer` resource through `createAudioBuffer`, named in the document
-as the `res:N` handle the submission assigns. A file input is decoded natively through `SampleCache` (the same
-miniaudio decode as today) and windowed natively by an offset, a length and a frame cap (as `compileSample` does); its
-creation reports whether any sample is non-finite. A buffer a run writes through `rp.core.buffer-recorder` is returned
-in the completion, its samples, rate and channel count.
+as the `res:N` handle the submission assigns. A file input is decoded natively through `SampleCache` (the miniaudio
+decode the kit compiler reads its sources with) and windowed natively by an offset, a length and a frame cap (as the
+kit compiler's window does); its creation reports whether any sample is non-finite. A buffer a run writes through
+`rp.core.buffer-recorder` is returned in the completion, its samples, rate and channel count, and whether any sample in
+it is not finite.
 
 **Every host.** The RPC sits in a facet the plugin, the SDL standalone, the CLI and the Node addon all bind. A job runs
 off every audio and UI thread and allocates nothing on any block thread.
 
-**Failures.** A planner refusal, an activate error (an r8brain allocation failing) or a cancel completes the job with
-the reason. Destroying the engine cancels its jobs and waits for their threads.
+**Failures.** A planner refusal, an activate error (an r8brain allocation failing), a node fault or a cancel completes
+the job with the reason. A guard a node reports does not fail a job: the completion carries how many times one fired
+and the first reason, and a non-finite value in a returned buffer is flagged, so what either means is the caller's to
+decide (a kit compile refuses the pass). Destroying the engine abandons its jobs in flight: the queued ones are dropped
+unrun, the running ones stop at their next block boundary, every worker is joined, and completions not yet drained go
+with the pool.
 
 ## 4. The nodes D2 adds or changes
 
@@ -81,22 +87,28 @@ the reason. Destroying the engine cancels its jobs and waits for their threads.
   -96..96 dB. The planner cannot see the plan's rate, so the kit compiler refuses a frequency at or above its source
   rate's Nyquist itself. The descriptor goldens re-record once; live patches get the wider ranges.
 - **`rp.dsp.normalize`**: a buffer player that normalises. It names an `audio-buffer` in its settings (as
-  `rp.dsp.resample` does); at activate, on the control thread, it computes the whole buffer's `rp::dsp::peak` and
-  `rp::dsp::normalizedGain(gain, peak)`, then streams the buffer times that gain, sample for sample the multiply
-  `GainEffect` makes today.
+  `rp.dsp.resample` does); at activate, on the control thread, it computes the peak over the channels it plays (the
+  loudest of each played channel's `rp::dsp::peak`) and `rp::dsp::normalizedGain(gain, peak)`, then streams the buffer
+  times that gain, sample for sample the multiply `GainEffect` makes today. A buffer channel it does not play is
+  silence and never scales a played one.
 - **`rp.dsp.dither` gains `seedFrom`**: an optional buffer reference; when set, activate seeds every channel from
   `rp::dsp::inputSeed` over that whole buffer, so channel 0 matches the kit path's seed. The kit graph points it at the
   resampled buffer the dither plays, at its full pre-trim length.
 - **`rp.dsp.dpcm`**: risa's DMC encoder as a buffer-reading node. `convertF32ToDpcm`'s whole-buffer DC blocker, peak
   normalise and 1-bit delta move into a kernel in `retroplug-dsp-kernels`; the node runs the kernel over its bound buffer
   at activate into storage it owns, then streams the delta bits as audio levels of plus and minus one.
+- **`rp.host.guards/1`**: a host service (`rp_node.h`) a node reports into when one of its guards replaces what its
+  kernel would have computed, with a string-literal reason, called on the block thread and never blocking or
+  allocating. The biquad's reset-and-silence and the dither's restart report there; the live engine offers no service,
+  so a live patch's guard is as it was, and an offline job's host counts the reports into its completion.
 - **Unchanged**: `rp.dsp.resample`, `rp.core.buffer-player`, `rp.core.buffer-recorder`.
 
 ## 5. The TypeScript kit compiler
 
-`compileKit(spec)` and `compileDmc(spec)` become TypeScript (`packages/retroplug/src/kit/`) with the signatures the CLI
-sessions call today, returning the same bank bytes. Each slot is a chain of offline jobs; slots run in parallel up to
-the pool's size.
+`compileKit(spec)` and `compileDmc(spec)` become TypeScript (`packages/retroplug/src/kit/`), returning the same bank
+bytes: their first argument is a runner (a backend's offline jobs) and their result is `{bytes, errors}`, the bank and
+one reason per spec sample, null exactly when the bank holds that sample whole. Each slot is a chain of offline jobs;
+slots run in parallel up to the pool's size.
 
 **LSDj, per slot, in today's order:**
 
@@ -117,7 +129,10 @@ TypeScript packs the delta bits LSB first, pads to a legal `16k+1` length and as
 TS risa packer.
 
 **Failures, as today**: a slot that fails is left empty and the kit still compiles; new, the result also lists each
-slot's error, so the CLI's read-back check gains a reason. A refused spec is that slot's error.
+slot's error, so the CLI's read-back check gains a reason. A refused spec is that slot's error, and so is a pass that
+records a non-finite sample or one a node's guard stepped into. Every slot the bank does not hold whole has a reason,
+a failure or not: an empty window, a window that resamples to under one wave frame, a sample the bank's space clipped
+or could not fit, and a sample past the bank's slot count.
 
 **Retired**: `KitCompiler`, the compile use of `SampleCache` beyond the job seam's decode, `LsdjKitCodec`,
 `RisaDmcCodec`'s encode, and the harness-only `compileKit` / `compileDmc` RPCs. Kernels and helpers the nodes or tests
@@ -125,15 +140,18 @@ use stay.
 
 ## 6. Tests
 
-- **The baseline is the oracle.** `kit-baseline.json` (D1's 39 entries) stays the one record. A new test-native suite
+- **The baseline is the oracle.** `kit-baseline.json` (D1's 39 entries) stays the one record. A test-native suite
   runs the TypeScript compiler through real offline jobs over the same matrix with the same synthesised sources and
-  compares each bank's FNV hash and slot length to the golden. Until native retires the C++ suite still holds native
-  `KitCompiler` to the same file, so both compilers are held to one record. Every entry must match through the graph
+  compares each bank's FNV hash and slot length to the golden. While both compilers exist, the C++ suite holds native
+  `KitCompiler` to the same file too, so they are held to one record until it retires with the compiler; a second
+  golden, `kit-edges.json`, records the banks past the baseline's matrix. Every entry must match through the graph
   compile before any caller moves; a mismatch means the graph is wrong, and nothing is re-recorded.
 - **The seam**: a job runs a document to completion and returns its buffers; parallel jobs do not interfere (TSan); a
-  cancel stops a job; an engine torn down with jobs in flight completes them with an error; a job allocates nothing on
-  any audio thread; file inputs equal today's `SampleCache` decode; a job runs while an audio thread plays the live plan
-  and changes nothing in it.
+  cancel stops a job; an engine torn down with jobs in flight abandons them (the running ones stop and nothing
+  completes); a job allocates nothing on any audio thread; file inputs equal the `SampleCache` decode; a job runs while
+  an audio thread plays the live plan and changes nothing in it; a guard a node reports is counted in the completion
+  and a non-finite recording is flagged; a job computes in the default floating-point environment whatever its
+  submitter's.
 - **The nodes**: `rp.dsp.normalize` equals today's normalising `GainEffect` bit for bit; `seedFrom` gives channel 0 the
   kit path's seed; `rp.dsp.dpcm`'s bits equal `convertF32ToDpcm`'s; the widened descriptors' goldens.
 - **Refusals**: a non-finite source, a filter at or above Nyquist and each out-of-range value give their errors, and a
@@ -165,12 +183,37 @@ implementers and follow-ups, Opus code reviews of tasks 2, 4 and 6, a DeepSeek a
 ## 8. Left for D3
 
 The CLI `render` and the UI's background renders on the offline jobs, keeping their block-size sequence and per-block
-side effects or re-scoping their byte-identity claim.
+side effects or re-scoping their byte-identity claim. What the seam hands them, and what D2's build settled about it:
+
+- A render sees `guardsFired` / `guardReason` and each buffer's `nonFinite`, and decides. Nothing fails on either, and
+  a cancelled or torn-down job has no buffers; a render that must stay byte-exact refuses a job whose count is not 0,
+  as the kit compiler does.
+- A job runs in the default floating-point environment whatever the caller's is (the audio thread's flush-to-zero
+  included), so a render's arithmetic is the CLI's.
+- Jobs are async and bounded (64 outstanding, up to 8 workers); the UI's render jobs already own a registry and a
+  thread each, and moving them onto this seam replaces that with the engine's pool. A host with no UI tick settles a
+  job through the native wait (`setOfflineWait`, which `createAudioDriver` turns on) or not at all.
+- Block size is the job seam's (1024 frames), not the caller's: D3 either accepts that, or the seam grows a block-size
+  request and a render's per-block side effects move with it.
+- What a render needs and the seam does not offer, so D3 must extend or decide it:
+  - The job registry is `rp.core`, `rp.dsp` and `rp.dsp.resample` only. A render builds an emulated system and runs its
+    roles' protocol script nodes, so system nodes (a `SystemFactory`, ROM and battery resources) and a script host must
+    reach a job, against invariant 9 and section 11's "a job needs nothing from the engine".
+  - There is no TypeScript cancel (stage 2 left it out on purpose, and `offline.ts` has none), where the UI's renders
+    cancel today.
+  - A completion returns its buffers only at the end of its fixed `frames`, where a render streams per-chunk WAV and
+    resolves its length mid-run (LSDj HFF auto-detect via NR52).
+  - Jobs are not rejected at project close (stage 2), where capability jobs are.
 
 ## 9. Left beyond D2
 
-- A UI for compiling kits in the plugin: D2 makes it possible and adds no menu row.
-- User-authored sample graphs on the same seam.
+- A UI for compiling kits in the plugin: D2 makes it possible and adds no menu row. The row imports `src/kit` and runs
+  it on the stores' backend, not `audioDriver`: the driver's compile is a CLI convenience, and a UI row must not turn on
+  the offline poll's native wait, which would block the frame the compile settles on.
+- User-authored sample graphs on the same seam. A script runtime for jobs is not one: a job's document may name only
+  the built-in C++ packages, so a user graph of script nodes needs a job-side script host of its own.
+- The `deps/enkiTS` submodule, kept but unbuilt: no RetroPlug code links it since the native compile retired, so a
+  later cleanup decides whether the submodule goes.
 
 ## 10. Prototype questions
 
@@ -183,3 +226,270 @@ side effects or re-scoping their byte-identity claim.
   which side is right?
 - How long is a 15-slot kit compile as jobs against today's enkiTS compile, and is the per-job host setup the cost?
 - Does the plugin's facet set allow a new engine facet, and how do completions reach a plugin UI that only polls?
+
+## 11. As built
+
+Section 7's seven commits landed in order, each green, and nothing on the emulated audio path moved: strict parity held
+bit for bit at every one, and no parity or kit golden was re-recorded (the `rp.dsp` descriptor goldens were, in
+stages 1 and 3, as their bullets say). The 39 baseline banks matched the TypeScript compiler through real offline jobs
+on the first run, and no later commit moved one; `kit-edges.json` was recorded once, from the compiler, in a run where
+every bank also equalled native's. This section records what each stage decided where the design was silent, what
+section 10 turned out to be, what building it corrected in sections 3 to 6 (each correction is in place there), and
+the findings worth naming; each stage's own report is the full record. Reviews asked for fixes, landed as follow-ups
+inside the stages' own commits: stage 1's clamp cases made to bite and its comments corrected, stage 2's poll stalled
+chains that await through extra async layers, stage 3's DPCM re-encode moved off the block thread and normalize's peak
+made the channels it plays, stage 4's guard handling, non-finite flag and floating-point environment added (the guard
+reporting where it first failed a job), stage 5's plugin test settled through a path the plugin does not have, and
+stage 6 took enkiTS out of the build. Where a fix changed what a stage built, its bullet says so, and no follow-up
+moved a kit byte or a parity baseline either.
+
+### What each stage settled
+
+- **1, the widened ranges.** `rp.dsp.gain` is linear -100..100 (default 1) and `rp.dsp.biquad` 1..96 000 Hz,
+  Q 0.001..1000 and -96..96 dB, so a kit spec's plain gain, its normalising gain, a 10 Hz high-pass at Q 20 and a
+  30 dB shelf all reach the kernels as written. The planners and `setParam` still clamp a document's value silently and
+  a wire is still unclamped, and the biquad clamps nothing itself, Nyquist included, so the compiler does the refusal
+  (stage 4). The two descriptor goldens were re-recorded once; the diff is exactly each param's `min` / `max`. Every
+  clamp case asks beyond a bound, runs the kernel at the asked value too, and requires the two to differ, so it proves
+  the clamp rather than a coincidence at the bound. The frequency ceiling (96 kHz) cannot be a clamp oracle on a
+  48 kHz plan, because the guard fires above Nyquist there; the low bounds carry that case's proof.
+- **2, the offline seam.** The pool is an `Engine` member (`OfflineJobs`) made at the first submission and reset
+  explicitly in `~Engine`, so a host that never submits has no registry and no thread. It runs
+  clamp(hardware_concurrency, 1, 8) workers, started on demand while every started one is busy and never retired, with
+  64 jobs outstanding (queued, running or finished-undrained) and 64 inputs plus outputs per job. A job's blocks are
+  1024 frames, the last partial; its transport is its rate, 120 bpm, ppq 0, not playing. Handles are deterministic:
+  input i is `res:(i + 1)`, output j is `res:(inputs + j + 1)`, computed on the TS side (`offlineInputRef` /
+  `offlineOutputRef`), and `returns` defaults to every output. The registry is the pool's, filled before any worker
+  exists with rp.core, rp.dsp and rp.dsp.resample, never changed, and shared read-only, so a script pack's load or
+  reload (which swaps only the engine's registry) cannot race a job; a script, system or host I/O node in a document is
+  refused as `unknown-type`. Each job decodes through a `SampleCache` of its own, so a file edited between two compiles
+  decodes as it is now. Interleaved float32 crosses the wire in the host's byte order. An input of no frames is
+  accepted, reported and returned, but its handle names a placeholder freed at once, and an output of no frames is
+  refused, so a caller whose chain meets an empty buffer submits nothing after it. A node fault fails the job: a
+  bypassed node means the recording is not what the document asked for. A cancel completes a queued job at once and
+  stops a running one at its next block boundary (or after its last block), both with the reason `cancelled`, and a
+  cancel that returned true always completes `cancelled`. `drainOfflineJobs` takes a wait (at most 1000 ms) and answers
+  the completions plus `inFlight`, the jobs still queued or running, which is how the TS side tells a live promise from
+  one whose job is gone. Stage 2's fix pass made the TypeScript poll drain without waiting while its chain is busy
+  (blocking only after eight quiet steps, so a chain that awaits through async layers reaches its next submission),
+  held runs past the 64-job bound in a FIFO instead of refusing them busy, started a worker when the only parked one
+  was claimed, and rejected a pending promise native no longer holds.
+- **3, the nodes.** `rp.dsp.normalize`, `rp.dsp.dpcm` and dither's `seedFrom` are all in `rp.dsp`, in the existing
+  `retroplug-dsp-nodes` target, so the pool's registry planned them unchanged; `rp.dsp.dpcm` declares `heap`, the
+  package's first such type, which shows a `heap` type stays here while rp.dsp.resample's split is about r8brain at
+  link time. Normalize's settings are `buffer`, `gain` and `channels`, its port is fixed `channels` wide, it plays
+  frames from frame 0 and keeps its position across a re-activation, and a settings commit re-folds a changed gain
+  against the peak the activation measured. Dither's effective seed is settled in `on_settings` as well as `activate`,
+  because a plan replacement commits settings to the live instance without re-activating it (the existing
+  `[dsp-nodes]` seed case caught that). The DPCM kernel writes one unpacked bit per byte and needs no scratch (two
+  passes over the input); the node encodes at activate into storage it owns and declares `normalize` its `rebuild_key`,
+  so a toggle rebuilds and re-encodes on the control thread rather than running a whole-buffer pass in a block-thread
+  commit. "No buffer named" is a `settings-invalid` refusal for both new nodes, and `rp.dsp.dpcm` refuses a
+  multi-channel buffer at activate. The review's fix pass moved the DPCM re-encode off the block thread, gave each node
+  a commit case, and made normalize's peak the channels it plays. Its descriptor goldens: dither's re-recorded for
+  `seedFrom`, and `normalize`'s and `dpcm`'s added.
+- **4, the compiler.** `src/kit/` is three files: `pack.ts` (the integer half and the three assemblers' constants),
+  `chain.ts` (validation, the refusals and the documents) and `compile.ts` (the two entry points). A slot's chain
+  starts with a document-less load job whose file input carries the cap, so native hands back the windowed samples and
+  the decoded rate. Missing spec fields take the native structs' defaults, which is a real departure from the old RPC
+  (its decode had no defaults, so any effect field left out threw for the whole kit); a non-finite normalising gain, an
+  out-of-range value and a filter at or above the source's Nyquist are refused after the load job, before any DSP pass
+  (Nyquist is relative to the rate the load decoded). Samples past the
+  bank's slot count are not compiled and get a reason. The two TypeScript assemblers were changed to native's byte
+  rules: native uppercases only a-z over the UTF-8 bytes it receives, and JavaScript's Unicode `toUpperCase` can change
+  a name's length, so no baseline bank moved but non-ASCII names now agree too. The two test-native files are
+  `kit-baseline` (the 39 banks) and `kit-compile` (the refusals, and the edge banks past them); `SOURCE_HASHES` binds
+  the synthesised sources to what the retired C++ suite generated, and both goldens are read at run time through
+  `__KIT_GOLDEN__` / `__KIT_EDGES_GOLDEN__`. A refused risa slot is empty (its entry 0xFF) where its empty window
+  alone would have placed `{0}`, and a refused LSDj slot keeps its name at length 0, which differs from the kit
+  without that slot.
+- **5, the switch-over.** `audioDriver.compileKit` / `compileDmc` return `Promise<KitCompileResult>` and run `src/kit`;
+  their runner is `realBackend`'s one offline queue (a second `OfflineJobs` would drain the same native completion queue
+  behind the first), and `createAudioDriver` turns the poll's native wait on. `runSession` and `CliTool.run` accept a
+  promise, so `await` works in the CLI's baked-in sessions and in a consumer's. The per-slot reason reaches the CLI's
+  read-back check, and a clipped slot with bytes prints a warning. The plugin's compile is checked on the UI test host,
+  whose registration is exactly the plugin's: it settles the compile from `ui.pump` + `backend.pumpJobs` alone, proves
+  a microtask-only stretch settles nothing, and asserts the harness RPCs are unbound. The compiler's callers moved with
+  it: the compile code reached the CLI bundle and the render worker's session path, not the UI bundle. `test:cli-ts`'s
+  new block drives the real binary's async exit path: the bank is on disk when `build-kit` exits 0, and a missing
+  source exits 1 with its reason on stderr. This stage also exposed a latent bug: the CLI's `--filter` left `gain` out
+  of the effect, so the old RPC had thrown `Field named 'gain' not found` for every such kit and the flag never
+  worked; the TypeScript compiler fills the 0 dB default and the flag now compiles.
+- **6, native retires.** `KitCompiler`, `KitCodec.hpp`, `LsdjKitCodec`, `RisaDmcCodec`, `KitUtil`, `SampleUtil`, the
+  harness `compileKit` / `compileDmc` RPCs, their DTOs, the `LsdjEffect` tagged union and the C++ baseline suite are
+  gone. What tests and the seam still name of the LSDj bank moved to `src/lsdj/KitBank.hpp` (the rate, `0x3fa0` and
+  its frame count), documented as the format the TypeScript compiler packs. `retroplug-core` compiles only
+  `SampleCache.cpp` from the kit path and links neither DSP library nor enkiTS any more; `retroplug-audio-test` and
+  `retroplug-graph-test` compile `src/lsdj/Effects.cpp` themselves, as the kit path's reference. The harness facet is
+  20 methods and the union 90 (was 22 and 92). enkiTS leaves the build entirely (the `add_subdirectory`, the warning
+  suppression and the third-party credit), since no shipped binary can contain it now; the submodule stays. CI's
+  Catch2 step drops `RP_KIT_GOLDEN_REQUIRE_TARGET` with the suite it guarded, and the `test:native` step keeps it,
+  because the TypeScript goldens still compare exact bytes there. The C++ seam test no longer holds the compiler's side
+  of an empty window: `kit-compile`'s EMP slots and the edges golden do.
+- **7, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build (and `src/dsp/`'s nodes, the
+  compiler and the seam describe what they do now rather than what they replaced).
+
+### Section 10's questions, answered
+
+- **Can `createAudioBuffer`, the resource table and a fresh `GraphHost` be built and torn down per job off the control
+  thread, with the registry shared read-only, and what does a job need from the engine?** Yes. A job's table, inputs,
+  outputs, host, plan and node instances are locals of the worker's run function, destroyed on that thread with the
+  table after the host; the pool's registry is filled once before any worker exists; and a job needs nothing from the
+  engine: no script host (script, system and host nodes are refused), no packages beyond the three statics. TSan is
+  clean with 25 jobs in parallel and beside a live audio thread, and a job changes nothing of the engine's plan.
+  Tests may give an `OfflineJobs` they build directly extra packages, which is how the activate-failure and fault
+  cases get their nodes.
+- **What does the window do at the edges?** `start = min(offset, size)` and
+  `frames = min(cap, min(length == 0 ? size : length, size - start))`, so an offset at or past the end is an empty
+  window (not an error), a zero length means "to the end", and the cap is scaled to the decoded rate. The cap's
+  arithmetic is part of the bytes: LSDj's is `ceil(float(32576) * (float(rate) / float(11468)))`, all in float with the
+  ratio first, and risa's `ceil(double(32648) * double(rate) / palRate)`, in double with the product first; computing
+  LSDj's in double moves the cap at 464 of the integer rates 1000..96000 (the mutation the test made). What the
+  compilers make of an empty window is settled in TypeScript rather than run: LSDj has no bytes (its assembler still
+  names the slot, with length 0), risa is its one-byte `{0}`. A window that resamples to fewer than 32 frames is LSDj
+  empty bytes too, and one that resamples to 0 is risa `{0}`.
+- **Does `buildKitBank` (and the risa packer) produce native `assemble`'s bytes?** Yes for all 39 baseline cases and
+  for every edge case, with the layout code untouched. They differed only on names outside printable ASCII, because the
+  TypeScript side uppercased with JavaScript's Unicode rule; native's byte rule is right (it is what the baseline and
+  every whole-kit compile so far were made by), so the TypeScript side now matches it. No splice changes, because a
+  name read back from a ROM is printable ASCII by construction.
+- **How long is a 15-slot kit as jobs against today's enkiTS compile, and is the per-job host setup the cost?**
+  Release build, this box (32 threads, so 8 workers), 8 runs each, on stage 4's temporary (uncommitted) bench:
+
+  | measurement | native `compileKit` (RPC) | TypeScript `compileKit` |
+  |---|---|---|
+  | 15 slots x 1 s at 44.1 kHz, three effects | best 1.5-1.6 ms, median 1.7-1.8 ms | best 22.0 ms, median 22.7-23.1 ms |
+  | 15 slots x 8192 frames, no effects | best 0.4-0.5 ms, median 0.5 ms | best 4.1-4.2 ms, median 4.2 ms |
+  | the 1 s kit, slot by slot (serial) | 10.5-11.1 ms | 49-51 ms |
+
+  Per job (serial round-trip means) for the 1 s kit: load 0.29-0.34, effects 0.47-0.49, the normalise pass 0.24-0.26,
+  resample 0.70-0.71, quantise 0.31-0.38 ms. **Per-job host setup is not the cost**: a trivial job is 0.011-0.017 ms
+  (a document-less load of 64 frames) to 0.03 ms (a 64-frame player into a recorder), about 2 ms across a kit's 75
+  jobs. The cost is moving buffers through TypeScript between passes: an echo job (buffer in, played, recorded,
+  returned) takes 0.07 ms at 8192 frames, 0.18 at 44 100 and 1.1 at 176 400, about 0.15 ms per 2 x 176 KB, all on the
+  one JS thread's msgpack and copies; a 1 s slot pushes about 1 MB through the RPC and a kit about 15 MB. The DSP
+  itself is the native milliseconds. The worst case the switch-over has to survive is 15 slots of 4 s at 96 kHz, each
+  capped at 272 700 frames with a filter, a normalise and a dither: native 4.9 ms median (4.8 best), TypeScript 83.2
+  ms (77.9 best), where an early TypeScript-side scan for non-finite samples had made that 467 ms (it moved into
+  native, below). About 80 ms of the UI thread is the cost of a worst-case kit compile if a future row ever runs one.
+  If the buffer traffic ever matters, the lever is keeping intermediates native (a job returning a handle the next job
+  reads, or a chained submission); the host setup needs no work.
+- **Does the plugin's facet set allow a new engine facet, and how do completions reach a UI that only polls?** Yes: the
+  three RPCs sit in the emulator facet, which the plugin, the SDL standalone, the CLI, the test hosts and the Node
+  addon all bind, so no host changed. Completions reach the plugin UI through the existing frame tick, `useJobPump` ->
+  `backend.pumpJobs()`, which drains offline completions too; with the editor closed the workers keep running and
+  completions wait in native's bounded queue (counting against the 64). Nothing in the plugin ever waits natively,
+  which is what the UI test case proves by settling a whole compile from frame pumps.
+
+### The guards, the non-finite flag and the floating-point environment
+
+Three mechanisms, all added by stage 4's reviews, make section 2's promise exact for values that never reach a bank
+whole:
+
+- **`rp.host.guards/1`** (`rp_node.h`) is a host service a node reports into when one of its guards replaces what its
+  kernel would have computed: `fired(ctx, reason)`, called on the block thread, never blocking or allocating, the
+  reason a string literal the reporting instance keeps alive. `rp.dsp.biquad` reports its reset-and-silence and
+  `rp.dsp.dither` its channel restart, exactly where each diverges from the kernel run directly. A host that offers no
+  service answers null and the node skips the call, so the live engine's behaviour is unchanged (and
+  `Engine::graphService` is pinned to answer null for it). Each offline job's host offers the service, and its
+  completion carries `guardsFired` and the first `guardReason`; a job a guard stepped into still completes `ok`, and
+  what the count means is the caller's to decide.
+- **`nonFinite`** is on every buffer an offline completion returns, read by native as it copies the buffer out. A
+  non-finite value no guard touches (a normalising gain over a subnormal peak, a plain gain over samples near
+  `FLT_MAX`) lands in the recording, and this flag is how a caller sees it without scanning the samples (a TypeScript
+  scan put the 15-slot worst case at 467 ms, against 83 ms with native's flag).
+- **The default floating-point environment** is held for a job's whole run, decode to copy-out, and restored after:
+  round to nearest, no trapping, and the flush bits cleared explicitly (x86 MXCSR FTZ and DAZ, AArch64 FPCR FZ and
+  FZ16), because a worker inherits its submitter's mode and audio hosts commonly flush. Without it a job submitted from
+  an audio thread with flush-to-zero on would compile a different bank from the kernels'.
+
+Together they make "every kit that compiles is byte-identical to the kernels" exact rather than a hope: every pass's
+input is finite (a checked window or a checked recording); a gain or a normalise has neither guard nor clamp, so a
+non-finite value it makes is in the recording or reaches a biquad; the biquad's clamp is the one place a pass could
+hide one, and a non-finite input or output leaves its state non-finite, which its guard reports; the resample has
+neither, so what it makes is recorded; and the quantise and DMC encode only ever get checked inputs, the dither
+reporting any restart. So a node departs from its kernel only where a guard reported it, and every other non-finite
+value is flagged. The compiler refuses a slot on either (guard count above 0, or a flagged recording), so a compiled
+bank is the kernels' bytes. D3 inherits the reports rather than a policy: a render sees the count and the flags and
+decides, and nothing fails on them.
+
+### What building it changed in sections 3 to 6
+
+- **Section 3**: a completion reports guards and a per-buffer non-finite flag, a node fault fails a job, and teardown
+  abandons jobs in flight rather than completing them with an error; the pool's 64-job bound, its 8-worker ceiling,
+  its 1024-frame blocks, its handles and its `inFlight` count are the seam's contract; the FP environment is pinned.
+- **Section 4**: normalize's peak is over the channels it plays; a missing buffer is a refusal for both new nodes and
+  a multi-channel buffer a refusal for `rp.dsp.dpcm`; the DPCM's encode is a rebuild key; and `rp.host.guards/1` is the
+  service both guards report into.
+- **Section 5**: the entry points take a runner and return `{bytes, errors}`; a pass a guard stepped into or a
+  non-finite recording refuses the slot; missing spec fields take native's defaults; and every empty or short slot,
+  not only a failed one, carries a reason.
+- **Section 6**: the C++ suite's second watch on the same file ends with the compiler it watched; `kit-edges.json`
+  records the edge banks; the teardown case asserts abandonment; and the suite gained the guard, non-finite and
+  FP-environment cases.
+
+### Found while building
+
+- **The kit baseline is a coarse oracle, and the compiler needed a wider net.** All 39 matched on the first run, and
+  the runner bites (forcing the quantise to `none` fails a case), but the baseline only sees 4-bit codes.
+  `kit-compile`'s edge banks over 31 further slots (dithers on chirps and noise, a five-effect chain with two normalise
+  passes, cut windows), plus the `[dsp-buffer-nodes]` and `[dpcm]` references, are what hold the arithmetic.
+- **A plan replacement does not re-activate a node**, so anything computed only in `activate` silently ignores a
+  settings commit. The dither's seed is now settled in both hooks; a user-built graph can hit the same trap.
+- **A guard report is the clean seam**, not a heuristic: a node need not know whether it runs offline, and a caller
+  gets an exact count instead of a scan. The alternative attempted first (scanning every recording in TypeScript) cost
+  most of a worst-case compile.
+- **`retroplug-graph-test` can hold a node to `rp::lsdj::processEffect`** by compiling `src/lsdj/Effects.cpp` into it
+  (it needs the kernels only), which is how "bit for bit with the kit path" is tested rather than "bit for bit with a
+  kernel the kit path also calls".
+- **The live engine's first periodic state snapshot allocates** (about block 46 of a booting Game Boy, two
+  allocations, with or without offline jobs). It is the 0.5 s state publish
+  [07-remaining-work.md](../spec/07-remaining-work.md) already lists among the allocations left on the block thread,
+  seen here on a Game Boy's `readStateSnapshot` rather than Mesen's serializer. Pre-existing, found by the live-plan
+  case, which now warms past it; C4's "a warmed core's whole block allocates nothing" excludes that publish, so this
+  is not a new C4 gap.
+- **`SampleCache`'s native behaviour differed from the seam's.** The old RPC kept one cache for the engine's life,
+  keyed by path and never invalidated, so a path rewritten in one session compiled the old audio; a job decodes fresh,
+  so a compile sees the file as it is now.
+- **The plugin test had to settle through the plugin's own path.** A case that made an audio driver settled through
+  the native wait, which the plugin never uses; it now settles from the frame tick and proves the wait is off.
+- **enkiTS was building nothing.** `add_subdirectory` still compiled the static library into `all` though nothing
+  linked it; it is out of the build now, and the submodule remains for a later cleanup to judge.
+- **The compiler reached the CLI and render bundles, not the UI's.** `audioDriver` imports `src/kit` and
+  `realBackend.offlineRunner`, so `cli-bundle.js` and the render worker's session path grew; `ui/main.tsx` and the
+  plugin control plane import neither.
+- **Not yet compiled with MSVC.** This box builds Linux, and the prototype's commits are local, so the Windows build
+  has not seen this tree; stage 6 also removed transitive includes (`lsdj/Effects.hpp` -> `BackendTypes.hpp` among
+  them), so "confirm the removed transitive includes build under MSVC" is a residual in
+  [07-remaining-work.md](../spec/07-remaining-work.md).
+
+### The suite, as built
+
+- `retroplug-engine-graph-test` gained `[offline]` (`test/engine-graph/OfflineJobs.test.cpp`): a job returns what the
+  same nodes give run directly, a kit-shaped job naming one buffer twice, two jobs on two workers, the pool's
+  concurrency and bound, a cancel queued and running, a teardown that abandons, every refusal's reason, the RPC's
+  sample bytes, both windows and the cap's arithmetic against copies of each formula, the file input against
+  `SampleCache`'s decode, the live plan compared bit for bit with a job beside it, the guard count and the non-finite
+  flag, the FP environment across a flushing submitter, and a hidden `[offline-bench]`. It went from 268 cases to 282
+  (273 passed and 9 skipped without fixtures; with the fixtures present the whole suite passes).
+- `retroplug-graph-test` gained `[dsp-buffer-nodes]` (`test/graph/DspBufferNodes.test.cpp`, 12 cases: normalize whole
+  and split, its peak, its committed gain, `seedFrom`'s channel 0 and the other channels, a committed seed, the DPCM's
+  bits, its `normalize` rebuild, a block counted from the plan's first block allocating nothing, the refusals, and the
+  Pico plan) and `[dsp][dpcm]` (the kernel against a verbatim copy of the retired codec), the binary going from 259
+  cases to 272: these 13 and stage 1's two `[dsp-nodes]` clamp cases.
+- `retroplug-audio-test` lost `[kit-baseline]` with the compiler (109 cases and 1 235 263 assertions to 108 and
+  1 234 835); `[dither]` and the rest are unchanged. `retroplug-audio-test "[kit-baseline]"` is now a Catch2 failure
+  ("No tests ran"), so a stale script fails loudly.
+- `test-native` went from 160 files to 163: `kit-baseline` (the 39 banks, 3 cases), `kit-compile` (against
+  `kit-edges.json` and the refusals, 9 cases) and `offline-run` (the promise side over the real host, 5 cases).
+  `test:ui` gained `kit-compile` (40 files), `pnpm test` gained `offline` and `kit` (171 files), and `test:n8` and the
+  launchpad suite are unchanged.
+- The goldens: `packages/native/test/audio/golden/kit-baseline.json` untouched and still the one record, beside it the
+  new `kit-edges.json` (12 banks, `{hash, size}`), both read through defines `run-native-tests.mjs` sets. The
+  TypeScript runner carries the baseline's rules: size and slot everywhere, the hash only on `linux-x64-gnu13`, a
+  second compile agreeing with itself elsewhere, `RP_KIT_GOLDEN_REQUIRE_TARGET=1` turning that into a failure, and
+  `RP_KIT_GOLDEN_UPDATE=1` refused off the target and for a case that disagrees with itself.
+- Both sanitizers are clean over the new tags (TSan with 25 jobs beside a live audio thread; ASan with leak detection
+  on), the consumer suites pass from scratch copies (BlipToaster 57 files, NESVJ 19), and the only count that differs
+  between the two sanitizer builds is the existing device-thread progress case, whose progress publishes depend on
+  scheduling.
````

Run: `sed -n '13352,13846p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 2: The specs**

````diff
diff --git a/spec/00-overview.md b/spec/00-overview.md
index ca73557b4..0cbb066fa 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -91,7 +91,7 @@ system routed to one pair by its audio routing.
 |---|---|---|
 | [`packages/native/`](../packages/native) | The C++ host: the RPC services + their facets, `Engine`, `SystemFactory`/backends, the command/snapshot/release seams, the script host the TS nodes (the MIDI routing, the controller session and the protocol nodes) run in, the DPF plugin, and both test hosts. | The sole native tree. |
 | [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane and React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the protocol nodes that run them, and the UI. | The sole control-plane + UI tree. |
-| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled into the `retroplug-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba,Sms}System`, the LSDj kit/sample pipeline, and the transport *primitives* (`SpscRing`, `FrameBufferTriple`, `MemorySnapshotTriple`). The node-graph executor and planner (`src/graph/`) are their own libs, `retroplug-graph-exec` and `retroplug-graph`. ROM classification and the LSDj sav codec are TS-owned. | Shared across the host + CLI + test binaries. |
+| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled into the `retroplug-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba,Sms}System`, the offline seam's sample decode (`SampleCache`), and the transport *primitives* (`SpscRing`, `FrameBufferTriple`, `MemorySnapshotTriple`). The node-graph executor and planner (`src/graph/`) and the DSP kernels (`src/dsp/`) are their own libs, `retroplug-graph-exec` / `retroplug-graph` and the `retroplug-dsp-*` targets, reached by the `rp.dsp` node packages. ROM classification, the LSDj sav codec and the kit compiler are TS-owned. | Shared across the host + CLI + test binaries. |
 
 The LSDj-persistence path is TS-owned. `retroplug-core` is a neutral static lib in the
 native tree; `retroplug-backend` wraps it with the `Engine` (which runs the systems on
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index c6a1e27e9..6d2555492 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -428,14 +428,32 @@ without the frame).
 the heap, pthreads and RTTI, so a build with no resampler omits it). r8brain's static members are a third target,
 `retroplug-r8brain-base`, beside the resampler: its one translation unit is vendored code (`R8bBase.cpp` includes
 r8brain's own `r8bbase.cpp`, on a throwing allocator), so it is kept off RetroPlug's own warning level.
-`retroplug-core` links both for the kit compilers,
-and the graph has them as `rp.dsp` (`rp.dsp.gain`, `.biquad`, `.dither`) and `rp.dsp.resample`, in targets of their own
-(`retroplug-dsp-nodes`, `retroplug-dsp-resample-nodes`) so `retroplug-graph` carries neither and a Pico build links
-`retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone. The kit compilers and the nodes run the same arithmetic, so a
-patched kit and a rendered patch share one DSP. `rp.dsp.resample` takes an `audio-buffer` resource at that buffer's own
-rate and outputs at the plan's; every other node's ports carry the plan's one rate. The kit compilers' output is
-reproducible: the TPDF dither seeds from a hash of the input it sees, held by the kit baseline
-([06-build-test.md](06-build-test.md), [02-native-host.md](02-native-host.md), D1 design sections 4 and 5).
+`retroplug-core` links neither (nothing of the core calls a kernel),
+and the graph has them as `rp.dsp` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `rp.dsp.resample`,
+in targets of their own (`retroplug-dsp-nodes`, `retroplug-dsp-resample-nodes`) so `retroplug-graph` carries neither and
+a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone. A kit compile and a rendered patch run the
+same arithmetic through those nodes, so a patched kit and a rendered patch share one DSP. `rp.dsp.resample` takes an
+`audio-buffer` resource at that buffer's own rate and outputs at the plan's; every other node's ports carry the plan's
+one rate. A kit compile's output is reproducible: the TPDF dither seeds from a hash of the input it sees, held by the
+kit baseline ([06-build-test.md](06-build-test.md), [02-native-host.md](02-native-host.md), D1 design sections 4 and 5,
+D2 design section 11).
+
+### The offline job pool
+
+A graph document also runs to completion off the audio thread. `submitOfflineJob` (the emulator facet) takes a
+document, its inputs (interleaved float samples, or file paths native decodes and windows), the empty buffers it
+records into and the frames to run; a later `drainOfflineJobs` hands back its completion, a bounded queue the control
+plane polls as capability jobs' completions are. An `Engine` owns the pool, made at the first submission, of up to
+clamp(hardware_concurrency, 1, 8) worker threads, with at most 64 jobs outstanding. Each job gets a `GraphHost` and a
+resource table of its own on its worker, which is that host's control thread and block thread at once (the quiescent
+path: no audio thread, 1024-frame blocks), and is destroyed there with the host. The pool has a node registry of its
+own, filled before any worker exists with the built-in C++ packages, so a job's document may name rp.core, rp.dsp and
+rp.dsp.resample and nothing else, and a script pack's load or reload cannot race a job's planning. A completion carries
+the returned buffers (interleaved, each with a non-finite flag), the file inputs' reports, and the count and first
+reason of any node guard that fired (`rp.host.guards/1`, a host service the live engine does not offer). Cancelling
+stops a job at its next block boundary, and destroying the engine abandons jobs in flight. The kit compiler is the
+seam's first user: TypeScript builds each pass's document and packs the bank, and native does every float the bytes
+depend on ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) sections 3 to 5 and 11).
 
 ---
 
@@ -507,6 +525,13 @@ lost mutation.
    Cores are built bare (no default roles baked into C++); feature behaviour lives in TS, the audio
    thread's part in the protocol script nodes.
 
+9. **An offline job shares nothing with the engine.** A job's host, resource table and node instances are made and
+   destroyed on its own worker, which is the two threads that host has; the only state it shares with the engine is the
+   pool's queue and completion deque, under the pool's mutex, the pool's registry, filled before any worker starts and
+   never changed, and r8brain's static, internally locked filter cache (shared by every job's resampler and the live
+   engine's). So a job never touches the live plan, the command ring, the engine's resource table or the audio thread,
+   and a script pack's reload (which swaps the engine's registry) can never reach a job's planning.
+
 ### Not yet built / deferred
 
 - **The registry double-copy** (§4) stands until the shared `SystemBase` can publish directly into
@@ -532,8 +557,9 @@ lost mutation.
 | [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), its one `SystemFactory`, the staged host MIDI, the host outputs and the registry; `buildSystem` / `destroySystem` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
 | [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
-| [`src/dsp/`](../packages/native/src/dsp) | the core-free DSP kernels the kit compilers and the `rp.dsp` nodes share: gain and peak, the RBJ biquads, dither and quantise, and the r8brain resampler |
-| [`DspNodes.cpp`](../packages/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither) and the `rp.dsp.resample` package beside it |
+| [`src/dsp/`](../packages/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
+| [`DspNodes.cpp`](../packages/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither, normalize, dpcm) and the `rp.dsp.resample` package beside it |
+| [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp) | the offline job pool: a document run to completion on a host of its own, its inputs, outputs and completion |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
 | [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 286c2b8c9..dd8f0655b 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -161,16 +161,19 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 | `stageNodeMidi` | `(string nodeId, vector<uint8> bytes) → bool` | the UI's staged message, at the node the running plan places under `nodeId` (`rp.stage/1`): a system node hands it to its core's ingress, the cart node pushes it at its link. False for an empty message; the id resolves at the drain against the plan that runs, and a message no node there takes (an id the plan does not place, a node that stages nothing) is dropped there |
 | `submitJob` | `(RpcJobRequest) → RpcJobSubmitResult` | a capability job for a node of the last published plan: `{id}` when it was taken, `{error}` when it was refused (a capability the node does not present, an operation it does not have, a device's "busy" or "not connected", the engine's 64-outstanding bound, an emulated restore). A read or a ROM header completes before it returns; a write at the next block boundary, or at once while no audio thread runs (the CLI between renders); a device's job on its own thread, after a cart submission has waited on the calling thread (the DAW's main thread in the plugin) up to the probe's 1 s bound for the cart's menu check, usually one USB round trip |
 | `drainJobs` | `→ vector<RpcJobCompletion>` | what finished since the last drain (`done`, `ok`, the result's bytes or the error) and every running job whose progress changed, with `progress` of `total` |
+| `submitOfflineJob` | `(RpcOfflineRequest) → RpcOfflineSubmitResult` | a graph document run to completion on a host of the engine's offline pool (`Engine::submitOfflineJob`): `{id}`, or `{error}` for a rate a `GraphHost` refuses, more than 64 inputs and outputs, an input that is neither samples nor a file, an output of no frames, a return naming no handle, or 64 jobs outstanding. A document may name only the built-in C++ packages. What fails once it runs (a file that will not decode, a planner refusal, an activate error, a node fault, a cancel) is the completion's error |
+| `drainOfflineJobs` | `(double waitMs) → RpcOfflineDrain` | the completions finished since the last drain, oldest first (each buffer's samples, rate, channels and `nonFinite`, the file inputs' reports, `guardsFired` / `guardReason`), plus `inFlight`, the jobs still queued or running. With `waitMs` it blocks up to that (at most 1000 ms) when nothing has finished, and returns at once when no job is queued or running |
+| `cancelOfflineJob` | `(uint32 id) → bool` | stop job `id`: a queued one completes now, unrun, a running one at its next block boundary (or as it finishes), both with the reason `cancelled`. False for an id neither queued nor running |
 | `drainMidiOut` | `→ vector<RpcMidiOut>` | the host MIDI out (`Engine::midiOut`, which `rp.host.midi-out` fills each block) gathered over the `renderAudio*` renders since the last drain, each message with its system and frame (`renderBlocks` returns its own) |
 | `droppedMessages` | `→ uint32` | messages dropped so far (`Engine::droppedMessages`) by the plan's full output queues; never reset, and it wraps, so a test compares two reads |
 
 The table is a selection. The facets
 ([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 19
-`host` methods, 15 `emulator` (the resources, the graph and script packs, the snapshot reads and writes, the
-capability jobs and the node staging), 22 `harness` (the renders, transport and sample rate, staged MIDI in and
-MIDI out, `droppedMessages`, `buildTarget`, the kit compiles and the audio runtimes' profiling calls),
+`host` methods, 18 `emulator` (the resources, the graph and script packs, the snapshot reads and writes, the
+capability jobs, the offline jobs and the node staging), 20 `harness` (the renders, transport and sample rate, staged
+MIDI in and MIDI out, `droppedMessages`, `buildTarget`, and the audio runtimes' profiling calls),
 27 `debug` and
-6 `driver`: 89 in the union. There is no call for what the roles do on the audio thread: their protocol
+6 `driver`: 90 in the union. There is no call for what the roles do on the audio thread: their protocol
 nodes are script node types every Engine embeds, and each node's settings ride the documents `setGraph`
 plans. Nor is there one for a system's knobs: they are its system node's settings, in the same documents,
 and its params, whose live edits are `setParam`
@@ -1086,8 +1089,7 @@ its **runtime** classes:
 | [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn`/`reserveAudio`. Its surface is what the engine and the nodes call: no battery-path accessors, no `fastBoot()` getter, no `clearSram`, no sub-block MIDI out, and no memory-snapshot arming (C4). The capabilities beside `debugTarget()` are `channelTap()` and, since C5, the nullable `memory()` / `sram()` / `rom()` accessors a system node presents to the capability job model (`system/SystemCapabilities.hpp`), and the per-block contract is `reserveAudio` plus `kDefaultBlockFrames`; its ingress queues report `inputAccepted()` / `inputDropped()` |
 | [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
 | [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the one core with a [`ChannelTap`](../packages/native/src/system/ChannelTap.hpp) today (it implements the interface itself, `channelTap()` returning `this`), so it is the `SyncTap` target. The engine's tap and build check name no console: it forward-declares `ChannelTap` and includes no core header, and with C5's cart node it has no console check at all |
-| the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
-| the DSP library (`src/dsp/`: gain and peak, the biquads, dither and quantise, the r8brain resampler) | the kit path's effect and resample arithmetic as block-streaming kernels, and the same kernels the `rp.dsp` nodes wrap |
+| `SampleCache`, `util/MinizZip`, `EmbeddedRoms` | the offline seam's file decode (miniaudio, mono float at the file's own rate, the decode a kit compile reads its sources through), `zip`/`unzip`, and embedded-ROM resolution. `lsdj/Effects` is the kit path's reference arithmetic now, compiled into the two test binaries that hold the DSP nodes to it, not into the core |
 | `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
 
 The graph the engine runs on is not in `retroplug-core`: the executor, planner and
@@ -1103,22 +1105,27 @@ the C++ tree.
 ### The DSP targets and the `rp.dsp` packages
 
 The kit path's DSP and the graph's DSP nodes are the same kernels, in `src/dsp/`, split across five targets:
-`retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise; the C++ standard library alone),
-`retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and RTTI), `retroplug-r8brain-base`
-(r8brain's static members, one vendored translation unit compiled on an allocator that throws, with no target declared
-for the vendored library; kept off RetroPlug's own warning level), and the two node packages
-`retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`) and `retroplug-dsp-resample-nodes` (`rp.dsp.resample`), in
-[cmake/graph.cmake](../packages/native/cmake/graph.cmake). `retroplug-core` links both kernel libraries for
-`KitCompiler`, `KitUtil` and `RisaDmcCodec`; `retroplug-graph` links none of them; `retroplug-backend` links both
-packages and `Engine.cpp` registers them beside the other static packages. The split is load-bearing: `rp.dsp` never
+`retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode; the C++ standard
+library alone), `retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and RTTI),
+`retroplug-r8brain-base` (r8brain's static members, one vendored translation unit compiled on an allocator that throws,
+with no target declared for the vendored library; kept off RetroPlug's own warning level), and the two node packages
+`retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `retroplug-dsp-resample-nodes`
+(`rp.dsp.resample`), in [cmake/graph.cmake](../packages/native/cmake/graph.cmake). `retroplug-core` links neither
+(nothing of the core calls a kernel), and `retroplug-graph` links neither; `retroplug-backend` links both packages
+and `Engine.cpp` registers them beside the other static packages, the offline job pool registering them in its own
+registry too. The split is load-bearing: `rp.dsp` never
 reaches r8brain, so a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone, leaves
 `dspResamplePackage()` out of its registration, and has neither the resampler nor r8brain in its binary, where the
-planner's `heap` refusal is only the second line. `rp.dsp.resample` reads an `audio-buffer` resource named by its
+planner's `heap` refusal is only the second line (`rp.dsp.dpcm` declares `heap` and stays in the package, so the split
+is r8brain's link-time cost, not allocation). `rp.dsp.resample` reads an `audio-buffer` resource named by its
 settings at that buffer's rate and outputs at the plan's; a recorder and a resampler on one buffer in one plan are
 unsupported (the planner refuses a resource bound to a port reader when the same resource has a port writer, and a
-settings ref is not a binding it sees), so a driver fills a buffer before the plan that plays it. A rate change
+settings ref is not a binding it sees), so a driver fills a buffer before the plan that plays it. `rp.dsp.normalize`
+and `rp.dsp.dpcm` read a buffer the same way, and the dither's `seedFrom` names one too, so a kit-shaped document can
+name one buffer twice (the player's port and the settings ref). A rate change
 resumes where the output had reached, to within one frame, so a fresh r8brain run-in is heard from there rather than a
-gap.
+gap. `rp.dsp.normalize` measures its peak over the channels it plays (the loudest of each played channel's peak), and
+`rp.dsp.dpcm` refuses a multi-channel buffer rather than mixing one.
 
 Two of the nodes guard their state, because nothing between a plan and the host output rejects a non-finite sample:
 `rp.dsp.biquad` resets a channel's filter and writes that block as silence when the filter comes out of it non-finite
@@ -1127,27 +1134,64 @@ straight through), and `rp.dsp.dither` restarts a channel whose state came out n
 would latch its error terms for the rest of the stream). The guards are on state, not on a document's values: a node
 hands its kernel the values it reads, because a clamp would be a different kit byte (the planner and `setParam` clamp
 a document's value into a param's range with no diagnostic, and a wired value is not clamped at all). The param ranges
-are also narrower than a kit spec's (gain 0..4, frequency 20 Hz to 20 kHz, Q 0.1 to 10, -24..24 dB), so a kit's values
-cannot reach the kernels through them unclamped; D2 carries that work. Both guards differ from the kernel run directly
-in a way a kit could see: a kit filter above its source rate's Nyquist compiles to constant codes today, where the node
-recovers, and a decoded 32-bit float WAV can carry a NaN or an infinity (nothing between `SampleCache` and
-`KitCompiler` checks `isfinite`), which latches either kernel where its node restarts, so D2 has to decide for both.
-Until the biquad's guard fires its kernel writes rail-to-rail output, so a held above-Nyquist setting is full-scale
-bursts between the guard's silent blocks. A dither node's channels each draw their own noise, channel `c` from the
-setting's seed plus `c` times a golden-ratio step, so a stereo patch's dither is decorrelated and channel 0 is the
-setting's own seed (a mono patch is unchanged); the kit path calls the kernel directly, so none of this moves a kit
-byte.
+cover any sane kit value (gain -100..100, frequency 1 Hz to 96 kHz, Q 0.001 to 1000, -96..96 dB), so a kit's values
+reach the kernels through them whole; the planner cannot see the plan's rate, so a kit compile refuses a frequency at
+or above its source rate's Nyquist itself. Each guard reports into `rp.host.guards/1` when the host offers it: the
+offline job pool does, and counts the reports into its job's completion. Both guards differ from the kernel run
+directly in a way a kit could see, so the compiler refuses the regimes rather than choosing between them: a filter at
+or above its source's Nyquist, a source with a non-finite sample, a pass that records a non-finite sample and a pass a
+guard stepped into each refuse that slot ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md)
+section 11). A dither node's channels each draw their own noise, channel `c` from the setting's seed plus `c` times a
+golden-ratio step, so a stereo patch's dither is decorrelated and channel 0 is the setting's own seed (a mono patch is
+unchanged), unless `seedFrom` names a buffer, whose first channel's hash seeds the stream instead; a kit compile is
+byte-identical because it refuses rather than recovers. No kit byte moves either way: the nodes are held to those
+kernels by their own tests.
 
 ### Kit reproducibility
 
-A kit compiles to the same bytes every time. The two TPDF dither algorithms seed their `std::mt19937` from
-`rp::dsp::inputSeed` over the resampled, pre-trim sample the dither receives, salted with a per-algorithm constant, and
-the other three algorithms are deterministic already. The kit baseline suite
+A kit compiles to the same bytes every time. The compiler is TypeScript (`packages/retroplug/src/kit/`): it builds
+each pass of a slot's chain as a graph document and packs the bank, and native does every float the bytes depend on,
+through the `rp.dsp` nodes in offline jobs (the seam below). The two TPDF dither algorithms seed their `std::mt19937`
+from `rp::dsp::inputSeed` over the resampled, pre-trim sample the dither receives, salted with a per-algorithm constant,
+and the other three algorithms are deterministic already. The kit baseline
 ([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts)) compiles a fixed matrix of synthesised
-sources through `KitCompiler` and the two codecs and holds each bank's FNV-1a-64, size and slot length to
+sources through that compiler and holds each bank's FNV-1a-64, size and slot length to
 [golden/kit-baseline.json](../packages/native/test/audio/golden/kit-baseline.json): exact bytes on `linux-x64-gnu13`,
 self-agreement and the layout facts elsewhere, with `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on CI's linux-x64 job and
-`RP_KIT_GOLDEN_UPDATE=1` re-recording on that target only. [06-build-test.md](06-build-test.md) has the suite's rules.
+`RP_KIT_GOLDEN_UPDATE=1` re-recording on that target only. A second golden,
+[kit-edges.json](../packages/native/test/audio/golden/kit-edges.json), holds the edge banks (`kit-compile.test.ts`)
+under the same rules. [06-build-test.md](06-build-test.md) has both suites' rules. A pass that records a non-finite
+sample, or one a node's guard stepped into, refuses its slot, which is how a compiled bank stays the kernels' bytes
+when a finite source overflows somewhere in the chain.
+
+### The offline job pool
+
+[`OfflineJobs`](../packages/native/src/host/engine/OfflineJobs.hpp) is the `Engine`'s, made at the first submission and
+reset before the engine's other members go. It runs every job on one worker thread from start to end, which is the job
+host's control thread and block thread at once (quiescent: no audio thread, so `setGraph` flushes inline and
+`GraphHost::processBlock` is the block driver, draining then running), in blocks of 1024 frames, the last partial,
+checking for a cancel between blocks. It starts workers on demand while every started one is busy, at most
+clamp(hardware_concurrency, 1, 8), and never retires one: an engine that ran a kit keeps up to eight parked threads
+for the rest of its life (a parked wait, costing no CPU). At most 64 jobs are outstanding at once (queued, running, or
+finished and not yet drained), so every taken job has room for its completion. Handles are known before a job runs:
+input i is `res:(i + 1)`, output j is `res:(inputs + j + 1)`. An input of no frames is accepted and reported but names
+no resource, and an output of no frames is refused. A file input is decoded through a `SampleCache` of the job's own
+(so a path rewritten between two jobs decodes as it is now) and windowed by
+[`offlineWindow`](../packages/native/src/host/engine/OfflineJobs.cpp), which reproduces the two kit windows
+exactly: an offset clamped to the end, a zero length meaning "to the end", and a frame cap scaled to the decoded rate
+in the window's own arithmetic (`float-ratio` for LSDj, `double-scale` for risa).
+
+A job's node registry is the pool's own, filled in its constructor with rp.core, rp.dsp and rp.dsp.resample before any
+worker exists and never changed, so the workers share it read-only; a script, system or host I/O node in a document is
+`unknown-type`, a node fault fails the job, and a guard report does not: the completion carries `guardsFired` and the
+first `guardReason`, each buffer carries `nonFinite`, and a caller decides (a kit compile refuses the pass). The whole
+run is held in the default floating-point environment, so a worker that inherited its submitter's flush-to-zero mode
+still computes what the kernels do. Cancelling completes a queued job unrun and stops a running one at its next block
+boundary; tearing the engine down abandons every job in flight, joins the workers and discards undrained completions,
+so no resampler outlives the engine (D1's r8brain teardown rule). The TypeScript side
+([`src/offline.ts`](../packages/retroplug/src/offline.ts)) turns submit-and-drain into a promise: the queue joins the
+capability jobs' live set so one pump settles both, the native wait is turned on by `createAudioDriver` for hosts with
+no UI tick, and runs past the 64-job bound wait in a FIFO rather than being refused busy.
 
 The txiki host `TjsHostRuntime` comes from
 [`deps/dpf.js`](../deps/dpf.js), not the shared core.
@@ -1190,7 +1234,7 @@ is one automatable parameter applied post-render across all 8 channels
 
 - [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
 - [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
-- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, the command handler, `SyncTap`.
+- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp), [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs, the registry and the offline job pool; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, `submitOfflineJob`, `drainOfflineJobs`, `cancelOfflineJob`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
 - [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes, with their settings and params), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host MIDI I/O nodes), [`N8Nodes.cpp`](../packages/native/src/host/graph/N8Nodes.cpp) (the cart's node), [`LaunchpadNodes.cpp`](../packages/native/src/host/graph/LaunchpadNodes.cpp) (the control surface's two), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
 - [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 6a0d915c8..30fa5e3be 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -20,7 +20,9 @@ it:
   `projectStore.ts`, `recentStore.ts`, `userConfigStore.ts`, `bindingsStore.ts`, plus the
   store-adjacent services `fileSelection.ts`, `fileWatcher.ts`, `sramAutoSave.ts`.
 - **Runtime / host seams** — `backend.ts` / `realBackend.ts` (control-plane RPC),
-  `audioDriver.ts` (the harness's render and profiling client), the composition roots
+  `audioDriver.ts` (the harness's render and profiling client, and the kit compiler's caller),
+  `capability.ts` / `offline.ts` (the two promise-based job seams: a node's capability job, and a graph
+  document run to completion on an engine-owned offline pool), the composition roots
   `pluginControlPlane.ts` / `appStores.ts` / `appHost.ts`, and the React/LVGL `ui/` tree.
 
 Roles and the protocol nodes that run them are their own subject: **doc 04** covers them end to end;
@@ -37,8 +39,8 @@ contract the application calls. It is deliberately narrow and deliberately grown
 thing can be done in pure TS, it belongs in the application layer, not here.* It is
 **synchronous** on purpose — in the real plugin these are in-process calls over the
 txiki↔C++ bridge — which keeps application logic straight-line and trivially testable
-against an in-memory mock. The **one** async method is `openFileBrowser`, which waits on
-human input.
+against an in-memory mock. The exceptions are `openFileBrowser` (it waits on human input) and the job seams'
+promises (`capability`, `offlineRun`), which settle from a pump or a native wait rather than a timer.
 
 The methods, grouped by concern:
 
@@ -47,7 +49,8 @@ The methods, grouped by concern:
 | **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
 | **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
 | **Emulator lifecycle** | `putResource(spec)` · `releaseResource(id)` · `setGraph(request)` · `loadScriptPack(request)` | Put an immutable byte block in the engine's `ResourceTable` (a ROM, a battery, a savestate) or drop the owner's hold on one; plan a graph document TS built (which systems run, in what order and wired how, each system node naming the resources its core boots from). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over these and the snapshot reads. Load a pack of script node types beside the built-in ones, or reload one, while audio runs ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): `{ ok, error, errors, types }`, the pack's types with their descriptors, or why not (the planner's `errors` when a reload's replan refused). Nothing in the app calls it yet, and the mock refuses it |
-| **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path (the one async method) |
+| **Job seams** | `capability(nodeId,op,args)` · `pumpJobs()` · `rejectPendingJobs(reason)` · `offlineRun(request)` | Promise-based jobs over the RPC's capability and offline job calls: a node's capability (C5) and a graph document run to completion on an engine-owned pool (D2), both settled by the one `pumpJobs` the UI's frame tick calls. `offlineRun`'s document may name only the built-in C++ packages (no script, system or host I/O node), and its promise settles from the tick or, in a host with none (the CLI, the Node addon, the test host), the native wait `createAudioDriver` turns on ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) sections 3 and 11) |
+| **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path |
 | **Live params** | `setParam(nodeId,paramId,value)` | Set a param of a node the last published plan places, at the start of the next block, clamped to its range: the store's live gain and a NES's expansion volume, on the system's node `sys-<id>`. A system's other knobs are its node's settings in the document `setGraph` plans, and the audio routing is the graph's wiring, so neither is a call ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)) |
 | **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
 | **Live reads** | `readState(id)` · `readSram(id)` · `getFrame(id)` | Pull the latest tear-free snapshot from the `SnapshotRegistry` (see doc 01) — never touches a live core |
@@ -693,7 +696,8 @@ and how `buildGraph` places and wires them.
   [`menuDefs.ts`](../packages/retroplug/ui/screens/menu/menuDefs.ts). The keyboard/gamepad
   bindings editor, Open Settings Folder, and the LSDj Mode submenu are all built.
 - The `ui?` field on a `RoleType` (a role's own settings render descriptor) is present in the
-  model but not yet consumed — it is the seam the deferred kit-patch UI will use.
+  model but not yet consumed: it is the seam a role-owned settings UI will use (kit patching does not need it;
+  `lsdj-assets` splices at construct).
 
 The store graph is now **unified**: the plugin's control-plane bundle composes the one
 `composeAppStores` graph and publishes it on `globalThis[Symbol.for("plugin")]`, and the editor's
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index a1e8d3f55..a2c7ce17c 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -466,10 +466,10 @@ inventory of remaining work.
   decoded to host MIDI) and **MasterSync**. Raw **Keyboard** (mode 4) still sends nothing: it needs a
   per-block `keys` feed, an input on `rp.lsdj.sync` that nothing in the running host would feed yet, and
   per-block host `buttons` the same.
-- **kit-patch UI-thread behaviour.** LSDj kit-patch runs entirely on the UI / control thread: it
-  compiles the kit (over the native `KitCompiler` primitives) and the `lsdj-assets` role folds it into the
-  ROM in memory. It has **no protocol node** and never reaches the audio thread. A role-owned settings UI
-  would need the deferred `ui?` render seam.
+- **kit-patch UI-thread behaviour.** LSDj kit-patch is orchestrated on the UI / control thread: the `lsdj-assets` role
+  splices a pre-built kit file into the ROM in memory (its construct hook), and it compiles nothing. It has **no
+  protocol node** and never reaches the audio thread. A plugin kit row could compile through `src/kit` on the stores'
+  backend (design section 9), but no row does it; a role-owned settings UI would need the deferred `ui?` render seam.
 - **`ui?` render descriptor + extension model.** The `ui?` field on `RoleType` is a labelled-deferred
   seam (a role's own settings UI), and the third-party extension model (registering `RoleType`s, ROM
   providers, and node types from outside the built-ins) is future work.
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index b51a499e9..fbfe9cbad 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -10,9 +10,9 @@ command ring, snapshot registry, release ring), see [01-architecture.md](01-arch
 The root [CMakeLists.txt](../CMakeLists.txt) declares the project as
 `NAME retroplug` and pulls in the native build with a single
 `add_subdirectory(packages/native native)` ([CMakeLists.txt](../CMakeLists.txt)), after the
-`deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, enkiTS, efsw, catch2); r8brain
-supplies headers only, so it is not added as a subdirectory.
-A full build produces the plugin (all formats) + the standalone.
+`deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, efsw, catch2); r8brain
+supplies headers only, so it is not added as a subdirectory, and `deps/enkiTS` is a submodule that nothing adds or
+links since the native kit compile retired. A full build produces the plugin (all formats) + the standalone.
 
 - `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
   **zero build-specific special-casing** — a bare `./build.sh` builds every `ALL` target. See
@@ -32,11 +32,11 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 
 | Target | Kind | Produces | In `ALL`? |
 |---|---|---|---|
-| `retroplug-core` | static lib | The shared emulator / kit-codec core. | yes (via consumers) |
-| `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the kit compilers and the `rp.dsp` nodes share (gain and peak, the eight RBJ biquads, dither and quantise); the C++ standard library alone. | yes (via `retroplug-core` and the node packages) |
-| `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via `retroplug-core` and the resample node package) |
+| `retroplug-core` | static lib | The shared emulator core: the system backends, the transport primitives, `SampleCache` (the offline seam's file decode) and the RPC-free utility the hosts need. `src/lsdj/` is test-reference code only now (the kit path's effect arithmetic, compiled into the two test binaries that hold the nodes to it). | yes (via consumers) |
+| `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
+| `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
 | `retroplug-r8brain-base` | static lib | r8brain's static members, one vendored translation unit (`R8bBase.cpp` includes `r8bbase.cpp`) compiled on a throwing allocator and linked into `retroplug-dsp-resample`. Kept off RetroPlug's `/W4 /WX` (vendored code keeps the compiler's default level) and given the deps' `-w` on GNU/Clang. | yes (via `retroplug-dsp-resample`) |
-| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/native/cmake/graph.cmake), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
+| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/native/cmake/graph.cmake), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
 | `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
 | `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
 | `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
@@ -217,43 +217,55 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`
   (`RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=<resources> pnpm test:native parity`).
 
-### The kit baseline (`[kit-baseline]`)
+### The kit baselines (`kit-baseline`, `kit-compile`)
 
-Exact kit bytes, not structure and audibility. `KitCompiler`'s output is what LSDj and risa play, and until D1 no test
-compared a compiled bank with anything; the suite is the gate Phase D's graph compiles are held to
-([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)).
+Exact kit bytes, not structure and audibility. A compiled bank is what LSDj and risa play, and these two goldens are
+the gate: D1 recorded 39 banks from the compiler native then had, D2's TypeScript compiler had to reproduce them
+through real offline jobs before any caller moved, and they are still the oracle now that native's compiler is gone
+([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
+[modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11).
 
+- **The compiler.** [`src/kit/`](../packages/retroplug/src/kit) compiles a spec as chains of offline graph jobs (D2):
+  each pass of a slot is a graph document the `rp.dsp` nodes run, native does every float the bytes depend on, and
+  TypeScript cuts the buffers, packs the codes and bits and assembles the bank.
 - **Matrix.** The kit baseline
   ([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts), run by `pnpm test:native`)
   synthesises every source in the test (two sines, a quiet sine, a chirp,
   seeded noise and silence at 22.05, 44.1 and 48 kHz, plus a 600-frame source shorter than one resampler chunk), writes
-  each as a 32-bit float WAV in a temp dir, and compiles it through `KitCompiler` over `LsdjKitCodec` / `RisaDmcCodec`,
-  so `SampleCache` and miniaudio's decode are in the loop and the suite needs no fixtures. 39 cases: LSDj has no
+  each as a 32-bit float WAV in a temp dir, and compiles it through real offline jobs, so `SampleCache` and miniaudio's
+  decode are in the loop and the suite needs no fixtures. 39 cases: LSDj has no
   effect, gain, normalise (a silent source too), a quiet source normalised at every rate and through a dither, a
-  negative gain, each of the eight filters, a 10 Hz high-pass at Q 20 and a 30 dB shelf (both outside the biquad
-  node's ranges), each of the five dithers, rotate on and off, a filter-gain-dither chain, the short source and a
-  two-slot bank; risa has five DMC rate indices, normalise off, loop on, a silent normalise at two rates, the short
-  source and a two-slot bank. The two-slot banks add a second sample
+  negative gain, each of the eight filters, a 10 Hz high-pass at Q 20 and a 30 dB shelf, each of the five dithers,
+  rotate on and off, a filter-gain-dither chain, the short source and a two-slot bank; risa has five DMC rate indices,
+  normalise off, loop on, a silent normalise at two rates, the short source and a two-slot bank. The two-slot banks
+  add a second sample
   named SNR with its own effects (on risa, a rate index of its own too) and, on the LSDj one, a lowercase over-long
   bank name, so the assembler's offset accumulation past slot 0, a second sample name and `writeString`'s uppercase
-  and truncation are held by a hash too (the 15-slot loop is in every case already).
-- **Golden.** `golden/kit-baseline.json` holds `target` and `{hash, size, slot}` per case, sorted, read with
-  reflect-cpp. `hash` is FNV-1a-64 of the bank; `size` and `slot` are the layout facts. The suite asserts each case
-  compiles clean, the bank carries every slot it compiles and the golden names exactly the matrix's cases, so a broken
-  case cannot pin a stable empty bank.
+  and truncation are held by a hash too (the 15-slot loop is in every case already). `SOURCE_HASHES` binds the
+  synthesised sources to the ones the retired C++ suite wrote, on the target where their `sin` values were recorded.
+- **The edge banks.** [kit-compile.test.ts](../packages/retroplug/test-native/kit-compile.test.ts) holds a second
+  golden, [kit-edges.json](../packages/native/test/audio/golden/kit-edges.json), 12 banks past the baseline's matrix:
+  a full 15-slot and a 16-slot kit, samples past the slot count, clipped and empty slots, filled defaults, rotate on,
+  and a filter-normalise-dither chain, each beside the refusal cases. (The byte rule for names outside ASCII is not a
+  golden: `test/kit/kit.test.ts`'s case pins it.)
+- **What a case records.** The baseline records `{hash, size, slot}`: the bank's FNV-1a-64 in lowercase hex, its size,
+  and slot 0's length. The edge golden records `{hash, size}` only, because `kit-compile` refuses slots by design. Each
+  suite asserts that every case's bank compiles (a refused slot included), that the bank carries every slot it
+  compiles, and that the case names equal the golden's keys exactly, so a broken case cannot pin a stable empty bank.
 - **Targets.** As parity: exact bytes only on `linux-x64-gnu13` (float arithmetic is bit-stable only per compiler and
   architecture); elsewhere a case compiles twice and must agree with itself, and `size` / `slot` are still compared.
-  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead, and CI's linux-x64 job sets it. The target is
-  `rp::buildTarget()`, the same function the RPC reports, so the two baselines name one toolchain.
-- **Re-record** with `RP_KIT_GOLDEN_UPDATE=1`. It refuses on any other host, and refuses a case whose two compiles
-  disagree (that case depends on something outside its input). The file is written through a sibling temp file and
-  renamed into place, so a failed write leaves the committed golden as it was. A re-record on a stable tree is a
-  byte-identical file, so a diff shows only moved banks. Moving the target is one change: `kBaselineTarget` in the
-  test, a re-record on the new host, the golden's `target` line, and CI's `RP_KIT_GOLDEN_REQUIRE_TARGET` step on that
-  host.
-- **A trap.** The baseline is coarse: a one-ulp change in a dither's arithmetic can move no 4-bit code across every
-  case (stage 3's Sierra Lite mutation), which is why the kernels have `[dsp]`'s verbatim references at 16 bits.
-  Treat a kit baseline failure as a real change to the bytes, and do not re-record it to make a refactor pass.
+  `RP_KIT_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead, and CI's linux-x64 job sets it on its
+  `test:native` step. Off that target (CI's arm64, macOS and Windows jobs) the banks are therefore held only by each
+  case's self-agreement and the structural asserts; the native comparison that used to make every edge bank exact on
+  every host went with the compiler.
+- **Re-record** with `RP_KIT_GOLDEN_UPDATE=1` on either runner (`pnpm test:native kit-baseline`, `... kit-compile`).
+  It refuses on any other host and refuses a case whose two compiles disagree (that case depends on something outside
+  its input), and the file is written through a sibling temp file and renamed into place, so a failed write leaves the
+  committed golden as it was. Without the variable the files are only ever read.
+- **A trap.** The baseline is a 4-bit view: a one-ulp change in a dither's arithmetic can move no code across every
+  case (D1's Sierra Lite mutation), and `kit-edges` is the same view. That is why the kernels have `[dsp]`'s verbatim
+  references at 16 bits and the buffered nodes `[dsp-buffer-nodes]`' own. Treat a kit golden failure as a real change
+  to the bytes, and do not re-record it to make a refactor pass.
 
 | Tier | Backend under test | Command | Runner | Test dir |
 |---|---|---|---|---|
@@ -309,7 +321,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
 | `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 protocol and serial checks, `[cart-jobs]` and `[cart-release]` among them since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[kit-baseline]` and `[dither]` hold every compiled bank and the TPDF seed (see [the kit baseline](#the-kit-baseline-kit-baseline)). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
@@ -373,7 +385,8 @@ Pick the cheapest tier that actually exercises the code you touched. Trust-but-v
 |---|---|---|
 | **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug/src` that doesn't need a live core. |
 | **The engine's output, bit for bit**: routing, block handling, MIDI staging, sync, anything Phase B moves | `pnpm test:native parity` | The parity baseline: one changed bit in any lane, MIDI byte or RAM byte fails, which RMS-based tests never see. |
-| **A kit compile or the DSP kernels/nodes**: the kit chain's effects and resample, anything D2's offline graphs will reuse | `pnpm test:plugin` (`[kit-baseline]`, `[dsp]`, `[dsp-nodes]`, `[dsp-resample]`) | The kit baseline holds every compiled bank to its recorded bytes (39 cases, a two-slot bank for each codec among them, and the values the `rp.dsp` nodes' ranges would clamp silently: a normalised gain past 4, a negative gain, a 10 Hz high-pass at Q 20, a 30 dB shelf); the `[dsp]` cases hold the kernels to their padded references at 16 bits where a 4-bit code cannot see a one-ulp move, and the nodes' cases hold the state guards and the dither's per-channel noise. |
+| **A kit compile**: `src/kit`'s chains, the packing, the bank, and the offline seam they run on | `pnpm test:native kit` (`kit-baseline`, `kit-compile`, `kit-dither`) and `pnpm test:native offline-run`; `pnpm test:ui kit-compile` for the plugin's facets | The 39 baseline banks and the 12 edge banks, exact bytes on `linux-x64-gnu13` and each case's self-agreement plus the structural asserts elsewhere ([the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `offline-run` is the promise side over the real host, and the UI case settles a whole compile from frame pumps on the plugin's own registration. |
+| **The DSP kernels/nodes**: the chain's effects and resample, the buffers the kit passes read | `pnpm test:plugin` (`[dsp]`, `[dsp-nodes]`, `[dsp-resample]`, `[dsp-buffer-nodes]`, and `[offline]` for the pool) | The `[dsp]` cases hold the kernels to their padded references at 16 bits where a 4-bit code cannot see a one-ulp move; the nodes' cases hold each node to its kernel, the state guards, the dither's per-channel noise and the buffer-reading nodes' peaks and seeds; `[offline]` holds the job pool. |
 | **DSP behaviour / cores / the command-ring & snapshot seam**: the protocol nodes and their wires, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem` on the real engine graph, the protocol nodes running in the script host every Engine embeds. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
 | **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
 | **Anything you want to eyeball** in the standalone | `pnpm screenshot` → Read `/tmp/retroplug.png` | Runs the JACK standalone under Xvfb + dummy jackd and dumps the LVGL screen. Drive input mid-run with `tools/standalone-key.sh` using `RETROPLUG_WINDOW_NAME="RetroPlug"`. |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 809b415e4..f036689da 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -352,12 +352,13 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   `lsdj/Effects`, `KitUtil` and `RisaDmcCodec` run those kernels. The graph has them as `rp.dsp.gain`, `.biquad`,
   `.dither` and `rp.dsp.resample` (`retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes`, the resampler a package and
   target of its own so a Pico build can leave it and r8brain out). TPDF dither is seeded from a hash of the sample it
-  sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]` holds 39 compiled banks to
-  a golden. Strict parity held bit for bit at every commit, and the baseline did not move by a hash through the kernel
-  move. What it leaves:
+  sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]` held 39 compiled banks to
+  a golden (the test-native runners hold them now; the next bullet). Strict parity held bit for bit at every commit,
+  and the baseline did not move by a hash through the kernel move. What it leaves:
   - **D2**: audio buffers in and out of a document, a run-to-completion driver over `GraphHost`, normalise in a graph,
     the two-rate kit chain, and kit compiles as graphs behind `compileKit` / `compileDmc`, byte-identical to the
-    baseline. D1's build fixes the seed rule (hash the whole pre-trim buffer and pass `rp::dsp::inputSeed` in the
+    baseline (as built, the TypeScript entry points of those names; the harness RPCs went with native's compiler, the
+    next bullet). D1's build fixes the seed rule (hash the whole pre-trim buffer and pass `rp::dsp::inputSeed` in the
     node's `seed`), normalise (call `normalizedGain`, a reciprocal multiply), and the order (effects at the source
     rate, resample, quantise, `frameAlign`, nibble pack).
   - **D2 has to widen the nodes or wire around them.** The nodes' param ranges are narrower than a kit spec can be
@@ -378,13 +379,44 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     codes today (the biquad's state goes NaN and the quantiser reads every NaN as code 0), where `rp.dsp.biquad`
     recovers, so D2 has to decide between the kernel call and the node for such a kit. Both guards also differ from the
     kernel on any non-finite sample a decoded 32-bit float WAV can carry (nothing between `SampleCache` and
-    `KitCompiler` checks `isfinite`): a NaN or an infinity latches a kernel where its node restarts, so D2 decides for
-    both rules. Before the biquad's guard fires its kernel writes rail-to-rail output, so a held above-Nyquist setting
+    `KitCompiler` checked `isfinite`): a NaN or an infinity latches a kernel where its node restarts, so D2 decides
+    for both rules (as built, its compiler refuses such a source and any pass a guard stepped into; the next bullet).
+    Before the biquad's guard fires its kernel writes rail-to-rail output, so a held above-Nyquist setting
     is full-scale bursts between the guard's silent blocks, not silence. A `rp::dsp::Resampler` destroyed after
     `R8bBase.cpp`'s statics is a use-after-free in r8brain's `unref` (it locks a static mutex and writes into
     cache-owned objects), so D2's driver and D3's renders keep every resampler inside an `Engine`'s life, as D1's hosts
-    do by tearing every Engine down before exit. The plugin still cannot compile a kit (the RPCs remain harness-only),
-    which D2 owns.
+    do by tearing every Engine down before exit. The plugin still could not compile a kit then (the RPCs were
+    harness-only), which D2 owned and did: the next bullet.
+- **D2 built: kit compiles as offline graphs**
+  ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md), seven commits): an offline job
+  seam runs a graph document to completion on a host of its own, on an engine-owned worker pool (`[offline]`);
+  `rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm` join `rp.dsp`; and `src/kit/` compiles LSDj and risa kits
+  as chains of those jobs, packing the banks in TypeScript. The 39 baseline banks and 12 edge banks are the
+  compiler's, held by `test-native/kit-baseline.test.ts` and `kit-compile.test.ts`; native `KitCompiler`, the two
+  codecs, `KitUtil`, `SampleUtil` and the harness `compileKit` / `compileDmc` RPCs are gone, so the plugin compiles
+  kits too. Strict parity held bit for bit at every commit, and no parity or kit golden was re-recorded (the `rp.dsp`
+  descriptor goldens were, in stages 1 and 3). What it leaves:
+  - **D3**: the CLI `render` and the UI background renders on the seam, keeping their block-size sequence and
+    per-block side effects or re-scoping their byte-identity claim. A render sees a completion's `guardsFired` /
+    `guardReason` and each buffer's `nonFinite` and decides; nothing fails on them, and a byte-exact caller refuses a
+    guard count above 0. The seam's block is a fixed 1024 frames, and its jobs are bounded (64 outstanding, up to 8
+    workers). D3 must also extend or decide what a render needs and the seam lacks: system nodes and a script host in a
+    job (its registry is rp.core, rp.dsp and rp.dsp.resample only), a TypeScript cancel, completions that stream
+    rather than return at the fixed `frames`, and a rejection at project close.
+  - **A plugin kit row** is possible and not built: it imports `src/kit` and runs it on the stores' backend, not
+    `audioDriver` (the driver's compile would turn on the offline poll's native wait and block the UI frame).
+  - **Residuals.** `src/lsdj/` is test-reference code only now (`Effects`, `KitBank.hpp`); `OffsetLookup.hpp` is dead
+    (nothing includes it) and a candidate for the same cleanup as enkiTS. Nothing in production calls
+    `rp::lsdj::processEffect`, so do not wire a caller back to it thinking it is the kit path. A
+    job's document may name only the built-in C++ packages (no script nodes), and a user graph of script nodes would
+    need a job-side script host. `deps/enkiTS` is a submodule nothing adds or links; a later cleanup decides whether it
+    goes. The compiler's intermediate buffers cross the RPC (about 1 MB a slot for a 1 s source, 15 MB a kit, for a
+    22 ms compile against native's 1.8 ms), so keeping them native is the lever if it ever matters. Off
+    `linux-x64-gnu13` the kit banks are held only by self-agreement and the structural asserts, since the native
+    comparison that used to make the edge banks exact everywhere went with the compiler.
+  - **Confirm the removed transitive includes build under MSVC.** The tree has not been built with MSVC since D2
+    deleted headers that used to arrive through `BackendTypes.hpp` and `retroplug-core`; run `build.bat` (or the
+    Windows CI job) before trusting the platform that has not run here.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 2e0612820..eff12e2cd 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -27,11 +27,13 @@ import/export paths are written once:
 - **`TRACKER_INTEGRATIONS`** — the registry that maps a system to its catalogs.
 - **`liveSav`** — reads the battery out of the *running* core rather than off disk, which is why
   the Songs menu acts on what the cart currently has.
-- **Kit compiles** are native: `KitCompiler` over `LsdjKitCodec` / `RisaDmcCodec`, with the effects and resample from
-  `src/dsp/` (the same block-streaming kernels the `rp.dsp` graph nodes wrap), exposed as the harness-facet
-  `compileKit` / `compileDmc` RPCs. TPDF dither seeds from a hash of its input, so a kit compiles to the same bytes
-  every time, and `retroplug-audio-test`'s `[kit-baseline]` holds the compiled banks (D1,
-  [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)).
+- **Kit compiles** are TypeScript ([`src/kit/`](../packages/retroplug/src/kit)): each slot is a chain of offline
+  graph jobs whose passes are `rp.dsp` documents (the effects and resample kernels in `src/dsp/`, which those nodes
+  wrap), and TypeScript packs the codes and bits and assembles the bank. TPDF dither seeds from a hash of its input, so
+  a kit compiles to the same bytes every time, and `test-native`'s `kit-baseline` and `kit-compile` hold the compiled
+  banks to their goldens (D1 and D2, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
+  [modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11). Every host compiles a kit,
+  the plugin included; the native compiler and its harness-facet RPCs are gone.
 
 What it does **not** abstract is role registration; each console registers its own roles in
 [romProviders.ts](../packages/retroplug/src/romProviders.ts).
@@ -46,8 +48,8 @@ the `risa-sync` role turns transport into risa's arm / start / 24-PPQN clock / s
 the emulated EverDrive N8 FIFO. Layout support covers risa 2.3.0.
 
 - Code: `src/risa/` (codec + ROM + runtime), `src/risaRole.ts`, `src/risaSync.ts`,
-  `src/tracker/risa*Catalog.ts`. The DMC kit codec is native (`src/risa/RisaDmcCodec.cpp`) and shares the `src/dsp/`
-  effects and resampler with LSDj's kit path.
+  `src/tracker/risa*Catalog.ts`. The DMC kit encode is `rp.dsp.dpcm` in the kit chain, and the bank packer is
+  `src/risa/rom/kit.ts`; there is no native risa codec.
 - **Prove a change:** `pnpm test risa` / `pnpm test:native risa`, and `pnpm reaper:risa-sync` for
   the real-DAW drift render (not in CI).
 - Deeper: [docs/risa-integration-plan.md](../docs/risa-integration-plan.md) (design rationale; the
````

Run: `sed -n '13858,14411p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 3: AGENTS.md and the live docs**

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 59ac3b80e..4aba78162 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -27,7 +27,8 @@ The rules below are the parts that don't fit those.
   also run `pnpm install` in `deps/dpf.js/deps/lv_binding_js`, or the UI bundle
   can't resolve `react`. Don't bump any submodule pointer (in either repo) without
   checking — they're managed deliberately. RetroPlug also keeps `deps/sameboy` +
-  `deps/mesen` / `deps/r8brain` / `deps/enkiTS` (the shared core), and `deps/efsw`
+  `deps/mesen` / `deps/r8brain` (the shared core; `deps/enkiTS` is still a submodule, but no
+  target adds or links it since the native kit compile retired), and `deps/efsw`
   (the file-watcher — `NativeFileWatcher` behind `HostRpcService::drainChangedPaths`
   — now uses it: `add_subdirectory`'d `EXCLUDE_FROM_ALL` at the root, `efsw-static`
   linked into `retroplug-backend`; the watcher is opt-in via `enableWatching`, which
@@ -64,7 +65,14 @@ The rules below are the parts that don't fit those.
   jobs' busy and not-connected refusals, an abandoned job and its `threads`, and the same node behind a real Engine
   driving the production cart adapters to a fake serial port (`[n8-adapters]`), a host's teardown included; the
   surface's two nodes (a frame-0 drain, the queue's bound, the first-block discard, uniqueness, one link each); and
-  the UI's staged message reaching the node the running plan places under its id;
+  the UI's staged message reaching the node the running plan places under its id; the offline job pool (D2,
+  [docs/modular-graph-phase-d2-design.md](docs/modular-graph-phase-d2-design.md) sections 3 and 11,
+  `test/engine-graph/OfflineJobs.test.cpp`): a document run to completion on a host of its own held to the same nodes
+  run directly, the pool's workers and 64-job bound, a cancel queued or running, a teardown that abandons jobs in
+  flight, every refusal's reason, the RPC's samples, both kit windows and the cap's arithmetic against copies of each
+  formula, the file input against `SampleCache`'s decode, the live plan unchanged beside a job and its warmed blocks
+  allocating nothing, a guard count and a per-buffer non-finite flag, and a job's own floating-point environment
+  (`[offline]`, with a hidden `[offline-bench]`);
   TS nodes run behind the C ABI in the script host (B3a,
   [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
   creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
@@ -98,14 +106,18 @@ The rules below are the parts that don't fit those.
   (`tools/run-profile.sh unit`).
   `retroplug-graph-test` holds the DSP library and its nodes: the kernels are chunk-exact and allocation-free in
   `process` once warmed, the resampler from its first chunk, the verbatim references of every dither algorithm and of
-  both padded error diffusions included (`[dsp]`); each `rp.dsp` node matches its kernel through a real plan, a block
-  counted from the plan's first block allocating nothing, a descriptor golden per type, a biquad reset when a sweep
-  past Nyquist poisons it, and recovering, a dither's channels drawing their own noise and a non-finite input
-  (a NaN, or an infinity the diffusion forms latch) restarting a channel, and gain/biquad/dither all plan on the Pico
-  (`[dsp-nodes]`); and `rp.dsp.resample` plays a
-  22.05 kHz buffer into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, takes a loop committed after
+  both padded error diffusions, and the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), all
+  in `[dsp]`; each `rp.dsp` node matches its kernel through a real plan, a block counted from the plan's first block
+  allocating nothing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream
+  proved different, a biquad reset when a sweep past Nyquist poisons it, and recovering, a dither's channels drawing
+  their own noise and a non-finite input (a NaN, or an infinity the diffusion forms latch) restarting a channel, and
+  gain/biquad/dither all plan on the Pico (`[dsp-nodes]`); and `rp.dsp.resample` plays a 22.05 kHz buffer into a
+  48 kHz plan and a 48 kHz one into the kit rate, at six splits, takes a loop committed after
   its buffer ran out, carries on after a rate change, wraps a loop and clamps to silence past the buffer's end when
-  re-activated and is refused on the Pico (`[dsp-resample]`).
+  re-activated and is refused on the Pico (`[dsp-resample]`), and `[dsp-buffer-nodes]` holds the three
+  buffer-reading nodes to the kit arithmetic: normalize whole and split, its peak and a committed gain, a dither's
+  `seedFrom` and a committed seed, the DPCM's bits and its `normalize` rebuild, a block counted from the plan's first
+  block, the refusals, and the Pico plan.
   `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
   plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
   `PluginExporter::reinitParameters` over a toy plugin.
@@ -423,13 +435,16 @@ own change with a re-record, as the LSDj master-sync fix and the narrow-host NES
 (every LSDj case, its MIDI-out and link-cable ones among them, and every controller, risa and GBA case is
 fixture-only), so a Phase B step is accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`.
 
-**The kit baseline** (`build/bin/retroplug-audio-test "[kit-baseline]"`, [spec/06-build-test.md](spec/06-build-test.md))
-holds the kit compilers' bytes: 39 synthesised cases through `KitCompiler` and the two codecs, each bank's FNV-1a-64,
-size and slot length against `packages/native/test/audio/golden/kit-baseline.json`. Exact bytes are compared only on
-`linux-x64-gnu13` (CI's linux-x64 job sets `RP_KIT_GOLDEN_REQUIRE_TARGET=1`), elsewhere a case must agree with itself,
-and a re-record (`RP_KIT_GOLDEN_UPDATE=1`) is refused off that host and for a case that disagrees with itself. Unlike
-parity it is a 4-bit view: a one-ulp change in a dither can leave every hash matching, which is what `[dsp]`'s
-16-bit references are for.
+**The kit baselines** (`pnpm test:native kit`, [spec/06-build-test.md](spec/06-build-test.md)) hold the kit compiler's
+bytes: `packages/retroplug/src/kit` compiles each case as chains of offline graph jobs, and the 39 synthesised cases'
+banks (and 12 edge banks) are compared as FNV-1a-64 and size (and, for the 39, slot length) against
+`packages/native/test/audio/golden/kit-baseline.json` and `.../kit-edges.json` (the edge records carry no slot).
+Exact bytes are compared only on `linux-x64-gnu13` (CI's linux-x64 job sets `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on its
+`test:native` step), elsewhere a
+case must agree with itself and the layout facts are still compared, and a re-record (`RP_KIT_GOLDEN_UPDATE=1`) is
+refused off that host and for a case that disagrees with itself. Unlike parity it is a 4-bit view: a one-ulp change in
+a dither can leave every hash matching, which is what `[dsp]`'s and `[dsp-buffer-nodes]`' references are for. The C++
+`[kit-baseline]` suite went with native's compiler; that tag now exits 2 in `retroplug-audio-test`.
 
 **`retroplug-cli` as a consumer's whole test harness** (`pnpm test:cli-ts`):
 `retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `run <session.ts>` does one
diff --git a/docs/lsdj-sav-ts-design.md b/docs/lsdj-sav-ts-design.md
index f93b5e329..d70f014ab 100644
--- a/docs/lsdj-sav-ts-design.md
+++ b/docs/lsdj-sav-ts-design.md
@@ -263,9 +263,9 @@ The model is a **standalone module** (`packages/retroplug/src/lsdj/model.ts`), *
 `KitInstrument` (song metadata) is **in scope**; the kit/sample *compiler* is not (§ scope).
 
 **Scope boundary.** In scope: `model/*` + `codec/*` + `SavSerialization.hpp`. Out of scope:
-the kit/sample pipeline (`Effects`, `KitUtil`, `KitCompiler`, `SampleCache`, `SampleUtil`,
-`OffsetLookup`) — compiled-but-dead, no model dependency, heavy native deps
-(r8brain/miniaudio/enkiTS). Only the word "kit" is shared.
+the kit/sample pipeline (`Effects`, `SampleCache`, `OffsetLookup`; native at the time, the compiler itself TypeScript
+since D2, [modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md) section 11): no model dependency, heavy
+native deps (r8brain/miniaudio). Only the word "kit" is shared.
 
 ---
 
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 41da98029..fd47f5d79 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -228,16 +228,20 @@ backend.writeFileAtomic(out, r.bytes());            // save the patched .gb
   Both decode (`kit.sampleData()`) and encode (`compileKit`'s `rotate`) gate on the ROM version, so import
   is byte-correct for any version — `LsdjRom.rotatesSamples` (true for 9.2.0+) drives both. Byte-exact with
   the LSDj tool's own kits.
-- **Sample import = native.** Resample (r8brain, anti-aliased), effects (gain/normalize, RBJ filter, TPDF dither),
-  and 4-bit pack happen in **C++**: `packages/native/src/lsdj/KitCompiler` (`KitUtil` / `SampleUtil`) over the
-  block-streaming kernels in `packages/native/src/dsp/`, which the `rp.dsp` graph nodes wrap too, fanned across an
-  enkiTS pool per sample, decoding WAV/MP3/FLAC via miniaudio. The two TPDF dithers seed from a hash of the sample the
-  dither receives (salted by algorithm), so a kit compiles to the same bytes every time, and the other three
-  algorithms are deterministic already; `retroplug-audio-test`'s `[kit-baseline]` holds every compiled bank to a golden
-  (D1, [modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md)). Exposed as the **harness-facet** RPC
-  `compileKit` (CLI/test-host only, never the plugin) on `AudioDriver`. The pure-TS `rom` module orchestrates: whole-kit
+- **Sample import = the kit compiler, TypeScript over native DSP nodes.** Resample (r8brain, anti-aliased), effects
+  (gain/normalize, RBJ filter, TPDF dither) and the DMC encode run as `rp.dsp` graph nodes inside offline jobs, and
+  TypeScript (`packages/retroplug/src/kit/`) builds each pass's document, cuts the buffers and packs the 4-bit
+  nibbles into the bank. The two TPDF dithers seed from a hash of the sample the dither receives (salted by
+  algorithm), so a kit compiles to the same bytes every time, and the other three
+  algorithms are deterministic already; `test-native`'s `kit-baseline` and `kit-compile` hold every compiled bank to a
+  golden (D1 and D2, [modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md) and
+  [modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md) section 11). The CLI and the harness hosts
+  compile through `AudioDriver`'s `compileKit` / `compileDmc` (settled by the driver's native wait); a host with a UI
+  tick, the plugin included, calls `src/kit`'s entry points on its backend, settled by the job pump. The pure-TS `rom`
+  module orchestrates: whole-kit
   builds take the returned 16 KB bank directly; single-sample imports splice the compiled bytes beside existing
-  samples' **raw** bytes and rebuild the offset table (`buildKitBank`), budget-checked against `0x3fa0`.
+  samples' **raw** bytes and rebuild the offset table (`buildKitBank`), budget-checked against `0x3fa0`. A slot that
+  does not compile is left empty and the result's `errors[i]` says why; the CLI's read-back prints it.
 - **Asset files (community formats, lsdpatch-compatible).** Whole assets interchange as the standard
   files: **`.lsdpal`** palettes (4-char name + the 40-byte palette body), **`.png`** fonts (an 8-tile-wide
   greyscale image — 64×72 for the 71 main tiles, 64×120 for the **extended** layout that also carries the
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 643337c2d..f4f2aee1d 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -839,9 +839,10 @@ A render loop pulls the same executor on any thread. That covers CLI `render`, b
 ([spec/11](../spec/11-ui-rendering.md)) and **kit compiles**: a kit compile is a graph `buffer-player` ->
 filter -> dither -> `buffer-recorder`, run as fast as possible, with the same DSP nodes a live graph
 uses. This is how one sample patcher serves LSDj, risa and BlipToaster: the generic chain plus a
-per-cart profile (sample rate, bit depth, slot layout, codec) contributed by each cart's package.
-Today's `src/kit/KitCompiler` (codec-neutral) and `src/lsdj/Effects` (gain, biquad, dither) are already
-most of this.
+per-cart profile (sample rate, bit depth, slot layout, codec) contributed by each cart's package. D2 built
+the seam (a document run to completion on an offline host of its own) and
+`packages/retroplug/src/kit/`, which builds each pass's document and packs the bank; D3 moves the CLI's and
+the UI's renders onto it.
 
 ### 5.8 The Pico runtime
 
@@ -1236,16 +1237,19 @@ leaves (its section 11): P7's remainder, and a cart's battery reachable only at
 ### Phase D: the DSP library and offline graphs
 
 - Library nodes: gain, biquad, dither, resample (porting `lsdj/Effects` and the r8brain path).
-- Kit compiles as offline graphs; proof is **byte-identical kit output** against today's `KitCompiler`.
+- Kit compiles as offline graphs; proof is **byte-identical kit output** against the native compiler they replaced
+  (`KitCompiler`, retired with its RPCs in D2; the goldens it seeded are still the oracle).
 - CLI `render` and background renders on the offline executor.
 
 D1 is built ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), six commits): the kit path's DSP is a
 core-free library of block-streaming kernels (`src/dsp/`, `retroplug-dsp-kernels` + `retroplug-dsp-resample`), the kit
 compilers run it, and the graph has it as `rp.dsp.gain`, `.biquad`, `.dither` and `rp.dsp.resample`; TPDF dither is
-seeded from the sample it receives, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]`
-records 39 compiled banks. D2 (audio buffers into and out of a document, a run-to-completion driver, normalise and the
-two-rate kit chain, kit compiles as graphs byte-identical to the baseline) and D3 (the CLI `render` and the background
-renders on that driver) are left.
+seeded from the sample it receives, so a kit compiles to the same bytes, and the kit baseline recorded 39 compiled
+banks. D2 is built too ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md), seven commits): the
+offline job seam, `rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm`, and `packages/retroplug/src/kit/`, whose
+TypeScript compiler builds each pass as a document and packs the bank, held byte for byte to those 39 banks; native's
+compiler retired with its RPCs, and every host, the plugin included, compiles kits. D3 (the CLI `render` and the
+background renders on that driver) is left.
 
 ### Phase E: the Pico runtime
 
````

Run: `sed -n '14423,14596p' docs/modular-graph-phase-d2-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

Run: `git diff --check`
Expected: no output: no trailing whitespace and no conflict markers.

Run: `git diff | grep "^+" | grep -c $'\u2014'`
Expected: 0: no added line carries an emdash. The report ran the same grep on `git show HEAD` after committing; before the commit, the working tree's diff is what carries the added lines.

Run: `pnpm check:docs`
Expected: check-doc-links: 75 file(s) clean: the reports record 74, and this plan is the 75th tracked Markdown file.

- [ ] **Step 4: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M AGENTS.md
 m deps/sameboy
 M docs/lsdj-sav-ts-design.md
 M docs/lsdj.md
 M docs/modular-graph-design.md
 M docs/modular-graph-phase-d1-design.md
 M docs/modular-graph-phase-d2-design.md
 M spec/00-overview.md
 M spec/01-architecture.md
 M spec/02-native-host.md
 M spec/03-ts-layer.md
 M spec/04-roles-protocol-nodes.md
 M spec/06-build-test.md
 M spec/07-remaining-work.md
 M spec/13-subsystems.md
````

````bash
git add -- AGENTS.md docs/lsdj-sav-ts-design.md docs/lsdj.md docs/modular-graph-design.md \
  docs/modular-graph-phase-d1-design.md docs/modular-graph-phase-d2-design.md spec/00-overview.md \
  spec/01-architecture.md spec/02-native-host.md spec/03-ts-layer.md spec/04-roles-protocol-nodes.md \
  spec/06-build-test.md spec/07-remaining-work.md spec/13-subsystems.md
git commit -F - <<'EOF'
docs: D2 runs kit compiles as offline graph jobs

The D2 design's status line now says built, sections 3 to 6 are corrected
where the build made them false (the completion's guard count and per-buffer
non-finite flag, a job abandoned rather than failed at teardown, the compiler's
signature and per-slot reasons, normalize's peak over the channels it plays,
rp.host.guards/1), sections 8 and 9 carry what D3 inherits and what is left
beyond D2, and a new section 11 records the build in the D1 design's style:
each of the seven stages' decisions, the section 10 prototype questions
answered (stage 4's throughput table included), the guards, the non-finite
flag and the pinned floating-point environment, what building it found, and
the suite as built.

D1 keeps its record; only the sentences that stated the native compiler and
the C++ kit baseline suite as current gain a pointer to D2.

The specs follow: spec/01 gains the offline job pool and its threading
invariant, spec/02 the three RPCs and the recounted facet methods (90 in the
union), the pool, the new rp.dsp types, the retired KitCompiler in the shared
core and a rewritten kit reproducibility section; spec/03 the job-seam
promises, spec/04 and spec/13 the TypeScript compiler, spec/06 the two kit
baselines on the test-native runner (off the target the edge banks are held by
self-agreement) and the new tags, spec/07 D2 built with its D3 and residuals,
spec/00 the core's real contents. AGENTS.md's kit baseline paragraph and deps
entry follow, docs/lsdj.md and modular-graph-design.md describe the compiler
that exists, and the lsdj-sav scope boundary no longer names deleted symbols.

The review's fix pass corrects what the build made false: no parity or kit
golden was re-recorded (the descriptor goldens were, in stages 1 and 3), the
edge golden's shape and contents, kit-patch splicing a pre-built file and
compiling nothing, the CLI and harness hosts on AudioDriver while a UI host
calls src/kit, the kit command's real filters (kit-dither included,
offline-run separate), and the four gaps D3 must extend or decide. It also
records the stages' smaller decisions, points the first-snapshot allocation at
the known 0.5 s publish, calls OffsetLookup.hpp dead, and corrects the
section 3 signatures, the DSP target lists, the list indentation and the two
figures for the old TypeScript scan.
EOF
````

---

### Task 8: the acceptance run

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. **There is no hardware leg**: D2 changes no console I/O, no cart path and no host device, so
nothing here needs a real NES or the N8 Pro. The counts are the tip verification at `578d339b7`, whose code is
identical to the final tip `00676a33a` (only docs changed after); its logs are `/tmp/d2pro/tip-*.log` and its summary
lines are in `tip-summary.txt`: every leg exit 0, including both consumers and both kit goldens. A line no log records
says so and asks for the count instead. The Reaper suite is new here: D2 gained an offline pool and three RPCs, so the
engine's host surface is worth a live host's check even though no log above records it.


- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build of every target with no warning (Task 7 changed Markdown only). A
full build reaches `[100%]` and prints a `Built target <name>` line for every target; which one comes last depends on
the parallel schedule, so do not pin it.

Run: `git log --oneline -8`
Expected: eight lines: Task 7's `docs: D2 runs kit compiles as offline graph jobs` down to Task 1's
`feat(dsp): rp.dsp.gain and rp.dsp.biquad cover any sane kit value`, then `docs: the D2 implementation plan`. The
design commit 1c403cd5 is the ninth.

- [ ] **Step 2: The suites, with and without fixtures**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 171 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. Engine-graph without fixtures
`test cases: 282 | 273 passed | 9 skipped`, `assertions: 66106 | 66106 passed`; graph-test
`All tests passed (356087 assertions in 272 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test
cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`. The other six are at their own counts:
`retroplug-midi-test` 35 / 19, `retroplug-dynparams-test` 24 / 7, `retroplug-plugin-test` 21 / 6,
`retroplug-lottie-test` 7 / 1, `retroplug-classid-test` 10 / 3 and `retroplug-watcher-test` 11 / 3, from the tip
log's sections. Report all ten lines.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (66261 assertions in 282 test cases)`: the same 282 cases with the fixture-gated sections
running.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` for
the serial binary: D2 touches no n8 code, so both are the baseline's.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, every case run, nothing re-recorded, and `git status` shows no
parity baseline changed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, the skip check passing. The tip verification ran the strict form only, so
report this run's skip line rather than a count no log states.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 163 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1),
nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 163 native test file(s) passed (jobs=16)`, with `# 136 case(s) SKIPPED in 61 file(s)` and the skip check
clean (the log lists every file and its count).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 40 UI test file(s) passed (jobs=16)`, the kit-compile case among them.

Run: `pnpm test:ui`
Expected: the same 40 files (a skip does not remove a file); report which cases skipped without the fixtures.

Run: `pnpm test:node`
Expected: `tests 12`, `pass 12`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`, including the four async-exit lines.

Run: `pnpm check:docs`
Expected: `check-doc-links: 75 file(s) clean` (the prototype's 74 plus this plan, whose links resolve from docs/).

- [ ] **Step 3: The kit goldens, and nothing re-recorded**

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, 3 passed, 0 skipped, 0 failed: the 39 banks, hash, size and slot,
in a fresh process on the recording host with the off-target fallback turned into a failure.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed: every edge bank against `kit-edges.json`.

Run: `pnpm test:native offline-run`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `git status --short`
Expected: only ` m deps/sameboy`: no kit golden, no descriptor golden and no parity baseline moved during the run. A
changed `test/audio/golden/*.json` or `test/graph/golden/rp.dsp.*` here means something re-recorded, which nothing in
this plan does.

- [ ] **Step 4: The sanitizers, on D2's tags**

D2's tags are `[offline]` in `retroplug-engine-graph-test`; `[dsp]`, `[dsp-nodes]`, `[dsp-resample]`,
`[dsp-buffer-nodes]`, `[resource]` and `[resources]` in `retroplug-graph-test`; and `[dither]` in
`retroplug-audio-test`. Build each binary in its own tree first, and note that `[resource]` and `[resources]` ride
along because they are the resource cases the new nodes' settings refs sit beside.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: both binaries in the TSan tree.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag, so dropping it changes no case), then
`All tests passed (42175 assertions in 107 test cases)`, no ThreadSanitizer report: the pool's 25-jobs-in-parallel
case is the point of this tree. This selection includes `[capability]`, so 2 or 4 more assertions than quoted are
normal, from the device-thread progress case, as the ground rules say.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263091 assertions in 78 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: all three binaries in the ASan tree.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (42175 assertions in 107 test cases)`, no
AddressSanitizer or LeakSanitizer report. This selection includes `[capability]` too, so the same 2 or 4 more
assertions than quoted are normal; the two trees differ only by scheduling, and it passes in both.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356087 assertions in 272 test cases)`: the whole graph binary under ASan, no report (the
nodes' buffers and the resamplers are freed with their instances).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report: the kit dithers' direct cases, which no task
touched after it moved out of the retired suite.

- [ ] **Step 5: The live hosts**

One at a time, with nothing rebuilding the plugin meanwhile. D2's tip verification ran the SDL smoke but not the
Reaper checks, so the smoke's line is quoted and the Reaper suite's is not: report it, and a failure here is a real
finding about a plugin host (the engine gained a pool and three RPCs in the emulator facet) rather than a missing
figure. Make sure no Reaper or jackd of an earlier run is left first.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines (boot and a non-blank render, the MIDI-clock transport,
multi-output audio, resize, the minimum clamp, the wheel, the close guard, the idle reclaim).

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight). The suite's own fixture step reads
`../resources/...`, which does not resolve from this worktree, so author them by hand with explicit ROM paths and run
the suite with `RP_SUITE_NO_BUILD=1`, which skips its own build and every fixture, after the build above.

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then `ALL 15 REAPER CHECKS PASSED` (the suite prints the inconclusive form
instead when a check skips). Report the line. If a render fails, run that one check alone once (its own
`pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

- [ ] **Step 6: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched. Never edit `/workspaces/bliptoaster` or `/workspaces/nesvj`: both commands copy the suite to `/tmp`, run it
there, and remove the copy, and both repositories' `git status --short` must stay empty. D2 changes what the CLI's kit
compiles return (a promise of `{bytes, errors}`) and adds `--filter`, so these are the checks that the consumer-facing
CLI still builds its kits.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d2-bt && mkdir -p /tmp/d2-bt/build /tmp/d2-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/d2-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/d2-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/d2-bt/retroplug-cli/ && cd /tmp/d2-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/d2-bt; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d2-vj && mkdir -p /tmp/d2-vj/build /tmp/d2-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/d2-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/d2-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/d2-vj/retroplug-cli/ && cd /tmp/d2-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/d2-vj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 7: The final tree**

Run: `git status --short`
Expected: only ` m deps/sameboy`; no scratch file and no parity or golden baseline changed.

---

