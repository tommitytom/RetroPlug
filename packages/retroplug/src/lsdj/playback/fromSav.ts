// Deriving a predictor's row-timing table from a live cart's battery.
//
// Kept OUT of the playback barrel on purpose. The predictor itself is bundled into the DSP kernel, and
// this pulls in the whole sav codec - which has no business on the audio thread, and is exactly why the
// table is derived here (on the control plane, where the bytes are) and pushed across as data.

import { decodeSav, isLsdjSav } from "../codec/sav";
import { SYNC_FROM_BYTE } from "../model";
import * as R from "../codec/regions";
import { songRowTicks, type RowTicksTable } from "./predict";

/** The row-timing table for the song currently in a cart's WORKING memory - the one that plays, not a
 *  slot in the archive.
 *
 *  Returns null rather than throwing for anything that is not a readable LSDj battery: a non-LSDj cart, a
 *  truncated image, a system whose SRAM has not been published yet. Callers treat that as "no song
 *  feedback", which degrades the LEDs and nothing else. */
export function songRowTicksFromSav(sav: Uint8Array | null | undefined): RowTicksTable | null {
  if (!sav || sav.length === 0) return null;
  try {
    return songRowTicks(decodeSav(sav).workingSong);
  } catch {
    return null;
  }
}

// The four regions of the working song that `songRowTicks` actually reads: which chain each song row holds,
// which phrases each chain holds, whether a chain is allocated at all, and groove 0 (the only groove the
// predictor honours). ~3 KB rather than the whole 32 KB bank, so editing an instrument or renaming a file
// does not read as a song change - and the offsets are safe to hardcode because the song layout is
// version-stable across every supported format (see regions.ts).
const SIG_REGIONS: readonly (readonly [number, number])[] = [
  [R.kModernRegions.chainAssignments, R.kSongRowCount * R.kChannelCount],
  [R.kModernRegions.chainPhrases, R.kChainCount * R.kChainLength],
  [R.kModernRegions.chainAllocations, R.kChainCount / 8],
  [R.kModernRegions.grooves, R.kGrooveLength],
];

/** The cart's SYNC setting, read straight out of the working song's setting byte, or null when the battery
 *  is not a readable LSDj one.
 *
 *  Worth knowing because a cart that is not in MI.MAP ignores row launches ENTIRELY WITHOUT COMPLAINT: it
 *  keeps playing and keeps stepping to the host clock, so nothing looks wrong, but pressing a pad does
 *  nothing at all. Measured in test-native/lsdj-sync-toggle, along with the way a player lands there - LSDj
 *  refuses to change SYNC while the cart is playing, so reaching for it mid-session leaves the setting on
 *  whichever option the first press hit, with no way back until playback stops.
 *
 *  A byte read rather than a decode: the offset is version-stable (regions.ts), and this is polled. */
export function savSyncMode(sav: Uint8Array | null | undefined): string | null {
  if (!sav || sav.length < R.kSongByteCount || !isLsdjSav(sav)) return null;
  return SYNC_FROM_BYTE.get(sav[R.kModernRegions.syncMode]) ?? null;
}

/** A cheap change signature over the parts of a live battery the row-timing table depends on.
 *
 *  The point is to notice edits the player makes INSIDE the tracker, which nothing else in the app can
 *  see - add a chain to a song row and the controller's grid should light it up. Deriving the table to
 *  find out would mean decoding the whole sav on a timer; this is an FNV-1a over ~3 KB instead, and the
 *  decode happens only once the answer has actually moved. Returns 0 for anything unreadable, which
 *  compares equal to itself and so simply never triggers. */
export function workingSongSignature(sav: Uint8Array | null | undefined): number {
  if (!sav || sav.length < R.kSongByteCount) return 0;
  let h = 0x811c9dc5;
  for (const [start, len] of SIG_REGIONS) {
    for (let i = start; i < start + len; i++) {
      h ^= sav[i];
      h = Math.imul(h, 0x01000193);
    }
  }
  return h >>> 0;
}
