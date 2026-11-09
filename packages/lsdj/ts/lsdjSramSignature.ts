// LSDj's meaningful-change signature over a battery (docs/modular-graph-phase-f4a-design.md section 4.3). The shell's
// loose-`.sav` auto-save asks every tracker integration for one, in product order, and falls back to a whole-image hash;
// this is the cart's, and it is the image's, not a role's: it reads the bytes and nothing else.
//
// LSDj rewrites its working RAM every frame (the work/total-time clocks tick constantly), so a whole-SRAM hash reports
// a fresh sav as "dirty" the instant it boots. Those volatile bytes are NOT modeled by the codec, so a canonical
// re-encode `encodeSong(decodeSong(workingSong))` normalises them away; the header + stored-project archive
// (0x8000..end) is static (it changes only on an explicit LSDj SAVE) and is hashed raw. The signature is dedup-only,
// never persisted.
import { decodeSong, encodeSong } from "./codec/song";

function fnvInto(h: number, bytes: Uint8Array): number {
  for (let i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193);
  }
  return h;
}

/** The signature of a full 128 KiB LSDj `.sav`, or `null` if `bytes` isn't one (the caller then asks the next
 *  integration, and finally a whole-SRAM hash). */
export function lsdjSramSignature(bytes: Uint8Array): number | null {
  // A full LSDj image is 128 KiB with the 'jk' SRAM-init magic at 0x813E.
  if (bytes.length < 0x20000 || bytes[0x813e] !== 0x6a || bytes[0x813f] !== 0x6b) return null;
  let canonical: Uint8Array;
  try {
    canonical = encodeSong(decodeSong(bytes.subarray(0, 0x8000)));
  } catch {
    return null;
  }
  let h = fnvInto(0x811c9dc5, canonical);
  h = fnvInto(h, bytes.subarray(0x8000));
  return h >>> 0;
}
