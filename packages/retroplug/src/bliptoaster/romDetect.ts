// BlipToaster ROM detection. BlipToaster is an NROM cart whose iNES header is indistinguishable from any other
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
export const BLIPTOASTER_MARKER = "bliptoaster";
const BLIPTOASTER_SCAN_LEN = 0x150;

export interface BlipToasterInfo {
  /** Semantic version [major, minor, patch]. */
  semver: [number, number, number];
}

/** Decode the BlipToaster SIG block from a ROM header prefix, or null if the marker is absent. Scans the first
 *  0x150 bytes for the tag, then reads the 3-byte semver after it. Reads at most the header prefix, so the
 *  short RomContext header is enough. */
export function blipToasterInfo(header: Uint8Array): BlipToasterInfo | null {
  const limit = Math.min(header.length, BLIPTOASTER_SCAN_LEN);
  for (let i = 0; i + BLIPTOASTER_MARKER.length < limit; i++) {
    let hit = true;
    for (let j = 0; j < BLIPTOASTER_MARKER.length; j++) {
      if (header[i + j] !== BLIPTOASTER_MARKER.charCodeAt(j)) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;

    const base = i + BLIPTOASTER_MARKER.length; // first byte after the marker = semver major
    return { semver: [header[base], header[base + 1], header[base + 2]] };
  }
  return null;
}

/** True if `header` (the ROM prefix) carries the BlipToaster marker. */
export function isBlipToasterRomHeader(header: Uint8Array): boolean {
  return blipToasterInfo(header) !== null;
}

/** The expansion audio chip a BlipToaster build carries. Only one can be active at a time, so each is
 *  a separate `.nes`. Decides which CC set the DAW parameter map exposes (parameterMap.ts). */
export type BlipToasterChip = "2a03" | "vrc6" | "vrc7" | "s5b" | "n163" | "mmc5";

// The SIG block carries only the marker + a semver, so the chip is read off the iNES mapper number
// instead - each expansion build must use its chip's mapper to get the audio through.
//
// GOTCHA: the base 2A03 build uses FME-7 (69) for kit banking, which is ALSO the Sunsoft 5B mapper,
// and the two ROMs are otherwise header-identical (same size, same flag bytes, no iNES 2.0 submapper).
// So 69 is ambiguous and resolves to "2a03": the 2A03 core is a strict subset of the S5B build, so an
// S5B ROM gets correct-but-incomplete lanes (its three squares and the shared envelope are missing)
// rather than wrong ones. Distinguishing them needs a chip byte in the ROM's SIG block; until then an
// S5B user can set `"chip": "s5b"` on the system's `bliptoaster` role in a thin `.rplg`.
const MAPPER_TO_CHIP: Record<number, BlipToasterChip> = {
  5: "mmc5",
  19: "n163",
  24: "vrc6",
  69: "2a03", // ambiguous with s5b - see above
  85: "vrc7",
};

/** Read the iNES mapper number from a ROM header prefix (low nibble in byte 6, high in byte 7). */
export function inesMapper(header: Uint8Array): number {
  if (header.length < 8) return 0;
  return (header[6] >> 4) | (header[7] & 0xf0);
}

/** Which BlipToaster build `header` is, from its iNES mapper. Defaults to the 2A03 core. */
export function blipToasterChip(header: Uint8Array): BlipToasterChip {
  return MAPPER_TO_CHIP[inesMapper(header)] ?? "2a03";
}
