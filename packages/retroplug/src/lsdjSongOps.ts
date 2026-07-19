// Byte-level edits over the LSDj SAV's 32 saved-song slots — the operations the Songs menu drives (the menu
// wraps these with IO: read live SRAM → op → write .sav → cold-boot). CRITICAL: these operate on the raw
// SAV bytes and NEVER round-trip song data through the Song model. The model is only a lossless byte
// round-trip WITH a template (the original bytes); stored projects re-encoded without one lose ~300 bytes
// each (silently — the decoded model still matches, but LSDj plays the raw bytes), which corrupted
// instruments/tables on Load/Delete/Add. Byte-level keeps every untouched song exactly as it was.
import { decompressSlot, injectSong, freeSong, freeSongSlot, savSongName, savSongVersion, loadSongToWorking } from "./lsdjSav";
import { decodeLsdsngRaw } from "./lsdj/codec/lsdsng";

const kProjectCount = 32;
const kActiveProj = 0x8140;

/** Load a stored song into working memory + mark it active (re-exported from the codec for the menu). */
export { loadSongToWorking } from "./lsdj/codec/sav";

// After adding a song we ALSO load it into working memory + make it active, so the cold-boot (loadSram)
// lands on the newly-added song rather than whatever was in working memory before.

/** Remove a slot's song (clear its blocks) + drop the active pointer if it referenced that slot. */
export function deleteSongInSav(sav: Uint8Array, slot: number): Uint8Array {
  const out = freeSong(sav, slot);
  if (out[kActiveProj] === slot) out[kActiveProj] = 0xff;
  return out;
}

/** Import a single `.lsdsng` into the first free slot (byte-exact) and load it into working memory so the
 *  reboot shows it. Null when malformed or the SAV is full. */
export function addLsdsngToSav(sav: Uint8Array, file: Uint8Array): Uint8Array | null {
  let parsed;
  try {
    parsed = decodeLsdsngRaw(file);
  } catch {
    return null;
  }
  const slot = freeSongSlot(sav);
  if (slot < 0) return null;
  const injected = injectSong(sav, slot, parsed.name, parsed.version, parsed.songBytes);
  if (!injected) return null;
  return loadSongToWorking(injected, slot) ?? injected;
}

/** Replace `slot`'s song from a `.lsdsng` (byte-exact): free the slot, inject. Null when malformed. */
export function replaceSongInSav(sav: Uint8Array, slot: number, file: Uint8Array): Uint8Array | null {
  let parsed;
  try {
    parsed = decodeLsdsngRaw(file);
  } catch {
    return null;
  }
  return injectSong(freeSong(sav, slot), slot, parsed.name, parsed.version, parsed.songBytes);
}

/** Copy every occupied song from `src` into `sav`'s free slots (byte-exact), until `sav` fills, and load the
 *  first imported song into working memory so the reboot shows it. */
export function importAllSongsFromSav(sav: Uint8Array, src: Uint8Array): Uint8Array {
  let out = sav;
  let firstSlot = -1;
  for (let s = 0; s < kProjectCount; s++) {
    const raw = decompressSlot(src, s);
    if (!raw) continue;
    const slot = freeSongSlot(out);
    if (slot < 0) break; // target full
    const injected = injectSong(out, slot, savSongName(src, s), savSongVersion(src, s), raw);
    if (!injected) break;
    out = injected;
    if (firstSlot < 0) firstSlot = slot;
  }
  return firstSlot >= 0 ? (loadSongToWorking(out, firstSlot) ?? out) : out;
}
