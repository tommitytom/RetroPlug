# Step 02 — Keyboard input

**Status:** Done.

## Goal

Computer keyboard → Game Boy buttons. UI thread captures DPF window-level key
events, translates them, and ships them to the DSP via a typed lock-free SPSC
queue. SameBoy applies them at sample-accurate offsets within each audio block.

## Depends on

- [Step 01](./01-sameboy-mvp.md).

## Architecture introduced

- **`GameboyButton`** — enum at [src/system/InputTypes.hpp](../src/system/InputTypes.hpp).
  Values match SameBoy's `GB_key_t` so `SameBoySystem` passes them through with
  no translation. Other system kinds (Mesen) get their own button enums.
- **`Command`** — POD tagged-union with a `ButtonPress` payload at
  [src/transport/CommandQueue.hpp](../src/transport/CommandQueue.hpp).
  Future commands (load ROM, set setting) extend the same type.
- **`CommandQueue`** — bounded lock-free SPSC ring (1024 entries), hand-rolled.
  Allocation-free on both threads after construction. Single producer (UI),
  single consumer (DSP).
- **`SystemBase::pressButton(button, down)`** — virtual API for queueing button
  transitions on the audio thread.
- **`SameBoySystem::pendingButtons_`** — per-system deque of timed button
  transitions. Spread across the next audio block (10 ms spacing) so press +
  release pairs from a single UI tick don't collapse to zero duration.

## What landed

- `LVGLPluginUI::onKeyboard` overrides DPF's window-level keyboard hook. Maps
  arrows/Z/X/Enter/Shift/Backspace to `GameboyButton`, pushes a `ButtonPress`
  command to the queue. Returns `false` for unmapped keys so LVGL still routes
  them (e.g. Esc still toggles the React menu).
- `LVGLPluginDSP::run` drains the queue at the top of each block, dispatching
  via `Project::findSystem`.
- `SameBoySystem::onProcess` interleaves `pendingButtons_` application with
  `GB_run`: at each iteration, any pending transition whose offset has been
  reached gets applied via `GB_set_key_state`. Leftovers shift back into the
  next block.
- `SharedDSPData` exposes both `Project*` and `CommandQueue*` to the UI.

## Verification

- All targets build and link.
- In carla: Enter advances the LSDJ splash; Z/X drive menus; arrows navigate.
  Esc still pops the React menu (unmapped → falls through).

## Carry-forward limitations

- **Hardcoded mapping.** No user-configurable bindings. The old project used
  Lua for this; the new one will need an extension or a JSON-config equivalent.
- **No multi-instance focus.** Always sends to `trackedSystemId = 0`. When step
  05 adds the tile grid, this needs to follow tile focus.
- **DPF onKeyboard bypasses LVGL focus routing.** Right call for an emulator,
  but means UI components can only see keys we explicitly forward.

## Open questions

- **Per-system button enums.** When step 17 lands Mesen, the `Command::Kind`
  variant either gains a `NesButton` payload or `pressButton` becomes templated.
  Cheap to evolve; design at that step.
- **Sample-accurate timing.** 10 ms spacing is a heuristic carried from the old
  code. LSDJ Arduinoboy modes (step 09) may want a different cadence; revisit.
