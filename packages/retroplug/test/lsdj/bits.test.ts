// Phase 2 gate: bits.ts contains the int32/uint8 hazard. Every setBits/setU8
// keeps the byte in 0..255, setBits is the exact inverse of bits() over a live
// splice, and high-bit values (>= 0x80) do not sign-extend through ~ / <<.
import { test, expect } from "../../testing/harness";
import { BitView, BitWriter, bitMask } from "../../src/lsdj/codec/bits";

test("bitMask matches (1<<n)-1 with 0xFF at >=8", () => {
  expect(bitMask(0)).toBe(0);
  expect(bitMask(1)).toBe(1);
  expect(bitMask(4)).toBe(0x0f);
  expect(bitMask(7)).toBe(0x7f);
  expect(bitMask(8)).toBe(0xff);
  expect(bitMask(9)).toBe(0xff);
});

test("setBits round-trips a field and never escapes 0..255 (exhaustive over pos/count)", () => {
  const buf = new Uint8Array(4);
  for (let seed = 0; seed < 4; seed++) {
    for (let pos = 0; pos < 8; pos++) {
      for (let count = 1; pos + count <= 8; count++) {
        buf[0] = (seed * 0x55 + pos * 7 + count) & 0xff; // arbitrary pre-existing bits
        const before = buf[0];
        const w = new BitWriter(buf);
        const max = bitMask(count);
        for (let v = 0; v <= max; v++) {
          w.setBits(0, pos, count, v);
          // the byte stays a byte
          expect(buf[0] >= 0 && buf[0] <= 255).toBeTruthy();
          // the field reads back exactly
          expect(new BitView(buf).bits(0, pos, count)).toBe(v);
          // bits OUTSIDE the field are untouched from `before`
          const fieldMask = (max << pos) & 0xff;
          expect(buf[0] & ~fieldMask & 0xff).toBe(before & ~fieldMask & 0xff);
        }
      }
    }
  }
});

test("high-bit values do not sign-extend through the splice", () => {
  const buf = new Uint8Array(1);
  const w = new BitWriter(buf);
  // full-byte high-bit write
  w.setU8(0, 0xff);
  expect(buf[0]).toBe(0xff);
  w.setBits(0, 0, 8, 0x80);
  expect(buf[0]).toBe(0x80);
  // writing into the top nibble of a byte whose low nibble is set
  buf[0] = 0x0f;
  w.setBits(0, 4, 4, 0x0f);
  expect(buf[0]).toBe(0xff);
  expect(new BitView(buf).bits(0, 4, 4)).toBe(0x0f);
});

test("out-of-bounds reads return 0; out-of-bounds writes are ignored", () => {
  const buf = new Uint8Array(2);
  const v = new BitView(buf);
  expect(v.u8(2)).toBe(0);
  expect(v.u8(999)).toBe(0);
  expect(v.bits(5, 0, 4)).toBe(0);
  const w = new BitWriter(buf);
  w.setU8(2, 0xff); // ignored
  w.setBits(9, 0, 4, 0xf); // ignored
  expect(buf[0]).toBe(0);
  expect(buf[1]).toBe(0);
});

test("u16le / setU16le round-trip", () => {
  const buf = new Uint8Array(4);
  const w = new BitWriter(buf);
  w.setU16le(1, 0xbeef);
  expect(buf[1]).toBe(0xef);
  expect(buf[2]).toBe(0xbe);
  expect(new BitView(buf).u16le(1)).toBe(0xbeef);
});
