# iOS AUv3 spike: SameBoy core + mGB as an iPad Audio Unit, with a standalone player app

## Summary

This branch brings RetroPlug to iOS as an AUv3 instrument (`aumu` / `mgbs` / `RPtm`) plus a SwiftUI container/player app, by wrapping the desktop emulator core in a new Xcode project under `ios/`. The AU loads the embedded mGB synth by default (silent until MIDI arrives), lets the user swap in a ROM from Files directly in the plugin UI, exposes the four GB channels as separate output busses, ports the desktop's full LSDj MIDI-sync role natively, and persists its complete session (cartridge, battery RAM, savestate, settings) into the host DAW's project file.

On top of that base it reaches **full Arduinoboy settings parity** — the per-mode MIDI channel assignments and MI.OUT CC matrix the hardware's maxpat Editor configures over SysEx are AU parameters here — adds a **GB Note Out** mode (MIDI out from *any* LSDj build via an APU register tap, no MI.OUT-patched ROM needed), an **LSDj song manager** in the player, and per-instance **mGB channel blocks** for multi-instance DAW setups.

The work is **all additive**, in three waves:

- `791a9879` — the AUv3 spike: SameBoy core + mGB as an iPad audio unit
- `4dbf896b` — the standalone player app + LSDj sync parity in the AU (also includes DAW session persistence, the in-plugin ROM picker, and the iPad orientation fix)
- the branch tip — Arduinoboy Editor settings parity (channels + CC matrix + mGB base channel), GB Note Out, the LSDj song manager, and the About/licensing + privacy-manifest housekeeping

## What changed in the parent (shared) tree

**Small and deliberate.** Two kinds of changes outside `ios/`:

**Inert, repo-level additions:**

| File | What it is |
|---|---|
| `.gitignore` | Ignores the iOS derived outputs (`ios/generated/`, `ios/.deps/`, `ios/.build/`, the xcodegen-emitted `.xcodeproj` and `Info.plist`s, `xcuserdata/`) |
| `.claude/settings.json` | Agent tool permissions for `xcodegen`/`xcodebuild` (dev tooling only) |
| `retroplug.xcworkspace/contents.xcworkspacedata` | A one-file workspace pointing at `ios/RetroPlugIOS.xcodeproj` so the iOS project opens from the repo root |

**The APU register-write tap** (backs the GB Note Out mode; inert on desktop):

- `cmake/patches/sameboy-per-channel-audio.patch` — the already-tracked SameBoy patch grows a register-write hook in `GB_apu_write` alongside the existing per-channel sample tap. Same patch file, applied by the same idempotent mechanism on both platforms.
- `packages/native/src/system/sameboy/SameBoySystem.{cpp,hpp}` — an opt-in per-block APU write log (`setApuWriteCapture` / `apuWriteLog_`), following the serial-out capture's exact gate pattern: **nothing runs unless a host arms it**, and nothing on desktop arms it (yet — the same log could back a desktop Note Out later).
- `docs/lsdj.md` / `AGENTS.md` — documentation for the above.

Still **not** touched: `packages/retroplug/` (the TS/UI side), `cmake/` build scripts, `build.sh`/`build.bat`, and every submodule pointer (`deps/sameboy`, `deps/dpf.js`, etc.). The desktop macOS / Windows / Linux builds compile from the same sources plus the extended-but-unarmed tap, and their behavior is unchanged.

### Shared code consumed read-only by the iOS targets

The iOS project deliberately *reuses* the parent tree rather than forking it:

- **Compiled directly into `RetroPlugKit`** (via `project.yml` source references): `packages/native/src/system/SystemBase.cpp`, `system/sameboy/SameBoySystem.cpp`, `system/sameboy/LinkGroup.cpp`, and `deps/sameboy/Core` (built with `GB_INTERNAL`, warnings silenced — mirroring `cmake/sameboy.cmake`'s non-Windows path).
- **`cmake/patches/sameboy-per-channel-audio.patch`** — the same tracked patch the desktop configure applies is applied idempotently by `ios/generate.sh` (same `git apply --reverse --check` dance). This is why `deps/sameboy` shows as dirty; that is expected on both platforms.
- **`resources/roms/mGB.gb`** and the SameBoy boot-ROM sources — embedded/assembled by `ios/generate.sh` into `ios/generated/` (gitignored), replicating `cmake/bin2h.cmake` / `bin2c.cmake` / `sameboy_bootroms.cmake` with host tools only (RGBDS + cc + python3), so no cross-compiled build helpers are needed.
- **Ported, not modified**: the AU's MIDI-sync engine is a native C++ port of the desktop TS role (`packages/retroplug/src/dspRoles.ts`, `lsdjKeyboardMap.ts`, `lsdjArduinoboy.ts`), and the render loop mirrors `packages/native/plugin/PluginDSP.cpp`'s prepare → step → finish triad. The TS originals are unchanged; parity drift between the two is a maintenance consideration called out in `ios/README.md`.

## What is confined to iOS (everything else)

All under `ios/`, three targets defined by `project.yml` (xcodegen):

### `RetroPlugKit` (dynamic framework — shared by app and extension)

- `RetroPlugAudioUnit.mm` — the AUv3 ↔ SameBoySystem bridge:
  - **Multi-out**: 5 stereo busses — bus 0 = mix, busses 1–4 = Pulse 1 / Pulse 2 / Wave / Noise stems (the desktop ChannelSplit routing). Renders once per quantum, caches lanes for per-bus render calls; the mix is summed from the stems.
  - **Threading model**: a lock-free SPSC command ring for realtime-cheap ops (buttons, reset, gain, fast-boot) plus a Dekker-handshake "bypass gate" for heavy ops (ROM swap, model change, state save/load) during which the render block emits silence.
  - **MIDI sync, full desktop parity** (AU parameters `syncMode` / `syncTempoDivisor` / `syncAutoStart`, so DAWs can automate them): mGB passthrough, LSDj MIDI sync (drift-free 24/divisor-PPQ tick walk off the host transport), Arduinoboy slave (note 24/25 arm, 26–29 divisor, 0xFA/0xFC bookends), MIDI Map (row jumps), Keyboard (PS/2 scancodes), and two *output* modes — MI.OUT (flag-framed serial → host MIDI notes/CC/PC) and Master Sync (LSDj clocks the DAW) — via a published AU MIDI output port.
  - **GB Note Out (beyond desktop parity)**: a third output mode that derives MIDI from what the emulated sound hardware is *told to play* (the shared-tree APU write tap) instead of what the ROM transmits over serial — so it works with **any** LSDj build or any ROM, no MI.OUT-patched "Arduinoboy" build required. Triggers → NoteOn (velocity from envelope, strict mono per voice with explicit NoteOffs), envelope → CC7, pan → CC10, pulse duty → CC70, slides/vibrato → pitch bend (±2 semitones, legato retrigger beyond).
  - **Arduinoboy Editor settings parity**: the per-application settings the hardware stores in EEPROM (configured by trash80's maxpat Editor over SysEx) are AU parameters, seeded with the firmware factory defaults, host-automatable, and persisted via `fullState`: per-mode MIDI channel assignments (slave sync / master sync / keyboard / MIDI map, mGB's five voice channels with unassigned-channel drop, MI.OUT's per-voice note+PC and CC channels — shared by Note Out); an **mGB base channel** (0 = per-voice assignments; 1–12 = the voices sit at base..base+4, so each DAW instance takes its own channel block with one knob); and the full **MI.OUT CC matrix** — per voice a CC mode (single-CC vs. LSDj `X` high-digit selecting one of seven CC numbers), a scaling flag, and the seven assignable CC numbers, ported verbatim from the firmware's `playCC` (defaults 1/2/3/7/10/11/12, which line up with mGB's own CC map so a MI.OUT instance can drive an mGB instance directly).
  - **DAW session persistence (`fullState`)**: saves/restores the cartridge (raw bytes for user ROMs; just the id string `"mgb"` for the built-in synth, so no ROM ships anywhere it isn't already), SRAM, a full savestate, and model/highpass/fast-boot/gain into the host project plist. Versioned, type-checked, tolerant of pre-feature state; funnels host XPC-thread access onto the main thread to preserve the control plane's single-writer contract. On session restore the saved cartridge replaces the default silently. User ROMs live only in user data (app Documents + the user's DAW project) — never in the app bundle; the only ROM embedded in the binary is mGB, which was already there.
- `RetroPlugCoreBridge.h` — the main-thread-only control surface (load ROM/SRAM/state, model, buttons, video frame reads via a lock-free triple buffer, autosave SRAM snapshots). Includes `loadSram:` for manual `.sav` loads: swaps the cartridge's battery RAM under the bypass gate and reboots (LSDj only reads its save at boot).
- `LsdjSav.swift` (in `Shared/`) — a minimal Swift port of the desktop LSDj codec (`packages/retroplug/src/lsdj/codec/{sav,rle}.ts`): sav-header parse (song names/versions/active slot) and the RLE block decompressor. Songs stay raw 0x8000-byte blobs — the emulator is the editor; this only lists and loads.

### `RetroPlugAU` (app-extension)

- `AudioUnitViewController` — the AUAudioUnitFactory plus a minimal plugin UI: a status label, **Load ROM…** and **Load .sav…** (Files document pickers, copied into the extension sandbox so no security-scope bookkeeping), and **Reset to mGB synth**. Whatever is loaded persists with the host project via `fullState`. Sibling `.sav` pairing stays app-only (a picker scope never covers directory siblings); DAW sessions carry SRAM in project state instead.
- Info.plist declares the component (`aumu` / `mgbs` / `RPtm`, sandbox-safe); the parent app embeds the framework, the appex reaches it via rpath.

### `RetroPlug` (SwiftUI app)

- Emulator screen (`GameBoyScreenView`, shared with the extension), touch controls, physical game-controller input, ROM library backed by Documents/ with Files-app sharing (`UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace` — the only sandbox-legal way ROM/.sav sibling pairing works), player settings, and UTI declarations for `.gb` / `.gbc` / `.sav` / `.rplg`.
- **LSDj song manager** (`LsdjSongsSheet`, in the player's toolbar menu): lists the stored projects in the running cart's battery RAM (name, version, active-slot marker) with tap-to-load (decompresses the song into working memory, marks it active, reboots — the archive is untouched so LSDj's own LOAD/SAVE screen still sees everything), plus a manual **Load .sav File…** importer that replaces battery RAM wholesale. Loading confirms only when it would lose something: the working song is compared byte-for-byte against its slot's stored copy, and a clean match loads silently. The previous battery save is written out before any swap.
- Supports all four orientations on iPad (required since the app doesn't set `UIRequiresFullScreen` and keeps Split View support).

### Tooling

- `ios/generate.sh` — idempotent one-time codegen (boot ROMs, embedded mGB, `GB_VERSION`, reflect-cpp headers); run before `xcodegen`.
- `ios/README.md` — build steps, architecture, phase plan.

## macOS / desktop impact

None. There is no macOS target in this branch; the macOS plugin remains the DPF build from `packages/native/plugin`, untouched. The only thing a desktop developer will notice is the new `retroplug.xcworkspace` at the repo root and the extra `.gitignore` entries.

## Testing

- iOS Debug build (device arm64) compiles clean — zero warnings in the new targets.
- Desktop parity of the sync modes is by construction (line-for-line ports of the TS role); the desktop's own `reaper:lsdj-*` timing matrix still applies to the desktop build, which this branch doesn't alter.
- Not yet covered (known follow-ups): automated tests for the AU (no iOS twin of `test:plugin` yet), `auval` validation on device, and host-matrix testing (AUM / Logic for iPad / Cubasis) of multi-out, the in-plugin ROM picker, and fullState round-trips.
