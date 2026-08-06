// The confirm-on-Load gate against REAL cores + REAL carts - the twin of test/tracker/workingSongDirty.test.ts
// (which covers the byte-level corpus). Two things only a booted core can prove:
//
//   1. NO FALSE POSITIVES ON A LIVE CART. LSDj is documented as rewriting working RAM every frame
//      (sramAutoSave.ts's whole reason for a semantic signature), so the risk is that the working song
//      drifts from its slot on its own and the prompt fires on every Load until users stop reading it.
//      The cart is verified LIVE (non-zero audio) so a "stable" result can't come from a dead core.
//   2. Broader risa coverage than the pure-TS fixtures allow - those are mostly legacy-layout, whose
//      load-to-working declines, so the real .srm songs are the only multi-song risa sample we have.
//
// Every cart is copied into the test's temp config dir first: the Load path WRITES the .sav, so pointing
// this at the shared resources tree would destroy the fixture's working song (which is exactly the bug
// under test, and exactly how it was destroyed once already).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { mutateLiveSav, lsdjSongCatalog, risaSongCatalog } from "../src/tracker";
import { BANK_DATA, WRAM_BANK_SIZE, SAVE_CURRENT_ENTRY_OFFSET } from "../src/risa/codec/constants";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const LSDJ_ROM = __RESOURCES_DIR__ + "/roms/tripledipper942.gbc";
const LSDJ_SAV = __RESOURCES_DIR__ + "/roms/tripledipper942.sav";
const RISA_ROM = __RESOURCES_DIR__ + "/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes";
const RISA_SRMS = ["hevander", "ecoli_soul", "let_go"];
const START = 7;

const peak = (a: Float32Array): number => {
  let m = 0;
  for (let i = 0; i < a.length; i++) { const v = Math.abs(a[i]); if (v > m) m = v; }
  return m;
};

test("lsdj: a loaded song stays clean across 30s of PLAYBACK on a real core (no false positives)", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ_ROM) || !be.fileExists(LSDJ_SAV)) { console.log("# SKIP: no LSDj cart"); return; }

  // Copy - the Load below writes the .sav.
  const rom = __CONFIG_DIR__ + "/lsdj.gbc", sav = __CONFIG_DIR__ + "/lsdj.sav";
  expect(be.writeFileAtomic(rom, be.readFile(LSDJ_ROM)!)).toBeTruthy();
  expect(be.writeFileAtomic(sav, be.readFile(LSDJ_SAV)!)).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  expect(project.systems.addSystem(rom) != null).toBeTruthy();
  audio.renderAudio(3000);

  // working := slot 0 through the catalog's own load, so the two start provably identical.
  expect(mutateLiveSav(be, project.systems, project.systems.view()[0], (s) => lsdjSongCatalog.load(s, 0))).toBeTruthy();
  audio.renderAudio(2000);

  const id0 = project.systems.view()[0].id;
  expect(lsdjSongCatalog.workingSongDirty!(project.systems.readSram(id0)!)).toBe(false);

  // Start playback: this is what makes LSDj advance (and its clocks tick).
  audio.pressButton(id0, START, true);
  audio.renderAudio(100);
  audio.pressButton(id0, START, false);

  let sawAudio = false;
  for (const ms of [1000, 5000, 14000, 10000]) {
    const buf = audio.renderAudio(ms);
    if (peak(buf) > 0.001) sawAudio = true;
    const id = project.systems.view()[0].id;
    const live = project.systems.readSram(id);
    expect(live != null).toBeTruthy();
    // THE assertion: nothing the running cart does on its own may look like an unsaved edit.
    expect(lsdjSongCatalog.workingSongDirty!(live!)).toBe(false);
  }
  // Guards the guard: a silent/dead core would make the loop above pass trivially.
  expect(sawAudio).toBe(true);
  console.log("[working-dirty] lsdj: clean across 30s of verified playback");
});

test("risa: every song in the real .srm carts loads clean (no false positives)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log("# SKIP: no risa ROM"); return; }

  let checked = 0;
  for (const name of RISA_SRMS) {
    const src = __RESOURCES_DIR__ + `/roms/risa/${name}.srm`;
    if (!be.fileExists(src)) continue;
    const bytes = be.readFile(src)!;
    for (const s of risaSongCatalog.list(bytes)) {
      const loaded = risaSongCatalog.load(bytes, s.index);
      if (!loaded) continue;
      // risa's load leaves the working song unlinked; saveWorkingToCatalog is what links it. Link it here
      // so the CONTENT comparison is what's under test (the unlinked case is covered in the pure-TS twin).
      const linked = risaLink(loaded, s.index);
      expect(risaSongCatalog.workingSongDirty!(linked)).toBe(false);
      checked++;
    }
  }
  console.log(`[working-dirty] risa: ${checked} real-cart slot loads, all clean`);
  expect(checked > 0).toBe(true);
});

// Stamp risa's 'current entry' byte, linking the working song to catalog slot `index`.
function risaLink(sav: Uint8Array, index: number): Uint8Array {
  const out = sav.slice();
  out[BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET] = index & 0xff;
  return out;
}
