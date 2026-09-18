# RetroPlug
A frontend for the [SameBoy](https://github.com/LIJI32/SameBoy) Game Boy and
[Mesen](https://github.com/SourMesen/Mesen2) NES / GBA / Master System / Game Gear
emulators, with a focus on music creation. It runs standalone and can be used as an
audio plugin (CLAP / VST3 / VST2 / AU) in your favourite DAW!

## Features
- **DAW sync for three music carts**: [LSDj](https://www.littlesounddj.com) (with
  [Arduinoboy](https://github.com/trash80/Arduinoboy)-style modes), mGB (Game Boy MIDI synth) smsggdj
  (Master System / Game Gear), and BlipToaster (NES MIDI synth)
- **Song management** - browse, load, import, export and reorder the songs inside a
  tracker cart's battery
- **ROM assets** - swap a cart's sample kits, palettes, themes and fonts, previewed live
  and patched non-destructively
- **Background rendering** - bounce a song to WAV from the menu while you keep working,
  or from the CLI
- Multiple instances in a single window, linked with virtual Game Boy link cables
- Flexible audio and MIDI routing, including splitting a single system out to its
  individual sound channels, or a NES out to its three 2A03 output pins
- **Real hardware** - drive an Everdrive N8 Pro over USB, and use a Novation Launchpad
  as a control surface

## Download
Visit the [releases](https://github.com/tommitytom/RetroPlug/releases) page to
download the latest version. Builds are published for Linux (x86_64 and aarch64, so a
Raspberry Pi works), Windows (x86_64) and macOS (universal).

## Installation
- **Standalone:** the standalone build is a single application that can be placed
  anywhere you'd like.
- **Plugin:** copy the plugin into your system's plugin folder, or wherever your DAW
  scans for plugins:
  - VST3: the platform VST3 folder (e.g. `~/.vst3` on Linux,
    `~/Library/Audio/Plug-Ins/VST3` on macOS,
    `C:\Program Files\Common Files\VST3` on Windows)
  - VST2: the platform VST2 folder (e.g. `~/.vst` on Linux,
    `~/Library/Audio/Plug-Ins/VST` on macOS,
    `C:\Program Files\Common Files\VST2` on Windows), or wherever your DAW is
    configured to scan
  - CLAP: the corresponding `CLAP` folder alongside it
  - AU (macOS only): `~/Library/Audio/Plug-Ins/Components`

## Usage
- **Load a ROM** by dragging it onto the window, or open the menu and choose
  `Load...`. A `.sav` sitting next to your ROM is loaded automatically. The ROM's
  console is detected from its header, so a mislabelled file still loads correctly.
- **Open the menu** with `Escape` (rebindable) to load ROMs, save your project, and
  configure everything. Arrow keys navigate, `Enter` activates, `Escape` backs out.
- **mGB is built in** - pick `Load mGB (GB MIDI Synth)` from the start menu and start
  throwing notes at it.
- **Your project state is saved into your DAW project** when you save. Battery SRAM
  can be auto-saved as well - see [Settings](#settings) - or written out manually from
  the `System` menu.
- **A music cart gets its own menu.** Load LSDj, risa, smsggdj or BlipToaster and a
  submenu named after the cart appears, holding its sync options, its songs and its
  assets - see [Music Carts](#music-carts).
- **Name your project** under `Project` > `Name` - it's what the window title and the
  `Recent` list show. Leave it empty (the default) and the name follows the loaded
  instance instead: its song, sav and ROM.
- **Nothing is saved or lost silently.** Most emulators automatically update the .sav 
  file associated with a ROM; RetroPlug does not do this. Quitting, or starting / loading 
  another project, with unsaved work asks first and lists what is unsaved: the project 
  file, and any cart whose battery differs from its `.sav` (naming the file it would write).

### The System menu
Per-instance emulator controls, under `System`:

- **Reset**, and **Reload on ROM Change** - watch the ROM file and reload it when it
  changes on disk, so an assembler in another window updates the running cart.
- **Emulator settings**, which differ per console: Model / Fast Boot / Highpass /
  DMG Palette / Colour Correction / Light Temp (Game Boy), Region / Remove Sprite
  Limit / APU Latency / Expansion Volume (NES), FM Audio (Master System).
  `Expansion Volume` is the level of the cartridge's own sound chip (VRC6, VRC7,
  Namco 163, Sunsoft 5B, MMC5, FDS) and never the console's own audio - and it
  follows through to a connected Everdrive N8 Pro, so a cart sits at the same level
  emulated and on the console.
- **Battery and state** - `Swap ROM (Preserve SRAM)...`, `New SRAM...`, `Load SRAM...`,
  `Save SRAM`, `Load State...`, `Save State`, `Save State As...`.
- **Render** - bounce this instance to a WAV in the background; see
  [Rendering](#rendering).

## Multiple Instances
You can load several systems in a single window and work with them side by side -
handy for running multiple copies of LSDj (linked with virtual link cables), or a mix
of consoles (currently, only Game Boys are linkable). From an instance's menu:

- **Add Instance** - load another ROM into a new instance.
- **Duplicate Instance** - clone the active instance, state and all.
- **Replace Instance** - swap the active instance for a different ROM.
- **Remove Instance** - remove it from the project.

The `Layout` option (Auto / Row / Column / Grid) and `Zoom` control how instances are
arranged and sized. `Tab` cycles between instances.

### Link Groups
Game Boy instances can be wired together with virtual link cables via the
`Link Group` option - set two or more instances to the same group to link them, the
same way you'd connect real hardware. (The link cable is a Game Boy feature, so the
option only appears on SameBoy instances with a peer.)

### Audio Routing
By default every instance is summed into a single stereo output. The `Audio Routing`
option offers:

- **Stereo** - the default; mixes all instances down to one stereo pair.
- **2 Ch / Inst** - each instance gets its own stereo pair (instance 1 → 1-2,
  instance 2 → 3-4, …).
- **1 Ch / Inst** - each instance on its own mono channel.
- **Channels** - one system's individual sound channels: a Game Boy's 4 channels as
  four stereo pairs across the 8 outs, or a NES's 5 core channels (square 1/2,
  triangle, noise, DMC) as mono on outs 1-5.
- **Pins** - a NES's three 2A03 output pins (Pulse, TND, Expansion) as mono on outs
  1-3.
- **Stereo Pins** - the same three pins, but a stereo *pair* each (outs 1/2, 3/4, 5/6)
  with L mirrored to R, so each pin arrives as an ordinary stereo track in a DAW that
  can only bus or FX whole pairs.

The last three only apply with a single instance loaded, since a split has nowhere to
go once there are peers, and the two pin modes additionally need that instance to be a
NES. The menu offers only what applies, and the audio engine re-checks regardless. In
the standalone, raise `Settings` > `Audio` > `Out Channels` to match, or the extra
streams have nowhere to land.

### MIDI Routing
By default every instance receives all MIDI on all channels. The `MIDI Routing`
option offers:

- **Send to All** - the default; every instance receives everything.
- **4 Ch / Inst** - four MIDI channels per instance (1-4 → instance 1, 5-8 →
  instance 2, …). Handy for splitting 16 channels across four mGB instances.
- **1 Ch / Inst** - one channel per instance.
- **Ch -> Inst** - map channels straight onto instances.

## Music Carts
RetroPlug recognises four music carts from a marker in the ROM, and gives each one a
submenu named after it. What that submenu holds depends on the cart:

| Cart | Console | DAW sync | Songs | Assets |
|---|---|---|---|---|
| [LSDj](https://www.littlesounddj.com) | Game Boy | 8 sync modes, see below | yes | Kits, Palettes, Fonts |
| risa | NES | follows the DAW transport | yes | Kits, Themes, Fonts |
| smsggdj | Master System / Game Gear | follows the DAW transport | yes | not yet |
| BlipToaster | NES | MIDI synth (no sequencer) | - | Kits, Themes, Fonts |

Unlike LSDj, smsggdj needs no sync mode picked: they follow the host transport
directly, so pressing play in your DAW plays the cart in time.

An unrecognised build of a cart is shown greyed out as `(Unsupported Version)`.

### Songs
The `Songs` submenu lists the songs saved in the cart's battery, and each one can be
loaded, exported, replaced or deleted, with `Add...` to import from a file and Move
Up / Down to reorder. Everything is done on the cart's own save format, so files stay
readable by the cart, by other tools, and on real hardware.

Loading a song from this menu is what a `Recent` row replays, and the working song's
name is what the window title shows.

> **smsggdj is different in one way worth knowing.** Its working song lives in the
> console's work RAM rather than in the battery, and the cart boots to a blank song on
> purpose. Loading is therefore done live - the song is written straight into the
> running cart, so nothing is written to disk and the cart is not restarted - but any
> *other* battery edit (delete, reorder, import…) does restart it, and RetroPlug warns
> when that would discard unsaved work.

### ROM Assets
The asset submenus (`Kits`, `Palettes`, `Fonts`, `Themes`, depending on the cart) list
what is baked into the ROM and let you replace a slot from a file, in each cart's own
formats - `.kit` / `.lsdpal` / `.png` for LSDj, `.rkit` / `.rit` / `.chr` for risa and
BlipToaster. Replacements are held as overrides and folded into the ROM in memory, so
**the ROM on disk is never touched** until you ask: `Export Patched ROM...` writes a
copy, and `Patch ROM in Place` rewrites the original.

To build a kit from raw audio, use the command line: `lsdj-rom`, `risa-rom` and
`bliptoaster-rom` all have `build-kit` (compile a kit file from WAVs) and
`import-sample` (splice one sample straight into a ROM's kit).

### LSDj Sync Modes
When an LSDj cart is loaded, its submenu carries the sync options. Each RetroPlug sync
mode expects LSDj's own `SYNC` setting (on the PROJECT screen) to be set to match. The
required LSDj setting is noted for each mode below.

- **Off** - no sync; LSDj plays independently of your DAW. LSDj `SYNC = OFF`.
- **MIDI Sync** - LSDj receives MIDI clock from your DAW and plays in time with the
  transport. LSDj `SYNC = MIDI`.
- **MIDI Sync (Arduinoboy)** - like MIDI Sync, but only starts once a `C-2` note is
  received, emulating the Arduinoboy variation. LSDj `SYNC = LSDJ`. Requires the
  Arduinoboy build of LSDj.
- **MIDI Map** - plays rows relative to incoming MIDI notes (`C-0` is row 0, and so
  on). LSDj `SYNC = MI.MAP`.
- **Keyboard / Keyboard MIDI** - drive LSDj's keyboard input from your keyboard or
  from MIDI notes. LSDj `SYNC = KEYBD`.
- **MIDI Passthrough** - forwards raw MIDI messages straight to the cartridge over the
  link port (the same path mGB uses); no particular LSDj `SYNC` setting is needed.
- **MIDI Out** - LSDj drives the clock and sends its channel notes back out to your
  DAW as MIDI. LSDj `SYNC = MI.OUT`. Requires the Arduinoboy build of LSDj.
- **Master Sync** - LSDj self-clocks as the master and sends MIDI clock out to your
  DAW. LSDj `SYNC = LSDJ`.

Three extra controls sit alongside the mode:

- **Tempo Divisor** - divides the incoming clock for slower/faster sync ratios.
- **Auto Start** - presses Start for you when the DAW transport starts, so a
  `SYNC=MIDI` cart arms itself automatically.
- **HD Player** - a full-window view of the song, all four chains and all four phrases
  at once, drawn in the cart's own font and palette. The cart keeps playing (and stays
  playable) underneath; `Escape` returns to the grid.

## Rendering
`System` > `Render` bounces an instance to a WAV **in the background**, from a fresh
copy of its current state - the running instance is never disturbed, and you can keep
working while it renders. The submenu sets the output folder and filename, the split
mode, sample rate, maximum duration and what to do if the file already exists;
`Settings` > `Default Render Dir` sets where new renders land by default.

With a loaded LSDj or risa song the length is **detected** from the song's `HFF` stop
and the file trimmed to it. smsggdj songs loop forever by design (nothing in that
cart's command set stops the transport), so those need a duration pinned.

The same engine is available from the command line - see below.

## Hardware
Two pieces of physical hardware are supported directly, and their submenus appear only
when the device is actually attached:

- **Everdrive N8 Pro** - load and boot a `.nes` onto a real NES over USB, stream live
  MIDI to it, drive host sync from a MIDI clock, and dump or restore its save. The
  cart's expansion-audio volume follows the NES instance's own `Expansion Volume`
  setting (under `System`), so a VRC6 / VRC7 / N163 / Sunsoft 5B / MMC5 cart's extra
  voices are audible without visiting the N8's own menu, at the level you set for the
  emulator. With no NES instance loaded the link sets unity for a cart
  whose mapper carries expansion audio, and leaves any other cart alone.
- **Novation Launchpad** - use a Launchpad as a control surface, with input/output port
  selection and a Follow Playhead mode. The menu appears once one is found: over USB
  that is its port name, and over TRS/DIN (where it arrives under the interface's name)
  `Settings > MIDI > Scan for Control Surface` asks each port what it is and records the
  pair that answers.

## Settings
The `Settings` menu covers:

- **Audio** (standalone only) - driver, output and input device, block size, sample
  rate, and output channel count.
- **MIDI** (standalone only) - input device, transport handling, external MIDI clock
  source, lookahead, and `Scan for Control Surface` (finds a Launchpad on any port,
  including one attached over TRS/DIN).
- **Keyboard Bindings / Gamepad Bindings** - remap the console buttons and app
  actions (open menu, cycle instances). Bindings live in named profiles you can
  create, rename and delete, and both keyboard and gamepad are edited directly in the
  menu - no config files to hand-edit.
- **SRAM Auto-Save** - Off, On Save, or Continuous.
- **Default Zoom** - the zoom applied to new projects.
- **Default Render Dir** - where background renders are written.
- **File Dialogs** - the OS-native file dialog, or RetroPlug's in-app browser.
- **Open Settings Folder** - opens the folder holding your configuration.

## Command Line
The build also produces `retroplug-cli` (in `build/bin/`), a self-contained
command-line tool. It runs the same emulator cores as the plugin, so what you hear in a
render matches what you hear in your project, and it needs no Node.js, npm or
`node_modules` at runtime - the binary is the whole tool.

```bash
build/bin/retroplug-cli --help                 # list the available commands
build/bin/retroplug-cli render --help          # options for a command
build/bin/retroplug-cli render song.gbc        # LSDj: auto-length stereo mix -> song.wav
```

| Command | What it does |
|---|---|
| `render` | Render a ROM/SAV to a WAV (full mix or per-channel stems) |
| `test` / `run` | Strip and run a directory of TypeScript tests, or a single session file |
| `lsdj-rom` / `risa-rom` / `bliptoaster-rom` | Inspect, extract and patch a cart's kits, palettes, themes and fonts |
| `n8-load` / `n8-bridge` / `n8-sync` / `n8-play` | Drive a physical Everdrive N8 Pro over USB |
| `analyze-capture` / `grab-frame` | Measure a hardware audio capture; grab a NES video frame off a capture card |
| `launchpad-probe` | Drive a real Launchpad and report what it sends back |

### Rendering from the command line
The `render` command boots a Game Boy (`.gb` / `.gbc`), NES (`.nes`), GBA (`.gba`),
Master System (`.sms`) or Game Gear (`.gg`) ROM and writes its audio to a WAV file. For
a saved tracker song it presses play so the song begins (pass `--no-start` to capture
raw boot audio); mGB needs no such press, as it plays from incoming MIDI. A sibling
`<rom>.sav` is loaded automatically if present. Highlights:

- **Automatic length** - with a loaded LSDj or risa `.sav` the song length is detected
  if the song ends with an `HFF` command, and the render trimmed to it. Otherwise pin a
  fixed length with `--duration 3s` (`ms` / `s` / `m`). smsggdj songs have no end to
  detect, so `--duration` is required for those.
- **Per-channel stems** - `--split channels` writes one WAV per sound channel (Game
  Boy: 4 stereo stems; NES: 5 mono core channels), and `--split pins` writes the NES
  analog output pins. The default `--split mix` writes a single mixed file.
- **Song selection** - `--list-songs` prints the songs saved in a cart's `.sav`, and
  `--song NAME` / `--song-index N` render a chosen one. Works for LSDj, risa and
  smsggdj; smsggdj boots to a blank song, so one of these is required or the render is
  silence (it will tell you).
- **Output control** - `--out <file>`, `--sample-rate <hz>`, `--bpm <n>` with
  `--transport` for tempo-synced playback, and `--no-start` to capture raw boot audio.

Run `retroplug-cli render --help` for the full flag reference.

### Scripting
`retroplug-cli <session.js>` runs a JavaScript session file directly, and
`retroplug-cli run <session.ts>` runs a TypeScript one - types are stripped in-process,
so no build step and no toolchain are involved. `retroplug-cli test <dir>` runs a whole
directory of TypeScript tests the same way, which is how a consuming project (a homebrew
ROM, say) can drive the emulator, MIDI and audio analysis in CI with nothing installed
but this one binary.

## Building
`build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points -
they run the configure this project needs and build in parallel. You'll need
CMake 3.14+, a C++20 compiler, and Node.js + pnpm (for the UI bundle); the
[.devcontainer/Dockerfile](.devcontainer/Dockerfile) has the full dependency list, or
just open the repo in the devcontainer. RetroPlug is developed in a dev container so it
is the recommended approach.

```bash
git clone --recursive <repo-url>
cd RetroPlug
pnpm install          # wires the deps/dpf.js link before the first configure
pnpm install --dir deps/dpf.js/deps/lv_binding_js   # its own workspace; the UI bundle needs it
./build.sh            # or: ./build.sh --clean to wipe build/ first
```

That second `pnpm install` is not optional on a fresh checkout: `lv_binding_js` is a
nested workspace of its own, and without it the UI bundle cannot resolve `react`.

`build.sh` passes any `-D<var>=<value>` straight through to the configure. One worth
knowing: `-DRETROPLUG_MESEN_LTO=ON` buys about 10% on the NES core and is used for
release builds, but it makes every incremental build re-run link-time codegen, so it is
off by default.

The built artifacts land in `build/bin/` (standalone, `.clap`, `.vst3`, VST2, AU, and
`retroplug-cli`).

## Acknowledgements
- [SameBoy](https://github.com/LIJI32/SameBoy) - accuracy-first Game Boy emulator
- [Mesen](https://github.com/SourMesen/Mesen2) - accuracy-first NES / GBA / SMS emulator
- [DPF](https://github.com/DISTRHO/DPF) - the cross-platform audio-plugin framework
  (CLAP / VST3 / VST2 / AU / JACK)
- [LVGL](https://github.com/lvgl/lvgl) and
  [lv_binding_js](https://github.com/tommitytom/lv_binding_js) - the UI toolkit and
  its React/JS bindings
- [mGB](https://github.com/trash80/mGB), [LSDj](https://www.littlesounddj.com), and
  [Arduinoboy](https://github.com/trash80/Arduinoboy) - the Game Boy music software
  this is all built to serve

## Troubleshooting
Please refrain from asking usage questions in GitHub issues, and use them purely for bugs and feature requests.  If you need help, the official support chat for this plugin is on the PSG Cabal discord channel: https://discord.gg/V3GyA5dtqB

### FAQ

**Q**: HALP! The keyboard does not work ;(

**A**: All hosts are different, and some have restrictions on routing keyboard input in to VST instruments.  First, click the center of the window to try and force the host to give focus to the correct control.  If that doesn't work, make sure your host is allowing the plugin to receive keyboard input (Renoise has an "Enable keyboard" option, etc). There are some quirks with certain DAWs...
- **Reaper** does not send the ctrl key to VST's, so you'll need to remap that to something different.
- **Ableton** has a bug in their VST2 implementation that strips any information about what key press is actually released (it always returns 0). It is recommended you use VST3 in ableton.

If you find you have issues with a particular DAW, please feel free to submit a bug report.

**Q**: OMG!  LSDj does not start when I hit play in my DAW :o

**A**: Make sure you have the correct sync mode selected in both LSDj, and in the context menu!

**Q**: WTF! I loaded my smsggdj cart and rendered it, but the WAV is silent (╯°□°）╯︵ ┻━┻

**A**: smsggdj boots to a blank song on purpose - it keeps no working song in its
battery. Pick a song first (in the app: the cart's `Songs` menu; on the command line:
`--song` / `--song-index`), and pin a `--duration`, since its songs loop rather than
ending.

## Donations
If you'd like to support development of RetroPlug, donations of any amount are appreciated!
[![Donate](https://www.paypalobjects.com/en_AU/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=TJTBWD3P7S7PG&currency_code=AUD&source=url)

## License
MIT
