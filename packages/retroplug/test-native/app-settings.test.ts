// Phase 1: system settings actually take effect on the LIVE core, driven through the stores. Gain is
// the crisp proof: a -90 dB edit silences a ringing mGB and 0 dB restores it, so the gain param the store
// sets (setParam) reaches the running emulator. Then the "sameboy" system-role config (highpass live,
// model via restart) reaches it as its system node's settings, published, without breaking playback. And a
// loaded project's saved gain is what its core plays at from its first block, which only the published
// document carries to it (docs/modular-graph-phase-c2-design.md section 5).
// One mGB system at a time: whole-mix RMS needs a single system in the shared native engine.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

// A C-major chord (one note per mGB pulse channel) that mGB sustains — so a later gain edit acts on
// a still-ringing voice.
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

// One project for the file: its graph is the engine's (one publisher per Engine), and each test removes what it built.
const be = createRealBackend();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
const audio = createAudioDriver();

// Warm a fresh mGB up (GB boot + mGB firmware init), sound the chord, and measure it.
const chordRms = (): number => {
  audio.renderAudio(1500);
  CHORD.forEach((m) => audio.stageMidiIn(m));
  return rms(audio.renderAudio(500));
};

test("live gain silences/restores a ringing mGB, and sameboy role config dispatches (through the stores)", () => {
  const id = project.systems.loadEmbedded("mgb")!;
  expect(typeof id).toBe("number");

  const loud = chordRms(); // the chord rings

  // --- live gain: the crisp proof that the gain param the store sets reaches the running core. Discard a short
  // settle window after each edit so RMS measures the settled level, not the ~20 ms gain ramp. ---
  expect(project.systems.setGain(id, -90)).toBeTruthy(); // floor → ~silence
  audio.renderAudio(150); // let the gain smoother ramp down (discard the transient)
  const silenced = rms(audio.renderAudio(500));
  expect(project.systems.setGain(id, 0)).toBeTruthy(); // unity → audible again
  audio.renderAudio(150); // ramp back up
  const restored = rms(audio.renderAudio(500));

  console.log(`[app-settings] loud=${loud.toFixed(5)} silenced=${silenced.toFixed(5)} restored=${restored.toFixed(5)}`);
  expect(loud > 0.001).toBeTruthy(); // the chord is audible
  expect(silenced < 0.001).toBeTruthy(); // -90 dB attenuated it to silence
  expect(restored > silenced).toBeTruthy(); // 0 dB brought it back — gain edits reach the live core

  // --- sameboy system-role config: highpass is live, model triggers a restart. Both must reach the core
  // as its node's settings, published, and leave the system playing. ---
  expect(project.systems.setRoleConfig(id, "sameboy", { highpass: "removeDcOffset" })).toBeTruthy(); // RemoveDcOffset, live
  const afterHighpass = rms(audio.renderAudio(500));
  expect(afterHighpass > 0.001).toBeTruthy(); // still ringing after a live filter change

  expect(project.systems.setRoleConfig(id, "sameboy", { model: "dmgB" })).toBeTruthy(); // DmgB → restartEmulator
  audio.renderAudio(1500); // the core rebooted — warm it up again
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const afterModel = rms(audio.renderAudio(1500));
  console.log(`[app-settings] afterHighpass=${afterHighpass.toFixed(5)} afterModel=${afterModel.toFixed(5)}`);
  expect(afterModel > 0.001).toBeTruthy(); // survived the model-change restart and plays on the new model
  project.systems.removeSystem(id);
});

test("a duplicate of a sounding system plays its first block at the gain its document carries, not a ramp", () => {
  // A duplicate boots a fresh core from the source's savestate, so it is sounding from its first block, where a loaded
  // project is still booting: only here can the first block's level be measured. A node gives its core the document's
  // gain at that block, snapped in, so a turned-down duplicate is silent from its first sample rather than ramping
  // down from unity. (The loaded-project case below discards 1500 ms of boot, so a 20 ms ramp hides inside it.)
  const firstBlockRms = (gainDb: number): number => {
    const src = project.systems.loadEmbedded("mgb")!;
    audio.renderAudio(1500); // the GB boot and mGB's firmware init
    CHORD.forEach((m) => audio.stageMidiIn(m));
    audio.renderAudio(500); // the chord rings
    if (gainDb !== 0) expect(project.systems.setGain(src, gainDb)).toBeTruthy();
    audio.renderAudio(150); // a live edit's own ramp settles before the state is captured
    const clone = project.systems.duplicateSystem(src)!;
    project.systems.removeSystem(src); // the clone alone, so what is measured is its first block
    const first = rms(audio.renderAudio(10)); // the first ~10 ms it plays
    project.systems.removeSystem(clone);
    return first;
  };
  const loud = firstBlockRms(0);
  const quiet = firstBlockRms(-90);
  console.log(`[app-settings] duplicate first 10 ms: 0 dB=${loud.toFixed(6)}, -90 dB=${quiet.toFixed(6)}`);
  expect(loud > 0.001).toBeTruthy(); // the clone rings from its first block
  expect(quiet).toBe(0); // and at -90 dB it is silent there: dbToLin's floor is exactly 0, not a ramp from unity
});

test("a project loaded with a saved gain plays at it from its first block, before any edit", () => {
  // Nothing but the published document carries the saved gain to a loaded core: it is built at 0 dB.
  const saved = project.systems.loadEmbedded("mgb")!;
  expect(project.systems.setGain(saved, -90)).toBeTruthy();
  const bytes = project.exportBytes()!;
  project.newProject();
  expect(project.loadBytes(bytes).kind).toBe("loaded");
  const [id] = project.systems.systems().map((e) => e.id);
  expect(project.systems.view()[0].settings.gainDb).toBe(-90);
  const loaded = chordRms();
  expect(project.systems.setGain(id, 0)).toBeTruthy();
  audio.renderAudio(150); // the ramp back up
  const restored = rms(audio.renderAudio(500));
  console.log(`[app-settings] loaded at -90 dB=${loaded.toFixed(5)} restored=${restored.toFixed(5)}`);
  expect(loaded < 0.001).toBeTruthy(); // silent: the core runs at the gain the project saved
  expect(restored > 0.001).toBeTruthy(); // and it is the gain: at 0 dB the same system rings
  project.systems.removeSystem(id);
});
