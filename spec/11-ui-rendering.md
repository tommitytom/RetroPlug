# 11 — UI-driven rendering (render tracks out from the UI)

**Status: built.** The `System > Render` submenu renders a system to WAV the same way
`retroplug-cli render` does — full mix or per-channel/per-pin stems — but as a **background job** inside
the plugin, so the menu can close while it runs and several systems can render at once. It builds on
[10-multichannel-audio-out.md](10-multichannel-audio-out.md) (the split modes / `renderAudioPerChannel`),
[09-cli-debugging.md](09-cli-debugging.md) (the CLI `render` it shares a job path with), and
[01-architecture.md](01-architecture.md) (the control plane / audio-thread split it must not violate).

## Thesis

The render must build a **fresh** emulator instance from a *copy* of the running system's SRAM (or a
savestate if the cart has no battery) and drive it offline — it must **never** touch the live
audio-thread cores. And the CLI render pipeline and the UI render must be **one shared library**, so
improving one improves both.

## The shape

A render is an **engine job**: a second kind of job on the engine's offline pool, beside the kit compile's graph jobs
([modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md), built on D2's
[pool](01-architecture.md)). The CLI and the UI submit the same job through the same RPCs.

```
System > Render menu ─▶ startSystemRender (ui/host/render.ts)     retroplug-cli render (cli/sessions/render.ts)
   │  browseThen → out path      │  snapshot live SRAM/savestate → job's own files  │  parse argv → spec
   ▼                             ▼                                                   ▼
RenderJobs.start (src/renderJobs.ts)  ──────────────▶  backend.engineRun("render", spec, {onProgress, onLog})
                                                          │  submitEngineJob / drainOfflineJobs / cancelOfflineJob
                                                          ▼
                                       OfflineJobs (the Engine's pool): a worker runs the job to the end
                                                          │  RenderHost (bare QuickJS + the job's own Engine)
                                                          ▼
                                       src/render/  shared library  (runRenderJob, in the render-worker bundle)
EmulatorTile badge ◀── renders.list() (the list RenderJobs keeps from the job's progress and promise)
```

## The shared render library — `packages/retroplug/src/render/`

Host-neutral TypeScript, run by every render: the engine job's worker bundle (`worker.ts`) is its only production
caller, and the in-process library tests call `runRenderJob` directly:

- `render.ts` — `runRenderJob(ctx, opts, hooks)`: builds the system, resolves the length (LSDj HFF
  auto-detect via NR52, or a fixed duration), streams per-chunk PCM to WAV (mix / channels / pins).
- `types.ts`: `RenderOpts` (the request; `pullMs` is a test knob for the stream's pull length, default 100),
  `RenderContext` (`{ backend, project, audio }`: a CLI `Session` satisfies it structurally, and the system's roles
  reach their protocol nodes in the graph its `project` publishes), `RenderHooks` (`onRendered` / `isCancelled` /
  `log` / `warn` / `partTag`, all optional), `RenderCancelled`. `isCancelled` is polled between the boot's steps
  (before any file exists) and before each chunk. The sink streams each output to a `.part` of the render's own,
  `<path>.<token>.part` (the token carries `partTag`, an engine job's `RenderHost` serial), and syncs it to the disk
  and moves it over `<path>` only when the render has been written whole; a render that is cancelled or fails
  `discard()`s its own `.part` files (and any name "If Exists: Rename" reserved with an empty one), so no WAV claims no
  audio, an export already at the path stays, and so does any other render's `.part` of it (two overlapping renders
  of one target each write a whole file); a failed sync or move into place keeps the finished `.part` files and the
  error names them. Rename counts any `.part` of a name as taken (in any case where the filesystem folds case), so one
  left by a process that died mid-render moves every later rename render of that name to the next until it is deleted
  by hand.
- `wav.ts` — the streaming RIFF/PCM16 codec (moved from `cli/wav.ts`, which now re-exports it).
- `worker.ts` — the background session entry (below).

The CLI `render` command ([cli/sessions/render.ts](../packages/retroplug/cli/sessions/render.ts)) parses its
arguments into the job spec, submits `backend.engineRun("render", spec, {onLog})` to its own engine and awaits it,
printing the job's console lines as they arrive (`--list-songs` stays in-process). It sets `keepAlive()` and
`setOfflineYield` so a minute-long render pumps without exhausting the launcher's bounded loop, and registers
`onInterrupt` so the first Ctrl-C cancels the job (it ends `ERROR: render: cancelled`, exit 1, and a cancel that
lands mid-render leaves no WAV).

## RenderHost — the offline render environment (`packages/native/src/host/render/RenderHost.*`)

A self-contained headless render: a **bare QuickJS** runtime (the script host's pattern: raw
`JS_NewRuntime`, **no txiki / libuv**) + its **own** `Engine` + the in-process RPC bridge, running
`worker.ts` compiled to **global-code bytecode** (`rp_render_worker_bundle`, esbuilt to an IIFE + `tjsc`
without `-m`, embedded in `retroplug-backend`). It reproduces the CLI's `main()` service graph minus
`TjsHostRuntime`; the render path is a synchronous, pull-based loop, so none of txiki's event loop is
needed. Beyond the ES standard library the only shims are `console` + a minimal UTF-8
`TextEncoder`/`TextDecoder` (the two Web globals the control plane touches at load) and the
`__rpcSend` + rendered/cancel/result thunks the worker reports through. Bare QuickJS also sidesteps the
multi-txiki-runtime class-id hazard entirely.

`run(jobJson, onRendered, isCancelled, onConsole)` blocks for the whole render on the calling thread, which is a
pool worker; ROM is loaded by path, SRAM/savestate ride as bytes. The job's engine runs **continuous blocks**
(`EngineRpcService::setContinuousBlocks`): whole 1024-frame blocks whatever each call asks for, the surplus carried to
the next call of the same shape, so a render's sample stream does not depend on the library's chunking. Console writes
go to `onConsole` (with none, to the process's stdout and stderr).

## Engine jobs - the pool (`.../engine/OfflineJobs.*`)

A render is submitted with `submitEngineJob({kind: "render", spec})` (the emulator facet) and settled by the same
`drainOfflineJobs` / `cancelOfflineJob` as a kit compile. The pool is the `Engine`'s, so a render belongs to its plugin
instance: it outlives the editor window, and the instance's teardown cancels it (at the next 100 ms chunk) and joins
the worker. Each job builds its `RenderHost` and `Engine` on its worker and destroys them there, referencing nothing on
the live plugin. A drain reports every running job's progress and the console lines written since the last one
(bounded: 256 lines and 64 KiB per job between drains); a completion is `done`, `cancelled` or `error`, with the files
written. Engine jobs hold at most all but one of the pool's workers (one for a pool of one), queue first in, first out,
and share the 64-job bound with graph jobs, so a kit compile always finds a worker. Process-wide state that parallel
cores touch is made safe: Mesen's setup is a function-local static,
initialised once whichever thread builds the first core
([MesenGlobalInit](../packages/mesen/native/src/system/mesen/MesenGlobalInit.cpp)), its `SimpleLock` is atomic, and
SameBoy's `Core/random.c`, `camera.c` and `sgb.c` statics are patched
([06-build-test.md](06-build-test.md)).

## The UI seam

- **No hooks.** Renders need no `__rp_*` window hook: the UI submits through the stores' backend
  (`stores.backend.engineRun`), so every host that mounts the emulator facet renders, the SDL standalone included.
- **`src/renderJobs.ts`** - `RenderJobs` (`start(systemId, spec, onSettled, discardFiles)`, `list()`, `cancel(id)`,
  `dismiss(id)`, `prune()`) keeps what each job's progress callback and promise report, plus its handle. `spec` can
  be a function of the job's key (a per-list token and the job's id, so unique across instances too), and `onSettled`
  runs once the job has ended, however it ended. It belongs to the store graph (`AppStores.renders`), which in a DAW is the control
  plane's, so it outlives the editor and a completion settles through the same queue that submitted the job (nothing
  drains with the editor closed; the WAV is on disk either way, and the badge catches up at the next pump).
- **`ui/host/render.ts`** - `startSystemRender` snapshots the live `readSram`/`readState` to files of the job's own
  under `configDir` (`.render-src-<key>.sav` / `.ss0`, `RENDER_SNAPSHOT_PREFIX`) and calls `renders.start` with their
  list as the submission's own (`EngineRunOptions.discardFiles`): the engine's pool deletes them however the job ends
  (on the job's worker before the completion is queued, or with the job itself when it is dropped unrun), and
  `onSettled` deletes them again on whichever road it took: a job the queue held past the 64-job bound and cancelled
  before it was submitted, a submission the RPC threw on, a spec function that threw, and every job a drain that threw
  failed, which native may still hold (a second removal is a no-op). Where that last road meets a job native still has
  queued, the boot refuses (`RenderOpts.inputsMustExist`); a job already past that check can lose the file before its
  boot reads it, a window of a handful of calls, and then boots the cart's blank start. What is left is an instance
  that ends with a render held in TS, and a crash, which the startup sweep takes (`src/renderSnapshotSweep.ts`: a
  snapshot more than a day old). The worker reads the snapshots only when it boots the render, which can be minutes
  later behind the worker budget, so a shared name would boot a later edit's (or another instance's) state; and the
  spec says they must exist when it boots (`RenderOpts.inputsMustExist`), so one the sweep took from under a live job
  refuses rather than booting blank, except in the window above. A snapshot that cannot be written refuses the render,
  listed as an error, rather than boot the sibling `.sav` on disk. `pickActiveRenderJob` selects a tile's badge job and
  `renderBadgeLabel` writes its text.
- **The instance's CONFIGURATION rides along** — `spec.roles` (the live system's roles, configs included)
  and `spec.gainDb`. The job boots a fresh core from the ROM on disk, so without them it would take each
  role's schema DEFAULTS and render a PAL project at NTSC, a Game Boy as the wrong model, a replaced DMC kit
  from the cart's original samples, or an expansion chip at unity whatever the instance's Expansion Volume
  says. `buildSystem` therefore goes through `adopt` whenever roles/gain are carried (the same construct-time
  path a project load uses: the roles' construct-time fields reach the system node that builds the core, and the
  gain and the core config's knobs reach the core as its system node's params and settings in the document that
  places it), and folds a NES split's `channelExportMode` INTO the carried `mesen` role rather than replacing the
  list. A CLI render carries neither: it has no project to copy and correctly boots the ROM as it ships.
- **Menu** — `System > Render` holds selectors — **Audio Routing** (the split mode: Mix / Channels / Pins,
  gated on platform), **Sample Rate** (44100 / 48000 / 96000), **Max Duration** (Left/Right ±1s,
  PageUp/PageDown ±30s) — persisted
  in `userConfig.render`, plus one **Render…** action that applies them. On-disk ROMs only (an embedded mGB
  has no path to reconstruct from — a v1 limit). The PageUp/PageDown coarse step rides a new
  `MenuItem.onCoarseStep`, routed from Menu.tsx's raw `"key"` bus (the Shift modifier isn't delivered to the
  menu; Page keys aren't LVGL-translated so they arrive there without stealing focus). "Max Duration" bounds
  every render — `runRenderJob`'s fixed fallback is now `durationMs ?? maxDurationMs` (default 300000, so
  default CLI output is unchanged).
- **Tile** - [EmulatorTile.tsx](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) samples
  `renders.list()` on the existing per-frame event and draws a bottom badge counting the audio
  rendered so far (`rendering 0:42 (tap to cancel)`); finished jobs are auto-dismissed. Deliberately **not**
  a percentage: an auto-detect render ends at the song's HFF stop, so the only bound available up front is
  `maxDurationMs` - a safety cap usually far longer than the song, which made the old fill bar creep to ~10%
  and finish. The reported number is therefore rendered milliseconds end to end (`onRendered` -> the job's
  `EngineJobRecord` -> the drain's progress -> `onProgress`, a running maximum), settling on the length written (the
  result's `renderedMs`). A tap on a rendering
  badge calls `renders.cancel`; a finished job whose system has no tile is dropped as it settles.

## Verification

- **The seam:** the `[engine-jobs]` Catch2 cases in `retroplug-engine-graph-test` (the pool's threading, the budget,
  cancel, teardown, continuous blocks and the carry), run under ThreadSanitizer and AddressSanitizer from `build-tsan` /
  `build-asan` (`tools/run-sanitizer.sh` does not run them); `pnpm test:native engine-run` (the promise side over the
  real host).
- **The render baseline** (`pnpm test:native render-baseline`): 13 renders through engine jobs hashed against
  `test-native/golden/render-baseline.json`, exact on `linux-x64-gnu13` (`RP_RENDER_GOLDEN_REQUIRE_TARGET=1` in CI),
  with `render-chunks` holding the chunk independence and `render-cli` the CLI's tool against the same golden
  ([06-build-test.md](06-build-test.md)).
- **A manual smoke:** `retroplug-render-host-test` (built by name) - `<job-json>` runs one engine job and is
  `cmp`-identical to `retroplug-cli render` (the same code); `--parallel <j>...` runs jobs concurrently on one pool;
  `--cancel <j>` cancels mid-render. The coverage CI runs is `[engine-jobs]`; the tool is not a CI step.
- **UI-seam logic:** `pnpm test render` - `startSystemRender` spec/snapshot (two queued renders of one system each
  boot their own snapshot, gone once settled; a render held past the queue's bound and a submission the RPC threw on
  delete theirs; a snapshot that cannot be written refuses the render; a render whose snapshot went refuses to boot),
  the startup sweep (`snapshot-sweep`),
  `pickActiveRenderJob`, `renderBadgeLabel`, `validSplits`/`formatDuration`, the `userConfig.render` setters/clamps,
  and (in `risa-autodetect`) that `onRendered` reports real audio time: monotonic, stepping by the render chunk,
  settling on the length written.
- **Badge + menu:** `pnpm test:ui render-badge` (tile badge) and `pnpm test:ui render-menu` (the Split /
  Sample Rate / Max-Duration selectors + PageUp/PageDown coarse step + Render… → a submitted engine job), both on
  the real LVGL display via a file-dropped ROM.
- **Carried configuration:** `pnpm test:native risa-render` — a carried `region` moves the DETECTED song
  length (PAL 59000 ms → NTSC 49800 ms, the 50/60 clock ratio), and a carried `gainDb` / role knob lands on
  the system the job builds. It has to be measured structurally like that: risa boots with randomized NES
  RAM, so two renders of one song differ by ~3 dB and no LEVEL comparison across renders means anything. What
  each knob then does to the audio is its own test (`pnpm test:native expansion-volume`). Mock tier,
  `pnpm test render`: `render-document` reads the document the store publishes and finds the carried gain on
  the system node's `params`.

## Not yet built / deferred

- **Project-level render** (all instances at once) - the pool already runs N engine jobs concurrently; only the
  "render the whole project" entry point is missing.
- **Tempo / transport UI** — no BPM/transport control in the menu (split, sample rate, and max duration are
  now selectable; a pinned *fixed* duration separate from the max cap is not).
- **Embedded-ROM render** (mGB with no on-disk path) — gated out; would need the `embeddedRom` marker
  threaded through the worker spec.
- **MIDI-driven renders** - a render sends no MIDI, so mGB and BlipToaster render silence; with them, CI would cover
  SameBoy's stems and the NES fixture-free. The render baseline's LSDj and risa cases need fixtures, so CI's coverage of
  the continuous path is smsggdj and the SGB jingle only.
