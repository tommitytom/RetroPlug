// CPU-state harness coverage for the Mesen GBA (ARM7) backend. Boots on
// Mesen's HLE BIOS (no real gba_bios.bin needed). Instruction stepping +
// side-effect-free peek are deferred to the Mesen debugger
// (porting/19-mesen-debugger.md), so this asserts the graceful "unsupported".

import { test, expect, emu, Mem } from "harness";

const GBA = "../resources/roms/nanoloop287d.gba";

test("GBA exposes ARM7 registers after boot", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  const r = emu.getRegisters(sys);
  // ARM register file r0..r15 + cpsr, plus the universal "pc" alias (= r15).
  for (let i = 0; i < 16; i++) expect(`r${i}` in r).toBeTruthy();
  expect("cpsr" in r).toBeTruthy();
  expect("pc" in r).toBeTruthy();
  expect(r.pc).toBe(r.r15);
});

test("GBA setRegister writes a general register", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  emu.setRegister(sys, "r0", 0x1234abcd);
  expect(emu.getRegisters(sys).r0).toBe(0x1234abcd);
});

test("GBA instruction stepping is deferred (step=0, runUntilPc=false)", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  // No Mesen debugger yet -> stepInstruction returns 0 and runUntilPc bails.
  expect(emu.step(sys)).toBe(0);
  const pc = emu.getRegisters(sys).pc;
  expect(emu.runUntilPc(sys, pc + 4, 100000)).toBeFalsy();
});

test("GBA IWRAM is readable via getMemory regions", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  const iwram = emu.readMemory(sys, Mem.Ram); // GbaIntWorkRam = 32 KiB
  expect(iwram.length).toBe(0x8000);
});
