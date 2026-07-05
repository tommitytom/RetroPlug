> **Status:** design — captured from a design discussion; forward-looking (informs the greenfield build), not yet implemented.

# RetroPlug greenfield — design plans

This directory is the index for the forward-looking **design** decisions behind
the [retroplug-greenfield](../README.md) package — the TS-first, test-driven
reimplementation of RetroPlug's orchestration layer. The docs here capture the
app-layer, extension-model, and C++/JS DSP-boundary decisions that came out of
design discussions with the maintainer.

## Context

The repo-root [architecture/](../../../architecture/README.md) docs are the
**whole-system / native** design: the block runner, project-state ownership, the
C++/TS boundary, the scriptable runtime, cross-core roles, MIDI routing,
multithreading, and LSDj. They describe what native RetroPlug is and where its
seams sit.

These greenfield plans **layer on top of** those. They take the same thesis —
C++ only where it's genuinely needed, everything structured in TS — and push it
into three areas the architecture docs don't fully resolve:

- the **application layer** the greenfield stores already model (systems,
  project, recent, user config, bindings, SRAM auto-save, file watching,
  roles),
- an **extension model** for building features on top of that layer, and
- the **C++/JS DSP boundary** — how meaning stays in TS while bytes and cores
  stay native.

Status across the set is **design, mostly not yet implemented**. They exist to
keep those directions cheap to reach as the greenfield build proceeds.

## The docs

Read in this order:

| # | Doc | Summary |
| --- | --- | --- |
| 01 | [Reference features](./01-reference-features.md) | The concrete unreleased/desired features (LSDj HD Player, LSDj Sample Patcher, in-UI offline export) that stress-test and shape the design. |
| 02 | [DSP data model](./02-dsp-data-model.md) | "Native owns bytes and cores; TS owns meaning": what data must be typed C++ vs an opaque blob. |
| 03 | [DSP JS runtime](./03-dsp-js-runtime.md) | The DSP gets its own bare QuickJS context (no txiki), fed by bytes over the existing lock-free queues; how config + scripts cross. |
| 04 | [Extension model](./04-extension-model.md) | Extensions = roles + UI contributions (menus, views) + a flat trusted SDK; how the reference features are built. |
| 05 | [Export and render](./05-export-and-render.md) | Export/offline-render is control-plane-orchestrated + per-role, not a monolithic native call. |
| 06 | [Audio routing and streams](./06-audio-routing-and-streams.md) | Audio routing becomes a JS decision over per-system output streams (NES stereo-mod). |
| 07 | [Host consumption](./07-host-consumption.md) | How the plugin / CLI / standalone consume the app layer, and how `emu.*` shrinks to a test/dev facade. |

## The through-line

One decision unifies every doc in this directory:

> **Native stays cores + bytes + a few scalars + two lock-free queues;
> everything structured — config, routing, export policy, UI, stop conditions —
> is TS/JS reading blobs.**

Each plan is a different face of that split. 01 supplies the features that pull
on it; 02 draws the line between typed native data and opaque blobs; 03 gives the
DSP a runtime that only sees bytes; 04 lets extensions add meaning without adding
native surface; 05 and 06 keep export policy and audio routing on the TS side of
the line; 07 shows the hosts consuming that layer and `emu.*` receding to a
test/dev facade.

## Links

- [architecture/03 — The C++/TS boundary](../../../architecture/03-cpp-ts-boundary.md)
  — the native contract and the leaf-first evacuation of orchestration to TS.
- [architecture/04 — The scriptable runtime](../../../architecture/04-scriptable-runtime.md)
  — the always-available runtime that lets orchestration leave C++.
- [architecture/06 — MIDI routing as hot-reloadable scripts](../../../architecture/06-midi-routing-scripts.md)
  — routing at the byte-level boundary, the precedent these plans extend to audio.
