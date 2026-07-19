// risa ROM detection. An NES ROM has no cartridge-title field (unlike Game Boy / LSDj), and risa's
// "RISA V" ASCII marker lives deep in the PRG — far past the ~336-byte header a RomContext exposes. So
// we detect risa by its distinctive iNES 2.0 header fingerprint, all within the first 16 bytes:
//   NES 2.0, mapper 5 (MMC5), battery, 64 KB PRG-NVRAM (the battery WRAM that holds the RSAV catalog).
// That combination — an MMC5 cart with a 64 KB battery — is what has a risa song catalog to manage;
// it's effectively risa-unique. A rare false positive is harmless: the Songs menu simply reads an empty
// catalog (listSongs returns []). (The header fingerprint approach; see docs/risa-integration-plan.md.)

/** True if `header` (the iNES header prefix) looks like a risa ROM: NES 2.0 + MMC5 + battery + 64 KB
 *  PRG-NVRAM. Reads only bytes 0..10, so the short RomContext header prefix is enough. */
export function isRisaRomHeader(header: Uint8Array): boolean {
  if (header.length < 16) return false;
  // "NES\x1a"
  if (header[0] !== 0x4e || header[1] !== 0x45 || header[2] !== 0x53 || header[3] !== 0x1a) return false;
  // NES 2.0 identifier: flags7 bits 2-3 == 0b10.
  if ((header[7] & 0x0c) !== 0x08) return false;
  // Mapper number (iNES 2.0: low nibble flags6 hi, mid nibble flags7 hi, high nibble flags8 lo) === 5 = MMC5.
  const mapper = (header[6] >> 4) | (header[7] & 0xf0) | ((header[8] & 0x0f) << 8);
  if (mapper !== 5) return false;
  // Battery-backed save (flags6 bit 1).
  if ((header[6] & 0x02) === 0) return false;
  // PRG-NVRAM (battery) size, iNES 2.0 byte 10 high nibble: 64 << 10 = 64 KB. This is the risa save WRAM.
  if ((header[10] >> 4) !== 0x0a) return false;
  return true;
}
