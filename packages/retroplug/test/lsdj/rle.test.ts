// Phase 2 gate: rle.ts compress/decompress are mutual inverses over synthetic
// 0x8000 songs that exercise every token — literal bytes, RLE runs (0xC0),
// self-escaped 0xC0/0xE0, default-wave runs (0xF0) and default-instrument runs
// (0xF1) — and span multiple 512-byte blocks (so the block-jump path is hit).
// Byte-exact validation against real compressed archives happens in the native
// tier (Phase 4 full-corpus sav round-trip).
import { test, expect } from "../../testing/harness";
import { compressProject, decompressProject } from "../../src/lsdj/codec/rle";

const kSongBytes = 0x8000;
const DEFAULT_WAVE = [0x8e, 0xcd, 0xcc, 0xbb, 0xaa, 0xa9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31];
const DEFAULT_INSTRUMENT = [0xa8, 0x00, 0x00, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0xf3, 0x00, 0x00];

function roundtrips(song: Uint8Array): void {
  const comp = compressProject(song, 1);
  expect(comp.bytes.length % 0x200).toBe(0);
  const back = decompressProject(comp.bytes, 0); // 1-based startBlock 1 -> 0-based block 0
  expect(back.length).toBe(kSongBytes);
  for (let i = 0; i < kSongBytes; i++) {
    if (back[i] !== song[i]) throw new Error(`mismatch at ${i}: got ${back[i]} want ${song[i]}`);
  }
}

test("all-zero song round-trips", () => {
  roundtrips(new Uint8Array(kSongBytes));
});

test("pseudo-random song round-trips (spans many blocks)", () => {
  const s = new Uint8Array(kSongBytes);
  let x = 0x12345678 >>> 0;
  for (let i = 0; i < kSongBytes; i++) {
    x = (x * 1103515245 + 12345) >>> 0;
    s[i] = (x >>> 16) & 0xff;
  }
  roundtrips(s);
});

test("RLE runs, self-escaped 0xC0/0xE0, and default patterns round-trip", () => {
  const s = new Uint8Array(kSongBytes);
  let o = 0;
  const put = (v: number) => {
    s[o++] = v;
  };
  // a long run of one byte (>4 triggers the RLE encoding, capped at 0xFF)
  for (let i = 0; i < 600; i++) put(0x42);
  // literal control bytes that must self-escape
  put(0xc0);
  put(0xe0);
  put(0xc0);
  put(0xe0);
  // a few default-wave frames (0xF0 token), then default-instrument frames (0xF1)
  for (let r = 0; r < 5; r++) for (const b of DEFAULT_WAVE) put(b);
  for (let r = 0; r < 300; r++) for (const b of DEFAULT_INSTRUMENT) put(b); // > 0xFF -> count saturates + splits
  // mixed literals
  for (let i = 0; o < kSongBytes; i++) put((i * 31 + 7) & 0xff);
  roundtrips(s);
});

test("a song of only default-wave frames round-trips", () => {
  const s = new Uint8Array(kSongBytes);
  for (let i = 0; i < kSongBytes; i++) s[i] = DEFAULT_WAVE[i % 16];
  roundtrips(s);
});

test("compressProject rejects a song shorter than 0x8000", () => {
  expect(() => compressProject(new Uint8Array(100), 1)).toThrow();
});
