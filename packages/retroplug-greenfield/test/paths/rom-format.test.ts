// detectRomFormat — classify a ROM by magic bytes (not extension), a faithful
// port of packages/native/src/system/RomFormat.hpp. Priority is NES → GBA → GB →
// unknown (first match wins), and short/empty buffers are unknown via the length
// guards. The logo byte-arrays here are transcribed independently from the C++ so
// a wrong copy in the implementation is caught.
import { test, expect } from "../../testing/harness";
import { detectRomFormat } from "../../src/romFormat";

// iNES magic "NES\x1A" at offset 0.
const NES_MAGIC = [0x4e, 0x45, 0x53, 0x1a];

// GBA Nintendo logo (first 32 bytes) at offset 0x04.
const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];

// Game Boy Nintendo logo (48 bytes) at offset 0x104.
const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];

// A zero-filled buffer of `len` with `bytes` written at `at`.
function buf(len: number, at: number, bytes: number[]): Uint8Array {
  const b = new Uint8Array(len);
  b.set(bytes, at);
  return b;
}

test("nes: iNES magic at offset 0", () => {
  expect(detectRomFormat(buf(64, 0, NES_MAGIC))).toBe("nes");
  expect(detectRomFormat(new Uint8Array(NES_MAGIC))).toBe("nes"); // exactly 4 bytes
});

test("gba: Nintendo logo at 0x04", () => {
  expect(detectRomFormat(buf(0x2000, 0x04, GBA_LOGO))).toBe("gba");
  expect(detectRomFormat(buf(0x04 + 32, 0x04, GBA_LOGO))).toBe("gba"); // exactly to the guard
});

test("sameboy: Game Boy logo at 0x104 (DMG + CGB carts share it)", () => {
  expect(detectRomFormat(buf(0x8000, 0x104, GB_LOGO))).toBe("sameboy");
  expect(detectRomFormat(buf(0x104 + 48, 0x104, GB_LOGO))).toBe("sameboy"); // exactly to the guard
});

test("unknown: no recognizable magic", () => {
  expect(detectRomFormat(new Uint8Array(0x8000))).toBe("unknown"); // all zero
  expect(detectRomFormat(new Uint8Array([1, 2, 3, 4, 5, 6]))).toBe("unknown");
});

test("unknown: short / empty buffers fall through the length guards", () => {
  expect(detectRomFormat(new Uint8Array(0))).toBe("unknown");
  expect(detectRomFormat(new Uint8Array([0x4e, 0x45, 0x53]))).toBe("unknown"); // 3 bytes, not the full NES magic
  expect(detectRomFormat(buf(0x104 + 47, 0x104, GB_LOGO.slice(0, 47)))).toBe("unknown"); // one byte short of the GB guard
});

test("priority: NES beats a GB logo, GBA beats a GB logo", () => {
  // Buffer carrying BOTH the NES magic at 0 and the GB logo at 0x104 → NES wins.
  const nesOverGb = buf(0x8000, 0x104, GB_LOGO);
  nesOverGb.set(NES_MAGIC, 0);
  expect(detectRomFormat(nesOverGb)).toBe("nes");

  // Buffer carrying BOTH the GBA logo at 0x04 and the GB logo at 0x104 → GBA wins.
  const gbaOverGb = buf(0x8000, 0x104, GB_LOGO);
  gbaOverGb.set(GBA_LOGO, 0x04);
  expect(detectRomFormat(gbaOverGb)).toBe("gba");
});
