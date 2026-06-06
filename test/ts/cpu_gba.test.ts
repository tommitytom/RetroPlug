// CPU-state harness coverage for the Mesen GBA (ARM7) backend. Boots on
// Mesen's HLE BIOS (no real gba_bios.bin needed).

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

test("GBA single-steps via Exec (cycles > 0)", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  expect(emu.step(sys)).toBeGreaterThan(0);
});

test("GBA runUntilPc steps back to a PC seen in the execution loop", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(2000); // settle into the main/idle loop
  emu.step(sys);
  const target = emu.getRegisters(sys).pc;
  emu.step(sys); // move off it; the loop returns here
  expect(emu.runUntilPc(sys, target, 5_000_000)).toBeTruthy();
});

test("GBA readCpu (side-effect-free peek) agrees with the IWRAM region", () => {
  const sys = emu.loadRom(GBA);
  emu.runMs(1000);
  const iwram = emu.readMemory(sys, Mem.Ram); // GbaIntWorkRam @ 0x03000000
  expect(iwram.length).toBe(0x8000); // 32 KiB
  for (const off of [0x0, 0x10, 0x100, 0x1000]) {
    expect(emu.readCpu(sys, 0x03000000 + off)).toBe(iwram[off]);
  }
});
