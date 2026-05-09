# Step 18 — Web/Emscripten port

**Status:** Pending.

## Goal

Build the same C++ core to WebAssembly via Emscripten and ship a browser
version of RetroPlug. Reuses `Project`, `SystemBase`, the role infrastructure
— anything that doesn't depend on DPF or shared-memory IPC. The shell becomes
a static web page with audio worklets and an HTMLCanvas.

## Depends on

- Everything functional you want in the web build. Most realistic landing
  point: after Phase 4 (extension framework), so users can ship custom
  views to the browser too.

## Architecture introduced

- **Emscripten target alongside DPF targets.** A separate
  `CMakeLists.txt`-driven build that compiles to `.wasm` + `.js`. The
  toolchain is `emcmake cmake` from `emsdk` (already vendored at
  [old/thirdparty/emsdk/](../old/thirdparty/emsdk/) — port to `deps/emsdk/`
  if we keep it vendored, otherwise expect users to install it).
- **Replace DPF with a thin shim.** A small `WebHost` class implements the
  same lifecycle the DSP relies on (sample rate, MIDI input/output, audio
  callback) backed by:
  - `AudioWorklet` for the audio thread.
  - `Web MIDI API` for MIDI.
  - `OffscreenCanvas` for the framebuffer.
- **Replace QuickJS+React with browser JS+React.** The UI bundle already
  builds with esbuild — point esbuild at the browser bundler config; render
  into an HTMLCanvas via `lvgl-pixels` or directly via Canvas2D, sidestepping
  LVGL entirely on the web. Decide:
  - **Reuse LVGL** via lv_binding_js's already-existing browser path (yes, it
    has one — see lv_binding_js docs). Same React tree; lvgljs-ui works.
    Highest fidelity to the desktop UI.
  - **Drop LVGL on the web** and render the React tree to native HTML/CSS.
    Better web ergonomics, two UI codebases to maintain.
  Recommend reusing LVGL via the lv_binding_js browser path — single UI
  codebase wins over fidelity-per-platform.
- **Shared memory replacement.** Browser doesn't share memory between threads
  the way in-process plugin formats do. AudioWorklets get a separate thread;
  use a `SharedArrayBuffer`-backed `FrameBufferTriple` (already designed to
  be lock-free atomic — same pattern works) plus `MessagePort` for command
  queues.
- **File handling.** ROM picker becomes the browser's file `<input>`. Save
  state becomes IndexedDB or browser-managed file save.

## Tasks

1. Set up Emscripten build target.
2. Build SameBoy + the new project core for wasm. Most code should compile
   unchanged; expect adventure with `<filesystem>` and threading.
3. Implement `WebHost` against AudioWorklet/Web MIDI/Canvas APIs.
4. Wire `SharedArrayBuffer` into `FrameBufferTriple` and `CommandQueue`
   (single-source-of-truth atomics already match).
5. Browser shell: small index.html + bootstrap that loads the wasm module
   and the UI bundle.
6. Persistence: IndexedDB-backed `getState`/`setState` round-trip; same JSON
   format as desktop.

## Verification

- Build succeeds on `emcmake cmake .. && make`.
- Open `localhost:port/retroplug/`; ROM picker accepts a `.gb`; LSDJ boots
  in browser; audio plays through Web Audio.
- Latency-sensitive features (LSDJ MIDI sync, Arduinoboy modes) work but
  acknowledge worse jitter than native.
- Same TS extensions load in browser as on desktop.

## Risks / open questions

- **AudioWorklet quirks.** Worklets have stricter scheduling and quota rules
  than native audio threads. Profile carefully; backbuffer audio if needed.
- **`SharedArrayBuffer` cross-origin requirements.** Browser security
  requires COOP/COEP headers. Document the deployment requirement.
- **Extension distribution.** Web users may want one-off extensions without
  rebuilding the bundle. Either ship a "load from URL" option (security
  implications) or accept the rebuild model.
- **r8brain on wasm.** Should compile fine; verify performance is acceptable.
- **Bundle size.** SameBoy + Mesen + LVGL + r8brain in wasm is several MB.
  Compress with brotli and code-split where possible. Mesen specifically
  may not be worth shipping in the web build for size reasons; gate behind
  a build flag.
