# RetroPlug2

A Game Boy emulator running as an audio plugin — modern reboot of the original
[RetroPlug](https://github.com/tommitytom/RetroPlug). Built for using LSDJ,
mGB, and other Game Boy music software inside a DAW, with the same playback
experience as a real DMG/CGB but as a normal track in your project.

Built on:

- [DPF](https://github.com/DISTRHO/DPF) — cross-platform audio plugin framework
  (LV2 / VST2 / VST3 / CLAP / JACK)
- [SameBoy](https://github.com/LIJI32/SameBoy) — high-accuracy Game Boy
  emulator core
- A React + TypeScript UI on top of LVGL via QuickJS — the framework slice is
  documented separately in [dpfjs.md](dpfjs.md)

## Status

Early reboot. The SameBoy MVP boots ROMs, audio plays at the host sample rate,
the LVGL menu overlay opens on Esc, and the file picker loads ROMs.

The original RetroPlug's deeper features — multi-instance routing, save-state
slots, MIDI control, mGB role, LSDJ sync / Arduinoboy / kit patching, the LSDJ
HD player — are tracked as ordered migration steps under [`porting/`](porting/).
Step 1 is in; steps 2 and 3 are partly in (basic input + file picker work, full
ROM-picker UI not yet); the rest are TODO.

## Building

Requires CMake 3.14+, a C++20 compiler, Node.js (for the esbuild UI bundle),
and a handful of X11 / OpenGL / audio dev libraries — see
[.devcontainer/Dockerfile](.devcontainer/Dockerfile) for the canonical apt
list, or just use the devcontainer (below).

```bash
git clone --recursive <repo-url>
cd RetroPlug2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Output:

| Format | Path |
|--------|------|
| JACK standalone | `build/bin/retroplug` |
| CLAP | `build/bin/retroplug.clap` |
| VST2 | `build/bin/retroplug-vst2.so` |
| VST3 | `build/bin/retroplug.vst3/` |
| LV2 | `build/bin/retroplug.lv2/` |
| Headless renderer | `build/bin/retroplug-cli` |

### Devcontainer

VS Code: open the repo and "Reopen in Container". The image bakes in every
build dep, the headless agent tooling (`xvfb`, `jackd2`, `xdotool`,
`pluginval`, `clap-validator`, `ffmpeg`, Node.js), and persists Claude Code
state in a Docker named volume.

## Headless workflows

A few pnpm scripts exist for testing without a DAW. All of them run cleanly
inside the devcontainer.

```bash
pnpm smoke   # mGB chord smoke (test/ts/gb/mgb.test.ts) -> /tmp/cli-smoke.wav
pnpm screenshot  # capture standalone UI -> /tmp/retroplug.png
pnpm validate    # clap-validator + pluginval against the built artifacts
```

### `retroplug-cli --test` — TypeScript tests (primary)

The headless test path. Tests run in-process in the embedded txiki/QuickJS
runtime (no Node, no DAW) and emit TAP — they can read emulator state and branch
on it (memory regions, CPU registers, instruction stepping, framebuffer/audio
capture, MIDI/serial-out capture, host transport, link groups, kit patching),
and author LSDj `.sav` state directly via the sav codec (no fragile UI
navigation):

```bash
pnpm test:cli     # transpile + run every test/ts/**/*.test.ts
```

```ts
import { test, expect, emu, Button, Mem } from "harness";

test("LSDJ boots and writes to WRAM", () => {
  const sys = emu.loadRom("../resources/roms/lsdj/lsdj9_4_2.gb");
  emu.runMs(2500);
  expect(emu.readMemory(sys, Mem.Ram).length).toBe(0x8000);
});
```

See [test/ts/README.md](test/ts/README.md) for the full `emu` API.

Run with no `--test` and `retroplug-cli` boots the embedded end-user CLI
([packages/cli](packages/cli)) — a JSON `--script` renderer
(`{"at_ms":100,"tap":"A","hold_ms":50}` events, `--rom`/`--out`/`--duration`/
`--save-sav`/`--save-rplg`/`--per-system-wav`/… flags). It's TypeScript over the
same `emu` client as the tests, bundled to QuickJS bytecode (no Node at runtime);
`RETROPLUG_CLI_BUNDLE_PATH` loads it from source for dev. For verification,
prefer writing TypeScript tests.

### Standalone screenshot tooling

The standalone (`build/bin/retroplug`, JACK target) periodically dumps the
LVGL screen to PNG when `RETROPLUG_SCREENSHOT_PATH` is set. Cadence is
controlled by `RETROPLUG_SCREENSHOT_INTERVAL_MS` (default 1000). Cost is zero
when the env var is unset.

For headless use, [tools/run-standalone.sh](tools/run-standalone.sh) wraps the
whole flow — launches Xvfb on a free display, starts a dummy-backend `jackd`,
runs the standalone with the screenshot hook armed, and tears everything down
on exit. One-time setup on a host without the devcontainer:

```bash
sudo apt-get install xvfb jackd2 xdotool
```

Then:

```bash
tools/run-standalone.sh                      # /tmp/retroplug.png after 3s
tools/run-standalone.sh /tmp/x.png 5         # custom path + 5s run
tools/run-standalone.sh /tmp/x.png 5 250     # 250ms screenshot cadence
```

To drive the UI mid-run, share the `DISPLAY` env and use
[tools/standalone-key.sh](tools/standalone-key.sh):

```bash
DISPLAY=:99 tools/run-standalone.sh /tmp/menu.png 4 500 &
DISPLAY=:99 tools/standalone-key.sh Escape Down Down Return
```

`Escape` opens the menu, arrow keys navigate, `Return` activates.

### Plugin-format validation

`pnpm validate` runs format-compliance validators against the built
`.clap` and `.vst3` artifacts:

- [clap-validator](https://github.com/free-audio/clap-validator) — CLAP
  protocol tests (state save/restore, parameter handling, MIDI, threading)
- [pluginval](https://github.com/Tracktion/pluginval) — VST3 / AU / LV2
  protocol tests (strictness 5 by default; bumpable in
  [tools/validate-plugins.sh](tools/validate-plugins.sh))

Both binaries are baked into the devcontainer image. On a host without the
devcontainer, install them manually:

```bash
curl -fsSL https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip -o pv.zip && unzip pv.zip && sudo install -m 0755 pluginval /usr/local/bin/
curl -fsSL https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-ubuntu-18.04.tar.gz | tar -xz && sudo install -m 0755 clap-validator /usr/local/bin/
```

Format validation catches DPF wrapper regressions, ABI bugs, and parameter /
state-restore drift — it does NOT exercise the GameBoy DSP. Behavioural
checks remain the job of `retroplug-cli`.

## Project layout

```
src/
  PluginDSP.cpp                      DSP class; runs SameBoy at host sample rate
  PluginUI.cpp                       UI class; owns JS engine + bridge
  PluginJsBridge.{hpp,cpp}           plugin.* JS bindings (getFrame, pressButton, …)
  PluginShared.hpp                   parameter spec + SharedDSPData (in-process)
  LvglJsEngine.{hpp,cpp}             txiki.js runtime wrapper
  project/
    Project.{hpp,cpp}                DSP-thread runtime container; system table + config
    ProjectConfig.hpp                reflectcpp-serializable settings
  system/
    SystemBase.hpp                   polymorphic emulator base class
    InputTypes.hpp                   GameboyButton enum
    sameboy/
      SameBoySystem.{hpp,cpp}        SameBoy lifecycle + audio + framebuffer
      SameBoyConfig.hpp              per-system config (model, savestate, …)
  transport/
    CommandQueue.hpp                 SPSC ring: UI → DSP (button presses, ROM swap)
    EventQueue.hpp                   SPSC ring: DSP → UI (released SystemBase pointers)
    FrameBufferTriple.hpp            seqlock-protected triple-buffer for video
ui/                                  React/TSX UI source (esbuild-bundled)
  PluginUI.tsx                       React entry point
  EmulatorTile.tsx                   Canvas widget that renders SameBoy frames
  MenuOverlay.tsx                    LVGL-focused menu (Esc to open)
runtime/lvgljs/                      typed JS-side bridge into the native runtime
cli/                                 retroplug-cli source (Wav, TestHarness, HarnessRpcService, main)
test/                                Catch2 unit tests + test/ts TypeScript harness tests
examples/reaper/                     committed Reaper .rpp fixtures (DAW host tests)
porting/                             ordered migration roadmap from old RetroPlug
tools/                               build-ui.js, run-standalone.sh, standalone-key.sh, validate-plugins.sh
deps/                                submodules: dpf, dpf-widgets, sameboy, lv_binding_js, rpcpp, catch2
```

For the React/TSX/QuickJS framework slice (everything that's not Game Boy or
SameBoy specific), see [dpfjs.md](dpfjs.md). For the migration plan, see
[porting/README.md](porting/README.md).

## Acknowledgements

- [SameBoy](https://github.com/LIJI32/SameBoy) — accuracy-first Game Boy
  emulator
- [DPF](https://github.com/DISTRHO/DPF) — the audio-plugin framework
- [LVGL](https://github.com/lvgl/lvgl) and
  [lv_binding_js](https://github.com/tommitytom/lv_binding_js) — UI toolkit
  and React/JS bindings
