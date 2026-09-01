// F3 (cli/pitch.ts): detectPitch is accurate on synthesized harmonic tones AND on real emulator-rendered
// 2A03 pulse, cross-checked against the white-box getApuState().pulse1.frequency (the emulator's own decoded
// Hz). This is the black-box tuning path; where a decoded-Hz readout exists it is the better oracle.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { detectPitch, centsError } from "../cli/pitch";
import { window } from "../cli/dsp";
import { type ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const SR = 44100;

/** A harmonic-rich tone (sum of 1/h harmonics) - what HPS is designed for; a pure sine is a degenerate case. */
function harmonicTone(f0: number, n: number, nHarm = 8, sr = SR): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let h = 1; h <= nHarm; h++) s += (1 / h) * Math.sin((2 * Math.PI * h * f0 * i) / sr);
    x[i] = 0.5 * s;
  }
  return x;
}

function pureSine(f0: number, n: number, sr = SR): Float32Array {
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = 0.5 * Math.sin((2 * Math.PI * f0 * i) / sr);
  return x;
}

test("detectPitch nails a synthesized 220 Hz harmonic tone within 5 cents", () => {
  const p = detectPitch(harmonicTone(220, 16384), { sampleRate: SR });
  expect(p.hz > 0 && Math.abs(centsError(p.hz, 220)) < 5).toBeTruthy();
  expect(p.confidence > 0.5).toBeTruthy();
});

test("detectPitch distinguishes octaves (110 vs 220) - not octave-folded", () => {
  const lo = detectPitch(harmonicTone(110, 16384), { sampleRate: SR });
  const hi = detectPitch(harmonicTone(220, 16384), { sampleRate: SR });
  // The ratio is ~2, and neither collapses to the other's octave.
  expect(Math.abs(hi.hz / lo.hz - 2) < 0.03).toBeTruthy();
  expect(Math.abs(centsError(lo.hz, 110)) < 10 && Math.abs(centsError(hi.hz, 220)) < 10).toBeTruthy();
});

test("detectPitch does NOT octave-fold a PURE sine (the subharmonic-lock regression)", () => {
  // A pure sine is the degenerate HPS case: without the subharmonic correction it locked onto i0/k (1-2
  // octaves low) at confidence ~1.0. Each of these must read its own frequency, not a subharmonic.
  for (const f of [220, 440, 880]) {
    const p = detectPitch(pureSine(f, 16384), { sampleRate: SR });
    expect(p.hz > 0 && Math.abs(centsError(p.hz, f)) < 20).toBeTruthy();
  }
});

test("detectPitch does NOT octave-fold a high fundamental above the HPS bin cap", () => {
  // Fundamentals above sampleRate/(2*harmonics) (~4410 Hz at 44.1k/5) fell below the hiBin cap and read an
  // octave low. A high chip note (~5000 Hz) must resolve near itself.
  const p = detectPitch(harmonicTone(5000, 16384, 3), { sampleRate: SR });
  expect(p.hz > 0 && Math.abs(centsError(p.hz, 5000)) < 25).toBeTruthy();
});

test("detectPitch on emulator 2A03 pulse matches getApuState decoded Hz within 5 cents", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }

  // ch1 -> APU Pulse1 (ch2 is broken in this ROM). Sweep a few octaves.
  for (const note of [45, 57, 69, 81]) {
    const id = s.project.systems.addSystem(NES);
    if (id == null) throw new Error("addSystem failed");
    let apu: ApuState | null = null;
    const tl = new Timeline()
      .note(200, note, { channel: 1, velocity: 100, durationMs: 500 })
      .at(450, (sess) => (apu = sess.backend.getApuState(id)));
    const pcm = renderTimeline(s, tl, { durationMs: 900, warmupMs: 1100 });
    s.project.systems.removeSystem(id);

    const truth = apu!.pulse1.frequency; // the emulator's own decoded pitch = ground truth
    const p = detectPitch(window(pcm, 450, 16384, SR), { sampleRate: SR, fmin: 30 });
    console.log(`# note ${note}: apu=${truth.toFixed(2)}Hz detect=${p.hz.toFixed(2)}Hz (${centsError(p.hz, truth).toFixed(1)}c) conf=${p.confidence.toFixed(2)}`);
    expect(p.hz > 0 && Math.abs(centsError(p.hz, truth)) < 5).toBeTruthy();
  }
});
