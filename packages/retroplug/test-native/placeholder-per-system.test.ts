// A placeholder holds its slot in the per-system render: [live, placeholder, live] answers three buffers, the empty
// slot's silent, because the planned host width (one pair per slot) sizes the buffers, not the live system count
// (docs/downloadable-packs-m3-design.md section 1).
//
// Run with: `pnpm test:native placeholder-per-system`.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

const PROJECT = "/tmp/rp-placeholder-per-system.rplg";

const peak = (a: Float32Array): number => {
  let m = 0;
  for (let i = 0; i < a.length; i++) m = Math.max(m, Math.abs(a[i]));
  return m;
};

test("renderAudioPerSystem answers one buffer per slot, a placeholder's silent", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver(project.graph);
  const systems = [
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
    { platform: "zz-missing", core: "zz-core", romPath: "/nowhere/song.zz", roles: [{ kind: "zz-core", config: {} }] },
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
  ];
  const settings = { midiRouting: "sendToAll" };
  be.writeFileAtomic(PROJECT, new TextEncoder().encode(JSON.stringify({ schemaVersion: "4", settings, systems })));
  expect(project.load(PROJECT).kind).toBe("loaded");
  expect(project.systems.slotView().map((v) => v.placeholder !== undefined)).toEqual([false, true, false]);

  audio.renderAudio(1500); // warm up (discarded)
  audio.stageMidiIn([0x90, 60, 100]); // sendToAll: every live system hears it
  const bufs = audio.renderAudioPerSystem(1500);
  console.log(`[placeholder-per-system] peaks ${bufs.map((b) => peak(b).toFixed(4)).join(" ")}`);
  expect(bufs.length).toBe(3);
  expect(peak(bufs[1])).toBe(0);
  expect(peak(bufs[0]) > 0).toBe(true);
  expect(peak(bufs[2]) > 0).toBe(true);

  project.systems.slotView().forEach((v) => project.systems.removeSystem(v.id));
  be.deleteFile(PROJECT);
});
