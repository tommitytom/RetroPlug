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
//   B8  launching an EMPTY row is not a stop - it starts a stopped cart, on a different row.
//   B9  an empty row is the END OF THE SONG, and the two directions differ:
//         advancing into one wraps to the start of the song (it is NOT skipped over),
//         launching one scans BACK to the nearest playable row at or before it.
//
// v1 simplifications, all deliberate and all things the differential test will quantify rather than
// hide: groove 0 only (a phrase-level G command is ignored), no H hop handling, and no awareness of
// edits made on the device.

import type { Song } from "../model";
// The leaf, not ../runtime: this module is bundled into the DSP kernel (the audio thread), and the
// runtime barrel would drag the whole WRAM reader in behind one four-element constant.
import { CHANNELS } from "../runtime/types";
import {
  idlePosition,
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

/** How long each channel spends on each song row: `[channel][row]`, null where it has nothing playable.
 *  This is the ONLY thing the model consults about a song, which is what makes it pushable to a context
 *  that cannot decode a sav for itself - the DSP thread (docs/launchpad-plan.md M5). */
export type RowTicksTable = (number | null)[][];

/** Coerce a table that came from somewhere untrusted (a config blob, so across a JSON boundary) into
 *  exactly `CHANNELS.length` x `SONG_ROWS`. A short or ragged table would otherwise read `undefined` for
 *  a missing row, which is neither "playable" nor "empty" and quietly misbehaves. */
export function normaliseRowTicks(table: unknown): RowTicksTable {
  const src = Array.isArray(table) ? table : [];
  const out: RowTicksTable = [];
  for (let ch = 0; ch < CHANNELS.length; ch++) {
    const row = Array.isArray(src[ch]) ? (src[ch] as unknown[]) : [];
    const perRow: (number | null)[] = [];
    for (let r = 0; r < SONG_ROWS; r++) {
      const v = row[r];
      perRow.push(typeof v === "number" && Number.isFinite(v) && v > 0 ? v : null);
    }
    out.push(perRow);
  }
  return out;
}

/** Build the table from a decoded song. Extracted so the control plane can derive it once and push it
 *  somewhere the song itself cannot go, with a single implementation of the arithmetic either way. */
export function songRowTicks(song: Song): RowTicksTable {
  // Groove 0 only in v1. A phrase-level G command selects another groove, which would change the row
  // duration; the differential test is what will say whether that matters in practice.
  const ticksPerPhrase = phraseTicks(song.grooves?.[0]?.steps ?? []);
  const out: RowTicksTable = [];
  for (let ch = 0; ch < CHANNELS.length; ch++) {
    const perRow: (number | null)[] = [];
    for (let row = 0; row < SONG_ROWS; row++) {
      const chainIndex = song.rows?.[row]?.chains?.[ch];
      const chain = chainIndex === null || chainIndex === undefined ? undefined : song.chains?.[chainIndex];
      const phrases = chain ? chainPhraseCount(chain.phrases ?? []) : 0;
      perRow.push(phrases === 0 ? null : phrases * ticksPerPhrase);
    }
    out.push(perRow);
  }
  return out;
}

export class PredictedLsdjModel implements PredictivePlaybackModel {
  readonly channelCount = CHANNELS.length;

  private readonly cursors: Cursor[] = [];
  private readonly rowTicksCache: RowTicksTable;
  private playing = false;

  // Reused answers. `position()` and `grid()` are called at least once per update on the audio thread,
  // and both used to allocate - a fresh array plus four objects, and an object plus a closure. The grid
  // never changes for a given song; the position object is overwritten in place.
  private readonly livePosition: PlaybackPosition;
  private readonly idle: PlaybackPosition;
  private readonly gridView: PlaybackGrid;

  /** Takes a decoded song, or a prebuilt `RowTicksTable` for a caller that has the arithmetic but not
   *  the song - the DSP-thread controller role, handed its table through config. */
  constructor(source: Song | RowTicksTable) {
    this.rowTicksCache = Array.isArray(source) ? normaliseRowTicks(source) : songRowTicks(source);
    for (let ch = 0; ch < this.channelCount; ch++) this.cursors.push({ row: null, remaining: 0, playing: false });

    this.livePosition = idlePosition(this.channelCount);
    this.idle = idlePosition(this.channelCount);
    const cache = this.rowTicksCache;
    const channelCount = this.channelCount;
    this.gridView = {
      rowCount: SONG_ROWS,
      channelCount,
      hasContent(channel: number, row: number): boolean {
        if (channel < 0 || channel >= channelCount || row < 0 || row >= SONG_ROWS) return false;
        return cache[channel][row] !== null;
      },
    };
  }

  /** A model over a prebuilt table - the same thing the constructor accepts, named for the call site. */
  static fromRowTicks(table: RowTicksTable): PredictedLsdjModel {
    return new PredictedLsdjModel(table);
  }

  /** The next row this channel plays after `row`.
   *
   *  MEASURED (B9): an empty row is the END OF THE SONG, not a hole to step over. So this advances by
   *  exactly one and, if that row has nothing playable, wraps to the start - it does NOT scan forward
   *  for the next populated row. A song with a gap in the middle therefore loops its first section
   *  forever, which is what a real cart does and what an earlier "skip the gap" reading got wrong.
   *  Null when the channel has no content anywhere, in which case there is nothing to advance to. */
  private nextRow(channel: number, row: number): number | null {
    const ticks = this.rowTicksCache[channel];
    const next = row + 1;
    if (next < SONG_ROWS && ticks[next] !== null) return next;
    return this.firstRow(channel);
  }

  /** The row the song restarts at: the first playable one. Row 0 in every ordinary song - the scan
   *  exists so a song whose first rows are blank still has somewhere to wrap to. */
  private firstRow(channel: number): number | null {
    const ticks = this.rowTicksCache[channel];
    for (let r = 0; r < SONG_ROWS; r++) if (ticks[r] !== null) return r;
    return null;
  }

  /** Where a launch of `row` actually lands. MEASURED (B9): launching an empty row does not park and is
   *  not ignored - the cart scans BACK to the nearest playable row at or before it. Note the asymmetry
   *  with nextRow, which wraps forward: these are genuinely two different rules on the cart, not one
   *  rule seen twice. Null when the channel has nothing playable at or before `row`. */
  private landingRow(channel: number, row: number): number | null {
    const ticks = this.rowTicksCache[channel];
    for (let r = row; r >= 0; r--) if (ticks[r] !== null) return r;
    return null;
  }

  launch(row: number): void {
    if (row < 0 || row >= SONG_ROWS) return;
    this.playing = true;
    for (let ch = 0; ch < this.channelCount; ch++) {
      const landed = this.landingRow(ch, row);
      const ticks = landed === null ? null : this.rowTicksCache[ch][landed];
      // A channel with nothing playable at or before the launched row has nowhere to land, so it stays
      // silent - the one case where a launch leaves a channel with no position at all.
      this.cursors[ch] = landed === null || ticks === null
        ? { row: null, remaining: 0, playing: false }
        : { row: landed, remaining: ticks, playing: true };
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

  /** The current position. The returned object is REUSED between calls (this runs per audio block), so
   *  a caller that needs to keep a position across updates must copy it. Every consumer so far reads it
   *  and discards it within the same update. */
  position(): PlaybackPosition {
    if (!this.playing) return this.idle;
    const p = this.livePosition;
    let anyPlaying = false;
    for (let i = 0; i < this.cursors.length; i++) {
      const c = this.cursors[i];
      const out = p.channels[i];
      out.playing = c.playing;
      out.songRow = c.playing ? c.row : null;
      if (c.playing) anyPlaying = true;
    }
    p.playing = anyPlaying;
    return p;
  }

  /** Which cells hold something launchable. Immutable for a given song, so this is built once. */
  grid(): PlaybackGrid {
    return this.gridView;
  }

  /** Ticks this channel spends on this row - exposed for tests and for a "progress through the row"
   *  readout, which the surface may want later. Null when the channel has nothing there. */
  rowTicks(channel: number, row: number): number | null {
    if (channel < 0 || channel >= this.channelCount || row < 0 || row >= SONG_ROWS) return null;
    return this.rowTicksCache[channel][row];
  }
}
