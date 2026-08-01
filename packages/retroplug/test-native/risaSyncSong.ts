// A risa "metronome" save: ONE noise hit per beat, for host-sync timing checks.
//
// risa's row grid is fixed at six clocks of 24 PPQN, i.e. FOUR rows per quarter note, so a note every
// fourth row is exactly one hit per beat whatever the DAW tempo. Under host sync risa's own project
// tempo is irrelevant - the host's F8 stream is what advances it - which is the point: if the hits stay
// on the DAW's beat grid, the sync is locked.
//
// Shared by the native grid test (dsp-risa-sync-grid) and the real-Reaper fixture (author-risa-rplg), so
// both measure the same song.
import { initWorkingDefaults, makeEmptySave } from "../src/risaSav";

// Working-image geometry (src/seq_data.h; mirrored in src/risa/codec/constants.ts). Bank 1 holds chains,
// the song grid and instruments; bank 0 holds phrases 0x00..0x7F.
const BANK = 0x2000;
const B1 = 1 * BANK;
const CHAIN0 = B1 + 0x0000; // 16 rows x (phrase, transpose)
const SONG = B1 + 0x1000; // 5 tracks x 128 chain ids
const INST0 = B1 + 0x1280; // 64 x 12 bytes
const MAGIC = B1 + 0x1e80; // 'N' '8' 'T' <ver>
const NAME = B1 + 0x1e8c; // 8 chars
const PHRASE0 = 0x0000; // bank 0: 16 rows x (note, inst, fx, val)

const TRACK_NOISE = 3; // pulse1, pulse2, triangle, NOISE, dmc
const SAVE_MAGIC_VER = 0x0c;
const CHAIN_EMPTY = 0xff;
const NOTE_EMPTY = 0xff;
const INST_EMPTY = 0xff;

/** Rows per quarter note on risa's fixed six-clock grid (24 PPQN / 6). */
export const RISA_ROWS_PER_BEAT = 4;

/** A 64 KB risa battery whose LIVE working song hits the noise channel once per beat. */
export function buildRisaMetronomeSav(): Uint8Array {
  const w = initWorkingDefaults();
  w.set([0x4e, 0x38, 0x54, SAVE_MAGIC_VER], MAGIC); // 'N8T' + the working-song layout version
  w.set(Array.from("RPSYNC  ", (c) => c.charCodeAt(0)), NAME);

  // Instrument 0: a percussive noise click. Byte layout is risa's seq_data_write_instrument order
  // (duty, ENV A, ENV D, table speed, sweep, extra, type, table idx, fine, -, ENV R, -).
  //
  // Each ENV byte is (level << 4) | speed, where speed 1 is fastest, F slowest, and 0 HOLDS at the
  // current level (docs/user-guide.md "Pulse and Noise Instrument Envelopes"). So A=0xF1 snaps to full
  // volume and D=0x01 decays straight back to silence: one sharp transient per note, decayed well
  // inside a beat at any tempo here. risa's own default (A=0x88, D=0x00) would HOLD instead, giving a
  // continuous tone with no measurable onsets.
  w.set([0x00, 0xf1, 0x01, 0x01, 0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00], INST0);

  // Phrase 0: a note every fourth row = one hit per beat.
  for (let row = 0; row < 16; row++) {
    const p = PHRASE0 + row * 4;
    const hit = row % RISA_ROWS_PER_BEAT === 0;
    w[p] = hit ? 12 : NOTE_EMPTY; // a mid-range noise pitch
    w[p + 1] = hit ? 0 : INST_EMPTY;
    w[p + 2] = 0; // no fx
    w[p + 3] = 0;
  }

  // Chain 0: all 16 rows play phrase 0, so one chain covers 16 phrases = 64 beats.
  for (let row = 0; row < 16; row++) {
    w[CHAIN0 + row * 2] = 0; // phrase 0
    w[CHAIN0 + row * 2 + 1] = 0; // no transpose
  }

  // Song: the noise track runs chain 0 for the first 8 song rows; every other track stays empty. A
  // locate past that is harmless (risa falls back to the last populated row), but 8 rows outlasts any
  // render here.
  for (let track = 0; track < 5; track++) {
    for (let row = 0; row < 128; row++) {
      w[SONG + track * 128 + row] = track === TRACK_NOISE && row < 8 ? 0 : CHAIN_EMPTY;
    }
  }

  // The 64 KB battery: banks 0..3 are the live working song, banks 4..7 an empty song catalog.
  const sav = makeEmptySave();
  sav.set(w, 0);
  return sav;
}
