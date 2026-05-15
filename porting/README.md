# RetroPlug Migration Plan

This directory documents the long-term migration from the legacy [old/](../old/)
codebase to the new DPF + LVGL + TypeScript shell at the repo root. The plan is
broken into discrete steps; each step is a self-contained PR-sized milestone.
Progress through the steps in order — earlier steps unblock later ones.

## Why we're doing this

The legacy codebase grew organically over years and accumulated several
load-bearing things that are no longer worth their cost:

- **An entt-based ECS** with a cross-thread `Replicator` mirroring components
  between an authoritative UI registry and a read-only audio registry. Powerful,
  but most of the flexibility was never used and the indirection made the code
  hard to reason about.
- **A custom in-house framework called orb**, with its own renderer, audio
  graph, event bus, and image/buffer types. Maintaining a UI framework
  alongside the actual product was unsustainable.
- **iplug2 + premake5 + a custom premake module format** — two build systems'
  worth of overhead.
- **Lua-based extensibility** for custom views/workflows. Lua's lack of types
  made the extension surface painful to evolve.

The new shell replaces all of these with smaller, off-the-shelf, stronger
primitives:

| Old | New |
| --- | --- |
| iplug2 | [DPF](../deps/dpf/) (DSP+UI plugin formats) |
| orb (renderer + UI framework) | [LVGL](../deps/lv_binding_js/deps/lvgl/) |
| Lua scripting | [txiki.js](../deps/lv_binding_js/deps/txiki/) (QuickJS) running React + TypeScript |
| Custom JSON serialization | [reflectcpp](../deps/rpcpp/) on plain structs |
| Cross-thread mirroring (Replicator) | Direct shared memory + typed SPSC queues + [rpcpp](../deps/rpcpp/) JSON-RPC |
| premake5 + custom modules | Plain CMake |
| entt ECS + hooks | Polymorphic `SystemBase` + composed `RomRole` vector |

## Architecture in one page

Three pieces replace what entt + Replicator did before. **Both are owned by the
DSP thread.** DPF's `getState`/`setState` run DSP-side without a UI, so config
ownership cannot live in the UI — see [step 4](./04-project-state.md).

1. **Runtime side (polymorphic).** DSP owns
   `std::vector<std::unique_ptr<SystemBase>>`. Concrete subclasses
   (`SameBoySystem`, later `MesenSystem`) hold live emulator state. Per-ROM
   behavior (LSDJ, MGB, Arduinoboy, user-added) is a vector of composed
   `std::unique_ptr<RomRole>` members on each system; multiple roles can run on
   the same ROM.

2. **Config side (plain data, reflectcpp).** A parallel `ProjectConfig` tree
   holds plain-struct `SystemConfig`, `RoleConfig`, etc. with no virtuals. This
   is what serializes via DPF state. `SystemBase::snapshotConfig()` rebuilds it
   from runtime state on demand. The UI keeps a *cache* of this tree, fetched
   via rpcpp on mount and kept in sync via DSP→UI events.

3. **Three transports tuned to data shape.**
   - **Direct shared memory** for streaming (per-system framebuffer
     triple-buffer, audio waveform). Bypasses every layer.
   - **rpcpp** (typed JSON-RPC over a queue transport) for cold-path requests:
     load ROM, save project, fetch memory snapshot.
   - **SPSC command/event queues** for hot-path UI↔DSP messaging: button
     presses, MIDI, config changes.

Two extension surfaces, by thread:

- **C++ `RomRole`** — audio-thread-bound work (LSDJ tempo divisors, Arduinoboy
  encoding, kit patching). Built-in only; recompile to add. Small audited set.
- **TypeScript extensions** — UI-thread, polling-based work (custom overlays,
  visualizations, save-data inspectors, alternative tile layouts). Hot-reloadable,
  user-extensible. Subscribe to memory snapshots via rpcpp, render React.

This split is what closes the gap from the old Lua extensibility: typed,
hot-reloadable, and the audio-thread boundary stays clean.

## Migration phases

The 18 steps are grouped into five phases.

### Phase 1 — Foundation

Get one ROM running end-to-end. Architecture seams in place even though most are
empty.

- [01: SameBoy MVP](./01-sameboy-mvp.md) — **DONE**. Bootstrap one ROM, render
  framebuffer, output audio.
- [02: Keyboard input](./02-keyboard-input.md) — **DONE**. UI→DSP command
  queue with `ButtonPress`; per-system pending-button queue.
- [03: ROM picker UI](./03-rom-picker.md) — Replace the hardcoded path with a
  real load flow. Lights up the rpcpp request/response surface.

### Phase 2 — Persistence + multi-instance

Make the work survive across DAW sessions and run more than one ROM at a time.

- [04: Project state](./04-project-state.md) — DPF `getState`/`setState`,
  `ProjectConfig` round-trip via reflectcpp. Promoted earlier than the old
  roadmap had it because users will lose work without this.
- [05: Multi-instance + tile grid](./05-multi-instance.md) — N>1 systems, React
  tile grid with Tab navigation, audio mixer.

### Phase 3 — MIDI and per-ROM behaviors

The features that actually make this a music plugin.

- [06: MIDI input/output routing](./06-midi-routing.md) — DPF MIDI → systems,
  routing modes from the old project.
- [07: MGB passthrough role](./07-mgb-role.md) — First concrete `RomRole`. ROM
  sniffer registers it. Validates the role abstraction.
- [08: LSDJ sync role](./08-lsdj-sync.md) — **DONE**. Simplest LSDJ MIDI sync
  mode (MidiSync only): host PPQ → 0xF8 → LSDJ serial. Offset table + autoplay
  RAM detection deferred to step 09 where Arduinoboy needs them anyway.
- [09: LSDJ Arduinoboy modes](./09-lsdj-arduinoboy.md) — **DONE**. Full
  Arduinoboy slave + MI.OUT master, MidiMap, KeyboardMidi, MidiPassthrough,
  UI mode picker, ROM-build (stock vs aboy) detection. CLI MIDI output drain
  for headless verification. Raw `Keyboard` mode is enum-only.
- [10: LSDJ kit patching](./10-lsdj-kit-patching.md) — Sample/kit upload via
  memory-patch path.

### Phase 4 — Custom views + extensibility

The "highly extensible" goal. Memory snapshot APIs, then a TS extension
framework, then two flagship built-in extensions.

- [11: Memory snapshot API](./11-memory-snapshots.md) — rpcpp method for
  fetching RAM/VRAM/SRAM. Foundation for everything in this phase.
- [12: TS extension framework](./12-ts-extensions.md) — Manifest-based
  registration, lifecycle hooks, hot-reload.
- [13: LSDJ HD player](./13-lsdj-hd.md) — Full-window LSDJ rendering. Built-in
  extension; doubles as the framework's reference implementation.
- [14: Sample matcher UI](./14-sample-matcher.md) — LSDJ-style sample editor.
  Built-in extension.

### Phase 5 — Quality + breadth

Audio fidelity, mid-session savestates, second emulator backend, web target.

- [15: Resampling](./15-resampling.md) — r8brain. Required for LSDJ sync
  accuracy at non-44.1 kHz host rates.
- [16: Savestate slots](./16-savestate-slots.md) — Mid-session save/load
  (distinct from project state which only saves once).
- [17: Mesen NES support](./17-mesen.md) — `MesenSystem`. Validates that
  `SystemBase` was the right interface.
- [18: Web/Emscripten port](./18-web-port.md) — DSP+UI to WASM. C++ core
  unchanged because no shared-memory IPC assumption was baked in.

## Working with this plan

Each step file follows the same structure:

- **Goal** — one-paragraph summary of what lands.
- **Depends on** — earlier steps that must be complete.
- **Architecture** — types/files that change.
- **Tasks** — concrete to-do list, in order.
- **Verification** — how to know it works end-to-end.
- **Risks / open questions** — things that may need to be revisited.

Step ordering is deliberate but not rigid — if a later step's value to you
exceeds an earlier one's prerequisite cost, reorder. Just update the
`Depends on` lines and the README.

## What's NOT in scope

A few legacy features are intentionally not migrated:

- **The standalone application/CLI** at [old/src/app/](../old/src/app/) and
  [old/src/cli/](../old/src/cli/). DPF's JACK target plus the Emscripten web
  build cover the same use cases.
- **Everdrive support** ([old/src/core/EverdriveComponents.h](../old/src/core/EverdriveComponents.h)).
  NES-only, Mesen-bound, very niche. Defer indefinitely.
- **The orb framework itself.** Stripped at the boundary — anywhere old code
  uses `orb::Image`, `orb::Float32Buffer`, etc., it's replaced with plain types.

These can be revisited if there's appetite, but they're not on the path to
parity.
