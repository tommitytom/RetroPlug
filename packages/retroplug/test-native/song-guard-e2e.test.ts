// End-to-end on a REAL cart: the guard fires on genuinely dirty work, Save & Load preserves it, and a
// .bak lands next to the sav. Operates on a temp copy.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { mutateLiveSav, lsdjSongCatalog, songLoadWouldDiscard } from "../src/tracker";
import { saveWorkingToCatalog } from "../src/lsdjSongOps";
import { decompressSlot, activeSlot } from "../src/lsdj/codec/sav";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

test("e2e: dirty working song is detected, Save & Load preserves it, .bak is written", () => {
  const be = createRealBackend();
  const src = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs";
  if (!be.fileExists(src + ".gbc")) { console.log("# SKIP"); return; }
  const rom = __CONFIG_DIR__ + "/t.gbc", sav = __CONFIG_DIR__ + "/t.sav";
  be.writeFileAtomic(rom, be.readFile(src + ".gbc")!);
  be.writeFileAtomic(sav, be.readFile(src + ".sav")!);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  project.systems.addSystem(rom);
  audio.renderAudio(3000);

  // Load song 0 so the cart is provably clean, then simulate an edit in working memory.
  mutateLiveSav(be, project.systems, project.systems.view()[0], (s) => lsdjSongCatalog.load(s, 0));
  audio.renderAudio(1500);
  let sys = project.systems.view()[0];
  expect(songLoadWouldDiscard(project.systems, sys)).toBe(false);
  console.log(`# after load: clean, active slot ${activeSlot(project.systems.readSram(sys.id)!)}`);

  // Edit working memory the way the tracker would, via the live sav.
  mutateLiveSav(be, project.systems, sys, (s) => { const o = s.slice(); o[0x200] ^= 0xff; return o; });
  audio.renderAudio(1500);
  sys = project.systems.view()[0];
  expect(songLoadWouldDiscard(project.systems, sys)).toBe(true);
  console.log("# after edit: guard would fire");

  // .bak exists from that write.
  expect(be.fileExists(sav + ".bak")).toBeTruthy();
  console.log("# .bak present");

  // Save & Load as one op, exactly as the menu does it.
  const edited = project.systems.readSram(sys.id)!.slice(0, 0x8000);
  const ok = mutateLiveSav(be, project.systems, sys, (s) => {
    const saved = saveWorkingToCatalog(s);
    return saved ? lsdjSongCatalog.load(saved, 1) : null;
  });
  expect(ok).toBeTruthy();
  audio.renderAudio(1500);

  const out = be.readFile(sav)!;
  expect(lsdjSongCatalog.workingName(out)).toBe(lsdjSongCatalog.list(out)[1].name); // song 1 now loaded
  expect([...decompressSlot(out, 0)!]).toEqual([...edited]); // the edit was preserved into slot 0
  expect(lsdjSongCatalog.workingSongDirty!(out)).toBe(false); // and nothing is pending
  console.log(`# save+load: edit preserved in slot 0, now playing "${lsdjSongCatalog.workingName(out)}"`);
});
