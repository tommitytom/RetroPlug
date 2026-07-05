> **Status:** design — captured from a design discussion; forward-looking (informs the greenfield build), not yet implemented.

# Reference features — LSDj HD Player, Sample Patcher, in-UI export

## Context

The [retroplug-greenfield](../) package is a TS-first reimplementation of
RetroPlug's orchestration layer, built test-driven against a single mockable
[`Backend`](../src/backend.ts) interface. So far the stores it grows
(systems, project, recent, user config, bindings, SRAM auto-save, file watcher,
roles) all cover the feature set of the current `main` branch — the only set
that was ever released.

This doc captures three features that the maintainer built in earlier
(unreleased) RetroPlug iterations, or wants next, and that were never shipped
(or only partially implemented). They are recorded here as **design-shaping
requirements, not immediate work**. The reason to write them down now is that
each one leans on a *different* hard part of the architecture — realtime state,
custom UI hosting, menu injection, memory writes, per-role export — so keeping
them in view stops the greenfield build from painting itself into a corner that
is cheap to avoid today and expensive to undo later.

None of the three is scheduled. Treat this as a stress-test of the extension,
DSP-boundary, and export designs — the docs that actually resolve these
requirements are linked from each section and gathered under
[Why these shape the design](#why-these-shape-the-design).

## LSDj HD Player

A custom, oversized view of a running LSDj instance that uses LSDj's own fonts
and palettes, rendered much larger than the regular LSDj tile UI.

- Shows **all 4 channels at once**, plus **all currently-playing phrases**.
- Selectable from an LSDj instance's system menu as **"LSDj HD Player"**.
- When selected it **takes over the tiled UI layout** until it is closed — it is
  not a small adjacent widget; it replaces the tile grid for that session.
- It is driven entirely from **realtime emulator state pumped from the DSP
  thread**. The regions it reads and what each supplies:

  | Region | Supplies |
  | --- | --- |
  | SRAM | the song content |
  | RAM  | the current playback position (current phrase / chain / note, etc.) |
  | ROM  | the sample names of the kits that appear in a phrase |

  All of these are **sampled in realtime** from the DSP-pumped state, not read
  once at open time.
- As built it is **view-only** — there is no editing. Editing was intended as
  future work but was never part of what shipped.

## LSDj Sample Patcher

The patcher reuses the **same custom UI** as the HD Player, pointed at a
different job: managing an LSDj instance's sample kits.

- Appears as a **tile adjacent to** the LSDj instance being patched, styled to
  look like a new LSDj screen for editing samples.
- Shows the **current set of kits and their sample names**, and allows **adding
  new kits** with **realtime patching** capability.
- Planned but not built: an **overlay mode**, reached by a button combo, that
  overlays the patcher directly on top of the LSDj instance being edited — so it
  feels like a genuine new LSDj screen rather than a neighbouring tile.

## In-UI offline export

The CLI already performs offline, multithreaded rendering. The maintainer wants
that same capability surfaced as a **UI menu item**, so a user can export
directly from the interface rather than only from the command line.

The caveat that shapes the design: **export must be per-role, not a single
native call.** Different roles finish differently and emit different artifacts:

- An LSDj song should render **until an `HFF` (end) command is reached** — the
  same signal tools like `lsdpack` use to know a song has finished without being
  given a duration up front.
- An `lsdpack`-style `.gbs` export is a **register-write ripper**, not rendered
  audio.

Because "when is it done?" and "what comes out?" both depend on the role, export
cannot be one fixed native entry point. See
[05-export-and-render.md](./05-export-and-render.md).

## Why these shape the design

Each feature decomposes into a derived requirement that a later plan doc owns.
Recording the mapping here keeps the requirement traceable to the feature that
motivated it.

| Derived requirement | Comes from | Resolved by |
| --- | --- | --- |
| Realtime state **subscription** pump — memory regions + frame delivered at a rate (Hz) | HD Player, Patcher | [04-extension-model.md](./04-extension-model.md), [03-dsp-js-runtime.md](./03-dsp-js-runtime.md) |
| Custom React views hosted in **three modes** — replace / adjacent / overlay | HD Player (replace), Patcher (adjacent + overlay) | [04-extension-model.md](./04-extension-model.md) |
| **Menu injection** — "LSDj HD Player" appearing in the system menu | HD Player | [04-extension-model.md](./04-extension-model.md) |
| **Realtime memory writes** | Patcher | [04-extension-model.md](./04-extension-model.md), [02-dsp-data-model.md](./02-dsp-data-model.md) |
| **Per-role, dynamic-length / capture export** | In-UI export | [05-export-and-render.md](./05-export-and-render.md) |

Taken together the three features exercise the read path (subscribe to
DSP-pumped state), the UI-hosting path (three placement modes plus menu
contributions), the write path (patching kits back into a live core), and the
offline path (role-specific completion and artifact). Keeping all four in view
is the point of this doc.

## Links

- [../../../architecture/08-lsdj.md](../../../architecture/08-lsdj.md) — the LSDj
  subsystem (sav codec, kit compile/patch, what is and isn't DSP).
- [04-extension-model.md](./04-extension-model.md) — the extension model:
  subscriptions, hosted views, menu injection, memory writes.
- [03-dsp-js-runtime.md](./03-dsp-js-runtime.md) — the DSP ↔ JS runtime and the
  state pump.
- [02-dsp-data-model.md](./02-dsp-data-model.md) — the DSP-side data model the
  patcher writes through.
- [05-export-and-render.md](./05-export-and-render.md) — per-role, dynamic-length
  and capture-style export.
