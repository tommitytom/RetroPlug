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

### P1 — MIDI input — ✅ DONE
Implemented via RtMidi ([MidiIo](../packages/native/src/host/input/MidiIo.hpp)) — virtual `RetroPlug In`
port + auto-opened hardware inputs → a lock-free ring drained in the audio callback → `engine.stageMidi`.
Hardware-verified (virtual ports open on ALSA, keyboard input fixed). Follow-ups: hotplug (ports scanned once
at startup) and a real sub-block frame offset (staged at frame 0 today).

Original design notes below (kept for reference):

No MIDI source at all (SDL2 has no MIDI). Blocks **mGB** (the GB MIDI synth is unplayable without it), NES
MIDI-driven ROMs, and every LSDj MIDI-sync/map/passthrough mode. Plugin reference: `PluginDSP::run` stages
host MIDI via `engine_.stageMidi(frame, bytes)` ([PluginDSP.cpp:163](../packages/native/plugin/PluginDSP.cpp#L163)).

**Decision: RtMidi** (vendored), behind a thin `MidiIo` seam — cross-platform (ALSA seq / JACK on Linux,
CoreMIDI on mac, WinMM/WinRT on Windows), small, permissively licensed, still maintained, and it supports
**virtual ports** on ALSA/CoreMIDI. (PortMidi is older/C/polling and less maintained; a DIY per-OS
implementation isn't worth it when RtMidi is exactly that abstraction.)

Design:
- **Input** — `RtMidiIn` callback (its own thread) pushes `{arrival, bytes}` into a lock-free SPSC ring; the
  audio callback drains it at block start and stages via the existing control→audio MIDI ring (`stageMidiIn`,
  NOT `stageMidi` directly — RtMidi is cross-thread). Frame offset can be approximate (live hardware MIDI
  jitters anyway; block-quantized is fine).
- **Ports** — create a virtual `RetroPlug` in+out port on Linux/mac (DAWs/controllers connect like the plugin
  does in a host) and auto-connect hardware inputs (USB-MIDI on handhelds). Windows has no virtual ports → open
  a hardware port (or defer).
- Where there's genuinely no MIDI source, an on-screen keyboard is a later nicety.

### P2 — MIDI output — ✅ DONE
`audioCb` drains `engine.midiOut()` to the `RtMidiOut` virtual port each block (mirrors
[PluginDSP.cpp:178-186](../packages/native/plugin/PluginDSP.cpp#L178)). Hardware-verified: LSDj Master-Sync
clock (`0xF8`) reaches `aseqdump`. `RETROPLUG_MIDI_LOG` dumps in + out bytes.

### P3 — File drag-and-drop — ✅ DONE
`handleEvents` translates SDL's drag-and-drop onto the App's `file-drop` channel (load / cold-boot-replace a
tile / pair a `.sav`), mirroring [PluginUI.cpp:392-400](../packages/native/plugin/PluginUI.cpp#L392). SDL2's
`SDL_DropEvent` carries no coordinates (`x`/`y` are SDL3-only) and one path per event, so the host batches
`SDL_DROPFILE`s across `SDL_DROPBEGIN`→`SDL_DROPCOMPLETE` into one newline-joined string (so a ROM+`.sav` pair
arrives together) and reads the cursor via `SDL_GetMouseState` for the tile hit-test (best-effort — the WM may
not feed SDL a `MOUSEMOTION` during an external drag; a stale/missed coord just falls back to the focused tile).
Desktop-only — the muOS handheld has no file manager. The JS routing (`resolveDropAction` + `hitTestTile`) is
unchanged and already covered by `test-ui/file-drop.test.ts`.

### P4 — Host transport / tempo — ✅ DONE
`audioCb` now derives the host transport from incoming MIDI real-time bytes instead of the old hardcoded
`setBpm(120)`/`setTransport(true)`. A small audio-thread-only estimator (`MidiClockSync`) reads the drained
MIDI: `0xF8` clock pulses feed a windowed 24-PPQN tempo estimate (BPM = 60·sr·pulses / (frames·24), measured
in audio frames since RtMidi timestamps are discarded), and `0xFA`/`0xFB`/`0xFC` (start/continue/stop) plus a
~0.5 s clock-presence timeout drive the playing flag — so a bare-clock master with no start/stop still plays.
With **no** external clock it falls back to the prior free-running 120/playing, so non-sync use (mGB, un-synced
LSDj) is unchanged. The real-time bytes are consumed for transport and not staged into the emulator; channel
messages (notes/CC) still stage as before.

This is all that's needed for **LSDj sync-to-external to lock**: the LSDj `MidiSync` role regenerates its `0xF8`
serial clock from the Engine's tempo/transport via `eachTick`
([dspRoles.ts](../packages/retroplug/src/dspRoles.ts), [PpqUtil.hpp](../packages/native/src/util/PpqUtil.hpp))
— it never read the raw wire clock — so feeding `setBpm`/`setTransport` (exactly as
[PluginDSP::run](../packages/native/plugin/PluginDSP.cpp#L152) does from the DAW's `TimePosition`) is the whole
job. Verified headlessly via `RETROPLUG_SDL_TEST_CLOCK=<bpm>` (synthetic clock → derived BPM within ~0.2, plus
stop + timeout-revert). Follow-up: MIDI clock has no sub-block frame offset (block-quantized, like MIDI-in).

### P5 — Multi-output audio routing
SDL is stereo-only (the 2-arg `processBlock`). The per-channel stems / per-instance outputs (Audio Routing
modes 1-3, the plugin's 8 outputs via `processBlock(outputs, N)`) all collapse to stereo, so those menu
options are inert. → Open a multichannel SDL device + the N-output `processBlock`. Low priority (matters only
on desktop with a multichannel interface).

### P6 — Window resize / zoom-to-grid + close guard — ✅ DONE
**Resize:** `__rp_setWindowSize` now resizes the window to the grid (multi-instance growth / zoom / layout
changes), mirroring the DPF standalone. Since SDL gives no platform resize callback, `resizeWindow` does inline
what DPF's callback does for the plugin: `SDL_SetWindowSize` + `lv_display_set_resolution` + realloc the DIRECT
draw buffer (`lv_display_set_buffers`) + recreate the STREAMING texture at the new size + emit the `"resize"`
JS event so the UI re-lays-out. It's gated on **windowed vs fullscreen**: `__rp_isWindowSizeControlled` returns
`true` for a fullscreen handheld (the WM owns a fixed panel → the UI fits via zoom, resize is inert) and `false`
for a desktop window (ours to size → the App's fit-to-grid effect drives it).

**Close guard:** `SDL_QUIT` (window button / WM / Ctrl-C) now routes through the unsaved-changes guard
(`__rp_onCloseRequested`), a line-for-line mirror of [PluginUI::onClose](../packages/native/plugin/PluginUI.cpp#L352):
a `true` (veto) return raises the save prompt and leaves the window open (the JS overlay calls `__rp_quitWindow`
itself once the user confirms Save/Discard); a clean project quits immediately. Previously `SDL_QUIT` set
`running = false` directly and lost unsaved work (only the Exit *menu* row guarded).

Verified headlessly via the `RETROPLUG_SDL_TEST_RESIZE=WxH` and `RETROPLUG_SDL_TEST_QUIT` self-test hooks
(resize reallocs + renders a full frame at the new size; a clean-project QUIT exits without veto) plus the
existing `test-ui/resize.test.ts` + `test-ui/close-guard.test.ts` for the JS contract.

### N/A — DAW-only (listed for completeness)
Parameters/automation, DPF state-chunk get/setState, latency reporting, per-output labels — plugin-in-DAW
concerns the standalone doesn't need.

## Planned architecture decisions

### Audio backend: SDL audio now → PortAudio (PipeWire) later
Current audio is the SDL callback → `engine.processBlock`. The eventual target is a **PortAudio fork with
native PipeWire support** (<https://github.com/tommitytom/portaudio/tree/pipewire>) — better latency + device
handling than SDL's ALSA-compat path, especially on handhelds. To keep the swap mechanical, keep the audio
backend behind the thin seam that's already mostly in place (`openAudio` / `audioCb` / `reconfigureAudio`
handoff): both SDL and PortAudio are callback-based, so the swap is "implement the same 3 functions against
PortAudio," selectable at build/config, with the RT-thread tuning + block-size reconfigure kept
backend-agnostic. Nothing in the MIDI design (P1/P2) changes — the MIDI ring still drains in whichever audio
callback is active. **Deferred** (after MIDI + the file browser).

### File browser: in-app (React/LVGL) browser is the default; native dialogs are a toggle — ✅ DONE
An **in-app React/LVGL file browser** ([fileBrowserMenu.ts](../packages/retroplug/ui/screens/menu/fileBrowserMenu.ts),
rendered by the Menu component so it inherits all the keyboard/gamepad nav) is the default on **every host** —
SDL *and* the DPF plugin — for both open and save (save adds a filename prompt). Backed by the cross-host
`backend.listDir(dir)` RPC ([backend.ts:53](../packages/retroplug/src/backend.ts#L53)), which now marks
directories with a trailing `/`.

- **Routing** ([useFileBrowser.ts](../packages/retroplug/ui/lvgl/useFileBrowser.ts)): the UI installs a JS
  `__rp_openFileBrowser` that opens the overlay, overriding any native host browser. `realBackend` still calls
  that global hook + awaits `__rp_onFileBrowserResult`, so the browse crosses the control-plane↔UI **bundle
  boundary via globalThis** (module singletons don't — that's the key subtlety).
- **Toggle**: `Settings > File Dialogs` (In-App / OS Native) sets `userConfig.useNativeFileDialogs`; when on
  *and* the host provides an OS dialog, the bridge delegates to it. On a host with no OS dialog it stays in-app.
- The old **SDL C++ browser** (`fbOpen`/`fbPopulate` + its input gating in main.cpp) has been **removed** — one
  browser implementation, and SDL keeps only `listDir`. Verified by `fileBrowser.test.ts` + the full UI suite.

Self-contained; independent of the MIDI/audio work.

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
