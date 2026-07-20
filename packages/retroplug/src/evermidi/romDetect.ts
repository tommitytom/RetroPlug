// EverMIDI ROM detection. EverMIDI is an NROM cart whose iNES header is indistinguishable from any other
// NROM game (no mapper/battery fingerprint like risa, no cartridge-title field like Game Boy). So it embeds
// a fixed ASCII marker "EVERMIDI" + a 1-byte version at the ROM head ($8000, file offset 0x10; see the SIG
// segment in the evermidi repo's rom/sig.s). We detect it by scanning the RomContext header prefix
// (ROLE_HEADER_LEN = 0x150 bytes) for that tag — the same approach risa uses for its "RISA-SYNC" marker.

const EVERMIDI_MARKER = "EVERMIDI";
const EVERMIDI_SCAN_LEN = 0x150;

/** The EverMIDI ROM format version advertised in the header prefix, or -1 if the marker is absent. Scans
 *  the first 0x150 bytes for the ASCII "EVERMIDI" tag; the byte immediately after it is the version (0x01
 *  today). Reads at most the header prefix, so the short RomContext header is enough. */
export function everMidiVersion(header: Uint8Array): number {
  const limit = Math.min(header.length, EVERMIDI_SCAN_LEN);
  for (let i = 0; i + EVERMIDI_MARKER.length < limit; i++) {
    let hit = true;
    for (let j = 0; j < EVERMIDI_MARKER.length; j++) {
      if (header[i + j] !== EVERMIDI_MARKER.charCodeAt(j)) {
        hit = false;
        break;
      }
    }
    if (hit) return header[i + EVERMIDI_MARKER.length];
  }
  return -1;
}

/** True if `header` (the ROM prefix) carries the EverMIDI marker (any version). */
export function isEverMidiRomHeader(header: Uint8Array): boolean {
  return everMidiVersion(header) >= 0;
}
