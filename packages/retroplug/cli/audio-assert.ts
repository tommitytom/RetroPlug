// F7: ergonomic tuning/timbre assertions so tests don't each reinvent pitch detection, built on the decoded
// Hz (F1: getExpansionAudioState().frequency / getApuState) and the black-box detector (F3: detectPitch).
// These throw a descriptive Error on failure, so they compose inside the harness test() blocks. The
// render-a-note-for-a-ROM glue stays consumer-side (it owns the variant->ROM mapping); this layer operates
// on a measured Hz or an already-rendered mono buffer.

import { detectPitch, centsError } from "./pitch";
import { magnitudeSpectrum, DEFAULT_SAMPLE_RATE } from "./dsp";

/** Assert a measured pitch is within `tolCents` of `expectedHz`. Feed it a decoded-Hz readout (F1, the
 *  better oracle) or a detectPitch result (F3). Throws with the cents error on failure. */
export function assertInTune(measuredHz: number, expectedHz: number, opts: { tolCents?: number } = {}): void {
  const { tolCents = 10 } = opts;
  const cents = centsError(measuredHz, expectedHz);
  if (!(measuredHz > 0)) throw new Error(`assertInTune: no pitch (measured ${measuredHz} Hz), expected ${expectedHz} Hz`);
  if (!(Math.abs(cents) <= tolCents)) {
    throw new Error(`assertInTune: ${measuredHz.toFixed(2)} Hz is ${cents.toFixed(1)} cents from ${expectedHz} Hz (tol ${tolCents})`);
  }
}

/** Detect the pitch of a mono buffer and assert it is within `tolCents` of `expectedHz`. Throws if no
 *  confident pitch is found or it is out of tune. */
export function assertPitchInTune(
  mono: Float32Array,
  expectedHz: number,
  opts: { tolCents?: number; sampleRate?: number; fmin?: number; fmax?: number; minConfidence?: number } = {},
): void {
  const { tolCents = 10, sampleRate = DEFAULT_SAMPLE_RATE, fmin, fmax, minConfidence = 0.5 } = opts;
  const p = detectPitch(mono, { sampleRate, fmin, fmax });
  if (!(p.hz > 0) || p.confidence < minConfidence) {
    throw new Error(`assertPitchInTune: no confident pitch (hz ${p.hz.toFixed(2)}, confidence ${p.confidence.toFixed(2)}), expected ${expectedHz} Hz`);
  }
  assertInTune(p.hz, expectedHz, { tolCents });
}

/** A compact, stable spectral fingerprint of a mono render: log-spaced band energies in dB, normalized so
 *  the loudest band is 0 and quantized to whole dB. Deterministic renders give an identical vector; a timbre
 *  change moves some bands. Store one as a golden and diff with assertFingerprint. */
export function spectralFingerprint(
  mono: Float32Array,
  opts: { bands?: number; sampleRate?: number; fmin?: number; fmax?: number } = {},
): number[] {
  const { bands = 24, sampleRate = DEFAULT_SAMPLE_RATE } = opts;
  const fmin = opts.fmin ?? 50;
  const fmax = opts.fmax ?? Math.min(sampleRate / 2, 12000);
  const { freqs, mag } = magnitudeSpectrum(mono, { sampleRate });

  // Log-spaced band edges; sum |X|^2 per band.
  const edges = new Float64Array(bands + 1);
  for (let b = 0; b <= bands; b++) edges[b] = fmin * Math.pow(fmax / fmin, b / bands);
  const power = new Float64Array(bands);
  for (let i = 0; i < mag.length; i++) {
    const f = freqs[i];
    if (f < fmin || f > fmax) continue;
    // Band index via the log mapping (avoids a per-bin edge scan).
    let b = Math.floor((Math.log(f / fmin) / Math.log(fmax / fmin)) * bands);
    if (b < 0) b = 0; else if (b >= bands) b = bands - 1;
    power[b] += mag[i] * mag[i];
  }
  const db = new Array<number>(bands);
  let max = -Infinity;
  for (let b = 0; b < bands; b++) { db[b] = 10 * Math.log10(power[b] + 1e-30); if (db[b] > max) max = db[b]; }
  return db.map((v) => Math.round(v - max)); // normalize to loudest = 0, quantize to whole dB
}

/** Assert a render's spectral fingerprint has not drifted from `golden` by more than `tol` dB in any band.
 *  Throws identifying the worst band on failure. */
export function assertFingerprint(mono: Float32Array, golden: number[], tol = 3, opts?: Parameters<typeof spectralFingerprint>[1]): void {
  const fp = spectralFingerprint(mono, opts);
  if (fp.length !== golden.length) throw new Error(`assertFingerprint: length ${fp.length} != golden ${golden.length}`);
  // A non-finite render (a NaN/Inf sample spreads through the FFT to every band) must FAIL, not silently
  // pass because |NaN - g| > tol is false.
  if (fp.some((v) => !Number.isFinite(v))) throw new Error(`assertFingerprint: non-finite render (NaN/Inf in the audio); got [${fp.join(",")}]`);
  let worst = 0, worstBand = -1;
  for (let i = 0; i < fp.length; i++) {
    const d = Math.abs(fp[i] - golden[i]);
    if (d > worst) { worst = d; worstBand = i; }
  }
  if (worst > tol) throw new Error(`assertFingerprint: band ${worstBand} drifted ${worst} dB (tol ${tol}); got [${fp.join(",")}]`);
}
