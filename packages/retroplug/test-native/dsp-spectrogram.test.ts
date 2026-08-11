// F4 (cli/spectrogram.ts): the STFT locates a tone in time+frequency, the rendered image maps frequency to
// the right rows (a tone row is brighter than an empty row), and the PNG path round-trips through the host's
// native pngEncode/pngDecode.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { stft, spectrogramImage, writeSpectrogramPng } from "../cli/spectrogram";
import { toMono } from "../cli/dsp";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const SR = 44100;

function sine(freq: number, n: number): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = 0.5 * Math.sin((2 * Math.PI * freq * i) / SR);
  return x;
}

test("spectrogramImage maps frequency to rows; a tone row is far brighter than an empty row", () => {
  // Pure 1000 Hz sine, linear freq axis so row math is exact: row = (H-1)*(1 - f/fmax).
  const pcm = sine(1000, SR); // 1 s mono
  const H = 256, fmax = 8000;
  const img = spectrogramImage(pcm, { fftSize: 2048, hopMs: 20, sampleRate: SR, fmax, logFreq: false, height: H, width: 100 });
  expect(img.width === 100 && img.height === H && img.rgba.length === 100 * H * 4).toBeTruthy();

  const lum = (col: number, row: number) => {
    const o = (row * img.width + col) * 4;
    return img.rgba[o] + img.rgba[o + 1] + img.rgba[o + 2];
  };
  const rowOf = (f: number) => Math.round((H - 1) * (1 - f / fmax));
  const col = 50;
  expect(lum(col, rowOf(1000)) > lum(col, rowOf(4000)) + 60).toBeTruthy(); // tone row much brighter
});

test("stft on an emulator 2A03 A440 shows the fundamental in a mid-note frame", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }
  const id = s.project.systems.addSystem(NES)!;
  const tl = new Timeline().note(200, 69, { channel: 1, velocity: 100, durationMs: 500 });
  const pcm = renderTimeline(s, tl, { durationMs: 900, warmupMs: 1100 });
  s.project.systems.removeSystem(id);

  const sg = stft(toMono(pcm), { fftSize: 4096, hopMs: 20, sampleRate: SR });
  expect(sg.magDb.length > 0).toBeTruthy();
  // A frame ~450 ms in (mid-note): the loudest bin below 1 kHz is the ~440 Hz fundamental.
  const fi = Math.min(sg.magDb.length - 1, Math.round((0.45 - sg.times[0]) / (sg.times[1] - sg.times[0] || 1)));
  const db = sg.magDb[fi];
  let best = -Infinity, bestHz = 0;
  for (let i = 0; i < db.length; i++) { if (sg.freqs[i] > 1000) break; if (db[i] > best) { best = db[i]; bestHz = sg.freqs[i]; } }
  expect(Math.abs(bestHz - 440) < sg.binHz * 2).toBeTruthy();
});

test("writeSpectrogramPng round-trips through native pngEncode/pngDecode", () => {
  const s = bootSession();
  const pcm = sine(1000, SR);
  const img = spectrogramImage(pcm, { fftSize: 2048, hopMs: 20, sampleRate: SR, width: 200, height: 256 });
  const png = s.backend.pngEncode(img.width, img.height, img.rgba);
  expect(png != null && png.length > 8).toBeTruthy();
  // PNG signature.
  expect(png![0] === 0x89 && png![1] === 0x50 && png![2] === 0x4e && png![3] === 0x47).toBeTruthy();
  const dec = s.backend.pngDecode(png!);
  expect(dec != null && dec!.width === 200 && dec!.height === 256 && dec!.rgba.length === 200 * 256 * 4).toBeTruthy();
  // The convenience path writes a file.
  const path = "/tmp/rp-spectrogram-test.png";
  expect(writeSpectrogramPng(s.backend, pcm, path, { width: 200, height: 128 })).toBeTruthy();
});

test("a silent signal renders dark (not saturated), and a zero-span dB range does not crash", () => {
  const img = spectrogramImage(new Float32Array(SR), { fftSize: 2048, hopMs: 20, sampleRate: SR, width: 20, height: 32 });
  let maxLum = 0;
  for (let i = 0; i < img.rgba.length; i += 4) maxLum = Math.max(maxLum, img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2]);
  expect(maxLum < 30).toBeTruthy(); // magma floor, near-black
  const ok = spectrogramImage(sine(1000, SR), { db: [-60, -60], width: 10, height: 16, sampleRate: SR });
  expect(ok.rgba.length === 10 * 16 * 4).toBeTruthy();
});
