// LSDj sav authoring/decoding for tests + tooling. Formerly a native RPC shim
// (savFromJson over globalThis[Symbol.for("plugin")].__rpcSend); now a thin
// re-export of the pure-TS codec (src/lsdj), so it works with no native host —
// the 14 existing callers keep their import unchanged, and decode is now
// available to TS for the first time.
export { savFrom, savFromJson, savToJson, encodeSav, decodeSav, decodeSong, encodeSong, encodeLsdsng, decodeLsdsng, decodeLsdsngRaw, encodeLsdsngRaw, listProjects, freeSongSlot, freeSong, injectSong, swapProjectSlots, decompressSlot, savSongName, savSongVersion, loadSongToWorking, decodeLsdprj, encodeLsdprj, lsdprjKitBank, usedKitIndices, remapSongKits } from "./lsdj";
export type { Sav, SavInput, Song, SongSettings, Instrument, StoredProject, SavProjectInfo, Lsdprj } from "./lsdj";
