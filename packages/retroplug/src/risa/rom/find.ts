// The marker scanner — the risa analog of ../../lsdj/rom/find.ts (port of risa's rom.js findMagicInRange).
// risa asset tables (the theme table) aren't at a static offset: a distinctive 6-byte magic marks the
// table head, so a bounded scan finds it without a per-version offset map.

/** First offset in `[start, start+size)` where `magic` matches, or -1 if absent. */
export function findMagicInRange(bytes: Uint8Array, magic: number[] | Uint8Array, start: number, size: number): number {
  const end = Math.min(bytes.length, start + size) - magic.length;
  for (let off = start; off <= end; off++) {
    let hit = true;
    for (let j = 0; j < magic.length; j++) {
      if (bytes[off + j] !== magic[j]) {
        hit = false;
        break;
      }
    }
    if (hit) return off;
  }
  return -1;
}
