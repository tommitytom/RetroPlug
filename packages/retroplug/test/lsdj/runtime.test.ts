// Pure-TS unit tests for the LSDj runtime-WRAM reader (plan Part B): version identification, layout
// resolution, and byte→state decoding. No emulator — the reader is fed synthetic WRAM byte arrays, so
// this runs on the mock tier (`pnpm test`). The end-to-end proof against a live core is the native
// test (test-native/lsdj-runtime.test.ts). Decode-logic cases use an explicit TEST_LAYOUT so they test
// the DECODER independently of the seeded offsets table (offsets.ts).
import { test, expect } from "../../testing/harness";
import { LsdjReader, parseLsdjVersion, identifyLsdj, decodeLsdjState, screenFromByte } from "../../src/lsdj/runtime";
import type { OffsetLayout } from "../../src/lsdj/runtime";

// An explicit layout with arbitrary-but-known offsets, to test decode logic without the real table.
const TEST_LAYOUT: OffsetLayout = {
  active: 0x0e0,
  phrases: 0x168,
  phraseRows: 0x16c,
  chains: 0x170,
  chainRows: 0x17c,
  songRows: 0x200,
  tempo: 0x52a,
  currentScreen: 0x402,
  cursors: {
    song: { col: 0x41e, row: 0x41f },
    table: { col: 0x92d, row: 0x92e },
  },
};

// A ROM header prefix with `title` written at 0x134 (where the GB cartridge title lives).
function header(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

// A 32K WRAM buffer (CGB) with `set` offsets applied over a zero background.
function wram(set: Record<number, number>): Uint8Array {
  const w = new Uint8Array(0x8000);
  for (const k of Object.keys(set)) w[Number(k)] = set[Number(k)];
  return w;
}

test("parseLsdjVersion handles numeric, letter-patch, aboy-build, and non-LSDj titles", () => {
  expect(parseLsdjVersion("LSDj-v9.4.2")).toEqual({ major: 9, minor: 4, patch: 2, patchLabel: "2", build: null, raw: "LSDJ-V9.4.2" });
  const letter = parseLsdjVersion("LSDj-v9.2.L")!;
  expect(letter.patch).toBe(21); // A=10 → L=21, so 9.2.L orders after 9.2.9
  expect(letter.patchLabel).toBe("L");
  const aboy = parseLsdjVersion("LSDj-v9.3.3aboy")!;
  expect(aboy.patch).toBe(3);
  expect(aboy.build).toBe("aboy");
  expect(parseLsdjVersion("LSDJ")).toBe(null); // bare pre-versioned title
  expect(parseLsdjVersion("MGB")).toBe(null);
});

test("identifyLsdj reads the version out of a header prefix", () => {
  expect(identifyLsdj(header("LSDj-v9.4.2"))!.major).toBe(9);
  expect(identifyLsdj(header("MGB"))).toBe(null);
});

test("decodeLsdjState decodes full playback state from an explicit layout", () => {
  const w = wram({
    0x0e0: 1, 0x0e1: 1, 0x0e2: 0, 0x0e3: 0, // ARE_CHANNELS_PLAYING: PU1+PU2 on, WAV+NOI off
    0x168: 0x10, 0x169: 0x11, 0x16a: 0xff, 0x16b: 0xff, // PLAYING_PHRASES
    0x16c: 3, 0x16d: 4, 0x16e: 0xff, 0x16f: 0xff, // PLAYING_PHRASE_ROWS
    0x170: 0, 0x171: 0, 0x17c: 1, 0x17d: 2, // PLAYING_CHAINS / _CHAIN_ROWS
    0x200: 2, 0x201: 5, 0x202: 0xff, 0x203: 0xff, // PLAYING_SONG_ROWS → max = 5
    0x402: 4, // CURRENT_SCREEN = SONG
    0x41e: 6, 0x41f: 1, // SONG cursor col/row
    0x52a: 128, // TEMPO
  });
  const s = decodeLsdjState(w, TEST_LAYOUT, null);

  expect(s.supported).toBeTruthy();
  expect(s.playing).toBeTruthy();
  expect(s.channels.pu1.playing).toBeTruthy();
  expect(s.channels.wav.playing).toBeFalsy();
  expect(s.channels.pu1.phrase).toBe(0x10);
  expect(s.channels.pu1.phraseRow).toBe(3);
  expect(s.channels.pu2.phraseRow).toBe(4);
  expect(s.channels.noi.phraseRow).toBe(null); // 0xff = not playing → null
  expect(s.channels.pu2.chainRow).toBe(2);
  expect(s.songRow).toBe(5); // max valid per-channel song row
  expect(s.screen).toBe("song");
  expect(s.cursor).toEqual({ col: 6, row: 1 });
  expect(s.tempo).toBe(128);
});

test("screen byte maps to name; out-of-range → unknown", () => {
  expect(screenFromByte(1)).toBe("phrase");
  expect(screenFromByte(4)).toBe("song");
  expect(screenFromByte(9)).toBe("project");
  expect(screenFromByte(0x0e)).toBe("help");
  expect(screenFromByte(0xff)).toBe("unknown");
  expect(screenFromByte(undefined)).toBe("unknown");
});

test("cursor resolves per active screen; null when that screen has no mapping or is unknown", () => {
  const onTable = decodeLsdjState(wram({ 0x402: 5, 0x92d: 7, 0x92e: 9 }), TEST_LAYOUT, null);
  expect(onTable.screen).toBe("table");
  expect(onTable.cursor).toEqual({ col: 7, row: 9 });
  // PHRASE screen (1) — TEST_LAYOUT has no phrase cursor → null.
  const onPhrase = decodeLsdjState(wram({ 0x402: 1 }), TEST_LAYOUT, null);
  expect(onPhrase.screen).toBe("phrase");
  expect(onPhrase.cursor).toBe(null);
});

test("drifting fields null in the layout degrade to unknown/null, positional still decodes", () => {
  const layout: OffsetLayout = { ...TEST_LAYOUT, tempo: null, currentScreen: null, cursors: null };
  const s = decodeLsdjState(wram({ 0x0e0: 1, 0x200: 3 }), layout, null);
  expect(s.playing).toBeTruthy(); // positional still works
  expect(s.songRow).toBe(3);
  expect(s.screen).toBe("unknown");
  expect(s.cursor).toBe(null);
  expect(s.tempo).toBe(null);
});

test("LsdjReader resolves a layout by version: modern supported, pre-v4 degraded", () => {
  const modern = LsdjReader.fromHeader(header("LSDj-v9.4.2"));
  expect(modern.supported).toBeTruthy();
  expect(modern.version!.major).toBe(9);
  // 9.4.2 has a detected drift shift, so screen decodes at its real offset (0x401).
  const s = modern.read(wram({ 0x0e0: 1, 0x401: 4 }));
  expect(s.playing).toBeTruthy();
  expect(s.screen).toBe("song");

  // Below the legacy table's floor (v4.0.0) → no layout at all.
  const old = LsdjReader.fromHeader(header("LSDj-v3.0.0"));
  expect(old.supported).toBeFalsy();
  const os = old.read(wram({ 0x0e0: 1 }));
  expect(os.supported).toBeFalsy();
  expect(os.playing).toBeFalsy();
  expect(os.screen).toBe("unknown");
});

test("a legacy version (v5.0.3) now decodes the FULL drift layout (screen/tempo/cursor) from driftLayouts", () => {
  const reader = LsdjReader.fromHeader(header("LSDj-v5.0.3"));
  expect(reader.supported).toBeTruthy();
  const L = reader.layout!;
  // v5.0.3 used to fall back to song-cursor-only (screen/tempo null). The full-layout detector now covers
  // it, so currentScreen/tempo/per-screen cursors resolve. Drive the reader through the layout's OWN
  // resolved offsets so the test asserts the coverage, not specific generated values.
  expect(L.currentScreen != null).toBeTruthy();
  expect(L.tempo != null).toBeTruthy();
  expect(L.cursors!.song).toBeTruthy();
  const s = reader.read(
    wram({
      0x0e8: 1, 0x168: 0x0a, 0x16c: 5, 0x170: 3, 0x17c: 2, 0x200: 7, // positional (legacy table, unchanged)
      [L.currentScreen!]: 4, // SONG
      [L.tempo!]: 120,
      [L.cursors!.song!.col]: 4, [L.cursors!.song!.row]: 3,
    }),
  );
  expect(s.channels.pu1.playing).toBeTruthy(); // active at 0xE8, not 0xE0
  expect(s.channels.pu1.phrase).toBe(0x0a);
  expect(s.songRow).toBe(7);
  expect(s.screen).toBe("song");
  expect(s.tempo).toBe(120);
  expect(s.cursor).toEqual({ col: 4, row: 3 });
});

test("an ancient legacy version (v4.0.0, relocated block) decodes rows but leaves phrase/chain # unknown", () => {
  const reader = LsdjReader.fromHeader(header("LSDj-v4.0.0"));
  expect(reader.supported).toBeTruthy();
  // v4.0.0 legacy: active 724 (0x2D4), phraseRows 915 (0x393), chainRows 923, songRows 927, cursor 492/493.
  const s = reader.read(wram({ 0x2d4: 1, 0x393: 6, 0x39b: 1, 0x39f: 9, 0x1ec: 2, 0x1ed: 8 }));
  expect(s.channels.pu1.playing).toBeTruthy();
  expect(s.channels.pu1.phraseRow).toBe(6);
  expect(s.songRow).toBe(9);
  expect(s.channels.pu1.phrase).toBe(null); // NUMBER offset unknown for the pre-v4.9.6 relocated block
  expect(s.channels.pu1.chain).toBe(null);
  expect(s.cursor).toEqual({ col: 2, row: 8 });
});
