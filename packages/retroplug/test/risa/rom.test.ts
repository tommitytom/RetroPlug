// risa ROM asset view/patch layer (M3) — pure byte-level tests. Mirrors test/lsdj/rom.test.ts: read the
// themes/fonts, prove setTheme/setChrFontSlot splice ONLY the intended bytes (byte-diff), and round-trip
// the .rit / .chr / planar-tile codecs. Uses a full synthetic risa ROM (theme table + CHR) so no emulator
// or real ROM is needed.
import { test, expect } from "../../testing/harness";
import { risaRomFull, risaRom, garbage } from "../systems/fixtures";
import { RisaRom, decodeThemeFromRom, encodeThemeRecord, encodeThemeName, parseRit, serializeRit, decodeTile, encodeTile } from "../../src/risa/rom";

const FIXED_OFFSET = 0x10 + 63 * 0x2000; // theme table base
const RECORD_BASE = FIXED_OFFSET + 6;
const NAMES_OFF = RECORD_BASE + 16 * 7;
const CHR_OFFSET = 0x10 + 0x80000;

/** The set of byte offsets that differ between two equal-length buffers. */
function changedOffsets(a: Uint8Array, b: Uint8Array): number[] {
  const out: number[] = [];
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) out.push(i);
  return out;
}

test("isRisa accepts a full synthetic risa ROM and rejects a header-prefix / garbage buffer", () => {
  expect(RisaRom.fromBytes(risaRomFull()).isRisa).toBe(true);
  expect(RisaRom.fromBytes(risaRom()).isRisa).toBe(false); // header prefix only (size mismatch)
  expect(RisaRom.fromBytes(garbage()).isRisa).toBe(false);
});

test("themes() decodes the 16-theme table located by the magic scan", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  expect(rom.hasThemes).toBe(true);
  expect(rom.themeCount).toBe(16);
  const themes = rom.themes();
  expect(themes.length).toBe(16);
  // Theme 1's roles were seeded (i*7 + r); decode reads them as "0xNN" strings.
  const t1 = themes[1].theme;
  expect(t1.bg).toBe("0x07"); // 1*7 + 0
  expect(t1.selection).toBe("0x0D"); // 1*7 + 6
  expect(themes[0].theme.name).toBe("TH0 "); // 4-char, space-padded
});

test("setTheme splices only the 7-byte record + 4-byte name for that slot", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  const before = rom.bytes().slice();

  const rec = new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07]);
  const name = new Uint8Array([0x5a, 0x5a, 0x5a, 0x5a]); // "ZZZZ"
  rom.setTheme(5, rec, name);

  const changed = changedOffsets(before, rom.bytes());
  const recStart = RECORD_BASE + 5 * 7;
  const nameStart = NAMES_OFF + 5 * 4;
  for (const off of changed) {
    const inRec = off >= recStart && off < recStart + 7;
    const inName = off >= nameStart && off < nameStart + 4;
    expect(inRec || inName).toBe(true);
  }
  expect(changed.length).toBe(11); // all 7 + 4 differ from the seed
  const back = rom.getTheme(5)!;
  expect(Array.from(back.recordBytes)).toEqual([1, 2, 3, 4, 5, 6, 7]);
  expect(Array.from(back.nameBytes)).toEqual([0x5a, 0x5a, 0x5a, 0x5a]);
});

test("encode/decode a theme round-trips through the .rit shape", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  const decoded = decodeThemeFromRom(rom.getTheme(3)!.recordBytes, rom.getTheme(3)!.nameBytes);
  // .rit serialize -> parse -> equal
  const rit = serializeRit(decoded);
  expect(parseRit(rit).theme).toEqual(decoded);
  // and re-encoding to ROM bytes reproduces the original record/name.
  expect(Array.from(encodeThemeRecord(decoded))).toEqual(Array.from(rom.getTheme(3)!.recordBytes));
  expect(Array.from(encodeThemeName(decoded))).toEqual(Array.from(rom.getTheme(3)!.nameBytes));
});

test("parseRit fills risa's cursor/selection fallbacks and rejects a malformed shape", () => {
  const t = parseRit({ theme: { name: "AB", bg: "0x10", alternate: "0x20" } }).theme;
  expect(t.name).toBe("AB  "); // padded to 4
  expect(t.cursor).toBe("0x20"); // defaults to alternate
  expect(t.selection).toBe("0x20"); // defaults to cursor ?? alternate
  expect(() => parseRit({ version: 1 })).toThrow(); // no theme
  expect(() => parseRit([1, 2, 3])).toThrow();
});

test("fonts: getChrFontSlot reads a slot, setChrFontSlot splices only that 8 KB bank", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  expect(rom.chrFontSlotCount).toBe(4);
  expect(rom.fonts().map((f) => f.slot)).toEqual([0, 1, 2, 3]);

  const slot1 = rom.getChrFontSlot(1)!;
  expect(slot1.length).toBe(0x2000);
  expect(slot1[0]).toBe((1 * 13 + 0) & 0xff); // the seeded pattern

  const before = rom.bytes().slice();
  const bank = new Uint8Array(0x2000).fill(0xab);
  rom.setChrFontSlot(2, bank);
  const changed = changedOffsets(before, rom.bytes());
  const start = CHR_OFFSET + 2 * 0x2000;
  for (const off of changed) expect(off >= start && off < start + 0x2000).toBe(true);
  expect(Array.from(rom.getChrFontSlot(2)!)).toEqual(Array.from(bank));
});

test("planar tile codec round-trips", () => {
  const bank = new Uint8Array(0x2000);
  const px = Array.from({ length: 64 }, (_v, i) => i & 3);
  encodeTile(bank, 7, px);
  expect(decodeTile(bank, 7)).toEqual(px);
});
