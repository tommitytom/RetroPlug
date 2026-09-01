// F7 (cli/audio-assert.ts): the ergonomic assertions - assertInTune / assertPitchInTune throw only when out
// of tune; spectralFingerprint is stable for identical audio and moves for different timbre; and the F1->F7
// path (decoded Hz -> assertInTune) proves VRC6 A4 is in tune, the test that was missing when N163 shipped.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { assertInTune, assertPitchInTune, spectralFingerprint, assertFingerprint } from "../cli/audio-assert";
import { type ExpansionAudioState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes";
const SR = 44100;

function threw(fn: () => void): boolean { try { fn(); return false; } catch { return true; } }
function rich(f0: number, n = 8, len = 16384): Float32Array {
  const x = new Float32Array(len);
  for (let i = 0; i < len; i++) { let s = 0; for (let h = 1; h <= n; h++) s += (1 / h) * Math.sin((2 * Math.PI * h * f0 * i) / SR); x[i] = 0.4 * s; }
  return x;
}

test("assertInTune passes in tune, throws out of tune", () => {
  expect(!threw(() => assertInTune(440.4, 440, { tolCents: 10 }))).toBeTruthy();
  expect(threw(() => assertInTune(466, 440, { tolCents: 10 }))).toBeTruthy(); // a semitone off
  expect(threw(() => assertInTune(0, 440))).toBeTruthy();                     // no pitch
});

test("assertPitchInTune detects a synthesized tone and enforces tuning", () => {
  expect(!threw(() => assertPitchInTune(rich(440), 440, { tolCents: 8 }))).toBeTruthy();
  expect(threw(() => assertPitchInTune(rich(466.16), 440, { tolCents: 10 }))).toBeTruthy(); // Bb vs A
});

test("spectralFingerprint is stable for identical audio and drifts for different timbre", () => {
  const a = rich(440, 8);
  const b = rich(440, 8);       // identical
  const c = rich(440, 2);       // fewer harmonics -> different timbre
  expect(!threw(() => assertFingerprint(a, spectralFingerprint(b), 1))).toBeTruthy();
  expect(threw(() => assertFingerprint(c, spectralFingerprint(a), 3))).toBeTruthy();
});

test("F1 -> F7: VRC6 A4 decoded Hz is in tune (the test missing when N163 shipped sharp)", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) { console.log("# SKIP: no VRC6 rom"); return; }
  const id = s.project.systems.addSystem(VRC6)!;
  let st: ExpansionAudioState | null = null;
  const tl = new Timeline()
    .note(200, 69, { channel: 6, velocity: 127, durationMs: 400 })
    .at(450, (sess) => (st = sess.backend.getExpansionAudioState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });
  s.project.systems.removeSystem(id);
  // Use the decoded Hz (F1) as the oracle - white-box, exact.
  expect(!threw(() => assertInTune(st!.channels[0].frequency, 440, { tolCents: 10 }))).toBeTruthy();
});

test("assertFingerprint FAILS on a non-finite render (no silent false pass)", () => {
  const bad = new Float32Array(16384); bad[100] = NaN; // one NaN spreads through the FFT to every band
  const golden = spectralFingerprint(rich(440, 8));
  expect(threw(() => assertFingerprint(bad, golden, 3))).toBeTruthy();
});
