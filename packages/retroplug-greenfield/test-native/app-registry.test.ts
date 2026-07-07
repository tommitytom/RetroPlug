// The snapshot registry is the control plane's one read door: getFrame/readState/readSram read an
// OWNED published copy by id, never the live core. Two things this proves that the old path couldn't:
//   1. reads work WHILE the audio thread runs — readState/readSram were audioRunning_-guarded (dead
//      the moment audio started, so project save/export mid-playback returned null); getFrame walked
//      the DSP-owned system list. All three now read the registry, unguarded.
//   2. the registry is SEEDED at construct, so a read right after construct works with no block
//      rendered, and a removed core's slot is released (its reads go null).
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

test("frame + state read through the registry while the background audio thread runs", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Ownership discipline (per dsp-threaded): construct + load the kernel BEFORE startAudio.
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  // Seeded at construct: state reads even before a block is rendered.
  expect(be.readState(id)!.length > 0).toBeTruthy();

  audio.renderAudio(1500); // GB boot + mGB init — publishes a frame + a state snapshot

  // Quiescent baseline: a rendered frame + a savestate.
  const cold = be.getFrame(id);
  expect(cold != null && cold.published).toBeTruthy();
  expect(be.readState(id)!.length > 0).toBeTruthy();

  // The real background audio thread: active_ is the QueuedInvoker, audioRunning_ is set — exactly
  // the state where readState used to return null and getFrame walked the live Project.
  expect(audio.startAudio()).toBeTruthy();
  audio.sleepMs(80); // let the audio thread publish into the registry

  const hotFrame = be.getFrame(id);
  expect(hotFrame != null && hotFrame.published).toBeTruthy(); // frame keeps flowing during the run
  expect(be.readState(id)!.length > 0).toBeTruthy();           // <- was null under the old guard

  expect(audio.stopAudio()).toBeTruthy();
  audio.drainReleased();
});

test("battery SRAM slices from the seeded snapshot; a removed core's reads go null", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/reg.gb";
  be.writeFile(rom, gbRomBattery());

  const store = new SystemsStore(be);
  const a = store.addSystem(rom)!;
  expect(typeof a).toBe("number");

  // Seeded at construct (no block rendered): a battery cart's SRAM slices out of the savestate.
  const sram = be.readSram(a);
  expect(sram != null && sram.length > 0).toBeTruthy();
  expect(be.readState(a)!.length > 0).toBeTruthy();

  const b = store.duplicateSystem(a)!;
  expect(be.readState(b)!.length > 0).toBeTruthy(); // the clone got its own seeded slot

  expect(store.removeSystem(b)).toBeTruthy();
  expect(be.readState(b)).toBe(null); // slot released → the read misses
  expect(be.readSram(b)).toBe(null);
  expect(be.readState(a)!.length > 0).toBeTruthy(); // the survivor is untouched
});
