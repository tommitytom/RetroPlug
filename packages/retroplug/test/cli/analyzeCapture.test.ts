// `analyze-capture`'s pitch gate: a capture channel at its noise floor still yields a confident "pitch"
// from the detector (an idle input read as 750 Hz at confidence 1.00), so the pitch line is gated on the
// window's level against --floor, and the detector is not even consulted for a silent window.
import { test, expect } from "../../testing/harness";
import { pitchLine } from "../../cli/sessions/analyze-capture";

test("a window under the floor is reported silent and the detector is never run", () => {
  let ran = false;
  const line = pitchLine(-75, -70, () => { ran = true; return { hz: 750, cents: 0, confidence: 1, harmonics: 4 }; }, 440);
  expect(ran).toBeFalsy();
  expect(line).toBe("  pitch: silent (rms -75.00 dBFS is under the -70 dBFS floor; no pitch reported)");
});

test("a sounding window reports the detected pitch, confidence and the error vs --expect-hz", () => {
  const line = pitchLine(-34, -70, () => ({ hz: 440.29, cents: 1.1, confidence: 0.97, harmonics: 5 }), 440);
  expect(line.startsWith("  pitch   440.29 Hz   confidence  0.97   vs 440 Hz:")).toBeTruthy();
  expect(line.endsWith("cents")).toBeTruthy();
  expect(pitchLine(-34, -70, () => ({ hz: 440, cents: 0, confidence: 1, harmonics: 5 }))).toBe("  pitch   440.00 Hz   confidence  1.00");
  expect(pitchLine(-34, -70, () => ({ hz: 0, cents: NaN, confidence: 0, harmonics: 0 }))).toBe("  pitch: none detected");
});
