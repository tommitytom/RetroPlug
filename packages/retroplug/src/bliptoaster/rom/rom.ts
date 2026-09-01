// BlipToasterRom — a pure-TS view/patcher over an BlipToaster (NROM) .nes image, the BlipToaster twin of
// ../../risa/rom/rom.ts. A .nes is mostly opaque code, so this reads/patches only the replaceable asset
// regions — the baked DMC kit bank and the CHR font — leaving everything else byte-identical. Construct
// with fromBytes (clones, so the caller's buffer is never mutated); after patching hand bytes() to the
// bliptoaster-assets role's spec.romBytes channel (the on-disk .nes is never rewritten).
//
// BlipToaster is NROM: a flat 32 KB PRG with one DPCM kit bank baked at CPU $C000 (PRG offset 0x4000; the KIT
// region in the bliptoaster repo's rom/nes.cfg) and the CHR after PRG. Unlike risa there is NO kit-metadata
// mirror (the ROM reads the kit index directly at boot), so setKit is a plain bank splice. The kit + font
// FORMATS are identical to risa's, so the pure codecs (bankToModel + the 8 KB bank / CHR layout) are reused.

import { isBlipToasterRomHeader } from "../romDetect";
import {
  HEADER_SIZE,
  PRG_16K_SIZE,
  KIT_BANK_SIZE,
  KIT_MAGIC,
  KIT_MAGIC_OFFSET,
  CHR_BANK_SIZE,
  THEME_META_MAGIC,
  THEME_RECORD_SIZE,
  THEME_NAME_SIZE,
  bankToModel,
  decodeThemeFromRom,
  findMagicInRange,
  type KitModel,
  type RisaTheme,
} from "../../risa/rom";

// BlipToaster bakes kit slot 0 at CPU $C000 = PRG offset 0x4000 (the KIT region in bliptoaster/rom/nes*.cfg). On a
// banking build the switchable window at $C000-$DFFF steps through 8K PRG banks 2..17, so slot k sits at
// 0x4000 + k*0x2000 (contiguous with slot 0) — the same arithmetic as risa's kitBankOffset.
const KIT_CPU_OFFSET = 0x4000;
// Banking builds (VRC6/VRC7/S5B/FME-7/N163) carry up to 16 switchable kit banks (the ROM's CC_DMC_BANK +
// nes-banked.cfg); NROM has no PRG banking, so its $C000 kit is fixed and it stays single-kit.
const BLIPTOASTER_MAX_KITS = 16;
// BlipToaster bakes a single risa-format theme (its UI is one 2-color screen). The 7-role record uses the
// same theme.ts codec as risa; the table lives in RODATA (the code region, before the kit).
const BLIPTOASTER_THEME_COUNT = 1;

interface Layout {
  kitOffset: number;
  chrOffset: number;
  chrSize: number;
}

/** Derive the kit/CHR offsets from the 16-byte iNES header, or null if the header/size is unusable. */
function computeLayout(bytes: Uint8Array): Layout | null {
  if (bytes.length < HEADER_SIZE) return null;
  const prgSize = bytes[4] * PRG_16K_SIZE;
  const chrSize = bytes[5] * CHR_BANK_SIZE;
  const kitOffset = HEADER_SIZE + KIT_CPU_OFFSET;
  if (kitOffset + KIT_BANK_SIZE > HEADER_SIZE + prgSize) return null; // slot 0 must fit inside PRG
  return { kitOffset, chrOffset: HEADER_SIZE + prgSize, chrSize };
}

/** The iNES mapper number (low nibble in byte 6, high nibble in byte 7, NES 2.0 hi bits in byte 8). */
function readMapper(bytes: Uint8Array): number {
  return (bytes[6] >> 4) | (bytes[7] & 0xf0) | ((bytes[8] & 0x0f) << 8);
}

/** How many DMC kit banks this ROM can switch among: 1 on NROM (mapper 0 — fixed $C000, no banking), else
 *  the number of 8K kit banks the PRG holds (all banks minus the 2 code banks + 1 fixed reset/vectors bank),
 *  capped at 16 to match the ROM's CC_DMC_BANK range. Mapper-agnostic among the banking builds. */
function computeKitCapacity(bytes: Uint8Array): number {
  if (bytes.length < HEADER_SIZE) return 1;
  if (readMapper(bytes) === 0) return 1;
  const prg8kBanks = bytes[4] * 2;
  const kitBanks = prg8kBanks - 2 /* code */ - 1 /* fixed reset/vectors */;
  return Math.max(1, Math.min(BLIPTOASTER_MAX_KITS, kitBanks));
}

export class BlipToasterRom {
  private readonly layout: Layout | null;
  private readonly markerOk: boolean;
  private readonly themeMetaOffset: number; // -1 when absent
  private readonly kitCapacity: number; // switchable kit banks (1 on NROM, up to 16 on a banking build)

  private constructor(private readonly rom: Uint8Array) {
    this.markerOk = isBlipToasterRomHeader(rom);
    this.layout = computeLayout(rom);
    this.kitCapacity = computeKitCapacity(rom);
    // Locate the risa-format theme table by its magic, scanning the code region ($8000-$BFFF, before
    // the kit bank) so a coincidental magic in the DPCM bytes can't match.
    this.themeMetaOffset =
      this.layout != null ? findMagicInRange(rom, THEME_META_MAGIC, HEADER_SIZE, KIT_CPU_OFFSET) : -1;
  }

  /** Wrap a ROM image (cloned, so patches never touch the caller's buffer). */
  static fromBytes(bytes: Uint8Array): BlipToasterRom {
    return new BlipToasterRom(bytes.slice());
  }

  /** True for a recognized BlipToaster image: the marker is present, the layout derives, and the file is big
   *  enough for its regions. NOT an exact-size check — the on-cart CHR region can exceed the header's
   *  declared CHR size (BlipToaster reserves 16 KB CHR but declares one 8 KB bank). */
  get isBlipToaster(): boolean {
    if (!this.markerOk || this.layout == null) return false;
    return this.rom.length >= this.layout.chrOffset + this.layout.chrSize;
  }

  /** The (possibly patched) image to feed to romBytes. */
  bytes(): Uint8Array {
    return this.rom;
  }

  // --- Themes (NES palette) — one risa-format theme, applied to the bg/text colors at boot -----------
  /** True if the theme table's magic was located in the code region. */
  get hasThemes(): boolean {
    return this.themeMetaOffset >= 0;
  }
  get themeCount(): number {
    return BLIPTOASTER_THEME_COUNT;
  }

  /** The raw on-ROM bytes of theme `idx`: a 7-byte record + 4-byte name. Null if there's no theme table. */
  getTheme(idx: number): { recordBytes: Uint8Array; nameBytes: Uint8Array } | null {
    if (!this.hasThemes) return null;
    const recordBase = this.themeMetaOffset + THEME_META_MAGIC.length;
    const namesOff = recordBase + BLIPTOASTER_THEME_COUNT * THEME_RECORD_SIZE;
    return {
      recordBytes: this.rom.slice(recordBase + idx * THEME_RECORD_SIZE, recordBase + (idx + 1) * THEME_RECORD_SIZE),
      nameBytes: this.rom.slice(namesOff + idx * THEME_NAME_SIZE, namesOff + (idx + 1) * THEME_NAME_SIZE),
    };
  }

  /** Splice theme `idx`'s record (7 bytes) + name (4 bytes) in place. No-op if there's no theme table. */
  setTheme(idx: number, recordBytes: Uint8Array, nameBytes: Uint8Array): void {
    if (!this.hasThemes) return;
    const recordBase = this.themeMetaOffset + THEME_META_MAGIC.length;
    const namesOff = recordBase + BLIPTOASTER_THEME_COUNT * THEME_RECORD_SIZE;
    this.rom.set(recordBytes.subarray(0, THEME_RECORD_SIZE), recordBase + idx * THEME_RECORD_SIZE);
    this.rom.set(nameBytes.subarray(0, THEME_NAME_SIZE), namesOff + idx * THEME_NAME_SIZE);
  }

  /** The decoded themes, for a menu inventory (empty when there's no theme table). */
  themes(): { slot: number; theme: RisaTheme }[] {
    if (!this.hasThemes) return [];
    const out: { slot: number; theme: RisaTheme }[] = [];
    for (let i = 0; i < BLIPTOASTER_THEME_COUNT; i++) {
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

  // --- Kits (DPCM) — 1 bank on NROM, up to 16 switchable banks on a banking build, no metadata mirror ---
  /** Number of switchable kit banks: 1 on NROM (fixed $C000), up to 16 on a banking build (derived from
   *  the iNES mapper + PRG geometry). Bounds every kit accessor and the assets menu's Add.../slot count. */
  kitBankCapacity(): number {
    return this.kitCapacity;
  }

  private kitBankOffset(idx: number): number {
    return this.layout!.kitOffset + idx * KIT_BANK_SIZE;
  }

  /** The raw 8 KB kit bank at `idx`, or null if out of range / no kit region. */
  getKitBank(idx: number): Uint8Array | null {
    if (!this.layout || idx < 0 || idx >= this.kitCapacity) return null;
    const off = this.kitBankOffset(idx);
    return this.rom.slice(off, off + KIT_BANK_SIZE);
  }

  /** True if kit bank `idx` is populated (its 0xA5 magic is set). */
  isKitPopulated(idx: number): boolean {
    if (!this.layout || idx < 0 || idx >= this.kitCapacity) return false;
    return this.rom[this.kitBankOffset(idx) + KIT_MAGIC_OFFSET] === KIT_MAGIC;
  }

  kitCount(): number {
    let n = 0;
    for (let i = 0; i < this.kitCapacity; i++) if (this.isKitPopulated(i)) n++;
    return n;
  }

  /** The first unpopulated kit slot within capacity (for Add...), or -1 when all slots are full. */
  firstFreeKitIndex(): number {
    for (let i = 0; i < this.kitCapacity; i++) if (!this.isKitPopulated(i)) return i;
    return -1;
  }

  /** The populated kits, for a menu inventory (slot + decoded name). */
  kits(): { slot: number; name: string; model: KitModel }[] {
    if (!this.layout) return [];
    const out: { slot: number; name: string; model: KitModel }[] = [];
    for (let i = 0; i < this.kitCapacity; i++) {
      if (!this.isKitPopulated(i)) continue;
      const model = bankToModel(this.getKitBank(i)!);
      out.push({ slot: i, name: model.name || `Kit ${i}`, model });
    }
    return out;
  }

  /** Splice a whole 8 KB kit bank into slot `idx`. */
  setKitBank(idx: number, bank: Uint8Array): void {
    if (!this.layout || idx < 0 || idx >= this.kitCapacity) return;
    this.rom.set(bank.subarray(0, KIT_BANK_SIZE), this.kitBankOffset(idx));
  }

  /** Splice a compiled 8 KB kit bank into slot `idx`. BlipToaster reads the kit index directly at boot, so
   *  there's no resident metadata mirror to update (unlike risa's setKit) — this is just the bank splice. */
  setKit(idx: number, bank: Uint8Array): void {
    this.setKitBank(idx, bank);
  }

  /** Empty kit slot `idx` (the erase form): zero the bank, which clears its 0xA5 populated marker. */
  clearKitBank(idx: number): void {
    this.setKitBank(idx, new Uint8Array(KIT_BANK_SIZE));
  }
}
