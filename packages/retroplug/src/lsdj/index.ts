// The pure-TS LSDj sav codec: the model (zod SSOT) + the binary codec, plus the
// JSON helpers that used to be a native RPC round-trip. `savFromJson` authors a
// .sav from a (lenient) JSON model — every unset cell takes its default, so
// `savFromJson("{}")` yields a valid empty 128 KiB image. `savToJson` decodes a
// .sav back into the model (the capability the native path never exposed to TS).
export * from "./model";
export { decodeSong, encodeSong } from "./codec/song";
export { encodeSav, decodeSav, kSavSize } from "./codec/sav";
export { compressProject, decompressProject } from "./codec/rle";
export { BitView, BitWriter } from "./codec/bits";

import { SavSchema } from "./model";
import { encodeSav, decodeSav } from "./codec/sav";

/** Encode a .sav from a JSON model (lenient: unset cells default). */
export function savFromJson(json: string): Uint8Array {
  return encodeSav(SavSchema.parse(JSON.parse(json)));
}

/** Decode a .sav image into its JSON model. */
export function savToJson(bytes: Uint8Array): string {
  return JSON.stringify(decodeSav(bytes));
}
