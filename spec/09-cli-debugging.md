# 09 — CLI debugging & ROM testing

**Status: built.** The **CLI session runner** (a standalone txiki binary that runs a JavaScript
session), **event scripting** (the `Timeline`), **WAV/screenshot** output, the compiled-in `render`
subcommand, AND the **full stimulus → observe → assert → report** loop are all shipped. Mesen's
debugger is surfaced through a dedicated **`debug` RPC facet**
([DebugRpcService](../packages/native/src/host/rpc/DebugRpcService.cpp)) — decoded APU/PPU state,
CPU-bus peek + poke (`readCpu`/`writeCpu`), region reads, CPU registers + step + PC-run, breakpoints
(`setBreakpoints`/`runUntilBreak`, with a real `$40F1` FIFO read-watchpoint that fires), execution
trace + step-into/over/out, per-frame register-event capture (`drainEvents`), profiler + disassembler
+ call stack, and cc65 `.dbg` labels — resolving the live system via `Engine::findSystem` (the
control-thread / direct-render regime). `Timeline.at(ms, fn)` is the observe/assert hook, and a
TAP-emitting `cli/sessions/rom-test.ts` is the agent-facing example. Proven on a real n8-midi core: a
ch1 note drives pulse1 at ~261 Hz (C4) with pulse2 silent.

**The one remaining item** is Mesen native `.mlb` label files (need a new parser — Mesen's C# one
isn't vendored; cc65 `.dbg` labels ship). See [07-remaining-work.md](07-remaining-work.md).

This doc builds on [02-native-host.md](02-native-host.md) (the Backend RPC surface, of which `debug`
is one facet) and [06-build-test.md](06-build-test.md) (how the CLI builds and is verified). The
stimulus path it drives is the DSP role kernel of [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).

## 1. The goal — agent-driven ROM development against a real core

The CLI's most valuable role is as a **scriptable test harness for developing a ROM against a real
emulator core**. The motivating case is a hand-written NES ROM (e.g. `bliptoaster.nes`, an Everdrive-style
MIDI→APU synth) developed **outside this repo**: an agent authors a TypeScript session that boots the
ROM on a real Mesen NES core, drives it (MIDI notes, button presses, transport), **observes** the
resulting machine state, **asserts** expectations, and gets a machine-readable **pass/fail** — then
iterates on the ROM and re-runs.

The full loop is **stimulus → observe → assert → report**. Today the CLI does the first and last steps
only (stimulus in, artifacts out). This doc is about closing the middle two.

## 2. What the CLI is today

A single standalone executable, `retroplug-cli`, that evals one pre-bundled session `.js` on
txiki/QuickJS against the real `Backend` — **no Node at runtime** (esbuild bundles the
TS session at build time; the binary runs the JS). See the CLI scaffold in
[packages/native/cli/main.cpp](../packages/native/cli/main.cpp) and
[packages/retroplug/cli/](../packages/retroplug/cli).

- **Session runtime** — [cli/session.ts](../packages/retroplug/cli/session.ts): `bootSession()`
  wires the store graph over `createRealBackend()` (loading the DSP kernel before the `onSystemsChange`
  hook), and `runSession(main)` runs the body and reports the exit code. `hostArgs()` reads the session's
  argv (hung off the `Symbol.for("plugin")` namespace — `tjs.args` is a read-only txiki accessor).
- **Event scripting** — [cli/timeline.ts](../packages/retroplug/cli/timeline.ts): a fluent,
  typed `Timeline` (MIDI/`note`/`press`/`tap`/`bpm`/`transport`/`screenshot` at absolute ms) whose
  `build()` is pure (stable ms-sort), and `renderTimeline(session, timeline, { durationMs, warmupMs })`
  advances the render chunk-by-chunk, firing each event at its time, returning the concatenated PCM.
- **Artifacts** — [cli/wav.ts](../packages/retroplug/cli/wav.ts) encodes interleaved-stereo
  Float32 PCM (from `audio.renderAudio(ms)`) to a 16-bit WAV; `audio.screenshot(id, path)` writes a PNG.

The current session verb vocabulary (all on the booted `Session`):

| Area | Verbs |
|---|---|
| Load | `project.systems.addSystem(romPath)` / `loadMgb()` |
| Drive | `audio.stageMidiIn(bytes)`, `audio.pressButton(id, button, down)`, `audio.setBpm(v)`, `audio.setTransport(running)` |
| Render | `audio.renderAudio(ms): Float32Array` |
| Observe — media/state | `audio.screenshot(id, path)`, `backend.readState/readSram(id)`, `backend.getFrame(id)` |
| Observe — debug (NES) | `backend.getApuState(id)`, `readCpu(id, addr)`, `readMemory(id, region)`, `getCpuRegisters(id)`, `setCpuRegister`, `stepInstruction`/`runUntilPc`, `setBreakpoints`/`runUntilBreak`, `setTrace`/`readTrace`, `stepInto`/`stepOver`/`stepOut`, `loadLabels`, `beginProfile`/`readProfile`, `disassemble`, `getCallStack` |
| Schedule / assert | `Timeline` (+ `.at(ms, fn)` — the observe/assert hook) + `renderTimeline` |

Example sessions live in [cli/sessions/](../packages/retroplug/cli/sessions) (`mgb-smoke.ts`,
`render-rom.ts`, `render-song.ts`, and `rom-test.ts` — the TAP-emitting debug/observe example) and are
bundled by the `retroplug-example-session` CMake target via
[tools/build-session.js](../tools/build-session.js). These are **loose `.js`** run by path
(`retroplug-cli build/cli/<name>.js …`) — a dev convenience that needs the build tree.

### The `render` subcommand (compiled-in)

For end users, `retroplug-cli` also carries **named subcommands whose session is compiled into the
binary** (tjsc bytecode, exactly like the plugin's control-plane/UI bundles) — no loose `.js`, no Node,
just the executable. The first is `render`, which takes a ROM + its battery `.sav` (or a savestate)
straight to WAV:

```
retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--max-duration t]
                          [--sample-rate hz] [--split mix|channels|pins] [--bpm n] [--transport]
                          [--no-start] [--song name | --song-index n] [--list-songs]
```

A missing `--sav` auto-pairs the sibling `<rom>.sav` (TS `siblingSavPath`); by default it presses
Start on boot so a saved song (LSDj) actually plays (`--no-start` for raw boot audio). `--duration`
accepts a time value (`3s`, `500ms`, `2m`; a bare number is ms). `--split` writes per-stream WAVs (no
combined file): `mix` = one WAV (GB stereo, NES mono — the NES mix's lanes are identical); `channels` =
GB 4 stereo stems or NES 5 mono core channels (square1/2, triangle, noise, dmc); `pins` = NES 3 mono
analog pins (pulse, tnd, expansion). **LSDj song selection** (GB only): a `.sav` holds up to 32 named
projects but LSDj only plays its *working song* on boot, so `--song NAME` / `--song-index N` decode the
sav, promote the chosen project to the working song (pure-TS `decodeSav`/`encodeSav` →
`adopt({ sramBytes })`), and boot that; `--list-songs` prints the sav's song names. **LSDj length
auto-detect**: when a valid LSDj sav is loaded (identified by the `'jk'` magic) and no `--duration` is
pinned, `render` renders to the song's **HFF stop** — the APU master-enable `$FF26` (NR52) going
high→low, polled each ~100 ms chunk via `backend.readCpu` (lsdpack's technique) — reports the length
(`length: <ms> ms … hff:true`) and trims the WAV to it; `--max-duration` caps the no-HFF fallback.
`--duration` forces a fixed length. Rendered audio is streamed straight to the WAV files as it renders
(bounded memory), via `createWavWriter` over the backend's `appendFile`/`writeFileAt`. The tool is
[cli/sessions/render.ts](../packages/retroplug/cli/sessions/render.ts) (`renderTool`, a `CliTool`; parser
split into the pure, unit-tested [cli/renderArgs.ts](../packages/retroplug/cli/renderArgs.ts)). Commands are
owned by the TS root dispatcher [cli/cli.ts](../packages/retroplug/cli/cli.ts) — registered in
[cli/tools.ts](../packages/retroplug/cli/tools.ts), bundled by the `retroplug-cli-bundle` CMake target and
linked into `retroplug-cli` (`rp_cli_bundle`); [cli/main.cpp](../packages/native/cli/main.cpp) evals that
dispatcher for a command (it prints the top-level help + routes `<cmd> --help`), or a `<session.js>` file by
path as the dev fallback. Smoke: `pnpm cli:render-smoke`.

## 3. The gap: the observe/assert half

This *was* the gap; §5–6 close it (the observe/assert half is now built). A test needs to see *what the
ROM did* and assert on it, in a form an agent can parse. NES APU registers (`$4000-$4013`) are
write-only / open-bus on real hardware, so you **cannot** observe MIDI→sound by reading registers — you
need the emulator's **decoded internal state**. That, plus memory/CPU introspection and structured
pass/fail, is what turns the CLI from an artifact producer into a ROM-test harness — and is exactly what
the shipped debug RPCs (§5) + `Timeline.at` + the TAP harness now provide.

## 4. Mesen's debugger is compiled in

The whole thing rides Mesen's own debugger, which is compiled into the native binaries:

- **The whole debugger is compiled in.** [deps/mesen/CMakeLists.txt](../deps/mesen/CMakeLists.txt) builds
  the `mesen` static lib by a recursive glob of `Core/*.cpp` (+ `Utilities/`) with **no
  debugger exclusions**. That pulls in the entire `Core/Debugger/` subtree — `Debugger`, `Breakpoint` /
  `BreakpointManager`, `ExpressionEvaluator`, `MemoryDumper`, `TraceLogger`, `CallstackManager`,
  `Profiler`, `LabelManager`, `EventManager`.
- **It is linked into the native binaries.** `retroplug-backend` links `retroplug-core`
  ([packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt#L23)), which
  compiles [MesenNesSystem.cpp](../packages/native/src/system/mesen/MesenNesSystem.cpp) +
  [MesenNesDebugSession.cpp](../packages/native/src/system/mesen/MesenNesDebugSession.cpp) and links
  `libmesen`. So the capability is present in `retroplug-cli` today.
- **A complete debug target already exists.** `MesenNesSystem` runs headless and single-threaded — it
  never calls `Emulator::Run()/RunFrame`, driving `NesCpu::Exec()` directly and claiming the emulation
  thread id — and lazily owns a `MesenNesDebugSession` (a full `rp::IDebugTarget`,
  [DebugTarget.hpp](../packages/native/src/system/DebugTarget.hpp)) reachable via
  `SystemBase::debugTarget()`. `getApuState()` reads `NesApu::GetState()` off the console (**no debugger
  init needed**); breakpoints/step/trace/profiler lazily call `InitDebugger()`.
- **The surfacing is done as the `debug` facet.** The C++ was compiled, linked, and reachable via
  `Engine → Project → SystemBase::debugTarget()`; it is now exposed through
  [DebugRpcService](../packages/native/src/host/rpc/DebugRpcService.cpp), which resolves the live system
  via `Engine::findSystem(id)` and forwards to `SystemBase::…` / `debugTarget()->…`. The facet was
  ported from the earlier (now-removed) harness surface rather than hand-rolled, so it is a strict
  wrapper over a proven `IDebugTarget`, verified by `test-native/cli-*.test.ts` against a real
  `bliptoaster.nes` — including a **read-watchpoint on the MIDI FIFO at `$40F1`** that fires.

## 5. Capability inventory (prioritized for a MIDI→APU ROM)

The whole inventory is **built** — each is a method on the `debug` facet (`DebugRpcService`),
resolving the live system via `Engine::findSystem` and forwarding to `SystemBase::…` / `debugTarget()->…`,
proven on a real `bliptoaster.nes` in `test-native/cli-*.test.ts`. Only Mesen native `.mlb` labels remain.

| Status | Capability | Mesen provides | RPC(s) + session use |
|---|---|---|---|
| **Built** | **Decoded per-channel APU state** — pulse1/2, triangle, noise, dmc: period, frequency Hz, duty, envelope volume, length | `NesApu::GetState()` → `rp::ApuState` (no `InitDebugger`) | `getApuState(id): ApuState` → `.at(t, s => expect(s.backend.getApuState(id).pulse1.frequency > 250 && …pulse1.envelopeVolume > 0).toBeTruthy())`. **The centerpiece** — the only way to observe MIDI→sound. |
| **Built** | **CPU-bus peek + region reads** (incl. the `$40F1` Everdrive MIDI FIFO) | `NesMemoryManager::DebugRead` (`PeekRam`); `GetMemory(MemoryType)` | `readCpu(id, addr): number\|null` (mapped I/O / FIFO); `readMemory(id, region): Uint8Array` (`MemoryRegion.Ram/OAM/…`). |
| **Built** | CPU registers + step + PC-run + register-write | `NesCpu::GetState/SetState`, `Debugger::Step` | `getCpuRegisters(id): CpuRegister[]`, `stepInstruction(id)`, `setCpuRegister(id, name, value)`, `runUntilPc(id, target, maxCycles)`. |
| **Built** | **Breakpoints + `runUntilBreak`** — exec/read/write watchpoints with expression conditions (`[$40F0]!=0`, `Y==0`, scanline/cycle gates) | `Breakpoint::Create` → `Debugger::SetBreakpoints`; `ExpressionEvaluator` | `setBreakpoints(id, Breakpoint[]): bool` + `runUntilBreak(id, maxCycles): BreakInfo`. The `$40F1` read-watch fires. Lazily inits the heavy debugger. |
| **Built** | Execution trace + step trio | `Debugger::GetExecutionTrace`, `TraceLogger` | `setTrace(id, on)`, `readTrace(id, count): TraceLine[]` (`{pc, text}`), `stepInto/stepOver/stepOut(id): BreakInfo`. |
| **Built** | **cc65 symbol labels** | `LabelManager` ← cc65 `.dbg` (`rp::parseCc65Dbg`) | `loadLabels(id, path): bool` (name-resolves disasm/profile/callstack). **`.mlb` still needs a new parser** — Mesen's C# one isn't vendored. |
| **Built** | Profiler + disassembler + call stack | `Profiler`, `Disassembler`, `CallstackManager` | `beginProfile/readProfile(id)`, `disassemble(id, addr, count)`, `getCallStack(id)`. |
| **Built** | **EventManager** — batch-capture every `$2000-$401F` register write per frame with PC/scanline/cycle | `NesEventManager` (`TakeEventSnapshot`/`GetEvents`) | `drainEvents(id)` per frame → assert the *sequence/timing* of APU writes. Frame-scoped (drain ~60/s). |
| **Built** | PPU state (scanline/cycle/scroll) | `NesDebugger::GetPpuState` | `getPpuState(id)`. Marginal for an audio ROM (the framebuffer is already published for screenshots); the tilemap/sprite/palette **viewers** are not wrapped. |
| **Built** | Memory **write**/poke (arrange state before assertions) | `MemoryDumper::SetMemoryValue` / `NesMemoryManager::DebugWrite` | `writeCpu(id, addr, val)`. |

## 6. Integration — built + remaining

The plumbing tier is integrated end to end; only the new-wrapper tier + external-authoring packaging remain.

**Built.** Every capability is the same seam:
- **Native** — a decl in [DebugRpcService.hpp](../packages/native/src/host/rpc/DebugRpcService.hpp), an
  impl in [DebugRpcService.cpp](../packages/native/src/host/rpc/DebugRpcService.cpp) (`SystemBase* sys =
  engine_.findSystem(id)`, guard null, then `sys->…` or `sys->debugTarget()->…`), and an `addMethod` line
  in the `debug` facet of
  [BackendRpcRegistration.hpp](../packages/native/src/host/rpc/BackendRpcRegistration.hpp).
- **TS** — the method on the `Backend` interface + a verbatim-field mirror in
  [backend.ts](../packages/retroplug/src/backend.ts) (`ApuState`, `CpuRegister`, `TraceLine`,
  `BreakInfo`, `ProfiledFunction`, `DisasmLine`, `CallFrame`, `Breakpoint`, + the `MemoryRegion` const), a
  `realBackend.ts` impl (a bare cast / `bytesOrNull`), and a deterministic `MockBackend` stub.
- **Ergonomics** — `Timeline.at(ms, fn)` runs `fn(session)` after the render advances to `ms`, so a session
  reads state + asserts at a scheduled time; a ROM test reuses
  [testing/harness.ts](../packages/retroplug/testing/harness.ts) `test()`/`expect()` to **emit TAP**.
- **Proof** — one `test-native/cli-*.test.ts` per capability (breakpoint PCs, the `$40F1` watch), plus the
  agent-facing `cli/sessions/rom-test.ts`.

**Remaining.**
1. **Mesen native `.mlb` labels** need a new parser (cc65 `.dbg` ships); the tilemap/sprite/palette PPU
   viewers are also unwrapped. Both are CLI-only niceties.
2. **External authoring.** The ROM (and its tests) live outside this repo, so the
   `session`/`timeline`/`observe`/`expect` API should become an **importable, stable entry** the bundler
   resolves, so out-of-repo scripts can `import { Timeline, expect } from "…"`. Today a test lives in-repo
   (a `cli/sessions/*.ts` or `test-native/*.test.ts`).
3. **Audio-active debug reads.** The shipped reads use `Engine::findSystem` — valid on the control thread
   while the audio thread isn't started (the CLI's direct render). Reading a live core *while the plugin
   plays* would need an invoker read-path; not needed for the CLI harness.

Authoring shape (from `cli/sessions/rom-test.ts`) — import `test`/`expect` from the harness (NOT
`runSession`; the harness owns TAP + `tjs.exit`), and read state through `s.backend`:

```ts
test("n8-midi: ch1 note → pulse1 at pitch; pulse2 silent", () => {
  const s = bootSession();
  const id = s.project.systems.addSystem(rom)!;
  let apu: ApuState | null = null;
  const tl = new Timeline()
    .note(200, 60, { channel: 1, durationMs: 400 })
    .at(400, (sess) => (apu = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1000 });
  expect(apu!.pulse1.period > 0 && apu!.pulse1.envelopeVolume > 0).toBeTruthy();
  expect(apu!.pulse1.frequency > 250 && apu!.pulse1.frequency < 275).toBeTruthy(); // ~C4
  expect(apu!.pulse2.envelopeVolume === 0).toBeTruthy(); // ch1 only → pulse2 silent
});
```

(Breakpoints work the same way: `s.backend.setBreakpoints(id, [{ …read-watch on $40F1… }])` then
`runUntilBreak(id, max)` → assert the returned `BreakInfo` stopped there.)

## 7. Caveats & gotchas

- **Patched vendor tree.** `deps/mesen` is **not** pristine upstream — the single-threaded break model
  (`Debugger` `_lastBreak`/`GetLastBreakEvent`/`ResumeFromBreak`; `SleepUntilResume` *returns* instead of
  blocking) and the `Breakpoint::Create` factory were added for this headless harness. A Mesen bump **must
  re-apply** them or breakpoints/stepping break.
- **Single-threaded / emulation-thread-only.** Every debug op claims the emulation thread via
  `Emulator::SetEmulationThreadId(this_thread)` so Mesen's `IsEmulationThread`/`DebugBreakHelper` no-op.
  This fits the CLI's single-threaded direct-render model but is **unsafe from the audio thread**.
- **`audioRunning_` live-read bug class.** This repo has a known pattern where ops that read a live core were
  `audioRunning_`-guarded and went silently dead in the running plugin. Any debug RPC that reads
  live state must route to the correct single-threaded path (not be gated behind `audioRunning_`) — **verify
  with an actual exit-zero run, not by inspection.** (See [01-architecture.md](01-architecture.md) on the
  read door and threading invariants.)
- **NES-only.** Only `MesenNesSystem` has a `debugTarget()`; `MesenGbaSystem` has none and SameBoy returns
  nullptr — so on GB/GBA the reads degrade gracefully (`getApuState` → an empty `ApuState`, the
  CPU/breakpoint reads → empty/false), never throwing. The harness is inherently NES-scoped today.
- **Teardown ordering is delicate.** The debugger must be stopped while the `Emulator`/`DebugHud` is still
  alive, or `ScriptManager`'s destructor → `DebugHud::ClearScreen` segfaults. Preserve the
  `MesenNesDebugSession` / `MesenNesSystem` teardown order when porting.
- **`InitDebugger` is heavyweight** (builds the full per-CPU debugger graph). It is lazy-on-first-
  breakpoint/step/trace/profile, so `getApuState` + `readCpu` + `readMemory` (which don't need it) stay cheap
  and non-debug renders aren't slowed — keep that laziness.
- **APU `enabled` semantics.** `enabled` is only the `$4015` switch (often set once at init), **not** "a
  note is sounding." Gate APU assertions on `period > 0 && envelopeVolume > 0` (square/noise) or
  `period/outputVolume`, else false positives.
- **Still unwrapped.** The PPU tilemap/sprite/palette **viewers** are compiled+linked but not wrapped
  (`getPpuState` scalar state is). cc65 `.dbg` labels are shipped; Mesen native `.mlb` label files still
  need a **new parser** (Mesen's C# one isn't vendored). A standalone `EvaluateExpression` is compiled
  but not surfaced (breakpoint expression conditions do go through the evaluator).
- **Known ROM bug, not a harness bug.** The committed `resources/roms/bliptoaster.nes` drives MIDI ch2 to
  Square1 (Square2 silent) — a stale cc65 binary already *caught* by `getApuState`. Rebuild the ROM; don't
  chase it as an integration defect.
