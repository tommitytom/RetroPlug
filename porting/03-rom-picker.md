# Step 03 — ROM picker UI

**Status:** Done.

## Goal

Replace the hardcoded `RETROPLUG_ROM_PATH` scaffolding with a real load flow:
the user picks a ROM in the React UI, the bytes are shipped to the DSP, and the
DSP swaps the running system in place. Lights up the rpcpp request/response
surface for the first time.

## Depends on

- [Step 02](./02-keyboard-input.md) for `CommandQueue` (already in place).

## Architecture introduced

- **rpcpp service: `ProjectService`** — a typed JSON-RPC service registered in
  `PluginJsBridge`. Methods:
  - `loadRom(systemId: SystemId, path: string) -> { ok: bool, error?: string }`
    — DSP-side `slurpFile` of the path, then swap the system.
  - `loadRomBytes(systemId, bytes: ArrayBuffer) -> ...` — same but with bytes
    (for drag-drop). Large payloads round-trip via base64 since rpcpp is JSON;
    revisit if this becomes a perf problem.
  - `getProjectConfig() -> ProjectConfig` — first read of the DSP-owned config
    on UI mount.
- **`Command::LoadRom`** — alternative path: replaces a system in place via the
  command queue. Carries a `std::shared_ptr<std::vector<uint8_t>>` so the DSP
  doesn't allocate. Choose between rpcpp (cold path, small in number, easy to
  debug) and SPSC (hot path, low latency); ROM load is cold-path → rpcpp.
- **TS UI: file picker.** Three options in priority order:
  1. **System file dialog** via tjs/libuv (txiki.js exposes `tjs.dialog`?
     check). Cleanest UX.
  2. **LVGL file explorer widget** (`lv_file_explorer`). All in-window. Less
     polished but no platform dependency.
  3. **Drag-and-drop** onto the plugin window via DPF's window event API.
- **`ConfigChanged` event** (DSP→UI) — fires when DSP-side `ProjectConfig`
  mutates so the UI cache stays in sync. Implementation: an SPSC `EventQueue`
  with a `ConfigChanged(ProjectConfig)` variant. UI polls during `uiIdle`.

## Tasks

1. Implement `getProjectConfig` rpcpp method; have UI call it on mount and
   cache the result in TS state.
2. Implement `loadRom(systemId, path)` rpcpp method. Slurp file, replace the
   `SameBoySystem` runtime in `Project` (rebuild from new config). Apply
   activate semantics so callbacks reattach.
3. UI: add a "Load ROM" button that triggers a file dialog; on selection, call
   `plugin.loadRom`.
4. Wire `ConfigChanged` event so the title bar / system list reflects the new
   ROM path.
5. Drop the `RETROPLUG_ROM_PATH` bootstrap scaffold from `LVGLPluginDSP`'s
   constructor — replace with "no system; load one to begin" empty state.
6. Drop the `HelloService` template scaffolding in `PluginJsBridge`.

## Verification

- New plugin instance starts with no system. UI shows a placeholder with a
  "Load ROM" button. Audio is silent; framebuffer placeholder.
- Picking an LSDJ ROM: system loads, framebuffer appears, audio plays. Reload a
  different ROM: previous system tears down cleanly, new one starts.
- DPF state save/load via the host (where supported in step 04) — config
  survives.

## Risks / open questions

- **rpcpp + binary blobs.** JSON-RPC is the wrong shape for multi-MB ROM data.
  Either base64 it (gross but works) or add a side-channel for binary payloads.
  Decide here; revisit at step 10 (kit patching) which has the same shape.
- **Hot ROM swap.** Tearing down `SameBoySystem` mid-block is risky. Either
  block the audio thread with a flag (atomic "wants reload") and apply at the
  top of the next `run()`, or accept a single audio-block glitch.
- **File-picker portability.** macOS, Windows, Linux all want different system
  dialogs. Easiest: pick LVGL's built-in file explorer for step 03 and revisit
  with a native fallback at step 18 (web port has its own picker mechanism).
