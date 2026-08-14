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

  // --- engine state, needed only for a load that lands WHILE THE TRANSPORT IS RUNNING -----------------
  // The cart's own load calls `load_rebase`, which opens with `ret z` on play_state: everything below is
  // inert for a load made while stopped, which is the common case. When it IS playing, skipping the
  // rebase leaves the sequencer wrapping at the OLD song's length forever, which is a persistent wrong
  // behaviour rather than a transient glitch - hence reproducing exactly these three effects.

  /** Non-zero while the cart's transport is running. Read, never written: stopping the engine properly
   *  means `engine_stop`'s whole job (sync lines released, channels quiesced), not a zeroed byte. */
  playState: number;
  /** Song length in rows, rescanned from the new song's grid. */
  engLen: number;
  /** Four queued LIVE cells, cleared because they address the OLD song's grid. */
  liveQ: number;
  liveQLen: number;
  /** Selected groove + position, reset together when the new song's selected groove is empty (which
   *  would otherwise stall the clock). */
  grooveSel: number;
  groovePos: number;

  /** PSG shadow attenuations, one byte per channel (0 loud .. $F silent). Read-only, and not part of
   *  loading a song: it is here so a test can certify the echo address by which channels the ENGINE
   *  actually drives, which is a binary fact, instead of by how loud a given window happens to be. */
  psgVols: number;
  psgVolsLen: number;
}
