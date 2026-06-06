// CPU-state harness coverage for the Mesen NES (6502) backend — proves the
// generic CPU interface is not SameBoy-specific.

import { test, expect, emu, Mem } from "harness";

const NES = "resources/roms/n8-midi.nes";

test("NES exposes 6502 registers after boot", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  const r = emu.getRegisters(sys);
  // 6502 register file — distinct from the Game Boy's af/bc/de/hl.
  for (const name of ["a", "x", "y", "sp", "ps", "pc"]) {
    expect(name in r).toBeTruthy();
  }
  expect(r.pc).toBeLessThan(0x10000); // 16-bit program counter
});

test("NES single-steps via Exec (cycles > 0, PC moves)", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  const before = emu.getRegisters(sys).pc;
  const cycles = emu.step(sys);
  const after = emu.getRegisters(sys).pc;
  expect(cycles).toBeGreaterThan(0);
  expect(after === before).toBeFalsy();
});

test("NES setRegister writes are observable", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  emu.setRegister(sys, "a", 0x42);
  expect(emu.getRegisters(sys).a).toBe(0x42);
});

test("NES runUntilPc caps to false when the target is never hit", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  // 0x0000 is zero-page RAM; the CPU won't execute there, so the cap trips.
  expect(emu.runUntilPc(sys, 0x0000, 100000)).toBeFalsy();
});

test("NES RAM is readable via getMemory regions", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  const ram = emu.readMemory(sys, Mem.Ram); // NesInternalRam = 2 KiB
  expect(ram.length).toBe(0x800);
});

test("readCpu (side-effect-free peek) is unsupported on NES today", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  let threw = false;
  try { emu.readCpu(sys, 0x0000); } catch { threw = true; }
  expect(threw).toBeTruthy(); // deferred to the Mesen debugger
});
