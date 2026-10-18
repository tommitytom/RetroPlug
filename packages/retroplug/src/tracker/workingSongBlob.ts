// A WorkingSongImage as bytes, for the project archive: the one place a working song that lives outside
// the battery (smsggdj's work RAM) has to be stored as data rather than inside a savestate.
//
// It exists for one window. A reset carries the working song across the reboot and writes it back once
// the new cart has booted (SystemsStore.carryWorkingSong); until then the core holds a blank song, so a
// project exported inside that window - a DAW saving its session, which it can do at any moment, or the
// editor closing before the cart is up - would capture the blank cart and lose the song. The archive
// carries the parked image beside the savestate instead, and loading parks it again.
//
//   0   4  "RPWS"
//   4   1  version (1)
//   5   1  flags: 1 = edited, 2 = settings present, 4 = slot present
//   6   2  block length (LE)
//   8   n  block
//   .   8  name     .   8  echo     .   7  settings (when flagged)
//   .   1  slot index, 4 fingerprint (LE), 1 song count (when flagged)
//
// A binary entry in the zip, not inline JSON, per the no-base64-in-.rplg rule.
import type { WorkingSongImage } from "./songCatalog";

const MAGIC = [0x52, 0x50, 0x57, 0x53]; // "RPWS"
const VERSION = 2; // 2 added the song count to the slot; no version-1 archive ever shipped
const NAME_LEN = 8;
const ECHO_LEN = 8;
const SETTINGS_LEN = 7;

export function encodeWorkingSong(song: WorkingSongImage): Uint8Array {
  const settings = song.settings && song.settings.length === SETTINGS_LEN ? song.settings : null;
  const n = song.block.length;
  const out = new Uint8Array(8 + n + NAME_LEN + ECHO_LEN + (settings ? SETTINGS_LEN : 0) + (song.slot ? 6 : 0));
  out.set(MAGIC, 0);
  out[4] = VERSION;
  out[5] = (song.edited ? 1 : 0) | (settings ? 2 : 0) | (song.slot ? 4 : 0);
  out[6] = n & 0xff;
  out[7] = (n >> 8) & 0xff;
  let at = 8;
  out.set(song.block, at);
  at += n;
  out.set(song.name.subarray(0, NAME_LEN), at);
  at += NAME_LEN;
  out.set(song.echo.subarray(0, ECHO_LEN), at);
  at += ECHO_LEN;
  if (settings) {
    out.set(settings, at);
    at += SETTINGS_LEN;
  }
  if (song.slot) {
    out[at] = song.slot.index & 0xff;
    new DataView(out.buffer, out.byteOffset).setUint32(at + 1, song.slot.fingerprint >>> 0, true);
    out[at + 5] = song.slot.count === undefined ? 0xff : song.slot.count & 0xff; // 0xff: unknown
  }
  return out;
}

/** Null for anything that is not exactly what encodeWorkingSong writes - a foreign or newer entry is
 *  dropped, never half-read into a cart. */
export function decodeWorkingSong(bytes: Uint8Array): WorkingSongImage | null {
  if (bytes.length < 8 || !MAGIC.every((m, i) => bytes[i] === m) || bytes[4] !== VERSION) return null;
  const flags = bytes[5];
  const n = bytes[6] | (bytes[7] << 8);
  const hasSettings = (flags & 2) !== 0;
  const hasSlot = (flags & 4) !== 0;
  if (bytes.length !== 8 + n + NAME_LEN + ECHO_LEN + (hasSettings ? SETTINGS_LEN : 0) + (hasSlot ? 6 : 0)) return null;
  let at = 8;
  const block = bytes.slice(at, at + n);
  at += n;
  const name = bytes.slice(at, at + NAME_LEN);
  at += NAME_LEN;
  const echo = bytes.slice(at, at + ECHO_LEN);
  at += ECHO_LEN;
  const song: WorkingSongImage = { block, name, echo, edited: (flags & 1) !== 0 };
  if (hasSettings) {
    song.settings = bytes.slice(at, at + SETTINGS_LEN);
    at += SETTINGS_LEN;
  }
  if (hasSlot) {
    song.slot = { index: bytes[at], fingerprint: new DataView(bytes.buffer, bytes.byteOffset).getUint32(at + 1, true) };
    if (bytes[at + 5] !== 0xff) song.slot.count = bytes[at + 5];
  }
  return song;
}
