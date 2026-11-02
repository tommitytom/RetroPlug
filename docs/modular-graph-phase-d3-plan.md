# Phase D3: renders as engine jobs - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Every render becomes a job on D2's offline pool. The CLI's `render` and the UI's background renders run as a
second kind of offline job, the engine job: a bare QuickJS runtime and an `Engine` of their own on a pool worker,
running the unchanged render library, submitted, drained and cancelled through the same RPCs, pump and promise side as
D2's kit compiles. Inside an engine job the engine runs whole 1024-frame blocks whatever chunk the library pulls, so a
render's sample stream no longer depends on its pulls, and a new render baseline records the bytes after the move.
`RenderJobRegistry`, the `__rp_*Render` hooks and the CLI's in-process render path retire; parity, the kit goldens and
both consumer suites do not move a byte.

**Architecture:** Per [modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md), whose section 10 ("As
built") records the build and which Task 5 lands with its status line, sections 1 to 8 corrected in place. The pool
gains a second job kind: `submitEngineJob({kind, spec})` queues a render, a pool worker builds a `RenderHost` (whose
`run()` builds the job's own `Engine`, RPC services and in-process server), evaluates the embedded render bundle and
destroys it all on that thread, and the drain reports each running job's progress and console lines and each finished
one's completion. Engine jobs hold at most all but one of the pool's workers, so a kit compile always finds one. The
job's `EngineRpcService` runs continuous blocks: a call returns what it asked for and carries the rest of its last block
to the next call of its shape. TypeScript wraps it as `backend.engineRun`, a promise with `cancel()`; the CLI awaits it
under an interrupt hook, and the UI keeps a `RenderJobs` list in the store graph the control plane owns.

**Tech stack:** C++20 with Catch2 (`retroplug-engine-graph-test`'s `[engine-jobs]`), the vendored Mesen and a second
tracked SameBoy patch for the process-wide state two cores on two threads share, QuickJS in every native host (the
render worker bundle runs in each job's own runtime), msgpack over rpcpp for the RPCs, TypeScript for the promise side,
the CLI, the UI's render list and their tests, and the `test-native` TAP runner for the render baseline. No submodule
moves (`deps/mesen` is vendored in-tree, not a submodule).

**Spec:** [modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md). D2's design and plan are
[modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md) and
[modular-graph-phase-d2-plan.md](modular-graph-phase-d2-plan.md).

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/d3-render` in this worktree, on top of the design commit 5c1d6a4bc: five commits, one per task (Task 6 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X, 32 threads). A block that creates a
file is that file as the prototype commit left it; one that changes it is the prototype's diff for it, so `git apply`
it from the step's text (or apply it hunk by hunk with the Edit tool); one that deletes files runs `git rm`; the one
binary file (a 32 KiB battery) is placed from its gzipped bytes in base64. The tip verification ran on `c653a5fb3`,
whose code is identical to the final tip `b43b29127` (only docs changed after), and its logs are `/tmp/d3pro/tip-*.log`.
Every expected pass, test count and figure is quoted from the stage reports in /tmp/d3pro, from the stage logs they
cite or from the tip logs, which is what the generator checks; a failing-test step's expected failure is what the
missing code produces, as each step says. When this plan was generated it was replayed in a scratch clone of
5c1d6a4bc, every block written or applied in order, and each task committed with the command given: each commit's tree
came out exactly as the prototype's, and each commit message as its. The prototype commits, by task:

- Task 1 `12414e0b3`: engine jobs run a render on the offline pool.
- Task 2 `13e161c29`: an engine job renders in whole blocks, and the render baseline records it.
- Task 3 `aaf1709c8`: the CLI and the UI render through engine jobs.
- Task 4 `62367f4b2`: `RenderJobRegistry` and the render hooks retire.
- Task 5 `b43b29127`: the docs. Task 6 is the acceptance run and commits nothing.

**Where this plan departs from the design as committed (5c1d6a4bc).** The design was written before the prototype;
this plan follows what was built, and Task 5 lands the design with section 10's record and sections 1 to 8 corrected.
In short:

- **The engine job's wire.** `submitEngineJob({kind, spec})` joins the emulator facet (19 methods, 91 in the union);
  the spec crosses as a JSON string and the answer is D2's `RpcOfflineSubmitResult`. `kind` must be exactly
  `"render"`: anything else is refused, `unknown engine job kind "X": the one kind is "render"`, and a bad spec is
  the job's own error (`render worker: job spec missing 'rom'`), not a refusal. `drainOfflineJobs` grows
  `engineCompletions` and `engineProgress` beside the graph `completions` and `inFlight`, which are unchanged;
  `cancelOfflineJob` takes either kind. Every running engine job is reported at every drain, with the console writes
  its script made since the drain before.
- **Console lines have a bound.** A job's lines live in a per-job `EngineJobRecord`, bounded between drains at 256
  lines and 64 KiB: past either a line is dropped and counted, the count rides the next report and TypeScript prints
  it as `[N console lines dropped]`, and a line longer than 64 KiB alone is cut at a UTF-8 boundary. A line is one
  console write, which can hold newlines. Native diagnostics printed straight to the process (Mesen's iNES log)
  still go to stderr: only the script's console is routed. With the editor closed nothing drains, which is why the
  bound exists.
- **The worker budget.** `engineBudget()` is all but one of the pool's size, and 1 for a pool of 1; graph jobs run
  on at most `size()`, and the pool starts at most `maxWorkers()` threads, which is `size()`, or 2 for a pool of 1,
  whose second thread a graph job takes while a render holds the first. A worker takes the oldest job its kind's
  limit allows, so renders wait first in, first out behind renders and a graph job starts past queued renders; a
  worker is started only when the runnable queued jobs outnumber the workers not running one, so a render held back
  by the budget starts no thread. No further fairness rule was needed. The pool's size is a constructor argument (0
  means `concurrency()`) and a test can give it an `EngineJobBody` of its own, so the rules are held to an exact
  timeline.
- **A job's lifetime is one worker's stack frame.** The worker pins the default floating-point environment (as graph
  jobs do), builds a `RenderHost`, whose `run()` builds the job's own `Engine`, `QueuedInvoker`, four RPC services
  and in-process server, evaluates the embedded render bundle synchronously and returns; all of it dies on the
  worker before it takes the pool's lock to publish. Teardown drops queued jobs, cancels running ones and joins, and
  a running render sees the cancel at its next chunk, so nothing a render touches outlives the pool, which `~Engine`
  resets before any other member. Lock order is the pool's mutex, then a record's lock; no lock is held across
  `RenderHost::run`. `OfflineJobs.hpp` lists the process-wide state a render shares and why each is safe: Mesen's
  home folder and its atomic scratch-path counters, the `MessageManager` lock, SameBoy's generator, camera seed, SGB
  table and `apu.c`'s constructor-built steps, and r8brain's statics with the bundle bytes; QuickJS keeps no
  per-thread state and `SnapshotRegistry` is per `Engine`.
- **Two vendored fixes the pool needed, found by ThreadSanitizer and by reading the cores.** Mesen's `SimpleLock`
  (vendored in `deps/mesen`, not a submodule), behind the process-wide `MessageManager` lock, read its holder and
  count as plain integers while another thread built a core; they are relaxed atomics now, with two
  `is_always_lock_free` asserts. SameBoy's power-on `GB_random` seed, the camera's noise seed and the SGB's lazily
  built `fm_sin` table are process statics a second Game Boy core raced on:
  `cmake/patches/sameboy-process-statics.patch` makes the first two relaxed atomics (GNU `__atomic` builtins, as
  only GCC and Clang build the core) and the third a load-time constructor, as `apu.c`'s tables are.
  `cmake/sameboy.cmake` applies a list of patches, the per-channel tap and then this one, each with the same
  idempotent and fail-loudly rules, so a configure leaves five files modified in `deps/sameboy` (`Core/apu.c`,
  `apu.h`, `random.c`, `camera.c`, `sgb.c`). The SGB jingle's bytes are unchanged by the patch; the camera half has
  no red/green, since no Game Boy Camera ROM exists here.
- **`engineRun`, cancel and progress.** `backend.engineRun(kind, spec, { onProgress, onLog })` returns a handle, a
  promise of `{ status, message, outputs, renderedMs }` with `cancel()`; `offlineRun`'s handle gains `cancel()`.
  `engineRun` resolves with all three statuses and rejects only on a refusal or a port that throws; a cancelled
  graph job rejects with `OfflineRunError("cancelled")`; a run still held on the TypeScript side settles at once on
  cancel; `cancel()` answers false once settled. Cancel wins (D2's rule, for both kinds): a cancel that returned
  true makes the outcome `cancelled` even if the render finished first, and such a completion keeps its `outputs`.
  `onProgress` is a running maximum, because an auto-detect render lowers its raw count once, to the length it
  wrote, and the result's `renderedMs` is that length. A settle that a callback re-enters returns without draining,
  and the lost-job check considers only the jobs pending at its drain. `KitJobRunner` names only the promise it
  needs. The mock rejects every engine job.
- **Continuous blocks, and the carry's rules.** `EngineRpcService::setContinuousBlocks(bool)` is C++ only, not an
  RPC, and `RenderHost` always sets it. `renderAudio`, `renderAudioPerSystem` and `renderAudioPerChannel` share one
  private pull loop: a call returns the frames it asks for and carries the rest of its last block, keyed on its
  shape (mix, per system or per channel), the per-channel system id, the stream count and the rate. A call of
  another key drops the carry, as does `renderBlocks`; the emulation behind dropped frames is not undone, so the
  next audio starts at the next block. Exact mode runs the old splits, so parity is untouched, and it stays for the
  in-process render tests. A split render settles 1500 ms in its stream's shape, not in the mix, so its stems line
  up with its mix; for a NES that settle shape is emulation timing, not only routing. `RenderOpts.pullMs` (default
  100) is a test knob over the stream's pulls only.
- **The honest scope of chunk independence.** Only a render with a node that consumes the transport or MIDI each
  block (smsggdj's host sync, `rp.mgb.midi`) ever depended on the chunking; under a mutation that forces engine jobs
  back to exact blocks exactly the host-synced smsggdj case of `render-chunks` fails, and the fixed smsggdj, LSDj
  stems and risa pins renders were already pull-independent. Acts between calls (presses, smsggdj's work-RAM load,
  the detection reads) land at the next block boundary after the call before them, so they follow that call's
  length; every fixed-length render is pull-independent end to end because its acts use lengths of their own. An
  auto-detect render polls once per pull, so its samples agree at any pull but its length is quantised to the poll
  (MACHETE at pulls of 100, 50 and 200 ms ends at 191700, 191750 and 191700 ms).
- **The render baseline.** `test-native/render-baseline.test.ts` holds 13 cases (`test-native/render-cases.ts`),
  each rendered through `engineRun` and hashed with the kit baseline's FNV-1a-64 over the whole WAV, to
  `test-native/golden/render-baseline.json`: 5 fixture-free (four smsggdj renders of songs authored into its
  battery, the host-synced one among them, and mGB as a Super Game Boy, whose boot jingle sounds with no MIDI), 4
  LSDj and 4 risa. Length and sizes are compared everywhere, hashes only on `linux-x64-gnu13` (elsewhere a case
  renders twice and must agree with itself); `RP_RENDER_GOLDEN_REQUIRE_TARGET=1` turns that fallback into a failure
  and CI's linux-x64 `test:native` step sets it; `RP_RENDER_GOLDEN_UPDATE=1` is refused off the target, for a
  self-disagreeing case and for a run missing any case; every case and file must sound except those a case lists as
  silent. The runner's `DETERMINISTIC_FILES` (`render-baseline`, `render-chunks`, `render-cli`) power their cores on
  in a fixed state, process-wide, so every engine job's own `Engine` too. CI sees the 5 fixture-free cases; no GBA
  case exists, since the repo's GBA ROM renders silence without a BIOS.
- **The detection did not drift.** Ten songs rendered by `retroplug-cli render` before the move and by engine jobs
  after (telemelt's five, tripledipper, risa's ecoli_soul, hevander, let_go and ecoli on NTSC) end on the same
  frame, and no detection changed outcome. The bytes do move, because the presses land at the block boundary after
  the settle (frame 66560 where the old path pressed at 66150 at 44.1 kHz): LSDj sparsely, risa from about frame
  900.
- **The CLI on engine jobs.** `retroplug-cli render` parses its arguments into the job spec, passes the paths as
  typed (the job runs in this process and shares its working directory), submits `engineRun("render", spec,
  {onLog})` and prints the job's lines as they arrive; `--list-songs` stays in-process. It calls `keepAlive()` and
  `setOfflineYield` (the next poll step after a native wait runs as a macrotask, set for the run and cleared after
  it): without the yield a minute-long render exhausts the launcher's bounded loop, and the hook is not
  unconditional because the native test host fires no timers. SIGINT is `__rp_onInterrupt` in `cli/main.cpp`,
  `onInterrupt` in `cli/session.ts`: the first Ctrl-C cancels the job, which ends `ERROR: render: cancelled`, exit
  1, and restores the default action, so a second kills a wedged job. A cancelled render's files are deleted (the
  library sink's `discard()`, on every cancel path). The cores' own stdout (Mesen's load log) goes straight to fd 1
  while the library's lines arrive through the drain, so the two can interleave differently from before.
- **The UI's render list lives in the store graph.** `RenderJobs` (`src/renderJobs.ts`) is `AppStores.renders`: in a
  DAW that graph is the control plane's, which outlives the editor and owns the queue that submitted, so a
  completion is drained by the queue that owns it and no adopt path is needed. `startSystemRender` submits through
  `stores.backend`; the tile badge samples `renders.list()` and a tap calls `renders.cancel`; finished jobs
  auto-dismiss, and one whose system has no tile is dropped as it settles and whenever the systems list changes. The
  SDL standalone gains renders through the same facet.
- **What retires.** `RenderJobRegistry`, the four `__rp_*Render` hooks (`__rp_startRender`, `__rp_cancelRender`,
  `__rp_dismissRenderJob`, `__rp_getRenderJobs`) with their declarations and `g_renderJobs`, the CLI's in-process
  render path, `RenderHost::Blocks` (it always runs continuous blocks) and `RenderContext.blocks` with the settle
  gate that read it. The library's exact-blocks engine and `setContinuousBlocks(false)` stay for the in-process
  render tests and parity.
- **`retroplug-render-host-test` is three legs on engine jobs.** `<job-json>` runs one job on a pool (the production
  path, still checked against `retroplug-cli render` by `cmp`), `--parallel <j>...` runs N jobs on a pool sized so
  all can run and asserts at least `min(N, engineBudget())` ran at once, and `--cancel <j>` cancels after 300 ms and
  asserts `cancelled`, no outputs and no file. `--registry` is gone. It is a manual smoke; `[engine-jobs]` carries
  the coverage CI runs, and `tools/run-sanitizer.sh` has no render leg (it never had one).
- **What no log backs, and what was left.** The Windows build has not seen this tree: the new cross-platform pieces
  are the SameBoy patch, `SimpleLock`'s atomics and the CLI's signal handling
  ([07-remaining-work.md](../spec/07-remaining-work.md) records it). `MesenGlobalInit.hpp` still says its setup is
  guarded by `std::call_once` where the `.cpp` uses a function-local static; the prototype's docs review found it
  after the code was final, and this plan does not change the prototype's code. No parity baseline and no kit golden
  is re-recorded by any task: only the new render golden (Task 2) is recorded, and the skip baseline is regenerated
  (Tasks 1 to 3), each proved against the prototype's bytes.


**Ground rules for whoever executes this:**
- This plan lives at `docs/modular-graph-phase-d3-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's links are relative to that location; every command and path in the steps is relative to the worktree
  root.
- Commit after each task (the commands are given; Task 6 commits nothing unless a step fails and is fixed, in a commit
  of its own). No `Co-Authored-By` or "generated with" trailer. Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code, and in Task 3 one
  pre-existing user-visible warning in `render.ts` that a `try` re-indented, whose emdash Task 4 removes): leave those
  be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with uncommitted
  work in the tree. Never touch a submodule. `deps/sameboy` shows as modified after any configure: that is the tracked
  audio patch, and from Task 1 on the second tracked patch too, expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- **Parity holds the engine's output to the bit, and no task re-records it.** Continuous blocks exist only inside an
  engine job; the live engine, the harness and parity keep exact blocks, so a strict parity failure is a real change:
  stop and find it. Never set `RP_PARITY_UPDATE`. The kit goldens are never re-recorded either: never set
  `RP_KIT_GOLDEN_UPDATE`.
- **One golden is recorded in this plan, in exactly one step**: Task 2's `render-baseline.json`, with
  `RP_RENDER_GOLDEN_UPDATE=1` by the `test-native` `render-baseline` file, on the target, with the fixtures. It is
  quoted whole below, and the step proves the recorder lands the same bytes. **The skip baseline is regenerated, never
  edited by hand**, in Tasks 1, 2 and 3: an unfiltered fixture-absent run with `--update-skip-baseline`, whose result
  must be the prototype's bytes (each step quotes the blob).
- Every whole-file block below (a `Create`) is followed by the `sed -n` range that extracts it from this plan and by the
  blob hash the file must have: write or extract the file, then `git hash-object <path>` must print the hash the step
  quotes, which is the prototype commit's blob. A retyped slip fails there rather than in a test that never reads the
  line. The one binary file is decoded from its block the same way.
- **The render golden's exact bytes exist on `linux-x64-gnu13` only**, so this plan is executed on that host:
  recording refuses any other, and CI's linux-x64 job is the other place they are compared. On a foreign host Task 2's
  recording step refuses and the plan stops there; do not force it.
- Every `check:docs` count below is 77 from Task 1 on: the reports and the tip log record 76, and this plan is the 77th
  tracked Markdown file `check-doc-links` walks.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; without it their
  cases skip. A shell `export` does not survive between commands. `pnpm test:native` takes ONE filter per run (an
  exact slug like `render-baseline`, or a dash-form prefix; `risa-render` runs its three files).
- The Catch2 binaries and `retroplug-render-host-test` are `EXCLUDE_FROM_ALL`: `cmake --build build` does not relink
  them, so a library edit needs the target built by name before a run. A new or removed source needs
  `cmake -S . -B build` first.
- The sanitizer trees are their own: `build-tsan/` and `build-asan/`. Configure one only if its `CMakeCache.txt` is
  missing (`cmake -S . -B build-tsan -DRETROPLUG_SANITIZE=thread`, or `build-asan` with `=address`); a build there
  re-runs CMake itself when a listed file changed. Build the binary in that tree before running it there; a stale
  prototype binary proves nothing.
- Case counts are exact. A `test cases: N | P passed | S skipped` line, an `All tests passed (A assertions in N test
  cases)` line and a per-file count must match the quoted N/P/S. Assertion totals are exact for
  `retroplug-graph-test`, `retroplug-audio-test`, the launchpad, n8 and serial binaries and the small six. **A run that
  includes `[engine-jobs]` or `[engine-jobs-hff]`, and so any whole engine-graph run, reports an assertion total that
  moves from run to run**: those cases assert over the drains that saw a job's progress, and how many drains a render
  spans is the scheduler's (two ThreadSanitizer runs of `[engine-jobs],[offline]` on one tree gave 1685763 and
  1684877). For those, the case count must match and the total is the prototype's, quoted for scale. A run that
  includes `[capability]` moves the same way, by 2 or 4 (D2's rule).
- The prototype branch `proto/d3-render` is this plan's provenance, and no step reads it: every byte check is a blob
  hash quoted in the step.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run one Reaper invocation
  at a time (`pnpm reaper:all`'s own fan-out is fine), with nothing else using Reaper or jackd meanwhile, and before
  each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper; pgrep -a jackd`).
- Track progress outside the committed plan: never tick its checkboxes. Ticking one modifies
  `docs/modular-graph-phase-d3-plan.md`, and every task's expected `git status` list would then gain a line it does
  not show.
- The Windows build has not seen this tree (this plan's prototype is local and Linux-only), so an MSVC-only warning
  is a real finding to report, not a stale quote; no step below builds on Windows.

**Commit this plan before Task 1.** Before the commit, check where the tree is: `git rev-parse --abbrev-ref HEAD` must
print `feature/modularize`, `git rev-parse --short=8 HEAD` must print `5c1d6a4b`, and `git status --short` must show
only ` m deps/sameboy`. If any of the three differs, stop: every step below assumes the design commit on a clean tree.
(`git -C deps/sameboy status --short` shows `Core/apu.c` and `Core/apu.h` modified, and also `Core/camera.c`,
`Core/random.c` and `Core/sgb.c` where an earlier configure of the prototype ran in this worktree; either is fine, and
Task 1's configure says which.) The run below copies the plan to its home in the repository and commits it on its own,
so every later task's `git status` and `git add` see a clean tree:

````bash
cp /tmp/d3plan/plan.md docs/modular-graph-phase-d3-plan.md
git add -- docs/modular-graph-phase-d3-plan.md
git commit -F - <<'EOF'
docs: the D3 implementation plan

The plan for Phase D3, renders as engine jobs, generated from the prototype's five commits on proto/d3-render: engine
jobs in the offline pool with the two vendored fixes for state shared across threads, continuous blocks and the render
baseline, the switch-over of the CLI and the UI, the retirement of RenderJobRegistry and the render hooks, and the
docs. Every expected count is quoted from a stage report, a stage log or the tip verification, and the code was
replayed commit by commit against the prototype's trees.
EOF
````

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/host/engine/OfflineTypes.hpp` | the engine job's request, lines, progress, completion and drain types (Task 1) |
| `packages/native/src/host/engine/OfflineJobs.{hpp,cpp}` | the second job kind: `submitEngine`, `EngineJobRecord`, the budget, the render body, the shared-state list (Task 1); the continuous-blocks body (Tasks 2 and 4) |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `submitEngineJob` and the drain's engine half (Task 1) |
| `packages/native/src/host/render/RenderHost.{hpp,cpp}` | the console sink (Task 1), `Blocks` (Task 2, gone in Task 4) |
| `packages/native/src/host/rpc/` | `submitEngineJob` in the emulator facet and its wire types (Task 1); continuous blocks and the carry in `EngineRpcService` (Task 2) |
| `packages/native/src/util/Determinism.hpp` | who sets `RETROPLUG_DETERMINISTIC` (Task 2) |
| `deps/mesen/Utilities/SimpleLock.{h,cpp}` | the holder and count as relaxed atomics (Task 1) |
| `cmake/patches/sameboy-process-statics.patch`, `cmake/sameboy.cmake` | SameBoy's process statics made thread-safe, and the patch list (Task 1) |
| `packages/native/test/engine-graph/EngineJobs.test.cpp` | `[engine-jobs]` and `[engine-jobs-hff]` (Tasks 1, 2 and 4) |
| `packages/native/cli/main.cpp`, `packages/retroplug/cli/` | `__rp_onInterrupt` and the render tool on engine jobs (Task 3) |
| `packages/native/src/host/render/RenderJobRegistry.{hpp,cpp}` | continuous blocks (Task 3), then deleted (Task 4) |
| `packages/native/plugin/PluginUI.cpp`, `packages/native/sdl/main.cpp` | the hooks and `g_renderJobs` removed, and the SDL comment (Task 4) |
| `packages/native/test/render-host-main.cpp` | `retroplug-render-host-test`, continuous (Task 3), then its three legs on engine jobs (Task 4) |
| `packages/retroplug/src/offline.ts`, `backend.ts`, `realBackend.ts`, `testing/mockBackend.ts` | `engineRun`, the handles' `cancel()` (Task 1), `setOfflineYield` (Task 3) |
| `packages/retroplug/src/render/` | `pullMs`, the settle shape (Task 2), `discard()` (Task 3), the gate retired (Task 4) |
| `packages/retroplug/src/renderJobs.ts`, `appStores.ts`, `ui/` | the render list in the store graph and the UI on it (Task 3) |
| `packages/retroplug/test-native/` | `engine-run` (Task 1), `render-cases`, `render-baseline`, `render-chunks` and the golden (Task 2), `render-cli` (Task 3) |
| `packages/retroplug/test/`, `test-ui/` | the offline cases (Tasks 1 and 3), the render list and the UI seam (Task 3) |
| `packages/retroplug/scripts/` | `DETERMINISTIC_FILES` and `__RENDER_GOLDEN__` (Tasks 2 and 3), the skip baseline (Tasks 1 to 3) |
| `tools/run-cli-ts-tests.sh`, `tools/fixtures/render-smsggdj.sav` | the real binary's render leg and its battery (Task 3) |
| `.github/workflows/build.yml` | `RP_RENDER_GOLDEN_REQUIRE_TARGET` on CI's linux-x64 `test:native` (Task 2) |
| `docs/`, `spec/`, `AGENTS.md`, `README.md` | the design's as-built record and the live docs (Task 5; AGENTS.md's SameBoy paragraph in Task 1) |

---

### Task 1: engine jobs run a render on the offline pool

Design section 3 and section 7's first commit. The pool runs a second kind of job: `submitEngine({kind, spec})` queues a
render, a worker builds a `RenderHost` on its own stack, runs the spec through it with the job's console routed into a
bounded record and its progress reported, and destroys it there before taking another job. Engine jobs hold at most all
but one of the workers. The emulator facet gains `submitEngineJob`, and the drain hands back the engine half beside D2's
graph completions. TypeScript gets `backend.engineRun` and a `cancel()` on both kinds' handles. Renders keep running
through `RenderJobRegistry` and the CLI's in-process path; nothing switches yet.

Before the pool can run two cores on two threads, the process-wide state they share has to be safe. ThreadSanitizer
found Mesen's `SimpleLock` race when a NES and an SMS core booted at once. SameBoy's three statics came from reading the
core, and a TSan build of the render tool showed two of them red and the patch green: the generator (`data race ...
deps/sameboy/Core/random.c:11 in GB_random`, two mGB jobs and an LSDj one) and the SGB table (`data race ...
Core/sgb.c:849 in fm_sin`, two Super Game Boy renders). Only the camera seed has no red/green, since no Game Boy
Camera ROM exists here. Those runs needed a non-deterministic tool build and are evidence here, not steps; the steps
below place the fixes and prove the pool on top of them.

**Files:**

- Create: `cmake/patches/sameboy-process-statics.patch`, `packages/native/test/engine-graph/EngineJobs.test.cpp`, `packages/retroplug/test-native/engine-run.test.ts`
- Modify: `AGENTS.md`, `cmake/sameboy.cmake`, `deps/mesen/Utilities/SimpleLock.cpp`, `deps/mesen/Utilities/SimpleLock.h`, `packages/native/cmake/tests.cmake`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/engine/OfflineJobs.hpp`, `packages/native/src/host/engine/OfflineTypes.hpp`, `packages/native/src/host/render/RenderHost.cpp`, `packages/native/src/host/render/RenderHost.hpp`, `packages/native/src/host/rpc/BackendRpcRegistration.hpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/retroplug/scripts/skip-baseline.json`, `packages/retroplug/src/backend.ts`, `packages/retroplug/src/kit/compile.ts`, `packages/retroplug/src/offline.ts`, `packages/retroplug/src/realBackend.ts`, `packages/retroplug/test/offline/offline.test.ts`, `packages/retroplug/testing/mockBackend.ts`

- [ ] **Step 1: The process-wide state a render touches**

`SimpleLock` keeps its holder thread id and its count in relaxed atomics, the count changed by a relaxed load and
store (only the holder writes it), with a `static_assert` that each is always lock-free. The new tracked SameBoy patch
makes `Core/random.c`'s seed and enabled flag and `Core/camera.c`'s noise seed relaxed atomics through the GNU
`__atomic` builtins and builds `Core/sgb.c`'s quarter-wave table in a load-time constructor; single-threaded values
are unchanged. `cmake/sameboy.cmake` applies a list of patches, each idempotently and each failing the configure
loudly if it no longer applies, and `AGENTS.md`'s `deps/sameboy` paragraph names both patches and the five files a
configure leaves modified.

Create `cmake/patches/sameboy-process-statics.patch`:

````
diff --git a/Core/camera.c b/Core/camera.c
index d6e6144..b51c8f9 100644
--- a/Core/camera.c
+++ b/Core/camera.c
@@ -1,5 +1,7 @@
 #include "gb.h"
 
+/* RetroPlug: process-wide, and written on a shoot while another thread's camera may read it, so every access is a
+   relaxed atomic (it is noise: a lost update only repeats a pattern). */
 static uint32_t noise_seed = 0;
 
 /* This is not a complete emulation of the camera chip. Only the features used by the Game Boy Camera ROMs are supported.
@@ -7,7 +9,7 @@ static uint32_t noise_seed = 0;
 
 static uint8_t generate_noise(uint8_t x, uint8_t y)
 {
-    uint32_t value = (x * 151 + y * 149) ^ noise_seed;
+    uint32_t value = (x * 151 + y * 149) ^ __atomic_load_n(&noise_seed, __ATOMIC_RELAXED);
     uint32_t hash = 0;
 
     while (value) {
@@ -140,7 +142,7 @@ void GB_camera_write_register(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
     addr &= 0x7F;
     if (addr == GB_CAMERA_SHOOT_AND_1D_FLAGS) {
         value &= 0x7;
-        noise_seed = GB_random();
+        __atomic_store_n(&noise_seed, GB_random(), __ATOMIC_RELAXED);
         if ((value & 1) && !(gb->camera_registers[GB_CAMERA_SHOOT_AND_1D_FLAGS] & 1)) {
             if (gb->camera_update_request_callback) {
                 gb->camera_update_request_callback(gb);
diff --git a/Core/random.c b/Core/random.c
index cc4d4d3..e31d5d2 100644
--- a/Core/random.c
+++ b/Core/random.c
@@ -1,37 +1,45 @@
 #include "random.h"
 #include <time.h>
 
+/* RetroPlug: the generator is process-wide, and a host runs Game Boys on several threads at once (an engine job's
+   render beside another, or beside the live engine's control and audio threads), so every access to it is a relaxed
+   atomic. It runs at every reset (power-on RAM) and during play too (the Super Game Boy jingle's noise, a camera
+   shoot). Two callers racing may lose an update or draw the same value, which only makes the noise less random; what
+   they must not do is race as plain memory, which is undefined behaviour. The core is built by GCC or Clang (clang-cl
+   on Windows), so the GNU __atomic builtins are available everywhere it compiles. */
 static uint64_t seed;
 static bool enabled = true;
 
 uint8_t GB_random(void)
 {
-    if (!enabled) return 0;
+    if (!__atomic_load_n(&enabled, __ATOMIC_RELAXED)) return 0;
     
-    seed *= 0x27BB2EE687B0B0FDL;
-    seed += 0xB504F32D;
-    return seed >> 56;
+    uint64_t next = __atomic_load_n(&seed, __ATOMIC_RELAXED);
+    next *= 0x27BB2EE687B0B0FDL;
+    next += 0xB504F32D;
+    __atomic_store_n(&seed, next, __ATOMIC_RELAXED);
+    return next >> 56;
 }
 
 uint32_t GB_random32(void)
 {
     GB_random();
-    return seed >> 32;
+    return __atomic_load_n(&seed, __ATOMIC_RELAXED) >> 32;
 }
 
 void GB_random_seed(uint64_t new_seed)
 {
-    seed = new_seed;
+    __atomic_store_n(&seed, new_seed, __ATOMIC_RELAXED);
 }
 
 void GB_random_set_enabled(bool enable)
 {
-    enabled = enable;
+    __atomic_store_n(&enabled, enable, __ATOMIC_RELAXED);
 }
 
 static void __attribute__((constructor)) init_seed(void)
 {
-    seed = time(NULL);
+    __atomic_store_n(&seed, (uint64_t)time(NULL), __ATOMIC_RELAXED);
     for (unsigned i = 64; i--;) {
         GB_random();
     }
diff --git a/Core/sgb.c b/Core/sgb.c
index 61ad2c5..359379e 100644
--- a/Core/sgb.c
+++ b/Core/sgb.c
@@ -831,9 +831,21 @@ void GB_sgb_load_default_data(GB_gameboy_t *gb)
     gb->sgb->effective_palettes[3] = LE16(built_in_palettes[3]);
 }
 
+/* RetroPlug: the quarter-wave table fm_sin reads is built once, at load, as apu.c's band_limited_init builds its
+   steps, rather than on first use: a host runs Super Game Boy cores on several threads at once, and two first jingles
+   filling a lazily built table would race on it. The values are the ones the lazy fill computed. */
+#define SIN_TABLE_LENGTH 128
+static double fm_sin_table[SIN_TABLE_LENGTH + 1];
+
+static void __attribute__((constructor)) fm_sin_init(void)
+{
+    for (unsigned i = 0; i < SIN_TABLE_LENGTH + 1; i++) {
+        fm_sin_table[i] = sin(i * M_PI / 2 / SIN_TABLE_LENGTH);
+    }
+}
+
 static double fm_sin(double phase)
 {
-#define SIN_TABLE_LENGTH 128
     phase /= 2 * M_PI;
     phase = fabs(phase);
     phase -= floor(phase);
@@ -844,18 +856,9 @@ static double fm_sin(double phase)
         return fm_sin(0.5 - phase);
     }
     
-    static bool once = false;
-    static double table[SIN_TABLE_LENGTH + 1];
-    if (!once) {
-        for (unsigned i = 0; i < SIN_TABLE_LENGTH + 1; i++) {
-            table[i] = sin(i * M_PI / 2 / SIN_TABLE_LENGTH);
-        }
-        once = true;
-    }
-    
     phase *= 4 * SIN_TABLE_LENGTH;
     double fraction = phase - floor(phase);
-    return table[(unsigned)floor(phase)] * (1 - fraction) + table[(unsigned)ceil(phase)] * (fraction);
+    return fm_sin_table[(unsigned)floor(phase)] * (1 - fraction) + fm_sin_table[(unsigned)ceil(phase)] * (fraction);
 }
 
 static double fm_synth(double phase)
````

Run: `mkdir -p cmake/patches; sed -n '329,463p' docs/modular-graph-phase-d3-plan.md > cmake/patches/sameboy-process-statics.patch`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object cmake/patches/sameboy-process-statics.patch`
Expected: `1ca6970ffffed435dfa1d68ed143cf906a7ed1f8`: the blob 12414e0b3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 21808e974..dc5787df9 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -124,15 +124,18 @@ The rules below are the parts that don't fit those.
 - **`deps/sameboy` is patched at configure — a dirty working tree there is
   EXPECTED, not stray changes.** The per-channel (4-stem) Game Boy audio tap lives
   in `Core/apu.{c,h}` and ships as a tracked patch
-  ([cmake/patches/sameboy-per-channel-audio.patch](cmake/patches/sameboy-per-channel-audio.patch)),
-  applied idempotently at configure by
+  ([cmake/patches/sameboy-per-channel-audio.patch](cmake/patches/sameboy-per-channel-audio.patch));
+  a second, [cmake/patches/sameboy-process-statics.patch](cmake/patches/sameboy-process-statics.patch),
+  makes the core's process-wide statics safe for cores on several threads at once
+  (`Core/random.c`, `Core/camera.c`, `Core/sgb.c`). Both are applied idempotently at configure by
   [cmake/sameboy.cmake](cmake/sameboy.cmake) via `git apply` (it uses
   `git apply --reverse --check` to no-op when already present). The parent repo
   tracks only the pinned submodule pointer, so after any configure `git status`
-  shows `deps/sameboy` with modified `Core/apu.c` / `Core/apu.h` — **don't
+  shows `deps/sameboy` with modified `Core/apu.c` / `Core/apu.h` / `Core/random.c` /
+  `Core/camera.c` / `Core/sgb.c` (all five expected): **don't
   reset / stash / commit that, and don't bump the pointer to "absorb" it.** A
-  submodule bump that invalidates the patch fails LOUDLY at configure (rather than
-  silently dropping per-channel output); regenerate the patch if that happens.
+  submodule bump that invalidates a patch fails LOUDLY at configure (rather than
+  silently dropping what it adds); regenerate the patch if that happens.
 - **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
   `build/` is load-bearing for the dev loop.
 - **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
diff --git a/cmake/sameboy.cmake b/cmake/sameboy.cmake
index 141be1224..b9f63e8cd 100644
--- a/cmake/sameboy.cmake
+++ b/cmake/sameboy.cmake
@@ -9,44 +9,54 @@ if(NOT EXISTS "${SAMEBOY_DIR}/Core/gb.h")
         "deps/sameboy is empty. Run: git submodule update --init --recursive")
 endif()
 
-# --- RetroPlug per-channel audio tap ----------------------------------------
+# --- RetroPlug patches --------------------------------------------------------
 # deps/sameboy is a pinned submodule; the parent repo tracks only its commit
-# pointer. The per-channel (4-stem) Game Boy audio tap lives in Core/apu.{c,h}
-# and is shipped as a tracked patch here, applied idempotently at configure so a
-# fresh clone gets it without bumping the submodule pointer. A future submodule
-# bump that invalidates the patch fails LOUDLY at configure rather than silently
-# dropping per-channel output.
-set(_SAMEBOY_PATCH "${CMAKE_SOURCE_DIR}/cmake/patches/sameboy-per-channel-audio.patch")
-set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_SAMEBOY_PATCH}")
+# pointer. RetroPlug's changes to the core ship as tracked patches here, applied
+# idempotently at configure so a fresh clone gets them without bumping the
+# submodule pointer:
+#   - sameboy-per-channel-audio.patch: the per-channel (4-stem) Game Boy audio tap
+#     in Core/apu.{c,h}.
+#   - sameboy-process-statics.patch: the core's process-wide mutable statics made
+#     safe for Game Boys run on several threads at once (engine jobs' renders, the
+#     live engine's control and audio threads): Core/random.c's generator and
+#     Core/camera.c's noise seed read and written as relaxed atomics, and
+#     Core/sgb.c's jingle sine table built by a load-time constructor instead of
+#     on first use.
+# A future submodule bump that invalidates a patch fails LOUDLY at configure
+# rather than silently dropping what it adds.
 find_package(Git QUIET)
 if(NOT GIT_FOUND)
-    message(FATAL_ERROR "git is required to apply the SameBoy per-channel patch (${_SAMEBOY_PATCH})")
+    message(FATAL_ERROR "git is required to apply the SameBoy patches (cmake/patches/sameboy-*.patch)")
 endif()
-# `git apply --reverse --check` succeeds only when the patch is already present.
-execute_process(
-    COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply --reverse --check "${_SAMEBOY_PATCH}"
-    RESULT_VARIABLE _SAMEBOY_PATCH_PRESENT
-    OUTPUT_QUIET ERROR_QUIET)
-if(_SAMEBOY_PATCH_PRESENT EQUAL 0)
-    message(STATUS "SameBoy: per-channel audio tap patch already present")
-else()
-    execute_process(
-        COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply --check "${_SAMEBOY_PATCH}"
-        RESULT_VARIABLE _SAMEBOY_PATCH_OK
-        OUTPUT_VARIABLE _SAMEBOY_PATCH_MSG ERROR_VARIABLE _SAMEBOY_PATCH_MSG)
-    if(NOT _SAMEBOY_PATCH_OK EQUAL 0)
-        message(FATAL_ERROR
-            "SameBoy per-channel patch no longer applies to deps/sameboy (did the "
-            "submodule move?). Regenerate ${_SAMEBOY_PATCH}.\n${_SAMEBOY_PATCH_MSG}")
-    endif()
+foreach(_SAMEBOY_PATCH_NAME sameboy-per-channel-audio sameboy-process-statics)
+    set(_SAMEBOY_PATCH "${CMAKE_SOURCE_DIR}/cmake/patches/${_SAMEBOY_PATCH_NAME}.patch")
+    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_SAMEBOY_PATCH}")
+    # `git apply --reverse --check` succeeds only when the patch is already present.
     execute_process(
-        COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply "${_SAMEBOY_PATCH}"
-        RESULT_VARIABLE _SAMEBOY_PATCH_APPLIED)
-    if(NOT _SAMEBOY_PATCH_APPLIED EQUAL 0)
-        message(FATAL_ERROR "Failed to apply SameBoy per-channel patch ${_SAMEBOY_PATCH}")
+        COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply --reverse --check "${_SAMEBOY_PATCH}"
+        RESULT_VARIABLE _SAMEBOY_PATCH_PRESENT
+        OUTPUT_QUIET ERROR_QUIET)
+    if(_SAMEBOY_PATCH_PRESENT EQUAL 0)
+        message(STATUS "SameBoy: ${_SAMEBOY_PATCH_NAME} patch already present")
+    else()
+        execute_process(
+            COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply --check "${_SAMEBOY_PATCH}"
+            RESULT_VARIABLE _SAMEBOY_PATCH_OK
+            OUTPUT_VARIABLE _SAMEBOY_PATCH_MSG ERROR_VARIABLE _SAMEBOY_PATCH_MSG)
+        if(NOT _SAMEBOY_PATCH_OK EQUAL 0)
+            message(FATAL_ERROR
+                "SameBoy ${_SAMEBOY_PATCH_NAME} patch no longer applies to deps/sameboy (did the "
+                "submodule move?). Regenerate ${_SAMEBOY_PATCH}.\n${_SAMEBOY_PATCH_MSG}")
+        endif()
+        execute_process(
+            COMMAND "${GIT_EXECUTABLE}" -C "${SAMEBOY_DIR}" apply "${_SAMEBOY_PATCH}"
+            RESULT_VARIABLE _SAMEBOY_PATCH_APPLIED)
+        if(NOT _SAMEBOY_PATCH_APPLIED EQUAL 0)
+            message(FATAL_ERROR "Failed to apply SameBoy ${_SAMEBOY_PATCH_NAME} patch ${_SAMEBOY_PATCH}")
+        endif()
+        message(STATUS "SameBoy: applied ${_SAMEBOY_PATCH_NAME} patch")
     endif()
-    message(STATUS "SameBoy: applied per-channel audio tap patch")
-endif()
+endforeach()
 
 file(STRINGS "${SAMEBOY_DIR}/version.mk" _SAMEBOY_VERSION_RAW
      REGEX "^VERSION[ \t]*:=[ \t]*")
diff --git a/deps/mesen/Utilities/SimpleLock.cpp b/deps/mesen/Utilities/SimpleLock.cpp
index 8ec43ead8..4b96ada1b 100644
--- a/deps/mesen/Utilities/SimpleLock.cpp
+++ b/deps/mesen/Utilities/SimpleLock.cpp
@@ -5,11 +5,15 @@
 
 thread_local std::thread::id SimpleLock::_threadID = std::this_thread::get_id();
 
+// RetroPlug: the holder check is a plain load on every target, never a hidden lock.
+static_assert(std::atomic<std::thread::id>::is_always_lock_free, "SimpleLock's holder id must be a lock-free atomic");
+static_assert(std::atomic<uint32_t>::is_always_lock_free, "SimpleLock's count must be a lock-free atomic");
+
 SimpleLock::SimpleLock()
 {
 	_lock.clear();
-	_lockCount = 0;
-	_holderThreadID = std::thread::id();
+	_lockCount.store(0, std::memory_order_relaxed);
+	_holderThreadID.store(std::thread::id(), std::memory_order_relaxed);
 }
 
 SimpleLock::~SimpleLock()
@@ -23,13 +27,13 @@ LockHandler SimpleLock::AcquireSafe()
 
 void SimpleLock::Acquire()
 {
-	if(_lockCount == 0 || _holderThreadID != _threadID) {
+	if(_lockCount.load(std::memory_order_relaxed) == 0 || _holderThreadID.load(std::memory_order_relaxed) != _threadID) {
 		WaitForAcquire(0);
-		_holderThreadID = _threadID;
-		_lockCount = 1;
+		_holderThreadID.store(_threadID, std::memory_order_relaxed);
+		_lockCount.store(1, std::memory_order_relaxed);
 	} else {
 		//Same thread can acquire the same lock multiple times
-		_lockCount++;
+		_lockCount.store(_lockCount.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
 	}
 }
 
@@ -53,15 +57,15 @@ bool SimpleLock::WaitForAcquire(uint32_t msTimeout)
 
 bool SimpleLock::TryAcquire(uint32_t msTimeout)
 {
-	if(_lockCount == 0 || _holderThreadID != _threadID) {
+	if(_lockCount.load(std::memory_order_relaxed) == 0 || _holderThreadID.load(std::memory_order_relaxed) != _threadID) {
 		if(!WaitForAcquire(msTimeout)) {
 			return false;
 		}
-		_holderThreadID = _threadID;
-		_lockCount = 1;
+		_holderThreadID.store(_threadID, std::memory_order_relaxed);
+		_lockCount.store(1, std::memory_order_relaxed);
 	} else {
 		//Same thread can acquire the same lock multiple times
-		_lockCount++;
+		_lockCount.store(_lockCount.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
 	}
 
 	return true;
@@ -69,12 +73,12 @@ bool SimpleLock::TryAcquire(uint32_t msTimeout)
 
 bool SimpleLock::IsFree()
 {
-	return _lockCount == 0;
+	return _lockCount.load(std::memory_order_relaxed) == 0;
 }
 
 bool SimpleLock::IsLockedByCurrentThread()
 {
-	return _lockCount > 0 && _holderThreadID == _threadID;
+	return _lockCount.load(std::memory_order_relaxed) > 0 && _holderThreadID.load(std::memory_order_relaxed) == _threadID;
 }
 
 void SimpleLock::WaitForRelease()
@@ -87,9 +91,10 @@ void SimpleLock::WaitForRelease()
 void SimpleLock::Release()
 {
 	if(IsLockedByCurrentThread()) {
-		_lockCount--;
-		if(_lockCount == 0) {
-			_holderThreadID = std::thread::id();
+		const uint32_t count = _lockCount.load(std::memory_order_relaxed) - 1;
+		_lockCount.store(count, std::memory_order_relaxed);
+		if(count == 0) {
+			_holderThreadID.store(std::thread::id(), std::memory_order_relaxed);
 			_lock.clear();
 		}
 	} else {
diff --git a/deps/mesen/Utilities/SimpleLock.h b/deps/mesen/Utilities/SimpleLock.h
index 685808dd7..55277d441 100644
--- a/deps/mesen/Utilities/SimpleLock.h
+++ b/deps/mesen/Utilities/SimpleLock.h
@@ -1,5 +1,6 @@
 #pragma once 
 #include "pch.h"
+#include <atomic>
 #include <thread>
 
 class SimpleLock;
@@ -20,8 +21,14 @@ class SimpleLock
 private:
 	thread_local static std::thread::id _threadID;
 
-	std::thread::id _holderThreadID;
-	uint32_t _lockCount;
+	// RetroPlug: the holder and its count are atomics, read relaxed, because every thread that acquires reads them to
+	// learn whether it already holds the lock while the holder may be writing them, which is a data race on plain
+	// members (ThreadSanitizer saw it on MessageManager's process-wide lock, two cores built on two threads at once).
+	// Relaxed is enough: only the holder writes them (so the count is a load and a store, not a locked read-modify-
+	// write), a thread's own writes are visible to it, and the id another thread writes is never the reader's own;
+	// what the lock protects is ordered by _lock's test_and_set and clear.
+	std::atomic<std::thread::id> _holderThreadID;
+	std::atomic<uint32_t> _lockCount;
 	atomic_flag _lock;
 	
 	bool WaitForAcquire(uint32_t msTimeout);
````

Run: `sed -n '473,708p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0, with `SameBoy: applied sameboy-process-statics patch` (or `SameBoy: sameboy-process-statics patch already present` where an earlier configure of the prototype left the three files patched) beside the per-channel patch's `already present`.

Run: `cmake -S . -B build`
Expected: exit 0, and both patches `already present`: the list is idempotent.

Run: `git -C deps/sameboy status --short`
Expected: these five lines, the two patches' files, and nothing else:

````
 M Core/apu.c
 M Core/apu.h
 M Core/camera.c
 M Core/random.c
 M Core/sgb.c
````

Run: `cmake --build build --target retroplug-host -j$(nproc)`
Expected: exit 0. The vendored Mesen recompiles whole (the report: "Changing `SimpleLock.h` recompiles all of mesen."), and if the configure printed `applied`, the three SameBoy sources recompile patched (where the patch was already present, nothing in SameBoy recompiles).

- [ ] **Step 2: Write the failing engine-job cases**

`EngineJobs.test.cpp` (tags `[engine-jobs]` and `[engine-jobs-hff]`) joins `retroplug-engine-graph-test`. Its cases: a
render job's WAV is a direct `RenderHost` run's byte for byte, its progress rising and its lines arriving; a sounding
LSDj render the same (fixture-gated); an auto-detected render lowers its count once, to the length it wrote
(`[engine-jobs-hff]`, fixture-gated, apart from the sanitizer legs because it takes about 12 s); a cancel ends a job
queued at once and running at its next chunk, one that lands as a render starts within a chunk of the cancel, and one
taken after the body finished still wins, keeping the files; an engine destroyed with renders in flight cancels and
joins every worker; renders hold all but one worker so a graph job still runs beside them, at budget 3 and 1, and the
ones held back start in submission order; an engine job computes in the default floating-point mode when its submitter
flushes subnormals; renders run back to back on one reused worker and in parallel; the console record keeps its
bounds; and the RPCs carry all of it and refuse an unknown kind. Several cases give the pool an `EngineJobBody` of
their own, to hold its rules to an exact timeline. The CMake hunk adds the source, so it lands here, before the code
it tests.

Create `packages/native/test/engine-graph/EngineJobs.test.cpp`:

````cpp
// Engine jobs (docs/modular-graph-phase-d3-design.md section 3, host/engine/OfflineJobs.hpp): a render run on a
// worker of the engine's offline pool by a RenderHost the worker builds, runs and destroys. A render job writes the
// WAV a RenderHost run directly writes for the same spec, byte for byte, reporting its progress (rising chunk by
// chunk to the rendered length on a fixed-length render) and its console lines as it goes; a cancel stops it, queued,
// starting or running, and one taken after its body finished still wins, keeping the files written; an engine
// destroyed with renders in flight cancels and joins them; renders hold at most all but one of the pool's workers, so
// a graph job still runs beside them (a pool of 1 starting its second thread for one), and the ones held back start
// in submission order; an engine job computes in the default floating-point mode; renders run back to back on one
// reused worker and in parallel on several, each giving the bytes a direct run gives; the console record keeps its
// bounds; the RPCs carry all of it; and a kind that is not "render" is refused.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[engine-jobs]"`. The ROMs are
// the repo's own (resources/roms), which render silence through the render library with no song or MIDI, so on CI the
// job-against-direct comparison pins the WAV's header and length; the case that needs a sounding render reads an LSDj
// song from the fixture tree (RETROPLUG_RESOURCES_DIR) and skips without it. A few cases give the pool a body of their
// own (EngineJobBody) to hold its rules to an exact timeline. Every core in this binary powers on in a fixed state
// (Deterministic.cpp), so two renders of one spec agree, and SameBoy's power-on generator is off.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/engine/OfflineJobs.hpp"
#include "host/render/RenderHost.hpp"
#include "host/rpc/EngineRpcService.hpp"

using rp::host::EngineJobBody;
using rp::host::EngineJobCompletion;
using rp::host::EngineJobHooks;
using rp::host::EngineJobOutcome;
using rp::host::EngineJobRequest;
using rp::host::EngineJobLine;
using rp::host::EngineJobProgress;
using rp::host::EngineJobRecord;
using rp::host::EngineJobsDrain;
using rp::host::EngineJobStream;
using rp::host::OfflineJobs;
using rp::host::OfflineRequest;
using rp::host::OfflineSamples;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("rp-engine-jobs-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

std::vector<char> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int threadCount() {
#if defined(__linux__)
    int n = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator("/proc/self/task")) ++n;
    return n;
#else
    return -1;
#endif
}

// A render job's spec: `rom` rendered for `durationMs` into `out`, plus `extra` fields (a leading comma each).
std::string renderSpec(const std::string& rom, const std::string& out, int durationMs, const std::string& extra = "") {
    return R"({"rom":")" + rom + R"(","out":")" + out + R"(","durationMs":)" + std::to_string(durationMs) + extra + "}";
}

// Ten minutes of audio: a render that runs for many seconds of wall time, which the cases cancel.
std::string longSpec(const TempDir& dir, const std::string& name) {
    return renderSpec(RP_MGB_ROM_PATH, dir.file(name), 600000);
}

// The fixture tree's path for `relative`, or empty when it is not there (as CI runs).
std::string fixture(const char* relative) {
    const char* root = std::getenv("RETROPLUG_RESOURCES_DIR");
    if (!root) return {};
    const std::string path = std::string(root) + relative;
    return fs::exists(path) ? path : std::string();
}

// What a RenderHost run directly on this thread gives for `spec`: its result, and its console lines, collected rather
// than printed.
struct Direct {
    retroplug::RenderHost::Result result;
    std::vector<std::string>      lines;
};
Direct renderDirectly(const std::string& spec) {
    Direct                d;
    retroplug::RenderHost host;
    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); });
    return d;
}

// What the drains reported for each job: its completion, and every progress report before it in order.
struct Seen {
    std::map<std::uint32_t, EngineJobCompletion>            done;
    std::map<std::uint32_t, std::vector<EngineJobProgress>> progress;
};

// Drain `drain` until every job in `ids` has completed (or five minutes have passed, a sanitizer build being slow),
// polling every millisecond so a running render's progress is seen many times.
template <class Drain>
Seen collect(Drain&& drain, const std::vector<std::uint32_t>& ids) {
    Seen       seen;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        drain(d);
        for (auto& p : d.progress) seen.progress[p.id].push_back(std::move(p));
        for (auto& c : d.completions) seen.done[c.id] = std::move(c);
        if (std::all_of(ids.begin(), ids.end(), [&](std::uint32_t id) { return seen.done.count(id) != 0; })) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return seen;
}

Seen collect(OfflineJobs& jobs, const std::vector<std::uint32_t>& ids) {
    return collect([&](EngineJobsDrain& d) { jobs.drain(std::chrono::milliseconds(0), nullptr, &d); }, ids);
}

// Wait until job `id` has reported some audio rendered: it is running, past its boot. False after `limit`.
bool waitRendering(OfflineJobs& jobs, std::uint32_t id, Seen& seen,
                   std::chrono::seconds limit = std::chrono::seconds(60)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        jobs.drain(std::chrono::milliseconds(0), nullptr, &d);
        bool rendering = false;
        for (auto& p : d.progress) {
            if (p.id == id && p.renderedMs > 0) rendering = true;
            seen.progress[p.id].push_back(std::move(p));
        }
        for (auto& c : d.completions) seen.done[c.id] = std::move(c);
        if (rendering) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Every line the job wrote, progress reports' and completion's, in order.
std::vector<EngineJobLine> linesOf(const Seen& seen, std::uint32_t id) {
    std::vector<EngineJobLine> lines;
    if (auto it = seen.progress.find(id); it != seen.progress.end())
        for (const auto& p : it->second) lines.insert(lines.end(), p.lines.begin(), p.lines.end());
    const auto& c = seen.done.at(id);
    lines.insert(lines.end(), c.lines.begin(), c.lines.end());
    return lines;
}

// A graph job that runs no document and returns its one input: done as soon as a worker takes it.
OfflineRequest echoRequest() {
    OfflineRequest r;
    r.inputs.emplace_back(OfflineSamples{{0.25f, -0.5f, 0.75f}, 1, 48000.0});
    r.returns = {1};
    r.frames = 0;
    r.rate = 48000.0;
    return r;
}

// Submit and wait for one graph job's completion, which must come while renders run.
rp::host::OfflineCompletion graphJobBesideRenders(OfflineJobs& jobs) {
    const auto s = jobs.submit(echoRequest());
    INFO(s.error);
    REQUIRE(s.id != 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& c : jobs.drain(std::chrono::milliseconds(50)))
            if (c.id == s.id) return c;
    }
    FAIL("the graph job did not complete while renders held the pool");
    return {};
}

// Turns on flush-to-zero (and denormals-are-zero) on this thread; false on a target this test cannot set it on.
bool setFlushToZero() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_setcsr(_mm_getcsr() | 0x8040u);  // MXCSR FTZ (bit 15) and DAZ (bit 6)
    return true;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= std::uint64_t(1) << 24;  // FPCR.FZ
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
    return true;
#else
    return false;
#endif
}

// Whether a subnormal survives a multiply by one on this thread: false under flush-to-zero.
bool keepsSubnormals() {
    volatile float tiny = 1.0e-39f;
    volatile float one = 1.0f;
    return std::fpclassify(tiny * one) != FP_ZERO;
}

// Spin until `flag` is set or ten seconds pass; whether it was set.
bool waitFor(const std::atomic<bool>& flag) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!flag.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return flag.load(std::memory_order_acquire);
}

std::string text(const std::vector<EngineJobLine>& lines) {
    std::string s;
    for (const auto& l : lines) s += (l.stream == EngineJobStream::Err ? "[err] " : "") + l.text + "\n";
    return s;
}

}  // namespace

TEST_CASE("an engine job renders the bytes a direct RenderHost run renders, reporting its progress and its lines",
          "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs;
    // Ten seconds of audio, long enough that the drains see its progress several times on the way.
    const std::string out = dir.file("out.wav");
    const std::string spec = renderSpec(RP_MGB_ROM_PATH, out, 10000);
    const auto        s = jobs.submitEngine({"render", spec});
    INFO(s.error);
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    const auto& c = seen.done.at(s.id);
    const auto  lines = linesOf(seen, s.id);
    INFO(c.message << "\n" << text(lines));
    REQUIRE(c.status == "done");
    CHECK(c.message.empty());
    REQUIRE(c.outputs == std::vector<std::string>{out});
    const auto jobBytes = readBytes(out);
    CHECK(jobBytes.size() == 44 + std::size_t(441000) * 2 * 2);  // ten seconds of 16-bit stereo at 44.1 kHz

    // The same spec run directly, over the same file: the bytes agree, the WAV header included, and so do the console
    // lines, which the job records without the newline the console ends each with, and drops none of.
    const Direct direct = renderDirectly(spec);
    REQUIRE(direct.result.status == "done");
    CHECK(jobBytes == readBytes(out));
    std::vector<std::string> expected;
    for (std::string l : direct.lines) {
        if (!l.empty() && l.back() == '\n') l.pop_back();
        expected.push_back(l);
    }
    std::vector<std::string> got;
    for (const auto& l : lines) got.push_back(l.text);
    CHECK(got == expected);
    CHECK(std::any_of(lines.begin(), lines.end(), [](const EngineJobLine& l) {
        return l.stream == EngineJobStream::Out && l.text.rfind("rendering ", 0) == 0;
    }));
    CHECK(c.droppedLines == 0);

    // Progress: seen while it ran, never going back, and the completion ends at the length rendered.
    const auto& reports = seen.progress.count(s.id) ? seen.progress.at(s.id) : std::vector<EngineJobProgress>{};
    CHECK(reports.size() >= 2);
    double last = 0.0;
    for (const auto& p : reports) {
        CHECK(p.renderedMs >= last);
        CHECK(p.droppedLines == 0);
        last = p.renderedMs;
    }
    CHECK(last > 0.0);
    CHECK(c.renderedMs >= last);
    CHECK(c.renderedMs == 10000.0);
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("a sounding render as an engine job is the direct run's, byte for byte", "[engine-graph][engine-jobs]") {
    const std::string rom = fixture("/roms/lsdj942telemelt_5songs.gbc");
    const std::string sav = fixture("/roms/lsdj942telemelt_5songs.sav");
    if (rom.empty() || sav.empty()) SKIP("the LSDj song fixture is not present (RETROPLUG_RESOURCES_DIR)");
    TempDir           dir;
    OfflineJobs       jobs;
    const std::string extra = R"(,"sav":")" + sav + R"(","songIndex":0)";
    const auto        s = jobs.submitEngine({"render", renderSpec(rom, dir.file("job.wav"), 3000, extra)});
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    INFO(seen.done.at(s.id).message);
    REQUIRE(seen.done.at(s.id).status == "done");
    REQUIRE(renderDirectly(renderSpec(rom, dir.file("direct.wav"), 3000, extra)).result.status == "done");
    const auto bytes = readBytes(dir.file("job.wav"));
    CHECK(bytes == readBytes(dir.file("direct.wav")));
    // And it is not silence: some 16-bit sample past the header is not zero.
    bool sound = false;
    for (std::size_t i = 44; i + 1 < bytes.size() && !sound; i += 2) sound = bytes[i] != 0 || bytes[i + 1] != 0;
    CHECK(sound);
}

TEST_CASE("an auto-detected render lowers its count once, at its end, to the length it wrote",
          "[engine-graph][engine-jobs-hff]") {
    // Tagged apart from [engine-jobs]: a song to its HFF is three minutes of audio, about twelve seconds of wall time,
    // which a sanitizer build multiplies past what its legs budget. The drains report the render's count chunk by
    // chunk, the chunks it holds back past the stop included; the completion's is the length it wrote, which the
    // count dropped to.
    const std::string rom = fixture("/roms/lsdj942telemelt_5songs.gbc");
    const std::string sav = fixture("/roms/lsdj942telemelt_5songs.sav");
    if (rom.empty() || sav.empty()) SKIP("the LSDj song fixture is not present (RETROPLUG_RESOURCES_DIR)");
    TempDir           dir;
    OfflineJobs       jobs;
    const std::string spec = R"({"rom":")" + rom + R"(","sav":")" + sav + R"(","out":")" + dir.file("hff.wav") +
                             R"(","songIndex":3})";
    const auto s = jobs.submitEngine({"render", spec});
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    const auto& c = seen.done.at(s.id);
    INFO(c.message);
    REQUIRE(c.status == "done");
    CHECK(c.renderedMs == 191700.0);  // MACHETE to its HFF
    // Rising while it runs, then down once to the length (the render reports the length before it ends, so the drop
    // can come in a progress report as well as in the completion), and never again.
    double most = 0.0, last = 0.0;
    int    drops = 0;
    for (const auto& p : seen.progress.at(s.id)) {
        if (p.renderedMs < last) ++drops;
        CHECK((p.renderedMs >= last || p.renderedMs == c.renderedMs));
        last = p.renderedMs;
        most = std::max(most, last);
    }
    if (c.renderedMs < last) ++drops;
    CHECK(drops <= 1);
    // Whether a drain caught a count above the length depends on when the polls fell (the window is the few chunks
    // held past the stop), so only that it never fell below is held here; the mock tier pins the drop exactly.
    INFO("most counted " << most << " ms, length " << c.renderedMs << " ms");
    CHECK(most >= c.renderedMs);
    CHECK(readBytes(dir.file("hff.wav")).size() == 44 + std::size_t(191700) * 441 / 10 * 4);
}

TEST_CASE("a cancel stops an engine job, queued at once and running at its next chunk", "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs({}, 2);  // a budget of one render: the second queues
    REQUIRE(jobs.engineBudget() == 1);
    const auto running = jobs.submitEngine({"render", longSpec(dir, "a.wav")}).id;
    const auto queued = jobs.submitEngine({"render", longSpec(dir, "b.wav")}).id;
    REQUIRE(running != 0);
    REQUIRE(queued != 0);
    // The queued one completes at the cancel, unrun.
    CHECK(jobs.cancel(queued));
    EngineJobsDrain first;
    jobs.drain(std::chrono::milliseconds(0), nullptr, &first);
    REQUIRE(first.completions.size() == 1);
    CHECK(first.completions[0].id == queued);
    CHECK(first.completions[0].status == "cancelled");
    CHECK(first.completions[0].renderedMs == 0.0);
    CHECK(first.completions[0].outputs.empty());
    CHECK_FALSE(jobs.cancel(queued));
    // The running one, once it renders, stops within a chunk of the cancel: ten minutes of audio cut short.
    Seen seen;
    REQUIRE(waitRendering(jobs, running, seen));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(jobs.cancel(running));
    seen = collect(jobs, {running});
    REQUIRE(seen.done.count(running) == 1);
    const auto& c = seen.done.at(running);
    CHECK(c.status == "cancelled");
    CHECK(c.outputs.empty());
    CHECK(c.renderedMs > 0.0);
    CHECK(c.renderedMs < 600000.0);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("an engine destroyed with renders in flight cancels them and joins every worker",
          "[engine-graph][engine-jobs]") {
    TempDir    dir;
    const int  before = threadCount();
    const auto t0 = std::chrono::steady_clock::now();
    {
        Engine e(48000.0);
        // Running renders, and queued ones past the budget.
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 1; ++i)
            REQUIRE(e.submitEngineJob({"render", longSpec(dir, "r" + std::to_string(i) + ".wav")}).id != 0);
        // Until the first is rendering, so the teardown meets renders mid-run rather than still booting.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (bool rendering = false; !rendering && std::chrono::steady_clock::now() < deadline;) {
            EngineJobsDrain d;
            e.drainOfflineJobs(std::chrono::milliseconds(0), nullptr, &d);
            for (const auto& p : d.progress) rendering = rendering || p.renderedMs > 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (before >= 0) CHECK(threadCount() > before);
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60));
    if (before >= 0) CHECK(threadCount() == before);  // every worker joined, every render's runtime gone with it
}

TEST_CASE("renders hold all but one of the pool's workers, so a graph job still runs beside them",
          "[engine-graph][engine-jobs]") {
    TempDir dir;
    SECTION("a pool of three: two renders run, a third queues, and a graph job takes the third worker") {
        OfflineJobs jobs({}, 3);
        CHECK(jobs.engineBudget() == 2);
        std::vector<std::uint32_t> renders;
        for (int i = 0; i < 3; ++i)
            renders.push_back(jobs.submitEngine({"render", longSpec(dir, "r" + std::to_string(i) + ".wav")}).id);
        for (const auto id : renders) REQUIRE(id != 0);
        Seen seen;
        REQUIRE(waitRendering(jobs, renders[0], seen));
        REQUIRE(waitRendering(jobs, renders[1], seen));
        const auto graph = graphJobBesideRenders(jobs);
        INFO(graph.error);
        CHECK(graph.ok);
        REQUIRE(graph.buffers.size() == 1);
        CHECK(graph.buffers[0].samples == std::vector<float>{0.25f, -0.5f, 0.75f});
        CHECK(jobs.workers() == 3);
        // The renders are still where they were: two running (each drain reports their progress), one queued.
        EngineJobsDrain d;
        std::size_t     inFlight = 0;
        jobs.drain(std::chrono::milliseconds(0), &inFlight, &d);
        CHECK(d.completions.empty());
        CHECK(d.progress.size() == 2);
        CHECK(inFlight == 3);
        // Graph jobs past the free worker still run, one after another, while the renders hold theirs.
        for (int i = 0; i < 3; ++i) CHECK(graphJobBesideRenders(jobs).ok);
        CHECK(jobs.workers() == 3);
        for (const auto id : renders) CHECK(jobs.cancel(id));
        seen = collect(jobs, renders);
        for (const auto id : renders) CHECK(seen.done.at(id).status == "cancelled");
    }
    SECTION("a pool of one: the render takes the worker, and a graph job starts the pool's second thread") {
        OfflineJobs jobs({}, 1);
        CHECK(jobs.engineBudget() == 1);
        CHECK(jobs.maxWorkers() == 2);
        const auto render = jobs.submitEngine({"render", longSpec(dir, "one.wav")}).id;
        const auto queued = jobs.submitEngine({"render", longSpec(dir, "two.wav")}).id;
        REQUIRE(render != 0);
        REQUIRE(queued != 0);
        Seen seen;
        REQUIRE(waitRendering(jobs, render, seen));
        CHECK(jobs.workers() == 1);  // the queued render started no thread: the budget holds it back
        CHECK(graphJobBesideRenders(jobs).ok);
        CHECK(jobs.workers() == 2);
        CHECK(jobs.cancel(render));
        CHECK(jobs.cancel(queued));
        seen = collect(jobs, {render, queued});
        CHECK(seen.done.at(render).status == "cancelled");
        CHECK(seen.done.at(queued).status == "cancelled");
        CHECK(jobs.workers() == 2);
    }
}

TEST_CASE("renders run back to back on one reused worker and in parallel on several, each giving a direct run's bytes",
          "[engine-graph][engine-jobs]") {
    TempDir dir;
    // A Game Boy, a NES and an SMS core, and an LSDj song when the fixtures are here: each core's process-wide and
    // thread-local state is met by the renders after it on the same thread, and beside it on others.
    std::vector<std::pair<std::string, std::string>> roms = {
        {"gb", RP_MGB_ROM_PATH}, {"nes", RP_BLIPTOASTER_ROM_PATH}, {"sms", RP_SMS_ROM_PATH}};
    std::vector<std::string> extras(roms.size());
    if (const auto rom = fixture("/roms/lsdj942telemelt_5songs.gbc"); !rom.empty()) {
        if (const auto sav = fixture("/roms/lsdj942telemelt_5songs.sav"); !sav.empty()) {
            roms.push_back({"lsdj", rom});
            extras.push_back(R"(,"sav":")" + sav + R"(","songIndex":0)");
        }
    }
    std::vector<std::vector<char>> reference;
    for (std::size_t i = 0; i < roms.size(); ++i) {
        const auto out = dir.file("ref-" + roms[i].first + ".wav");
        REQUIRE(renderDirectly(renderSpec(roms[i].second, out, 2000, extras[i])).result.status == "done");
        reference.push_back(readBytes(out));
    }
    const auto runAll = [&](OfflineJobs& jobs, const std::string& prefix) {
        std::vector<std::uint32_t> ids;
        for (std::size_t i = 0; i < roms.size(); ++i) {
            const auto out = dir.file(prefix + roms[i].first + ".wav");
            ids.push_back(jobs.submitEngine({"render", renderSpec(roms[i].second, out, 2000, extras[i])}).id);
            REQUIRE(ids.back() != 0);
        }
        const Seen seen = collect(jobs, ids);
        for (std::size_t i = 0; i < roms.size(); ++i) {
            INFO(roms[i].first);
            REQUIRE(seen.done.count(ids[i]) == 1);
            INFO(seen.done.at(ids[i]).message);
            CHECK(seen.done.at(ids[i]).status == "done");
            CHECK(readBytes(dir.file(prefix + roms[i].first + ".wav")) == reference[i]);
        }
    };
    {
        OfflineJobs jobs({}, 1);
        runAll(jobs, "serial-");
        runAll(jobs, "serial-again-");  // the same worker, its second round
        CHECK(jobs.workers() == 1);     // every render ran on the one thread
    }
    {
        OfflineJobs jobs({}, static_cast<unsigned>(roms.size() + 1));  // a budget of every render at once
        runAll(jobs, "parallel-");
        CHECK(jobs.workers() == roms.size());
    }
}

TEST_CASE("a cancel that lands as a render starts ends it within a chunk of the cancel",
          "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs;
    const auto  id = jobs.submitEngine({"render", longSpec(dir, "boot.wav")}).id;
    REQUIRE(id != 0);
    // Cancelled as soon as a drain shows it running, usually before it has reported any audio, while it boots its
    // system: the render's check before each chunk, its first included, ends it there. How far it got before the
    // cancel landed is the scheduler's, so the bound is taken from a drain made right after the cancel: the count
    // only rises, so what that drain sees is at least where the render stood at the cancel, and the render stops
    // before the chunk after the one it was in.
    double     seenBefore = -1.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (seenBefore < 0.0 && std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        jobs.drain(std::chrono::milliseconds(0), nullptr, &d);
        for (const auto& p : d.progress)
            if (p.id == id) seenBefore = p.renderedMs;
    }
    REQUIRE(seenBefore >= 0.0);
    CHECK(jobs.cancel(id));
    EngineJobsDrain after;
    jobs.drain(std::chrono::milliseconds(0), nullptr, &after);
    double atCancel = -1.0;
    for (const auto& p : after.progress)
        if (p.id == id) atCancel = p.renderedMs;
    for (const auto& c : after.completions)
        if (c.id == id) atCancel = c.renderedMs;
    REQUIRE(atCancel >= 0.0);
    Seen seen = collect(jobs, {id});
    for (auto& c : after.completions) seen.done[c.id] = c;
    REQUIRE(seen.done.count(id) == 1);
    const auto& c = seen.done.at(id);
    INFO("rendered when first seen running: " << seenBefore << " ms, just after the cancel: " << atCancel << " ms");
    CHECK(c.status == "cancelled");
    CHECK(c.outputs.empty());
    CHECK(c.renderedMs <= atCancel + 100.0);  // at most the one chunk under way when the cancel landed
    CHECK(c.renderedMs < 600000.0);
}

TEST_CASE("a cancel taken after a job's body finished still ends it cancelled, keeping the files it wrote",
          "[engine-graph][engine-jobs]") {
    // A body of the test's own that has finished its work and waits, so the cancel lands after the end of the run
    // and before the worker publishes it: cancel() answered true, so the outcome is the cancel, and the file a render
    // would have finished writing is still named.
    std::atomic<bool>   finished{false};
    std::atomic<bool>   release{false};
    const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks& hooks) {
        hooks.rendered(1000.0);
        finished.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        return EngineJobOutcome{"done", "", {"/written.wav"}};
    };
    OfflineJobs jobs({}, 2, body);
    const auto  id = jobs.submitEngine({"render", "{}"}).id;
    REQUIRE(id != 0);
    REQUIRE(waitFor(finished));
    CHECK(jobs.cancel(id));
    release.store(true, std::memory_order_release);
    const Seen seen = collect(jobs, {id});
    REQUIRE(seen.done.count(id) == 1);
    CHECK(seen.done.at(id).status == "cancelled");
    CHECK(seen.done.at(id).message.empty());
    CHECK(seen.done.at(id).outputs == std::vector<std::string>{"/written.wav"});
    CHECK(seen.done.at(id).renderedMs == 1000.0);
}

TEST_CASE("renders held back by the budget start in submission order", "[engine-graph][engine-jobs]") {
    std::mutex                 mutex;
    std::vector<std::string>   started;
    std::atomic<bool>          release{false};
    const EngineJobBody body = [&](const EngineJobRequest& r, const EngineJobHooks&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started.push_back(r.spec);
        }
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        return EngineJobOutcome{"done", "", {}};
    };
    OfflineJobs jobs({}, 2, body);  // a budget of one
    std::vector<std::uint32_t> ids;
    for (const char* spec : {"a", "b", "c", "d"}) ids.push_back(jobs.submitEngine({"render", spec}).id);
    // The first holds the budget until every one is queued behind it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (bool first = false; !first && std::chrono::steady_clock::now() < deadline;) {
        std::lock_guard<std::mutex> lock(mutex);
        first = !started.empty();
    }
    release.store(true, std::memory_order_release);
    const Seen seen = collect(jobs, ids);
    for (const auto id : ids) CHECK(seen.done.at(id).status == "done");
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(started == std::vector<std::string>{"a", "b", "c", "d"});
    CHECK(jobs.workers() == 1);  // the held renders started no thread of their own
}

TEST_CASE("an engine job runs in the default floating-point mode whatever its submitter's",
          "[engine-graph][engine-jobs]") {
    // As a graph job: a worker inherits the mode of the thread whose submission started it under glibc, and an audio
    // host commonly flushes subnormals. The body reads the mode it runs in; the probe skips where a new thread does
    // not inherit its creator's mode, since the case would then pass vacuously.
    std::atomic<bool>   kept{false};
    const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks&) {
        kept.store(keepsSubnormals(), std::memory_order_release);
        return EngineJobOutcome{"done", "", {}};
    };
    OfflineJobs jobs({}, 2, body);
    bool        flushable = false;
    bool        inherited = false;
    Seen        seen;
    std::uint32_t id = 0;
    std::thread submitter([&] {
        if (!setFlushToZero()) return;
        flushable = true;
        std::thread probe([&] { inherited = !keepsSubnormals(); });
        probe.join();
        if (!inherited) return;
        id = jobs.submitEngine({"render", "{}"}).id;
        if (id != 0) seen = collect(jobs, {id});
    });
    submitter.join();
    if (!flushable) SKIP("this target has no flush-to-zero mode this test knows how to set");
    if (!inherited) SKIP("a new thread does not inherit the submitter's flush-to-zero mode on this platform");
    REQUIRE(id != 0);
    REQUIRE(seen.done.count(id) == 1);
    CHECK(seen.done.at(id).status == "done");
    CHECK(kept.load(std::memory_order_acquire));
}

TEST_CASE("an engine job's console record keeps its bounds between takes, and counts what it drops",
          "[engine-graph][engine-jobs]") {
    EngineJobRecord record;
    EngineJobProgress p;
    record.rendered(250.0);
    record.write(EngineJobStream::Out, "rendering x\n");
    record.write(EngineJobStream::Err, "a warning\n");
    record.write(EngineJobStream::Out, "no newline");
    record.take(p);
    CHECK(p.renderedMs == 250.0);
    REQUIRE(p.lines.size() == 3);
    CHECK(p.lines[0].text == "rendering x");
    CHECK(p.lines[0].stream == EngineJobStream::Out);
    CHECK(p.lines[1].text == "a warning");
    CHECK(p.lines[1].stream == EngineJobStream::Err);
    CHECK(p.lines[2].text == "no newline");
    CHECK(p.droppedLines == 0);
    // A take empties it, the rendered length aside.
    record.take(p);
    CHECK(p.lines.empty());
    CHECK(p.renderedMs == 250.0);
    // Past the line bound, lines are dropped and counted, and the count goes with the next take.
    for (std::size_t i = 0; i < EngineJobRecord::kMaxLines + 10; ++i) record.write(EngineJobStream::Out, "x\n");
    record.take(p);
    CHECK(p.lines.size() == EngineJobRecord::kMaxLines);
    CHECK(p.droppedLines == 10);
    record.take(p);
    CHECK(p.droppedLines == 0);
    // Past the byte bound the same, and a line longer than the bound alone is cut to it at a UTF-8 boundary.
    const std::string half(EngineJobRecord::kMaxBytes / 2, 'a');
    record.write(EngineJobStream::Out, half);
    record.write(EngineJobStream::Out, half);
    record.write(EngineJobStream::Out, "one byte too many");
    record.take(p);
    CHECK(p.lines.size() == 2);
    CHECK(p.droppedLines == 1);
    // A three-byte character straddling the bound is left out whole rather than split.
    std::string wide(EngineJobRecord::kMaxBytes - 1, 'b');
    wide += "\xE2\x82\xAC";  // U+20AC, its first byte the bound's last
    record.write(EngineJobStream::Err, wide);
    record.take(p);
    REQUIRE(p.lines.size() == 1);
    CHECK(p.lines[0].text == std::string(EngineJobRecord::kMaxBytes - 1, 'b'));
}

TEST_CASE("the engine job RPCs carry a render's progress, lines and completion, and refuse an unknown kind",
          "[engine-graph][engine-jobs]") {
    TempDir          dir;
    Engine           e(48000.0);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    const auto       refused = rpc.submitEngineJob({"graph", "{}"});
    CHECK_FALSE(refused.id);
    REQUIRE(refused.error);
    CHECK(*refused.error == R"(unknown engine job kind "graph": the one kind is "render")");
    CHECK(e.submitEngineJob({"", "{}"}).id == 0);
    CHECK(OfflineJobs().submitEngine({"Render", "{}"}).id == 0);
    // A spec the worker cannot read is the job's to report, not a refusal.
    const auto bad = rpc.submitEngineJob({"render", "{}"});
    REQUIRE(bad.id);
    const auto taken = rpc.submitEngineJob({"render", renderSpec(RP_MGB_ROM_PATH, dir.file("rpc.wav"), 3000)});
    REQUIRE(taken.id);
    std::map<std::uint32_t, RpcEngineJobCompletion> done;
    std::vector<RpcEngineJobLine>                   lines;
    double                                          last = 0.0;
    bool                                            monotonic = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (done.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        RpcOfflineDrain d = rpc.drainOfflineJobs(100);
        CHECK(d.completions.empty());  // no graph job was submitted
        for (auto& p : d.engineProgress) {
            if (p.id != *taken.id) continue;
            monotonic = monotonic && p.renderedMs >= last;
            last = p.renderedMs;
            lines.insert(lines.end(), p.lines.begin(), p.lines.end());
        }
        for (auto& c : d.engineCompletions) done[c.id] = std::move(c);
    }
    REQUIRE(done.size() == 2);
    CHECK(done.at(*bad.id).status == "error");
    CHECK(done.at(*bad.id).message == "render worker: job spec missing 'rom'");
    const auto& c = done.at(*taken.id);
    INFO(c.message);
    CHECK(c.status == "done");
    CHECK(c.outputs == std::vector<std::string>{dir.file("rpc.wav")});
    CHECK(c.renderedMs == 3000.0);
    CHECK(monotonic);
    lines.insert(lines.end(), c.lines.begin(), c.lines.end());
    REQUIRE_FALSE(lines.empty());
    CHECK(std::all_of(lines.begin(), lines.end(),
                      [](const RpcEngineJobLine& l) { return l.stream == "stdout" || l.stream == "stderr"; }));
    CHECK(std::any_of(lines.begin(), lines.end(), [&](const RpcEngineJobLine& l) {
        return l.stream == "stdout" && l.text == dir.file("rpc.wav");
    }));
    CHECK(rpc.drainOfflineJobs(0).inFlight == 0);
    CHECK(e.offlineJobsOutstanding() == 0);
}
````

Run: `mkdir -p packages/native/test/engine-graph; sed -n '752,1497p' docs/modular-graph-phase-d3-plan.md > packages/native/test/engine-graph/EngineJobs.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/engine-graph/EngineJobs.test.cpp`
Expected: `e9265b30e29b54a6b35a8c40c757cc59d8d6fa70`: the blob 12414e0b3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index c3f5a0db6..4ac907812 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -91,7 +91,8 @@ retroplug_add_test_binary(
 # the audio thread, the protocol script nodes send what the DSP kernel they replaced
 # was recorded sending, every document in buildGraph's goldens, its role goldens among them, plans on it, and an
 # offline job (docs/modular-graph-phase-d2-design.md section 3) runs a document to completion on a worker host of its
-# own, apart from the live plan. A hidden bench times a script node per block, another an offline job.
+# own, apart from the live plan, as an engine job (docs/modular-graph-phase-d3-design.md section 3) runs a render with a
+# runtime and an Engine of its own. A hidden bench times a script node per block, another an offline job.
 # Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
 # CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
 # (the authoring layer for their toy nodes) is embedded here alone: cmake/bundles.cmake, retroplug-script-test-bundle.
@@ -106,6 +107,7 @@ retroplug_add_test_binary(
              test/engine-graph/Devices.test.cpp        # rp.host.devices/1: the links a hardware node binds by name
              test/engine-graph/Capabilities.test.cpp   # rp.system.memory/1, sram/1, rom/1 as asynchronous jobs
              test/engine-graph/OfflineJobs.test.cpp    # a document run to completion on a worker host of its own
+             test/engine-graph/EngineJobs.test.cpp     # a render run on the same pool by a RenderHost of its own
              test/engine-graph/N8Node.test.cpp         # rp.n8.nes: the real cart as a node, its jobs on the link
              test/engine-graph/N8Adapters.test.cpp     # rp.n8.nes through N8CartLink / N8Host to a fake serial port
              test/engine-graph/Surface.test.cpp        # rp.launchpad.surface-in / -out: the control surface as nodes
````

Run: `sed -n '1507,1528p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the new test source is listed.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: the build fails compiling `EngineJobs.test.cpp`: `EngineJobRequest`, `EngineJobsDrain`, `EngineJobBody`, `EngineJobRecord`, `OfflineJobs::submitEngine` and `engineBudget` do not exist yet. Nothing else fails to build.

- [ ] **Step 3: The pool runs engine jobs**

`OfflineTypes.hpp` gains the engine job's request, stream, line, progress, completion and drain types. `OfflineJobs`
takes its size and an optional body as constructor arguments, keeps one queue of both kinds, schedules each worker
onto the oldest job its kind's limit allows, starts a worker only when the runnable queued jobs outnumber the idle
ones (reserving `maxWorkers()` up front, so starting one never grows the vector), and runs an engine job's body inside
the floating-point pin; the default body builds a `RenderHost`, runs the spec and destroys it on the worker.
`EngineJobRecord` holds a job's rendered milliseconds and its bounded lines under its own lock; the drain takes
`mutex_`, then each running job's record, and a cancel taken after the body finished still makes the outcome
`cancelled`. The header documents the threads, the lock order, teardown and the process-wide state a render shares.
`RenderHost::run` takes a console sink (with none, the console still goes to the process's stdout and stderr, which
the registry and the tool keep), and script exception reports go through it. `Engine` forwards `submitEngineJob` and
the drain's engine half; `BackendTypes.hpp` adds the wire types and the drain's two arrays; `EngineRpcService` answers
`submitEngineJob` and fills them; the emulator facet counts 19 methods and the union 91.

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 8ebeb8dd1..2db4e8c0f 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -259,7 +259,8 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
 }
 
 // The offline pool first: it cancels its jobs and joins its workers, so no job thread outlives the engine, and each
-// job's host and nodes (a resampler among them) are gone before the engine's own members start to go.
+// job's host and nodes (a resampler among them), and each render's runtime, Engine and cores, are gone before the
+// engine's own members start to go.
 Engine::~Engine() { offline_.reset(); }
 
 rp::host::OfflineSubmit Engine::submitOfflineJob(rp::host::OfflineRequest request) {
@@ -267,9 +268,15 @@ rp::host::OfflineSubmit Engine::submitOfflineJob(rp::host::OfflineRequest reques
     return offline_->submit(std::move(request));
 }
 
-std::vector<rp::host::OfflineCompletion> Engine::drainOfflineJobs(std::chrono::milliseconds wait,
-                                                                  std::size_t*              inFlight) {
-    if (offline_) return offline_->drain(wait, inFlight);
+rp::host::OfflineSubmit Engine::submitEngineJob(rp::host::EngineJobRequest request) {
+    if (!offline_) offline_ = std::make_unique<rp::host::OfflineJobs>();
+    return offline_->submitEngine(std::move(request));
+}
+
+std::vector<rp::host::OfflineCompletion> Engine::drainOfflineJobs(std::chrono::milliseconds  wait,
+                                                                  std::size_t*               inFlight,
+                                                                  rp::host::EngineJobsDrain* engine) {
+    if (offline_) return offline_->drain(wait, inFlight, engine);
     if (inFlight) *inFlight = 0;
     return {};
 }
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index ddcdeade6..909317524 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -181,13 +181,20 @@ public:
     // that never submits starts no thread, and joined first as the engine goes. Taken or refused at once
     // (OfflineJobs::submit); its completion comes through a later drain.
     rp::host::OfflineSubmit submitOfflineJob(rp::host::OfflineRequest request);
-    // The completions since the last drain, oldest first; with `wait`, waits up to it for one while a job is in
-    // flight (OfflineJobs::drain), which a host with no UI tick does between pumps. `inFlight`, when given, is set to
-    // the jobs still queued or running after it.
+    // An engine job (docs/modular-graph-phase-d3-design.md section 3) on the same pool: a script bundle of `kind` (the
+    // one is "render") run with a QuickJS runtime and an Engine of its own, which is not this one. It belongs to this
+    // engine all the same: a render survives its editor closing, and this engine's teardown cancels it and joins its
+    // worker. Taken or refused at once (OfflineJobs::submitEngine).
+    rp::host::OfflineSubmit submitEngineJob(rp::host::EngineJobRequest request);
+    // The graph jobs' completions since the last drain, oldest first; with `engine`, the engine jobs' completions and
+    // every running one's progress too (OfflineJobs::drain). With `wait`, waits up to it for one while a job is in
+    // flight, which a host with no UI tick does between pumps. `inFlight`, when given, is set to the jobs still queued
+    // or running after it.
     std::vector<rp::host::OfflineCompletion> drainOfflineJobs(
-        std::chrono::milliseconds wait = std::chrono::milliseconds(0), std::size_t* inFlight = nullptr);
-    // Cancel a queued or running job: it completes with the reason "cancelled". False for one neither queued nor
-    // running.
+        std::chrono::milliseconds wait = std::chrono::milliseconds(0), std::size_t* inFlight = nullptr,
+        rp::host::EngineJobsDrain* engine = nullptr);
+    // Cancel a queued or running job of either kind: it completes with the reason "cancelled". False for one neither
+    // queued nor running.
     bool        cancelOfflineJob(std::uint32_t id);
     // Offline jobs queued, running, or finished and not yet drained.
     std::size_t offlineJobsOutstanding() const;
@@ -507,10 +514,10 @@ private:
     std::array<rp::host::Job*, kMaxJobs>            applied_{};
     std::size_t                                     appliedCount_ = 0;
 
-    // The offline job pool, made at the first submission. It uses nothing of the engine's (its registry, its jobs'
-    // tables and hosts are its own), so where it sits among the members does not matter for what it touches; the
-    // destructor still resets it first, so every worker is joined, and every node a job built destroyed, before any
-    // other member goes.
+    // The offline job pool, made at the first submission. It uses nothing of the engine's (its registry, its graph
+    // jobs' tables and hosts, and its engine jobs' runtimes and Engines are its own), so where it sits among the
+    // members does not matter for what it touches; the destructor still resets it first, so every worker is joined,
+    // and every node, core and runtime a job built destroyed, before any other member goes.
     std::unique_ptr<rp::host::OfflineJobs>          offline_;
 
     // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index 27cb2282e..303a8120e 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -19,6 +19,7 @@
 #include "graph/nodes/DspNodes.hpp"
 #include "graph/nodes/DspResampleNodes.hpp"
 #include "host/engine/DefaultFloatEnvironment.hpp"
+#include "host/render/RenderHost.hpp"
 #include "kit/SampleCache.hpp"
 
 namespace rp::host {
@@ -115,7 +116,10 @@ std::optional<OfflineWindow> offlineWindow(std::size_t decodedFrames, std::uint3
     return OfflineWindow{start, std::min(maxFrames, std::min(length0, decodedFrames - start))};
 }
 
-OfflineJobs::OfflineJobs(std::span<const rp_package* const> extra) {
+OfflineJobs::OfflineJobs(std::span<const rp_package* const> extra, unsigned size, EngineJobBody body)
+    : size_(size == 0 ? concurrency() : std::min(size, kMaxWorkers)), body_(std::move(body)) {
+    // Room for every worker up front, so starting one never grows the vector under a submission.
+    workers_.reserve(maxWorkers());
     const auto add = [this](const rp_package* p) {
         std::string why;
         if (!registry_.addPackage(p, &why))
@@ -178,35 +182,76 @@ OfflineSubmit OfflineJobs::submit(OfflineRequest request) {
         if (h == 0 || h > handles)
             return refuse("a return names res:" + std::to_string(h) + ", which is no input or output of the job");
 
+    auto job = std::make_unique<Job>();
+    job->kind = Kind::Graph;
+    job->request = std::move(request);
+    return enqueue(std::move(job));
+}
+
+OfflineSubmit OfflineJobs::submitEngine(EngineJobRequest request) {
+    // The kinds are the embedded bundles a job can run; D3 embeds one.
+    if (request.kind != "render")
+        return OfflineSubmit{0, "unknown engine job kind \"" + request.kind + "\": the one kind is \"render\""};
+    auto job = std::make_unique<Job>();
+    job->kind = Kind::Engine;
+    job->engine = std::move(request);
+    return enqueue(std::move(job));
+}
+
+bool OfflineJobs::runnable(const Job& job) const {
+    return job.kind == Kind::Graph ? runningGraph_ < size_ : runningEngine_ < engineBudget();
+}
+
+std::size_t OfflineJobs::runnableQueued() const {
+    std::size_t graph = 0, engine = 0;
+    for (const auto& job : queue_) (job->kind == Kind::Graph ? graph : engine)++;
+    return std::min<std::size_t>(graph, size_ - runningGraph_) +
+           std::min<std::size_t>(engine, engineBudget() - runningEngine_);
+}
+
+OfflineSubmit OfflineJobs::enqueue(std::unique_ptr<Job> job) {
     std::lock_guard<std::mutex> lock(mutex_);
-    if (stopping_) return refuse("the engine is going");
-    if (outstanding_ >= kMaxJobs)
-        return refuse("busy: " + std::to_string(kMaxJobs) + " offline jobs are outstanding");
-    // One more worker unless more are parked than jobs already wait for one (a parked worker a notify has woken stays
-    // counted until it takes the lock, so the jobs queued since have claimed it), while there is room; made here, under
-    // the lock it starts by taking.
-    if (idle_ <= queue_.size() && workers_.size() < concurrency()) {
+    if (stopping_) return {0, "the engine is going"};
+    if (outstanding_ >= kMaxJobs) return {0, "busy: " + std::to_string(kMaxJobs) + " offline jobs are outstanding"};
+    job->id = nextId_;
+    queue_.push_back(std::move(job));
+    // One more worker when the jobs that could start now outnumber the workers not running one (a parked worker a
+    // notify has woken stays counted until it claims a job, and a worker just started counts from its start, so the
+    // jobs queued since have claimed them), while there is room; made here, under the lock it starts by taking. A job
+    // its kind's limit holds back starts no worker: a worker that finishes a job of that kind takes it.
+    if (runnableQueued() > idle_ && workers_.size() < maxWorkers()) {
+        // A thread that cannot be started (std::system_error, or std::bad_alloc for its state) leaves the job queued
+        // for the workers there are; with none, it is refused and leaves nothing behind, its id not taken.
+        bool        failed = false;
+        std::string why;
         try {
             workers_.emplace_back([this] { workerLoop(); });
-        } catch (const std::system_error& e) {
-            if (workers_.empty()) return refuse(std::string("no worker thread could be started: ") + e.what());
+            ++idle_;
+        } catch (const std::exception& e) {
+            failed = true;
+            why = e.what();
+        } catch (...) {
+            failed = true;
+            why = "an unknown error";
+        }
+        if (failed && workers_.empty()) {
+            queue_.pop_back();
+            return {0, "no worker thread could be started: " + why};
         }
     }
-    auto job = std::make_unique<Job>();
-    job->id = nextId_++;
-    job->request = std::move(request);
-    const std::uint32_t id = job->id;
-    queue_.push_back(std::move(job));
+    const std::uint32_t id = nextId_++;
     ++outstanding_;
     work_.notify_one();
     return {id, {}};
 }
 
-std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait, std::size_t* inFlight) {
+std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait, std::size_t* inFlight,
+                                                  EngineJobsDrain* engine) {
     std::unique_lock<std::mutex> lock(mutex_);
-    if (completions_.empty() && wait.count() > 0) {
-        finished_.wait_for(lock, std::min(wait, kMaxDrainWait), [this] {
-            return stopping_ || !completions_.empty() || outstanding_ == 0;
+    const auto ready = [&] { return !completions_.empty() || (engine && !engineCompletions_.empty()); };
+    if (!ready() && wait.count() > 0) {
+        finished_.wait_for(lock, std::min(wait, kMaxDrainWait), [&] {
+            return stopping_ || ready() || (queue_.empty() && running_.empty());
         });
     }
     std::vector<OfflineCompletion> out;
@@ -214,7 +259,21 @@ std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait
     for (auto& c : completions_) out.push_back(std::move(c));
     outstanding_ -= completions_.size();
     completions_.clear();
-    if (inFlight) *inFlight = outstanding_;  // what is left is queued or running: every completion was just taken
+    if (engine) {
+        for (auto& c : engineCompletions_) engine->completions.push_back(std::move(c));
+        outstanding_ -= engineCompletions_.size();
+        engineCompletions_.clear();
+        // Every running engine job's progress, its lines since the drain before taken from its record. A job in
+        // running_ is alive while this lock is held: its worker takes the lock to take it out before it frees it.
+        for (Job* job : running_) {
+            if (job->kind != Kind::Engine) continue;
+            EngineJobProgress p;
+            p.id = job->id;
+            job->record.take(p);
+            engine->progress.push_back(std::move(p));
+        }
+    }
+    if (inFlight) *inFlight = queue_.size() + running_.size();
     return out;
 }
 
@@ -222,12 +281,18 @@ bool OfflineJobs::cancel(std::uint32_t id) {
     std::lock_guard<std::mutex> lock(mutex_);
     for (auto it = queue_.begin(); it != queue_.end(); ++it) {
         if ((*it)->id != id) continue;
+        std::unique_ptr<Job> job = std::move(*it);
         queue_.erase(it);
-        OfflineCompletion c;
-        c.id = id;
-        c.error = "cancelled";
-        completions_.push_back(std::move(c));
+        if (job->kind == Kind::Engine) {
+            engineCompletions_.push_back(engineCompletion(*job, "cancelled"));
+        } else {
+            OfflineCompletion c;
+            c.id = id;
+            c.error = "cancelled";
+            completions_.push_back(std::move(c));
+        }
         finished_.notify_all();
+        // A queued job holds no worker, so cancelling it frees none for another job to take.
         return true;
     }
     for (Job* job : running_) {
@@ -251,28 +316,121 @@ std::size_t OfflineJobs::workers() const {
 void OfflineJobs::workerLoop() {
     std::unique_lock<std::mutex> lock(mutex_);
     for (;;) {
-        ++idle_;
-        work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
-        --idle_;
+        // Counted idle by whoever made this worker so: the submission that started it, or the end of its last job.
+        auto next = queue_.end();
+        work_.wait(lock, [&] {
+            if (stopping_) return true;
+            next = std::find_if(queue_.begin(), queue_.end(), [&](const auto& j) { return runnable(*j); });
+            return next != queue_.end();
+        });
         if (stopping_) return;
-        std::unique_ptr<Job> job = std::move(queue_.front());
-        queue_.pop_front();
+        --idle_;
+        std::unique_ptr<Job> job = std::move(*next);
+        queue_.erase(next);
         running_.push_back(job.get());
+        (job->kind == Kind::Graph ? runningGraph_ : runningEngine_)++;
         lock.unlock();
-        OfflineCompletion done = run(*job);
+        OfflineCompletion   done;
+        EngineJobCompletion engineDone;
+        if (job->kind == Kind::Graph) done = run(*job);
+        else engineDone = runEngine(*job);
         lock.lock();
         running_.erase(std::find(running_.begin(), running_.end(), job.get()));
+        (job->kind == Kind::Graph ? runningGraph_ : runningEngine_)--;
+        ++idle_;
         // A cancel taken while the job ran wins, even over a run that got to its end first: cancel() answered true,
-        // and the caller holds that answer as the job's outcome.
-        if (job->cancelled.load(std::memory_order_acquire)) {
-            done.ok = false;
-            done.error = "cancelled";
-            done.buffers.clear();
-        }
+        // and the caller holds that answer as the job's outcome. A render that finished before its cancel was seen
+        // keeps the files it wrote in its outputs: they are whole, and on disk either way.
+        const bool cancelled = job->cancelled.load(std::memory_order_acquire);
         if (stopping_) return;  // the pool is going, and its completions with it
-        completions_.push_back(std::move(done));
+        if (job->kind == Kind::Graph) {
+            if (cancelled) {
+                done.ok = false;
+                done.error = "cancelled";
+                done.buffers.clear();
+            }
+            completions_.push_back(std::move(done));
+        } else {
+            EngineJobCompletion c = engineCompletion(*job, cancelled ? "cancelled" : std::move(engineDone.status));
+            if (!cancelled) c.message = std::move(engineDone.message);
+            c.outputs = std::move(engineDone.outputs);
+            engineCompletions_.push_back(std::move(c));
+        }
         finished_.notify_all();
+        // The job's slot is free: a job its kind's limit held back may start now, which this worker takes as it loops
+        // if nothing older is runnable; the others re-check in case that leaves one for them.
+        if (!queue_.empty()) work_.notify_all();
+    }
+}
+
+EngineJobCompletion OfflineJobs::engineCompletion(Job& job, std::string status) {
+    EngineJobCompletion c;
+    c.id = job.id;
+    c.status = std::move(status);
+    job.record.take(c);
+    return c;
+}
+
+void EngineJobRecord::rendered(double ms) {
+    std::lock_guard<std::mutex> lock(mutex_);
+    renderedMs_ = ms;
+}
+
+void EngineJobRecord::write(EngineJobStream stream, std::string_view text) {
+    if (!text.empty() && text.back() == '\n') text.remove_suffix(1);  // the console ends every write with one
+    if (text.size() > kMaxBytes) {
+        // Cut at a UTF-8 boundary: back off any continuation bytes the cut would split from their lead byte.
+        std::size_t cut = kMaxBytes;
+        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
+        text = text.substr(0, cut);
+    }
+    std::lock_guard<std::mutex> lock(mutex_);
+    if (lines_.size() >= kMaxLines || bytes_ + text.size() > kMaxBytes) {
+        ++dropped_;
+        return;
     }
+    bytes_ += text.size();
+    lines_.push_back({stream, std::string(text)});
+}
+
+EngineJobCompletion OfflineJobs::runEngine(Job& job) {
+    EngineJobCompletion out;
+    const auto cancelled = [&job] { return job.cancelled.load(std::memory_order_acquire); };
+    // The whole run in the default mode, as a graph job's: a render's arithmetic is then the CLI's, whatever the
+    // submitting thread's mode (a worker inherits it under glibc).
+    const DefaultFloatEnvironment floatEnvironment;
+    if (cancelled()) {
+        out.status = "cancelled";
+        return out;
+    }
+    try {
+        const EngineJobHooks hooks{
+            [&job](double ms) { job.record.rendered(ms); },
+            cancelled,
+            [&job](bool error, const char* text) {
+                job.record.write(error ? EngineJobStream::Err : EngineJobStream::Out, text);
+            },
+        };
+        EngineJobOutcome r = body_ ? body_(job.engine, hooks) : renderBody(job.engine, hooks);
+        out.status = std::move(r.status);
+        out.message = std::move(r.message);
+        out.outputs = std::move(r.outputs);
+    } catch (const std::exception& e) {
+        out.status = "error";
+        out.message = std::string("the engine job failed: ") + e.what();
+    } catch (...) {
+        out.status = "error";
+        out.message = "the engine job failed";
+    }
+    return out;
+}
+
+EngineJobOutcome OfflineJobs::renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks) {
+    // Built, run and destroyed on this thread: the runtime anchors its stack check to the thread that makes it, and
+    // the job's Engine, its cores and its plan's nodes go before this worker takes another job.
+    retroplug::RenderHost         host;
+    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console);
+    return {std::move(r.status), std::move(r.message), std::move(r.outputs)};
 }
 
 OfflineCompletion OfflineJobs::run(Job& job) {
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
index a32e1f786..3ed7d72ca 100644
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -6,9 +6,11 @@
 #include <cstddef>
 #include <cstdint>
 #include <deque>
+#include <functional>
 #include <memory>
 #include <mutex>
 #include <span>
+#include <string_view>
 #include <string>
 #include <thread>
 #include <vector>
@@ -43,10 +45,12 @@
 // ring, its resource table or its audio thread: the only state a worker shares is this pool's queue and completions,
 // under its mutex.
 //
-// The workers are started as jobs need them, up to concurrency() (the machine's hardware threads, at least one, at
-// most kMaxWorkers), so a host that never submits has no thread at all; jobs past that wait in submission order. A
-// worker never retires on its own: once started it stays parked between jobs until the pool goes, so an engine that ran
-// one kit compile keeps up to kMaxWorkers idle threads for the rest of its life (each a parked wait, costing no CPU).
+// The workers are started as jobs need them, up to maxWorkers() (the pool's size, which is the machine's hardware
+// threads, at least one, at most kMaxWorkers, unless a test gives another; see engine jobs below for a size of 1), so a
+// host that never submits has no thread at all; jobs past that wait, in the order "Sharing the pool" below gives. A
+// worker never retires on its
+// own: once started it stays parked between jobs until the pool goes, so an engine that ran one kit compile keeps up to
+// kMaxWorkers idle threads for the rest of its life (each a parked wait, costing no CPU).
 // At most kMaxJobs are outstanding at once, queued, running, or finished and not yet drained, so every job taken has
 // room for its completion and none is ever dropped.
 //
@@ -57,8 +61,109 @@
 // returns as a buffer of no frames. An OUTPUT of no frames is refused at submission, so a caller whose chain meets an
 // empty buffer (a window that came out empty, a resample whose length comes out 0) does not submit the jobs after it:
 // it knows what they would make. Every buffer is planar inside the job, interleaved in a completion.
+//
+// Engine jobs (docs/modular-graph-phase-d3-design.md section 3). The pool runs a second kind of job: a script bundle
+// run with a QuickJS runtime and an Engine of its own, which carries everything a graph job's host does not (system
+// nodes, ROM and battery resources, the protocol script packs, an advancing transport). The one kind is "render": the
+// worker builds a retroplug::RenderHost (host/render/RenderHost.hpp), runs the job's spec through it and destroys it,
+// all on that worker and inside the job's floating-point pin, so the runtime, the job's Engine, every core it boots
+// and every node and resampler its plan builds are made and freed on the one thread, before the worker takes its next
+// job. Nothing a render job touches belongs to the engine that owns this pool: the RenderHost's Engine is its own, as
+// are its resource table, snapshot registry and script host. What it shares with other jobs, and with the live engine
+// building cores on its control thread and running them on its audio thread, is the process's:
+//   - Mesen's per-process home folder, under which each core's scratch paths (the SMS staged ROM, the GBA BIOS, the
+//     NES SD card) are numbered per core by atomic counters rather than by system id, which every job's Engine starts
+//     at 1;
+//   - Mesen's MessageManager, whose process-wide SimpleLock keeps its holder and count in atomics (two cores built on
+//     two threads at once raced on them as plain members);
+//   - SameBoy's core statics, made safe by cmake/patches/sameboy-process-statics.patch: its random generator
+//     (Core/random.c, which runs at every reset for power-on RAM and during play for the Super Game Boy jingle's
+//     noise and a camera shoot) and the camera's noise seed (Core/camera.c) are read and written as relaxed atomics,
+//     so two callers at once may draw the same value, which only makes noise less random; the jingle's sine table
+//     (Core/sgb.c) is built by a load-time constructor rather than on first use; and apu.c's band-limited steps were
+//     already built that way. Everything else the core keeps at file scope is const, or the debugger's, which is not
+//     built. A deterministic build turns the generator off, which is why ThreadSanitizer cannot see a race on it in
+//     the engine-graph test binary;
+//   - r8brain's statics, and the embedded bundle's bytes, which nothing writes.
+// A worker that ran one render runs the next on a clean thread: QuickJS keeps no thread-local state (its class ids are
+// per runtime, and a RenderHost registers no class), and the thread-local state Mesen keeps (its notification dispatch
+// buffers, which hold no reference past a send, and SimpleLock's thread id) is per thread by design.
+//
+// An engine job reports as it runs: its script's progress (the milliseconds of audio rendered, RenderHost's
+// onRendered) and its console writes go into the job's EngineJobRecord under the record's own lock, which the worker
+// takes and a drain takes inside the pool's (never the other way round), and each drain hands back every running
+// engine job's progress with the lines written since the drain before. The rendered milliseconds are the script's last
+// report, which a render raises chunk by chunk and, when it detects a song's end, lowers once at the end to the length
+// it wrote (its count included the chunks it held back past the stop); a completion's is that final length. A "line"
+// is one console write, which can hold newlines of its own (a multi-line string, or a script exception's report with
+// its stack). A record is bounded between drains (EngineJobRecord::kMaxLines, kMaxBytes), because a plugin whose
+// editor is closed drains nothing for as long as a render runs; a line past either bound is dropped and counted, and
+// the count is reported (and reset) with the lines. A cancel is the render's own cooperative check, polled once per
+// chunk the render library pulls (100 ms of audio), so a running render stops within a chunk of the cancel, or once
+// its system has booted if the cancel lands during the boot.
+//
+// Sharing the pool. A render runs a full emulator flat out for seconds or minutes, where a kit compile's graph jobs
+// take milliseconds, so engine jobs run on at most engineBudget() workers at once, all but one of size(), and further
+// engine jobs wait, in submission order, while graph jobs start past them: a worker takes the oldest job it may run.
+// Graph jobs run on at most size() workers. A pool of size 1 cannot leave a worker free and still run a render at
+// all, so its budget is 1, and it starts a second thread (maxWorkers() is 2) for a graph job that arrives while a
+// render holds the first: a kit compile then shares the machine's one hardware thread with the render rather than
+// waiting minutes behind it. So a graph job always finds a worker, as long as the OS can start a thread when the pool
+// needs one: a worker that cannot be started leaves the job queued for the workers there are (enqueue), which may all
+// be rendering. Both kinds count toward kMaxJobs.
 namespace rp::host {
 
+// An engine job's record of what its script reported: the milliseconds of audio it has rendered, and the console lines
+// it wrote since the record was last taken, within the bounds (at most kMaxLines lines and kMaxBytes bytes of them; a
+// line past either is dropped and counted, and one longer than kMaxBytes alone is cut to it at a UTF-8 boundary).
+// The job's worker writes it as the script reports; a drain, and at the end the worker, take it. Any thread, under its
+// own lock, which a drain takes inside the pool's and the worker takes alone.
+class EngineJobRecord {
+public:
+    static constexpr std::size_t kMaxLines = 256;
+    static constexpr std::size_t kMaxBytes = 64 * 1024;
+
+    void rendered(double ms);
+    // One console write, the newline the console ends it with dropped.
+    void write(EngineJobStream stream, std::string_view text);
+    // The rendered length, and the lines and dropped count since the last take, which leave the record.
+    template <class Report>
+    void take(Report& out) {
+        std::lock_guard<std::mutex> lock(mutex_);
+        out.renderedMs = renderedMs_;
+        out.lines = std::move(lines_);
+        out.droppedLines = dropped_;
+        lines_.clear();
+        bytes_ = 0;
+        dropped_ = 0;
+    }
+
+private:
+    std::mutex                 mutex_;
+    double                     renderedMs_ = 0.0;
+    std::vector<EngineJobLine> lines_;
+    std::size_t                bytes_ = 0;
+    std::uint32_t              dropped_ = 0;
+};
+
+// What an engine job's body hears from the pool and reports through: the rendered milliseconds and the console writes
+// go into the job's record, and `cancelled` is the job's cancel flag, which the body polls (a render once per chunk).
+struct EngineJobHooks {
+    std::function<void(double ms)>                      rendered;
+    std::function<bool()>                               cancelled;
+    std::function<void(bool error, const char* text)>   console;
+};
+// How an engine job's body ended: "done", "cancelled" or "error", the error's message, and the files it wrote.
+struct EngineJobOutcome {
+    std::string              status;
+    std::string              message;
+    std::vector<std::string> outputs;
+};
+// An engine job's body, run on its worker inside the pool's floating-point pin. The pool's own runs a RenderHost over
+// the spec; a test gives one of its own to hold the pool's scheduling, cancel and floating-point rules to an exact
+// timeline.
+using EngineJobBody = std::function<EngineJobOutcome(const EngineJobRequest&, const EngineJobHooks&)>;
+
 class OfflineJobs {
 public:
     static constexpr std::size_t   kMaxJobs = 64;        // outstanding: queued, running, or finished and not drained
@@ -69,16 +174,26 @@ public:
     static constexpr std::chrono::milliseconds kMaxDrainWait{1000};
 
     // The node types a job's document may name: the built-in C++ packages, then `extra` (a test's own), registered
-    // here, before any worker runs, and never changed. A package the registry refuses is logged and left out.
-    explicit OfflineJobs(std::span<const rp_package* const> extra = {});
+    // here, before any worker runs, and never changed. A package the registry refuses is logged and left out. `size`
+    // is the pool's size, concurrency() when 0 (a test gives another to run a pool of 1 on any machine), and `body`
+    // what an engine job runs, the RenderHost body when empty (a test gives its own).
+    explicit OfflineJobs(std::span<const rp_package* const> extra = {}, unsigned size = 0, EngineJobBody body = {});
     // Cancels every job: the queued ones are dropped unrun, the running ones stop at their next block boundary or
-    // before their next input; then joins every worker. Completions not yet drained go with the pool.
+    // before their next input (a graph job) or at their next chunk (an engine job); then joins every worker.
+    // Completions not yet drained go with the pool.
     ~OfflineJobs();
     OfflineJobs(const OfflineJobs&) = delete;
     OfflineJobs& operator=(const OfflineJobs&) = delete;
 
-    // The workers the pool runs jobs on at most: std::thread::hardware_concurrency(), at least 1, at most kMaxWorkers.
+    // The default pool size: std::thread::hardware_concurrency(), at least 1, at most kMaxWorkers.
     static unsigned concurrency();
+    // The pool's size: graph jobs run on at most this many workers at once.
+    unsigned size() const { return size_; }
+    // Engine jobs run on at most this many workers at once: all but one of size(), and 1 for a pool of 1.
+    unsigned engineBudget() const { return size_ > 1 ? size_ - 1 : 1; }
+    // The threads the pool starts at most: size(), or 2 for a pool of 1, whose second thread a graph job takes while a
+    // render holds the first.
+    unsigned maxWorkers() const { return size_ > 1 ? size_ : 2; }
 
     // ---- any thread (the engine calls them on its control thread) ----
     // Take a job, or refuse it at once (id 0, the reason in `error`): a rate GraphHost refuses, more than kMaxResources
@@ -88,45 +203,78 @@ public:
     // that cannot be started. What fails only once the job runs (a file that will not decode, a document the planner
     // refuses, a node that will not activate or faults, a cancel) is its completion's error.
     OfflineSubmit submit(OfflineRequest request);
-    // The completions finished since the last drain, oldest first. With `wait`, and none finished, waits up to `wait`
-    // (at most kMaxDrainWait) for one, returning at once when no job is queued or running; never waits otherwise.
-    // `inFlight`, when given, is set to the jobs still queued or running after it, read under the same lock.
+    // Take an engine job, or refuse it at once: a kind that is not "render", kMaxJobs outstanding (graph and engine
+    // jobs together), and a worker that cannot be started. Everything about its spec is the job's to find out, and its
+    // completion's status "error" says what.
+    OfflineSubmit submitEngine(EngineJobRequest request);
+    // The graph jobs' completions finished since the last drain, oldest first. With `engine`, the engine jobs'
+    // completions are taken too, and every running engine job's progress with the console lines it wrote since the
+    // drain before (each line one console write, which can hold newlines of its own); without it, they stay for a
+    // drain that takes them, still outstanding. With `wait`, and nothing to take finished, waits up to `wait` (at most
+    // kMaxDrainWait) for one, returning at once when no job is queued or running; never waits otherwise. `inFlight`,
+    // when given, is set to the jobs still queued or running after it, read under the same lock.
     std::vector<OfflineCompletion> drain(std::chrono::milliseconds wait = std::chrono::milliseconds(0),
-                                         std::size_t* inFlight = nullptr);
-    // Cancel job `id`: a queued one completes now, unrun; a running one stops at its next block boundary, or as it
-    // finishes when it is already past its last (a job decoding a file finishes that decode and stops before its next
-    // input; one planning finishes the plan and stops before its first block). Either completes with the reason
-    // "cancelled". False for an id that is neither queued nor running (finished, or never taken).
+                                         std::size_t* inFlight = nullptr, EngineJobsDrain* engine = nullptr);
+    // Cancel job `id`: a queued one completes now, unrun; a running graph job stops at its next block boundary, or as
+    // it finishes when it is already past its last (a job decoding a file finishes that decode and stops before its
+    // next input; one planning finishes the plan and stops before its first block); a running engine job at its next
+    // chunk. Either completes with the reason "cancelled" (a graph job's error, an engine job's status). False for an
+    // id that is neither queued nor running (finished, or never taken).
     bool cancel(std::uint32_t id);
     // Jobs outstanding: queued, running, or finished and not yet drained.
     std::size_t outstanding() const;
-    // Worker threads started so far (never more than concurrency()).
+    // Worker threads started so far (never more than maxWorkers()).
     std::size_t workers() const;
     // The node types a job's document may name.
     const graph::NodeRegistry& registry() const { return registry_; }
 
 private:
+    enum class Kind : std::uint8_t { Graph, Engine };
+
     struct Job {
         std::uint32_t     id = 0;
-        OfflineRequest    request;
+        Kind              kind = Kind::Graph;
+        OfflineRequest    request;  // a graph job's
+        EngineJobRequest  engine;   // an engine job's
         std::atomic<bool> cancelled{false};
+        EngineJobRecord   record;   // an engine job's
     };
 
+    // ---- under mutex_ ----
+    // Whether a queued job of this kind may start now, given the workers its kind already holds.
+    bool runnable(const Job& job) const;
+    // How many queued jobs could start now: what a parked worker would take.
+    std::size_t runnableQueued() const;
+    // Queue the job, starting a worker when the runnable jobs outnumber the parked ones; refuses (and drops the job)
+    // when the pool is going, is full, or can start no worker at all.
+    OfflineSubmit enqueue(std::unique_ptr<Job> job);
+    // An engine job's completion with `status`, its record taken into it.
+    static EngineJobCompletion engineCompletion(Job& job, std::string status);
+
     void              workerLoop();
     // Runs the job, and releases its request's samples once the job's table holds them.
     OfflineCompletion run(Job& job);
+    // Runs an engine job's body on this thread, inside the floating-point pin.
+    EngineJobCompletion runEngine(Job& job);
+    // The pool's own engine job body: builds a RenderHost on this thread, runs the spec, and destroys it here.
+    static EngineJobOutcome renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks);
 
     // Filled by the constructor and never changed: the workers read it without the lock.
     graph::NodeRegistry registry_;
+    const unsigned      size_;
+    const EngineJobBody body_;
 
     mutable std::mutex                mutex_;
-    std::condition_variable           work_;      // a worker waits for a job, or for the pool to stop
+    std::condition_variable           work_;      // a worker waits for a job it may run, or for the pool to stop
     std::condition_variable           finished_;  // a drain waits for a completion
     std::deque<std::unique_ptr<Job>>  queue_;
     std::vector<Job*>                 running_;   // owned by the worker running each
     std::deque<OfflineCompletion>     completions_;
+    std::deque<EngineJobCompletion>   engineCompletions_;
     std::size_t                       outstanding_ = 0;
-    std::size_t                       idle_ = 0;  // workers parked, waiting for a job
+    std::size_t                       idle_ = 0;  // workers not running a job: parked, or started and not yet parked
+    unsigned                          runningGraph_ = 0;
+    unsigned                          runningEngine_ = 0;
     std::vector<std::thread>          workers_;
     std::uint32_t                     nextId_ = 1;
     bool                              stopping_ = false;
diff --git a/packages/native/src/host/engine/OfflineTypes.hpp b/packages/native/src/host/engine/OfflineTypes.hpp
index 2aad5c3ce..983009301 100644
--- a/packages/native/src/host/engine/OfflineTypes.hpp
+++ b/packages/native/src/host/engine/OfflineTypes.hpp
@@ -8,8 +8,9 @@
 #include <vector>
 
 // The value types of an offline job's seam (docs/modular-graph-phase-d2-design.md section 3): a submission, a window
-// and a completion. They sit apart from the pool (OfflineJobs.hpp, which documents the jobs) so the engine's header
-// names them without dragging in the pool's threads, queue and node registry.
+// and a completion, for a graph job and for an engine job (docs/modular-graph-phase-d3-design.md section 3). They sit
+// apart from the pool (OfflineJobs.hpp, which documents the jobs) so the engine's header names them without dragging
+// in the pool's threads, queue and node registry.
 namespace rp::host {
 
 // How a file input's frame cap scales a frame count at a kit rate back to the decoded rate, reproducing the two kit
@@ -117,4 +118,53 @@ struct OfflineCompletion {
     std::string                     guardReason;
 };
 
+// An engine job (docs/modular-graph-phase-d3-design.md section 3): a script bundle of `kind` run on a pool worker with
+// a QuickJS runtime and an Engine of its own, given `spec`. D3 has one kind, "render", the render-worker bundle
+// (RenderHost), whose spec is the render job JSON it reads; the pool refuses any other kind at submission.
+struct EngineJobRequest {
+    std::string kind;
+    std::string spec;
+};
+
+// One console write a job's script made (console.log, console.warn and the rest), without the newline the console
+// ends each with, and which stream it went to. A write can hold newlines of its own: a multi-line string, or a script
+// exception's report with its stack.
+enum class EngineJobStream : std::uint8_t { Out, Err };
+struct EngineJobLine {
+    EngineJobStream stream = EngineJobStream::Out;
+    std::string     text;
+};
+
+// A running engine job's state at a drain: how much audio it has rendered (its last report, in milliseconds), the
+// console lines it wrote since the drain before, and how many lines it wrote past the record's bound in that time,
+// which were dropped (EngineJobRecord, in OfflineJobs.hpp). A render raises its report chunk by chunk; one that
+// detects a song's end lowers it once, at its end, to the length it wrote.
+struct EngineJobProgress {
+    std::uint32_t              id = 0;
+    double                     renderedMs = 0.0;
+    std::vector<EngineJobLine> lines;
+    std::uint32_t              droppedLines = 0;
+};
+
+// A finished engine job: `status` "done", "cancelled" or "error", `message` the error's (empty otherwise), `outputs`
+// the files it wrote, and the progress fields as a drain reports them, which here are its final ones: the rendered
+// length it ended at (for a render that detected its song's end, the length it wrote, which can be below its earlier
+// reports), and the lines it wrote since its last progress report.
+struct EngineJobCompletion {
+    std::uint32_t              id = 0;
+    std::string                status;
+    std::string                message;
+    std::vector<std::string>   outputs;
+    double                     renderedMs = 0.0;
+    std::vector<EngineJobLine> lines;
+    std::uint32_t              droppedLines = 0;
+};
+
+// The engine jobs' part of a drain: the completions since the last drain, oldest first, and every running engine
+// job's progress.
+struct EngineJobsDrain {
+    std::vector<EngineJobCompletion> completions;
+    std::vector<EngineJobProgress>   progress;
+};
+
 }  // namespace rp::host
diff --git a/packages/native/src/host/render/RenderHost.cpp b/packages/native/src/host/render/RenderHost.cpp
index 283f51925..908ab18f0 100644
--- a/packages/native/src/host/render/RenderHost.cpp
+++ b/packages/native/src/host/render/RenderHost.cpp
@@ -42,14 +42,16 @@ std::string toStr(JSContext* ctx, JSValueConst v) {
     return out;
 }
 
+// A script exception's report goes where the worker's console.error would: the host's console sink when it has one.
 void dumpError(JSContext* ctx) {
     JSValue exc = JS_GetException(ctx);
-    std::fprintf(stderr, "render-host: %s\n", toStr(ctx, exc).c_str());
+    std::string report = "render-host: " + toStr(ctx, exc) + "\n";
     JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
-    if (!JS_IsUndefined(stack) && !JS_IsNull(stack))
-        std::fprintf(stderr, "%s\n", toStr(ctx, stack).c_str());
+    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) report += toStr(ctx, stack) + "\n";
     JS_FreeValue(ctx, stack);
     JS_FreeValue(ctx, exc);
+    if (RenderHost* h = hostOf(ctx)) h->onStderr(report.c_str());
+    else std::fputs(report.c_str(), stderr);
 }
 
 // __rpcSend(request) -> response | null. The RpcSendFn* rides in funcData[0] (an ArrayBuffer of the pointer
@@ -209,9 +211,11 @@ RenderHost::~RenderHost() {
     }
 }
 
-RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled) {
+RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled,
+                                   ConsoleFn onConsole) {
     renderedFn_ = std::move(onRendered);
     cancelFn_ = std::move(isCancelled);
+    consoleFn_ = std::move(onConsole);
     result_ = Result{};
 
     // The Engine service graph — one per host (mirrors cli/main.cpp), minus TjsHostRuntime. No audio thread
@@ -298,11 +302,13 @@ void RenderHost::onResult(std::string status, std::string message, std::vector<s
 }
 
 void RenderHost::onStdout(const char* text) {
-    std::fputs(text, stdout);
+    if (consoleFn_) consoleFn_(false, text);
+    else std::fputs(text, stdout);
 }
 
 void RenderHost::onStderr(const char* text) {
-    std::fputs(text, stderr);
+    if (consoleFn_) consoleFn_(true, text);
+    else std::fputs(text, stderr);
 }
 
 } // namespace retroplug
diff --git a/packages/native/src/host/render/RenderHost.hpp b/packages/native/src/host/render/RenderHost.hpp
index 66351af93..a58587aaa 100644
--- a/packages/native/src/host/render/RenderHost.hpp
+++ b/packages/native/src/host/render/RenderHost.hpp
@@ -3,9 +3,10 @@
 // RenderHost — a self-contained, headless audio-render environment: a BARE QuickJS runtime (no txiki, no
 // libuv) + its own Engine + the in-process RPC bridge, running the render-worker bundle
 // (packages/retroplug/src/render/worker.ts) as global-code bytecode. It is the "retroplug-cli render"
-// pipeline, but embeddable and driven synchronously on the calling thread — designed to run on a dedicated
-// worker thread (one per job; see RenderJobRegistry), so the plugin can render a fresh system to disk
-// without touching the live audio-thread cores.
+// pipeline, but embeddable and driven synchronously on the calling thread, designed to run on a worker
+// thread (a dedicated one per job in RenderJobRegistry; a pool worker in an engine job, host/engine/OfflineJobs.hpp),
+// so the plugin can render a fresh system to disk without touching the live audio-thread cores. Built, run and
+// destroyed on the one thread.
 //
 // One-shot: construct, run() once, destroy. run() blocks for the whole render. Only console +
 // TextEncoder/TextDecoder + the RPC / rendered / cancel / result thunks are bound (the render path needs
@@ -26,6 +27,9 @@ class RenderHost {
 public:
     using RenderedFn = std::function<void(double)>;  // audio rendered so far, in milliseconds
     using CancelFn   = std::function<bool()>;         // polled per chunk; true aborts the render
+    // One console write the worker made (console.log and the rest, each ending in a newline), `error` for the stderr
+    // ones (console.warn / error / trace) and for a script exception's report.
+    using ConsoleFn  = std::function<void(bool error, const char* text)>;
 
     struct Result {
         std::string status;                // "done" | "cancelled" | "error"; empty if the worker never reported
@@ -40,9 +44,11 @@ public:
     RenderHost& operator=(const RenderHost&) = delete;
 
     // Render one job. `jobJson` is a RenderOpts-shaped spec (at least {"rom":"...","out":"..."}); the worker
-    // fills the CLI defaults for anything omitted. onRendered / isCancelled are optional. Returns the
-    // worker's reported result (status "error" with a message on any internal failure).
-    Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {});
+    // fills the CLI defaults for anything omitted. onRendered / isCancelled / onConsole are optional; with no
+    // onConsole the worker's console writes go to the process's stdout and stderr. Returns the worker's reported
+    // result (status "error" with a message on any internal failure).
+    Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {},
+               ConsoleFn onConsole = {});
 
     // Called by the bound JS thunks (which recover the host via the JS context opaque). Public so the
     // free-function thunks in the .cpp can reach them; not part of the external API.
@@ -58,6 +64,7 @@ private:
 
     RenderedFn renderedFn_;
     CancelFn   cancelFn_;
+    ConsoleFn  consoleFn_;
     Result     result_;
 };
 
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index acf424ad1..b1c33e004 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -36,7 +36,7 @@ void registerHostRpc(Server& s, HostRpcService& h) {
 }
 
 // --- emulator: resources / lifecycle / script packs / live params / snapshot reads / capability jobs / offline jobs /
-// input (18) ---
+// input (19) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::putResource>(e);
@@ -51,6 +51,7 @@ void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::submitJob>(e);
     s.template addMethod<&EngineRpcService::drainJobs>(e);
     s.template addMethod<&EngineRpcService::submitOfflineJob>(e);
+    s.template addMethod<&EngineRpcService::submitEngineJob>(e);
     s.template addMethod<&EngineRpcService::drainOfflineJobs>(e);
     s.template addMethod<&EngineRpcService::cancelOfflineJob>(e);
     s.template addMethod<&EngineRpcService::screenshot>(e);
@@ -127,7 +128,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (90 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (91 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index d88b0e7c0..0ad550432 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -206,9 +206,43 @@ struct RpcOfflineCompletion {
     std::uint32_t                      guardsFired;
     std::string                        guardReason;
 };
-// What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running after
-// them (so a caller holding a promise for a job native no longer holds learns so, rather than poll for it forever).
+// An engine job (docs/modular-graph-phase-d3-design.md section 3, Engine::submitEngineJob): a script bundle of `kind`
+// run on a worker of the same pool with a runtime and an Engine of its own; the one kind is "render", whose `spec` is
+// the render job JSON (a RenderOpts the render worker reads). Its answer is a graph job's, RpcOfflineSubmitResult.
+struct RpcEngineJobRequest {
+    std::string kind;
+    std::string spec;
+};
+// One console write a job's script made, without its newline, and its stream: "stdout" or "stderr".
+struct RpcEngineJobLine {
+    std::string stream;
+    std::string text;
+};
+// A running engine job at a drain: the milliseconds of audio rendered, the lines written since the drain before, and
+// how many were dropped past the job's record bound in that time.
+struct RpcEngineJobProgress {
+    std::uint32_t                 id;
+    double                        renderedMs;
+    std::vector<RpcEngineJobLine> lines;
+    std::uint32_t                 droppedLines;
+};
+// A finished engine job: `status` "done", "cancelled" or "error", the error's `message`, the files written, and its
+// final progress fields.
+struct RpcEngineJobCompletion {
+    std::uint32_t                 id;
+    std::string                   status;
+    std::string                   message;
+    std::vector<std::string>      outputs;
+    double                        renderedMs;
+    std::vector<RpcEngineJobLine> lines;
+    std::uint32_t                 droppedLines;
+};
+// What drainOfflineJobs answers: the graph jobs' completions, how many of the engine's jobs (of both kinds) are still
+// queued or running after them (so a caller holding a promise for a job native no longer holds learns so, rather than
+// poll for it forever), the engine jobs' completions, and every running engine job's progress.
 struct RpcOfflineDrain {
-    std::vector<RpcOfflineCompletion> completions;
-    std::uint32_t                     inFlight;
+    std::vector<RpcOfflineCompletion>   completions;
+    std::uint32_t                       inFlight;
+    std::vector<RpcEngineJobCompletion> engineCompletions;
+    std::vector<RpcEngineJobProgress>   engineProgress;
 };
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index dd4be39bf..4f10e4511 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -279,12 +279,35 @@ RpcOfflineSubmitResult EngineRpcService::submitOfflineJob(RpcOfflineRequest requ
     return out;
 }
 
+RpcOfflineSubmitResult EngineRpcService::submitEngineJob(RpcEngineJobRequest request) {
+    RpcOfflineSubmitResult  out{std::nullopt, std::nullopt};
+    rp::host::OfflineSubmit submitted =
+        engine_.submitEngineJob({std::move(request.kind), std::move(request.spec)});
+    if (submitted.id != 0) out.id = submitted.id;
+    else out.error = std::move(submitted.error);
+    return out;
+}
+
+namespace {
+
+std::vector<RpcEngineJobLine> toRpcLines(std::vector<rp::host::EngineJobLine>& lines) {
+    std::vector<RpcEngineJobLine> out;
+    out.reserve(lines.size());
+    for (auto& l : lines)
+        out.push_back({l.stream == rp::host::EngineJobStream::Err ? "stderr" : "stdout", std::move(l.text)});
+    return out;
+}
+
+}  // namespace
+
 RpcOfflineDrain EngineRpcService::drainOfflineJobs(double waitMs) {
     // A NaN or a negative wait waits not at all; the engine bounds a long one.
     const double ms = std::isfinite(waitMs) ? std::clamp(waitMs, 0.0, 1000.0) : (waitMs > 0 ? 1000.0 : 0.0);
-    RpcOfflineDrain out{{}, 0};
-    std::size_t     inFlight = 0;
-    for (auto& c : engine_.drainOfflineJobs(std::chrono::milliseconds(static_cast<std::int64_t>(ms)), &inFlight)) {
+    RpcOfflineDrain           out{{}, 0, {}, {}};
+    std::size_t               inFlight = 0;
+    rp::host::EngineJobsDrain engine;
+    for (auto& c : engine_.drainOfflineJobs(std::chrono::milliseconds(static_cast<std::int64_t>(ms)), &inFlight,
+                                            &engine)) {
         RpcOfflineCompletion rc{c.id, c.ok, std::move(c.error), {}, {}, c.guardsFired, std::move(c.guardReason)};
         for (auto& b : c.buffers) {
             rfl::Bytestring bytes;
@@ -298,6 +321,11 @@ RpcOfflineDrain EngineRpcService::drainOfflineJobs(double waitMs) {
             rc.inputs.push_back({i.handle, i.decodedRate, i.decodedFrames, i.frames, i.nonFinite});
         out.completions.push_back(std::move(rc));
     }
+    for (auto& c : engine.completions)
+        out.engineCompletions.push_back({c.id, std::move(c.status), std::move(c.message), std::move(c.outputs),
+                                         c.renderedMs, toRpcLines(c.lines), c.droppedLines});
+    for (auto& p : engine.progress)
+        out.engineProgress.push_back({p.id, p.renderedMs, toRpcLines(p.lines), p.droppedLines});
     out.inFlight = static_cast<std::uint32_t>(inFlight);
     return out;
 }
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index caa5d3f64..7733bc62d 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -64,12 +64,16 @@ public:
     // exactly one kind (samples, channels and rate; or a path, with its window), samples that are not whole float32
     // values, and a cap arithmetic that is neither "float-ratio" nor "double-scale".
     RpcOfflineSubmitResult            submitOfflineJob(RpcOfflineRequest request);
-    // The completions since the last drain, oldest first, and the jobs still queued or running after them. `waitMs`
-    // above 0 waits up to that long (at most a second) for one while a job is in flight: what a host with no UI tick
-    // does between pumps (src/offline.ts). A host that pumps from its UI tick passes 0, and never blocks the thread its
-    // UI runs on.
+    // An engine job (Engine::submitEngineJob, docs/modular-graph-phase-d3-design.md section 3) on the same pool: its
+    // id, whose completion and progress later drainOfflineJobs calls hand back, or why it was refused.
+    RpcOfflineSubmitResult            submitEngineJob(RpcEngineJobRequest request);
+    // The completions since the last drain, oldest first, of both kinds, every running engine job's progress, and the
+    // jobs still queued or running after them. `waitMs` above 0 waits up to that long (at most a second) for a
+    // completion while a job is in flight: what a host with no UI tick does between pumps (src/offline.ts). A host that
+    // pumps from its UI tick passes 0, and never blocks the thread its UI runs on.
     RpcOfflineDrain                   drainOfflineJobs(double waitMs);
-    // Cancel a queued or running offline job; it completes with the reason "cancelled". False for one that is neither.
+    // Cancel a queued or running offline job of either kind; it completes with the reason "cancelled". False for one
+    // that is neither.
     bool                              cancelOfflineJob(std::uint32_t id);
 
     // (Live-core debug/inspection — getApuState/readCpu/breakpoints/trace/profiler/… — moved to
````

Run: `sed -n '1556,2536p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-engine-graph-test "[engine-jobs]"`
Expected: `test cases: 12 | 11 passed | 1 skipped`: the skip is the fixture-gated sounding LSDj case.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test "[engine-jobs],[offline],[engine-jobs-hff]"`
Expected: `All tests passed (1674578 assertions in 30 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): every case runs, D2's `[offline]` unchanged, and the HFF case's length is 191700 ms.

- [ ] **Step 4: Show that the budget bites**

A budget of the whole pool would let renders take every worker, so a kit compile would wait behind them. Apply this
temporary mutation, which makes `engineBudget()` the pool's size, and the budget case must fail; then reverse it.

````patch
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -190,7 +190,7 @@
     // The pool's size: graph jobs run on at most this many workers at once.
     unsigned size() const { return size_; }
     // Engine jobs run on at most this many workers at once: all but one of size(), and 1 for a pool of 1.
-    unsigned engineBudget() const { return size_ > 1 ? size_ - 1 : 1; }
+    unsigned engineBudget() const { return size_; }
     // The threads the pool starts at most: size(), or 2 for a pool of 1, whose second thread a graph job takes while a
     // render holds the first.
     unsigned maxWorkers() const { return size_ > 1 ? size_ : 2; }
````

Run: `sed -n '2557,2568p' docs/modular-graph-phase-d3-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-engine-graph-test "[engine-jobs]"`
Expected: fails, about 20 s slower than a green run, in exactly three cases: `renders hold all but one of the pool's workers, so a graph job still runs beside them` fails its pool-of-three section's `CHECK(engineBudget() == 2)` and then, about 20 s later, `the graph job did not complete while renders held the pool` (its pool-of-one section passes); `a cancel stops an engine job, queued at once and running at its next chunk` fails its `REQUIRE(jobs.engineBudget() == 1)`; and `renders held back by the budget start in submission order` fails `CHECK(jobs.workers() == 1)`, two renders starting at once (its order check may fail too). The other two cases that build a pool of 2 submit one job each and pass.

Run: `sed -n '2557,2568p' docs/modular-graph-phase-d3-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `OfflineJobs.hpp` is the task's file again (`git diff` of it shows only the task's own change). Then rebuild with `cmake --build build --target retroplug-engine-graph-test -j$(nproc)` and rerun `build/bin/retroplug-engine-graph-test "[engine-jobs]"`: `test cases: 12 | 11 passed | 1 skipped` again.

- [ ] **Step 5: Write the failing TypeScript cases**

`test/offline/offline.test.ts` gains five cases over its virtual-clock port, which now runs engine jobs (a spec of
`{ms, out, endMs?}` that reports its elapsed units as rendered milliseconds, writes a line as it starts and ends, and
lowers its count to `endMs` when given) and cancels: the mock rejects every engine job and its handles have nothing to
cancel; an engine job settles with its status and length, its progress a running maximum and its lines delivered as
the drains report them; a job an engine job's callback submits as the last job ends is submitted and settles; one a
callback submits while two engine jobs end in one drain leaves the second to settle as it ended; and a cancel settles
a held run at once and a submitted one of either kind through native's cancel. `test-native/engine-run.test.ts` runs
real render jobs: a WAV written with its progress and lines; a spec it cannot render as the job's error and an unknown
kind refused; `cancel()` stopping a running render while a graph job completes beside it; an auto-detected render's
running maximum against its length (fixture-gated); and `offlineRun`'s handle cancelling a graph job.

Create `packages/retroplug/test-native/engine-run.test.ts`:

````ts
// Engine jobs over the real host (src/offline.ts, docs/modular-graph-phase-d3-design.md section 3): backend.engineRun
// runs a render on a worker of the engine's offline pool, with a runtime and an Engine of its own, and `await` settles
// in this host, which fires no timer and has no frame tick, through the poll's native wait the audio driver turns on.
// A render settles "done" with the WAV it wrote and its length, reporting its progress (a running maximum) and its
// console lines on the way; an auto-detected LSDj render (a fixture) ends below its progress, at the length written;
// cancel() stops a running render, which settles "cancelled", after a graph job completed while it ran; and
// offlineRun's handle cancels a graph job, which rejects with the reason "cancelled".
// The pool, the threading, the budget, the bytes against a direct RenderHost run and teardown are held by
// retroplug-engine-graph-test "[engine-jobs]".
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { decodeWav } from "../src/render";
import { OfflineRunError, type EngineLogLine, type EngineRunHandle } from "../src/offline";

declare const __REPO_RESOURCES_DIR__: string;
declare const __RESOURCES_DIR__: string;

const MGB = __REPO_RESOURCES_DIR__ + "/roms/mGB.gb";

const be = createRealBackend();
createAudioDriver(); // the poll's wait: this host has no UI tick

// A WAV path of this run's own: the files of the suite run in parallel processes.
const RUN = `${Date.now()}-${Math.floor(Math.random() * 1e9)}`;
const outPath = (name: string) => `/tmp/rp-engine-run-${RUN}-${name}.wav`;

test("a render job writes its WAV and settles done, its progress and console lines arriving as it runs", async () => {
  const out = outPath("done");
  const progress: number[] = [];
  const lines: EngineLogLine[] = [];
  const result = await be.engineRun(
    "render",
    { rom: MGB, out, durationMs: 2000, sampleRate: 48000 },
    { onProgress: (ms) => progress.push(ms), onLog: (line) => lines.push(line) },
  );
  expect(result).toEqual({ status: "done", message: "", outputs: [out], renderedMs: 2000 });

  const bytes = be.readFile(out);
  expect(bytes != null).toBe(true);
  const tag = (at: number) => String.fromCharCode(...bytes!.subarray(at, at + 4));
  expect(tag(0)).toBe("RIFF");
  expect(tag(8)).toBe("WAVE");
  const wav = decodeWav(bytes!);
  expect(wav.sampleRate).toBe(48000);
  expect(wav.channels).toBe(2);
  expect(wav.pcm.length).toBe(96000 * 2); // two seconds, interleaved stereo
  expect(bytes!.byteLength).toBe(44 + 96000 * 2 * 2);

  expect(progress.length > 0).toBe(true);
  for (let i = 1; i < progress.length; i++) expect(progress[i] > progress[i - 1]).toBe(true);
  expect(progress[progress.length - 1]).toBe(2000);
  expect(lines.some((l) => l.stream === "stdout" && l.text.startsWith("rendering "))).toBe(true);
  expect(lines.some((l) => l.stream === "stdout" && l.text === out)).toBe(true);
  expect(lines.every((l) => !l.text.endsWith("\n"))).toBe(true);
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(out);
});

test("a render job reports a spec it cannot render as its error, and an unknown kind is refused", async () => {
  expect(await be.engineRun("render", { out: outPath("no-rom") })).toEqual({
    status: "error",
    message: "render worker: job spec missing 'rom'",
    outputs: [],
    renderedMs: 0,
  });
  let refused = "";
  await be.engineRun("bake" as "render", {}).catch((e: Error) => (refused = e.message));
  expect(refused).toBe('unknown engine job kind "bake": the one kind is "render"');
});

test("cancel() stops a running render, which settles cancelled, and a graph job completes while it runs", async () => {
  // Ten minutes of audio: many seconds of wall time. Once it has rendered some, a graph job (a document-less echo) is
  // submitted, and its completion, which comes while the render still runs, cancels the render. That pins the two
  // kinds overlapping on the pool; the budget itself (a render never holding the last worker) is the C++ case's.
  let handle: EngineRunHandle | null = null;
  let renderSettled = false;
  let renderRunningAtGraph = false;
  let cancelled: boolean | null = null;
  let lastMs = 0;
  let graph: Promise<number> | null = null;
  const echo = { inputs: [{ samples: Float32Array.of(0.5), channels: 1, rate: 48000 }], returns: [1], frames: 0 };
  handle = be.engineRun(
    "render",
    { rom: MGB, out: outPath("cancel"), durationMs: 600000 },
    {
      onProgress: (ms) => {
        lastMs = ms;
        if (graph !== null || ms < 500) return;
        graph = be.offlineRun({ ...echo, rate: 48000 }).then((r) => {
          renderRunningAtGraph = !renderSettled;
          cancelled = handle!.cancel();
          return r.buffers[0].samples[0];
        });
      },
    },
  );
  void handle.then(() => (renderSettled = true));
  const result = await handle;
  expect(graph !== null).toBe(true);
  expect(await graph!).toBe(0.5);
  expect(renderRunningAtGraph).toBe(true);
  expect(cancelled).toBe(true);
  expect(result.status).toBe("cancelled");
  expect(result.outputs).toEqual([]);
  expect(lastMs >= 500 && lastMs < 600000).toBe(true);
  expect(handle.cancel()).toBe(false); // settled: nothing left to cancel
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(outPath("cancel"));
});

test("an auto-detected render's progress is a running maximum, and its result carries the length it wrote",
  async () => {
    // An LSDj song that ends in an HFF: the render counts the chunks it holds back past the stop as it goes, then
    // writes up to the stop, so native's count drops once at the end. onProgress never goes back; the length written
    // is the result's renderedMs, and the WAV's.
    const rom = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.gbc";
    const sav = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.sav";
    if (!be.fileExists(rom) || !be.fileExists(sav)) skip(`engine-run: the LSDj song fixture is not at ${rom}`);
    const out = outPath("hff");
    const progress: number[] = [];
    const result = await be.engineRun(
      "render",
      { rom, sav, out, songIndex: 3 },
      { onProgress: (ms) => progress.push(ms) },
    );
    expect(result.status).toBe("done");
    expect(result.renderedMs).toBe(191700); // MACHETE's length to its HFF
    // Whether a drain caught a count above the length depends on when the polls fell (the mock tier pins the clamp
    // on an exact timeline); what holds every time is that onProgress never went back and reached the length.
    for (let i = 1; i < progress.length; i++) expect(progress[i] > progress[i - 1]).toBe(true);
    expect(progress[progress.length - 1] >= result.renderedMs).toBe(true);
    console.log(`[engine-run] hff: most reported ${progress[progress.length - 1]} ms, length ${result.renderedMs} ms`);
    const wav = decodeWav(be.readFile(out)!);
    expect(wav.pcm.length / wav.channels).toBe(191700 * 44.1);
    be.deleteFile(out);
  });

test("offlineRun's handle cancels a graph job, which rejects with the reason cancelled", async () => {
  // A looping player recorded for its first frames over ten minutes of blocks: long enough to be cancelled mid-run.
  const document = JSON.stringify({
    nodes: [
      {
        id: "p",
        type: "rp.core.buffer-player",
        settings: { channels: 1, layout: "mono", loop: true },
        resources: { sample: "res:1" },
      },
      { id: "rec", type: "rp.core.buffer-recorder", settings: { channels: 1 }, resources: { target: "res:2" } },
    ],
    wires: [{ from: "p.out", to: "rec.in" }],
  });
  const run = be.offlineRun({
    document,
    inputs: [{ samples: new Float32Array(256).fill(0.25), channels: 1, rate: 48000 }],
    outputs: [{ channels: 1, frames: 256, rate: 48000 }],
    frames: 48000 * 600,
    rate: 48000,
  });
  expect(run.cancel()).toBe(true);
  let error: Error | null = null;
  await run.catch((e: Error) => (error = e));
  expect(error! instanceof OfflineRunError).toBe(true);
  expect(error!.message).toBe("cancelled");
  expect(run.cancel()).toBe(false);
  expect(be.pumpJobs()).toBe(0);
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '2599,2765p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/engine-run.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/engine-run.test.ts`
Expected: `f8bae9a23e7c8e9efa5c5616d89f9b190fff4385`: the blob 12414e0b3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test/offline/offline.test.ts b/packages/retroplug/test/offline/offline.test.ts
index 5ec2fae48..005e88548 100644
--- a/packages/retroplug/test/offline/offline.test.ts
+++ b/packages/retroplug/test/offline/offline.test.ts
@@ -1,12 +1,17 @@
-// Offline jobs as promises (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3): the mock backend rejects
-// every job, since the mock tier runs no graph; and the promise side over a virtual-time port: a refusal rejects at
-// once with native's reason, a failure with an OfflineRunError carrying the inputs' reports, several jobs submitted in
-// one run are all in flight at once and settle through the poll's native wait with no timer, a chain of jobs (direct,
-// or through extra async layers) keeps moving while a long job runs, a fan-out past native's bound is held back rather
-// than refused, a job native no longer holds rejects rather than being polled forever, a port call that throws fails
-// its own run rather than the queue, the shared pump (pumpAllJobs, as the UI tick's pumpJobs) settles them with the
-// wait off, and the wire request carries the handles and the default returns.
-// The native half is held by retroplug-engine-graph-test "[offline]" and test-native/offline-run.
+// Offline jobs as promises (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3, and the engine jobs of
+// docs/modular-graph-phase-d3-design.md section 3): the mock backend rejects every job of either kind, since the mock
+// tier runs no graph and has no engine, and hands back a handle with nothing to cancel; an engine job settles with its
+// status and length, its progress (a running maximum) and console lines delivered as the drains report them, a job
+// one of its callbacks submits included, even when two jobs end in the drain it runs in; a cancel settles a held run
+// at once and a submitted one through native's cancel, for both kinds; and the promise side over a virtual-time port:
+// a refusal rejects at once with native's reason, a failure with an OfflineRunError carrying the inputs' reports,
+// several jobs submitted in one run are all in flight at once and settle through the poll's native wait with no timer,
+// a chain of jobs (direct, or through extra async layers) keeps moving while a long job runs, a fan-out past native's
+// bound is held back rather than refused, a job native no longer holds rejects rather than being polled forever, a port
+// call that throws fails its own run rather than the queue, the shared pump (pumpAllJobs, as the UI tick's pumpJobs)
+// settles them with the wait off, and the wire request carries the handles and the default returns.
+// The native half is held by retroplug-engine-graph-test "[offline]" and "[engine-jobs]", and test-native/offline-run
+// and engine-run.
 import { test, expect } from "../../testing/harness";
 import { MockBackend } from "../../testing/mockBackend";
 import { liveJobCount, pumpAllJobs } from "../../src/capability";
@@ -17,6 +22,9 @@ import {
   offlineInputRef,
   offlineOutputRef,
   setOfflineWait,
+  type EngineWireCompletion,
+  type EngineWireProgress,
+  type EngineWireRequest,
   type OfflineRunRequest,
   type OfflineSubmitResult,
   type OfflineWireCompletion,
@@ -27,11 +35,22 @@ import {
 // A port over a virtual clock, as native behaves: a job runs for `frames` time units from its submission (0: done at
 // once), a drain that waits advances the clock to the next job's end when none has finished, a drain that does not
 // wait never moves it, at most OFFLINE_MAX_JOBS are outstanding (submitted and not yet drained) and one past that is
-// refused busy. A job echoes its first input's samples, or fails for a document of "fail".
+// refused busy. A job echoes its first input's samples, or fails for a document of "fail". An engine job's spec is
+// {"ms": n, "out": path, "endMs"?: m}: it runs n time units, each drain reporting the units elapsed as its rendered
+// milliseconds, writes one line as it starts and one as it ends, and completes "done" with `out` as its output (and
+// `endMs` as its final length when given, as a render that detected its song's end lowers its count), or "error" for
+// a spec with no out. A cancel ends a job at the next drain, a graph job's error and an engine job's status
+// "cancelled".
 function clockPort() {
   let now = 0;
   let next = 1;
-  const jobs: { id: number; request: OfflineWireRequest; end: number }[] = [];
+  const jobs: {
+    id: number;
+    request: OfflineWireRequest;
+    engine?: { ms: number; out?: string; endMs?: number; start: number; started: boolean };
+    end: number;
+    cancelled?: boolean;
+  }[] = [];
   const log: string[] = [];
   const waits: number[] = [];
   const submitted: OfflineWireRequest[] = [];
@@ -53,11 +72,53 @@ function clockPort() {
       most = Math.max(most, jobs.length);
       return { id: next++, error: null };
     },
+    submitEngineJob(request: EngineWireRequest): OfflineSubmitResult {
+      if (request.kind !== "render") return { id: null, error: `unknown engine job kind "${request.kind}"` };
+      if (jobs.length >= OFFLINE_MAX_JOBS)
+        return { id: null, error: `busy: ${OFFLINE_MAX_JOBS} offline jobs are outstanding` };
+      const spec = JSON.parse(request.spec) as { ms: number; out?: string; endMs?: number };
+      const engine = { ms: spec.ms, out: spec.out, endMs: spec.endMs, start: now, started: false };
+      const request0: OfflineWireRequest = { inputs: [], outputs: [], returns: [], frames: 0, rate: 1 };
+      jobs.push({ id: next, request: request0, engine, end: now + spec.ms });
+      log.push(`t=${now} submit engine ${spec.out ?? ""}`);
+      most = Math.max(most, jobs.length);
+      return { id: next++, error: null };
+    },
+    cancelOfflineJob(id: number): boolean {
+      const j = jobs.find((x) => x.id === id);
+      if (!j || j.cancelled) return false;
+      j.cancelled = true;
+      j.end = now;
+      return true;
+    },
     drainOfflineJobs(waitMs: number): OfflineWireDrain {
       waits.push(waitMs);
       if (waitMs > 0 && jobs.length && !jobs.some((j) => j.end <= now)) now = Math.min(...jobs.map((j) => j.end));
       const done: OfflineWireCompletion[] = [];
-      for (const j of jobs.filter((x) => x.end <= now)) {
+      const engineDone: EngineWireCompletion[] = [];
+      const engineProgress: EngineWireProgress[] = [];
+      for (const j of jobs.filter((x) => x.engine)) {
+        const e = j.engine!;
+        const lines = e.started ? [] : [{ stream: "stdout" as const, text: `rendering ${e.out ?? ""}` }];
+        e.started = true;
+        const renderedMs = Math.min(now, j.end) - e.start;
+        if (j.end > now) {
+          engineProgress.push({ id: j.id, renderedMs, lines, droppedLines: 0 });
+          continue;
+        }
+        const status = j.cancelled ? "cancelled" : e.out ? "done" : "error";
+        if (status === "done") lines.push({ stream: "stdout", text: e.out! });
+        const message = status === "error" ? "the spec names no out" : "";
+        const outputs = status === "done" ? [e.out!] : [];
+        const finalMs = status === "done" && e.endMs !== undefined ? e.endMs : renderedMs;
+        engineDone.push({ id: j.id, status, message, outputs, renderedMs: finalMs, lines, droppedLines: 0 });
+      }
+      for (const j of jobs.filter((x) => !x.engine && x.end <= now)) {
+        if (j.cancelled) {
+          const none = { buffers: [], inputs: [], guardsFired: 0, guardReason: "" };
+          done.push({ id: j.id, ok: false, error: "cancelled", ...none });
+          continue;
+        }
         const report = { handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true };
         if (j.request.document === "fail") {
           const failed = { id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] };
@@ -78,8 +139,8 @@ function clockPort() {
           guardReason: "",
         });
       }
-      for (const d of done) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
-      return { completions: done, inFlight: jobs.length };
+      for (const d of [...done, ...engineDone]) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
+      return { completions: done, inFlight: jobs.length, engineCompletions: engineDone, engineProgress };
     },
   };
   return port;
@@ -111,6 +172,132 @@ test("the mock backend rejects every offline job: only a native host runs graphs
   expect(be.pumpJobs()).toBe(0);
 });
 
+test("the mock backend rejects every engine job, and its handles have nothing to cancel", async () => {
+  const be = new MockBackend("/cfg");
+  const handle = be.engineRun("render", { rom: "/x.gb", out: "/x.wav" });
+  expect(typeof handle.cancel).toBe("function");
+  expect(handle.cancel()).toBe(false);
+  let message = "";
+  await handle.catch((e: Error) => (message = e.message));
+  expect(message).toBe("engine jobs need a native host: the mock backend has no engine to run them on");
+  const graph = be.offlineRun({ frames: 64, rate: 48000 });
+  expect(graph.cancel()).toBe(false);
+  await graph.catch(() => {});
+  expect(be.pumpJobs()).toBe(0);
+});
+
+test("an engine job settles with its status, its progress and console lines arriving as the drains report them",
+  async () => {
+    const port = clockPort();
+    const jobs = new OfflineJobs(port);
+    const progress: number[] = [];
+    const lines: string[] = [];
+    const onProgress = (ms: number) => progress.push(ms);
+    const onLog = (l: { stream: string; text: string }) => lines.push(`${l.stream}: ${l.text}`);
+    // With the wait off a pump drains; the clock moves only as the test moves it.
+    const h = jobs.engine("render", { ms: 3, out: "/o.wav" }, { onProgress, onLog });
+    expect(port.log).toEqual(["t=0 submit engine /o.wav"]);
+    for (let t = 0; t < 3; t++) {
+      port.advance(1);
+      jobs.pump();
+    }
+    const result = await h;
+    expect(result).toEqual({ status: "done", message: "", outputs: ["/o.wav"], renderedMs: 3 });
+    expect(progress).toEqual([1, 2, 3]);
+    expect(lines).toEqual(["stdout: rendering /o.wav", "stdout: /o.wav"]);
+    // A failure resolves with its status and message rather than rejecting; a refusal rejects.
+    const failed = await waiting(() => jobs.engine("render", JSON.stringify({ ms: 1 })));
+    expect(failed).toEqual({ status: "error", message: "the spec names no out", outputs: [], renderedMs: 1 });
+    // A render that detects its song's end lowers its count at the end: onProgress holds its maximum, and the length
+    // it wrote comes with the result.
+    const hff: number[] = [];
+    const h2 = jobs.engine("render", { ms: 5, out: "/hff.wav", endMs: 3 }, { onProgress: (ms) => hff.push(ms) });
+    for (let t = 0; t < 5; t++) {
+      port.advance(1);
+      jobs.pump();
+    }
+    expect((await h2).renderedMs).toBe(3);
+    expect(hff).toEqual([1, 2, 3, 4]); // the completion's lower count is not a call
+    let refused = "";
+    await jobs.engine("bake" as "render", {}).catch((e: Error) => (refused = e.message));
+    expect(refused).toBe('unknown engine job kind "bake"');
+    expect(jobs.pendingCount()).toBe(0);
+    expect(liveJobCount()).toBe(0);
+  });
+
+test("a job an engine job's callback submits as the last job ends is submitted, and settles", async () => {
+  const port = clockPort();
+  const jobs = new OfflineJobs(port);
+  await waiting(async () => {
+    let next: Promise<number[]> | null = null;
+    const onProgress = () => {
+      // Inside the drain that ended the render, with nothing else in flight: not a job native lost.
+      next ??= jobs.run(job("after", 2, 4)).then((r) => Array.from(r.buffers[0].samples));
+    };
+    expect((await jobs.engine("render", { ms: 1, out: "/a.wav" }, { onProgress })).status).toBe("done");
+    expect(await next!).toEqual([4]);
+  });
+  expect(jobs.pendingCount()).toBe(0);
+});
+
+test("a job a callback submits while two engine jobs end in one drain leaves the second to settle as it ended",
+  async () => {
+    // Both renders end in the same drain. The first one's progress callback submits a job that completes at once,
+    // which pumps; a drain nested inside the one still settling would find nothing in flight and take the second
+    // render, whose completion the outer drain has yet to reach, for lost.
+    const port = clockPort();
+    const jobs = new OfflineJobs(port);
+    await waiting(async () => {
+      let next: Promise<number[]> | null = null;
+      const onProgress = (ms: number) => {
+        if (ms >= 2) next ??= jobs.run(job("now", 0, 6)).then((r) => Array.from(r.buffers[0].samples));
+      };
+      const first = jobs.engine("render", { ms: 2, out: "/1.wav" }, { onProgress });
+      const second = jobs.engine("render", { ms: 2, out: "/2.wav" });
+      expect((await first).status).toBe("done");
+      expect(await second).toEqual({ status: "done", message: "", outputs: ["/2.wav"], renderedMs: 2 });
+      expect(await next!).toEqual([6]);
+    });
+    expect(jobs.pendingCount()).toBe(0);
+  });
+
+test("a cancel settles a held run at once, and a submitted one of either kind through native's cancel", async () => {
+  const port = clockPort();
+  const jobs = new OfflineJobs(port);
+  await waiting(async () => {
+    // Submitted: the graph job rejects "cancelled", the engine job resolves with that status, both at a drain.
+    const graph = jobs.run(job("long", 100, 1));
+    const render = jobs.engine("render", { ms: 100, out: "/r.wav" });
+    expect(graph.cancel()).toBe(true);
+    expect(render.cancel()).toBe(true);
+    let error: OfflineRunError | null = null;
+    await graph.catch((e: OfflineRunError) => (error = e));
+    expect(error! instanceof OfflineRunError).toBe(true);
+    expect(error!.message).toBe("cancelled");
+    expect(await render).toEqual({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
+    expect(port.now()).toBe(0); // neither ran to its end
+    // Settled: nothing left to cancel.
+    expect(graph.cancel()).toBe(false);
+    expect(render.cancel()).toBe(false);
+    // Held past native's bound: settled here, never submitted.
+    const fill = Array.from({ length: OFFLINE_MAX_JOBS }, (_, k) => jobs.run(job(`f${k}`, 5, k)));
+    const heldGraph = jobs.run(job("held", 1, 7));
+    const heldRender = jobs.engine("render", { ms: 1, out: "/h.wav" });
+    expect(port.mostInFlight()).toBe(OFFLINE_MAX_JOBS);
+    expect(heldGraph.cancel()).toBe(true);
+    expect(heldRender.cancel()).toBe(true);
+    let heldError = "";
+    await heldGraph.catch((e: Error) => (heldError = e.message));
+    expect(heldError).toBe("cancelled");
+    expect(await heldRender).toEqual({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
+    await Promise.all(fill);
+    expect(port.submitted.some((r) => r.document === "held")).toBe(false);
+    expect(port.log.some((l) => l.includes("/h.wav"))).toBe(false);
+  });
+  expect(jobs.pendingCount()).toBe(0);
+  expect(liveJobCount()).toBe(0);
+});
+
 test("a refusal rejects at once, and a failure rejects with its reason and the inputs' reports", async () => {
   const port = clockPort();
   const jobs = new OfflineJobs(port);
@@ -199,7 +386,9 @@ test("a job native no longer holds rejects, rather than being polled forever", a
   // A port that takes a job and then holds no record of it: nothing comes back and nothing is in flight.
   const jobs = new OfflineJobs({
     submitOfflineJob: () => ({ id: 1, error: null }),
+    submitEngineJob: () => ({ id: 1, error: null }),
     drainOfflineJobs: () => ({ completions: [], inFlight: 0 }),
+    cancelOfflineJob: () => false,
   });
   let message = "";
   await waiting(() => jobs.run(job("lost", 1)).catch((e: Error) => (message = e.message)));
@@ -256,7 +445,9 @@ test("a submit that throws rejects its own run, held or not, and the rest still
       if (request.document === "boom") throw new Error("the port refused a field");
       return port.submitOfflineJob(request);
     },
+    submitEngineJob: (request) => port.submitEngineJob(request),
     drainOfflineJobs: (waitMs) => port.drainOfflineJobs(waitMs),
+    cancelOfflineJob: (id) => port.cancelOfflineJob(id),
   });
   let message = "";
   await waiting(async () => {
@@ -301,10 +492,12 @@ test("a drain that throws rejects every pending run, not just the one that saw i
   let broken = false;
   const jobs = new OfflineJobs({
     submitOfflineJob: () => ({ id: next++, error: null }),
+    submitEngineJob: () => ({ id: next++, error: null }),
     drainOfflineJobs: () => {
       if (broken) throw new Error("the drain broke");
       return { completions: [], inFlight: next - 1 };
     },
+    cancelOfflineJob: () => false,
   });
   const runs = [jobs.run(job("a", 1)), jobs.run(job("b", 1))];
   broken = true;
````

Run: `sed -n '2775,3072p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test offline`
Expected: fails: the 11 cases D2 left pass and the 5 new ones fail, because the mock backend has no `engineRun`, the queue has no `engine` method and `run` returns a bare promise with no `cancel()`. (The test imports only types it lacks, which the bundler erases, so the file bundles and runs.)

- [ ] **Step 6: The promise side**

`src/offline.ts` keeps one `OfflineJobs` queue for both kinds, as native's one drain answers both: `engine(kind, spec,
options)` submits through the port's `submitEngineJob`, routes each drain's progress and lines to the job's callbacks
(progress as a running maximum) and settles it from its completion; `jobHandle()` makes a promise with `cancel()` for
either kind, cancelling a held run at once and a submitted one through `cancelOfflineJob`; a settle a callback
re-enters returns without draining, and the lost-job check considers only the jobs pending at its drain. The port
gains `submitEngineJob` and `cancelOfflineJob`. `backend.ts` documents `engineRun` and `offlineRun`'s handle,
`realBackend.ts` wires both, the mock rejects engine jobs with "engine jobs need a native host: the mock backend has
no engine to run them on" and answers false to both handles' `cancel()`, and `KitJobRunner` becomes an interface
naming only the promise-returning `offlineRun`, so the kit tests' scripted runners need no cancel.

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 8dcce84fd..6b2814313 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -18,7 +18,7 @@
 // belongs in the application layer, not here.
 
 import type { CapabilityArgs, CapabilityOp, CapabilityProgress, CapabilityResults } from "./capability";
-import type { OfflineRunRequest, OfflineRunResult } from "./offline";
+import type { EngineJobKind, EngineRunHandle, EngineRunOptions, OfflineRunHandle, OfflineRunRequest } from "./offline";
 import type { Platform, Core } from "./platform";
 import type { TapArm } from "./graph/buildGraph";
 
@@ -258,8 +258,22 @@ export interface Backend {
    *  Zero frames: an input whose window comes out empty is accepted (its report says `frames: 0`, its buffer comes back
    *  empty) but names no resource, so a document cannot bind it; an output of no frames is refused. A caller whose
    *  chain meets an empty buffer (an empty window, a resample whose length comes out 0) submits nothing after it and
-   *  makes what those jobs would have made itself. */
-  offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult>;
+   *  makes what those jobs would have made itself.
+   *
+   *  The handle's `cancel()` cancels the job: one held in TS settles at once, a submitted one when a later pump or
+   *  wait drains its completion, rejecting with an OfflineRunError whose message is "cancelled". */
+  offlineRun(request: OfflineRunRequest): OfflineRunHandle;
+
+  /** Run an engine job (docs/modular-graph-phase-d3-design.md section 3) on the same pool and the same pump: a script
+   *  bundle of `kind` with a runtime and an Engine of its own, given `spec` (a value is sent as its JSON). The one kind
+   *  is "render", whose spec is the render job the render worker reads (src/render/worker.ts). Settles with
+   *  `{ status, message, outputs, renderedMs }`, "done", "cancelled" or "error", rather than rejecting on a failure;
+   *  rejects only when native refuses the job (a kind it does not run) or the port throws. `onProgress` hears the
+   *  milliseconds of audio rendered, as a running maximum (`renderedMs` is the length the render ended at, which for
+   *  an auto-detected song end can be below it), and `onLog` each console write the job's script made, both delivered
+   *  by the pump that drains them. What settles it is what settles offlineRun; the two kinds share native's bound. The
+   *  mock rejects every job: only a native host has an engine to run one on. */
+  engineRun(kind: EngineJobKind, spec: object | string, options?: EngineRunOptions): EngineRunHandle;
 
   /** Reject every pending capability promise with `reason`: the project closed, and the nodes they named with it. A
    *  real client's queue is its engine's, shared by every emulator client of a runtime, so this reaches theirs too. */
@@ -461,7 +475,7 @@ export type EmulatorBackend = Pick<
   | "putResource" | "releaseResource"
   | "setGraph" | "loadScriptPack" | "setParam"
   | "pressButton" | "stageNodeMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
-  | "capability" | "pumpJobs" | "rejectPendingJobs" | "offlineRun"
+  | "capability" | "pumpJobs" | "rejectPendingJobs" | "offlineRun" | "engineRun"
 >;
 
 /** Live-core inspection / stepping / breakpoints / profiler — the CLI-only debug facet (spec/09). */
diff --git a/packages/retroplug/src/kit/compile.ts b/packages/retroplug/src/kit/compile.ts
index 8f639ded2..3bee7bfa4 100644
--- a/packages/retroplug/src/kit/compile.ts
+++ b/packages/retroplug/src/kit/compile.ts
@@ -27,8 +27,7 @@
 // empty slot, and has none.
 
 import type { KitCompileSpec, KitSampleSpec, RisaKitCompileSpec, RisaDmcSampleSpec } from "../audioDriver";
-import type { Backend } from "../backend";
-import { OfflineRunError, type OfflineCap, type OfflineRunRequest } from "../offline";
+import { OfflineRunError, type OfflineCap, type OfflineRunRequest, type OfflineRunResult } from "../offline";
 import { buildKitBankPlaced } from "../lsdj/rom/buildKit";
 import { KIT_MAX_SAMPLES, KIT_MAX_SAMPLE_SPACE } from "../lsdj/rom/constants";
 import { assembleKitBankPlaced, type AssembleSlot } from "../risa/rom/kit";
@@ -56,11 +55,14 @@ import {
   trimToFrames,
 } from "./pack";
 
-/** What the compilers run their jobs on: a backend's offline jobs (every native host has them; the mock rejects).
+/** What the compilers run their jobs on: a backend's offline jobs (every native host has them; the mock rejects), of
+ *  which the compilers need only the promise, not the handle's cancel.
  *  The promises settle only in a host that pumps or waits for its offline jobs (src/offline.ts): one with a UI tick
  *  pumps from it, and one without needs the poll's native wait, which createAudioDriver turns on as it is made (or
  *  setOfflineWait). In a host with neither, a compile never settles. */
-export type KitJobRunner = Pick<Backend, "offlineRun">;
+export interface KitJobRunner {
+  offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult>;
+}
 
 /** A compiled kit: the bank, and each spec sample's reason in spec order, null exactly when the bank holds that
  *  sample whole. A reason is a failure (the slot is empty), or why a slot that compiled is still empty or short (an
diff --git a/packages/retroplug/src/offline.ts b/packages/retroplug/src/offline.ts
index 15cbdee8f..c07380440 100644
--- a/packages/retroplug/src/offline.ts
+++ b/packages/retroplug/src/offline.ts
@@ -4,6 +4,16 @@
 // later (drainOfflineJobs, from a bounded queue). This is the promise side of that; only the real backend has it, since
 // only a native host runs graphs (the mock rejects).
 //
+// Engine jobs (docs/modular-graph-phase-d3-design.md section 3) ride the same queue: a script bundle of a kind (the
+// one is "render") run on a worker of the same pool with a runtime and an Engine of its own (submitEngineJob).
+// Native's one drain hands back both kinds' completions, and every running engine job's progress and console lines,
+// so one queue here holds both kinds' promises. An engine job's promise settles with its status ("done", "cancelled"
+// or "error") rather than rejecting on a failure: a render that fails has written nothing, and says why in its
+// message, and only a refusal (or a port that throws) rejects it. Each handle, of either kind, can cancel its job: a
+// run still held here settles at once, and a submitted one through native's cancel, settled by a later drain like any
+// completion (a graph job's promise then rejects with the reason "cancelled", an engine job's resolves with that
+// status).
+//
 // The poll follows the capability jobs' (src/capability.ts), whose live set it joins, so one pump settles both kinds:
 // once right after each submission, from the UI's frame tick (ui/app/useJobPump.ts, through backend.pumpJobs) while
 // anything is pending, and after each audio-driver call that lets time pass (pumpAllJobs). What it cannot share is the
@@ -170,16 +180,88 @@ export interface OfflineWireCompletion {
   guardReason: string;
 }
 
-/** What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running. */
+/** The kinds of engine job: the embedded script bundles a job can run. */
+export type EngineJobKind = "render";
+
+/** How an engine job ended: it ran to its end ("done"), was cancelled, or failed ("error", with its message). */
+export type EngineRunStatus = "done" | "cancelled" | "error";
+
+/** A finished engine job: its status, the error's message ("" otherwise), the files it wrote, and the milliseconds of
+ *  audio it ended at: for a render, the length it wrote, which for one that detected its song's end can be below the
+ *  most onProgress reported (that count included the chunks held back past the stop). */
+export interface EngineRunResult {
+  status: EngineRunStatus;
+  message: string;
+  outputs: string[];
+  renderedMs: number;
+}
+
+/** One console write a job's script made (console.log and the rest), without its final newline, and its stream. A
+ *  write can hold newlines of its own: a multi-line string, or a script exception's report with its stack. */
+export interface EngineLogLine {
+  stream: "stdout" | "stderr";
+  text: string;
+}
+
+/** What an engine job reports while it runs, delivered from the pump that drains it. */
+export interface EngineRunOptions {
+  /** The milliseconds of audio rendered so far, each time it has risen since the last report: a running maximum,
+   *  never going back. A render that detects its song's end lowers its own count once, at the end, to the length it
+   *  wrote; that length is the result's `renderedMs`, not a last call here. */
+  onProgress?: (renderedMs: number) => void;
+  /** Each console write the job's script made, in order. Native keeps a bounded record between drains
+   *  (EngineJobRecord: 256 lines, 64 KiB); writes past it were dropped, and arrive as one stderr line saying how
+   *  many. */
+  onLog?: (line: EngineLogLine) => void;
+}
+
+/** A job's promise, with a way to cancel the job. `cancel()` answers whether there was a job to cancel: true for one
+ *  held here or queued or running in native, which then ends cancelled (a graph job's promise rejects with the reason
+ *  "cancelled", an engine job's resolves with that status); false once it has finished. */
+export interface JobHandle<T> extends Promise<T> {
+  cancel(): boolean;
+}
+export type OfflineRunHandle = JobHandle<OfflineRunResult>;
+export type EngineRunHandle = JobHandle<EngineRunResult>;
+
+/** `promise` as a handle whose cancel is `cancel`. */
+export function jobHandle<T>(promise: Promise<T>, cancel: () => boolean): JobHandle<T> {
+  return Object.assign(promise, { cancel });
+}
+
+// The engine job wire shapes (native BackendTypes.hpp: RpcEngineJobRequest, RpcEngineJobProgress,
+// RpcEngineJobCompletion).
+export interface EngineWireRequest {
+  kind: string;
+  spec: string;
+}
+export interface EngineWireProgress {
+  id: number;
+  renderedMs: number;
+  lines: EngineLogLine[];
+  droppedLines: number;
+}
+export interface EngineWireCompletion extends EngineWireProgress {
+  status: string;
+  message: string;
+  outputs: string[];
+}
+
+/** What drainOfflineJobs answers: the graph jobs' completions, how many of the engine's jobs (of both kinds) are
+ *  still queued or running, the engine jobs' completions, and every running engine job's progress. */
 export interface OfflineWireDrain {
   completions: OfflineWireCompletion[];
   inFlight: number;
+  engineCompletions?: EngineWireCompletion[];
+  engineProgress?: EngineWireProgress[];
 }
 
-/** The two native calls the jobs ride: what a backend hands OfflineJobs. */
+/** The native calls the jobs ride: what a backend hands OfflineJobs. */
 export interface OfflinePort {
   submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult;
+  submitEngineJob(request: EngineWireRequest): OfflineSubmitResult;
   drainOfflineJobs(waitMs: number): OfflineWireDrain;
+  cancelOfflineJob(id: number): boolean;
 }
 
 /** Native's bound on one engine's outstanding offline jobs (OfflineJobs::kMaxJobs): past it the queue holds runs
@@ -234,37 +316,60 @@ function asError(e: unknown): Error {
   return e instanceof Error ? e : new Error(String(e));
 }
 
-interface Pending {
-  resolve: (value: OfflineRunResult) => void;
+type Waiter = (
+  | { kind: "graph"; resolve: (value: OfflineRunResult) => void }
+  | { kind: "engine"; resolve: (value: EngineRunResult) => void; options: EngineRunOptions; renderedMs: number }
+) & {
   reject: (error: Error) => void;
+  id: number | null; // native's id once submitted
+};
+
+type Submission = { kind: "graph"; wire: OfflineWireRequest } | { kind: "engine"; wire: EngineWireRequest };
+
+// A caller's callback that throws is the caller's bug, and must not strand the other promises the drain settles.
+function report(callback: () => void): void {
+  try {
+    callback();
+  } catch (e) {
+    console.error(`an engine job's callback threw: ${asError(e).message}`);
+  }
 }
 
-/** The promises over one queue of offline jobs: native's, which is one per engine. */
+/** The promises over one queue of offline jobs, graph and engine: native's, which is one per engine. */
 export class OfflineJobs implements JobQueue {
-  private readonly pending = new Map<number, Pending>();
+  private readonly pending = new Map<number, Waiter>();
   // Runs made while OFFLINE_MAX_JOBS were pending, oldest first, submitted as completions make room.
-  private readonly held: { wire: OfflineWireRequest; waiter: Pending }[] = [];
+  private readonly held: { submission: Submission; waiter: Waiter }[] = [];
   private polling = false;
+  private settling = false;   // a drain's results are being settled: an engine job's callback runs inside it
   private quiet = 0;          // the poll's steps in a row that settled nothing and saw no submission
   private submitted = false;  // a submission since the poll's last step
 
   constructor(private readonly port: OfflinePort) {}
 
-  /** Run a job. Rejects with native's reason when it is refused (at once, or when a held run is submitted); otherwise
-   *  settles with its completion, or rejects with an OfflineRunError carrying native's reason. Never rejects because
-   *  native's bound is full: the run waits here for room instead. A drain that threw (settle) is the one exception: the
-   *  jobs native still holds are no longer counted here, so a run submitted before they are drained can be refused
-   *  busy. */
-  run(request: OfflineRunRequest): Promise<OfflineRunResult> {
-    const wire = toWire(request);
-    let waiter: Pending | null = null;
-    const promise = new Promise<OfflineRunResult>((resolve, reject) => (waiter = { resolve, reject }));
-    if (this.pending.size >= OFFLINE_MAX_JOBS) this.held.push({ wire, waiter: waiter! });
-    else this.submit(wire, waiter!);
-    if (this.outstanding() > 0) setQueueLive(this, true);
-    this.pump();
-    this.poll();
-    return promise;
+  /** Run a graph job. Rejects with native's reason when it is refused (at once, or when a held run is submitted);
+   *  otherwise settles with its completion, or rejects with an OfflineRunError carrying native's reason ("cancelled"
+   *  for a cancel). Never rejects because native's bound is full: the run waits here for room instead. A drain that
+   *  threw (settle) is the one exception: the jobs native still holds are no longer counted here, so a run submitted
+   *  before they are drained can be refused busy. */
+  run(request: OfflineRunRequest): OfflineRunHandle {
+    let waiter: Waiter | null = null;
+    const promise = new Promise<OfflineRunResult>(
+      (resolve, reject) => (waiter = { kind: "graph", resolve, reject, id: null }),
+    );
+    return jobHandle(promise, this.start({ kind: "graph", wire: toWire(request) }, waiter!));
+  }
+
+  /** Run an engine job of `kind` over `spec` (a value is sent as its JSON). Rejects with native's reason when it is
+   *  refused; otherwise settles with its status, message and outputs, a failure and a cancel included. Held here past
+   *  native's bound like a graph job, the two kinds sharing it. */
+  engine(kind: EngineJobKind, spec: object | string, options: EngineRunOptions = {}): EngineRunHandle {
+    let waiter: Waiter | null = null;
+    const promise = new Promise<EngineRunResult>(
+      (resolve, reject) => (waiter = { kind: "engine", resolve, reject, id: null, options, renderedMs: 0 }),
+    );
+    const wire = { kind, spec: typeof spec === "string" ? spec : JSON.stringify(spec) };
+    return jobHandle(promise, this.start({ kind: "engine", wire }, waiter!));
   }
 
   /** Drain native's completions without waiting and settle their promises; nothing (no native call) while none is
@@ -282,22 +387,91 @@ export class OfflineJobs implements JobQueue {
     return this.pending.size + this.held.length;
   }
 
-  private submit(wire: OfflineWireRequest, waiter: Pending): void {
+  // Submit (or hold) a run, start the poll, and answer its handle's cancel.
+  private start(submission: Submission, waiter: Waiter): () => boolean {
+    if (this.pending.size >= OFFLINE_MAX_JOBS) this.held.push({ submission, waiter });
+    else this.submit(submission, waiter);
+    if (this.outstanding() > 0) setQueueLive(this, true);
+    this.pump();
+    this.poll();
+    return () => this.cancel(waiter);
+  }
+
+  private cancel(waiter: Waiter): boolean {
+    const at = this.held.findIndex((h) => h.waiter === waiter);
+    if (at >= 0) {
+      // Never submitted: it settles here, as native would settle a queued job it cancelled.
+      this.held.splice(at, 1);
+      if (waiter.kind === "graph") waiter.reject(new OfflineRunError("cancelled", []));
+      else waiter.resolve({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
+      if (this.outstanding() === 0) setQueueLive(this, false);
+      return true;
+    }
+    if (waiter.id == null || this.pending.get(waiter.id) !== waiter) return false;
+    // Native completes it cancelled (a queued job at once, a running one at its next block or chunk), and the next
+    // drain settles it, a pump's or the poll's.
+    try {
+      return this.port.cancelOfflineJob(waiter.id);
+    } catch {
+      return false;
+    }
+  }
+
+  private submit(submission: Submission, waiter: Waiter): void {
     this.submitted = true;
     let submitted: OfflineSubmitResult;
     try {
-      submitted = this.port.submitOfflineJob(wire);
+      submitted =
+        submission.kind === "graph"
+          ? this.port.submitOfflineJob(submission.wire)
+          : this.port.submitEngineJob(submission.wire);
     } catch (e) {
       // A request the port throws on (an RPC error, say) fails that run, never the queue: a held run reaches this from
       // settle's loop, where a throw would otherwise strand the rest of the promises.
       waiter.reject(asError(e));
       return;
     }
-    if (submitted.id == null) waiter.reject(new Error(submitted.error ?? "the offline job was refused"));
-    else this.pending.set(submitted.id, waiter);
+    if (submitted.id == null) {
+      const what = submission.kind === "graph" ? "offline" : "engine";
+      waiter.reject(new Error(submitted.error ?? `the ${what} job was refused`));
+    } else {
+      waiter.id = submitted.id;
+      this.pending.set(submitted.id, waiter);
+    }
+  }
+
+  // An engine job's lines and progress, as a drain reports them, to its callbacks.
+  private progress(waiter: Waiter & { kind: "engine" }, p: EngineWireProgress): void {
+    const { onLog, onProgress } = waiter.options;
+    if (onLog) {
+      for (const line of p.lines) report(() => onLog(line));
+      if (p.droppedLines > 0) {
+        const text = `[${p.droppedLines} console line${p.droppedLines === 1 ? "" : "s"} dropped]`;
+        report(() => onLog({ stream: "stderr", text }));
+      }
+    }
+    // A running maximum: a render's own count drops once, at its end, to the length it wrote (the result carries it).
+    if (p.renderedMs > waiter.renderedMs) {
+      waiter.renderedMs = p.renderedMs;
+      if (onProgress) report(() => onProgress(p.renderedMs));
+    }
   }
 
   private settle(waitMs: number): number {
+    // An engine job's callbacks run synchronously inside a settle, and one may submit or pump (a progress callback
+    // that starts a job, say): it submits, but the drain is left to this settle's caller, so one drain's results are
+    // never interleaved with another's (a nested drain would find nothing in flight and take the jobs this one has
+    // still to settle for lost).
+    if (this.settling) return this.outstanding();
+    this.settling = true;
+    try {
+      return this.drainAndSettle(waitMs);
+    } finally {
+      this.settling = false;
+    }
+  }
+
+  private drainAndSettle(waitMs: number): number {
     if (this.pending.size === 0) {
       if (this.held.length === 0) setQueueLive(this, false);
       return this.outstanding();
@@ -318,9 +492,11 @@ export class OfflineJobs implements JobQueue {
       setQueueLive(this, false);
       return 0;
     }
+    // The jobs this drain answered for: one an engine job's callback submits below is not among them.
+    const asked = new Set(this.pending.keys());
     for (const c of drained.completions) {
       const p = this.pending.get(c.id);
-      if (!p) continue;
+      if (!p || p.kind !== "graph") continue;
       this.pending.delete(c.id);
       if (!c.ok) {
         p.reject(new OfflineRunError(c.error, c.inputs));
@@ -340,16 +516,28 @@ export class OfflineJobs implements JobQueue {
         guardReason: c.guardReason,
       });
     }
+    for (const p of drained.engineProgress ?? []) {
+      const w = this.pending.get(p.id);
+      if (w?.kind === "engine") this.progress(w, p);
+    }
+    for (const c of drained.engineCompletions ?? []) {
+      const w = this.pending.get(c.id);
+      if (w?.kind !== "engine") continue;
+      this.pending.delete(c.id);
+      this.progress(w, c); // its last lines, and the length it ended at
+      const status = c.status as EngineRunStatus;
+      w.resolve({ status, message: c.message, outputs: c.outputs, renderedMs: c.renderedMs });
+    }
     // A promise whose job native no longer holds (none came back, and nothing is in flight) would otherwise be polled
     // forever: it cannot complete, so it fails. One runtime binding one engine never gets here.
-    if (this.pending.size > 0 && drained.inFlight === 0) {
-      const lost = [...this.pending.values()];
-      this.pending.clear();
-      for (const p of lost) p.reject(new Error("the engine holds no record of this offline job"));
+    if (drained.inFlight === 0) {
+      const lost = [...this.pending].filter(([id]) => asked.has(id));
+      for (const [id] of lost) this.pending.delete(id);
+      for (const [, p] of lost) p.reject(new Error("the engine holds no record of this offline job"));
     }
     while (this.held.length && this.pending.size < OFFLINE_MAX_JOBS) {
       const next = this.held.shift()!;
-      this.submit(next.wire, next.waiter);
+      this.submit(next.submission, next.waiter);
     }
     if (this.outstanding() === 0) setQueueLive(this, false);
     return this.outstanding();
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 7f91f3d8f..3ea99604b 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -18,7 +18,13 @@ import type { OpenSerialPort, SerialClient, SerialPortInfo } from "./n8/transpor
 import { savFromJson as savFromJsonTs } from "./lsdj";
 import { makeCall } from "./rpcClient";
 import { CapabilityJobs, type JobCompletion, type JobRequest, type JobSubmitResult } from "./capability";
-import { OfflineJobs, type OfflineSubmitResult, type OfflineWireDrain, type OfflineWireRequest } from "./offline";
+import {
+  OfflineJobs,
+  type EngineWireRequest,
+  type OfflineSubmitResult,
+  type OfflineWireDrain,
+  type OfflineWireRequest,
+} from "./offline";
 
 
 // --- file dialog (async, UI-direct) ------------------------------------------------------------------
@@ -117,14 +123,16 @@ function sharedJobs(): CapabilityJobs {
   return realJobs;
 }
 
-// The offline jobs' queue, shared by every emulator client of a runtime for the capability jobs' reason: native's
-// completion queue is one per engine.
+// The offline jobs' queue, graph and engine jobs alike, shared by every emulator client of a runtime for the capability
+// jobs' reason: native's completion queue is one per engine, and one drain hands back both kinds.
 let realOffline: OfflineJobs | null = null;
 
 function sharedOffline(): OfflineJobs {
   realOffline ??= new OfflineJobs({
     submitOfflineJob: (request: OfflineWireRequest) => makeCall()("submitOfflineJob", request) as OfflineSubmitResult,
+    submitEngineJob: (request: EngineWireRequest) => makeCall()("submitEngineJob", request) as OfflineSubmitResult,
     drainOfflineJobs: (waitMs: number) => makeCall()("drainOfflineJobs", waitMs) as OfflineWireDrain,
+    cancelOfflineJob: (id: number) => makeCall()("cancelOfflineJob", id) as boolean,
   });
   return realOffline;
 }
@@ -165,6 +173,7 @@ export function createEmulatorClient(): EmulatorBackend {
     // One pump for both kinds: the UI tick's, and the one after each submission.
     pumpJobs: () => jobs.pump() + offline.pump(),
     offlineRun: (request) => offline.run(request),
+    engineRun: (kind, spec, options) => offline.engine(kind, spec, options),
     rejectPendingJobs: (reason) => jobs.rejectAll(reason),
     getFrame: (id): FrameData | null => {
       const r = call("getFrame", id) as { width: number; height: number; published: boolean; data?: Uint8Array } | null;
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 9130df88a..6897ac0b7 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -6,7 +6,14 @@
 
 import type { ApuState, ApuSquareState, Backend, BreakHitBatch, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, PutResourceResult, ResourceSpec, ResourceType, TraceLine, ZipEntry } from "../src/backend";
 import { savFromJson } from "../src/lsdj";
-import type { OfflineRunRequest, OfflineRunResult } from "../src/offline";
+import {
+  jobHandle,
+  type EngineJobKind,
+  type EngineRunHandle,
+  type EngineRunOptions,
+  type OfflineRunHandle,
+  type OfflineRunRequest,
+} from "../src/offline";
 import type { GraphDocument, NodeDoc } from "../src/graph/graphDocument";
 import { rebuildKeyOf } from "../src/graph/buildGraph";
 import { canonicalJson } from "../src/graph/canonicalJson";
@@ -804,10 +811,18 @@ export class MockBackend implements Backend {
   }
 
   // The mock tier has no graph executor, so it runs no offline job: a caller learns so rather than getting buffers no
-  // graph produced.
-  offlineRun(_request: OfflineRunRequest): Promise<OfflineRunResult> {
+  // graph produced. Its handle has nothing to cancel.
+  offlineRun(_request: OfflineRunRequest): OfflineRunHandle {
     this.log.push("offlineRun");
-    return Promise.reject(new Error("offline jobs need a native host: the mock backend cannot run graphs"));
+    const error = new Error("offline jobs need a native host: the mock backend cannot run graphs");
+    return jobHandle(Promise.reject(error), () => false);
+  }
+
+  // Nor an engine to run an engine job's runtime on (a render boots a real core), so every engine job rejects.
+  engineRun(_kind: EngineJobKind, _spec: object | string, _options?: EngineRunOptions): EngineRunHandle {
+    this.log.push("engineRun");
+    const error = new Error("engine jobs need a native host: the mock backend has no engine to run them on");
+    return jobHandle(Promise.reject(error), () => false);
   }
 
   getFrame(id: number): FrameData | null {
````

Run: `sed -n '3094,3595p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test offline`
Expected: `# 16 passed, 0 skipped, 0 failed` (the report's `offline: 16 cases`).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native engine-run`
Expected: `[engine-run] hff: most reported 191700 ms, length 191700 ms`, then `# 5 passed, 0 skipped, 0 failed`.

- [ ] **Step 7: Regenerate the skip baseline**

`engine-run`'s HFF case skips without the fixtures, so the skip baseline gains its entry (1 of 5). The same
regeneration also writes `lsdj-rom`'s total as 11, which had drifted before D3; the diff below is what it writes.
Apply it, then run the regeneration itself, which must land the same bytes.

````diff
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index f660e71a8..5bea88180 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -48,6 +48,10 @@
    "skipped": 1,
    "total": 1
   },
+  "engine-run": {
+   "skipped": 1,
+   "total": 5
+  },
   "lsdj-arduinoboy-slave": {
    "skipped": 1,
    "total": 1
@@ -110,7 +114,7 @@
   },
   "lsdj-rom": {
    "skipped": 8,
-   "total": 10
+   "total": 11
   },
   "lsdj-runtime": {
    "skipped": 1,
````

Run: `sed -n '3614,3637p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native --update-skip-baseline`
Expected: `# 164 native test file(s) passed (jobs=16)`, then `# skip-baseline updated: native now records 62 skipping file(s)`.

Run: `git hash-object packages/retroplug/scripts/skip-baseline.json`
Expected: `5bea88180cab3f8c415fb9cfd04f7a7b4337ad6b`: the blob 12414e0b3 holds, so the regeneration wrote the bytes the diff above placed.

- [ ] **Step 8: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, with the plugin, `retroplug-sdl` and the CLI; the only warning is the pre-existing `-Wformat-truncation` in `PluginDSP.cpp:186`, which recompiles because `Engine.hpp` changed.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 300 | 289 passed | 11 skipped` (was 287 / 278 / 9); graph 356156 / 273, audio 1234835 / 108 and launchpad 144 / 29, unchanged.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1712154 assertions in 300 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's); the first fix pass's run, and the second added no case.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, none skipped, nothing re-recorded.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, 3 passed.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 164 native test file(s) passed` (163 and `engine-run`); only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native audio-render`
Expected: 1 passed: the old render paths are untouched. Run `render-guard` (1 passed), `risa-render` (its three files, 6, 1 and 1 passed) and `sms-render` (6 passed) the same way, each with the fixtures.

Run: `pnpm test:ui render-menu`
Expected: `# 1 passed, 0 skipped, 0 failed`. Then `pnpm test:ui render-badge`, 1 passed, and `pnpm test render`, `# 4 test file(s) passed (jobs=16)`.

Run: `pnpm test`
Expected: `# 171 test file(s) passed`.

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: the 40 UI files pass.

Run: `pnpm test:node`
Expected: 12 pass, 0 fail.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:n8`
Expected: 529 / 57 and 7 / 1, the baseline's (fakes only, no hardware).

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK`.

- [ ] **Step 9: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0: the TSan tree re-runs CMake for the new source and applies (or finds) the SameBoy patch.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (1686182 assertions in 29 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no ThreadSanitizer report, and no new suppression: `tsan.supp` still holds only its two efsw entries.

Run: `cmake --build build-asan --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (1666882 assertions in 29 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 10: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M AGENTS.md
 M cmake/sameboy.cmake
 M deps/mesen/Utilities/SimpleLock.cpp
 M deps/mesen/Utilities/SimpleLock.h
 m deps/sameboy
 M packages/native/cmake/tests.cmake
 M packages/native/src/host/engine/Engine.cpp
 M packages/native/src/host/engine/Engine.hpp
 M packages/native/src/host/engine/OfflineJobs.cpp
 M packages/native/src/host/engine/OfflineJobs.hpp
 M packages/native/src/host/engine/OfflineTypes.hpp
 M packages/native/src/host/render/RenderHost.cpp
 M packages/native/src/host/render/RenderHost.hpp
 M packages/native/src/host/rpc/BackendRpcRegistration.hpp
 M packages/native/src/host/rpc/BackendTypes.hpp
 M packages/native/src/host/rpc/EngineRpcService.cpp
 M packages/native/src/host/rpc/EngineRpcService.hpp
 M packages/retroplug/scripts/skip-baseline.json
 M packages/retroplug/src/backend.ts
 M packages/retroplug/src/kit/compile.ts
 M packages/retroplug/src/offline.ts
 M packages/retroplug/src/realBackend.ts
 M packages/retroplug/test/offline/offline.test.ts
 M packages/retroplug/testing/mockBackend.ts
?? cmake/patches/sameboy-process-statics.patch
?? packages/native/test/engine-graph/EngineJobs.test.cpp
?? packages/retroplug/test-native/engine-run.test.ts
````

````bash
git add -- cmake/patches/sameboy-process-statics.patch packages/native/test/engine-graph/EngineJobs.test.cpp \
  packages/retroplug/test-native/engine-run.test.ts AGENTS.md cmake/sameboy.cmake \
  deps/mesen/Utilities/SimpleLock.cpp deps/mesen/Utilities/SimpleLock.h packages/native/cmake/tests.cmake \
  packages/native/src/host/engine/Engine.cpp packages/native/src/host/engine/Engine.hpp \
  packages/native/src/host/engine/OfflineJobs.cpp packages/native/src/host/engine/OfflineJobs.hpp \
  packages/native/src/host/engine/OfflineTypes.hpp packages/native/src/host/render/RenderHost.cpp \
  packages/native/src/host/render/RenderHost.hpp packages/native/src/host/rpc/BackendRpcRegistration.hpp \
  packages/native/src/host/rpc/BackendTypes.hpp packages/native/src/host/rpc/EngineRpcService.cpp \
  packages/native/src/host/rpc/EngineRpcService.hpp packages/retroplug/scripts/skip-baseline.json \
  packages/retroplug/src/backend.ts packages/retroplug/src/kit/compile.ts packages/retroplug/src/offline.ts \
  packages/retroplug/src/realBackend.ts packages/retroplug/test/offline/offline.test.ts \
  packages/retroplug/testing/mockBackend.ts
git commit -F - <<'EOF'
feat(engine): engine jobs run a render on the offline pool

The offline pool runs a second kind of job (docs/modular-graph-phase-d3-design.md section 3): an engine job, whose
one kind is "render". A pool worker runs the job's body inside the pool's floating-point pin; the pool's own body
builds a RenderHost, runs the job's spec through it and destroys it, all on that thread, so the runtime, the job's own
Engine and every core and node it builds are made and freed there before the worker takes another job. Renders keep
running through RenderJobRegistry and the CLI's in-process path; nothing switches yet.

- OfflineJobs: submitEngine({kind, spec}) refuses any kind but "render"; a drain given an EngineJobsDrain also hands
  back the engine completions (status done / cancelled / error, message, outputs, final rendered length and lines)
  and every running engine job's progress with the console writes made since the drain before. The writes live in
  an EngineJobRecord, bounded between drains (256 lines, 64 KiB; past either a line is dropped and counted, an
  over-long line is cut at a UTF-8 boundary). RenderHost takes a console sink, so a job's console goes into its
  record rather than the process's stdout and stderr. A test can size the pool and give it an EngineJobBody of its
  own; starting a worker never grows the worker vector, and a thread that cannot start leaves nothing behind.
- The budget: engine jobs run on at most all but one of the pool's workers (a pool of 1 runs one, and starts a
  second thread for a graph job that arrives while a render holds the first); further renders wait in submission
  order while graph jobs start past them; both kinds count toward the 64-job bound.
- Cancel is RenderHost's cooperative check, polled per chunk; a cancel taken after the body finished still wins and
  keeps the files written; an engine's teardown cancels its engine jobs and joins them with the rest of the pool.
- RPCs: submitEngineJob in the emulator facet (19 methods, the union 91); drainOfflineJobs answers
  engineCompletions and engineProgress beside the unchanged graph completions; cancelOfflineJob takes either kind.
- TypeScript: backend.engineRun(kind, spec, { onProgress, onLog }) returns a handle, a promise of
  { status, message, outputs, renderedMs } with cancel(); offlineRun's handle gains cancel(). onProgress is a running
  maximum, since a render that detects its song's end lowers its count once to the length it wrote, which the result
  carries. One queue holds both kinds, as native's one drain answers both; a callback that submits inside a settle
  neither drains nested nor is taken for a lost job. The mock rejects engine jobs. KitJobRunner names only the
  promise it needs.
- Shared process state made thread-safe for cores on several threads at once: Mesen's SimpleLock keeps its holder
  and count in relaxed atomics (two cores built on two threads raced on MessageManager's process-wide lock, which
  ThreadSanitizer reported), and a second tracked SameBoy patch (cmake/patches/sameboy-process-statics.patch,
  applied by cmake/sameboy.cmake beside the per-channel one) covers the core's process-wide mutable statics:
  Core/random.c's generator (run at every reset and during play) and Core/camera.c's noise seed become relaxed
  atomics, and Core/sgb.c's jingle sine table is built by a load-time constructor instead of on first use (two
  Super Game Boy renders raced on it under TSan; the table's values and the jingle's bytes are unchanged).
  Deterministic builds turn the generator off, which hid it from the engine-graph binary's TSan legs. AGENTS.md
  names the five files a configure leaves modified in deps/sameboy.
- The skip baseline is the one an unfiltered fixture-absent run writes: engine-run's fixture case, and lsdj-rom's
  total, which had drifted.

Tests: retroplug-engine-graph-test [engine-jobs] (12 cases: a render's WAV byte for byte a direct RenderHost run's,
its progress and lines; a sounding LSDj render, fixture-gated; cancel queued, as it starts, running and after the body
finished; teardown with renders in flight; the budget at 3 and at 1; held renders starting in submission order; the
FP pin; renders back to back on one reused worker and in parallel; the record's bounds; the RPCs and the unknown
kind) and [engine-jobs-hff] (an auto-detected LSDj render's count, fixture-gated, apart from the sanitizer legs),
test-native/engine-run (with a fixture-gated HFF case), and offline.test's mock, handle, running-maximum and
nested-settle cases.
EOF
````

---

### Task 2: an engine job renders in whole blocks, and the render baseline records it

Design section 4 and section 7's second commit. Inside an engine job the engine's three render calls run whole
1024-frame blocks only: a call returns the frames it asks for, the rest of its last block is carried to the next call
of its shape, and a call of another shape, a rate change or a `renderBlocks` run drops it. Only the `RenderHost` an
engine job builds switches it on; the CLI's in-process render, `RenderJobRegistry`, the tool, the harness, the live
engine and the Node addon keep exact blocks, so parity is held bit for bit and the CLI's split renders are
`cmp`-identical before and after. The render library learns which way its engine runs and, in continuous blocks,
settles a split render in its stream's shape, so its stems end where its mix does. The render baseline is recorded
here, through engine jobs, once.

The baseline's bytes move against today's renders, but no detection drifted: ten songs, deterministic cores both
sides, end on the same frame through `retroplug-cli render` and through engine jobs (the header lists them).

**Files:**

- Create: `packages/retroplug/test-native/golden/render-baseline.json`, `packages/retroplug/test-native/render-baseline.test.ts`, `packages/retroplug/test-native/render-cases.ts`, `packages/retroplug/test-native/render-chunks.test.ts`
- Modify: `.github/workflows/build.yml`, `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/engine/OfflineJobs.hpp`, `packages/native/src/host/render/RenderHost.cpp`, `packages/native/src/host/render/RenderHost.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/native/src/util/Determinism.hpp`, `packages/native/test/engine-graph/EngineJobs.test.cpp`, `packages/retroplug/scripts/run-native-tests.mjs`, `packages/retroplug/scripts/skip-baseline.json`, `packages/retroplug/src/render/render.ts`, `packages/retroplug/src/render/types.ts`, `packages/retroplug/src/render/worker.ts`

- [ ] **Step 1: Write the failing native cases**

`EngineJobs.test.cpp` gains four `[engine-jobs]` cases over an mGB that MIDI plays, routed through `rp.mgb.midi` as
buildGraph wires it: in continuous blocks a render's bytes do not depend on the lengths its calls ask for, with a
second note staged between calls, where the same splits in exact blocks do differ (the built-in control); a carried
tail goes only to a call of its shape, and a `renderBlocks` or a 48-to-44.1 kHz rate change drops it; a per-channel
render on an eight-lane host carries across its calls and a mix call between drops the stems' carry; and exact blocks
carry nothing. Its direct `RenderHost` runs use continuous blocks, as the pool's body will.

````diff
diff --git a/packages/native/test/engine-graph/EngineJobs.test.cpp b/packages/native/test/engine-graph/EngineJobs.test.cpp
index e9265b30e..bdf76ee52 100644
--- a/packages/native/test/engine-graph/EngineJobs.test.cpp
+++ b/packages/native/test/engine-graph/EngineJobs.test.cpp
@@ -1,13 +1,18 @@
-// Engine jobs (docs/modular-graph-phase-d3-design.md section 3, host/engine/OfflineJobs.hpp): a render run on a
-// worker of the engine's offline pool by a RenderHost the worker builds, runs and destroys. A render job writes the
-// WAV a RenderHost run directly writes for the same spec, byte for byte, reporting its progress (rising chunk by
-// chunk to the rendered length on a fixed-length render) and its console lines as it goes; a cancel stops it, queued,
-// starting or running, and one taken after its body finished still wins, keeping the files written; an engine
-// destroyed with renders in flight cancels and joins them; renders hold at most all but one of the pool's workers, so
-// a graph job still runs beside them (a pool of 1 starting its second thread for one), and the ones held back start
-// in submission order; an engine job computes in the default floating-point mode; renders run back to back on one
-// reused worker and in parallel on several, each giving the bytes a direct run gives; the console record keeps its
-// bounds; the RPCs carry all of it; and a kind that is not "render" is refused.
+// Engine jobs (docs/modular-graph-phase-d3-design.md section 3, host/engine/OfflineJobs.hpp): a render run on a worker
+// of the engine's offline pool by a RenderHost the worker builds, runs and destroys. A render job writes the WAV a
+// RenderHost run directly in continuous blocks writes for the same spec, byte for byte, reporting its progress (rising
+// chunk by chunk to the rendered length on a fixed-length render) and its console lines as it goes; a cancel stops it,
+// queued, starting or running, and one taken after its body finished still wins, keeping the files written; an engine
+// destroyed with renders in flight cancels and joins them; renders hold at most all but one of the pool's workers, so a
+// graph job still runs beside them (a pool of 1 starting its second thread for one), and the ones held back start in
+// submission order; an engine job computes in the default floating-point mode; renders run back to back on one reused
+// worker and in parallel on several, each giving the bytes a direct run gives; the console record keeps its bounds; the
+// RPCs carry all of it; and a kind that is not "render" is refused. Continuous blocks themselves (design section 4,
+// EngineRpcService::setContinuousBlocks), over an mGB that MIDI plays: a render's bytes do not depend on the lengths
+// its calls ask for, a message staged between two calls landing where the block they end in ends, where exact blocks
+// land it where the call ended; a carried tail goes only to a call of its own shape (a per-channel render's across
+// its calls, on an eight-lane host), and a call of another shape, a renderBlocks run or a rate change drops it; exact
+// blocks carry nothing.
 //
 // Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[engine-jobs]"`. The ROMs are
 // the repo's own (resources/roms), which render silence through the render library with no song or MIDI, so on CI the
@@ -22,6 +27,7 @@
 #include <cmath>
 #include <cstdint>
 #include <cstdlib>
+#include <cstring>
 #include <filesystem>
 #include <fstream>
 #include <iterator>
@@ -44,6 +50,8 @@
 #include "host/render/RenderHost.hpp"
 #include "host/rpc/EngineRpcService.hpp"
 
+#include "GraphDocs.hpp"
+
 using rp::host::EngineJobBody;
 using rp::host::EngineJobCompletion;
 using rp::host::EngineJobHooks;
@@ -109,7 +117,7 @@ std::string fixture(const char* relative) {
 }
 
 // What a RenderHost run directly on this thread gives for `spec`: its result, and its console lines, collected rather
-// than printed.
+// than printed. In continuous blocks, as an engine job runs its RenderHost.
 struct Direct {
     retroplug::RenderHost::Result result;
     std::vector<std::string>      lines;
@@ -117,7 +125,8 @@ struct Direct {
 Direct renderDirectly(const std::string& spec) {
     Direct                d;
     retroplug::RenderHost host;
-    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); });
+    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); },
+                        retroplug::RenderHost::Blocks::Continuous);
     return d;
 }
 
@@ -744,3 +753,190 @@ TEST_CASE("the engine job RPCs carry a render's progress, lines and completion,
     CHECK(rpc.drainOfflineJobs(0).inFlight == 0);
     CHECK(e.offlineJobsOutstanding() == 0);
 }
+
+// ---- continuous blocks: the engine side of an engine job's render (EngineRpcService::setContinuousBlocks) ----------
+
+namespace {
+
+constexpr double kBlocksRate = 48000.0;  // 48 frames a millisecond, so every length below is a whole frame count
+
+// An mGB placed alone on a host, booted for 2048 ms (96 whole blocks, so nothing is carried), then sent a note to
+// sound: mGB is silent until MIDI plays it, and ignores MIDI for its first second and a half. The host is stereo, or,
+// for `perChannel`, its four stems on a pair each (8 lanes), as buildGraph places a perChannel render.
+struct BlocksRig {
+    Engine                                 engine{kBlocksRate};
+    QueuedInvoker                          invoker{engine};
+    EngineRpcService                       svc{engine, invoker};
+    rp::host::test::Roms                   roms;
+    std::vector<rp::host::test::DocSystem> systems{{1, rp::host::test::kGbType}};
+
+    explicit BlocksRig(bool continuous, bool perChannel = false) {
+        svc.setContinuousBlocks(continuous);
+        rp::host::test::withRoms(engine, systems, roms);
+        REQUIRE(place(perChannel));
+        const std::size_t boot =
+            perChannel ? svc.renderAudioPerChannel(1, 2048.0).at(0).size() : svc.renderAudio(2048.0).size();
+        REQUIRE(boot == 98304 * 2 * sizeof(float));
+        REQUIRE(svc.carriedFrames() == 0);
+        REQUIRE(svc.stageMidiIn({0x90, 60, 100}, 0));
+    }
+
+    // Plan the stereo or the per-channel document, as audioDriver's withShape swaps them around a render call.
+    bool place(bool perChannel) {
+        using namespace rp::host::test;
+        namespace d = docs_detail;
+        // The host's MIDI reaches mGB as buildGraph wires it: through its protocol node, onto the link port.
+        std::vector<std::string> nodes{docNode("midi-in", "rp.host.midi-in"), docNode("sys-1/mgb", "rp.mgb.midi")};
+        std::vector<std::string> wires{docWire("midi-in.midi", "sys-1/mgb.midi"),
+                                       docWire("sys-1/mgb.serialIn", "sys-1.serialIn")};
+        if (!perChannel) return engine.setGraph(graphRequest(stereoDocument(systems, DocExtras{nodes, wires}), {1})).ok;
+        nodes.push_back(d::sys(systems[0]));
+        for (std::uint32_t k = 0; k < 4; ++k)
+            wires.push_back(d::wire("sys-1.ch" + std::to_string(k + 1), d::out(2 * k, 2)));
+        nodes.push_back(d::node("out", "rp.host.audio-out", d::buses({2, 2, 2, 2})));
+        return engine.setGraph(graphRequest(d::document(nodes, wires), {1})).ok;
+    }
+};
+
+std::vector<std::byte> concat(std::vector<std::byte> a, const rfl::Bytestring& b) {
+    a.insert(a.end(), b.begin(), b.end());
+    return a;
+}
+
+bool sounds(const std::vector<std::byte>& pcm) {
+    for (std::size_t i = 0; i + sizeof(float) <= pcm.size(); i += sizeof(float)) {
+        float v;
+        std::memcpy(&v, pcm.data() + i, sizeof v);
+        if (v != 0.0f) return true;
+    }
+    return false;
+}
+
+} // namespace
+
+namespace {
+
+// A second of the note pulled in calls of the lengths `ms`, a second note (an octave up) staged after call `offAfter`.
+struct Split {
+    std::vector<double> ms;
+    std::size_t         offAfter;
+};
+
+std::vector<std::byte> pullSplit(bool continuous, const Split& split) {
+    BlocksRig              rig(continuous);
+    std::vector<std::byte> out;
+    for (std::size_t k = 0; k < split.ms.size(); ++k) {
+        const rfl::Bytestring pcm = rig.svc.renderAudio(split.ms[k]);
+        CHECK(pcm.size() == static_cast<std::size_t>(split.ms[k] * 48) * 2 * sizeof(float));
+        out = concat(std::move(out), pcm);
+        if (k == split.offAfter) REQUIRE(rig.svc.stageMidiIn({0x90, 72, 100}, 0));
+    }
+    CHECK(out.size() == 48000 * 2 * sizeof(float));
+    return out;
+}
+
+} // namespace
+
+TEST_CASE("in continuous blocks a render's bytes do not depend on the lengths its calls ask for",
+          "[engine-graph][engine-jobs]") {
+    // The second note is staged after a call ending at 470, 480 or 490 ms: all inside the 23rd block after the boot
+    // (469.33 to 490.67 ms), so in continuous blocks it lands where that block ends whichever it is, and a call the
+    // carry satisfies (the 10 ms after 470) renders no block for it to land in. In exact blocks it lands where the call
+    // ended, so there the splits differ: the control that shows the comparison can fail.
+    const std::vector<Split> splits{{{480.0, 520.0}, 0},
+                                    {{100.0, 370.0, 530.0}, 1},
+                                    {{37.0, 453.0, 250.0, 260.0}, 1},
+                                    {{470.0, 10.0, 13.0, 507.0}, 0}};
+    std::vector<std::vector<std::byte>> continuous, exact;
+    for (const Split& split : splits) {
+        continuous.push_back(pullSplit(true, split));
+        exact.push_back(pullSplit(false, split));
+    }
+    CHECK(sounds(continuous[0]));
+    for (std::size_t k = 1; k < splits.size(); ++k) CHECK(continuous[k] == continuous[0]);
+    CHECK(exact[1] != exact[0]);  // the second note at 470 ms against 480
+    CHECK(exact[2] != exact[0]);  // at 490 ms
+}
+
+TEST_CASE("in continuous blocks a carried tail goes only to a call of its shape, and a renderBlocks or a rate change "
+          "drops it",
+          "[engine-graph][engine-jobs]") {
+    // 50 ms is 2400 frames: three blocks run and 672 frames (14 ms) are carried. A per-system call (the same two lanes
+    // on this host, another shape) drops them and starts at the next block; the control drains them first.
+    BlocksRig dropped(true);
+    CHECK(dropped.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
+    CHECK(dropped.svc.carriedFrames() == 672);
+    const auto viaDrop = dropped.svc.renderAudioPerSystem(100.0);
+    CHECK(dropped.svc.carriedFrames() == 5120 - 4800);
+
+    BlocksRig drained(true);
+    CHECK(drained.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
+    CHECK(drained.svc.renderAudio(14.0).size() == 672 * 2 * sizeof(float));
+    CHECK(drained.svc.carriedFrames() == 0);
+    const auto viaDrain = drained.svc.renderAudioPerSystem(100.0);
+    REQUIRE(viaDrop.size() == 1);
+    REQUIRE(viaDrain.size() == 1);
+    CHECK(viaDrop[0].size() == 4800 * 2 * sizeof(float));
+    CHECK(sounds(std::vector<std::byte>(viaDrop[0].begin(), viaDrop[0].end())));
+    CHECK(viaDrop[0] == viaDrain[0]);
+
+    // A renderBlocks run's blocks come after the carried frames, which it drops.
+    CHECK(dropped.svc.carriedFrames() == 320);
+    RpcRenderBlocksRequest blocks{2, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
+    CHECK(dropped.svc.renderBlocks(blocks).error.empty());
+    CHECK(dropped.svc.carriedFrames() == 0);
+
+    // A rate change (which needs an engine with no systems) drops the frames carried at the old rate: 10 ms at 44.1 kHz
+    // then runs a new block and carries 583, where handing out the 544 frames 10 ms carried at 48 kHz would carry 103.
+    Engine           engine(kBlocksRate);
+    QueuedInvoker    invoker(engine);
+    EngineRpcService svc(engine, invoker);
+    svc.setContinuousBlocks(true);
+    REQUIRE(engine.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({}), {})).ok);
+    CHECK(svc.renderAudio(10.0).size() == 480 * 2 * sizeof(float));
+    CHECK(svc.carriedFrames() == 544);
+    REQUIRE(svc.setSampleRate(44100.0));
+    CHECK(svc.renderAudio(10.0).size() == 441 * 2 * sizeof(float));
+    CHECK(svc.carriedFrames() == 583);
+}
+
+TEST_CASE("in continuous blocks a per-channel render carries its tail to its next call, and a mix call drops it",
+          "[engine-graph][engine-jobs]") {
+    // The shape every split render streams in: mGB's four stems, eight lanes. 50 then 100 ms (the 672 frames 50 ms
+    // carries going to the second call) against one call of 150 ms: the same stems, and both carry 992 frames.
+    BlocksRig split(true, true);
+    auto      first = split.svc.renderAudioPerChannel(1, 50.0);
+    CHECK(split.svc.carriedFrames() == 672);
+    const auto second = split.svc.renderAudioPerChannel(1, 100.0);
+    CHECK(split.svc.carriedFrames() == 992);
+    BlocksRig whole(true, true);
+    const auto once = whole.svc.renderAudioPerChannel(1, 150.0);
+    CHECK(whole.svc.carriedFrames() == 992);
+    REQUIRE(first.size() == 4);
+    REQUIRE(second.size() == 4);
+    REQUIRE(once.size() == 4);
+    for (std::size_t k = 0; k < 4; ++k) {
+        const auto joined = concat(std::vector<std::byte>(first[k].begin(), first[k].end()), second[k]);
+        CHECK(joined.size() == 7200 * 2 * sizeof(float));
+        CHECK(joined == std::vector<std::byte>(once[k].begin(), once[k].end()));
+    }
+    CHECK(sounds(std::vector<std::byte>(once[0].begin(), once[0].end())));  // the note, on the first pulse stem
+
+    // A mix call between (the stereo document planned for it, as withShape does) drops the stems' 992 frames and
+    // carries its own: 10 ms takes 480 frames of a new block and carries 544, where handing out the stems' frames would
+    // carry 512. Back on the stems, the mix's 544 are dropped in turn: another new block, 544 carried, not 64.
+    REQUIRE(split.place(false));
+    CHECK(split.svc.renderAudio(10.0).size() == 480 * 2 * sizeof(float));
+    CHECK(split.svc.carriedFrames() == 544);
+    REQUIRE(split.place(true));
+    CHECK(split.svc.renderAudioPerChannel(1, 10.0).at(0).size() == 480 * 2 * sizeof(float));
+    CHECK(split.svc.carriedFrames() == 544);
+}
+
+TEST_CASE("exact blocks carry nothing: each call renders its own length", "[engine-graph][engine-jobs]") {
+    BlocksRig rig(false);
+    CHECK(rig.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
+    CHECK(rig.svc.carriedFrames() == 0);
+    CHECK(rig.svc.renderAudioPerSystem(10.0)[0].size() == 480 * 2 * sizeof(float));
+    CHECK(rig.svc.carriedFrames() == 0);
+}
````

Run: `sed -n '3846,4105p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: the build fails compiling `EngineJobs.test.cpp`: `RenderHost::Blocks`, `EngineRpcService::setContinuousBlocks` and `carriedFrames` do not exist yet.

- [ ] **Step 2: Continuous blocks in the engine job**

`EngineRpcService` gains `setContinuousBlocks(bool)` (C++ only) and `carriedFrames()` for the tests; the three render
calls share one private `pull(shape, id, streams, ms, block)` loop, which in exact mode runs exactly the old splits;
the carry is `{shape, id, rate, frames, streams}`, dropped by `dropCarry()`, which `renderBlocks` calls first; and the
`scratchL_` / `scratchR_` members go. Its comment says a block's host MIDI out is gathered whole, so out of a block
whose tail is carried it can be drained before that tail's audio is handed out. `RenderHost::run` takes a `Blocks`
argument (default exact), sets the service from it and puts `blocks: "exact"|"continuous"` on the plugin namespace;
`OfflineJobs::renderBody` runs continuous. `Determinism.hpp`'s comment names who sets the variable.

````diff
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index 303a8120e..79b448b29 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -427,9 +427,12 @@ EngineJobCompletion OfflineJobs::runEngine(Job& job) {
 
 EngineJobOutcome OfflineJobs::renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks) {
     // Built, run and destroyed on this thread: the runtime anchors its stack check to the thread that makes it, and
-    // the job's Engine, its cores and its plan's nodes go before this worker takes another job.
+    // the job's Engine, its cores and its plan's nodes go before this worker takes another job. In continuous blocks:
+    // the engine runs whole blocks whatever each of the render's calls asks for (docs/modular-graph-phase-d3-design.md
+    // section 4).
     retroplug::RenderHost         host;
-    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console);
+    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console,
+                                               retroplug::RenderHost::Blocks::Continuous);
     return {std::move(r.status), std::move(r.message), std::move(r.outputs)};
 }
 
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
index 3ed7d72ca..0dad9c9ad 100644
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -65,12 +65,13 @@
 // Engine jobs (docs/modular-graph-phase-d3-design.md section 3). The pool runs a second kind of job: a script bundle
 // run with a QuickJS runtime and an Engine of its own, which carries everything a graph job's host does not (system
 // nodes, ROM and battery resources, the protocol script packs, an advancing transport). The one kind is "render": the
-// worker builds a retroplug::RenderHost (host/render/RenderHost.hpp), runs the job's spec through it and destroys it,
-// all on that worker and inside the job's floating-point pin, so the runtime, the job's Engine, every core it boots
-// and every node and resampler its plan builds are made and freed on the one thread, before the worker takes its next
-// job. Nothing a render job touches belongs to the engine that owns this pool: the RenderHost's Engine is its own, as
-// are its resource table, snapshot registry and script host. What it shares with other jobs, and with the live engine
-// building cores on its control thread and running them on its audio thread, is the process's:
+// worker builds a retroplug::RenderHost (host/render/RenderHost.hpp), runs the job's spec through it in continuous
+// blocks (whole blocks whatever each render call asks for, design section 4) and destroys it, all on that worker and
+// inside the job's floating-point pin, so the runtime, the job's Engine, every core it boots and every node and
+// resampler its plan builds are made and freed on the one thread, before the worker takes its next job. Nothing a
+// render job touches belongs to the engine that owns this pool: the RenderHost's Engine is its own, as are its resource
+// table, snapshot registry and script host. What it shares with other jobs, and with the live engine building cores on
+// its control thread and running them on its audio thread, is the process's:
 //   - Mesen's per-process home folder, under which each core's scratch paths (the SMS staged ROM, the GBA BIOS, the
 //     NES SD card) are numbered per core by atomic counters rather than by system id, which every job's Engine starts
 //     at 1;
@@ -256,7 +257,8 @@ private:
     OfflineCompletion run(Job& job);
     // Runs an engine job's body on this thread, inside the floating-point pin.
     EngineJobCompletion runEngine(Job& job);
-    // The pool's own engine job body: builds a RenderHost on this thread, runs the spec, and destroys it here.
+    // The pool's own engine job body: builds a RenderHost on this thread, runs the spec in continuous blocks, and
+    // destroys it here.
     static EngineJobOutcome renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks);
 
     // Filled by the constructor and never changed: the workers read it without the lock.
diff --git a/packages/native/src/host/render/RenderHost.cpp b/packages/native/src/host/render/RenderHost.cpp
index 908ab18f0..935670c53 100644
--- a/packages/native/src/host/render/RenderHost.cpp
+++ b/packages/native/src/host/render/RenderHost.cpp
@@ -212,7 +212,7 @@ RenderHost::~RenderHost() {
 }
 
 RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled,
-                                   ConsoleFn onConsole) {
+                                   ConsoleFn onConsole, Blocks blocks) {
     renderedFn_ = std::move(onRendered);
     cancelFn_ = std::move(isCancelled);
     consoleFn_ = std::move(onConsole);
@@ -225,6 +225,7 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
     QueuedInvoker invoker{engine};
     HostRpcService hostSvc;
     EngineRpcService engineSvc{engine, invoker};
+    engineSvc.setContinuousBlocks(blocks == Blocks::Continuous);
     DebugRpcService debugSvc{engine};
     AudioDriverRpcService driver{engine, invoker};
 
@@ -242,7 +243,8 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
     // and driven on the same (worker) thread, but re-anchor before entering JS to be safe (as ScriptHost does).
     JS_UpdateStackTop(rt_);
 
-    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson] } — the worker reads args[0].
+    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson], blocks }: the worker reads args[0], and
+    // `blocks` ("exact" or "continuous") tells the render library which way its calls run.
     // bindRpcSend here is the bare-runtime one (this host has no TjsHostRuntime), which is why
     // installPluginNamespace takes a callback rather than a runtime.
     installPluginNamespace(ctx_, [&](JSValue ns) {
@@ -250,6 +252,8 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
         JSValue args = JS_NewArray(ctx_);
         JS_SetPropertyUint32(ctx_, args, 0, JS_NewStringLen(ctx_, jobJson.data(), jobJson.size()));
         JS_SetPropertyStr(ctx_, ns, "args", args);
+        const char* mode = blocks == Blocks::Continuous ? "continuous" : "exact";
+        JS_SetPropertyStr(ctx_, ns, "blocks", JS_NewString(ctx_, mode));
     });
 
     // Run the worker bundle as global-code bytecode. Its top-level main() reads the spec, renders, and
diff --git a/packages/native/src/host/render/RenderHost.hpp b/packages/native/src/host/render/RenderHost.hpp
index a58587aaa..c06b716e0 100644
--- a/packages/native/src/host/render/RenderHost.hpp
+++ b/packages/native/src/host/render/RenderHost.hpp
@@ -31,6 +31,12 @@ public:
     // ones (console.warn / error / trace) and for a script exception's report.
     using ConsoleFn  = std::function<void(bool error, const char* text)>;
 
+    // How the job's engine runs the blocks the worker's render calls pull. Exact: each call as whole blocks and then
+    // one of the remainder, as the CLI's in-process render runs them. Continuous: whole blocks only, the frames past a
+    // call carried to the next (EngineRpcService::setContinuousBlocks), so a render's bytes do not depend on how much
+    // each call asks for; an engine job runs this way (docs/modular-graph-phase-d3-design.md section 4).
+    enum class Blocks { Exact, Continuous };
+
     struct Result {
         std::string status;                // "done" | "cancelled" | "error"; empty if the worker never reported
         std::string message;               // error detail (when status == "error")
@@ -48,7 +54,7 @@ public:
     // onConsole the worker's console writes go to the process's stdout and stderr. Returns the worker's reported
     // result (status "error" with a message on any internal failure).
     Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {},
-               ConsoleFn onConsole = {});
+               ConsoleFn onConsole = {}, Blocks blocks = Blocks::Exact);
 
     // Called by the bound JS thunks (which recover the host via the JS context opaque). Public so the
     // free-function thunks in the .cpp can reach them; not part of the external API.
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 4f10e4511..7274b9321 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -366,115 +366,136 @@ bool EngineRpcService::stageNodeMidi(std::string nodeId, std::vector<std::uint8_
     return true;
 }
 
-rfl::Bytestring EngineRpcService::renderAudio(double ms) {
-    // This thread calls processBlock itself, which only the Engine's owning thread may do.
-    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};
-    if (!rendersInto(2)) return {};
-    scratchL_.resize(kBlockSize);  // idempotent — no per-call realloc after the first
-    scratchR_.resize(kBlockSize);
-
-    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
-    std::vector<float> out;
-    out.reserve(total * 2);
-    // The Engine owns transport + the ppq clock and consumes any host MIDI staged via stageMidiIn on
-    // the first block it renders.
-    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
-        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
-        engine_.processBlock(frames, scratchL_.data(), scratchR_.data());
+void EngineRpcService::setContinuousBlocks(bool on) {
+    continuous_ = on;
+    dropCarry();
+}
+
+void EngineRpcService::dropCarry() {
+    carry_.shape = Shape::None;
+    carry_.frames = 0;
+    carry_.streams.clear();
+}
+
+std::vector<std::vector<float>> EngineRpcService::pull(Shape shape, std::uint32_t id, std::size_t streams, double ms,
+                                                       const BlockFn& block) {
+    const double                    rate = engine_.sampleRate();
+    const std::uint64_t             total = static_cast<std::uint64_t>(ms * rate / 1000.0);
+    std::vector<std::vector<float>> out(streams);  // per stream: interleaved L,R,L,R...
+    for (auto& o : out) o.reserve(total * 2);
+
+    std::uint64_t done = 0;
+    if (continuous_) {
+        // A carry of another shape, stream count or rate is not this call's audio (setContinuousBlocks).
+        if (carry_.shape != shape || carry_.id != id || carry_.rate != rate || carry_.streams.size() != streams)
+            dropCarry();
+        const std::uint64_t take = std::min<std::uint64_t>(carry_.frames, total);
+        for (std::size_t i = 0; i < carry_.streams.size() && take > 0; ++i) {
+            auto&      c = carry_.streams[i];
+            const auto end = c.begin() + static_cast<std::ptrdiff_t>(take * 2);
+            out[i].insert(out[i].end(), c.begin(), end);
+            c.erase(c.begin(), end);
+        }
+        carry_.frames -= take;
+        done = take;
+    }
+    if (done == total) return out;
+
+    // One persistent L/R block buffer per stream, listed as the lanes a block renders into (L0, R0, L1, R1, ...).
+    std::vector<std::vector<float>> lanes(streams * 2, std::vector<float>(kBlockSize));
+    std::vector<float*>             ptrs(streams * 2);
+    for (std::size_t k = 0; k < ptrs.size(); ++k) ptrs[k] = lanes[k].data();
+
+    // The Engine owns transport + the ppq clock and consumes any host MIDI staged via stageMidiIn on the first block
+    // it renders. Every carried frame was handed out before this point, so a block's frames follow the call's in time.
+    while (done < total) {
+        const std::uint64_t want = total - done;
+        const auto frames =
+            continuous_ ? kBlockSize : static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, want));
+        block(frames, ptrs.data());
         accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
-        for (std::uint32_t f = 0; f < frames; ++f) {
-            out.push_back(scratchL_[f]);  // interleave L,R,L,R…
-            out.push_back(scratchR_[f]);
+        const auto give = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, want));
+        for (std::size_t i = 0; i < streams; ++i) {
+            const float* l = ptrs[i * 2];
+            const float* r = ptrs[i * 2 + 1];
+            for (std::uint32_t f = 0; f < give; ++f) {
+                out[i].push_back(l[f]);  // interleave L,R,L,R…
+                out[i].push_back(r[f]);
+            }
         }
+        if (give < frames) {
+            // Continuous blocks only: the rest of the block is the next call's, if that call is of this shape.
+            carry_.shape = shape;
+            carry_.id = id;
+            carry_.rate = rate;
+            carry_.frames = frames - give;
+            carry_.streams.assign(streams, {});
+            for (std::size_t i = 0; i < streams; ++i) {
+                carry_.streams[i].reserve(carry_.frames * 2);
+                for (std::uint32_t f = give; f < frames; ++f) {
+                    carry_.streams[i].push_back(ptrs[i * 2][f]);
+                    carry_.streams[i].push_back(ptrs[i * 2 + 1][f]);
+                }
+            }
+        }
+        done += give;
     }
-    const auto* p = reinterpret_cast<const std::byte*>(out.data());
-    return rfl::Bytestring(p, p + out.size() * sizeof(float));
+    return out;
 }
 
-std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
+namespace {
+
+std::vector<rfl::Bytestring> toBytestrings(std::vector<std::vector<float>>&& streams) {
     std::vector<rfl::Bytestring> result;
-    if (ms <= 0.0 || invoker_.audioThreadOwns()) return result;  // as renderAudio
-    const std::size_t n = engine_.systemCount();
-    if (n == 0) return result;
-    if (!rendersInto(2 * n)) return result;
-
-    // One persistent L/R block buffer per system; the PerSystem shape routes each core into its own pair,
-    // so linked systems interleave exactly as they do in the mixed path.
-    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
-    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
-    std::vector<float*> ls(n), rs(n), lanes;  // lanes: the pairs as one channel list (L0, R0, L1, R1, …)
-    std::vector<std::vector<float>> out(n);  // per system: interleaved L,R,L,R…
-    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
-    for (std::size_t i = 0; i < n; ++i) {
-        ls[i] = bl[i].data();
-        rs[i] = br[i].data();
-        lanes.insert(lanes.end(), {ls[i], rs[i]});
-        out[i].reserve(total * 2);
+    result.reserve(streams.size());
+    for (const auto& s : streams) {
+        const auto* p = reinterpret_cast<const std::byte*>(s.data());
+        result.emplace_back(p, p + s.size() * sizeof(float));
     }
+    return result;
+}
 
-    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
-        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
-        engine_.processBlock(frames, lanes.data(), lanes.size());
-        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
-        for (std::size_t i = 0; i < n; ++i)
-            for (std::uint32_t f = 0; f < frames; ++f) {
-                out[i].push_back(ls[i][f]);  // interleave L,R,L,R…
-                out[i].push_back(rs[i][f]);
-            }
-    }
+} // namespace
 
-    result.reserve(n);
-    for (std::size_t i = 0; i < n; ++i) {
-        const auto* p = reinterpret_cast<const std::byte*>(out[i].data());
-        result.emplace_back(p, p + out[i].size() * sizeof(float));
-    }
-    return result;
+rfl::Bytestring EngineRpcService::renderAudio(double ms) {
+    // This thread calls processBlock itself, which only the Engine's owning thread may do.
+    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};
+    if (!rendersInto(2)) return {};
+    auto out = pull(Shape::Mix, 0, 1, ms, [this](std::uint32_t frames, float* const* lanes) {
+        engine_.processBlock(frames, lanes[0], lanes[1]);
+    });
+    const auto* p = reinterpret_cast<const std::byte*>(out[0].data());
+    return rfl::Bytestring(p, p + out[0].size() * sizeof(float));
+}
+
+std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
+    if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};  // as renderAudio
+    const std::size_t n = engine_.systemCount();
+    if (n == 0) return {};
+    if (!rendersInto(2 * n)) return {};
+    // The PerSystem shape routes each core into its own pair, so linked systems interleave exactly as they do in the
+    // mixed path.
+    return toBytestrings(pull(Shape::PerSystem, 0, n, ms, [this, n](std::uint32_t frames, float* const* lanes) {
+        engine_.processBlock(frames, lanes, 2 * n);
+    }));
 }
 
 std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerChannel(std::uint32_t id, double ms) {
-    std::vector<rfl::Bytestring> result;
-    if (invoker_.audioThreadOwns()) return result;  // as renderAudio
+    if (invoker_.audioThreadOwns()) return {};  // as renderAudio
     // Single-system only: the PerChannel shape fans one system's streams, so a second system's would collide
     // into these buffers. The mixed/per-system paths are the multi-system routes.
-    if (ms <= 0.0 || engine_.systemCount() != 1) return result;
+    if (ms <= 0.0 || engine_.systemCount() != 1) return {};
     SystemBase* sys = engine_.findSystem(id);
-    if (sys == nullptr) return result;
+    if (sys == nullptr) return {};
 
     // The target's output streams (Game Boy = 4 stereo: Pulse 1/Pulse 2/Wave/Noise; default = 1 "Mix").
     const std::size_t n = sys->channelLayout().size();
-    if (n == 0) return result;
-    if (!rendersInto(2 * n)) return result;
-
-    // One persistent L/R block buffer per stream; the PerChannel shape routes each stream into its own pair.
-    std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
-    std::vector<std::vector<float>> br(n, std::vector<float>(kBlockSize));
-    std::vector<float*> ls(n), rs(n), lanes;  // lanes: the pairs as one channel list (L0, R0, L1, R1, …)
-    std::vector<std::vector<float>> out(n);  // per stream: interleaved L,R,L,R…
-    const std::uint64_t total = static_cast<std::uint64_t>(ms * engine_.sampleRate() / 1000.0);
-    for (std::size_t i = 0; i < n; ++i) {
-        ls[i] = bl[i].data();
-        rs[i] = br[i].data();
-        lanes.insert(lanes.end(), {ls[i], rs[i]});
-        out[i].reserve(total * 2);
-    }
-
-    for (std::uint64_t s = 0; s < total; s += kBlockSize) {
-        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kBlockSize, total - s));
-        engine_.processBlock(frames, lanes.data(), lanes.size());
-        accumulateMidiOut();  // gather this block's host MIDI out before the next block clears it
-        for (std::size_t i = 0; i < n; ++i)
-            for (std::uint32_t f = 0; f < frames; ++f) {
-                out[i].push_back(ls[i][f]);  // interleave L,R,L,R…
-                out[i].push_back(rs[i][f]);
-            }
-    }
-
-    result.reserve(n);
-    for (std::size_t i = 0; i < n; ++i) {
-        const auto* p = reinterpret_cast<const std::byte*>(out[i].data());
-        result.emplace_back(p, p + out[i].size() * sizeof(float));
-    }
-    return result;
+    if (n == 0) return {};
+    if (!rendersInto(2 * n)) return {};
+    // The PerChannel shape routes each stream into its own pair.
+    return toBytestrings(pull(Shape::PerChannel, id, n, ms, [this, n](std::uint32_t frames, float* const* lanes) {
+        engine_.processBlock(frames, lanes, 2 * n);
+    }));
 }
 
 double EngineRpcService::sampleRate() const {
@@ -602,6 +623,8 @@ RpcRenderBlocksResult EngineRpcService::renderBlocks(RpcRenderBlocksRequest req)
     // no plan placed the surface waits in the link, and the surface-in node of a plan that does place it drains what the
     // link held on its first block after it bound the link, forwarding none of it. A press staged while the surface is
     // bound is delivered at the next block - renderBlocks' first included - as stageControllerIn promises.
+    // Blocks of the caller's own lengths: frames a continuous render carried would come after them, out of order.
+    dropCarry();
     std::uint64_t total = 0;
     for (const RpcRenderBlock& b : req.blocks) total += b.frames;
     // Whole-run planar lanes, zero-filled; each block renders straight into its slice of the result.
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 7733bc62d..51db8cdb4 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -1,6 +1,7 @@
 #pragma once
 
 #include <cstdint>
+#include <functional>
 #include <optional>
 #include <string>
 #include <vector>
@@ -87,6 +88,21 @@ public:
     // id resolves against the plan running when it drains. The contract is optimistic: refuse only what can never be
     // delivered, an empty message (a node the plan does not place takes nothing, silently).
     bool            stageNodeMidi(std::string nodeId, std::vector<std::uint8_t> bytes);
+    // Continuous blocks (docs/modular-graph-phase-d3-design.md section 4), which only an engine job's render turns on
+    // (RenderHost): renderAudio, renderAudioPerSystem and renderAudioPerChannel then run whole kBlockSize blocks and
+    // nothing else. A call returns the frames it asks for; the rest of its last block waits for the next call, so the
+    // engine's blocks fall where they would whatever lengths the calls ask for. Those carried frames belong to the
+    // shape that rendered them (the mix, the per-system lanes, or one system's channels, with its stream count) and to
+    // the rate: a call of another shape, or after the rate changed, drops them rather than hand one shape's lanes out
+    // as another's or one rate's frames out at another, and so does renderBlocks, whose blocks come after them in
+    // time. The emulation behind dropped frames is not undone: the next call's audio starts where the last block
+    // ended. A block's host MIDI out is gathered whole (drainMidiOut), so out of a block whose tail is carried it can
+    // be drained before that tail's audio is handed out; its frames stay block-relative. Off, as every other host runs
+    // it, a call renders whole blocks and then one of the remainder: the splits parity records. Not an RPC: the host
+    // that builds this service sets it before any script runs.
+    void            setContinuousBlocks(bool on);
+    // The frames the last render call in continuous blocks left for the next one (none in exact blocks): for tests.
+    std::size_t     carriedFrames() const { return carry_.frames; }
     rfl::Bytestring renderAudio(double ms);
     // Per-system audio: each live system's interleaved-stereo PCM, in slot order (marshals to
     // Uint8Array[]). Isolates each core so LSDj link-cable sync is provable (a follower's own RMS).
@@ -153,6 +169,16 @@ public:
     std::vector<std::string>            dspTraceNames();
 
 private:
+    // Which render call a carried block came from (setContinuousBlocks): a carry is handed only to a call of its shape.
+    enum class Shape : std::uint8_t { None, Mix, PerSystem, PerChannel };
+    // One block of `frames` into the call's lanes, each stream's L then R (L0, R0, L1, R1, ...), of kBlockSize floats.
+    using BlockFn = std::function<void(std::uint32_t frames, float* const* lanes)>;
+    // The frames `ms` asks for at the engine's rate, in `streams` interleaved-stereo outputs (L,R,L,R...), rendered by
+    // `block`: in exact splits, or in whole blocks with the carry setContinuousBlocks describes. `id` is the system a
+    // PerChannel call fans, 0 for the others.
+    std::vector<std::vector<float>> pull(Shape shape, std::uint32_t id, std::size_t streams, double ms,
+                                         const BlockFn& block);
+    void dropCarry();
     bool rendersInto(std::size_t channels) const;
     // Append the block's host MIDI out (Engine::midiOut(), cleared each block) to accumMidiOut_.
     // Called after each processBlock in the render loops so drainMidiOut() sees the whole window.
@@ -162,8 +188,16 @@ private:
     QueuedInvoker& invoker_;   // the one mutation path (push; flushes inline when quiescent)
 
     static constexpr std::uint32_t kBlockSize = 1024;
-    std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
-    std::vector<float>       scratchR_;
+    // The frames past the last call's in its last block, in continuous blocks; empty otherwise.
+    struct Carry {
+        Shape                           shape = Shape::None;
+        std::uint32_t                   id = 0;
+        double                          rate = 0.0;
+        std::size_t                     frames = 0;
+        std::vector<std::vector<float>> streams;  // per stream: interleaved L,R... not yet handed out
+    };
+    bool                     continuous_ = false;
+    Carry                    carry_;
     std::vector<RpcMidiOut> accumMidiOut_;  // host MIDI out gathered across a render window (drainMidiOut)
     rp::host::HarnessSurfaceLink* harnessSurface_ = nullptr;  // the headless host's own surface, if it has one
 };
diff --git a/packages/native/src/util/Determinism.hpp b/packages/native/src/util/Determinism.hpp
index 2e8a69a6e..e8ef48267 100644
--- a/packages/native/src/util/Determinism.hpp
+++ b/packages/native/src/util/Determinism.hpp
@@ -4,9 +4,11 @@
 // GB_random() returns 0 (it otherwise seeds from time(NULL), filling WRAM / HRAM / OAM / wave RAM at
 // reset), Mesen NES RAM powers on as all zeros (NesConfig defaults to Random), and the SMS/GG VDP starts
 // at scanline 0 (SmsConfig::RandomizeVdpStartScanline, a field RetroPlug adds to the vendored Mesen).
-// Real hardware powers on unpredictably, which is what the app keeps; only the parity suite (test-native/parity) sets this, so
-// a recorded hash cannot depend on the wall clock. Carts with a real-time clock (MBC3 timer, HuC3, TPP1)
-// still read the host clock in SameBoy's default RTC mode, and no parity case uses one.
+// Real hardware powers on unpredictably, which is what the app keeps. The test-native runner sets this for the
+// parity suite (test-native/parity) and for render-baseline and render-chunks (its DETERMINISTIC_FILES), and so for
+// every engine job's Engine in those processes, so a recorded hash cannot depend on the wall clock; the C++
+// engine-graph binary sets it for itself (test/engine-graph/Deterministic.cpp). Carts with a real-time clock (MBC3
+// timer, HuC3, TPP1) still read the host clock in SameBoy's default RTC mode, and no parity or render case uses one.
 
 #include <cstdlib>
 #include <cstring>
````

Run: `sed -n '4125,4573p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-engine-graph-test "[engine-jobs]"`
Expected: `test cases: 16 | 15 passed | 1 skipped`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test "[engine-jobs],[engine-jobs-hff]"`
Expected: `All tests passed (13673 assertions in 17 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`: exact blocks run the old splits, bit for bit.

- [ ] **Step 3: Write the render baseline and its chunk test**

`test-native/render-cases.ts` (not a test) holds the matrix: the 13 cases with their fixtures, the smsggdj batteries
it authors (METRO, HIGH, CHORD), `missingFixture`, `writeScratch` and `renderCase`, which runs a case through
`engineRun`, reads each file back, hashes it with FNV-1a-64, checks it sounds and deletes it.
`render-baseline.test.ts` holds the matrix to the golden: its names are the golden's keys; the fixture-free, LSDj and
risa renders write the files the golden records; and an LSDj split render's summed stems match its mix at lag 0 (the
peak of the normalised cross-correlation over plus or minus 1024 frames). It records under `RP_RENDER_GOLDEN_UPDATE=1`
and refuses off the target, for a self-disagreeing case and for a run missing any case. `render-chunks.test.ts`
renders at pulls of 100, 37 and 250 ms (100, 50 and 200 for the HFF case): a host-synced smsggdj render, a fixed
smsggdj render, LSDj stems with Start pressed and risa pins with three gestures write the same files at every pull,
and an LSDj HFF render writes the same samples, ending within a pull. The runner defines `__RENDER_GOLDEN__` and
powers both files' cores on in a fixed state (`DETERMINISTIC_FILES`). The golden is quoted whole: 95 lines, one per
file, and the recording in the next step proves the recorder lands these bytes.

Create `packages/retroplug/test-native/render-cases.ts`:

````ts
// The renders the render baseline records and the chunk test pulls (render-baseline.test.ts, render-chunks.test.ts),
// each through an engine job (backend.engineRun, docs/modular-graph-phase-d3-design.md sections 3 and 4): their specs,
// the scratch files they read, and how what they write is read back. Not a `.test.ts`, so the runner's walk skips it.
//
// What sounds. The repo's own ROMs (resources/roms) render digital silence through the render library: mGB and the
// BlipToaster carts play only what MIDI sends them, which a render does not do, and smsggdj boots to a blank song.
// So the cases that need no fixture are smsggdj's, playing songs this file authors into a battery (smsSyncSong's
// metronome, an octave up, and a three-channel chord) and loads by index or by name, one of them clocked by the host's
// running transport, and mGB booted as a Super Game Boy, whose boot jingle sounds with no MIDI. The jingle is the SGB
// BIOS's own (SameBoy's sgb.c), mixed after the APU, so it reaches the mix and no stem: a Game Boy split render and
// every NES render still need a fixture, or a render that plays MIDI (design section 8). The fixture tree adds LSDj
// (tripledipper's working song, fixed; telemelt's songs to their HFF stop, picked by name and by index) and risa
// (ecoli_soul to its HFF stop, at PAL and NTSC; blumarbl's split renders, fixed). The GBA fixture (nanoloop) renders
// silence without a BIOS, so no GBA case is here.
import { createRealBackend } from "../src/realBackend";
import { buildSav } from "../src/smsggdj/codec/sav";
import { savBytes } from "../test/risa/fixtures";
import type { EngineRunResult } from "../src/offline";
import {
  buildMetronomeBlock,
  buildConfigBlock,
  SMS_SYNC_OFF,
  SMS_SYNC_IN24,
  CHAIN_ENTRIES,
  HIT_NOTE,
  P_CHAINS,
  P_PHRASES,
  P_SONG,
  STEPS_PER_PHRASE,
} from "./smsSyncSong";
import { fnv1a64Hex } from "./kit/sources";

declare const __REPO_RESOURCES_DIR__: string;
declare const __RESOURCES_DIR__: string;

export type Backend = ReturnType<typeof createRealBackend>;

/** Which tree a case's ROM comes from: the repo's own ("none"), or the fixture tree (RETROPLUG_RESOURCES_DIR). */
export type Fixture = "none" | "lsdj" | "risa";

export interface RenderCase {
  name: string;
  fixture: Fixture;
  /** The job spec without its `out`, given this run's scratch prefix (the files `writeScratch` wrote). */
  spec: (scratch: string) => Record<string, unknown>;
  /** The files it writes, by the suffix render.ts names them with: "" for a mix, "_<stream>" for a split. */
  outputs: string[];
  /** Outputs that may be silent (a stem its song never plays); every other output, and the case, must sound. */
  silent?: string[];
}

const MGB = __REPO_RESOURCES_DIR__ + "/roms/mGB.gb";
const SMS = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const GG = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg";
const TRIPLEDIPPER = __RESOURCES_DIR__ + "/roms/tripledipper942.gbc"; // its working song, in the sibling .sav
const TELEMELT = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.gbc"; // five songs, in the sibling .sav
const RISA = __RESOURCES_DIR__ + "/roms/risa/risa-v2.2.1/risa-2.2.1-pal.nes";
const ECOLI = __RESOURCES_DIR__ + "/roms/risa/ecoli_soul.srm"; // a song whose last track HFFs (59 s at PAL)

const GB_STEMS = ["_pulse1", "_pulse2", "_wave", "_noise"];
const NES_CHANNELS = ["_square1", "_square2", "_triangle", "_noise", "_dmc"];
const NES_PINS = ["_pulse", "_tnd", "_expansion"];

const smsSav = (scratch: string) => `${scratch}-smsggdj.sav`;
const smsSyncSav = (scratch: string) => `${scratch}-smsggdj-sync.sav`;
const blumarblSav = (scratch: string) => `${scratch}-blumarbl.srm`;

export const CASES: RenderCase[] = [
  {
    name: "smsggdj/sms/index-0/mix",
    fixture: "none",
    spec: (d) => ({ rom: SMS, sav: smsSav(d), songIndex: 0, durationMs: 4000 }),
    outputs: [""],
  },
  {
    name: "smsggdj/sms/name-chord/mix-48000",
    fixture: "none",
    spec: (d) => ({ rom: SMS, sav: smsSav(d), song: "chord", durationMs: 3000, sampleRate: 48000 }),
    outputs: [""],
  },
  {
    name: "smsggdj/gg/index-1/mix",
    fixture: "none",
    spec: (d) => ({ rom: GG, sav: smsSav(d), songIndex: 1, durationMs: 3000 }),
    outputs: [""],
  },
  {
    // The cart clocked by the host: its sync node turns the running transport into the cart's clock, block by block.
    name: "smsggdj/sms/sync-in24/transport-120bpm",
    fixture: "none",
    spec: (d) => ({
      rom: SMS,
      sav: smsSyncSav(d),
      songIndex: 0,
      durationMs: 4000,
      transport: true,
      bpm: 120,
      roles: [{ kind: "sms-sync", config: { machine: "sms" } }],
    }),
    outputs: [""],
  },
  {
    // The Super Game Boy's boot jingle, unpressed: SameBoy's render path on CI.
    name: "sameboy/mgb-sgb/no-start/mix",
    fixture: "none",
    spec: () => ({ rom: MGB, start: false, durationMs: 4000, roles: [{ kind: "sameboy", config: { model: "sgb" } }] }),
    outputs: [""],
  },
  {
    name: "lsdj/tripledipper/fixed-5s/mix",
    fixture: "lsdj",
    spec: () => ({ rom: TRIPLEDIPPER, durationMs: 5000 }),
    outputs: [""],
  },
  {
    name: "lsdj/tripledipper/fixed-5s/channels",
    fixture: "lsdj",
    spec: () => ({ rom: TRIPLEDIPPER, durationMs: 5000, split: "channels" }),
    outputs: GB_STEMS,
  },
  {
    name: "lsdj/telemelt/hff/name-machete/mix",
    fixture: "lsdj",
    spec: () => ({ rom: TELEMELT, song: "machete" }),
    outputs: [""],
  },
  {
    name: "lsdj/telemelt/hff/index-3/channels",
    fixture: "lsdj",
    spec: () => ({ rom: TELEMELT, songIndex: 3, split: "channels" }),
    outputs: GB_STEMS,
  },
  {
    name: "risa/ecoli/hff/mix",
    fixture: "risa",
    spec: () => ({ rom: RISA, sav: ECOLI }),
    outputs: [""],
  },
  {
    name: "risa/ecoli/hff/region-ntsc/mix",
    fixture: "risa",
    spec: () => ({ rom: RISA, sav: ECOLI, roles: [{ kind: "mesen", config: { region: "ntsc" } }] }),
    outputs: [""],
  },
  {
    name: "risa/blumarbl/fixed-3s/channels",
    fixture: "risa",
    spec: (d) => ({ rom: RISA, sav: blumarblSav(d), songIndex: 0, durationMs: 3000, split: "channels" }),
    outputs: NES_CHANNELS,
    silent: ["_square1", "_triangle", "_dmc"], // its first three seconds play on the other two
  },
  {
    name: "risa/blumarbl/fixed-3s/pins",
    fixture: "risa",
    spec: (d) => ({ rom: RISA, sav: blumarblSav(d), songIndex: 0, durationMs: 3000, split: "pins" }),
    outputs: NES_PINS,
  },
];

export const caseNamed = (name: string): RenderCase => {
  const c = CASES.find((k) => k.name === name);
  if (!c) throw new Error(`no render case named ${name}`);
  return c;
};

/** The ROMs and batteries `fixture` needs, or why they are not all there. */
export function missingFixture(be: Backend, fixture: Fixture): string | null {
  const need = fixture === "lsdj" ? [TRIPLEDIPPER, TELEMELT] : fixture === "risa" ? [RISA, ECOLI] : [MGB, SMS, GG];
  const missing = need.filter((p) => !be.fileExists(p));
  return missing.length ? `missing ${missing.join(", ")}` : null;
}

// smsggdj's battery: three songs. METRO is smsSyncSong's metronome (a note a beat on the first tone channel), HIGH
// the same an octave up, CHORD a major triad on the three tone channels (the metronome's phrase through chains 1 and
// 2, transposed 4 and 7 semitones).
function smsggdjSav(): Uint8Array {
  const metro = buildMetronomeBlock();
  const high = buildMetronomeBlock();
  for (let i = P_PHRASES; i < P_PHRASES + STEPS_PER_PHRASE * 4; i += 4) if (high[i] === HIT_NOTE) high[i] += 12;
  const chord = buildMetronomeBlock();
  for (const [chain, transpose] of [[1, 4], [2, 7]])
    for (let i = 0; i < CHAIN_ENTRIES; i++) chord.set([0x00, transpose], P_CHAINS + chain * 32 + i * 2);
  for (let row = 0; row < 8; row++) chord.set([0x00, 0x01, 0x02, 0xff], P_SONG + row * 4);
  const songs = [
    { block: metro, name: "METRO" },
    { block: high, name: "HIGH" },
    { block: chord, name: "CHORD" },
  ];
  return buildSav(songs, 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
}

/** Write the batteries the cases read under `scratch` (a path prefix of this run's own). */
export function writeScratch(be: Backend, scratch: string): void {
  if (!be.writeFile(smsSav(scratch), smsggdjSav())) throw new Error(`cannot write ${smsSav(scratch)}`);
  // The metronome alone, its OPTIONS block set to IN24: the cart waits for the host's clock, 24 a quarter note.
  const sync = buildSav([{ block: buildMetronomeBlock(), name: "METRO" }], 32 * 1024, buildConfigBlock(SMS_SYNC_IN24))!;
  if (!be.writeFile(smsSyncSav(scratch), sync)) throw new Error(`cannot write ${smsSyncSav(scratch)}`);
  if (!be.writeFile(blumarblSav(scratch), savBytes("v2_blumarbl")))
    throw new Error(`cannot write ${blumarblSav(scratch)}`);
}

/** One file a render wrote: its suffix, FNV-1a-64 and size, whether its samples are not all zero, and, when asked
 *  for, its bytes. */
export interface RenderedFile {
  suffix: string;
  hash: string;
  size: number;
  sounds: boolean;
  bytes?: Uint8Array;
}

export interface Rendered {
  result: EngineRunResult;
  files: RenderedFile[];
  /** The frames an auto-detect render ended at, from its `length:` line; null for a fixed render. */
  frames: number | null;
}

const WAV_HEADER = 44; // render/wav.ts writes a canonical 44-byte header, then 16-bit PCM

/** Render `c` as an engine job into `<scratch>-<tag>` (with `extra` in its spec), read back each file it wrote and
 *  delete it. */
export async function renderCase(
  be: Backend,
  c: RenderCase,
  scratch: string,
  tag: string,
  extra: Record<string, unknown> = {},
  keepBytes = false,
): Promise<Rendered> {
  const base = `${scratch}-${c.name.split("/").join("_")}-${tag}`;
  const split = c.outputs.length !== 1 || c.outputs[0] !== "";
  const out = split ? base : `${base}.wav`;
  let frames: number | null = null;
  const result = await be.engineRun(
    "render",
    { ...c.spec(scratch), ...extra, out },
    {
      onLog: (line) => {
        const m = /^length: \d+ ms \((\d+) frames/.exec(line.text);
        if (m) frames = Number(m[1]);
      },
    },
  );
  const files: RenderedFile[] = [];
  for (const suffix of c.outputs) {
    const path = split ? `${base}${suffix}.wav` : out;
    const bytes = be.readFile(path);
    if (!bytes) continue;
    let sounds = false;
    for (let i = WAV_HEADER; i < bytes.length && !sounds; i++) sounds = bytes[i] !== 0;
    files.push({ suffix, hash: fnv1a64Hex(bytes), size: bytes.length, sounds, bytes: keepBytes ? bytes : undefined });
    be.deleteFile(path);
  }
  return { result, files, frames };
}
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '4609,4864p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/render-cases.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/render-cases.ts`
Expected: `ba96dfc998ce0bea280e7262daf0104375e69841`: the blob 13e161c29 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/render-baseline.test.ts`:

````ts
// The render baseline (docs/modular-graph-phase-d3-design.md section 6): every case of render-cases.ts rendered as an
// engine job, in continuous blocks (section 4), each WAV it writes held to test-native/golden/render-baseline.json.
// The cases are render-cases.ts's CASES; this file is the baseline's only runner, and the golden holds the target and
// each case's record.
//
// The rules the golden records under, the kit baseline's (kit-baseline.test.ts) and parity's refusal of silence. A
// record is the length the render ended at (the job's renderedMs) and, per file, its FNV-1a-64 in lowercase hex and
// its size. The length and the sizes are compared on every host; the hashes only on the host the golden was recorded on
// (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic is bit-stable only for one compiler on one
// architecture. Elsewhere a case renders twice and must agree with itself, and RP_RENDER_GOLDEN_REQUIRE_TARGET=1 makes
// a foreign host fail instead (CI's linux-x64 job sets it on its test:native step). The case names must be the
// golden's keys exactly. Every case must sound, and every file of it but the ones its case names as silent (a stem its
// song never plays), which a recording checks as well.
//
// The cores power on in a fixed state (the runner sets RETROPLUG_DETERMINISTIC for this file, which reaches the
// engine job's own Engine: the variable is the process's), so two renders of one case agree. The smsggdj and SGB
// cases need no fixture and run on CI; the LSDj and risa cases need the fixture tree (RETROPLUG_RESOURCES_DIR) and
// SKIP without it, so this baseline is held whole only on a run with the fixtures and RP_FAIL_ON_SKIP=1.
//
// RP_RENDER_GOLDEN_UPDATE=1 re-records the golden, refused anywhere but the target, for any case whose two renders
// disagree, and for a run missing any case (fixtures absent): its last case writes the file, through a sibling temp
// file. Without the variable the file is only ever read.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import {
  CASES,
  caseNamed,
  missingFixture,
  renderCase,
  writeScratch,
  type Fixture,
  type Rendered,
  type RenderCase,
} from "./render-cases";

declare const __RENDER_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const UPDATE = tjs.env.RP_RENDER_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = tjs.env.RP_RENDER_GOLDEN_REQUIRE_TARGET === "1";

// This run's scratch prefix, in its own config dir, which the runner deletes.
const SCRATCH = `${be.configDir()}/render-baseline`;
writeScratch(be, SCRATCH);

interface FileGolden {
  file: string; // the suffix: "" for a mix
  hash: string;
  size: number;
}
interface CaseGolden {
  ms: number;
  files: FileGolden[];
}
interface Golden {
  target: string;
  cases: Record<string, CaseGolden>;
}

const recorded: Record<string, CaseGolden> = {};

function readGolden(): Golden {
  const bytes = be.readFile(__RENDER_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__RENDER_GOLDEN__}; record it with RP_RENDER_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as Golden;
}

// The golden's format: cases in byte order, one line per file, so a recording of the same renders is the same file.
function goldenText(golden: Golden): string {
  const names = Object.keys(golden.cases).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const rows = names.map((n) => {
    const c = golden.cases[n];
    const files = c.files.map((f) => `      { "file": "${f.file}", "hash": "${f.hash}", "size": ${f.size} }`);
    return `    "${n}": {\n      "ms": ${c.ms},\n      "files": [\n  ${files.join(",\n  ")}\n      ]\n    }`;
  });
  return `{\n  "target": "${golden.target}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
}

const record = (r: Rendered): CaseGolden => ({
  ms: r.result.renderedMs,
  files: r.files.map((f) => ({ file: f.suffix, hash: f.hash, size: f.size })),
});

// What every render of a case must be, golden or not: done, every file written, the case sounding, and every file
// but the ones it may leave silent.
function checkRendered(c: RenderCase, r: Rendered): void {
  expect(r.result.status, `${c.name}: ${r.result.message}`).toBe("done");
  expect(
    r.files.map((f) => f.suffix),
    `${c.name} writes its files`,
  ).toEqual(c.outputs);
  expect(
    r.files.some((f) => f.sounds),
    `${c.name} sounds`,
  ).toBe(true);
  for (const f of r.files)
    if (!(c.silent ?? []).includes(f.suffix)) expect(f.sounds, `${c.name}${f.suffix} sounds`).toBe(true);
}

/** Render `fixture`'s cases (all at once: the engine's pool runs them side by side) and hold them to the golden, or
 *  record them. */
async function holdCases(fixture: Fixture): Promise<Map<string, Rendered>> {
  const cases = CASES.filter((c) => c.fixture === fixture);
  const missing = missingFixture(be, fixture);
  if (missing) skip(`render-baseline: ${missing}`);
  const host = audio.buildTarget();
  const onTarget = host === BASELINE_TARGET;
  if (UPDATE && !onTarget)
    throw new Error(
      `a recording only happens on ${BASELINE_TARGET}, and this host is ${host}; ` +
        "a digest recorded here would be a fallback, not the baseline",
    );
  if (!UPDATE && REQUIRE_TARGET && !onTarget)
    throw new Error(
      `this host is ${host} but the baseline was recorded on ${BASELINE_TARGET}, so exact bytes would not be compared`,
    );

  const first = await Promise.all(cases.map((c) => renderCase(be, c, SCRATCH, "a")));
  const second = UPDATE || !onTarget ? await Promise.all(cases.map((c) => renderCase(be, c, SCRATCH, "b"))) : null;
  const golden = UPDATE ? null : readGolden();
  cases.forEach((c, k) => {
    const r = first[k];
    checkRendered(c, r);
    // A case whose two renders disagree depends on something outside its spec: its digest would mean nothing.
    if (second) expect(record(second[k]), `${c.name} renders twice to the same files`).toEqual(record(r));
    if (UPDATE) {
      recorded[c.name] = record(r);
      return;
    }
    const expected = golden!.cases[c.name];
    expect(expected !== undefined, `${c.name} is in the golden`).toBe(true);
    expect(r.result.renderedMs, `${c.name}'s length`).toBe(expected.ms);
    expect(
      r.files.map((f) => f.size),
      `${c.name}'s sizes`,
    ).toEqual(expected.files.map((f) => f.size));
    if (onTarget)
      expect(
        r.files.map((f) => f.hash),
        `${c.name}'s hashes`,
      ).toEqual(expected.files.map((f) => f.hash));
  });
  console.log(
    `# ${cases.length} ${fixture === "none" ? "fixture-free" : fixture} renders ` +
      (UPDATE ? "recorded" : onTarget ? "match the golden's hashes" : `agree with themselves on ${host}`),
  );
  return new Map(cases.map((c, k) => [c.name, first[k]]));
}

test("the matrix is the golden's: its names are the golden's keys", () => {
  expect(new Set(CASES.map((c) => c.name)).size).toBe(CASES.length);
  if (UPDATE) return; // a recording writes the keys this matrix has
  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  expect(CASES.map((c) => c.name).sort()).toEqual(Object.keys(golden.cases).sort());
});

test("the fixture-free renders (smsggdj songs, the SGB jingle) write the files the golden records", async () => {
  await holdCases("none");
});

test("LSDj renders to the files the golden records", async () => {
  const rendered = await holdCases("lsdj");
  // A song detected to its HFF stop ends at one frame whether it renders as a mix or as stems. Coarse: the end is
  // quantised to the 100 ms poll, so this would rarely catch stems starting a part block late; the next case does.
  const mix = rendered.get("lsdj/telemelt/hff/name-machete/mix")!;
  const stems = rendered.get("lsdj/telemelt/hff/index-3/channels")!;
  expect(stems.frames, "the stems' HFF end").toBe(mix.frames);
  expect(stems.result.renderedMs).toBe(mix.result.renderedMs);
});

// The left lane of a 16-bit stereo WAV's samples.
function leftLane(wav: Uint8Array): Float64Array {
  const view = new DataView(wav.buffer, wav.byteOffset + 44, wav.length - 44);
  const out = new Float64Array(Math.floor(view.byteLength / 4));
  for (let f = 0; f < out.length; f++) out[f] = view.getInt16(f * 4, true);
  return out;
}

// The lag in [-max, max] at which `b` best matches `a` over `len` frames from `at`: the peak of their normalised
// cross-correlation.
function bestLag(a: Float64Array, b: Float64Array, at: number, len: number, max: number): number {
  let best = 0;
  let bestScore = -Infinity;
  for (let lag = -max; lag <= max; lag++) {
    let dot = 0;
    let nb = 0;
    for (let i = at; i < at + len; i++) {
      dot += a[i] * b[i + lag];
      nb += b[i + lag] * b[i + lag];
    }
    const score = nb > 0 ? dot / Math.sqrt(nb) : -Infinity;
    if (score > bestScore) {
      bestScore = score;
      best = lag;
    }
  }
  return best;
}

test("an LSDj split render's stems line up with its mix: their sum matches it at lag 0", async () => {
  // A split render settles in its stream's shape (render.ts runRenderJob), so the block its settle leaves over is
  // its own and its stems start at the frame its mix does. Settled in the mix instead, the per-channel calls would
  // drop that carry (EngineRpcService::setContinuousBlocks) and the stems would start the rest of the settle's last
  // block later: 410 frames at 44.1 kHz.
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-baseline: ${missing}`);
  const [mix, stems] = await Promise.all(
    ["lsdj/tripledipper/fixed-5s/mix", "lsdj/tripledipper/fixed-5s/channels"].map((n) =>
      renderCase(be, caseNamed(n), SCRATCH, "align", {}, true),
    ),
  );
  for (const r of [mix, stems]) expect(r.result.status, r.result.message).toBe("done");
  const m = leftLane(mix.files[0].bytes!);
  const sum = new Float64Array(m.length);
  for (const f of stems.files) leftLane(f.bytes!).forEach((v, i) => (sum[i] += v));
  let first = 0;
  while (first < m.length && m[first] === 0) first++;
  const at = first + 2048; // a window inside the song, with room for the lags either side
  expect(at + 8192 + 1024 < m.length, "the mix sounds early enough to compare").toBe(true);
  expect(bestLag(m, sum, at, 8192, 1024), "the summed stems' lag against the mix").toBe(0);
});

test("risa renders to the files the golden records", async () => {
  await holdCases("risa");
});

if (UPDATE)
  test("record every case to the golden", () => {
    const missing = CASES.filter((c) => recorded[c.name] === undefined).map((c) => c.name);
    if (missing.length)
      throw new Error(`a recording holds every case, and these were not rendered: ${missing.join(", ")}`);
    const text = new TextEncoder().encode(goldenText({ target: BASELINE_TARGET, cases: recorded }));
    expect(be.writeFileAtomic(__RENDER_GOLDEN__, text), `write ${__RENDER_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${CASES.length} renders to ${__RENDER_GOLDEN__}`);
  });
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '4876,5117p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/render-baseline.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/render-baseline.test.ts`
Expected: `bf3b528272883f51125f332773e6f42163792032`: the blob 13e161c29 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/render-chunks.test.ts`:

````ts
// A render's bytes do not depend on how much audio each of its pulls asks for (docs/modular-graph-phase-d3-design.md
// section 4): in an engine job the engine runs whole blocks whatever a call asks for and carries the rest of its last
// block to the next call, so the emulation and its audio fall on the same block grid however the render library
// chunks its stream. Pulled through RenderOpts.pullMs, the test knob over the stream's 100 ms chunks.
//
// Where the block grid shows. A render with no running transport and no MIDI emulates the same whatever its blocks
// are, so its samples would not follow the pull with exact blocks either; the host-synced smsggdj render is the one
// here whose would: its sync node turns the running transport into the cart's clock block by block, and with exact
// blocks (each call its own length) its file moves with the pull. That case is the one exact blocks fail.
//
// What the grid cannot cover. Between its calls the library also acts: it presses play (Start; risa's three
// gestures), loads smsggdj's song, and on an auto-detect render polls the song's state after each pull. An act lands
// at the block after the call before it ends, so it moves with that call's length by construction. The gestures and
// the song load use lengths of their own (100 ms holds, 250 ms steps, and the 1500 ms settle), which pullMs leaves
// alone, so a fixed-length render's files are identical at any pull, presses and all, as the other cases pin. An
// auto-detect render's pull is its detection poll, though, and it ends at the start of the first pull its poll reads
// stopped, so its samples agree and its length moves with the pull, by less than the longest one.
//
// The cores power on in a fixed state (the runner sets RETROPLUG_DETERMINISTIC for this file). The smsggdj renders
// need no fixture; the LSDj and risa ones read the fixture tree (RETROPLUG_RESOURCES_DIR) and SKIP without it.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { caseNamed, missingFixture, renderCase, writeScratch, type Rendered } from "./render-cases";

const be = createRealBackend();
createAudioDriver(); // the offline poll's wait: this host has no UI tick

const SCRATCH = `${be.configDir()}/render-chunks`; // this run's own config dir, which the runner deletes
writeScratch(be, SCRATCH);

const PULLS = [100, 37, 250]; // ms: the default, one no block length divides, and one longer than the hold gestures

/** `name` rendered once per pull in `pulls`, all at once on the engine's pool. */
const atPulls = (name: string, pulls: number[], keepBytes = false): Promise<Rendered[]> =>
  Promise.all(pulls.map((pullMs) => renderCase(be, caseNamed(name), SCRATCH, `pull${pullMs}`, { pullMs }, keepBytes)));

/** A fixed-length render's files at every pull are the default pull's: same hashes, same sizes. */
function sameFiles(name: string, rendered: Rendered[]): void {
  for (const r of rendered) expect(r.result.status, `${name}: ${r.result.message}`).toBe("done");
  expect(rendered[0].files.some((f) => f.sounds), `${name} sounds`).toBe(true);
  const files = (r: Rendered) => r.files.map((f) => `${f.suffix} ${f.hash} ${f.size}`);
  for (let k = 1; k < rendered.length; k++)
    expect(files(rendered[k]), `${name} pulled at ${PULLS[k]} ms`).toEqual(files(rendered[0]));
}

test("a host-synced smsggdj render writes the same file at every pull", async () => {
  const name = "smsggdj/sms/sync-in24/transport-120bpm";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length smsggdj render (its song loaded and played) writes the same file at every pull", async () => {
  const name = "smsggdj/sms/index-0/mix";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length LSDj split render (Start pressed) writes the same stems at every pull", async () => {
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-chunks: ${missing}`);
  const name = "lsdj/tripledipper/fixed-5s/channels";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length risa split render (its three gestures pressed) writes the same pins at every pull", async () => {
  const missing = missingFixture(be, "risa");
  if (missing) skip(`render-chunks: ${missing}`);
  const name = "risa/blumarbl/fixed-3s/pins";
  sameFiles(name, await atPulls(name, PULLS));
});

test("an LSDj render to its HFF stop writes the same samples at every pull, ending within a pull", async () => {
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-chunks: ${missing}`);
  const pulls = [100, 50, 200];
  const rendered = await atPulls("lsdj/telemelt/hff/name-machete/mix", pulls, true);
  for (const r of rendered) expect(r.result.status, r.result.message).toBe("done");
  const pcm = rendered.map((r) => r.files[0].bytes!.subarray(44)); // past the WAV header
  const shortest = Math.min(...pcm.map((p) => p.length));
  for (let k = 1; k < pcm.length; k++) {
    let differ = -1;
    for (let i = 0; i < shortest && differ < 0; i++) if (pcm[k][i] !== pcm[0][i]) differ = i;
    expect(differ, `the samples pulled at ${pulls[k]} ms against ${pulls[0]} ms`).toBe(-1);
  }
  const lengths = rendered.map((r) => r.result.renderedMs);
  console.log(`# HFF lengths at pulls ${pulls.join(", ")} ms: ${lengths.join(", ")} ms`);
  expect(Math.max(...lengths) - Math.min(...lengths) < Math.max(...pulls)).toBe(true);
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '5129,5215p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/render-chunks.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/render-chunks.test.ts`
Expected: `6170e7cfccb77435ce0ff54daae7104306ccc8b4`: the blob 13e161c29 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-native/golden/render-baseline.json`:

````json
{
  "target": "linux-x64-gnu13",
  "cases": {
    "lsdj/telemelt/hff/index-3/channels": {
      "ms": 191700,
      "files": [
        { "file": "_pulse1", "hash": "2e27d16400c632f7", "size": 33815924 },
        { "file": "_pulse2", "hash": "616a44128428ac84", "size": 33815924 },
        { "file": "_wave", "hash": "d6e2ec339da8d2f3", "size": 33815924 },
        { "file": "_noise", "hash": "e1e159951a72f58d", "size": 33815924 }
      ]
    },
    "lsdj/telemelt/hff/name-machete/mix": {
      "ms": 191700,
      "files": [
        { "file": "", "hash": "9923f649c18aa81a", "size": 33815924 }
      ]
    },
    "lsdj/tripledipper/fixed-5s/channels": {
      "ms": 5000,
      "files": [
        { "file": "_pulse1", "hash": "03e66ee500bc0cde", "size": 882044 },
        { "file": "_pulse2", "hash": "77d5c1269162273e", "size": 882044 },
        { "file": "_wave", "hash": "8795fe44009c5656", "size": 882044 },
        { "file": "_noise", "hash": "03f386d0e8436596", "size": 882044 }
      ]
    },
    "lsdj/tripledipper/fixed-5s/mix": {
      "ms": 5000,
      "files": [
        { "file": "", "hash": "c24700eaccee81f6", "size": 882044 }
      ]
    },
    "risa/blumarbl/fixed-3s/channels": {
      "ms": 3000,
      "files": [
        { "file": "_square1", "hash": "6889b817fb4b0293", "size": 264644 },
        { "file": "_square2", "hash": "1495186248745402", "size": 264644 },
        { "file": "_triangle", "hash": "6889b817fb4b0293", "size": 264644 },
        { "file": "_noise", "hash": "7137025070f57c16", "size": 264644 },
        { "file": "_dmc", "hash": "6889b817fb4b0293", "size": 264644 }
      ]
    },
    "risa/blumarbl/fixed-3s/pins": {
      "ms": 3000,
      "files": [
        { "file": "_pulse", "hash": "e63a67eb0bcb53ff", "size": 264644 },
        { "file": "_tnd", "hash": "62382a9f18d719cc", "size": 264644 },
        { "file": "_expansion", "hash": "2137fd9ca19c14f5", "size": 264644 }
      ]
    },
    "risa/ecoli/hff/mix": {
      "ms": 59000,
      "files": [
        { "file": "", "hash": "54393a15fcb680bb", "size": 5203844 }
      ]
    },
    "risa/ecoli/hff/region-ntsc/mix": {
      "ms": 49800,
      "files": [
        { "file": "", "hash": "63124a2bc55afbcb", "size": 4392404 }
      ]
    },
    "sameboy/mgb-sgb/no-start/mix": {
      "ms": 4000,
      "files": [
        { "file": "", "hash": "ab268313917fac4e", "size": 705644 }
      ]
    },
    "smsggdj/gg/index-1/mix": {
      "ms": 3000,
      "files": [
        { "file": "", "hash": "0404a43ce8216dfe", "size": 529244 }
      ]
    },
    "smsggdj/sms/index-0/mix": {
      "ms": 4000,
      "files": [
        { "file": "", "hash": "c309fbacf1c2803a", "size": 705644 }
      ]
    },
    "smsggdj/sms/name-chord/mix-48000": {
      "ms": 3000,
      "files": [
        { "file": "", "hash": "e418e50d3668e64a", "size": 576044 }
      ]
    },
    "smsggdj/sms/sync-in24/transport-120bpm": {
      "ms": 4000,
      "files": [
        { "file": "", "hash": "751568bed2d8e02e", "size": 705644 }
      ]
    }
  }
}
````

Run: `mkdir -p packages/retroplug/test-native/golden; sed -n '5227,5321p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/golden/render-baseline.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/golden/render-baseline.json`
Expected: `2bf4e013d3225dc6ac4594310cec83d89c4b2f53`: the blob 13e161c29 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/run-native-tests.mjs b/packages/retroplug/scripts/run-native-tests.mjs
index cbd4152b7..8effeaad6 100644
--- a/packages/retroplug/scripts/run-native-tests.mjs
+++ b/packages/retroplug/scripts/run-native-tests.mjs
@@ -33,6 +33,9 @@ const REPO_RESOURCES_DIR = join(REPO, "resources");
 // unnoticed. Same env override scripts/gen-risa-symbols.mjs already uses.
 const RISA_SRC = process.env.RISA_SRC || "/workspaces/risa-v2.2.1-source";
 
+// Files outside parity/ whose cores power on in a fixed state (below): the render baseline and its chunk test.
+const DETERMINISTIC_FILES = new Set(["render-baseline", "render-chunks"]);
+
 const HOST =
   process.env.RETROPLUG_HOST ||
   join(REPO, "build/bin/retroplug-host" + (process.platform === "win32" ? ".exe" : ""));
@@ -111,6 +114,9 @@ async function runOne({ file, slug }) {
         __KIT_EDGES_GOLDEN__: JSON.stringify(
           join(REPO, "packages/native/test/audio/golden/kit-edges.json").replaceAll("\\", "/"),
         ),
+        // The render baseline, the WAVs engine jobs write (render-baseline.test.ts reads it and, under
+        // RP_RENDER_GOLDEN_UPDATE=1, records it).
+        __RENDER_GOLDEN__: JSON.stringify(join(TEST_DIR, "golden/render-baseline.json").replaceAll("\\", "/")),
       },
     });
   } catch (e) {
@@ -119,10 +125,11 @@ async function runOne({ file, slug }) {
     return false;
   }
 
-  // The parity suite hashes exact output, so its cores power on in a fixed state (util/Determinism.hpp).
-  // Every other file keeps the random power-on the app has.
+  // The parity suite and the render baseline hash exact output, so their cores power on in a fixed state
+  // (util/Determinism.hpp): the process's, so an engine job's own Engine too. Every other file keeps the random
+  // power-on the app has.
   const env = { ...process.env, RETROPLUG_USER_CONFIG_DIR: cfgDir };
-  if (slug.startsWith("parity/")) env.RETROPLUG_DETERMINISTIC = "1";
+  if (slug.startsWith("parity/") || DETERMINISTIC_FILES.has(slug)) env.RETROPLUG_DETERMINISTIC = "1";
   else delete env.RETROPLUG_DETERMINISTIC;
   const run = await spawnBuffered(HOST, [outFile], { cwd: PKG, env });
   flush(slug, run.output);
````

Run: `sed -n '5331,5369p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: fails, `# 2 passed, 0 skipped, 3 failed`: the render library still settles every render in the mix, so a split render's stems start up to a block after its mix. The matrix case and the fixture-free case pass (every fixture-free case is a mix); `LSDj renders to the files the golden records` and `risa renders to the files the golden records` fail on a split case (the prototype's settle-in-the-mix mutation run failed the LSDj one on `lsdj/tripledipper/fixed-5s/channels`'s hashes), and the alignment case fails with `the summed stems' lag against the mix: expected 0, got -410`. That mutation run, `s2f-align-red.log`, is this state.

- [ ] **Step 4: The library settles in its stream's shape, and the golden is recorded**

`RenderContext.blocks` tells the library how its engine runs, read by the worker from the plugin namespace; in
continuous blocks a split render's 1500 ms settle runs in the per-channel shape, while exact blocks still settle in
the mix, which keeps the CLI's NES stems byte for byte (a NES's settle shape is emulation timing, not only routing).
`RenderOpts.pullMs` (a spec field the worker passes and the CLI never sets) changes only `driveFixed`'s and
`driveAutoDetect`'s pulls; the play gestures (100 ms), smsggdj's load steps (250 ms) and the settle keep their own.

````diff
diff --git a/packages/retroplug/src/render/render.ts b/packages/retroplug/src/render/render.ts
index 603cf270d..cb7bca348 100644
--- a/packages/retroplug/src/render/render.ts
+++ b/packages/retroplug/src/render/render.ts
@@ -54,7 +54,7 @@ const SMS_START = 7; // SmsButton::Start -> SmsController::Buttons::Pause. smsgg
 // high→low edge (the technique lsdpack uses). Hardware-level, so version-independent + tempo/hop-agnostic.
 const NR52_ADDR = 0xff26;
 const NR52_ON = 0x80; // bit 7 = all-sound-on
-const DETECT_CHUNK_MS = 100; // poll granularity (≈ detection precision)
+const DETECT_CHUNK_MS = 100; // the play gestures' hold, and the pull (so the poll granularity) unless pullMs says
 const DETECT_OFF_CHUNKS = 2; // NR52 must read off this many consecutive chunks to count as the HFF stop
 
 
@@ -552,11 +552,15 @@ function buildSink(ctx: RenderContext, o: RenderOpts, id: number, platform: Plat
   };
 }
 
-/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk), so the output is
- *  byte-identical to a single renderAudio(ms) of the same length for any sample rate / ms. `leadIn` is the
+/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk). In continuous blocks
+ *  (RenderContext.blocks, an engine job) the output is byte-identical to a single renderAudio(ms) of the same length
+ *  for any sample rate / ms / pull; in exact blocks only while no node consumes the transport or MIDI block by block
+ *  (a sync node such as rp.sms.sync), whose clock then follows the pulls' splits. `leadIn` is the
  *  captured play-gesture audio (the song's opening frames) — committed first so the recording's head isn't
  *  lost, trimmed against the target like any other chunk. */
-function driveFixed(sink: RenderSink, targetFrames: number, sr: number, hooks: RenderHooks, leadIn: Float32Array[][] = []): void {
+function driveFixed(
+  sink: RenderSink, targetFrames: number, sr: number, hooks: RenderHooks, pullMs: number, leadIn: Float32Array[][] = [],
+): void {
   let done = 0;
   const emit = (chunk: Float32Array[]) => {
     if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
@@ -566,7 +570,7 @@ function driveFixed(sink: RenderSink, targetFrames: number, sr: number, hooks: R
   for (const chunk of leadIn) { if (done >= targetFrames) break; emit(chunk); }
   while (done < targetFrames) {
     if (hooks.isCancelled?.()) throw new RenderCancelled();
-    emit(sink.renderChunk(DETECT_CHUNK_MS));
+    emit(sink.renderChunk(pullMs));
     hooks.onRendered?.((done / sr) * 1000); // committed audio so far (the last chunk is trimmed to the target)
   }
 }
@@ -574,8 +578,12 @@ function driveFixed(sink: RenderSink, targetFrames: number, sr: number, hooks: R
 /** Length auto-detect render: stream chunk-by-chunk, polling `isPlaying` (LSDj's NR52 / risa's seq_mode),
  *  and stop at the HFF (the probe going true→false, sustained ≥DETECT_OFF_CHUNKS). Holds back the current
  *  contiguous off-streak (≤DETECT_OFF_CHUNKS whole chunks) so committed frames end exactly at the stop; a
- *  reset flushes them in order. Caps at maxMs (no-HFF fallback → keep everything). */
-function driveAutoDetect(sink: RenderSink, isPlaying: () => boolean, maxMs: number, sr: number, hooks: RenderHooks, leadIn: Float32Array[][] = []): StopMarkers {
+ *  reset flushes them in order. Caps at maxMs (no-HFF fallback → keep everything). Each chunk is `pullMs` long,
+ *  so the poll, and with it where the render can end, follows it. */
+function driveAutoDetect(
+  sink: RenderSink, isPlaying: () => boolean, maxMs: number, sr: number, hooks: RenderHooks, pullMs: number,
+  leadIn: Float32Array[][] = [],
+): StopMarkers {
   let total = 0; // frames rendered (committed + held)
   let committed = 0; // frames streamed to disk
   let elapsed = 0;
@@ -601,11 +609,11 @@ function driveAutoDetect(sink: RenderSink, isPlaying: () => boolean, maxMs: numb
 
   while (elapsed < maxMs) {
     if (hooks.isCancelled?.()) throw new RenderCancelled();
-    const chunk = sink.renderChunk(DETECT_CHUNK_MS);
+    const chunk = sink.renderChunk(pullMs);
     if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
     const frameBefore = total;
     total += chunk[0].length / 2;
-    elapsed += DETECT_CHUNK_MS;
+    elapsed += pullMs;
     // Rendered audio so far, counting the held off-streak: monotonic, and at most DETECT_OFF_CHUNKS ahead
     // of what lands on disk (a confirmed HFF stop discards those held chunks).
     hooks.onRendered?.((total / sr) * 1000);
@@ -725,6 +733,9 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
   const platform = platformOf(o.rom);
 
   validateRenderOpts(o, platform);
+  const pullMs = o.pullMs ?? DETECT_CHUNK_MS;
+  if (!(Number.isFinite(pullMs) && pullMs > 0))
+    throw new Error(`render: pullMs must be a positive number of ms (got ${o.pullMs})`);
 
   // The sample rate is baked into each core at construct, so it MUST be set before buildSystem.
   if (o.sampleRate !== undefined && !ctx.audio.setSampleRate(o.sampleRate))
@@ -739,7 +750,14 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
       : undefined;
   const id = buildSystem(ctx, o, platform, seed?.seed);
 
-  ctx.audio.renderAudio(1500); // settle boot (past the mGB/LSDj splash) before driving playback
+  // Settle boot (past the mGB/LSDj splash) before driving playback. Discarded, but in continuous blocks it runs in
+  // the shape the render streams in: the frames past a call in its last block go to the next call only if that call
+  // is of the same shape (EngineRpcService::setContinuousBlocks), so a split render settling in the mix would start
+  // its stems up to a block later than the same render's mix starts, and its detected length would differ by as
+  // much. Exact blocks settle in the mix: there the stems start where the mix does either way, and the shape a NES
+  // settles in moves its stems' bytes.
+  if (o.split === "mix" || ctx.blocks !== "continuous") ctx.audio.renderAudio(1500);
+  else ctx.audio.renderAudioPerChannel(id, 1500);
   if (o.bpm) ctx.audio.setBpm(o.bpm);
   if (o.transport) ctx.audio.setTransport(true);
 
@@ -784,7 +802,7 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
   const leadIn = pressPlay(ctx, sink, o, id, platform);
 
   if (autoDetect) {
-    const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, leadIn);
+    const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, pullMs, leadIn);
     const outputs = sink.finishAll();
     const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
     log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
@@ -793,7 +811,7 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
     return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
   }
   const targetFrames = Math.floor(((o.durationMs ?? o.maxDurationMs) * sr) / 1000); // exact target frame count
-  driveFixed(sink, targetFrames, sr, hooks, leadIn);
+  driveFixed(sink, targetFrames, sr, hooks, pullMs, leadIn);
   const outputs = sink.finishAll();
   hooks.onRendered?.(Math.round((targetFrames / sr) * 1000));
   return { outputs };
diff --git a/packages/retroplug/src/render/types.ts b/packages/retroplug/src/render/types.ts
index 02be9b55c..d977388a1 100644
--- a/packages/retroplug/src/render/types.ts
+++ b/packages/retroplug/src/render/types.ts
@@ -42,6 +42,12 @@ export interface RenderOpts {
   // take them from and correctly boots the ROM as it ships.
   roles?: RoleInstance[]; // the live system's roles (system + feature), configs included
   gainDb?: number; // the instance's own gain (CommonSettings.gainDb); 0 / absent = unity
+  // How much audio each pull of the render's stream asks for, in ms: 100 when absent, as every caller but
+  // render-chunks.test.ts leaves it. A test knob: only in an engine job (continuous blocks) does the engine run whole
+  // blocks whatever a pull asks for, so there a render's samples do not depend on it (its play gestures and song load
+  // keep lengths of their own); an auto-detect render's pull is also its detection poll, though, so the length it
+  // ends at moves with it. In exact blocks (the UI's RenderJobRegistry) a render with a sync node moves with it.
+  pullMs?: number;
 }
 
 /** The driver calls a render makes: the subset of `AudioDriver` a `RenderContext` needs. Naming it is what keeps a
@@ -58,6 +64,10 @@ export interface RenderContext {
   backend: Backend; // readFile / readCpu + the WavBackend writers (writeFile / appendFile / writeFileAt)
   project: ProjectStore;
   audio: RenderAudioDriver;
+  // How the engine runs the render's calls: "continuous" in an engine job, whose engine runs whole blocks and carries
+  // the rest of a call's last block to the next call of the same shape (docs/modular-graph-phase-d3-design.md
+  // section 4); "exact" (or absent) where each call runs its own length, as the CLI's in-process render does.
+  blocks?: "exact" | "continuous";
 }
 
 /** Host callbacks. All optional so the CLI passes none (logging defaults to console, no progress/cancel).
diff --git a/packages/retroplug/src/render/worker.ts b/packages/retroplug/src/render/worker.ts
index e9c4e2f8a..1bd609114 100644
--- a/packages/retroplug/src/render/worker.ts
+++ b/packages/retroplug/src/render/worker.ts
@@ -41,13 +41,19 @@ function normalize(spec: RenderJobSpec): RenderOpts {
     // for a CLI render, which boots the ROM as it ships.
     roles: spec.roles,
     gainDb: spec.gainDb,
+    pullMs: spec.pullMs, // a test knob (RenderOpts.pullMs)
     listSongs: false, // the worker only renders — --list-songs is a CLI-only query
   };
 }
 
+/** What the RenderHost puts on globalThis[Symbol.for("plugin")]: the job spec, and how its engine runs blocks. */
+type PluginNamespace = { args?: string[]; blocks?: string };
+function pluginNamespace(): PluginNamespace | undefined {
+  return (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as PluginNamespace | undefined;
+}
+
 function readSpec(): RenderJobSpec {
-  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { args?: string[] } | undefined;
-  const raw = ns?.args?.[0];
+  const raw = pluginNamespace()?.args?.[0];
   if (!raw) throw new Error("render worker: no job spec (expected plugin.args[0])");
   const spec = JSON.parse(raw) as RenderJobSpec;
   if (!spec || typeof spec.rom !== "string") throw new Error("render worker: job spec missing 'rom'");
@@ -64,11 +70,14 @@ function main(): void {
   }
 
   const session = bootSession(); // structurally satisfies RenderContext (backend / project / audio)
+  // An engine job's host runs whole blocks (RenderHost::Blocks), which the settle follows (runRenderJob).
+  const blocks = pluginNamespace()?.blocks === "continuous" ? "continuous" : "exact";
   // A render is an EXPORT. It builds systems, may load a savestate into one, and drops them again, so the
   // before-teardown battery flush would turn an export into a write in the user's save folder.
   session.project.systems.setBatteryFlush(false);
   try {
-    const result = runRenderJob(session, opts, {
+    const ctx = { backend: session.backend, project: session.project, audio: session.audio, blocks } as const;
+    const result = runRenderJob(ctx, opts, {
       onRendered: (ms) => __rp_reportRenderedMs?.(ms),
       isCancelled: () => __rp_isRenderCancelled?.() ?? false,
     });
````

Run: `sed -n '5387,5570p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RP_RENDER_GOLDEN_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `ok 6 - record every case to the golden` and `# 6 passed, 0 skipped, 0 failed`: every case rendered twice and agreed with itself, and the file is rewritten whole.

Run: `git hash-object packages/retroplug/test-native/golden/render-baseline.json`
Expected: `2bf4e013d3225dc6ac4594310cec83d89c4b2f53`: the recorder wrote the bytes the block above placed.

Run: `git add -- packages/retroplug/test-native/golden/render-baseline.json`
Expected: nothing: the recorded golden is staged, as the commit's status below shows it.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then `# 5 passed, 0 skipped`, about 60 s.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# HFF lengths at pulls 100, 50, 200 ms: 191700, 191750, 191700 ms`, then `5 passed, 0 skipped`.

CI's linux-x64 `test:native` step requires the render golden's host as it already requires the kit goldens':

````diff
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index 1ad6e968a..7a56b6d94 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -83,10 +83,12 @@ jobs:
       - name: Native tests (real host + emulator cores)
         # This job is the parity baseline's target: exact audio checking must run here, not fall back. It is the kit
         # baselines' target too: test-native's kit-baseline and kit-compile compare exact bank bytes only on the host
-        # that recorded them, so this job demands that it is that host rather than falling back to self-agreement.
+        # that recorded them, so this job demands that it is that host rather than falling back to self-agreement. The
+        # render baseline (render-baseline, its fixture-free cases here) is held the same way.
         env:
           RP_PARITY_REQUIRE_TARGET: "1"
           RP_KIT_GOLDEN_REQUIRE_TARGET: "1"
+          RP_RENDER_GOLDEN_REQUIRE_TARGET: "1"
         run: pnpm test:native
 
       - name: Plugin unit tests (Catch2)
````

Run: `sed -n '5594,5611p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

- [ ] **Step 5: Show that continuous blocks bite**

Of the five chunk cases only the host-synced smsggdj render ever depended on the block grid, so forcing engine jobs
back to exact blocks must fail exactly that case. Apply this temporary mutation of the pool's body, run the chunk test
(the runner rebuilds `retroplug-host`, which embeds the pool), and reverse it.

````patch
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -432,7 +432,7 @@
     // section 4).
     retroplug::RenderHost         host;
     retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console,
-                                               retroplug::RenderHost::Blocks::Continuous);
+                                               retroplug::RenderHost::Blocks::Exact);
     return {std::move(r.status), std::move(r.message), std::move(r.outputs)};
 }
 
````

Run: `sed -n '5624,5635p' docs/modular-graph-phase-d3-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `not ok 1 - a host-synced smsggdj render writes the same file at every pull` and `# 4 passed, 0 skipped, 1 failed`: the pull at 37 ms writes a different file in exact blocks.

Run: `sed -n '5624,5635p' docs/modular-graph-phase-d3-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed. Then rerun `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`: 5 passed again.

- [ ] **Step 6: Regenerate the skip baseline**

Without the fixtures `render-baseline` skips 3 of its 5 cases (the LSDj, alignment and risa ones) and `render-chunks`
3 of 5. The diff below is what the regeneration writes; apply it, then run the regeneration itself.

````diff
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index 5bea88180..9913b23a6 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -184,6 +184,14 @@
    "skipped": 2,
    "total": 3
   },
+  "render-baseline": {
+   "skipped": 3,
+   "total": 5
+  },
+  "render-chunks": {
+   "skipped": 3,
+   "total": 5
+  },
   "risa-220-layout": {
    "skipped": 1,
    "total": 1
````

Run: `sed -n '5653,5671p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native --update-skip-baseline`
Expected: `# 166 native test file(s) passed (jobs=16)`, then `# skip-baseline updated: native now records 64 skipping file(s)`.

Run: `git hash-object packages/retroplug/scripts/skip-baseline.json`
Expected: `9913b23a65d5be72bf933debb8cbadfba9ed171e`: the blob 13e161c29 holds.

Run: `RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native render-baseline`
Expected: `5 fixture-free renders match the golden's hashes`, `2 passed, 3 skipped`, the skip check passing: CI's share.

- [ ] **Step 7: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0; the only warning is the pre-existing `-Wformat-truncation` in `PluginDSP.cpp:186`.

Run: `pnpm test:plugin`
Expected: all 10 exit 0; `retroplug-engine-graph-test` `test cases: 304 | 293 passed | 11 skipped` (was 300 / 289 / 11); graph 356156 / 273, audio 1234835 / 108 and launchpad 144 / 29, unchanged.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native engine-run`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native audio-render`
Expected: 1 passed; `render-guard` 1, `risa-render` (three files) 6, 1 and 1, `sms-render` 6, each run the same way: the in-process renders are exact blocks, unchanged.

Run: `pnpm test:ui render-menu`
Expected: 1 passed; `render-badge` 1 passed; `pnpm test render` 4 files passed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 166 native test file(s) passed`; only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: 166 passed, the skip check passing.

Run: `pnpm test`
Expected: `# 171 test file(s) passed`.

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:node`
Expected: 12 pass, 0 fail.

- [ ] **Step 8: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (1685004 assertions in 33 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no ThreadSanitizer report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (1667076 assertions in 33 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

- [ ] **Step 9: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M .github/workflows/build.yml
 m deps/sameboy
 M packages/native/src/host/engine/OfflineJobs.cpp
 M packages/native/src/host/engine/OfflineJobs.hpp
 M packages/native/src/host/render/RenderHost.cpp
 M packages/native/src/host/render/RenderHost.hpp
 M packages/native/src/host/rpc/EngineRpcService.cpp
 M packages/native/src/host/rpc/EngineRpcService.hpp
 M packages/native/src/util/Determinism.hpp
 M packages/native/test/engine-graph/EngineJobs.test.cpp
 M packages/retroplug/scripts/run-native-tests.mjs
 M packages/retroplug/scripts/skip-baseline.json
 M packages/retroplug/src/render/render.ts
 M packages/retroplug/src/render/types.ts
 M packages/retroplug/src/render/worker.ts
A  packages/retroplug/test-native/golden/render-baseline.json
?? packages/retroplug/test-native/render-baseline.test.ts
?? packages/retroplug/test-native/render-cases.ts
?? packages/retroplug/test-native/render-chunks.test.ts
````

````bash
git add -- packages/retroplug/test-native/golden/render-baseline.json \
  packages/retroplug/test-native/render-baseline.test.ts packages/retroplug/test-native/render-cases.ts \
  packages/retroplug/test-native/render-chunks.test.ts .github/workflows/build.yml \
  packages/native/src/host/engine/OfflineJobs.cpp packages/native/src/host/engine/OfflineJobs.hpp \
  packages/native/src/host/render/RenderHost.cpp packages/native/src/host/render/RenderHost.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/src/util/Determinism.hpp packages/native/test/engine-graph/EngineJobs.test.cpp \
  packages/retroplug/scripts/run-native-tests.mjs packages/retroplug/scripts/skip-baseline.json \
  packages/retroplug/src/render/render.ts packages/retroplug/src/render/types.ts \
  packages/retroplug/src/render/worker.ts
git commit -F - <<'EOF'
feat(render): an engine job renders in whole blocks, and the render baseline records it

Continuous blocks (docs/modular-graph-phase-d3-design.md section 4) live on EngineRpcService, switched on only by
the RenderHost an engine job builds (RenderHost::Blocks::Continuous; the CLI's in-process render, RenderJobRegistry,
render-host-test, the harness, the live engine and the Node addon keep exact blocks). With it on, renderAudio,
renderAudioPerSystem and renderAudioPerChannel run whole 1024-frame blocks only: a call returns the frames it asks
for and the rest of its last block is carried to the next call. The carry belongs to the shape that rendered it (mix,
per-system, or one system's channels with its stream count) and to the rate: a call of another shape, a rate change
or a renderBlocks run drops it, and the job's end drops it with the service. The three render calls share one pull
loop; in exact blocks it runs the splits it ran before, which parity holds bit for bit.

The render library learns which way its engine runs (RenderContext.blocks, from the RenderHost's plugin namespace):
in continuous blocks a split render settles in its stream's shape, so its carry is its own and its stems end where
its mix does; exact blocks still settle in the mix, which keeps the CLI's NES stems byte for byte. RenderOpts.pullMs
is a test knob over the stream's pull length.

Tests: [engine-jobs] gains four cases over an mGB that MIDI plays (call lengths do not move the bytes, where exact
blocks do; a carry goes only to its own shape and a renderBlocks or a rate change drops it; a per-channel render on
an eight-lane host carries across its calls and a mix call between drops it; exact blocks carry nothing), and its
direct RenderHost runs use continuous blocks. render-chunks.test.ts pulls renders at three pull lengths: a
host-synced smsggdj render (the case exact blocks fail), fixed renders with their play gestures, and an LSDj HFF
render whose samples agree while its length moves with the poll. render-baseline.test.ts holds 13 engine-job renders
to test-native/golden/render-baseline.json (FNV-1a-64 and size per file, and the length), under the kit baseline's
rules and parity's refusal of silence: five fixture-free ones (four smsggdj renders of authored songs, and mGB as a
Super Game Boy, whose boot jingle sounds with no MIDI), four LSDj and four risa ones from the fixture tree. It also
holds an LSDj split render's summed stems to its mix at lag 0, which a settle in the mix moves to -410. The runner
powers both files' cores on in a fixed state, and CI's linux-x64 job sets RP_RENDER_GOLDEN_REQUIRE_TARGET. The golden
was recorded on linux-x64-gnu13 with the fixtures; the skip baseline was regenerated with them absent.
EOF
````

---

### Task 3: the CLI and the UI render through engine jobs

Design section 5 and section 7's third commit. `retroplug-cli render` and System > Render submit a render as an engine
job instead of running the library in-process or on a `RenderJobRegistry` thread. The CLI parses its arguments into
the job spec with the paths as typed, awaits the job under `keepAlive()` with the offline poll's macrotask yield, prints
the job's lines as they arrive and maps an error or a cancel to the same `ERROR:` line and exit 1. Ctrl-C reaches it
through the launcher's new `__rp_onInterrupt` hook, and a cancelled render deletes the WAVs it opened. The UI keeps a
`RenderJobs` list in the store graph, submits through the stores' backend, and its badge reads the list instead of
polling a hook. `RenderJobRegistry`, the hooks and `runRenderJob`'s in-process callers stay for Task 4; the registry
and the tool render in continuous blocks so the "UI worker equals CLI" leg still holds (without it, an LSDj mix and its
pulse2 stem differed from byte 21025, because the Start press lands one block boundary later). The render baseline is
not re-recorded: the CLI's files are now the baseline's.

The prototype also ran the CLI by hand: an LSDj HFF render from relative paths (`length: 191700 ms (8453970 frames
@44100Hz) hff:true`), a NES split into its five channels, the errors, `--list-songs`, and an ASan build of the CLI
interrupted mid-render with leaks detected (exit 1, `ERROR: render: cancelled`, no WAV, no report). The binary leg in
Step 5 is the reproducible part of those.

**Files:**

- Create: `packages/retroplug/src/renderJobs.ts`, `packages/retroplug/test-native/render-cli.test.ts`, `packages/retroplug/test-ui/render-job.test.ts`, `packages/retroplug/test-ui/scriptedRenders.ts`, `packages/retroplug/test/render/render-jobs.test.ts`, `tools/fixtures/render-smsggdj.sav`
- Modify: `packages/native/cli/main.cpp`, `packages/native/src/host/render/RenderJobRegistry.cpp`, `packages/native/test/render-host-main.cpp`, `packages/retroplug/cli/session.ts`, `packages/retroplug/cli/sessions/render.ts`, `packages/retroplug/scripts/run-native-tests.mjs`, `packages/retroplug/scripts/skip-baseline.json`, `packages/retroplug/src/appStores.ts`, `packages/retroplug/src/offline.ts`, `packages/retroplug/src/render/render.ts`, `packages/retroplug/src/render/types.ts`, `packages/retroplug/test-ui/render-badge.test.ts`, `packages/retroplug/test-ui/render-menu.test.ts`, `packages/retroplug/test/offline/offline.test.ts`, `packages/retroplug/test/render/ui-seam.test.ts`, `packages/retroplug/ui/host/nativeHooks.ts`, `packages/retroplug/ui/host/render.ts`, `packages/retroplug/ui/screens/grid/EmulatorTile.tsx`, `packages/retroplug/ui/screens/menu/menuDefs.ts`, `tools/run-cli-ts-tests.sh`

- [ ] **Step 1: Write the failing TypeScript cases**

`test/offline/offline.test.ts` gains `a host with timers: the poll resumes through the yield after a waited step,
never by itself`. `test/render/render-jobs.test.ts` covers the list: a job listed against its system with its
progress, its outcome, cancel and dismiss, and a job whose system has no tile dropped as it settles and on a pruning.
`test/render/ui-seam.test.ts` follows the new `startSystemRender(backend, renders, sys, req, out)`, which starts the
render in a `RenderJobs` stand-in instead of a `__rp_startRender` spy.

Create `packages/retroplug/test/render/render-jobs.test.ts`:

````ts
// The renders the UI has started (src/renderJobs.ts): a job is listed rendering with the system it came from, its
// progress moves renderedMs, and its result settles it done, cancelled or error; a refusal or a throw at submission is
// listed as an error; cancel() is the job handle's; a dismissed job stays gone when it settles; and the engine's console
// lines reach this runtime's console. The jobs here are scripted promises: the engine side is test-native/engine-run
// and the C++ [engine-jobs] cases.
import { test, expect } from "../../testing/harness";
import { RenderJobs } from "../../src/renderJobs";
import { jobHandle, type EngineRunOptions, type EngineRunResult } from "../../src/offline";

interface Scripted {
  options: EngineRunOptions;
  spec: object;
  settle(result: EngineRunResult): void;
  fail(error: Error): void;
  cancelled: number;
}

/** A backend whose engineRun hands out controllable jobs; `jobs` lists them in submission order. */
function scripted(): { backend: { engineRun: RenderJobsBackend }; jobs: Scripted[] } {
  const jobs: Scripted[] = [];
  const engineRun: RenderJobsBackend = (_kind, spec, options = {}) => {
    let resolve!: (r: EngineRunResult) => void;
    let reject!: (e: Error) => void;
    const promise = new Promise<EngineRunResult>((res, rej) => ((resolve = res), (reject = rej)));
    const job: Scripted = { options, spec: spec as object, settle: resolve, fail: reject, cancelled: 0 };
    jobs.push(job);
    return jobHandle(promise, () => {
      job.cancelled++;
      return true;
    });
  };
  return { backend: { engineRun }, jobs };
}
type RenderJobsBackend = ConstructorParameters<typeof RenderJobs>[0]["engineRun"];

const done = (renderedMs: number): EngineRunResult => ({ status: "done", message: "", outputs: ["/o.wav"], renderedMs });
const later = (): Promise<void> => new Promise((r) => void Promise.resolve().then(() => r()));

test("a started render is listed rendering for its system, and progress moves its renderedMs", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(3, { rom: "/a.nes" });
  expect(renders.list()).toEqual([{ id, systemId: 3, state: "rendering", renderedMs: 0, message: "" }]);
  expect(jobs[0].spec).toEqual({ rom: "/a.nes" });
  jobs[0].options.onProgress!(1500);
  expect(renders.list()[0].renderedMs).toBe(1500);
});

test("a result settles the job: done keeps the length written, cancelled and error say so", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  renders.start(2, {});
  renders.start(3, {});
  jobs[0].options.onProgress!(9000);
  jobs[0].settle(done(8100)); // a render that detects its end lowers its count to the length it wrote
  jobs[1].settle({ status: "cancelled", message: "", outputs: [], renderedMs: 200 });
  jobs[2].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
  await later();
  await later();
  const [a, b, c] = renders.list();
  expect([a.state, a.renderedMs]).toEqual(["done", 8100]);
  expect(b.state).toBe("cancelled");
  expect([c.state, c.message]).toEqual(["error", "boom"]);
});

test("a rejected or unsubmittable render is listed as an error", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  jobs[0].fail(new Error("the engine job was refused"));
  const thrower = new RenderJobs({
    engineRun: () => {
      throw new Error("no RPC surface");
    },
  });
  thrower.start(2, {});
  await later();
  await later();
  expect(renders.list()[0].state).toBe("error");
  expect(renders.list()[0].message).toBe("the engine job was refused");
  expect(thrower.list()[0].state).toBe("error");
  expect(thrower.list()[0].message).toBe("no RPC surface");
});

test("cancel() is the job handle's, and false for an id that is not listed", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(1, {});
  expect(renders.cancel(id)).toBe(true);
  expect(jobs[0].cancelled).toBe(1);
  expect(renders.cancel(id + 100)).toBe(false);
});

test("a dismissed job stays gone when it settles", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(1, {});
  renders.dismiss(id);
  jobs[0].options.onProgress!(100);
  jobs[0].settle(done(100));
  await later();
  await later();
  expect(renders.list().length).toBe(0);
});

test("the job's console lines go to this runtime's console by stream", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  const out: string[] = [];
  const log = console.log;
  const err = console.error;
  console.log = (s: string) => void out.push(`out:${s}`);
  console.error = (s: string) => void out.push(`err:${s}`);
  try {
    jobs[0].options.onLog!({ stream: "stdout", text: "render: wrote x" });
    jobs[0].options.onLog!({ stream: "stderr", text: "warning" });
  } finally {
    console.log = log;
    console.error = err;
  }
  expect(out).toEqual(["out:render: wrote x", "err:warning"]);
});

test("a finished job whose system has no tile is dropped as it settles, and when the systems change", async () => {
  const { backend, jobs } = scripted();
  let live = [1, 2];
  const renders = new RenderJobs(backend, () => live);
  renders.start(1, {});
  renders.start(2, {});
  renders.start(3, {}); // a system that is already gone
  jobs[0].settle(done(100));
  jobs[2].settle(done(100));
  await later();
  await later();
  // System 3's finished job went as it settled; system 1's waits for its tile to dismiss it.
  expect(renders.list().map((j) => [j.systemId, j.state])).toEqual([[1, "done"], [2, "rendering"]]);
  jobs[1].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
  await later();
  await later();
  live = [2]; // system 1 is removed: its done job goes with it, system 2's error stays for its tile
  renders.prune();
  expect(renders.list().map((j) => [j.systemId, j.state])).toEqual([[2, "error"]]);
});
````

Run: `mkdir -p packages/retroplug/test/render; sed -n '5850,5994p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test/render/render-jobs.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/render/render-jobs.test.ts`
Expected: `28881acb8a0673d9cee875be8b25a2410b758f60`: the blob aaf1709c8 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test/offline/offline.test.ts b/packages/retroplug/test/offline/offline.test.ts
index 005e88548..a3b361abc 100644
--- a/packages/retroplug/test/offline/offline.test.ts
+++ b/packages/retroplug/test/offline/offline.test.ts
@@ -22,6 +22,7 @@ import {
   offlineInputRef,
   offlineOutputRef,
   setOfflineWait,
+  setOfflineYield,
   type EngineWireCompletion,
   type EngineWireProgress,
   type EngineWireRequest,
@@ -508,3 +509,34 @@ test("a drain that throws rejects every pending run, not just the one that saw i
   expect(jobs.pendingCount()).toBe(0);
   expect(liveJobCount()).toBe(0);
 });
+
+test("a host with timers: the poll resumes through the yield after a waited step, never by itself", async () => {
+  const port = clockPort();
+  const queue = new OfflineJobs(port);
+  const resumes: (() => void)[] = [];
+  setOfflineWait(7);
+  setOfflineYield((resume) => void resumes.push(resume));
+  try {
+    let result: { status: string } | null = null;
+    // Two jobs, so the first waited drain settles one and leaves the other for a further step.
+    void queue.engine("render", { ms: 5, out: "/a.wav" });
+    void queue.engine("render", { ms: 50, out: "/o.wav" }).then((r) => (result = r));
+    // The quiet steps drain without waiting, as microtasks; the first step that waits hands the poll to the yield.
+    for (let i = 0; i < 40 && resumes.length === 0; i++) await Promise.resolve();
+    expect(resumes.length, "the waited step handed the poll to the yield").toBe(1);
+    expect(port.waits.some((w) => w > 0)).toBe(true);
+    const drains = port.waits.length;
+    // Left alone, the poll takes no further step: it resumes only when the host's loop calls the yield's resume.
+    for (let i = 0; i < 40; i++) await Promise.resolve();
+    expect(port.waits.length, "no drain until the host resumes the poll").toBe(drains);
+    expect(result).toBe(null);
+    for (let i = 0; i < 20 && result === null; i++) {
+      resumes.splice(0).forEach((r) => r());
+      for (let k = 0; k < 40; k++) await Promise.resolve();
+    }
+    expect(result!.status).toBe("done");
+  } finally {
+    setOfflineYield(null);
+    setOfflineWait(0);
+  }
+});
diff --git a/packages/retroplug/test/render/ui-seam.test.ts b/packages/retroplug/test/render/ui-seam.test.ts
index 1b2dbe7fe..face29bf8 100644
--- a/packages/retroplug/test/render/ui-seam.test.ts
+++ b/packages/retroplug/test/render/ui-seam.test.ts
@@ -1,6 +1,6 @@
 // The UI render seam (ui/host/render.ts), pure-TS: startSystemRender snapshots the live system's state to a
-// temp file and calls the __rp_startRender hook with the right spec; pickActiveRenderJob chooses the tile's
-// badge job. The native render itself (RenderHost/RenderJobRegistry) is proven separately in C++.
+// temp file and starts the render in the store graph's RenderJobs with the right spec; pickActiveRenderJob chooses
+// the tile's badge job. The render itself (an engine job) is proven separately, in C++ and in test-native.
 
 import { test, expect } from "../../testing/harness";
 import { MockBackend } from "../../testing/mockBackend";
@@ -27,16 +27,18 @@ interface Captured {
   };
 }
 
-/** Install a __rp_startRender spy; `box.last` holds the most recent capture. */
-function spyStartRender(returnId = 7): { box: { last: Captured | null }; restore: () => void } {
-  const g = globalThis as { __rp_startRender?: (id: number, spec: string) => number };
-  const prev = g.__rp_startRender;
+/** A RenderJobs stand-in recording each start; `box.last` holds the most recent capture. */
+function spyStartRender(returnId = 7): { box: { last: Captured | null }; renders: { start(id: number, spec: object): number } } {
   const box: { last: Captured | null } = { last: null };
-  g.__rp_startRender = (systemId, spec) => {
-    box.last = { systemId, spec: JSON.parse(spec) };
-    return returnId;
+  return {
+    box,
+    renders: {
+      start: (systemId, spec) => {
+        box.last = { systemId, spec: JSON.parse(JSON.stringify(spec)) };
+        return returnId;
+      },
+    },
   };
-  return { box, restore: () => { g.__rp_startRender = prev; } };
 }
 
 test("startSystemRender: battery cart → snapshots live SRAM to a temp file and calls the hook with a mix spec", () => {
@@ -46,6 +48,7 @@ test("startSystemRender: battery cart → snapshots live SRAM to a temp file and
 
   const jobId = startSystemRender(
     backend,
+    spy.renders,
     sysView({ id: 1, romPath: "/roms/song.gb", battery: true }),
     { split: "mix", sampleRate: 48000, maxDurationMs: 90000 },
     "/out/song.wav",
@@ -63,17 +66,15 @@ test("startSystemRender: battery cart → snapshots live SRAM to a temp file and
   expect(cap.spec.maxDurationMs).toBe(90000);
   expect(cap.spec.sav).toBe("/config/.render-src-sys1.sav"); // the temp snapshot, under configDir
   expect(cap.spec.state).toBe(undefined);
-  spy.restore();
 });
 
 test("startSystemRender: channels split threads the split mode through", () => {
   const backend = new MockBackend("/config");
   backend.setSram(2, new Uint8Array([9]));
   const spy = spyStartRender();
-  startSystemRender(backend, sysView({ id: 2, battery: true }), { split: "channels" }, "/out/x");
+  startSystemRender(backend, spy.renders, sysView({ id: 2, battery: true }), { split: "channels" }, "/out/x");
   expect(spy.box.last!.spec.split).toBe("channels");
   expect(spy.box.last!.spec.sampleRate).toBe(undefined); // omitted → worker uses the engine default
-  spy.restore();
 });
 
 test("startSystemRender: the instance's configuration rides along, so the offline core is not built at defaults", () => {
@@ -87,11 +88,10 @@ test("startSystemRender: the instance's configuration rides along, so the offlin
     { kind: "mesen", config: { region: "pal", expansionVolume: 50 } },
     { kind: "bliptoaster-assets", config: { overrides: [{ type: "kit", slot: 1, path: "/kits/mine.rkit" }] } },
   ];
-  startSystemRender(backend, sysView({ id: 3, romPath: "/roms/a.nes", battery: true, roles, settings: { gainDb: -6, reloadOnRomChange: false } }), { split: "mix" }, "/out/x.wav");
+  startSystemRender(backend, spy.renders, sysView({ id: 3, romPath: "/roms/a.nes", battery: true, roles, settings: { gainDb: -6, reloadOnRomChange: false } }), { split: "mix" }, "/out/x.wav");
 
   expect(spy.box.last!.spec.roles).toEqual(roles); // verbatim, configs and all
   expect(spy.box.last!.spec.gainDb).toBe(-6);
-  spy.restore();
 });
 
 test("startSystemRender: a default-configured instance sends no roles/gain, so a render stays a plain boot", () => {
@@ -100,30 +100,18 @@ test("startSystemRender: a default-configured instance sends no roles/gain, so a
   const backend = new MockBackend("/config");
   backend.setSram(1, new Uint8Array([1]));
   const spy = spyStartRender();
-  startSystemRender(backend, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/x.wav");
+  startSystemRender(backend, spy.renders, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/x.wav");
   expect(spy.box.last!.spec.roles).toBe(undefined);
   expect(spy.box.last!.spec.gainDb).toBe(undefined);
-  spy.restore();
 });
 
-test("startSystemRender: no on-disk ROM → no-op (returns null, hook not called)", () => {
+test("startSystemRender: no on-disk ROM → no-op (returns null, nothing started)", () => {
   const backend = new MockBackend("/config");
   const spy = spyStartRender();
-  const jobId = startSystemRender(backend, sysView({ id: 1, romPath: "", battery: true }), { split: "mix" }, "/out/x.wav");
+  const jobId = startSystemRender(backend, spy.renders, sysView({ id: 1, romPath: "", battery: true }), { split: "mix" }, "/out/x.wav");
   expect(jobId).toBe(null);
   expect(spy.box.last).toBe(null);
   expect(backend.readSramCalls.length).toBe(0);
-  spy.restore();
-});
-
-test("startSystemRender: no hook bound (headless) → inert null", () => {
-  const g = globalThis as { __rp_startRender?: unknown };
-  const prev = g.__rp_startRender;
-  delete g.__rp_startRender;
-  const backend = new MockBackend("/config");
-  backend.setSram(1, new Uint8Array([1]));
-  expect(startSystemRender(backend, sysView({ id: 1 }), { split: "mix" }, "/out/x.wav")).toBe(null);
-  g.__rp_startRender = prev;
 });
 
 test("renderBaseName: song name → SAV basename → ROM stem, in priority order", () => {
````

Run: `sed -n '6004,6164p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test offline`
Expected: fails to bundle: `BUILD FAILED: offline/offline`, with esbuild's `No matching export in "packages/retroplug/src/offline.ts" for import "setOfflineYield"` (esbuild refuses a value import the module does not export).

Run: `pnpm test render`
Expected: fails: `render-jobs` cannot resolve `../../src/renderJobs`, and `ui-seam`'s `startSystemRender` cases fail on the old signature, which takes no render list; the other three render files pass.

- [ ] **Step 2: The offline yield, the render list and the UI on it**

`src/offline.ts` gains `setOfflineYield(fn | null)`: after a poll step that waited in native, the next step is
scheduled through the yield (a macrotask) instead of a microtask. `src/renderJobs.ts` is the list: `start(systemId,
spec)`, `list()`, `cancel(id)`, `dismiss(id)` and `prune()`, each entry `{id, systemId, state, renderedMs, message}`
plus its job handle, progress from `onProgress`, the outcome from the promise, console lines to `console.log` /
`console.error`. `AppStores.renders` holds one, composed with the systems list so a systems change prunes it.
`ui/host/render.ts`'s `startSystemRender` starts through it (the `__rp_*Render` wrappers go, and `RenderJobStatus` is
re-exported from `src`), the menu passes `ctx.stores.renders`, and the tile's badge samples `renders.list()` and a tap
calls `renders.cancel`, with the same wording and auto-dismiss. `nativeHooks.ts` only changes a type import here; the
hook declarations stay until Task 4.

Create `packages/retroplug/src/renderJobs.ts`:

````ts
// The renders this instance has started from the UI (System > Render), as the tile badge shows them. A render is an
// engine job (backend.engineRun, docs/modular-graph-phase-d3-design.md): this list keeps what the job's promise and
// progress callbacks report - which system it came from, how much audio it has rendered, how it ended - plus the
// job's handle, so a tap on the badge cancels it. It belongs to the store graph, not to the editor: in a DAW the
// graph is the control plane's and outlives the editor, so a render started in one editor session is still listed
// (and its completion still settles through the graph's own queue) when the editor opens again.
//
// Nothing here pumps. A job settles when its queue drains it: at the submission's own pump, from the editor's frame
// tick (ui/app/useJobPump.ts) while one is open, or at the next submission. With the editor closed nothing drains, so a
// render that finishes then is still listed rendering until the editor opens again; its WAV is on disk either way.
//
// A finished job is dismissed by the tile of its system (ui/screens/grid/EmulatorTile.tsx). One whose system has no
// tile (removed, or the project replaced) would never be, so it is dropped as it settles and whenever the systems
// list changes (prune).

import type { Backend } from "./backend";
import type { EngineRunHandle } from "./offline";

/** One render's status, as the tile badge reads it. */
export interface RenderJobStatus {
  id: number;
  systemId: number;
  state: "rendering" | "done" | "error" | "cancelled";
  renderedMs: number; // audio rendered so far (ms); on a finished job, the length written
  message: string; // error detail (state === "error")
}

interface Entry {
  status: RenderJobStatus;
  handle: EngineRunHandle | null; // null when the submission itself threw
}

export class RenderJobs {
  private readonly entries = new Map<number, Entry>();
  private nextId = 1;

  /** `liveSystems` names the systems that have a tile now; without it every job is kept until dismissed. */
  constructor(
    private readonly backend: Pick<Backend, "engineRun">,
    private readonly liveSystems?: () => number[],
  ) {}

  /** Start rendering `spec` (a render job spec) for system `systemId`, and return the job's id here. A render the
   *  engine refuses, or that cannot be submitted, is listed as an error. */
  start(systemId: number, spec: object): number {
    const status: RenderJobStatus = { id: this.nextId++, systemId, state: "rendering", renderedMs: 0, message: "" };
    const entry: Entry = { status, handle: null };
    this.entries.set(status.id, entry);
    const update = (change: Partial<RenderJobStatus>): void => {
      // A job the badge dismissed is no longer listed, and what it reports after is dropped.
      if (this.entries.get(status.id) !== entry) return;
      entry.status = { ...entry.status, ...change };
      if (entry.status.state !== "rendering") this.prune();
    };
    try {
      entry.handle = this.backend.engineRun("render", spec, {
        onProgress: (renderedMs) => update({ renderedMs }),
        onLog: (line) => (line.stream === "stderr" ? console.error(line.text) : console.log(line.text)),
      });
    } catch (e) {
      update({ state: "error", message: (e as Error)?.message ?? String(e) });
      return status.id;
    }
    entry.handle.then(
      (result) => {
        if (result.status === "done") update({ state: "done", renderedMs: result.renderedMs });
        else if (result.status === "cancelled") update({ state: "cancelled" });
        else update({ state: "error", message: result.message });
      },
      (e: unknown) => update({ state: "error", message: (e as Error)?.message ?? String(e) }),
    );
    return status.id;
  }

  /** The renders listed, oldest first. A snapshot: a job's status object is replaced when it changes. */
  list(): RenderJobStatus[] {
    return [...this.entries.values()].map((e) => e.status);
  }

  /** Ask a running render to stop. It ends cancelled once the host drains it. False for a job that has finished. */
  cancel(id: number): boolean {
    return this.entries.get(id)?.handle?.cancel() ?? false;
  }

  /** Drop every finished job whose system has no tile any more. */
  prune(): void {
    if (!this.liveSystems) return;
    const live = new Set(this.liveSystems());
    for (const [id, e] of this.entries)
      if (e.status.state !== "rendering" && !live.has(e.status.systemId)) this.entries.delete(id);
  }

  /** Drop a job from the list (the badge dismissing a finished one). */
  dismiss(id: number): void {
    this.entries.delete(id);
  }
}
````

Run: `mkdir -p packages/retroplug/src; sed -n '6191,6287p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/src/renderJobs.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/src/renderJobs.ts`
Expected: `2e387ed8175b241a10b461142adec23bd3eeb39b`: the blob aaf1709c8 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/src/appStores.ts b/packages/retroplug/src/appStores.ts
index 2d1862671..f5fad73af 100644
--- a/packages/retroplug/src/appStores.ts
+++ b/packages/retroplug/src/appStores.ts
@@ -24,6 +24,7 @@ import { UserConfigStore } from "./userConfigStore";
 import { BindingsStore } from "./bindingsStore";
 import { ProjectStore } from "./projectStore";
 import { FileSelection } from "./fileSelection";
+import { RenderJobs } from "./renderJobs";
 
 /** The categories of store change a consumer can observe. `project` = project settings / dirty;
  *  `systems` = the systems-structure list; the rest name their store. */
@@ -39,6 +40,8 @@ export interface AppStores {
   project: ProjectStore;
   /** Turns a user's file pick (ROM / .sav) into the right systems op — the menu's Load / Add items. */
   fileSelection: FileSelection;
+  /** The renders started from System > Render, which the tile badge lists. */
+  renders: RenderJobs;
 }
 
 export interface ComposeOptions {
@@ -69,12 +72,16 @@ export function composeAppStores({ backend = createRealBackend({ debug: false })
   bindings.ensureDefaults();
 
   const project = new ProjectStore(backend, recent, registry);
-  project.setOnSystemsChange(() => notify("systems"));
+  const renders = new RenderJobs(backend, () => project.systems.view().map((s) => s.id));
+  project.setOnSystemsChange(() => {
+    renders.prune();
+    notify("systems");
+  });
   project.setOnChange(() => notify("project"));
   // Focus is transient: re-render the tiles on the systems channel, but bypass dirty + the graph publish.
   project.systems.setOnFocusChange(() => notify("systems"));
 
   const fileSelection = new FileSelection(backend, project.systems);
 
-  return { backend, registry, recent, userConfig, bindings, project, fileSelection };
+  return { backend, registry, recent, userConfig, bindings, project, fileSelection, renders };
 }
diff --git a/packages/retroplug/src/offline.ts b/packages/retroplug/src/offline.ts
index c07380440..cbb227e68 100644
--- a/packages/retroplug/src/offline.ts
+++ b/packages/retroplug/src/offline.ts
@@ -28,8 +28,8 @@
 // in one synchronous run is in flight at once, a chain of jobs keeps moving, and `await offlineRun(...)` settles with
 // no timer at all. A host whose UI tick pumps never turns the wait on, so no step ever blocks its UI thread. In a host
 // with no tick, one `host.pump()` does not return until nothing is pending: libuv timers and I/O do not run meanwhile,
-// and a CLI's Ctrl-C between pumps is not seen. That is milliseconds for a kit; D3's minute-long renders will need a
-// macrotask yield once a waited step returns in a host with timers.
+// and a CLI's Ctrl-C between pumps is not seen. That is milliseconds for a kit; a minute-long render yields to the
+// host's loop after each waited step where the host has timers (setOfflineYield, which the CLI's render sets).
 //
 // Native holds at most OFFLINE_MAX_JOBS outstanding per engine and refuses a submission past that as busy. The queue
 // surfaces that only after a drain that threw: one runtime's pending promises are exactly its engine's outstanding jobs
@@ -282,6 +282,17 @@ export function setOfflineWait(ms: number): void {
   offlineWaitMs = ms > 0 ? ms : 0;
 }
 
+// How a host with timers lets its event loop run between waited steps, or null for a pure microtask chain.
+let offlineYield: ((resume: () => void) => void) | null = null;
+
+/** Make the poll hand control back to the host's event loop after each step that waited in native: `yieldTo(resume)`
+ *  must call `resume` from a later macrotask (a CLI passes setTimeout(resume, 0)). Without it, one `host.pump()`
+ *  does not return until nothing is pending, so a minute-long render holds libuv's timers, I/O and a Ctrl-C. A host
+ *  with no timers (the native test host never fires setTimeout) must not set it, or the poll would never resume. */
+export function setOfflineYield(yieldTo: ((resume: () => void) => void) | null): void {
+  offlineYield = yieldTo;
+}
+
 function toWire(request: OfflineRunRequest): OfflineWireRequest {
   const inputs = (request.inputs ?? []).map((input): OfflineWireInput => {
     if ("samples" in input) {
@@ -557,10 +568,12 @@ export class OfflineJobs implements JobQueue {
         return;
       }
       let again = false;
+      let waited = false;
       try {
         const wait = this.submitted || this.quiet < QUIET_STEPS ? 0 : offlineWaitMs;
         const before = this.pending.size;
         this.submitted = false;
+        waited = wait > 0;
         const left = this.settle(wait);
         if (left > 0) {
           if (wait > 0 || this.submitted || this.pending.size !== before) this.quiet = 0;
@@ -572,7 +585,9 @@ export class OfflineJobs implements JobQueue {
         // every later submission.
         if (!again) this.polling = false;
       }
-      if (again) void Promise.resolve().then(step);
+      if (!again) return;
+      if (waited && offlineYield) offlineYield(step);
+      else void Promise.resolve().then(step);
     };
     void Promise.resolve().then(step);
   }
diff --git a/packages/retroplug/ui/host/nativeHooks.ts b/packages/retroplug/ui/host/nativeHooks.ts
index 4eee222a2..5e4979fec 100644
--- a/packages/retroplug/ui/host/nativeHooks.ts
+++ b/packages/retroplug/ui/host/nativeHooks.ts
@@ -10,7 +10,7 @@
 // `native` is globalThis itself, not a snapshot - a hook bound (or a test fake installed) after import is seen.
 
 import type { AppStores, StoreChannel } from "../../src/appStores";
-import type { RenderJobStatus } from "./render";
+import type { RenderJobStatus } from "../../src/renderJobs";
 import type { NativeAudioConfig } from "../screens/menu/audioDraft";
 import type { MidiConfig } from "../screens/menu/midiDevices";
 import type { TransportState } from "../screens/menu/transport";
diff --git a/packages/retroplug/ui/host/render.ts b/packages/retroplug/ui/host/render.ts
index 95bdb195d..8958e242a 100644
--- a/packages/retroplug/ui/host/render.ts
+++ b/packages/retroplug/ui/host/render.ts
@@ -1,14 +1,16 @@
-// UI-side seam for background audio rendering. The render itself runs natively (a RenderHost on its own
-// worker thread; see packages/native/src/host/render) — this module only snapshots the live system's state
-// and drives the __rp_* hooks the editor binds (PluginUI.cpp). Inert (no-ops / empty) in the headless
-// harness where the hooks are absent, exactly like openPath / the file browser.
+// UI-side seam for background audio rendering. The render itself is an engine job (backend.engineRun, run by
+// the render library on a worker of the engine's offline pool) - this module only snapshots the live system's
+// state, submits the job through the store graph's RenderJobs list, and holds the pure helpers the Render menu and
+// the tile badge share.
 
 import type { SystemView } from "../../src/systemsStore";
 import type { SplitMode, OnExists } from "../../src/render";
 import { resolveSongCatalog } from "../../src/tracker";
 import { stem } from "../../src/pathUtil";
 import { resolveSavPath } from "../../src/savPaths";
-import { native } from "./nativeHooks";
+import type { RenderJobs, RenderJobStatus } from "../../src/renderJobs";
+
+export type { RenderJobStatus };
 
 /** The backend methods startSystemRender needs — all on the plugin's control-plane channel (host +
  *  emulator facets), so it accepts either Backend or the narrower ControlPlaneBackend structurally. */
@@ -19,15 +21,6 @@ interface RenderBackend {
   writeFileAtomic(path: string, bytes: Uint8Array): boolean;
 }
 
-/** One background render job's status, as __rp_getRenderJobs reports it (mirrors RenderJobRegistry::Status). */
-export interface RenderJobStatus {
-  id: number;
-  systemId: number;
-  state: "rendering" | "done" | "error" | "cancelled";
-  renderedMs: number; // audio rendered so far (ms); on a finished job, the length written
-  message: string; // error detail (state === "error")
-}
-
 /** The render-menu selections carried into a render (from the persisted userConfig.render). */
 export interface RenderRequest {
   split: SplitMode;
@@ -37,13 +30,19 @@ export interface RenderRequest {
 }
 
 /** Snapshot the live system's persisted state (its battery SRAM, or a savestate if the cart has no battery)
- *  to a temp file, then start a background render of a FRESH instance built from it — never the running core.
+ *  to a temp file, then start a background render (an engine job, listed in `renders`) of a FRESH instance built
+ *  from it, never the running core.
  *  The instance's CONFIGURATION (its roles + gain) rides along, so "fresh" means a new core set up the way
  *  this one is, not one at the format's defaults. `outPath` is the chosen WAV path (a prefix for split modes,
- *  matching the CLI). Returns the job id, or null if it couldn't start (no on-disk ROM, or the hook is absent
- *  in the headless harness). */
-export function startSystemRender(backend: RenderBackend, sys: SystemView, req: RenderRequest, outPath: string): number | null {
-  if (!native.__rp_startRender || !sys.romPath) return null;
+ *  matching the CLI). Returns the job's id in `renders`, or null if it couldn't start (no on-disk ROM). */
+export function startSystemRender(
+  backend: RenderBackend,
+  renders: Pick<RenderJobs, "start">,
+  sys: SystemView,
+  req: RenderRequest,
+  outPath: string,
+): number | null {
+  if (!sys.romPath) return null;
   const spec: Record<string, unknown> = { rom: sys.romPath, out: outPath, split: req.split };
   if (req.sampleRate !== undefined) spec.sampleRate = req.sampleRate;
   if (req.maxDurationMs !== undefined) spec.maxDurationMs = req.maxDurationMs;
@@ -87,8 +86,7 @@ export function startSystemRender(backend: RenderBackend, sys: SystemView, req:
     }
   }
 
-  const id = native.__rp_startRender(sys.id, JSON.stringify(spec));
-  return id > 0 ? id : null;
+  return renders.start(sys.id, spec);
 }
 
 /** The base filename a render should default to, in priority order: the loaded tracker cart's WORKING song
@@ -103,21 +101,6 @@ export function renderBaseName(backend: Pick<RenderBackend, "readSram">, sys: Sy
   return song || savName || stem(sys.romPath) || "render";
 }
 
-/** Request cancellation of a running render (cooperative — aborts at the next chunk). */
-export function cancelRender(jobId: number): void {
-  native.__rp_cancelRender?.(jobId);
-}
-
-/** Drop a finished job (the tile dismisses a completed/errored badge). */
-export function dismissRenderJob(jobId: number): void {
-  native.__rp_dismissRenderJob?.(jobId);
-}
-
-/** This editor's background render jobs (for the per-frame tile poll). Empty in the headless harness. */
-export function getRenderJobs(): RenderJobStatus[] {
-  return native.__rp_getRenderJobs?.() ?? [];
-}
-
 /** The split modes valid for a system's platform: mix always; channels on GB/NES; pins on NES only. Used to
  *  gate the Render menu's Split selector + to clamp a stored split to the system being rendered. */
 export function validSplits(sys: Pick<SystemView, "core" | "platform">): SplitMode[] {
diff --git a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
index 4630ccdaa..8fe8ae90c 100644
--- a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
+++ b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
@@ -18,7 +18,7 @@ import type { CanvasHandle } from "../../lvgl/lv";
 import { tagTestId } from "../../lvgl/StableSlot";
 import { LsdjOverlay } from "./LsdjOverlay";
 import { RisaOverlay } from "./RisaOverlay";
-import { getRenderJobs, cancelRender, dismissRenderJob, pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../host/render";
+import { pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../host/render";
 
 export function EmulatorTile({
   systemId,
@@ -40,19 +40,20 @@ export function EmulatorTile({
   /** Test tag for the dim overlay — present only while the tile is unfocused (a live focus signal). */
   dimTestId?: string;
 }) {
-  const { backend } = useStores();
+  const { backend, renders } = useStores();
   const canvasRef = useRef<CanvasHandle | null>(null);
 
   // The active background render badge for this system (or null): poll this system's jobs, auto-dismiss the
   // finished ones, and surface the active render (or a lingering error). Re-renders only when the LABEL
-  // changes (so once a second while rendering). getRenderJobs() is inert (empty) in the headless harness.
+  // changes (so once a second while rendering). The list is the store graph's (src/renderJobs.ts), kept from the
+  // job pump's progress.
   const sampleRenderJob = useCallback((): RenderJobStatus | null => {
-    const jobs = getRenderJobs();
+    const jobs = renders.list();
     for (const j of jobs)
       if (j.systemId === systemId && (j.state === "done" || j.state === "cancelled"))
-        dismissRenderJob(j.id); // the render finished (file written) — drop the job, vanish the badge
+        renders.dismiss(j.id); // the render finished (file written): drop the job, vanish the badge
     return pickActiveRenderJob(jobs, systemId);
-  }, [systemId]);
+  }, [renders, systemId]);
   const renderJob = useFrameSampled(sampleRenderJob, renderJobKey);
 
   useNativeEvent("frame", () => {
@@ -102,7 +103,7 @@ export function EmulatorTile({
         // Background-render badge along the tile's bottom: how much audio has been rendered so far. Tap while
         // rendering to cancel; tap an error to dismiss. (The click also bubbles to focus the tile — harmless.)
         <Box
-          onClick={() => (renderJob.state === "rendering" ? cancelRender(renderJob.id) : dismissRenderJob(renderJob.id))}
+          onClick={() => (renderJob.state === "rendering" ? renders.cancel(renderJob.id) : renders.dismiss(renderJob.id))}
           align={{ type: EAlignType.ALIGN_BOTTOM_MID, pos: [0, 0] }}
           style={{
             width,
diff --git a/packages/retroplug/ui/screens/menu/menuDefs.ts b/packages/retroplug/ui/screens/menu/menuDefs.ts
index 68c79781e..cd821e3ff 100644
--- a/packages/retroplug/ui/screens/menu/menuDefs.ts
+++ b/packages/retroplug/ui/screens/menu/menuDefs.ts
@@ -812,6 +812,7 @@ function renderSubmenu(ctx: MenuContext, sys: SystemView): MenuItem {
       () =>
         void startSystemRender(
           ctx.stores.backend,
+          ctx.stores.renders,
           sys,
           { split, sampleRate: r.sampleRate, maxDurationMs: r.maxDurationSec * 1000, onExists: r.onExists },
           joinPath(effectiveDir, `${curName}.wav`),
````

Run: `sed -n '6297,6565p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test offline`
Expected: `ok 17 - a host with timers: the poll resumes through the yield after a waited step, never by itself`, then `# 17 passed, 0 skipped, 0 failed`.

Run: `pnpm test render`
Expected: `render-jobs` `# 7 passed, 0 skipped, 0 failed`, `ui-seam` 16 passed, then `# 5 test file(s) passed (jobs=16)`.

- [ ] **Step 3: The UI's render tests move to the job path**

`test-ui/scriptedRenders.ts` composes a store graph over the real backend with `engineRun` replaced by jobs a test
controls (or kept real), publishes it on `pluginNamespace()` as the control plane does and reopens the UI so the
provider picks it up. `render-menu` asserts the submitted spec (out, split, rate, onExists) and that the job is listed
against its system; `render-badge` drives rendering, a cancelling tap, cancelled, an error tap and done through the
real tile; and the new `render-job` settles a real 3 s mGB render from `ui.pump` and `backend.pumpJobs` alone (a
microtask-only stretch settles nothing first), sees the badge, and finds the job done, dismissed and its WAV on disk.

Create `packages/retroplug/test-ui/scriptedRenders.ts`:

````ts
// A store graph whose renders are scripted, for the UI tests of System > Render and the tile badge. The harness has an
// engine, but a real render takes seconds and a test wants to see the spec the menu sent and to put a job in each
// state; so the graph is composed over the real backend with engineRun replaced by controllable jobs, and published the
// way the plugin's control plane publishes its own (pluginNamespace().stores), which the next mount of the UI picks up.

import { composeAppStores, type AppStores, type StoreChannel } from "../src/appStores";
import { createRealBackend } from "../src/realBackend";
import { jobHandle, type EngineRunOptions, type EngineRunResult } from "../src/offline";
import { ui } from "./uiHarness";

/** One render the UI submitted: what it was asked, and the controls the test settles it with. */
export interface ScriptedRender {
  spec: Record<string, unknown>;
  options: EngineRunOptions;
  cancelled: number;
  settle(result: EngineRunResult): void;
}

export interface ScriptedStores {
  stores: AppStores;
  renders: ScriptedRender[];
  /** Put the plugin namespace back as it was and remount the UI on a graph of its own. */
  restore(): void;
}

type Namespace = { stores?: AppStores; setUiNotify?: (fn: ((channel: StoreChannel) => void) | null) => void };

/** `real`: keep the backend's own engineRun (real jobs on the harness's engine) and script nothing. */
export function installScriptedStores(real = false): ScriptedStores {
  const sym = Symbol.for("plugin");
  const g = globalThis as Record<symbol, unknown>;
  const before = g[sym] as Namespace | undefined;
  const ns: Namespace = before ?? {};
  const saved = { stores: ns.stores, setUiNotify: ns.setUiNotify };

  const renders: ScriptedRender[] = [];
  const realBackend = createRealBackend({ debug: false });
  const backend = real ? realBackend : {
    ...realBackend,
    engineRun: (_kind: string, spec: object | string, options: EngineRunOptions = {}) => {
      let resolve!: (r: EngineRunResult) => void;
      const promise = new Promise<EngineRunResult>((res) => (resolve = res));
      const job: ScriptedRender = {
        spec: (typeof spec === "string" ? JSON.parse(spec) : JSON.parse(JSON.stringify(spec))) as Record<string, unknown>,
        options,
        cancelled: 0,
        settle: resolve,
      };
      renders.push(job);
      return jobHandle(promise, () => {
        job.cancelled++;
        return true;
      });
    },
  };

  let uiNotify: ((channel: StoreChannel) => void) | null = null;
  const stores = composeAppStores({ backend, notify: (channel) => uiNotify?.(channel) });
  ns.stores = stores;
  ns.setUiNotify = (fn) => {
    uiNotify = fn;
  };
  g[sym] = ns;
  ui.boot();
  ui.reopen(); // the UI's store provider reads the namespace as it mounts

  return {
    stores,
    renders,
    restore() {
      ns.stores = saved.stores;
      ns.setUiNotify = saved.setUiNotify;
      if (before === undefined) delete g[sym];
      ui.reopen();
    },
  };
}
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '6589,6665p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-ui/scriptedRenders.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/scriptedRenders.ts`
Expected: `6031e37b912f030132a42410afe05bc10c24d4e0`: the blob aaf1709c8 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-ui/render-job.test.ts`:

````ts
// A real render settles on the plugin's own path: the editor binds no harness facet and no audio driver, so nothing
// waits in native, and the only mover of the engine job's promise is useJobPump's backend.pumpJobs on a frame. Like
// kit-compile.test.ts for graph jobs, this runs the store graph's RenderJobs over the real backend on this harness
// (registerHostRpc + registerEmulatorRpc, as PluginDSP.cpp and the SDL host mount): the badge appears on the tile of the
// system the render came from, the job ends done, the badge clears, and the WAV is on disk.
import { test, expect, ui, navTo, Key } from "ui-harness";
import { installScriptedStores } from "./scriptedRenders";

test("a real render settles from frame pumps alone: the badge shows, the job ends done, the file is written", async () => {
  const scripted = installScriptedStores(true);
  const { stores } = scripted;
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  const sysId = stores.project.systems.view()[0].id;

  const out = ui.romDir() + "/rp-ui-render.wav";
  stores.backend.deleteFile(out);
  const id = stores.renders.start(sysId, { rom: ui.romDir() + "/mGB.gb", durationMs: 3000, out });

  // Microtasks alone settle nothing: a native wait in the poll would, which is the settle path the plugin lacks.
  for (let i = 0; i < 16; i++) await Promise.resolve();
  expect(stores.renders.list().find((j) => j.id === id)!.state).toBe("rendering");

  let sawBadge = false;
  const until = Date.now() + 60000;
  while (Date.now() < until && stores.renders.list().some((j) => j.id === id && j.state === "rendering")) {
    ui.pump();
    stores.backend.pumpJobs();
    sawBadge ||= ui.findByTextContaining("rendering") != null;
    await Promise.resolve();
  }
  expect(sawBadge, "the tile showed the badge while the render ran").toBe(true);
  const job = stores.renders.list().find((j) => j.id === id);
  expect(job?.state === "done" || job === undefined, `the render ended done: ${job?.message}`).toBe(true);
  expect(stores.backend.fileExists(out)).toBe(true);
  // The tile dismisses the finished job and the badge clears.
  ui.pump(10);
  expect(ui.findByTextContaining("rendering")).toBe(null);
  expect(stores.renders.list().length).toBe(0);

  stores.backend.deleteFile(out);
  scripted.restore();
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '6677,6723p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-ui/render-job.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/render-job.test.ts`
Expected: `998d55fb755a4d2f96eac15ba0cf5be3a66016d1`: the blob aaf1709c8 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test-ui/render-badge.test.ts b/packages/retroplug/test-ui/render-badge.test.ts
index cd550def7..8fcf5dd00 100644
--- a/packages/retroplug/test-ui/render-badge.test.ts
+++ b/packages/retroplug/test-ui/render-badge.test.ts
@@ -1,35 +1,14 @@
-// The background-render badge on a system tile, end to end on the headless display. The editor binds
-// __rp_getRenderJobs natively; the harness doesn't, so (like resize.test.ts spying __rp_setWindowSize) we
-// stub it to feed the tile a fake job and assert the badge renders for each state. Proves the tile's
-// per-frame poll + the badge JSX (Box/Text/align props) actually work — not just typecheck.
+// The background-render badge on a system tile, end to end on the headless display. The tile lists the store graph's
+// renders (src/renderJobs.ts); a scripted graph (test-ui/scriptedRenders.ts) lets the test start a render for the loaded
+// system and move it through each state, then asserts the badge renders for each. Proves the tile's per-frame poll, the
+// badge JSX (Box/Text/align props) and the tap-to-cancel route actually work - not just typecheck.
 
 import { test, expect, ui, navTo, Key } from "ui-harness";
+import { installScriptedStores } from "./scriptedRenders";
 
-interface FakeJob {
-  id: number;
-  systemId: number;
-  state: "rendering" | "done" | "error" | "cancelled";
-  renderedMs: number;
-  message: string;
-}
-
-type RenderGlobals = {
-  __rp_getRenderJobs?: () => FakeJob[];
-  __rp_cancelRender?: (id: number) => void;
-  __rp_dismissRenderJob?: (id: number) => void;
-};
-
-// One job per candidate system id, so whichever id mGB lands on, its tile matches.
-function jobsInState(state: FakeJob["state"], renderedMs: number, message = ""): FakeJob[] {
-  return [0, 1, 2, 3, 4].map((sid) => ({ id: 900 + sid, systemId: sid, state, renderedMs, message }));
-}
-
-test("the render badge shows the rendered duration / error on a tile and clears when the job finishes", () => {
-  const g = globalThis as RenderGlobals;
-  const dismissed: number[] = [];
-  g.__rp_cancelRender = () => {};
-  g.__rp_dismissRenderJob = (id) => dismissed.push(id);
-  g.__rp_getRenderJobs = () => jobsInState("rendering", 42_000);
+test("the render badge shows the rendered duration / error on a tile, cancels on a tap, and clears when the job finishes", () => {
+  const scripted = installScriptedStores();
+  const { stores, renders } = scripted;
 
   expect(ui.boot()).toBeTruthy();
   ui.pump(30);
@@ -37,28 +16,46 @@ test("the render badge shows the rendered duration / error on a tile and clears
   ui.tapKey(Key.Enter);
   ui.pump(30);
   expect(ui.findByTestId("tile-0") != null).toBeTruthy();
+  const sysId = stores.project.systems.view()[0].id;
 
   // Rendering: the per-frame poll surfaces the badge, counting the audio rendered so far (no percentage -
   // a tracker render's true length isn't known until its HFF stop lands).
+  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
+  renders[0].options.onProgress!(42_000);
   ui.pump(10);
   expect(ui.findByTextContaining("rendering") != null).toBeTruthy();
   expect(ui.findByTextContaining("0:42") != null).toBeTruthy();
   expect(ui.findByTextContaining("%")).toBe(null);
 
-  // Error: the badge switches to the failure message.
-  g.__rp_getRenderJobs = () => jobsInState("error", 42_000, "boom");
+  // A tap on the badge cancels the job through its handle.
+  const badge = ui.findByTextContaining("rendering")!;
+  ui.clickAt(badge.x + 2, badge.y + 2);
+  ui.pump(5);
+  expect(renders[0].cancelled).toBe(1);
+
+  // Cancelled: the tile dismisses the job and the badge clears.
+  renders[0].settle({ status: "cancelled", message: "", outputs: [], renderedMs: 42_000 });
+  ui.pump(10);
+  expect(ui.findByTextContaining("rendering")).toBe(null);
+  expect(stores.renders.list().length).toBe(0);
+
+  // Error: the badge switches to the failure message, and a tap dismisses it.
+  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
+  renders[1].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
   ui.pump(10);
   expect(ui.findByTextContaining("render failed") != null).toBeTruthy();
   expect(ui.findByTextContaining("rendering")).toBe(null);
+  const failed = ui.findByTextContaining("render failed")!;
+  ui.clickAt(failed.x + 2, failed.y + 2);
+  ui.pump(5);
+  expect(ui.findByTextContaining("render failed")).toBe(null);
 
   // Done: the tile dismisses the job and the badge clears.
-  g.__rp_getRenderJobs = () => jobsInState("done", 42_100);
+  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
+  renders[2].settle({ status: "done", message: "", outputs: ["/o.wav"], renderedMs: 42_100 });
   ui.pump(10);
-  expect(dismissed.length > 0).toBeTruthy();
-  expect(ui.findByTextContaining("render failed")).toBe(null);
   expect(ui.findByTextContaining("rendering")).toBe(null);
+  expect(stores.renders.list().length).toBe(0);
 
-  delete g.__rp_getRenderJobs;
-  delete g.__rp_cancelRender;
-  delete g.__rp_dismissRenderJob;
+  scripted.restore();
 });
diff --git a/packages/retroplug/test-ui/render-menu.test.ts b/packages/retroplug/test-ui/render-menu.test.ts
index 688247bfe..43acba4d2 100644
--- a/packages/retroplug/test-ui/render-menu.test.ts
+++ b/packages/retroplug/test-ui/render-menu.test.ts
@@ -4,13 +4,10 @@
 //   Split / Sample Rate / Max Duration, and a dialog-less "Render".
 // The file browser is stubbed; we assert the Output Dir opens in DIRECTORY mode and that "Render" writes
 // to <outputDir>/<filename>.wav with the chosen routing / rate / on-exists policy — no dialog at render time.
+// "Render" submits an engine job through the store graph's backend (test-ui/scriptedRenders.ts scripts the job).
 
 import { test, expect, ui, navTo, Key } from "ui-harness";
-
-interface Captured {
-  systemId: number;
-  spec: { split: string; sampleRate?: number; maxDurationMs?: number; onExists?: string; out: string };
-}
+import { installScriptedStores } from "./scriptedRenders";
 
 interface BrowseOpen {
   title: string;
@@ -20,15 +17,10 @@ interface BrowseOpen {
 
 test("render submenu: Output Dir folder-picks, and Render writes <dir>/<name>.wav (no dialog)", () => {
   const g = globalThis as {
-    __rp_startRender?: (id: number, spec: string) => number;
     __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string, sd: string, dir: boolean) => void;
     __rp_onFileBrowserResult?: (path: string | null) => void;
   };
-  let captured: Captured | null = null;
-  g.__rp_startRender = (systemId, spec) => {
-    captured = { systemId, spec: JSON.parse(spec) };
-    return 1;
-  };
+  const scripted = installScriptedStores();
   // Stub the native browser: record each open (title / directory-mode / startDir), then resolve a folder.
   const opens: BrowseOpen[] = [];
   g.__rp_openFileBrowser = (title, _p, _s, _d, sd, dir) => {
@@ -89,7 +81,7 @@ test("render submenu: Output Dir folder-picks, and Render writes <dir>/<name>.wa
   ui.pump(6);
   expect(ui.findByTextContaining("Sample Rate: 48000 Hz") != null).toBeTruthy();
 
-  // Render → dialog-less; __rp_startRender fires with the composed path + the chosen options. No extra
+  // Render → dialog-less; an engine job is submitted with the composed path + the chosen options. No extra
   // browser open (still just the one Output Dir dialog). Focus is on Sample Rate (above the Render action),
   // so this down-only navTo lands on the child action, not the parent "Render" submenu row above it.
   expect(navTo("Render")).toBeTruthy();
@@ -97,13 +89,16 @@ test("render submenu: Output Dir folder-picks, and Render writes <dir>/<name>.wa
   ui.pump(10);
 
   expect(opens.length).toBe(1); // Render opened NO dialog
-  expect(captured != null).toBeTruthy();
-  const cap = captured!;
-  expect(cap.spec.out).toBe("/tmp/renders/mGB.wav"); // <Output Dir>/<Filename>.wav
-  expect(cap.spec.split).toBe("channels");
-  expect(cap.spec.sampleRate).toBe(48000);
-  expect(cap.spec.onExists).toBe("rename");
+  expect(scripted.renders.length).toBe(1);
+  const spec = scripted.renders[0].spec;
+  expect(spec.out).toBe("/tmp/renders/mGB.wav"); // <Output Dir>/<Filename>.wav
+  expect(spec.split).toBe("channels");
+  expect(spec.sampleRate).toBe(48000);
+  expect(spec.onExists).toBe("rename");
+  // The job is listed against the system the menu came from, for that tile's badge.
+  const sysId = scripted.stores.project.systems.view()[0].id;
+  expect(scripted.stores.renders.list().map((j) => [j.systemId, j.state])).toEqual([[sysId, "rendering"]]);
 
-  delete g.__rp_startRender;
+  scripted.restore();
   delete g.__rp_openFileBrowser;
 });
````

Run: `sed -n '6733,6905p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:ui render-menu`
Expected: 1 passed, 0 skipped, 0 failed.

Run: `pnpm test:ui render-badge`
Expected: 1 passed, 0 skipped, 0 failed.

Run: `pnpm test:ui render-job`
Expected: `ok 1 - a real render settles from frame pumps alone: the badge shows, the job ends done, the file is written`, `# 1 UI test file(s) passed (jobs=16)`.

- [ ] **Step 4: The CLI renders through an engine job**

The launcher binds `__rp_onInterrupt(fn)`: in the `keepAlive` loop the first SIGINT calls `fn` on the JS thread and
restores the default action, and a session that registers nothing (the bridges) still exits 0 on Ctrl-C.
`cli/session.ts` exports it as `onInterrupt`. The render tool parses argv into the spec, calls `keepAlive()` and
`setOfflineYield` (cleared in a `finally`), registers `handle.cancel()` as its interrupt, submits
`backend.engineRun("render", spec, {onLog})`, prints stdout lines with `console.log` and stderr lines with
`console.error`, and maps `error` to its message and `cancelled` to `render: cancelled`, each thrown, so the launcher
prints `ERROR: ...` and exits 1. The library's sink gains `discard()`, called when `runRenderJob` catches
`RenderCancelled`, so every cancel path removes the files it opened (a writer patches its header only when it
finishes, so a cut-short file would claim no audio). `RenderJobRegistry` and the tool pass `Blocks::Continuous`. The
runner adds `render-cli` to `DETERMINISTIC_FILES`. `render-cli.test.ts` runs the tool inside the test host with a
stand-in `setTimeout`: a smsggdj song and an LSDj mix and its stems are the baseline's files with the exact library
lines in order; a failing render ends with the job's message; a usage error is thrown before any submit; a cancelled
render leaves no WAV; and the battery `test:cli-ts` renders is the one the baseline authors, byte for byte, so it
cannot drift. That battery is the one binary file in this plan.

Create `packages/retroplug/test-native/render-cli.test.ts`:

````ts
// `retroplug-cli render`'s tool (cli/sessions/render.ts) held to the render baseline: its arguments become an engine job
// (docs/modular-graph-phase-d3-design.md section 5), so the files it writes are the files the baseline's case of the same
// spec records, hash for hash on the baseline's host (the sizes anywhere), and the tool's console output is the render
// library's own lines. A render that fails ends the tool with the job's message, and --list-songs stays in-process.
//
// The tool runs here against the test host's session, not the launcher: the host fires no timers, so the tool's
// setTimeout, which it hands the offline poll to yield to a launcher's loop, is given a stand-in that resumes at once.
// The launcher's own half (an unbounded pump, a Ctrl-C between steps, exit codes, the output streams) is held against the
// real binary by tools/run-cli-ts-tests.sh (pnpm test:cli-ts).
import { test, expect, skip } from "../testing/harness";
import { bootSession } from "../src/bootSession";
import { renderTool } from "../cli/sessions/render";
import { caseNamed, missingFixture, writeScratch } from "./render-cases";
import { fnv1a64Hex } from "./kit/sources";

declare const __RENDER_GOLDEN__: string;
declare const __REPO_RESOURCES_DIR__: string;

interface Golden {
  cases: Record<string, { ms: number; files: { file: string; hash: string; size: number }[] }>;
}

const session = bootSession();
const be = session.backend;
const SCRATCH = `${be.configDir()}/render-cli`;
writeScratch(be, SCRATCH);

const BASELINE_TARGET = "linux-x64-gnu13";
const golden = (): Golden => JSON.parse(new TextDecoder().decode(be.readFile(__RENDER_GOLDEN__)!)) as Golden;

/** Run the render tool with `args`, with the timer the launcher would give it and its console captured. */
async function runTool(args: string[]): Promise<string[]> {
  const g = globalThis as { setTimeout?: unknown };
  const saved = g.setTimeout;
  g.setTimeout = (fn: () => void) => void Promise.resolve().then(fn);
  const lines: string[] = [];
  const log = console.log;
  const err = console.error;
  console.log = (s: string) => void lines.push(s);
  console.error = (s: string) => void lines.push(`stderr: ${s}`);
  try {
    await renderTool.run(session, args);
  } finally {
    console.log = log;
    console.error = err;
    g.setTimeout = saved;
  }
  return lines;
}

/** Render baseline case `name` through the tool, with the flags its spec maps to, and hold its files to the golden. */
async function holdThroughTool(
  name: string,
  flags: string[],
  outs: string[],
): Promise<{ lines: string[]; rom: string; out: string }> {
  const c = caseNamed(name);
  const missing = missingFixture(be, c.fixture);
  if (missing) skip(`render-cli: ${missing}`);
  const spec = c.spec(SCRATCH);
  const base = `${SCRATCH}-${name.split("/").join("_")}`;
  const split = outs.length !== 1 || outs[0] !== "";
  const args = [
    spec.rom as string,
    ...(spec.sav ? ["--sav", spec.sav as string] : []),
    ...flags,
    "--out",
    split ? base : `${base}.wav`,
  ];
  const lines = await runTool(args);
  const expected = golden().cases[name];
  const onTarget = session.audio.buildTarget() === BASELINE_TARGET;
  expect(outs.length, `${name}: files in the golden`).toBe(expected.files.length);
  outs.forEach((suffix, k) => {
    const path = split ? `${base}${suffix}.wav` : `${base}.wav`;
    const bytes = be.readFile(path);
    expect(bytes !== null, `${name}${suffix} was written`).toBe(true);
    be.deleteFile(path);
    expect(bytes!.length, `${name}${suffix}'s size`).toBe(expected.files[k].size);
    if (onTarget) expect(fnv1a64Hex(bytes!), `${name}${suffix}'s hash`).toBe(expected.files[k].hash);
  });
  return { lines, rom: spec.rom as string, out: split ? base : `${base}.wav` };
}

test("a smsggdj song rendered by the tool is the baseline's file", async () => {
  const { lines, rom, out } = await holdThroughTool(
    "smsggdj/sms/index-0/mix",
    ["--song-index", "0", "--duration", "4s"],
    [""],
  );
  // The library's own lines, in order, on the stream it logs them to.
  expect(lines).toEqual([
    'song "METRO" (slot 0) \u2192 work RAM',
    `rendering ${rom} \u2192 ${out} (4000ms)\u2026`,
    out,
    `rendered ${rom} \u2192 ${out} (@44100Hz)`,
  ]);
});

test("an LSDj render by the tool, mix and stems, is the baseline's files", async () => {
  await holdThroughTool("lsdj/tripledipper/fixed-5s/mix", ["--duration", "5s"], [""]);
  await holdThroughTool("lsdj/tripledipper/fixed-5s/channels", ["--duration", "5s", "--split", "channels"], [
    "_pulse1",
    "_pulse2",
    "_wave",
    "_noise",
  ]);
});

test("a render that fails ends the tool with the job's message", async () => {
  let message = "";
  try {
    await runTool([`${SCRATCH}-no-such.gb`, "--duration", "1s"]);
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message.length > 0, "the tool rejected").toBe(true);
  expect(message.includes("no-such.gb"), `the message names the ROM: ${message}`).toBe(true);
});

test("a usage error is thrown before any job is submitted", async () => {
  let message = "";
  try {
    await runTool(["--split", "sideways", "x.gb"]);
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message.startsWith("render: --split must be one of")).toBe(true);
});

test("a cancelled render ends cancelled and leaves no WAV behind (its header would claim no audio)", async () => {
  const c = caseNamed("smsggdj/sms/index-0/mix");
  const out = `${SCRATCH}-cancelled.wav`;
  let started = false;
  const handle = be.engineRun("render", { ...c.spec(SCRATCH), durationMs: 120000, out }, {
    onProgress: () => {
      // The first chunk is rendered, so the file is open with its placeholder header.
      if (!started) {
        started = true;
        handle.cancel();
      }
    },
  });
  const result = await handle;
  expect(result.status).toBe("cancelled");
  expect(be.fileExists(out), "the partial WAV was removed").toBe(false);
});

test("tools/fixtures/render-smsggdj.sav, which test:cli-ts renders, is the battery the baseline's smsggdj cases author", () => {
  const committed = be.readFile(`${__REPO_RESOURCES_DIR__}/../tools/fixtures/render-smsggdj.sav`);
  const authored = be.readFile(`${SCRATCH}-smsggdj.sav`);
  expect(committed !== null && authored !== null).toBe(true);
  expect(fnv1a64Hex(committed!)).toBe(fnv1a64Hex(authored!));
  expect(committed!.length).toBe(authored!.length);
});
````

Run: `mkdir -p packages/retroplug/test-native; sed -n '6940,7094p' docs/modular-graph-phase-d3-plan.md > packages/retroplug/test-native/render-cli.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-native/render-cli.test.ts`
Expected: `b9b120733e68d46a677b63e353c7ae0e87c4d88d`: the blob aaf1709c8 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 478ef691c..a385411ec 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -94,6 +94,18 @@ JSValue jsKeepAlive(JSContext*, JSValueConst, int, JSValueConst*) {
     return JS_UNDEFINED;
 }
 
+// globalThis.__rp_onInterrupt(fn) - a session asks to hear Ctrl-C instead of being stopped by it. Only the unbounded
+// (keepAlive) loop honours it: the FIRST SIGINT calls fn on the JS thread, restores the default disposition so a
+// second one kills the process (a job wedged in native would otherwise hang the exit), and the loop carries on until
+// the session calls tjs.exit. A batch export uses it to cancel its job and exit non-zero; a bridge, whose normal end is
+// Ctrl-C, registers nothing and keeps the clean exit 0.
+JSValue g_onInterrupt = JS_UNDEFINED;
+JSValue jsOnInterrupt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
+    JS_FreeValue(ctx, g_onInterrupt);
+    g_onInterrupt = (argc >= 1 && JS_IsFunction(ctx, argv[0])) ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
+    return JS_UNDEFINED;
+}
+
 // globalThis.__rp_loadTsStripper() — evaluate the compiled-in type stripper into this context, defining
 // globalThis.__stripTypes(source, filename). Called ONCE, lazily, by cli/tsStrip.ts when a `test` / `run`
 // command actually has TypeScript to strip; every other command never pays the ~4 MB. Global-code
@@ -236,6 +248,9 @@ int main(int argc, char** argv) try {
     // bounded batch backstop to run-until-exit (see the pump loop below).
     JS_SetPropertyStr(ctx, global, "__rp_keepAlive", JS_NewCFunction(ctx, jsKeepAlive, "__rp_keepAlive", 0));
 
+    // globalThis.__rp_onInterrupt: a session hears the first Ctrl-C (see jsOnInterrupt).
+    JS_SetPropertyStr(ctx, global, "__rp_onInterrupt", JS_NewCFunction(ctx, jsOnInterrupt, "__rp_onInterrupt", 1));
+
     // globalThis.__rp_loadTsStripper — cli/tsStrip.ts calls it lazily to install __stripTypes.
     JS_SetPropertyStr(ctx, global, "__rp_loadTsStripper",
                       JS_NewCFunction(ctx, jsLoadTsStripper, "__rp_loadTsStripper", 0));
@@ -273,10 +288,23 @@ int main(int argc, char** argv) try {
     if (g_keepAlive) {
         // Long-running session (e.g. a live MIDI bridge): pump until it exits or Ctrl-C. The 250us cadence
         // matches the native bridge loop; the session's own setInterval poll fires as the loop pumps.
-        while (!g_exit.set && !g_sigint) {
+        while (!g_exit.set) {
+            if (g_sigint) {
+                if (JS_IsUndefined(g_onInterrupt)) break;
+                // Hand the first Ctrl-C to the session; a second one gets the default action.
+                g_sigint = 0;
+                std::signal(SIGINT, SIG_DFL);
+                JSValue r = JS_Call(ctx, g_onInterrupt, JS_UNDEFINED, 0, nullptr);
+                if (JS_IsException(r)) tjs_dump_error(ctx);
+                JS_FreeValue(ctx, r);
+                JS_FreeValue(ctx, g_onInterrupt);
+                g_onInterrupt = JS_UNDEFINED;
+            }
             host.pump();
             std::this_thread::sleep_for(std::chrono::microseconds(250));
         }
+        JS_FreeValue(ctx, g_onInterrupt);
+        g_onInterrupt = JS_UNDEFINED;
     } else if (!g_exit.set && !g_sigint) {
         std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
         return 1;
diff --git a/packages/native/src/host/render/RenderJobRegistry.cpp b/packages/native/src/host/render/RenderJobRegistry.cpp
index 8fb9b42d0..4419d9031 100644
--- a/packages/native/src/host/render/RenderJobRegistry.cpp
+++ b/packages/native/src/host/render/RenderJobRegistry.cpp
@@ -92,7 +92,11 @@ void RenderJobRegistry::runJob(Job* job) {
             std::lock_guard<std::mutex> lk(mutex_);
             job->status.renderedMs = ms;
         },
-        [job] { return job->cancelRequested.load(std::memory_order_relaxed); });
+        [job] { return job->cancelRequested.load(std::memory_order_relaxed); },
+        {},
+        // As an engine job renders (OfflineJobs::renderBody), so its bytes are the CLI's and System > Render's; no
+        // caller of this registry is left but retroplug-render-host-test.
+        RenderHost::Blocks::Continuous);
 
     {
         std::lock_guard<std::mutex> lk(mutex_);
diff --git a/packages/native/test/render-host-main.cpp b/packages/native/test/render-host-main.cpp
index 4660e0905..b5f078acc 100644
--- a/packages/native/test/render-host-main.cpp
+++ b/packages/native/test/render-host-main.cpp
@@ -29,7 +29,7 @@ int runOne(const char* jobJson) {
     retroplug::RenderHost::Result r = host.run(
         jobJson,
         [&lastRenderedMs](double ms) { lastRenderedMs = ms; },
-        [] { return false; });
+        [] { return false; }, {}, retroplug::RenderHost::Blocks::Continuous);
     std::fprintf(stderr, "render-host-test: status=%s rendered=%.0fms message=%s outputs=%zu\n",
                  r.status.c_str(), lastRenderedMs, r.message.c_str(), r.outputs.size());
     for (const std::string& p : r.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
diff --git a/packages/retroplug/cli/session.ts b/packages/retroplug/cli/session.ts
index e0be5e02f..ea2a4398f 100644
--- a/packages/retroplug/cli/session.ts
+++ b/packages/retroplug/cli/session.ts
@@ -65,6 +65,13 @@ export function keepAlive(): void {
   (globalThis as { __rp_keepAlive?: () => void }).__rp_keepAlive?.();
 }
 
+/** Hear the first Ctrl-C instead of being stopped by it: the launcher (in its unbounded keepAlive loop) calls `fn` on
+ *  the first interrupt, then restores the default action so a second one kills the process. The session decides how
+ *  it ends (a batch export cancels its job and exits non-zero). No-op if the host didn't bind the hook. */
+export function onInterrupt(fn: () => void): void {
+  (globalThis as { __rp_onInterrupt?: (fn: () => void) => void }).__rp_onInterrupt?.(fn);
+}
+
 /** Boot a session and run `main` WITHOUT auto-exiting on success — for a long-running tool that sets up an
  *  event-driven poll loop (setInterval) + keepAlive() and returns, leaving the native pump to run it until
  *  tjs.exit / Ctrl-C. Any throw during setup is reported and exits 1 (mirrors runSession's catch). */
diff --git a/packages/retroplug/cli/sessions/render.ts b/packages/retroplug/cli/sessions/render.ts
index c739c0da6..2c6e0c9a4 100644
--- a/packages/retroplug/cli/sessions/render.ts
+++ b/packages/retroplug/cli/sessions/render.ts
@@ -6,14 +6,18 @@
 //   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--sample-rate hz]
 //                              [--split mix|channels|pins] [--bpm n] [--transport] [--no-start]
 //
-// This is a thin CLI front: parseRenderArgs turns argv into a RenderOpts, then the shared render library
-// (src/render) does the work — the same code the UI/background render worker runs, so improvements land in
-// both. --list-songs is handled here (it's a query, not a render). See RENDER_HELP in renderArgs.ts for the
-// full flag reference and src/render/render.ts for the render semantics (LSDj auto-length, song selection).
+// This is a thin CLI front: parseRenderArgs turns argv into a RenderOpts, which goes to the engine as a render
+// job (backend.engineRun) - the same job the UI's System > Render submits, run by the render library on a worker
+// of the engine's offline pool. The tool prints the job's console lines as they arrive and maps its result onto
+// the exit code. --list-songs is handled here (it's a query, not a render). See RENDER_HELP in renderArgs.ts for
+// the full flag reference and src/render/render.ts for the render semantics (LSDj auto-length, song selection).
 import { parseRenderArgs, RENDER_SUMMARY, RENDER_HELP, type RenderOpts } from "../renderArgs";
 import type { CliTool } from "../tools";
-import type { Session } from "../session";
-import { platformOf, readSav, readRisaSongs, readSmsggdjSongs, runRenderJob } from "../../src/render";
+import { keepAlive, onInterrupt, type Session } from "../session";
+import { setOfflineYield } from "../../src/offline";
+import { platformOf, readSav, readRisaSongs, readSmsggdjSongs } from "../../src/render";
+
+declare function setTimeout(fn: () => void, ms: number): unknown; // txiki's timers
 
 /** --list-songs: print the sav's populated song slots and exit (renders nothing). GB (LSDj), NES (risa),
  *  SMS/GG (smsggdj). */
@@ -45,13 +49,27 @@ function listSongs(s: Session, o: RenderOpts): void {
   );
 }
 
-function runRender(s: Session, args: string[]): void {
+async function runRender(s: Session, args: string[]): Promise<void> {
   const o = parseRenderArgs(args);
   if (o.listSongs) { listSongs(s, o); return; }
-  // Default hooks: logging → console, no progress/cancel. The Session structurally satisfies RenderContext.
-  // Exporting must not write the user's .sav: see the same call in render/worker.ts.
-  s.project.systems.setBatteryFlush(false);
-  runRenderJob(s, o);
+  // A render is a minutes-long job in a host with timers: the launcher's bounded pump would give up on it, so run the
+  // unbounded pump, and let the job's poll yield to it between waits (src/offline.ts setOfflineYield). The first
+  // Ctrl-C cancels the job, which ends cancelled at its next chunk and removes its partial files (the library's
+  // discard), so the command fails with "render: cancelled" below rather than reporting success over a cut-short
+  // export; a second Ctrl-C is the default action, for a job that does not stop.
+  keepAlive();
+  setOfflineYield((resume) => void setTimeout(resume, 0));
+  try {
+    const handle = s.backend.engineRun("render", o, {
+      onLog: (line) => (line.stream === "stderr" ? console.error(line.text) : console.log(line.text)),
+    });
+    onInterrupt(() => void handle.cancel());
+    const result = await handle;
+    if (result.status === "error") throw new Error(result.message);
+    if (result.status === "cancelled") throw new Error("render: cancelled");
+  } finally {
+    setOfflineYield(null);
+  }
 }
 
 /** The `render` CLI tool: name + summary for the top-level index, detailed --help, and the render body.
diff --git a/packages/retroplug/scripts/run-native-tests.mjs b/packages/retroplug/scripts/run-native-tests.mjs
index 8effeaad6..76722386f 100644
--- a/packages/retroplug/scripts/run-native-tests.mjs
+++ b/packages/retroplug/scripts/run-native-tests.mjs
@@ -33,8 +33,9 @@ const REPO_RESOURCES_DIR = join(REPO, "resources");
 // unnoticed. Same env override scripts/gen-risa-symbols.mjs already uses.
 const RISA_SRC = process.env.RISA_SRC || "/workspaces/risa-v2.2.1-source";
 
-// Files outside parity/ whose cores power on in a fixed state (below): the render baseline and its chunk test.
-const DETERMINISTIC_FILES = new Set(["render-baseline", "render-chunks"]);
+// Files outside parity/ whose cores power on in a fixed state (below): the render baseline, its chunk test and the
+// CLI check.
+const DETERMINISTIC_FILES = new Set(["render-baseline", "render-chunks", "render-cli"]);
 
 const HOST =
   process.env.RETROPLUG_HOST ||
diff --git a/packages/retroplug/src/render/render.ts b/packages/retroplug/src/render/render.ts
index cb7bca348..07b0f54ec 100644
--- a/packages/retroplug/src/render/render.ts
+++ b/packages/retroplug/src/render/render.ts
@@ -1,8 +1,8 @@
 // The shared render orchestration: boot a fresh system from a ROM (+ its battery .sav / a savestate) and
 // stream its audio to WAV — full mix or per-channel/per-pin stems, with tracker song-length auto-detection.
-// Host-neutral: it drives a RenderContext (the CLI's Session or the worker's control plane) and reports
-// through RenderHooks. The CLI `render` command (cli/sessions/render.ts) and the background render worker
-// both call runRenderJob; improving this file improves both.
+// Host-neutral: it drives a RenderContext (a booted control plane) and reports through RenderHooks. The render
+// worker (worker.ts) calls runRenderJob inside every engine job, and the CLI `render` command (cli/sessions/render.ts)
+// and the UI's System > Render both submit one; improving this file improves both.
 //
 // Song-length auto-detect: when a supported tracker is loaded (and no fixed duration is pinned), render to
 // the song's HFF stop, report the length, and trim the output to it. LSDj (GB) detects the stop via the APU
@@ -485,6 +485,7 @@ interface RenderSink {
   renderChunk(ms: number): Float32Array[];
   emit(chunk: Float32Array[], takeFrames: number): void; // commit the first `takeFrames` frames of a chunk
   finishAll(): string[]; // patch every writer's header, log + return the output paths
+  discard(): void; // delete the files opened so far: a cancelled render leaves no WAV whose header says it is empty
 }
 
 /** Left channel of an interleaved-stereo buffer, first `frames` frames (NES streams are mono in the L lane). */
@@ -549,6 +550,12 @@ function buildSink(ctx: RenderContext, o: RenderOpts, id: number, platform: Plat
       log(summary);
       return paths.slice();
     },
+    discard() {
+      // A writer patches its header only in finish(), so a render stopped part way would leave PCM behind a header
+      // that claims none. A cancelled export is not an export: remove what it opened rather than leave a file that
+      // looks usable (the job's status says cancelled, and a shell sees a non-zero exit).
+      for (const p of paths) ctx.backend.deleteFile(p);
+    },
   };
 }
 
@@ -799,20 +806,25 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
 
   // Auto-start playback, capturing the song's opening frames as the recording's head (see pressPlay). The
   // sink exists first so the button-hold render is captured in the right mix/split shape, not discarded.
-  const leadIn = pressPlay(ctx, sink, o, id, platform);
-
-  if (autoDetect) {
-    const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, pullMs, leadIn);
+  try {
+    const leadIn = pressPlay(ctx, sink, o, id, platform);
+
+    if (autoDetect) {
+      const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, pullMs, leadIn);
+      const outputs = sink.finishAll();
+      const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
+      log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
+      if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms — add an HFF to the song end for exact length`);
+      hooks.onRendered?.(lengthMs); // settle on the length actually written (the held tail is trimmed off)
+      return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
+    }
+    const targetFrames = Math.floor(((o.durationMs ?? o.maxDurationMs) * sr) / 1000); // exact target frame count
+    driveFixed(sink, targetFrames, sr, hooks, pullMs, leadIn);
     const outputs = sink.finishAll();
-    const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
-    log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
-    if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms — add an HFF to the song end for exact length`);
-    hooks.onRendered?.(lengthMs); // settle on the length actually written (the held tail is trimmed off)
-    return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
+    hooks.onRendered?.(Math.round((targetFrames / sr) * 1000));
+    return { outputs };
+  } catch (e) {
+    if (e instanceof RenderCancelled) sink.discard();
+    throw e;
   }
-  const targetFrames = Math.floor(((o.durationMs ?? o.maxDurationMs) * sr) / 1000); // exact target frame count
-  driveFixed(sink, targetFrames, sr, hooks, pullMs, leadIn);
-  const outputs = sink.finishAll();
-  hooks.onRendered?.(Math.round((targetFrames / sr) * 1000));
-  return { outputs };
 }
diff --git a/packages/retroplug/src/render/types.ts b/packages/retroplug/src/render/types.ts
index d977388a1..fc2449de8 100644
--- a/packages/retroplug/src/render/types.ts
+++ b/packages/retroplug/src/render/types.ts
@@ -66,7 +66,8 @@ export interface RenderContext {
   audio: RenderAudioDriver;
   // How the engine runs the render's calls: "continuous" in an engine job, whose engine runs whole blocks and carries
   // the rest of a call's last block to the next call of the same shape (docs/modular-graph-phase-d3-design.md
-  // section 4); "exact" (or absent) where each call runs its own length, as the CLI's in-process render does.
+  // section 4); "exact" (or absent) where each call runs its own length, as the in-process callers (the render
+  // tests) run it.
   blocks?: "exact" | "continuous";
 }
 
````

Run: `sed -n '7104,7378p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Decode `tools/fixtures/render-smsggdj.sav` (32768 bytes, gzipped and base64-encoded):

````base64
H4sIAAAAAAACA+3TsUrDUBQG4HNroZMU3HTyIbo7tGoUS6H6CD6IUyCzr6BLZ2dfwPdxk5jbkErA
Sh06iN8HOeTPTUK4P7mdz64n6TS2eW6OKk1Oujw/v1sueutVqtL9Zr24uiz6zx8dPKabty5Pi8Vy
FsBf8JpHOsyzTvEQ9Y6pzmnrWOVRrm/deMmjzBfe89lZjAftJzRvW/tnuW62KD7qroO2iOPeZu+S
FLGXIvb3R5QxjGEZoxh9VfLUVpIGKvmuEgAAAAAAAAAAAAAAAAD4pemFPQAAAAAAAAAAAAAAAAAA
AAB+9gnpzawyAIAAAA==
````

Run: `mkdir -p tools/fixtures; sed -n '7387,7391p' docs/modular-graph-phase-d3-plan.md | base64 -d | gunzip > tools/fixtures/render-smsggdj.sav`
Expected: exit 0: the file is 32768 bytes.

Run: `git hash-object tools/fixtures/render-smsggdj.sav`
Expected: `7f394c34159c0a231ebffbac703f81dfe58e1529`: the blob aaf1709c8 holds for this file.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-cli`
Expected: `ok 6 - tools/fixtures/render-smsggdj.sav, which test:cli-ts renders, is the battery the baseline's smsggdj cases author` and `# 6 passed, 0 skipped, 0 failed` (the run Task 4's tree recorded; this file does not change after this task).

Run: `cmake --build build --target retroplug-cli -j$(nproc)`
Expected: exit 0.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the tool, which still runs `RenderHost` directly, now in continuous blocks.

The tool runs the UI's worker path (`RenderHost` with the job's spec) and the CLI now runs an engine job, so their
files agree only if both run continuous blocks: with the tool left on exact blocks the LSDj mix and its pulse2 stem
differed from byte 21025, because the Start press lands one block boundary later. The render has to sound for the
comparison to bite, which is why it is telemelt with its battery and not a silent ROM.

Run: `mkdir -p /tmp/d3-rht; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/one.wav","durationMs":3000}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/one.json; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/ch","durationMs":3000,"split":"channels"}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/ch.json`
Expected: exit 0: two job specs for 3 s of telemelt (a fixture) with its battery, a mix and its four stems. Telemelt's working song (WORMHOLE) sounds: the prototype's stage 3 run of this song (the ROM and its battery copied side by side) through the same two paths (`/tmp/d3pro/s3work/host-mix` and `host-channels_*`) has 261520 of its 264600 mix samples nonzero, and pulse2, wave and noise sounding (pulse1 rests for these 3 s), so a byte that moves shows in the `cmp` below.

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/one.json)"`
Expected: exit 0, the render's lines printed and `render-host-test: status=done rendered=3000ms message= outputs=1`.

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/ch.json)"`
Expected: exit 0, `render-host-test: status=done rendered=3000ms message= outputs=4` and the four `ch_<stem>.wav` paths.

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --out /tmp/d3-rht/cli.wav`
Expected: exit 0, its last line `rendered <rom> -> <out> (@44100Hz)` (printed with an arrow character).

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --split channels --out /tmp/d3-rht/cli-ch`
Expected: exit 0, its four `cli-ch_<stem>.wav` paths printed.

Run: `cmp /tmp/d3-rht/one.wav /tmp/d3-rht/cli.wav; for c in pulse1 pulse2 wave noise; do cmp /tmp/d3-rht/ch_$c.wav /tmp/d3-rht/cli-ch_$c.wav; done`
Expected: nothing: the mix and each of the four stems are byte-identical. This is the leg the prototype ran by hand on this song's mix and stems (and on a NES's five channels). Then `rm -rf /tmp/d3-rht`.

- [ ] **Step 5: The real binary's render leg**

`tools/run-cli-ts-tests.sh` gains leg 6d: the real `retroplug-cli render` on the fixture-free smsggdj case from a
relative working directory with relative arguments. It checks exit 0, the four stdout lines with the paths as typed,
an empty stderr, the WAV's size against the golden's and, on Linux x86_64 with `RETROPLUG_DETERMINISTIC=1`, its hash;
a missing ROM and a bad `--split` each exit 1 with their message; and SIGINT mid-render (a 600 s cap, the render run
with `exec` so the signal reaches it) exits 1 within 10 s, prints `ERROR: render: cancelled` and leaves no `cut.wav`.

````diff
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index 7bb49ee6c..a31f7b259 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -246,6 +246,107 @@ contains "the refusal names the Nyquist frequency" "$TMP/build-low.err" "Nyquist
 "$CLI" lsdj-rom build-kit "$TMP/kit-low.json" "$TMP/low-cut.kit" --filter LowPass --cutoff 2000 >"$TMP/build-lowcut.log" 2>&1
 check "the same source compiles with a --cutoff under its Nyquist" 0 $?
 
+# 6d. `render` through the REAL binary, which test-native's render-cli.test.ts (the tool inside the test host) cannot
+#     reach: the launcher's unbounded pump, the exit codes, the output streams and Ctrl-C. A render is an engine job
+#     (docs/modular-graph-phase-d3-design.md), so this is also the check that the binary's files are the render
+#     baseline's: the fixture-free smsggdj case, run from a RELATIVE working directory with relative arguments, must
+#     write the golden's bytes (the hash on the baseline's host, Linux x86_64 here; the size anywhere).
+REPO_ROM="$REPO/resources/roms/smsggdj_v0_45.sms"
+GOLDEN="$REPO/packages/retroplug/test-native/golden/render-baseline.json"
+if [ -f "$REPO_ROM" ] && [ -f "$GOLDEN" ]; then
+	RD="$TMP/render"
+	mkdir -p "$RD"
+	cp "$REPO_ROM" "$RD/smsggdj.sms"
+	cp "$FIX/render-smsggdj.sav" "$RD/metro.sav"
+	out="$TMP/render.out"
+	err="$TMP/render.err"
+	(cd "$RD" && RETROPLUG_DETERMINISTIC=1 "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 4s --out metro.wav >"$out" 2>"$err")
+	check "render of a relative path exits 0" 0 $?
+	contains "render prints the song it loaded" "$out" 'song "METRO" (slot 0)'
+	contains "render prints its announcement with the paths as typed" "$out" "rendering smsggdj.sms → metro.wav (4000ms)…"
+	contains "render prints the file it wrote" "$out" "metro.wav"
+	contains "render prints its summary" "$out" "rendered smsggdj.sms → metro.wav (@44100Hz)"
+	if [ -s "$err" ]; then
+		echo "FAIL - a clean render wrote to stderr"
+		sed 's/^/       | /' "$err" >&2
+		fails=$((fails + 1))
+	else
+		echo "ok   - a clean render writes nothing to stderr"
+	fi
+	want="$(python3 - "$GOLDEN" <<'PY'
+import json, sys
+c = json.load(open(sys.argv[1]))["cases"]["smsggdj/sms/index-0/mix"]["files"][0]
+print(c["hash"], c["size"])
+PY
+)"
+	got="$(python3 - "$RD/metro.wav" <<'PY'
+import sys
+h = 0xcbf29ce484222325
+d = open(sys.argv[1], "rb").read()
+for b in d:
+    h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
+print("%016x" % h, len(d))
+PY
+)"
+	if [ "${got#* }" = "${want#* }" ]; then
+		echo "ok   - the CLI's WAV is the golden's size (${got#* } B)"
+	else
+		echo "FAIL - the CLI's WAV is ${got#* } B, the golden's is ${want#* } B"
+		fails=$((fails + 1))
+	fi
+	if [ "$(uname -sm)" = "Linux x86_64" ]; then
+		if [ "${got%% *}" = "${want%% *}" ]; then
+			echo "ok   - the CLI's WAV hashes to the golden's (${got%% *})"
+		else
+			echo "FAIL - the CLI's WAV hashes to ${got%% *}, the golden's is ${want%% *}"
+			fails=$((fails + 1))
+		fi
+	fi
+
+	# A failed render and a usage error exit 1 with their messages, naming the path as typed.
+	(cd "$RD" && "$CLI" render nope.sms --duration 1s >"$TMP/render-bad.out" 2>"$TMP/render-bad.err")
+	check "render of a missing ROM exits 1" 1 $?
+	contains "the missing-ROM message names the path as typed" "$TMP/render-bad.err" "ERROR: render: could not load ROM: nope.sms"
+	(cd "$RD" && "$CLI" render smsggdj.sms --split sideways >"$TMP/render-use.out" 2>"$TMP/render-use.err")
+	check "render with a bad flag value exits 1" 1 $?
+	contains "the usage message says what was wrong" "$TMP/render-use.err" "ERROR: render: --split must be one of mix|channels|pins (got sideways)"
+
+	# Ctrl-C mid-render cancels the job: prompt, non-zero, and no WAV left behind (a cancelled render's header would
+	# claim no audio, so the library removes what it opened). A 600 s cap is ~30 s of work, so it is still running.
+	(cd "$RD" && exec "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 600s --out cut.wav >"$TMP/render-int.out" 2>"$TMP/render-int.err") &
+	pid=$!
+	for _ in $(seq 1 100); do
+		[ -f "$RD/cut.wav" ] && break
+		sleep 0.1
+	done
+	sleep 1
+	started=$SECONDS
+	kill -INT "$pid" 2>/dev/null
+	wait "$pid"
+	code=$?
+	if [ "$code" != "0" ] && [ "$code" != "" ]; then
+		echo "ok   - an interrupted render exits non-zero ($code)"
+	else
+		echo "FAIL - an interrupted render exited $code"
+		fails=$((fails + 1))
+	fi
+	if [ $((SECONDS - started)) -le 10 ]; then
+		echo "ok   - the interrupt stopped the render promptly"
+	else
+		echo "FAIL - the interrupted render took $((SECONDS - started)) s to stop"
+		fails=$((fails + 1))
+	fi
+	contains "an interrupted render reports it was cancelled" "$TMP/render-int.err" "ERROR: render: cancelled"
+	if [ -e "$RD/cut.wav" ]; then
+		echo "FAIL - an interrupted render left $RD/cut.wav behind"
+		fails=$((fails + 1))
+	else
+		echo "ok   - an interrupted render left no WAV whose header claims no audio"
+	fi
+else
+	echo "skip - render leg: the smsggdj ROM or the render golden is absent"
+fi
+
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
 for f in pass.test fail.test helper; do
````

Run: `sed -n '7441,7552p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test:cli-ts`
Expected: the render lines, among them `ok - the CLI's WAV hashes to the golden's (c309fbacf1c2803a)`, `ok - an interrupted render exits non-zero (1)` and `ok - an interrupted render left no WAV whose header claims no audio`, then `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: Regenerate the skip baseline**

Without the fixtures `render-cli` skips its LSDj case, 1 of 6. The diff below is what the regeneration writes; apply
it, then run the regeneration itself.

````diff
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index 9913b23a6..a3485e3ef 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -192,6 +192,10 @@
    "skipped": 3,
    "total": 5
   },
+  "render-cli": {
+   "skipped": 1,
+   "total": 6
+  },
   "risa-220-layout": {
    "skipped": 1,
    "total": 1
````

Run: `sed -n '7567,7581p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native --update-skip-baseline`
Expected: `# 167 native test file(s) passed (jobs=16)`, then `# skip-baseline updated: native now records 65 skipping file(s)`.

Run: `git hash-object packages/retroplug/scripts/skip-baseline.json`
Expected: `a3485e3efbc84d04006206108f32ab08c701c49d`: the blob aaf1709c8 holds.

- [ ] **Step 7: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the plugin and `retroplug-sdl` included (both embed the UI bundle).

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm test`
Expected: `# 172 test file(s) passed` (171 and `render-jobs`).

Run: `pnpm test:plugin`
Expected: all 10 exit 0, `retroplug-engine-graph-test` `test cases: 304 | 293 passed | 11 skipped`, unchanged.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed` (166 and `render-cli`); only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: 167 passed, the skip check passing.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped`, the golden unchanged.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native audio-render`
Expected: 1 passed; `render-guard` 1, `risa-render` (three files) 6, 1 and 1, `sms-render` 6, each run the same way.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed` (40 and `render-job`).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed.

Run: `pnpm test:node`
Expected: 12 pass, 0 fail.

Run: `pnpm test:n8`
Expected: 529 / 57 and 7 / 1.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d3-bt && mkdir -p /tmp/d3-bt/build /tmp/d3-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/d3-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/d3-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/d3-bt/retroplug-cli/ && cd /tmp/d3-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/d3-bt; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`: no CLI render a consumer runs broke.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d3-vj && mkdir -p /tmp/d3-vj/build /tmp/d3-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/d3-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/d3-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/d3-vj/retroplug-cli/ && cd /tmp/d3-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/d3-vj; exit $e'`
Expected: `PASS: PASS: 19/19 test file(s) ok`. Neither repository is edited: `git -C /workspaces/bliptoaster status --short` and `git -C /workspaces/nesvj status --short` print nothing.

- [ ] **Step 8: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/native/cli/main.cpp
 M packages/native/src/host/render/RenderJobRegistry.cpp
 M packages/native/test/render-host-main.cpp
 M packages/retroplug/cli/session.ts
 M packages/retroplug/cli/sessions/render.ts
 M packages/retroplug/scripts/run-native-tests.mjs
 M packages/retroplug/scripts/skip-baseline.json
 M packages/retroplug/src/appStores.ts
 M packages/retroplug/src/offline.ts
 M packages/retroplug/src/render/render.ts
 M packages/retroplug/src/render/types.ts
 M packages/retroplug/test-ui/render-badge.test.ts
 M packages/retroplug/test-ui/render-menu.test.ts
 M packages/retroplug/test/offline/offline.test.ts
 M packages/retroplug/test/render/ui-seam.test.ts
 M packages/retroplug/ui/host/nativeHooks.ts
 M packages/retroplug/ui/host/render.ts
 M packages/retroplug/ui/screens/grid/EmulatorTile.tsx
 M packages/retroplug/ui/screens/menu/menuDefs.ts
 M tools/run-cli-ts-tests.sh
?? packages/retroplug/src/renderJobs.ts
?? packages/retroplug/test-native/render-cli.test.ts
?? packages/retroplug/test-ui/render-job.test.ts
?? packages/retroplug/test-ui/scriptedRenders.ts
?? packages/retroplug/test/render/render-jobs.test.ts
?? tools/fixtures/render-smsggdj.sav
````

````bash
git add -- packages/retroplug/src/renderJobs.ts packages/retroplug/test-native/render-cli.test.ts \
  packages/retroplug/test-ui/render-job.test.ts packages/retroplug/test-ui/scriptedRenders.ts \
  packages/retroplug/test/render/render-jobs.test.ts tools/fixtures/render-smsggdj.sav packages/native/cli/main.cpp \
  packages/native/src/host/render/RenderJobRegistry.cpp packages/native/test/render-host-main.cpp \
  packages/retroplug/cli/session.ts packages/retroplug/cli/sessions/render.ts \
  packages/retroplug/scripts/run-native-tests.mjs packages/retroplug/scripts/skip-baseline.json \
  packages/retroplug/src/appStores.ts packages/retroplug/src/offline.ts packages/retroplug/src/render/render.ts \
  packages/retroplug/src/render/types.ts packages/retroplug/test-ui/render-badge.test.ts \
  packages/retroplug/test-ui/render-menu.test.ts packages/retroplug/test/offline/offline.test.ts \
  packages/retroplug/test/render/ui-seam.test.ts packages/retroplug/ui/host/nativeHooks.ts \
  packages/retroplug/ui/host/render.ts packages/retroplug/ui/screens/grid/EmulatorTile.tsx \
  packages/retroplug/ui/screens/menu/menuDefs.ts tools/run-cli-ts-tests.sh
git commit -F - <<'EOF'
refactor(render): the CLI and the UI render through engine jobs

`retroplug-cli render` and System > Render now submit a render as an engine job
(backend.engineRun) instead of running the render library in-process or on a
RenderJobRegistry thread.

CLI: the tool parses its arguments into the job spec, hands it to the engine
with the paths as typed (the job runs in this process, so it shares the working
directory), awaits the job and prints its console lines as they arrive, mapping
an error or a cancel to the same ERROR line and exit 1. --list-songs stays
in-process. A render is a minutes-long job in a host with timers, so the tool
calls keepAlive() (the launcher's unbounded pump) and gives the offline poll a
macrotask yield after each step that waited in native (setOfflineYield, set for
the run and cleared after it; the native test host fires no timers).

Ctrl-C: the launcher gains __rp_onInterrupt(fn). In the keepAlive loop the
first SIGINT calls fn on the JS thread and restores the default action, so a
second one kills a job wedged in native; a session that registers nothing (the
bridges) still exits 0 on Ctrl-C. The render registers handle.cancel(): the job
ends cancelled at its next chunk and the command fails with "ERROR: render:
cancelled", exit 1, the existing cancelled mapping. A cancelled render deletes
the WAVs it opened (the library's sink discard), since a writer patches its
header only when it finishes and a cut-short file would claim no audio; the UI's
cancel shares the library and behaves the same.

UI: a RenderJobs list in the store graph (src/renderJobs.ts, AppStores.renders)
keeps each render's system, progress and outcome plus its job handle. The menu
submits through the stores' backend; the tile badge reads the list instead of
polling __rp_getRenderJobs and a tap calls the handle's cancel(); finished jobs
auto-dismiss as before, and one whose system has no tile is dropped as it
settles and when the systems list changes. The graph is the control plane's in
a DAW, so a render outlives the editor and its completion is drained by the
queue that submitted it. RenderJobRegistry, the __rp_*Render hooks and
runRenderJob's in-process callers stay for the next stage; the registry and
render-host-test render in continuous blocks so the UI worker == CLI leg still
holds.

Tests: render-cli.test.ts holds the tool's files and exact output lines to the
render baseline (a smsggdj song, an LSDj mix and its stems) and a cancelled
render's removed WAV; tools/run-cli-ts-tests.sh renders a fixture-free smsggdj
song through the real binary from a relative directory (exit codes, lines,
golden hash and size, a missing ROM, a usage error, SIGINT mid-render); the
offline tests pin setOfflineYield; the UI render-menu and render-badge tests run
against a scripted store graph (test-ui/scriptedRenders.ts) and render-job
settles a real render from frame pumps alone; ui-seam follows the new
startSystemRender signature; render-jobs.test.ts covers the list. The render
baseline is not re-recorded.
EOF
````

---

### Task 4: `RenderJobRegistry` and the render hooks retire

Design section 7's fourth commit. Every render now runs as an engine job, so what the UI's per-job threads needed goes:
`RenderJobRegistry` and its CMake entry, the process-global registry in `plugin/PluginUI.cpp` with its four hooks, and
their declarations in `ui/host/nativeHooks.ts`. `RenderHost` runs only as an engine job's body, so it loses its `Blocks`
parameter and always sets continuous blocks; nothing runs the library on an exact-blocks engine with a split any more,
so `RenderContext.blocks` and the settle gate go too, with no byte moved (the gate guarded no combination left). The
in-process render tests keep calling `runRenderJob` as library tests: they test its behaviour at levels, not the seam.
`retroplug-render-host-test` drives engine jobs on an `OfflineJobs` pool.

**Files:**

- Modify: `packages/native/CMakeLists.txt`, `packages/native/plugin/PluginUI.cpp`, `packages/native/sdl/main.cpp`, `packages/native/src/host/engine/OfflineJobs.cpp`, `packages/native/src/host/render/RenderHost.cpp`, `packages/native/src/host/render/RenderHost.hpp`, `packages/native/src/system/mesen/MesenGlobalInit.hpp`, `packages/native/test/engine-graph/EngineJobs.test.cpp`, `packages/native/test/render-host-main.cpp`, `packages/retroplug/src/render/render.ts`, `packages/retroplug/src/render/types.ts`, `packages/retroplug/src/render/worker.ts`, `packages/retroplug/test-ui/midi-clock.test.ts`, `packages/retroplug/ui/host/nativeHooks.ts`
- Delete: `packages/native/src/host/render/RenderJobRegistry.cpp`, `packages/native/src/host/render/RenderJobRegistry.hpp`

- [ ] **Step 1: The registry and the hooks go**

Delete the registry with `git rm`:

````bash
git rm -q -- packages/native/src/host/render/RenderJobRegistry.cpp \
  packages/native/src/host/render/RenderJobRegistry.hpp
````

Run: `git status --short --untracked-files=all`
Expected: one `D  <path>` line per file just removed (staged, worktree gone) and ` m deps/sameboy`; nothing else.

`packages/native/CMakeLists.txt` drops the registry's source and says what the tool drives now; `plugin/PluginUI.cpp`
drops `g_renderJobs`, the four hooks, their binds and the include; `sdl/main.cpp` says renders need no hook, being
engine jobs through the shared backend's RPCs; `nativeHooks.ts` drops the four declarations and the `RenderJobStatus`
import; and `midi-clock.test.ts` stops naming one in a comment.

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index cca97ea4f..77364b1c1 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -206,7 +206,6 @@ add_library(retroplug-backend STATIC
     src/host/dsp/ScriptCompiler.cpp         # bare-QuickJS bytecode compiler (script packs' source, the tests' toys)
     src/host/script/ScriptHost.cpp          # the script-node runtimes: TS nodes behind the C ABI, a pair per pack
     src/host/render/RenderHost.cpp          # bare-QuickJS render host (own Engine, runs the worker bundle)
-    src/host/render/RenderJobRegistry.cpp   # background render jobs — one worker thread each, cancellable
     ${RENDER_WORKER_BUNDLE_C}               # embedded render-worker bytecode (rp_render_worker_bundle)
     ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
 )
@@ -251,9 +250,9 @@ set_target_properties(retroplug-host PROPERTIES
     RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
 )
 
-# retroplug-render-host-test: drives one RenderHost render from a JSON job spec, so a background/UI render
-# can be proven byte-identical to `retroplug-cli render`. EXCLUDE_FROM_ALL — built by name for the parity
-# check, not part of a normal build.
+# retroplug-render-host-test: submits renders from JSON job specs as engine jobs on an OfflineJobs pool (one,
+# several in parallel, or one cancelled), a manual smoke beside the [engine-jobs] cases and a binary to build in
+# build-tsan/ and build-asan/. EXCLUDE_FROM_ALL: built by name, not part of a normal build.
 add_executable(retroplug-render-host-test EXCLUDE_FROM_ALL test/render-host-main.cpp)
 target_link_libraries(retroplug-render-host-test PRIVATE retroplug-backend)
 set_target_properties(retroplug-render-host-test PROPERTIES
diff --git a/packages/native/plugin/PluginUI.cpp b/packages/native/plugin/PluginUI.cpp
index bebbcbc29..5cf11130c 100644
--- a/packages/native/plugin/PluginUI.cpp
+++ b/packages/native/plugin/PluginUI.cpp
@@ -25,7 +25,6 @@
 #include "host/input/GamepadManager.hpp" // SDL controller poll (shared UI-thread input)
 #include "host/input/GamepadPump.hpp"    // the event->bus switch, shared with the SDL standalone
 #include "host/QuickJsGlobals.hpp"       // close guard + file-browser delivery, shared with SDL
-#include "host/render/RenderJobRegistry.hpp" // background render jobs (the __rp_*Render hooks)
 #include "host/ui/LvglWheelScroll.hpp"    // shared wheel → hit-tested scroll — with the SDL host + UI tests
 #include "host/ui/NativeFileDialog.hpp"   // OS-native file picker (pfd) — shared with the SDL host
 
@@ -505,66 +504,6 @@ PluginUI* windowUiFromData(JSContext* ctx, JSValue* funcData) {
     return retroplug::contextTargetFromData<PluginUI>(ctx, funcData);
 }
 
-// Background render jobs, PROCESS-GLOBAL so a render survives its editor window closing (each job spins its
-// own RenderHost + Engine — nothing here references the live plugin). Jobs are tagged with the editor
-// (PluginUI*) that started them, so a multi-instance host only shows its own; the static's destructor
-// cancels + joins any still-running thread at process exit.
-retroplug::RenderJobRegistry g_renderJobs;
-
-// __rp_startRender(systemId, specJson): kick off a background render of a fresh system (the UI has already
-// snapshotted the live SRAM/savestate into the spec's sav/state path). Returns the job id.
-JSValue jsStartRender(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
-    PluginUI* ui = windowUiFromData(ctx, funcData);
-    if (!ui || argc < 2) return JS_NewInt64(ctx, 0);
-    std::uint32_t systemId = 0;
-    JS_ToUint32(ctx, &systemId, argv[0]);
-    const char* spec = JS_ToCString(ctx, argv[1]);
-    retroplug::RenderJobRegistry::JobId id =
-        g_renderJobs.start(ui, systemId, spec ? std::string(spec) : std::string());
-    if (spec) JS_FreeCString(ctx, spec);
-    return JS_NewInt64(ctx, static_cast<std::int64_t>(id));
-}
-
-// __rp_cancelRender(jobId): request cooperative cancellation of a running render.
-JSValue jsCancelRender(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue*) {
-    if (argc >= 1) {
-        std::int64_t id = 0;
-        JS_ToInt64(ctx, &id, argv[0]);
-        g_renderJobs.cancel(static_cast<retroplug::RenderJobRegistry::JobId>(id));
-    }
-    return JS_UNDEFINED;
-}
-
-// __rp_dismissRenderJob(jobId): drop a finished job (the UI dismisses a completed/errored badge).
-JSValue jsDismissRenderJob(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue*) {
-    if (argc >= 1) {
-        std::int64_t id = 0;
-        JS_ToInt64(ctx, &id, argv[0]);
-        g_renderJobs.dismiss(static_cast<retroplug::RenderJobRegistry::JobId>(id));
-    }
-    return JS_UNDEFINED;
-}
-
-// __rp_getRenderJobs(): this editor's render jobs as [{ id, systemId, state, renderedMs, message }], for the
-// per-frame tile poll. state is "rendering" | "done" | "error" | "cancelled".
-JSValue jsGetRenderJobs(JSContext* ctx, JSValueConst, int, JSValueConst*, int, JSValue* funcData) {
-    JSValue arr = JS_NewArray(ctx);
-    PluginUI* ui = windowUiFromData(ctx, funcData);
-    if (!ui) return arr;
-    std::vector<retroplug::RenderJobRegistry::Status> jobs = g_renderJobs.snapshot(ui);
-    for (std::uint32_t i = 0; i < jobs.size(); ++i) {
-        const auto& s = jobs[i];
-        JSValue o = JS_NewObject(ctx);
-        JS_SetPropertyStr(ctx, o, "id", JS_NewInt64(ctx, static_cast<std::int64_t>(s.id)));
-        JS_SetPropertyStr(ctx, o, "systemId", JS_NewUint32(ctx, s.systemId));
-        JS_SetPropertyStr(ctx, o, "state", JS_NewString(ctx, retroplug::RenderJobRegistry::stateName(s.state)));
-        JS_SetPropertyStr(ctx, o, "renderedMs", JS_NewFloat64(ctx, s.renderedMs));
-        JS_SetPropertyStr(ctx, o, "message", JS_NewString(ctx, s.message.c_str()));
-        JS_SetPropertyUint32(ctx, arr, i, o);
-    }
-    return arr;
-}
-
 JSValue jsSetWindowSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
     PluginUI* ui = windowUiFromData(ctx, funcData);
     if (ui && argc >= 2) {
@@ -655,10 +594,6 @@ void installWindowSizeHooks(JSContext* ctx, PluginUI* ui) {
     bind("__rp_quitWindow", jsQuitWindow, 0);
     bind("__rp_openPath", jsOpenPath, 1);
     bind("__rp_setWindowTitle", jsSetWindowTitle, 1);
-    bind("__rp_startRender", jsStartRender, 2);
-    bind("__rp_cancelRender", jsCancelRender, 1);
-    bind("__rp_dismissRenderJob", jsDismissRenderJob, 1);
-    bind("__rp_getRenderJobs", jsGetRenderJobs, 0);
     JS_FreeValue(ctx, g);
 }
 
diff --git a/packages/native/sdl/main.cpp b/packages/native/sdl/main.cpp
index 1ed9c0c6e..e3bd7ef27 100644
--- a/packages/native/sdl/main.cpp
+++ b/packages/native/sdl/main.cpp
@@ -411,7 +411,7 @@ void setHostOutputs(AppState& a) {
 
 // ---- __rp_* window hooks (PluginUI's window-op seam, single-instance) ------------------------------
 // The UI optional-chains these, so anything we omit is simply inert. We provide the handful a handheld
-// standalone needs; the render-job hooks are left to a later milestone.
+// standalone needs; renders need no hook here, they are engine jobs through the shared backend's RPCs.
 
 JSValue jsQuitWindow(JSContext*, JSValueConst, int, JSValueConst*) {
     if (g_app) g_app->running = false;
diff --git a/packages/retroplug/test-ui/midi-clock.test.ts b/packages/retroplug/test-ui/midi-clock.test.ts
index c39d0f6f3..ef6735500 100644
--- a/packages/retroplug/test-ui/midi-clock.test.ts
+++ b/packages/retroplug/test-ui/midi-clock.test.ts
@@ -1,6 +1,5 @@
 // The Instance menu's MIDI Clock submenu on the headless display. The editor binds __rp_getTransport
-// natively; the harness doesn't, so (like render-badge.test.ts stubbing __rp_getRenderJobs) we stub the
-// seam and drive it.
+// natively; the harness doesn't, so we stub the seam and drive it.
 //
 // The case that matters is the one that shipped broken: while an EXTERNAL master is running, its tempo
 // changes on its own — the user drags a fader in their DAW — and a menu built once sat there showing the
diff --git a/packages/retroplug/ui/host/nativeHooks.ts b/packages/retroplug/ui/host/nativeHooks.ts
index 5e4979fec..967cc724b 100644
--- a/packages/retroplug/ui/host/nativeHooks.ts
+++ b/packages/retroplug/ui/host/nativeHooks.ts
@@ -10,7 +10,6 @@
 // `native` is globalThis itself, not a snapshot - a hook bound (or a test fake installed) after import is seen.
 
 import type { AppStores, StoreChannel } from "../../src/appStores";
-import type { RenderJobStatus } from "../../src/renderJobs";
 import type { NativeAudioConfig } from "../screens/menu/audioDraft";
 import type { MidiConfig } from "../screens/menu/midiDevices";
 import type { TransportState } from "../screens/menu/transport";
@@ -47,12 +46,6 @@ export interface RpHooks {
   ) => void;
   __rp_onFileBrowserResult?: (path: string | null) => void;
 
-  // --- background rendering (plugin editor) -----------------------------------------------------------------
-  __rp_startRender?: (systemId: number, specJson: string) => number;
-  __rp_cancelRender?: (jobId: number) => void;
-  __rp_dismissRenderJob?: (jobId: number) => void;
-  __rp_getRenderJobs?: () => RenderJobStatus[];
-
   // --- audio device / MIDI devices / transport (SDL standalone) -------------------------------------------
   __rp_getAudioConfig?: () => NativeAudioConfig;
   __rp_setAudioConfig?: (sampleRate: number, blockSize: number, outChannels: number, driver: string, device: string) => void;
````

Run: `sed -n '7786,7950p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the removed source is gone from the build.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 (the tool is `EXCLUDE_FROM_ALL`, so its old source, which still names the registry until Step 3, is not built here).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed`: no UI test stubbed a hook.

- [ ] **Step 2: RenderHost runs only continuous blocks, and the gate goes**

`RenderHost::Blocks` and its parameter go: `run()` always sets continuous blocks and no longer puts `blocks` on the
plugin namespace, and `OfflineJobs::renderBody` and the test's direct run drop the argument. `RenderContext.blocks`
and the gate term go, so the settle is `o.split === "mix" ? renderAudio : renderAudioPerChannel`, and the settle's and
`driveFixed`'s comments name the continuous-blocks engine of an engine job against the exact-blocks engine of the
in-process tests. The worker drops the field, and the NES "no HFF stop" warning loses its emdash.
`MesenGlobalInit.hpp`'s comment no longer names the registry.

````diff
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index 79b448b29..303a8120e 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -427,12 +427,9 @@ EngineJobCompletion OfflineJobs::runEngine(Job& job) {
 
 EngineJobOutcome OfflineJobs::renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks) {
     // Built, run and destroyed on this thread: the runtime anchors its stack check to the thread that makes it, and
-    // the job's Engine, its cores and its plan's nodes go before this worker takes another job. In continuous blocks:
-    // the engine runs whole blocks whatever each of the render's calls asks for (docs/modular-graph-phase-d3-design.md
-    // section 4).
+    // the job's Engine, its cores and its plan's nodes go before this worker takes another job.
     retroplug::RenderHost         host;
-    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console,
-                                               retroplug::RenderHost::Blocks::Continuous);
+    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console);
     return {std::move(r.status), std::move(r.message), std::move(r.outputs)};
 }
 
diff --git a/packages/native/src/host/render/RenderHost.cpp b/packages/native/src/host/render/RenderHost.cpp
index 935670c53..703ded811 100644
--- a/packages/native/src/host/render/RenderHost.cpp
+++ b/packages/native/src/host/render/RenderHost.cpp
@@ -212,7 +212,7 @@ RenderHost::~RenderHost() {
 }
 
 RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled,
-                                   ConsoleFn onConsole, Blocks blocks) {
+                                   ConsoleFn onConsole) {
     renderedFn_ = std::move(onRendered);
     cancelFn_ = std::move(isCancelled);
     consoleFn_ = std::move(onConsole);
@@ -225,7 +225,7 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
     QueuedInvoker invoker{engine};
     HostRpcService hostSvc;
     EngineRpcService engineSvc{engine, invoker};
-    engineSvc.setContinuousBlocks(blocks == Blocks::Continuous);
+    engineSvc.setContinuousBlocks(true);
     DebugRpcService debugSvc{engine};
     AudioDriverRpcService driver{engine, invoker};
 
@@ -243,8 +243,7 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
     // and driven on the same (worker) thread, but re-anchor before entering JS to be safe (as ScriptHost does).
     JS_UpdateStackTop(rt_);
 
-    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson], blocks }: the worker reads args[0], and
-    // `blocks` ("exact" or "continuous") tells the render library which way its calls run.
+    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson] }: the worker reads args[0].
     // bindRpcSend here is the bare-runtime one (this host has no TjsHostRuntime), which is why
     // installPluginNamespace takes a callback rather than a runtime.
     installPluginNamespace(ctx_, [&](JSValue ns) {
@@ -252,8 +251,6 @@ RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRend
         JSValue args = JS_NewArray(ctx_);
         JS_SetPropertyUint32(ctx_, args, 0, JS_NewStringLen(ctx_, jobJson.data(), jobJson.size()));
         JS_SetPropertyStr(ctx_, ns, "args", args);
-        const char* mode = blocks == Blocks::Continuous ? "continuous" : "exact";
-        JS_SetPropertyStr(ctx_, ns, "blocks", JS_NewString(ctx_, mode));
     });
 
     // Run the worker bundle as global-code bytecode. Its top-level main() reads the spec, renders, and
diff --git a/packages/native/src/host/render/RenderHost.hpp b/packages/native/src/host/render/RenderHost.hpp
index c06b716e0..2775e2f21 100644
--- a/packages/native/src/host/render/RenderHost.hpp
+++ b/packages/native/src/host/render/RenderHost.hpp
@@ -3,10 +3,11 @@
 // RenderHost — a self-contained, headless audio-render environment: a BARE QuickJS runtime (no txiki, no
 // libuv) + its own Engine + the in-process RPC bridge, running the render-worker bundle
 // (packages/retroplug/src/render/worker.ts) as global-code bytecode. It is the "retroplug-cli render"
-// pipeline, but embeddable and driven synchronously on the calling thread, designed to run on a worker
-// thread (a dedicated one per job in RenderJobRegistry; a pool worker in an engine job, host/engine/OfflineJobs.hpp),
-// so the plugin can render a fresh system to disk without touching the live audio-thread cores. Built, run and
-// destroyed on the one thread.
+// pipeline, driven synchronously on the calling thread: the body of an engine job, run on a pool worker
+// (host/engine/OfflineJobs.hpp), so a fresh system renders to disk without touching the live audio-thread cores.
+// Built, run and destroyed on the one thread. The job's engine runs whole blocks whatever each of the render's calls
+// asks for, the frames past a call carried to the next (EngineRpcService::setContinuousBlocks), so a render's bytes do
+// not depend on how much each call pulls (docs/modular-graph-phase-d3-design.md section 4).
 //
 // One-shot: construct, run() once, destroy. run() blocks for the whole render. Only console +
 // TextEncoder/TextDecoder + the RPC / rendered / cancel / result thunks are bound (the render path needs
@@ -31,12 +32,6 @@ public:
     // ones (console.warn / error / trace) and for a script exception's report.
     using ConsoleFn  = std::function<void(bool error, const char* text)>;
 
-    // How the job's engine runs the blocks the worker's render calls pull. Exact: each call as whole blocks and then
-    // one of the remainder, as the CLI's in-process render runs them. Continuous: whole blocks only, the frames past a
-    // call carried to the next (EngineRpcService::setContinuousBlocks), so a render's bytes do not depend on how much
-    // each call asks for; an engine job runs this way (docs/modular-graph-phase-d3-design.md section 4).
-    enum class Blocks { Exact, Continuous };
-
     struct Result {
         std::string status;                // "done" | "cancelled" | "error"; empty if the worker never reported
         std::string message;               // error detail (when status == "error")
@@ -54,7 +49,7 @@ public:
     // onConsole the worker's console writes go to the process's stdout and stderr. Returns the worker's reported
     // result (status "error" with a message on any internal failure).
     Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {},
-               ConsoleFn onConsole = {}, Blocks blocks = Blocks::Exact);
+               ConsoleFn onConsole = {});
 
     // Called by the bound JS thunks (which recover the host via the JS context opaque). Public so the
     // free-function thunks in the .cpp can reach them; not part of the external API.
diff --git a/packages/native/src/system/mesen/MesenGlobalInit.hpp b/packages/native/src/system/mesen/MesenGlobalInit.hpp
index b9dd2fe83..6b9c1ad74 100644
--- a/packages/native/src/system/mesen/MesenGlobalInit.hpp
+++ b/packages/native/src/system/mesen/MesenGlobalInit.hpp
@@ -4,8 +4,8 @@
 
 // One-time, thread-safe initialization of Mesen's PROCESS-GLOBAL state (the "home folder" for its incidental
 // config writes + the message-manager options). Both MesenNesSystem and MesenGbaSystem set the same values
-// at construct; with the background render path building cores on multiple worker threads at once
-// (RenderJobRegistry), those setters would race. std::call_once makes the setup run exactly once, whichever
+// at construct; with the engine jobs' renders building cores on multiple pool workers at once
+// (host/engine/OfflineJobs.hpp), those setters would race. std::call_once makes the setup run exactly once, whichever
 // thread constructs the first Mesen core.
 void mesenGlobalInit();
 
diff --git a/packages/native/test/engine-graph/EngineJobs.test.cpp b/packages/native/test/engine-graph/EngineJobs.test.cpp
index bdf76ee52..9c9232aa4 100644
--- a/packages/native/test/engine-graph/EngineJobs.test.cpp
+++ b/packages/native/test/engine-graph/EngineJobs.test.cpp
@@ -117,7 +117,7 @@ std::string fixture(const char* relative) {
 }
 
 // What a RenderHost run directly on this thread gives for `spec`: its result, and its console lines, collected rather
-// than printed. In continuous blocks, as an engine job runs its RenderHost.
+// than printed, run as an engine job runs its RenderHost.
 struct Direct {
     retroplug::RenderHost::Result result;
     std::vector<std::string>      lines;
@@ -125,8 +125,7 @@ struct Direct {
 Direct renderDirectly(const std::string& spec) {
     Direct                d;
     retroplug::RenderHost host;
-    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); },
-                        retroplug::RenderHost::Blocks::Continuous);
+    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); });
     return d;
 }
 
diff --git a/packages/retroplug/src/render/render.ts b/packages/retroplug/src/render/render.ts
index 07b0f54ec..218692695 100644
--- a/packages/retroplug/src/render/render.ts
+++ b/packages/retroplug/src/render/render.ts
@@ -559,10 +559,10 @@ function buildSink(ctx: RenderContext, o: RenderOpts, id: number, platform: Plat
   };
 }
 
-/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk). In continuous blocks
- *  (RenderContext.blocks, an engine job) the output is byte-identical to a single renderAudio(ms) of the same length
- *  for any sample rate / ms / pull; in exact blocks only while no node consumes the transport or MIDI block by block
- *  (a sync node such as rp.sms.sync), whose clock then follows the pulls' splits. `leadIn` is the
+/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk). On the continuous-blocks engine
+ *  of an engine job the output is byte-identical to a single renderAudio(ms) of the same length for any sample rate /
+ *  ms / pull; on an exact-blocks engine (the in-process tests) only while no node consumes the transport or MIDI
+ *  block by block (a sync node such as rp.sms.sync), whose clock then follows the pulls' splits. `leadIn` is the
  *  captured play-gesture audio (the song's opening frames) — committed first so the recording's head isn't
  *  lost, trimmed against the target like any other chunk. */
 function driveFixed(
@@ -757,13 +757,12 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
       : undefined;
   const id = buildSystem(ctx, o, platform, seed?.seed);
 
-  // Settle boot (past the mGB/LSDj splash) before driving playback. Discarded, but in continuous blocks it runs in
-  // the shape the render streams in: the frames past a call in its last block go to the next call only if that call
-  // is of the same shape (EngineRpcService::setContinuousBlocks), so a split render settling in the mix would start
-  // its stems up to a block later than the same render's mix starts, and its detected length would differ by as
-  // much. Exact blocks settle in the mix: there the stems start where the mix does either way, and the shape a NES
-  // settles in moves its stems' bytes.
-  if (o.split === "mix" || ctx.blocks !== "continuous") ctx.audio.renderAudio(1500);
+  // Settle boot (past the mGB/LSDj splash) before driving playback. Discarded, but it runs in the shape the render
+  // streams in: on a continuous-blocks engine (an engine job's) the frames past a call in its last block go to the
+  // next call only if that call is of the same shape (EngineRpcService::setContinuousBlocks), so a split render
+  // settling in the mix would start its stems up to a block later than the same render's mix starts, and its detected
+  // length would differ by as much. An exact-blocks engine carries nothing, so there the shape is only a choice.
+  if (o.split === "mix") ctx.audio.renderAudio(1500);
   else ctx.audio.renderAudioPerChannel(id, 1500);
   if (o.bpm) ctx.audio.setBpm(o.bpm);
   if (o.transport) ctx.audio.setTransport(true);
@@ -814,7 +813,7 @@ export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHoo
       const outputs = sink.finishAll();
       const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
       log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
-      if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms — add an HFF to the song end for exact length`);
+      if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms - add an HFF to the song end for exact length`);
       hooks.onRendered?.(lengthMs); // settle on the length actually written (the held tail is trimmed off)
       return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
     }
diff --git a/packages/retroplug/src/render/types.ts b/packages/retroplug/src/render/types.ts
index fc2449de8..ccf96fac6 100644
--- a/packages/retroplug/src/render/types.ts
+++ b/packages/retroplug/src/render/types.ts
@@ -46,7 +46,8 @@ export interface RenderOpts {
   // render-chunks.test.ts leaves it. A test knob: only in an engine job (continuous blocks) does the engine run whole
   // blocks whatever a pull asks for, so there a render's samples do not depend on it (its play gestures and song load
   // keep lengths of their own); an auto-detect render's pull is also its detection poll, though, so the length it
-  // ends at moves with it. In exact blocks (the UI's RenderJobRegistry) a render with a sync node moves with it.
+  // ends at moves with it. On an exact-blocks engine (the in-process render tests) a render with a sync node moves
+  // with it.
   pullMs?: number;
 }
 
@@ -64,11 +65,6 @@ export interface RenderContext {
   backend: Backend; // readFile / readCpu + the WavBackend writers (writeFile / appendFile / writeFileAt)
   project: ProjectStore;
   audio: RenderAudioDriver;
-  // How the engine runs the render's calls: "continuous" in an engine job, whose engine runs whole blocks and carries
-  // the rest of a call's last block to the next call of the same shape (docs/modular-graph-phase-d3-design.md
-  // section 4); "exact" (or absent) where each call runs its own length, as the in-process callers (the render
-  // tests) run it.
-  blocks?: "exact" | "continuous";
 }
 
 /** Host callbacks. All optional so the CLI passes none (logging defaults to console, no progress/cancel).
diff --git a/packages/retroplug/src/render/worker.ts b/packages/retroplug/src/render/worker.ts
index 1bd609114..739deea62 100644
--- a/packages/retroplug/src/render/worker.ts
+++ b/packages/retroplug/src/render/worker.ts
@@ -46,8 +46,8 @@ function normalize(spec: RenderJobSpec): RenderOpts {
   };
 }
 
-/** What the RenderHost puts on globalThis[Symbol.for("plugin")]: the job spec, and how its engine runs blocks. */
-type PluginNamespace = { args?: string[]; blocks?: string };
+/** What the RenderHost puts on globalThis[Symbol.for("plugin")]: the job spec. */
+type PluginNamespace = { args?: string[] };
 function pluginNamespace(): PluginNamespace | undefined {
   return (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as PluginNamespace | undefined;
 }
@@ -70,13 +70,11 @@ function main(): void {
   }
 
   const session = bootSession(); // structurally satisfies RenderContext (backend / project / audio)
-  // An engine job's host runs whole blocks (RenderHost::Blocks), which the settle follows (runRenderJob).
-  const blocks = pluginNamespace()?.blocks === "continuous" ? "continuous" : "exact";
   // A render is an EXPORT. It builds systems, may load a savestate into one, and drops them again, so the
   // before-teardown battery flush would turn an export into a write in the user's save folder.
   session.project.systems.setBatteryFlush(false);
   try {
-    const ctx = { backend: session.backend, project: session.project, audio: session.audio, blocks } as const;
+    const ctx = { backend: session.backend, project: session.project, audio: session.audio } as const;
     const result = runRenderJob(ctx, opts, {
       onRendered: (ms) => __rp_reportRenderedMs?.(ms),
       isCancelled: () => __rp_isRenderCancelled?.() ?? false,
````

Run: `sed -n '7975,8218p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-engine-graph-test "[engine-jobs]"`
Expected: `test cases: 16 | 15 passed | 1 skipped`, as in Task 2.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`: zero bytes moved, the golden untouched.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-cli`
Expected: `# 6 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native audio-render`
Expected: 1 passed; `render-guard` 1, `risa-render` (three files) 6, 1 and 1, `sms-render` 6, each run the same way: the in-process render tests, unchanged.

- [ ] **Step 3: retroplug-render-host-test drives engine jobs**

The tool is rewritten on `rp::host::OfflineJobs`: `<job-json>` submits one engine job and drains it, `--parallel
<j>...` runs N jobs on a pool sized `min(N + 1, kMaxWorkers)` and asserts the most jobs one drain saw running or
completing together is at least `min(N, engineBudget())`, and `--cancel <j>` cancels after 300 ms and asserts the
completion is cancelled with no outputs and the spec's `out` absent. Every console and status line goes to stderr,
flushed, so a redirected log keeps their order. `--registry` is gone and is now an unknown usage.

````diff
diff --git a/packages/native/test/render-host-main.cpp b/packages/native/test/render-host-main.cpp
index b5f078acc..ac14ceea6 100644
--- a/packages/native/test/render-host-main.cpp
+++ b/packages/native/test/render-host-main.cpp
@@ -1,101 +1,163 @@
-// retroplug-render-host-test — a thin harness for the background-render pieces:
+// retroplug-render-host-test - a thin harness for the engine jobs that run renders (host/engine/OfflineJobs.hpp):
 //
-//   retroplug-render-host-test <job-json>          drive ONE RenderHost render (byte-identical parity vs
-//                                                   `retroplug-cli render`; exits 0 on a "done" result)
-//   retroplug-render-host-test --registry <j1> <j2>...   run N jobs CONCURRENTLY through RenderJobRegistry,
-//                                                   poll to completion, assert all done (exits 0)
-//   retroplug-render-host-test --cancel <job-json>  start a job, cancel it mid-render, assert it ends
-//                                                   "cancelled" (exits 0)
+//   retroplug-render-host-test <job-json>          run ONE render as an engine job (its bytes are those of
+//                                                   `retroplug-cli render`, which submits the same job; exits 0 on
+//                                                   a "done" completion)
+//   retroplug-render-host-test --parallel <j1> <j2>...   run N renders as engine jobs on one pool sized so all N run at
+//                                                   once; assert every one is done and that they overlapped (exits 0)
+//   retroplug-render-host-test --cancel <job-json>  submit a render, cancel it mid-render, assert it completes
+//                                                   "cancelled" with no outputs and no file left (exits 0)
 //
-// Not shipped; built by name for the Phase-2/3 checks.
+// Not shipped; built by name for the render checks (and under the sanitizers, in build-tsan/ and build-asan/, for the
+// pool's threads). The [engine-jobs] cases of retroplug-engine-graph-test are the automated guard.
 
+#include <algorithm>
 #include <chrono>
 #include <cstdio>
 #include <cstring>
+#include <filesystem>
+#include <map>
+#include <set>
 #include <string>
 #include <thread>
 #include <vector>
 
-#include "host/render/RenderHost.hpp"
-#include "host/render/RenderJobRegistry.hpp"
+#include "host/engine/OfflineJobs.hpp"
 
 namespace {
 
-using retroplug::RenderJobRegistry;
+using rp::host::EngineJobCompletion;
+using rp::host::EngineJobsDrain;
+using rp::host::OfflineJobs;
 
-int runOne(const char* jobJson) {
-    retroplug::RenderHost host;
-    double lastRenderedMs = -1.0;
-    retroplug::RenderHost::Result r = host.run(
-        jobJson,
-        [&lastRenderedMs](double ms) { lastRenderedMs = ms; },
-        [] { return false; }, {}, retroplug::RenderHost::Blocks::Continuous);
-    std::fprintf(stderr, "render-host-test: status=%s rendered=%.0fms message=%s outputs=%zu\n",
-                 r.status.c_str(), lastRenderedMs, r.message.c_str(), r.outputs.size());
-    for (const std::string& p : r.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
-    return r.ok() ? 0 : 1;
+// Every line goes to stderr, flushed as written, so the jobs' console lines and this tool's own status lines keep their
+// order in a redirected log.
+void echo(const std::vector<rp::host::EngineJobLine>& lines) {
+    for (const auto& l : lines) std::fprintf(stderr, "%s\n", l.text.c_str());
+    std::fflush(stderr);
 }
 
-// Run N jobs concurrently through the registry; poll snapshots until all terminal. Returns 0 iff all Done.
-int runRegistry(const std::vector<std::string>& jobs) {
-    RenderJobRegistry registry;
-    for (std::size_t i = 0; i < jobs.size(); ++i) registry.start(nullptr, static_cast<std::uint32_t>(i + 1), jobs[i]);
+struct Collected {
+    std::map<std::uint32_t, EngineJobCompletion> done;
+    std::size_t                                  mostRunning = 0;  // the most jobs one drain saw running at once
+};
 
-    for (;;) {
-        std::this_thread::sleep_for(std::chrono::milliseconds(50));
-        auto snap = registry.snapshot(nullptr);
-        std::size_t terminal = 0;
-        for (const auto& s : snap)
-            if (s.state != RenderJobRegistry::State::Rendering) ++terminal;
-        if (terminal == snap.size()) {
-            int rc = 0;
-            for (const auto& s : snap) {
-                std::fprintf(stderr, "job %llu (sys %u): %s rendered=%.0fms outputs=%zu %s\n",
-                             (unsigned long long)s.id, s.systemId, RenderJobRegistry::stateName(s.state),
-                             s.renderedMs, s.outputs.size(), s.message.c_str());
-                if (s.state != RenderJobRegistry::State::Done) rc = 1;
-            }
-            registry.clearFinished();
-            return rc;
+// Drains the pool until `count` jobs have completed (or `timeout` passes), echoing the jobs' console lines.
+Collected collect(OfflineJobs& pool, std::size_t count, std::chrono::seconds timeout = std::chrono::seconds(900)) {
+    Collected   out;
+    const auto  end = std::chrono::steady_clock::now() + timeout;
+    while (out.done.size() < count && std::chrono::steady_clock::now() < end) {
+        EngineJobsDrain drain;
+        pool.drain(std::chrono::milliseconds(100), nullptr, &drain);
+        std::set<std::uint32_t> running;
+        for (const auto& p : drain.progress) {
+            running.insert(p.id);
+            echo(p.lines);
         }
+        for (auto& c : drain.completions) {
+            running.insert(c.id);
+            echo(c.lines);
+            out.done.emplace(c.id, std::move(c));
+        }
+        out.mostRunning = std::max(out.mostRunning, running.size());
     }
+    return out;
 }
 
-// Start one (long) job, cancel it shortly after, assert it terminates "cancelled".
-int runCancel(const char* jobJson) {
-    RenderJobRegistry registry;
-    RenderJobRegistry::JobId id = registry.start(nullptr, 1, jobJson);
-    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let it get past boot into the render loop
-    registry.cancel(id);
+std::uint32_t submit(OfflineJobs& pool, const std::string& spec) {
+    rp::host::OfflineSubmit s = pool.submitEngine({"render", spec});
+    if (s.id == 0) std::fprintf(stderr, "render-host-test: submit refused: %s\n", s.error.c_str());
+    return s.id;
+}
 
-    for (;;) {
-        std::this_thread::sleep_for(std::chrono::milliseconds(50));
-        auto snap = registry.snapshot(nullptr);
-        if (snap.empty()) break;
-        const auto& s = snap.front();
-        if (s.state != RenderJobRegistry::State::Rendering) {
-            std::fprintf(stderr, "cancel: job %llu ended %s (rendered=%.0fms)\n",
-                         (unsigned long long)s.id, RenderJobRegistry::stateName(s.state), s.renderedMs);
-            return s.state == RenderJobRegistry::State::Cancelled ? 0 : 1;
-        }
+void print(const EngineJobCompletion& c) {
+    std::fprintf(stderr, "job %u: %s rendered=%.0fms outputs=%zu %s\n", c.id, c.status.c_str(), c.renderedMs,
+                 c.outputs.size(), c.message.c_str());
+    for (const std::string& p : c.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
+    std::fflush(stderr);
+}
+
+// Run the jobs on a pool whose engine budget covers all of them; returns 0 iff every one completes "done" and, with
+// more than one, the pool ran them side by side.
+int runJobs(const std::vector<std::string>& specs) {
+    // An engine budget is the pool's size less one, so N renders at once want N + 1 workers.
+    OfflineJobs pool({}, static_cast<unsigned>(std::min<std::size_t>(specs.size() + 1, OfflineJobs::kMaxWorkers)));
+    std::vector<std::uint32_t> ids;
+    for (const std::string& spec : specs) {
+        const std::uint32_t id = submit(pool, spec);
+        if (id == 0) return 1;
+        ids.push_back(id);
+    }
+    const Collected got = collect(pool, ids.size());
+    int             rc = got.done.size() == ids.size() ? 0 : 1;
+    if (rc != 0)
+        std::fprintf(stderr, "render-host-test: %zu of %zu jobs never completed\n", ids.size() - got.done.size(),
+                     ids.size());
+    for (const auto& [id, c] : got.done) {
+        print(c);
+        if (c.status != "done") rc = 1;
+    }
+    const std::size_t want = std::min<std::size_t>(ids.size(), pool.engineBudget());
+    if (rc == 0 && got.mostRunning < want) {
+        std::fprintf(stderr, "render-host-test: %zu jobs ran at once, expected %zu\n", got.mostRunning, want);
+        rc = 1;
+    } else if (rc == 0) {
+        std::fprintf(stderr, "render-host-test: up to %zu jobs ran at once\n", got.mostRunning);
     }
-    return 1;
+    return rc;
+}
+
+// Submit one (long) job, cancel it shortly after, assert it completes "cancelled" and leaves nothing behind. `out` is
+// the spec's output path, which a cancelled render must not leave.
+int runCancel(const std::string& spec, const std::string& out) {
+    OfflineJobs         pool;
+    const std::uint32_t id = submit(pool, spec);
+    if (id == 0) return 1;
+    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let it get past boot into the render loop
+    if (!pool.cancel(id)) {
+        std::fprintf(stderr, "cancel: job %u was not running (it finished before the cancel)\n", id);
+        return 1;
+    }
+    const Collected got = collect(pool, 1, std::chrono::seconds(60));
+    if (got.done.empty()) {
+        std::fprintf(stderr, "cancel: job %u never completed\n", id);
+        return 1;
+    }
+    const EngineJobCompletion& c = got.done.begin()->second;
+    std::fprintf(stderr, "cancel: job %u ended %s (rendered=%.0fms, %zu outputs)\n", c.id, c.status.c_str(),
+                 c.renderedMs, c.outputs.size());
+    if (c.status != "cancelled") return 1;
+    if (!c.outputs.empty()) return 1;
+    if (!out.empty() && std::filesystem::exists(out)) {
+        std::fprintf(stderr, "cancel: %s was left behind\n", out.c_str());
+        return 1;
+    }
+    return 0;
+}
+
+// The spec's "out" for the cancel leg's file check; a plain scan, as the spec is the caller's own JSON.
+std::string outOf(const std::string& spec) {
+    const std::string key = "\"out\":\"";
+    const std::size_t at = spec.find(key);
+    if (at == std::string::npos) return {};
+    const std::size_t from = at + key.size();
+    return spec.substr(from, spec.find('"', from) - from);
 }
 
 } // namespace
 
 int main(int argc, char** argv) {
-    if (argc >= 3 && std::strcmp(argv[1], "--registry") == 0) {
+    if (argc >= 3 && std::strcmp(argv[1], "--parallel") == 0) {
         std::vector<std::string> jobs;
         for (int i = 2; i < argc; ++i) jobs.emplace_back(argv[i]);
-        return runRegistry(jobs);
+        return runJobs(jobs);
     }
     if (argc >= 3 && std::strcmp(argv[1], "--cancel") == 0) {
-        return runCancel(argv[2]);
+        return runCancel(argv[2], outOf(argv[2]));
     }
     if (argc < 2) {
-        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json> | --registry <j>... | --cancel <j>\n");
+        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json> | --parallel <j>... | --cancel <j>\n");
         return 2;
     }
-    return runOne(argv[1]);
+    return runJobs({argv[1]});
 }
````

Run: `sed -n '8251,8482p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0.

Run: `cmake --build build --target retroplug-cli -j$(nproc)`
Expected: exit 0.

Run: `mkdir -p /tmp/d3-rht; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/one.wav","durationMs":3000}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/one.json; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/ch","durationMs":3000,"split":"channels"}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/ch.json`
Expected: exit 0: two job specs for 3 s of telemelt (a fixture) with its battery, a mix and its four stems. Telemelt's working song (WORMHOLE) sounds: the prototype's stage 3 run of this song (the ROM and its battery copied side by side) through the same two paths (`/tmp/d3pro/s3work/host-mix` and `host-channels_*`) has 261520 of its 264600 mix samples nonzero, and pulse2, wave and noise sounding (pulse1 rests for these 3 s), so a byte that moves shows in the `cmp` below.

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/one.json)"`
Expected: exit 0, with `job 1: done rendered=3000ms outputs=1` and `render-host-test: up to 1 jobs ran at once` (the form `s4f-one.log` logged for this leg on a silent ROM: `job 1: done rendered=3000ms outputs=1`, `render-host-test: up to 1 jobs ran at once`).

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/ch.json)"`
Expected: exit 0, with `job 1: done rendered=3000ms outputs=4`.

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --out /tmp/d3-rht/cli.wav`
Expected: exit 0, its last line `rendered <rom> -> <out> (@44100Hz)` (printed with an arrow character).

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --split channels --out /tmp/d3-rht/cli-ch`
Expected: exit 0, its four `cli-ch_<stem>.wav` paths printed.

Run: `cmp /tmp/d3-rht/one.wav /tmp/d3-rht/cli.wav; for c in pulse1 pulse2 wave noise; do cmp /tmp/d3-rht/ch_$c.wav /tmp/d3-rht/cli-ch_$c.wav; done`
Expected: nothing: the mix and each of the four stems are byte-identical. The tool's single leg is byte-identical to `retroplug-cli render` (the report: "single leg exit 0 and `cmp`-identical to `retroplug-cli render`"), now because both are the same engine job.

Run: `for n in r1 r2 r3; do printf '{"rom":"%s","out":"/tmp/d3-rht/%s.wav","durationMs":3000}' /workspaces/resources/retro/lsdjtest.gb $n > /tmp/d3-rht/$n.json; done`
Expected: exit 0: three 3 s job specs of the LSDj test ROM, which renders silence. These legs need no sound: they check that jobs overlap and that a cancel ends one, not its bytes.

Run: `printf '{"rom":"%s","out":"/tmp/d3-rht/cx.wav","durationMs":600000}' /workspaces/resources/retro/lsdjtest.gb > /tmp/d3-rht/cx.json`
Expected: exit 0: a job with a 600 s cap, to cancel (silent too, for the same reason).

Run: `build/bin/retroplug-render-host-test --parallel "$(cat /tmp/d3-rht/r1.json)" "$(cat /tmp/d3-rht/r2.json)" "$(cat /tmp/d3-rht/r3.json)"`
Expected: exit 0, three `done rendered=3000ms outputs=1` lines, then `render-host-test: up to 3 jobs ran at once`.

Run: `build/bin/retroplug-render-host-test --cancel "$(cat /tmp/d3-rht/cx.json)"`
Expected: exit 0, with `cancel: job 1 ended cancelled (rendered=<N>ms, 0 outputs)`: the rendered length depends on how far it got in 300 ms (the prototype's runs logged 4300 ms and 0 ms).

Run: `test -e /tmp/d3-rht/cx.wav; echo $?`
Expected: `1`: the cancelled render left no WAV.

- [ ] **Step 4: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0.

Run: `pnpm typecheck`
Expected: exit 0.

Run: `pnpm test`
Expected: `# 172 test file(s) passed`.

Run: `pnpm test:plugin`
Expected: all 10 exit 0, `retroplug-engine-graph-test` `test cases: 304 | 293 passed | 11 skipped`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1711910 assertions in 304 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed`; only dsp-bench and nes-bench skip.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: 167 passed, the skip check passing; the render files skip only what the skip baseline lists.

Run: `pnpm test:ui render-menu`
Expected: 1 passed; `render-badge` 1 passed; `render-job` 1 passed; `pnpm test render` 5 files passed.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: 9 passed, 0 skipped, 0 failed.

Run: `pnpm test:node`
Expected: 12 pass, 0 fail (the report's `test:node 12/0`).

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm test:n8`
Expected: 529 / 57 and 7 / 1.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK`.

Run: `git grep -nE "RenderJobRegistry|__rp_startRender|__rp_getRenderJobs|__rp_cancelRender|__rp_dismissRender|g_renderJobs" -- packages tools CMakeLists.txt cmake`
Expected: nothing, exit 1: no code or tool names a retired symbol (the docs are Task 5's).

- [ ] **Step 5: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the TSan tree re-runs CMake for the removed source.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` first (`[n8]` is not an engine-graph tag, so dropping it changes no case), then `All tests passed (1697892 assertions in 128 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no ThreadSanitizer report. That run is the stage's first pass; its fix pass changed no code these cases run.

Run: `mkdir -p /tmp/d3-rht; for n in p1 p2 p4; do printf '{"rom":"%s","out":"/tmp/d3-rht/%s.wav","durationMs":1500}' /workspaces/resources/retro/lsdjtest.gb $n > /tmp/d3-rht/$n.json; done`
Expected: exit 0: three 1.5 s job specs for the sanitizer legs (`cx.json` from Step 3 is the cancel's). The LSDj test ROM renders silence, which these legs do not need: they look for a sanitizer report, not bytes.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/p4.json)"`
Expected: exit 0, with `job 1: done rendered=1500ms outputs=1`, no report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-render-host-test --parallel "$(cat /tmp/d3-rht/p1.json)" "$(cat /tmp/d3-rht/p2.json)"`
Expected: exit 0, ending `render-host-test: up to 2 jobs ran at once`, no report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-render-host-test --cancel "$(cat /tmp/d3-rht/cx.json)"`
Expected: exit 0, `ended cancelled` with 0 outputs, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-render-host-test -j$(nproc)`
Expected: exit 0.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680041 assertions in 128 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/p4.json)"`
Expected: exit 0, `job 1: done rendered=1500ms outputs=1`, no report. Then run the `--parallel` leg (`up to 2 jobs ran at once`) and the `--cancel` leg (`ended cancelled`, 0 outputs) the same way with `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1"`: `all three also exit 0 under build-tsan and build-asan (detect_leaks=1) with no sanitizer output`. Then `rm -rf /tmp/d3-rht`.

- [ ] **Step 6: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/native/CMakeLists.txt
 M packages/native/plugin/PluginUI.cpp
 M packages/native/sdl/main.cpp
 M packages/native/src/host/engine/OfflineJobs.cpp
 M packages/native/src/host/render/RenderHost.cpp
 M packages/native/src/host/render/RenderHost.hpp
D  packages/native/src/host/render/RenderJobRegistry.cpp
D  packages/native/src/host/render/RenderJobRegistry.hpp
 M packages/native/src/system/mesen/MesenGlobalInit.hpp
 M packages/native/test/engine-graph/EngineJobs.test.cpp
 M packages/native/test/render-host-main.cpp
 M packages/retroplug/src/render/render.ts
 M packages/retroplug/src/render/types.ts
 M packages/retroplug/src/render/worker.ts
 M packages/retroplug/test-ui/midi-clock.test.ts
 M packages/retroplug/ui/host/nativeHooks.ts
````

````bash
git add -- packages/native/CMakeLists.txt packages/native/plugin/PluginUI.cpp packages/native/sdl/main.cpp \
  packages/native/src/host/engine/OfflineJobs.cpp packages/native/src/host/render/RenderHost.cpp \
  packages/native/src/host/render/RenderHost.hpp packages/native/src/system/mesen/MesenGlobalInit.hpp \
  packages/native/test/engine-graph/EngineJobs.test.cpp packages/native/test/render-host-main.cpp \
  packages/retroplug/src/render/render.ts packages/retroplug/src/render/types.ts \
  packages/retroplug/src/render/worker.ts packages/retroplug/test-ui/midi-clock.test.ts \
  packages/retroplug/ui/host/nativeHooks.ts
git commit -F - <<'EOF'
refactor(render): RenderJobRegistry and the render hooks retire

Every render now runs as an engine job, so what the UI's per-job threads needed goes: RenderJobRegistry (and its CMake
entry), the process-global registry in plugin/PluginUI.cpp with its __rp_startRender, __rp_cancelRender,
__rp_dismissRenderJob and __rp_getRenderJobs hooks, and their declarations in ui/host/nativeHooks.ts.

RenderHost runs only as an engine job's body, which is continuous blocks, so it loses its Blocks parameter and always
sets them. Nothing runs the render library on an exact-blocks engine with a split, so RenderContext.blocks and the
settle gate that read it go too: the settle always runs in the shape the render streams in. The in-process render
tests keep calling runRenderJob as library tests.

retroplug-render-host-test drives engine jobs on an OfflineJobs pool: the single-job leg, --parallel (N jobs on a pool
sized so all N run at once, asserted to have overlapped) and --cancel (one job cancelled, asserted to complete
cancelled with no outputs and no file left). Comments that named the deleted symbols describe the engine jobs, and the
NES "no HFF stop" warning loses its emdash.
EOF
````

---

### Task 5: the docs say what D3 built

Design section 7's fifth commit and section 10. The design's status line says built; sections 1 to 8 are corrected
where the build made them false (the chunking narrowed to transport- and MIDI-driven renders, acts between calls,
auto-detect quantised to the poll, the carry's key, the budget with a pool of one, the tool's legs); and section 10
records the build: the engine job's wire, threads, console, cancel and progress, the process-wide state and its three
fixes, continuous blocks and the settle shape, the honest scope of chunk independence, the 13-case baseline and its
rules, section 9's questions answered, the switch-over and retirement, what building it found and what D3 leaves. D2's
"Left for D3" says what D3 closed and how, and the main design's section 5.7 and Phase D say D3 is built. The specs,
`AGENTS.md` and the README follow, and the older documents that named `RenderJobRegistry` carry a one-line note.

**Files:**

- Modify: `AGENTS.md`, `README.md`, `docs/audit-2026-09-20.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-d2-design.md`, `docs/modular-graph-phase-d3-design.md`, `docs/sms-support.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/09-cli-debugging.md`, `spec/10-multichannel-audio-out.md`, `spec/11-ui-rendering.md`, `spec/README.md`

- [ ] **Step 1: The design's record**

The D3 design gets its status line, sections 1 to 8 corrected in place and section 10 "As built"; D2's design says in
its header and section 8 what D3 closed and how, keeping its history; the main design's section 5.7, its Phase D list
and its Phase D note say D3 is built.

````diff
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index f4f2aee1d..550f23785 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -841,8 +841,10 @@ filter -> dither -> `buffer-recorder`, run as fast as possible, with the same DS
 uses. This is how one sample patcher serves LSDj, risa and BlipToaster: the generic chain plus a
 per-cart profile (sample rate, bit depth, slot layout, codec) contributed by each cart's package. D2 built
 the seam (a document run to completion on an offline host of its own) and
-`packages/retroplug/src/kit/`, which builds each pass's document and packs the bank; D3 moves the CLI's and
-the UI's renders onto it.
+`packages/retroplug/src/kit/`, which builds each pass's document and packs the bank. D3 moved the CLI's and the UI's
+renders onto the same pool as a second kind of job, the engine job: a bare QuickJS runtime and an `Engine` of their
+own on a pool worker running the render library, not a graph document (a render is interactive, which a fixed document
+is not; [modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md)).
 
 ### 5.8 The Pico runtime
 
@@ -1239,7 +1241,7 @@ leaves (its section 11): P7's remainder, and a cart's battery reachable only at
 - Library nodes: gain, biquad, dither, resample (porting `lsdj/Effects` and the r8brain path).
 - Kit compiles as offline graphs; proof is **byte-identical kit output** against the native compiler they replaced
   (`KitCompiler`, retired with its RPCs in D2; the goldens it seeded are still the oracle).
-- CLI `render` and background renders on the offline executor.
+- CLI `render` and background renders on the offline pool (as engine jobs; D3).
 
 D1 is built ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), six commits): the kit path's DSP is a
 core-free library of block-streaming kernels (`src/dsp/`, `retroplug-dsp-kernels` + `retroplug-dsp-resample`), the kit
@@ -1248,8 +1250,11 @@ seeded from the sample it receives, so a kit compiles to the same bytes, and the
 banks. D2 is built too ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md), seven commits): the
 offline job seam, `rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm`, and `packages/retroplug/src/kit/`, whose
 TypeScript compiler builds each pass as a document and packs the bank, held byte for byte to those 39 banks; native's
-compiler retired with its RPCs, and every host, the plugin included, compiles kits. D3 (the CLI `render` and the
-background renders on that driver) is left.
+compiler retired with its RPCs, and every host, the plugin included, compiles kits. D3 is built too
+([modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md), five commits): the CLI's `render` and the UI's
+background renders are engine jobs on the same pool, `RenderJobRegistry` and the `__rp_*Render` hooks are gone, an
+engine job's engine runs continuous 1024-frame blocks, and a render baseline records the bytes (a render's bits moved
+once, deliberately).
 
 ### Phase E: the Pico runtime
 
diff --git a/docs/modular-graph-phase-d2-design.md b/docs/modular-graph-phase-d2-design.md
index daa06986a..feb908639 100644
--- a/docs/modular-graph-phase-d2-design.md
+++ b/docs/modular-graph-phase-d2-design.md
@@ -7,9 +7,9 @@ D1 ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md)) made k
 a byte baseline, moved the kit DSP into core-free kernels and put those kernels in the graph as `rp.dsp` nodes. D2 runs
 the kit compile itself as graphs: a generic offline job seam runs a graph document to completion on its own host and
 returns the buffers it recorded, TypeScript builds the kit chain's documents and packs the bank, and native
-`KitCompiler` retires. Every kit that compiles stays byte-identical to D1's baseline. The same seam later serves D3's
-renders and user-built sample graphs, and because it runs in every host, the plugin gains kit compiles. Phase D's
-outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).
+`KitCompiler` retires. Every kit that compiles stays byte-identical to D1's baseline. The same pool later serves D3's
+renders (as a second kind of job) and user-built sample graphs, and because it runs in every host, the plugin gains
+kit compiles. Phase D's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).
 
 ## 1. Before D2
 
@@ -182,6 +182,19 @@ implementers and follow-ups, Opus code reviews of tasks 2, 4 and 6, a DeepSeek a
 
 ## 8. Left for D3
 
+D3 built this ([modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md), section 10). It did not extend the
+graph job: renders became a second kind of job on the same pool, the engine job, which builds a bare QuickJS runtime
+and an `Engine` of its own on a worker, so the list below is closed as follows. The pool's 1024-frame block was
+accepted (an engine job's engine runs continuous blocks, so the render library's chunking no longer reaches the bytes,
+and a render baseline records them once). System nodes, a script host, a playing transport and per-block MIDI are the
+job's own `Engine`'s, not the graph job's registry, which is unchanged. TypeScript cancel exists for both kinds
+(`engineRun` and `offlineRun` return handles with `cancel()`). A render's completion streams in the sense that matters:
+the library writes its WAV per chunk on the worker, and the drain reports progress and console lines meanwhile.
+Rejection at project close was not added, since a render boots from files on disk. `host.pump()` yields through
+`setOfflineYield` in the CLI's render. A graph job's fixed transport and its lack of MIDI input remain, and MIDI-driven
+renders are D3's own left item. The graph job's guard reports (`guardsFired`, `guardReason`) never reach a render,
+which runs no graph job. The text below is what D2 knew when it was written.
+
 The CLI `render` and the UI's background renders on the offline jobs, keeping their block-size sequence and per-block
 side effects or re-scoping their byte-identity claim. What the seam hands them, and what D2's build settled about it:
 
@@ -445,7 +458,7 @@ neither, so what it makes is recorded; and the quantise and DMC encode only ever
 reporting any restart. So a node departs from its kernel only where a guard reported it, and every other non-finite
 value is flagged. The compiler refuses a slot on either (guard count above 0, or a flagged recording), so a compiled
 bank is the kernels' bytes. D3 inherits the reports rather than a policy: a render sees the count and the flags and
-decides, and nothing fails on them.
+decides, and nothing fails on them (D3's renders run no graph job, so the reports never reach one).
 
 ### What building it changed in sections 3 to 6
 
diff --git a/docs/modular-graph-phase-d3-design.md b/docs/modular-graph-phase-d3-design.md
index c4bcf255a..b6186975c 100644
--- a/docs/modular-graph-phase-d3-design.md
+++ b/docs/modular-graph-phase-d3-design.md
@@ -1,14 +1,15 @@
 # Phase D3: renders as engine jobs
 
-**Date:** 2026-11-02 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9
+**Date:** 2026-11-02 · **Branch:** `feature/modularize` · **Status:** built (D3, five commits); section 10 records the build,
+answers section 9 and carries what was left, and sections 1 to 8 are corrected in place
 
 D2 ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md)) built the offline job seam: a native pool that
 runs a graph document to completion on a host of its own, with a TypeScript promise side, and moved the kit compile onto
 it. D3 moves the renders onto the same seam. The CLI's `render` and the UI's background renders become a second kind of
 job, the engine job: a bare QuickJS runtime and an `Engine` of their own on a pool worker, running the unchanged render
 library. One pool, one RPC surface, one pump and one cancel serve kit compiles and renders alike, and the CLI and the
-UI render through literally the same code. A render's bytes stop depending on how the render library chunks its pulls.
-Phase D's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).
+UI render through literally the same code. The sample stream inside a render stops depending on how the render library
+chunks its pulls. Phase D's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).
 
 ## 1. Before D3
 
@@ -25,11 +26,13 @@ Phase D's outline is the main design's section 8 ([modular-graph-design.md](modu
 - **D2's graph jobs lack what a render needs** (D2 design section 8): their registry is `rp.core`, `rp.dsp` and
   `rp.dsp.resample` only, with no system nodes, ROM or battery resources or script packs; their transport is fixed;
   they take no events; they return buffers only at the end; TypeScript cannot cancel one.
-- **Render bytes depend on the chunking.** The library pulls audio in 100 ms chunks and `renderAudio` runs each as
-  1024-frame blocks plus a short remainder (1024, 1024, 1024, 1024, 314 at 44.1 kHz). The transport advance, the
-  protocol script nodes, the snapshot publishes and the MIDI-out gathering happen per block, so the short block at each
-  chunk end is in the bits. No golden records render bytes; `retroplug-render-host-test` holds the UI worker byte for
-  byte to `retroplug-cli render`, and the render tests check levels and detection.
+- **A render's block grid followed the chunking.** The library pulls audio in 100 ms chunks and `renderAudio` runs each
+  as 1024-frame blocks plus a short remainder (1024, 1024, 1024, 1024, 314 at 44.1 kHz). The transport advance, the
+  protocol script nodes, the snapshot publishes and the MIDI-out gathering happen per block, so a node that consumes
+  the transport or MIDI each block (the host sync of smsggdj, `rp.mgb.midi`) put the short block at each chunk end in
+  the bits. A render with no such node never depended on it: the cores emulate the same whatever the block lengths.
+  No golden recorded render bytes; `retroplug-render-host-test` held the UI worker byte for byte to `retroplug-cli
+  render`, and the render tests check levels and detection.
 
 ## 2. Decisions taken
 
@@ -39,10 +42,11 @@ The user's, on 2026-11-02:
   RPCs, pool and TypeScript promise side as the kit's graph jobs; `RenderJobRegistry` and the CLI's in-process render
   path retire. Not graph-level render jobs (system nodes, events and streaming added to D2's graph jobs), not speed
   and streaming only, and not closing D3 as already met.
-- **Chunk-independent bytes, recorded after the move.** A render job runs continuous 1024-frame blocks whatever chunk
-  the library pulls, so a render's bytes no longer depend on the chunk size; every render's bits move once, and a new
-  render baseline is recorded after the move. Not identical to today, not a staged proof first, and not cross-path
-  equality only.
+- **Chunk-independent samples, recorded after the move.** A render job runs continuous 1024-frame blocks whatever chunk
+  the library pulls, so the sample stream no longer depends on the chunk size (section 4 gives the scope); the bits of
+  the renders that act between calls or run such a node move once, and a new render baseline is recorded after the
+  move. Not identical to
+  today, not a staged proof first, and not cross-path equality only.
 - **The job hosts its own runtime.** An engine job carries a QuickJS runtime and an `Engine`, as `RenderHost` does, and
   runs the unchanged synchronous render library on its worker. Not TypeScript driving a remote engine session from the
   caller's runtime (it would put render work on the plugin's UI thread), and not an autonomous native recipe (every
@@ -68,32 +72,50 @@ environment (as graph jobs do) and tears the host down on that thread. Cancel is
 polled per chunk; an engine's teardown cancels its engine jobs and joins them.
 
 **Sharing the pool.** A render is a full emulator running flat out for seconds or minutes; a kit compile takes
-milliseconds. Engine jobs hold at most all but one of the pool's workers at once, the rest queue, so a kit compile
-always finds a worker. Both kinds count toward the one bound on outstanding jobs.
+milliseconds. Engine jobs hold at most all but one of the pool's workers at once (one, for a pool of one), the rest
+queue first in, first out, so a kit compile always finds a worker: a pool of one starts a second thread for a graph job
+that arrives while a render holds the first. Both kinds count toward the one bound on outstanding jobs.
 
 **Ownership.** A render belongs to its plugin instance's `Engine`, not to the process: it survives the editor closing,
 as today, and an instance's teardown cancels it, where `RenderJobRegistry`'s threads ran on into static teardown.
 
 ## 4. Continuous blocks
 
-Inside an engine job the engine's `renderAudio` and `renderAudioPerChannel` always run whole 1024-frame blocks: a call
-returns the frames asked for and keeps the surplus for the next call, and the surplus is dropped when the job ends.
+Inside an engine job the engine's `renderAudio`, `renderAudioPerSystem` and `renderAudioPerChannel` always run whole
+1024-frame blocks: a call returns the frames asked for and keeps the surplus for the next call, and the surplus is
+dropped when the job ends. The carry is keyed on the call's shape (mix, per system or per channel), the per-channel
+system id, the stream count and the rate; a call of another key drops it, as does `renderBlocks`.
 Between the library's chunks the emulator has therefore run up to 1023 frames past the audio handed back, so a button
 press lands at the next block boundary after that point, and an end-of-song read sees state up to 1023 frames later
 than the audio returned. Both are deterministic and are in the new baseline; detection precision stays the 100 ms
-chunk. Outside engine jobs `renderAudio` keeps its exact splits, so parity, which drives it with its own, is untouched.
+chunk.
+
+What this makes independent of the chunking, and what it does not:
+
+- The sample stream of an engine job no longer depends on how the library chunks its pulls. Only a render with a node
+  that
+  consumes the transport or MIDI each block (smsggdj's host sync, `rp.mgb.midi`) depended on it; a running transport
+  with no such node did not, and the others, tracker renders included, were already independent in exact blocks.
+- Acts the library takes between calls (button presses, smsggdj's work-RAM load, the detection reads) land at the next
+  block boundary after the call before them. They are deterministic but tied to the lengths of those calls, which a
+  fixed-length render times with calls of its own lengths, so every fixed-length render is pull-independent end to end.
+- An auto-detect render polls once per pull, so its samples agree at any pull but its length is quantised to the poll.
+
+Outside engine jobs `renderAudio` keeps its exact splits, so parity, which drives it with its own, is untouched.
 
 ## 5. The TypeScript side and the callers
 
-**The promise side.** `backend.engineRun("render", spec, { onProgress })` is a promise of `{ status, message,
-outputs }`, settled through D2's shared pump: the native wait in a host with no UI tick (the CLI, the Node addon, the
+**The promise side.** `backend.engineRun("render", spec, { onLog, onProgress })` is a promise of `{ status,
+message, outputs, renderedMs }`, settled through D2's shared pump: the native wait in a host with no UI tick (the CLI,
+the Node addon, the
 test hosts), the UI frame tick in the plugin and the SDL standalone. Its handle has `cancel()`, and `offlineRun`'s
 gains the same (D2 left TypeScript cancel out). A job's console lines arrive with its progress, and the CLI prints
 them as they come, as it does today.
 
 **The CLI.** `retroplug-cli render` parses its arguments into the same spec, submits an engine job to its own engine
 and awaits it, instead of running `runRenderJob` over its session. `--list-songs`, which is not a render, stays
-in-process. The command's output and exit codes stay what they are.
+in-process. The command's output and exit codes stay what they are, but for Ctrl-C and the cores' stdout
+interleaving (section 10).
 
 **The UI.** System > Render submits the job through the stores' backend, not `audioDriver`, tagged with the system it
 came from; the tile badge reads a small list of running render jobs kept from the pump's progress, replacing the
@@ -112,13 +134,15 @@ disk and depends on nothing in the project, as today; graph jobs behave the same
   region, smsggdj. Recorded once after the move (`RP_RENDER_GOLDEN_UPDATE=1`), refusing silence as parity's recording
   does; exact bytes compared only on `linux-x64-gnu13` with a require flag for CI's linux-x64 job; the fixture cases
   (LSDj, risa) count only on a run with fixtures and `RP_FAIL_ON_SKIP=1`, and CI runs the fixture-free subset.
-- **Chunk independence:** one render pulled at three different chunk sizes gives identical bytes.
+- **Chunk independence:** one render pulled at three different chunk sizes gives identical bytes, for the renders the
+  scope in section 4 covers; an auto-detect render's samples agree and its length is held to within a pull.
 - **The seam:** submit, progress and console lines, cancel from TypeScript for both kinds, an engine torn down with
   renders in flight, a kit job getting a worker while renders hold the rest, under TSan and ASan.
 - **Moved:** the UI's `render-menu` and `render-badge` tests onto the job path; `retroplug-render-host-test` reworked to
-  drive engine jobs, keeping its parallel and cancel legs.
+  drive engine jobs, keeping a parallel and a cancel leg.
 - **Unchanged:** the existing render tests (`audio-render`, the risa and sms renders, `render-guard`), parity bit for
-  bit, the kit goldens, the CLI's render output and exit codes (`test:cli-ts`), both consumer suites.
+  bit, the kit goldens, the CLI's render output and exit codes (`test:cli-ts`; Ctrl-C and the stdout
+  interleaving aside, section 10), both consumer suites.
 
 ## 7. Landing
 
@@ -147,6 +171,7 @@ Opus final review.
 - The kit-patcher latency follow-up (spec/07): chaining a kit slot's passes inside one job so only the final buffer
   crosses into TypeScript.
 - MIDI-driven renders (an mGB or BlipToaster render fed a MIDI file), which the render library does not do today.
+  Section 10 lists the rest D3 left.
 
 ## 9. Prototype questions
 
@@ -158,3 +183,179 @@ Opus final review.
 - How far do the HFF end frames move against today on the fixture songs, and does any detection change outcome?
 - What happens when a plugin instance is torn down mid-render, and when an editor reopens on renders that finished
   while it was closed?
+
+## 10. As built
+
+Five commits on `feature/modularize`: engine jobs in the pool, continuous blocks and the render baseline, the
+switch-over, retirement, and these docs. Parity bit for bit, the kit goldens and both consumer suites were untouched
+throughout; the render baseline is the only golden D3 records. Sections 1 to 8 are corrected in place where the build
+made them false (chunking, the worker budget, the carry's rules).
+
+### Engine jobs in the pool (commit 1)
+
+- **Wire.** `submitEngineJob({kind, spec})` joins the emulator facet (19 methods, 91 in the union); the spec crosses as
+  a JSON string; the answer is D2's `RpcOfflineSubmitResult`. `drainOfflineJobs` grows `engineCompletions` and
+  `engineProgress` beside the graph `completions` and `inFlight`. `kind` must be exactly `"render"`; anything else is a
+  refusal naming it, and a bad spec is the job's own error (`render worker: job spec missing 'rom'`), not a refusal.
+- **The worker budget** (design question 3). `engineBudget()` is all but one of the pool's size (one for a pool of one);
+  graph jobs run on at most `size` and the pool's threads on at most `maxWorkers()`, which is `size` or 2 for a pool of
+  one. A worker takes the oldest job its kind's limit allows, so renders wait first in, first out behind renders and a
+  graph job starts past queued renders; a worker is started only when the runnable queued jobs outnumber the workers not
+  running one, so a render held back by the budget starts no thread. No further fairness rule was needed. The pool's
+  size is a constructor argument (0 means `concurrency()`) so tests size it.
+- **A job's lifetime is one worker's stack frame.** The worker pins the default floating-point environment, builds a
+  `RenderHost` (a bare QuickJS runtime), whose `run()` builds the job's own `Engine`, `QueuedInvoker`, four RPC services
+  and in-process server, evaluates the embedded render bundle synchronously and returns; everything dies on the worker
+  before it takes the pool's lock to publish. Teardown sets `stopping_`, drops queued jobs, cancels running ones and
+  joins; a running render sees the cancel at its next chunk, so nothing a render touches outlives the pool, which is
+  reset before any other `Engine` member. Lock order is the pool's mutex, then a record's lock; no lock is held across
+  `RenderHost::run`.
+- **Console and progress.** The job's script console routes into a per-job `EngineJobRecord`, bounded between drains
+  (256 lines, 64 KiB; past either a line is dropped and counted, and the count rides the next report, which TypeScript
+  prints as `[N console lines dropped]`). A line is one console write, which can hold newlines. Native diagnostics
+  printed straight to the process (Mesen's iNES log) still go to stderr. Every running job is reported at every drain;
+  `onProgress` fires when the value moved and is a running maximum, because an auto-detect render lowers the raw count
+  once, to the length it wrote; the result's `renderedMs` is that length.
+- **Cancel wins** (D2's rule, kept for both kinds): a cancel that returned true makes the outcome `cancelled`, even if
+  the render finished first, and such a completion keeps its `outputs`. TypeScript: `engineRun` resolves with all
+  three statuses and rejects only on a refusal or a port that throws; `offlineRun`'s promise rejects with
+  `OfflineRunError("cancelled")`; a run still held on the TypeScript side settles at once on cancel; `cancel()` answers
+  false once settled. A settle that a callback re-enters returns without draining, and the lost-job check considers only
+  the jobs pending at its drain.
+- **Process-wide state a render touches** (design question 2). A QuickJS runtime and an `Engine` per job on a reused
+  pool thread is safe, after three fixes found by ThreadSanitizer and by reading the cores. Mesen's
+  `MessageManager` lock (`SimpleLock`, vendored in `deps/mesen`, not a submodule) read its holder and count as plain
+  integers while another thread built a core; they are relaxed atomics now. SameBoy's power-on `GB_random` seed, the
+  camera's noise seed and the SGB's lazily built `fm_sin` table are process statics a second Game Boy core raced on:
+  `cmake/patches/sameboy-process-statics.patch` (the second tracked SameBoy patch, applied by `cmake/sameboy.cmake`
+  after the per-channel one, with the same idempotent and fail-loudly rules) makes the first two relaxed atomics and the
+  third a load-time constructor, as `apu.c`'s own tables are. Nothing else is shared: QuickJS keeps no per-thread
+  state, Mesen's scratch paths are numbered by atomic counters, `SnapshotRegistry` is per `Engine`, and r8brain's
+  statics outlive every pool. The pool header lists this explicitly.
+- **Teardown mid-render and editor reopen** (design question 5). An engine's destruction cancels at the next chunk and
+  joins; completions not drained go with the pool; a render cancelled mid-render has its WAV removed (commit 3). With
+  the editor
+  closed, completions wait in native's queue (counting toward the 64) and the record bound caps the lines; the
+  control-plane runtime's queue still holds the promise, so the next pump after the editor reopens settles it.
+
+### Continuous blocks and the render baseline (commit 2)
+
+- **Where they live.** `EngineRpcService::setContinuousBlocks(bool)` is C++ only, not an RPC. All three render calls
+  share one private `pull` loop; the carry (`{shape, id, rate, frames, streams}`) is dropped by `renderBlocks` and by a
+  call of another key. The emulation behind dropped frames is not undone: the next audio starts at the next block.
+  Exact mode runs the old splits, so parity is untouched. `RenderHost` always sets continuous (commit 4); the exact path
+  stays for the in-process library tests.
+- **The settle shape.** The library settles 1500 ms with a mix call and then streams a split render per channel. Under
+  the drop rule that would start a split render's stems up to a block after its own mix, so a split render settles in
+  the stream's shape. Making that unconditional moved the CLI's NES stems byte for byte, which is why commit 2 gated it
+  on the engine and commit 4 removed the gate once only engine jobs remained. A NES's settle shape moves its stems:
+  mix against per-channel changes every later sample and diverges triangle and dmc seconds in, so the per-channel shape
+  is emulation timing for a NES, not only routing. A Game Boy is unaffected. Pinned by a case that sums an LSDj render's
+  stems against its mix at lag 0 (red with the old settle, lag -410).
+- **The chunk knob.** `RenderOpts.pullMs` (default 100, a test knob the CLI never sets) changes only the stream's pulls;
+  the play gestures (100 ms), smsggdj's load steps (250 ms) and the settle (1500 ms) keep their own lengths.
+- **The honest scope of chunk independence** (the narrowing of sections 1, 4 and 6). Measured with `render-chunks`
+  (5 cases) and a mutation that forces engine jobs back to exact blocks:
+  - Only a render with a node that consumes the transport or MIDI each block depended on the chunking (a running
+    transport with no such node did not): the host-synced smsggdj render
+    differs between pulls of 100, 37 and 250 in exact blocks and is identical in continuous ones; under the mutation
+    exactly that case fails.
+  - Everything else was already independent: the smsggdj fixed render, LSDj stems with Start pressed and risa's pins
+    with three gestures were identical at all three pulls even in exact blocks.
+  - Acts between calls land at the next block boundary after the call before them, so they follow that call's length by
+    construction; they are independent of `pullMs` only because their calls have lengths of their own.
+  - An auto-detect render (LSDj and risa by default) polls once per pull. MACHETE at pulls of 100, 50 and 200 has
+    byte-identical samples over the common length and ends at 191700, 191750 and 191700 ms. The test asserts the samples
+    agree and the lengths differ by less than the longest pull. Making detection pull-independent would need the
+    block-granular probes of the autonomous-recipe direction (section 8).
+- **The render baseline** (`packages/retroplug/test-native/golden/render-baseline.json`, `render-baseline.test.ts`,
+  the cases in `render-cases.ts`). 13 cases, each rendered through `engineRun` and hashed with the kit baseline's
+  FNV-1a-64 over the whole WAV (header included): 5 fixture-free, 4 LSDj, 4 risa.
+  - Fixture-free: smsggdj playing songs authored into its battery (METRO, HIGH, CHORD) as `smsggdj/sms/index-0/mix`,
+    `smsggdj/sms/name-chord/mix-48000`, `smsggdj/gg/index-1/mix` and the host-synced
+    `smsggdj/sms/sync-in24/transport-120bpm`, plus
+    `sameboy/mgb-sgb/no-start/mix` (mGB as a Super Game Boy, whose boot jingle sounds with no MIDI).
+  - LSDj fixtures: tripledipper fixed 5 s (mix and 4 stems) and telemelt's MACHETE auto-detected by name (mix) and by
+    index (4 stems). Risa fixtures (risa-2.2.1-pal.nes): ecoli_soul HFF on PAL (59000 ms) and on NTSC through a carried
+    region role (49800 ms), and blumarbl fixed 3 s as channels and pins. There is no GBA case: the repo's GBA ROM
+    renders exact silence without a BIOS.
+  - Rules: length and sizes are compared everywhere; hashes only on `linux-x64-gnu13`, elsewhere a case renders twice
+    and must agree with itself; `RP_RENDER_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail (CI's linux-x64
+    `test:native` step sets it); `RP_RENDER_GOLDEN_UPDATE=1` is refused off the target, for a self-disagreeing case and
+    for a run missing any case; every case and file must sound, except the files a case lists as silent (blumarbl's
+    first 3 s leave square1, triangle and dmc silent). The test-native runner sets `RETROPLUG_DETERMINISTIC=1` for
+    `render-baseline`, `render-chunks` and `render-cli` (`DETERMINISTIC_FILES`), as it does for `parity/`; the variable
+    is process-wide, so it reaches every engine job's own `Engine`.
+- **Question 1: which fixture-free renders make sound.** Two sources only. smsggdj with an authored song (4 cases, both
+  consoles, a name and an index pick, two rates, the host-synced path through `rp.sms.sync`) and mGB as a Super Game Boy
+  (the BIOS boot jingle, mixed after the APU, so its stems are silent). mGB and all six BlipToaster carts are silent
+  because a render sends no MIDI, and smsggdj boots blank. So CI's share is 5 sounding cases, 4 on Mesen's SMS/GG core
+  and the SGB jingle on SameBoy's mix. CI has no coverage of a NES, of a split render, or of a Game Boy's per-channel
+  stems; the C++ `[engine-jobs]` cases cover the last through `rp.mgb.midi`. MIDI-driven renders (section 8) are the way
+  to close it.
+- **Question 4: how far the HFF end frames move.** They do not. Ten songs, deterministic cores both sides, the old side
+  being `retroplug-cli render` (the library in-process for ecoli NTSC): telemelt slots 0 to 4 (324400, 304100, 204400,
+  191700 and 259400 ms), tripledipper (262500), risa ecoli_soul (59000), hevander (91100), let_go (59000) and ecoli
+  NTSC (49800) all end on the same frame, and no detection changed outcome. The bytes do move: LSDj differs sparsely
+  from about frame 9 to 22 thousand on, by song (MACHETE 11555 of 16.9 million samples, at most 505 LSB; SEMIYOYO 36
+  percent), and
+  risa's from about frame 900, not by a shift. Cause: the presses land at the block boundary after the settle (frame
+  66560 where the old path pressed at 66150 at 44.1 kHz).
+
+### The switch-over and retirement (commits 3 and 4)
+
+- **The CLI.** `retroplug-cli render` parses its arguments, submits `engineRun("render", spec, {onLog})` and prints the
+  job's lines as they arrive (stdout lines to stdout, stderr to stderr). `--list-songs` stays in-process. Its pump is
+  `keepAlive()` plus `setOfflineYield` (the next poll step after a native wait runs as a macrotask): without the yield a
+  minute-long render exhausts the launcher's bounded loop of 20000 pumps, and the hook is not unconditional because the
+  native test host fires no timers. SIGINT is `__rp_onInterrupt` (`cli/main.cpp`), exposed as `onInterrupt` in
+  `cli/session.ts`: the first Ctrl-C cancels the job, which ends cancelled and prints `ERROR: render: cancelled`, exit 1
+  (one rule for every cancel, not 130), and restores the default handler so a second kills a wedged job. A cancelled
+  render's files are deleted (`discard()` on the library's sink, on every cancel path). Printed paths are as the user
+  typed them; messages and exit codes are otherwise as before. The cores' own stdout (Mesen's load log) goes straight to
+  fd 1 while the library's lines arrive through the drain about 100 ms later, so the two can interleave differently.
+- **The UI.** `RenderJobs` (`src/renderJobs.ts`) is kept in the store graph (`AppStores.renders`), not in a UI module:
+  in a DAW that graph is the control plane's, which outlives the editor and owns the `OfflineJobs` queue that submitted,
+  so a completion is drained by the queue that owns it and no adopt path is needed. `startSystemRender` submits through
+  `stores.backend`; the tile badge samples `renders.list()` instead of polling a hook and a tap calls `renders.cancel`;
+  finished jobs auto-dismiss, and one whose system has no tile is dropped as it settles and whenever the systems list
+  changes. The SDL standalone gains renders (it had no `__rp_startRender`) through the same facet.
+- **Retired.** `RenderJobRegistry`, the four `__rp_*Render` hooks (and their declarations), `g_renderJobs`, the CLI's
+  in-process render path, `RenderHost`'s exact-blocks mode and `RenderContext.blocks`. The library's exact-blocks engine
+  and `setContinuousBlocks(false)` stay for the in-process render tests and parity.
+- **`retroplug-render-host-test`** is three legs on engine jobs: `<job-json>` runs one job on a pool (the production
+  path, so "byte-identical to `retroplug-cli render`" holds because it is the same code, and is still checked by `cmp`),
+  `--parallel <j>...` runs N jobs on a pool sized so all can run and asserts that at least `min(N, engineBudget())`
+  ran at
+  once, and `--cancel <j>` cancels after 300 ms and asserts `cancelled`, no outputs and no file. It is a manual smoke;
+  `[engine-jobs]` carries the coverage CI runs. `--registry` is gone. There is no render leg in `run-sanitizer.sh`
+  (the script only runs the host's `dsp-threaded` and `dsp-lifecycle` slugs); the thread and address checks are
+  `[engine-jobs]` built in `build-tsan` and `build-asan`, and the tool's three legs run by hand in both.
+
+### Found while building
+
+- **The tests that bite.** Moving the CLI alone broke "UI worker equals CLI" until the registry rendered continuous too
+  (the Start press lands one block boundary later); a mutation of engine jobs to exact blocks fails exactly the
+  host-synced case; removing the settle shape fails the stems-against-mix case and the stem hashes; removing the
+  floating-point pin fails the flush-to-zero case; a budget of `size` fails the graph-job-while-renders-hold-the-pool
+  case; a TSan build of the tool finds the SameBoy statics red and the patch makes it green.
+- **Three different risa batteries render identical bytes** on risa-2.2.1-pal (ecoli_soul, let_go and the ROM's own
+  `.sav`); hevander differs. Probably one working song; not investigated.
+- **Hashing is the long pole of the baseline.** FNV-1a-64 in QuickJS runs at about 150 ns per byte: MACHETE's four
+  stems take about 37 s of render-baseline's roughly 60 s with fixtures.
+
+### Left beyond D3
+
+- **MIDI-driven renders.** A render fed a MIDI file would give mGB (Game Boy stems) and BlipToaster (NES channels, pins
+  and expansion chips) fixture-free sound, and with it CI coverage of SameBoy's per-channel path, the NES and every
+  split render (all of which only a fixture run covers today; section 8).
+- **CI's continuous-path coverage is thin.** The render baseline's LSDj and risa cases need fixtures, so on CI only
+  smsggdj and the SGB jingle run through engine jobs against exact bytes.
+- **Detection is quantised to the poll.** Block-granular probes (or an autonomous recipe) would make an auto-detect
+  render's length pull-independent.
+- **A cancel can hang a wedged job.** A render that never settles hangs the CLI where the old bounded loop gave up; the
+  second Ctrl-C is the way out. A closed editor leaves a finished render listed as rendering until the next pump.
+- **A GBA render case** needs a BIOS to make sound.
+- **Windows and the other platforms** have not built this tree; the new cross-platform pieces are the SameBoy patch (GNU
+  `__atomic` builtins, so GCC and Clang only, which is all that builds the core), `SimpleLock`'s atomics and the
+  CLI's `__rp_onInterrupt` signal handling.
````

Run: `sed -n '8685,9094p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 2: The specs**

`spec/11-ui-rendering.md` is rewritten for engine jobs: the shape, the library, `RenderHost`, the pool, the UI seam
without hooks, and a verification section that names the real checks (the thread-sanitizer pass it used to claim never
existed). `spec/01` and `spec/02` describe two job kinds, the RPC table with 19 emulator and 91 union methods, the
pool's rules for each kind and Mesen's `SimpleLock`; `spec/03` `engineRun`, `setOfflineYield` and the render list;
`spec/06` the render baseline, its flags and `DETERMINISTIC_FILES` under a heading of its own, the `[engine-jobs]`
tag, both SameBoy patches and the tool's legs; `spec/07` D3 done and its residuals; `spec/09` the CLI's render as a
job and its Ctrl-C; `spec/10` the second patch; and `spec/README.md`'s row the render as an engine job.

````diff
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index 0c16d4b86..e3149820e 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -360,8 +360,8 @@ A host runs one control-plane runtime, and every `Engine` it holds has a **scrip
 with two more for each **script node pack** it holds: the built-in one, and any loaded beside it
 ([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp),
 [modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 2,
-[modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)). (A UI
-render job adds a bare QuickJS `RenderHost` with an Engine, so a script host, of its own,
+[modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)). (A render, an
+engine job on the offline pool, adds a bare QuickJS `RenderHost` with an Engine, so a script host, of its own,
 [11-ui-rendering.md](11-ui-rendering.md); the Node-API host runs its control plane in V8.) None is
 shared, and no JS value passes between any two:
 
@@ -440,11 +440,21 @@ D2 design section 11).
 
 ### The offline job pool
 
+The pool runs two kinds of job. A **graph job** runs a graph document to completion off the audio thread; an **engine
+job** (D3, [modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md)) runs a render: a worker builds
+a bare QuickJS `RenderHost` with an `Engine` of its own, runs the render library to the end in continuous 1024-frame
+blocks and destroys both on the same thread, so the CLI's `render` and the UI's background renders are one code path.
+`submitEngineJob` (kind `"render"`, a spec) shares `drainOfflineJobs` and `cancelOfflineJob`; a drain also carries
+each running engine job's progress and the console lines it wrote. Engine jobs hold at most all but one of the pool's
+workers (one for a pool of one), so a kit compile always finds a worker, and both kinds count toward the 64-job bound.
+The rest of this section is the graph job.
+
 A graph document also runs to completion off the audio thread. `submitOfflineJob` (the emulator facet) takes a
 document, its inputs (interleaved float samples, or file paths native decodes and windows), the empty buffers it
 records into and the frames to run; a later `drainOfflineJobs` hands back its completion, a bounded queue the control
 plane polls as capability jobs' completions are. An `Engine` owns the pool, made at the first submission, of up to
-clamp(hardware_concurrency, 1, 8) worker threads, with at most 64 jobs outstanding. Each job gets a `GraphHost` and a
+`maxWorkers()` worker threads (clamp(hardware_concurrency, 1, 8), or 2 for a pool of one), with at most 64 jobs
+outstanding. Each job gets a `GraphHost` and a
 resource table of its own on its worker, which is that host's control thread and block thread at once (the quiescent
 path: no audio thread, 1024-frame blocks), and is destroyed there with the host. The pool has a node registry of its
 own, filled before any worker exists with the built-in C++ packages, so a job's document may name rp.core, rp.dsp and
@@ -525,7 +535,9 @@ lost mutation.
    Cores are built bare (no default roles baked into C++); feature behaviour lives in TS, the audio
    thread's part in the protocol script nodes.
 
-9. **An offline job shares nothing with the engine.** A job's host, resource table and node instances are made and
+9. **An offline job shares nothing with the engine.** (An engine job's whole `Engine` is its own too; what parallel
+   cores share is process-wide state made safe: Mesen's setup and `SimpleLock`, SameBoy's patched statics.) A job's
+   host, resource table and node instances are made and
    destroyed on its own worker, which is the two threads that host has; the only state it shares with the engine is the
    pool's queue and completion deque, under the pool's mutex, the pool's registry, filled before any worker starts and
    never changed, and r8brain's static, internally locked filter cache (shared by every job's resampler and the live
@@ -559,7 +571,7 @@ lost mutation.
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
 | [`src/dsp/`](../packages/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
 | [`DspNodes.cpp`](../packages/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither, normalize, dpcm) and the `rp.dsp.resample` package beside it |
-| [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp) | the offline job pool: a document run to completion on a host of its own, its inputs, outputs and completion (the request and completion types are in [`OfflineTypes.hpp`](../packages/native/src/host/engine/OfflineTypes.hpp), which `Engine.hpp` includes in place of the pool) |
+| [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp) | the offline job pool: a document run to completion on a host of its own, or a render (an engine job: a `RenderHost` and `Engine` of its own), its inputs, outputs and completion (the request and completion types are in [`OfflineTypes.hpp`](../packages/native/src/host/engine/OfflineTypes.hpp), which `Engine.hpp` includes in place of the pool) |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
 | [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index c4e08d36b..991ebc9bf 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -162,18 +162,20 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 | `submitJob` | `(RpcJobRequest) → RpcJobSubmitResult` | a capability job for a node of the last published plan: `{id}` when it was taken, `{error}` when it was refused (a capability the node does not present, an operation it does not have, a device's "busy" or "not connected", the engine's 64-outstanding bound, an emulated restore). A read or a ROM header completes before it returns; a write at the next block boundary, or at once while no audio thread runs (the CLI between renders); a device's job on its own thread, after a cart submission has waited on the calling thread (the DAW's main thread in the plugin) up to the probe's 1 s bound for the cart's menu check, usually one USB round trip |
 | `drainJobs` | `→ vector<RpcJobCompletion>` | what finished since the last drain (`done`, `ok`, the result's bytes or the error) and every running job whose progress changed, with `progress` of `total` |
 | `submitOfflineJob` | `(RpcOfflineRequest) → RpcOfflineSubmitResult` | a graph document run to completion on a host of the engine's offline pool (`Engine::submitOfflineJob`): `{id}`, or `{error}` for a rate a `GraphHost` refuses, more than 64 inputs and outputs, an input that is neither samples nor a file, an output of no frames, a return naming no handle, or 64 jobs outstanding. A document may name only the built-in C++ packages. What fails once it runs (a file that will not decode, a planner refusal, an activate error, a node fault, a cancel) is the completion's error |
-| `drainOfflineJobs` | `(double waitMs) → RpcOfflineDrain` | the completions finished since the last drain, oldest first (each buffer's samples, rate, channels and `nonFinite`, the file inputs' reports, `guardsFired` / `guardReason`), plus `inFlight`, the jobs still queued or running. With `waitMs` it blocks up to that (at most 1000 ms) when nothing has finished, and returns at once when no job is queued or running |
-| `cancelOfflineJob` | `(uint32 id) → bool` | stop job `id`: a queued one completes now, unrun, a running one at its next block boundary (or as it finishes), both with the reason `cancelled`. False for an id neither queued nor running |
+| `submitEngineJob` | `(RpcEngineJobRequest) → RpcOfflineSubmitResult` | an engine job (D3): `{kind, spec}` with `kind` exactly `"render"` and `spec` the render job JSON, run on a pool worker by a `RenderHost` and an `Engine` of its own (`Engine::submitEngineJob`). `{id}`, or `{error}` for an unknown kind (naming it) or 64 jobs outstanding; a bad spec is the job's own error. Ids are shared with graph jobs |
+| `drainOfflineJobs` | `(double waitMs) → RpcOfflineDrain` | the completions finished since the last drain, oldest first (each buffer's samples, rate, channels and `nonFinite`, the file inputs' reports, `guardsFired` / `guardReason`), plus `inFlight`, the jobs still queued or running. A drain also fills `engineCompletions` (`{id, status "done"/"cancelled"/"error", message, outputs, renderedMs, lines, droppedLines}`) and `engineProgress` (every running engine job's rendered milliseconds and its console lines since the last drain, each a stream and text). With `waitMs` it blocks up to that (at most 1000 ms) when nothing has finished, and returns at once when no job is queued or running |
+| `cancelOfflineJob` | `(uint32 id) → bool` | stop job `id` of either kind: a queued one completes now, unrun, a running graph job at its next block boundary (or as it finishes), a running render at its next 100 ms chunk, both with the reason `cancelled` (a render's cancel wins over its finishing first). False for an id neither queued nor running |
 | `drainMidiOut` | `→ vector<RpcMidiOut>` | the host MIDI out (`Engine::midiOut`, which `rp.host.midi-out` fills each block) gathered over the `renderAudio*` renders since the last drain, each message with its system and frame (`renderBlocks` returns its own) |
 | `droppedMessages` | `→ uint32` | messages dropped so far (`Engine::droppedMessages`) by the plan's full output queues; never reset, and it wraps, so a test compares two reads |
 
 The table is a selection. The facets
 ([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 19
-`host` methods, 18 `emulator` (the resources, the graph and script packs, the snapshot reads and writes, the
-capability jobs, the offline jobs and the node staging), 20 `harness` (the renders, transport and sample rate, staged
+`host` methods, 19 `emulator` (the resources, the graph and script packs, the snapshot reads and writes, the
+capability jobs, the offline and engine jobs and the node staging), 20 `harness` (the renders, transport
+and sample rate, staged
 MIDI in and MIDI out, `droppedMessages`, `buildTarget`, and the audio runtimes' profiling calls),
 27 `debug` and
-6 `driver`: 90 in the union. There is no call for what the roles do on the audio thread: their protocol
+6 `driver`: 91 in the union. There is no call for what the roles do on the audio thread: their protocol
 nodes are script node types every Engine embeds, and each node's settings ride the documents `setGraph`
 plans. Nor is there one for a system's knobs: they are its system node's settings, in the same documents,
 and its params, whose live edits are `setParam`
@@ -869,6 +871,14 @@ bound makes it unreachable; a debug build asserts). The ring grows only, keeping
 its block size through `setMaxBlockFrames` never loses a queued frame. The NES's per-channel tap rings
 (`NesSoundMixer::_streamBuf`, one a stream) and its blips are sized and made with the same reserve.
 
+Mesen is vendored in-tree (`deps/mesen`, not a submodule), so RetroPlug's edits to it are plain source changes, each
+marked
+`// RetroPlug:` where they are small. One matters to parallel cores: `Utilities/SimpleLock.{h,cpp}` keeps its holder
+thread id and re-entrancy count as relaxed atomics, because `MessageManager`'s process-wide lock is taken by every
+core build and a render builds cores on pool workers beside the live engine's; ThreadSanitizer found the plain-integer
+read racing (a count changes by a relaxed load and store, never a locked read-modify-write, and the ids are asserted
+lock-free).
+
 ## `ScriptHost` + `ScriptCompiler`: the script-node runtime
 
 Native runs the TypeScript script nodes (the MIDI routing, the controller session and each role's
@@ -1171,12 +1181,26 @@ when a finite source overflows somewhere in the chain.
 
 ### The offline job pool
 
-[`OfflineJobs`](../packages/native/src/host/engine/OfflineJobs.hpp) is the `Engine`'s, made at the first submission and
-reset before the engine's other members go. It runs every job on one worker thread from start to end, which is the job
+[`OfflineJobs`](../packages/native/src/host/engine/OfflineJobs.hpp) runs two kinds of job. A **graph job** is the
+document run described below. An **engine job** (D3,
+[modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md))
+is a render: the worker pins the default floating-point environment, builds a
+[`RenderHost`](../packages/native/src/host/render/RenderHost.hpp) (a bare QuickJS runtime) whose `run()` builds the
+job's own `Engine`, in-process RPC services and server, evaluates the embedded render bundle synchronously and returns,
+and everything is destroyed on that worker before the completion is published. The job's engine runs continuous blocks
+(`EngineRpcService::setContinuousBlocks`, C++ only): `renderAudio`, `renderAudioPerSystem` and `renderAudioPerChannel`
+run whole 1024-frame blocks and carry the surplus to the next call of the same shape, system, stream count and rate.
+Progress (rendered milliseconds, a running maximum) and the script's console lines go into a per-job
+`EngineJobRecord`, bounded to 256 lines and 64 KiB between drains. Engine jobs run on at most `engineBudget()` workers,
+all but one of the pool's size (one for a pool of one, whose `maxWorkers()` of 2 lets a graph job start beside it); a
+worker takes the oldest job its kind's limit allows. The pool is the `Engine`'s, made at the first submission and
+reset before the engine's other members go. A graph job runs on one worker thread from start to end, which is the job
 host's control thread and block thread at once (quiescent: no audio thread, so `setGraph` flushes inline and
 `GraphHost::processBlock` is the block driver, draining then running), in blocks of 1024 frames, the last partial,
-checking for a cancel between blocks. It starts workers on demand while every started one is busy, at most
-clamp(hardware_concurrency, 1, 8), and never retires one: an engine that ran a kit keeps up to eight parked threads
+checking for a cancel between blocks (an engine job's blocks are continuous and its cancel is polled per chunk). The
+pool starts workers on demand while a runnable job is queued and every started one is busy, at most `maxWorkers()`:
+the pool's size, clamp(hardware_concurrency, 1, 8), or 2 for a pool of one. A render held back by the engine budget
+starts no thread. A worker is never retired: an engine that ran a kit keeps up to eight parked threads
 for the rest of its life (a parked wait, costing no CPU). At most 64 jobs are outstanding at once (queued, running, or
 finished and not yet drained), so every taken job has room for its completion. Handles are known before a job runs:
 input i is `res:(i + 1)`, output j is `res:(inputs + j + 1)`. An input of no frames is accepted and reported but names
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 30fa5e3be..5b352cdab 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -21,9 +21,11 @@ it:
   store-adjacent services `fileSelection.ts`, `fileWatcher.ts`, `sramAutoSave.ts`.
 - **Runtime / host seams** — `backend.ts` / `realBackend.ts` (control-plane RPC),
   `audioDriver.ts` (the harness's render and profiling client, and the kit compiler's caller),
-  `capability.ts` / `offline.ts` (the two promise-based job seams: a node's capability job, and a graph
-  document run to completion on an engine-owned offline pool), the composition roots
-  `pluginControlPlane.ts` / `appStores.ts` / `appHost.ts`, and the React/LVGL `ui/` tree.
+  `capability.ts` / `offline.ts` (the two promise-based job seams: a node's capability job, and the engine-owned
+  offline pool's graph documents and render jobs), `renderJobs.ts` (the renders the UI started, kept in the store
+  graph for
+  the tile badge), the composition roots `pluginControlPlane.ts` / `appStores.ts` / `appHost.ts`, and the React/LVGL
+  `ui/` tree.
 
 Roles and the protocol nodes that run them are their own subject: **doc 04** covers them end to end;
 this doc summarises them and links across. For the threading model (the command ring,
@@ -40,7 +42,7 @@ thing can be done in pure TS, it belongs in the application layer, not here.* It
 **synchronous** on purpose — in the real plugin these are in-process calls over the
 txiki↔C++ bridge — which keeps application logic straight-line and trivially testable
 against an in-memory mock. The exceptions are `openFileBrowser` (it waits on human input) and the job seams'
-promises (`capability`, `offlineRun`), which settle from a pump or a native wait rather than a timer.
+promises (`capability`, `offlineRun`, `engineRun`), which settle from a pump or a native wait rather than a timer.
 
 The methods, grouped by concern:
 
@@ -49,7 +51,7 @@ The methods, grouped by concern:
 | **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
 | **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
 | **Emulator lifecycle** | `putResource(spec)` · `releaseResource(id)` · `setGraph(request)` · `loadScriptPack(request)` | Put an immutable byte block in the engine's `ResourceTable` (a ROM, a battery, a savestate) or drop the owner's hold on one; plan a graph document TS built (which systems run, in what order and wired how, each system node naming the resources its core boots from). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over these and the snapshot reads. Load a pack of script node types beside the built-in ones, or reload one, while audio runs ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): `{ ok, error, errors, types }`, the pack's types with their descriptors, or why not (the planner's `errors` when a reload's replan refused). Nothing in the app calls it yet, and the mock refuses it |
-| **Job seams** | `capability(nodeId,op,args)` · `pumpJobs()` · `rejectPendingJobs(reason)` · `offlineRun(request)` | Promise-based jobs over the RPC's capability and offline job calls: a node's capability (C5) and a graph document run to completion on an engine-owned pool (D2), both settled by the one `pumpJobs` the UI's frame tick calls. `offlineRun`'s document may name only the built-in C++ packages (no script, system or host I/O node), and its promise settles from the tick or, in a host with none (the CLI, the Node addon, the test host), the native wait `createAudioDriver` turns on ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) sections 3 and 11) |
+| **Job seams** | `capability(nodeId,op,args)` · `pumpJobs()` · `rejectPendingJobs(reason)` · `offlineRun(request)` · `engineRun(kind,spec,{onProgress,onLog})` | Promise-based jobs over the RPC's capability and offline job calls: a node's capability (C5) and a graph document run to completion on an engine-owned pool (D2), both settled by the one `pumpJobs` the UI's frame tick calls. `offlineRun`'s document may name only the built-in C++ packages (no script, system or host I/O node), and its promise settles from the tick or, in a host with none (the CLI, the Node addon, the test host), the native wait `createAudioDriver` turns on ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) sections 3 and 11). `engineRun` runs a render (D3, kind `"render"`): a promise of `{status, message, outputs, renderedMs}` that resolves for `done`, `cancelled` and `error` alike and rejects only on a refusal or a port that throws, with `onProgress` (a running maximum) and `onLog` (a job's console lines); both runs return a handle with `cancel()`, and the mock backend rejects `engineRun` (it has no engine to run one on). `setOfflineYield` makes the poll resume through a macrotask after a native wait (the CLI's render sets it) |
 | **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path |
 | **Live params** | `setParam(nodeId,paramId,value)` | Set a param of a node the last published plan places, at the start of the next block, clamped to its range: the store's live gain and a NES's expansion volume, on the system's node `sys-<id>`. A system's other knobs are its node's settings in the document `setGraph` plans, and the audio routing is the graph's wiring, so neither is a call ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)) |
 | **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 29faf20a4..0576ccd5b 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -48,7 +48,7 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 | `retroplug-node` | shared module | The N-API addon — the same backend behind `require()`. Gated on `RETROPLUG_NODE_ADDON=ON` (needs `node_api.h`). | opt-in |
 | `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
 | `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
-| `retroplug-render-host-test` | executable | Drives the offline `RenderHost` directly ([11-ui-rendering.md](11-ui-rendering.md)). `EXCLUDE_FROM_ALL`. | no |
+| `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
 | `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` | executable | The ten Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`. | no |
 | `retroplug-n8-test` / `-n8-hwtest` | executable | Everdrive N8 protocol checks; `-hwtest` needs the physical console. `EXCLUDE_FROM_ALL`. | no |
 
@@ -159,8 +159,9 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   slot's CC at frame 0), renders on mGB and BlipToaster exactly as the same messages staged in frame order
   do, since `rp.host.midi-in` stable-sorts the staging. `renderBlocks` refuses unsorted MIDI, so it stages
   through `stageMidiIn`.
-- **Cores power on in a fixed state.** The runner sets `RETROPLUG_DETERMINISTIC=1` for `parity/` files
-  only ([util/Determinism.hpp](../packages/native/src/util/Determinism.hpp)): SameBoy's power-on randomness
+- **Cores power on in a fixed state.** The runner sets `RETROPLUG_DETERMINISTIC=1` for `parity/` files and
+  for `render-baseline`, `render-chunks` and `render-cli` (`DETERMINISTIC_FILES`)
+  ([util/Determinism.hpp](../packages/native/src/util/Determinism.hpp)): SameBoy's power-on randomness
   off, Mesen NES RAM all zeros, the SMS VDP starting at scanline 0. The app keeps the random power-on.
 - **Baselines** are `parity/baseline/<group>.json`, one value per line, each lane `<fnv> <rms>`, so a
   re-record diffs readably. Audio is compared exactly only on the target a baseline was recorded on
@@ -267,6 +268,46 @@ through real offline jobs before any caller moved, and they are still the oracle
   references at 16 bits and the buffered nodes `[dsp-buffer-nodes]`' own. Treat a kit golden failure as a real change
   to the bytes, and do not re-record it to make a refactor pass.
 
+### The render baseline (`render-baseline`, `render-chunks`, `render-cli`)
+
+Exact render bytes, the way the kit baseline holds kit bytes. A render is an engine job
+([11-ui-rendering.md](11-ui-rendering.md), [modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md)
+section 10) and these files hold what it writes.
+
+- **Matrix.** [render-cases.ts](../packages/retroplug/test-native/render-cases.ts) defines 13 cases and
+  [render-baseline.test.ts](../packages/retroplug/test-native/render-baseline.test.ts) renders each through `engineRun`,
+  reads the WAVs back and compares each with `golden/render-baseline.json` (`{ms, files: [{file, hash, size}]}` a case):
+  5 fixture-free (smsggdj playing songs authored into its battery: `smsggdj/sms/index-0/mix`,
+  `smsggdj/sms/name-chord/mix-48000`, `smsggdj/gg/index-1/mix`, the host-synced
+  `smsggdj/sms/sync-in24/transport-120bpm`, and `sameboy/mgb-sgb/no-start/mix`, mGB as a
+  Super Game Boy whose boot jingle sounds with no MIDI), 4 LSDj (tripledipper fixed 5 s as mix and 4 stems; telemelt's
+  MACHETE auto-detected, by name as a mix and by index as 4 stems) and 4 risa (ecoli_soul HFF on PAL and, through a
+  carried region role, NTSC; blumarbl fixed 3 s as channels and pins). The hash is the kit baseline's FNV-1a-64 over the
+  whole WAV, header included. Every case must sound (and every file, but the ones a case lists as silent).
+- **Targets.** Length and size are compared everywhere; the hash only on `linux-x64-gnu13`, elsewhere a case renders
+  twice and must agree with itself. `RP_RENDER_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail instead; CI's
+  linux-x64
+  `test:native` step sets it ([build.yml](../.github/workflows/build.yml)). `RP_RENDER_GOLDEN_UPDATE=1` re-records
+  (`pnpm test:native render-baseline`) and is refused off the target, for a case that disagrees with itself and for a
+  run that did not render every case (fixtures absent). The runner gives these files `RETROPLUG_DETERMINISTIC=1`.
+- **Fixtures and CI.** The LSDj and risa cases need `RETROPLUG_RESOURCES_DIR` (`__RESOURCES_DIR__`), so CI runs the 5
+  fixture-free cases (2 of the 5 test cases in the file; the rest skip, counted in the skip baseline), and a change to
+  the render path is accepted on a run with fixtures and `RP_FAIL_ON_SKIP=1`. CI's coverage of the continuous path is
+  smsggdj and the SGB jingle only: there is none of a NES, a split render or a Game Boy's stems until renders can be
+  fed MIDI.
+- **`render-chunks`** holds the chunk independence (5 cases): the host-synced smsggdj render, a fixed smsggdj render,
+  LSDj stems and risa pins each write the same files at pulls of 100, 37 and 250 ms (`RenderOpts.pullMs`), and an
+  auto-detect render writes the same samples and ends within one pull. Forcing engine jobs back to exact blocks fails
+  exactly the host-synced case. **`render-cli`** (6 cases) runs the CLI's `render` tool in the test host against the
+  same
+  golden, and the real binary's leg is `tools/run-cli-ts-tests.sh` (`pnpm test:cli-ts`: relative paths, the golden's
+  size and hash, errors, SIGINT leaving no file).
+- **A trap.** An intentional change to the render path's audio re-records in the same commit, and the diff is the
+  record of what moved; treat a failure as a real change to the bytes. Hashing in QuickJS is the long pole (about 150
+  ns a byte): the baseline takes about 60 s with fixtures.
+
+### The test tiers
+
 | Tier | Backend under test | Command | Runner | Test dir |
 |---|---|---|---|---|
 | **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
@@ -321,7 +362,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
 | `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 protocol and serial checks, `[cart-jobs]` and `[cart-release]` among them since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
@@ -386,6 +427,7 @@ Pick the cheapest tier that actually exercises the code you touched. Trust-but-v
 | **Application-layer logic** — stores, project model, path/sibling/suffix resolution, config schemas, routing, recent, kit selection | `pnpm test [slug]` | Runs the real TS against the mock backend on `tjs`. No emulator, ~instant. The canonical first stop for anything in `packages/retroplug/src` that doesn't need a live core. |
 | **The engine's output, bit for bit**: routing, block handling, MIDI staging, sync, anything Phase B moves | `pnpm test:native parity` | The parity baseline: one changed bit in any lane, MIDI byte or RAM byte fails, which RMS-based tests never see. |
 | **A kit compile**: `src/kit`'s chains, the packing, the bank, and the offline seam they run on | `pnpm test:native kit` (`kit-baseline`, `kit-compile`, `kit-dither`) and `pnpm test:native offline-run`; `pnpm test:ui kit-compile` for the plugin's facets | The 39 baseline banks and the 12 edge banks, exact bytes on `linux-x64-gnu13` and each case's self-agreement plus the structural asserts elsewhere ([the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `offline-run` is the promise side over the real host, and the UI case settles a whole compile from frame pumps on the plugin's own registration. |
+| **A render**: the render library, engine jobs, the CLI's `render`, the Render menu and its badge | `pnpm test:native render` (`render-baseline`, `render-chunks`, `render-cli`; `engine-run` by name) with `RETROPLUG_RESOURCES_DIR` and `RP_FAIL_ON_SKIP=1`, `retroplug-engine-graph-test "[engine-jobs]"` (under `build-tsan` / `build-asan` for the pool), `pnpm test render`, `pnpm test:ui` (`render-menu`, `render-badge`, `render-job`), `pnpm test:cli-ts` | The baseline holds the bytes (exact on `linux-x64-gnu13`), `[engine-jobs]` the pool's threading and the continuous blocks, the UI cases the submission and the badge on a scripted backend and on a real 3 s render, and `test:cli-ts` the real binary's output, exit codes and Ctrl-C. |
 | **The DSP kernels/nodes**: the chain's effects and resample, the buffers the kit passes read | `pnpm test:plugin` (`[dsp]`, `[dsp-nodes]`, `[dsp-resample]`, `[dsp-buffer-nodes]`, and `[offline]` for the pool) | The `[dsp]` cases hold the kernels to their padded references at 16 bits where a 4-bit code cannot see a one-ulp move; the nodes' cases hold each node to its kernel, the state guards, the dither's per-channel noise and the buffer-reading nodes' peaks and seeds; `[offline]` holds the job pool. |
 | **DSP behaviour / cores / the command-ring & snapshot seam**: the protocol nodes and their wires, MIDI/serial, transport, actual emulator output | `pnpm test:native [slug]` | Runs the **real** Backend RPC surface with a live `SameBoySystem` on the real engine graph, the protocol nodes running in the script host every Engine embeds. Can read emulator state and branch on it. This is the tier that proves audio actually flows. |
 | **React/LVGL UI** — App/SystemGrid/tiles/Menu, input, layout | `pnpm test:ui [slug]` | Boots the real UI bundle on a software LVGL display and asserts on the live widget tree (structure, not just pixels). |
@@ -412,6 +454,13 @@ same options (the C1 design's section 9). Any finding aborts the host → nonzer
 
 ## Build gotchas
 
+- **`deps/sameboy` is patched at configure, twice.** `cmake/sameboy.cmake` applies two tracked patches idempotently
+  (`git apply --reverse --check` no-ops one already present): `cmake/patches/sameboy-per-channel-audio.patch` (the
+  per-channel audio tap, `Core/apu.{c,h}`) and then `cmake/patches/sameboy-process-statics.patch` (the power-on random
+  seed and its enable flag as relaxed atomics in `Core/random.c`, the camera's noise seed in `Core/camera.c`, and the
+  SGB's `fm_sin` table built by a load-time constructor in `Core/sgb.c`, so that parallel Game Boy cores in engine jobs
+  do not race). After any configure `git status` shows five modified files in `deps/sameboy`: expected. A bump that
+  invalidates either patch fails loudly at configure.
 - **The embedded bundles are derived — never commit them.** `cp-bundle.js`/`cp-bundle_data.c` and
   `ui-bundle.js`/`ui-bundle_data.c` are CMake `BYPRODUCTS` under `build/native/`
   (CMake `BYPRODUCTS` of the `retroplug-cp-bundle` / `-ui-bundle` custom targets), regenerated from
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index a751cf527..047aa2401 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -388,8 +388,8 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     wired param is not clamped. D2 needs wider ranges, settings-level values or a control wire, and the baseline now
     carries the cases to hold it to: a quiet source normalised at all three rates (and through a dither), a negative
     gain, a 10 Hz high-pass at Q 20, and a 30 dB high shelf.
-  - **D3**: the CLI `render` and the UI background renders on the offline driver, keeping their block-size sequence
-    and per-block side effects or re-scoping their byte-identity claim; D1 changed neither path.
+  - **D3** (built since: the D3 bullet below): the CLI `render` and the UI background renders moved to engine jobs on
+    the offline pool; D1 changed neither path.
   - **Residuals.** The kit baseline is a 4-bit view and nearly blind to a one-ulp move in Sierra Lite or JJN (the
     `[dsp]` cases carry that). `rp.dsp.resample` and a recorder on one buffer in one plan are unsupported (the planner
     refuses a port reader beside a writer, and a settings ref is a binding it does not see), and a rate change resumes
@@ -416,14 +416,10 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   codecs, `KitUtil`, `SampleUtil` and the harness `compileKit` / `compileDmc` RPCs are gone, so the plugin compiles
   kits too. Strict parity held bit for bit at every commit, and no parity or kit golden was re-recorded (the `rp.dsp`
   descriptor goldens were, in stages 1 and 3). What it leaves:
-  - **D3**: the CLI `render` and the UI background renders on the seam, keeping their block-size sequence and
-    per-block side effects or re-scoping their byte-identity claim. A render sees a completion's `guardsFired` /
-    `guardReason` and each buffer's `nonFinite` and decides; nothing fails on them, and a byte-exact caller refuses a
-    guard count above 0. The seam's block is a fixed 1024 frames, and its jobs are bounded (64 outstanding, up to 8
-    workers). D3 must also extend or decide what a render needs and the seam lacks: system nodes and a script host in a
-    job (its registry is rp.core, rp.dsp and rp.dsp.resample only), a TypeScript cancel, completions that stream
-    rather than return at the fixed `frames`, a rejection at project close, a playing and advancing transport (a job's
-    is fixed at 120 bpm, ppq 0, not playing) and per-block MIDI or other event input (a job has none).
+  - **D3** (built since: the next bullet): the CLI `render` and the UI background renders on the pool as engine jobs
+    (kind `render`: a bare QuickJS runtime and an `Engine` of their own on a worker), not as graph jobs, so a job's
+    registry, transport and MIDI input needed no extension. A graph job's transport stays fixed at 120 bpm, ppq 0, not
+    playing, and takes no MIDI.
   - **A plugin kit row** is possible and not built: it imports `src/kit` and runs it on the stores' backend, not
     `audioDriver` (the driver's compile would turn on the offline poll's native wait and block the UI frame).
   - **Residuals.** `src/lsdj/` is test-reference code only now (`Effects`, `KitBank.hpp`); `OffsetLookup.hpp` is dead
@@ -449,6 +445,36 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **Confirm the removed transitive includes build under MSVC.** The tree has not been built with MSVC since D2
     deleted headers that used to arrive through `BackendTypes.hpp` and `retroplug-core`; run `build.bat` (or the
     Windows CI job) before trusting the platform that has not run here.
+- **D3 built: renders as engine jobs**
+  ([modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md), five commits): the pool runs a second
+  kind of job, a render, on a `RenderHost` and an `Engine` of its own, with progress and console lines in the drain,
+  TypeScript cancel for both kinds (`engineRun` / `offlineRun` handles) and a worker budget that keeps one worker for
+  kit compiles; an engine job's engine runs continuous 1024-frame blocks, so a render's samples do not depend on how
+  the library chunks its pulls; `retroplug-cli render` and the UI's System > Render submit the same job, and
+  `RenderJobRegistry`, the `__rp_*Render` hooks and the CLI's in-process render path are gone. The render baseline
+  (`render-baseline`, 13 cases) records the bytes; no parity or kit golden was re-recorded. Two vendored fixes for
+  parallel cores came with it: Mesen's `SimpleLock` atomics and `cmake/patches/sameboy-process-statics.patch`. What it
+  leaves:
+  - **CI cannot see most of the render path.** The only fixture-free sounding renders are smsggdj with an authored song
+    (4 cases) and mGB as a Super Game Boy (its boot jingle), so CI holds 5 of the baseline's 13 cases and nothing of a
+    NES, a split render or a Game Boy's stems; the LSDj and risa cases run only with fixtures. MIDI-driven renders (an
+    mGB or BlipToaster render fed a MIDI file) would close it and are not built. The C++ `[engine-jobs]` cases are the
+    only fixture-free per-channel coverage today (mGB through `rp.mgb.midi`).
+  - **An auto-detect render's length is quantised to its 100 ms poll**, and acts between the library's calls (presses,
+    smsggdj's load, detection reads) land at the next block boundary after the call. Block-granular probes or an
+    autonomous recipe (the D3 design's section 8) would remove both.
+  - **`tools/run-sanitizer.sh` has no render leg.** The thread and address checks of the pool are `[engine-jobs]` built
+    in `build-tsan` / `build-asan` by hand (`retroplug-render-host-test`'s three legs also run clean there); a script
+    leg would make them routine. `retroplug-render-host-test` is a manual smoke, not a CI step.
+  - **Wedged jobs.** A render that never settles hangs the CLI where the old bounded loop gave up (a second Ctrl-C kills
+    it); with the editor closed nothing drains, so a render that finished then is listed as rendering until the editor
+    pumps again (its WAV is on disk).
+  - **The SameBoy camera half of the statics patch has no red-green test** (no Game Boy Camera ROM exists in the repo or
+    the fixtures), and the exact render bytes are held only on `linux-x64-gnu13`. The patch uses GNU `__atomic`
+    builtins, which GCC and Clang (clang-cl on Windows) have; the tree has not been built with MSVC since D2.
+  - **A GBA render case** is missing: without a BIOS the repo's GBA ROM renders exact silence.
+  - **Cores print straight to the process.** Mesen's load log goes to fd 1 while the job's console lines arrive through
+    the drain, so in the CLI the two can interleave differently than before.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
diff --git a/spec/09-cli-debugging.md b/spec/09-cli-debugging.md
index cb99b586d..86d689d9e 100644
--- a/spec/09-cli-debugging.md
+++ b/spec/09-cli-debugging.md
@@ -80,7 +80,9 @@ bundled by the `retroplug-example-session` CMake target via
 For end users, `retroplug-cli` also carries **named subcommands whose session is compiled into the
 binary** (tjsc bytecode, exactly like the plugin's control-plane/UI bundles) — no loose `.js`, no Node,
 just the executable. The first is `render`, which takes a ROM + its battery `.sav` (or a savestate)
-straight to WAV:
+straight to WAV. It runs as an engine job on the CLI's own engine ([11-ui-rendering.md](11-ui-rendering.md)); the
+first Ctrl-C cancels it (`ERROR: render: cancelled`, exit 1, no WAV from a cancel that lands mid-render) and a second
+kills the process:
 
 ```
 retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--max-duration t]
diff --git a/spec/10-multichannel-audio-out.md b/spec/10-multichannel-audio-out.md
index f029c4300..e9483e317 100644
--- a/spec/10-multichannel-audio-out.md
+++ b/spec/10-multichannel-audio-out.md
@@ -151,7 +151,9 @@ SameBoy is a **pinned vendored submodule**, so the tap ships as a tracked patch
 (`cmake/patches/sameboy-per-channel-audio.patch`) applied idempotently at configure by
 `cmake/sameboy.cmake` — no submodule-pointer bump, durable on a fresh clone, and a future bump that
 invalidates the patch fails loudly at configure rather than silently dropping per-channel output
-(resolved, §8/§9).
+(resolved, §8/§9). A second tracked patch, `cmake/patches/sameboy-process-statics.patch`, is applied after it and for a
+different reason: it makes SameBoy's process-wide statics (the power-on random seed, the camera's noise seed, the SGB's
+`fm_sin` table) safe for parallel cores ([02-native-host.md](02-native-host.md)).
 
 ## 3. NES design (Mesen) — secondary, CLI-first
 
diff --git a/spec/11-ui-rendering.md b/spec/11-ui-rendering.md
index 9fb20d74b..a0b044eb3 100644
--- a/spec/11-ui-rendering.md
+++ b/spec/11-ui-rendering.md
@@ -4,7 +4,7 @@
 `retroplug-cli render` does — full mix or per-channel/per-pin stems — but as a **background job** inside
 the plugin, so the menu can close while it runs and several systems can render at once. It builds on
 [10-multichannel-audio-out.md](10-multichannel-audio-out.md) (the split modes / `renderAudioPerChannel`),
-[09-cli-debugging.md](09-cli-debugging.md) (the CLI `render` pipeline it now shares), and
+[09-cli-debugging.md](09-cli-debugging.md) (the CLI `render` it shares a job path with), and
 [01-architecture.md](01-architecture.md) (the control plane / audio-thread split it must not violate).
 
 ## Thesis
@@ -16,35 +16,44 @@ improving one improves both.
 
 ## The shape
 
+A render is an **engine job**: a second kind of job on the engine's offline pool, beside the kit compile's graph jobs
+([modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md), built on D2's
+[pool](01-architecture.md)). The CLI and the UI submit the same job through the same RPCs.
+
 ```
-System > Render menu ─▶ startSystemRender (ui/host/render.ts)
-   │  browseThen → out path      │  snapshot live SRAM/savestate → temp file
-   ▼                             ▼
-__rp_startRender(sysId, specJson)  ──▶  PluginUI  ──▶  RenderJobRegistry (process-global)
-                                                          │  one std::thread per job
+System > Render menu ─▶ startSystemRender (ui/host/render.ts)     retroplug-cli render (cli/sessions/render.ts)
+   │  browseThen → out path      │  snapshot live SRAM/savestate → temp file        │  parse argv → spec
+   ▼                             ▼                                                   ▼
+RenderJobs.start (src/renderJobs.ts)  ──────────────▶  backend.engineRun("render", spec, {onProgress, onLog})
+                                                          │  submitEngineJob / drainOfflineJobs / cancelOfflineJob
                                                           ▼
-                                                     RenderHost  (bare-QuickJS + own Engine)
-                                                          │  runs the render-worker bundle
+                                       OfflineJobs (the Engine's pool): a worker runs the job to the end
+                                                          │  RenderHost (bare QuickJS + the job's own Engine)
                                                           ▼
-                                       src/render/  shared library  ◀── also the CLI `render` command
-EmulatorTile badge ◀── __rp_getRenderJobs (per-frame poll) ◀── RenderJobRegistry::snapshot(owner)
+                                       src/render/  shared library  (runRenderJob, in the render-worker bundle)
+EmulatorTile badge ◀── renders.list() (the list RenderJobs keeps from the job's progress and promise)
 ```
 
 ## The shared render library — `packages/retroplug/src/render/`
 
-Host-neutral TypeScript, consumed by **both** the CLI `render` command and the background worker:
+Host-neutral TypeScript, run by every render: the engine job's worker bundle (`worker.ts`) is its only production
+caller, and the in-process library tests call `runRenderJob` directly:
 
 - `render.ts` — `runRenderJob(ctx, opts, hooks)`: builds the system, resolves the length (LSDj HFF
   auto-detect via NR52, or a fixed duration), streams per-chunk PCM to WAV (mix / channels / pins).
-- `types.ts`: `RenderOpts` (the request), `RenderContext` (`{ backend, project, audio }`: the CLI's
-  `Session` satisfies it structurally, and the system's roles reach their protocol nodes in the graph its
-  `project` publishes), `RenderHooks` (`onRendered` / `isCancelled` / `log` / `warn`, all
-  optional so the CLI passes none), `RenderCancelled`.
+- `types.ts`: `RenderOpts` (the request; `pullMs` is a test knob for the stream's pull length, default 100),
+  `RenderContext` (`{ backend, project, audio }`: a CLI `Session` satisfies it structurally, and the system's roles
+  reach their protocol nodes in the graph its `project` publishes), `RenderHooks` (`onRendered` / `isCancelled` /
+  `log` / `warn`, all optional), `RenderCancelled`. A cancelled render's sink `discard()`s the files it opened.
 - `wav.ts` — the streaming RIFF/PCM16 codec (moved from `cli/wav.ts`, which now re-exports it).
 - `worker.ts` — the background session entry (below).
 
-The CLI `render` command ([cli/sessions/render.ts](../packages/retroplug/cli/sessions/render.ts)) is now a
-thin `parseRenderArgs → runRenderJob` wrapper (`--list-songs` stays CLI-only).
+The CLI `render` command ([cli/sessions/render.ts](../packages/retroplug/cli/sessions/render.ts)) parses its
+arguments into the job spec, submits `backend.engineRun("render", spec, {onLog})` to its own engine and awaits it,
+printing the job's console lines as they arrive (`--list-songs` stays in-process). It sets `keepAlive()` and
+`setOfflineYield` so a minute-long render pumps without exhausting the launcher's bounded loop, and registers
+`onInterrupt` so the first Ctrl-C cancels the job (it ends `ERROR: render: cancelled`, exit 1, and a cancel that
+lands mid-render leaves no WAV).
 
 ## RenderHost — the offline render environment (`packages/native/src/host/render/RenderHost.*`)
 
@@ -58,27 +67,40 @@ needed. Beyond the ES standard library the only shims are `console` + a minimal
 `__rpcSend` + rendered/cancel/result thunks the worker reports through. Bare QuickJS also sidesteps the
 multi-txiki-runtime class-id hazard entirely.
 
-`run(jobJson, onRendered, isCancelled)` blocks for the whole render on the calling thread; ROM is loaded
-by path, SRAM/savestate ride as bytes. Output is **byte-identical** to `retroplug-cli render`.
-
-## RenderJobRegistry — concurrent, cancellable jobs (`.../RenderJobRegistry.*`)
-
-Owns the in-flight jobs, one dedicated `std::thread` each. **Process-global** (a static in
-[PluginUI.cpp](../packages/native/plugin/PluginUI.cpp)) so a render survives its editor window closing —
-each job spins its own `RenderHost`+`Engine`, referencing nothing on the live plugin. Thread-safe status
-for the UI poll; cooperative cancel via an atomic the worker polls between chunks; jobs are tagged with the
-editor (`owner`) that started them so a multi-instance host only shows its own. Mesen's process-global
-setup is guarded by `std::call_once` ([MesenGlobalInit](../packages/native/src/system/mesen/MesenGlobalInit.cpp))
-so concurrent core construction can't race.
+`run(jobJson, onRendered, isCancelled, onConsole)` blocks for the whole render on the calling thread, which is a
+pool worker; ROM is loaded by path, SRAM/savestate ride as bytes. The job's engine runs **continuous blocks**
+(`EngineRpcService::setContinuousBlocks`): whole 1024-frame blocks whatever each call asks for, the surplus carried to
+the next call of the same shape, so a render's sample stream does not depend on the library's chunking. Console writes
+go to `onConsole` (with none, to the process's stdout and stderr).
+
+## Engine jobs - the pool (`.../engine/OfflineJobs.*`)
+
+A render is submitted with `submitEngineJob({kind: "render", spec})` (the emulator facet) and settled by the same
+`drainOfflineJobs` / `cancelOfflineJob` as a kit compile. The pool is the `Engine`'s, so a render belongs to its plugin
+instance: it outlives the editor window, and the instance's teardown cancels it (at the next 100 ms chunk) and joins
+the worker. Each job builds its `RenderHost` and `Engine` on its worker and destroys them there, referencing nothing on
+the live plugin. A drain reports every running job's progress and the console lines written since the last one
+(bounded: 256 lines and 64 KiB per job between drains); a completion is `done`, `cancelled` or `error`, with the files
+written. Engine jobs hold at most all but one of the pool's workers (one for a pool of one), queue first in, first out,
+and share the 64-job bound with graph jobs, so a kit compile always finds a worker. Process-wide state that parallel
+cores touch is made safe: Mesen's setup is a function-local static,
+initialised once whichever thread builds the first core
+([MesenGlobalInit](../packages/native/src/system/mesen/MesenGlobalInit.cpp)), its `SimpleLock` is atomic, and
+SameBoy's `Core/random.c`, `camera.c` and `sgb.c` statics are patched
+([06-build-test.md](06-build-test.md)).
 
 ## The UI seam
 
-- **Hooks** (bound in `installWindowSizeHooks`, per-context routed): `__rp_startRender(sysId, specJson)`,
-  `__rp_cancelRender(jobId)`, `__rp_dismissRenderJob(jobId)`, `__rp_getRenderJobs()`. See
-  [03-ts-layer.md](03-ts-layer.md) for the `__rp_*` pattern.
+- **No hooks.** Renders need no `__rp_*` window hook: the UI submits through the stores' backend
+  (`stores.backend.engineRun`), so every host that mounts the emulator facet renders, the SDL standalone included.
+- **`src/renderJobs.ts`** - `RenderJobs` (`start(systemId, spec)`, `list()`, `cancel(id)`, `dismiss(id)`, `prune()`)
+  keeps what each job's progress callback and promise report, plus its handle. It belongs to the store graph
+  (`AppStores.renders`), which in a DAW is the control plane's, so it outlives the editor and a completion settles
+  through the same queue that submitted the job (nothing drains with the editor closed; the WAV is on disk either
+  way, and the badge catches up at the next pump).
 - **`ui/host/render.ts`** — `startSystemRender` snapshots the live `readSram`/`readState` to a temp file
-  under `configDir` and calls `__rp_startRender`; `pickActiveRenderJob` selects a tile's badge job and
-  `renderBadgeLabel` writes its text. Inert in the headless harness (no hooks bound).
+  under `configDir` and calls `renders.start`; `pickActiveRenderJob` selects a tile's badge job and
+  `renderBadgeLabel` writes its text.
 - **The instance's CONFIGURATION rides along** — `spec.roles` (the live system's roles, configs included)
   and `spec.gainDb`. The job boots a fresh core from the ROM on disk, so without them it would take each
   role's schema DEFAULTS and render a PAL project at NTSC, a Game Boy as the wrong model, a replaced DMC kit
@@ -97,25 +119,35 @@ so concurrent core construction can't race.
   menu; Page keys aren't LVGL-translated so they arrive there without stealing focus). "Max Duration" bounds
   every render — `runRenderJob`'s fixed fallback is now `durationMs ?? maxDurationMs` (default 300000, so
   default CLI output is unchanged).
-- **Tile** — [EmulatorTile.tsx](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) polls
-  `__rp_getRenderJobs()` on the existing per-frame event and draws a bottom badge counting the audio
+- **Tile** - [EmulatorTile.tsx](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) samples
+  `renders.list()` on the existing per-frame event and draws a bottom badge counting the audio
   rendered so far (`rendering 0:42 (tap to cancel)`); finished jobs are auto-dismissed. Deliberately **not**
   a percentage: an auto-detect render ends at the song's HFF stop, so the only bound available up front is
   `maxDurationMs` - a safety cap usually far longer than the song, which made the old fill bar creep to ~10%
-  and finish. The reported number is therefore rendered milliseconds end to end (`onRendered` ->
-  `__rp_reportRenderedMs` -> `RenderJobRegistry::Status::renderedMs`), settling on the length written.
+  and finish. The reported number is therefore rendered milliseconds end to end (`onRendered` -> the job's
+  `EngineJobRecord` -> the drain's progress -> `onProgress`, a running maximum), settling on the length written (the
+  result's `renderedMs`). A tap on a rendering
+  badge calls `renders.cancel`; a finished job whose system has no tile is dropped as it settles.
 
 ## Verification
 
-- **Native parity/concurrency/cancel:** `retroplug-render-host-test` (built by name) — `<job-json>` proves
-  byte-identity vs `retroplug-cli render`; `--registry <j>...` runs jobs concurrently; `--cancel <j>` aborts
-  mid-render. Clean under ThreadSanitizer (`tools/run-sanitizer.sh thread`, built in `build-tsan/`).
+- **The seam:** the `[engine-jobs]` Catch2 cases in `retroplug-engine-graph-test` (the pool's threading, the budget,
+  cancel, teardown, continuous blocks and the carry), run under ThreadSanitizer and AddressSanitizer from `build-tsan` /
+  `build-asan` (`tools/run-sanitizer.sh` does not run them); `pnpm test:native engine-run` (the promise side over the
+  real host).
+- **The render baseline** (`pnpm test:native render-baseline`): 13 renders through engine jobs hashed against
+  `test-native/golden/render-baseline.json`, exact on `linux-x64-gnu13` (`RP_RENDER_GOLDEN_REQUIRE_TARGET=1` in CI),
+  with `render-chunks` holding the chunk independence and `render-cli` the CLI's tool against the same golden
+  ([06-build-test.md](06-build-test.md)).
+- **A manual smoke:** `retroplug-render-host-test` (built by name) - `<job-json>` runs one engine job and is
+  `cmp`-identical to `retroplug-cli render` (the same code); `--parallel <j>...` runs jobs concurrently on one pool;
+  `--cancel <j>` cancels mid-render. The coverage CI runs is `[engine-jobs]`; the tool is not a CI step.
 - **UI-seam logic:** `pnpm test render` — `startSystemRender` spec/snapshot, `pickActiveRenderJob`,
   `renderBadgeLabel`, `validSplits`/`formatDuration`, the `userConfig.render` setters/clamps, and (in
   `risa-autodetect`) that `onRendered` reports real audio time: monotonic, stepping by the render chunk,
   settling on the length written.
 - **Badge + menu:** `pnpm test:ui render-badge` (tile badge) and `pnpm test:ui render-menu` (the Split /
-  Sample Rate / Max-Duration selectors + PageUp/PageDown coarse step + Render… → `__rp_startRender`), both on
+  Sample Rate / Max-Duration selectors + PageUp/PageDown coarse step + Render… → a submitted engine job), both on
   the real LVGL display via a file-dropped ROM.
 - **Carried configuration:** `pnpm test:native risa-render` — a carried `region` moves the DETECTED song
   length (PAL 59000 ms → NTSC 49800 ms, the 50/60 clock ratio), and a carried `gainDb` / role knob lands on
@@ -127,11 +159,12 @@ so concurrent core construction can't race.
 
 ## Not yet built / deferred
 
-- **Project-level render** (all instances at once) — the registry + per-job-thread model already supports N
-  concurrent jobs; only the "render the whole project" entry point is missing.
+- **Project-level render** (all instances at once) - the pool already runs N engine jobs concurrently; only the
+  "render the whole project" entry point is missing.
 - **Tempo / transport UI** — no BPM/transport control in the menu (split, sample rate, and max duration are
   now selectable; a pinned *fixed* duration separate from the max cap is not).
 - **Embedded-ROM render** (mGB with no on-disk path) — gated out; would need the `embeddedRom` marker
   threaded through the worker spec.
-- **Multi-instance-in-DAW badge on editor reopen** — a job's owner is the editor session, so a close/reopen
-  loses the badge (the render still completes + writes its file).
+- **MIDI-driven renders** - a render sends no MIDI, so mGB and BlipToaster render silence; with them, CI would cover
+  SameBoy's stems and the NES fixture-free. The render baseline's LSDj and risa cases need fixtures, so CI's coverage of
+  the continuous path is smsggdj and the SGB jingle only.
diff --git a/spec/README.md b/spec/README.md
index 27990217e..974b9da50 100644
--- a/spec/README.md
+++ b/spec/README.md
@@ -38,7 +38,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 | [08-profiling.md](08-profiling.md) | **Built** (behind `RETROPLUG_PROFILE`, `pnpm profile`). A benchmark harness that profiles the DSP-thread JS runtime — allocations/GC — under an mGB + heavy-MIDI workload: the refcount-churn reframe, the off-RT `renderAudio` harness, in-process QuickJS allocator instrumentation, the agent-friendly tool tier, and CI gates. |
 | [09-cli-debugging.md](09-cli-debugging.md) | **Built** (the plumbing tier). The CLI as a scriptable ROM-test harness: the session runner + `Timeline` + WAV/screenshot output, the compiled-in `render` subcommand, and Mesen's compiled-in debugger (decoded APU/PPU state, CPU/memory peeks + poke, breakpoints, trace, profiler, cc65 labels) surfaced through the debug RPC facet so an agent can drive a real NES and assert on its state. |
 | [10-multichannel-audio-out.md](10-multichannel-audio-out.md) | **Built** (steps 1–7; per-mapper NES expansion sub-channels remain). Outputting the individual console sound channels of one instance: Game Boy → 8 outputs (a stereo pair per channel, via a tracked SameBoy tap), NES → 5 mono stems or the hardware "stereo-mod" pins (via a `NesSoundMixer` edit). One mode-driven host seam (`channelLayout()` + a lane-counted `finishBlock`, with the graph TS builds wiring each stream to its lanes), the `channelSplit` / `pinSplit` / `stereoPinSplit` plugin options (menu rows `Channels` / `Pins` / `Stereo Pins`; a stereo stream takes a lane pair, a mono one a single lane, so the NES fits 8 outputs - except `Stereo Pins`, which spends a pair per pin so each is its own DAW track), and a `renderAudioPerChannel` CLI RPC feeding multichannel / per-stereo / per-mono WAV export. |
-| [11-ui-rendering.md](11-ui-rendering.md) | **Built.** Rendering a system to WAV from the `System > Render` menu as a background job — a fresh instance built from a *copy* of the live SRAM/savestate (never the running core). A shared `src/render/` library the CLI `render` command and the worker both run; a bare-QuickJS `RenderHost` (own `Engine`, no txiki) running a global-code worker bundle; a `RenderJobRegistry` of per-job threads (concurrent, cancellable, survives editor close); and the `__rp_*Render` hooks + tile elapsed-duration/cancel badge. |
+| [11-ui-rendering.md](11-ui-rendering.md) | **Built.** Rendering a system to WAV from the `System > Render` menu or `retroplug-cli render` as an engine job on the offline pool - a fresh instance built from a *copy* of the live SRAM/savestate (never the running core). A shared `src/render/` library run by a bare-QuickJS `RenderHost` (own `Engine`, no txiki, continuous blocks) on a pool worker; one `engineRun` promise, cancellable, surviving editor close; the `RenderJobs` list behind the tile elapsed-duration/cancel badge; and the render baseline that holds the bytes. |
 | [12-dynamic-parameters.md](12-dynamic-parameters.md) | **Built** (CLAP + VST3; other formats keep their construct-time names). Per-ROM named DAW automation lanes mapped to mGB / BlipToaster MIDI CCs: the fixed-pool / mutable-descriptor contract the two formats actually permit, the DPF fork's `requestParameterInfoUpdate()` + re-declare-and-diff behind an opt-in macro, the plugin's CC slot pool and its `__rp_parameterMapJson` seam, and the real-Reaper check that proves a host acts on the flag. |
 | [13-subsystems.md](13-subsystems.md) | **The index of everything else.** One short entry each for the subsystems the docs above do not cover: the common tracker interface (`src/tracker/`), risa, smsggdj, the LSDj HD player, the Everdrive N8 hardware bridge, Launchpad, the controller role, the pico firmware, `retroplug-sdl`, the Node addon, and the node graph the engine runs on. What each is, where it lives, and which command proves a change to it. |
 
````

Run: `sed -n '9114,9694p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 3: AGENTS.md, the README and the older notes**

`AGENTS.md` gains the `[engine-jobs]` tag in the engine-graph paragraph, the `SimpleLock` note, a render-baseline
paragraph beside the kit baselines and a rewritten UI/background rendering paragraph (the tool's `<job-json>`,
`--parallel` and `--cancel` legs, a manual smoke). The README says the CLI's render runs as an engine job and what
Ctrl-C does to it, and the two older documents that named `RenderJobRegistry` carry a one-line note that D3 retired
it.

````diff
diff --git a/AGENTS.md b/AGENTS.md
index dc5787df9..17591f56e 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -72,7 +72,12 @@ The rules below are the parts that don't fit those.
   flight, every refusal's reason, the RPC's samples, both kit windows and the cap's arithmetic against copies of each
   formula, the file input against `SampleCache`'s decode, the live plan unchanged beside a job and its warmed blocks
   allocating nothing, a guard count and a per-buffer non-finite flag, and a job's own floating-point environment
-  (`[offline]`, with a hidden `[offline-bench]`);
+  (`[offline]`, with a hidden `[offline-bench]`); engine jobs, a render run on a pool worker by a `RenderHost` and an
+  `Engine` of its own (D3, [docs/modular-graph-phase-d3-design.md](docs/modular-graph-phase-d3-design.md),
+  `test/engine-graph/EngineJobs.test.cpp`): a job's WAV against the same render run directly, the worker budget and its
+  first-in-first-out order, a cancel at boot, mid-render and after the body, an engine destroyed with renders in flight,
+  a flush-to-zero submitter, continuous blocks and the carry's keys, and per-channel stems (`[engine-jobs]`, with the
+  fixture-gated `[engine-jobs-hff]`; the pool's sanitizer checks are this tag built in `build-tsan` / `build-asan`);
   TS nodes run behind the C ABI in the script host (B3a,
   [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
   creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
@@ -135,7 +140,8 @@ The rules below are the parts that don't fit those.
   `Core/camera.c` / `Core/sgb.c` (all five expected): **don't
   reset / stash / commit that, and don't bump the pointer to "absorb" it.** A
   submodule bump that invalidates a patch fails LOUDLY at configure (rather than
-  silently dropping what it adds); regenerate the patch if that happens.
+  silently dropping what it adds); regenerate the patch if that happens. (Mesen is vendored in-tree, so its one
+  parallel-cores fix, `Utilities/SimpleLock.{h,cpp}`'s atomic holder id and count, is a plain source change.)
 - **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
   `build/` is load-bearing for the dev loop.
 - **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
@@ -449,6 +455,16 @@ refused off that host and for a case that disagrees with itself. Unlike parity i
 a dither can leave every hash matching, which is what `[dsp]`'s and `[dsp-buffer-nodes]`' references are for. The C++
 `[kit-baseline]` suite went with native's compiler; that tag now exits 2 in `retroplug-audio-test`.
 
+**The render baseline** (`pnpm test:native render-baseline`, [spec/06-build-test.md](spec/06-build-test.md)) holds the
+bytes of renders run as engine jobs: 13 cases (smsggdj playing authored songs, mGB as a Super Game Boy, LSDj, risa),
+each output WAV's FNV-1a-64 and size against `packages/retroplug/test-native/golden/render-baseline.json`. Exact bytes
+are compared only on `linux-x64-gnu13` (CI's linux-x64 job sets `RP_RENDER_GOLDEN_REQUIRE_TARGET=1`), elsewhere a case
+renders twice and must agree with itself, and a re-record (`RP_RENDER_GOLDEN_UPDATE=1`) is refused off that host, for a
+case that disagrees with itself and for a run missing a case. The test-native runner gives `render-baseline`,
+`render-chunks` and `render-cli` `RETROPLUG_DETERMINISTIC=1` (`DETERMINISTIC_FILES`), as it does `parity/`. The LSDj and
+risa cases need fixtures, so CI runs 5 of the 13 (smsggdj and the SGB jingle): accept a render-path change only on a
+run with fixtures and `RP_FAIL_ON_SKIP=1`. `render-chunks` holds the sample stream to every pull length.
+
 **`retroplug-cli` as a consumer's whole test harness** (`pnpm test:cli-ts`):
 `retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `run <session.ts>` does one
 file - so a consumer repo (BlipToaster) needs **no Node, npm, esbuild or node_modules**. Its kit is the
@@ -485,9 +501,11 @@ JavaScript. (`@swc/wasm-typescript` was tried first and rejected: under the WAMR
 on 3 of 7 real test files and silently emitted EMPTY output on 2 more, which makes a test file "pass".)
 
 **UI/background rendering** (the `System > Render` menu; [spec/11-ui-rendering.md](spec/11-ui-rendering.md)):
-the CLI `render` command and the UI render share one library (`packages/retroplug/src/render/`), run offline
-by a bare-QuickJS `RenderHost` (own `Engine`) on a per-job thread (`RenderJobRegistry`). Verify with
-`retroplug-render-host-test` (built by name — `cmake --build build --target retroplug-render-host-test`): a
-`<job-json>` render is byte-identical to `retroplug-cli render`; `--registry <j>...` runs jobs concurrently;
-`--cancel <j>` aborts mid-render — build it in `build-tsan/` for the ThreadSanitizer pass. The UI-seam logic
-is `pnpm test render`; the tile badge is `pnpm test:ui render-badge`.
+the CLI `render` command and the UI render are one engine job (D3): both submit `backend.engineRun("render", spec)`,
+and a pool worker runs the shared library (`packages/retroplug/src/render/`) in a bare-QuickJS `RenderHost` with an
+`Engine` of its own, in continuous 1024-frame blocks. The UI keeps its list of renders in `AppStores.renders`
+(`src/renderJobs.ts`). Verify with the render tests (`pnpm test:native render`, `pnpm test render`,
+`pnpm test:ui render-badge`), `[engine-jobs]`, and `retroplug-render-host-test` (built by name - `cmake --build build
+--target retroplug-render-host-test`), a manual smoke that CI does not run: `<job-json>` runs one render as an engine
+job (`cmp`-identical to `retroplug-cli render`, the same code); `--parallel <j>...` runs jobs concurrently on one pool;
+`--cancel <j>` cancels mid-render. Build it in `build-tsan/` for a ThreadSanitizer pass.
diff --git a/README.md b/README.md
index 9c829808b..56790e803 100644
--- a/README.md
+++ b/README.md
@@ -280,7 +280,8 @@ The `render` command boots a Game Boy (`.gb` / `.gbc`), NES (`.nes`), GBA (`.gba
 Master System (`.sms`) or Game Gear (`.gg`) ROM and writes its audio to a WAV file. For
 a saved tracker song it presses play so the song begins (pass `--no-start` to capture
 raw boot audio); mGB needs no such press, as it plays from incoming MIDI. A sibling
-`<rom>.sav` is loaded automatically if present. Highlights:
+`<rom>.sav` is loaded automatically if present. The render runs as an engine job; the first Ctrl-C cancels it (exit 1,
+no WAV), a second kills it. Highlights:
 
 - **Automatic length** - with a loaded LSDj or risa `.sav` the song length is detected
   if the song ends with an `HFF` command, and the render trimmed to it. Otherwise pin a
diff --git a/docs/audit-2026-09-20.md b/docs/audit-2026-09-20.md
index bdd9c06ab..5e3e07005 100644
--- a/docs/audit-2026-09-20.md
+++ b/docs/audit-2026-09-20.md
@@ -108,7 +108,7 @@ Docs: `AGENTS.md` identifiers all resolve; its rot is numeric (counts). `spec/06
 - `EngineInvoker.cpp:74-82` `stageControllerMidi` sets `len = bytes.size()` but copies 4 bytes; `drainInto` (`:181`) reads `data..data+len` past the union for any message > 4 bytes. Only the RPC entry guards it.
 - `EngineInvoker.cpp:13-36` vs `EngineRpcService.cpp:87-101`: on a full command ring, `adoptSystem`/`replaceSystem` delete the freshly built core and `removeSystem` is dropped silently, after `constructSystem` already returned `true` to TS. `freePending` never deletes `stageSystemMidi.ext` (leak at teardown).
 - `NesEverdriveFifo.hpp:865-874,913-915,1169-1170`: `de.is_directory()` is the throwing overload, called inside a Mesen memory handler on the audio thread with `_mutex` held; an escape is `std::terminate` in the DAW. The rest of the file uses `error_code` overloads.
-- `HostSyncTrace.hpp:851,991` plain `static unsigned instances` incremented per `Engine` ctor; `RenderJobRegistry` constructs Engines on concurrent job threads (data race, diagnostic path only).
+- `HostSyncTrace.hpp:851,991` plain `static unsigned instances` incremented per `Engine` ctor; `RenderJobRegistry` (retired by D3; renders are now engine jobs on the offline pool's workers) constructs Engines on concurrent job threads (data race, diagnostic path only).
 - `HostRpcService.cpp:52-59,74-78` `readFile` sizes from `fs::file_size` then reads (TOCTOU); a mid-read error returns the partial prefix as success.
 
 **Hosts**
diff --git a/docs/sms-support.md b/docs/sms-support.md
index 28fd54518..e6d472db9 100644
--- a/docs/sms-support.md
+++ b/docs/sms-support.md
@@ -476,7 +476,7 @@ Both prove the queue contract (gate at offset, release in sorted order, rebase p
 
 **One more thing the leg needs that nothing budgets: the ROM must be driven into `SYNC: IN` through its own UI first.** `$DD` is untouched at boot; the ROM is not in a slave sync mode until the user sets it. That is a scripted button sequence in the fixture, not a config flag.
 
-**And `tools/run-sanitizer.sh thread`** over the new step loop is not addressed anywhere in the design. `MesenSmsSystem` construction on `RenderJobRegistry` worker threads is the same `mesenGlobalInit` hazard the first scoping pass flags, and TSAN is the only thing that finds it. ASan is *not* the thing that finds the teardown UAF (both frames are in the uninstrumented `libmesen.a`).
+**And `tools/run-sanitizer.sh thread`** over the new step loop is not addressed anywhere in the design. `MesenSmsSystem` construction on `RenderJobRegistry` (retired by D3; renders are now engine jobs on the offline pool's workers) worker threads is the same `mesenGlobalInit` hazard the first scoping pass flags, and TSAN is the only thing that finds it. ASan is *not* the thing that finds the teardown UAF (both frames are in the uninstrumented `libmesen.a`).
 
 ---
 
@@ -543,7 +543,7 @@ Ordered so each phase ends in an exit-zero. Items 3/4/5/6 are the diff from the
 | # | Item | Files | Effort | Risk | Why |
 |---|---|---|---|---|---|
 | 1 | `MesenSmsConfig.hpp` | new `packages/native/src/system/mesen/MesenSmsConfig.hpp` | trivial | low | Copy of `MesenGbaConfig.hpp` (45 lines). **Must be named `MesenSmsConfig`** - Mesen has a global `struct SmsConfig` at `SettingTypes.h:695`, the exact collision documented at `MesenGbaConfig.hpp:12-17`. Drop `biosPath`. Carries the model discriminator only as a boot-time selector, not a user knob (D1). The `using Tag = rfl::Literal<"gba">` at `MesenGbaConfig.hpp:20` is vestigial: `rfl::TaggedUnion` appears once repo-wide. Copy for symmetry or drop; do not build on it. |
-| 2 | `MesenSmsSystem.{hpp,cpp}` | new, in `mesen/` | **medium-large** | **medium** | Boilerplate is a structural clone of `MesenGbaSystem` (115 + 470 lines): `saveStateBytes` / `loadStateBytes` / `clearSram` / `clone` / `setGainDb` / `finishBlock` / `stateSnapshotSize` / `captureStateSnapshot` are literal copies. **`stepIfBelowTarget` is NOT** - see section 2.3, and D2. `onDeactivate` is **not** a literal copy either: it needs `emu_->Stop(false, true, false)` before `emu_.reset()` (bottom line (a)). `emu_->SetEmulationThreadId` in `prepareForBlock` (GBA does it at `:229`) or the parallel render path breaks; `mesenGlobalInit()` before constructing the Emulator or concurrent construction on `RenderJobRegistry` worker threads races. Gain: `gainSmoother_.next()` **once per sample frame across all lanes** (`MesenNesSystem.cpp:398-403`). One class serves both platforms; the `.sms`/`.gg` difference is config plus the sync transport. |
+| 2 | `MesenSmsSystem.{hpp,cpp}` | new, in `mesen/` | **medium-large** | **medium** | Boilerplate is a structural clone of `MesenGbaSystem` (115 + 470 lines): `saveStateBytes` / `loadStateBytes` / `clearSram` / `clone` / `setGainDb` / `finishBlock` / `stateSnapshotSize` / `captureStateSnapshot` are literal copies. **`stepIfBelowTarget` is NOT** - see section 2.3, and D2. `onDeactivate` is **not** a literal copy either: it needs `emu_->Stop(false, true, false)` before `emu_.reset()` (bottom line (a)). `emu_->SetEmulationThreadId` in `prepareForBlock` (GBA does it at `:229`) or the parallel render path breaks; `mesenGlobalInit()` before constructing the Emulator or concurrent construction on `RenderJobRegistry` (retired by D3; renders are now engine jobs on the offline pool's workers) worker threads races. Gain: `gainSmoother_.next()` **once per sample frame across all lanes** (`MesenNesSystem.cpp:398-403`). One class serves both platforms; the `.sms`/`.gg` difference is config plus the sync transport. |
 | 3 | `configureSms()` / `configureGg()` | `MesenSmsSystem.cpp` | trivial | **high** | Four zero-defaults, each a silent plausible-looking failure. **(a)** `ChannelVolumes[4] = {}` - see H4. **(b)** `Port1`/`Port2` default `ControllerType::None`, so no device is created, `pressButton` no-ops, **and** the `_controlDevices.size() > 0` guard in `SmsControlManager::UpdateControlDevices` (`SmsControlManager.cpp:50`) never fires, so every frame does `_deviceLock.AcquireSafe()` + `ClearDevices()` + two `shared_ptr` allocations on the audio thread. An RT-safety bug, not just dead input. **(c)** `RamPowerOnState = RamState::Random` (`SettingTypes.h:702`) noise-fills the always-allocated 32 KB cart RAM: measured `nonZeroBytes=32661` versus 0 with `AllZeros`, and the savestate goes **43686 -> 9202 bytes** (4.7x, the compressor cannot squeeze noise), feeding `stateSnapshotSize()` and every Duplicate. Also required for H5 determinism. **(d)** Overscan, item 5. **(e)** `EnableFmAudio` - see H3 and open question 3. Must run **before** `LoadRom` (region resolution depends on it, risk 10; and section 2.3's immutable-region limitation makes it the only chance). |
 | 4 | **ROM identity: stage a real file with a real stem** | `MesenSmsSystem.cpp` | small | **high** | Three consumers of the VirtualFile name conflict: model selection (`SmsConsole.cpp:46-59`), battery stem (`Emulator.cpp:591` -> `SmsMemoryManager.cpp:92-96` `LoadBattery`, unconditional for non-SG/non-CV), and `Reset()` (`SmsConsole.cpp:148-152` -> `Emulator.cpp:347`, re-reads from disk). Measured: a constant `"rom.sms"` makes Reset a **silent no-op** and collides every game's battery on one file. **Resolution: materialize the bytes to a per-system temp file named `<real-stem>.<sms\|gg>`** under the existing Mesen tmp dir (`MesenGlobalInit.cpp:13`, `MesenGbaSystem.cpp:43`) and hand `LoadRom` that path. Model correct, battery stem unique per title, Reset a real power cycle, `spec.romBytes` overrides survive Reset. D1 makes the extension choice trivial: it comes from `spec.platform`, not a sniff. |
 | 5 | Overscan + framebuffer size, **two configurations** | `MesenSmsSystem.{hpp,cpp}` | small | medium | **Measured: both models emit 256x240 with default (zero) overscan.** `SmsVdp.cpp:648` always emits a 256x240 `RenderedFrame`; `BaseVideoFilter::GetFrameInfo` (`BaseVideoFilter.cpp:33-36`) subtracts overscan; all three `SmsConfig` overscans default `{}` (`SettingTypes.h:717-719`) because Mesen's real per-console defaults live in the .NET UI you do not build. So SMS shows 24 black rows top and bottom and **GG shows a 160x144 image floating in a mostly-black frame** unless `GameGearOverscan` is `{48,48,48,48}`. Settled by D1: two platforms, two overscan configurations, two `FrameBufferTriple(width, height)` ctor dims. `MesenVideoDevice`'s min-clamp means a mismatch is a silently wrong picture, not a crash. |
@@ -563,7 +563,7 @@ Ordered so each phase ends in an exit-zero. Items 3/4/5/6 are the diff from the
 | 19 | Vendor the test ROMs | `resources/roms/` | trivial | low | `smsggdj_v0_45.{sms,gg}`, 131,072 bytes each, currently only at `/workspaces/resources/roms/smsggdj/`. Without them item 18 skips silently like `tools/author-risa-rplg.js:24-27` and CI proves nothing. MIT, `/workspaces/smsggdj/LICENSE`. `resources/` never ships (`release.yml`), so no notices row. |
 | 20 | The boot-and-render legs | `packages/retroplug/test-native/app-cores.test.ts` | trivial | low | `bootsAndRenders(SMS, "sms", warmupMs)` and the `"gg"` twin, mirroring `:49-50`. **Source from `__REPO_RESOURCES_DIR__`, not `__RESOURCES_DIR__`**, or they silently skip on CI exactly as GBA does. |
 | 21 | `sms-sync` role + `SmsSyncRole` | `dspRoles.ts`, `romProviders.ts`, `mesen/roles/SmsSyncRole.{hpp,cpp}`, `pluginControlPlane.ts:99-107` | **medium** | **medium** | Section 2.4. `SmsSyncRole` is `NesN8FifoRole` minus `flushAll`; the TS role is `risaSync` with a level encoder instead of a byte protocol. No PDC entry after all - measured, see the Reaper section. Needs item 12 first, or the marker role has zero header bytes to match on. |
-| 22 | Verification legs the plan must actually run | - | small | low | `pnpm -r typecheck` (`package.json:14`) is in **no** workflow step (`build.yml:67-77,169-179,333-343` run only `test` / `test:native` / `test:plugin` / `test:ui`), and item 11's `DEFAULT_CORE` change is a hard compile error nothing else catches. Plus `tools/run-sanitizer.sh` (`thread` for `mesenGlobalInit()` under concurrent construction on `RenderJobRegistry` worker threads, and for item 3b's audio-thread allocations). **Note ASan will NOT catch item 10.** |
+| 22 | Verification legs the plan must actually run | - | small | low | `pnpm -r typecheck` (`package.json:14`) is in **no** workflow step (`build.yml:67-77,169-179,333-343` run only `test` / `test:native` / `test:plugin` / `test:ui`), and item 11's `DEFAULT_CORE` change is a hard compile error nothing else catches. Plus `tools/run-sanitizer.sh` (`thread` for `mesenGlobalInit()` under concurrent construction on `RenderJobRegistry` (retired by D3; renders are now engine jobs on the offline pool's workers) worker threads, and for item 3b's audio-thread allocations). **Note ASan will NOT catch item 10.** |
 
 ---
 
````

Run: `sed -n '9712,9832p' docs/modular-graph-phase-d3-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean`: the reports record 76, and this plan is the 77th tracked Markdown file `check-doc-links` walks.

Run: `git diff --check`
Expected: no output: no trailing whitespace and no conflict markers.

Run: `git diff | grep "^+" | grep -c $'\u2014'`
Expected: `0`, and an exit status of 1, which is `grep -c`'s answer when nothing matches, not a failure: no added line carries an emdash. The report ran the same grep on the commit's diff after committing; before the commit, the working tree's diff is what carries the added lines.

- [ ] **Step 4: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M AGENTS.md
 M README.md
 m deps/sameboy
 M docs/audit-2026-09-20.md
 M docs/modular-graph-design.md
 M docs/modular-graph-phase-d2-design.md
 M docs/modular-graph-phase-d3-design.md
 M docs/sms-support.md
 M spec/01-architecture.md
 M spec/02-native-host.md
 M spec/03-ts-layer.md
 M spec/06-build-test.md
 M spec/07-remaining-work.md
 M spec/09-cli-debugging.md
 M spec/10-multichannel-audio-out.md
 M spec/11-ui-rendering.md
 M spec/README.md
````

````bash
git add -- AGENTS.md README.md docs/audit-2026-09-20.md docs/modular-graph-design.md \
  docs/modular-graph-phase-d2-design.md docs/modular-graph-phase-d3-design.md docs/sms-support.md \
  spec/01-architecture.md spec/02-native-host.md spec/03-ts-layer.md spec/06-build-test.md spec/07-remaining-work.md \
  spec/09-cli-debugging.md spec/10-multichannel-audio-out.md spec/11-ui-rendering.md spec/README.md
git commit -F - <<'EOF'
docs: D3 runs renders as engine jobs

The D3 design gets its status and a section 10 "As built": each stage's decisions, the section 9 questions answered
(fixture-free sound, the pool's safety with its three vendored fixes, the worker budget, the detection drift table,
teardown and editor reopen), the honest scope of chunk independence, and what D3 leaves. Sections 1 to 8 are corrected
where the build made them false (chunking, the budget, the carry).

D2's "Left for D3" says what D3 closed and how; the main design's Phase D and section 5.7 say D3 is built. The specs
describe the new architecture: spec/11 (engine jobs, the pool, the job list behind the badge, the verification
section, which no longer claims a thread-sanitizer pass that never existed), spec/01 and 02 (two job kinds, the RPC
table, 19 emulator and 91 union methods, Mesen's SimpleLock), spec/03 (engineRun), spec/06 (the render baseline and
its flags, the [engine-jobs] tag, the second SameBoy patch, the render-host-test legs), spec/07 (D3 done, its
residuals: CI sees only 5 of the 13 baseline renders, MIDI-driven renders), spec/10 and the README row. AGENTS.md gets
the render paragraph, a render-baseline paragraph beside the kit baselines and the engine-jobs tag. Older documents
that named RenderJobRegistry carry a one-line note.
EOF
````

---

### Task 6: the acceptance run

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. **There is no hardware leg**: D3 changes no console I/O, no cart path and no host device, so
nothing here needs a real NES or the N8 Pro. The counts are the tip verification at `c653a5fb3`, whose code is identical
to the final tip `b43b29127` (only docs changed after); its logs are `/tmp/d3pro/tip-*.log` and its summary is
`tip-summary.txt`: 30 legs, every one exit 0, both consumers, both kit goldens and the render baseline among them. A
line no log records says so and asks for the count instead. The tip ran neither the Reaper suite nor the tool's three
legs; the legs are quoted from Task 4's logs, and the Reaper suite runs here because the plugin's render path and the
engine's RPC surface changed.

- [ ] **Step 1: The tree and the build**

Run: `git status --short`
Expected: only ` m deps/sameboy`.

Run: `git -C deps/sameboy status --short`
Expected: ` M Core/apu.c`, ` M Core/apu.h`, ` M Core/camera.c`, ` M Core/random.c` and ` M Core/sgb.c`: the two
tracked patches, and nothing else.

Run: `cmake --build build -j$(nproc)`
Expected: a clean build of every target (Task 5 changed Markdown only). A full build reaches `[100%]` and prints a
`Built target <name>` line for every target; which one comes last depends on the parallel schedule, so do not pin it.

Run: `git log --oneline -6`
Expected: six lines: Task 5's `docs: D3 runs renders as engine jobs`, Task 4's `refactor(render): RenderJobRegistry and
the render hooks retire`, Task 3's `refactor(render): the CLI and the UI render through engine jobs`, Task 2's
`feat(render): an engine job renders in whole blocks, and the render baseline records it`, Task 1's `feat(engine):
engine jobs run a render on the offline pool`, then `docs: the D3 implementation plan`. The design commit 5c1d6a4bc is
the seventh.

- [ ] **Step 2: The suites, with and without fixtures**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 172 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`. Engine-graph without fixtures
`test cases: 304 | 293 passed | 11 skipped` (the tip's assertion line was `assertions: 1700018 | 1700018 passed`; that
total moves from run to run, the case line does not); graph-test `All tests passed (356156 assertions in 273 test
cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144
assertions in 29 test cases)`. The other six are at their own counts: `retroplug-midi-test` 35 / 19,
`retroplug-dynparams-test` 24 / 7, `retroplug-plugin-test` 21 / 6, `retroplug-lottie-test` 7 / 1,
`retroplug-classid-test` 10 / 3 and `retroplug-watcher-test` 11 / 3, from the tip log's sections. Report all ten lines.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (N assertions in 304 test cases)`: the same 304 cases with the fixture-gated ones running
(the tip recorded 1711945; the case count is exact).

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` for
the serial binary: D3 touches no n8 code, so both are the baseline's.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, every case run, nothing re-recorded, and `git status` shows no
parity baseline changed.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)` with `# 46 case(s) SKIPPED in 10 file(s)`, the skip check
passing: CI's view, as `s4fv-parityci.log` recorded it on Task 4's tree, whose code is the final tree's. (Without the
prefix the runner falls back to `../resources`, which exists beside this worktree and holds some fixtures, so a bare
run is not CI's view.)

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1),
nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check
clean (the log lists every file and its count; `render-baseline(3)`, `render-chunks(3)`, `render-cli(1)` and
`engine-run(1)` among them).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`, `render-job` among them.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:ui`
Expected: the same 41 files (a skip does not remove a file); report which cases skipped without the fixtures, which
no log records.

Run: `pnpm test:node`
Expected: `tests 12`, `pass 12`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`, including the real binary's render leg: `ok - the CLI's WAV is the
golden's size (705644 B)`, `ok - the CLI's WAV hashes to the golden's (c309fbacf1c2803a)` and `ok - an interrupted
render reports it was cancelled` among its lines.

Run: `pnpm check:docs`
Expected: `check-doc-links: 77 file(s) clean` (the prototype's 76 plus this plan, whose links resolve from docs/).

- [ ] **Step 3: The goldens, the render tests, and nothing re-recorded**

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, 3 passed, 0 skipped, 0 failed.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`: every edge bank against `kit-edges.json`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa
renders match the golden's hashes`, then 5 passed, 0 skipped, 0 failed: all 13 cases on the recording host with the
off-target fallback turned into a failure.

Run: `RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, then `# 2 passed, 3 skipped, 0 failed`, the skip check
passing: CI's share, as `s4fv-renderci.log` recorded it on Task 4's tree.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# HFF lengths at pulls 100, 50, 200 ms: 191700, 191750, 191700 ms`, then 5 passed, 0 skipped, 0 failed.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-cli`
Expected: 6 passed, 0 skipped, 0 failed, its last case `ok 6 - tools/fixtures/render-smsggdj.sav, which test:cli-ts
renders, is the battery the baseline's smsggdj cases author`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native engine-run`
Expected: `[engine-run] hff: most reported 191700 ms, length 191700 ms`, then 5 passed, 0 skipped, 0 failed, its last
case `ok 5 - offlineRun's handle cancels a graph job, which rejects with the reason cancelled`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff`
1, `risa-render-region` 1 and `sms-render` 6 passed: the in-process library tests, exact blocks.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0: `render-menu` and `render-badge` 1 passed each, then `pnpm test render`'s `# 5 test file(s) passed
(jobs=16)` (`render-jobs` 7, `ui-seam` 16).

Run: `git status --short`
Expected: only ` m deps/sameboy`: no render golden, no kit golden, no skip baseline and no parity baseline moved
during the run. A changed `test-native/golden/render-baseline.json`, `test/audio/golden/*.json` or
`scripts/skip-baseline.json` here means something re-recorded, which nothing in this step does.

- [ ] **Step 4: The sanitizers, on D3's tags**

D3's tags are `[engine-jobs]` (and D2's `[offline]`, whose pool it shares) in `retroplug-engine-graph-test`; the tip
verification ran them inside its wider engine-graph list, and D2's graph and kit legs ride along unchanged. Build each
binary in its own tree first.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the three binaries in the TSan tree.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (N assertions in 33 test cases)`, no ThreadSanitizer report: renders on several workers at
once, the budget, teardown with renders in flight. The case count is exact; the last logged run of this selection
(Task 2's tree) recorded 1685004, and the total moves from run to run.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag, so dropping it changes no case), then
`All tests passed (N assertions in 128 test cases)`, no report (the tip recorded 1697646).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263160 assertions in 79 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the four binaries in the ASan tree.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[engine-jobs],[offline]"`
Expected: `All tests passed (N assertions in 33 test cases)`, no AddressSanitizer or LeakSanitizer report (Task 2's
tree recorded 1667076).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (N assertions in 128 test cases)`, no report (the tip
recorded 1680115).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356156 assertions in 273 test cases)`: the whole graph binary under ASan, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

Run: `mkdir -p /tmp/d3-rht; for n in p1 p2 p4; do printf '{"rom":"%s","out":"/tmp/d3-rht/%s.wav","durationMs":1500}' /workspaces/resources/retro/lsdjtest.gb $n > /tmp/d3-rht/$n.json; done; printf '{"rom":"%s","out":"/tmp/d3-rht/cx.wav","durationMs":600000}' /workspaces/resources/retro/lsdjtest.gb > /tmp/d3-rht/cx.json`
Expected: exit 0: the sanitizer legs' job specs, on the LSDj test ROM, which renders silence; these legs look for a
sanitizer report, not bytes, so they need no sound.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-render-host-test --parallel "$(cat /tmp/d3-rht/p1.json)" "$(cat /tmp/d3-rht/p2.json)"`
Expected: exit 0, `render-host-test: up to 2 jobs ran at once`, no report. Run the single leg (`p4.json`: `job 1: done
rendered=1500ms outputs=1`) and the cancel leg (`--cancel` with `cx.json`: `ended cancelled`, 0 outputs, no
`cx.wav`) the same way, then all three in `build-asan` with `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1"`. These
are Task 4's legs, logged there; the tip ran none of them. Then `rm -rf /tmp/d3-rht`.

- [ ] **Step 5: retroplug-render-host-test's three legs**

Run: `cmake --build build --target retroplug-render-host-test retroplug-cli -j$(nproc)`
Expected: exit 0.

Run: `mkdir -p /tmp/d3-rht; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/one.wav","durationMs":3000}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/one.json; printf '{"rom":"%s","sav":"%s","out":"/tmp/d3-rht/ch","durationMs":3000,"split":"channels"}' /workspaces/resources/roms/lsdj942telemelt_5songs.gbc /workspaces/resources/roms/lsdj942telemelt_5songs.sav > /tmp/d3-rht/ch.json`
Expected: exit 0: two job specs for 3 s of telemelt (a fixture) with its battery, a mix and its four stems. Telemelt's working song (WORMHOLE) sounds: the prototype's stage 3 run of this song (the ROM and its battery copied side by side) through the same two paths (`/tmp/d3pro/s3work/host-mix` and `host-channels_*`) has 261520 of its 264600 mix samples nonzero, and pulse2, wave and noise sounding (pulse1 rests for these 3 s), so a byte that moves shows in the `cmp` below.

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/one.json)"`
Expected: exit 0, with `job 1: done rendered=3000ms outputs=1` and `render-host-test: up to 1 jobs ran at once`.

Run: `build/bin/retroplug-render-host-test "$(cat /tmp/d3-rht/ch.json)"`
Expected: exit 0, with `job 1: done rendered=3000ms outputs=4`.

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --out /tmp/d3-rht/cli.wav`
Expected: exit 0, its last line `rendered <rom> -> <out> (@44100Hz)` (printed with an arrow character).

Run: `build/bin/retroplug-cli render /workspaces/resources/roms/lsdj942telemelt_5songs.gbc --sav /workspaces/resources/roms/lsdj942telemelt_5songs.sav --duration 3s --split channels --out /tmp/d3-rht/cli-ch`
Expected: exit 0, its four `cli-ch_<stem>.wav` paths printed.

Run: `cmp /tmp/d3-rht/one.wav /tmp/d3-rht/cli.wav; for c in pulse1 pulse2 wave noise; do cmp /tmp/d3-rht/ch_$c.wav /tmp/d3-rht/cli-ch_$c.wav; done`
Expected: nothing: the mix and each of the four stems are byte-identical. The tool's job and the CLI's are the same engine job.

Run: `for n in r1 r2 r3; do printf '{"rom":"%s","out":"/tmp/d3-rht/%s.wav","durationMs":3000}' /workspaces/resources/retro/lsdjtest.gb $n > /tmp/d3-rht/$n.json; done; printf '{"rom":"%s","out":"/tmp/d3-rht/cx.wav","durationMs":600000}' /workspaces/resources/retro/lsdjtest.gb > /tmp/d3-rht/cx.json`
Expected: exit 0: the LSDj test ROM, silent, for the two legs that need no sound (overlap and cancel).

Run: `build/bin/retroplug-render-host-test --parallel "$(cat /tmp/d3-rht/r1.json)" "$(cat /tmp/d3-rht/r2.json)" "$(cat /tmp/d3-rht/r3.json)"`
Expected: exit 0, `render-host-test: up to 3 jobs ran at once`.

Run: `build/bin/retroplug-render-host-test --cancel "$(cat /tmp/d3-rht/cx.json)"`
Expected: exit 0, `cancel: job 1 ended cancelled (rendered=<N>ms, 0 outputs)` (Task 4's run logged `rendered=4300ms`; how far it got in 300 ms varies), and `test -e /tmp/d3-rht/cx.wav` then fails. Then `rm -rf /tmp/d3-rht`.

- [ ] **Step 6: The live hosts**

One at a time, with nothing rebuilding the plugin meanwhile. D3's tip verification ran the SDL smoke but not the
Reaper checks, so the smoke's line is quoted and the Reaper suite's is not: report it, and a failure here is a real
finding about a plugin host (the plugin lost its render hooks and the engine gained a job kind and an RPC) rather than
a missing figure. Make sure no Reaper or jackd of an earlier run is left first.

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

- [ ] **Step 7: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched. Never edit `/workspaces/bliptoaster` or `/workspaces/nesvj`: both commands copy the suite to `/tmp`, run it
there, and remove the copy, and both repositories' `git status --short` must stay empty. D3 changes how the CLI renders
(an engine job, Ctrl-C a cancel), so these are the checks that a consumer's CLI-driven suite still passes.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d3-bt && mkdir -p /tmp/d3-bt/build /tmp/d3-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/d3-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/d3-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/d3-bt/retroplug-cli/ && cd /tmp/d3-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/d3-bt; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed.

Run: `bash -c 'R=$PWD; rm -rf /tmp/d3-vj && mkdir -p /tmp/d3-vj/build /tmp/d3-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/d3-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/d3-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/d3-vj/retroplug-cli/ && cd /tmp/d3-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/d3-vj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 8: The final tree**

Run: `git status --short`
Expected: only ` m deps/sameboy`; no scratch file and no parity, kit or render golden or skip baseline changed.

---

