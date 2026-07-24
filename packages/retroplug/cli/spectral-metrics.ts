// F5: scalar timbre / quality metrics over rendered audio, for regressions that are spectral but not pitch
// (a patch loses its harmonics, DMC gains noise, a channel aliases). Built on the F2 magnitude spectrum.
// Pure functions over a mono Float32Array; all frequencies in Hz, sampleRate defaults to 44100.

import { magnitudeSpectrum, DEFAULT_SAMPLE_RATE } from "./dsp";

/** Spectral centroid ("brightness"): the magnitude-weighted mean frequency in Hz. 0 for a silent signal. */
export function spectralCentroid(x: Float32Array, sampleRate: number = DEFAULT_SAMPLE_RATE): number {
  const { freqs, mag } = magnitudeSpectrum(x, { sampleRate });
  let num = 0, den = 0;
  for (let i = 0; i < mag.length; i++) { num += freqs[i] * mag[i]; den += mag[i]; }
  return den > 0 ? num / den : 0;
}

/** Sum of the magnitudes at the first `n` harmonics of `f0` (H1..Hn) - total harmonic strength. */
export function harmonicEnergy(x: Float32Array, f0: number, n = 8, sampleRate: number = DEFAULT_SAMPLE_RATE): number {
  const { mag, binHz } = magnitudeSpectrum(x, { sampleRate });
  let sum = 0;
  for (let h = 1; h <= n; h++) {
    const bin = Math.round((h * f0) / binHz);
    if (bin > 0 && bin < mag.length) sum += mag[bin];
  }
  return sum;
}

/** Total harmonic distortion: sqrt(sum(H2..Hn^2)) / H1 (0 for a pure sine at f0). Returns 0 if f0 is silent. */
export function thd(x: Float32Array, f0: number, n = 8, sampleRate: number = DEFAULT_SAMPLE_RATE): number {
  const { mag, binHz } = magnitudeSpectrum(x, { sampleRate });
  const binOf = (h: number) => Math.round((h * f0) / binHz);
  const b1 = binOf(1);
  const fundamental = b1 > 0 && b1 < mag.length ? mag[b1] : 0;
  if (fundamental <= 0) return 0;
  let restSq = 0;
  for (let h = 2; h <= n; h++) {
    const bin = binOf(h);
    if (bin > 0 && bin < mag.length) restSq += mag[bin] * mag[bin];
  }
  return Math.sqrt(restSq) / fundamental;
}

/** Noise-floor level: 20*log10(median magnitude / peak magnitude), in dB. A clean tone concentrates energy
 *  (peak >> median -> very negative dB = low floor); a noisy/corrupted signal reads closer to 0 (high floor).
 *  So a clean DMC one-shot has a LOWER noiseFloorDb than a corrupted one. Returns -Infinity for silence. */
export function noiseFloorDb(x: Float32Array, sampleRate: number = DEFAULT_SAMPLE_RATE): number {
  const { mag } = magnitudeSpectrum(x, { sampleRate });
  let peak = 0;
  const vals: number[] = [];
  for (let i = 1; i < mag.length; i++) { vals.push(mag[i]); if (mag[i] > peak) peak = mag[i]; }
  if (peak <= 0 || vals.length === 0) return -Infinity;
  vals.sort((a, b) => a - b);
  const median = vals[vals.length >> 1];
  return 20 * Math.log10((median || 1e-30) / peak);
}

/** Absolute spectral power in the [loHz, hiHz] band, in dB (10*log10 of the summed |X|^2). A band containing
 *  a tone reads far above an empty band. Returns -Infinity for an empty/silent band. */
export function bandEnergyDb(x: Float32Array, loHz: number, hiHz: number, sampleRate: number = DEFAULT_SAMPLE_RATE): number {
  const { freqs, mag } = magnitudeSpectrum(x, { sampleRate });
  let sum = 0;
  for (let i = 0; i < mag.length; i++) {
    if (freqs[i] >= loHz && freqs[i] <= hiHz) sum += mag[i] * mag[i];
  }
  return sum > 0 ? 10 * Math.log10(sum) : -Infinity;
}
