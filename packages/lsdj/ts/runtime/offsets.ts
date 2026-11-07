// LSDj WRAM offset table, keyed by ROM version. Every offset is WRAM-relative (absolute addr - 0xC000).
//
// PROVENANCE — layered, cross-validated against each other:
//   * legacyOffsets.generated.ts — the old RetroPlug `ecs` OffsetLookupData ported to version →
//     [active, phraseRows, chainRows, songRows, songCursorCol, songCursorRow], covering v4.0.0..9.2.x
//     (the range the original tool supported). Exact per-version positional + SONG cursor.
//   * driftLayouts.generated.ts — the FULL per-field drifting layout (CURRENT_SCREEN + TEMPO + every
//     per-screen cursor) detected field-by-field on a real core (detect.ts detectDriftLayout), v4.3.0+.
//     AUTHORITATIVE: the drifting block is non-rigid (TEMPO and the TABLE cursor drift independently of
//     the screen/song/chain/phrase/instrument cluster), so a single shift can't reproduce it.
//   * driftShifts.generated.ts — the older rigid single-integer shift of the DRIFTING block vs the
//     LSDisJ 9.2.L reference, v8.2.1..9.4.2; a FALLBACK for versions not yet in driftLayouts. For
//     versions in both the legacy and shift tables, (songCursorCol - 0x41E) equals the shift exactly.
//   * REF_DRIFT below — the LSDisJ 9.2.L reference the shift applies to.
//
// Resolution per version: POSITIONAL from the legacy table (exact) or, for versions past it (9.2-letter
// builds, 9.3+), the modern band. DRIFTING (screen/tempo/cursors) from driftLayouts where present, else
// the rigid shift, else just the SONG cursor from the legacy table with screen/tempo null. A version with
// no positional layout at all → null (supported:false).
import type { CursorOffset, LsdjVersion, OffsetLayout, Screen } from "./types";
import { driftLayouts } from "./driftLayouts.generated";
import { driftShifts } from "./driftShifts.generated";
import { legacyOffsets } from "./legacyOffsets.generated";

interface Positional {
  active: number;
  phrases: number | null;
  phraseRows: number;
  chains: number | null;
  chainRows: number;
  songRows: number;
}

// The modern (v7.5.9+, 224-block) positional layout — the fallback for versions the legacy table doesn't
// list (the 9.2 letter builds, and 9.3+). The contiguous PLAYING_* block puts the phrase/chain NUMBER
// registers 4/0 bytes ahead of their row registers.
const MODERN_POS: Positional = {
  active: 0x0e0, // ARE_CHANNELS_PLAYING
  phrases: 0x168, // PLAYING_PHRASES
  phraseRows: 0x16c, // PLAYING_PHRASE_ROWS
  chains: 0x170, // PLAYING_CHAINS
  chainRows: 0x17c, // PLAYING_CHAIN_ROWS
  songRows: 0x200, // PLAYING_SONG_ROWS
};
// Only fall back to MODERN_POS for versions at/after this (leaves pre-v7.6 unknown-versions unsupported
// rather than guessing the 224 block for them). Legacy covers everything below anyway.
const MODERN_FALLBACK_FROM = { major: 7, minor: 6 };

// --- Drifting block (per-version shift over the LSDisJ 9.2.L reference) -----------------------------
interface Drifting {
  tempo: number;
  currentScreen: number;
  cursors: Partial<Record<Screen, CursorOffset>>;
}
const REF_DRIFT: Drifting = {
  tempo: 0x52a, // TEMPO
  currentScreen: 0x402, // CURRENT_SCREEN
  cursors: {
    song: { col: 0x41e, row: 0x41f }, // SONG_CURSOR_COL / _ROW
    chain: { col: 0x41a, row: 0x41b }, // CHAIN_CURSOR_COL / _ROW
    phrase: { col: 0x416, row: 0x417 }, // PHRASE_CURSOR_COL / _ROW
    instrument: { col: 0x429, row: 0x428 }, // INSTR_CURSOR_COL / _ROW
    table: { col: 0x92d, row: 0x92e }, // TABLE_CURSOR_COL / _ROW
  },
};

function shiftDrift(d: Drifting, shift: number): Drifting {
  const cursors: Partial<Record<Screen, CursorOffset>> = {};
  for (const k of Object.keys(d.cursors) as Screen[]) {
    const c = d.cursors[k]!;
    cursors[k] = { col: c.col + shift, row: c.row + shift };
  }
  return { tempo: d.tempo + shift, currentScreen: d.currentScreen + shift, cursors };
}

// Key = "major.minor.patchLabel" plus the build tag when present (e.g. "9.3.3-aboy"). Including the
// build keeps forks like arduinoboy — whose layout can differ from stock — from inheriting stock's
// offsets; an undetected fork stays unknown rather than wrong.
function versionKey(v: LsdjVersion): string {
  const base = `${v.major}.${v.minor}.${v.patchLabel}`;
  return v.build ? `${base}-${v.build}` : base;
}

function positionalFor(v: LsdjVersion, key: string): Positional | null {
  const legacy = legacyOffsets[key];
  if (legacy) {
    const [active, phraseRows, chainRows, songRows] = legacy;
    // The phrase/chain NUMBER registers are only known for the contiguous modern-layout block
    // (phraseRows 0x16C / chainRows 0x17C) — v4.9.6+. The ancient relocated block leaves them unknown.
    const modernBlock = phraseRows === 0x16c && chainRows === 0x17c;
    return {
      active,
      phrases: modernBlock ? 0x168 : null,
      phraseRows,
      chains: modernBlock ? 0x170 : null,
      chainRows,
      songRows,
    };
  }
  if (v.major > MODERN_FALLBACK_FROM.major || (v.major === MODERN_FALLBACK_FROM.major && v.minor >= MODERN_FALLBACK_FROM.minor)) {
    return { ...MODERN_POS };
  }
  return null;
}

/** Resolve the WRAM offset layout for a parsed version, or null when no positional layout is known.
 *  Drifting fields (tempo/currentScreen/all cursors) come from, in precedence: (1) the full per-field
 *  detected layout (driftLayouts, authoritative — the block is non-rigid, so tempo/table can differ from
 *  the shift); (2) the rigid single-shift model (driftShifts) for v8.2.1+ not yet in driftLayouts; (3)
 *  the legacy SONG cursor only, with screen/tempo null. */
export function layoutForVersion(v: LsdjVersion): OffsetLayout | null {
  const key = versionKey(v);
  const pos = positionalFor(v, key);
  if (!pos) return null;

  // 1) Full per-field drift layout, detected on a real core — authoritative where present.
  const full = driftLayouts[key];
  if (full) return { ...pos, tempo: full.tempo, currentScreen: full.currentScreen, cursors: full.cursors };

  // 2) Rigid single-shift model (a v8.2.1+ build the full detector hasn't covered yet).
  const shift = driftShifts[key];
  if (shift !== undefined) {
    const d = shiftDrift(REF_DRIFT, shift);
    return { ...pos, tempo: d.tempo, currentScreen: d.currentScreen, cursors: d.cursors };
  }

  // 3) No drift data: the legacy table still gives the SONG cursor; screen/tempo are unknown.
  const legacy = legacyOffsets[key];
  const cursors = legacy ? { song: { col: legacy[4], row: legacy[5] } } : null;
  return { ...pos, tempo: null, currentScreen: null, cursors };
}
