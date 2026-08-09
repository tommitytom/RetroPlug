// The SMDJ4 RLE codec, pinned against smsggdj's reference implementation.
//
// `rleCompress` was certified BYTE-IDENTICAL to /workspaces/smsggdj/tools/rle.js over a corpus during
// authoring - the real demo song (6,912 -> 722), all-zero, all-empty-step, pseudo-random, every run
// boundary, and cross-decode in both directions (their decoder reads our stream and ours reads theirs).
// The vectors below are frozen from that oracle so the test does not depend on the sibling repo, which
// is the same discipline used for the LSDj sav codec against liblsdj.
//
// Why byte-identity rather than just round-tripping: the cart decodes anything well-formed, so a
// "better" packer would appear to work here and produce streams `tools/savetool.html` and the on-cart
// Z80 decoder might not read the same way. Interop is the requirement, not compression ratio.
import { test, expect } from "../../testing/harness";
import { rleCompress, rleDecompress, rlePack, RLE_UNIT } from "../../src/smsggdj/codec/rle";

const U = (arr: number[][]): Uint8Array => Uint8Array.from(arr.flat());
const rep = (unit: number[], n: number): Uint8Array => U(Array.from({ length: n }, () => unit));
/** n all-distinct units, so nothing can be run-encoded. */
const seq = (n: number): Uint8Array =>
  U(Array.from({ length: n }, (_, i) => [i & 0xff, 0xaa, 0x55, (i >> 8) & 0xff]));
const bytes = (...b: number[]): Uint8Array => Uint8Array.from(b);

test("frozen vectors: the exact streams tools/rle.js emits", () => {
  // A literal run's control byte is (units - 1); a repeat run's is 0x80 | (units - 2). Every one of
  // these is short enough to check by eye against that rule, which is the point of freezing them small.
  expect(rleCompress(U([[1, 2, 3, 4]]))).toEqual(bytes(0x00, 1, 2, 3, 4)); // 1 literal unit
  expect(rleCompress(rep([1, 2, 3, 4], 2))).toEqual(bytes(0x80, 1, 2, 3, 4)); // 2 -> repeat, not literal
  expect(rleCompress(rep([1, 2, 3, 4], 3))).toEqual(bytes(0x81, 1, 2, 3, 4));
  // A literal run must STOP where a repeat begins, or it swallows compressible data.
  expect(rleCompress(U([[1, 1, 1, 1], [2, 2, 2, 2], [2, 2, 2, 2], [2, 2, 2, 2]])))
    .toEqual(bytes(0x00, 1, 1, 1, 1, 0x81, 2, 2, 2, 2));
});

test("frozen vectors: the run-length boundaries", () => {
  // 129 units is the largest repeat one control byte can express (0x80 | 0x7F).
  expect(rleCompress(rep([9, 8, 7, 6], 129))).toEqual(bytes(0xff, 9, 8, 7, 6));
  // 130 must split into 129 + 1, and the tail is a LITERAL - a 1-unit repeat is not encodable.
  expect(rleCompress(rep([9, 8, 7, 6], 130))).toEqual(bytes(0xff, 9, 8, 7, 6, 0x00, 9, 8, 7, 6));

  // 128 distinct units is the largest literal run (0x7F).
  const lit128 = rleCompress(seq(128));
  expect(lit128.length).toBe(1 + 128 * RLE_UNIT);
  expect(lit128[0]).toBe(0x7f);
  // 129 splits 128 + 1, so a second control byte appears.
  const lit129 = rleCompress(seq(129));
  expect(lit129.length).toBe(2 + 129 * RLE_UNIT);
  expect(lit129[0]).toBe(0x7f);
  expect(lit129[1 + 128 * RLE_UNIT]).toBe(0x00);
});

test("round-trips every shape, including ones that do not compress", () => {
  const BLOCK = 6912;
  const shapes: [string, Uint8Array][] = [
    ["all zero", new Uint8Array(BLOCK)],
    ["all 0xFF", new Uint8Array(BLOCK).fill(0xff)],
    ["empty steps", U(Array.from({ length: BLOCK / 4 }, () => [0x00, 0xff, 0x00, 0x00]))],
    ["distinct units", seq(BLOCK / 4)],
  ];
  // A deterministic PRNG stands in for incompressible data - the case that hits the store-raw floor.
  let s = 12345;
  const rnd = new Uint8Array(BLOCK);
  for (let i = 0; i < BLOCK; i++) {
    s = (s * 1103515245 + 12345) & 0x7fffffff;
    rnd[i] = (s >>> 16) & 0xff;
  }
  shapes.push(["pseudo-random", rnd]);

  for (const [name, block] of shapes) {
    const back = rleDecompress(rleCompress(block), BLOCK);
    expect(back != null).toBeTruthy();
    expect(back!).toEqual(block);
    void name;
  }
});

test("pack falls back to store-raw exactly when RLE would not shrink the block", () => {
  // The floor is what makes the format safe for incompressible songs: an RLE stream of random data is
  // LARGER than the block, and the directory's raw flag is how the ROM stores it at 6,912 anyway.
  const BLOCK = 6912;
  let s = 999;
  const rnd = new Uint8Array(BLOCK);
  for (let i = 0; i < BLOCK; i++) {
    s = (s * 1103515245 + 12345) & 0x7fffffff;
    rnd[i] = (s >>> 16) & 0xff;
  }
  const packedRandom = rlePack(rnd);
  expect(packedRandom.raw).toBe(true);
  expect(packedRandom.bytes).toEqual(rnd); // verbatim, not a re-encode
  expect(rleCompress(rnd).length > BLOCK).toBeTruthy(); // ...and RLE really would have expanded it

  const packedSparse = rlePack(new Uint8Array(BLOCK));
  expect(packedSparse.raw).toBe(false);
  expect(packedSparse.bytes.length < BLOCK).toBeTruthy();
});

test("a malformed stream decodes to null, never to a plausible empty song", () => {
  // The reference decoder walks off the end of a truncated stream and turns the undefined bytes into
  // zeroes. That is fine for a tool reading its own output and wrong for a save file, which is
  // untrusted input: a silently zero-filled block IS a valid-looking empty song, so a corrupt entry
  // would load as "your song is gone" instead of being rejected.
  expect(rleDecompress(bytes(0x80, 1, 2))).toBe(null); // repeat run, unit truncated
  expect(rleDecompress(bytes(0x02, 1, 2, 3, 4))).toBe(null); // literal claims 3 units, carries 1
  expect(rleDecompress(bytes(0xff, 1, 2, 3, 4), 16)).toBe(null); // 129 units overruns expectedLen
  // Well-formed but the wrong size for the slot is also a rejection, not a silent short read.
  expect(rleDecompress(rleCompress(rep([1, 2, 3, 4], 4)), 6912)).toBe(null);
  expect(rleDecompress(new Uint8Array(0), 0)).toEqual(new Uint8Array(0)); // empty is well-formed
});

test("compress rejects a block that is not a whole number of units", () => {
  // Silently truncating would corrupt the last row rather than failing, and every caller has a
  // 6,912-byte block by construction - so anything else is a bug upstream worth surfacing.
  expect(() => rleCompress(new Uint8Array(6911))).toThrow();
});
