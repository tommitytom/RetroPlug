// LSDj ROM identification: parse the version out of the Game Boy cartridge title. TS only ever sees a
// ROM header prefix (backend.readFilePrefix(romPath, 0x150)), never the whole ROM, and the title at
// 0x134 carries the full "LSDj-vX.Y.Z(+build)" string — so version identification needs nothing more.
import type { LsdjVersion } from "./types";

// Decode the GB cartridge title field (0x134..0x143) to uppercase ASCII, stopping at the first byte
// that isn't printable ASCII. Stopping on non-printable (not just NUL) matters because a 15-char title
// (e.g. "LSDj-v9.3.3aboy") runs right up to 0x142, leaving 0x143 = the CGB flag (0x80/0xC0), which must
// NOT be appended to the title or version parsing sees a trailing garbage char.
export function romTitle(header: Uint8Array): string {
  let s = "";
  for (let i = 0x134; i < 0x144 && i < header.length; i++) {
    const c = header[i];
    if (c < 0x20 || c > 0x7e) break;
    s += String.fromCharCode(c);
  }
  return s.toUpperCase().trim();
}

/** True when the ROM header is any LSDj build (stock or arduinoboy) — title starts with "LSDJ". */
export function isLsdjTitle(header: Uint8Array): boolean {
  return romTitle(header).startsWith("LSDJ");
}

const THIRD = /^(\d*)([A-Z]*)$/;

/**
 * Parse an LSDj title into a version. Handles "LSDJ-V9.4.2", letter patch levels "LSDJ-V9.2.L"
 * (A..N → numeric patch 10.., so ordering is monotonic), and build suffixes "LSDJ-V9.3.3ABOY"
 * (→ patch 3, build "aboy"). Returns null if the title isn't an LSDj version string (e.g. the very
 * old bare "LSDJ" titles that predate the versioned title format).
 */
export function parseLsdjVersion(title: string): LsdjVersion | null {
  const raw = title.toUpperCase().trim();
  const m = /^LSDJ-V(\d+)\.(\d+)\.(.+)$/.exec(raw);
  if (!m) return null;
  const major = parseInt(m[1], 10);
  const minor = parseInt(m[2], 10);
  const third = m[3].trim();

  let patch = 0;
  let patchLabel = third;
  let build: string | null = null;
  const pm = THIRD.exec(third);
  if (pm) {
    const digits = pm[1];
    const letters = pm[2];
    if (digits.length) {
      patch = parseInt(digits, 10); // "3ABOY" → patch 3, build "aboy"
      patchLabel = digits;
      build = letters.length ? letters.toLowerCase() : null;
    } else if (letters.length) {
      patch = 10 + (letters.charCodeAt(0) - 65); // letter patch "L" → 21 (A=10)
      patchLabel = letters[0];
      build = letters.length > 1 ? letters.slice(1).toLowerCase() : null;
    }
  }
  return { major, minor, patch, patchLabel, build, raw };
}

/** Identify an LSDj ROM version from its header prefix, or null if it isn't a versioned LSDj ROM. */
export function identifyLsdj(header: Uint8Array): LsdjVersion | null {
  const t = romTitle(header);
  if (!t.startsWith("LSDJ")) return null;
  return parseLsdjVersion(t);
}
