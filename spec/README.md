# RetroPlug — architecture spec

This directory is the single source of truth for the RetroPlug architecture — the
implementation in [`packages/native/`](../packages/native) (C++ host)
and [`packages/retroplug/`](../packages/retroplug) (TypeScript + React/LVGL UI).

The guiding principle is one sentence:

> **Native owns bytes and cores; TypeScript owns meaning.**

Native (C++) owns the emulator cores, their raw bytes, and the real-time audio thread; it makes no
policy decisions. TypeScript owns everything with meaning — system identity, ROM classification,
paths, the project model and its serialization, config, roles/DSP behaviour, routing, and the UI —
and drives native through one narrow `Backend` RPC surface.

## Status

The port is complete. RetroPlug is a single build: the older **legacy** build (the twin
`Plugin*.cpp` DSP/UI, `packages/ui/`, `packages/cli/`, and the old generated RPC client) has been
deleted, the transitional target suffix dropped, and the plugin identity reverted to the canonical
`RetroPlug` strings. These docs describe that build in the present tense. What remains is
**feature work, not migration** — a handful of gaps (the raw LSDj Keyboard mode, NES per-mapper
expansion sub-channels, the kit-patch UI rework) tracked in
[07-remaining-work.md](07-remaining-work.md).

## The documents

| Doc | What it covers |
|---|---|
| [00-overview.md](00-overview.md) | The front door: what RetroPlug is, the thesis, the artifact set (clap/vst3/vst2/au/jack + standalone + test hosts), the package layout, a one-screen component map. |
| [01-architecture.md](01-architecture.md) | **The canonical runtime model.** The three hosts over one Backend RPC surface (the capability facets); control plane vs audio thread; the command ring, the `SnapshotRegistry` read door, the release ring; the two QuickJS runtimes; the threading/ownership invariants. Other docs reference this one. |
| [02-native-host.md](02-native-host.md) | The C++ host (`packages/native`): the full RPC surface + its facets, `Engine` + `EngineInvoker`, `SnapshotRegistry` internals, `SystemFactory` + the core backends, the bare DSP context runner, and the shared-core classes it wraps. |
| [03-ts-layer.md](03-ts-layer.md) | The TypeScript layer (`packages/retroplug`): the `Backend` interface, the stores + the reconstruct-in-place idiom, the control-plane composition, the `__rp_*` hooks, and the React/LVGL UI. |
| [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model (system-role vs feature-role) and the DSP role kernel: the byte-sink ABI, the drift-exact PPQ clock in JS, and store→kernel projection. |
| [05-data-persistence.md](05-data-persistence.md) | The project model and `.rplg` (thin vs export), DPF get/setState, the config schemas, the forward-tolerant-read + version-stamp policy, the LSDj sav codec, and SRAM auto-save. |
| [06-build-test.md](06-build-test.md) | How the build works, the pnpm scripts, and the headless verification loop — which command proves which kind of change. The practical "how do I verify a change" doc. |
| [07-remaining-work.md](07-remaining-work.md) | The remaining feature gaps now the port is complete: the raw LSDj Keyboard mode, NES per-mapper expansion sub-channels, the kit-patch UI rework, the deferred items, and the code-comment cleanup follow-up. |
| [08-profiling.md](08-profiling.md) | **Built** (behind `RETROPLUG_PROFILE`, `pnpm profile`). A benchmark harness that profiles the DSP-thread JS runtime — allocations/GC — under an mGB + heavy-MIDI workload: the refcount-churn reframe, the off-RT `renderAudio` harness, in-process QuickJS allocator instrumentation, the agent-friendly tool tier, and CI gates. |
| [09-cli-debugging.md](09-cli-debugging.md) | **Built** (the plumbing tier). The CLI as a scriptable ROM-test harness: the session runner + `Timeline` + WAV/screenshot output, the compiled-in `render` subcommand, and Mesen's compiled-in debugger (decoded APU/PPU state, CPU/memory peeks + poke, breakpoints, trace, profiler, cc65 labels) surfaced through the debug RPC facet so an agent can drive a real NES and assert on its state. |
| [10-multichannel-audio-out.md](10-multichannel-audio-out.md) | **Built** (steps 1–6; per-mapper NES expansion sub-channels remain). Outputting the individual console sound channels of one instance: Game Boy → 8 outputs (a stereo pair per channel, via a tracked SameBoy tap), NES → 5 mono stems or the hardware "stereo-mod" pins (via a `NesSoundMixer` edit). One router/mode-driven host seam (`channelLayout()` + a lane-counted `finishBlock`), a GB-scoped `AudioRouting::ChannelSplit` plugin option, and a `renderAudioPerChannel` CLI RPC feeding multichannel / per-stereo / per-mono WAV export. |
| [11-ui-rendering.md](11-ui-rendering.md) | **Built.** Rendering a system to WAV from the `System > Render` menu as a background job — a fresh instance built from a *copy* of the live SRAM/savestate (never the running core). A shared `src/render/` library the CLI `render` command and the worker both run; a bare-QuickJS `RenderHost` (own `Engine`, no txiki) running a global-code worker bundle; a `RenderJobRegistry` of per-job threads (concurrent, cancellable, survives editor close); and the `__rp_*Render` hooks + tile progress/cancel badge. |

## Reading order

- **New to the codebase?** [00](00-overview.md) → [01](01-architecture.md), then dip into
  [02](02-native-host.md) / [03](03-ts-layer.md) for the side you're working on.
- **Changing code and want to verify it?** Go straight to [06-build-test.md](06-build-test.md).
- **Working on the port itself?** [07-remaining-work.md](07-remaining-work.md) is the map of what's left.

## Provenance

`spec/` replaces three older planning directories (`porting/`, `architecture/`, and
`packages/retroplug/plans/`) whose content had drifted out of sync with the code and with
each other. This set describes the architecture **as it is today**; forward-looking work lives under
a clearly-labelled "Not yet built / deferred" heading in each doc, and is inventoried in
[07-remaining-work.md](07-remaining-work.md). Stale in-code comments that still describe the old design are
tracked as a separate cleanup task (note, don't fix) in [07-remaining-work.md](07-remaining-work.md#4-code-comment-cleanup-backlog-note-dont-fix).
