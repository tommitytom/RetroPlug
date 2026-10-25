// The render calls drive processBlock on the CALLING thread, which only the Engine's owner may do. While
// the background audio thread owns it, they must refuse (as renderBlocks does) rather than render
// concurrently with it; once the thread has stopped they render again.
import { test, expect } from "../testing/harness";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec } from "../src/backend";
import { directGraph } from "../src/graph/directGraph";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

test("renderAudio, renderAudioPerSystem and renderAudioPerChannel refuse while the audio thread runs", () => {
  const audio = createAudioDriver(graph.graph);
  expect(graph.add(MGB, 1)).toBeTruthy();

  expect(audio.startAudio()).toBeTruthy();
  expect(audio.renderAudio(20).length).toBe(0);
  expect(audio.renderAudioPerSystem(20).length).toBe(0);
  expect(audio.renderAudioPerChannel(1, 20).length).toBe(0);
  expect(audio.stopAudio()).toBeTruthy();

  expect(audio.renderAudio(20).length).toBeGreaterThan(0);
  expect(audio.renderAudioPerSystem(20).length).toBe(1);
  expect(audio.renderAudioPerChannel(1, 20).length).toBe(4);
  graph.remove(1);
});
