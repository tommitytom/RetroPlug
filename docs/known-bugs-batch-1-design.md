# Known bugs, batch 1

Five fixes from [spec/07](../spec/07-remaining-work.md)'s "Known bugs, recorded and not fixed" list, and one found
while looking at them. Each is small and local; they ship as one batch because they share a plan, a review cycle and
an acceptance run, not because they touch each other.

| # | Bug | Where | Found |
|---|-----|-------|-------|
| 1 | emu2413's VRC7 tables are built behind a plain flag: two VRC7 carts built at once race | `deps/mesen` | D3 |
| 2 | A Game Boy's press queue can drop a flood's last release, leaving a button held | `packages/sameboy` | C4 |
| 3 | A Game Boy button byte of 8 or more writes past SameBoy's key arrays in Release | `packages/sameboy` | this batch |
| 4 | A UI render's snapshot files are left behind when the instance ends first, or on a crash | `packages/native`, `packages/retroplug` | D3 |
| 5 | `__rp_syncLatencyMs` follows the stored sync mode, not the one MI.MAP forces | none (closed as unreachable) | C2 |

User decisions (2026-11-11): the button-byte bound joins the batch; the render snapshots are owned by the job pool,
with an age sweep for a crash's leftovers; the press queue keeps a reserve for releases (approach A below); the process
is a design, a plan with one task per fix, each implemented and given a spec review and an Opus code review, the kit as
acceptance and one Opus final review (no prototype branch).

## 1. emu2413's VRC7 tables

`deps/mesen/Core/Shared/Utilities/emu2413.cpp` builds its tables (`makeTllTable`, `makeRksTable`, `makeSinTable`,
`makeDefaultPatch`) the first time `OPLL_new` runs, behind `static uint8_t table_initialized`. Two VRC7 carts built at
once (the engine's offline pool beside the live engine, two render jobs) both see it clear and both write the tables
while either may read them. The NES game database's and the GBA dispatch tables' flags were made acquire/release in D3;
this is the one left.

**Fix.** The flag goes. `OPLL_new` calls `ensureTables()`, whose function-local `static const bool` initialiser runs
`initializeTables()`: C++ runs a block-scope static's initialiser exactly once and makes every other thread reaching it
wait until it has finished, which is what the double-checked atomics elsewhere in the tree spell out by hand. The change
carries a `// RetroPlug:` marker (46 marked files; AGENTS.md says 45 in two places).

**Test.** A `[mesen-statics]` case in `packages/mesen/native/test/MesenShared.test.cpp` calls `OPLL_new` on two threads
released together (the file's `onTwoThreadsAtOnce`) and frees both chips. Like the file's two existing statics cases it
proves the race only as the process's first build, so the tag is run alone in `build-tsan`. Before the fix lands, the
case is run under ThreadSanitizer against the old flag to show it reports the race.

## 2. The Game Boy press queue keeps room for releases

`SameBoySystem::pressButton` appends an edge (`{offset, button, down}`) to `pendingButtons_`, an
`rp::FixedQueue` of 1024 entries, each scheduled 10 ms (`buttonSpacingSamples_`) after the one before so the joypad
debouncer sees every edge; `stepIfBelowTarget` applies those whose offset has come and `finishBlock` carries the rest.
A Mesen console coalesces by button and applies its whole queue each block; a Game Boy's edges are meant to be seen in
turn, so they cannot coalesce (C4 design, section 11). The queue drains about 100 edges a second, so a session that
applies pushes inline between blocks (the CLI's timeline, an offline render) or a script node's buttons output
saturated at 64 a block can outrun it. What is past the bound is dropped and counted; if the last dropped edge is a
release, its button stays held until a later release arrives.

Three rules were weighed. **Coalescing redundant edges** (drop a press of a button already queued down) does not stop
an alternating flood and moves later edges' timing. **Rewriting the tail on overflow** breaks the rule that every edge
is seen. **A reserve for releases** (chosen) leaves everything below the bound as it is.

**Fix.**
- `std::uint8_t queuedDown_`, one bit a button: the state the button's last queued edge leaves it in (all up at
  construction; a rebuilt core is a new object).
- A **held release** (a release of a button whose bit is set) needs one free slot. Every other edge (a press, or a
  release of a button already up) needs more than `GameboyButton::Count` (8) free slots. Free slots are
  `capacity() - size()` (a push compacts the consumed prefix, so that is the room a push has).
- An edge refused by the reserve is counted in a new `pressesRefused_`, which `inputDropped()` adds to the queue's own
  `dropped()`, so the total keeps meaning "edges this core did not take". The bit changes only when a push succeeds.
- The invariant, stated in the code: free slots are never fewer than the buttons held. A press is taken only with more
  than 8 free, so after it at least 8 remain, and at most 8 buttons are held; a held release frees a held button for
  each slot it takes. So once a button's press is queued, its release always fits.
- Unchanged: the 10 ms spacing, the carry, the drain loop and the bound. Below the last 8 slots nothing differs, so
  parity and the `[alloc]` case (zero Game Boy drops at 64 presses a block) hold as they are.
- A test seam, `peekPendingPress(button, down)`, shaped like the Mesen consoles' (`MesenNesSystem.hpp`): the last
  queued edge for a button, if one is queued.

**Tests** (`packages/native/test/engine-graph/SystemNode.test.cpp`, `[input-drops]`):
- The existing "Game Boy presses" section (1028 alternating edges, `(i % 8, i % 2 == 0)`) has its expected accepted and
  dropped counts re-derived under the reserve, with the arithmetic in a comment.
- A new case: a flood of presses past the point where presses are refused, then a release of every held button; every
  release is accepted and each button's last queued edge is its release.

## 3. The Game Boy button byte is bounded

The buttons input on a system node (`SystemNode.hpp`, payload `[button, down]`) and the `pressButton` RPC hand the
byte to `SameBoySystem::pressButton`, which casts it to `GameboyButton` and later to `GB_key_t` for
`GB_set_key_state`. SameBoy checks the index with `assert` only (`deps/sameboy/Core/joypad.c`), so in a Release build a
byte from 8 to 255 (a script node's output, the SDK, a hand-edited document) writes `gb->keys[0][index]` and
`key_bounce_timing[index]` past their ends, inside the emulator's state. The Mesen consoles already map such a byte to
no button, and a test pins it.

**Fix.** In the same function, a byte of `GameboyButton::Count` or more returns before the queue: counted as accepted,
as on Mesen ("a byte it has no bit for is still a press"), taking no slot, adding no 10 ms spacing and driving nothing.

**Test.** The existing "a press of a byte the console has no button for drives no button" case gains a Game Boy
section: A down, the stray byte 200, A up; all three accepted, none dropped, A's last queued edge is up, two entries
queued.

## 4. Render snapshots are the job pool's to delete

`startSystemRender` (`packages/retroplug/ui/host/render.ts`) snapshots a system's battery (`.sav`) and, for a cart
without one or whose working song lives outside it, its state (`.ss0`) as `<configDir>/.render-src-<key>.<ext>`, the
key `<list token>-<job id>`. It deletes them in the job's `onSettled`, which `RenderJobs` (`src/renderJobs.ts`) runs when
the job's promise settles. An instance that ends first never hears its jobs settle: the native pool abandons a queued
job and cancels a running one at teardown, with no TS left to listen. A crash leaves them too. The config directory is
shared by every process and instance (the standalones, every plugin instance, the CLI), so a sweep that deleted every
`.render-src-*` could take a file a live job of another instance has yet to boot.

### 4.1 The pool deletes a job's files however it ends

- `EngineJobRequest` (`host/engine/OfflineTypes.hpp`) and `RpcEngineJobRequest` (`host/rpc/BackendTypes.hpp`) gain
  `discardFiles`, a list of paths, empty by default. TS's `engineRun(kind, spec, opts)` takes it as an option and puts
  it on the wire request. The CLI, `engine-run`'s tests and every other caller pass nothing, so a user's own
  `--sav`/`--state` is never touched.
- A `Job` holds the list in a small owner whose destructor removes each path (`std::filesystem::remove` with an
  `error_code`: a missing file or a failure is ignored, nothing throws), so every road that drops a job unrun (refused
  at `enqueue`, cancelled while queued, `~OfflineJobs` clearing the queue) deletes its files with it.
- A job that ran deletes them explicitly on its worker as soon as its body returns (done, error or cancelled, a running
  job at teardown included), before its completion is queued, so by the time TS hears a job settled its files are gone;
  the owner is empty after that. This keeps the promise `RenderJobs.start`'s comment makes ("the engine has let go of
  the job's files by then").
- `startSystemRender` passes its snapshot paths as `discardFiles` and no longer deletes on settle. Its `onSettled`
  deletes only when nothing was submitted (`spec(key)` or `engineRun` threw), the one case the pool was never handed the
  list. `RenderJobs` and its `onSettled` contract are unchanged.

### 4.2 A startup sweep for what a crash leaves

- `HostRpcService` gains `fileAgeMs(path)`: `file_time_type::clock::now() - last_write_time(path)` in milliseconds, or
  nothing for a missing or unreadable file. Measuring on the file clock itself avoids C++20's `clock_cast`, which
  libc++ and MSVC do not agree on. The TS `Backend`, `realBackend` and the mock backend gain it.
- `sweepRenderSnapshots(backend, configDir)` lists the config directory and deletes every `.render-src-*` older than 24
  hours; a file it cannot age or delete is skipped. It runs once in `createAppStores`, beside `bindings.ensureDefaults()`,
  so the plugin and both standalones run it. No live job's snapshot is a day old: a queued render waits behind other
  renders, which take seconds to minutes.

### 4.3 Tests

- Native, `[engine-jobs]`: a job's listed files are gone after it ends done, in error and cancelled, refused at submit,
  cancelled while queued (a pool of 1 with a long job ahead) and abandoned at teardown (the pool destroyed with jobs
  queued); an unlisted file beside them survives. `fileAgeMs` on a just-written file is small and not negative, and on
  a missing one is nothing.
- TS, `test/render/ui-seam.test.ts`: the snapshot cases assert the paths are handed over as `discardFiles` rather than
  deleted by the UI; a spec that throws still has its snapshots deleted; two queued renders each hand over their own.
- TS, a sweep test over the mock backend's ages: an old `.render-src-*` goes; a fresh one, an old file of another name
  and a directory stay.
- `test:ui`: a render run end to end in the UI harness leaves no `.render-src-*` once it settles, and none when the
  instance is torn down mid-render.

## 5. The latency report's stale mode: closed as unreachable

`__rp_syncLatencyMs` (`packages/retroplug/src/syncLatency.ts`, read only by `PluginDSP.cpp`) asks each placed role's
`hostLatencyMs`, and the LSDj sync role answers from its stored mode, not the one a Launchpad controller app forces
(`controllerSyncOverride`). Since F5a the plugin product's list (`package.json`'s `retroplug.products.plugin`) leaves
out `launchpad` and `lsdj-launchpad`, and that one product covers every plugin binary, the DPF JACK standalone included
(`packages/native/cmake/plugin.cmake`). No binary that reports a latency to a host can have a controller force a mode.
spec/07's entry is closed with that reason and a tripwire: a control surface joining the plugin's list must make the
report ask the placed mode.

## 6. Docs

- spec/07: the five checklist items ticked with their commits; item 3 recorded and closed; the C4 entry on the press
  queue states the reserve rule; the D3 snapshot entry says the pool deletes and the sweep covers a crash; item 5's
  entry closed as in section 5.
- AGENTS.md: the Mesen marker count, 45 to 46, both places.
- This document gains an as-built section and an execution record once the batch lands.

## 7. Plan, execution and acceptance

**Tasks**, each test-first (the new test seen failing before its fix):
1. emu2413's tables (section 1).
2. The press queue reserve and the button-byte bound (sections 2 and 3): one function, one test file.
3. The pool's `discardFiles` and the UI's handover (section 4.1).
4. `fileAgeMs` and the startup sweep (section 4.2).
5. Docs (sections 5 and 6).

Implementers on the pi models; a spec review and an Opus code review per task; an Opus final review of the batch.
Work files in `.superpowers/kb1plan` and `.superpowers/kb1exec`.

**Acceptance**, on the tip:
- The parallel kit (`.superpowers/verify/tip-verify-par.sh`), every SDL, Reaper and PipeWire leg under the device guard
  as always; nothing in this batch opens a device.
- Strict parity with fixtures and `RP_FAIL_ON_SKIP=1`: nothing moves and nothing is re-recorded (a moved case would
  mean the reserve reached a run that never neared the bound).
- ThreadSanitizer: `[mesen-statics]` alone in `build-tsan`; `[engine-jobs]` and `[input-drops]`.
- The render cases of `test:plugin`, `test:native`, `pnpm test` and `test:ui`, and the CI view of `test:native`.
- BlipToaster and NESVJ through the rebuilt CLI.

## 8. As built

The five tasks landed as planned, each tree identical to the one the plan's author validated, and two follow-up
passes then took the per-task reviews' and the final review's findings. What differs from sections 1 to 7:

- **Section 1: the race was wider than VRC7.** Every SMS/GG console builds an `SmsFmAudio`, and so an `OPLL_new`,
  FM or not, so two SMS/GG consoles built at once raced on the same tables. The fix covers them; the comments, the
  `[mesen-statics]` case (renamed "the OPLL sound chip's tables can be built on two threads at once, the first in the
  process") and spec/07 say so. The vendored file is C-style, so its marker is `/* RetroPlug: */`; AGENTS.md now counts
  `RetroPlug:` markers by `grep -rl RetroPlug: deps/mesen` (46).
- **Section 3: the RPC narrowed first.** `EngineRpcService::pressButton` took a `uint32` and cast it to `uint8`, so
  260 reached the core as 4 (A) before the core's bound could see it. A value above 255 now becomes 0xFF, a byte no
  console's table names.
- **Section 4.1: TS deletes too.** The pool deletes a job's listed files on every road through it, but a render held in
  TS's own queue past the 64-job bound (`src/offline.ts`) never reaches the pool, and a drain that throws fails jobs
  the pool may still run. So `startSystemRender`'s settle deletes its snapshots as well, on every road it settles on (a
  second removal is a no-op; each job's key is unique), and the design's "no longer deletes on settle" is not what was
  built. `RenderJobs.start` gained an optional fourth parameter, the `discardFiles` list, called after `spec`.
- **Section 4.1: the pool deletes only snapshot names.** `JobFiles` keeps a listed path only if its file name starts
  with `.render-src-` (`kRenderSnapshotPrefix`, the native twin of TS's `RENDER_SNAPSHOT_PREFIX`), so no bug in a
  caller can make the pool delete a user's own battery or state file. An unknown kind's refusal deletes its list as
  every other refusal does. Each removal is wrapped so a throw from building a path cannot terminate a host, and no
  removal or `Job` destruction runs under the pool's lock (a `cancelling_` count keeps a job that is between its queue
  and its completion counted as in flight, so a drain racing a cancel does not report it lost).
- **Section 4.1: a render refuses a vanished snapshot.** The UI's spec sets `inputsMustExist`, and `runRenderJob`
  refuses such a spec whose `sav` or `state` names no file, before it boots, so a snapshot the sweep or a settle took
  costs that render an error instead of a silent render from blank battery RAM. The CLI never sets it, so its `--sav`
  for a file not yet written is unchanged. A narrow window stays: a job past its check whose snapshot goes before its
  boot reads it.
- **Section 4.2: the sweep is isolated, and its clock is wall time.** It runs in `composeAppStores` (the design said
  `createAppStores`), wrapped so a failure warns and the app still starts. `file_time_type::clock` follows the wall
  clock, so a suspend longer than a day or a clock jump on a device with no real-time clock can age a queued render's
  snapshot past the bound; with `inputsMustExist` that render fails rather than renders wrong. The module comment
  says so.
- **Section 4.3: tests moved.** The native `fileAgeMs` check is under `[host]` in `retroplug-audio-test` (beside
  `HostRpcService`'s other cases), not `[engine-jobs]`. The `test:ui` cases (a render driven through the menu leaving
  no snapshot) were not written: the UI harness's render cases start a spec directly, so the chain is held instead by
  `ui-seam.test.ts` (the handover and the settle's deletion on every road, a held render and a thrown submission
  included), the native `[engine-jobs]` cases (every road through the pool, and a file of another name surviving each)
  and the real-RPC `engine-run` and `fs-config` cases.
- **Found and closed here, beyond the list:** `GameboyButton` bytes of 8 or more (section 3), the RPC narrowing above,
  and a refused push that `inputDropped()` would have counted twice (unreachable today).

## 9. Execution

- Design 99e1c04d5, plan 67f1b0368 (written and validated by DeepSeek against the tree, then replayed from its own text
  to the identical tree; no prototype branch).
- Tasks, by Qwen, each tree checked identical to the validated one and Opus-reviewed: 0f322f0a6 (emu2413), 431e20007
  (the press queue and the byte), 5602a0c06 (`discardFiles`), 219f5389e (`fileAgeMs` and the sweep), f74103578 (docs).
- Follow-up A (DeepSeek, the 31 per-task findings): 51a37bf5d, 1c7d6af49, 02287970a, 4253ec155. Opus final review at
  4253ec155: no Critical, one Important (an implicit narrowing MSVC `/W4 /WX` would refuse in the RPC's byte cast).
- Follow-up B (DeepSeek, the final review's findings): 69a6e1c5e, a1997ccf4.
- Acceptance: the serial kit at 4253ec155, 45 legs, all exit 0 (strict parity with fixtures and `RP_FAIL_ON_SKIP=1`,
  nothing re-recorded; TSan and ASan; BlipToaster 57/57 and NESVJ 19/19; the guarded SDL legs, SDL PipeWire 27/0/0, no
  card playback node, video or serial device opened). Re-run on the final tree after follow-up B: the parallel kit and
  `[mesen-statics]` alone under ThreadSanitizer: 47 legs exit 0 (`sdl:controller` skipped by the parallel kit's rule; it
  passed in the serial run), BlipToaster 57/57, NESVJ 19/19, SDL PipeWire 27/0/0, and the OPLL case clean.
- Owed: no Windows `/W4 /WX` or macOS build has seen this batch (the first CI run of the branch covers it).
