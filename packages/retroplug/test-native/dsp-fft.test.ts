// F2 (cli/dsp.ts): the FFT core is correct - a synthesized sine peaks in the right bin, and Parseval's
// theorem holds (time energy == one-sided spectrum energy) within rounding. Pure DSP, no emulator.
import { test, expect } from "../testing/harness";
import { magnitudeSpectrum, fft, nextPow2, toMono, window } from "../cli/dsp";

const SR = 44100;

function sine(freq: number, n: number, sr = SR): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = Math.sin((2 * Math.PI * freq * i) / sr);
  return x;
}

test("magnitudeSpectrum peaks in the bin nearest a synthesized 440 Hz sine", () => {
  const x = sine(440, 8192);
  const { freqs, mag, binHz } = magnitudeSpectrum(x, { window: "hann", sampleRate: SR });
  let peak = 0, pk = 0;
  for (let i = 1; i < mag.length; i++) if (mag[i] > peak) { peak = mag[i]; pk = i; }
  // The peak bin's center frequency is within one bin of 440.
  expect(Math.abs(freqs[pk] - 440) <= binHz).toBeTruthy();
});

test("Parseval: time-domain energy equals one-sided spectrum energy (rect window)", () => {
  const N = 8192; // power of two -> no zero padding, exact
  const x = sine(300, N);
  let eTime = 0;
  for (let i = 0; i < N; i++) eTime += x[i] * x[i];

  const { mag } = magnitudeSpectrum(x, { window: "rect", sampleRate: SR });
  const half = mag.length - 1; // = N/2
  let eFreq = mag[0] * mag[0] + mag[half] * mag[half];
  for (let k = 1; k < half; k++) eFreq += 2 * mag[k] * mag[k];
  eFreq /= N;

  expect(Math.abs(eTime - eFreq) / eTime < 1e-4).toBeTruthy();
});

test("fft matches a direct DFT on a small random signal", () => {
  const N = 16;
  const re = new Float64Array(N), im = new Float64Array(N);
  // Deterministic pseudo-signal (no Math.random - keeps the test reproducible).
  const x = new Float64Array(N);
  for (let i = 0; i < N; i++) x[i] = Math.sin(i * 1.1) + 0.5 * Math.cos(i * 0.3);
  re.set(x);
  fft(re, im);
  // Direct DFT of x, compared to the FFT output bin by bin.
  for (let k = 0; k < N; k++) {
    let dr = 0, di = 0;
    for (let n = 0; n < N; n++) {
      const a = (-2 * Math.PI * k * n) / N;
      dr += x[n] * Math.cos(a);
      di += x[n] * Math.sin(a);
    }
    expect(Math.abs(re[k] - dr) < 1e-9 && Math.abs(im[k] - di) < 1e-9).toBeTruthy();
  }
});

test("nextPow2 + toMono + window helpers behave", () => {
  expect(nextPow2(1) === 1 && nextPow2(3) === 4 && nextPow2(1024) === 1024 && nextPow2(1025) === 2048).toBeTruthy();
  // Interleaved stereo [L0,R0,L1,R1] -> mix is the per-frame average.
  const st = new Float32Array([1, 3, 2, 4]);
  const mono = toMono(st, { channel: "mix" });
  expect(mono.length === 2 && mono[0] === 2 && mono[1] === 3).toBeTruthy();
  expect(toMono(st, { channel: "left" })[1] === 2 && toMono(st, { channel: "right" })[1] === 4).toBeTruthy();
  // window() extracts a mono slice and zero-fills past the end.
  const w = window(st, 0, 4, SR);
  expect(w[0] === 2 && w[1] === 3 && w[2] === 0 && w[3] === 0).toBeTruthy();
});

test("nextPow2 does not hang on huge n; window zero-fills a negative startMs (no NaN)", () => {
  expect(nextPow2(2 ** 30 + 1) === 2 ** 31).toBeTruthy(); // finite cap, not an infinite 32-bit-shift loop
  const w = window(new Float32Array([1, 1, 1, 1]), -100, 4, SR); // negative startMs -> zero-fill, not NaN
  expect(w.every((v) => Number.isFinite(v))).toBeTruthy();
});
