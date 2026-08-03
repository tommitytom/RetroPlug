// Continuous SRAM auto-save against the REAL backend + a REAL core: the battery reaches disk on its own,
// without any explicit save, and a steady cart stops costing writes.
//
// This exists because pump() shipped with NO caller at all - the preference was offered, persisted, and
// did nothing. The unit tests (test/sram/auto-save) drive the saver over a mock; this proves the same
// thing composes with real readSram snapshots and real file IO. What it does NOT cover is the frame-tick
// registration in useSramAutoSave/App.tsx, which follows the same untested convention as useSongWatch.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { UserConfigStore } from "../src/userConfigStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { SramAutoSaver } from "../src/sramAutoSave";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const SRC = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs";

test("Continuous mirrors a live battery to its .sav with no explicit save", () => {
  const be = createRealBackend();
  if (!be.fileExists(SRC + ".gbc")) { console.log("# SKIP: no LSDj cart"); return; }
  const rom = __CONFIG_DIR__ + "/cont.gbc", sav = __CONFIG_DIR__ + "/cont.sav";
  be.writeFileAtomic(rom, be.readFile(SRC + ".gbc")!);
  be.writeFileAtomic(sav, be.readFile(SRC + ".sav")!);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const userConfig = new UserConfigStore(be);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  const id = project.systems.addSystem(rom)!;
  audio.renderAudio(2000);

  const saver = new SramAutoSaver(be, project.systems, userConfig);

  // Off / OnProjectSave: the pump must stay out of the way entirely.
  userConfig.setSramAutoSave("Off");
  expect(saver.pump()).toBe(0);
  userConfig.setSramAutoSave("OnProjectSave");
  expect(saver.pump()).toBe(0);

  // Continuous: first observation seeds against the file we just wrote (identical), so no write...
  userConfig.setSramAutoSave("Continuous");
  const first = saver.pump();
  console.log(`[continuous] first tick wrote ${first} system(s)`);

  // ...and a steady, playing cart keeps costing nothing. This is the raw-hash fast path doing its job; if
  // the battery churned every frame this would write on every tick and the 2s poll would be a disk grinder.
  audio.renderAudio(3000);
  let writes = 0;
  for (let i = 0; i < 5; i++) { audio.renderAudio(500); writes += saver.pump(); }
  console.log(`[continuous] ${writes} write(s) across 5 ticks of steady playback`);
  expect(writes).toBe(0);

  // And when the file does NOT match the live battery, Continuous puts the live one on disk - the whole
  // point of the preference. A fresh saver so this is a first observation (the seed-vs-write branch), which
  // is also what a newly opened editor does.
  const live = project.systems.readSram(id)!;
  expect(be.writeFileAtomic(sav, new Uint8Array(live.length))).toBeTruthy(); // disk is now stale/blank
  expect(sameBytes(be.readFile(sav)!, live)).toBe(false);

  const fresh = new SramAutoSaver(be, project.systems, userConfig);
  const wrote = fresh.pump();
  const after = be.readFile(sav)!;
  console.log(`[continuous] stale file: ${wrote} write(s), sav now matches the live battery=${sameBytes(after, live)}`);
  expect(wrote).toBe(1);
  expect(sameBytes(after, live)).toBe(true);
});

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  return a.length === b.length && a.every((v, i) => v === b[i]);
}
