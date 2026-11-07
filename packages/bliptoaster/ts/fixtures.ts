// Synthetic BlipToaster ROM images: a full-size flat NROM cart and the banking (mapper 69, FME-7) shape the shipped
// builds use, each carrying the "bliptoaster" marker at the ROM head, the interleaved theme table + settings block and
// a populated DMC kit + CHR fonts. A fixture is the cart's own knowledge (its SIG block, its table stride, its bank
// geometry), so it lives here and the shell's test tree re-exports it for its store, menu and registry tests.

const CHR_BANK_BYTES = 0x2000; // one font slot = one 8 KB CHR bank

// The real ROM's 16 theme names, in table order (`g_themeTable`, the bliptoaster repo's src/core/sys.c). The
// fixtures carry them verbatim so a menu / inventory test asserts the labels a user actually reads.
const BLIPTOASTER_THEME_NAMES = [
  "DFLT", "DARK", "NEON", "LITE", "CRT ", "ICE ", "FIRE", "GB  ",
  "AQUA", "MONO", "PLSM", "MTRX", "FOG ", "SUN ", "MOON", "AMBR",
];

/** The BlipToaster theme table as the ROM bakes it: the `\xA5\x5ATHME` magic, then `count` INTERLEAVED entries
 *  of [7 role bytes + a 4-char name] - 11 bytes each, the name right behind its own record. That stride is
 *  BlipToaster's own and is NOT risa's layout (16 records, then 16 names), so a fixture written risa-style
 *  reads only entry 0 correctly. Role 0 (bg) is per-slot so a test can tell the entries apart; slot 0 is the
 *  real DFLT's 0x0D/0x30. `count` below 16 is for the table-bound check. */
export function blipToasterThemeTable(count = 16): Uint8Array {
  const ENTRY = 7 + 4;
  const out = new Uint8Array(6 + count * ENTRY);
  out.set([0xa5, 0x5a, 0x54, 0x48, 0x4d, 0x45], 0); // THEME_META_MAGIC
  for (let i = 0; i < count; i++) {
    const off = 6 + i * ENTRY;
    out.set([0x0d + i, 0x30, 0x00, 0x10, 0x30, 0x21, 0x11], off); // bg, normal, shaded, alternate, status, cursor, selection
    for (const [k, c] of Array.from(BLIPTOASTER_THEME_NAMES[i % 16]).entries()) out[off + 7 + k] = c.charCodeAt(0);
  }
  return out;
}

/** The baked settings block as the ROM bakes it (src/core/settings.c): the `\xA5\x5ASETT` magic, the format
 *  byte, then the rig fields; +8 and +13..15 stay reserved 0xFF. Defaults are the power-on ones the ROM bakes -
 *  all zero except `ppu`, which is 1 (the screen draws), i.e. the block of a ROM straight out of the build. */
export function blipToasterSettingsBlock(
  fields: { version?: number; baseChannel?: number; ppu?: number; velCurve?: number; theme?: number; font?: number } = {},
): Uint8Array {
  const out = new Uint8Array(16);
  out.set([0xa5, 0x5a, 0x53, 0x45, 0x54, 0x54], 0); // "\xA5\x5ASETT"
  out[6] = fields.version ?? 1;
  out[7] = fields.baseChannel ?? 0;
  out[8] = 0xff; // reserved - held the default kit slot before the kit became CC-only
  out[9] = fields.ppu ?? 1; // the one non-zero power-on default: PPU enabled
  out[10] = fields.velCurve ?? 0;
  out[11] = fields.theme ?? 0;
  out[12] = fields.font ?? 0;
  out.fill(0xff, 13);
  return out;
}

// The theme table + settings block, spliced at `at` exactly as the ROM lays them out: ADJACENT, the settings
// magic starting the byte after the table's last entry. That adjacency is load-bearing for the fixtures - it
// is the case that bounds the theme count (countThemes stops on the $A5 it cannot read as a palette index),
// so a fixture that left slack there would not exercise it.
function splicePrgBlocks(rom: Uint8Array, at: number, themeCount = 16): void {
  const table = blipToasterThemeTable(themeCount);
  rom.set(table, at);
  rom.set(blipToasterSettingsBlock(), at + table.length);
}

/** A full-size synthetic BlipToaster ROM (16 + 32 KB PRG + 8 KB CHR = 0xA010, NROM) carrying the "bliptoaster"
 *  marker at the ROM head + the theme table + settings block + one populated DMC kit at $C000 (PRG offset
 *  0x4000) + a distinct CHR font region. The flat single-kit, single-font cart: every SHIPPED build banks
 *  (see blipToasterMultiKitRom), so this one exists to hold the NROM read path - capacity 1, fonts 1 - which
 *  BlipToasterRom still derives from the header rather than assuming. */
export function blipToasterRom(): Uint8Array {
  const PRG = 0x8000; // 2 × 16 KB (NROM, 32 KB)
  const CHR = 0x2000; // 1 × 8 KB
  const rom = new Uint8Array(0x10 + PRG + CHR);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1A"
  rom[4] = 0x02; // 2 × 16 KB PRG
  rom[5] = 0x01; // 1 × 8 KB CHR
  rom[6] = 0x01; // vertical mirroring, mapper 0 (NROM)

  // The BlipToaster SIG block at the ROM head ($8000 = file offset 0x10), as the SIG segment bakes it:
  // the "bliptoaster" marker (also the display name) + semver(0.1.0).
  const MARK = "bliptoaster";
  for (let i = 0; i < MARK.length; i++) rom[0x10 + i] = MARK.charCodeAt(i);
  let p = 0x10 + MARK.length;
  rom[p++] = 0x00; rom[p++] = 0x01; rom[p++] = 0x00; // semantic version 0.1.0

  // The theme table + settings block in the code region (before the kit at 0x4010), at a fixed offset for
  // tests - the code region is exactly where the ROM puts them, and where both magic scans look.
  splicePrgBlocks(rom, 0x100);

  // One populated DMC kit at $C000 (PRG offset 0x4000): a "TEST" kit with a "KIK" sample-0 name, one index
  // entry, the other 15 empty (0xFF), and the 0xA5 populated magic - the same 8 KB bank layout risa uses.
  const kitOffset = 0x10 + 0x4000;
  rom[kitOffset + 0] = 0xff; // slot-0 DPCM data (1 byte)
  rom.fill(0x20, kitOffset + 0x1ed0, kitOffset + 0x1ed0 + 16 * 3); // sample-names region = spaces
  for (const [i, c] of Array.from("TEST").entries()) rom[kitOffset + 0x1ec0 + i] = c.charCodeAt(0); // kit name
  for (const [i, c] of Array.from("KIK").entries()) rom[kitOffset + 0x1ed0 + i] = c.charCodeAt(0); // sample 0 name
  rom.set([0, 0, 12, 0], kitOffset + 0x1f00); // index[0] = [addr 0, lenReg 0, rate 12, flags 0]
  for (let slot = 1; slot < 16; slot++) rom[kitOffset + 0x1f00 + slot * 4] = 0xff; // empty index entries
  rom[kitOffset + 0x1f40] = 0xa5; // populated magic

  // CHR: one 8 KB font slot, filled with a distinct pattern (for font read/patch tests).
  const chrOffset = 0x10 + PRG;
  for (let b = 0; b < CHR; b++) rom[chrOffset + b] = (b * 7 + 3) & 0xff;
  return rom;
}

/** A full-size synthetic BlipToaster BANKING ROM - the SHIPPED shape: 16 + 256 KB PRG + 32 KB CHR, mapper 69
 *  (FME-7), carrying the "bliptoaster" marker + the 16-entry theme table + the settings block + one populated
 *  DMC kit in slot 0, with slots 1..15 reserved (unpopulated). The banking header (PRG 16 × 16 KB, mapper != 0)
 *  drives BlipToasterRom.kitBankCapacity() to 16, so the assets menu goes addable/16-slot; the 4 declared CHR
 *  banks give it the real build's 4 fonts. Kit slot k sits at PRG offset 0x4000 + k*0x2000 (banks 2..17), and
 *  the geometry throughout is the real cfg/nes-banked.cfg's. */
export function blipToasterMultiKitRom(): Uint8Array {
  const PRG = 0x40000; // 16 × 16 KB (256 KB banking build)
  const CHR = 0x8000; // 4 × 8 KB - one font each
  const rom = new Uint8Array(0x10 + PRG + CHR);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1A"
  rom[4] = 0x10; // 16 × 16 KB PRG (256 KB)
  rom[5] = 0x04; // 4 × 8 KB CHR (32 KB)
  rom[6] = 0x51; // vertical mirroring, mapper 69 low nibble (Sunsoft 5B / FME-7)
  rom[7] = 0x40; // mapper 69 high nibble

  const MARK = "bliptoaster"; // marker (also the display name)
  for (let i = 0; i < MARK.length; i++) rom[0x10 + i] = MARK.charCodeAt(i);
  let p = 0x10 + MARK.length;
  rom[p++] = 0x00; rom[p++] = 0x01; rom[p++] = 0x00; // semantic version 0.1.0

  // Theme table + settings block in the code region (same placement as blipToasterRom()).
  splicePrgBlocks(rom, 0x100);

  // Kit slot 0 populated at bank 2 (PRG offset 0x4000). Slots 1..15 (banks 3..17) stay unpopulated (their
  // magic byte is 0x00 ≠ 0xA5), matching the real ROM's reserved-fill banks.
  const kitOffset = 0x10 + 0x4000;
  rom[kitOffset + 0] = 0xff; // slot-0 DPCM data (1 byte)
  rom.fill(0x20, kitOffset + 0x1ed0, kitOffset + 0x1ed0 + 16 * 3); // sample-names region = spaces
  for (const [i, c] of Array.from("TEST").entries()) rom[kitOffset + 0x1ec0 + i] = c.charCodeAt(0); // kit name
  for (const [i, c] of Array.from("KIK").entries()) rom[kitOffset + 0x1ed0 + i] = c.charCodeAt(0); // sample 0 name
  rom.set([0, 0, 12, 0], kitOffset + 0x1f00); // index[0] = [addr 0, lenReg 0, rate 12, flags 0]
  for (let slot = 1; slot < 16; slot++) rom[kitOffset + 0x1f00 + slot * 4] = 0xff; // empty index entries
  rom[kitOffset + 0x1f40] = 0xa5; // populated magic (slot 0 only)

  // CHR: 4 × 8 KB font slots, each a distinct pattern whose FIRST byte is its own slot index - so a test that
  // reads or splices a font can say which bank it got rather than just "some 8 KB".
  const chrOffset = 0x10 + PRG;
  for (let b = 0; b < CHR; b++) rom[chrOffset + b] = (b * 7 + 3) & 0xff;
  for (let slot = 0; slot * CHR_BANK_BYTES < CHR; slot++) rom[chrOffset + slot * CHR_BANK_BYTES] = slot;
  return rom;
}
