// F4: an STFT + a color-mapped spectrogram image, for timbre work (FM patch CCs, DMC playback, vibrato,
// aliasing) where there is no register to read and no single scalar captures "does it sound right" - a
// human or an agent can LOOK at the image. The STFT (stft) is also exposed for programmatic checks.
//
// The image is built as an RGBA buffer in pure TS (magma-style colormap over dB); encoding to PNG reuses
// the host's existing native `pngEncode` (lodepng) rather than a hand-rolled deflate, so callers pass a
// backend (or any {pngEncode, writeFile}) to writeSpectrogramPng. Pure/deterministic otherwise.

import { magnitudeSpectrum, DEFAULT_SAMPLE_RATE, type WindowType } from "./dsp";

export interface StftOpts {
  fftSize?: number;    // default 2048
  hopMs?: number;      // default ~10 ms
  sampleRate?: number; // default 44100
  window?: WindowType; // default "hann"
}

export interface Stft {
  times: Float32Array;    // frame center times in seconds
  freqs: Float32Array;    // bin center frequencies in Hz (0..fftSize/2)
  magDb: Float32Array[];  // per-frame magnitude in dB relative to the global peak (<= 0)
  binHz: number;
}

/** Short-time Fourier transform of a MONO signal. magDb is normalized so the loudest bin across the whole
 *  signal is 0 dB. Bridge from renderTimeline's interleaved stereo with dsp.toMono (or dsp.window). */
export function stft(mono: Float32Array, opts: StftOpts = {}): Stft {
  const { fftSize = 2048, hopMs = 10, sampleRate = DEFAULT_SAMPLE_RATE, window = "hann" } = opts;
  const hop = Math.max(1, Math.round((hopMs / 1000) * sampleRate));
  const nFrames = mono.length >= fftSize ? Math.floor((mono.length - fftSize) / hop) + 1 : 0;

  const frames: Float32Array[] = [];
  const times = new Float32Array(nFrames);
  let freqs: Float32Array = new Float32Array(0);
  let binHz = sampleRate / fftSize;
  let peak = 0;
  const frame = new Float32Array(fftSize);
  for (let f = 0; f < nFrames; f++) {
    const start = f * hop;
    for (let i = 0; i < fftSize; i++) frame[i] = mono[start + i];
    const spec = magnitudeSpectrum(frame, { window, sampleRate });
    freqs = spec.freqs;
    binHz = spec.binHz;
    frames.push(spec.mag);
    times[f] = (start + fftSize / 2) / sampleRate;
    for (let i = 0; i < spec.mag.length; i++) if (spec.mag[i] > peak) peak = spec.mag[i];
  }
  // Normalize to the global peak. A silent signal (peak ~ 0) normalizes by 1 so every bin maps to a very
  // negative dB (dark floor), instead of 0/0 -> 0 dB reading as saturated bright.
  const norm = peak > 1e-12 ? peak : 1;
  const magDb = frames.map((mag) => {
    const db = new Float32Array(mag.length);
    for (let i = 0; i < mag.length; i++) db[i] = 20 * Math.log10((mag[i] + 1e-12) / norm);
    return db;
  });
  return { times, freqs, magDb, binHz };
}

export interface SpectrogramOpts extends StftOpts {
  fmax?: number;         // top of the frequency axis, default 8000
  logFreq?: boolean;     // log frequency axis (default true - musically meaningful)
  db?: [number, number]; // dB range mapped to the colormap, default [-90, 0]
  width?: number;        // default = number of STFT frames
  height?: number;       // default 256
}

export interface RgbaImage { width: number; height: number; rgba: Uint8Array; }

// Magma-style anchors (perceptually increasing luminance), sampled 0..1; linearly interpolated.
const MAGMA: [number, number, number][] = [
  [0.001, 0.000, 0.014], [0.116, 0.062, 0.267], [0.317, 0.072, 0.485], [0.516, 0.122, 0.507],
  [0.716, 0.215, 0.475], [0.897, 0.360, 0.399], [0.984, 0.557, 0.372], [0.996, 0.784, 0.501],
  [0.987, 0.991, 0.749],
];
function magma(t: number): [number, number, number] {
  // Clamp to [0,1]; a non-finite t (e.g. a zero-span dB range makes t = 0/0 = NaN) maps to the floor color.
  const clamped = Number.isFinite(t) ? Math.max(0, Math.min(1, t)) : 0;
  const x = clamped * (MAGMA.length - 1);
  const i = Math.min(MAGMA.length - 2, Math.floor(x));
  const f = x - i;
  const a = MAGMA[i], b = MAGMA[i + 1];
  return [
    Math.round(255 * (a[0] + (b[0] - a[0]) * f)),
    Math.round(255 * (a[1] + (b[1] - a[1]) * f)),
    Math.round(255 * (a[2] + (b[2] - a[2]) * f)),
  ];
}

/** Compute an STFT of a MONO signal and render it to an RGBA spectrogram image (time on X, frequency on Y
 *  with low at the bottom). Pure - no I/O. Persist with writeSpectrogramPng or your own pngEncode + writeFile.
 *  Bridge from an interleaved-stereo render with dsp.toMono. */
export function spectrogramImage(mono: Float32Array, opts: SpectrogramOpts = {}): RgbaImage {
  const s = stft(mono, opts);
  const { fmax = 8000, logFreq = true, db = [-90, 0] } = opts;
  const [dbMin, dbMax] = db;
  const nFrames = s.magDb.length;
  const width = Math.max(1, opts.width ?? nFrames);
  const height = Math.max(1, opts.height ?? 256);
  const nBins = s.freqs.length;
  const fMinDisp = Math.max(s.binHz, 20); // avoid log(0)
  const rgba = new Uint8Array(width * height * 4);

  for (let col = 0; col < width; col++) {
    const frameIdx = nFrames <= 1 || width <= 1 ? 0 : Math.round((col * (nFrames - 1)) / (width - 1));
    const dbRow = s.magDb[frameIdx];
    for (let row = 0; row < height; row++) {
      // row 0 = top = high frequency.
      const yf = height <= 1 ? 1 : (height - 1 - row) / (height - 1); // 0 at bottom, 1 at top
      const freq = logFreq ? fMinDisp * Math.pow(fmax / fMinDisp, yf) : yf * fmax;
      const bin = Math.round(freq / s.binHz);
      const dbv = dbRow && bin >= 0 && bin < nBins ? dbRow[bin] : dbMin;
      const t = (dbv - dbMin) / (dbMax - dbMin);
      const [r, g, b] = magma(t);
      const o = (row * width + col) * 4;
      rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = b; rgba[o + 3] = 255;
    }
  }
  return { width, height, rgba };
}

/** Minimal host seam: the native PNG encoder + file writer (a structural subset of Backend). */
export interface PngWriter {
  pngEncode(width: number, height: number, rgba: Uint8Array): Uint8Array | null;
  writeFile(path: string, bytes: Uint8Array): boolean;
}

/** Render a spectrogram of a MONO signal and write it to `path` as a PNG (via the host's native pngEncode).
 *  Returns whether the file was written. Bridge from an interleaved-stereo render with dsp.toMono. */
export function writeSpectrogramPng(backend: PngWriter, mono: Float32Array, path: string, opts: SpectrogramOpts = {}): boolean {
  const img = spectrogramImage(mono, opts);
  const png = backend.pngEncode(img.width, img.height, img.rgba);
  if (!png) return false;
  return backend.writeFile(path, png);
}
