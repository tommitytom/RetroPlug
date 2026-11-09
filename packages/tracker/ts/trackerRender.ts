// The render hooks a cart contributes (docs/modular-graph-phase-f4a-design.md section 4.1): what the shell's render
// library (packages/retroplug/src/render/render.ts) needs of a tracker to play and measure a saved song, as data and
// functions the cart owns. The library keeps its pipeline - the boot, the settle, the lead-in, the sink, the part files,
// the cancel points - and asks the integration for the parts that are the cart's: its songs, the SRAM a chosen one is
// seeded from (LSDj, risa), the live load a cart whose working song is work RAM needs (smsggdj), the button gesture that
// starts playback, the register or flag that says the song is still playing, and what a render of it should be warned it
// cannot do.
//
// The host and sink types are the shell's capabilities, narrowed to what these hooks use, so a hook is testable without a
// render: the library's RenderContext satisfies them structurally (render.ts builds the host over its backend and
// audio). Nothing here reads a file: the library passes the ROM and the battery bytes in, because resolving a sibling
// .sav is the host's (it owns the paths and the backend).
import type { SongInfo } from "./songCatalog";

/** The hold a play gesture's press needs to register, in whole milliseconds: the length of the chunk a gesture
 *  captures its head in. The render's pull length defaults to it too, so the lead-in a gesture returns is what its poll
 *  granularity counts per chunk; a cart reading its own value would skew the length cap. */
export const PLAY_HOLD_MS = 100;

/** What a render hook drives of the booted system: the cart's memory, its buttons and audio time. */
export interface RenderHost {
  /** The audio a hook discards while it works (a boot wait, a prep press): whole milliseconds, in the render's mix. */
  renderAudio(ms: number): void;
  /** A CPU register's value, or null when the core cannot be read: LSDj's NR52 sound-enable is the one such read. */
  readCpu(id: number, addr: number): number | null;
  /** The system's work RAM, or null when a snapshot is not available this block. */
  readRam(id: number): Uint8Array | null;
  /** Write into the system's work RAM; false when the range is refused (a wrong layout, a full command ring). */
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean;
  pressButton(id: number, button: number, down: boolean): void;
}

/** The render's own stream: a chunk in the shape the request asked for (one mix buffer, or one per stream), rendered
 *  through the sink so what a play gesture captures is the recording's head rather than audio thrown away. */
export interface RenderPlaySink {
  /** Render `ms` and return the chunk: mix is one interleaved-stereo buffer, a split one buffer per stream. */
  renderChunk(ms: number): Float32Array[];
}

/** What a render of a cart is told about the request, for the warnings it is the cart's to raise. */
export interface RenderFacts {
  /** The ROM's bytes, as the render read them for its classification (null when the file could not be read). */
  readonly rom: Uint8Array | null;
  /** The battery the render will boot from, or null when there is none to read. */
  readonly sav: Uint8Array | null;
  /** The request carries a savestate: its work RAM is the working song, whatever the battery holds. */
  readonly state: boolean;
  /** A song was selected (--song / --song-index), so the cart's own load has it. */
  readonly song: boolean;
  /** The length the request pins, if it does. */
  readonly durationMs: number | undefined;
  /** The cap a render of no pinned length runs to (and the auto-detect's bound). */
  readonly maxDurationMs: number;
}

/** One cart's render hooks. Absent values mean the render does without: no seedSong and no loadSong refuses a song
 *  flag, no pressPlay keeps the console's default gesture (its descriptor's `render.defaultPlay`), and no playingProbe
 *  renders a fixed length. */
export interface TrackerRender {
  /** The saved songs of a battery, for --list-songs and for the not-found and empty-slot messages. */
  listSongs(sav: Uint8Array): readonly SongInfo[];
  /** The length the cart stores a song's name at, where a long enough --song is a prefix of the stored name (LSDj's 8:
   *  "--song MYSONGNAME" matches the project "MYSONGNA"). Absent: the whole name is compared. */
  songNameLength?: number;
  /** How this cart spells its songs where the neutral form will not do (LSDj's "(no named projects)", smsggdj's
   *  "0:METRO"), so a message the render prints keeps the cart's own words. Absent: `index: name` per song and
   *  "(no saved songs)". */
  songList?: {
    /** The one line a selection error prints (its "songs: ..." tail). */
    listing?(songs: readonly SongInfo[]): string;
    /** What --list-songs prints under its header when the battery holds none. */
    empty: string;
  };
  /** Before the boot: the SRAM the fresh system boots from, with project `index` promoted to the working song (LSDj,
   *  risa). Null when the slot holds no song this build can promote, which the render refuses (see seedRefusal). */
  seedSong?(sav: Uint8Array, index: number): Uint8Array | null;
  /** Why a seedSong that answered null cannot promote `index`, in the cart's words: the render throws this before the
   *  boot rather than play the battery's own working song. Absent: the render's own sentence, naming the cart and the
   *  battery. */
  seedRefusal?(index: number): string;
  /** After the boot settle: write song `index` into the booted core (smsggdj, whose working song is work RAM). Throws
   *  on every failure path, because rendering the blank boot song instead would make a silent WAV that looks like a
   *  successful render. */
  loadSong?(host: RenderHost, id: number, rom: Uint8Array, sav: Uint8Array, index: number): void;
  /** Press play and return the lead-in chunks the press rendered (they are the recording's head). Absent: the console's
   *  default gesture, or none. */
  pressPlay?(host: RenderHost, id: number, rom: Uint8Array, sink: RenderPlaySink): Float32Array[][];
  /** The "still playing?" probe for length auto-detect, or null when this build cannot tell (the render then runs for
   *  the fixed length). `facts` is the request, so the reason can quote the length the render falls back to, and `warn`
   *  says it when the reason is the cart's, not the request's. */
  playingProbe?(
    host: RenderHost,
    id: number,
    rom: Uint8Array,
    facts: RenderFacts,
    warn: (msg: string) => void,
  ): (() => boolean) | null;
  /** What a render of this cart should be told it cannot do, through `warn`: a boot state that will be silent without
   *  a song. Called once, after the settle and before the song loads. */
  warnings?(facts: RenderFacts, warn: (msg: string) => void): void;
}
