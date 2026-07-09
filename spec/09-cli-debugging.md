# 09 — CLI debugging & ROM testing

**Status: partly built.** The greenfield **CLI session runner** (a standalone txiki binary that runs a
TS-authored session), **event scripting** (the `Timeline`), **WAV/screenshot** output, AND the **do-first
observe slice** are built and verified. The observe slice (shipped) surfaces the live-core reads
`getApuState` / `readCpu` / `readMemory` / `getCpuRegisters` / `stepInstruction` through the greenfield
backend RPC (`EngineRpcService` → `BackendFacade` → `backend.ts`, resolving the live system via
`Engine::findSystem` — the control-thread/direct-render regime), plus `Timeline.at(ms, fn)` as the
observe/assert hook and a TAP-emitting `cli/sessions/rom-test.ts` example. Proven on a real n8-midi core:
a ch1 note drives pulse1 at ~261 Hz (C4) with pulse2 silent. **Still a plan (next slice):** breakpoints,
trace, labels, and the rest of §5's "next/later" rows. The load-bearing finding of this doc — the whole
debugger is **already compiled into the greenfield binaries and already proven on the legacy CLI**, so the
remaining work is RPC plumbing, not emulator work — held: the do-first slice was pure plumbing.

This doc builds on [02-native-host.md](02-native-host.md) (the `BackendFacade` RPC surface it would
extend) and [06-build-test.md](06-build-test.md) (how the CLI builds and is verified). The stimulus path
it drives is the DSP role kernel of [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).

## 1. The goal — agent-driven ROM development against a real core

The CLI's most valuable role is as a **scriptable test harness for developing a ROM against a real
emulator core**. The motivating case is a hand-written NES ROM (e.g. `n8-midi.nes`, an Everdrive-style
MIDI→APU synth) developed **outside this repo**: an agent authors a TypeScript session that boots the
ROM on a real Mesen NES core, drives it (MIDI notes, button presses, transport), **observes** the
resulting machine state, **asserts** expectations, and gets a machine-readable **pass/fail** — then
iterates on the ROM and re-runs.

The full loop is **stimulus → observe → assert → report**. Today the CLI does the first and last steps
only (stimulus in, artifacts out). This doc is about closing the middle two.

## 2. What the CLI is today

A single standalone executable, `retroplug-greenfield-cli`, that evals one pre-bundled session `.js` on
txiki/QuickJS against the real greenfield `Backend` — **no Node at runtime** (esbuild bundles the
TS session at build time; the binary runs the JS). See the CLI scaffold in
[packages/native-greenfield/cli/main.cpp](../packages/native-greenfield/cli/main.cpp) and
[packages/retroplug-greenfield/cli/](../packages/retroplug-greenfield/cli).

- **Session runtime** — [cli/session.ts](../packages/retroplug-greenfield/cli/session.ts): `bootSession()`
  wires the store graph over `createRealBackend()` (loading the DSP kernel before the `onSystemsChange`
  hook), and `runSession(main)` runs the body and reports the exit code. `hostArgs()` reads the session's
  argv (hung off the `Symbol.for("plugin")` namespace — `tjs.args` is a read-only txiki accessor).
- **Event scripting** — [cli/timeline.ts](../packages/retroplug-greenfield/cli/timeline.ts): a fluent,
  typed `Timeline` (MIDI/`note`/`press`/`tap`/`bpm`/`transport`/`screenshot` at absolute ms) whose
  `build()` is pure (stable ms-sort), and `renderTimeline(session, timeline, { durationMs, warmupMs })`
  advances the render chunk-by-chunk, firing each event at its time, returning the concatenated PCM.
- **Artifacts** — [cli/wav.ts](../packages/retroplug-greenfield/cli/wav.ts) encodes interleaved-stereo
  Float32 PCM (from `audio.renderAudio(ms)`) to a 16-bit WAV; `audio.screenshot(id, path)` writes a PNG.

The current session verb vocabulary (all on the booted `Session`):

| Area | Verbs |
|---|---|
| Load | `project.systems.addSystem(romPath)` / `loadMgb()` |
| Drive | `audio.stageMidiIn(bytes)`, `audio.pressButton(id, button, down)`, `audio.setBpm(v)`, `audio.setTransport(running)` |
| Render | `audio.renderAudio(ms): Float32Array` |
| Observe (today) | `audio.screenshot(id, path)`, `backend.readState/readSram(id)`, `backend.getFrame(id)` |
| Schedule | `Timeline` + `renderTimeline` |

Example sessions live in [cli/sessions/](../packages/retroplug-greenfield/cli/sessions) (`mgb-smoke.ts`,
`render-rom.ts`, `render-song.ts`) and are bundled by the `retroplug-greenfield-example-session` CMake
target via [tools/build-greenfield-session.js](../tools/build-greenfield-session.js).

## 3. The gap: the observe/assert half

For **rendering** artifacts the CLI is complete. For **testing** it is not: a test needs to see *what the
ROM did* and assert on it, in a form an agent can parse. NES APU registers (`$4000-$4013`) are
write-only / open-bus on real hardware, so you **cannot** observe MIDI→sound by reading registers — you
need the emulator's **decoded internal state**. That, plus memory/CPU introspection and structured
pass/fail, is what turns the CLI from an artifact producer into a ROM-test harness.

## 4. Mesen's debugger is already compiled in — and already proven on the legacy CLI

A survey of the vendored Mesen core and the integration seam established the decisive facts:

- **The whole debugger is compiled in.** [deps/mesen/CMakeLists.txt](../deps/mesen/CMakeLists.txt) builds
  the `mesen` static lib by a recursive glob of `Core/*.cpp` (+ `Utilities/`, `Lua/`) with **no
  debugger exclusions**. That pulls in the entire `Core/Debugger/` subtree — `Debugger`, `Breakpoint` /
  `BreakpointManager`, `ExpressionEvaluator`, `MemoryDumper`, `TraceLogger`, `CallstackManager`,
  `Profiler`, `LabelManager`, `EventManager` — plus Mesen's Lua engine.
- **It is linked into the greenfield binaries.** `retroplug-greenfield-backend` links `retroplug-cli-core`
  ([packages/native-greenfield/CMakeLists.txt](../packages/native-greenfield/CMakeLists.txt#L41)), which
  compiles [MesenNesSystem.cpp](../packages/native/src/system/mesen/MesenNesSystem.cpp) +
  [MesenNesDebugSession.cpp](../packages/native/src/system/mesen/MesenNesDebugSession.cpp) and links
  `libmesen`. So the capability is present in `retroplug-greenfield-cli` today.
- **A complete debug target already exists.** `MesenNesSystem` runs headless and single-threaded — it
  never calls `Emulator::Run()/RunFrame`, driving `NesCpu::Exec()` directly and claiming the emulation
  thread id — and lazily owns a `MesenNesDebugSession` (a full `rp::IDebugTarget`,
  [DebugTarget.hpp](../packages/native/src/system/DebugTarget.hpp)) reachable via
  `SystemBase::debugTarget()`. `getApuState()` reads `NesApu::GetState()` off the console (**no debugger
  init needed**); breakpoints/step/trace/profiler lazily call `InitDebugger()`.
- **The exact harness already exists and passes on the *legacy* CLI.** The legacy
  [HarnessRpcService](../packages/native/cli/HarnessRpcService.cpp) +
  [HarnessRpcRegistration.hpp](../packages/native/cli/HarnessRpcRegistration.hpp) bridge the `IDebugTarget`
  to ~15 JSON-RPC methods (getApuState, readMemory, setBreakpoints, runUntilBreak, step*, setTrace/readTrace,
  disassemble, getCallStack, beginProfile/readProfile, loadLabels, CPU registers, readCpuByte,
  stepInstruction). `test/ts/nes/{apu,debug,cpu}.test.ts` already drive a **real** `n8-midi.nes` via MIDI
  and assert on APU channels, memory, breakpoints, and traces — including a **read-watchpoint on the MIDI
  FIFO at `$40F1`** that fires.
- **The greenfield gap is narrow and mechanical.** `packages/native-greenfield` and
  `packages/retroplug-greenfield` have **zero** references to `debugTarget`/`getApuState`/`setBreakpoints`.
  The C++ is compiled, linked, and reachable via `Engine → Project → SystemBase::debugTarget()`; it is
  simply not surfaced through `EngineRpcService` / `BackendFacade`.

**Decision: port, do not hand-roll.** Writing bespoke `getApuState`/`readMemory` RPCs would re-implement a
strict subset of a working, tested surface. The right move is to port the proven `HarnessRpcService`
methods onto the greenfield RPC seam and wrap them in a TS session/`observe` API.

## 5. Capability inventory (prioritized for a MIDI→APU ROM)

| Priority | Capability | Mesen provides | Reachable in greenfield build | Cost | CLI/session mapping |
|---|---|---|---|---|---|
| **Do first** | **Decoded per-channel APU state** — Square1/2, Triangle, Noise, DMC: period, frequency Hz, duty, envelope volume, length | `NesApu::GetState()` → `ApuState` (`Core/NES/…/NesApu.cpp`); freq computed live | compiled-in **and implemented**: `MesenNesDebugSession::getApuState()` → `rp::ApuState`; **no `InitDebugger`** | low (1 RPC) | `getApuState(id)` → `Timeline.at(t, s => expect(apu.square1.frequencyHz ≈ 262 && apu.square1.envelopeVolume > 0))`. **The centerpiece** — the only way to observe MIDI→sound. |
| **Do first** | **Side-effect-free CPU-bus peek + flat region reads** (incl. the `$40F0/$40F1` Everdrive MIDI FIFO) | `NesMemoryManager::DebugRead` (non-destructive `PeekRam`); `Emulator::GetMemory(MemoryType)` | compiled-in **and implemented**: `SystemBase::readCpuByte()` / `getMemory()` | low (2 RPCs) | `readCpu(id, addr)` for mapped I/O / FIFO status; `readMemory(id, region)` for RAM/OAM/VRAM/PRG. |
| **Do first** | CPU registers + single-step (`A/X/Y/SP/PS/PC`, `stepInstruction`, `runUntilPc`) | `NesCpu::GetState/SetState`, `Debugger::Step` | compiled-in **and base-implemented** on `SystemBase` | low | `getCpuRegisters/setRegister/stepInto`. |
| Next | **Breakpoints + `runUntilBreak`** with expression conditions (exec/read/write watchpoints; `[$40F0]!=0`, scanline/cycle gates) | `Breakpoint::Create` → `Debugger::SetBreakpoints`; `ExpressionEvaluator` | compiled-in, implemented **and tested** (FIFO read-watch fires) | low (port RPCs) | `setBreakpoints(id, […])` + `runUntilBreak(id, maxCycles)` → `.at(break, …)`. Lazily inits the heavyweight debugger. |
| Next | Execution trace (per-instruction disasm + register trace) | `Debugger::GetExecutionTrace`, `TraceLogger` | compiled-in and implemented | low | `setTrace/readTrace` — post-mortem assertions on the instruction stream. |
| Next | **cc65 symbol labels** (name-resolved disasm/callstack/profile) | `LabelManager` fed from cc65 `.dbg` via `rp::parseCc65Dbg` | compiled-in and implemented **for cc65 `.dbg`** | low (cc65); **`.mlb` needs a new parser** (Mesen's C# parser is not vendored) | `loadLabels(id, dbgPath)`. |
| Later | Profiler + disassembler + call stack | `Profiler`, `Disassembler`, `CallstackManager` | compiled-in and implemented | low (port RPCs) | `beginProfile/readProfile`, `disassemble`, `getCallStack`. |
| Later | **EventManager** — batch-capture every `$2000-$401F` register write per frame with PC/scanline/cycle | `NesEventManager` (`TakeEventSnapshot`/`GetEvents`) | compiled + linked but **no `rp::` wrapper yet** | medium (new C++ wrapper + struct) | `drainEvents(id)` per frame → assert the *sequence/timing* of APU writes. Frame-scoped (drain ~60/s). |
| Later | PPU state + viewers (scanline/cycle/scroll; tilemap/sprite/palette) | `NesDebugger::GetPpuState`, `NesPpuTools` | compiled + linked but **no `rp::` wrapper**; framebuffer already published for screenshots | medium (structs + caller-alloc buffers) | `getPpuState/getSpriteList`. Marginal for an audio ROM. |
| Later | Memory **write**/poke (arrange state before assertions) | `MemoryDumper::SetMemoryValue` / `NesMemoryManager::DebugWrite` | compiled-in but **unimplemented** anywhere (only `setRegister` exists) | low (1 new method) | `writeCpu(id, addr, val)`. |
| **Avoid** | Mesen Lua scripting (`ScriptManager`/`LuaApi` + Lua 5.4) | `emu.read/write`, `addMemoryCallback`, `addEventCallback` | **compiled + linked but dead** (`LoadScript` never called) | medium–high, **not worth it** | None. Its `getState` exposes *raw* registers (poorer than our decoded APU state); a second interpreter beside the TS/QuickJS session is net-negative. Extend `IDebugTarget` instead. |

## 6. Proposed greenfield integration

Three layers; only the first touches C++, and it is plumbing over already-compiled symbols.

1. **Native — port the RPCs.** Add the debug methods to
   [EngineRpcService](../packages/native-greenfield/src/EngineRpcService.cpp) →
   [BackendFacade](../packages/native-greenfield/src/BackendFacade.hpp) →
   [backend.ts](../packages/retroplug-greenfield/src/backend.ts), each forwarding to
   `project_.findSystem(id)->debugTarget()->…` (or `SystemBase::readCpuByte/getMemory/getCpuRegisters/…`),
   mirroring `HarnessRpcService`. Start with the **Do-first** rows (getApuState, readCpu, readMemory, CPU
   registers/step). Reuse the existing reflect-cpp result structs (`rp::ApuState` et al.).
2. **TS — the ergonomics.** A typed `observe` surface on the `Session` (`getApuState(id)`, `readCpu(id,
   addr)`, `readMemory(id, region)`, …); a `Timeline.at(ms, fn)` scheduled-callback event so assertions run
   *at a scheduled time* mid-render; reuse [testing/harness.ts](../packages/retroplug-greenfield/testing/harness.ts)
   `test()`/`expect()` so a ROM test **emits TAP** the agent parses; plus pure audio-analysis helpers
   (RMS / dominant-frequency) to cross-check the *sound* against the register state.
3. **External authoring.** Because the ROM (and its tests) live outside this repo, the
   `session`/`timeline`/`observe`/`expect` API must be an **importable, stable entry** the bundler resolves,
   so out-of-repo scripts can `import { runTest, Timeline, expect } from "…"`. This is what makes it a real
   "agents write scripts against a real NES" system rather than in-repo examples.

Sketch of the target authoring experience:

```ts
runTest("n8-midi: ch1 note → Square1 at pitch; ch2 → Square2 (not Square1)", (s) => {
  const id = s.project.systems.addSystem(rom);
  const tl = new Timeline()
    .note(1000, 60, { channel: 1, durationMs: 500 })
    .at(1200, () => {
      const apu = s.getApuState(id);
      expect(apu.square1.envelopeVolume > 0).toBe(true);
      expectNear(apu.square1.frequencyHz, 262, 3);
      expect(apu.square2.envelopeVolume).toBe(0);   // the ch2 regression guard
    })
    .at(1300, () => expect(s.readCpu(id, 0x40f1) !== 0).toBe(false)); // FIFO drained
  renderTimeline(s, tl, { durationMs: 2000, warmupMs: 1000 });
});
```

### First slice (recommended)

`getApuState` + `readCpu`/`readMemory` + CPU-registers/step ported to the greenfield RPC, a TS
`observe` API + `Timeline.at()`, a TAP wrapper, and one example `n8-midi` APU test. Breakpoints /
`runUntilBreak` / trace / cc65 labels follow. This is the smallest change that makes the CLI an
agent-runnable NES test harness — and it re-catches the known `ch2→Square1` ROM bug on day one.

## 7. Caveats & gotchas

- **Patched vendor tree.** `deps/mesen` is **not** pristine upstream — the single-threaded break model
  (`Debugger` `_lastBreak`/`GetLastBreakEvent`/`ResumeFromBreak`; `SleepUntilResume` *returns* instead of
  blocking) and the `Breakpoint::Create` factory were added for this headless harness. A Mesen bump **must
  re-apply** them or breakpoints/stepping break.
- **Single-threaded / emulation-thread-only.** Every debug op claims the emulation thread via
  `Emulator::SetEmulationThreadId(this_thread)` so Mesen's `IsEmulationThread`/`DebugBreakHelper` no-op.
  This fits the CLI's single-threaded direct-render model but is **unsafe from the audio thread**.
- **`audioRunning_` live-read bug class.** This repo has a known pattern where ops that read a live core were
  `audioRunning_`-guarded and went silently dead in the running plugin. Any greenfield debug RPC that reads
  live state must route to the correct single-threaded path (not be gated behind `audioRunning_`) — **verify
  with an actual exit-zero run, not by inspection.** (See [01-architecture.md](01-architecture.md) on the
  read door and threading invariants.)
- **NES-only.** Only `MesenNesSystem` has a `debugTarget()`; `MesenGbaSystem` has none and SameBoy returns
  nullptr (`getApuState` throws on GB). The harness is inherently NES-scoped today.
- **Teardown ordering is delicate.** The debugger must be stopped while the `Emulator`/`DebugHud` is still
  alive, or `ScriptManager`'s destructor → `DebugHud::ClearScreen` segfaults. Preserve the
  `MesenNesDebugSession` / `MesenNesSystem` teardown order when porting.
- **`InitDebugger` is heavyweight** (builds the full per-CPU debugger graph incl. the dead Lua
  `ScriptManager`). Keep the lazy-on-first-breakpoint/step/trace init so `getApuState` + `readCpu` +
  `readMemory` (which don't need it) stay cheap and non-debug renders aren't slowed.
- **Lua is compiled in, not absent** — but dead-linked (`LoadScript` never called). It is linker weight, not
  a foundation; do not build the harness on it.
- **APU `enabled` semantics.** `enabled` is only the `$4015` switch (often set once at init), **not** "a
  note is sounding." Gate APU assertions on `period > 0 && envelopeVolume > 0` (square/noise) or
  `period/outputVolume`, else false positives.
- **Source-present-not-wrapped costs (honest).** `EventManager` and the PPU viewers are compiled+linked but
  have **zero** `rp::` wrappers — real glue (structs + caller-allocated buffers). Memory-**write** and a
  standalone `EvaluateExpression` are compiled but unimplemented (~small adds). Mesen native `.mlb` label
  files need a **new parser** (only cc65 `.dbg` works today).
- **Known ROM bug, not a harness bug.** The committed `resources/roms/n8-midi.nes` drives MIDI ch2 to
  Square1 (Square2 silent) — a stale cc65 binary already *caught* by `getApuState`. Rebuild the ROM; don't
  chase it as an integration defect.
