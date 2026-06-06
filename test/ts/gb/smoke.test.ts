// First TypeScript harness smoke test. Run via:
//   make -C build cli-ts-test
// or directly:
//   node tools/build-test.js test/ts/smoke.test.ts build/test-js/smoke.js
//   build/bin/retroplug-cli --test build/test-js/smoke.js
//
// ROM paths are relative to the repo root (the make target runs there).

import { test, expect, emu, Button, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

test("LSDJ boots and writes to WRAM", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500); // past the ~1.5s GB boot logo

  const wram = emu.readMemory(sys, Mem.Ram);
  // CGB has 8 banks of 4 KiB WRAM = 32 KiB via GB_DIRECT_ACCESS_RAM.
  expect(wram.length).toBe(0x8000);

  let nonzero = 0;
  for (const b of wram) if (b !== 0) nonzero++;
  expect(nonzero).toBeGreaterThan(0);
});

test("repeated WRAM reads return independent same-size buffers", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);

  const a = emu.readMemory(sys, Mem.Ram);
  emu.runMs(500); // advance; emulator memory may change underneath
  const b = emu.readMemory(sys, Mem.Ram);
  // Each call hands back a fresh copy of the live region (the C++ side never
  // exposes the raw pointer), so the two are distinct objects of equal size.
  expect(b.length).toBe(a.length);
  expect(a === b).toBeFalsy();
});

test("SELECT+UP chord drives input without throwing", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(15000); // boot + LSDJ cartridge self-test
  emu.chord(sys, [Button.Select, Button.Up]); // SONG -> PROJECT
  // Reaching here means the chord + advances ran; deeper screen-state
  // assertions come once WRAM field offsets are mapped.
  expect(true).toBeTruthy();
});
