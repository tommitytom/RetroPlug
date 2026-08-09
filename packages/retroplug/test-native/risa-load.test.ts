// Native cold-boot proof for Load-to-working: the battery loadSongToWorkingInSav produces is accepted by
// the REAL risa firmware and boots showing the loaded song. Boot risa with a v2 battery (song BLUMARBL),
// read the live 64 KB via readSram, Load song 0 into the working region, then cold-boot from it through
// the SAME seam the menu uses (writeFileAtomic + loadSram) and confirm the firmware kept the working song
// (its name at bank-1 0x1E8C) and the catalog. SKIPs when the built ROM is absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { loadSongToWorkingInSav } from "../src/risaSongOps";
import { listSongs, decodeSongName, expandRecordToWorking, recordBytesAt, normalizeSaveContainer, CURRENT_LAYOUT } from "../src/risaSav";
import { savBytes } from "../test/risa/fixtures";

declare const __DSP_KERNEL_BUNDLE__: string;

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const SAV_PATH = "/tmp/rp-risa-load-src.sav";
const LOADED_PATH = "/tmp/rp-risa-load-working.sav";
const NAME_OFF = 0x2000 + 0x1e8c; // bank 1 (0x2000) + SONG_NAME_OFFSET — the working-song name
const CUR_ENTRY_OFF = 0x2000 + 0x1e94; // bank 1 + SAVE_CURRENT_ENTRY_OFFSET: the working-song slot link

test("Load-to-working: the produced battery cold-boots risa onto the loaded song", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-load: no ROM at ${RISA_ROM}`); return; }

  const battery = savBytes("v2_blumarbl"); // current v2, one song: BLUMARBL
  expect(be.writeFile(SAV_PATH, battery)).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = (project.systems.loadRom(RISA_ROM, { explicitSav: SAV_PATH }) as { system: number }).system;
  audio.renderAudio(1500); // boot + settle

  // Load song 0 (BLUMARBL) into the working region on the LIVE battery readSram returns.
  const live = be.readSram(id)!;
  expect(listSongs(live).some((s) => s.name === "BLUMARBL")).toBeTruthy();
  const loaded = loadSongToWorkingInSav(live, 0);
  expect(loaded).toBeTruthy();

  // The splice matches the pure codec on the real live bytes, and the catalog is preserved - except at the
  // 'current entry' link byte, which Load stamps with the slot it came from (a bare record expansion leaves
  // it 0xFF). Compare around it, then assert it explicitly.
  const expectedWorking = expandRecordToWorking(recordBytesAt(normalizeSaveContainer(live).save, CURRENT_LAYOUT, 0)!);
  expect(loaded!.slice(0, 0x8000).every((b, i) => (i === CUR_ENTRY_OFF ? b === 0 : b === expectedWorking[i]))).toBeTruthy();

  // Cold-boot from it via the menu's seam (writeFileAtomic + loadSram), then settle. loadSram reconstructs
  // the core under a new id (rebuildInPlace), so read the battery back from that id.
  expect(be.writeFileAtomic(LOADED_PATH, loaded!)).toBeTruthy();
  const id2 = project.systems.loadSram(id, LOADED_PATH);
  expect(id2).toBeTruthy();
  audio.renderAudio(1500);

  // The firmware accepted the battery and kept the working song: its name reads back as BLUMARBL, and the
  // catalog (banks 4-7) still lists it.
  const back = be.readSram(id2!)!;
  expect(back.length).toBe(0x10000);
  const workingName = decodeSongName(back.slice(NAME_OFF, NAME_OFF + 8));
  console.log(`[risa-load] working-song name after cold boot: "${workingName}"`);
  expect(workingName).toBe("BLUMARBL");
  expect(listSongs(back).some((s) => s.name === "BLUMARBL")).toBeTruthy();

  // And the firmware kept the link Load stamped: it reads this byte into lds_current_entry on boot, marks
  // the song with '>' in its FILE list, and pre-selects that slot on SAVE. Leaving it 0xFF is what made the
  // cart append a duplicate of the song the user had just loaded, with the host never involved.
  console.log(`[risa-load] working→slot link after cold boot: ${back[CUR_ENTRY_OFF]}`);
  expect(back[CUR_ENTRY_OFF]).toBe(0);
});
