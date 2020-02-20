# RetroPlug
A VST wrapper around the SameBoy GameBoy emulator, with Arduinoboy support

## Features
- Wraps [SameBoy](https://github.com/LIJI32/SameBoy) v0.12.1
- Full MIDI support for [mGB](https://github.com/trash80/mGB)
- Syncs [LSDj](https://www.littlesounddj.com) to your DAW
- Emulates various [Arduinoboy](https://github.com/trash80/Arduinoboy) modes
- Realtime LSDj sample patching!

## Current Limitations (subject to change)
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
- To edit button mapping, go to `Settings -> Open Settings Folder...` and edit `buttons.json` (full list of button names below)
- For mGB, the usual Arduinoboy rules apply: https://github.com/trash80/mGB - no additional config needed, just throw notes at it!
- For LSDj, an additional menu will appear in the settings menu, allowing you to set sync modes (Arduinoboy emulation)

## Button Mapping
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

### Supported Keys for buttons.json:
Keys `0 - 9` and `A - Z` can be used for alpha numeric keys, as well as the following keys:

```
Backspace, Tab, Clear, Enter, Shift, Ctrl, Alt, Pause, Caps, Esc, Space, PageUp, PageDown, End, Home, LeftArrow, UpArrow, RightArrow, DownArrow, Select, Print, Execute, PrintScreen, Insert, Delete, Help, LeftWin, RightWin, Sleep, NumPad0, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9, Multiply, Add, Separator, Subtract, Decimal, Divide, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, NumLock, Scroll
```
All key names are CASE SENSITIVE!

## Multiple Instances
You can load multiple instances of the emulator in a single window, and link them with virtual link cables.  The goal of this feature is to offer a streamlined way of working with multiple instances of LSDj.  You can create an additional emulator instance using the `Project -> Add Instance` submenu.  Choose one of the following options:
- **Load ROM...** - Loads a ROM from disk in to the new instance.
- **Same ROM** - Loads the ROM of the currently active instance in to the new instance.  The created instance will load the .sav file accompanying the ROM.
- **Duplicate** - Creates an exact copy of the currently active instance by coying its state.

Additional information:
- You can create up to 4 instances.
- Instances are linked when the `Game Link` option is enabled.  This has to be done on every instance that is to be linked.
- You can move between instances with the `Tab` key.

### Audio Routing
By default, the output of all instances will be summed together in to a single stereo output.  The `Project -> Audio Routing` menu contains additional options for audio routing:

- **Stereo Mixdown** - The default option.  Mixes the audio of all instances in to two stereo channels.
- **Two Channels Per Instance** - Each instance will output on its own 2 channels.  Instance 1 will output on channels 1-2, instance 2 will output on channels 3-4, etc.  This may require additional configuration in your DAW.  _This feature is not available in the standalone version._

### MIDI Routing
By default, each instance receives all MIDI messages received on all channels.  The `Project -> MIDI Routing` menu contains additional options for MIDI routing:

- **All Channels to All Instances** - The default option.  Sends all messages received to all instances.
    - A usage example for this mode could be using LSDj in the Arduinoboy MIDI sync mode, where you want multiple instances to sync to a single `C-2` note on.
- **Four Channels Per Instance** - Evenly maps MIDI channels across up to four instances.
    - The first instance will receive channels 1-4, the second will receive channels 5-8, etc.
    - This is handy for mGB, since it allows you to evenly split all 16 MIDI channels across 4 mGB instances.
    - Also useful for LSDj in MIDI Map mode, since the mode makes use of 2 channels per instance.
- **One Channel Per Instance** - Similar to the mode above but for more simplistic uses.
    - The first instance will receive MIDI from channel 1, the second will receive MIDI from channel 2, etc.
    - A more obvious choice if you want control over individual instances when using the Arduinoboy MIDI sync mode (LSDj).

### Multi LSDj Setup
For LSDj versions lower than v6.1.0, one instance of LSDj needs to be set to `MASTER` and the others need to be set to `SLAVE`.  Versions v6.1.0 and above no longer have these modes, but have a singular `LSDJ` mode, where the instance that initiates playing automatically becomes the master.  It appears there may be additional overhead to this new mode that may make things go slightly out of sync - so it is recommended that if you want perfect sync between more than one instance of LSDj that you use one of the MIDI sync modes.  This will sync both instances to your DAW, rather than to each other.

## LSDj Integration
When LSDj is detected, additional options are added to the context menus.
### Sync Modes
- **Off**: No sync with your DAW at all.  If you hit play in LSDj it will play regardless of what else is happening.

- **MIDI Sync**:
  * Receives MIDI clock from your DAW when the transport is running.
  * If you hit play in LSDj, it will not play until you hit play in your DAW. LSDj should be set to "MIDI" mode on the project page.
  * In this mode LSDj knows nothing about the song position in your DAW, all it knows is that it is receiving a MIDI clock, and that it should play.

- **MIDI Sync (Arduinoboy Variation)**:
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

### .sav Manipulation
Thanks to the liblsdj library you are able to list, export, import, load, and delete tracks contained in your LSDj save.  The `LSDj Songs` context menu contains these features.  Additionally, dragging a `.lsdsng` file on to the window will add the song to your `.sav` file and load it immediately (if the `.sav` has space to contain the song).

### Kit Patching
Kits can be patched, deleted, and exported using the `LSDj Kits` menu.
* Kits can be imported from `.kit` files, or imported from a previously patched LSDj ROM.
* To import kits to the next available slot, use the `LSDj Kits -> Import...` menu item.  If a kit is already in the ROM it will not be imported.  This allows you to import all kits from a ROM that the current ROM does not have without creating duplicates.
* To patch a specific kit slot, or to replace an existing kit, choose the kit/slot you want to replace in the context menu, and choose `Load...` or `Replace...` respectively.
* Importing kits to a fresh slot requires a reset (this will be done automatically, no song data will be lost).  Replacing existing kits does not require a reset, and can be done in realtime **while the kit you are patching is currently playing**!
* Kits are saved in to your DAWs project file when you hit save, or alternatively in to a `.retroplug` file when you save the project to disk.

The ROM that you loaded from disk isn't actually modified.  To save the patched ROM out to disk for use on harware or other emulators, using the `System -> Save ROM...` menu option.

### Updating LSDj
Updating to a new verison of LSDj can be quite cumbersome when your ROM is patched with custom samples, though RetroPlug tries to help make this easy by offering a way of swapping out a ROM and keeping custom samples patched.  To do this, use the `System -> Replace ROM...` menu item and select the new LSDj ROM.

### Additional Options
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
|Delete|Delete|Yes|
|StartSelection|Shift (Hold)|No|
|CopySelection|Ctrl + C|No|
|CutSelection|Ctrl + X|No|
|PasteSelection|Ctrl + V|No|

## Roadmap
- v1.0.0
    - 32bit builds
    - Mac builds
    - Outputs from individual audio channels
    - Lua scripting
- v2.0.0
    - Additional emulators.  Support for C64, GBA and Megadrive/Genesis is being explored.

## Dependencies
- [SameBoy](https://github.com/LIJI32/SameBoy) - The emulator itself
- [iPlug2](https://github.com/iPlug2/iPlug2) - Audio plugin framework
- [libsdj](https://github.com/stijnfrishert/liblsdj) - Adds the functionality to manipulate LSDj save files
- [rapidjson](https://github.com/Tencent/rapidjson) - JSON library used for dealing with configs and save states

## Building
### Windows
RetroPlug is developed in Visual Studio 2019, however SameBoy has to be built using msys2 (mingw) on Windows.  Since we can't statically link libraries produced by mingw in VS, the DLL output from the build process is compiled as a resource in to the final VST DLL, which is then loaded from memory at runtime.  This allows us to ship the VST as a single DLL.
- Install [msys2](https://www.msys2.org/) to the default location
- Make sure [rgbds](https://github.com/rednex/rgbds) is accessible from your command line (via the PATH variable or some other method)
- Run `thirdparty/SameBoy/retroplug/build.bat`
- Open `RetroPlug.sln` in Visual Studio 2019 and build.

### Mac
Coming soon!

## Troubleshooting
Please refrain from asking usage questions in GitHub issues, and use them purely for bugs and feature requests.  If you need help, the official support chat for this plugin is on the PSG Cabal discord channel: https://discord.gg/9MdBJST

### FAQ

**Q**: HALP! The keyboard does not work ;(

**A**: All hosts are different, and some have restrictions on routing keyboard input in to VST instruments.  First, click the center of the window to try and force the host to give focus to the correct control.  If that doesn't work, make sure your host is allowing the plugin to receive keyboard input (Renoise has an "Enable keyboard" option, etc).  If it still doesn't work, try editing default keyboard mapping in `buttons.json` (written to `C:\Users\USERNAME\AppData\Roaming\RetroPlug` on first run).  There are some quirks with certain DAWs...
- **Reaper** does not send the ctrl key to VST's, so you'll need to remap that to something different.

If you find you have issues with a particular DAW, please feel free to submit a bug report.

**Q**: OMG!  LSDj does not start when I hit play in my DAW :o

**A**: Make sure you have the correct sync mode selected in both LSDj, and in the context menu!

## Donations
If you'd like to support development of RetroPlug, donations of any amount are appreciated!
[![Donate](https://www.paypalobjects.com/en_AU/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=TJTBWD3P7S7PG&currency_code=AUD&source=url)

## License
MIT
