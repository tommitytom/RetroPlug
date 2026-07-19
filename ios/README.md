# RetroPlug iOS

The iPad port of RetroPlug's audio core: the SameBoy emulator + the embedded
mGB ROM (Game Boy MIDI synth) as an **AUv3 instrument** (`aumu` / `mgbs` /
`RPtm`), shipped inside a **standalone SwiftUI player app** that is itself a
usable Game Boy player (ROM library, touch controls, game controllers,
battery saves, savestates).

**What this is NOT (yet):** no Engine/QuickJS control plane or project layer,
no Mesen (NES/GBA), and the AUv3 extension's own view is still a placeholder
label. See the phase list at the bottom.

## Prerequisites

```sh
brew install rgbds xcodegen        # boot-ROM assembler + project generator
git submodule update --init deps/sameboy
```

An Apple Developer team signed into Xcode (Settings → Accounts). The project
defaults to the Toilville LLC team (`72D54NN2X5`); edit `DEVELOPMENT_TEAM` in
`project.yml` to use another (e.g. the free Personal Team, `C9ST9QM58E` —
note free teams re-expire installs after 7 days).

## Build & sideload

```sh
cd ios
./generate.sh        # boot ROMs (RGBDS) + embedded mGB ROM + reflect-cpp headers
xcodegen             # emits RetroPlugIOS.xcodeproj
open ../retroplug.xcworkspace
```

In Xcode: select the **RetroPlug** scheme, pick your iPad (plugged in or on
the same network with developer mode enabled), Run. First install on a new
device: trust the developer profile in Settings → General → VPN & Device
Management.

- The **app** is a standalone player — load a `.gb`/`.gbc` from Files or the
  built-in library, or start the embedded mGB synth and play its note pads
  (MIDI ch 1–4 = pu1 / pu2 / wav / noi).
- The **AUv3** registers with the system on first app launch and then appears
  in AUM, GarageBand, Cubasis, etc. as *tommitytom: RetroPlug mGB*.

## The app

- **Library** — `Documents/roms|saves|states`, exposed in the Files app
  (`UIFileSharingEnabled`), so ROMs (and sibling `.sav` / `.rplg` files) can
  be dropped in directly or imported via the document picker (multi-select a
  ROM together with its `.sav`/`.rplg` — the picker's security scope doesn't
  extend to siblings). Sorted most-recently-played first.
- **`.rplg` sidecars** — a thin desktop project (raw JSON, not `.rplg.zip`)
  next to a ROM carries its SameBoy role config (model, fast boot) and
  lsdj-sync role config (sync mode, tempo divisor, auto-start) across.
  Decoding is forward-tolerant like the desktop role-config path; projects
  stamped with a newer schema are ignored rather than half-applied.
- **Input** — touch controls (one gesture surface for the D-pad, so diagonals
  and finger slides work) or physical game controllers (MFi / PlayStation /
  Xbox, positional Nintendo mapping; Menu/Options = Start/Select).
- **Saves** — battery SRAM is written on eject/load-over and when the app
  backgrounds; savestates get one slot per ROM. mGB's synth settings live in
  cartridge RAM and persist the same way.
- **LSDj songs** — the running cart's SRAM is parsed as an LSDj save (song
  list, active slot, working-song-dirty check), with a song manager sheet to
  load a stored song into working memory and a manual `.sav` load that
  replaces battery RAM wholesale.
- **Screen** — a `CADisplayLink` pulls the emulator's latest frame from a
  lock-free triple buffer and blits it to a `CALayer` (`GameBoyScreenView`,
  shared with the extension target) — SwiftUI's view graph stays out of the
  60 Hz loop.
- **Settings** — SameBoy model (Auto/DMG/SGB/CGB/AGB…), fast boot, gain, the
  MIDI sync mode, and the active mode's channel assignments / CC matrix (see
  below); persisted in `UserDefaults` and pushed into the AU on launch.

## The audio unit

- **Multi-out**: 5 stereo busses — bus 0 the stereo mix, busses 1–4 the four
  GB channel stems (Pulse 1 / Pulse 2 / Wave / Noise), the desktop
  ChannelSplit routing fed by the SameBoy per-channel tap. Hosts that don't
  do multi-out just connect bus 0.
- **MIDI sync modes** — full parity with the desktop lsdj-sync role's active
  modes (`packages/retroplug/src/dspRoles.ts`), translated inside the render
  block and delivered over the GB link port:
  `mGB notes` (byte-for-byte passthrough, the default) · `LSDj MIDI sync`
  (host transport/tempo walked at 24 PPQN ÷ divisor, or incoming 0xF8 clock)
  · `Arduinoboy sync` · `MIDI map` (NoteOn → SONG-row jump) · `keyboard MIDI`
  (notes → PS/2 scancodes) · `MI.OUT` and `master sync` (LSDj's outgoing
  serial decoded onto the AU's **MIDI output**, so LSDj can play or clock the
  DAW). Exposed as AU parameters (sync mode / tempo divisor / auto-start) so
  DAW hosts can automate them; the desktop `keyboard` mode (host key feed) is
  the only one without an iOS twin.
- **Arduinoboy Editor parity** — the per-application settings the hardware
  stores in EEPROM (and the maxpat Editor configures over SysEx) are AU
  parameters here, seeded with the firmware factory defaults and persisted in
  the host project via `fullState`:
  - **per-mode MIDI channels** (`RPMidiChannelSetting`, addresses 16–32):
    slave sync / master sync / keyboard / MIDI map channel (map channel plays
    rows 0–127, the next channel up 128–255), the five mGB voice channels
    (input remapped to mGB's fixed PU1/PU2/WAV/NOI/POLY; unassigned channels
    drop), and MI.OUT's per-voice note (+ PC) and CC channels — shared by
    Note Out.
  - **mGB base channel** (address 80): 0 = the five per-voice assignments;
    1–12 = the voices sit contiguously at base..base+4, so multiple DAW
    instances each take their own channel block with one knob.
  - **MI.OUT CC matrix** (addresses 40–75, firmware `playCC` verbatim): per
    voice a CC mode (single CC vs. the LSDj `X` value's high digit selecting
    one of seven CC numbers), a scaling flag (×8 multi / ÷111×127 single —
    off passes LSDj's byte through untouched, firmware quirks included), and
    the seven assignable CC numbers (defaults 1/2/3/7/10/11/12 — chosen
    upstream so MI.OUT output drives an mGB instance's pulse width, envelope,
    sweep, and pan directly).
- **GB Note Out (APU tap)** — one iOS-first mode beyond desktop parity: MIDI
  out from **any** LSDj build (or any ROM at all), no MI.OUT-patched
  "Arduinoboy" ROM required. MI.OUT only exists in the special aboy builds
  because the transmit code lives in the ROM; Note Out sidesteps that by
  decoding what the emulated sound hardware is *told to play* instead of what
  the ROM chooses to transmit. A SameBoy register-write tap (part of the
  tracked per-channel patch) logs every APU register write; the render block
  turns them into MIDI on the MI.OUT per-voice note/CC channel assignments:
  - channel **triggers → NoteOn**, always preceded by an explicit NoteOff for
    the voice's previous note (the four voice streams are strictly mono);
    velocity comes from the envelope volume. Notes release on envelope-0,
    DAC power-off, NR51 mute, APU power-off, and on leaving the mode.
  - **envelope volume → CC7** (LSDj `E` command / instrument envelope),
    **panning → CC10** (`O` command: L=0 / center=64 / R=127),
    **pulse duty → CC70**, and **slides/vibrato → pitch bend** (`P`/`L`/`V`
    commands, ±2 semitones; a slide past that retriggers legato).
  - pitch mapping: pulse `131072/(2048-p)` Hz, wave `65536/(2048-p)` (one
    tone cycle per 32-sample table), noise a monotonic map of NR43 so noise
    pitches still track.

  Caveat: it reads what's *audible*, not the song data — tables/arps arrive
  as fast real notes, WAV kicks as low notes, and pulse sweep drums report
  only their trigger pitch (the sweep runs inside the APU, invisible to
  register writes). That's inherent to the approach.
- **`fullState`** — ROM, SRAM, savestate, and settings round-trip through the
  host's session save/restore.

## How it talks to the emulator (CoreBridge)

`RetroPlugCoreBridge.h` is the main-thread-only control surface SwiftUI uses.
Two channels reach the render thread:

- a **lock-free SPSC command ring** for realtime-cheap ops (buttons, reset,
  gain, fast boot), drained at the top of every render block;
- a **bypass gate** for heavy ops (ROM swap, model change, save/load state &
  SRAM): the render block emits silence while the main thread mutates the
  emulator directly.

Video reads the render thread's triple buffer and never blocks. A periodic
SRAM snapshot (≤ ~0.5 s stale) supports non-blocking autosave.

The app hosts the unit **in-process** under a separate component subtype
(`mgbl`): instantiating the extension's own description resolves to an
out-of-process proxy, which would make the CoreBridge unreachable.

## How it maps to the desktop code

| Desktop (DPF plugin) | iOS |
|---|---|
| `PluginDSP::run()` → `engine_.processBlock` | `RetroPlugAudioUnit` `internalRenderBlock` → `SameBoySystem` prepare/step/finish |
| `engine_.stageMidi` from DPF MidiEvents | `AURenderEventMIDI` → sync-mode translation → link-port serial |
| lsdj-sync role (`dspRoles.ts`) | the same modes, ported to the render block + AU parameters |
| ChannelSplit multi-out port groups | busses 1–4 (stems) + bus 0 (mix) |
| project / role config (TS, zod) | `.rplg` sidecar decode (forward-tolerant) + `UserDefaults` |
| cmake `sameboy` target (+ per-channel patch, boot ROMs) | same sources/flags, generated by `ios/generate.sh`, built by Xcode |

Sources compiled from the existing tree (unmodified):
`packages/native/src/system/{SystemBase,sameboy/SameBoySystem,sameboy/LinkGroup}.cpp`
plus `deps/sameboy/Core/*.c`. Everything else in `ios/` is new:

- `RetroPlugKit/` — the framework: `RetroPlugAudioUnit.mm` (render path,
  sync modes, fullState) + `RetroPlugCoreBridge.h` (the control surface)
- `RetroPlugApp/` — the player: `EmulatorController` (AVAudioEngine host +
  everything SwiftUI does to the emulator), `RomLibrary` (on-disk library +
  `.rplg` decode), `PlayerSettings`, `ControllerInput`, `Views/`
- `RetroPlugAU/` — the extension (factory + placeholder view)
- `Shared/GameBoyScreenView.swift` — the LCD, used by both targets
- `Shared/LsdjSav.swift` — the LSDj `.sav` parser (song list, active slot,
  working-song load) behind the song manager

## Phases from here

1. **Engine + control plane** — compile `retroplug-backend` (QuickJS/txiki —
   interpreter-only, so App Store-legal) for iOS; swap the AU's direct
   `SameBoySystem` for the real `Engine::processBlock`.
2. **Extension parity** — App Group container so the AUv3 can reach the
   app's ROM library; a real extension view (the shared `GameBoyScreenView`
   is already in the appex target).
3. **Distribution** — licensing review required before anything beyond
   personal sideloading (GPLv3 bits + embedded ROMs).
