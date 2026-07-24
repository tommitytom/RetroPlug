// Byte-level edits over the LSDj SAV's 32 saved-song slots — the operations the Songs menu drives (the menu
// wraps these with IO: read live SRAM → op → write .sav → cold-boot). CRITICAL: these operate on the raw
// SAV bytes and NEVER round-trip song data through the Song model. The model is only a lossless byte
// round-trip WITH a template (the original bytes); stored projects re-encoded without one lose ~300 bytes
// each (silently — the decoded model still matches, but LSDj plays the raw bytes), which corrupted
// instruments/tables on Load/Delete/Add. Byte-level keeps every untouched song exactly as it was.
import { decompressSlot, injectSong, freeSong, freeSongSlot, savSongName, savSongVersion, loadSongToWorking, listProjects, swapProjectSlots } from "./lsdjSav";
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

/** Reorder saved songs by list POSITION: swap the songs at positions `from` and `to` in the slot-sorted
 *  saved-song list (what the menu shows) — NOT slot numbers. LSDj addresses songs by a fixed slot number, so
 *  this swaps the two slots' contents (name / version / block-ownership tags) and follows the active pointer;
 *  the compressed blocks never move. Null (a no-op) when either position is out of range or from === to. Used
 *  by the shared song menu's Move Up/Down (which passes adjacent positions). */
export function moveSongInSav(sav: Uint8Array, from: number, to: number): Uint8Array | null {
  const slots = listProjects(sav).map((p) => p.slot); // occupied slots, sorted by slot number
  if (from < 0 || to < 0 || from >= slots.length || to >= slots.length || from === to) return null;
  return swapProjectSlots(sav, slots[from], slots[to]);
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

/** Copy the songs at the given SOURCE slots from `src` into `sav`'s free slots (byte-exact), until `sav`
 *  fills. Slots empty in `src` are skipped. The live WORKING song (offset 0) and every existing saved song
 *  are left byte-for-byte untouched — an import only fills free slots, never clobbers the user's current
 *  song or overwrites occupied slots. Neither input buffer is mutated (injectSong returns fresh images). */
export function importSongsFromSav(sav: Uint8Array, src: Uint8Array, slots: number[]): Uint8Array {
  let out = sav;
  for (const s of slots) {
    const raw = decompressSlot(src, s);
    if (!raw) continue;
    const slot = freeSongSlot(out);
    if (slot < 0) break; // target full — stop; everything already imported stays, nothing is overwritten
    const injected = injectSong(out, slot, savSongName(src, s), savSongVersion(src, s), raw);
    if (!injected) break;
    out = injected;
  }
  return out;
}

/** Copy every occupied song from `src` into `sav`'s free slots (byte-exact), until `sav` fills. Preserves
 *  the working song + existing saved songs (see importSongsFromSav). */
export function importAllSongsFromSav(sav: Uint8Array, src: Uint8Array): Uint8Array {
  return importSongsFromSav(sav, src, Array.from({ length: kProjectCount }, (_, s) => s));
}
