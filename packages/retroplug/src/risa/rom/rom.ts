// RisaRom — a pure-TS view/patcher over a risa (NES/MMC5) .nes image, mirroring ../../lsdj/rom/rom.ts.
// A .nes is mostly opaque program code, so this reads the asset sections (themes / fonts) out of the raw
// bytes and patches them IN PLACE, leaving everything else byte-identical. Construct with fromBytes (which
// clones, so the caller's buffer is never mutated); after patching, hand bytes() to writeFileAtomic — or,
// for a non-destructive override, feed bytes() through the risa-assets role's spec.romBytes channel.
//
// All asset OFFSETS are DERIVED from the iNES header (computeLayout) — nothing is hard-coded. The theme
// table + the kit-metadata mirror are located by a 6-byte magic scan; the CHR font slots + the 32 DPCM
// kit banks are position-deterministic (chrOffset / kitOffset + slot*0x2000).

import { isRisaRomHeader } from "../romDetect";
import { findMagicInRange } from "./find";
import {
  HEADER_SIZE,
  PRG_16K_SIZE,
  PRG_8K_SIZE,
  PRG_FIXED_SIZE,
  KIT_BANK_COUNT,
  KIT_BANK_SIZE,
  KIT_MAGIC,
  KIT_MAGIC_OFFSET,
  CHR_BANK_SIZE,
  THEME_META_MAGIC,
  THEME_COUNT,
  THEME_RECORD_SIZE,
  THEME_NAME_SIZE,
  KIT_META_MAGIC,
  KIT_META_TOTAL_SIZE,
  KIT_META_HINT_OFFSETS,
  KIT_META_NAMES_SIZE,
  KIT_META_SAMPLE_NAMES_SIZE,
  KIT_META_NAMES_STRIDE,
  KIT_META_SAMPLE_NAMES_STRIDE,
  KIT_META_SLOT_PRESENT_STRIDE,
} from "./constants";
import { decodeThemeFromRom } from "./theme";
import { bankToModel, deriveMetaFromBank, type KitModel } from "./kit";
import type { RisaTheme } from "./types";

interface Layout {
  kitOffset: number;
  fixedOffset: number;
  chrOffset: number;
  chrSize: number;
}

/** Derive the kit/fixed/CHR offsets from the 16-byte iNES header, or null if the header/size is unusable
 *  (too small for the 32 kit banks). Mirrors risa's rom.js computeLayout. */
function computeLayout(bytes: Uint8Array): Layout | null {
  if (bytes.length < HEADER_SIZE) return null;
  const prg16kBanks = bytes[4];
  const chrSize = bytes[5] * 0x2000;
  const prgSize = prg16kBanks * PRG_16K_SIZE;
  const prg8kBanks = prg16kBanks * 2;
  const lastPrgBank = prg8kBanks - 1;
  const kitFirstBank = lastPrgBank - KIT_BANK_COUNT;
  if (kitFirstBank < 16) return null;
  return {
    kitOffset: HEADER_SIZE + kitFirstBank * PRG_8K_SIZE,
    fixedOffset: HEADER_SIZE + lastPrgBank * PRG_8K_SIZE,
    chrOffset: HEADER_SIZE + prgSize,
    chrSize,
  };
}

/** True if a KIT_META_MAGIC candidate at `off` is valid: the magic matches AND the slot-present sub-table
 *  is all 0/1 (rejects a coincidental magic hit). Mirrors risa's isValidKitMetaCandidate. */
function isKitMetaCandidate(bytes: Uint8Array, off: number, start: number, end: number): boolean {
  if (off < start || off + KIT_META_TOTAL_SIZE > end) return false;
  for (let i = 0; i < KIT_META_MAGIC.length; i++) if (bytes[off + i] !== KIT_META_MAGIC[i]) return false;
  const presentBase = off + KIT_META_MAGIC.length + KIT_META_NAMES_SIZE + KIT_META_SAMPLE_NAMES_SIZE;
  for (let i = 0; i < 32 * KIT_META_SLOT_PRESENT_STRIDE; i++) {
    const v = bytes[presentBase + i];
    if (v !== 0 && v !== 1) return false;
  }
  return true;
}

/** Locate the kit-metadata mirror: try the hint offsets, then a bounded scan of [HEADER_SIZE, kitOffset).
 *  The scan is mandatory — in risa 2.2.1 the mirror sits past both hints (at 0x215EA). -1 when absent. */
function findKitMetaMagic(bytes: Uint8Array, kitOffset: number): number {
  const start = HEADER_SIZE;
  const end = Math.min(bytes.length, kitOffset);
  for (const off of KIT_META_HINT_OFFSETS) if (isKitMetaCandidate(bytes, off, start, end)) return off;
  const maxOff = end - KIT_META_TOTAL_SIZE;
  for (let off = start; off <= maxOff; off++) if (isKitMetaCandidate(bytes, off, start, end)) return off;
  return -1;
}

export class RisaRom {
  private readonly layout: Layout | null;
  private readonly themeMetaOffset: number; // -1 when absent
  private readonly kitMetaOffset: number; // -1 when absent
  private readonly headerOk: boolean;

  private constructor(private readonly rom: Uint8Array) {
    this.headerOk = rom.length >= HEADER_SIZE && isRisaRomHeader(rom.subarray(0, HEADER_SIZE));
    this.layout = computeLayout(rom);
    this.themeMetaOffset =
      this.layout != null ? findMagicInRange(rom, THEME_META_MAGIC, this.layout.fixedOffset, PRG_FIXED_SIZE) : -1;
    this.kitMetaOffset = this.layout != null ? findKitMetaMagic(rom, this.layout.kitOffset) : -1;
  }

  /** Wrap a ROM image (cloned, so patches never touch the caller's buffer). */
  static fromBytes(bytes: Uint8Array): RisaRom {
    return new RisaRom(bytes.slice());
  }

  /** True for a recognized risa image of the exact expected size (iNES fingerprint + 16 + PRG + CHR). */
  get isRisa(): boolean {
    if (!this.headerOk || this.layout == null) return false;
    const expected = HEADER_SIZE + this.rom[4] * PRG_16K_SIZE + this.rom[5] * 0x2000;
    return this.rom.length === expected;
  }

  /** The (possibly patched) image to write back / feed to romBytes. */
  bytes(): Uint8Array {
    return this.rom;
  }

  // --- Themes -----------------------------------------------------------------------------------------
  /** True if the theme table's magic was located in the fixed bank. */
  get hasThemes(): boolean {
    return this.themeMetaOffset >= 0;
  }
  get themeCount(): number {
    return THEME_COUNT;
  }

  /** The raw on-ROM bytes of theme `idx`: a 7-byte record + 4-byte name. Null if there's no theme table. */
  getTheme(idx: number): { recordBytes: Uint8Array; nameBytes: Uint8Array } | null {
    if (!this.hasThemes) return null;
    const recordBase = this.themeMetaOffset + THEME_META_MAGIC.length;
    const namesOff = recordBase + THEME_COUNT * THEME_RECORD_SIZE;
    return {
      recordBytes: this.rom.slice(recordBase + idx * THEME_RECORD_SIZE, recordBase + (idx + 1) * THEME_RECORD_SIZE),
      nameBytes: this.rom.slice(namesOff + idx * THEME_NAME_SIZE, namesOff + (idx + 1) * THEME_NAME_SIZE),
    };
  }

  /** Splice theme `idx`'s record (7 bytes) + name (4 bytes) in place. No-op if there's no theme table. */
  setTheme(idx: number, recordBytes: Uint8Array, nameBytes: Uint8Array): void {
    if (!this.hasThemes) return;
    const recordBase = this.themeMetaOffset + THEME_META_MAGIC.length;
    const namesOff = recordBase + THEME_COUNT * THEME_RECORD_SIZE;
    this.rom.set(recordBytes.subarray(0, THEME_RECORD_SIZE), recordBase + idx * THEME_RECORD_SIZE);
    this.rom.set(nameBytes.subarray(0, THEME_NAME_SIZE), namesOff + idx * THEME_NAME_SIZE);
  }

  /** The decoded themes, for a menu inventory (empty when there's no theme table). */
  themes(): { slot: number; theme: RisaTheme }[] {
    if (!this.hasThemes) return [];
    const out: { slot: number; theme: RisaTheme }[] = [];
    for (let i = 0; i < THEME_COUNT; i++) {
      const t = this.getTheme(i)!;
      out.push({ slot: i, theme: decodeThemeFromRom(t.recordBytes, t.nameBytes) });
    }
    return out;
  }

  // --- Fonts (CHR) ------------------------------------------------------------------------------------
  /** Number of 8 KB CHR font slots (chrSize / 0x2000). */
  get chrFontSlotCount(): number {
    return this.layout ? Math.max(1, Math.floor(this.layout.chrSize / CHR_BANK_SIZE)) : 0;
  }

  /** The raw 8 KB CHR bank for font slot `idx`, or null if there's no CHR region. */
  getChrFontSlot(idx: number): Uint8Array | null {
    if (!this.layout || this.chrFontSlotCount === 0) return null;
    const slot = Math.min(idx, this.chrFontSlotCount - 1);
    const off = this.layout.chrOffset + slot * CHR_BANK_SIZE;
    return this.rom.slice(off, off + CHR_BANK_SIZE);
  }

  /** Splice a whole 8 KB CHR bank into font slot `idx`. No-op if there's no CHR region. */
  setChrFontSlot(idx: number, bytes: Uint8Array): void {
    if (!this.layout || this.chrFontSlotCount === 0) return;
    const slot = Math.min(idx, this.chrFontSlotCount - 1);
    const off = this.layout.chrOffset + slot * CHR_BANK_SIZE;
    this.rom.set(bytes.subarray(0, CHR_BANK_SIZE), off);
  }

  /** The font slots, for a menu inventory (CHR has no name table — just slot indices). */
  fonts(): { slot: number }[] {
    return Array.from({ length: this.chrFontSlotCount }, (_v, slot) => ({ slot }));
  }

  // --- Kits (DPCM) ------------------------------------------------------------------------------------
  /** True if the resident kit-metadata mirror was located (needed to keep the on-device kit list in sync). */
  get hasKitMeta(): boolean {
    return this.kitMetaOffset >= 0;
  }

  private kitBankOffset(idx: number): number {
    return this.layout!.kitOffset + idx * KIT_BANK_SIZE;
  }

  /** The raw 8 KB kit bank at `idx` (0..31), or null if there's no kit region. */
  getKitBank(idx: number): Uint8Array | null {
    if (!this.layout || idx < 0 || idx >= KIT_BANK_COUNT) return null;
    const off = this.kitBankOffset(idx);
    return this.rom.slice(off, off + KIT_BANK_SIZE);
  }

  /** True if kit bank `idx` is populated (its 0xA5 magic is set). */
  isKitPopulated(idx: number): boolean {
    if (!this.layout || idx < 0 || idx >= KIT_BANK_COUNT) return false;
    return this.rom[this.kitBankOffset(idx) + KIT_MAGIC_OFFSET] === KIT_MAGIC;
  }

  kitCount(): number {
    let n = 0;
    for (let i = 0; i < KIT_BANK_COUNT; i++) if (this.isKitPopulated(i)) n++;
    return n;
  }

  /** The first unpopulated kit slot, or -1 if all 32 are full. */
  firstFreeKitIndex(): number {
    for (let i = 0; i < KIT_BANK_COUNT; i++) if (!this.isKitPopulated(i)) return i;
    return -1;
  }

  /** The populated kits, for a menu inventory (slot + decoded name). */
  kits(): { slot: number; name: string; model: KitModel }[] {
    if (!this.layout) return [];
    const out: { slot: number; name: string; model: KitModel }[] = [];
    for (let i = 0; i < KIT_BANK_COUNT; i++) {
      if (!this.isKitPopulated(i)) continue;
      const model = bankToModel(this.getKitBank(i)!);
      out.push({ slot: i, name: model.name || `Kit ${i}`, model });
    }
    return out;
  }

  /** Splice a whole 8 KB kit bank into slot `idx` — does NOT touch the mirror (use setKit for that). */
  setKitBank(idx: number, bank: Uint8Array): void {
    if (!this.layout || idx < 0 || idx >= KIT_BANK_COUNT) return;
    this.rom.set(bank.subarray(0, KIT_BANK_SIZE), this.kitBankOffset(idx));
  }

  /** Write the three metadata-mirror rows for kit `idx`. No-op when the mirror wasn't located. */
  updateKitMeta(idx: number, nameBytes: Uint8Array, sampleNamesBytes: Uint8Array, slotPresentBytes: Uint8Array): void {
    if (!this.hasKitMeta || idx < 0 || idx >= KIT_BANK_COUNT) return;
    const base = this.kitMetaOffset + KIT_META_MAGIC.length;
    this.rom.set(nameBytes.subarray(0, KIT_META_NAMES_STRIDE), base + idx * KIT_META_NAMES_STRIDE);
    this.rom.set(
      sampleNamesBytes.subarray(0, KIT_META_SAMPLE_NAMES_STRIDE),
      base + KIT_META_NAMES_SIZE + idx * KIT_META_SAMPLE_NAMES_STRIDE,
    );
    this.rom.set(
      slotPresentBytes.subarray(0, KIT_META_SLOT_PRESENT_STRIDE),
      base + KIT_META_NAMES_SIZE + KIT_META_SAMPLE_NAMES_SIZE + idx * KIT_META_SLOT_PRESENT_STRIDE,
    );
  }

  /** Splice a compiled 8 KB kit bank into slot `idx` AND update the resident mirror from the bank's own
   *  bytes — the dual-write, so the on-device kit list can't go stale. This is the splice callers want. */
  setKit(idx: number, bank: Uint8Array): void {
    this.setKitBank(idx, bank);
    const meta = deriveMetaFromBank(bank);
    this.updateKitMeta(idx, meta.nameBytes, meta.sampleNamesBytes, meta.slotPresentBytes);
  }

  /** Empty kit slot `idx`: zero the bank + clear its mirror row (the erase form). */
  clearKitBank(idx: number): void {
    if (!this.layout || idx < 0 || idx >= KIT_BANK_COUNT) return;
    this.setKitBank(idx, new Uint8Array(KIT_BANK_SIZE));
    this.updateKitMeta(
      idx,
      new Uint8Array(KIT_META_NAMES_STRIDE),
      new Uint8Array(KIT_META_SAMPLE_NAMES_STRIDE).fill(0x20),
      new Uint8Array(KIT_META_SLOT_PRESENT_STRIDE),
    );
  }
}
