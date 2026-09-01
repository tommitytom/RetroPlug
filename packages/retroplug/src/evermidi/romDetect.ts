// EverMIDI ROM detection. EverMIDI is an NROM cart whose iNES header is indistinguishable from any other
// NROM game (no mapper/battery fingerprint like risa, no cartridge-title field like Game Boy). So it embeds
// a fixed ASCII marker "bliptoaster" at the ROM head ($8000, file offset 0x10; see the SIG segment in the
// ROM repo's rom/src/core/sig.s), followed by a 3-byte semantic version. We detect it by scanning the
// RomContext header prefix (ROLE_HEADER_LEN = 0x150 bytes) for that tag — the same approach risa uses for
// "RISA-SYNC". The marker doubles as the ROM's display name.
//
// SIG block, offsets from the marker:
//   +0   "bliptoaster"  detection marker + display name
//   +11  semver         3 bytes: major, minor, patch
//   +14  padding        $FF filler out to a FIXED 16-byte block
//
// The block is padded upstream so the marker's length cannot shift the code after it (SIG is first in PRG,
// and a slide there changes 6502 page-crossing and so the idle-loop cycle count). We only read the marker
// and the semver, so the padding is inert here. The project was renamed from EverMIDI to BlipToaster; the
// marker changed with it, and pre-rename ROMs (which carried "evermidi-n8", or "EVERMIDI" before that) are
// deliberately NOT detected — there is no fallback.

/** The BlipToaster detection marker, which is also the ROM's display name. */
export const EVERMIDI_MARKER = "bliptoaster";
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
