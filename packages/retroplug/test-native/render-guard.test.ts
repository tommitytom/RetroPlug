// The render calls drive processBlock on the CALLING thread, which only the Engine's owner may do. While
// the background audio thread owns it, they must refuse (as renderBlocks does) rather than render
// concurrently with it; once the thread has stopped they render again.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec } from "../src/backend";

const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

test("renderAudio, renderAudioPerSystem and renderAudioPerChannel refuse while the audio thread runs", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  expect(be.constructSystem(MGB, 1)).toBeTruthy();

  expect(audio.startAudio()).toBeTruthy();
  expect(audio.renderAudio(20).length).toBe(0);
  expect(audio.renderAudioPerSystem(20).length).toBe(0);
  expect(audio.renderAudioPerChannel(1, 20).length).toBe(0);
  expect(audio.stopAudio()).toBeTruthy();

  expect(audio.renderAudio(20).length).toBeGreaterThan(0);
  expect(audio.renderAudioPerSystem(20).length).toBe(1);
  expect(audio.renderAudioPerChannel(1, 20).length).toBe(4);
  be.removeSystem(1);
});
