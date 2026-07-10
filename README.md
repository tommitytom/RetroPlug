# RetroPlug2

A Game Boy emulator running as an audio plugin — modern reboot of the original
[RetroPlug](https://github.com/tommitytom/RetroPlug). Built for using LSDJ,
mGB, and other Game Boy music software inside a DAW, with the same playback
experience as a real DMG/CGB but as a normal track in your project.

Built on:

- [DPF](https://github.com/DISTRHO/DPF) — cross-platform audio plugin framework
  (CLAP / VST3 / VST2 / JACK)
- [SameBoy](https://github.com/LIJI32/SameBoy) — high-accuracy Game Boy
  emulator core
- A React + TypeScript UI on top of LVGL via QuickJS — the framework slice is
  the [deps/dpf.js](deps/dpf.js) submodule (documented in its own README)

## Status

The architecture is *native owns bytes and cores; TS owns meaning*: a C++ host
([packages/native-greenfield/](packages/native-greenfield)) owns the emulator
cores, realtime queues, byte codecs, and OS paths/dialogs, while the application
logic + React/LVGL UI ([packages/retroplug/](packages/retroplug)) is TypeScript
over a single synchronous `Backend` interface. It boots ROMs, plays
multi-instance audio at the host sample rate, drives the menu/tile UI, and
persists projects. The shared emulator/Project/codec core lives in
[packages/native/src/](packages/native/src) (compiled into the `retroplug-core` lib).

An older build was replaced by this one and has since been removed; the residual
rename/cleanup and the remaining feature gaps (Windows link fixes, AU, the native
file watcher) are tracked in [spec/07-migration.md](spec/07-migration.md). The full
architecture is documented in [`spec/`](spec/README.md).

## Building

Requires CMake 3.14+, a C++20 compiler, Node.js (for the esbuild UI bundle), and
a handful of X11 / OpenGL / audio dev libraries — see
[.devcontainer/Dockerfile](.devcontainer/Dockerfile) for the canonical apt list,
or just use the devcontainer (below). `build.sh` (Linux/macOS) / `build.bat`
(Windows) are the canonical entry points — they run the configure this project
needs and build in parallel.

```bash
git clone --recursive <repo-url>
cd RetroPlug2
pnpm install          # wires the deps/dpf.js link before the first configure
./build.sh            # or: cmake -S . -B build && cmake --build build -j$(nproc)
```

Output (`build/bin/`):

| Format | Path |
|--------|------|
| JACK standalone | `build/bin/retroplug` |
| CLAP | `build/bin/retroplug.clap` |
| VST3 | `build/bin/retroplug.vst3/` |
| VST2 | `build/bin/retroplug-vst2.so` |

### Devcontainer

VS Code: open the repo and "Reopen in Container". The image bakes in every
build dep, the headless agent tooling (`xvfb`, `jackd2`, `xdotool`,
`pluginval`, `clap-validator`, `ffmpeg`, Node.js), and persists Claude Code
state in a Docker named volume.

## Headless workflows

A few pnpm scripts test without a DAW. All run cleanly inside the devcontainer.

```bash
pnpm test        # pure-TS store/kernel tests on the txiki runtime (no C++ build)
pnpm test:native # real host + emulator cores (the native-greenfield test host)
pnpm test:ui     # the React/LVGL UI on a headless software display
pnpm screenshot  # capture the standalone UI -> /tmp/retroplug.png
pnpm validate    # clap-validator + pluginval against the built artifacts
```

The LSDj-sync / DAW-timing / audio-quality matrix (real Reaper renders +
`reaper-timing-analyze.py`) is documented in [docs/lsdj.md](docs/lsdj.md).

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
- [pluginval](https://github.com/Tracktion/pluginval) — VST3 protocol tests
  (strictness 5 by default; bumpable in
  [tools/validate-plugins.sh](tools/validate-plugins.sh))

Both binaries are baked into the devcontainer image. On a host without the
devcontainer, install them manually:

```bash
curl -fsSL https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip -o pv.zip && unzip pv.zip && sudo install -m 0755 pluginval /usr/local/bin/
curl -fsSL https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-ubuntu-18.04.tar.gz | tar -xz && sudo install -m 0755 clap-validator /usr/local/bin/
```

Format validation catches DPF wrapper regressions, ABI bugs, and parameter /
state-restore drift — it does NOT exercise the Game Boy DSP. Behavioural
checks are the job of `pnpm test:native` (real cores) + the Reaper matrix.

## Project layout

```
packages/
  native-greenfield/   C++ host — the DPF plugin + standalone + the txiki test host
                       (Engine, BackendFacade, SnapshotRegistry, DspRuntime, plugin/). See spec/.
  retroplug/           TS layer + React/LVGL UI (stores, DSP kernel, roles, tests)
  native/src/          the shared emulator/Project/codec core (compiled into retroplug-core):
                       SameBoy + Mesen systems, Project, the LSDj sav codec, transport primitives
examples/reaper/       Reaper .rpp fixtures for the DAW host tests (derived; regenerated by the authors)
resources/             ROMs (mGB, LSDj, n8-midi) + the LSDj manual
spec/                  the architecture spec (one doc per concern) + the migration record
tools/                 build/bundle scripts, the headless standalone + reaper harness, validators
deps/                  domain submodules: sameboy, mesen, r8brain, enkiTS, efsw (file watcher)
deps/dpf.js/           the generic framework submodule (DPF, lv_binding_js→LVGL/txiki, rpcpp,
                       msgpack-c, dpf-widgets, lvgl-js-native, the lvgljs runtime); consumed
                       via require.resolve + add_subdirectory + a pnpm link
```

For the React/TSX/QuickJS framework slice (everything that's not Game Boy or
SameBoy specific), see the [deps/dpf.js](deps/dpf.js) submodule. For the
architecture and the migration record, see [spec/README.md](spec/README.md).

## Acknowledgements

- [SameBoy](https://github.com/LIJI32/SameBoy) — accuracy-first Game Boy
  emulator
- [DPF](https://github.com/DISTRHO/DPF) — the audio-plugin framework
- [LVGL](https://github.com/lvgl/lvgl) and
  [lv_binding_js](https://github.com/tommitytom/lv_binding_js) — UI toolkit
  and React/JS bindings
