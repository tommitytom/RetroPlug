// The pure-TS LSDj sav codec: the model (zod SSOT) + the binary codec, plus the
// authoring/decoding helpers that used to be a native RPC round-trip. `savFrom`
// authors a .sav from a (lenient, type-checked) model object — every unset cell
// takes its default, so `savFrom({})` yields a valid empty 128 KiB image;
// `savFromJson` is the same over a raw JSON string (loading a file / string).
// `savToJson` decodes a .sav back to the model (never exposed to TS natively).
export * from "./model";
export { decodeSong, encodeSong } from "./codec/song";
export { encodeSav, decodeSav, kSavSize, listProjects, isLsdjSav, freeSongSlot, freeSong, injectSong, swapProjectSlots, decompressSlot, savSongName, savSongVersion, loadSongToWorking, type SavProjectInfo } from "./codec/sav";
export { encodeLsdsng, decodeLsdsng, decodeLsdsngRaw, encodeLsdsngRaw } from "./codec/lsdsng";
export { decodeLsdprj, encodeLsdprj, lsdprjKitBank, usedKitIndices, remapSongKits, type Lsdprj } from "./codec/lsdprj";
export { compressProject, decompressProject } from "./codec/rle";
export { BitView, BitWriter } from "./codec/bits";

// The runtime-WRAM reader (transient playback/UI state) — separate concern from the saved-song codec
// above. Namespaced re-export to avoid leaking its many symbols into the top-level barrel.
export * as runtime from "./runtime";

// The ROM asset module (read/patch kits, palettes, fonts in a .gb image) — likewise namespaced.
export * as rom from "./rom";

import { SavSchema, type SavInput } from "./model";
import { encodeSav, decodeSav } from "./codec/sav";

/** Encode a .sav from a lenient model object (type-checked; unset cells default). Prefer this from TS. */
export function savFrom(input: SavInput): Uint8Array {
  return encodeSav(SavSchema.parse(input));
}

/** Encode a .sav from a raw JSON string (for loading a file / string). `savFrom` is the typed path. */
export function savFromJson(json: string): Uint8Array {
  return savFrom(JSON.parse(json) as SavInput);
}

/** Decode a .sav image into its JSON model. */
export function savToJson(bytes: Uint8Array): string {
  return JSON.stringify(decodeSav(bytes));
}
