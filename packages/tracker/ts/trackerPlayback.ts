// The playback half of a tracker integration (docs/modular-graph-phase-f4a-design.md section 4.2): what the control
// plane polls of a cart to follow the song it is playing, and what it reads of a cart's memory to know where the
// playhead is. A cart contributes it to its integration, beside its song catalog and its render hooks, and the shell
// asks the integration its roles name - so the controller's poll and the graph's placement name no cart and import
// none.
//
// The state a PlaybackReader answers with is the cart's own: the shell passes it back to the same integration's
// anchor.playing / anchor.rows, which read it, so the type stays unknown here.
//
// A system whose integration has no playback reads "no sync mode, no signature, no timing table, no reader", and the
// gate is the ROM's ROLES, not its battery: a ROM whose title names no tracker reads none of the four even beside a
// battery a tracker could read, which is the widening section 6's E3 records (test/tracker/playback-absent.test.ts
// pins what LSDj's own functions answer for a battery that is not its own, which is the same answer but for a
// different reason).

/** What the control plane polls of a cart, and how it reads one's position. */
export interface TrackerPlayback {
  /** The cart's own SYNC setting as its battery spells it ("MidiMap", "Lsdj", ...), or null when the image is not
   *  this cart's (a row launch into a cart that is not in MI.MAP is ignored without complaint, and the setting is the
   *  only sign). Polled. */
  syncMode(sram: Uint8Array | null): string | null;
  /** A cheap change signature over the parts of the battery the song's structure lives in, 0 for anything unreadable:
   *  an edit made inside the tracker is invisible to everything else, and this is what notices it. */
  songSignature(sram: Uint8Array | null): number;
  /** The per-song-row clock table the controller's predictor runs on, or null when the battery holds no song this
   *  build can read. Derived here because decoding a battery is a control-plane job: the audio thread has neither the
   *  bytes nor any business running a codec. */
  rowTicks(sram: Uint8Array | null): RowTicksTable | null;
  /** Reading a running cart: the reader itself, and what its state means. */
  readonly anchor: PlaybackAnchor;
}

/** The row-timing table the predictor runs on: `[channel][row]`, the clock ticks that row holds on that channel, or
 *  null for a channel with no chain there. */
export type RowTicksTable = (number | null)[][];

/** Decodes a cart's work RAM into the state its own `anchor.playing` / `anchor.rows` read. Built once per ROM from
 *  the header, not once per poll: the layout it decodes is the build's, which does not change while the system runs. */
export interface PlaybackReader {
  read(wram: Uint8Array): unknown;
}

/** Where a cart's playhead is, read from its live state. */
export interface PlaybackAnchor {
  /** The reader for a ROM header prefix (the cart's build, which decides the memory layout), or null for a version
   *  this build has no layout for. */
  reader(header: Uint8Array): PlaybackReader | null;
  /** Whether anything is playing at all: the signal a start edge is detected on. Reads what `reader` returned. */
  playing(state: unknown): boolean;
  /** Each channel's own song row, or null where it is not playing. Per channel rather than one row for the song,
   *  because channels genuinely diverge as their chains end. */
  rows(state: unknown): (number | null)[];
}

/** Where the cart was seen starting playback on its OWN, as the controller node is told: a start is a rare, discrete
 *  event that fits the "push it as role config" path, where streaming a position every block would not. */
export interface ControllerAnchor {
  rows: (number | null)[];
  /** Bumped once per anchoring event. The node re-anchors when this CHANGES, so the same anchor riding along on later
   *  structure pushes is applied once rather than re-applied forever. */
  seq: number;
}
