// BlipToaster ROM asset view/patch layer — pure byte-level tests. Mirrors test/risa/rom.test.ts: read the kit +
// font, prove setKit/setChrFontSlot splice ONLY the intended bytes (byte-diff), and that isBlipToaster accepts a
// full ROM but rejects a marker-less / truncated / garbage buffer. No emulator or real ROM needed.
import { test, expect } from "../../testing/harness";
import {
  blipToasterRom,
  blipToasterMultiKitRom,
  blipToasterThemeTable,
  blipToasterSettingsBlock,
  nesRom,
  garbage,
} from "../systems/fixtures";
import { BlipToasterRom } from "../../src/bliptoaster/rom";
import { serializeRit, parseRit } from "../../src/risa/rom";

const KIT_OFFSET = 0x10 + 0x4000; // the baked kit at $C000
const CHR_OFFSET = 0x10 + 0x8000; // CHR follows the 32 KB PRG
const THEME_OFFSET = 0x100; // the theme table in blipToasterRom() (code region, before the kit)

/** The set of byte offsets that differ between two equal-length buffers. */
function changedOffsets(a: Uint8Array, b: Uint8Array): number[] {
  const out: number[] = [];
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) out.push(i);
  return out;
}

test("isBlipToaster accepts a full BlipToaster ROM and rejects marker-less / truncated / garbage buffers", () => {
  expect(BlipToasterRom.fromBytes(blipToasterRom()).isBlipToaster).toBe(true);
  expect(BlipToasterRom.fromBytes(nesRom()).isBlipToaster).toBe(false); // NES magic but no BLIPTOASTER marker
  expect(BlipToasterRom.fromBytes(garbage()).isBlipToaster).toBe(false);
  expect(BlipToasterRom.fromBytes(blipToasterRom().slice(0, 0x100)).isBlipToaster).toBe(false); // marker but too small
});

test("kits() decodes the baked kit; getKitBank reads its 8 KB bank", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  expect(rom.kitCount()).toBe(1);
  expect(rom.isKitPopulated(0)).toBe(true);
  const kits = rom.kits();
  expect(kits.length).toBe(1);
  expect(kits[0].slot).toBe(0);
  expect(kits[0].name).toBe("TEST");
  expect(rom.getKitBank(0)!.length).toBe(0x2000);
});

test("setKit splices only the 8 KB kit bank (no metadata mirror)", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  const before = rom.bytes().slice();

  const bank = new Uint8Array(0x2000).fill(0xab);
  bank[0x1f40] = 0xa5; // keep it a populated bank
  rom.setKit(0, bank);

  const changed = changedOffsets(before, rom.bytes());
  for (const off of changed) expect(off >= KIT_OFFSET && off < KIT_OFFSET + 0x2000).toBe(true);
  expect(Array.from(rom.getKitBank(0)!)).toEqual(Array.from(bank));
});

test("clearKitBank empties the slot (drops the populated magic)", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  expect(rom.isKitPopulated(0)).toBe(true);
  rom.clearKitBank(0);
  expect(rom.isKitPopulated(0)).toBe(false);
});

test("NROM is single-kit: capacity 1, no free slot, out-of-range setKit is a no-op", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  expect(rom.kitBankCapacity()).toBe(1);
  expect(rom.firstFreeKitIndex()).toBe(-1); // slot 0 populated, capacity 1 → nothing free
  const before = rom.bytes().slice();
  const bank = new Uint8Array(0x2000).fill(0x77);
  bank[0x1f40] = 0xa5;
  rom.setKit(5, bank); // beyond capacity — ignored
  expect(changedOffsets(before, rom.bytes()).length).toBe(0);
  expect(rom.isKitPopulated(5)).toBe(false);
});

test("a banking ROM exposes 16 kit banks: capacity 16, first free is slot 1, setKit(5) adds a bank", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterMultiKitRom());
  expect(rom.isBlipToaster).toBe(true);
  expect(rom.kitBankCapacity()).toBe(16);
  // Only slot 0 is baked; the rest are reserved/empty.
  expect(rom.kitCount()).toBe(1);
  expect(rom.kits().map((k) => k.slot)).toEqual([0]);
  expect(rom.firstFreeKitIndex()).toBe(1);

  // Splice a distinct populated bank into slot 5 — only that bank's 8 KB changes.
  const before = rom.bytes().slice();
  const bank = new Uint8Array(0x2000).fill(0x99);
  for (const [i, c] of Array.from("DRUM").entries()) bank[0x1ec0 + i] = c.charCodeAt(0); // kit name
  bank[0x1f40] = 0xa5; // populated
  rom.setKit(5, bank);

  const slot5Off = 0x10 + 0x4000 + 5 * 0x2000;
  for (const off of changedOffsets(before, rom.bytes())) expect(off >= slot5Off && off < slot5Off + 0x2000).toBe(true);
  expect(rom.isKitPopulated(5)).toBe(true);
  expect(rom.kits().map((k) => k.slot)).toEqual([0, 5]);
  expect(rom.firstFreeKitIndex()).toBe(1); // slot 1 still free
});

test("fonts: getChrFontSlot reads the slot, setChrFontSlot splices only that 8 KB bank", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
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

test("themes() decodes all 16 baked themes from the INTERLEAVED table, not just entry 0", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  expect(rom.hasThemes).toBe(true);
  expect(rom.themeCount).toBe(16);
  const themes = rom.themes();
  expect(themes.map((t) => t.slot)).toEqual([...Array(16).keys()]);
  // The names are what a menu row / `info` line shows. Read from 11-byte entries: on risa's split layout
  // (16 records, THEN 16 names) only entry 0's name lands in the right place, so this list is the assertion
  // that the stride is BlipToaster's own.
  expect(themes.map((t) => t.theme.name)).toEqual([
    "DFLT", "DARK", "NEON", "LITE", "CRT ", "ICE ", "FIRE", "GB  ",
    "AQUA", "MONO", "PLSM", "MTRX", "FOG ", "SUN ", "MOON", "AMBR",
  ]);
  expect(themes[0].theme.bg).toBe("0x0D"); // the fixture's per-slot bg = 0x0D + slot
  expect(themes[0].theme.normal).toBe("0x30");
  expect(themes[15].theme.bg).toBe("0x1C");
});

test("the theme count is the table's, bounded by the first byte that is not a palette index", () => {
  // The shipped ROM puts the settings block's $A5 magic immediately after the last entry, with no terminator
  // and no fill — so a table of 3 must read as 3, not run on into whatever follows it.
  const short = blipToasterRom();
  short.set(blipToasterThemeTable(3), 0x100);
  short.set(blipToasterSettingsBlock(), 0x100 + 6 + 3 * 11);
  const rom = BlipToasterRom.fromBytes(short);
  expect(rom.themeCount).toBe(3);
  expect(rom.themes().map((t) => t.theme.name)).toEqual(["DFLT", "DARK", "NEON"]);
  // Past the last entry there is nothing to read or write: getTheme is null and setTheme cannot grow the table.
  expect(rom.getTheme(3)).toBe(null);
  expect(rom.getTheme(-1)).toBe(null);
  const before = rom.bytes().slice();
  rom.setTheme(3, new Uint8Array(7).fill(1), new Uint8Array(4).fill(0x5a));
  expect(changedOffsets(before, rom.bytes())).toEqual([]);
});

test("a ROM with no theme table reports none", () => {
  const bare = blipToasterRom();
  bare.fill(0, 0x100, 0x100 + 6); // wipe the THME magic
  const rom = BlipToasterRom.fromBytes(bare);
  expect(rom.hasThemes).toBe(false);
  expect(rom.themeCount).toBe(0);
  expect(rom.themes()).toEqual([]);
  expect(rom.getTheme(0)).toBe(null);
});

test("setTheme splices only the 7-byte record + 4-byte name of that entry", () => {
  const rom = BlipToasterRom.fromBytes(blipToasterRom());
  const before = rom.bytes().slice();

  const rec = new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07]);
  const name = new Uint8Array([0x5a, 0x5a, 0x5a, 0x5a]); // "ZZZZ"
  rom.setTheme(5, rec, name); // an entry the old split-layout reader could not even address

  const changed = changedOffsets(before, rom.bytes());
  const entryStart = THEME_OFFSET + 6 + 5 * 11; // after the 6-byte magic, 5 whole 11-byte entries in
  for (const off of changed) expect(off >= entryStart && off < entryStart + 11).toBe(true);
  expect(changed.length).toBe(11); // all 7 + 4 differ from the seed
  const back = rom.getTheme(5)!;
  expect(Array.from(back.recordBytes)).toEqual([1, 2, 3, 4, 5, 6, 7]);
  expect(Array.from(back.nameBytes)).toEqual([0x5a, 0x5a, 0x5a, 0x5a]);
  // Its neighbours are untouched, which is what an 11-byte stride buys.
  expect(rom.getTheme(4)!.nameBytes[0]).toBe("C".charCodeAt(0)); // "CRT "
  expect(rom.getTheme(6)!.nameBytes[0]).toBe("F".charCodeAt(0)); // "FIRE"
});

test("a theme round-trips through the .rit shape", () => {
  const t = BlipToasterRom.fromBytes(blipToasterRom()).themes()[0].theme;
  expect(parseRit(serializeRit(t)).theme).toEqual(t);
});
