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

// The SIG block carries only the marker + a semver, so the chip is read from the ROM's own on-screen
// CHIP LABEL instead: `blit_str(0, 1, AUDIO_CHIP_NAME)` in the ROM repo's src/ui/ui.c prints it in the
// monitor header, and AUDIO_CHIP_NAME (src/midi/main.h) is picked by the same #if chain that selects the
// audio driver. It is a plain NUL-terminated ASCII literal in RODATA, so the ROM states which chip it
// drives in bytes we can read - one label per build, and never a second chip's.
//
// This is what tells the two MAPPER-69 builds apart, which nothing in the header can: the base 2A03
// build takes FME-7 (69) for kit banking alone, which is also the Sunsoft 5B mapper, and the two ROMs
// are byte-identical across all 16 header bytes, the same size, and not iNES 2.0 (so there is no
// submapper to split them). Their bodies differ in ~16.7 KB; only the label names the difference.
const CHIP_LABELS: ReadonlyArray<readonly [string, BlipToasterChip]> = [
  ["2A03", "2a03"],
  ["VRC6", "vrc6"],
  ["VRC7", "vrc7"],
  ["S5B", "s5b"],
  ["N163", "n163"],
  ["MMC5", "mmc5"],
];

/** How much of a BlipToaster ROM to read to reach the chip label. The cart's linker config
 *  (cfg/nes-banked.cfg) gives the PRG region a HARD size - `PRG: start = $8000, size = $4000`, holding
 *  SIG + LOWCODE + CODE + RODATA, with the DMC kit banks starting at file offset $4010 - so every string
 *  literal in the main window lives in $10..$4010 or the link fails. The bound is enforced by the ROM's
 *  build, not estimated from wherever the label happens to sit today. */
export const BLIPTOASTER_CHIP_SCAN_LEN = 0x4010;

// The fallback when the label is out of reach (a caller that only had a header prefix) or absent: the
// iNES mapper, since each expansion build must use its chip's mapper to get the audio through. Mapper 69
// stays AMBIGUOUS here and resolves to "2a03" - the 2A03 core is a strict subset of the S5B build, so an
// S5B ROM read this way gets correct-but-incomplete lanes (its three squares and the shared envelope are
// missing) rather than wrong ones.
const MAPPER_TO_CHIP: Record<number, BlipToasterChip> = {
  5: "mmc5",
  19: "n163",
  24: "vrc6",
  69: "2a03", // ambiguous with s5b - only the label scan separates them
  85: "vrc7",
};

/** Read the iNES mapper number from a ROM header prefix (low nibble in byte 6, high in byte 7). */
export function inesMapper(header: Uint8Array): number {
  if (header.length < 8) return 0;
  return (header[6] >> 4) | (header[7] & 0xf0);
}

// Scan `rom` for a NUL-terminated chip label. Requiring the terminator is what makes this exact rather
// than a substring guess: it rejects the "VRC7 PATCH" heading (ui.c) that shares the VRC7 tag, and drops
// the odds of a chance hit in 6502 code to nil. A ROM naming two different chips is reported as no match
// instead of picking one, so a future build that mentions another chip in a string degrades to the mapper
// rather than answering confidently and wrongly.
function chipFromLabel(rom: Uint8Array): BlipToasterChip | null {
  const limit = Math.min(rom.length, BLIPTOASTER_CHIP_SCAN_LEN);
  let found: BlipToasterChip | null = null;

  for (const [label, chip] of CHIP_LABELS) {
    for (let i = 0; i + label.length < limit; i++) {
      if (rom[i + label.length] !== 0) continue; // the NUL terminator, checked first: it rejects most i
      let hit = true;
      for (let j = 0; j < label.length; j++) {
        if (rom[i + j] !== label.charCodeAt(j)) {
          hit = false;
          break;
        }
      }
      if (!hit) continue;
      if (found !== null && found !== chip) return null;
      found = chip;
      break;
    }
  }
  return found;
}

/** Which BlipToaster build `rom` is. Prefers the ROM's own chip label, which needs a prefix of
 *  BLIPTOASTER_CHIP_SCAN_LEN; falls back to the iNES mapper (ambiguous for 69) when the caller passed
 *  a shorter read or the label is absent. Defaults to the 2A03 core. */
export function blipToasterChip(rom: Uint8Array): BlipToasterChip {
  return chipFromLabel(rom) ?? MAPPER_TO_CHIP[inesMapper(rom)] ?? "2a03";
}
