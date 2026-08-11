// Deriving a predictor's row-timing table from a live cart's battery.
//
// Kept OUT of the playback barrel on purpose. The predictor itself is bundled into the DSP kernel, and
// this pulls in the whole sav codec - which has no business on the audio thread, and is exactly why the
// table is derived here (on the control plane, where the bytes are) and pushed across as data.

import { decodeSav } from "../codec/sav";
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
