// EverMidiRom — a pure-TS view/patcher over an EverMIDI (NROM) .nes image, the EverMIDI twin of
// ../../risa/rom/rom.ts. A .nes is mostly opaque code, so this reads/patches only the replaceable asset
// regions — the baked DMC kit bank and the CHR font — leaving everything else byte-identical. Construct
// with fromBytes (clones, so the caller's buffer is never mutated); after patching hand bytes() to the
// evermidi-assets role's spec.romBytes channel (the on-disk .nes is never rewritten).
//
// EverMIDI is NROM: a flat 32 KB PRG with one DPCM kit bank baked at CPU $C000 (PRG offset 0x4000; the KIT
// region in the evermidi repo's rom/nes.cfg) and the CHR after PRG. Unlike risa there is NO kit-metadata
// mirror (the ROM reads the kit index directly at boot), so setKit is a plain bank splice. The kit + font
// FORMATS are identical to risa's, so the pure codecs (bankToModel + the 8 KB bank / CHR layout) are reused.

import { isEverMidiRomHeader } from "../romDetect";
import {
  HEADER_SIZE,
  PRG_16K_SIZE,
  KIT_BANK_SIZE,
  KIT_MAGIC,
  KIT_MAGIC_OFFSET,
  CHR_BANK_SIZE,
  bankToModel,
  type KitModel,
} from "../../risa/rom";

// EverMIDI bakes one DPCM kit bank at CPU $C000 = PRG offset 0x4000 (the KIT region in evermidi/rom/nes.cfg).
const KIT_CPU_OFFSET = 0x4000;
const KIT_COUNT = 1; // one baked kit on NROM (multi-kit needs the expansion-mapper CC_DMC_BANK ROM work)

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
  if (kitOffset + KIT_BANK_SIZE > HEADER_SIZE + prgSize) return null; // the kit must fit inside PRG
  return { kitOffset, chrOffset: HEADER_SIZE + prgSize, chrSize };
}

export class EverMidiRom {
  private readonly layout: Layout | null;
  private readonly markerOk: boolean;

  private constructor(private readonly rom: Uint8Array) {
    this.markerOk = isEverMidiRomHeader(rom);
    this.layout = computeLayout(rom);
  }

  /** Wrap a ROM image (cloned, so patches never touch the caller's buffer). */
  static fromBytes(bytes: Uint8Array): EverMidiRom {
    return new EverMidiRom(bytes.slice());
  }

  /** True for a recognized EverMIDI image: the marker is present, the layout derives, and the file is big
   *  enough for its regions. NOT an exact-size check — the on-cart CHR region can exceed the header's
   *  declared CHR size (EverMIDI reserves 16 KB CHR but declares one 8 KB bank). */
  get isEverMidi(): boolean {
    if (!this.markerOk || this.layout == null) return false;
    return this.rom.length >= this.layout.chrOffset + this.layout.chrSize;
  }

  /** The (possibly patched) image to feed to romBytes. */
  bytes(): Uint8Array {
    return this.rom;
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

  // --- Kits (DPCM) — one bank on NROM, no metadata mirror ---------------------------------------------
  private kitBankOffset(idx: number): number {
    return this.layout!.kitOffset + idx * KIT_BANK_SIZE;
  }

  /** The raw 8 KB kit bank at `idx`, or null if out of range / no kit region. */
  getKitBank(idx: number): Uint8Array | null {
    if (!this.layout || idx < 0 || idx >= KIT_COUNT) return null;
    const off = this.kitBankOffset(idx);
    return this.rom.slice(off, off + KIT_BANK_SIZE);
  }

  /** True if kit bank `idx` is populated (its 0xA5 magic is set). */
  isKitPopulated(idx: number): boolean {
    if (!this.layout || idx < 0 || idx >= KIT_COUNT) return false;
    return this.rom[this.kitBankOffset(idx) + KIT_MAGIC_OFFSET] === KIT_MAGIC;
  }

  kitCount(): number {
    let n = 0;
    for (let i = 0; i < KIT_COUNT; i++) if (this.isKitPopulated(i)) n++;
    return n;
  }

  /** The populated kits, for a menu inventory (slot + decoded name). */
  kits(): { slot: number; name: string; model: KitModel }[] {
    if (!this.layout) return [];
    const out: { slot: number; name: string; model: KitModel }[] = [];
    for (let i = 0; i < KIT_COUNT; i++) {
      if (!this.isKitPopulated(i)) continue;
      const model = bankToModel(this.getKitBank(i)!);
      out.push({ slot: i, name: model.name || `Kit ${i}`, model });
    }
    return out;
  }

  /** Splice a whole 8 KB kit bank into slot `idx`. */
  setKitBank(idx: number, bank: Uint8Array): void {
    if (!this.layout || idx < 0 || idx >= KIT_COUNT) return;
    this.rom.set(bank.subarray(0, KIT_BANK_SIZE), this.kitBankOffset(idx));
  }

  /** Splice a compiled 8 KB kit bank into slot `idx`. EverMIDI reads the kit index directly at boot, so
   *  there's no resident metadata mirror to update (unlike risa's setKit) — this is just the bank splice. */
  setKit(idx: number, bank: Uint8Array): void {
    this.setKitBank(idx, bank);
  }

  /** Empty kit slot `idx` (the erase form): zero the bank, which clears its 0xA5 populated marker. */
  clearKitBank(idx: number): void {
    this.setKitBank(idx, new Uint8Array(KIT_BANK_SIZE));
  }
}
