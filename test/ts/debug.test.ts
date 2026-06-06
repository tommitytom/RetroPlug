// Breakpoint / watchpoint / stepping on the NES (Mesen debugger, headless).
// evermidi == n8-midi.nes; $9C69 (label midiIdleLoop) reads the MIDI FIFO at
// $40F1 — see disassembly in observe.test.ts.

import { test, expect, emu } from "harness";

const NES = "resources/roms/n8-midi.nes";
const FIXTURE = "test/ts/fixtures/n8-midi.dbg";

test("execute breakpoint stops at the address", () => {
  const sys = emu.loadRom(NES);
  emu.loadLabels(sys, FIXTURE);
  emu.runMs(500);
  emu.setBreakpoints(sys, [{ type: "execute", start: 0x9c69 }]);
  const info = emu.runUntilBreak(sys, 5_000_000);
  expect(info.broke).toBeTruthy();
  expect(info.pc).toBe(0x9c69);
  expect(info.breakpointId >= 0).toBeTruthy();
});

test("read watchpoint breaks on the MIDI FIFO access ($40F1)", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  // The idle loop polls $40F1; a read watchpoint there must fire.
  emu.setBreakpoints(sys, [{ type: "read", start: 0x40f1 }]);
  const info = emu.runUntilBreak(sys, 5_000_000);
  expect(info.broke).toBeTruthy();
  expect(info.breakpointId >= 0).toBeTruthy();
});

test("stepInto advances roughly one instruction", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  const pc0 = emu.getRegisters(sys).pc;
  const info = emu.stepInto(sys);
  expect(info.broke).toBeTruthy();
  // PC moved (linear) or a branch landed elsewhere — either way it changed.
  expect(info.pc !== pc0 || emu.getRegisters(sys).pc !== pc0).toBeTruthy();
});

test("runUntilBreak returns broke=false when no breakpoint is hit", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  emu.setBreakpoints(sys, [{ type: "execute", start: 0xfffe }]); // not reached
  const info = emu.runUntilBreak(sys, 200_000);
  expect(info.broke).toBeFalsy();
});

test("conditional breakpoint honours the expression", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  // A contradiction never matches — the breakpoint must not fire.
  emu.setBreakpoints(sys, [{ type: "execute", start: 0x9c69, condition: "1 == 0" }]);
  expect(emu.runUntilBreak(sys, 500_000).broke).toBeFalsy();

  // Y is 0 at the idle-loop entry — this condition matches and fires at $9c69.
  emu.setBreakpoints(sys, [{ type: "execute", start: 0x9c69, condition: "Y == 0" }]);
  const info = emu.runUntilBreak(sys, 5_000_000);
  expect(info.broke).toBeTruthy();
  expect(info.pc).toBe(0x9c69);
});
