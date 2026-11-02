# Phase D3: renders as engine jobs

**Date:** 2026-11-02 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9

D2 ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md)) built the offline job seam: a native pool that
runs a graph document to completion on a host of its own, with a TypeScript promise side, and moved the kit compile onto
it. D3 moves the renders onto the same seam. The CLI's `render` and the UI's background renders become a second kind of
job, the engine job: a bare QuickJS runtime and an `Engine` of their own on a pool worker, running the unchanged render
library. One pool, one RPC surface, one pump and one cancel serve kit compiles and renders alike, and the CLI and the
UI render through literally the same code. A render's bytes stop depending on how the render library chunks its pulls.
Phase D's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)).

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
- **Render bytes depend on the chunking.** The library pulls audio in 100 ms chunks and `renderAudio` runs each as
  1024-frame blocks plus a short remainder (1024, 1024, 1024, 1024, 314 at 44.1 kHz). The transport advance, the
  protocol script nodes, the snapshot publishes and the MIDI-out gathering happen per block, so the short block at each
  chunk end is in the bits. No golden records render bytes; `retroplug-render-host-test` holds the UI worker byte for
  byte to `retroplug-cli render`, and the render tests check levels and detection.

## 2. Decisions taken

The user's, on 2026-11-02:

- **Unify on one seam.** Renders become a second kind of offline job, driven through the same submit, drain and cancel
  RPCs, pool and TypeScript promise side as the kit's graph jobs; `RenderJobRegistry` and the CLI's in-process render
  path retire. Not graph-level render jobs (system nodes, events and streaming added to D2's graph jobs), not speed
  and streaming only, and not closing D3 as already met.
- **Chunk-independent bytes, recorded after the move.** A render job runs continuous 1024-frame blocks whatever chunk
  the library pulls, so a render's bytes no longer depend on the chunk size; every render's bits move once, and a new
  render baseline is recorded after the move. Not identical to today, not a staged proof first, and not cross-path
  equality only.
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
milliseconds. Engine jobs hold at most all but one of the pool's workers at once, the rest queue, so a kit compile
always finds a worker. Both kinds count toward the one bound on outstanding jobs.

**Ownership.** A render belongs to its plugin instance's `Engine`, not to the process: it survives the editor closing,
as today, and an instance's teardown cancels it, where `RenderJobRegistry`'s threads ran on into static teardown.

## 4. Continuous blocks

Inside an engine job the engine's `renderAudio` and `renderAudioPerChannel` always run whole 1024-frame blocks: a call
returns the frames asked for and keeps the surplus for the next call, and the surplus is dropped when the job ends.
Between the library's chunks the emulator has therefore run up to 1023 frames past the audio handed back, so a button
press lands at the next block boundary after that point, and an end-of-song read sees state up to 1023 frames later
than the audio returned. Both are deterministic and are in the new baseline; detection precision stays the 100 ms
chunk. Outside engine jobs `renderAudio` keeps its exact splits, so parity, which drives it with its own, is untouched.

## 5. The TypeScript side and the callers

**The promise side.** `backend.engineRun("render", spec, { onProgress })` is a promise of `{ status, message,
outputs }`, settled through D2's shared pump: the native wait in a host with no UI tick (the CLI, the Node addon, the
test hosts), the UI frame tick in the plugin and the SDL standalone. Its handle has `cancel()`, and `offlineRun`'s
gains the same (D2 left TypeScript cancel out). A job's console lines arrive with its progress, and the CLI prints
them as they come, as it does today.

**The CLI.** `retroplug-cli render` parses its arguments into the same spec, submits an engine job to its own engine
and awaits it, instead of running `runRenderJob` over its session. `--list-songs`, which is not a render, stays
in-process. The command's output and exit codes stay what they are.

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
- **Chunk independence:** one render pulled at three different chunk sizes gives identical bytes.
- **The seam:** submit, progress and console lines, cancel from TypeScript for both kinds, an engine torn down with
  renders in flight, a kit job getting a worker while renders hold the rest, under TSan and ASan.
- **Moved:** the UI's `render-menu` and `render-badge` tests onto the job path; `retroplug-render-host-test` reworked to
  drive engine jobs, keeping its parallel and cancel legs.
- **Unchanged:** the existing render tests (`audio-render`, the risa and sms renders, `render-guard`), parity bit for
  bit, the kit goldens, the CLI's render output and exit codes (`test:cli-ts`), both consumer suites.

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

## 9. Prototype questions

- Which fixture-free renders make sound (authored songs on the repo's ROMs), so CI's share of the render baseline is
  not all silence?
- Is a QuickJS runtime and an `Engine` per job safe on a reused pool thread, with no thread-local state left behind
  (LVGL's and QuickJS's class ids, Mesen's scratch paths)?
- What are the right numbers for the worker budget, and does a render queued behind others need a fairness rule?
- How far do the HFF end frames move against today on the fixture songs, and does any detection change outcome?
- What happens when a plugin instance is torn down mid-render, and when an editor reopens on renders that finished
  while it was closed?
