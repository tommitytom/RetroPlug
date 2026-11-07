// Top-level risa save barrel — the import surface for the app/UI (menus, stores) and tests, mirroring
// @retroplug/lsdj's barrel. Exposes the catalog reader/writer plus the song-payload codec (record <-> model <->
// working-RAM).
export {
  listSongs,
  isRisaSav,
  parseCatalog,
  normalizeSaveContainer,
  chooseCatalogLayout,
  decodeSongName,
  workingSongInfo,
  // write side (byte-level catalog ops)
  writeRecord,
  deleteRecord,
  moveRecord,
  recordBytesAt,
  makeEmptySave,
  kSaveSize,
  CURRENT_LAYOUT,
  LEGACY_LAYOUT,
  // song-payload codec (record <-> model <-> working-RAM)
  decodeRecord,
  encodeRecord,
  writeWorking,
  readWorking,
  expandRecordToWorking,
  initWorkingDefaults,
} from "./index";
export type { RisaSongInfo, CatalogLayout, RisaRecord } from "./index";
