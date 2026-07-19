// Minimal valid ROM byte buffers for systems-store tests. The mock classifies via
// detectPlatform, so a fixture only needs the right magic at the right offset.
// (Not a *.test.ts, so the runner ignores it; imported by the store tests.)

const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];

const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];

/** A buffer that classifies as a Game Boy ROM (Nintendo logo at 0x104). */
export function gbRom(): Uint8Array {
  const b = new Uint8Array(0x8000);
  b.set(GB_LOGO, 0x104);
  return b;
}

/** A GB ROM whose header declares MBC1+RAM+BATTERY (0x147) + 8KB SRAM (0x149), so a REAL
 *  SameBoy core allocates battery RAM and saveSramBytes() is non-empty. (The mock ignores
 *  the cartridge header, so this only matters for the native-host integration tests.) */
export function gbRomBattery(): Uint8Array {
  const b = gbRom();
  b[0x147] = 0x03; // MBC1 + RAM + BATTERY
  b[0x149] = 0x02; // 8 KB SRAM
  return b;
}

/** A GB battery ROM whose cartridge title (0x134) reads "LSDJ", so the ROM providers attach the
 *  lsdj-sync role — and thus its load-time sav-seed hook. The mock ignores the cartridge header, so
 *  the title at 0x134 is what the provider keys on. */
export function lsdjRom(title = "LSDJ"): Uint8Array {
  const b = gbRomBattery();
  for (let i = 0; i < title.length && i < 0x10; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

/** A buffer that classifies as a GBA ROM (Nintendo logo at 0x04). */
export function gbaRom(): Uint8Array {
  const b = new Uint8Array(0x8000);
  b.set(GBA_LOGO, 0x04);
  return b;
}

/** A buffer that classifies as an NES ROM (iNES magic at 0). */
export function nesRom(): Uint8Array {
  const b = new Uint8Array(0x4000);
  b.set([0x4e, 0x45, 0x53, 0x1a], 0);
  return b;
}

/** A minimal but VALID iNES ROM (mapper 1 / MMC1, battery flag) that a real Mesen core boots and for
 *  which it allocates battery-backed save RAM — so saveSramBytes() / readSram() are non-empty. 16-byte
 *  header + one 16 KB PRG bank (CHR-RAM); PRG holds a JMP-to-self at the reset vector so the booted CPU
 *  idles instead of running off into garbage. (The mock ignores the header; this only matters for the
 *  native-host integration tests.) */
export function nesRomBattery(): Uint8Array {
  const PRG = 0x4000; // one 16 KB bank
  const b = new Uint8Array(0x10 + PRG);
  b.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1A"
  b[4] = 0x01; // 1 x 16 KB PRG
  b[5] = 0x00; // 0 x 8 KB CHR → CHR-RAM
  b[6] = 0x12; // flags6: mapper low nibble = 1 (MMC1) | bit1 battery
  b[7] = 0x00; // flags7: mapper high nibble = 0, iNES v1
  // A single 16 KB bank maps to $C000-$FFFF. Put `JMP $C000` at $C000 and point the reset vector at it.
  b[0x10 + 0x0000] = 0x4c; // JMP abs
  b[0x10 + 0x0001] = 0x00;
  b[0x10 + 0x0002] = 0xc0; // → $C000 (infinite loop)
  b[0x10 + 0x3ffc] = 0x00; // reset vector low  ($FFFC)
  b[0x10 + 0x3ffd] = 0xc0; // reset vector high ($FFFD) → $C000
  return b;
}

/** A minimal ROM carrying the real risa 2.2.1 iNES 2.0 header fingerprint (NES 2.0, mapper 5 / MMC5,
 *  battery, 512 KB PRG, 32 KB CHR, 64 KB PRG-NVRAM) so the risa ROM provider attaches the `risa` role.
 *  Detection reads only the header; the body is padding (the mock ignores it). */
export function risaRom(): Uint8Array {
  const b = new Uint8Array(0x200); // >= ROLE_HEADER_LEN so the provider sees a full header prefix
  b.set([0x4e, 0x45, 0x53, 0x1a, 0x20, 0x04, 0x53, 0x08, 0x00, 0x00, 0xa0], 0);
  return b;
}

/** A present-but-not-a-ROM buffer (classifies "unknown"). */
export function garbage(): Uint8Array {
  return new Uint8Array(0x8000); // all zero
}
