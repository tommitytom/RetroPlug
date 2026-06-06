# 19 — Mesen debugger: GBA stepping + side-effect-free CPU peek

**Status:** deferred. The generic CPU-state interface (step 18-era harness work)
ships with two capabilities stubbed out on the Mesen backends. This document is
the implementation guide for wiring them up later — no interface change is
needed, only method bodies.

## What's missing and why

The cross-emulator CPU interface lives on `SystemBase` (see
[src/system/CpuState.hpp](../src/system/CpuState.hpp) and the `getCpuRegisters` /
`setCpuRegister` / `getProgramCounter` / `readCpuByte` / `stepInstruction`
virtuals). SameBoy implements all of it via the SameBoy core. The Mesen backends
([MesenNesSystem](../src/system/mesen/MesenNesSystem.cpp),
[MesenGbaSystem](../src/system/mesen/MesenGbaSystem.cpp)) implement registers +
PC + register writes directly off the typed CPU (`NesCpu`/`GbaCpu`), but two
things require Mesen's **debugger**, which the wrappers don't initialise:

| Capability | NES today | GBA today | Needs |
| --- | --- | --- | --- |
| `stepInstruction()` | ✓ `NesCpu::Exec()` | **0 (deferred)** | `Debugger::Step` |
| `readCpuByte()` (no side effects) | **nullopt (deferred)** | **nullopt (deferred)** | `MemoryDumper::GetMemoryValue` |

NES already single-steps without the debugger (`Exec()` runs exactly one
instruction). GBA can't — ARM/Thumb are variable-width and the wrapper drives
the core a frame at a time (`GbaConsole::RunFrame`), so instruction stepping
needs the debugger's stepper. The side-effect-free peek needs the debugger's
`MemoryDumper` on both NES and GBA (the raw `Emulator::GetMemory` buffer has I/O
side effects and doesn't follow CPU-address banking).

## The Mesen APIs

All reachable from the wrapper's `emu_` (`std::unique_ptr<Emulator>`):

- **Init (once):** `emu_->InitDebugger()` then
  `DebuggerRequest req = emu_->GetDebugger(true);` → `Debugger* dbg = req.GetDebugger();`
  (`Emulator.h` `InitDebugger` / `GetDebugger`; `DebuggerRequest.h`). The
  `DebuggerRequest` is a refcounting handle — hold it (or re-acquire) for the
  system's lifetime; release on `onDeactivate`.
- **Step one instruction:** `dbg->Step(cpuType, 1, StepType::Step);`
  (`Debugger.h`). `cpuType` is `CpuType::Nes` or `CpuType::Gba`
  (`Core/Shared/CpuType.h`). For the `stepInstruction()` return value (cycles),
  diff `GbaCpuState::CycleCount` (or `NesCpuState::CycleCount`) before/after.
- **Side-effect-free peek:**
  `dbg->GetMemoryDumper()->GetMemoryValue(memType, addr, /*disableSideEffects=*/true);`
  (`MemoryDumper.h`). `memType` is the CPU-address-space memory type
  (`MemoryType::NesMemory` / `MemoryType::GbaMemory`) so banking is honoured.

## Wiring it in

1. **Init at activate, tear down at deactivate.** In each Mesen wrapper's
   `onActivate`, after `LoadRom`, call `InitDebugger()` + cache the
   `DebuggerRequest`. In `onDeactivate`, release it before `emu_.reset()`.
   **Cost:** the debugger allocates breakpoint managers, a trace logger, call
   stacks, etc. — heavyweight, and it slows `Exec()`/`RunFrame()` because every
   instruction now goes through the debugger hook. Acceptable for the test
   harness (not realtime); if it regresses `cli-smoke`/`gba_smoke` render speed,
   gate init behind a flag set only by the harness (e.g. a `Mesen*Config`
   `enableDebugger` field, off by default).
2. **`MesenGbaSystem::stepInstruction()`** — override (currently inherits the
   base `0`): set the emulation thread id (as `onProcess` does), read
   `CycleCount`, `dbg->Step(CpuType::Gba, 1, StepType::Step)`, return the cycle
   delta. With this, the base `runUntilPc()` starts working on GBA for free.
3. **`readCpuByte()`** — override on both Mesen wrappers (currently inherit the
   base `nullopt`): return
   `dbg->GetMemoryDumper()->GetMemoryValue(MemoryType::{Nes,Gba}Memory, addr, true)`.
   NES could optionally route through the debugger for `stepInstruction` too for
   consistency, but `Exec()` is cheaper and already correct — leave it.

## Verifying once wired

- Flip the deferred assertions in
  [test/ts/cpu_gba.test.ts](../test/ts/cpu_gba.test.ts): `emu.step(sys)` should
  now return `> 0`, and `runUntilPc` should reach a nearby PC.
- Flip the "unsupported" assertion in
  [test/ts/cpu_nes.test.ts](../test/ts/cpu_nes.test.ts): `emu.readCpu(sys, addr)`
  should return a byte that agrees with `getMemory` for RAM addresses, and read
  PPU/IO addresses without side effects.
- Add a GBA `readCpu` test mirroring the NES one.
- Regression: `make -C build cli-smoke gba_smoke cli-nes-smoke` must stay green
  (watch render-speed if the debugger is always-on rather than harness-gated).
