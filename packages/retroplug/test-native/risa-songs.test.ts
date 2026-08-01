// M1 native smoke: prove the risa catalog reader works on bytes that came through RetroPlug's REAL
// readSram seam (not just a fixture file). Boot risa with a v2 battery that carries a known song, read
// the live 64 KB WRAM back, and list it. SKIPs cleanly when the built ROM is absent (like risa-m0-spike).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { listSongs } from "../src/risaSav";
import { savBytes } from "../test/risa/fixtures";

declare const __DSP_KERNEL_BUNDLE__: string;

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const SAV_PATH = "/tmp/rp-risa-v2-blumarbl.sav";

test("risa catalog reader lists songs from a live battery via readSram", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-songs: no ROM at ${RISA_ROM}`); return; }

  // A v2 battery carrying one known song (BLUMARBL) — the same oracle-verified fixture the golden test uses.
  const battery = savBytes("v2_blumarbl");
  expect(be.writeFile(SAV_PATH, battery)).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = (project.systems.loadRom(RISA_ROM, { explicitSav: SAV_PATH }) as { system: number }).system;
  expect(typeof id).toBe("number");
  expect(project.systems.view()[0].platform).toBe("nes");

  audio.renderAudio(1500); // boot + settle

  const sram = be.readSram(id);
  expect(sram != null).toBeTruthy();
  expect(sram!.length).toBe(0x10000);

  const songs = listSongs(sram!);
  console.log(`[risa-songs] live battery lists ${songs.length} song(s): ${songs.map((s) => s.name).join(", ")}`);
  // The seeded current-format catalog survives the cold boot: the reader sees the known song via readSram.
  expect(songs.some((s) => s.name === "BLUMARBL")).toBeTruthy();
});
