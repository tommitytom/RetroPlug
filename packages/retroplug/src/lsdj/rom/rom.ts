// LsdjRom — the top-level view/patcher over a Game Boy LSDj ROM image. Unlike the .sav codec (a full
// model round-trip), a .gb is mostly opaque program code, so this reads the asset sections (kits /
// palettes / fonts) out of the raw bytes and patches them IN PLACE, leaving everything else
// byte-identical. Construct with fromBytes (which clones, so the caller's buffer is never mutated);
// after patching, hand bytes() to backend.writeFileAtomic to save a new .gb.
import type { LsdjVersion } from "../runtime/types";
import type { RomImage } from "./types";
import { identifyLsdj } from "../runtime/identify";
import { KitView } from "./kit";
import { PaletteView, findPaletteBase } from "./palette";
import { FontView, findFontBase } from "./font";
import { paletteCount, paletteNames, fontNames, setPaletteName } from "./names";
import {
  BANK_SIZE,
  FONT_COUNT,
  FONT_GFX_TILE_COUNT,
  FONT_LUM_GREY,
  FONT_LUM_WHITE,
  FONT_SHADE_RGB,
  FONT_TILE_COUNT,
  FONT_TILES_X,
  KIT_COUNT,
  KIT_EMPTY_MAGIC,
  KIT_LOOKUP,
  KIT_NAME_OFFSET,
  KIT_NAME_SIZE,
  PALETTE_NAME_SIZE,
  PALETTE_SIZE,
  ROM_SIZE,
  ROTATION_MIN_MAJOR,
  ROTATION_MIN_MINOR,
} from "./constants";

const FONT_IMAGE_WIDTH = FONT_TILES_X * 8; // 64px — the fixed font image width (8 tiles)

// A single font PNG pixel's RGBA → its 2bpp shade (0/1/3), bucketed by luminance max(r,g,b) exactly as
// lsdpatch loadImageData does (white → 0, grey → 1, black → 3; value 2 is never produced).
function shadeFromRgb(r: number, g: number, b: number): number {
  const lum = Math.max(r, g, b);
  return lum >= FONT_LUM_WHITE ? 0 : lum >= FONT_LUM_GREY ? 1 : 3;
}

// A .lsdpal file = 4 ASCII name chars + the 40-byte palette body (see palette.ts). The name in the file
// carries no NUL; it's space-padded to 4.
const LSDPAL_NAME_LEN = PALETTE_NAME_SIZE - 1; // 4
const LSDPAL_SIZE = LSDPAL_NAME_LEN + PALETTE_SIZE; // 44

// LSDj 9.2.0+ stores kit samples with the per-frame rotation (see kit.ts decodeNibbles). Gate on the ROM
// version; unknown/older versions decode + encode un-rotated.
function versionRotatesSamples(v: LsdjVersion | null): boolean {
  if (!v) return false;
  return v.major > ROTATION_MIN_MAJOR || (v.major === ROTATION_MIN_MAJOR && v.minor >= ROTATION_MIN_MINOR);
}

export class LsdjRom {
  readonly version: LsdjVersion | null;
  /** Whether this ROM's LSDj version (9.2.0+) uses the per-frame sample rotation — for both decoding its
   *  kits (kit.ts) and encoding imported samples to match (pass to compileKit's `rotate`). */
  readonly rotatesSamples: boolean;

  private constructor(private readonly rom: Uint8Array) {
    this.version = identifyLsdj(rom);
    this.rotatesSamples = versionRotatesSamples(this.version);
  }

  /** Wrap a ROM image (cloned, so patches never touch the caller's buffer). */
  static fromBytes(bytes: Uint8Array): LsdjRom {
    return new LsdjRom(bytes.slice());
  }

  /** True when the image is a recognised LSDj build and the expected size. */
  get isLsdj(): boolean {
    return this.version != null && this.rom.length === ROM_SIZE;
  }

  /** The (possibly patched) ROM image to write back. */
  bytes(): Uint8Array {
    return this.rom;
  }

  // --- kits ---
  kit(index: number): KitView {
    return new KitView(this.rom, index, this.rotatesSamples);
  }
  kits(): KitView[] {
    return Array.from({ length: KIT_COUNT }, (_, i) => new KitView(this.rom, i, this.rotatesSamples));
  }

  /** Overwrite a kit slot with a whole 16 KB bank (e.g. from compileKit / buildKitBank). */
  setKitBank(index: number, bank: Uint8Array): void {
    if (index < 0 || index >= KIT_COUNT || bank.length !== BANK_SIZE) return;
    this.rom.set(bank, KIT_LOOKUP[index] * BANK_SIZE);
  }

  // --- kit files (.kit = a full 16 KB bank, byte-identical to the in-ROM bank) ---
  /** The kit slot's whole 16 KB bank — the body of a `.kit` file. */
  exportKitFile(index: number): Uint8Array {
    if (index < 0 || index >= KIT_COUNT) return new Uint8Array(0);
    const off = KIT_LOOKUP[index] * BANK_SIZE;
    return this.rom.slice(off, off + BANK_SIZE);
  }
  /** Import a `.kit` file (a raw 16 KB bank) into a slot. Throws on a wrong size (not a kit bank). The
   *  bank is copied verbatim — its samples keep whatever rotation they were saved with (as lsdpatch does;
   *  a `.kit` made for LSDj ≥ 9.2.0 imported into an older ROM would be frame-rotated). */
  importKitFile(index: number, bank: Uint8Array): void {
    if (bank.length !== BANK_SIZE) throw new Error(`not a kit bank: ${bank.length} bytes (expected ${BANK_SIZE})`);
    this.setKitBank(index, bank);
  }
  /** Mark a kit slot empty — LSDj's "no kit here" state (the non-destructive delete). Writes the empty
   *  marker over the first offset entry and clears the name; leftover sample bytes are ignored by LSDj. */
  eraseKit(index: number): void {
    if (index < 0 || index >= KIT_COUNT) return;
    const off = KIT_LOOKUP[index] * BANK_SIZE;
    this.rom[off] = KIT_EMPTY_MAGIC & 0xff;
    this.rom[off + 1] = (KIT_EMPTY_MAGIC >> 8) & 0xff;
    for (let i = 0; i < KIT_NAME_SIZE; i++) this.rom[off + KIT_NAME_OFFSET + i] = 0;
  }

  // --- palette files (.lsdpal = 4-char name + the 40-byte palette body) ---
  /** Serialize palette `index` to a `.lsdpal` file: 4 ASCII name chars (space-padded) + 40 palette bytes. */
  exportPaletteFile(index: number): Uint8Array {
    const pals = this.palettes();
    if (index < 0 || index >= pals.length) return new Uint8Array(0);
    const out = new Uint8Array(LSDPAL_SIZE);
    const name = (pals[index].name || "").toUpperCase();
    for (let i = 0; i < LSDPAL_NAME_LEN; i++) {
      const c = i < name.length ? name.charCodeAt(i) : 0x20;
      out[i] = c >= 0x20 && c <= 0x7e ? c : 0x20;
    }
    out.set(pals[index].raw(), LSDPAL_NAME_LEN);
    return out;
  }
  /** Import a `.lsdpal` file into palette `index`: writes the 40 colour bytes + the 4-char name. Throws on
   *  a wrong size. */
  importPaletteFile(index: number, file: Uint8Array): void {
    if (file.length !== LSDPAL_SIZE) throw new Error(`not a .lsdpal file: ${file.length} bytes (expected ${LSDPAL_SIZE})`);
    const count = paletteCount(this.rom);
    const base = findPaletteBase(this.rom, count);
    if (base < 0 || index < 0 || index >= count) throw new Error(`palette ${index} out of range (${count} palettes)`);
    let name = "";
    for (let i = 0; i < LSDPAL_NAME_LEN; i++) {
      const c = file[i];
      if (c >= 0x20 && c <= 0x7e) name += String.fromCharCode(c);
    }
    new PaletteView(this.rom, base, index).setRaw(file.slice(LSDPAL_NAME_LEN));
    setPaletteName(this.rom, index, count, name.trimEnd());
  }

  // --- palettes (count + names + colours all version-derived; empty when the section can't be found) ---
  palettes(): PaletteView[] {
    const count = paletteCount(this.rom);
    const base = findPaletteBase(this.rom, count);
    if (base < 0 || count <= 0) return [];
    const names = paletteNames(this.rom, count);
    return Array.from({ length: count }, (_, i) => new PaletteView(this.rom, base, i, names[i] ?? ""));
  }

  // --- fonts (marker-located header; names from the bank-27 table; empty when not found) ---
  fonts(): FontView[] {
    const base = findFontBase(this.rom);
    if (base < 0) return [];
    const names = fontNames(this.rom);
    return Array.from({ length: FONT_COUNT }, (_, i) => new FontView(this.rom, base, i, names[i] ?? ""));
  }

  // --- font images (.png at the caller; this maps tiles ↔ RGBA) ---
  /** Render font `index` to an RGBA image: the 71 main tiles, or (with `includeGfx`) also the 46 shared
   *  graphics tiles (the extended layout). 8 tiles wide (64px); height rounds up to whole tile rows.
   *  Returns an empty image if the font block isn't found or the index is out of range. */
  exportFontImage(index: number, includeGfx = false): RomImage {
    const fonts = this.fonts();
    if (index < 0 || index >= fonts.length) return { width: 0, height: 0, rgba: new Uint8Array(0) };
    const font = fonts[index];
    const tileCount = includeGfx ? FONT_TILE_COUNT + FONT_GFX_TILE_COUNT : FONT_TILE_COUNT;
    const rows = Math.ceil(tileCount / FONT_TILES_X);
    const width = FONT_IMAGE_WIDTH;
    const height = rows * 8;
    const rgba = new Uint8Array(width * height * 4);
    for (let i = 3; i < rgba.length; i += 4) rgba[i] = 0xff; // opaque (unused slots stay black, as lsdpatch)
    for (let t = 0; t < tileCount; t++) {
      const px = t < FONT_TILE_COUNT ? font.tile(t) : font.gfxTile(t - FONT_TILE_COUNT);
      const baseX = (t % FONT_TILES_X) * 8;
      const baseY = Math.floor(t / FONT_TILES_X) * 8;
      for (let y = 0; y < 8; y++) {
        for (let x = 0; x < 8; x++) {
          const rgb = FONT_SHADE_RGB[px[y * 8 + x] & 0x3];
          const o = ((baseY + y) * width + (baseX + x)) * 4;
          rgba[o] = (rgb >> 16) & 0xff;
          rgba[o + 1] = (rgb >> 8) & 0xff;
          rgba[o + 2] = rgb & 0xff;
        }
      }
    }
    return { width, height, rgba };
  }

  /** Import an RGBA font image into font `index`: writes the 71 main tiles and, when the image is tall
   *  enough (≥ 117 tiles, the extended layout), the 46 SHARED graphics tiles too, then regenerates the
   *  inverted/shaded variants. The image must be 64px wide (8 tiles). Throws otherwise / on a bad index. */
  importFontImage(index: number, image: RomImage): void {
    const fonts = this.fonts();
    if (index < 0 || index >= fonts.length) throw new Error(`font ${index} out of range (${fonts.length} fonts)`);
    if (image.width !== FONT_IMAGE_WIDTH) throw new Error(`font image must be ${FONT_IMAGE_WIDTH}px wide (got ${image.width})`);
    if (image.rgba.length < image.width * image.height * 4) throw new Error("font image rgba buffer too small");
    const font = fonts[index];
    const tilesInImage = FONT_TILES_X * Math.floor(image.height / 8);
    // lsdpatch: an image carrying the full extended set imports all 117; anything smaller is main-only.
    const maxTiles = tilesInImage >= FONT_TILE_COUNT + FONT_GFX_TILE_COUNT ? FONT_TILE_COUNT + FONT_GFX_TILE_COUNT : FONT_TILE_COUNT;
    for (let t = 0; t < maxTiles; t++) {
      const baseX = (t % FONT_TILES_X) * 8;
      const baseY = Math.floor(t / FONT_TILES_X) * 8;
      const px = new Array<number>(64);
      for (let y = 0; y < 8; y++) {
        for (let x = 0; x < 8; x++) {
          const o = ((baseY + y) * image.width + (baseX + x)) * 4;
          px[y * 8 + x] = shadeFromRgb(image.rgba[o], image.rgba[o + 1], image.rgba[o + 2]);
        }
      }
      if (t < FONT_TILE_COUNT) font.setTile(t, px);
      else font.setGfxTile(t - FONT_TILE_COUNT, px);
    }
    font.regenerateVariants();
  }
}
