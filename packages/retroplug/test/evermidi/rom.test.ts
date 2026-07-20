// EverMIDI ROM asset view/patch layer — pure byte-level tests. Mirrors test/risa/rom.test.ts: read the kit +
// font, prove setKit/setChrFontSlot splice ONLY the intended bytes (byte-diff), and that isEverMidi accepts a
// full ROM but rejects a marker-less / truncated / garbage buffer. No emulator or real ROM needed.
import { test, expect } from "../../testing/harness";
import { everMidiRom, nesRom, garbage } from "../systems/fixtures";
import { EverMidiRom } from "../../src/evermidi/rom";

const KIT_OFFSET = 0x10 + 0x4000; // the baked kit at $C000
const CHR_OFFSET = 0x10 + 0x8000; // CHR follows the 32 KB PRG

/** The set of byte offsets that differ between two equal-length buffers. */
function changedOffsets(a: Uint8Array, b: Uint8Array): number[] {
  const out: number[] = [];
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) out.push(i);
  return out;
}

test("isEverMidi accepts a full EverMIDI ROM and rejects marker-less / truncated / garbage buffers", () => {
  expect(EverMidiRom.fromBytes(everMidiRom()).isEverMidi).toBe(true);
  expect(EverMidiRom.fromBytes(nesRom()).isEverMidi).toBe(false); // NES magic but no EVERMIDI marker
  expect(EverMidiRom.fromBytes(garbage()).isEverMidi).toBe(false);
  expect(EverMidiRom.fromBytes(everMidiRom().slice(0, 0x100)).isEverMidi).toBe(false); // marker but too small
});

test("kits() decodes the baked kit; getKitBank reads its 8 KB bank", () => {
  const rom = EverMidiRom.fromBytes(everMidiRom());
  expect(rom.kitCount()).toBe(1);
  expect(rom.isKitPopulated(0)).toBe(true);
  const kits = rom.kits();
  expect(kits.length).toBe(1);
  expect(kits[0].slot).toBe(0);
  expect(kits[0].name).toBe("TEST");
  expect(rom.getKitBank(0)!.length).toBe(0x2000);
});

test("setKit splices only the 8 KB kit bank (no metadata mirror)", () => {
  const rom = EverMidiRom.fromBytes(everMidiRom());
  const before = rom.bytes().slice();

  const bank = new Uint8Array(0x2000).fill(0xab);
  bank[0x1f40] = 0xa5; // keep it a populated bank
  rom.setKit(0, bank);

  const changed = changedOffsets(before, rom.bytes());
  for (const off of changed) expect(off >= KIT_OFFSET && off < KIT_OFFSET + 0x2000).toBe(true);
  expect(Array.from(rom.getKitBank(0)!)).toEqual(Array.from(bank));
});

test("clearKitBank empties the slot (drops the populated magic)", () => {
  const rom = EverMidiRom.fromBytes(everMidiRom());
  expect(rom.isKitPopulated(0)).toBe(true);
  rom.clearKitBank(0);
  expect(rom.isKitPopulated(0)).toBe(false);
});

test("fonts: getChrFontSlot reads the slot, setChrFontSlot splices only that 8 KB bank", () => {
  const rom = EverMidiRom.fromBytes(everMidiRom());
  expect(rom.chrFontSlotCount).toBe(1);
  expect(rom.fonts().map((f) => f.slot)).toEqual([0]);

  const slot0 = rom.getChrFontSlot(0)!;
  expect(slot0.length).toBe(0x2000);
  expect(slot0[0]).toBe((0 * 7 + 3) & 0xff); // the seeded pattern

  const before = rom.bytes().slice();
  const bank = new Uint8Array(0x2000).fill(0xcd);
  rom.setChrFontSlot(0, bank);
  const changed = changedOffsets(before, rom.bytes());
  for (const off of changed) expect(off >= CHR_OFFSET && off < CHR_OFFSET + 0x2000).toBe(true);
  expect(Array.from(rom.getChrFontSlot(0)!)).toEqual(Array.from(bank));
});
