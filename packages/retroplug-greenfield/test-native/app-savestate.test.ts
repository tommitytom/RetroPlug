// Per-system Save/Load State + SRAM end-to-end against a REAL emulator + real files. The mock store
// tests (test/systems/store-mutate) prove the orchestration; this proves the real bytes flow:
// saveState dumps the registry-published savestate to disk (safe WHILE the audio thread runs — the
// whole point of dropping the audioRunning_ guard, see app-registry), loadState reads it back and
// reconstructs the core in place via a stateBytes construct. SRAM slices out of a battery cart's
// savestate the same way.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { SystemsStore } from "../src/systemsStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { gbRomBattery } from "../test/systems/fixtures";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __CONFIG_DIR__: string;

test("saveState → disk → loadState round-trips a real core, saving while the audio thread runs", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Ownership discipline (per dsp-threaded): construct + load the kernel BEFORE startAudio.
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");
  audio.renderAudio(1500); // boot + publish a state snapshot into the registry

  const statePath = __CONFIG_DIR__ + "/mgb.ss0";

  // Save WHILE the background audio thread owns the Engine — the registry read is guard-free (was null
  // under the old audioRunning_ guard, so a mid-playback save wrote nothing).
  expect(audio.startAudio()).toBeTruthy();
  audio.sleepMs(80);
  expect(project.systems.saveState(id, statePath)).toBeTruthy();
  expect(audio.stopAudio()).toBeTruthy();
  audio.drainReleased();

  // Real savestate bytes reached disk.
  const onDisk = be.readFile(statePath);
  expect(onDisk != null && onDisk.length > 0).toBeTruthy();

  // Load it back: reconstruct in place (new id, same single slot), the core boots from the file's bytes.
  const newId = project.systems.loadState(id, statePath);
  expect(newId != null && newId !== id).toBeTruthy();
  expect(project.systems.view().length).toBe(1);
  expect(project.systems.view()[0].id).toBe(newId);
  expect(be.readState(newId as number)!.length > 0).toBeTruthy(); // the rebuilt core has a live snapshot
});

test("saveSram writes a battery cart's SRAM and loadSram rebuilds from it", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/ss.gb";
  be.writeFile(rom, gbRomBattery());

  const store = new SystemsStore(be);
  const a = store.addSystem(rom)!;
  expect(typeof a).toBe("number");

  const sramPath = __CONFIG_DIR__ + "/ss.sav";
  expect(store.saveSram(a, sramPath)).toBeTruthy();
  const onDisk = be.readFile(sramPath);
  expect(onDisk != null && onDisk.length > 0).toBeTruthy(); // real SRAM sliced from the savestate

  const newId = store.loadSram(a, sramPath);
  expect(newId != null && newId !== a).toBeTruthy(); // in-place rebuild with the file's SRAM
  expect(be.readSram(newId as number)!.length > 0).toBeTruthy();
});

test("New SRAM zeros the battery and Reset carries the live battery forward", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/ss.gb";
  be.writeFile(rom, gbRomBattery());

  const store = new SystemsStore(be);
  const a = store.addSystem(rom)!;
  expect(typeof a).toBe("number");

  // A fresh battery cart powers up with SameBoy's 0xFF fill — so an all-zero result below is a real change,
  // not a coincidence.
  const fresh = be.readSram(a)!;
  expect(fresh.length > 0 && fresh.some((b) => b !== 0)).toBeTruthy();

  // New SRAM cold-boots with an all-zero seed; native sizes it to the cart's battery and the live core
  // reads blank SRAM. Only a real onActivate zero-fill can prove this — the mock can't.
  const blanked = store.newSram(a)!;
  expect(blanked !== a).toBeTruthy();
  expect(be.readSram(blanked)!.every((b) => b === 0)).toBeTruthy();

  // Reset carries the *live* (now-blanked) battery forward — not a disk re-read (no .sav was written) and
  // not a fresh 0xFF fill. If reset dropped the live battery, this would read 0xFF and fail.
  const rst = store.reset(blanked)!;
  expect(rst !== blanked).toBeTruthy();
  expect(be.readSram(rst)!.every((b) => b === 0)).toBeTruthy();
});
