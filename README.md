# RetroPlug
A VST wrapper around the SameBoy GameBoy emulator, with Arduinoboy support

## Features
- Wraps [SameBoy](https://github.com/LIJI32/SameBoy) v0.1.2
- Full MIDI support for [mGB](https://github.com/trash80/mGB)
- Syncs [LSDj](https://www.littlesounddj.com) to your DAW
- Emulates various [Arduinoboy](https://github.com/trash80/Arduinoboy) modes

## Current limitations (subject to change)
- VST2 only
- Windows only
- 64bit only

## Download
Visit the [releases](https://github.com/tommitytom/RetroPlug/releases) page to download the latest version.

## Usage
- Load it as you would any normal VST
- Double click to open file browser, or drag a rom on to the UI to load
- A .sav with the same name as your rom will be loaded if it is present
- Right click to bring up a menu with various options
- The emulator state is saved in to the project file in your DAW when you hit save, which will persist your changes.  **YOUR .sav IS NOT AUTO SAVED**.  If you want to save out the .sav then do so from the SRAM context menu.
- To edit button mapping, go to Settings -> Open Settings Folder... and edit `buttons.json` (full list of button names below)
- For mGB, the usual Arduinoboy rules apply: https://github.com/trash80/mGB - no additional config needed, just throw notes at it!
- For LSDj, an additional menu will appear in the settings menu, allowing you to set sync modes (Arduinoboy emulation)

## Button mapping
Keyboard button mapping is currently only configurable with JSON configuration files.  On first run a config file is written to `C:\Users\USERNAME\AppData\Roaming\RetroPlug` containing the following default button map:

|Button|Default key|
|------|-----------|
|A|Z|
|B|X|
|Up|UpArrow|
|Down|DownArrow|
|Left|LeftArrow|
|Right|RightArrow|
|Select|Ctrl|
|Start|Enter|

### Supported keys for buttons.json:
Keys `0 - 9` and `A - Z` can be used for alpha numeric keys, as well as the following keys:

```
Backspace, Tab, Clear, Enter, Shift, Ctrl, Alt, Pause, Caps, Esc, Space, PageUp, PageDown, End, Home, LeftArrow, UpArrow, RightArrow, DownArrow, Select, Print, Execute, PrintScreen, Insert, Delete, Help, LeftWin, RightWin, Sleep, NumPad0, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9, Multiply, Add, Separator, Subtract, Decimal, Divide, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, NumLock, Scroll
```
All key names are CASE SENSITIVE!

## LSDj specific settings
### Sync modes
- **Off**: No sync with your DAW at all.  If you hit play in LSDj it will play regardless of what else is happening.

- **MIDI Sync**:
  * Receives MIDI clock from your DAW when the transport is running.
  * If you hit play in LSDj, it will not play until you hit play in your DAW. LSDj should be set to "MIDI" mode on the project page.
  * In this mode LSDj knows nothing about the song position in your DAW, all it knows is that it is receiving a MIDI clock, and that it should play.

- **MIDI Sync (Arduinoboy mode)**:
  * Receives MIDI clock from your DAW, but only plays once a C-2 note is received.
  * Additional Arduinoboy options are emulated, full list can be found [here](https://github.com/trash80/Arduinoboy/#mode-1---lsdj-as-midi-slave-sync).

- **MIDI Map**:
  * Receives MIDI clock from your DAW, and plays the row number relative to MIDI notes that you send it.  C-0 is row 0, C#-0 is row 1, etc.
  * Notes sent on channel 2 map to rows 128 and above.  Ohter channels are ignored.
  * Rows are stopped when note offs are received, or when you hit stop in your DAW.
  * Requires the Arduinboy build of LSDj.

- **Auto Play**:
  * When this option is enabled, RetroPlug will simulate a press of the start button whenever the transport in your DAW is started or stopped.
  * Works in combination with the other sync modes.
  * This option is pretty dumb (it doesnt know if LSDj is playing or not), so it's possible to make it think it is in the wrong state if you manually press `Start`.  If this happens just press `Start` again.

### Additional options
- **Keyboard Shortcuts**: Enables a set of common shortcuts that allows you to use LSDj with a keyboard in a more intuitive manner.  This works by sending combinations of button presses to LSDj, so support for these may not always be perfect!  Many of the hotkeys for these settings can be modified in the button config file (see [Button mapping](#button-mapping)), although a few can't:

| Action | Default key(s) | Configurable|
|--------|----------------|-------------|
|ScreenUp|W|Yes|
|ScreenLeft|A|Yes|
|ScreenDown|S|Yes|
|ScreenRight|D|Yes|
|DownTenRows|PageDown|Yes|
|UpTenRows|PageUp|Yes|
|CancelSelection|Esc|Yes|
|StartSelection|Shift (Hold)|No|
|CopySelection|Ctrl + C|No|
|CutSelection|Ctrl + X|No|
|PasteSelection|Ctrl + V|No|

## Dependencies
- [SameBoy](https://github.com/LIJI32/SameBoy) - The emulator itself
- [iPlug2](https://github.com/iPlug2/iPlug2) - Audio plugin framework
- [tao json](https://github.com/taocpp/json) - JSON library used for dealing with configs and save states

## Building
### Windows
RetroPlug is developed in Visual Studio 2019, however SameBoy has to be built using msys2 (mingw) on Windows.  Since we can't statically link libraries produced by mingw in VS, the DLL output from the build process is compiled as a resource in to the final VST DLL, which is then loaded from memory at runtime.  This allows us to ship the VST as a single DLL.
- Install [msys2](https://www.msys2.org/) to the default location
- Make sure [rgbds](https://github.com/rednex/rgbds) is accessible from your command line (via the PATH variable or some other method)
- Run `thirdparty/SameBoy/retroplug/build.bat`
- Open `RetroPlug.sln` in Visual Studio 2019 and build.

### Mac
Coming soon!

## What to expect in the future
- 32bit builds
- Mac builds
- Support for multiple emulator instances in the same window, for optimum 2x LSDj composing
- LSDj keyboard mode, if anyone has a use for it (convince me!)

## Troubleshooting
Please refrain from asking usage questions in GitHub issues, and use them purely for bugs and feature requests.  If you need help, the official support chat for this plugin is on the PSG Cabal discord channel: https://discord.gg/9MdBJST

### FAQ

**Q**: HALP! The keyboard does not work ;(

**A**: All hosts are different, and some have restrictions on routing keyboard input in to VST instruments.  First, click the center of the window to try and force the host to give focus to the correct control.  If that doesn't work, make sure your host is allowing the plugin to receive keyboard input (Renoise has an "Enable keyboard" option, etc).  If it still doesn't work, try editing default keyboard mapping in `buttons.json` (written to `C:\Users\USERNAME\AppData\Roaming\RetroPlug` on first run).  There are some quirks with certain DAWs...
- **Reaper** does not send the ctrl key to VST's, so you'll need to remap that to something different.

If you find you have issues with a particular DAW, please feel free to submit a bug report.

**Q**: OMG!  LSDj does not start when I hit play in my DAW :o

**A**: Make sure you have the correct sync mode selected in both LSDj, and in the context menu!

## License
MIT