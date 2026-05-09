# Step 01 — SameBoy MVP

**Status:** Done.

## Goal

Boot one Game Boy ROM end-to-end through the new architecture. Audio out to the
host, framebuffer rendered onscreen, no input. Designed for N instances even
though only one is instantiated.

## Depends on

Nothing — this is the foundation.

## Architecture introduced

The first concrete shapes of every long-term abstraction land in this step,
even when the only inhabitant is a placeholder:

- **`SystemBase`** — virtual interface for an emulator instance.
  [src/system/SystemBase.hpp](../src/system/SystemBase.hpp).
- **`SameBoySystem`** — concrete subclass wrapping `GB_gameboy_t*` and the
  vblank/audio callbacks. [src/system/sameboy/SameBoySystem.cpp](../src/system/sameboy/SameBoySystem.cpp).
- **`Project`** — DSP-thread runtime container holding
  `std::vector<std::unique_ptr<SystemBase>>`. [src/project/Project.hpp](../src/project/Project.hpp).
- **`ProjectConfig`** — plain-data, reflectcpp-serializable mirror.
  [src/project/ProjectConfig.hpp](../src/project/ProjectConfig.hpp).
- **`SystemConfig`** — `std::variant` of per-system configs (just `SameBoyConfig`
  in this step). [src/system/SystemConfig.hpp](../src/system/SystemConfig.hpp).
- **`RomRole`** — virtual interface for per-ROM-type behavior; vector member on
  `SameBoySystem`. Empty seam — first inhabitant lands at step 7.
- **`FrameBufferTriple`** — generic triple-buffered RGBA frame transport, sized
  at construction. Reused by every system kind.
  [src/transport/FrameBufferTriple.hpp](../src/transport/FrameBufferTriple.hpp).
- **`SharedDSPData`** — the shared pointer the UI reaches via
  `getPluginInstancePointer()`. [src/PluginShared.hpp](../src/PluginShared.hpp).

SameBoy itself is vendored at [deps/sameboy/](../deps/sameboy/) with a CMake
target that mirrors the old Premake build (excludes `sm83_disassembler.c`,
`symbol_hash.c`, `debugger.c`; defines `GB_INTERNAL`, `GB_DISABLE_TIMEKEEPING`,
`GB_DISABLE_DEBUGGER`).

## What landed

- DSP bootstraps one `SameBoySystem` from `RETROPLUG_ROM_PATH` (defaults to
  `/home/tommitytom/retro/LSDj-v5.0.3.gb`) at construction time.
- Audio: SameBoy's APU sample callback writes into a per-system stereo
  accumulator; `onProcess` drives `GB_run` until enough samples are produced,
  then deinterleaves into DPF's planar L/R outputs. Master gain applied last.
- Video: vblank callback writes pixels through a custom `rgbEncode` that
  produces LVGL-native XRGB8888 directly (no per-pixel swizzle). After each
  vblank, the triple-buffer publishes and the writer rotates.
- UI: a C++-owned `lv_image` widget on the LVGL screen reads the latest frame
  from the triple-buffer in `uiIdle()` and invalidates. Render at 2× scale.
- React UI is simplified: title, master-gain slider, Esc-menu overlay.
- `plugin.getFrame(systemId)` JS bridge exists as scaffolding for a future
  React-side `<EmulatorTile/>` component (deferred until lv_binding_js gains a
  proper Canvas widget).

## Verification

- All DPF targets (VST2/3, CLAP, LV2, AU, JACK) link clean.
- `[RetroPlug] bootstrap SameBoy id=1 rom='...'` log line fires during LV2 ttl
  generation, confirming the ROM read + `Project::addSystem` path runs at plugin
  instantiation.
- In carla loading the VST3: LSDJ splash bouncing on the canvas, audio chime
  plays. Master-gain slider attenuates audibly.

## Carry-forward limitations

Documented here so later steps know what's still missing:

- **No keyboard input.** Step 02.
- **No UI ROM picker.** ROM is hardcoded. Step 03.
- **No project persistence.** DPF state hooks not wired. Step 04.
- **Single-instance only at runtime.** Architecture supports N; UI shows 1.
  Step 05.
- **No resampling.** `GB_set_sample_rate(host_rate)` accepted as approximate.
  Step 15.
- **No LV2 framebuffer.** Out-of-process UI binary can't reach the DSP-owned
  triple-buffer; falls back to placeholder. Audio still works in LV2.
  Probably solved alongside step 18 (web port) — both need a real IPC path.
- **Framebuffer rendered from C++, not React.** lv_binding_js's Canvas
  component is a stub. Switch to a React-side `<EmulatorTile/>` once a real
  Canvas / raw-pixel-image widget exists in lv_binding_js.

## Open questions

- **Pixel-format swizzle.** Changed `rgbEncode` to LVGL-native XRGB8888.
  Emscripten target (step 18) wants browser-canvas RGBA — revisit then.
- **Boot ROMs disconnected from .asm.** Pre-baked headers shortcut the
  rgbasm/pb12 pipeline. Fine until SameBoy is bumped.
