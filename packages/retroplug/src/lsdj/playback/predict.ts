// Dead reckoning for an LSDj cart we cannot see inside: a real Game Boy on the end of an Arduinoboy.
//
// In MI.MAP the cart is a clock SLAVE, so the host knows more than it might seem (docs/launchpad-plan.md
// 4.2): it generates the clock, it sent the launch, and it has the song file. What it cannot know is
// anything the player does on the handheld. So this simulates the one thing that IS deterministic - a
// sequencer walking song rows at a rate we are dictating - and the differential test measures how far
// that drifts from a real cart.
//
// Every rule here was MEASURED against lsdj9_3_3-arduinoboy.gb rather than taken from documentation
// (test-native/lsdj-playback-probe.test.ts, summarised in docs/launchpad-plan.md 2.5):
//
//   B2  6 clock ticks per phrase step, so a 16-step phrase is 96 ticks.
//   B3  the cart auto-advances one row per chain and WRAPS at the end of the song.
//   B4  a launch sets ALL channels to the row; they then advance independently as their chains end.
//   B6  a chain ends at its FIRST EMPTY phrase slot - so chain length is just "slots before the null".
//
// v1 simplifications, all deliberate and all things the differential test will quantify rather than
// hide: groove 0 only (a phrase-level G command is ignored), no H hop handling, and no awareness of
// edits made on the device.

import type { Song } from "../model";
import { CHANNELS } from "../runtime";
import {
  idlePosition,
  type ChannelPosition,
  type PlaybackGrid,
  type PlaybackPosition,
  type PredictivePlaybackModel,
} from "../../tracker/playbackModel";

/** MEASURED (B2): steps per phrase, and the fallback tick count for a step. */
const STEPS_PER_PHRASE = 16;
const DEFAULT_TICKS_PER_STEP = 6;
const SONG_ROWS = 256;

/** Ticks one phrase takes under `groove`. LSDj cycles the groove's non-zero prefix across the phrase's
 *  16 steps, so the factory 6/6 groove gives 96 ticks - one bar at 24 PPQN. A groove of all zeroes would
 *  never advance the cart at all, so it falls back to the factory value rather than dividing by nothing. */
export function phraseTicks(grooveSteps: readonly number[]): number {
  const prefix: number[] = [];
  for (const s of grooveSteps) {
    if (s === 0) break;
    prefix.push(s);
  }
  if (prefix.length === 0) return STEPS_PER_PHRASE * DEFAULT_TICKS_PER_STEP;
  let total = 0;
  for (let i = 0; i < STEPS_PER_PHRASE; i++) total += prefix[i % prefix.length];
  return total;
}

/** How many phrases a chain plays: slots up to the first empty one (MEASURED, B6 - a chain with a hole
 *  ends at the hole, and the phrase past it never sounds). 0 means the chain is not playable at all. */
export function chainPhraseCount(phrases: readonly (number | null)[]): number {
  let n = 0;
  for (const p of phrases) {
    if (p === null || p === undefined) break;
    n++;
  }
  return n;
}

/** Per-channel cursor. `remaining` counts down the ticks left in the chain currently playing. */
interface Cursor {
  row: number | null;
  remaining: number;
  playing: boolean;
}

export class PredictedLsdjModel implements PredictivePlaybackModel {
  readonly channelCount = CHANNELS.length;

  private readonly cursors: Cursor[] = [];
  private readonly rowTicksCache: (number | null)[][] = []; // [channel][row], null = no content
  private readonly ticksPerPhrase: number;
  private playing = false;

  constructor(song: Song) {
    // Groove 0 only in v1. A phrase-level G command selects another groove, which would change the row
    // duration; the differential test is what will say whether that matters in practice.
    this.ticksPerPhrase = phraseTicks(song.grooves?.[0]?.steps ?? []);

    for (let ch = 0; ch < this.channelCount; ch++) {
      this.cursors.push({ row: null, remaining: 0, playing: false });
      const perRow: (number | null)[] = [];
      for (let row = 0; row < SONG_ROWS; row++) perRow.push(this.computeRowTicks(song, ch, row));
      this.rowTicksCache.push(perRow);
    }
  }

  /** Ticks this channel spends on this song row, or null when it has nothing playable there. */
  private computeRowTicks(song: Song, channel: number, row: number): number | null {
    const chainIndex = song.rows?.[row]?.chains?.[channel];
    if (chainIndex === null || chainIndex === undefined) return null;
    const chain = song.chains?.[chainIndex];
    if (!chain) return null;
    const phrases = chainPhraseCount(chain.phrases ?? []);
    return phrases === 0 ? null : phrases * this.ticksPerPhrase;
  }

  /** The next row this channel plays after `row`: the following row with content, wrapping to the first
   *  one when the song runs out (MEASURED, B3 - the cart wraps rather than stopping). Null when the
   *  channel has no content anywhere, in which case there is nothing to advance to. */
  private nextRow(channel: number, row: number): number | null {
    const ticks = this.rowTicksCache[channel];
    for (let r = row + 1; r < SONG_ROWS; r++) if (ticks[r] !== null) return r;
    for (let r = 0; r <= row; r++) if (ticks[r] !== null) return r;
    return null;
  }

  launch(row: number): void {
    if (row < 0 || row >= SONG_ROWS) return;
    this.playing = true;
    for (let ch = 0; ch < this.channelCount; ch++) {
      const ticks = this.rowTicksCache[ch][row];
      // A channel with nothing at the launched row parks there silently. It has no chain to time, so
      // nothing advances it - the cart's behaviour here is untested, and is called out in the plan.
      this.cursors[ch] = ticks === null
        ? { row, remaining: 0, playing: false }
        : { row, remaining: ticks, playing: true };
    }
    // MEASURED: the launch byte itself counts as the cart's first tick. Three independent observations
    // agree - the first phrase step lands at tick 5 rather than 6, the first row change at 95 rather
    // than 96, and the differential sweep peaks at exactly +1 (lsdj-playback-differential). Folding it
    // in here keeps the caller's contract simple: send the cart a launch, call launch(); send it a
    // clock byte, call advance(1). Leave it out and every consumer has to know this quirk.
    this.advance(1);
  }

  advance(ticks: number): void {
    if (!this.playing || ticks <= 0) return;
    for (let ch = 0; ch < this.channelCount; ch++) {
      const cur = this.cursors[ch];
      if (!cur.playing || cur.row === null) continue;
      let left = ticks;
      // A loop, not a subtraction: a big tick step (a slow UI frame, a long audio block) can carry a
      // channel through several short chains at once, and each crossing has to move the row.
      while (left > 0) {
        if (left < cur.remaining) {
          cur.remaining -= left;
          break;
        }
        left -= cur.remaining;
        const next = this.nextRow(ch, cur.row);
        if (next === null) {
          cur.playing = false;
          cur.remaining = 0;
          break;
        }
        cur.row = next;
        cur.remaining = this.rowTicksCache[ch][next] ?? 0;
        if (cur.remaining === 0) {
          cur.playing = false;
          break;
        }
      }
    }
  }

  stop(): void {
    this.playing = false;
    for (const c of this.cursors) c.playing = false;
  }

  reset(): void {
    this.playing = false;
    for (let ch = 0; ch < this.channelCount; ch++) this.cursors[ch] = { row: null, remaining: 0, playing: false };
  }

  position(): PlaybackPosition {
    if (!this.playing) return idlePosition(this.channelCount);
    const channels: ChannelPosition[] = this.cursors.map((c) => ({ playing: c.playing, songRow: c.playing ? c.row : null }));
    return { playing: channels.some((c) => c.playing), channels };
  }

  grid(): PlaybackGrid {
    const cache = this.rowTicksCache;
    const channelCount = this.channelCount;
    return {
      rowCount: SONG_ROWS,
      channelCount,
      hasContent(channel: number, row: number): boolean {
        if (channel < 0 || channel >= channelCount || row < 0 || row >= SONG_ROWS) return false;
        return cache[channel][row] !== null;
      },
    };
  }

  /** Ticks this channel spends on this row - exposed for tests and for a "progress through the row"
   *  readout, which the surface may want later. Null when the channel has nothing there. */
  rowTicks(channel: number, row: number): number | null {
    if (channel < 0 || channel >= this.channelCount || row < 0 || row >= SONG_ROWS) return null;
    return this.rowTicksCache[channel][row];
  }
}
