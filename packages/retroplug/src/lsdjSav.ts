// LSDj sav authoring/decoding for tests + tooling. Formerly a native RPC shim
// (savFromJson over globalThis[Symbol.for("plugin")].__rpcSend); now a thin
// re-export of the pure-TS codec (src/lsdj), so it works with no native host —
// the 14 existing callers keep their import unchanged, and decode is now
// available to TS for the first time.
export { savFromJson, savToJson, encodeSav, decodeSav, decodeSong, encodeSong } from "./lsdj";
export type { Sav, Song, Instrument } from "./lsdj";
