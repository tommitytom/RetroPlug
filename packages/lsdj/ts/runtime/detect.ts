// Offline detector for the drifting WRAM block (plan Part C). Boots an LSDj ROM, drives it, and finds
// the per-version SHIFT of the drifting block (tempo/CURRENT_SCREEN/cursors) relative to the LSDisJ
// 9.2.L reference — the single integer offsets.ts needs to derive every drifting offset for a version.
//
// Method (the shift model, validated on 9.4.2 = -1), using TWO independent anchors so a non-uniform
// relayout can't slip through as a bogus shift:
//   1. Boot a 4-channel detection song and START → all four ARE_CHANNELS_PLAYING flip 0→1 (positional
//      sanity check against the seeded band).
//   2. SCREEN anchor: navigate SONG→CHAIN→PHRASE (SELECT+RIGHT) and find the single byte reading the
//      screen ENUM (4/3/1) — that is CURRENT_SCREEN. shift = itsOffset - 0x402 (9.2.L CURRENT_SCREEN).
//   3. CURSOR anchor: one DOWN press must move the byte at 0x41f+shift (SONG_CURSOR_ROW) by exactly 1.
//      Only emit the shift when both anchors agree — content-independent (no reliance on sav format /
//      tempo landing), so it works even when an authored sav doesn't fully load on an older build.
// Pure over a small driver interface (no backend/codec import — the host entry authors the sav and wires
// the real backend), so it is unit-testable with a fake driver.
import type { CursorOffset, LsdjVersion, Screen } from "./types";
import { identifyLsdj } from "./identify";

// Game Boy button indices (GameboyButton enum: Right=0 … Down=3 … Select=6, Start=7).
const RIGHT = 0;
const LEFT = 1;
const UP = 2;
const DOWN = 3;
const SELECT = 6;
const START = 7;

// 9.2.L reference offsets the shift is measured against.
const REF_SCREEN = 0x402; // CURRENT_SCREEN
const REF_SONG_CURSOR_ROW = 0x41f; // SONG_CURSOR_ROW
// LSDisJ screen enum values used as the CURRENT_SCREEN detection signature.
const SCREEN_SONG = 4;
const SCREEN_CHAIN = 3;
const SCREEN_PHRASE = 1;

/** The minimal emulator control surface the detector needs (satisfied by the real backend+audio). */
export interface DetectDriver {
  readFilePrefix(path: string, length: number): Uint8Array | null;
  construct(id: number, romPath: string, sram: Uint8Array): boolean;
  remove(id: number): void;
  readWram(id: number): Uint8Array | null;
  press(id: number, button: number, down: boolean): void;
  render(ms: number): void;
}

export type DetectStatus = "ok" | "not-lsdj" | "no-song-screen" | "screen-not-found" | "cursor-mismatch" | "shift-out-of-range";

// The drifting block only ever nudges a few bytes between builds (observed range ±4 across v8..v9). A
// far larger "shift" is a coincidental screen/cursor byte match, not a real relayout — reject it so the
// version degrades to "unknown" rather than shipping a bogus offset.
const MAX_SHIFT = 32;

export interface DetectResult {
  romPath: string;
  title: string;
  version: LsdjVersion | null;
  key: string | null; // "major.minor.patchLabel", the driftShifts map key
  positionalOk: boolean; // all four ARE_CHANNELS_PLAYING flags flipped 0→1 at the seeded offset
  shift: number | null; // drifting-block shift vs 9.2.L, or null when undetectable / anchors disagree
  screenOffset: number | null;
  status: DetectStatus;
}

// Must match offsets.ts's key format: build tag included so forks (e.g. arduinoboy) key separately.
const versionKey = (v: LsdjVersion): string => {
  const base = `${v.major}.${v.minor}.${v.patchLabel}`;
  return v.build ? `${base}-${v.build}` : base;
};

// A SELECT+RIGHT screen-change chord. SELECT must lead (~200 ms) or LSDj drops the chord; keys are
// never pressed simultaneously (see docs/lsdj.md).
function chordRight(d: DetectDriver, id: number): void {
  d.press(id, SELECT, true);
  d.render(200);
  d.press(id, RIGHT, true);
  d.render(90);
  d.press(id, RIGHT, false);
  d.render(40);
  d.press(id, SELECT, false);
  d.render(150);
}

/**
 * Detect the drifting-block shift for one ROM. `detectionSav` should be a 4-channel song (so the
 * positional START check flips all channels); its content otherwise doesn't matter to the anchors.
 */
export function detectDriftShift(d: DetectDriver, romPath: string, detectionSav: Uint8Array): DetectResult {
  const header = d.readFilePrefix(romPath, 0x150);
  const version = header ? identifyLsdj(header) : null;
  const base: DetectResult = {
    romPath,
    title: header ? headerTitle(header) : "",
    version,
    key: version ? versionKey(version) : null,
    positionalOk: false,
    shift: null,
    screenOffset: null,
    status: "ok",
  };
  if (!version) return { ...base, status: "not-lsdj" };

  const id = 1;
  if (!d.construct(id, romPath, detectionSav)) return { ...base, status: "not-lsdj" };
  try {
    d.render(6000); // reach the SONG screen from the authored sav (skips the self-test)
    const pre = d.readWram(id);
    if (!pre) return { ...base, status: "no-song-screen" };

    // Positional sanity: START → all four ARE_CHANNELS_PLAYING (0x0e0..0x0e3) flip 0→1; then stop.
    d.press(id, START, true);
    d.render(120);
    d.press(id, START, false);
    d.render(400);
    const playing = d.readWram(id)!;
    base.positionalOk = [0, 1, 2, 3].every((i) => pre[0x0e0 + i] === 0 && playing[0x0e0 + i] === 1);
    d.press(id, START, true);
    d.render(120);
    d.press(id, START, false);
    d.render(200);

    // Cursor anchor baseline (cursor at row 0), then one DOWN (→ row 1).
    const onSong = d.readWram(id)!;
    d.press(id, DOWN, true);
    d.render(70);
    d.press(id, DOWN, false);
    d.render(70);
    const afterDown = d.readWram(id)!;

    // Screen anchor: SONG → CHAIN → PHRASE.
    chordRight(d, id);
    const onChain = d.readWram(id)!;
    chordRight(d, id);
    const onPhrase = d.readWram(id)!;

    const screenOffsets: number[] = [];
    for (let i = 0; i < 0x1000; i++) {
      if (onSong[i] === SCREEN_SONG && onChain[i] === SCREEN_CHAIN && onPhrase[i] === SCREEN_PHRASE) screenOffsets.push(i);
    }
    if (screenOffsets.length !== 1) return { ...base, status: "screen-not-found" };
    const screenOffset = screenOffsets[0];
    const shift = screenOffset - REF_SCREEN;
    if (Math.abs(shift) > MAX_SHIFT) return { ...base, screenOffset, status: "shift-out-of-range" };

    // Cursor anchor must agree: the DOWN press moved SONG_CURSOR_ROW at 0x41f+shift by exactly 1.
    const cursorOff = REF_SONG_CURSOR_ROW + shift;
    if (afterDown[cursorOff] !== onSong[cursorOff] + 1) return { ...base, screenOffset, status: "cursor-mismatch" };

    return { ...base, shift, screenOffset, status: "ok" };
  } finally {
    d.remove(id);
  }
}

function headerTitle(header: Uint8Array): string {
  let s = "";
  for (let i = 0x134; i < 0x144 && i < header.length; i++) {
    const c = header[i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.trim();
}

// ---------------------------------------------------------------------------------------------------
// FULL drift-LAYOUT detector (extends coverage below v8.2.1, where the rigid single-shift model breaks)
//
// The scalar detectDriftShift() above assumes the whole drifting block moved as ONE unit (screen ↔
// cursor distance constant) — true only for v8.2..v9. On older builds the block's internal structure
// changed (spike: v6.9.0 screen↔songCursor delta is 0x1F vs 0x1D on 9.2.L), so we detect EVERY drifting
// field independently and emit a full per-version layout instead of one integer. Confirmed by the spike
// down to v4.3.0 (the identifiable floor — v4.0..4.2 carry a bare "LSDJ" title): there IS a single
// CURRENT_SCREEN enum byte with the modern values, and the five cursor screens sit on the linear
// SELECT+RIGHT walk SONG→CHAIN→PHRASE→INSTRUMENT→TABLE.
//
// Unlike detectDriftShift this boots the ROM's OWN companion .sav (proven to reach SONG cleanly), so
// CHAIN/PHRASE/INSTRUMENT/TABLE have real content and their cursors move. TEMPO is disambiguated by the
// host re-encoding that sav to a DISTINCTIVE tempo (default 128 gives several coincidental matches) and
// passing it as `expectedTempo`.

// The five cursor-bearing screens in SELECT+RIGHT order from SONG, with their CURRENT_SCREEN enum value.
const CURSOR_WALK: { name: Screen; screen: number }[] = [
  { name: "song", screen: 4 },
  { name: "chain", screen: 3 },
  { name: "phrase", screen: 1 },
  { name: "instrument", screen: 6 },
  { name: "table", screen: 5 },
];
const SCAN = 0x1000; // fixed-bank-0 window that holds the whole runtime block

export type DriftLayoutStatus = "ok" | "not-lsdj" | "no-song-screen" | "screen-not-found";

export interface DriftLayoutResult {
  romPath: string;
  title: string;
  version: LsdjVersion | null;
  key: string | null;
  currentScreen: number | null; // CURRENT_SCREEN enum byte
  tempo: number | null; // TEMPO byte (null when the distinctive scan wasn't unique)
  cursors: Partial<Record<Screen, CursorOffset>>; // per-screen {col,row}, only those detected
  status: DriftLayoutStatus;
}

function chord(d: DetectDriver, id: number, dir: number): void {
  d.press(id, SELECT, true);
  d.render(200);
  d.press(id, dir, true);
  d.render(90);
  d.press(id, dir, false);
  d.render(40);
  d.press(id, SELECT, false);
  d.render(150);
}

function tap(d: DetectDriver, id: number, btn: number, ms = 70): void {
  d.press(id, btn, true);
  d.render(ms);
  d.press(id, btn, false);
  d.render(ms);
}

// Navigate to a target screen by SELECT-stepping `dir` until CURRENT_SCREEN reads `want` (bounded).
function gotoScreen(d: DetectDriver, id: number, screenOff: number, want: number, dir: number, maxSteps = 6): boolean {
  for (let n = 0; n < maxSteps; n++) {
    if (d.readWram(id)![screenOff] === want) return true;
    chord(d, id, dir);
  }
  return d.readWram(id)![screenOff] === want;
}

const TAP_MS = 70;

// Detect the {col,row} cursor on the CURRENT screen. Pins the cursor to the top-left corner, then probes
// with a DOWN, a RIGHT, and an equal idle. A real cursor axis is CROSS-AXIS INDEPENDENT and IDLE-STABLE:
//   * row byte  = +1 on DOWN, unchanged by RIGHT, stationary during idle;
//   * col byte  = unchanged by DOWN, +1 on RIGHT, stationary during idle.
// This rejects both time counters (move during idle) AND per-keypress counters (move on BOTH DOWN and
// RIGHT — the trap a single-axis probe falls into). col/row must also be ADJACENT (every LSDj cursor pair
// is: song/chain/phrase/table use col = row-1). `colAfterRow` handles the INSTRUMENT screen, whose column
// does NOT respond to RIGHT (it stays 0 for a pulse instrument) but is structurally at row+1 (verified on
// 9.2.L/8.5.1/9.4.2): when set and no adjacent column is detected, the column is derived as row+1.
function detectCursor(d: DetectDriver, id: number, colAfterRow = false): CursorOffset | null {
  for (let n = 0; n < 4; n++) tap(d, id, UP, 45);
  for (let n = 0; n < 4; n++) tap(d, id, LEFT, 45);

  const a = d.readWram(id)!;
  tap(d, id, DOWN, TAP_MS);
  const b = d.readWram(id)!;
  tap(d, id, RIGHT, TAP_MS);
  const c = d.readWram(id)!;
  d.render(2 * TAP_MS); // equal idle
  const e = d.readWram(id)!;

  const rowCands: number[] = [];
  const colCands: number[] = [];
  for (let i = 0; i < SCAN; i++) {
    if (e[i] !== c[i]) continue; // moved during idle → counter, not a cursor
    if (b[i] === a[i] + 1 && c[i] === b[i] && b[i] <= 0x7f) rowCands.push(i); // DOWN moved it, RIGHT didn't
    if (b[i] === a[i] && c[i] === b[i] + 1 && c[i] <= 0x7f) colCands.push(i); // RIGHT moved it, DOWN didn't
  }

  // Adjacent (col,row) pair — every LSDj cursor pair is adjacent.
  for (const rr of rowCands) for (const cc of colCands) if (Math.abs(rr - cc) === 1) return { col: cc, row: rr };

  // INSTRUMENT: no adjacent column was found (RIGHT doesn't move it; any colCand is a stray). Derive the
  // column as the byte just after the (unique) row.
  if (colAfterRow && rowCands.length === 1) return { col: rowCands[0] + 1, row: rowCands[0] };
  return null;
}

// Boot `sram`, detect CURRENT_SCREEN + all reachable cursors, and return the pristine on-SONG snapshot
// (for the caller's tempo differential). Returns null onSong when the screen anchor can't be resolved.
interface BootDetect {
  status: DriftLayoutStatus;
  onSong: Uint8Array | null;
  screenOff: number | null;
  cursors: Partial<Record<Screen, CursorOffset>>;
}
function bootAndDetect(d: DetectDriver, romPath: string, sram: Uint8Array): BootDetect {
  const out: BootDetect = { status: "ok", onSong: null, screenOff: null, cursors: {} };
  const id = 1;
  if (!d.construct(id, romPath, sram)) return { ...out, status: "not-lsdj" };
  try {
    d.render(6000); // reach SONG (authored era-format song stamps the SRAM-init magic → skips self-test)
    const onSong = d.readWram(id);
    if (!onSong) return { ...out, status: "no-song-screen" };
    out.onSong = onSong;

    // 1) CURRENT_SCREEN: walk SONG→CHAIN→PHRASE→INSTRUMENT→TABLE (pristine, no cursor moves yet) and find
    // the single byte reading the enum sequence [4,3,1,6,5] — a 5-value signature that is essentially
    // always unique.
    const snaps: Uint8Array[] = [onSong];
    for (let k = 0; k < 4; k++) {
      chord(d, id, RIGHT);
      snaps.push(d.readWram(id)!);
    }
    const wantSeq = CURSOR_WALK.map((s) => s.screen); // [4,3,1,6,5]
    const screenOffs: number[] = [];
    for (let i = 0; i < SCAN; i++) if (snaps.every((s, k) => s[i] === wantSeq[k])) screenOffs.push(i);
    if (screenOffs.length !== 1) return { ...out, status: "screen-not-found" };
    const screenOff = screenOffs[0];
    out.screenOff = screenOff;

    // 2) Per-screen cursors. Return to SONG, then detect on each screen along the SELECT+RIGHT walk. SONG
    // is detected LAST: detecting it moves the song-position cursor, which would empty the CHAIN/PHRASE
    // views if done first.
    gotoScreen(d, id, screenOff, 4, LEFT);
    for (const step of CURSOR_WALK.slice(1)) {
      chord(d, id, RIGHT);
      if (d.readWram(id)![screenOff] !== step.screen) continue; // lost our place — skip this screen
      const c = detectCursor(d, id, step.name === "instrument"); // instrument column is static at row+1
      if (c) out.cursors[step.name] = c;
    }
    if (gotoScreen(d, id, screenOff, 4, LEFT)) {
      const songCursor = detectCursor(d, id);
      if (songCursor) out.cursors.song = songCursor;
    }
    return out;
  } finally {
    d.remove(id);
  }
}

/**
 * Detect the full drifting layout (CURRENT_SCREEN + TEMPO + all five cursor offsets) for one ROM.
 *
 * `savA`/`savB` must be 4-channel detection songs authored at the ROM's ERA format version (from the
 * companion sav's formatVersion) — an authored song boots to SONG and navigates on every era, whereas a
 * companion sav leaves some builds (e.g. 8.5.1) in a state where SELECT+RIGHT nav doesn't register. The
 * two savs carry DIFFERENT tempos (`tempoA`/`tempoB`, both distinctive): TEMPO is the byte holding tempoA
 * after boot A and tempoB after boot B (a differential that rejects coincidental static bytes), picked
 * closest to CURRENT_SCREEN when several bytes mirror the live BPM (project field / display buffers).
 */
export function detectDriftLayout(d: DetectDriver, romPath: string, savA: Uint8Array, tempoA: number, savB: Uint8Array, tempoB: number): DriftLayoutResult {
  const header = d.readFilePrefix(romPath, 0x150);
  const version = header ? identifyLsdj(header) : null;
  const base: DriftLayoutResult = {
    romPath,
    title: header ? headerTitle(header) : "",
    version,
    key: version ? versionKey(version) : null,
    currentScreen: null,
    tempo: null,
    cursors: {},
    status: "ok",
  };
  if (!version) return { ...base, status: "not-lsdj" };

  // Boot A: screen + cursor detection (+ the pristine on-SONG snapshot for the tempo differential).
  const a = bootAndDetect(d, romPath, savA);
  base.cursors = a.cursors;
  base.currentScreen = a.screenOff;
  if (a.status !== "ok" || a.onSong == null || a.screenOff == null) return { ...base, status: a.status };
  const screenOff = a.screenOff;

  // Boot B: same song at a different tempo. TEMPO tracks tempoA→tempoB; a coincidental static byte does
  // not. Among the tracking bytes, the canonical register is closest to CURRENT_SCREEN (mirrors are far).
  const id = 1;
  if (d.construct(id, romPath, savB)) {
    try {
      d.render(6000);
      const wB = d.readWram(id);
      if (wB) {
        const cands: number[] = [];
        for (let i = 0; i < SCAN; i++) if (a.onSong[i] === tempoA && wB[i] === tempoB) cands.push(i);
        if (cands.length) base.tempo = cands.reduce((best, i) => (Math.abs(i - screenOff) < Math.abs(best - screenOff) ? i : best));
      }
    } finally {
      d.remove(id);
    }
  }
  return base;
}
