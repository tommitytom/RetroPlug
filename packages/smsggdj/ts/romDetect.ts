// Identify an smsggdj ROM and its app version.
//
// Sega carts carry no title field - the header is 16 bytes of magic, checksum, product code and region
// at the END of a bank - so a build marker is the only identity available, exactly as it is for risa on
// the NES. `isSmsggdjRom` (./smsSync.ts) already finds that marker for the role provider; this adds
// the VERSION, which the tracker integration needs to decide whether this build can be driven.
//
// The version is NOT adjacent to the marker. In v0.45 the marker sits at $3640 and the version string
// at $367B, with the UI string table between them ("REGION: PAL 50HZ", "PLAY", "STOP", "WAIT", ...), so
// this scans for the version's own shape rather than reading a fixed offset - the same approach
// identifyRisaVersion takes, and for the same reason: the layout moves between builds.
import { isSmsggdjRom } from "./smsSync";

/** How far in to scan. The marker and the string table both live low in bank 0; scanning the whole
 *  128 KB would risk matching sample data or a song baked into the ROM. */
const SCAN_LEN = 0x8000;

/** A version string like "0.45" or "0.46a", or null when the ROM carries no recognisable one.
 *
 *  The suffix letter is part of the version, not noise: smsggdj ships point releases as v0.45a-style
 *  builds, and treating "0.45a" as "0.45" would claim support for a build we have never seen. */
export function identifySmsggdjVersion(rom: Uint8Array): string | null {
  if (!isSmsggdjRom(rom)) return null;
  const end = Math.min(rom.length, SCAN_LEN);
  for (let i = 0; i < end - 4; i++) {
    if (rom[i] !== 0x56) continue; // 'V'
    let s = "";
    for (let k = i + 1; k < end && s.length < 8; k++) {
      const c = rom[k];
      const digit = c >= 0x30 && c <= 0x39;
      const dot = c === 0x2e;
      const alpha = c >= 0x61 && c <= 0x7a;
      if (digit || dot || alpha) s += String.fromCharCode(c);
      else break;
    }
    const m = /^(\d+\.\d+[a-z]?)$/.exec(s);
    if (m) return m[1];
  }
  return null;
}
