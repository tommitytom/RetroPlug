# RetroPlug
A frontend for the [SameBoy](https://github.com/LIJI32/SameBoy) Game Boy and
[Mesen](https://github.com/SourMesen/Mesen2) NES emulators, with a focus on music
creation. It runs standalone and can be used as an audio plugin (CLAP / VST3 / VST2 / AU) in your favourite DAW!

## Features
- Wraps [SameBoy](https://github.com/LIJI32/SameBoy) (Game Boy) and
  [Mesen](https://github.com/SourMesen/Mesen2) (NES)
- Runs standalone, or as a CLAP / VST3 / VST2 / AU plugin inside your DAW
- Full GameBoy MIDI support for [mGB](https://github.com/trash80/mGB)
- Syncs [LSDj](https://www.littlesounddj.com) to your DAW, with
  [Arduinoboy](https://github.com/trash80/Arduinoboy)-style sync modes
- Multiple instances in a single window, linked with virtual Game Boy link cables
- Flexible audio and MIDI routing, including splitting a single system out to its
  individual sound channels

## Download
Visit the [releases](https://github.com/tommitytom/RetroPlug/releases) page to
download the latest version.

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
  `Load...`. A `.sav` sitting next to your ROM is loaded automatically.
- **Open the menu** with `Escape` (rebindable) to load ROMs, save your project, and
  configure everything. Arrow keys navigate, `Enter` activates, `Escape` backs out.
- **mGB is built in** - pick `Load mGB (GB MIDI Synth)` from the start menu and start
  throwing notes at it.
- **Your project state is saved into your DAW project** when you save. Battery SRAM
  can be auto-saved as well - see [Settings](#settings) - or written out manually from
  the `System` menu.
- **For LSDj**, an additional `LSDj` menu appears once an LSDj cart is detected,
  letting you set the sync mode (see [LSDj Integration](#lsdj-integration)).

Each instance's `System` menu also lets you reset, swap the ROM while keeping the
running SRAM, and load/save SRAM and save states. Game Boy systems expose the
SameBoy model, high-pass filter and fast-boot; NES systems expose region and the
sprite-limit override.

## Multiple Instances
You can load several systems in a single window and work with them side by side -
handy for running multiple copies of LSDj, or a mix of Game Boy and NES. From an
instance's menu:

- **Add Instance** - load another ROM into a new instance.
- **Duplicate Instance** - clone the active instance, state and all.
- **Replace Instance** - swap the active instance for a different ROM.
- **Remove Instance** - remove it from the project.

The `Layout` option (Auto / Row / Column / Grid) and `Zoom` control how instances are
arranged and sized. `Tab` cycles between instances.

### Link Groups
Game Boy instances can be wired together with virtual link cables via the
`Link Group` option - set two or more instances to the same group to link them, the
same way you'd connect real hardware.

### Audio Routing
By default every instance is summed into a single stereo output. The `Audio Routing`
option offers:

- **Stereo** - the default; mixes all instances down to one stereo pair.
- **2 Ch / Inst** - each instance gets its own stereo pair (instance 1 → 1-2,
  instance 2 → 3-4, …).
- **1 Ch / Inst** - each instance on its own mono channel.
- **Channels (1 GB)** - with a single Game Boy loaded, split it out to its individual
  sound channels on separate outputs.

### MIDI Routing
By default every instance receives all MIDI on all channels. The `MIDI Routing`
option offers:

- **Send to All** - the default; every instance receives everything.
- **4 Ch / Inst** - four MIDI channels per instance (1-4 → instance 1, 5-8 →
  instance 2, …). Handy for splitting 16 channels across four mGB instances.
- **1 Ch / Inst** - one channel per instance.
- **Ch -> Inst** - map channels straight onto instances.

## LSDj Integration
When an LSDj cart is loaded, an `LSDj` menu appears with its sync options.

Each RetroPlug sync mode expects LSDj's own `SYNC` setting (on the PROJECT screen) to
be set to match. The required LSDj setting is noted for each mode below.

### Sync Modes
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

Two extra controls sit alongside the mode:

- **Tempo Divisor** - divides the incoming clock for slower/faster sync ratios.
- **Auto Start** - presses Start for you when the DAW transport starts, so a
  `SYNC=MIDI` cart arms itself automatically.

## Settings
The `Settings` menu covers:

- **SRAM Auto-Save** - Off, On Save, or Continuous.
- **Default Zoom** - the zoom applied to new projects.
- **Keyboard Bindings / Gamepad Bindings** - remap the Game Boy buttons and app
  actions (open menu, cycle instances). Bindings live in named profiles you can
  create, rename and delete, and both keyboard and gamepad are edited directly in the
  menu - no config files to hand-edit.
- **Open Settings Folder** - opens the folder holding your configuration.

Recent projects are available under the `Recent` menu, where you can re-load, relocate
a moved project, rename it, or remove it from the list.

## Command Line
The build also produces `retroplug-cli` (in `build/bin/`), a self-contained
command-line tool for rendering ROMs to audio without opening a DAW or the UI. It runs
the same emulator cores as the plugin, so what you hear in a render matches what you
hear in your project.

```bash
build/bin/retroplug-cli --help                 # list the available commands
build/bin/retroplug-cli render --help          # options for a command
build/bin/retroplug-cli render song.gbc        # LSDj: auto-length stereo mix -> song.wav
```

The `render` command boots a Game Boy (`.gb` / `.gbc`), NES (`.nes`) or GBA (`.gba`)
ROM and writes its audio to a WAV file. For a saved LSDj song it presses Start so the
song begins playing (pass `--no-start` to capture raw boot audio); mGB needs no such
press, as it plays from incoming MIDI. A sibling `<rom>.sav` is loaded automatically
if present. Highlights:

- **Automatic length** - with a loaded LSDj `.sav` the song length is detected and the
  render trimmed to it, or pin a fixed length with `--duration 3s` (`ms` / `s` / `m`).
- **Per-channel stems** - `--split channels` writes one WAV per sound channel (Game
  Boy: 4 stereo stems; NES: 5 mono core channels), and `--split pins` writes the NES
  analog output pins. The default `--split mix` writes a single mixed file.
- **Song selection** - `--list-songs` prints the saved songs in an LSDj `.sav`, and
  `--song NAME` / `--song-index N` render a chosen song from the save.
- **Output control** - `--out <file>`, `--sample-rate <hz>`, `--bpm <n>` with
  `--transport` for tempo-synced playback, and `--no-start` to capture raw boot audio.

Run `retroplug-cli render --help` for the full flag reference. The CLI can also run a
JavaScript session file directly (`retroplug-cli <session.js>`) for scripted tests and
analysis.

## Building
`build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points -
they run the configure this project needs and build in parallel. You'll need
CMake 3.14+, a C++20 compiler, and Node.js (for the UI bundle); the
[.devcontainer/Dockerfile](.devcontainer/Dockerfile) has the full dependency list, or
just open the repo in the devcontainer. RetroPlug is developed in a dev container so it is the recommended approach.

```bash
git clone --recursive <repo-url>
cd RetroPlug
pnpm install          # wires the deps/dpf.js link before the first configure
./build.sh            # or: cmake -S . -B build && cmake --build build -j$(nproc)
```

The built artifacts land in `build/bin/` (standalone, `.clap`, `.vst3`, VST2, AU).

## Acknowledgements
- [SameBoy](https://github.com/LIJI32/SameBoy) - accuracy-first Game Boy emulator
- [Mesen](https://github.com/SourMesen/Mesen2) - accuracy-first NES emulator
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

## Donations
If you'd like to support development of RetroPlug, donations of any amount are appreciated!
[![Donate](https://www.paypalobjects.com/en_AU/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=TJTBWD3P7S7PG&currency_code=AUD&source=url)

## License
MIT
</content>
</invoke>
