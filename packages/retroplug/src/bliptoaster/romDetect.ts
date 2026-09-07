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
//   +14  chip           4 bytes: which expansion build this is, ASCII, NUL-padded ($FF = pre-tag ROM)
//   +18  padding        $FF filler out to a FIXED 20-byte block
//
// The block is padded upstream so a field's length cannot shift the code after it (SIG is first in PRG,
// and a slide there changes 6502 page-crossing and so the idle-loop cycle count). The project was renamed
// from EverMIDI to BlipToaster; the marker changed with it, and pre-rename ROMs (which carried
// "evermidi-n8", or "EVERMIDI" before that) are deliberately NOT detected — there is no fallback.

/** The BlipToaster detection marker, which is also the ROM's display name. */
export const BLIPTOASTER_MARKER = "bliptoaster";
const BLIPTOASTER_SCAN_LEN = 0x150;
const CHIP_TAG_OFFSET = 14; // from the marker: past the 11-byte marker + 3-byte semver
const CHIP_TAG_LEN = 4;

/** The expansion audio chip a BlipToaster build carries. Only one can be active at a time, so each is
 *  a separate `.nes`. Decides which CC set the DAW parameter map exposes (parameterMap.ts). */
export type BlipToasterChip = "2a03" | "vrc6" | "vrc7" | "s5b" | "n163" | "mmc5";

// The ASCII a build uses for itself, in the SIG chip tag and in the monitor header it prints on screen.
// Both come from AUDIO_CHIP_NAME (the ROM repo's src/midi/main.h), chosen by the same #if chain that
// selects the audio driver, so the two agree by construction.
const CHIP_LABELS: ReadonlyArray<readonly [string, BlipToasterChip]> = [
  ["2A03", "2a03"],
  ["VRC6", "vrc6"],
  ["VRC7", "vrc7"],
  ["S5B", "s5b"],
  ["N163", "n163"],
  ["MMC5", "mmc5"],
];

function chipForLabel(label: string): BlipToasterChip | null {
  return CHIP_LABELS.find(([l]) => l === label)?.[1] ?? null;
}

// Decode the 4-byte chip tag at `base`. Absent on any pre-tag ROM, whose SIG has $FF filler here, and on
// a tag naming a chip this build does not know - both fall through to the label scan rather than guessing.
function chipTagAt(header: Uint8Array, base: number): BlipToasterChip | null {
  if (base + CHIP_TAG_LEN > header.length) return null;
  let s = "";
  for (let i = 0; i < CHIP_TAG_LEN; i++) {
    const c = header[base + i];
    if (c === 0) break; // NUL padding, e.g. "S5B\0"
    if (c < 0x20 || c > 0x7e) return null; // $FF filler (or anything non-printable) = no tag
    s += String.fromCharCode(c);
  }
  return chipForLabel(s);
}

export interface BlipToasterInfo {
  /** Semantic version [major, minor, patch]. */
  semver: [number, number, number];
  /** Which expansion build, from the SIG chip tag. Null on a ROM built before the tag existed. */
  chip: BlipToasterChip | null;
}

/** Decode the BlipToaster SIG block from a ROM header prefix, or null if the marker is absent. Scans the first
 *  0x150 bytes for the tag, then reads the fields after it. Reads at most the header prefix, so the
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
    return {
      semver: [header[base], header[base + 1], header[base + 2]],
      chip: chipTagAt(header, i + CHIP_TAG_OFFSET),
    };
  }
  return null;
}

/** True if `header` (the ROM prefix) carries the BlipToaster marker. */
export function isBlipToasterRomHeader(header: Uint8Array): boolean {
  return blipToasterInfo(header) !== null;
}

/** True if identifying this ROM's build needs the DEEP prefix - i.e. it is a BlipToaster ROM predating
 *  the SIG chip tag, so only the on-screen label can name its build. Lets a caller holding the short
 *  header decide whether to pay the bigger read (systemsStore.roleHeader). */
export function needsChipLabelScan(header: Uint8Array): boolean {
  const info = blipToasterInfo(header);
  return info !== null && info.chip === null;
}

// ===== Which build is this? =====
//
// Three sources, in order of how much they can be trusted. Nothing in the iNES header can answer it:
// the base 2A03 build takes FME-7 (69) for kit banking alone, which is also the Sunsoft 5B mapper, and
// those two ROMs are byte-identical across all 16 header bytes, the same size, and not iNES 2.0 (so
// there is no submapper to split them). Their bodies differ in ~16.7 KB.
//
//  1. THE SIG CHIP TAG (above). A declared field at a fixed offset inside the 0x150 prefix everything
//     already reads. Present from the build that added it.
//  2. THE ON-SCREEN CHIP LABEL, for ROMs older than that tag. `blit_str(0, 1, AUDIO_CHIP_NAME)` in the
//     ROM repo's src/ui/ui.c prints it in the monitor header, and it is a plain NUL-terminated ASCII
//     literal in RODATA - so an untagged ROM still states which chip it drives. Costs the deep read.
//  3. THE INES MAPPER, when the caller only had a header prefix.

/** How much of a BlipToaster ROM to read to reach the on-screen chip label (source 2). The cart's linker
 *  config (cfg/nes-banked.cfg) gives the PRG region a HARD size - `PRG: start = $8000, size = $4000`,
 *  holding SIG + LOWCODE + CODE + RODATA, with the DMC kit banks starting at file offset $4010 - so every
 *  string literal in the main window lives in $10..$4010 or the link fails. The bound is enforced by the
 *  ROM's build, not estimated from wherever the label happens to sit today. Only a ROM predating the SIG
 *  tag needs it; see `needsChipLabelScan`. */
export const BLIPTOASTER_CHIP_SCAN_LEN = 0x4010;

// The last resort: the iNES mapper, since each expansion build must use its chip's mapper to get the
// audio through. Mapper 69 stays AMBIGUOUS here and resolves to "2a03" - the 2A03 core is a strict
// subset of the S5B build, so an S5B ROM read this way gets correct-but-incomplete lanes (its three
// squares and the shared envelope are missing) rather than wrong ones.
const MAPPER_TO_CHIP: Record<number, BlipToasterChip> = {
  5: "mmc5",
  19: "n163",
  24: "vrc6",
  69: "2a03", // ambiguous with s5b - only the SIG tag or the label separates them
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

/** Which BlipToaster build `rom` is: the SIG chip tag, else the on-screen label (which needs a prefix of
 *  BLIPTOASTER_CHIP_SCAN_LEN), else the iNES mapper (ambiguous for 69). Defaults to the 2A03 core. */
export function blipToasterChip(rom: Uint8Array): BlipToasterChip {
  return (
    blipToasterInfo(rom)?.chip ?? chipFromLabel(rom) ?? MAPPER_TO_CHIP[inesMapper(rom)] ?? "2a03"
  );
}
