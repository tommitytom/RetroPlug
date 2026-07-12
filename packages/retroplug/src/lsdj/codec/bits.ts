// Byte + sub-byte cursors over a flat LSDj buffer — the pure-TS port of
// SavView.hpp (BitView == SavView, BitWriter == SavWriter). Bit semantics match
// liblsdj exactly (bytes.c): a field at (byte, pos, count) is
// `(byte >> pos) & mask(count)` on read and is spliced in via
// `(byte & ~(mask<<pos)) | ((value & mask) << pos)` on write.
//
// THIS IS THE ONLY MODULE where `~` and `<<` appear. JS bitwise ops are 32-bit
// SIGNED: `~x` is negative and `mask << pos` can reach the sign bit, so every
// splice is re-masked to a byte with `& 0xff`. song.ts / sav.ts call these
// methods and never do raw bit math, so the int32/uint8 hazard is contained here.

export const bitMask = (count: number): number => (count >= 8 ? 0xff : (1 << count) - 1);

export class BitView {
  constructor(private readonly d: Uint8Array) {}

  get size(): number {
    return this.d.length;
  }

  inBounds(off: number, len = 1): boolean {
    return off <= this.d.length && len <= this.d.length - off;
  }

  u8(off: number): number {
    return off < this.d.length ? this.d[off] : 0; // out of bounds reads 0, as C++
  }

  bits(off: number, pos: number, count: number): number {
    return (this.u8(off) >> pos) & bitMask(count); // >> of a 0..255 byte is safe
  }

  u16le(off: number): number {
    return this.u8(off) | (this.u8(off + 1) << 8); // < 0x10000, safe
  }

  slice(off: number, len: number): Uint8Array {
    return this.inBounds(off, len) ? this.d.subarray(off, off + len) : new Uint8Array(0);
  }
}

export class BitWriter {
  constructor(private readonly d: Uint8Array) {}

  get size(): number {
    return this.d.length;
  }

  inBounds(off: number, len = 1): boolean {
    return off <= this.d.length && len <= this.d.length - off;
  }

  u8(off: number): number {
    return off < this.d.length ? this.d[off] : 0;
  }

  bits(off: number, pos: number, count: number): number {
    return (this.u8(off) >> pos) & bitMask(count);
  }

  setU8(off: number, v: number): void {
    if (off < this.d.length) this.d[off] = v & 0xff;
  }

  setBits(off: number, pos: number, count: number, v: number): void {
    if (off >= this.d.length) return;
    const mask = bitMask(count);
    // ~(mask<<pos) is a negative int32; re-mask the whole splice back to a byte.
    this.d[off] = ((this.d[off] & ~(mask << pos)) | ((v & mask) << pos)) & 0xff;
  }

  setU16le(off: number, v: number): void {
    this.setU8(off, v);
    this.setU8(off + 1, (v >> 8) & 0xff);
  }

  copyIn(off: number, src: Uint8Array): void {
    if (this.inBounds(off, src.length) && src.length) this.d.set(src, off);
  }

  reader(): BitView {
    return new BitView(this.d);
  }
}
