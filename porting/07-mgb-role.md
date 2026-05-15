# Step 07 — MGB passthrough role

**Status:** Done.

## Goal

Land the first concrete `RomRole`. MGB (a Game Boy MIDI ROM) wants raw MIDI
bytes forwarded as-is to the GB's serial port — no Arduinoboy translation, no
clock sync, just a MIDI cable into the link cable. Validates the role
abstraction and the ROM-sniffer that picks roles at load time.

## Depends on

- [Step 06](./06-midi-routing.md) (need MIDI flowing into systems).

## Architecture introduced

- **`RomKind` ROM sniffer.** `enum class { Generic, Lsdj, Mgb }` and a
  `detectRomKind(const std::vector<uint8_t>& rom) -> RomKind` function that
  reads the GB header (title at 0x134, optional MGB sentinel byte). Lives at
  `src/system/sameboy/RomSniffer.{hpp,cpp}`.
- **`MgbPassthroughRole`** at `src/system/sameboy/roles/MgbPassthroughRole.{hpp,cpp}`.
  Implements `RomRole::onMidi` by pushing raw MIDI bytes (status, data1, data2)
  into the system's serial buffer. SameBoy's serial transfer callbacks read
  from that buffer.
- **`SameBoySystem::serialIn_`** — a `FixedQueue<uint8_t, 256>` that the serial
  transfer-bit-start callback drains one bit at a time. Mirror of old
  [old/src/core/SystemTypes.h](../old/src/core/SystemTypes.h)'s `SystemIo::input.serial`.
  In step 01 we declared trivial `serialStart`/`serialEnd` callbacks; this
  step replaces them with real ones.
- **`SameBoySystem::onActivate` ROM-role attachment.** After ROM load,
  `detectRomKind` runs; based on the result, built-in roles are pushed onto
  `roles_`.
- **`RoleConfig`** — first concrete inhabitant: `MgbRoleConfig {}` (empty for
  now; add fields like a transpose offset later if useful). The `std::variant`
  in `SystemConfig.hpp` gains its first alternative.

## Tasks

1. Implement `detectRomKind`. The legacy code's logic is in
   [old/src/lsdj/Rom.h](../old/src/lsdj/Rom.h) and the LSDJ utils — port the
   relevant bits.
2. Implement `MgbPassthroughRole`. Methods: `onAttach`, `onMidi` (write to
   `serialIn_`), `snapshotConfig` (return `MgbRoleConfig{}`).
3. Wire the SameBoy serial-transfer callbacks to drain `serialIn_` one bit at
   a time. Reference the old `serialStart`/`serialEnd` at
   [old/src/sameboy/SameBoyUtil.cpp:201-203](../old/src/sameboy/SameBoyUtil.cpp#L201-L203)
   to see what a real implementation looks like.
4. Update `Project::addSystem` (variant dispatch) so `SameBoyConfig.roles`
   is replayed on load: each `RoleConfig` alternative gets matched to a
   concrete role and pushed onto the runtime `roles_`.
5. Test with an MGB ROM: send notes via MIDI, hear them.

## Verification

- Load `mGB.gb` (or any version). `detectRomKind` returns `Mgb`.
- `[RetroPlug] attached MGB passthrough role to system X` logs.
- Connect a MIDI source in carla. Notes play through mGB's audio.
- Save project → reload: roles vector restored from `RoleConfig` variant.

## Risks / open questions

- **Serial byte ordering.** Game Boy serial is bit-by-bit, MSB first. The old
  code pumped bytes in via a `TimedByte` queue with frame timing; check
  whether MGB needs that timing or whether "as fast as possible" works.
- **Role attachment vs config.** Roles are registered automatically by the
  sniffer, but the user might want to *override* (e.g. force a specific role
  on a custom ROM). Solution: `SameBoyConfig::roles` is the source of truth at
  load time; if empty, the sniffer fills it in. Manual edits via UI overwrite
  the sniffer's choice.
- **Multiple MGB-like ROMs.** Some ROMs implement MGB-compatible MIDI
  protocols. Treat the sniff as a heuristic, not a contract.
