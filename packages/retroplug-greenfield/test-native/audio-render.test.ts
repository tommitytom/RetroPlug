// The greenfield host renders REAL audio from a REAL Game Boy core. Boots the embedded mGB
// synth, captures an idle baseline (near-silent control), sends a MIDI C-major chord, and
// asserts the rendered audio is non-silent AND louder than idle — proving the MIDI drove the
// core's sound. Self-contained, in-TS RMS (no reaper), mirroring test/ts/gb/mgb.test.ts.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { SystemsStore } from "../src/systemsStore";

// mGB listens on a MIDI channel per pulse voice; a C-major chord is one note per channel.
const CHORD: number[][] = [
  [0x90, 60, 100], // ch0 C4
  [0x91, 64, 100], // ch1 E4
  [0x92, 67, 100], // ch2 G4
];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("mGB renders non-silent audio from a MIDI chord (idle-silence control)", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();

  const id = new SystemsStore(be).loadMgb()!; // embedded mGB → real core in the shared Project
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init (discarded)

  const idle = rms(audio.renderAudio(500)); // no MIDI yet → near-silent baseline
  for (const msg of CHORD) audio.sendMidi(id, msg);
  const playing = rms(audio.renderAudio(1500)); // the chord rings

  console.log(`[audio-render] mGB RMS idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.01).toBeTruthy(); // control: silent before the notes
  expect(playing > 0.001).toBeTruthy(); // the chord is audible
  expect(playing > idle).toBeTruthy(); // and it was driven by the MIDI
});
