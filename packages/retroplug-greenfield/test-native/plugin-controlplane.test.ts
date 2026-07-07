// The plugin's control-plane bundle, exercised the way the DPF plugin will: import it (composing the
// stores + loading the DSP kernel + defining the __rp_* globals), then drive project I/O through those
// globals. Proves the bundle boots, autoloads a real .rplg (reaper's seed path), plays through the
// kernel, and round-trips a base64 state chunk (DPF getState/setState).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import { createAudioDriver } from "../src/audioDriver";
import "../src/pluginControlPlane"; // side-effect: compose the control plane + define the __rp_* globals

declare const __CONFIG_DIR__: string;

const g = globalThis as Record<string, unknown>;

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

test("the control-plane bundle composes, loads the kernel, and exposes the __rp_* globals", () => {
  expect(g.__rp_ready).toBeTruthy(); // kernel compiled + loaded
  expect(typeof g.__rp_loadProjectPath).toBe("function");
  expect(typeof g.__rp_loadProjectB64).toBe("function");
  expect(typeof g.__rp_saveProjectB64).toBe("function");
});

test("autoload an mGB .rplg → the kernel plays it; base64 getState/setState round-trips", () => {
  // Author an mGB .rplg to disk with a throwaway store, then clear it from the shared native Project
  // so the control plane loads into an empty backend (like a fresh plugin instance).
  const author = new ProjectStore(createRealBackend(), new RecentStore(createRealBackend()), buildAppRegistry());
  const rplg = __CONFIG_DIR__ + "/cp_mgb.rplg";
  author.systems.loadMgb();
  expect(author.export(rplg)).toBeTruthy();
  author.newProject(); // remove the author's system from the shared native Project

  // Autoload through the control-plane global (reaper's RETROPLUG_AUTOLOAD_PROJECT path).
  const loadPath = g.__rp_loadProjectPath as (p: string) => boolean;
  expect(loadPath(rplg)).toBeTruthy();

  // The load's onSystemsChange projected the store into the kernel; drive audio.
  const audio = createAudioDriver();
  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init
  const idle = rms(audio.renderAudio(500));
  CHORD.forEach((m) => audio.stageMidiIn(m));
  const playing = rms(audio.renderAudio(1500));
  console.log(`[plugin-controlplane] idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(idle < 0.01).toBeTruthy();
  expect(playing > 0.001).toBeTruthy();
  expect(playing > idle).toBeTruthy(); // the autoloaded project plays through the control-plane path

  // DPF getState → setState("") → setState(chunk) round-trip.
  const saveB64 = g.__rp_saveProjectB64 as () => string;
  const loadB64 = g.__rp_loadProjectB64 as (b: string) => boolean;
  const chunk = saveB64();
  expect(chunk.length > 0).toBeTruthy();
  expect(loadB64("")).toBeTruthy(); // reset to empty
  expect(loadB64(chunk)).toBeTruthy(); // restore from the chunk
});
