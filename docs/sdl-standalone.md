# RetroPlug SDL standalone — status, gaps, and future work

`retroplug-sdl` ([packages/native/sdl/main.cpp](../packages/native/sdl/main.cpp)) is a DPF-free SDL2 host for
the RetroPlug standalone. It reuses the whole core (backend + txiki runtime + LVGL UI bundle) and replaces
only the DPF/pugl window+audio+input shell with SDL2. It exists because DPF's standalone (`retroplug-jack`)
needs X11/pugl, which rules out framebuffer-only targets (e.g. muOS handhelds — fbdev + Mali EGL, no X11);
SDL2 drives those via its own video backend. **Goal: make `retroplug-sdl` the primary standalone; keep
`retroplug-jack` around for debugging DPF/plugin behaviour.**

## What the SDL host already does

- Full control-plane + React/LVGL UI on one shared `TjsHostRuntime` (same graph as the plugin), embedded
  CP/UI bytecode bundles, the `__rp_mountUI` / `__rp_ready` boot.
- SDL2 window; software blit of the LVGL buffer with dirty-region present; SDL drives GLES2 internally.
- SDL audio callback → `engine.processBlock` (stereo); live sample-rate/block-size reconfigure (Settings →
  Audio); `audio.cfg` persistence.
- Input: keyboard + mouse → LVGL indevs + the `key` bus; gamepad (`GamepadManager`) → menu nav + game input.
- On-screen LVGL **file browser** (no dependence on OS dialogs), **Exit** menu row, `openPath`, window title.
- Optional Linux RT audio thread (SCHED_FIFO + core affinity), for headroom on constrained devices.

It is **not** strictly a subset of the DPF standalone — it *adds* the on-screen file browser, the RT audio
thread, live audio reconfigure, and dirty-region present.

## Gaps vs the DPF/JACK standalone (to reach parity)

### P1 — MIDI input (the big one)
No MIDI source at all (SDL2 has no MIDI). Blocks **mGB** (the GB MIDI synth is unplayable without it), NES
MIDI-driven ROMs, and every LSDj MIDI-sync/map/passthrough mode. Plugin reference: `PluginDSP::run` stages
host MIDI via `engine_.stageMidi(frame, bytes)` ([PluginDSP.cpp:163](../packages/native/plugin/PluginDSP.cpp#L163)).
→ Add a MIDI-in source: ALSA seq (Linux, incl. USB-MIDI) / CoreMIDI (mac) / WinMM (win), or RtMidi
cross-platform; feed `engine.stageMidi` from a poll on/near the audio thread. Where there's no MIDI port,
consider an on-screen keyboard.

### P2 — MIDI output
`engine.midiOut()` is never drained. Blocks LSDj **MI.OUT** / Master Sync / MIDI passthrough to external gear.
→ Drain `engine.midiOut()` each block to an out port; mirror
[PluginDSP.cpp:178-186](../packages/native/plugin/PluginDSP.cpp#L178).

### P3 — File drag-and-drop
No `SDL_DROPFILE` handling. The file browser covers loading, but desktop drop (ROM/.sav/project → the App's
`file-drop` channel: load / cold-boot-replace a tile / pair a `.sav`) is absent. → `SDL_DROPFILE` →
`jsEngine.emit("file-drop", paths + x/y)`, mirror [PluginUI.cpp:389-398](../packages/native/plugin/PluginUI.cpp#L389).
Trivial.

### P4 — Host transport / tempo
Hardcoded `setBpm(120)` / `setTransport(true)` ([main.cpp:630](../packages/native/sdl/main.cpp#L630)). No
external tempo/transport — LSDj sync-to-external won't lock. Tied to P1: derive BPM + start/stop from
incoming MIDI clock once MIDI-in exists.

### P5 — Multi-output audio routing
SDL is stereo-only (the 2-arg `processBlock`). The per-channel stems / per-instance outputs (Audio Routing
modes 1-3, the plugin's 8 outputs via `processBlock(outputs, N)`) all collapse to stereo, so those menu
options are inert. → Open a multichannel SDL device + the N-output `processBlock`. Low priority (matters only
on desktop with a multichannel interface).

### P6 — Window resize / zoom-to-grid + close guard
`jsSetWindowSize` is a **no-op** ([main.cpp:446](../packages/native/sdl/main.cpp#L446)) — the window is fixed,
so multi-instance grid growth + zoom changes don't resize it. And `SDL_QUIT` sets `running = false` directly
([main.cpp:839](../packages/native/sdl/main.cpp#L839)), **bypassing the unsaved-changes guard**
(`__rp_onCloseRequested`) — closing via the window button / Ctrl-C skips the save prompt (the Exit *menu* item
does guard). → Implement `jsSetWindowSize` via `SDL_SetWindowSize`; route `SDL_QUIT` through
`__rp_onCloseRequested`.

### N/A — DAW-only (listed for completeness)
Parameters/automation, DPF state-chunk get/setState, latency reporting, per-output labels — plugin-in-DAW
concerns the standalone doesn't need.

## Performance notes (future work)

Profiled with the in-tree harness (`dsp-bench` / `nes-bench` under a `RETROPLUG_PROFILE` host; the `DSP_SPAN_*`
trace spans). Audio-thread JS is ~0.5% of a block for both cores — the emulator core dominates. Two items
stand out, most relevant on constrained (low-power) devices:

- **NES savestate-serialize spike.** `SnapshotRegistry::publishAll` pumps core state to the control plane on
  the audio thread every block (a framebuffer copy) plus a **full savestate serialize every 0.5 s**
  (`SystemBase::publishStateSnapshot` → `captureStateSnapshot`, which runs *inside* the core block). For
  Mesen that serialize is heavy — a large periodic audio-thread spike — while it's consumed only **on demand**
  (save-state / save-project / render; nothing polls it, and NES SRAM auto-save uses the savestate-independent
  `readSram`). → **On-demand / request-gated capture** (the control thread flags "capture now", the audio
  thread serializes on the next block, the caller waits ~1 block) removes the spike during play. Instrumented
  via `DSP_SPAN_PUBLISH`.
- **NES core cost.** Mesen is far heavier than SameBoy per block; on a low-power core it can miss realtime.
  LTO on the Mesen target buys ~10% (already enabled for optimized builds); sample-rate/block-size don't help
  (Mesen emulates a full NES-second per wall-second). Remaining levers are a lighter/less-accurate NES core or
  accepting NES as best-effort. (Note: LTO *regresses* SameBoy — a tight computed-goto core — so it stays
  `-O3` no-LTO.)

## Build / run

```bash
cmake --build build --target retroplug-sdl -j$(nproc)
# headless smoke: RETROPLUG_SDL_EXIT_AFTER_FRAMES=120 RETROPLUG_SDL_SCREENSHOT=/tmp/f.bmp ./build/bin/retroplug-sdl
```
