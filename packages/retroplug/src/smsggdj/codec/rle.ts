// smsggdj's RLE codec - the compression SMDJ4 song blobs are stored in.
//
// A PackBits variant over a 4-BYTE UNIT (one phrase row / table row), which is why it earns its keep:
// a sparse song is mostly repeated `00 FF 00 00` empty steps, so whole pools collapse to a few bytes.
//
//   control byte c:
//     bit 7 = 0  ->  literal run: copy the next (c & 0x7F) + 1 units verbatim   (1..128 units)
//     bit 7 = 1  ->  repeat  run: read 1 unit, emit it (c & 0x7F) + 2 times     (2..129 units)
//
// Ported from /workspaces/smsggdj/tools/rle.js, which is itself cross-checked against the on-cart Z80
// codec (src/rle.asm) by tools/rletest.py + rle_z80mirror.py. `compress` is byte-identical to that
// reference - certified over a corpus during authoring and pinned by frozen vectors in
// test/smsggdj/rle.test.ts, the same oracle discipline used for the LSDj sav codec against liblsdj.
// Do not "improve" the packer: the cart decodes whatever we write, but a stream this repo can produce
// and `tools/savetool.html` cannot read would be a silent interop break.
//
// `decompress` is deliberately STRICTER than the reference, which walks off the end of a truncated
// stream and turns the resulting undefined bytes into zeroes. That is fine for a tool reading its own
// output and wrong for us: a save file is untrusted input, and a silently zero-filled song looks like
// a valid empty one. Malformed input returns null here and the caller rejects the entry.

/** The compression unit: one phrase/table row. Runs are counted in units, never in bytes. */
export const RLE_UNIT = 4;

const MAX_LITERAL_UNITS = 128; // (c & 0x7F) + 1
const MAX_REPEAT_UNITS = 129; // (c & 0x7F) + 2
const REPEAT_FLAG = 0x80;

function unitsEqual(a: Uint8Array, i: number, j: number): boolean {
  return a[i] === a[j] && a[i + 1] === a[j + 1] && a[i + 2] === a[j + 2] && a[i + 3] === a[j + 3];
}

/** RLE-compress `data` (length must be a multiple of RLE_UNIT). Byte-identical to tools/rle.js. */
export function rleCompress(data: Uint8Array): Uint8Array {
  if (data.length % RLE_UNIT !== 0) throw new Error(`rleCompress: length ${data.length} is not a multiple of ${RLE_UNIT}`);
  const n = data.length / RLE_UNIT;
  // Worst case is every unit its own literal run: n * (1 + UNIT) = data.length * 1.25. Doubling is
  // ample and saves growing an array per unit, which matters in QuickJS on a 6,912-byte block.
  const out = new Uint8Array(data.length * 2 + 1);
  let w = 0;
  let i = 0;

  while (i < n) {
    let run = 1;
    while (i + run < n && run < MAX_REPEAT_UNITS && unitsEqual(data, (i + run) * RLE_UNIT, i * RLE_UNIT)) run++;

    if (run >= 2) {
      out[w++] = REPEAT_FLAG | (run - 2);
      out.set(data.subarray(i * RLE_UNIT, i * RLE_UNIT + RLE_UNIT), w);
      w += RLE_UNIT;
      i += run;
      continue;
    }

    // A literal run, ending at the first unit that starts a 2+ repeat (which is better spent as one).
    let j = i;
    while (j < n && j - i < MAX_LITERAL_UNITS && !(j + 1 < n && unitsEqual(data, (j + 1) * RLE_UNIT, j * RLE_UNIT))) j++;
    if (j === i) j = i + 1; // the unit at `i` starts a repeat we already rejected; emit it literally
    out[w++] = j - i - 1;
    out.set(data.subarray(i * RLE_UNIT, j * RLE_UNIT), w);
    w += (j - i) * RLE_UNIT;
    i = j;
  }
  return out.slice(0, w);
}

/** Decode an RLE stream. `expectedLen`, when given, is enforced exactly. Returns null for any stream
 *  that is truncated mid-run, overruns, or does not decode to `expectedLen` - a save entry that fails
 *  here is rejected rather than loaded as a plausible-looking empty song. */
export function rleDecompress(stream: Uint8Array, expectedLen?: number): Uint8Array | null {
  const cap = expectedLen ?? Number.MAX_SAFE_INTEGER;
  const out: number[] = [];
  let i = 0;

  while (i < stream.length) {
    const c = stream[i++];
    if (c & REPEAT_FLAG) {
      const count = (c & 0x7f) + 2;
      if (i + RLE_UNIT > stream.length) return null; // truncated: no unit to repeat
      const b = i;
      i += RLE_UNIT;
      if (out.length + count * RLE_UNIT > cap) return null;
      for (let r = 0; r < count; r++) {
        out.push(stream[b], stream[b + 1], stream[b + 2], stream[b + 3]);
      }
    } else {
      const bytes = ((c & 0x7f) + 1) * RLE_UNIT;
      if (i + bytes > stream.length) return null; // truncated literal run
      if (out.length + bytes > cap) return null;
      for (let k = 0; k < bytes; k++) out.push(stream[i++]);
    }
  }
  if (expectedLen !== undefined && out.length !== expectedLen) return null;
  return Uint8Array.from(out);
}

/** Pack a song block for the heap: RLE, or the verbatim block when RLE would not shrink it.
 *
 *  The store-raw floor is what makes the format safe for incompressible data - random bytes expand
 *  under any RLE, and the directory's `raw` flag lets the ROM store them at exactly 6,912 rather than
 *  failing or growing. `>=` rather than `>` matches the reference: an equal-sized RLE stream buys
 *  nothing and costs a decode. */
export function rlePack(block: Uint8Array): { raw: boolean; bytes: Uint8Array } {
  const rle = rleCompress(block);
  return rle.length >= block.length ? { raw: true, bytes: block } : { raw: false, bytes: rle };
}
