# 11 — UI-driven rendering (render tracks out from the UI)

**Status: built.** The `System > Render` submenu renders a system to WAV the same way
`retroplug-cli render` does — full mix or per-channel/per-pin stems — but as a **background job** inside
the plugin, so the menu can close while it runs and several systems can render at once. It builds on
[10-multichannel-audio-out.md](10-multichannel-audio-out.md) (the split modes / `renderAudioPerChannel`),
[09-cli-debugging.md](09-cli-debugging.md) (the CLI `render` pipeline it now shares), and
[01-architecture.md](01-architecture.md) (the control plane / audio-thread split it must not violate).

## Thesis

The render must build a **fresh** emulator instance from a *copy* of the running system's SRAM (or a
savestate if the cart has no battery) and drive it offline — it must **never** touch the live
audio-thread cores. And the CLI render pipeline and the UI render must be **one shared library**, so
improving one improves both.

## The shape

```
System > Render menu ─▶ startSystemRender (ui/lvgl/render.ts)
   │  browseThen → out path      │  snapshot live SRAM/savestate → temp file
   ▼                             ▼
__rp_startRender(sysId, specJson)  ──▶  PluginUI  ──▶  RenderJobRegistry (process-global)
                                                          │  one std::thread per job
                                                          ▼
                                                     RenderHost  (bare-QuickJS + own Engine)
                                                          │  runs the render-worker bundle
                                                          ▼
                                       src/render/  shared library  ◀── also the CLI `render` command
EmulatorTile badge ◀── __rp_getRenderJobs (per-frame poll) ◀── RenderJobRegistry::snapshot(owner)
```

## The shared render library — `packages/retroplug/src/render/`

Host-neutral TypeScript, consumed by **both** the CLI `render` command and the background worker:

- `render.ts` — `runRenderJob(ctx, opts, hooks)`: builds the system, resolves the length (LSDj HFF
  auto-detect via NR52, or a fixed duration), streams per-chunk PCM to WAV (mix / channels / pins).
- `types.ts` — `RenderOpts` (the request), `RenderContext` (`{ backend, project, dsp, audio }` — the CLI's
  `Session` satisfies it structurally), `RenderHooks` (`onProgress` / `isCancelled` / `log` / `warn`, all
  optional so the CLI passes none), `RenderCancelled`.
- `wav.ts` — the streaming RIFF/PCM16 codec (moved from `cli/wav.ts`, which now re-exports it).
- `worker.ts` — the background session entry (below).

The CLI `render` command ([cli/sessions/render.ts](../packages/retroplug/cli/sessions/render.ts)) is now a
thin `parseRenderArgs → runRenderJob` wrapper (`--list-songs` stays CLI-only).

## RenderHost — the offline render environment (`packages/native/src/host/render/RenderHost.*`)

A self-contained headless render: a **bare QuickJS** runtime (the `DspRuntime` pattern — raw
`JS_NewRuntime`, **no txiki / libuv**) + its **own** `Engine` + the in-process RPC bridge, running
`worker.ts` compiled to **global-code bytecode** (`rp_render_worker_bundle`, esbuilt to an IIFE + `tjsc`
without `-m`, embedded in `retroplug-backend`). It reproduces the CLI's `main()` service graph minus
`TjsHostRuntime`; the render path is a synchronous, pull-based loop, so none of txiki's event loop is
needed. Beyond the ES standard library the only shims are `console` + a minimal UTF-8
`TextEncoder`/`TextDecoder` (the two Web globals the control plane touches at load) and the
`__rpcSend` + progress/cancel/result thunks the worker reports through. Bare QuickJS also sidesteps the
multi-txiki-runtime class-id hazard entirely.

`run(jobJson, onProgress, isCancelled)` blocks for the whole render on the calling thread; ROM is loaded
by path, SRAM/savestate ride as bytes. Output is **byte-identical** to `retroplug-cli render`.

## RenderJobRegistry — concurrent, cancellable jobs (`.../RenderJobRegistry.*`)

Owns the in-flight jobs, one dedicated `std::thread` each. **Process-global** (a static in
[PluginUI.cpp](../packages/native/plugin/PluginUI.cpp)) so a render survives its editor window closing —
each job spins its own `RenderHost`+`Engine`, referencing nothing on the live plugin. Thread-safe status
for the UI poll; cooperative cancel via an atomic the worker polls between chunks; jobs are tagged with the
editor (`owner`) that started them so a multi-instance host only shows its own. Mesen's process-global
setup is guarded by `std::call_once` ([MesenGlobalInit](../packages/native/src/system/mesen/MesenGlobalInit.cpp))
so concurrent core construction can't race.

## The UI seam

- **Hooks** (bound in `installWindowSizeHooks`, per-context routed): `__rp_startRender(sysId, specJson)`,
  `__rp_cancelRender(jobId)`, `__rp_dismissRenderJob(jobId)`, `__rp_getRenderJobs()`. See
  [03-ts-layer.md](03-ts-layer.md) for the `__rp_*` pattern.
- **`ui/lvgl/render.ts`** — `startSystemRender` snapshots the live `readSram`/`readState` to a temp file
  under `configDir` and calls `__rp_startRender`; `pickActiveRenderJob` selects a tile's badge job. Inert
  in the headless harness (no hooks bound).
- **Menu** — `System > Render` → Render Mix / Channels / Pins (channels = GB/NES, pins = NES); on-disk ROMs
  only (an embedded mGB has no path to reconstruct from — a v1 limit).
- **Tile** — [EmulatorTile.tsx](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) polls
  `__rp_getRenderJobs()` on the existing per-frame event and draws a bottom progress + cancel badge;
  finished jobs are auto-dismissed.

## Verification

- **Native parity/concurrency/cancel:** `retroplug-render-host-test` (built by name) — `<job-json>` proves
  byte-identity vs `retroplug-cli render`; `--registry <j>...` runs jobs concurrently; `--cancel <j>` aborts
  mid-render. Clean under ThreadSanitizer (`tools/run-sanitizer.sh thread`, built in `build-tsan/`).
- **UI-seam logic:** `pnpm test render` — `startSystemRender` spec/snapshot + `pickActiveRenderJob`.
- **Badge rendering:** `pnpm test:ui render-badge` — the tile badge on the real LVGL display.

## Not yet built / deferred

- **Project-level render** (all instances at once) — the registry + per-job-thread model already supports N
  concurrent jobs; only the "render the whole project" entry point is missing.
- **Duration / tempo UI** — v1 uses the CLI defaults (LSDj auto-length, else 5-min fixed); no fixed-duration
  or transport control in the menu yet.
- **Embedded-ROM render** (mGB with no on-disk path) — gated out; would need the `embeddedRom` marker
  threaded through the worker spec.
- **Multi-instance-in-DAW badge on editor reopen** — a job's owner is the editor session, so a close/reopen
  loses the badge (the render still completes + writes its file).
