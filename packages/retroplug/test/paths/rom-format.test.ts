// detectPlatform - classify a ROM by magic bytes, a faithful port of
// packages/native/src/system/RomFormat.hpp. Priority is NES → GBA → GB → Sega →
// unknown (first match wins), and short/empty buffers are unknown via the length
// guards. The logo byte-arrays here are transcribed independently from the C++ so
// a wrong copy in the implementation is caught.
//
// The Sega tier is the only one that consults the file extension, and only where the
// content genuinely can't decide: which machine an unfilled region nibble means, and
// (as the last tier of all) a file carrying no magic anywhere. The precedence tests at
// the bottom are the ones that matter - an extension must never beat real content.
import { test, expect } from "../../testing/harness";
import { detectPlatform } from "../../src/platform";

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

// Sega 8-bit header magic "TMR SEGA", at the END of a bank rather than the file start.
const SEGA_MAGIC = [0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41];

// A zero-filled buffer of `len` with `bytes` written at `at`.
function buf(len: number, at: number, bytes: number[]): Uint8Array {
  const b = new Uint8Array(len);
  b.set(bytes, at);
  return b;
}

// A Sega ROM image with its 16-byte header at `at`: the magic, then `region` as the high nibble of the
// header's last byte (magic + 0xF) - 4 = SMS Export, 6 = GG Export, 0 = an unfilled homebrew field.
function segaRom(at: number, region: number, len = 0x20000): Uint8Array {
  const b = buf(len, at, SEGA_MAGIC);
  b[at + 0xf] = (region << 4) | 0x0c; // low nibble is the ROM-size code, which we never read
  return b;
}

test("nes: iNES magic at offset 0", () => {
  expect(detectPlatform(buf(64, 0, NES_MAGIC))).toBe("nes");
  expect(detectPlatform(new Uint8Array(NES_MAGIC))).toBe("nes"); // exactly 4 bytes
});

test("gba: Nintendo logo at 0x04", () => {
  expect(detectPlatform(buf(0x2000, 0x04, GBA_LOGO))).toBe("gba");
  expect(detectPlatform(buf(0x04 + 32, 0x04, GBA_LOGO))).toBe("gba"); // exactly to the guard
});

test("gb: Game Boy logo at 0x104 (DMG + CGB carts share it)", () => {
  expect(detectPlatform(buf(0x8000, 0x104, GB_LOGO))).toBe("gb");
  expect(detectPlatform(buf(0x104 + 48, 0x104, GB_LOGO))).toBe("gb"); // exactly to the guard
});

test("unknown: no recognizable magic", () => {
  expect(detectPlatform(new Uint8Array(0x8000))).toBe("unknown"); // all zero
  expect(detectPlatform(new Uint8Array([1, 2, 3, 4, 5, 6]))).toBe("unknown");
});

test("unknown: short / empty buffers fall through the length guards", () => {
  expect(detectPlatform(new Uint8Array(0))).toBe("unknown");
  expect(detectPlatform(new Uint8Array([0x4e, 0x45, 0x53]))).toBe("unknown"); // 3 bytes, not the full NES magic
  expect(detectPlatform(buf(0x104 + 47, 0x104, GB_LOGO.slice(0, 47)))).toBe("unknown"); // one byte short of the GB guard
});

test("priority: NES beats a GB logo, GBA beats a GB logo", () => {
  // Buffer carrying BOTH the NES magic at 0 and the GB logo at 0x104 → NES wins.
  const nesOverGb = buf(0x8000, 0x104, GB_LOGO);
  nesOverGb.set(NES_MAGIC, 0);
  expect(detectPlatform(nesOverGb)).toBe("nes");

  // Buffer carrying BOTH the GBA logo at 0x04 and the GB logo at 0x104 → GBA wins.
  const gbaOverGb = buf(0x8000, 0x104, GB_LOGO);
  gbaOverGb.set(GBA_LOGO, 0x04);
  expect(detectPlatform(gbaOverGb)).toBe("gba");
});

// --- Sega 8-bit (Master System / Game Gear) ---------------------------------

test("sega: TMR SEGA at each bank-end offset", () => {
  // $7FF0 is the usual spot; $1FF0/$3FF0 are where an 8 KB/16 KB cart puts it.
  expect(detectPlatform(segaRom(0x7ff0, 4))).toBe("sms");
  expect(detectPlatform(segaRom(0x3ff0, 4))).toBe("sms");
  expect(detectPlatform(segaRom(0x1ff0, 4))).toBe("sms");
});

test("sega: a copier-headered dump shifts the header by 0x200 and still classifies", () => {
  // Mesen strips a 512-byte copier header at load (SmsConsole.cpp), so these boot fine - they must
  // classify too, or a ROM that runs perfectly would be rejected as "not a ROM".
  expect(detectPlatform(segaRom(0x7ff0 + 0x200, 4))).toBe("sms");
  expect(detectPlatform(segaRom(0x1ff0 + 0x200, 6))).toBe("gg");
});

test("sega: the region nibble picks the machine", () => {
  expect(detectPlatform(segaRom(0x7ff0, 3))).toBe("sms"); // SMS Japan
  expect(detectPlatform(segaRom(0x7ff0, 4))).toBe("sms"); // SMS Export
  expect(detectPlatform(segaRom(0x7ff0, 5))).toBe("gg"); // GG Japan
  expect(detectPlatform(segaRom(0x7ff0, 6))).toBe("gg"); // GG Export
  expect(detectPlatform(segaRom(0x7ff0, 7))).toBe("gg"); // GG International
});

test("sega: an unfilled region nibble defers to the extension, then defaults to sms", () => {
  // Homebrew that never filled the field in. Nothing in the content can decide, so the extension does.
  expect(detectPlatform(segaRom(0x7ff0, 0), ".gg")).toBe("gg");
  expect(detectPlatform(segaRom(0x7ff0, 0), ".sms")).toBe("sms");
  // No extension either → Master System, which is what Mesen does for any extension that isn't .gg.
  expect(detectPlatform(segaRom(0x7ff0, 0))).toBe("sms");
  expect(detectPlatform(segaRom(0x7ff0, 0), ".bin")).toBe("sms");
});

test("sega: a headerless ROM classifies from the extension alone", () => {
  // Plenty of SMS/GG homebrew ships with no header at all. This is the last tier, reached only after
  // every content tier declines.
  expect(detectPlatform(new Uint8Array(0x8000), ".sms")).toBe("sms");
  expect(detectPlatform(new Uint8Array(0x8000), ".gg")).toBe("gg");
  expect(detectPlatform(new Uint8Array(0x8000), ".gb")).toBe("unknown"); // not a Sega extension
  expect(detectPlatform(new Uint8Array(0x8000))).toBe("unknown"); // no extension offered
});

test("sega: the header must be reachable in the buffer that was read", () => {
  // The cheap 0x134-byte first tier can never see a $7FF0 header - that's exactly why classifyRom
  // reads a second, deeper tier. A truncated buffer must decline rather than guess.
  expect(detectPlatform(segaRom(0x7ff0, 4).subarray(0, 0x134))).toBe("unknown");
  expect(detectPlatform(segaRom(0x7ff0, 4).subarray(0, 0x7ff0 + 7))).toBe("unknown"); // one byte short
});

test("precedence: real content always beats the extension", () => {
  // The whole point of the extension tiers is that they are LAST. A mislabelled file still classifies
  // by what it actually is - otherwise a .nes renamed to .sms would be handed to the wrong core.
  expect(detectPlatform(buf(0x8000, 0, NES_MAGIC), ".sms")).toBe("nes");
  expect(detectPlatform(buf(0x8000, 0x104, GB_LOGO), ".gg")).toBe("gb");
  expect(detectPlatform(buf(0x8000, 0x04, GBA_LOGO), ".sms")).toBe("gba");
  // And a real region nibble beats a contradicting extension too.
  expect(detectPlatform(segaRom(0x7ff0, 6), ".sms")).toBe("gg");
  expect(detectPlatform(segaRom(0x7ff0, 4), ".gg")).toBe("sms");
});
