# RetroPlug2 — architecture spec

This directory is the single source of truth for the **greenfield** RetroPlug2 architecture — the
active reimplementation in [`packages/native-greenfield/`](../packages/native-greenfield) (C++ host)
and [`packages/retroplug-greenfield/`](../packages/retroplug-greenfield) (TypeScript + React/LVGL UI).

The guiding principle is one sentence:

> **Native owns bytes and cores; TypeScript owns meaning.**

Native (C++) owns the emulator cores, their raw bytes, and the real-time audio thread; it makes no
policy decisions. TypeScript owns everything with meaning — system identity, ROM classification,
paths, the project model and its serialization, config, roles/DSP behaviour, routing, and the UI —
and drives native through one narrow `Backend` RPC surface.

## Status

RetroPlug2 is mid-port. Two plugin builds coexist in the repo today: **greenfield** (documented
here) and the older **legacy** build ([`packages/native/src/Plugin*.cpp`](../packages/native/src) +
[`packages/ui/`](../packages/ui) + [`packages/retroplug/`](../packages/retroplug) +
[`packages/cli/`](../packages/cli)). Legacy still ships only because greenfield has not yet reached
full parity. **Legacy is being removed** — greenfield will become the sole build, the `-greenfield`
suffix dropped. These docs describe greenfield; legacy appears only where a doc must note it is going
away. The switchover itself — the feature gap, the delete/rename checklist, and the risks — is
[07-migration.md](07-migration.md).

## The documents

| Doc | What it covers |
|---|---|
| [00-overview.md](00-overview.md) | The front door: what greenfield is, the thesis, the artifact set (clap/vst3/jack + standalone + test hosts), the package layout, a one-screen component map. |
| [01-architecture.md](01-architecture.md) | **The canonical runtime model.** The three hosts over one `BackendFacade`; control plane vs audio thread; the command ring, the `SnapshotRegistry` read door, the release ring; the two QuickJS runtimes; the threading/ownership invariants. Other docs reference this one. |
| [02-native-host.md](02-native-host.md) | The C++ host (`packages/native-greenfield`): the full RPC surface, `Engine` + `QueuedInvoker`, `SnapshotRegistry` internals, `SystemFactory` + the core backends, the bare DSP context runner, and the shared-core classes it wraps. |
| [03-ts-layer.md](03-ts-layer.md) | The TypeScript layer (`packages/retroplug-greenfield`): the `Backend` interface, the stores + the reconstruct-in-place idiom, the control-plane composition, the `__rp_*` hooks, and the React/LVGL UI. |
| [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model (system-role vs feature-role) and the DSP role kernel: the byte-sink ABI, the drift-exact PPQ clock in JS, and store→kernel projection. |
| [05-data-persistence.md](05-data-persistence.md) | The project model and `.rplg` (thin vs export), DPF get/setState, the config schemas, the forward-tolerant-read + version-stamp policy, the LSDj sav codec, and SRAM auto-save. |
| [06-build-test.md](06-build-test.md) | How greenfield builds, the pnpm scripts, and the headless verification loop — which command proves which kind of change. The practical "how do I verify a change" doc. |
| [07-migration.md](07-migration.md) | The switchover to greenfield-as-sole-build: the feature gap, the shared-vs-legacy C++ map, the rename/delete checklist, the gaps greenfield must own, and the code-comment cleanup follow-up. |
| [08-profiling.md](08-profiling.md) | **Plan (not yet built).** A benchmark harness to profile the DSP-thread JS runtime — allocations/GC — under an mGB + heavy-MIDI workload: the refcount-churn reframe, the off-RT `renderAudio` harness, in-process QuickJS allocator instrumentation, the agent-friendly tool tier, and CI gates. |
| [09-cli-debugging.md](09-cli-debugging.md) | **Partly built.** The CLI as a scriptable ROM-test harness: the built session runner + `Timeline` + WAV/screenshot output, and the **plan** to surface Mesen's already-compiled-in debugger (decoded APU state, CPU/memory peeks, breakpoints, trace, cc65 labels) through the greenfield RPC — porting the proven legacy `HarnessRpcService` surface — so an agent can drive a real NES and assert on its state. |

## Reading order

- **New to the codebase?** [00](00-overview.md) → [01](01-architecture.md), then dip into
  [02](02-native-host.md) / [03](03-ts-layer.md) for the side you're working on.
- **Changing code and want to verify it?** Go straight to [06-build-test.md](06-build-test.md).
- **Working on the port itself?** [07-migration.md](07-migration.md) is the map of what's left.

## Provenance

`spec/` replaces three older planning directories (`porting/`, `architecture/`, and
`packages/retroplug-greenfield/plans/`) whose content had drifted out of sync with the code and with
each other. This set describes the architecture **as it is today**; forward-looking work lives under
a clearly-labelled "Not yet built / deferred" heading in each doc, and is inventoried in
[07-migration.md](07-migration.md). Stale in-code comments that still describe the old design are
tracked as a separate cleanup task (note, don't fix) in [07-migration.md](07-migration.md#6-code-comment-cleanup-backlog-note-dont-fix).
