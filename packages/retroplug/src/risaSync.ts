// The risa host-sync byte protocol (the NES/MMC5 tracker, driven over the EverDrive N8 Pro receive FIFO).
// NOT MIDI: a raw byte stream that reuses MIDI status VALUES — 0xF8/0xFA/0xFC are real System Real-Time
// (clock/start/stop); 0xF9 52 ss cc is risa's PRIVATE locate packet, borrowing 0xF9 (undefined in real
// MIDI). The `risa-sync` DSP role (dspRoles.ts) generates these from the DAW transport and delivers them
// over ctx.pushCoreBytes → the N8 FIFO. Pure + unit-tested (test/risa/sync-locate.test.ts).

/** Locate-packet status byte (System Real-Time 0xF9 — unused by real MIDI, so safe to reuse). */
export const RISA_LOCATE_STATUS = 0xf9;
/** Locate sub-command byte (arm + seek). */
export const RISA_LOCATE_SUB = 0x52;
/** Transport start — plays the most-recently-armed locate. */
export const RISA_START = 0xfa;
/** One 24-PPQN sequencer clock tick. */
export const RISA_CLOCK = 0xf8;
/** Transport stop — gate clocks immediately, then stop. */
export const RISA_STOP = 0xfc;
/** risa clocks its sequencer at 24 pulses per quarter note. */
export const RISA_PPQN = 24;

export interface RisaLocate {
  /** Song row, 0..0x7f. */
  songRow: number;
  /** Chain row, 0..0x0f. */
  chainRow: number;
}

/** Map an absolute DAW PPQ (quarter-note) position to risa's (songRow, chainRow) locate. A "phrase" is four
 *  quarters; a song row holds 16 chain rows:
 *    phrase = floor(max(ppq,0)/4);  songRow = (phrase >> 4) & 0x7f;  chainRow = phrase & 0x0f
 *  Negative / pre-roll ppq clamps to phrase 0. Note the locate is phrase-granular (protocol 01): a seek to
 *  a mid-phrase position lands risa at that phrase's start — risa applies its own sparse-row backward
 *  fallback from there. */
export function risaLocate(ppq: number): RisaLocate {
  const phrase = Math.floor(Math.max(ppq, 0) / 4);
  return { songRow: (phrase >> 4) & 0x7f, chainRow: phrase & 0x0f };
}

/** The 4-byte arm+locate packet (`F9 52 songRow chainRow`) for a locate. */
export function risaArmPacket(loc: RisaLocate): number[] {
  return [RISA_LOCATE_STATUS, RISA_LOCATE_SUB, loc.songRow & 0x7f, loc.chainRow & 0x0f];
}
