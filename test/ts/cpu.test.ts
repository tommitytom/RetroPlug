// CPU-state harness coverage (SameBoy-only): registers, banking-aware reads,
// instruction stepping, and run-until-PC.

import { test, expect, emu, Mem, CpuReg } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

test("PC sits in the cartridge address space after boot", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  const regs = emu.getRegisters(sys);
  // After the boot ROM hands off, execution is in ROM (0x0000..0x7FFF).
  expect(regs.pc).toBeLessThan(0x8000);
  expect(regs.sp).toBeGreaterThan(0); // stack pointer initialised
});

test("stepInstruction advances PC and returns cycles", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  const before = emu.getRegisters(sys).pc;
  const cycles = emu.step(sys);
  const after = emu.getRegisters(sys).pc;
  expect(cycles).toBeGreaterThan(0);
  // Some instruction executed; PC moved (or a jump landed elsewhere).
  expect(after === before).toBeFalsy();
});

test("setRegister writes are observable", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  emu.setRegister(sys, CpuReg.BC, 0x1234);
  expect(emu.getRegisters(sys).bc).toBe(0x1234);
});

test("readCpu agrees with the WRAM region view", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  const wram = emu.readMemory(sys, Mem.Ram); // 0xC000.. mirror of bank 0
  // GB_safe_read_memory at 0xC000 reads WRAM bank 0, offset 0.
  expect(emu.readCpu(sys, 0xc000)).toBe(wram[0]);
});

test("runUntilPc returns false (not hang) when the target is never hit", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  // 0x0000 is the boot/interrupt vector base; LSDJ won't execute it here, so
  // the cap must trip and return false rather than spin forever.
  const hit = emu.runUntilPc(sys, 0x0000, 100000);
  expect(hit).toBeFalsy();
});
