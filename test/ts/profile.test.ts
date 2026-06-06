// Profiler coverage on the NES (Mesen debugger). evermidi == n8-midi.nes.
// Function NAMES require symbols (loadLabels, M3); here we assert the profiler
// captures hot functions keyed by address.

import { test, expect, emu, printProfile } from "harness";

const NES = "resources/roms/n8-midi.nes";

test("profiler captures hot functions during execution", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);          // boot a bit before profiling
  emu.beginProfile(sys);   // init debugger + reset profiler
  emu.runMs(500);          // the window we measure
  const fns = emu.readProfile(sys);

  // Some functions ran and burned cycles.
  expect(fns.length).toBeGreaterThan(0);
  expect(fns[0].exclusiveCycles).toBeGreaterThan(0);

  // Sorted hottest-first (exclusive cycles, descending).
  for (let i = 1; i < fns.length; i++) {
    expect(fns[i - 1].exclusiveCycles >= fns[i].exclusiveCycles).toBeTruthy();
  }

  // Inclusive >= exclusive per function (callees are counted in inclusive).
  for (const f of fns) {
    expect(f.inclusiveCycles >= f.exclusiveCycles).toBeTruthy();
  }

  console.log("top functions:\n" + printProfile(fns, 10));
});

test("beginProfile resets the accumulated stats", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  emu.beginProfile(sys);
  emu.runMs(200);
  const a = emu.readProfile(sys);
  const totalA = a.reduce((s, f) => s + f.exclusiveCycles, 0);

  emu.beginProfile(sys); // reset
  const fresh = emu.readProfile(sys);
  const totalFresh = fresh.reduce((s, f) => s + f.exclusiveCycles, 0);

  expect(totalA).toBeGreaterThan(0);
  expect(totalFresh < totalA).toBeTruthy(); // reset cleared most/all cycles
});

test("loadLabels names the hot function in the profile (cc65 .dbg)", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(500);
  // The fixture labels CPU $9C69 (= abs PRG $1C69, the idle loop) "midiIdleLoop".
  expect(emu.loadLabels(sys, "test/ts/fixtures/n8-midi.dbg")).toBeTruthy();
  emu.beginProfile(sys);
  emu.runMs(500);
  const fns = emu.readProfile(sys);
  const hot = fns.find((f) => f.address === 0x1c69);
  expect(hot !== undefined).toBeTruthy();
  expect(hot!.label).toBe("midiIdleLoop");
});

test("sendMidi exercises evermidi's MIDI path (more functions profiled)", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1500); // boot
  emu.beginProfile(sys);
  // Note on/off bursts feed the N8 MIDI FIFO -> evermidi's handlers run.
  for (let i = 0; i < 8; i++) {
    emu.sendMidi(sys, [0x90, 0x3c + i, 0x7f]); // note on
    emu.runMs(40);
    emu.sendMidi(sys, [0x80, 0x3c + i, 0x00]); // note off
    emu.runMs(20);
  }
  const fns = emu.readProfile(sys).filter((f) => f.exclusiveCycles > 0);
  // The idle loop alone profiles ~2 functions; driving MIDI exercises many more.
  expect(fns.length).toBeGreaterThan(5);
});

test("profiler is unsupported on SameBoy (clear error)", () => {
  const sys = emu.loadRom("../resources/roms/lsdj/lsdj9_4_2.gb");
  emu.runMs(100);
  let threw = false;
  try { emu.beginProfile(sys); } catch { threw = true; }
  expect(threw).toBeTruthy();
});
