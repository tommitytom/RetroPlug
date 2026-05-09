# Step 09 — LSDJ Arduinoboy modes

**Status:** Pending.

## Goal

Implement the full Arduinoboy command surface that LSDJ users rely on: master
mode (LSDJ generates MIDI clock for outboard gear), keyboard/note mapping
modes, song-row triggering. Each mode is selectable per-system at runtime.

## Depends on

- [Step 08](./08-lsdj-sync.md) for the LSDJ role + offset infrastructure.

## Architecture introduced

The legacy implementation lives across
[old/src/lsdj/LsdjController.cpp](../old/src/lsdj/LsdjController.cpp),
[old/src/lsdj/LsdjAudioHooks.cpp](../old/src/lsdj/LsdjAudioHooks.cpp), and the
LSDJ settings header. Modes:

- `MidiSync` — already in step 08.
- `MidiSyncArduinoboy` — full Arduinoboy protocol; LSDJ commands (play/stop,
  tempo) are encoded as MIDI bytes back to the host, plus the sync clock.
- `MidiMap` — direct note mapping. MIDI notes trigger LSDJ phrases by index.
- `Keyboard` — host keyboard input mapped to LSDJ note table.
- `KeyboardMidi` — keyboard, but driving LSDJ via MIDI internally.
- `MidiPassthrough` — bypass Arduinoboy logic; raw MIDI to LSDJ's serial port
  (effectively MGB behavior on an LSDJ ROM).

In the new architecture, modes are *configuration* on a single `LsdjSyncRole`
(simpler, mirrors the legacy `LsdjAudioHooks`) rather than separate roles.
Reason: modes are mutually exclusive on a given system, and they share enough
state (clock counter, last-row tracking, tempo divisor) that splitting them
adds bookkeeping with no win.

- **`LsdjSyncConfig::mode`** — extended `enum class LsdjSyncMode { Off,
  MidiSync, MidiSyncArduinoboy, MidiMap, Keyboard, KeyboardMidi,
  MidiPassthrough }`. reflectcpp serializes by name.
- **`LsdjSyncRole` mode dispatch** — `onMidi` and `onProcessBlock` switch on
  `mode_` and call into per-mode helpers. Each helper is a 50-150 line port
  from the legacy hooks.
- **MIDI output.** `MidiSyncArduinoboy` master mode emits clock + transport
  messages back to the host. Use the per-system `midiOut` queue scaffolded
  in step 06; gather into DPF's `writeMidiEvent` at end-of-block.
- **`LsdjAudioStateComponent`-equivalent runtime state** on the role:
  `lastRow`, `tempoDivisor`, `keyboardOctave`, `arduinoboyPlaying`. Snapshotted
  via `snapshotConfig` if needed for persistence (most of these are transient
  and shouldn't persist).

## Tasks

1. Port mode-handling code from `LsdjAudioHooks.cpp` and `LsdjController.cpp`.
   The hardest mode is `MidiSyncArduinoboy` — port carefully, line-by-line.
2. Wire the `midiOut` path so master-mode clock reaches the host.
3. UI menu: extend the LSDJ submenu so the user can pick mode + tempo divisor
   + autoplay. Persist via `LsdjSyncConfig`.
4. Tests: at minimum smoke-test each mode in carla with a MIDI loopback.

## Verification

- `MidiSyncArduinoboy`: drive a hardware MIDI device from LSDJ's transport.
  Stop/start LSDJ → observe MIDI clock + start/stop bytes.
- `MidiMap`: send notes from a MIDI keyboard → LSDJ triggers phrases.
- `Keyboard`: type on the computer keyboard → LSDJ plays notes (separate from
  the d-pad/A/B mapping established in step 02).
- Save/load: mode + tempo divisor survive a project round-trip.

## Risks / open questions

- **Sample-accurate clock.** Arduinoboy master mode generates clock from the
  emulator's tempo — LSDJ's tempo register is read each block, then a clock
  pulse is queued for output. Drift over long playback if not resampled
  properly (step 15).
- **Mode switching mid-playback.** Some modes leave LSDJ in a partial state
  (e.g. mid-row). Need to define a "safe" reset on mode change.
- **Keyboard mode collision with d-pad.** Step 02's hardcoded keyboard
  mapping covers Z/X/arrows for d-pad. `Keyboard` mode wants letter keys for
  notes. Need to gate the d-pad mapping when `Keyboard` mode is active. Add
  a `LsdjSyncRole::wantsKeyboard()` query to `SystemBase` and route around it.
