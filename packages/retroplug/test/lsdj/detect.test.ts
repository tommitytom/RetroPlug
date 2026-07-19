// Unit tests for the offline drift detector via a FAKE emulator driver — no host needed, runs on the mock
// tier (`pnpm test`). The end-to-end detection against real cores is exercised by `pnpm lsdj:gen-offsets`
// (whose output is driftLayouts.generated.ts) and the native test test-native/lsdj-old-drift.test.ts.
import { test, expect } from "../../testing/harness";
import { detectDriftShift, detectDriftLayout, type DetectDriver } from "../../src/lsdj/runtime/detect";

const RIGHT = 0, LEFT = 1, UP = 2, DOWN = 3, SELECT = 6, START = 7;

function makeHeader(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

// A minimal fake LSDj that models exactly the state the two anchors probe: a playing flag (START
// toggles), a screen cursor advanced SONG(4)→CHAIN(3)→PHRASE(1) by SELECT+RIGHT, and a song-cursor-row
// bumped by DOWN. `screenOff`/`cursorOff` place CURRENT_SCREEN and SONG_CURSOR_ROW so tests can inject a
// uniform shift, a non-uniform layout, or an out-of-range one.
class FakeGb implements DetectDriver {
  private down = new Set<number>();
  private playing = false;
  private screenIdx = 0;
  private cursorRow = 0;
  private readonly screens = [4, 3, 1];
  constructor(
    private header: Uint8Array,
    private screenOff: number,
    private cursorOff: number,
    private constructOk = true,
  ) {}
  readFilePrefix(): Uint8Array { return this.header; }
  construct(): boolean { this.playing = false; this.screenIdx = 0; this.cursorRow = 0; return this.constructOk; }
  remove(): void {}
  render(): void {}
  press(_id: number, button: number, down: boolean): void {
    const wasDown = this.down.has(button);
    if (down) this.down.add(button); else this.down.delete(button);
    if (button === START && wasDown && !down) this.playing = !this.playing; // START release toggles play
    if (button === RIGHT && down && this.down.has(SELECT)) this.screenIdx = Math.min(this.screenIdx + 1, 2); // SELECT+RIGHT
    if (button === DOWN && wasDown && !down) this.cursorRow += 1; // DOWN release moves the cursor down
  }
  readWram(): Uint8Array {
    const w = new Uint8Array(0x8000);
    for (let i = 0; i < 4; i++) w[0x0e0 + i] = this.playing ? 1 : 0;
    w[this.screenOff] = this.screens[this.screenIdx];
    w[this.cursorOff] = this.cursorRow;
    return w;
  }
}

const SAV = new Uint8Array(0); // the fake ignores the sav

test("detects a uniform shift when both anchors agree", () => {
  // 9.4.2-style: CURRENT_SCREEN at 0x401 (shift -1), SONG_CURSOR_ROW at 0x41e (= 0x41f - 1).
  const fake = new FakeGb(makeHeader("LSDj-v9.4.2"), 0x401, 0x41e);
  const r = detectDriftShift(fake, "lsdj9_4_2.gb", SAV);
  expect(r.status).toBe("ok");
  expect(r.shift).toBe(-1);
  expect(r.screenOffset).toBe(0x401);
  expect(r.positionalOk).toBeTruthy();
  expect(r.key).toBe("9.4.2");
});

test("cursor anchor disagreement → cursor-mismatch, no shift emitted", () => {
  // Screen says shift -1, but the cursor is NOT at 0x41e → non-uniform layout, must be refused.
  const fake = new FakeGb(makeHeader("LSDj-v9.9.9"), 0x401, 0x430);
  const r = detectDriftShift(fake, "x.gb", SAV);
  expect(r.status).toBe("cursor-mismatch");
  expect(r.shift).toBe(null);
});

test("an implausibly large shift is rejected (range guard)", () => {
  const fake = new FakeGb(makeHeader("LSDj-v9.9.9"), 0x402 + 40, 0x41f + 40);
  const r = detectDriftShift(fake, "x.gb", SAV);
  expect(r.status).toBe("shift-out-of-range");
  expect(r.shift).toBe(null);
});

test("non-LSDj header → not-lsdj (no boot attempted)", () => {
  const fake = new FakeGb(makeHeader("MGB"), 0x402, 0x41f);
  const r = detectDriftShift(fake, "mgb.gb", SAV);
  expect(r.status).toBe("not-lsdj");
  expect(r.version).toBe(null);
});

test("arduinoboy build keys separately (build tag in the key)", () => {
  const fake = new FakeGb(makeHeader("LSDj-v9.3.3aboy"), 0x402, 0x41f);
  const r = detectDriftShift(fake, "lsdj9_3_3-arduinoboy.gb", SAV);
  expect(r.key).toBe("9.3.3-aboy");
  expect(r.shift).toBe(0); // uniform here, so it would detect — real aboy differs (cursor-mismatch)
});

// ---------------------------------------------------------------------------------------------------
// detectDriftLayout: a fuller fake modelling the FULL per-field detector — screen navigation, a per-
// screen (col,row) cursor that responds independently to DOWN/RIGHT, and a tempo byte set from the boot's
// sav. Exercises the whole algorithm (screen sequence, tempo differential, cursor cross-axis probing +
// the INSTRUMENT col-after-row derivation) on the mock tier.
interface FakeScreen { enum: number; col: number; row: number; colMoves: boolean }
class FakeLayoutGb implements DetectDriver {
  private selectHeld = false;
  private idx = 0;
  private col: number[];
  private row: number[];
  private tempo = 0;
  constructor(
    private header: Uint8Array,
    private screenOff: number,
    private tempoOff: number,
    private screens: FakeScreen[],
    private navigable = true,
  ) {
    this.col = screens.map(() => 0);
    this.row = screens.map(() => 0);
  }
  readFilePrefix(): Uint8Array { return this.header; }
  construct(_id: number, _rom: string, sram: Uint8Array): boolean {
    this.idx = 0; this.selectHeld = false; this.col = this.screens.map(() => 0); this.row = this.screens.map(() => 0);
    this.tempo = sram[0] ?? 0; // the host passes the boot's tempo as the sav's first byte
    return true;
  }
  remove(): void {}
  render(): void {}
  press(_id: number, button: number, down: boolean): void {
    if (button === SELECT) { this.selectHeld = down; return; }
    if (down) {
      if (this.selectHeld && this.navigable && button === RIGHT) this.idx = Math.min(this.idx + 1, this.screens.length - 1);
      if (this.selectHeld && this.navigable && button === LEFT) this.idx = Math.max(this.idx - 1, 0);
      return; // cursor moves happen on release; screen nav on the SELECT+dir down-edge
    }
    if (this.selectHeld) return; // a chord's dir-release (SELECT still held) is not a cursor move
    const s = this.screens[this.idx];
    if (button === DOWN) this.row[this.idx] += 1;
    else if (button === UP) this.row[this.idx] = Math.max(0, this.row[this.idx] - 1);
    else if (button === RIGHT && s.colMoves) this.col[this.idx] += 1;
    else if (button === LEFT && s.colMoves) this.col[this.idx] = Math.max(0, this.col[this.idx] - 1);
  }
  readWram(): Uint8Array {
    const w = new Uint8Array(0x8000);
    w[this.screenOff] = this.screens[this.idx].enum;
    w[this.tempoOff] = this.tempo;
    this.screens.forEach((s, i) => { w[s.col] = this.col[i]; w[s.row] = this.row[i]; });
    return w;
  }
}

// A 9.4.2-shaped layout: screen 0x401, tempo 0x529, the tight cursor cluster (song/chain/phrase col=row-1),
// the INSTRUMENT (col=row+1, RIGHT does NOT move it → derived), and the far TABLE cursor.
const SCREENS_942: FakeScreen[] = [
  { enum: 4, col: 0x41d, row: 0x41e, colMoves: true }, // song
  { enum: 3, col: 0x419, row: 0x41a, colMoves: true }, // chain
  { enum: 1, col: 0x415, row: 0x416, colMoves: true }, // phrase
  { enum: 6, col: 0x428, row: 0x427, colMoves: false }, // instrument — col at row+1, not RIGHT-movable
  { enum: 5, col: 0x92f, row: 0x930, colMoves: true }, // table (far from the cluster)
];
const tempoSav = (t: number) => new Uint8Array([t]);

test("detectDriftLayout recovers screen, tempo (differential), and all five cursors", () => {
  const fake = new FakeLayoutGb(makeHeader("LSDj-v9.4.2"), 0x401, 0x529, SCREENS_942);
  const r = detectDriftLayout(fake, "lsdj9_4_2.gb", tempoSav(190), 190, tempoSav(120), 120);
  expect(r.status).toBe("ok");
  expect(r.key).toBe("9.4.2");
  expect(r.currentScreen).toBe(0x401);
  expect(r.tempo).toBe(0x529);
  expect(r.cursors.song).toEqual({ col: 0x41d, row: 0x41e });
  expect(r.cursors.chain).toEqual({ col: 0x419, row: 0x41a });
  expect(r.cursors.phrase).toEqual({ col: 0x415, row: 0x416 });
  expect(r.cursors.instrument).toEqual({ col: 0x428, row: 0x427 }); // derived col = row+1
  expect(r.cursors.table).toEqual({ col: 0x92f, row: 0x930 });
});

test("detectDriftLayout: non-LSDj header → not-lsdj (no boot)", () => {
  const fake = new FakeLayoutGb(makeHeader("MGB"), 0x401, 0x529, SCREENS_942);
  const r = detectDriftLayout(fake, "mgb.gb", tempoSav(190), 190, tempoSav(120), 120);
  expect(r.status).toBe("not-lsdj");
});

test("detectDriftLayout: screen anchor unresolvable → screen-not-found", () => {
  // navigable=false: SELECT+RIGHT never changes the screen, so the [4,3,1,6,5] signature never appears.
  const fake = new FakeLayoutGb(makeHeader("LSDj-v9.4.2"), 0x401, 0x529, SCREENS_942, false);
  const r = detectDriftLayout(fake, "x.gb", tempoSav(190), 190, tempoSav(120), 120);
  expect(r.status).toBe("screen-not-found");
});
