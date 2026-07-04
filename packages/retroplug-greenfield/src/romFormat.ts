// Detect which emulator backend a ROM should run on, by magic bytes rather than
// file extension — a mislabelled .gb that's really a .nes still picks the right
// backend, and an unrelated file is rejected as "unknown". A faithful port of
// packages/native/src/system/RomFormat.hpp: priority NES → GBA → GB → unknown
// (first match wins); short/empty buffers are unknown via the length guards.

export type RomFormat = "sameboy" | "nes" | "gba" | "unknown";

// Bytes needed to classify any supported ROM: the Game Boy logo ends at $0133, so
// a 0x134-byte prefix covers all three headers (NES needs 4, GBA needs 36). This is
// how much to read when classifying from a file header rather than the whole ROM.
export const ROM_SNIFF_LEN = 0x134;

// GBA Nintendo logo (first 32 of 156 bytes) at $0004 — the boot ROM CRC-checks it.
const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];
const GBA_LOGO_OFFSET = 0x04;

// Game Boy Nintendo logo (48 bytes) at $0104 — required by the GB boot ROM, so
// present in every DMG/CGB cart.
const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];
const GB_LOGO_OFFSET = 0x104;

function matchesAt(bytes: Uint8Array, offset: number, sig: number[]): boolean {
  if (bytes.length < offset + sig.length) return false;
  for (let i = 0; i < sig.length; i++) {
    if (bytes[offset + i] !== sig[i]) return false;
  }
  return true;
}

/** Classify `bytes` as a Game Boy / NES / GBA ROM, or `"unknown"`. */
export function detectRomFormat(bytes: Uint8Array): RomFormat {
  // iNES: 'N','E','S',0x1A at offset 0.
  if (bytes.length >= 4 && bytes[0] === 0x4e && bytes[1] === 0x45 && bytes[2] === 0x53 && bytes[3] === 0x1a) {
    return "nes";
  }
  if (matchesAt(bytes, GBA_LOGO_OFFSET, GBA_LOGO)) return "gba";
  if (matchesAt(bytes, GB_LOGO_OFFSET, GB_LOGO)) return "sameboy";
  return "unknown";
}
