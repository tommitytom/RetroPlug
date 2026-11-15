// The first controller app: launch LSDj song rows from the grid, and show where the song is.
//
// LAYOUT. The grid is LSDj's song screen, twice. Four columns are the four channels (pu1, pu2, wav, noi)
// and eight rows are eight song rows; the right half continues the song, so the whole 8x8 shows 16
// consecutive rows - exactly one LSDj song-screen page.
//
//       x=0 x=1 x=2 x=3 | x=4 x=5 x=6 x=7
//       pu1 pu2 wav noi | pu1 pu2 wav noi
//  y=0   r0  r0  r0  r0 |  r8  r8  r8  r8
//  ...
//  y=7   r7  r7  r7  r7 | r15 r15 r15 r15
//
// A column SHOWS a channel but cannot SELECT one: MI.MAP launches a whole song row and every channel
// jumps together (MEASURED, docs/launchpad-plan.md 2.5 B4), so pressing pu2 at row 5 does exactly what
// pressing pu1 at row 5 does. Four columns are worth half the grid anyway, because the channels then
// advance independently as their chains end - four cursors, not one - and four columns are the only way
// to see that.
//
// WHAT THIS APP DOES NOT HAVE. A stop. MI.MAP has no stop message, the 0xFE handshake does nothing to
// playback (B5), and launching an empty row does not stop a cart either - it starts a stopped one (B8).
// The only stop is the host transport, which is not a pad. Offering a stop pad would be a lie.

import { Palette, palette, type Led } from "@retroplug/launchpad";
import type { ControllerApp, ControllerCtx } from "@retroplug/launchpad/session";
import type { PlaybackPosition } from "@retroplug/tracker/playbackModel";

/** Columns of channels per pane, and the two panes that make up the 16-row window. */
export const CHANNELS_ACROSS = 4;
export const PANE_ROWS = 8;
export const WINDOW_ROWS = PANE_ROWS * 2;
/** Song rows addressable by the page control. 256 rows / 16 per page. */
export const MAX_PAGE = 15;

/** Ticks per quantise unit at 24 PPQN, in the HOST's clock. A `bar` assumes 4/4 - the block info carries no time
 *  signature, and 96 ticks is also exactly one 16-step LSDj phrase at the factory groove (B2), so it is the right unit
 *  for this tracker whatever the host's meter says. The cart's own beat and bar are these multiplied by its clock
 *  divisor (ControllerCtx.tickDivisor): it is clocked at 24 / divisor ticks a quarter, so at divisor 2 an LSDj phrase
 *  lasts two host bars and a `bar` launch that counted the host's would land mid-phrase. */
const TICKS_PER_BEAT = 24;
const TICKS_PER_BAR = 96;

export type Quantise = "immediate" | "beat" | "bar" | "hostBeat" | "hostBar" | "rowEnd";
export const QUANTISE_VALUES = ["immediate", "beat", "bar", "hostBeat", "hostBar", "rowEnd"] as const;

// Colours. Only the seven palette entries the manual names in prose have known meanings (the full
// 128-entry table is an image in the PDF), so the app is built from those rather than from invented
// indices - see ts/protocol.ts.
const OFF: Led = { mode: "off" };
const CONTENT: Led = { mode: "static", colour: palette(Palette.greenDim) };
const PLAYHEAD: Led = { mode: "static", colour: palette(Palette.greenBright) };
/** Pulse is synchronised to MIDI beat clock BY THE DEVICE, so a row waiting for its bar breathes on the
 *  beat for free - no per-update LED traffic to animate it. */
const CUED: Led = { mode: "pulse", colour: palette(Palette.yellow) };

interface MapState {
  page?: number;
  follow?: boolean;
  /** The row waiting for its quantise boundary, or null. */
  pending?: number | null;
  /** The tick that pending fires at (beat/bar), or null for the rowEnd rule. Derived each update from the press and
   *  the rule in force; a boundary a rule or divisor change has MOVED behind the playhead is re-snapped from now, so
   *  the launch never fires off the grid. */
  pendingTick?: number | null;
  /** The tick every press landed on, whatever the rule: the wait is re-derived from it each update, so a rule or a
   *  divisor changed while the row pulses yellow moves the wait with it, in either direction. Re-based to now when a
   *  moved boundary is re-snapped, so a later block derives that same tick. */
  pendingFromTick?: number | null;
  /** The row the followed channel was on when every press landed, as pendingFromTick is: the rowEnd rule reads it. */
  pendingFromRow?: number | null;
  /** What the grid was last painted from - see needsPaint. */
  painted?: { page: number; follow: boolean; pending: number | null; revision: number; rows: number[] };
}

/** The song row shown at grid position (x, y) for a window starting at `base`. */
export function rowAt(base: number, x: number, y: number): number {
  return base + (x >> 2) * PANE_ROWS + y;
}

/** The channel column shown at grid x. */
export function channelAt(x: number): number {
  return x & 3;
}

/** The row the display follows: the first playing channel's, in pu1/pu2/wav/noi order. Null when nothing
 *  is playing. Deliberately not an average or a maximum - those wander meaninglessly once the channels
 *  diverge, and they do diverge in ordinary play. */
export function followRow(position: PlaybackPosition): number | null {
  const channels = position.channels;
  for (let i = 0; i < channels.length; i++) {
    const c = channels[i];
    if (c.playing && c.songRow !== null) return c.songRow;
  }
  return null;
}

const clampPage = (p: number): number => Math.max(0, Math.min(MAX_PAGE, Math.trunc(p) || 0));

/** The tick a launch pressed at `tick` should fire on. Snaps UP to the next boundary, so a press landing
 *  exactly on one fires immediately rather than waiting a whole extra unit. `beat` and `bar` count the CART's clock at
 *  the grid the host clocks it on: one cart beat is `TICKS_PER_BEAT` host ticks times the divisor, from ppq 0, which is
 *  where the host's walk puts the cart's own ticks (a seek resyncs that walk, so the two can part company until the
 *  cart is restarted). `hostBeat` and `hostBar` count the host's, whatever the cart does. */
function boundaryTick(tick: number, quantise: Quantise, tickDivisor: number): number {
  const bar = quantise === "bar" || quantise === "hostBar";
  const cart = quantise === "beat" || quantise === "bar";
  const divisor = cart ? tickDivisor : 1; // the ctx promises an integer >= 1 (launchpad/ts/session.ts)
  const unit = (bar ? TICKS_PER_BAR : TICKS_PER_BEAT) * divisor;
  return Math.ceil(tick / unit) * unit;
}

export const lsdjMidiMap: ControllerApp = (c: ControllerCtx) => {
  const st = c.state as MapState;
  if (st.page === undefined) {
    st.page = clampPage((c.config.page as number) ?? 0);
    st.follow = c.config.follow !== false;
    st.pending = null;
    st.pendingTick = null;
    st.pendingFromTick = null;
    st.pendingFromRow = null;
  }

  const quantise = (c.config.quantise as Quantise) ?? "bar";
  const position = c.playback.position();
  const grid = c.playback.grid();
  const current = followRow(position);

  handleInput(c, st, quantise, position, current);
  resolvePending(c, st, quantise, current);

  // Follow AFTER resolving, so the update that fires a launch already shows the row it jumped to.
  if (st.follow) {
    const now = followRow(c.playback.position());
    if (now !== null && (now < st.page! * WINDOW_ROWS || now >= (st.page! + 1) * WINDOW_ROWS)) {
      st.page = clampPage(Math.floor(now / WINDOW_ROWS));
    }
  }

  const now = c.playback.position();
  if (needsPaint(st, grid, now)) paint(c, st, grid, now);
};

/** Whether anything the grid shows has changed since it was last painted: the page, follow, the cued row, any
 *  channel's playhead, or the song itself. The app runs every audio block and the answer is nearly always no, so
 *  this keeps the steady state to a handful of comparisons instead of repainting all 64 pads to find out. The
 *  surface keeps what was painted, so a reconnect's full repaint needs no paint here either. */
function needsPaint(st: MapState, grid: ReturnType<ControllerCtx["playback"]["grid"]>, pos: PlaybackPosition): boolean {
  const p = st.painted ?? (st.painted = { page: -1, follow: false, pending: null, revision: -1, rows: [] });
  const revision = grid.revision ?? -2; // no revision: cannot tell, so always repaint
  let changed = p.page !== st.page || p.follow !== st.follow || p.pending !== st.pending
    || p.revision !== revision || revision === -2;
  const channels = pos.channels;
  for (let i = 0; i < channels.length; i++) {
    const row = channels[i].playing && channels[i].songRow !== null ? channels[i].songRow! : -1;
    if (p.rows[i] !== row) {
      changed = true;
      if (i < p.rows.length) p.rows[i] = row;
      else p.rows.push(row);
    }
  }
  p.page = st.page!;
  p.follow = st.follow!;
  p.pending = st.pending ?? null;
  p.revision = revision;
  return changed;
}

function handleInput(
  c: ControllerCtx,
  st: MapState,
  quantise: Quantise,
  position: PlaybackPosition,
  current: number | null,
): void {
  const events = c.events;
  for (let e = 0; e < events.length; e++) {
    const ev = events[e];
    if (ev.kind !== "down") continue; // a release is not a stop, so there is nothing to do on "up"

    if (ev.button === "up") { st.page = clampPage(st.page! - 1); continue; }
    if (ev.button === "down") { st.page = clampPage(st.page! + 1); continue; }
    if (ev.button === "session") { st.follow = !st.follow; continue; }
    if (!ev.pad) continue;

    const row = rowAt(st.page! * WINDOW_ROWS, ev.pad.x, ev.pad.y);
    if (row > c.target.maxRow) continue; // rows 254/255 collide with the protocol's sentinels (B7)

    // Quantising against a cart that is not playing would wait for a boundary the cart will never reach,
    // so the first launch of a session is always immediate.
    if (quantise === "immediate" || !position.playing) {
      c.target.launch(row);
      st.pending = null;
      st.pendingTick = null;
      st.pendingFromTick = null;
      st.pendingFromRow = null;
      continue;
    }

    // Both of the press's facts are kept, whatever the rule: resolvePending derives the wait from the rule in force
    // on the block it reads, so the rule changing under a cued row is a re-derive and not a wait nothing ends.
    st.pending = row;
    st.pendingFromTick = c.tick;
    st.pendingFromRow = current;
    st.pendingTick = quantise === "rowEnd" ? null : boundaryTick(c.tick, quantise, c.tickDivisor);
  }
}

function resolvePending(c: ControllerCtx, st: MapState, quantise: Quantise, current: number | null): void {
  if (st.pending === null || st.pending === undefined) return;

  // The rule and the divisor are both per block (ControllerCtx.tickDivisor), and a cart bar is 96 times it: either
  // changed while this launch waits moves the wait with it, so the fire tick is derived from the press each update
  // rather than kept from the press. A boundary the new grid has MOVED behind the playhead - a divisor shrink, a rule
  // change to a grid the old tick is not on - is re-snapped forward on this grid, and the press is re-based to now so
  // a later block derives the same tick: the old tick is off the new grid, and firing at once is no boundary at all.
  // A boundary that did NOT change is left alone, even when a block (a large one, a seek) steps past it, so it fires
  // on that block as it always did rather than a whole unit late.
  if (quantise === "rowEnd") {
    st.pendingTick = null;
  } else if (st.pendingFromTick !== null && st.pendingFromTick !== undefined) {
    let next = boundaryTick(st.pendingFromTick, quantise, c.tickDivisor);
    if (next !== st.pendingTick && next < c.tick) {
      st.pendingFromTick = c.tick;
      next = boundaryTick(c.tick, quantise, c.tickDivisor);
    }
    st.pendingTick = next;
  }

  // rowEnd fires on the model's next row change, which works identically on the observed and predicted
  // paths and needs no "ticks remaining" accessor that only one of them could answer.
  const due = quantise === "rowEnd"
    ? current !== st.pendingFromRow
    : st.pendingTick !== null && st.pendingTick !== undefined && c.tick >= st.pendingTick;

  if (!due) return;
  c.target.launch(st.pending);
  st.pending = null;
  st.pendingTick = null;
  st.pendingFromTick = null;
  st.pendingFromRow = null;
}

function paint(
  c: ControllerCtx,
  st: MapState,
  grid: ReturnType<ControllerCtx["playback"]["grid"]>,
  position: PlaybackPosition,
): void {
  const base = st.page! * WINDOW_ROWS;
  const maxRow = c.target.maxRow;

  for (let y = 0; y < PANE_ROWS; y++) {
    for (let x = 0; x < CHANNELS_ACROSS * 2; x++) {
      const row = rowAt(base, x, y);
      const channel = channelAt(x);
      let led = OFF;
      if (row <= maxRow) {
        const ch = position.channels[channel];
        if (st.pending === row) led = CUED; // the whole row cues, because a launch is song-wide
        else if (ch && ch.playing && ch.songRow === row) led = PLAYHEAD;
        else if (grid.hasContent(channel, row)) led = CONTENT;
      }
      c.surface.setPad(x, y, led);
    }
  }

  c.surface.setButton("up", st.page! > 0 ? CONTENT : OFF);
  c.surface.setButton("down", st.page! < MAX_PAGE ? CONTENT : OFF);
  c.surface.setButton("session", st.follow ? PLAYHEAD : CONTENT);
}
