// The N8 CHR <-> grayscale-PNG codec (src/n8/chrImage.ts): dump a CHR bank as an editable tile grid and encode
// it back. Pure, no hardware. Checks the SHADE4 ramp, the row-major tile layout, and a lossless roundtrip.
import { test, expect } from "../../testing/harness";
import { chrToPng, pngToChr, SHADE4, grayToPixel, type ChrImage } from "../../src/n8/chrImage";
import { encodeTile } from "../../src/risa/rom";

const px = (img: ChrImage, x: number, y: number): number[] => {
  const o = (y * img.width + x) * 4;
  return [img.rgba[o], img.rgba[o + 1], img.rgba[o + 2], img.rgba[o + 3]];
};
const gray = (v: number): number[] => [SHADE4[v], SHADE4[v], SHADE4[v], 0xff];

test("chrToPng lays a bank out as a grayscale tile grid with the SHADE4 ramp", () => {
  const bank = new Uint8Array(8192);
  // tile 0 row 0 = pixel values 0,1,2,3,0,1,2,3 (rest 0)
  encodeTile(bank, 0, Array.from({ length: 64 }, (_, i) => (i < 8 ? i % 4 : 0)));
  const img = chrToPng(bank);
  expect(img.width).toBe(128); // 16 tiles * 8
  expect(img.height).toBe(256); // 512 tiles / 16 wide = 32 rows * 8
  expect(px(img, 0, 0)).toEqual(gray(0)); // 0 -> white paper
  expect(px(img, 1, 0)).toEqual(gray(1));
  expect(px(img, 2, 0)).toEqual(gray(2));
  expect(px(img, 3, 0)).toEqual(gray(3)); // 3 -> black ink
});

test("tile placement is row-major (tile 16 starts on the second tile-row)", () => {
  const bank = new Uint8Array(8192);
  encodeTile(bank, 16, Array(64).fill(3)); // solid-ink tile at grid col 0, row 1
  const img = chrToPng(bank);
  expect(px(img, 0, 8)).toEqual(gray(3)); // (0,8) = tile 16 top-left
  expect(px(img, 0, 0)).toEqual(gray(0)); // tile 0 still blank
});

test("chrToPng -> pngToChr is a byte-identical roundtrip for any bank", () => {
  const bank = new Uint8Array(8192);
  for (let i = 0; i < bank.length; i++) bank[i] = (i * 37 + 11) & 0xff;
  const back = pngToChr(chrToPng(bank));
  expect(back.length).toBe(bank.length);
  expect(Array.from(back)).toEqual(Array.from(bank));
});

test("grayToPixel buckets edited grays to the nearest of 4 levels", () => {
  expect(grayToPixel(0xff)).toBe(0);
  expect(grayToPixel(0xaa)).toBe(1);
  expect(grayToPixel(0x55)).toBe(2);
  expect(grayToPixel(0x00)).toBe(3);
  expect(grayToPixel(0xf0)).toBe(0); // near-white -> paper
  expect(grayToPixel(0x08)).toBe(3); // near-black -> ink
});

test("pngToChr rejects a malformed image", () => {
  expect(() => pngToChr({ width: 12, height: 8, rgba: new Uint8Array(12 * 8 * 4) })).toThrow(); // not /8
  expect(() => pngToChr({ width: 16, height: 8, rgba: new Uint8Array(4) })).toThrow(); // rgba too small
});
