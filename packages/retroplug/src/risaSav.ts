// Top-level risa save barrel — the import surface for the app/UI (menus, stores) and tests, mirroring
// ./lsdjSav.ts. M1 exposes the read-only catalog reader; M2 adds the write-side song ops.
export {
  listSongs,
  parseCatalog,
  normalizeSaveContainer,
  chooseCatalogLayout,
  decodeSongName,
  // write side (byte-level catalog ops)
  writeRecord,
  deleteRecord,
  moveRecord,
  recordBytesAt,
  makeEmptySave,
  kSaveSize,
  CURRENT_LAYOUT,
  LEGACY_LAYOUT,
} from "./risa";
export type { RisaSongInfo, CatalogLayout } from "./risa";
