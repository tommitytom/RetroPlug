// F5 (cli/spectral-metrics.ts): the timbre metrics move in the right direction - brightness rises with high
// harmonics, harmonicEnergy/THD rise with harmonic content, and the noise floor rises when hiss is added.
// Pure synthesized signals (deterministic LCG noise, no Math.random) so the assertions are stable.
import { test, expect } from "../testing/harness";
import { spectralCentroid, harmonicEnergy, thd, noiseFloorDb, bandEnergyDb } from "../cli/spectral-metrics";

const SR = 44100;
const N = 16384;

function sine(f0: number, n = N): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = 0.5 * Math.sin((2 * Math.PI * f0 * i) / SR);
  return x;
}
function rich(f0: number, nHarm: number, n = N): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) { let s = 0; for (let h = 1; h <= nHarm; h++) s += (1 / h) * Math.sin((2 * Math.PI * h * f0 * i) / SR); x[i] = 0.4 * s; }
  return x;
}
function withNoise(base: Float32Array, amp: number): Float32Array {
  const x = new Float32Array(base.length);
  let seed = 0x1234567; // deterministic LCG
  for (let i = 0; i < base.length; i++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; const r = (seed / 0x7fffffff) * 2 - 1; x[i] = base[i] + amp * r; }
  return x;
}

test("spectralCentroid rises with high-harmonic content (brightness)", () => {
  const dull = sine(440);                 // fundamental only
  const bright = rich(440, 12);           // many harmonics
  expect(spectralCentroid(bright, SR) > spectralCentroid(dull, SR)).toBeTruthy();
});

test("harmonicEnergy + THD rise with harmonic content; a pure sine has ~zero THD", () => {
  const pure = sine(440);
  const harm = rich(440, 8);
  expect(harmonicEnergy(harm, 440, 8, SR) > harmonicEnergy(pure, 440, 8, SR)).toBeTruthy();
  expect(thd(pure, 440, 8, SR) < 0.05).toBeTruthy();
  expect(thd(harm, 440, 8, SR) > 0.2).toBeTruthy();
});

test("noiseFloorDb rises when hiss is added to a clean tone", () => {
  const clean = rich(440, 6);
  const noisy = withNoise(clean, 0.15);
  expect(noiseFloorDb(noisy, SR) > noiseFloorDb(clean, SR)).toBeTruthy();
});

test("bandEnergyDb is far higher in the band containing the tone than an empty band", () => {
  const x = sine(440);
  const inBand = bandEnergyDb(x, 300, 600, SR);   // contains 440
  const empty = bandEnergyDb(x, 3000, 6000, SR);  // no energy
  expect(inBand > empty + 20).toBeTruthy();
});
