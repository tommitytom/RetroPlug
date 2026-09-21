// The byte helpers risa's three codecs (record, working, sav) each kept their own copy of: a little-endian
// u16 pair in two of them, and the 8-byte song-name codec in all three - two byte-for-byte identical and a
// third written as an index loop with its own name, whose comment already pointed at the other two.
import { SONG_NAME_LEN, UNTITLED } from "./constants";

export function readU16(bytes: Uint8Array, off: number): number {
  return bytes[off] | (bytes[off + 1] << 8);
}

export function writeU16(bytes: Uint8Array, off: number, value: number): void {
  bytes[off] = value & 0xff;
  bytes[off + 1] = (value >> 8) & 0xff;
}

/** Decode a song name: ASCII up to the first NUL, right-trimmed; empty -> UNTITLED. */
export function decodeName(bytes: Uint8Array): string {
  let out = "";
  for (const byte of bytes) {
    if (byte === 0) break;
    out += String.fromCharCode(byte);
  }
  return out.replace(/\s+$/, "") || UNTITLED;
}

/** Encode a song name into SONG_NAME_LEN bytes, SPACE-padded (0x20) as the cart writes them. */
export function encodeName(name: string): Uint8Array {
  const out = new Uint8Array(SONG_NAME_LEN).fill(0x20);
  const s = String(name || UNTITLED).slice(0, SONG_NAME_LEN);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}
