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

// The host-sync capability marker. Every normal risa build from 2.3.0 on embeds the ASCII tag "RISAxyz"
// within the first 0x150 bytes of the .nes (the RomContext header prefix, ROLE_HEADER_LEN), where xyz are
// the three version digits: 2.3.0 ships "RISA230". risa generates it from APP_VERSION_TEXT, so the marker
// both identifies the dormant, UI-less N8-FIFO receive path (F9-locate / FA / F8 / FC) that the `risa-sync`
// DSP role drives from the DAW transport, AND stamps the version - readable from the header prefix alone,
// unlike the "RISA V<x.y.z>" tag identifyRisaVersion() scans for, which sits deep in the PRG.
//
// Pre-2.3.0 releases carry no marker and have no receive path, so they simply don't get the role. (An
// early prototype used a "RISA-SYNC" tag and a 4-byte arm packet; 2.3.0 rejects that arm outright, so
// there is one protocol, not two.)
const RISA_MARKER = "RISA";
const RISA_MARKER_DIGITS = 3;
const RISA_MARKER_SCAN_LEN = 0x150;

/** The app version advertised in the ROM header prefix as "RISAxyz" (e.g. "2.3.0"), or null when the
 *  marker is absent - a pre-2.3.0 build, or not risa at all. Reads at most the header prefix, so the
 *  short RomContext header is enough; identifyRisaVersion() is the whole-ROM equivalent for older carts. */
export function risaMarkerVersion(header: Uint8Array): string | null {
  const limit = Math.min(header.length, RISA_MARKER_SCAN_LEN);
  const end = limit - RISA_MARKER.length - RISA_MARKER_DIGITS;
  for (let i = 0; i <= end; i++) {
    let hit = true;
    for (let j = 0; j < RISA_MARKER.length; j++) {
      if (header[i + j] !== RISA_MARKER.charCodeAt(j)) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;
    const d = i + RISA_MARKER.length;
    // Exactly three ASCII digits, one per version component.
    if (header[d] < 0x30 || header[d] > 0x39) continue;
    if (header[d + 1] < 0x30 || header[d + 1] > 0x39) continue;
    if (header[d + 2] < 0x30 || header[d + 2] > 0x39) continue;
    return `${header[d] - 0x30}.${header[d + 1] - 0x30}.${header[d + 2] - 0x30}`;
  }
  return null;
}

/** True if the ROM advertises the risa host-sync receive path. */
export function isRisaSyncRom(header: Uint8Array): boolean {
  return risaMarkerVersion(header) !== null;
}
