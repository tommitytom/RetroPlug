# 19 — Mesen CPU introspection (and when you'd actually need the debugger)

**Status:** done. The CPU-state interface
([src/system/CpuState.hpp](../src/system/CpuState.hpp) + the `SystemBase`
virtuals) is fully implemented on all three backends, including the two
capabilities originally pencilled in as "deferred to the Mesen debugger" — but
**without** initialising the debugger. This note records why, and sketches the
real debugger path for the day a feature genuinely needs it.

## What shipped, and how (no debugger)

The first pass at this assumed GBA instruction-stepping and side-effect-free
CPU peek required Mesen's debugger. They don't — Mesen exposes both directly:

| Capability | Implementation | Reachable from |
| --- | --- | --- |
| `stepInstruction()` (NES) | `NesCpu::Exec()` + `CycleCount` delta | `NesConsole::GetCpu()` |
| `stepInstruction()` (GBA) | `GbaCpu::Exec<false,false>()` + `CycleCount` delta | `GbaConsole::GetCpu()` |
| `readCpuByte()` (NES) | `NesMemoryManager::DebugRead(addr)` | `NesConsole::GetMemoryManager()` |
| `readCpuByte()` (GBA) | `GbaMemoryManager::DebugRead(addr)` | `GbaConsole::GetMemoryManager()` |

`GbaCpu::Exec<inlineHalt, debuggerEnabled>()` is the same public per-instruction
method `GbaConsole::RunFrame()` loops on (with `debuggerEnabled = false` when
`!IsDebugging()`), so stepping one ARM/Thumb instruction is just one `Exec`
call — exactly analogous to NES. `DebugRead` is the memory managers' existing
banking-aware, side-effect-free read (`disableSideEffects` is implicit). Both
avoid the heavyweight debugger entirely, so the normal render path
(`cli-smoke` / `cli-nes-smoke` / `cli-gba-smoke`) pays nothing.

See [MesenNesSystem.cpp](../src/system/mesen/MesenNesSystem.cpp) and
[MesenGbaSystem.cpp](../src/system/mesen/MesenGbaSystem.cpp) (the `-- CPU state`
sections), covered by [test/ts/cpu_nes.test.ts](../test/ts/cpu_nes.test.ts) and
[test/ts/cpu_gba.test.ts](../test/ts/cpu_gba.test.ts).

## When you'd actually need the Mesen debugger

`Exec` + `DebugRead` cover register/PC/step/peek. The full debugger is only
worth its cost for capabilities the current interface doesn't expose:

- **Breakpoints / watchpoints** — run-until-(memory write to X) / (PC hits X
  with a condition), execution/read/write watchpoints. `runUntilPc()` today is
  a brute-force step loop; a real PC breakpoint would be far faster and could
  express conditions.
- **Trace logging / disassembly** — per-instruction disassembly + a trace
  buffer for "what did the CPU just do".
- **cpsr (and other status-register) writes** on GBA — currently unsupported
  (`GbaCpuFlags` has `ToInt32` but no `FromInt32`); the debugger's register-set
  path handles these.

### The debugger API (sketch)

All reachable from `emu_` (`std::unique_ptr<Emulator>`):

- **Init (lazy, once):** `DebuggerRequest req = emu_->GetDebugger(/*autoInit=*/true);`
  → `Debugger* dbg = req.GetDebugger();`. `GetDebugger` only returns a live
  debugger when `IsRunning()` (true after `LoadRom`) and creates `_debugger`
  on first call; it persists until `emu_->StopDebugger()`. `DebuggerRequest`
  only refcounts in-flight calls (`_debugRequestCount`) — it does **not** own
  the debugger's lifetime, so a short-lived stack `DebuggerRequest` per call is
  fine; call `StopDebugger()` in `onDeactivate` if you ever init it.
  (`Core/Shared/Emulator.{h,cpp}`, `Core/Shared/DebuggerRequest.{h,cpp}`.)
- **Step (alternative to `Exec`):** `dbg->Step(CpuType::Nes|Gba, 1, StepType::Step)`
  (`Core/Debugger/Debugger.h`). Designed around Mesen's threaded run-loop
  (break-on-count); integrating it with the harness's manual drive needs care —
  prefer `Exec` for plain stepping, reach for `Step` only for typed step modes
  (step-over / step-out / run-to-IRQ).
- **Breakpoints:** `dbg->GetBreakpointManager()` / `SetBreakpoints(...)`.
- **Side-effect-free peek (already covered by `DebugRead`):**
  `dbg->GetMemoryDumper()->GetMemoryValue(MemoryType::NesMemory|GbaMemory, addr, true)`
  (`Core/Debugger/MemoryDumper.h`) — equivalent to `DebugRead`, no reason to
  switch unless you're already holding the debugger for breakpoints.

**Cost:** `InitDebugger()` allocates breakpoint managers, a trace logger, call
stacks, etc. and routes every instruction through the debugger hook
(`Exec<*, true>`), slowing emulation. If a future feature needs it, init it
**lazily on first use** and gate it so the render path stays on
`Exec<*, false>` — or expose it behind a harness-only flag.
