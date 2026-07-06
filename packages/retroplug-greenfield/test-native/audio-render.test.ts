// The greenfield host renders REAL audio from a REAL Game Boy core, driven purely through the TS
// DSP kernel (cores construct bare — no C++ roles). Boots the embedded mGB synth, captures an idle
// baseline (near-silent control), then stages a MIDI C-major chord that the kernel's midi-routing
// role fans to the system and its `mgb` role turns into serial input; asserts the rendered audio is
// non-silent AND louder than idle — proving the MIDI drove the core's sound through the kernel path.
// Complements dsp-serial (routing-presence toggle) with an idle-baseline magnitude check. In-TS RMS.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { createDspRuntime } from "../src/dspRuntime";
import { SystemsStore } from "../src/systemsStore";

declare const __DSP_KERNEL_BUNDLE__: string;

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
  const dsp = createDspRuntime();

  const id = new SystemsStore(be).loadMgb()!; // embedded mGB → real bare core in the shared Project
  expect(typeof id).toBe("number");

  // The kernel is the only behaviour layer: midi-routing (SendToAll) fans host MIDI to the system,
  // and its `mgb` role turns the routed bytes into serial input (mGB reads MIDI-over-serial).
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(
    dsp.setSystems({
      project: [{ kind: "midi-routing", config: { mode: 0 } }],
      systems: [{ id, pipeline: [{ kind: "mgb", config: {} }] }],
    }),
  ).toBeTruthy();

  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init (discarded)

  const idle = rms(audio.renderAudio(500)); // no MIDI staged → near-silent baseline
  CHORD.forEach((m) => audio.stageMidiIn(m)); // host MIDI → kernel routing → mGB serial
  const playing = rms(audio.renderAudio(1500)); // the chord rings

  console.log(`[audio-render] mGB RMS idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.01).toBeTruthy(); // control: silent before the notes
  expect(playing > 0.001).toBeTruthy(); // the chord is audible
  expect(playing > idle).toBeTruthy(); // and it was driven by the MIDI through the kernel
});
