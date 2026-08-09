// The two orthogonal axes a loaded system carries, kept separate on purpose:
//
//   - `Platform` - what the ROM targets (Game Boy / NES / GBA / Master System / Game Gear). Detected
//     from magic bytes.
//   - `Core`     — the emulator that runs it (SameBoy / Mesen). Chosen per platform.
//
// The relationship is many-to-many in principle (Mesen does NES+GBA+SMS/GG; a GB ROM could run on
// either core), so the two are never a single string. In v1 the core is auto-derived from the platform
// via `defaultCoreFor` - there's no user override yet, but the axes stay distinct so one can be added
// without reshaping. The sniffer classifies to a `Platform` (never a core), by logo/magic rather than
// file extension: a mislabelled .gb that's really a .nes still picks the right platform, and an
// unrelated file is `"unknown"`. Detection logic mirrors packages/native/src/system/RomFormat.hpp
// (priority NES → GBA → GB → Sega → unknown); the vocabulary is this package's own (platforms, not
// core names).
//
// The Sega tier is the one that reads the file extension, and only ever as a TIEBREAK - see
// `detectPlatform`. Content still wins outright whenever it says anything.

/** What a ROM targets. */
export type Platform = "gb" | "nes" | "gba" | "sms" | "gg";

/** The emulator that runs a platform. */
export type Core = "sameboy" | "mesen";

/** The default core for each platform (v1 has no override; the axes stay distinct regardless). */
export const DEFAULT_CORE: Record<Platform, Core> = {
  gb: "sameboy",
  nes: "mesen",
  gba: "mesen",
  sms: "mesen",
  gg: "mesen",
};

/** Pick the core that runs `platform` (the sole core-selection policy in v1). */
export function defaultCoreFor(platform: Platform): Core {
  return DEFAULT_CORE[platform];
}

// Bytes needed to classify a Nintendo ROM: the Game Boy logo ends at $0133, so a 0x134-byte prefix
// covers all three headers (NES needs 4, GBA needs 36). This is how much to read when classifying from
// a file header rather than the whole ROM.
export const ROM_SNIFF_LEN = 0x134;

// Bytes needed to reach a Sega 8-bit header, which sits at the END of the first bank rather than the
// start of the file: $7FF0 for a 32 KB-or-larger cart, plus $200 more for a copier-headered dump. Far
// past ROM_SNIFF_LEN, which is why `classifyRom` reads in two tiers and only a file that isn't a
// GB/NES/GBA ROM ever pays this read.
export const SEGA_SNIFF_LEN = 0x8200;

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

// Sega 8-bit header magic ("TMR SEGA"), which unlike the Nintendo logos lives at the END of a bank.
// $7FF0 is the usual spot; $1FF0/$3FF0 are where an 8 KB/16 KB cart puts it. Each is doubled by the
// +$200 copier-header variant Mesen strips at load (SmsConsole.cpp: `(size % 0x400) == 0x200`) - such
// a ROM boots fine, so it must classify too.
const SEGA_MAGIC = [0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41]; // "TMR SEGA"
const SEGA_BASE_OFFSETS = [0x7ff0, 0x3ff0, 0x1ff0];
const SEGA_COPIER_SKIP = 0x200;
const SEGA_HEADER_OFFSETS = SEGA_BASE_OFFSETS.flatMap((o) => [o, o + SEGA_COPIER_SKIP]);

// Region/machine code, high nibble of the header's last byte (magic + $F). 3/4 are Master System
// (Japan/Export), 5/6/7 Game Gear (Japan/Export/International). Anything else is a homebrew that never
// filled it in, and falls through to the extension.
const GG_REGION_NIBBLES = new Set([5, 6, 7]);
const SMS_REGION_NIBBLES = new Set([3, 4]);

function matchesAt(bytes: Uint8Array, offset: number, sig: number[]): boolean {
  if (bytes.length < offset + sig.length) return false;
  for (let i = 0; i < sig.length; i++) {
    if (bytes[offset + i] !== sig[i]) return false;
  }
  return true;
}

/** `"sms"`/`"gg"` for a `.sms`/`.gg` extension, else null. Lowercase, dot included. */
function segaExtension(ext: string | undefined): Platform | null {
  if (ext === ".gg") return "gg";
  if (ext === ".sms") return "sms";
  return null;
}

// Which Sega machine a located header describes. The region nibble decides when it's one of the five
// defined values; otherwise the extension breaks the tie, and failing that we default to Master System
// - exactly what Mesen does for any extension that isn't .gg (SmsConsole.cpp:46-60).
function segaMachine(bytes: Uint8Array, headerOffset: number, ext: string | undefined): Platform {
  const nibble = (bytes[headerOffset + 0xf] ?? 0) >> 4;
  if (GG_REGION_NIBBLES.has(nibble)) return "gg";
  if (SMS_REGION_NIBBLES.has(nibble)) return "sms";
  return segaExtension(ext) ?? "sms";
}

/** Classify `bytes` as a Game Boy / NES / GBA / Master System / Game Gear ROM, or `"unknown"`.
 *
 *  `ext` (lowercase, dot included) is consulted ONLY where the content genuinely can't decide: which
 *  Sega machine a header with an unfilled region nibble means, and - as the last tier of all - a file
 *  with no recognised magic anywhere, which is how headerless SMS/GG homebrew loads. It can never
 *  override a positive content match, so a `.sms` file that is really a `.nes` still classifies `nes`.
 *  Omit it to classify on bytes alone. */
export function detectPlatform(bytes: Uint8Array, ext?: string): Platform | "unknown" {
  // iNES: 'N','E','S',0x1A at offset 0.
  if (bytes.length >= 4 && bytes[0] === 0x4e && bytes[1] === 0x45 && bytes[2] === 0x53 && bytes[3] === 0x1a) {
    return "nes";
  }
  if (matchesAt(bytes, GBA_LOGO_OFFSET, GBA_LOGO)) return "gba";
  if (matchesAt(bytes, GB_LOGO_OFFSET, GB_LOGO)) return "gb";
  for (const off of SEGA_HEADER_OFFSETS) {
    if (matchesAt(bytes, off, SEGA_MAGIC)) return segaMachine(bytes, off, ext);
  }
  // No magic anywhere. Plenty of SMS/GG homebrew ships without a header at all, so the extension is the
  // only thing left to go on - but only here, after every content tier has declined.
  return segaExtension(ext) ?? "unknown";
}

// GB cartridge types (header $147) that carry battery-backed save memory — the set SameBoy allocates a
// .sav for. NES uses iNES flags6 bit 1 (header byte 6). GBA/SMS/GG/unknown default to "has battery" so a
// save is never wrongly disabled (GBA save-type detection is heuristic and out of scope here).
const GB_BATTERY_CART_TYPES = new Set([0x03, 0x06, 0x09, 0x0d, 0x0f, 0x10, 0x13, 0x1b, 0x1e, 0x22, 0xff]);

/** Whether a cart has battery-backed save memory, read from its `header` — NES iNES flags6 bit 1, GB
 *  cartridge-type at $147. Only ever returns `false` when we're certain there's no save (so a real
 *  battery/LSDj cart is never wrongly greyed); GBA/SMS/GG and anything unrecognized default to `true`.
 *
 *  SMS/GG carry NO battery bit in their header - the cart either wires up SRAM or it doesn't, and
 *  nothing on the ROM side says which. Defaulting to `true` is load-bearing rather than lazy: smsggdj
 *  IS a battery cart, and its songs live in that SRAM, so a `false` here would grey out the only rows
 *  that can save a user's work. */
export function romHasBattery(header: Uint8Array, platform: Platform): boolean {
  if (platform === "nes") return header.length > 6 && (header[6] & 0x02) !== 0;
  if (platform === "gb") return header.length > 0x147 && GB_BATTERY_CART_TYPES.has(header[0x147]);
  return true; // gba / sms / gg / anything else - never disable
}
