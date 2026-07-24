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
  CURRENT_LAYOUT,
  kSaveSize,
  type CatalogLayout,
} from "./risa";

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
