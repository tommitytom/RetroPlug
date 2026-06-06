// Passive debugger observation on the NES (Mesen): disassembly, execution
// trace, and call stack. evermidi == n8-midi.nes.

import { test, expect, emu } from "harness";

const NES = "resources/roms/n8-midi.nes";

test("disassemble returns instructions at an address", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  const pc = emu.getRegisters(sys).pc;
  const lines = emu.disassemble(sys, pc, 4);
  expect(lines.length).toBe(4);
  expect(lines[0].address).toBe(pc);
  expect(lines[0].text.length).toBeGreaterThan(0); // a mnemonic
  expect(lines[0].bytes.length).toBeGreaterThan(0); // hex byte(s)
});

test("trace logger captures the recent instruction stream", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  emu.setTrace(sys, true);
  emu.runMs(50); // accumulate a trace
  const rows = emu.readTrace(sys, 16);
  emu.setTrace(sys, false);

  expect(rows.length).toBeGreaterThan(0);
  // Each row carries a PC and a formatted disassembly line.
  expect(rows[0].text.length).toBeGreaterThan(0);
  expect(rows[0].pc >= 0).toBeTruthy();
});

test("call stack is readable (named once labels load)", () => {
  const sys = emu.loadRom(NES);
  emu.loadLabels(sys, "test/ts/fixtures/n8-midi.dbg");
  emu.runMs(800);
  const stack = emu.getCallStack(sys);
  // The idle loop is shallow but there is at least the entry frame; labels
  // resolve where a frame's target matches a loaded symbol.
  expect(Array.isArray(stack)).toBeTruthy();
  for (const f of stack) expect(typeof f.address).toBe("number");
});
