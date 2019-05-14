# RetroPlug
A VST wrapper around the SameBoy GameBoy emulator, with Arduinoboy support

## Features
- Wraps SameBoy 0.1.2
- Full MIDI support for mGB
- Syncs LSDJ to your DAW
- Emulates various Arduinoboy modes

## Current limitations (subject to change)
- VST2 only
- Windows only
- 64bit only

## Usage
- Load it as you would any normal VST
- Double click to open file browser, or drag a rom on to the UI to load
- A .sav with the same name as your rom will be loaded if it is present
- Right click to bring up a menu with various options
- The emulator state is saved in to the project file in your DAW when you hit save, which will persist your changes.  **YOUR .sav IS NOT AUTO SAVED**.  If you want to save out the .sav then do so from the SRAM context menu.
- To edit button mapping, go to Settings -> Open Settings Folder... and edit `buttons.json` (full list of button names below)
- For mGB, the usual Arduinoboy rules apply: https://github.com/trash80/mGB - no additional config needed, just throw notes at it!
- For LSDj, an additional menu will appear in the settings menu, allowing you to set sync modes (Arduinoboy emulation)

## LSDj sync modes
- **Off**: no sync with your DAW at all.  If you hit play in LSDj it will play regardless of what else is happening.

- **MIDI Sync**: Receives MIDI clock from your DAW when the transport is running.  If you hit play in LSDj, it will not play until you hit play in your DAW. LSDj should be set to "MIDI" mode on the project page.  In this mode LSDj knows nothing about the song position in your DAW, all it knows is that it is receiving a MIDI clock, and that it should play.

- **MIDI Sync (Arduinoboy mode)**: Receives MIDI clock from your DAW, but only plays once a C-2 note is received.  Additional Arduinoboy options are emulated, full list can be found here: https://github.com/trash80/Arduinoboy/#mode-1---lsdj-as-midi-slave-sync

- **MIDI Map**: Receives MIDI clock from your DAW, and plays the row number relative to MIDI notes that you send it.  C-0 is row 0, C#-0 is row 1, etc.  Rows are stopped when note offs are received, or when you hit stop in your DAW.  Requires the Arduinboy build of LSDj.

## Button mapping
On first run a config file is written to `C:\Users\USERNAME\AppData\Roaming\RetroPlug` containing the following default button map:
```
{
  "A": "Z",
  "B": "X",
  "Down": "DownArrow",
  "Left": "LeftArrow",
  "Right": "RightArrow",
  "Select": "C",
  "Start": "Enter",
  "Up": "UpArrow"
}
```

## Supported keys for buttons.json:
Keys `0 - 9` and `A - Z` can be used for alpha numeric keys, as well as the following keys:

```
Backspace, Tab, Clear, Enter, Shift, Ctrl, Alt, Pause, Caps, Esc, Space, PageUp, PageDown, End, Home, LeftArrow, UpArrow, RightArrow, DownArrow, Select, Print, Execute, PrintScreen, Insert, Delete, Help, LeftWin, RightWin, Sleep, NumPad0, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9, Multiply, Add, Separator, Subtract, Decimal, Divide, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, NumLock, Scroll
```
All key names are CASE SENSITIVE!

## Dependencies
- [SameBoy](https://github.com/LIJI32/SameBoy) - The emulato itself
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
- Standalone version with support for multiple emulator instances in the same window, for optimum 2x LSDj composing
- Smart keyboard mapping so you can use LSDj with a keyboard in the same way you would use any other keyboard based tracker.
- LSDj keyboard mode, if anyone has a use for it (convince me!)

## Troubleshooting

**Q**: HALP! The keyboard does not work ;(

**A**: All hosts are different, and some have restrictions on routing keyboard input in to VST instruments.  First, click the center of the window to try and force the host to give focus to the correct control.  If that doesn't work, make sure your host is allowing the plugin to receive keyboard input (Renoise has an "Enable keyboard" option, etc).  If it still doesn't work, try editing default keyboard mapping in `buttons.json` (written to `C:\Users\USERNAME\AppData\Roaming\RetroPlug` on first run).  There are some quirks with certain DAWs...
- **Reaper** does not send the ctrl key to VST's, so you'll need to remap that to something different.
- **Ableton** doesn't seem to support sending keystrokes to a VST! So for the time being RetroPlug won't be of much use in Ableton beyond using mGB.  I will further investigate this.

**Q**: OMG!  LSDj does not start when I hit play in my DAW :o

**A**: Make sure you have the correct sync mode selected in both LSDj, and in the context menu!

## License
MIT