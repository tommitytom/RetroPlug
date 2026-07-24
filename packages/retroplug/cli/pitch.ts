// Robust fundamental-pitch detection for the CLI test SDK, replacing the octave-ambiguous inline
// autocorrelation the consumer chip tests used (acPitch / verify-pitch). Uses an FFT magnitude spectrum
// (cli/dsp.ts) + Harmonic Product Spectrum with sub-bin parabolic interpolation. HPS survives strong
// harmonics and inharmonic content (square/saw/FM), and - unlike a >=70 Hz autocorrelation floor - a low
// default fmin lets an octave error be DETECTED, not silently filtered (the exact blind spot that let N163
// ship an octave off). White-box decoded Hz (getExpansionAudioState().frequency / getApuState) is still the
// better tuning oracle when available; this is the black-box path for rendered audio.

import { magnitudeSpectrum, DEFAULT_SAMPLE_RATE } from "./dsp";

export interface PitchResult {
  hz: number;          // fundamental in Hz; 0 when no confident pitch (e.g. silence)
  cents: number;       // signed cents from the nearest equal-tempered note (A440 grid); NaN when hz==0
  confidence: number;  // 0..1 - how dominant the HPS peak is over the spectrum
  harmonics: number;   // how many harmonics (of `harmonics`) actually carried energy at the estimate
}

/** Signed cents of a measured pitch vs an expected frequency: 1200*log2(measured/expected). NOT octave-
 *  folded - an octave error reads as +/-1200, so tuning tests actually catch it (the N163 lesson). */
export function centsError(measuredHz: number, expectedHz: number): number {
  if (!(measuredHz > 0) || !(expectedHz > 0)) return NaN;
  return 1200 * Math.log2(measuredHz / expectedHz);
}

/** Signed cents from `hz` to the nearest equal-tempered semitone on the A440 grid (range about -50..+50). */
function centsFromNearestNote(hz: number): number {
  if (!(hz > 0)) return NaN;
  const semis = 12 * Math.log2(hz / 440); // semitones from A4
  return (semis - Math.round(semis)) * 100;
}

/** Fundamental via FFT + Harmonic Product Spectrum. `fmin` defaults low (deliberately) so octave errors are
 *  detected; `harmonics` is the HPS depth (default 5). Returns hz 0 with confidence 0 on silence. Reliable
 *  for 2A03 pulse/triangle and harmonic tones (within a few cents of the true pitch); a strongly inharmonic
 *  FM timbre (VRC7) can lock onto a partial, so for expansion-audio tuning read the decoded Hz
 *  (getExpansionAudioState().frequency, F1) instead - it is exact. */
export function detectPitch(
  x: Float32Array,
  opts: { sampleRate?: number; fmin?: number; fmax?: number; harmonics?: number } = {},
): PitchResult {
  const {
    sampleRate = DEFAULT_SAMPLE_RATE,
    fmin = 20,
    fmax = sampleRate / 2,
    harmonics = 5,
  } = opts;

  const { mag, binHz } = magnitudeSpectrum(x, { window: "hann", sampleRate });
  const bins = mag.length;

  // Silence guard: if there's essentially no spectral energy, there's no pitch.
  let maxMag = 0;
  for (let i = 1; i < bins; i++) if (mag[i] > maxMag) maxMag = mag[i];
  if (maxMag <= 1e-9) return { hz: 0, cents: NaN, confidence: 0, harmonics: 0 };

  // HPS: multiply the spectrum by its 2x..Hx downsamples. A small additive floor keeps a near-pure tone
  // from collapsing to zero (its upper-harmonic bins are ~0). But the floor also lets a subharmonic i0/k
  // score as high as the fundamental (its k-th term lands on the real peak), so the raw HPS argmax
  // octave-folds sparse/pure tones DOWN. The post-argmax correction below snaps such a ghost back up.
  const eps = 1e-6 * maxMag;
  const loBin = Math.max(1, Math.floor(fmin / binHz));
  // A fundamental at bin i needs harmonic H at bin H*i < bins, so the usable top bin is (bins-1)/harmonics.
  const hiBin = Math.min(Math.ceil(fmax / binHz), Math.floor((bins - 1) / harmonics));
  if (hiBin < loBin) return { hz: 0, cents: NaN, confidence: 0, harmonics: 0 };

  const hps = new Float64Array(hiBin + 1);
  for (let i = loBin; i <= hiBin; i++) {
    let prod = 1;
    for (let h = 1; h <= harmonics; h++) prod *= mag[h * i] + eps;
    hps[i] = prod;
  }

  // Peak of the HPS = the candidate fundamental bin.
  let peakBin = loBin;
  let peak = hps[loBin];
  for (let i = loBin + 1; i <= hiBin; i++) if (hps[i] > peak) { peak = hps[i]; peakBin = i; }

  // Confidence from the HPS peak's dominance over the median (the ratio cancels HPS's huge dynamic range).
  const sorted = Array.from(hps.slice(loBin, hiBin + 1)).sort((a, b) => a - b);
  const median = sorted.length ? sorted[sorted.length >> 1] : 0;
  const confidence = peak > 0 ? Math.max(0, Math.min(1, 1 - median / peak)) : 0;

  // Subharmonic correction. A true subharmonic ghost bin carries essentially NO energy (only spectral
  // leakage); a real fundamental - even a weak one, as FM/VRC7 patches often have - carries some. So only
  // snap up when the candidate is at the leakage floor (< 2% of the peak), then to the first strong multiple.
  // This unfolds a pure sine (HPS picks i0/k, mag[i0/k] ~ 0 -> snap to i0) and a fundamental above the hiBin
  // cap (HPS finds its subharmonic -> snap up), WITHOUT snapping a genuinely weak-but-present fundamental up
  // to a louder partial (the FM case). Where a signal is strongly inharmonic (FM), prefer decoded Hz (F1).
  let fundBin = peakBin;
  if (mag[peakBin] < 0.02 * maxMag) {
    for (let m = 2; m <= harmonics + 1; m++) {
      const mb = Math.round(peakBin * m);
      if (mb >= bins) break;
      if (mag[mb] >= 0.10 * maxMag) { fundBin = mb; break; }
    }
  }

  // Sub-bin refinement: parabolic interpolation on the log-magnitude around the fundamental bin.
  let refined = fundBin;
  if (fundBin > 0 && fundBin < bins - 1) {
    const a = Math.log(mag[fundBin - 1] + eps);
    const b = Math.log(mag[fundBin] + eps);
    const c = Math.log(mag[fundBin + 1] + eps);
    const denom = a - 2 * b + c;
    if (denom !== 0) {
      const delta = (0.5 * (a - c)) / denom;
      if (delta > -1 && delta < 1) refined = fundBin + delta;
    }
  }
  const hz = refined * binHz;

  // How many harmonics actually reinforced the estimate (carried real energy, not just the floor).
  let usedHarmonics = 0;
  for (let h = 1; h <= harmonics; h++) {
    const b = Math.round(h * refined);
    if (b < bins && mag[b] > 0.1 * maxMag) usedHarmonics++;
  }

  return { hz, cents: centsFromNearestNote(hz), confidence, harmonics: usedHarmonics };
}
