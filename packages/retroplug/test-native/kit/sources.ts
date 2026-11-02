// The kit baseline's synthesised sources, and what writing and hashing them takes. Every expression is the retired C++
// suite's signal generator, transcribed: its double arithmetic is JavaScript's own (both are IEEE doubles, and
// Math.sin on this host is the C library's sin), and each place it rounded to float is a Math.fround here. A binary32
// product rounded once is exactly binary32 multiplication (the double product of two floats is exact), and a double
// difference of two floats rounded once is binary32 subtraction, so each fround below is one float operation.
//
// The proof that these are the sources the baseline was recorded from is SOURCE_HASHES: each source's FNV-1a-64 over
// its float bytes, recorded from the retired C++ suite, which kit-baseline.test.ts holds every source to on the target
// before it compiles anything.

/** The source signals the baseline's matrix uses. */
export type Signal = "sine440" | "sine1200" | "quiet" | "chirp" | "noise" | "silence" | "short";

/** Each signal's length in frames: 8192, and 600 for the source shorter than one resampler chunk. */
export function signalFrames(signal: Signal): number {
  return signal === "short" ? 600 : 8192;
}

const F = Math.fround;

// 0.8f * float(sin(2 * pi * hz * t)), t = i / rate in double.
function sine(hz: number, rate: number, frames: number): Float32Array {
  const out = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const t = i / rate;
    out[i] = F(F(0.8) * F(Math.sin(2.0 * Math.PI * hz * t)));
  }
  return out;
}

// A 200 Hz to 4000 Hz linear sweep, its phase integrated in double: 0.7f * float(sin(phase)).
function chirp(rate: number, frames: number): Float32Array {
  const out = new Float32Array(frames);
  let phase = 0.0;
  for (let i = 0; i < frames; i++) {
    const f = 200.0 + ((4000.0 - 200.0) * i) / frames;
    phase += (2.0 * Math.PI * f) / rate;
    out[i] = F(F(0.7) * F(Math.sin(phase)));
  }
  return out;
}

/** std::mt19937's 32-bit Mersenne Twister, its output sequence as the standard specifies it (the C++ noise source's
 *  generator, seeded by its one-word constructor). */
export class Mt19937 {
  private readonly mt = new Uint32Array(624);
  private index = 624;

  constructor(seed: number) {
    this.mt[0] = seed >>> 0;
    for (let i = 1; i < 624; i++) {
      const prev = this.mt[i - 1] ^ (this.mt[i - 1] >>> 30);
      this.mt[i] = (Math.imul(1812433253, prev) + i) >>> 0;
    }
  }

  next(): number {
    if (this.index >= 624) {
      for (let i = 0; i < 624; i++) {
        const y = (this.mt[i] & 0x80000000) | (this.mt[(i + 1) % 624] & 0x7fffffff);
        let v = this.mt[(i + 397) % 624] ^ (y >>> 1);
        if (y & 1) v ^= 0x9908b0df;
        this.mt[i] = v >>> 0;
      }
      this.index = 0;
    }
    let y = this.mt[this.index++];
    y ^= y >>> 11;
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= y >>> 18;
    return y >>> 0;
  }
}

// Uniform noise in [-0.6, 0.6): unit = float(rng() >> 8) * 2^-24 (exact), then 1.2f * unit - 0.6f in binary32.
function noise(frames: number): Float32Array {
  const rng = new Mt19937(0x5eed1234);
  const out = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const unit = F((rng.next() >>> 8) * (1 / 16777216));
    out[i] = F(F(F(1.2) * unit) - F(0.6));
  }
  return out;
}

/** A signal's samples at `rate`, as the baseline's matrix defines them. */
export function signalSamples(signal: Signal, rate: number): Float32Array {
  const frames = signalFrames(signal);
  switch (signal) {
    case "sine440":
    case "short":
      return sine(440.0, rate, frames);
    case "sine1200":
      return sine(1200.0, rate, frames);
    case "quiet": {
      // The sine scaled by 1/16, an exact power of two: v *= 0.0625f.
      const out = sine(440.0, rate, frames);
      for (let i = 0; i < out.length; i++) out[i] = F(out[i] * 0.0625);
      return out;
    }
    case "chirp":
      return chirp(rate, frames);
    case "noise":
      return noise(frames);
    case "silence":
      return new Float32Array(frames);
  }
}

/** A source's file name stem: `<signal>-<rate>`. */
export function sourceKey(signal: Signal, rate: number): string {
  return `${signal}-${rate}`;
}

/** Each source's FNV-1a-64 over its float bytes (little-endian), recorded with the golden on linux-x64-gnu13. The
 *  noise and the silence do not depend on the rate, so their three hashes are one. */
export const SOURCE_HASHES: Readonly<Record<string, string>> = {
  "chirp-22050": "0425491f935aed01",
  "chirp-44100": "735870598c6d9d72",
  "chirp-48000": "a595850389c0d047",
  "noise-22050": "08ca2ba2e1299154",
  "noise-44100": "08ca2ba2e1299154",
  "noise-48000": "08ca2ba2e1299154",
  "quiet-22050": "42161ba4d3471d02",
  "quiet-44100": "afedb2e5342405a5",
  "quiet-48000": "91ea2ea83ee3d194",
  "short-44100": "0cf632f5372f90b2",
  "silence-22050": "8f6955bf94ec2325",
  "silence-44100": "8f6955bf94ec2325",
  "silence-48000": "8f6955bf94ec2325",
  "sine1200-22050": "5bb6e9a74e070fbe",
  "sine1200-44100": "d24e7b47a71f64ab",
  "sine1200-48000": "c1f44b47987b2501",
  "sine440-22050": "2d851d6783d27970",
  "sine440-44100": "882a8e6d90d79c07",
  "sine440-48000": "331dabe7b2984a0a",
};

/** A 32-bit float, mono WAV (fmt tag 3), every field little-endian: the layout of the written sources. */
export function floatWav(samples: Float32Array, rate: number): Uint8Array {
  const bytes = new Uint8Array(44 + samples.length * 4);
  const v = new DataView(bytes.buffer);
  const tag = (at: number, s: string): void => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)));
  tag(0, "RIFF");
  v.setUint32(4, 36 + samples.length * 4, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  v.setUint32(16, 16, true);
  v.setUint16(20, 3, true); // IEEE float
  v.setUint16(22, 1, true); // mono
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 4, true);
  v.setUint16(32, 4, true);
  v.setUint16(34, 32, true);
  tag(36, "data");
  v.setUint32(40, samples.length * 4, true);
  samples.forEach((s, i) => v.setFloat32(44 + i * 4, s, true));
  return bytes;
}

/** FNV-1a-64 of `bytes` as 16 lowercase hex digits (rp::hash::fnv1a64, the kit baseline's bank hash). The 64-bit
 *  state is two 32-bit halves: the prime is 2^40 + 0x1b3, so a multiply is the state times 0x1b3 plus the state
 *  shifted left 40, each half exact in a double before it is reduced. */
export function fnv1a64Hex(bytes: Uint8Array): string {
  let hi = 0xcbf29ce4;
  let lo = 0x84222325;
  for (let i = 0; i < bytes.length; i++) {
    lo = (lo ^ bytes[i]) >>> 0;
    const loProduct = lo * 0x1b3;
    const carry = Math.floor(loProduct / 0x100000000);
    const newHi = (Math.imul(hi, 0x1b3) >>> 0) + carry + (((lo & 0xffffff) << 8) >>> 0);
    lo = loProduct >>> 0;
    hi = newHi >>> 0;
  }
  return hi.toString(16).padStart(8, "0") + lo.toString(16).padStart(8, "0");
}

/** The float bytes of `samples`, little-endian (the C++ hash reads a float's bytes in memory order on this host). */
export function floatBytes(samples: Float32Array): Uint8Array {
  const out = new Uint8Array(samples.length * 4);
  const v = new DataView(out.buffer);
  samples.forEach((s, i) => v.setFloat32(i * 4, s, true));
  return out;
}
