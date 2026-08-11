// Where an smsggdj build keeps the things a host-side song load has to touch, as WORK-RAM OFFSETS
// (`readRam`/`writeRam` index the 8 KB region mapped at CPU $C000, so an offset is what a caller needs).
//
// The set is small on purpose: this is not a runtime-state reader like risa's, which decodes playback
// position every block. It is the minimum needed to make a load COMPLETE, which is exactly the block
// plus the two fields SMDJ4 keeps outside it.

export interface SmsggdjLayout {
  /** The ROM's own version, which may differ from the snapshot the addresses came from (see aliases). */
  version: string;
  /** The 6,912-byte working song block. Offset 0: the block leads work RAM, asserted at generation. */
  song: number;
  /** 6,912 - the SMDJ4 block length, a property of the format rather than of the build. */
  songLen: number;
  /** The 8-char song name. NOT in the block - SMDJ4 keeps it in the directory entry. */
  name: number;
  nameLen: number;
  /** Echo settings: mode, tap1, tap2, red1, red2, stereo, tsp1, tsp2 - eight contiguous bytes, which
   *  the generator asserts rather than assumes. Also directory-entry data, and AUDIBLE if left stale. */
  echo: number;
  echoLen: number;
  /** The cart's own "working song differs from its slot" flag, cleared by its load and save. */
  edited: number;
  /** Which slot the PROJECT screen is pointing at. */
  slot: number;
}
