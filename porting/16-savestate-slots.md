# Step 16 — Savestate slots

**Status:** Pending.

## Goal

Mid-session save/load — pause-and-rewind workflow. Distinct from
[step 04](./04-project-state.md)'s host-driven project save: this is a
user-driven "save right now, restore later" mechanism, with multiple slots
per system and a UI for managing them.

## Depends on

- [Step 04](./04-project-state.md) for the serialization helpers.

## Architecture introduced

- **`SameBoyConfig::savestateSlots`** — `std::vector<SameBoySavestate>` where
  each slot has a name, timestamp, and `std::vector<uint8_t>` of bytes from
  `GB_save_state_to_buffer`. The bytes round-trip via project state save —
  this is intentional, savestate slots are part of the project.
- **rpcpp methods**:
  - `saveSlot(systemId, slotIndex, name) -> { ok, error? }`
  - `loadSlot(systemId, slotIndex) -> { ok, error? }`
  - `deleteSlot(systemId, slotIndex)`
  - `listSlots(systemId) -> [{ index, name, timestamp }]`
- **DSP-side handling.** `saveSlot` calls `GB_save_state_to_buffer` and
  stuffs into the slot. `loadSlot` calls `GB_load_state_from_buffer` —
  must be applied at top of next `run()` (audio glitches otherwise are fine
  but tearing is not). Use a "wants-restore" flag with the slot bytes
  alongside.
- **Hotkeys.** Convention: F5 = quicksave, F9 = quickload (slot 0). Wire via
  `LVGLPluginUI::onKeyboard` alongside the existing emulator-button mapping.
  Don't pass through to the emulator (consume the event).
- **UI panel.** `<SavestatePanel/>` either built-in or as a TS extension.
  Lists slots, allows naming, save/load buttons.

## Tasks

1. Extend `SameBoyConfig` with the slots vector.
2. Implement the four rpcpp methods.
3. DSP applies `loadSlot` at the top of the next `run()` to avoid mid-block
   restoration tearing.
4. Hotkey handlers in `LVGLPluginUI::onKeyboard`.
5. UI panel.

## Verification

- Quicksave with F5; play for a few seconds; quickload with F9; emulator
  rewinds to the saved point.
- Multiple slots persist across project save/load.
- Loading a slot recorded under a different ROM is rejected (compare ROM
  hash). Show a clear error in the UI.

## Risks / open questions

- **Slot count limits.** SameBoy savestates are typically 100-200 KiB.
  10 slots × 4 systems × 200 KiB = ~8 MiB inside the project file. Acceptable
  with step 04's compression. Cap at 20 slots/system to be safe.
- **Cross-version savestate compatibility.** SameBoy bumps `STRUCT_VERSION`
  occasionally; old slots become unreadable. Detect, log, mark slot as
  invalid in the UI.
- **Lsdj sync state in savestates.** A loaded slot may put LSDJ mid-row;
  the role's sync counter should reset on restore. Add a `RomRole::onReset`
  call after savestate restoration.
- **MIDI state during restore.** Note-offs may go missing if a slot is
  restored mid-note. Send "all notes off" through any active output role
  on restore.
