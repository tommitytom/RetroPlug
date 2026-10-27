// The plugin's control-plane bundle, exercised the way the DPF plugin will: import it (composing the
// stores + defining the __rp_* globals; the script nodes are in the bundle the engine embeds), then drive
// project I/O through those globals. Proves the bundle boots, autoloads a real .rplg (reaper's seed path), plays through
// the script nodes, and round-trips a base64 state chunk (DPF getState/setState).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import { createAudioDriver } from "../src/audioDriver";
import "../src/pluginControlPlane"; // side-effect: compose the control plane + define the __rp_* globals
import { lsdjRom } from "../test/systems/fixtures";

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

test("the control-plane bundle composes and exposes the __rp_* globals", () => {
  expect(g.__rp_ready).toBeTruthy(); // composed
  expect(typeof g.__rp_loadProjectPath).toBe("function");
  expect(typeof g.__rp_loadProjectB64).toBe("function");
  expect(typeof g.__rp_saveProjectB64).toBe("function");
});

test("autoload an mGB .rplg.zip → the kernel plays it; base64 getState/setState round-trips", () => {
  // Author an mGB `.rplg.zip` (export = PKZIP) to disk with a throwaway store, then clear it from the
  // shared native engine so the control plane loads into an empty backend (like a fresh plugin instance).
  // __rp_loadProjectPath routes through load(), which picks the zip path by the `.rplg.zip` extension.
  const author = new ProjectStore(createRealBackend(), new RecentStore(createRealBackend()), buildAppRegistry());
  const rplg = __CONFIG_DIR__ + "/cp_mgb.rplg.zip";
  author.systems.loadMgb();
  expect(author.export(rplg)).toBeTruthy();
  author.newProject(); // remove the author's system from the shared native engine

  // Autoload through the control-plane global (reaper's RETROPLUG_AUTOLOAD_PROJECT path).
  const loadPath = g.__rp_loadProjectPath as (p: string) => boolean;
  expect(loadPath(rplg)).toBeTruthy();

  // The load published the graph, its protocol nodes in it; drive audio.
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

test("the latency the plugin reports follows the lsdj-sync its pipeline places: one left out adds none", () => {
  // LSDj in a host-clocked sync mode reports a fixed latency for the host's PDC. On an mGB cart whose project stored
  // lsdj-sync, the store places mgb (the role the ROM calls for) and leaves lsdj-sync out: it runs nothing, so it
  // shifts nothing, and a reported latency would only move the cart off the grid.
  const be = createRealBackend();
  const lsdj = __CONFIG_DIR__ + "/cp_latency_lsdj.gb";
  expect(be.writeFile(lsdj, lsdjRom())).toBeTruthy();
  const project = (name: string, system: Record<string, unknown>): string => {
    const path = __CONFIG_DIR__ + "/" + name;
    const roles = [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode: "midiSync" } }];
    const doc = { schemaVersion: "4", settings: {}, systems: [{ platform: "gb", core: "sameboy", ...system, roles }] };
    expect(be.writeFile(path, new TextEncoder().encode(JSON.stringify(doc)))).toBeTruthy();
    return path;
  };
  const loadPath = g.__rp_loadProjectPath as (p: string) => boolean;
  const latency = g.__rp_syncLatencyMs as () => string;
  expect(loadPath(project("cp_latency_placed.rplg", { romPath: lsdj }))).toBeTruthy();
  expect(latency()).toBe("33"); // an LSDj cart in MIDI sync: its lsdj-sync is placed
  expect(loadPath(project("cp_latency_left_out.rplg", { embeddedRom: "mgb" }))).toBeTruthy();
  expect(latency()).toBe("0");
  (g.__rp_newProject as () => void)(); // the cores go, so nothing later in this host emulates them
});
