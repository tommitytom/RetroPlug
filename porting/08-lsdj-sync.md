# Step 08 — LSDJ minimal sync

**Status:** Pending.

## Goal

The simplest LSDJ MIDI sync mode: external MIDI clock from the host drives
LSDJ's tempo. No Arduinoboy command set yet — just a clock cable. Establishes
the LSDJ-role plumbing (memory access, ROM offset detection, clock injection)
that the more complex modes in step 09 build on.

## Depends on

- [Step 07](./07-mgb-role.md) for the role-attachment + serial path.

## Architecture introduced

- **`LsdjSyncRole`** at `src/system/sameboy/roles/LsdjSyncRole.{hpp,cpp}`.
  Subscribes to MIDI events; on each MIDI clock tick (0xF8) emits a serial
  byte that LSDJ interprets as a sync pulse. Mode-aware: in step 08 it only
  implements `MidiSync` (LSDJ's basic external-clock mode).
- **LSDJ ROM offset table** at `src/system/sameboy/lsdj/Offsets.hpp`. Port
  from [old/src/lsdj/OffsetLookupData.h](../old/src/lsdj/OffsetLookupData.h)
  (the legacy `OFFSET_LOOKUP` map indexed by ROM hash). Maps known LSDJ
  versions to RAM offsets for tempo, current row, current chain, etc. Failure
  mode: unknown LSDJ version → role attaches but logs a warning and
  features-degraded.
- **`SameBoySystem::ramView(MemoryType)`** — direct accessor to SameBoy's
  RAM/ROM/SRAM buffers via `GB_get_direct_access`. Returns a
  `MemoryAccessor` (port from
  [old/src/core/MemoryAccessor.h](../old/src/core/MemoryAccessor.h)) that
  roles use.
- **`LsdjSyncConfig`** — alternative in `RoleConfig` variant. Fields:
  `mode: enum { MidiSync, /* others arrive in step 09 */ }`, `tempoDivisor:
  uint8_t`, `autoplay: bool`.
- **ROM hash detection.** Compute a hash (xxhash, already in deps) of the ROM
  bytes at load time, look up offsets. Cache on `SameBoySystem`.

## Tasks

1. Port `OffsetLookupData.h` and the matching utility from
   [old/src/lsdj/OffsetLookup.cpp](../old/src/lsdj/OffsetLookup.cpp). Strip
   orb dependencies.
2. Implement `LsdjSyncRole::onAttach` — compute ROM hash, look up offsets,
   stash on the role.
3. Implement `LsdjSyncRole::onMidi` for clock messages (status byte 0xF8).
   On each tick, increment a counter and write to LSDJ's sync byte at the
   right RAM offset (or write a serial byte — check old impl
   [old/src/lsdj/LsdjAudioHooks.cpp](../old/src/lsdj/LsdjAudioHooks.cpp)).
4. Implement `LsdjSyncRole::onProcessBlock` for autoplay if configured: detect
   LSDJ's "stopped" state (read tempo/row offsets from RAM), trigger play
   automatically if the host transport says playing.
5. Update the ROM sniffer to emit `Lsdj` for any LSDJ-shaped ROM, and have
   `SameBoySystem::onActivate` add an `LsdjSyncRole` with default mode.
6. UI menu: "LSDJ → Sync mode" picker (just `Off` / `MidiSync` for now).

## Verification

- Load LSDJ. `LsdjSyncRole` logs an attachment with the matching offset table.
- Set sync mode to `MidiSync` in the menu. Press play in the host.
- LSDJ syncs to host tempo: starting/stopping the transport drives LSDJ's
  playback in lockstep.

## Risks / open questions

- **Unknown LSDJ versions.** New LSDJ releases break the offset table. Plan
  for the role to log clearly when the ROM hash is unknown, and fall back to
  passive sync only.
- **Clock drift at non-44.1 kHz host rates.** Without resampling (step 15)
  the GB ticks at the host rate, not 4194304/512 Hz. Step 15 lands before
  this becomes audible at typical tempos but flag it.
- **Tempo divisor.** LSDJ supports 1/2/4/8 divisor. Wire as a config field;
  default to 1 (24 PPQN).
- **Read-modify-write on shared RAM.** Writing to LSDJ's sync byte from the
  role requires the role and SameBoy to coordinate ownership of that memory
  region. Both run on the audio thread, so no atomicity issue, but ordering
  matters: write before `GB_run` sees the new state.
