# Phase D3: renders as engine jobs

**Date:** 2026-11-02 · **Branch:** `feature/modularize` · **Status:** built (D3, five commits); section 10 records the build,
answers section 9 and carries what was left, and sections 1 to 8 are corrected in place

D2 ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md)) built the offline job seam: a native pool that
runs a graph document to completion on a host of its own, with a TypeScript promise side, and moved the kit compile onto
it. D3 moves the renders onto the same seam. The CLI's `render` and the UI's background renders become a second kind of
job, the engine job: a bare QuickJS runtime and an `Engine` of their own on a pool worker, running the unchanged render
library. One pool, one RPC surface, one pump and one cancel serve kit compiles and renders alike, and the CLI and the
UI render through literally the same code. The sample stream inside a render stops depending on how the render library
chunks its pulls. Phase D's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).

## 1. Before D3

- **Renders already run offline.** `RenderHost` is a bare QuickJS runtime, an `Engine` of its own and the in-process
  RPC bridge, running the render-worker bundle (`packages/retroplug/src/render/worker.ts`) synchronously;
  `RenderJobRegistry` gives each UI render a `std::thread` of its own and lives as a process global in
  `plugin/PluginUI.cpp`, reached through the direct `__rp_startRender`, `__rp_cancelRender` and `__rp_getRenderJobs`
  hooks, which the tile badge polls every frame. The CLI's `render` runs the same library (`runRenderJob`,
  `packages/retroplug/src/render/render.ts`) in-process over its own session.
- **A render is interactive.** Between chunks the library presses buttons (risa's Start and Select), reads emulator
  memory to find the song's end (LSDj's NR52, risa's sequencer flag, smsggdj's work RAM), writes memory (smsggdj's
  live song load) and streams each chunk to WAV, trimming at the detected stop. It cannot be a fixed document run for a
  fixed frame count.
- **D2's graph jobs lack what a render needs** (D2 design section 8): their registry is `rp.core`, `rp.dsp` and
  `rp.dsp.resample` only, with no system nodes, ROM or battery resources or script packs; their transport is fixed;
  they take no events; they return buffers only at the end; TypeScript cannot cancel one.
- **A render's block grid followed the chunking.** The library pulls audio in 100 ms chunks and `renderAudio` runs each
  as 1024-frame blocks plus a short remainder (1024, 1024, 1024, 1024, 314 at 44.1 kHz). The transport advance, the
  protocol script nodes, the snapshot publishes and the MIDI-out gathering happen per block, so a node that consumes
  the transport or MIDI each block (the host sync of smsggdj, `rp.mgb.midi`) put the short block at each chunk end in
  the bits. A render with no such node never depended on it: the cores emulate the same whatever the block lengths.
  No golden recorded render bytes; `retroplug-render-host-test` held the UI worker byte for byte to `retroplug-cli
  render`, and the render tests check levels and detection.

## 2. Decisions taken

The user's, on 2026-11-02:

- **Unify on one seam.** Renders become a second kind of offline job, driven through the same submit, drain and cancel
  RPCs, pool and TypeScript promise side as the kit's graph jobs; `RenderJobRegistry` and the CLI's in-process render
  path retire. Not graph-level render jobs (system nodes, events and streaming added to D2's graph jobs), not speed
  and streaming only, and not closing D3 as already met.
- **Chunk-independent samples, recorded after the move.** A render job runs continuous 1024-frame blocks whatever chunk
  the library pulls, so the sample stream no longer depends on the chunk size (section 4 gives the scope); the bits of
  the renders that act between calls or run such a node move once, and a new render baseline is recorded after the
  move. Not identical to
  today, not a staged proof first, and not cross-path equality only.
- **The job hosts its own runtime.** An engine job carries a QuickJS runtime and an `Engine`, as `RenderHost` does, and
  runs the unchanged synchronous render library on its worker. Not TypeScript driving a remote engine session from the
  caller's runtime (it would put render work on the plugin's UI thread), and not an autonomous native recipe (every
  tracker's end detection rewritten in C++).
- Every prototype stage is reviewed by Opus; every role runs on Anthropic models.

## 3. Engine jobs

**One seam, two kinds.** D2's pool runs graph jobs and engine jobs. A graph job still plans against the pool's own
immutable registry of built-in packages and needs nothing from the engine (D2 section 11). An engine job builds its
own `Engine`, which carries system nodes, ROM and battery resources, the protocol script packs, a real advancing
transport, staged input and the snapshot publishes: a render is the engine's full live path run on a worker. Nothing
new reaches a graph job's host.

**Submitting.** `submitEngineJob({ kind, spec })` returns a job id or a refusal at once. `kind` names the embedded
script bundle the job runs; D3 has one, `"render"`, the render-worker bundle, and builds no other speculatively. `spec`
is the render job JSON the worker reads today. Completions drain through D2's queue: `done`, `cancelled` or `error`, a
message, and the files written. Each drain also reports every running job's progress (milliseconds of audio rendered)
and the lines the job's script wrote to its console since the last drain.

**Running.** A pool worker builds a `RenderHost` on its own thread, runs the job in the default floating-point
environment (as graph jobs do) and tears the host down on that thread. Cancel is the existing cooperative check,
polled per chunk; an engine's teardown cancels its engine jobs and joins them.

**Sharing the pool.** A render is a full emulator running flat out for seconds or minutes; a kit compile takes
milliseconds. Engine jobs hold at most all but one of the pool's workers at once (one, for a pool of one), the rest
queue first in, first out, so a kit compile always finds a worker: a pool of one starts a second thread for a job of
either kind that arrives while one of the other holds the first, a graph job beside a render or a render beside a graph
job. Both kinds count toward the one bound on outstanding jobs.

**Ownership.** A render belongs to its plugin instance's `Engine`, not to the process: it survives the editor closing,
as today, and an instance's teardown cancels it, where `RenderJobRegistry`'s threads ran on into static teardown.

## 4. Continuous blocks

Inside an engine job the engine's `renderAudio`, `renderAudioPerSystem` and `renderAudioPerChannel` always run whole
1024-frame blocks: a call returns the frames asked for and keeps the surplus for the next call, and the surplus is
dropped when the job ends. The carry is keyed on the call's shape (mix, per system or per channel), the per-channel
system id, the stream count and the rate; a call of another key drops it, as does `renderBlocks`.
Between the library's chunks the emulator has therefore run up to 1023 frames past the audio handed back, so a button
press lands at the next block boundary after that point, and an end-of-song read sees state up to 1023 frames later
than the audio returned. Both are deterministic and are in the new baseline; detection precision stays the 100 ms
chunk.

What this makes independent of the chunking, and what it does not:

- The sample stream of an engine job no longer depends on how the library chunks its pulls. Only a render with a node
  that
  consumes the transport or MIDI each block (smsggdj's host sync, `rp.mgb.midi`) depended on it; a running transport
  with no such node did not, and the others, tracker renders included, were already independent in exact blocks.
- Acts the library takes between calls (button presses, smsggdj's work-RAM load, the detection reads) land at the next
  block boundary after the call before them. They are deterministic but tied to the lengths of those calls, which a
  fixed-length render times with calls of its own lengths, so every fixed-length render is pull-independent end to end.
- An auto-detect render polls once per pull, so its samples agree at any pull but its length is quantised to the poll.

Outside engine jobs `renderAudio` keeps its exact splits, so parity, which drives it with its own, is untouched.

## 5. The TypeScript side and the callers

**The promise side.** `backend.engineRun("render", spec, { onLog, onProgress })` is a promise of `{ status,
message, outputs, renderedMs }`, settled through D2's shared pump: the native wait in a host with no UI tick (the CLI,
the Node addon, the
test hosts), the UI frame tick in the plugin and the SDL standalone. Its handle has `cancel()`, and `offlineRun`'s
gains the same (D2 left TypeScript cancel out). A job's console lines arrive with its progress, and the CLI prints
them as they come, as it does today.

**The CLI.** `retroplug-cli render` parses its arguments into the same spec, submits an engine job to its own engine
and awaits it, instead of running `runRenderJob` over its session. `--list-songs`, which is not a render, stays
in-process. The command's output and exit codes stay what they are, but for Ctrl-C and the cores' stdout
interleaving (section 10).

**The UI.** System > Render submits the job through the stores' backend, not `audioDriver`, tagged with the system it
came from; the tile badge reads a small list of running render jobs kept from the pump's progress, replacing the
`__rp_getRenderJobs` poll; tapping the badge calls `cancel()`; finished jobs auto-dismiss. With the editor closed a
render runs on, its completion waits in native's queue until the editor pumps again, and its WAV is on disk either way.

**Projects.** Renders are not rejected when a project closes or loads: a render boots a fresh system from files on
disk and depends on nothing in the project, as today; graph jobs behave the same.

**Every host.** The Node addon and the SDL standalone bind the same facet, so they get renders through the same RPCs.

## 6. Tests

- **The render baseline** (new, test-native, beside the kit baselines): a fixed matrix of renders, each output WAV
  hashed: mix and stems on each console, fixed-length and HFF auto-detect, a song picked by name and by index, risa's
  region, smsggdj. Recorded once after the move (`RP_RENDER_GOLDEN_UPDATE=1`), refusing silence as parity's recording
  does; exact bytes compared only on `linux-x64-gnu13` with a require flag for CI's linux-x64 job; the fixture cases
  (LSDj, risa) count only on a run with fixtures and `RP_FAIL_ON_SKIP=1`, and CI runs the fixture-free subset.
- **Chunk independence:** one render pulled at three different chunk sizes gives identical bytes, for the renders the
  scope in section 4 covers; an auto-detect render's samples agree and its length is held to within a pull.
- **The seam:** submit, progress and console lines, cancel from TypeScript for both kinds, an engine torn down with
  renders in flight, a kit job getting a worker while renders hold the rest, under TSan and ASan.
- **Moved:** the UI's `render-menu` and `render-badge` tests onto the job path; `retroplug-render-host-test` reworked to
  drive engine jobs, keeping a parallel and a cancel leg.
- **Unchanged:** the existing render tests (`audio-render`, the risa and sms renders, `render-guard`), parity bit for
  bit, the kit goldens, the CLI's render output and exit codes (`test:cli-ts`; Ctrl-C and the stdout
  interleaving aside, section 10), both consumer suites.

## 7. Landing

Five commits, each green:

1. **Engine jobs in the pool**: the `"render"` kind with `RenderHost` as its body, progress and console lines in the
   drain, the worker budget, `engineRun`, TypeScript cancel for both kinds, and their tests. `RenderJobRegistry` and
   the CLI's path still run renders.
2. **Continuous blocks** inside engine jobs, the chunk-independence test and the render baseline, recorded through
   engine jobs.
3. **The switch-over**: the CLI's `render` and the UI's menu and badge on engine jobs, held to that baseline.
4. **Retirement**: `RenderJobRegistry`, the `__rp_*Render` hooks and the CLI's in-process render path deleted;
   `retroplug-render-host-test` on engine jobs.
5. **Docs.**

As D2, on Anthropic models: a prototype on a local `proto/` branch, its stages 1 (the engine job's threading and
lifetimes) and 2 (the block driver, where the new bytes are made) built by Opus and the rest by Sonnet, every stage
reviewed by Opus and fixed after its review; the plan generated from the prototype by Opus and replayed, then an Opus
plan review; Sonnet implementers and follow-ups, Opus code reviews of tasks 1, 2 and 4, a Sonnet acceptance run, an
Opus final review.

## 8. Left beyond D3

- A render as an autonomous recipe (a document, an input schedule, a native stop rule and an output spec, run with no
  script per chunk), when user-built sample graphs need the same shape.
- The kit-patcher latency follow-up (spec/07): chaining a kit slot's passes inside one job so only the final buffer
  crosses into TypeScript.
- MIDI-driven renders (an mGB or BlipToaster render fed a MIDI file), which the render library does not do today.
  Section 10 lists the rest D3 left.

## 9. Prototype questions

- Which fixture-free renders make sound (authored songs on the repo's ROMs), so CI's share of the render baseline is
  not all silence?
- Is a QuickJS runtime and an `Engine` per job safe on a reused pool thread, with no thread-local state left behind
  (LVGL's and QuickJS's class ids, Mesen's scratch paths)?
- What are the right numbers for the worker budget, and does a render queued behind others need a fairness rule?
- How far do the HFF end frames move against today on the fixture songs, and does any detection change outcome?
- What happens when a plugin instance is torn down mid-render, and when an editor reopens on renders that finished
  while it was closed?

## 10. As built

Five commits on `feature/modularize`: engine jobs in the pool, continuous blocks and the render baseline, the
switch-over, retirement, and these docs. Parity bit for bit, the kit goldens and both consumer suites were untouched
throughout; the render baseline is the only golden D3 records. Sections 1 to 8 are corrected in place where the build
made them false (chunking, the worker budget, the carry's rules).

### Engine jobs in the pool (commit 1)

- **Wire.** `submitEngineJob({kind, spec})` joins the emulator facet (19 methods, 91 in the union); the spec crosses as
  a JSON string; the answer is D2's `RpcOfflineSubmitResult`. `drainOfflineJobs` grows `engineCompletions` and
  `engineProgress` beside the graph `completions` and `inFlight`. `kind` must be exactly `"render"`; anything else is a
  refusal naming it, and a bad spec is the job's own error (`render worker: job spec missing 'rom'`), not a refusal.
- **The worker budget** (design question 3). `engineBudget()` is all but one of the pool's size (one for a pool of one);
  graph jobs run on at most `size` and the pool's threads on at most `maxWorkers()`, which is `size` or 2 for a pool of
  one. A worker takes the oldest job its kind's limit allows, so renders wait first in, first out behind renders and a
  graph job starts past queued renders; a worker is started only when the runnable queued jobs outnumber the workers not
  running one, so a render held back by the budget starts no thread. No further fairness rule was needed. The pool's
  size is a constructor argument (0 means `concurrency()`) so tests size it.
- **A job's lifetime is one worker's stack frame.** The worker pins the default floating-point environment, builds a
  `RenderHost` (a bare QuickJS runtime), whose `run()` builds the job's own `Engine`, `QueuedInvoker`, four RPC services
  and in-process server, evaluates the embedded render bundle synchronously and returns; everything dies on the worker
  before it takes the pool's lock to publish. Teardown sets `stopping_`, drops queued jobs, cancels running ones and
  joins; a running render sees the cancel at its next chunk, so nothing a render touches outlives the pool, which is
  reset before any other `Engine` member. Lock order is the pool's mutex, then a record's lock; no lock is held across
  `RenderHost::run`.
- **A render's JS stack.** A pool worker is a plain `std::thread` (512 KiB of stack on macOS, 1 MiB on Windows), so a
  `RenderHost`'s runtime is held to 256 KiB as `ScriptHost`'s are (`RenderHost::kStackBytes`), leaving the engine and
  its cores, which run below the render's JS on the same stack, the other half. A recursion past it is a RangeError the
  job reports as its error, where QuickJS's default limit of 1 MiB ran off a macOS worker's stack; an `[engine-jobs]`
  case parses a spec nested a hundred thousand deep on a 512 KiB thread. Measured over the baseline's LSDj, risa and
  smsggdj renders, the JS needs about 68 KiB of it (116 under ASan) and the whole thread peaks at 117 KiB (186).
- **Console and progress.** The job's script console routes into a per-job `EngineJobRecord`, bounded between drains
  (256 lines, 64 KiB; past either a line is dropped and counted, and the count rides the next report, which TypeScript
  prints as `[N console lines dropped]`). A line is one console write, which can hold newlines. Native diagnostics
  printed straight to the process (Mesen's iNES log) still go to stderr. Every running job is reported at every drain;
  `onProgress` fires when the value moved and is a running maximum, because an auto-detect render lowers the raw count
  once, to the length it wrote; the result's `renderedMs` is that length.
- **Cancel wins** (D2's rule, kept for both kinds): a cancel that returned true makes the outcome `cancelled`, even if
  the render finished first, and such a completion keeps its `outputs`. TypeScript: `engineRun` resolves with all
  three statuses and rejects only on a refusal or a port that throws; `offlineRun`'s promise rejects with
  `OfflineRunError("cancelled")`; a run still held on the TypeScript side settles at once on cancel; `cancel()` answers
  false once settled. A settle that a callback re-enters returns without draining, and the lost-job check considers only
  the jobs pending at its drain.
- **Process-wide state a render touches** (design question 2). A QuickJS runtime and an `Engine` per job on a reused
  pool thread is safe, after three fixes found by ThreadSanitizer and by reading the cores. Mesen's
  `MessageManager` lock (`SimpleLock`, vendored in `deps/mesen`, not a submodule) read its holder and count as plain
  integers while another thread built a core; they are relaxed atomics now. SameBoy's power-on `GB_random` seed, the
  camera's noise seed and the SGB's lazily built `fm_sin` table are process statics a second Game Boy core raced on:
  `cmake/patches/sameboy-process-statics.patch` (the second tracked SameBoy patch, applied by `cmake/sameboy.cmake`
  after the per-channel one, with the same idempotent and fail-loudly rules) makes the first two relaxed atomics and the
  third a load-time constructor, as `apu.c`'s own tables are. Nothing else is shared: QuickJS keeps no per-thread
  state, Mesen's scratch paths are numbered by atomic counters, `SnapshotRegistry` is per `Engine`, and r8brain's
  statics outlive every pool. The pool header lists this explicitly.
- **Teardown mid-render and editor reopen** (design question 5). An engine's destruction cancels at the next chunk and
  joins; completions not drained go with the pool; a render cancelled mid-render has its WAV removed (commit 3). With
  the editor
  closed, completions wait in native's queue (counting toward the 64) and the record bound caps the lines; the
  control-plane runtime's queue still holds the promise, so the next pump after the editor reopens settles it.

### Continuous blocks and the render baseline (commit 2)

- **Where they live.** `EngineRpcService::setContinuousBlocks(bool)` is C++ only, not an RPC. All three render calls
  share one private `pull` loop; the carry (`{shape, id, rate, frames, streams}`) is dropped by `renderBlocks` and by a
  call of another key. The emulation behind dropped frames is not undone: the next audio starts at the next block.
  Exact mode runs the old splits, so parity is untouched. `RenderHost` always sets continuous (commit 4); the exact path
  stays for the in-process library tests. A render assumes nothing else advances its engine between calls: the audio
  driver's thread and the debug facet's stepping run the cores outside them and keep the carry, and no render uses
  either.
- **The settle shape.** The library settles 1500 ms with a mix call and then streams a split render per channel. Under
  the drop rule that would start a split render's stems up to a block after its own mix, so a split render settles in
  the stream's shape. Making that unconditional moved the CLI's NES stems byte for byte, which is why commit 2 gated it
  on the engine and commit 4 removed the gate once only engine jobs remained. A NES's settle shape moves its stems:
  mix against per-channel changes every later sample and diverges triangle and dmc seconds in, so the per-channel shape
  is emulation timing for a NES, not only routing. A Game Boy is unaffected. Pinned by a case that sums an LSDj render's
  stems against its mix at lag 0 (red with the old settle, lag -410).
- **The chunk knob.** `RenderOpts.pullMs` (default 100, a test knob the CLI never sets) changes only the stream's pulls;
  the play gestures (100 ms), smsggdj's load steps (250 ms) and the settle (1500 ms) keep their own lengths.
- **The honest scope of chunk independence** (the narrowing of sections 1, 4 and 6). Measured with `render-chunks`
  (5 cases) and a mutation that forces engine jobs back to exact blocks:
  - Only a render with a node that consumes the transport or MIDI each block depended on the chunking (a running
    transport with no such node did not): the host-synced smsggdj render
    differs between pulls of 100, 37 and 250 in exact blocks and is identical in continuous ones; under the mutation
    exactly that case fails.
  - Everything else was already independent: the smsggdj fixed render, LSDj stems with Start pressed and risa's pins
    with three gestures were identical at all three pulls even in exact blocks.
  - Acts between calls land at the next block boundary after the call before them, so they follow that call's length by
    construction; they are independent of `pullMs` only because their calls have lengths of their own.
  - An auto-detect render (LSDj and risa by default) polls once per pull. MACHETE at pulls of 100, 50 and 200 has
    byte-identical samples over the common length and ends at 191700, 191750 and 191700 ms. The test asserts the samples
    agree and the lengths differ by less than the longest pull. Making detection pull-independent would need the
    block-granular probes of the autonomous-recipe direction (section 8).
- **The render baseline** (`packages/retroplug/test-native/golden/render-baseline.json`, `render-baseline.test.ts`,
  the cases in `render-cases.ts`). 13 cases, each rendered through `engineRun` and hashed with the kit baseline's
  FNV-1a-64 over the whole WAV (header included): 5 fixture-free, 4 LSDj, 4 risa.
  - Fixture-free: smsggdj playing songs authored into its battery (METRO, HIGH, CHORD) as `smsggdj/sms/index-0/mix`,
    `smsggdj/sms/name-chord/mix-48000`, `smsggdj/gg/index-1/mix` and the host-synced
    `smsggdj/sms/sync-in24/transport-120bpm`, plus
    `sameboy/mgb-sgb/no-start/mix` (mGB as a Super Game Boy, whose boot jingle sounds with no MIDI).
  - LSDj fixtures: tripledipper fixed 5 s (mix and 4 stems) and telemelt's MACHETE auto-detected by name (mix) and by
    index (4 stems). Risa fixtures (risa-2.2.1-pal.nes): ecoli_soul HFF on PAL (59000 ms) and on NTSC through a carried
    region role (49800 ms), and blumarbl fixed 3 s as channels and pins. There is no GBA case: the repo's GBA ROM
    renders exact silence without a BIOS.
  - Rules: length and sizes are compared everywhere; hashes only on `linux-x64-gnu13`, elsewhere a case renders twice
    and must agree with itself; `RP_RENDER_GOLDEN_REQUIRE_TARGET=1` makes a foreign host fail (CI's linux-x64
    `test:native` step sets it); `RP_RENDER_GOLDEN_UPDATE=1` is refused off the target, for a self-disagreeing case and
    for a run missing any case; every case and file must sound, except the files a case lists as silent (blumarbl's
    first 3 s leave square1, triangle and dmc silent). The test-native runner sets `RETROPLUG_DETERMINISTIC=1` for
    `render-baseline`, `render-chunks` and `render-cli` (`DETERMINISTIC_FILES`), as it does for `parity/`; the variable
    is process-wide, so it reaches every engine job's own `Engine`.
- **Question 1: which fixture-free renders make sound.** Two sources only. smsggdj with an authored song (4 cases, both
  consoles, a name and an index pick, two rates, the host-synced path through `rp.sms.sync`) and mGB as a Super Game Boy
  (the BIOS boot jingle, mixed after the APU, so its stems are silent). mGB and all six BlipToaster carts are silent
  because a render sends no MIDI, and smsggdj boots blank. So CI's share is 5 sounding cases, 4 on Mesen's SMS/GG core
  and the SGB jingle on SameBoy's mix. CI has no coverage of a NES, of a split render, or of a Game Boy's per-channel
  stems; the C++ `[engine-jobs]` cases cover the last through `rp.mgb.midi`. MIDI-driven renders (section 8) are the way
  to close it.
- **Question 4: how far the HFF end frames move.** They do not. Ten songs, deterministic cores both sides, the old side
  being `retroplug-cli render` (the library in-process for ecoli NTSC): telemelt slots 0 to 4 (324400, 304100, 204400,
  191700 and 259400 ms), tripledipper (262500), risa ecoli_soul (59000), hevander (91100), let_go (59000) and ecoli
  NTSC (49800) all end on the same frame, and no detection changed outcome. The bytes do move: LSDj differs sparsely
  from about frame 9 to 22 thousand on, by song (MACHETE 11555 of 16.9 million samples, at most 505 LSB; SEMIYOYO 36
  percent), and
  risa's from about frame 900, not by a shift. Cause: the presses land at the block boundary after the settle (frame
  66560 where the old path pressed at 66150 at 44.1 kHz).

### The switch-over and retirement (commits 3 and 4)

- **The CLI.** `retroplug-cli render` parses its arguments, submits `engineRun("render", spec, {onLog})` and prints
  the job's lines as they arrive (stdout lines to stdout, stderr to stderr). `--list-songs` stays in-process. Its pump
  is `keepAlive()` plus `setOfflineYield` (the next poll step after a native wait runs as a macrotask): without the
  yield a minute-long render exhausts the launcher's bounded loop of 20000 pumps, and the hook is not unconditional
  because the native test host fires no timers. SIGINT is `__rp_onInterrupt` (`cli/main.cpp`), exposed as
  `onInterrupt` in `cli/session.ts`: the first Ctrl-C cancels the job, which ends cancelled and prints
  `ERROR: render: cancelled`, exit 1 (one rule for every cancel, not 130), and restores the default handler so a
  second kills a wedged job. A cancelled render, or one that fails mid-render, leaves no WAV: the sink streams each
  output to a `.part` of the render's own, `<path>.<token>.part` (the token from the clock, the runtime's
  `Math.random` and a count, drawn again while a name is on disk), moves each over its path only when the whole render
  has been written, and `discard()` deletes its own `.part` files (with any name "If Exists: Rename" reserved), so an
  export that was already at the path stays as it was. Since no two renders share a `.part`, two overlapping
  "overwrite" renders of one target each write a whole WAV and the last to end is the one left there, and a render
  never opens, truncates or deletes another's `.part`. A failed move into place is the one error that keeps them:
  every `.part` is then a finished render, possibly minutes of work (on Windows a DAW holding the previous export open
  is enough to refuse the move), so none is deleted and the error says where the render is ("could not replace X; the
  render is at X.<token>.part"), and for a split render which stems were moved and which are left as `.part` files; a
  later render of X leaves that file alone. The backend has no fsync, so a power loss just after a move can leave the
  new file short on a filesystem that does not order data before a rename. "If Exists: Rename" counts any `.part` of
  an output (found by listing its directory) as taken and reserves the chosen name with the render's own empty `.part`
  where it is chosen, so a render started while another runs takes the next name (two choosing in the same instant
  could still tie: the backend has no exclusive create). A `.part` left by a process that died mid-render (a second
  Ctrl-C, a crashed DAW) is never removed, as nothing tells it from a live render's: it makes every later "rename"
  render of that name take the next one, with no `X.wav` there, until it is deleted by hand. Printed paths are as the
  user typed them; messages and exit codes are otherwise as before. The cores' own stdout (Mesen's load log) goes
  straight to fd 1 while the library's lines arrive through the drain about 100 ms later, so the two can interleave
  differently.
- **The UI.** `RenderJobs` (`src/renderJobs.ts`) is kept in the store graph (`AppStores.renders`), not in a UI module:
  in a DAW that graph is the control plane's, which outlives the editor and owns the `OfflineJobs` queue that submitted,
  so a completion is drained by the queue that owns it and no adopt path is needed. `startSystemRender` submits through
  `stores.backend`; the tile badge samples `renders.list()` instead of polling a hook and a tap calls `renders.cancel`;
  finished jobs auto-dismiss, and one whose system has no tile is dropped as it settles and whenever the systems list
  changes. The SDL standalone gains renders (it had no `__rp_startRender`) through the same facet.
- **Retired.** `RenderJobRegistry`, the four `__rp_*Render` hooks (and their declarations), `g_renderJobs`, the CLI's
  in-process render path, `RenderHost`'s exact-blocks mode and `RenderContext.blocks`. The library's exact-blocks engine
  and `setContinuousBlocks(false)` stay for the in-process render tests and parity.
- **The systems signal has observers.** `ProjectStore.addOnSystemsChange` appends; the control plane (also the SDL
  host's) adds its watched-ROM refresh beside the store graph's own notify and `renders.prune()` instead of replacing
  them, which had left finished renders of a removed system listed in those hosts. One observer's throw is caught and
  warned, so the ones after it still run and the edit that signalled still completes.
- **A pull is at least one frame.** `RenderOpts.pullMs` must come to at least one frame at the render's rate (a
  smaller pull rendered no audio and never ended), and a chunk with no frames is an error.
- **`retroplug-render-host-test`** is three legs on engine jobs: `<job-json>` runs one job on a pool (the production
  path, so "byte-identical to `retroplug-cli render`" holds because it is the same code, and is still checked by
  `cmp`), `--parallel <j>...` runs N jobs on a pool sized so all can run, asserts its engine budget covers
  `min(N, kMaxWorkers - 1)` before submitting and that some drain's progress showed that many running at once (a
  completion is not counted: its worker may already have taken the next job, so counting both saw overlap in jobs run
  back to back), and `--cancel <j>` cancels once a drain shows the job rendering and asserts `cancelled`, a rendered
  length above zero, no outputs, and the files its spec names as they were before it ran (a mix spec's `out` and
  `<stem>_N.wav`, a split one's `<prefix>_<stream>.wav` and `<prefix>_N_<stream>.wav`, so "onExists":"rename" is
  covered, each with every `.part` of it, read with the backend's JSON reader and listed before the job is submitted):
  none new, a leftover `.part` among them, and none changed or deleted, so an earlier export stays, and so does a
  `.part` another render left. Each job's console lines are labelled `[job N]`. It is a manual smoke; `[engine-jobs]`
  carries the coverage CI runs. `--registry` is gone. There is no render leg in `run-sanitizer.sh` (the script only
  runs the host's `dsp-threaded` and `dsp-lifecycle` slugs); the thread and address checks are `[engine-jobs]` built
  in `build-tsan` and `build-asan`, and the tool's three legs run by hand in both.

### Found while building

- **The tests that bite.** Moving the CLI alone broke "UI worker equals CLI" until the registry rendered continuous too
  (the Start press lands one block boundary later); a mutation of engine jobs to exact blocks fails exactly the
  host-synced case; removing the settle shape fails the stems-against-mix case and the stem hashes; removing the
  floating-point pin fails the flush-to-zero case; a budget of `size` fails the graph-job-while-renders-hold-the-pool
  case; a TSan build of the tool finds the SameBoy statics red and the patch makes it green.
- **Three different risa batteries render identical bytes** on risa-2.2.1-pal (ecoli_soul, let_go and the ROM's own
  `.sav`); hevander differs. Probably one working song; not investigated.
- **Hashing is the long pole of the baseline.** FNV-1a-64 in QuickJS runs at about 150 ns per byte: MACHETE's four
  stems take about 37 s of render-baseline's roughly 60 s with fixtures.

### Left beyond D3

- **MIDI-driven renders.** A render fed a MIDI file would give mGB (Game Boy stems) and BlipToaster (NES channels, pins
  and expansion chips) fixture-free sound, and with it CI coverage of SameBoy's per-channel path, the NES and every
  split render (all of which only a fixture run covers today; section 8).
- **CI's continuous-path coverage is thin.** The render baseline's LSDj and risa cases need fixtures, so on CI only
  smsggdj and the SGB jingle run through engine jobs against exact bytes. `[engine-jobs]`'s back-to-back and parallel
  case renders two SGB jingles too, side by side first, so a job against a direct run compares sound on CI, and with
  the tag run alone they are the process's first jingles, which guards the SGB table's load-time build under TSan.
- **Detection is quantised to the poll.** Block-granular probes (or an autonomous recipe) would make an auto-detect
  render's length pull-independent.
- **A cancel can hang a wedged job.** A render that never settles hangs the CLI where the old bounded loop gave up; the
  second Ctrl-C is the way out. A closed editor leaves a finished render listed as rendering until the next pump.
- **A GBA render case** needs a BIOS to make sound.
- **Windows and the other platforms** have not built this tree; the new cross-platform pieces are the SameBoy patch (GNU
  `__atomic` builtins, so GCC and Clang only, which is all that builds the core), `SimpleLock`'s atomics and the
  CLI's `__rp_onInterrupt` signal handling.
