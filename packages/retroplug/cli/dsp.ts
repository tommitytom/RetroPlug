// Dependency-free DSP kernel for the CLI test SDK: a real radix-2 FFT, window functions, and a
// windowed magnitude spectrum over interleaved-stereo @44100 PCM (renderTimeline / renderAudio output).
// This is the shared spectral primitive the pitch detector (cli/pitch.ts), spectrogram (cli/spectrogram.ts)
// and timbre metrics (cli/spectral-metrics.ts) build on, replacing the bespoke inline autocorrelation the
// consumer chip tests used to reimplement. Pure functions over typed arrays: same input -> same output, no
// wall-clock, no Node-only APIs (runs under both the txiki CLI host and a consumer's Node).

export const DEFAULT_SAMPLE_RATE = 44100;

/** Smallest power of two >= n (>= 1). */
export function nextPow2(n: number): number {
  if (n <= 1) return 1;
  // Guard the 32-bit shift: p <<= 1 wraps to negative then 0 past 2^30, which would spin forever.
  if (n > 0x40000000) return 0x80000000;
  let p = 1;
  while (p < n) p <<= 1;
  return p;
}

/** In-place radix-2 Cooley-Tukey FFT. `re`/`im` must be the same power-of-two length; on return they hold
 *  the complex spectrum. Twiddles come from a precomputed table (no accumulated recurrence error), so the
 *  transform is accurate enough for cents-level pitch work. */
export function fft(re: Float64Array, im: Float64Array): void {
  const n = re.length;
  if (n <= 1) return;
  if ((n & (n - 1)) !== 0) throw new Error(`fft: length ${n} is not a power of two`);

  // Bit-reversal permutation.
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
      const ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }

  // Twiddle table: tw[j] = exp(-2*pi*i * j / n), j in [0, n/2).
  const half = n >> 1;
  const cos = new Float64Array(half);
  const sin = new Float64Array(half);
  for (let j = 0; j < half; j++) {
    const a = (-2 * Math.PI * j) / n;
    cos[j] = Math.cos(a);
    sin[j] = Math.sin(a);
  }

  for (let len = 2; len <= n; len <<= 1) {
    const h = len >> 1;
    const step = n / len; // index stride into the full-size twiddle table
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < h; k++) {
        const wr = cos[k * step];
        const wi = sin[k * step];
        const a = i + k;
        const b = a + h;
        const vr = re[b] * wr - im[b] * wi;
        const vi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - vr;
        im[b] = im[a] - vi;
        re[a] += vr;
        im[a] += vi;
      }
    }
  }
}

export type WindowType = "hann" | "hamming" | "blackman" | "rect";

/** Window coefficients of length `n` (periodic form, matching FFT/STFT convention). */
export function windowCoeffs(type: WindowType, n: number): Float64Array {
  const w = new Float64Array(n);
  if (n <= 0) return w;
  if (type === "rect") { w.fill(1); return w; }
  for (let i = 0; i < n; i++) {
    const x = (2 * Math.PI * i) / n; // periodic (i/n, not i/(n-1)) - correct for spectral analysis
    switch (type) {
      case "hann":     w[i] = 0.5 - 0.5 * Math.cos(x); break;
      case "hamming":  w[i] = 0.54 - 0.46 * Math.cos(x); break;
      case "blackman": w[i] = 0.42 - 0.5 * Math.cos(x) + 0.08 * Math.cos(2 * x); break;
    }
  }
  return w;
}

/** De-interleave 2-channel PCM to mono. `channel` picks left, right, or the L+R average (default "mix"). */
export function toMono(pcm: Float32Array, opts: { channel?: "left" | "right" | "mix" } = {}): Float32Array {
  const { channel = "mix" } = opts;
  const n = pcm.length >> 1;
  const out = new Float32Array(n);
  if (channel === "left") for (let i = 0; i < n; i++) out[i] = pcm[i * 2];
  else if (channel === "right") for (let i = 0; i < n; i++) out[i] = pcm[i * 2 + 1];
  else for (let i = 0; i < n; i++) out[i] = 0.5 * (pcm[i * 2] + pcm[i * 2 + 1]);
  return out;
}

/** A mono window of `n` samples starting at `startMs`, extracted from interleaved-stereo PCM (L+R mix).
 *  Zero-fills past the end of `pcm`. */
export function window(
  pcm: Float32Array,
  startMs: number,
  n: number,
  sampleRate: number = DEFAULT_SAMPLE_RATE,
): Float32Array {
  const start = Math.floor((startMs / 1000) * sampleRate) * 2;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const s = start + i * 2;
    // Zero-fill outside the buffer at BOTH ends (a negative startMs must not read out of range -> NaN).
    out[i] = s >= 0 && s + 1 < pcm.length ? 0.5 * (pcm[s] + pcm[s + 1]) : 0;
  }
  return out;
}

export interface Spectrum {
  freqs: Float32Array;    // bin center frequencies in Hz, bins 0..N/2
  mag: Float32Array;      // magnitude per bin (|X|), bins 0..N/2
  sampleRate: number;
  binHz: number;          // sampleRate / fftSize
}

/** Windowed magnitude spectrum of a mono signal. Applies `window` (default Hann - leakage matters for
 *  tuning), zero-pads to the next power of two, FFTs, and returns the magnitude of bins 0..N/2. */
export function magnitudeSpectrum(
  x: Float32Array,
  opts: { window?: WindowType; sampleRate?: number } = {},
): Spectrum {
  const { window: winType = "hann", sampleRate = DEFAULT_SAMPLE_RATE } = opts;
  const size = nextPow2(x.length);
  const win = windowCoeffs(winType, x.length);
  const re = new Float64Array(size);
  const im = new Float64Array(size);
  for (let i = 0; i < x.length; i++) re[i] = x[i] * win[i];
  fft(re, im);

  const bins = (size >> 1) + 1;
  const mag = new Float32Array(bins);
  const freqs = new Float32Array(bins);
  const binHz = sampleRate / size;
  for (let i = 0; i < bins; i++) {
    mag[i] = Math.hypot(re[i], im[i]);
    freqs[i] = i * binHz;
  }
  return { freqs, mag, sampleRate, binHz };
}
