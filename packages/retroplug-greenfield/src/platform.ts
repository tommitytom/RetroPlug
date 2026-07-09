// The two orthogonal axes a loaded system carries, kept separate on purpose:
//
//   - `Platform` — what the ROM targets (Game Boy / NES / GBA). Detected from magic bytes.
//   - `Core`     — the emulator that runs it (SameBoy / Mesen). Chosen per platform.
//
// The relationship is many-to-many in principle (Mesen does NES+GBA; a GB ROM could run on either
// core), so the two are never a single string. In v1 the core is auto-derived from the platform via
// `defaultCoreFor` — there's no user override yet, but the axes stay distinct so one can be added
// without reshaping. The sniffer classifies to a `Platform` (never a core), by logo/magic rather than
// file extension: a mislabelled .gb that's really a .nes still picks the right platform, and an
// unrelated file is `"unknown"`. Detection logic mirrors packages/native/src/system/RomFormat.hpp
// (priority NES → GBA → GB → unknown); the vocabulary is greenfield's own (platforms, not core names).

/** What a ROM targets. */
export type Platform = "gb" | "nes" | "gba";

/** The emulator that runs a platform. */
export type Core = "sameboy" | "mesen";

/** The default core for each platform (v1 has no override; the axes stay distinct regardless). */
export const DEFAULT_CORE: Record<Platform, Core> = {
  gb: "sameboy",
  nes: "mesen",
  gba: "mesen",
};

/** Pick the core that runs `platform` (the sole core-selection policy in v1). */
export function defaultCoreFor(platform: Platform): Core {
  return DEFAULT_CORE[platform];
}

// Bytes needed to classify any supported ROM: the Game Boy logo ends at $0133, so a 0x134-byte prefix
// covers all three headers (NES needs 4, GBA needs 36). This is how much to read when classifying from
// a file header rather than the whole ROM.
export const ROM_SNIFF_LEN = 0x134;

// GBA Nintendo logo (first 32 of 156 bytes) at $0004 — the boot ROM CRC-checks it.
const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];
const GBA_LOGO_OFFSET = 0x04;

// Game Boy Nintendo logo (48 bytes) at $0104 — required by the GB boot ROM, so present in every
// DMG/CGB cart. (DMG vs CGB is a SameBoy `model` knob, not a separate platform.)
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
export function detectPlatform(bytes: Uint8Array): Platform | "unknown" {
  // iNES: 'N','E','S',0x1A at offset 0.
  if (bytes.length >= 4 && bytes[0] === 0x4e && bytes[1] === 0x45 && bytes[2] === 0x53 && bytes[3] === 0x1a) {
    return "nes";
  }
  if (matchesAt(bytes, GBA_LOGO_OFFSET, GBA_LOGO)) return "gba";
  if (matchesAt(bytes, GB_LOGO_OFFSET, GB_LOGO)) return "gb";
  return "unknown";
}

// GB cartridge types (header $147) that carry battery-backed save memory — the set SameBoy allocates a
// .sav for. NES uses iNES flags6 bit 1 (header byte 6). GBA/unknown default to "has battery" so a save is
// never wrongly disabled (GBA save-type detection is heuristic and out of scope here).
const GB_BATTERY_CART_TYPES = new Set([0x03, 0x06, 0x09, 0x0d, 0x0f, 0x10, 0x13, 0x1b, 0x1e, 0x22, 0xff]);

/** Whether a cart has battery-backed save memory, read from its `header` — NES iNES flags6 bit 1, GB
 *  cartridge-type at $147. Only ever returns `false` when we're certain there's no save (so a real
 *  battery/LSDj cart is never wrongly greyed); GBA and anything unrecognized default to `true`. */
export function romHasBattery(header: Uint8Array, platform: Platform): boolean {
  if (platform === "nes") return header.length > 6 && (header[6] & 0x02) !== 0;
  if (platform === "gb") return header.length > 0x147 && GB_BATTERY_CART_TYPES.has(header[0x147]);
  return true; // gba / anything else — never disable
}
