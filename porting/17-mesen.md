# Step 17 — Mesen NES support

**Status:** Pending.

## Goal

Add a second emulator backend — Mesen — for NES support. Validates that
`SystemBase` was the right interface; surfaces wherever the abstraction needs
adjustment. Mesen also covers SNES, GB (a duplicate path with SameBoy), PC
Engine, SMS, GBA, WonderSwan in principle, but the migration scope here is
NES only (the legacy project stopped there).

## Depends on

- [Step 05](./05-multi-instance.md) (multi-system foundation).
- [Step 06](./06-midi-routing.md) (MIDI delivery to systems).
- Realistically, [step 02 (keyboard input)](./02-keyboard-input.md) needs
  extending to per-system button enums first.

## Architecture introduced

- **Mesen vendoring.** Move Mesen2 from
  [old/thirdparty/Mesen2/](../old/thirdparty/Mesen2/) to `deps/mesen/`. Mesen
  is C++ and substantially heavier than SameBoy — a new CMake target with a
  curated source list (NES-only subset to keep build time sane).
- **`MesenSystem : SystemBase`** at `src/system/mesen/MesenSystem.{hpp,cpp}`.
  Mirrors the SameBoy port shape: framebuffer triple-buffer at
  256×240, audio sample callback writes into a per-system stereo accumulator,
  `onProcess` drives the emulator until enough samples accumulate.
- **`MesenConfig`** — variant alternative in `SystemConfig`. Fields:
  ROM path/bytes, console type (initially fixed to NES), savestate.
- **Per-system button enums.** `NesButton` with values matching Mesen's
  internal enum. The `Command::Kind::ButtonPress` payload becomes
  `std::variant<GameboyButton, NesButton>` — or a flat `uint8_t button` plus
  a `SystemKind` discriminator. The variant is more typed; the flat form is
  smaller. Recommend the variant — the queue is bounded so size matters less
  than safety.
- **ROM kind detection.** Distinguish NES (`.nes`, iNES header) from GB. The
  sniffer (introduced step 07) gains a "what system can run this ROM"
  classification, feeding into `Project::addSystem(SystemConfig)` factory.
- **NES-specific roles.** Probably none for the first cut. Future: an
  Everdrive role for famitracker/MIDI use cases (out of scope; legacy code
  has it at [old/src/core/EverdriveComponents.h](../old/src/core/EverdriveComponents.h)).

## Tasks

1. Vendor Mesen NES core. Aggressively prune unused bits (debugger, GUI
   pieces).
2. Implement `MesenSystem` mirroring `SameBoySystem`'s shape. Pay close
   attention to the per-block sample-accuracy invariants.
3. Update the `Command::ButtonPress` payload to be system-kind-aware.
4. Update `LVGLPluginUI::onKeyboard` to pick the right button enum for the
   focused system.
5. Update the ROM picker to suggest the right `SystemConfig` variant for the
   chosen file.
6. Multi-system audio mixer already handles arbitrary stereo systems — verify
   no Game-Boy-specific assumptions leaked in.
7. Test: load an NES ROM. Render at 2× scale (the framebuffer is bigger than
   GB so adjust default tile size).

## Verification

- NES ROM loads, framebuffer renders at 256×240, audio plays.
- An NES + Game Boy in one project play simultaneously, mix to stereo.
- Per-system focus routes the right button enum: arrows, A, B, Start, Select
  reach the focused system whichever kind it is.
- Save/load: both systems round-trip through project state.

## Risks / open questions

- **Build size.** Mesen is a much larger codebase than SameBoy. Plugin binary
  may grow several MB. Acceptable; flag if it crosses host-format limits.
- **Mesen audio latency.** Mesen's audio handling expects `AudioDevice`
  abstractions; we're plugging in a callback. The legacy port at
  [old/src/mesen/](../old/src/mesen/) has the work done — port carefully.
- **Sample rate.** NES native audio rate is messy. Same resampling story
  as SameBoy (step 15) — internal native rate, r8brain to host.
- **Cross-system roles.** Most LSDJ-equivalent features for NES are
  trackers/famitone style — different shape. Defer the NES role universe
  entirely to user demand.
