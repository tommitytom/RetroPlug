# SDL standalone: release readiness checklist

Everything that has to be dealt with before `retroplug-sdl` replaces the DPF standalone (`retroplug-jack`)
in `release.yml`. Assessed 2026-10-06 against `main@cfeb80e0`. **Keep this file current:** tick an item
(`- [x]`) in the same commit that fixes it, and add the commit hash. New findings go in the right section.

Status at assessment: `./build.sh` exit 0, `pnpm sdl:smoke` 8/8, `pnpm sdl:pipewire` 8 passed / 0 failed
(wide-output leg skipped: no wireplumber in the container).

Background: [sdl-standalone.md](sdl-standalone.md) (feature history; its parity list predates most of this),
[audit-2026-09-20.md](audit-2026-09-20.md) (items marked *audit* below were first raised there).

## 1. Bugs (fix first)

- [x] **MidiIo ring has multiple producers.** *Fixed in `e059a786`: producers serialize on `pushMutex_`,
  `poll()` stays lock-free; guarded by the ring case in `retroplug-midi-test` (plain build aborted/segfaulted
  3/3 without the fix, TSan clean with it).* Original finding: `MidiIo.hpp` documented an SPSC ring with
  one producer, but the virtual `RetroPlug In` port and every hardware `RtMidiIn` each run their own
  callback thread into `pushRing` (`MidiIo.cpp`). Any hardware input pick (or All Devices) gives two or more concurrent
  producers: torn slots, lost messages, possible heap corruption from two threads assigning one slot's
  vector. Fix: serialize the producer side (spinlock / mutex in `pushRing`; consumer stays lock-free), or
  one ring per port. Add a TSan run that drives two producers.
- [ ] **Pa_StopStream / Pa_StartStream used as a lock.** `reconfigureMidi`, the Launchpad
  `setOnLinkChanged` and `setOnScanBusy` callbacks in `sdl/main.cpp` stop and restart the stream to
  quiesce the audio thread. On PipeWire `StopStream` calls `pw_stream_disconnect`, so hand-patched routing
  (wide outs in qpwgraph) is lost and a new node appears; every case is an audible gap. Fix: a pause
  handshake with the callback (request flag + ack) instead of a stream restart.
- [ ] **Dead audio device = frozen app.** The emulator only advances inside the audio callback. A device
  that disappears (USB unplug, PipeWire node removed, suspend) freezes everything; nothing watches stream
  health (`Pa_SetStreamFinishedCallback` / `Pa_IsStreamActive`). *audit:* `Pa_StartStream` /
  `Pa_StopStream` return values are ignored, so a failed restart leaves a live-looking app with a dead
  emulator and an undrained command ring. Fall back to the headless pump (or reopen) on failure.
- [ ] **Bad persisted audio config = muted forever.** If `audio.json` holds params the device rejects,
  `openAudio` mutes on every launch with no fallback to defaults, and `__rp_getAudioConfig` has no
  status field, so the UI can't say the device failed to open. Retry with defaults; expose
  open/muted/error to Settings > Audio.
- [ ] **RT tuning applies once per process** (*audit*). `paCallback`'s `static std::atomic<bool> tuned`
  latches on the first stream. On ALSA (the handheld) each `StartStream` spawns a new thread, and because
  of the stop/start lock above that now happens in ordinary use, so the audio thread loses SCHED_FIFO +
  core pinning. On PipeWire the same call sets SCHED_FIFO 80 and pins PipeWire's shared data-loop thread,
  overriding rtkit. Re-tune per callback thread (compare `pthread_self()`), and only on ALSA.

## 2. Parity gaps vs the DPF standalone

- [ ] **System > Render missing.** `__rp_startRender` / `__rp_cancelRender` / `__rp_dismissRenderJob` /
  `__rp_getRenderJobs` are bound only in `plugin/PluginUI.cpp` (around `RenderJobRegistry g_renderJobs`).
  That code is not DPF-specific; lift it into a shared binder (see section 5) and bind it in SDL.
- [ ] **No file watcher.** SDL never calls `svc.host.enableWatching(...)` nor pumps `__rp_pumpWatcher`
  (the plugin does both: `PluginDSP::activate`, `PluginUI::uiIdle` every 15 ticks). `config.json` /
  bindings edits and ROM-rebuild hot reload are dead in the standalone.
- [ ] **Right-click instance menu dead; every button is a left click.** SDL never emits the `"mouse"`
  bus (`[button, press, x, y]`, consumed in `App.tsx` for right-click-to-open), and
  `handleEvents` sets `input.mouseDown` for any button, so right/middle click acts as a left click on
  LVGL. Map SDL buttons to DPF's (left=1, right=2, middle=3), emit `"mouse"`, and only drive the pointer
  indev from the left button.
- [ ] **No HiDPI.** No `SDL_WINDOW_ALLOW_HIGHDPI` and a software renderer: blurry on Retina and fractional
  Wayland, no DPI awareness on Windows. The DPF host uses `getScaleFactor()`.
- [ ] **MIDI-in sub-block timing.** `renderAudioBlock` stages everything at frame 0 and RtMidi timestamps
  are discarded (`MidiIo::onMidiIn` ignores `timeStamp`; `Message.seq` is never turned into an offset).
  Up to one block of jitter: 10.7 ms at 512 frames, 85 ms at the handheld's 4096. The plugin carries
  `e.frame`.
- [ ] **MIDI clock is tempo-only, no phase lock.** `MidiClockSync` estimates BPM over 24-pulse windows
  (window ends on a block boundary, so roughly +/-1 pulse, ~4%) and the Engine free-runs at that estimate.
  Expect LSDj to drift in phase against an external master over a long take. Not yet measured: add a
  drift render (like `reaper:lsdj-*` / `--drift`) before deciding how much to invest.
- [ ] **Initial window size ignores the project.** Always opens 640x480; the plugin asks
  `__rp_initialWindowSize`. Minor (the fit-to-grid effect resizes after mount).

## 3. Release packaging

- [ ] **Windows: console subsystem.** No `WIN32_EXECUTABLE` on the target, so a console window opens with
  the app. Use the GUI subsystem (keep a `--console` / log-file path for diagnostics).
- [ ] **Windows: Auto driver resolves to MME.** `autoOutputHostApiIndex` falls back to the platform
  default host API, which is MME (first in `deps/portaudio/src/os/win/pa_win_hostapis.c`). Prefer WASAPI
  on Windows (CoreAudio on macOS is fine).
- [ ] **macOS: bare executable.** No `.app` bundle, no embedded `SDL2.framework` / `@executable_path`
  rpath, no Info.plist / icon, no signing / notarization.
- [ ] **CLI / launch UX.** Fullscreen only via `RETROPLUG_SDL_FULLSCREEN` (no flag, no toggle key); no
  positional file argument (open-with / file association); no `--help` / `--version`; unknown args
  silently ignored; `--width` / `--height` unvalidated (0 is accepted).
- [ ] **Device hotplug.** PortAudio's device list is fixed at `Pa_Initialize`, so a USB interface plugged
  in later never appears in Settings > Audio until restart. MIDI ports are likewise scanned at open (and
  on a pick).
- [ ] **Release workflow.** Add `retroplug-sdl` to the four `release.yml` packaging allowlists (and drop
  the "deliberately NOT packaged" comments) once the rest of this file is done; decide whether
  `retroplug-jack` keeps shipping.

## 4. Config / persistence

- [ ] **`audio.json` / `midi.json` are unversioned.** Native-owned reflect-cpp JSON with no version stamp,
  against the AGENTS.md config-migration rule. `midi.json` already changed meaning silently (`""` was All
  Devices, now None). Stamp them (or move them into the TS-owned versioned config) and write atomically
  (temp file + rename; `writeTextFile` truncates in place).

## 5. Code quality / duplication

- [ ] **Shared host hook binder.** Duplicated with `PluginUI` (*audit*): window-geometry latch
  (`wmControlled` / `sizeHonored` / `requestedW/H`), file-browser hook + delivery, quit / openPath / title
  hooks, PNG screenshot (x3 with `test/ui/RenderCore.cpp`), control-plane boot (x4 with CLI + Node). One
  binder taking a small host interface would also bring Render (section 2) to SDL for free.
- [ ] **Test scaffolding in the shipping `main()`** (*audit*). 23 `getenv("RETROPLUG_...")` call sites (18 distinct variables),
  including a complete `MidiClockSync` unit test. Move `MidiClockSync` to `src/host` with a Catch2 test;
  gate the rest behind one self-test entry point.
- [ ] **Split `sdl/main.cpp`** (~1900 lines) into audio / input / window / hooks units.
- [ ] **Stale comments, here and in `sdl-standalone.md`:** "SDL2 audio callback" (it's PortAudio), "SDL
  drives Mali GLES2 internally" (`SDL_RENDERER_SOFTWARE` forces the software renderer), header says the
  gamepad feeds the keypad indev (`pumpGamepad`'s comment says it must not), `see audioCb` (gone),
  `allowed_changes=0`, "interleave into the SDL stream", "Instance menu > Transport" vs "MIDI Clock".
  `sdl-standalone.md` also claims parity that sections 2-3 contradict.
- [ ] **Trivia.** Duplicate `#include <atomic>`; dead `(void)ctx;` in the main loop; one-line wrappers
  `readReady` / `hasGlobalFunction` / `deliverFileBrowserResult` around the shared helpers.

## 6. Test coverage

- [ ] `pnpm sdl:smoke` as a CI step (today CI proves it compiles and links, not that it runs).
- [ ] `retroplug-sdl` under `tools/run-sanitizer.sh`. (The MidiIo ring is covered on its own now: build
  `retroplug-midi-test` in `build-tsan/` and run it with `TSAN_OPTIONS=halt_on_error=1`.)
- [ ] Run the `sdl:pipewire` wide-output leg somewhere wireplumber starts.
- [ ] Smoke checks for each parity fix above (render job, watcher, right-click `"mouse"` bus).
