// Byte-level risa catalog song operations — the risa analog of ./lsdjSongOps.ts. Each op normalizes
// the incoming battery to a fresh 64 KB image, edits the RSAV catalog in the layout that's present
// (current v2 @0x8000 or legacy v1 @0x6000), and returns the new 64 KB image. Records are treated as
// OPAQUE blobs — no song-payload round-trip (that model is lossy; mirrors the LSDj rule). The menu
// edit cycle is: readSram -> op -> writeFileAtomic(.sav) -> loadSram (cold boot).
//
// Delete / reorder / add-record / replace-record + record extraction (for export) treat records as
// opaque; load-to-working expands a record's payload into the working-song RAM (banks 0-3) via the
// song-payload codec (./risa/codec/working). The .risong file format (kit-aware zip) comes later.
import {
  normalizeSaveContainer,
  chooseCatalogLayout,
  parseCatalog,
  writeRecord,
  deleteRecord,
  moveRecord,
  recordBytesAt,
  expandRecordToWorking,
  encodeRecord,
  readWorking,
  CURRENT_LAYOUT,
  kSaveSize,
  type CatalogLayout,
} from "./risa";
import { BANK_DATA, WRAM_BANK_SIZE, SAVE_CURRENT_ENTRY_OFFSET } from "./risa/codec/constants";

/** Normalize to a fresh, editable 64 KB image + the catalog layout present in it. Throws if there is
 *  no valid RSAV catalog to edit. */
function editable(rawSave: Uint8Array): { save: Uint8Array; layout: CatalogLayout } {
  const save = normalizeSaveContainer(rawSave).save; // a fresh copy — safe to mutate
  const layout = chooseCatalogLayout(save);
  if (!layout) throw new Error("No RSAV catalog to edit in this save");
  return { save, layout };
}

/** The raw bytes of the song record at `index` (whole record incl. its 16-byte header), or null when
 *  there is no catalog / the slot is out of range. The source for exporting a single song. */
export function songRecordBytes(rawSave: Uint8Array, index: number): Uint8Array | null {
  let save: Uint8Array;
  try {
    save = normalizeSaveContainer(rawSave).save;
  } catch {
    return null;
  }
  const layout = chooseCatalogLayout(save);
  if (!layout) return null;
  return recordBytesAt(save, layout, index);
}

/** Delete the song at `index`, compacting the catalog. Returns the new 64 KB battery image. */
export function deleteSongInSav(rawSave: Uint8Array, index: number): Uint8Array {
  const { save, layout } = editable(rawSave);
  deleteRecord(save, index, layout);
  return save;
}

/** Reorder: move the song at `from` to `to`. Returns the new 64 KB image (unchanged if from === to). */
export function moveSongInSav(rawSave: Uint8Array, from: number, to: number): Uint8Array {
  const { save, layout } = editable(rawSave);
  moveRecord(save, from, to, layout);
  return save;
}

/** Overwrite the song at `index` with `record` (a whole record carrying its own length header). */
export function replaceSongRecordInSav(rawSave: Uint8Array, index: number, record: Uint8Array): Uint8Array {
  const { save, layout } = editable(rawSave);
  writeRecord(save, index, record, layout);
  return save;
}

/** Append `record` as a new song. If the battery has no catalog yet, an empty current (v2) catalog is
 *  initialized in place first (the live banks 0-3 are preserved). Returns the new 64 KB image. */
export function addSongRecordToSav(rawSave: Uint8Array, record: Uint8Array): Uint8Array {
  const save = normalizeSaveContainer(rawSave).save;
  let layout = chooseCatalogLayout(save);
  if (!layout) {
    // Initialize a blank current-v2 catalog in the (unallocated) banks 4-7, keeping the live song.
    save.fill(0, CURRENT_LAYOUT.offset, kSaveSize);
    save.set([0x52, 0x53, 0x41, 0x56], CURRENT_LAYOUT.offset); // "RSAV"
    save[CURRENT_LAYOUT.offset + 4] = CURRENT_LAYOUT.version;
    layout = CURRENT_LAYOUT;
  }
  const cat = parseCatalog(save, layout);
  writeRecord(save, cat.count, record, layout); // append past the last record
  return save;
}

/** Copy the songs at the given SOURCE catalog `indices` from `src` into `target`, appended as new slots
 *  (byte-exact records). Best-effort fill, matching the LSDj sibling: a record that can't be read (bad
 *  index) OR won't fit (the catalog is full / out of space) is SKIPPED, not fatal — every song that fits is
 *  still imported. addSongRecordToSav works on a fresh copy and writeRecord's space check throws before any
 *  write, so a skipped record leaves `out` byte-identical (no partial corruption). */
export function importSongsFromSav(target: Uint8Array, src: Uint8Array, indices: number[]): Uint8Array {
  let out = target;
  for (const i of indices) {
    const record = songRecordBytes(src, i);
    if (!record) continue; // out of range / malformed → skip
    try {
      out = addSongRecordToSav(out, record);
    } catch {
      // no room for THIS record (full / out of catalog space) — keep what already fit, try the rest
    }
  }
  return out;
}

/** Load the saved song at `index` into the working-song region (WRAM banks 0-3) of a fresh battery, so
 *  a cold boot comes up showing it — the risa analog of LSDj loadSongToWorking. The catalog (banks 4-7)
 *  is preserved. Returns null when there is no current-layout catalog, the slot is out of range, or the
 *  record is malformed. Requires the CURRENT (v2 @0x8000) layout: the working song occupies banks 0-3,
 *  which a legacy catalog (@0x6000, banks 3-7) overlaps — but the firmware migrates a legacy battery to
 *  current on boot, so the live readSram the menu operates on is always current-layout. */
export function loadSongToWorkingInSav(rawSave: Uint8Array, index: number): Uint8Array | null {
  try {
    const save = normalizeSaveContainer(rawSave).save; // a fresh copy — safe to mutate
    if (chooseCatalogLayout(save) !== CURRENT_LAYOUT) return null; // the working song needs banks 0-3 free
    const record = recordBytesAt(save, CURRENT_LAYOUT, index);
    if (!record) return null;
    save.set(expandRecordToWorking(record), 0); // overwrite banks 0-3; catalog banks 4-7 untouched
    return save;
  } catch {
    return null; // unrecognized container / corrupt catalog / malformed record → leave the sav untouched
  }
}

/** The live WORKING song (WRAM banks 0-3) encoded as a catalog record — the source for exporting or saving
 *  the working song. null when the container is unrecognized. (readWorking reads banks 0-3 straight off the
 *  64 KB image; encodeRecord serializes it into a self-describing record, same as a catalog entry.) */
export function workingSongRecord(rawSave: Uint8Array): Uint8Array | null {
  let save: Uint8Array;
  try {
    save = normalizeSaveContainer(rawSave).save;
  } catch {
    return null;
  }
  return encodeRecord(readWorking(save));
}

/** Save the live working song into the catalog as a new slot, and link the working song to it (set the
 *  'current entry' byte to the new index) so it is no longer "unsaved". Appends the working record past the
 *  last catalog entry (initializing a blank v2 catalog first if there is none, keeping banks 0-3). Returns
 *  the new 64 KB image. Throws on a malformed working song / full catalog (the menu wraps it in tryOp). */
export function saveWorkingToCatalog(rawSave: Uint8Array): Uint8Array {
  const save = normalizeSaveContainer(rawSave).save; // a fresh copy — safe to mutate
  const record = encodeRecord(readWorking(save));
  const before = chooseCatalogLayout(save);
  const newIndex = before ? parseCatalog(save, before).count : 0; // the slot the append will land in
  const out = addSongRecordToSav(save, record); // appends (+ inits a v2 catalog when absent), keeps banks 0-3
  out[BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET] = newIndex & 0xff; // link working → the new slot
  return out;
}
