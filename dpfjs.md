# dpfjs

A pairing of [DPF](https://github.com/DISTRHO/DPF) (DISTRHO Plugin Framework) with
[LVGL](https://github.com/lvgl/lvgl) and [lv_binding_js](https://github.com/tommitytom/lv_binding_js)
to write audio-plugin UIs in **React + TypeScript**.

> This document describes the framework slice. Plugin-specific details (in this
> case, RetroPlug2's GameBoy-emulator code) are in [README.md](README.md).
>
> **The framework has now been extracted to its own repo: `dpf.js`** (a sibling
> checkout at `../dpf.js`, consumed via `require.resolve` + `add_subdirectory`).
> The generic C++ (`src/dpfjs/`, lvgl-js-native), the `runtime/lvgljs/` runtime,
> and the framework submodules (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c,
> efsw) all live there now — so paths like `runtime/lvgljs/index.ts` below are
> under `../dpf.js/`. This copy stays as the canonical prose walkthrough until
> the docs migrate into the dpf.js repo (a deferred publish step).

## How it works

```
TSX/JSX  →  esbuild  →  bundle.js  →  QuickJS (txiki.js)  →  React reconciler  →  LVGL widgets  →  DPF window
```

- **esbuild** bundles `ui/*.tsx` + `runtime/*.ts` into a single JS file.
  CMake invokes `tools/build-ui.js`; the bundle is then embedded into the
  plugin binary as a C byte array (`build/ui/bundle_data.c`) so artifacts
  stay relocatable.
- **txiki.js** (QuickJS + libuv) hosts the JS runtime. `LvglJsEngine`
  drives `UV_RUN_NOWAIT` from DPF's idle callback so timers and events
  don't block the UI thread.
- **React reconciler** (`lv_binding_js`) diffs the virtual DOM and
  creates/updates native `lv_obj_t`s.
- **LVGL** renders to a software framebuffer; DPF composites it as an
  OpenGL texture in the plugin window.
- Each plugin instance gets its own JS runtime + LVGL display, so the
  pairing is multi-instance safe.

## Layout (framework parts)

- `src/`
  - `PluginUI.cpp` — DPF UI subclass; owns the JS engine, bridges DPF
    lifecycle (`uiIdle`, `parameterChanged`, `onResize`, `onKeyboard`,
    `uiFileBrowserSelected`) into the JS runtime.
  - `PluginDSP.cpp` — DPF DSP subclass; reads parameters via the spec
    table.
  - `PluginShared.hpp` — `kPluginParameters[]` (single source of truth
    for parameter symbol/name/range/hints; consumed by both DSP and UI)
    and `SharedDSPData` (in-process pointer struct so the UI can reach
    DSP state directly when the plugin format allows it).
  - `PluginJsBridge.{hpp,cpp}` — plugin-specific JS bridges. Where to
    add custom DSP↔UI APIs that aren't covered by the generic
    parameter/state machinery.
  - `LvglJsEngine.{hpp,cpp}` — txiki.js runtime wrapper, JS↔native
    bridge. Owns the generic parameter machinery
    (`setParamWriteCallback`, `registerParameter`, `pushParameter`).
  - `lv_conf.h` — LVGL configuration. `LV_USE_FLOAT = 1` and
    `LV_USE_SNAPSHOT = 1`.
- `ui/` — plugin-author React/TSX. Add as many `.ts`/`.tsx` files as
  you like; esbuild bundles transitively from `ui/PluginUI.tsx`.
  `ui/tsconfig.json` mirrors esbuild's import aliases for IDE support.
- `runtime/lvgljs/index.ts` — typed JS-side front door for the native
  bridge. Plugin code does `import { setParameter, on, useParameter } from "lvgljs"`
  rather than reaching into `globalThis[Symbol.for("lvgljs")]` directly.
- `tools/build-ui.js` — esbuild script. CMake calls it as
  `node build-ui.js <bundle.js> <bundle_data.c> <bundle.d>`; running with
  no args writes `../build/ui/bundle.js` for ad-hoc dev.
- `deps/`
  - `dpf/` — DISTRHO Plugin Framework
  - `dpf-widgets/` — `LVGLWidget` integration; calls `lv_timer_handler`
    from DPF's `idleCallback`
  - `lv_binding_js/` — forked v9 port at `tommitytom/lv_binding_js` branch
    `lvgl-v9-port`. Vendors LVGL v9.5.0 and txiki.js v26.4.0.

## Build pipeline

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

CMake watches `ui/*.ts*` and `runtime/*.ts*` via `CONFIGURE_DEPENDS` glob,
and esbuild emits a metafile-derived depfile. Editing any TS/TSX file
rebuilds the bundle, regenerates the embedded C array, and relinks the
plugin. Output lands in `build/bin/`.

The bundle is **never committed**; it's regenerated per build.

### Hot reload without relinking

For tighter iteration, set the override env var when launching the host:

```bash
LVGL_PLUGIN_BUNDLE_PATH=$PWD/build/ui/bundle.js jalv build/bin/<plugin>.lv2
```

`PluginUI.cpp` reads this env var and, when set, loads the bundle from
disk instead of from the embedded byte array. Re-run
`node tools/build-ui.js $PWD/build/ui/bundle.js` to rewrite the bundle;
the host picks it up the next time it loads the UI. Some DAWs (notably
on macOS) sanitise the environment, so this works most reliably with
jalv / Carla / standalone.

## Parameter sync

Bidirectional, owned by `LvglJsEngine`:

- **DSP → UI**: host `parameterChanged()` → `LvglJsEngine::pushParameter(idx, value)`
  → emits `"parameter"` event → React's `useParameter` hook updates state.
- **UI → DSP**: React's `setX(value)` (returned by `useParameter`) →
  `lvgljs.setParameter(idx, value)` C function → `paramWrite` callback
  set via `setParamWriteCallback` → DPF `editParameter` /
  `setParameterValue`.

The parameter spec is single-source-of-truth in `PluginShared.hpp`:
`kPluginParameters[]` defines symbol/name/range/hints. `PluginDSP::initParameter`
copies fields onto DPF's `Parameter` struct; `PluginUI` loops over the
same array calling `jsEngine.registerParameter(i, kPluginParameters[i].symbol)`.

To add a parameter: append one row to `kPluginParameters[]` and a
`case` in DSP's `getParameterValue`/`setParameterValue`. The UI side
picks it up automatically.

## JS API surface

Plugin TS code imports from `"lvgljs"` (aliased in `tools/build-ui.js`
and `ui/tsconfig.json`):

```ts
import { setParameter, on, off, useParameter } from "lvgljs";
```

- `setParameter(name | index, value)` — push to host
- `on(channel, handler)` / `off(channel, handler)` — subscribe to native
  events emitted via `LvglJsEngine::emit()`
- `useParameter(name, initial)` — React hook returning
  `[value, setter]`. Throws on mount if `name` is unknown.

Plugin code should not reach into `globalThis[Symbol.for("lvgljs")]`
directly — `runtime/lvgljs/index.ts` is the supported front door so it
can evolve without breaking imports.

## Plugin format quirks

`SharedDSPData` only works for **in-process** plugin formats (VST2,
VST3, CLAP, AU, JACK). For LV2 the UI and DSP run in separate binaries;
`getPluginInstancePointer()` returns null and a `__attribute__((weak))`
fallback in `PluginUI.cpp` makes `getSharedDSPData()` return null
gracefully. Anything that depends on `SharedDSPData` should degrade
silently in that case. **Parameter sync still works in all formats** —
that path goes through DPF, not shared memory.

## Screenshot env var (UI inspection)

`PluginUI.cpp` periodically dumps the LVGL screen to PNG when
`RETROPLUG_SCREENSHOT_PATH` is set. Cadence is controlled by
`RETROPLUG_SCREENSHOT_INTERVAL_MS` (default 1000). When the env var is
unset, the hook is zero-cost. Implementation: `lv_snapshot_take`
(LVGL v9, gated by `LV_USE_SNAPSHOT = 1` in `lv_conf.h`) →
`lodepng_encode24_file` (already vendored via `lv_binding_js`).

Useful for headless / agent verification of UI changes — pair with
Xvfb if there's no display server.

> When dpfjs is extracted, this env var should be renamed to something
> generic like `DPFJS_SCREENSHOT_PATH`. For now it carries RetroPlug2's
> name.

## Plugin-format validation

Two off-the-shelf binary validators handle protocol-compliance testing
for any DPF plugin:

- [pluginval](https://github.com/Tracktion/pluginval) — VST3, AU, LV2,
  LADSPA. Strictness 1–10 (5 is "minimum host compatibility", 10 is
  parameter fuzz + state-restoration). Exit-code based; CI-friendly.
- [clap-validator](https://github.com/free-audio/clap-validator) — CLAP
  format. Runs each test in a child process so plugin crashes don't
  kill the runner.

Both are single-binary GitHub Releases downloads. They catch DPF
wrapper regressions, ABI bugs, parameter / state-restore drift, and
threading violations — they don't exercise plugin-specific DSP, since
they feed silent / synthetic input.

## Workflow conventions

- The framework submodules (lv_binding_js → LVGL/txiki, DPF, rpcpp, msgpack-c,
  efsw) live in the `dpf.js` repo now. Don't commit changes to any submodule
  pointer — in either repo — without checking; they're managed deliberately.
- Treat the bundle as derived: never check in `build/ui/bundle.js` or
  `build/ui/bundle_data.c`.
- The `build/` directory needs to stay configured; don't `rm -rf build`
  to "fix" CMake — investigate first.
