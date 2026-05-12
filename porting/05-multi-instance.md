# Step 05 — Multi-instance + tile grid

**Status:** Done.

## Goal

Run more than one Game Boy at a time. Add a React-side tile grid with Tab
navigation, per-instance focus, an audio mixer that sums N stereo streams with
soft-clipping, and per-system gain. This is the first step where the
"design for N, run with 1" architecture decisions actually pay off.

## Depends on

- [Step 03](./03-rom-picker.md) (need a way to load multiple ROMs).
- [Step 04](./04-project-state.md) (need to save/load N-system projects).

## Architecture introduced

- **`ProjectConfig::layout`** — `enum class SystemLayout { Auto, Row, Column,
  Grid }`, ported from the legacy [old/src/core/ProjectState.h](../old/src/core/ProjectState.h).
- **`SameBoyConfig::gainDb`** — per-system trim, applied in `onProcess` before
  summing into outputs.
- **`AudioRouting`** — `enum class { StereoMixDown, TwoChannelsPerInstance,
  TwoChannelsPerChannel }`. MVP implements `StereoMixDown` only; the others
  need DPF audio-port count to vary, which is host-format dependent. Defer
  multi-channel modes to a follow-up.
- **`PluginShared::focusedSystemId`** — atomic `SystemId` set by UI focus
  changes; read by `LVGLPluginUI::onKeyboard` to route key events to the right
  system.
- **TS: `SystemTile`** — React component rendering one framebuffer + (later)
  any TS-extension overlays. Tab cycles focus. Esc still pops the menu.
- **TS: `SystemGrid`** — flex container that arranges tiles per
  `ProjectConfig::layout`.
- **`Command::AddSystem(SystemConfig)` / `RemoveSystem(SystemId)`** — already
  partly in step 03; finalize for multi-instance.

## Tasks

1. **Audio mixer.** Update `LVGLPluginDSP::run` to clear outputs once and let
   each system's `onProcess` *sum* into them. Apply per-system gain inside
   `SameBoySystem::onProcess`. Apply a soft-clip (tanh or simple cubic) at the
   master stage to prevent N>1 systems from clipping.
2. **System layout.** Move the framebuffer-display logic out of
   `LVGLPluginUI`'s C++ ctor (which currently creates a single `lv_image`) and
   into a per-tile thing. Two options:
   - Continue with C++-direct rendering: `PluginUI` maintains a `vector<lv_obj_t*>`
     of framebuffer widgets, repositioning on layout change.
   - Switch to a real React `<EmulatorTile/>` component now (requires fixing
     lv_binding_js's Canvas component).
   Recommendation: stay with C++-direct + make tile positions data-driven from
   the React tree. Switch to React-driven framebuffers when the lv_binding_js
   Canvas widget exists (probably alongside step 13).
3. **Focus + Tab routing.** UI captures Tab → cycles `focusedSystemId`. DSP
   reads it to dispatch keypresses. Don't reroute `Esc` (still goes to menu).
4. **Add/remove flow.** Wire `Command::AddSystem` / `RemoveSystem` against the
   command queue, not rpcpp — these are user actions but expected to be
   immediate. Or keep them on rpcpp for the request/response confirmation
   shape; both work.
5. **Layout chooser** in the React menu (Auto / Row / Column / Grid).

## Verification

- Load two ROMs (e.g. an LSDJ and a homebrew). Both render in the tile grid.
- Press Tab — focus moves to the next tile (visual indicator: brighter border?)
- Press a button: only the focused tile responds.
- Audio: both ROMs mixed into the stereo output. Adjust per-system gain — only
  that tile's audio attenuates.
- Save/load: layout, gain trims, and ROM bytes round-trip.

## Risks / open questions

- **Focus visual indicator.** Need a reliable way to show "this tile owns
  keys" — a coloured border on the focused tile is the simplest.
- **Tile sizing.** Auto-layout based on N: 1=center, 2=row, 3-4=2x2,
  more=square-ish. Match the old project's layout heuristic if there's one.
- **CPU scaling.** Each instance is independently emulated at full speed.
  4× SameBoy is fine on modern CPUs but worth measuring. Mesen (step 17) is
  much heavier; keep the mixer lean.
- **MIDI routing.** Cross-cuts step 06. Worth landing 06 in tandem to verify
  per-system MIDI delivery from day one of multi-instance.
