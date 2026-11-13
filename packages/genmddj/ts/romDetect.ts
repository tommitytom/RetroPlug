// genmddj's identity, from its ROM header: the product code's `GM GENMDDJ-` at $180 (the field reads `GM GENMDDJ-00`,
// src/main.asm:596), and the build's own version string, genmddj's `ver_str` ("V0.27", src/main.asm:18493), in the
// first 64 KiB. Both, because either alone is a guess: a homebrew could fill the product-code field, and a version
// string is four kinds of other data.
//
// 64 KiB, not 32: in the built ROM the version string is at $D1B1 (53681, proved with `grep -boa
// 'V[0-9][0-9]*\.[0-9][0-9]*' build/genmddj.bin`, whose FIRST match is that one), which is past the halfway point of a
// 32 KiB scan and past the 0x150-byte short header the role providers otherwise see. The descriptor's
// providerHeaderBytes is 0x10000 for exactly this reason, so the scan and the prefix agree.
//
// The gate is REQUIRED, not tidy (design section 4): the sync drives controller port 2's TR and TH, which every Mega
// Drive game reads as player 2, so attaching it to any ROM would inject phantom button presses.
const PRODUCT_CODE = "GM GENMDDJ-";
const PRODUCT_CODE_OFFSET = 0x180;
const VERSION_SCAN_LEN = 0x10000;

function matches(rom: Uint8Array, offset: number, text: string): boolean {
  if (rom.length < offset + text.length) return false;
  for (let i = 0; i < text.length; i++) if (rom[offset + i] !== text.charCodeAt(i)) return false;
  return true;
}

// The scan itself, without the product-code gate: the public version function and the identity predicate both use it,
// and if it called isGenmddjRom they would recurse.
function identifyGenmddjVersionUnchecked(rom: Uint8Array): string | null {
  const end = Math.min(rom.length, VERSION_SCAN_LEN);
  for (let i = 0; i < end - 4; i++) {
    if (rom[i] !== 0x56) continue; // 'V'
    let text = "";
    for (let k = i + 1; k < end && text.length < 8; k++) {
      const c = rom[k];
      if ((c >= 0x30 && c <= 0x39) || c === 0x2e) text += String.fromCharCode(c);
      else break;
    }
    const m = /^(\d+\.\d+)$/.exec(text);
    if (m) return m[1];
  }
  return null;
}

/** The version string ("0.27") a genmddj build carries, or null. Gated first, so a ROM that is not genmddj never
 *  reports a version. */
export function identifyGenmddjVersion(rom: Uint8Array): string | null {
  return isGenmddjRom(rom) ? identifyGenmddjVersionUnchecked(rom) : null;
}

/** True when `rom` is a genmddj build: its product code and one of its version strings. */
export function isGenmddjRom(rom: Uint8Array): boolean {
  if (!matches(rom, PRODUCT_CODE_OFFSET, PRODUCT_CODE)) return false;
  return identifyGenmddjVersionUnchecked(rom) !== null;
}
