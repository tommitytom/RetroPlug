// Duplicate works WHILE the audio thread runs — now as TS orchestration. The store pulls the source's
// savestate from the registry (readState — a tear-free control-thread read, safe while the audio thread
// steps the core) and builds an INDEPENDENT clone via constructSystem-with-state, whose queued adopt
// lands on the audio thread. (The old native duplicateSystem cloned the live core + bailed with
// `if (audioRunning_) return null`; there is no native duplicate method anymore.) This starts the
// background audio thread, lets it publish a snapshot, duplicates through the store, and asserts a second
// instance actually lands. Resource-free (embedded mGB). Ownership discipline (per dsp-threaded):
// construct + load the kernel BEFORE startAudio; read systemCount only AFTER stopAudio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

test("duplicate (TS orchestration) clones off the state snapshot while the audio thread runs", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // Load the role kernel + install the store→DSP hook BEFORE startAudio.
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // GB boot + mGB init — enough blocks to publish a state snapshot

  // Run the real background audio thread: mutations enqueue for it + audioRunning_ is set.
  expect(audio.startAudio()).toBeTruthy();
  audio.sleepMs(50); // let the audio thread publish at least one tear-free state snapshot

  // The store reads readState(id) (safe while running) + constructSystem-with-state; the adopt is queued.
  const dupId = project.systems.duplicateSystem(id);
  expect(dupId != null).toBeTruthy();
  expect(dupId === id).toBeFalsy(); // a fresh id, not the source

  audio.sleepMs(50); // drain the queued AddSystem onto the audio thread
  expect(audio.stopAudio()).toBeTruthy();
  audio.drainReleased();

  // Both cores are live in the Project (the source + the adopted clone).
  expect(audio.systemCount()).toBe(2);
});
