// LSDj ROM asset module (src/lsdj/rom): pure-TS units over synthetic ROM buffers. Real-ROM extraction
// is covered by test-native/lsdj-rom.test.ts.
import { test, expect } from "../../testing/harness";
import { findPattern, findPatternAnywhere } from "../../src/lsdj/rom/find";
import { KitView, decodeNibbles } from "../../src/lsdj/rom/kit";
import { buildKitBank, sampleBytesFromBank, kitSampleSpace } from "../../src/lsdj/rom/buildKit";
import { PaletteView, findPaletteBase, unpackRgb555, packRgb555, decodeLsdpal, encodeLsdpal } from "../../src/lsdj/rom/palette";
import { FontView, findFontBase } from "../../src/lsdj/rom/font";
import { findGrayscaleNames, paletteCount, paletteNames, fontNames, setPaletteName, setFontName } from "../../src/lsdj/rom/names";
import { LsdjRom } from "../../src/lsdj/rom/rom";
import { BANK_SIZE, ROM_SIZE, KIT_LOOKUP, PALETTE_CHECK, PALETTE_SIZE, PALETTE_COUNT, FONT_HEADER_CHECK, FONT_TILE_COUNT, FONT_GFX_TILE_COUNT, FONT_VARIANT_STRIDE, FONT_TILE_SIZE } from "../../src/lsdj/rom/constants";

// Little-endian u16 store into a buffer (mirrors the on-ROM offset-table encoding).
function setU16le(buf: Uint8Array, off: number, v: number): void {
  buf[off] = v & 0xff;
  buf[off + 1] = (v >> 8) & 0xff;
}
function ascii(buf: Uint8Array, off: number, s: string): void {
  for (let i = 0; i < s.length; i++) buf[off + i] = s.charCodeAt(i);
}

test("findPattern: locates a marker within a bank and applies the addend", () => {
  const rom = new Uint8Array(4 * BANK_SIZE);
  const at = 2 * BANK_SIZE + 0x100;
  rom.set([0xde, 0xad, 0xbe, 0xef], at);
  expect(findPattern(rom, 2, [0xde, 0xad, 0xbe, 0xef])).toBe(at);
  expect(findPattern(rom, 2, [0xde, 0xad, 0xbe, 0xef], 4)).toBe(at + 4); // addend
  expect(findPattern(rom, 1, [0xde, 0xad, 0xbe, 0xef])).toBe(-1); // wrong bank → not found
  expect(findPattern(rom, 2, [0xde, 0xad, 0x00])).toBe(-1); // no match
});

test("findPattern: an empty pattern / out-of-range bank is -1", () => {
  const rom = new Uint8Array(2 * BANK_SIZE);
  expect(findPattern(rom, 0, [])).toBe(-1);
  expect(findPattern(rom, 5, [0x00])).toBe(-1); // bank past the end
});

test("findPatternAnywhere: scans across banks", () => {
  const rom = new Uint8Array(4 * BANK_SIZE);
  const at = 3 * BANK_SIZE + 0x20;
  rom.set([0x01, 0x02, 0x03], at);
  expect(findPatternAnywhere(rom, [0x01, 0x02, 0x03])).toBe(at);
  expect(findPatternAnywhere(rom, [0x09, 0x09])).toBe(-1);
});

// --- kit read + rename round-trip ---

// A 1 MiB ROM with one valid kit in slot 0 (bank 8): one 2-byte sample (4 nibbles), named.
function romWithKit(): Uint8Array {
  const rom = new Uint8Array(ROM_SIZE);
  const base = KIT_LOOKUP[0] * BANK_SIZE;
  setU16le(rom, base + 0, 0x4060); // entry 0 = sample-data start (valid magic)
  setU16le(rom, base + 2, 0x4062); // entry 1 = sample 0 end (2 bytes of data)
  ascii(rom, base + 0x22, "BD-"); // sample 0 name
  ascii(rom, base + 0x52, "KICKS"); // kit name (≤6)
  rom[base + 0x60] = 0xf0; // nibbles 15,0
  rom[base + 0x61] = 0x0f; // nibbles 0,15
  return rom;
}

test("KitView: decodes validity, names, sample count and inverted 4-bit PCM (un-rotated)", () => {
  const kit = new KitView(romWithKit(), 0, false); // rotate=false → per-byte decode
  expect(kit.valid).toBeTruthy();
  expect(kit.empty).toBeFalsy();
  expect(kit.name()).toBe("KICKS");
  expect(kit.sampleCount()).toBe(1);
  expect(kit.sampleName(0)).toBe("BD-");
  const pcm = kit.sampleData(0);
  expect(pcm.length).toBe(4); // 2 bytes → 4 nibbles
  // Nibbles are stored INVERTED (amp = 0xF - stored): stored 15 → -1, stored 0 → +1.
  expect(pcm[0]).toBe(-1); // byte 0xf0 high nibble 15
  expect(pcm[1]).toBe(1); //  low nibble 0
  expect(pcm[2]).toBe(1); //  byte 0x0f high nibble 0
  expect(pcm[3]).toBe(-1); // low nibble 15
});

test("KitView: setName / setSampleName round-trip and don't touch sample data", () => {
  const rom = romWithKit();
  const base = KIT_LOOKUP[0] * BANK_SIZE;
  const kit = new KitView(rom, 0);
  kit.setName("NEW");
  kit.setSampleName(0, "SD-");
  const re = new KitView(rom, 0);
  expect(re.name()).toBe("NEW");
  expect(re.sampleName(0)).toBe("SD-");
  // offset table + sample data untouched → still one sample, same PCM
  expect(re.sampleCount()).toBe(1);
  expect(rom[base + 0x60]).toBe(0xf0);
  expect(rom[base + 0x61]).toBe(0x0f);
  // trailing name bytes NUL-padded (6-char field, "NEW" = 3 chars)
  expect(rom[base + 0x52 + 3]).toBe(0);
});

// --- nibble decode: invert + 9.2.0+ frame rotation ---

test("decodeNibbles: inverts, and un-rotates a 32-sample frame when rotate=true", () => {
  // One frame: source samples 0..31 = amplitudes 0..31 clamped to a nibble; encode inverted+rotated by hand.
  const frame = new Uint8Array(16);
  // Build a frame where stored nibble at encoded position p = p (so amp = 0xF - p). Rotation: encoded pos i
  // holds source (i-1); so source sample s sits at encoded (s+1)%32.
  for (let p = 0; p < 32; p++) {
    const byte = Math.floor(p / 2);
    if (p % 2 === 0) frame[byte] |= (p & 0xf) << 4;
    else frame[byte] |= p & 0xf;
  }
  const un = decodeNibbles(frame, true);
  expect(un.length).toBe(32);
  // source sample s was stored at encoded (s+1)%32 with stored nibble ((s+1)%32)&0xf → amp = 0xF - that.
  const amp = (n: number) => Math.fround(((0xf - n) / 15) * 2 - 1);
  expect(un[0]).toBe(amp(1 & 0xf)); // source 0 ← encoded pos 1
  expect(un[5]).toBe(amp(6 & 0xf)); // source 5 ← encoded pos 6
  // rotate=false reads straight through (no un-rotation), still inverted.
  const straight = decodeNibbles(frame, false);
  expect(straight[0]).toBe(amp(0)); // encoded pos 0 nibble = 0
  expect(straight[1]).toBe(amp(1));
});

// --- buildKitBank + sampleBytesFromBank (splice primitives) ---

test("buildKitBank: offset table, names, budget, empty-slot sentinel; sampleBytesFromBank round-trips", () => {
  const s0 = new Uint8Array(16).fill(0x12);
  const s1 = new Uint8Array(32).fill(0x34);
  const bank = buildKitBank("MYKIT", [
    { name: "AAA", bytes: s0 },
    { name: "BBB", bytes: s1 },
  ]);
  expect(bank.length).toBe(BANK_SIZE);

  // A KitView over the bank (placed in a fresh ROM slot) reads it back.
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bank, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0, false);
  expect(kit.name()).toBe("MYKIT");
  expect(kit.sampleCount()).toBe(2);
  expect(kit.sampleName(0)).toBe("AAA");
  expect(kit.sampleName(1)).toBe("BBB");
  expect([...kit.rawSampleBytes(0)]).toEqual([...s0]);
  expect([...kit.rawSampleBytes(1)]).toEqual([...s1]);

  // sampleBytesFromBank pulls a sample straight out of a standalone bank (base 0).
  expect([...sampleBytesFromBank(bank, 0)]).toEqual([...s0]);
  expect([...sampleBytesFromBank(bank, 1)]).toEqual([...s1]);
  expect(sampleBytesFromBank(bank, 2).length).toBe(0); // unused slot

  // Empty-slot sentinel: slot 2's name[0] = 0, offset entry = 0.
  const nameOff = KIT_LOOKUP[0] * BANK_SIZE + 0x22 + 2 * 3;
  expect(rom[nameOff]).toBe(0);
});

test("kitSampleSpace + buildKitBank clip past the 0x3fa0 budget", () => {
  const big = { name: "BIG", bytes: new Uint8Array(0x3fa0 + 16).fill(0x11) };
  expect(kitSampleSpace([big])).toBe(0x3fa0 + 16); // over budget by one frame
  const bank = buildKitBank("K", [big]);
  const kit0 = sampleBytesFromBank(bank, 0);
  expect(kit0.length).toBe(0x3fa0); // clipped to the budget
});

// --- palette read + colour patch round-trip ---

test("unpackRgb555 / packRgb555: 5→8-bit expansion and word-level round-trip", () => {
  expect(unpackRgb555(0x7fff)).toEqual({ r: 255, g: 255, b: 255 });
  expect(unpackRgb555(0)).toEqual({ r: 0, g: 0, b: 0 });
  // RGB555 is lossy in 8-bit space, but every 15-bit word round-trips exactly through pack∘unpack.
  for (const w of [0x0000, 0x1234, 0x4a29, 0x7fff, 0x03e0]) expect(packRgb555(unpackRgb555(w))).toBe(w);
});

test("PaletteView: locates the block, decodes a colour, and patches it in place", () => {
  const rom = new Uint8Array(ROM_SIZE);
  // Put the block at bank 1, offset 0x100; the PALETTE_CHECK marker sits right AFTER it.
  const base = 1 * BANK_SIZE + 0x100;
  setU16le(rom, base, packRgb555({ r: 0xf8, g: 0, b: 0 })); // pal 0, set 0, colour 0 = red
  rom.set(PALETTE_CHECK, base + PALETTE_COUNT * PALETTE_SIZE); // marker after the 6×40 block
  expect(findPaletteBase(rom, PALETTE_COUNT)).toBe(base);

  const pal = new PaletteView(rom, base, 0);
  expect(pal.color(0, 0)).toEqual({ r: 255, g: 0, b: 0 }); // 5-bit 31 → 255
  pal.setColor(0, 0, { r: 0, g: 0xf8, b: 0 }); // → green
  expect(new PaletteView(rom, base, 0).color(0, 0)).toEqual({ r: 0, g: 255, b: 0 });
});

// --- font read + tile patch round-trip ---

test("FontView: locates the block, decodes a 2bpp tile, and patches it in place", () => {
  const rom = new Uint8Array(ROM_SIZE);
  const anchor = 30 * BANK_SIZE + 0x40;
  rom.set(FONT_HEADER_CHECK, anchor);
  const base = anchor; // header marker lands ON the base (addend 0)
  expect(findFontBase(rom)).toBe(base);

  const font = new FontView(rom, base, 0);
  // a fresh tile is all-zero
  expect(font.tile(0).every((p) => p === 0)).toBeTruthy();
  // set a checker-ish row: pixel (0,0)=3, (0,1)=1, (0,2)=2
  const px = new Array(64).fill(0);
  px[0] = 3;
  px[1] = 1;
  px[2] = 2;
  font.setTile(0, px);
  const re = new FontView(rom, base, 0).tile(0);
  expect(re[0]).toBe(3);
  expect(re[1]).toBe(1);
  expect(re[2]).toBe(2);
  expect(re[3]).toBe(0);
});

// --- names: grayscale landmark → palette count + font/palette names (bank 27) ---

test("names: findGrayscaleNames + paletteCount + palette/font name tables", () => {
  const rom = new Uint8Array(ROM_SIZE);
  const B = 27 * BANK_SIZE + 0x100; // match start (the 3-slot grayscale landmark = the font names)
  ascii(rom, B + 0, "FNT0"); // 4 non-zero + implicit NUL → matches the landmark pattern
  ascii(rom, B + 5, "FNT1");
  ascii(rom, B + 10, "FNT2");
  // grayBase = B+15; after it: 2 "extra" slots + 2 palette slots = 4 valid → paletteCount 4/2 = 2.
  ascii(rom, B + 15, "XTRA");
  ascii(rom, B + 20, "YTRA");
  ascii(rom, B + 25, "PALA"); // palette name 0 (grayBase + 5*count)
  ascii(rom, B + 30, "PALB"); // palette name 1
  rom[B + 15 + 4 * 5 + 4] = 0x01; // terminator: a non-zero 5th byte stops the count run

  expect(findGrayscaleNames(rom)).toBe(B + 15);
  expect(paletteCount(rom)).toBe(2);
  expect(paletteNames(rom, 2)).toEqual(["PALA", "PALB"]);
  expect(fontNames(rom)).toEqual(["FNT0", "FNT1", "FNT2"]);
  // No landmark → paletteCount falls back to PALETTE_COUNT, names empty.
  const blank = new Uint8Array(ROM_SIZE);
  expect(findGrayscaleNames(blank)).toBe(-1);
  expect(paletteCount(blank)).toBe(6); // PALETTE_COUNT fallback
  expect(fontNames(blank)).toEqual([]);
});

// --- LsdjRom facade ---

test("LsdjRom.rotatesSamples gates on the LSDj version (9.2.0+)", () => {
  const withTitle = (title: string): LsdjRom => {
    const rom = new Uint8Array(ROM_SIZE);
    ascii(rom, 0x134, title);
    return LsdjRom.fromBytes(rom);
  };
  expect(withTitle("LSDJ-V9.4.2").rotatesSamples).toBeTruthy(); // 9.4.2 ≥ 9.2 → rotates
  expect(withTitle("LSDJ-V9.2.0").rotatesSamples).toBeTruthy(); // exactly 9.2.0 → rotates
  expect(withTitle("LSDJ-V9.1.9").rotatesSamples).toBeFalsy(); // 9.1.9 < 9.2 → no rotation
  expect(withTitle("LSDJ-V6.9.0").rotatesSamples).toBeFalsy();
  expect(withTitle("NOT-LSDJ").rotatesSamples).toBeFalsy(); // unknown → no rotation
});

test("LsdjRom.fromBytes clones (patches never touch the caller's buffer) and parses the version", () => {
  const rom = romWithKit();
  ascii(rom, 0x134, "LSDJ-V9.4.2"); // GB title
  const lr = LsdjRom.fromBytes(rom);
  expect(lr.version?.raw).toBe("LSDJ-V9.4.2");
  lr.kit(0).setName("ZZZ");
  expect(lr.kit(0).name()).toBe("ZZZ"); // patched on the clone
  expect(new KitView(rom, 0).name()).toBe("KICKS"); // caller's buffer untouched
});

// --- name writers (bank 27) ---

test("setPaletteName / setFontName write the bank-27 table (round-trips through the readers)", () => {
  const rom = new Uint8Array(ROM_SIZE);
  const B = 27 * BANK_SIZE + 0x100;
  ascii(rom, B + 0, "FNT0");
  ascii(rom, B + 5, "FNT1");
  ascii(rom, B + 10, "FNT2");
  ascii(rom, B + 15, "XTRA");
  ascii(rom, B + 20, "YTRA");
  ascii(rom, B + 25, "PALA");
  ascii(rom, B + 30, "PALB");
  rom[B + 15 + 4 * 5 + 4] = 0x01; // count terminator → 2 palettes

  setPaletteName(rom, 1, 2, "cool"); // lowercase → uppercased, index 1
  setFontName(rom, 0, "zx");        // short → space-padded to 4
  expect(paletteNames(rom, 2)).toEqual(["PALA", "COOL"]);
  expect(fontNames(rom)).toEqual(["ZX", "FNT1", "FNT2"]);
  // out-of-range / missing-landmark are no-ops
  setPaletteName(rom, 5, 2, "NOPE");
  expect(paletteNames(rom, 2)).toEqual(["PALA", "COOL"]);
});

// --- palette raw + .lsdpal file round-trip ---

test("decodeLsdpal / encodeLsdpal: a .lsdpal ↔ structured colour-sets round-trips (word-exact)", () => {
  const file = new Uint8Array(4 + PALETTE_SIZE);
  ascii(file, 0, "NEON");
  for (let w = 0; w < 20; w++) {
    const word = (w * 1234 + 567) & 0x7fff; // a VALID 15-bit RGB555 word (bit 15 unused)
    setU16le(file, 4 + w * 2, word);
  }
  const decoded = decodeLsdpal(file)!;
  expect(decoded.name).toBe("NEON");
  expect(decoded.colorSets.length).toBe(5);
  expect(decoded.colorSets[0].colors.length).toBe(4);
  // RGB555 is lossy in 8-bit, but decode→encode of a real .lsdpal is byte-exact (pack∘unpack is word-exact).
  expect([...encodeLsdpal(decoded.name, decoded.colorSets)]).toEqual([...file]);
  expect(decodeLsdpal(new Uint8Array(10))).toBe(null); // wrong size
});

test("PaletteView.raw / setRaw round-trip the 40-byte palette body", () => {
  const rom = new Uint8Array(ROM_SIZE);
  const base = 1 * BANK_SIZE + 0x100;
  const body = new Uint8Array(PALETTE_SIZE);
  for (let i = 0; i < PALETTE_SIZE; i++) body[i] = (i * 7 + 1) & 0xff;
  const pal = new PaletteView(rom, base, 0);
  pal.setRaw(body);
  expect(new PaletteView(rom, base, 0).raw()).toEqual(body);
  pal.setRaw(new Uint8Array(10)); // wrong size → ignored
  expect(new PaletteView(rom, base, 0).raw()).toEqual(body);
});

// A synthetic ROM with a version title, a `count`-palette block + PALETTE_CHECK marker, and the bank-27
// names landmark sized to the same count — enough for the LsdjRom palette-file operations.
function romWithPalettes(count: number): Uint8Array {
  const rom = new Uint8Array(ROM_SIZE);
  ascii(rom, 0x134, "LSDJ-V9.4.2");
  const base = 1 * BANK_SIZE + 0x100;
  rom.set(PALETTE_CHECK, base + count * PALETTE_SIZE); // marker after the block
  // names landmark: 3 font slots, then `count` "extra" + `count` palette-name slots (2*count valid).
  const B = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3; i++) ascii(rom, B + i * 5, `FN${i}A`);
  for (let i = 0; i < 2 * count; i++) ascii(rom, B + 15 + i * 5, `PN${i}A`);
  rom[B + 15 + 2 * count * 5 + 4] = 0x01; // terminator
  return rom;
}

test("LsdjRom import/exportPaletteFile: 44-byte .lsdpal writes colours + name and round-trips", () => {
  const lr = LsdjRom.fromBytes(romWithPalettes(2));
  expect(lr.palettes().length).toBe(2);

  // Author a .lsdpal: "NEON" + a distinctive 40-byte body.
  const file = new Uint8Array(4 + PALETTE_SIZE);
  ascii(file, 0, "NEON");
  for (let i = 0; i < PALETTE_SIZE; i++) file[4 + i] = (i * 3 + 5) & 0xff;
  lr.importPaletteFile(1, file);

  expect(lr.palettes()[1].name).toBe("NEON");
  expect(lr.palettes()[1].raw()).toEqual(file.slice(4));
  // Export reproduces the file byte-for-byte.
  expect(lr.exportPaletteFile(1)).toEqual(file);
  // Wrong-size input throws; out-of-range index throws.
  expect(() => lr.importPaletteFile(1, new Uint8Array(10))).toThrow();
  expect(() => lr.importPaletteFile(9, file)).toThrow();
});

// --- fonts: tile ↔ image mapping, gfx tiles, variant regen ---

// A ROM with just a font block (FONT_HEADER_CHECK in bank 30, with room before it for the shared gfx
// block at base − 0x2E0). No names landmark (font names default to "") — fine for tile/image tests.
function romWithFonts(): { rom: Uint8Array; base: number } {
  const rom = new Uint8Array(ROM_SIZE);
  const base = 30 * BANK_SIZE + 0x400; // leaves 0x400 > 0x2E0 for the gfx block before it
  rom.set(FONT_HEADER_CHECK, base);
  return { rom, base };
}

// A recognisable 8×8 tile using only shades that survive the rgb→shade bucket round-trip (0/1/3).
function sampleTile(seed: number): number[] {
  const shades = [0, 1, 3];
  return Array.from({ length: 64 }, (_, i) => shades[(i + seed) % 3]);
}

test("font export/import round-trips main + gfx tiles through an RGBA image (extended layout)", () => {
  const { rom } = romWithFonts();
  const src = LsdjRom.fromBytes(rom);
  const font = src.fonts()[0];
  // Paint a few main tiles and gfx tiles with known patterns.
  font.setTile(3, sampleTile(0));
  font.setTile(FONT_TILE_COUNT - 1, sampleTile(1)); // last main tile
  font.setGfxTile(0, sampleTile(2));
  font.setGfxTile(FONT_GFX_TILE_COUNT - 1, sampleTile(0));

  const img = src.exportFontImage(0, true); // extended: 64×120
  expect(img.width).toBe(64);
  expect(img.height).toBe(Math.ceil((FONT_TILE_COUNT + FONT_GFX_TILE_COUNT) / 8) * 8);

  // Import into a fresh ROM and confirm the tiles survive.
  const dst = LsdjRom.fromBytes(romWithFonts().rom);
  dst.importFontImage(0, img);
  const f2 = dst.fonts()[0];
  expect(f2.tile(3)).toEqual(sampleTile(0));
  expect(f2.tile(FONT_TILE_COUNT - 1)).toEqual(sampleTile(1));
  expect(f2.gfxTile(0)).toEqual(sampleTile(2));
  expect(f2.gfxTile(FONT_GFX_TILE_COUNT - 1)).toEqual(sampleTile(0));
});

test("font import of a main-only image leaves the shared gfx block untouched", () => {
  const { rom } = romWithFonts();
  const lr = LsdjRom.fromBytes(rom);
  lr.fonts()[0].setGfxTile(0, sampleTile(1)); // pre-existing gfx content
  const mainOnly = lr.exportFontImage(0, false); // 64×72, no gfx rows
  expect(mainOnly.height).toBe(Math.ceil(FONT_TILE_COUNT / 8) * 8);

  const dst = LsdjRom.fromBytes(romWithFonts().rom);
  dst.fonts()[0].setGfxTile(0, sampleTile(1)); // same pre-existing gfx
  dst.importFontImage(0, mainOnly); // main-only → maxTiles = 71, gfx not written
  expect(dst.fonts()[0].gfxTile(0)).toEqual(sampleTile(1)); // unchanged
});

test("font import must be 64px wide and rejects a bad index", () => {
  const lr = LsdjRom.fromBytes(romWithFonts().rom);
  expect(() => lr.importFontImage(0, { width: 48, height: 72, rgba: new Uint8Array(48 * 72 * 4) })).toThrow();
  expect(() => lr.importFontImage(9, { width: 64, height: 72, rgba: new Uint8Array(64 * 72 * 4) })).toThrow();
});

test("regenerateVariants writes the inverted (+0x4D2) and shaded (+0x9A4) copies (lsdpatch formulas)", () => {
  const { rom, base } = romWithFonts();
  const lr = LsdjRom.fromBytes(rom);
  const font = lr.fonts()[0];
  font.setTile(2, sampleTile(0));
  font.regenerateVariants();
  // Recompute the tile-2 data offset the same way FontView does (base + 0*FONT_SIZE + header).
  const buf = lr.bytes();
  const tileOff = base + 130 + 2 * FONT_TILE_SIZE;
  for (let i = 0; i < FONT_TILE_SIZE; i += 2) {
    // inverted: swapped + complemented bitplane bytes
    expect(buf[tileOff + FONT_VARIANT_STRIDE + i]).toBe(~buf[tileOff + i + 1] & 0xff);
    expect(buf[tileOff + FONT_VARIANT_STRIDE + i + 1]).toBe(~buf[tileOff + i] & 0xff);
    // shaded: plane0 OR'd with the dither mask, plane1 copied
    expect(buf[tileOff + FONT_VARIANT_STRIDE * 2 + i]).toBe(buf[tileOff + i] | (i % 4 === 2 ? 0xaa : 0x55));
    expect(buf[tileOff + FONT_VARIANT_STRIDE * 2 + i + 1]).toBe(buf[tileOff + i + 1]);
  }
});

// --- kit .kit file round-trip ---

test("LsdjRom import/exportKitFile: a full 16 KB bank round-trips into a slot", () => {
  const lr = LsdjRom.fromBytes(romWithKit());
  const original = lr.exportKitFile(0);
  expect(original.length).toBe(BANK_SIZE);

  // Build a distinct valid bank and import it into slot 5.
  const bank = buildKitBank("NEWKIT", [{ name: "SN", bytes: Uint8Array.of(0x12, 0x34, 0x56, 0x78) }]);
  lr.importKitFile(5, bank);
  expect(lr.exportKitFile(5)).toEqual(bank);
  expect(lr.kit(5).name()).toBe("NEWKIT");
  // A non-bank-sized buffer throws.
  expect(() => lr.importKitFile(5, new Uint8Array(100))).toThrow();
});

test("LsdjRom eraseKit: empties a valid kit slot (the non-destructive delete)", () => {
  const lr = LsdjRom.fromBytes(romWithKit());
  const bank = buildKitBank("DOOMED", [{ name: "SN", bytes: Uint8Array.of(1, 2, 3, 4) }]);
  lr.importKitFile(7, bank);
  expect(lr.kit(7).valid).toBeTruthy();

  lr.eraseKit(7);
  expect(lr.kit(7).valid).toBe(false);
  expect(lr.kit(7).empty).toBeTruthy();
  expect(lr.kit(7).name()).toBe("");
});
