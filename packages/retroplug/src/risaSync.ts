// The risa host-sync byte protocol (the NES/MMC5 tracker, driven over the EverDrive N8 Pro receive FIFO).
// NOT MIDI: a raw byte stream that reuses MIDI status VALUES — 0xF8/0xFA/0xFC are real System Real-Time
// (clock/start/stop); 0xF9 52 ss cc tt is risa's PRIVATE locate packet, borrowing 0xF9 (undefined in real
// MIDI). The `risa-sync` DSP role (dspRoles.ts) generates these from the DAW transport and delivers them
// over ctx.pushCoreBytes → the N8 FIFO. Pure + unit-tested (test/dsp/risa-sync.test.ts).
//
// Protocol reference: risa's docs/sync/host-sync-protocol.md (2.3.0 and later). The 4-byte arm an early
// prototype used is REJECTED by released risa, so the 5-byte form is the only one.

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
/** Clocks per phrase: 16 rows of the fixed six-clock locate grid (so tickOffset is 0x00..0x5F). */
export const RISA_CLOCKS_PER_PHRASE = 96;

export interface RisaLocate {
  /** Song row, 0..0x7f. */
  songRow: number;
  /** Chain row, 0..0x0f. */
  chainRow: number;
  /** Position on the six-clock grid within the phrase, 0..0x5f. */
  tickOffset: number;
  /** The absolute 24-PPQN clock this locate names. The clock the arm covers, so the role must NOT also
   *  send an F8 for it: risa primes that row itself during guarded subframe service. */
  absoluteClock: number;
}

/** Map an absolute DAW PPQ (quarter-note) position to risa's locate, per the protocol doc:
 *    absoluteClock = floor(max(ppq,0) * 24)
 *    phrase        = floor(absoluteClock / 96)
 *    songRow       = (phrase >> 4) & 0x7f;  chainRow = phrase & 0x0f;  tickOffset = absoluteClock % 96
 *  Negative / pre-roll ppq clamps to clock 0. The locate is EXACT on the six-clocks-per-row grid: unlike
 *  the phrase-granular prototype packet, a mid-phrase seek lands on its own row. It does not reconstruct
 *  notes, envelopes, tables, grooves or effects from earlier rows; risa applies its own sparse song-row
 *  and chain-row backward fallback from there. */
export function risaLocate(ppq: number): RisaLocate {
  const absoluteClock = Math.floor(Math.max(ppq, 0) * RISA_PPQN);
  const phrase = Math.floor(absoluteClock / RISA_CLOCKS_PER_PHRASE);
  return {
    songRow: (phrase >> 4) & 0x7f,
    chainRow: phrase & 0x0f,
    tickOffset: absoluteClock % RISA_CLOCKS_PER_PHRASE,
    absoluteClock,
  };
}

/** The 5-byte arm+locate packet (`F9 52 songRow chainRow tickOffset`). risa requires a fresh arm before
 *  every start; it gates playback and discards clocks queued for the old position. */
export function risaArmPacket(loc: RisaLocate): number[] {
  return [RISA_LOCATE_STATUS, RISA_LOCATE_SUB, loc.songRow & 0x7f, loc.chainRow & 0x0f, loc.tickOffset & 0x7f];
}
