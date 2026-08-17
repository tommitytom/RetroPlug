// Identify an smsggdj ROM and its app version.
//
// Sega carts carry no title field - the header is 16 bytes of magic, checksum, product code and region
// at the END of a bank - so a build marker is the only identity available, exactly as it is for risa on
// the NES. `isSmsggdjRom` (src/smsSync.ts) already finds that marker for the role provider; this adds
// the VERSION, which the tracker integration needs to decide whether this build can be driven.
//
// The version is NOT adjacent to the marker. In v0.45 the marker sits at $3640 and the version string
// at $367B, with the UI string table between them ("REGION: PAL 50HZ", "PLAY", "STOP", "WAIT", ...), so
// this scans for the version's own shape rather than reading a fixed offset - the same approach
// identifyRisaVersion takes, and for the same reason: the layout moves between builds.
import { isSmsggdjRom } from "../smsSync";

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

/** Comparable key for a version string: major, minor, then the point-release letter ("" sorts first). */
function versionKey(v: string): [number, number, string] {
  const m = /^(\d+)\.(\d+)([a-z]?)$/.exec(v);
  return m ? [Number(m[1]), Number(m[2]), m[3]] : [0, 0, ""];
}

/** The first build that maintains the SMDJ4 superblock's currently-loaded-slot byte, and so the first
 *  one whose songs RetroPlug can load: the whole Songs menu turns on writing that byte and cold-booting,
 *  because the working song lives in work RAM and the cart otherwise boots blank (main.asm:238).
 *
 *  An older build is not broken here, it is simply not driveable - the integration reports it
 *  unsupported and the menu greys out, rather than offering rows that would appear to work and load
 *  nothing. Same contract risa uses for a version with no bundled RAM layout. */
export const SMSGGDJ_CUR_SLOT_VERSION = "0.46";

/** Whether this build honours the currently-loaded-slot byte. */
export function supportsCurSlot(version: string | null): boolean {
  if (!version) return false;
  const [maj, min] = versionKey(version);
  const [rMaj, rMin] = versionKey(SMSGGDJ_CUR_SLOT_VERSION);
  return maj > rMaj || (maj === rMaj && min >= rMin);
}
