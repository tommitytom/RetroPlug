// EverMIDI ROM detection. EverMIDI is an NROM cart whose iNES header is indistinguishable from any other
// NROM game (no mapper/battery fingerprint like risa, no cartridge-title field like Game Boy). So it embeds
// a fixed ASCII marker "evermidi-n8" at the ROM head ($8000, file offset 0x10; see the SIG segment in the
// evermidi repo's rom/src/core/sig.s), followed by a 3-byte semantic version. We detect it by scanning the
// RomContext header prefix (ROLE_HEADER_LEN = 0x150 bytes) for that tag — the same approach risa uses for
// "RISA-SYNC". The marker doubles as the ROM's display name.
//
// SIG block, offsets from the marker:
//   +0   "evermidi-n8"  detection marker + display name
//   +11  semver         3 bytes: major, minor, patch

/** The EverMIDI detection marker, which is also the ROM's display name. */
export const EVERMIDI_MARKER = "evermidi-n8";
const EVERMIDI_SCAN_LEN = 0x150;

export interface EverMidiInfo {
  /** Semantic version [major, minor, patch]. */
  semver: [number, number, number];
}

/** Decode the EverMIDI SIG block from a ROM header prefix, or null if the marker is absent. Scans the first
 *  0x150 bytes for the tag, then reads the 3-byte semver after it. Reads at most the header prefix, so the
 *  short RomContext header is enough. */
export function everMidiInfo(header: Uint8Array): EverMidiInfo | null {
  const limit = Math.min(header.length, EVERMIDI_SCAN_LEN);
  for (let i = 0; i + EVERMIDI_MARKER.length < limit; i++) {
    let hit = true;
    for (let j = 0; j < EVERMIDI_MARKER.length; j++) {
      if (header[i + j] !== EVERMIDI_MARKER.charCodeAt(j)) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;

    const base = i + EVERMIDI_MARKER.length; // first byte after the marker = semver major
    return { semver: [header[base], header[base + 1], header[base + 2]] };
  }
  return null;
}

/** True if `header` (the ROM prefix) carries the EverMIDI marker. */
export function isEverMidiRomHeader(header: Uint8Array): boolean {
  return everMidiInfo(header) !== null;
}
