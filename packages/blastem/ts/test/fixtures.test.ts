// The TS images are twins of the native ones, so the pins here are the numbers SyntheticRom.test.cpp pins for its own.
// Both sides generate their bytes independently: an image that drifts fails the file that moved, and the parity group
// cannot quietly start booting bytes the native cases never checked.
import { test, expect } from "@retroplug/testing";
import {
  fmPsgRom, psgOnlyRom, fmOnlyRom, hvPsgRom, vintHintRom, z80TimingRom, vintDmaRom, fnv1a, MD_ROM_SIZE,
} from "../fixtures";

const charsAt = (bytes: Uint8Array, offset: number, text: string): boolean => {
  for (let i = 0; i < text.length; i++) if (bytes[offset + i] !== text.charCodeAt(i)) return false;
  return true;
};

/** The native header's pins, by image. */
const HASHES: [string, () => Uint8Array, number][] = [
  ["psgOnlyRom", psgOnlyRom, 0x6397d2df],
  ["fmPsgRom", fmPsgRom, 0x0dcf5fca],
  ["fmOnlyRom", fmOnlyRom, 0x8f0d759b],
  ["hvPsgRom", hvPsgRom, 0xdceb58e7],
  ["vintHintRom", vintHintRom, 0x3dc48361],
  ["z80TimingRom", z80TimingRom, 0xa3c8a6bf],
  ["vintDmaRom", vintDmaRom, 0xbdd1a4bf],
];

for (const [name, make, want] of HASHES) {
  test(`${name} is the bytes the native cases boot`, () => {
    const rom = make();
    expect(rom.length).toBe(MD_ROM_SIZE);
    expect(fnv1a(rom)).toBe(want);
  });
}

test("every image carries the header the ROM gate reads and the vectors that point at $200", () => {
  for (const [, make] of HASHES) {
    const rom = make();
    expect(charsAt(rom, 0x100, "SEGA MEGA DRIVE ")).toBe(true);
    expect(charsAt(rom, 0x100, "SEGA 32X")).toBe(false); // the header the native gate refuses
    expect([rom[0], rom[1], rom[2], rom[3]]).toEqual([0x00, 0xff, 0xfe, 0x00]); // SP $00FFFE00
    expect([rom[4], rom[5], rom[6], rom[7]]).toEqual([0x00, 0x00, 0x02, 0x00]); // reset PC $000200
  }
});
