// A system's serial input IN the render loop, driven by the real script nodes. Host MIDI is staged
// globally; the routing node fans it to systems, and each system's `mgb` role's node turns its
// routed bytes into serial input (mGB reads MIDI-over-serial, so raw serial == onMidi).
// The toggle is ROUTING PRESENCE — the native twin of routing.test.ts's two cases: with a routing
// role the chord reaches mGB and it sounds; with none it reaches no system and mGB stays silent. So
// serial input demonstrably reaches the live core, purely from the graph's structure. In-TS RMS.
import { test, expect } from "../testing/harness";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec } from "../src/backend";
import { directGraph } from "../src/graph/directGraph";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

// The embedded mGB, built bare (no roles of its own): the pipeline set here is every protocol node placed for it.
const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

// One note per channel (mGB's per-voice MIDI inputs) — a C-major chord.
const CHORD: number[][] = [
  [0x90, 60, 100],
  [0x91, 64, 100],
  [0x92, 67, 100],
];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a DSP pushSerialIn (mgb role) in the render loop flips the core silent ↔ audible via routing", () => {
  const audio = createAudioDriver();

  const id = 1; // this direct-backend test picks its own id (fresh host per file)
  expect(graph.add(MGB, id)).toBeTruthy(); // a real mGB core
  audio.renderAudio(1500); // warm up mGB

  const stageChord = () => CHORD.forEach((m) => audio.stageMidiIn(m));

  // No routing role -> staged MIDI reaches no system -> mGB gets nothing -> silent.
  expect(graph.setPipeline(id, [{ kind: "mgb", config: {} }])).toBeTruthy();
  stageChord();
  const muted = rms(audio.renderAudio(1500));

  // Add midi-routing (SendToAll) -> the chord reaches mGB's serial via pushSerialIn -> it plays.
  expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
  stageChord();
  const passed = rms(audio.renderAudio(1500));

  console.log(`[dsp-serial] muted=${muted.toFixed(5)} passed=${passed.toFixed(5)}`);
  expect(muted < 0.001).toBeTruthy(); // no routing -> the bytes reach no system -> silent
  expect(passed > 0.001).toBeTruthy(); // routing -> mgb pushes them into serialIn -> the core sounds
  expect(passed > muted).toBeTruthy();
});
