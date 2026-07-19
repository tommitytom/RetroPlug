// Codec for the risa (NES/MMC5 tracker) battery save — the read side (M1: list songs).
// The battery is the raw 64 KB MMC5 WRAM image (8 x 8 KB banks at CPU $6000-$7FFF). Saved songs
// live in an "RSAV" catalog: a 0x100-byte header (magic / version / count / used) followed by
// tightly-packed variable-length song records. Unlike LSDj's fixed 32-slot table + alloc bitmap,
// risa's directory IS the record chain — each record header carries its own length, so listing
// songs is a header-only walk that also yields each song's version + length cheaply.
//
// Two catalog layouts exist and both are read here (mirrors risa's own tools/rom_patcher
// save_manager/catalog.js `chooseCatalogLayout`):
//   - current (v2): region at 0x8000, 0x8000 bytes (WRAM banks 4-7). Live current-firmware batteries.
//   - legacy  (v1): region at 0x6000, 0xA000 bytes (banks 3-7). Shipped demo .srm files use this.
//
// Ported from tools/rom_patcher/src/save_manager/{catalog.js,constants.js,record_codec.js}. Mirrors
// the module shape of ../../lsdj/codec/sav.ts (listSongs is the risa analog of LSDj listProjects).

/** One saved song in the RSAV catalog — index + name, plus the record version and total byte length
 *  (both read cheaply from the record header, no payload decode). */
export interface RisaSongInfo {
  index: number;
  name: string;
  version: number;
  length: number;
}

/** A catalog layout: where the RSAV region sits and its header version. */
export interface CatalogLayout {
  key: "current" | "legacy";
  offset: number;
  size: number;
  version: number;
}

export const kSaveSize = 0x10000; // 64 KB — the normalized battery image size
const kSaveSizeWithTail = 0x10400; // 65 KB — emulator/Everdrive .srm (64 KB + 1 KB tail)
const kTruncatedSize = 0x8000; // 32 KB — MMC5 "rescue" dump (zero-extended to 64 KB)
const kPocketSize = 0x40000; // 256 KB — Analogue Pocket wrapper (save is the first 64 KB)

const kSaveHeaderSize = 0x100; // catalog header size; records start here (region-relative)
const kRecHeader = 0x10; // per-record header size (length + name + version)
const kNameLen = 8; // SONG_NAME_LEN
const kUntitled = "UNTITLED";

const RSAV_MAGIC = [0x52, 0x53, 0x41, 0x56]; // "RSAV"

export const CURRENT_LAYOUT: CatalogLayout = { key: "current", offset: 0x8000, size: 0x8000, version: 2 };
export const LEGACY_LAYOUT: CatalogLayout = { key: "legacy", offset: 0x6000, size: 0xa000, version: 1 };

function readU16(bytes: Uint8Array, off: number): number {
  return bytes[off] | (bytes[off + 1] << 8);
}

function writeU16(bytes: Uint8Array, off: number, value: number): void {
  bytes[off] = value & 0xff;
  bytes[off + 1] = (value >> 8) & 0xff;
}

/** Decode an 8-byte song name: ASCII up to the first NUL, right-trimmed; empty -> "UNTITLED".
 *  (record_codec.js decodeName / encodeName space-pad with 0x20.) */
export function decodeSongName(bytes: Uint8Array): string {
  let s = "";
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === 0) break;
    s += String.fromCharCode(bytes[i]);
  }
  return s.replace(/\s+$/, "") || kUntitled;
}

/** Normalize an on-disk container to the raw 64 KB battery image. Handles the 32 KB rescue dump
 *  (zero-extended), the plain 64 KB image, the 65 KB emulator/.srm tail variant, and the 256 KB
 *  Analogue Pocket wrapper (save is the first 64 KB). Throws on an unrecognized length. */
export function normalizeSaveContainer(buffer: Uint8Array): { save: Uint8Array; format: string } {
  const src = buffer;
  if (src.length === kTruncatedSize) {
    const save = new Uint8Array(kSaveSize);
    save.set(src);
    return { save, format: "mesen" };
  }
  if (src.length === kSaveSize) return { save: src.slice(), format: "mesen" };
  if (src.length === kSaveSizeWithTail) {
    const suffix = src.slice(kSaveSize, kSaveSizeWithTail);
    const format = suffix.some((v) => v !== 0) ? "mesen" : "everdrive";
    return { save: src.slice(0, kSaveSize), format };
  }
  if (src.length === kPocketSize) return { save: src.slice(0, kSaveSize), format: "pocket" };
  throw new Error(
    `Expected 32 KB rescue, 64 KB risa .sav, 65 KB emulator save, or 256 KB Analogue Pocket save; got ${src.length} bytes`,
  );
}

/** True if a valid RSAV magic + version sits at `offset` (a cheap layout probe). */
function hasCatalogAt(save: Uint8Array, layout: CatalogLayout): boolean {
  for (let i = 0; i < RSAV_MAGIC.length; i++) {
    if (save[layout.offset + i] !== RSAV_MAGIC[i]) return false;
  }
  return save[layout.offset + 4] === layout.version;
}

/** Pick the catalog layout present in a normalized 64 KB image: current (0x8000/v2) preferred, then
 *  legacy (0x6000/v1). Returns null if neither is present. */
export function chooseCatalogLayout(save: Uint8Array): CatalogLayout | null {
  if (hasCatalogAt(save, CURRENT_LAYOUT)) return CURRENT_LAYOUT;
  if (hasCatalogAt(save, LEGACY_LAYOUT)) return LEGACY_LAYOUT;
  return null;
}

/** Strict catalog parse of a normalized 64 KB image for the given layout. Validates magic, version,
 *  the used-byte bound, each record's length + bounds, and that the record walk lands exactly at
 *  0x100 + used. Throws on any violation. (Faithful port of catalog.js parseCatalog.) */
export function parseCatalog(
  save: Uint8Array,
  layout: CatalogLayout,
): { version: number; count: number; used: number; free: number; records: RisaSongInfo[] } {
  if (save.length !== kSaveSize) throw new Error("Internal save buffer must be 64 KB");
  const r = layout.offset;
  for (let i = 0; i < RSAV_MAGIC.length; i++) {
    if (save[r + i] !== RSAV_MAGIC[i]) throw new Error(`RSAV catalog not found in ${layout.key} layout`);
  }
  if (save[r + 4] !== layout.version) throw new Error(`Unsupported RSAV catalog version ${save[r + 4]}`);

  const count = save[r + 5];
  const used = readU16(save, r + 6);
  if (used > layout.size - kSaveHeaderSize) throw new Error("RSAV used byte count exceeds save region");

  const records: RisaSongInfo[] = [];
  let off = kSaveHeaderSize;
  for (let i = 0; i < count; i++) {
    if (off + kRecHeader > kSaveHeaderSize + used) throw new Error("RSAV record header extends past used region");
    const recOff = r + off;
    const len = readU16(save, recOff);
    if (len < kRecHeader) throw new Error(`RSAV record ${i} has invalid length`);
    if (off + len > kSaveHeaderSize + used) throw new Error(`RSAV record ${i} extends past used region`);
    records.push({
      index: i,
      name: decodeSongName(save.subarray(recOff + 2, recOff + 2 + kNameLen)),
      version: save[recOff + 10],
      length: len,
    });
    off += len;
  }
  if (off !== kSaveHeaderSize + used) throw new Error("RSAV record lengths do not match used byte count");

  return { version: layout.version, count, used, free: layout.size - kSaveHeaderSize - used, records };
}

/** List the saved songs in a risa battery — the risa analog of LSDj `listProjects`. Tolerant: accepts
 *  any recognized container size, tries the current then legacy catalog layout, and returns [] (never
 *  throws) when no valid catalog is present or the catalog fails to parse. A cheap header-only walk. */
export function listSongs(rawSave: Uint8Array): RisaSongInfo[] {
  let save: Uint8Array;
  try {
    save = normalizeSaveContainer(rawSave).save;
  } catch {
    return [];
  }
  const layout = chooseCatalogLayout(save);
  if (!layout) return [];
  try {
    return parseCatalog(save, layout).records;
  } catch {
    return [];
  }
}

// ---------------------------------------------------------------------------
// Write side — byte-level catalog record ops. Faithful ports of risa's own
// tools/rom_patcher save_manager/catalog.js {writeRecord,deleteRecord,moveRecord,makeEmptySave}.
// All mutate a normalized 64 KB image in place for a given layout; the record chain stays tightly
// packed (record[i+1] starts where record[i] ends) and the header count/used stay in sync. The
// higher-level, container-aware, clone-and-return wrappers live in ../../risaSongOps.ts.
// ---------------------------------------------------------------------------

/** The raw bytes of each record in `layout`'s catalog, in walk order. */
function catalogRecordBytes(save: Uint8Array, layout: CatalogLayout): Uint8Array[] {
  const cat = parseCatalog(save, layout);
  const out: Uint8Array[] = [];
  let off = kSaveHeaderSize;
  for (const rec of cat.records) {
    const start = layout.offset + off;
    out.push(save.slice(start, start + rec.length));
    off += rec.length;
  }
  return out;
}

/** Raw bytes of the record at `index` (a whole record incl. its 16-byte header), or null if the slot
 *  is out of range. Used by export + reorder. */
export function recordBytesAt(save: Uint8Array, layout: CatalogLayout, index: number): Uint8Array | null {
  const records = catalogRecordBytes(save, layout);
  return index >= 0 && index < records.length ? records[index] : null;
}

/** Insert (index === count) or overwrite (index < count) a record at `index`. `record` must carry its
 *  own length header (u16 @0 === record.length). Throws on a bad record, out-of-range index, a full
 *  (255-song) catalog, or insufficient free space. */
export function writeRecord(save: Uint8Array, index: number, record: Uint8Array, layout: CatalogLayout): void {
  if (record.length < kRecHeader) throw new Error("Song record is too short");
  if (readU16(record, 0) !== record.length) throw new Error("Song record length header does not match payload");

  const cat = parseCatalog(save, layout);
  if (index < 0 || index > cat.count) throw new Error("Song slot index is out of range");

  const oldLen = index < cat.count ? cat.records[index].length : 0;
  if (oldLen === 0 && cat.count === 0xff) throw new Error("RSAV catalog already contains 255 songs");
  const newUsed = cat.used - oldLen + record.length;
  if (newUsed > layout.size - kSaveHeaderSize) throw new Error("Not enough free .sav catalog space");

  const usedStart = layout.offset + kSaveHeaderSize;
  // Absolute offset of the slot being written: the existing record's start, or just past all records.
  let insertOff = usedStart + cat.used;
  if (index < cat.count) {
    let acc = 0;
    for (let i = 0; i < index; i++) acc += cat.records[i].length;
    insertOff = usedStart + acc;
  }
  const tail = save.slice(insertOff + oldLen, usedStart + cat.used);
  save.set(tail, insertOff + record.length);
  save.set(record, insertOff);
  save.fill(0, usedStart + newUsed, usedStart + cat.used);
  writeU16(save, layout.offset + 6, newUsed);
  if (oldLen === 0) save[layout.offset + 5] = cat.count + 1;
}

/** Remove the record at `index`, compacting the records after it. Throws if `index` is out of range. */
export function deleteRecord(save: Uint8Array, index: number, layout: CatalogLayout): void {
  const cat = parseCatalog(save, layout);
  if (index < 0 || index >= cat.count) throw new Error("Song slot index is out of range");
  const usedStart = layout.offset + kSaveHeaderSize;
  let acc = 0;
  for (let i = 0; i < index; i++) acc += cat.records[i].length;
  const recStart = usedStart + acc;
  const len = cat.records[index].length;
  const tail = save.slice(recStart + len, usedStart + cat.used);
  save.set(tail, recStart);
  const newUsed = cat.used - len;
  save.fill(0, usedStart + newUsed, usedStart + cat.used);
  writeU16(save, layout.offset + 6, newUsed);
  save[layout.offset + 5] = cat.count - 1;
}

/** Reorder: move the record at `fromIndex` to `toIndex`, re-packing the chain. No-op (returns false)
 *  when the indices are equal; throws if either index is out of range (leaving `save` untouched). */
export function moveRecord(save: Uint8Array, fromIndex: number, toIndex: number, layout: CatalogLayout): boolean {
  const cat = parseCatalog(save, layout);
  if (!Number.isInteger(fromIndex) || fromIndex < 0 || fromIndex >= cat.count) {
    throw new Error("Source song slot index is out of range");
  }
  if (!Number.isInteger(toIndex) || toIndex < 0 || toIndex >= cat.count) {
    throw new Error("Target song slot index is out of range");
  }
  if (fromIndex === toIndex) return false;

  const records = catalogRecordBytes(save, layout);
  const [moved] = records.splice(fromIndex, 1);
  records.splice(toIndex, 0, moved);

  let off = layout.offset + kSaveHeaderSize;
  for (const bytes of records) {
    save.set(bytes, off);
    off += bytes.length;
  }
  return true;
}

/** A blank 64 KB battery with an empty current (v2) RSAV catalog at 0x8000. */
export function makeEmptySave(): Uint8Array {
  const bytes = new Uint8Array(kSaveSize);
  const r = CURRENT_LAYOUT.offset;
  bytes.set(RSAV_MAGIC, r);
  bytes[r + 4] = CURRENT_LAYOUT.version;
  return bytes;
}
