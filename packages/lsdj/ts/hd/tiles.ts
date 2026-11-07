// The LSDj font's glyph vocabulary - ported from the old C++ `lsdj::FontTiles` (src/lsdj/LsdjCanvas.h on
// the ecs-linux branch). The enum ordinals ARE the tile indices into a font's 71-tile main glyph set, so
// these values index `FontView.tile(n)` directly (../rom/font.ts reads the same 130-byte-header + 71-tile
// layout the old Rom.h did, so the indices carry over unchanged).
//
// Everything here is a pure lookup over that vocabulary: ASCII → tile, nibble → tile, LSDj note byte →
// its 3-tile spelling, and command letter → tile.

import type { Command } from "../model";

/** Tile indices into a font's 71-glyph main set. Ordinals are load-bearing - do not reorder. */
export enum FontTiles {
  Note = 0,
  ArrowRight,
  Space,
  Num0,
  Num1,
  Num2,
  Num3,
  Num4,
  Num5,
  Num6,
  Num7,
  Num8,
  Num9,
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  Dash,
  Hash,
  QuestionMark,
  ExclamationMark,
  Copyright,
  Special,
  Comma,
  Period,
  Colon,
  Equals,
  SawInv,
  Saw,
  PanL,
  PanR,
  OK,
  ArrowLeft,
  SineInv,
  Sine,
  Duty1,
  Duty2,
  Plus,
  BracketLeft,
  Duty3,
  Duty4,
  Duty5,
  Underscore,
  Duty6,
  Duty7,
  Percent,
  BracketRight,
  Semicolon,
  Slash,
}

/** The 5 colour-sets of an LSDj palette, in ROM order. Ordinals index `RomPalette.colorSets`. */
export enum ColorSets {
  Normal = 0,
  Shaded,
  Alternate,
  Selection,
  Scroll,
}

export const COLOR_SET_COUNT = 5;
export const FONT_GLYPH_COUNT = 71;

/** One glyph of the LSDj font, by ASCII code point. Only the characters LSDj's font actually has are
 *  mapped (upper-case letters, digits, and three punctuation marks); anything else blanks to Space. */
export function findTile(code: number): FontTiles {
  switch (code) {
    case 0x2e: // .
      return FontTiles.Period;
    case 0x2d: // -
      return FontTiles.Dash;
    case 0x2f: // /
      return FontTiles.Slash;
  }
  if (code >= 0x30 && code <= 0x39) return FontTiles.Num0 + (code - 0x30); // 0-9
  if (code >= 0x41 && code <= 0x5a) return FontTiles.A + (code - 0x41); // A-Z
  return FontTiles.Space;
}

/** The digit glyph for a 0..9 value. Callers pass nibbles via the hex path, so 10..15 never reach here. */
export function findNumberTile(value: number): FontTiles {
  return FontTiles.Num0 + value;
}

// LSDj's command letters happen to be exactly the FontTiles letter of the same name, so the mapping is a
// name lookup rather than the old C++ switch over lsdj_command_t. "None" (and anything unrecognised)
// renders as a dash, matching an empty command column.
const COMMAND_TILES: Record<string, FontTiles> = {
  A: FontTiles.A, B: FontTiles.B, C: FontTiles.C, D: FontTiles.D, E: FontTiles.E, F: FontTiles.F,
  G: FontTiles.G, H: FontTiles.H, K: FontTiles.K, L: FontTiles.L, M: FontTiles.M, N: FontTiles.N,
  O: FontTiles.O, P: FontTiles.P, Q: FontTiles.Q, R: FontTiles.R, S: FontTiles.S, T: FontTiles.T,
  V: FontTiles.V, W: FontTiles.W, X: FontTiles.X, Y: FontTiles.Y, Z: FontTiles.Z,
};

export function getCommandTile(command: Command): FontTiles {
  return COMMAND_TILES[command] ?? FontTiles.Dash;
}

// Semitone offset within an octave → the note letter, plus whether it carries a sharp. LSDj spells every
// accidental as a sharp (no flats).
const NOTE_LETTERS: FontTiles[] = [
  FontTiles.C, FontTiles.C, FontTiles.D, FontTiles.D, FontTiles.E, FontTiles.F,
  FontTiles.F, FontTiles.G, FontTiles.G, FontTiles.A, FontTiles.A, FontTiles.B,
];
const NOTE_SHARP = [false, true, false, true, false, false, true, false, true, false, true, false];

/** Spell an LSDj note byte as its 3 tiles (letter, sharp-or-blank, octave digit). Note 0 is "no note" and
 *  spells "---"; otherwise the byte is 1-based, and LSDj displays octaves from 3 up. `out` is written in
 *  place so the caller can reuse one array per row. */
export function formatNote(note: number, out: FontTiles[]): void {
  if (note === 0) {
    out[0] = FontTiles.Dash;
    out[1] = FontTiles.Dash;
    out[2] = FontTiles.Dash;
    return;
  }

  const n = note - 1;
  const octave = Math.floor(n / 12);
  const step = n - octave * 12;

  out[0] = NOTE_LETTERS[step];
  out[1] = NOTE_SHARP[step] ? FontTiles.Hash : FontTiles.Space;
  out[2] = findNumberTile(octave + 3);
}
