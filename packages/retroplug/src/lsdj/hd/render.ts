// The HD player's layout - a TS port of `Ui::renderMode2` and its helpers (src/lsdj/LsdjUi.cpp on the
// ecs-linux branch). One wide screen showing, side by side: the song order down the left, then all four
// channels' currently-playing chains, then all four channels' currently-playing phrases, with playback
// arrows tracking the running cart.
//
// Everything here is pure - it takes the decoded song, the live runtime state and a canvas, and draws.
// No React, no backend, no I/O, so it is directly unit-testable against a golden tile grid.

import type { Chain, Phrase, Song } from "../model";
import { CHANNELS, type LsdjState } from "../runtime";
import { LsdjHdCanvas } from "./canvas";
import { ColorSets, FontTiles, formatNote, getCommandTile } from "./tiles";

/** Looks up a kit sample's 3-character name. Supplied by the caller so the ROM reads can be cached across
 *  frames - `LsdjRom.kit(k).sampleName(s)` re-reads the cartridge on every call. */
export type KitSampleNameLookup = (kit: number, sample: number) => string;

const CHANNEL_NAMES = ["PU1", "PU2", "WAV", "NOI"];

// Column geometry, in tiles, straight from the original.
const CHAIN_OFFSET_X = 17;
const PHRASE_OFFSET_X = CHAIN_OFFSET_X + 15;
const PHRASE_WIDTH = 17;
const CHAIN_BLOCK_HEIGHT = 18;

// The widest a phrase row gets: a KIT instrument shifts the command column right by 2, putting the
// command value at local x 12..13. Non-kit rows stop at 11.
const PHRASE_ROW_TILES = 14;

/** The grid the HD view needs, in tiles. The width is set by the LAST phrase column: its body starts at
 *  PHRASE_OFFSET_X + 3 * PHRASE_WIDTH + 2 and runs PHRASE_ROW_TILES wide. The original was hard-coded to
 *  97 columns (776 px), two short - so a kit-instrument row in the 4th channel's phrase column had its
 *  command value clipped off the right edge (the old canvas logged "out of range" and dropped the tile). */
export const HD_COLS = PHRASE_OFFSET_X + PHRASE_WIDTH * 3 + 2 + PHRASE_ROW_TILES;
export const HD_ROWS = 72;

// LSDj parks a stopped channel's position registers at 0xFF; the TS reader surfaces that as null, so the
// comparisons below (which the original made against the raw byte) go through this.
const NO_POS = 0xff;
const pos = (v: number | null): number => (v == null ? NO_POS : v);

/** Bookmarked song rows shade differently. LSDj stores 16 bookmark slots per channel, each holding a row
 *  index (0xFF = empty), in the 0x40-byte bookmarks block. */
function isRowBookmarked(song: Song, channel: number, row: number): boolean {
  const base = channel * 16;
  for (let i = 0; i < 16; i++) if (song.bookmarks[base + i] === row) return true;
  return false;
}

/** The chain playing on `channel`, plus its index. A null index is LSDj's "no chain" (the original's
 *  0xFF), and the index is still drawn in that case - the header shows FF. */
function chainAt(song: Song, channel: number, songRow: number): { chain: Chain | null; index: number } {
  const row = song.rows[songRow & 0xff];
  const index = row ? row.chains[channel] : null;
  if (index == null) return { chain: null, index: NO_POS };
  return { chain: song.chains[index] ?? null, index };
}

// ---- the three data grids --------------------------------------------------------

/** The song order: 4 channels x N rows of chain indices, with a playback arrow per active channel and the
 *  edit cursor highlighted. Drawn at the caller's translation. */
export function renderSongData(canvas: LsdjHdCanvas, song: Song, state: LsdjState, rowOffset = 0): void {
  const songRowCount = canvas.rows - 2;
  // The runtime reader reports the cursor for whichever screen is showing, so it only means a song-grid
  // position while the cart is actually on the song screen.
  const cursor = state.screen === "song" ? state.cursor : null;

  for (let y = 0; y < songRowCount; y++) {
    const row = y + rowOffset;
    for (let x = 0; x < 4; x++) {
      let colorSet = isRowBookmarked(song, x, row) ? ColorSets.Shaded : ColorSets.Normal;
      if (cursor && cursor.col === x && cursor.row === y) colorSet = ColorSets.Selection;

      const xOff = x * 3;
      const ch = state.channels[CHANNELS[x]];
      if (ch.playing && ch.songRow === row) canvas.drawTile(xOff, y, FontTiles.ArrowRight, ColorSets.Normal);

      const idx = song.rows[row & 0xff]?.chains[x] ?? null;
      if (idx != null) canvas.hexNumber(xOff + 1, y, idx, colorSet);
      else canvas.text(xOff + 1, y, "--", colorSet);
    }
  }
}

/** One chain's 16 steps: phrase index + transposition, with a playback arrow on the playing step. */
export function renderChainData(canvas: LsdjHdCanvas, chain: Chain | null, state: LsdjState, channel: number): void {
  const ch = state.channels[CHANNELS[channel]];
  for (let i = 0; i < 16; i++) {
    if (ch.playing && pos(ch.chainRow) === i) canvas.drawTile(0, i, FontTiles.ArrowRight, ColorSets.Normal);

    const phraseIndex = chain ? chain.phrases[i] : null;
    const transpose = chain ? chain.transpositions[i] : 0;

    if (phraseIndex != null) canvas.hexNumber(1, i, phraseIndex, ColorSets.Normal);
    else canvas.text(1, i, "--", ColorSets.Normal);

    canvas.hexNumber(4, i, transpose, ColorSets.Normal);
  }
}

/** One phrase's 16 steps: note (or the two kit sample names), instrument, and command + value.
 *  `playbackOffset` is the step to mark with an arrow, or 0xFF for none. */
export function renderPhraseData(
  canvas: LsdjHdCanvas,
  song: Song,
  phrase: Phrase | null,
  playbackOffset: number,
  kitSampleName: KitSampleNameLookup,
): void {
  if (!phrase) {
    // An empty column still draws its skeleton, so the grid doesn't visually collapse.
    for (let i = 0; i < 16; i++) {
      canvas.text(1, i, "---", ColorSets.Normal);
      canvas.text(5, i, "I", ColorSets.Shaded);
      canvas.text(6, i, "--", ColorSets.Normal);
      canvas.text(9, i, "-", ColorSets.Shaded);
      canvas.text(10, i, "00", ColorSets.Normal);
    }
    return;
  }

  const noteTiles: FontTiles[] = [FontTiles.Space, FontTiles.Space, FontTiles.Space];

  for (let i = 0; i < 16; i++) {
    const instrumentIndex = phrase.instruments[i];
    const instrument = instrumentIndex != null ? song.instruments[instrumentIndex] ?? null : null;
    const cmd = phrase.commands[i];
    const cmdValue = phrase.commandValues[i];
    const note = phrase.notes[i];

    if (i === playbackOffset) canvas.drawTile(0, i, FontTiles.ArrowRight, ColorSets.Normal);

    // A kit instrument reinterprets the note byte as two 4-bit sample slots, and the row widens to fit
    // both sample names - which pushes the instrument and command columns right.
    let instrumentOffset = 0;
    let commandOffset = 3;
    let drawn = false;

    if (instrument && instrument.type === "kit") {
      const sample1 = note >> 4;
      const sample2 = note & 0x0f;

      if (sample1 === 0) canvas.text(1, i, "---", ColorSets.Shaded);
      else canvas.text(1, i, kitSampleName(instrument.kit1, sample1 - 1), ColorSets.Shaded);

      if (sample2 === 0) canvas.text(4, i, "---", ColorSets.Normal);
      else canvas.text(4, i, kitSampleName(instrument.kit2, sample2 - 1), ColorSets.Normal);

      instrumentOffset = 3;
      commandOffset = 5;
      drawn = true;
    }

    if (!drawn) {
      formatNote(note, noteTiles);
      for (let t = 0; t < 3; t++) canvas.drawTile(1 + t, i, noteTiles[t], ColorSets.Normal);
    }

    canvas.text(5 + instrumentOffset, i, "I", ColorSets.Shaded);
    if (instrumentIndex == null) canvas.text(6 + instrumentOffset, i, "--", ColorSets.Normal);
    else canvas.hexNumber(6 + instrumentOffset, i, instrumentIndex, ColorSets.Normal);

    canvas.drawTile(6 + commandOffset, i, getCommandTile(cmd), ColorSets.Shaded);

    if (cmd === "O") {
      // Panning is spelled out as L/R rather than a hex value.
      if (cmdValue === 0) {
        canvas.drawTile(7 + commandOffset, i, FontTiles.L, ColorSets.Normal);
        canvas.drawTile(8 + commandOffset, i, FontTiles.R, ColorSets.Normal);
      } else if (cmdValue === 1) {
        canvas.drawTile(7 + commandOffset, i, FontTiles.L, ColorSets.Normal);
      } else if (cmdValue === 2) {
        canvas.drawTile(8 + commandOffset, i, FontTiles.R, ColorSets.Normal);
      }
    } else {
      canvas.hexNumber(7 + commandOffset, i, cmdValue, ColorSets.Normal);
    }
  }
}

// ---- the whole screen ------------------------------------------------------------

/** Draw the full HD view. The caller flushes the canvas afterwards. */
export function renderMode2(
  canvas: LsdjHdCanvas,
  song: Song,
  state: LsdjState,
  kitSampleName: KitSampleNameLookup,
): void {
  const { cols, rows } = canvas;
  const rowOffset = 0;

  canvas.setTranslation(0, 0);
  canvas.fill(0, 0, cols, rows, ColorSets.Normal, 0);
  canvas.text(0, 0, "SONG", ColorSets.Normal);

  const songRowCount = rows - 2;
  for (let y = 0; y < songRowCount; y++) canvas.hexNumber(0, y + 2, y + rowOffset, ColorSets.Alternate);

  // Vertical rules separating the song / chain / phrase groups.
  canvas.setTranslation(2, 0);
  canvas.fill(CHAIN_OFFSET_X - 4, 0, 1, rows, ColorSets.Normal, 1);
  canvas.fill(PHRASE_OFFSET_X - 4, 0, 1, rows, ColorSets.Normal, 1);

  canvas.setTranslation(2, 2);
  renderSongData(canvas, song, state, rowOffset);

  // Chains - one 16-step block per channel, stacked.
  for (let i = 0; i < 4; i++) {
    const ch = state.channels[CHANNELS[i]];
    const { chain, index } = chainAt(song, i, pos(ch.songRow));

    canvas.setTranslation(CHAIN_OFFSET_X, i * CHAIN_BLOCK_HEIGHT);
    canvas.text(0, 0, "CHAIN", ColorSets.Normal);
    canvas.hexNumber(6, 0, index, ColorSets.Normal);
    canvas.text(9, 0, CHANNEL_NAMES[i], ColorSets.Shaded);

    canvas.setTranslation(CHAIN_OFFSET_X, i * CHAIN_BLOCK_HEIGHT + 2);
    for (let j = 0; j < 16; j++) canvas.hexNumber(0, j, j, ColorSets.Alternate, false);

    // (The original re-drew the chain body once per row number, 16 times over; it is idempotent, so
    // drawing it once is identical output for a sixteenth of the work.)
    canvas.setTranslation(CHAIN_OFFSET_X + 1, i * CHAIN_BLOCK_HEIGHT + 2);
    renderChainData(canvas, chain, state, i);
  }

  // Phrases - one column per channel, each showing the four phrases around the playing one.
  for (let i = 0; i < 4; i++) {
    const columnX = PHRASE_OFFSET_X + PHRASE_WIDTH * i;
    const ch = state.channels[CHANNELS[i]];
    const { chain } = chainAt(song, i, pos(ch.songRow));
    const chainRow = pos(ch.chainRow);
    const groupOffset = chainRow !== NO_POS ? Math.floor(chainRow / 4) : 0;

    canvas.setTranslation(columnX, 0);
    canvas.text(0, 0, "PHRASE", ColorSets.Normal);

    let phraseIndex: number | null = null;
    if (chain) {
      phraseIndex = chain.phrases[chainRow === NO_POS ? 0 : chainRow];
      canvas.hexNumber(7, 0, phraseIndex ?? NO_POS, ColorSets.Normal);
    }

    canvas.text(11, 0, CHANNEL_NAMES[i], ColorSets.Shaded);

    for (let j = 0; j < 64; j++) canvas.hexNumber(0, j + 2, j + groupOffset * 64, ColorSets.Alternate, true);

    for (let j = 0; j < 4; j++) {
      canvas.setTranslation(columnX + 2, j * 16 + 2);

      const group = j + groupOffset * 4;
      const stepPhraseIndex = chain ? chain.phrases[group] : null;
      const phrase = stepPhraseIndex != null ? song.phrases[stepPhraseIndex] ?? null : null;

      // Only the phrase that is actually playing gets the row arrow - the same phrase can appear more
      // than once in a chain, so it is matched by index too.
      let phraseOffset = NO_POS;
      if (phrase && chainRow === group && stepPhraseIndex === phraseIndex) phraseOffset = pos(ch.phraseRow);

      renderPhraseData(canvas, song, phrase, phraseOffset, kitSampleName);
    }
  }
}
