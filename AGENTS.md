# Agent guide

A DPF audio plugin with an LVGL UI driven by React running in txiki.js (QuickJS). Single-instance template; built via CMake into LV2 / VST2 / VST3 / CLAP / JACK targets.

## Layout

- `src/` — C++ plugin code
  - `PluginDSP.cpp` — DSP class (`run()`, parameters, oscillator state)
  - `PluginUI.cpp` — DPF UI subclass; owns the JS engine and forwards parameters
  - `PluginShared.hpp` — `SharedDSPData` (ring buffer; in-process formats only) and `kPluginParameters` (single source of truth for parameter spec, consumed by both DSP and UI)
  - `PluginJsBridge.{hpp,cpp}` — plugin-specific JS bridges. Currently only `pushWaveform`; this is where to add custom DSP↔UI JS APIs for your plugin.
  - `LvglJsEngine.{hpp,cpp}` — txiki.js runtime wrapper, JS↔native bridge. Owns generic parameter machinery (`setParamWriteCallback`, `registerParameter`, `pushParameter`).
  - `DistrhoPluginInfo.h` — plugin metadata, I/O config, format categories
  - `lv_conf.h` — LVGL configuration (note: `LV_USE_FLOAT = 1`)
- `ui/` — plugin-author React/TSX code
  - `PluginUI.tsx` — React UI entry point. Add as many `.ts`/`.tsx` files as you like and import them from here; esbuild bundles transitively.
  - `tsconfig.json` — TS language-server config. Mirrors the esbuild aliases so the IDE resolves `"lvgljs"` and `"lvgljs-ui"`.
- `runtime/` — framework-provided JS-side runtime (not plugin-author code)
  - `lvgljs/index.ts` — typed front door for the native bridge. Exports `setParameter`, `on`, `off`, and the `useParameter` React hook. Plugin code should `import { ... } from "lvgljs"` rather than reaching into `globalThis[Symbol.for("lvgljs")]`.
- `tools/`
  - `build-ui.js` — esbuild script. CMake invokes it as `node build-ui.js <bundle.js> <bundle_data.c> <bundle.d>`; running with no args writes `../build/ui/bundle.js` for ad-hoc dev.
- Bundle is **not committed**. CMake produces `build/ui/bundle.js` plus `build/ui/bundle_data.c` (C byte array) on every plugin build, and links the latter into the plugin so the UI is embedded.
- `deps/` — submodules
  - `dpf/` — DISTRHO Plugin Framework
  - `dpf-widgets/` — `LVGLWidget` integration (calls `lv_timer_handler` from `idleCallback`)
  - `lv_binding_js/` — forked v9 port at `tommitytom/lv_binding_js` branch `lvgl-v9-port`

## Build

```bash
cd build && make -j$(nproc)
```

That's it — CMake watches `ui/*.ts*` and `runtime/*.ts*` (via `CONFIGURE_DEPENDS` glob)
and esbuild's metafile-derived depfile, so editing any TS/TSX file rebuilds the bundle,
regenerates the embedded C array, and relinks the plugin. Output plugins land in
`build/bin/`.

### Dev iteration without relinking

For tighter UI feedback, set the override env var when launching the host:

```bash
LVGL_PLUGIN_BUNDLE_PATH=$PWD/build/ui/bundle.js jalv build/bin/lvgl-demo-plugin.lv2
```

Then `node tools/build-ui.js $PWD/build/ui/bundle.js` rewrites the bundle and the
host picks it up next time it loads the UI. Some DAWs (notably on macOS) sanitize
the environment, so this trick works most reliably with jalv/Carla/standalone.

## Architecture notes

**DSP → UI waveform path**: `LVGLPluginDSP::run()` writes peak-downsampled samples into `SharedDSPData::waveformRing`. `LVGLPluginUI::uiIdle()` drains the ring and calls `PluginJsBridge::pushWaveform()`, which emits a "waveform" event via `LvglJsEngine::emit()`. The React `Waveform` component subscribes via `on("waveform", ...)` from `"lvgljs"` and calls `setPoints` to re-render an `<Line>`.

**Parameter sync (bidirectional)** — generic, owned by `LvglJsEngine`:
- DSP → UI: host `parameterChanged()` → `LvglJsEngine::pushParameter(idx, value)` → emits "parameter" event → `useParameter` hook updates React state.
- UI → DSP: React's `setGain(value)` (returned by `useParameter`) → `lvgljs.setParameter(idx, value)` C function → `paramWrite` callback set via `setParamWriteCallback` → DPF `editParameter` / `setParameterValue`.

**Parameter spec is single-source-of-truth in `PluginShared.hpp`**: `kPluginParameters[]` defines symbol/name/range/hints. `PluginDSP::initParameter` copies fields onto DPF's `Parameter` struct; `PluginUI` loops over the same array calling `jsEngine.registerParameter(i, kPluginParameters[i].symbol)`. Add a parameter by appending one row to the table (and a `case` in DSP `getParameterValue`/`setParameterValue`).

**JS API surface**: plugin TS code imports from `"lvgljs"` (aliased in `tools/build-ui.js` and `ui/tsconfig.json`). Available exports: `setParameter(name|index, value)`, `on/off(channel, handler)`, and the `useParameter(name, initial)` React hook. The hook throws at mount on unknown parameter names. The native bridge is still registered under `globalThis[Symbol.for("lvgljs")]`, but plugin code should not reach into it directly — `runtime/lvgljs/index.ts` is the supported front door.

**`SharedDSPData` only works for in-process formats** (VST2/3, CLAP, AU, JACK). For LV2 the UI/DSP run in separate binaries; `getPluginInstancePointer()` returns null and a weak fallback in `PluginUI.cpp` makes `getSharedDSPData()` return null. The waveform display is silently disabled in that case. Parameter sync still works in all formats.

**Bundle is embedded into the plugin** at build time. Loading is buffer-based via `LvglJsEngine::evalModuleBuffer`, which calls `TJS_EvalModuleContent` directly — same semantics as the file-based path (`import.meta.url`, `load` event, `tjs:*` import resolution all work). The plugin is fully relocatable.

## Workflow conventions

- Don't push to remotes or open PRs without an explicit ask. The user pushes their own work.
- Don't commit changes to `deps/lv_binding_js` or `deps/txiki` from the parent repo without checking — the submodule pointers are managed deliberately.
- Building requires the existing `build/` directory to be configured already; don't `rm -rf build` to "fix" CMake — investigate first.

## Testing

Load any of the built artifacts under `build/bin/` in a host (Carla, Reaper, Bitwig, jalv for LV2). The plugin currently generates a sine/square test tone — there's no input-processing path. Use the in-plugin sliders or host automation to verify both directions of parameter sync.
