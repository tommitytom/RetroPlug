# Step 13 — LSDJ HD player

**Status:** Pending.

## Goal

Render LSDJ's UI at large scale, redrawn from emulator state rather than
upscaled framebuffer. Replaces (or covers) the small framebuffer view when
active. Built as the framework's flagship reference TS extension.

## Depends on

- [Step 12](./12-ts-extensions.md) (extension framework).
- [Step 11](./11-memory-snapshots.md) (RAM polling).

## Architecture introduced

- **Extension `ui/extensions/lsdj-hd/`**. Activates per-system when the
  system's role list contains an `LsdjSyncRole`.
- **LSDJ font rendering.** Port the bitmap font + glyph layout from
  [old/src/lsdj/LsdjCanvas.cpp](../old/src/lsdj/LsdjCanvas.cpp). Either:
  - Pre-render to PNG/spritesheet and use LVGL's `lv_image` per-glyph (simpler,
    cheap to build).
  - Or implement an `LV_FONT_FMT_TXT_SMALL_INDEXED` font and use LVGL's `lv_label`
    (matches LSDJ's color/highlight semantics better but more upfront work).
  Recommend the spritesheet for first iteration.
- **State decoder.** A TS module that reads RAM via `useMemory(systemId,
  Ram, 60)`, walks LSDJ's data structures (current screen, song row, chain,
  phrase, instrument, table), and produces a React tree representing what
  LSDJ would draw. Port the decoding from
  [old/src/lsdj/Ram.h](../old/src/lsdj/Ram.h) and the LSDJ docs.
- **Layout.** Match LSDJ's 8×16 character grid, scaled. The decoder emits
  `<Cell row col fg bg char>` elements; a CSS-grid-style React component lays
  them out.
- **Cursor + highlight.** LSDJ uses inverse-color highlighting for cursor
  position; the decoder reads cursor RAM offsets and applies the highlight at
  the right cell.
- **Tile takeover.** When this extension is active for a system, the
  small framebuffer is hidden and replaced by the HD canvas. `Extension`
  interface gains a `replacesTile?: boolean` flag; if true, the framework
  hides the C++ `lv_image` for that slot.

## Tasks

1. Port the LSDJ font + glyph table.
2. Port the RAM decoder.
3. Build the React component tree.
4. Wire the `replacesTile` switch in the framework.
5. Add a menu toggle: "LSDJ → HD mode" per system.

## Verification

- Toggle HD mode on. The 160×144 framebuffer is replaced by a large LSDJ-style
  view that updates as the user navigates through chains, phrases, etc.
- Toggle HD mode off. Back to framebuffer.
- Play LSDJ — cursor moves, highlights correctly, UI tracks state at 60 Hz
  with no stutter.

## Risks / open questions

- **Glyph fidelity.** LSDJ's font has version-specific tweaks. Pin to a known
  LSDJ version's font; document that other versions render at the version's
  font too.
- **Performance.** Rendering 8×16=128 cells × ~30 columns at 60 Hz is fine on
  modern CPUs. If R-side reconciliation costs add up, switch to a manual
  draw against an `lv_canvas` (when one exists).
- **LSDJ version coverage.** Memory layout drift across LSDJ releases means
  the decoder needs offset tables (similar to step 08's sync offsets). Reuse
  the offset infrastructure.
- **Multi-system HD.** With N tiles in HD mode, rendering cost scales
  linearly. Encourage usage on the focused tile only — auto-disable HD on
  unfocused tiles by default.
