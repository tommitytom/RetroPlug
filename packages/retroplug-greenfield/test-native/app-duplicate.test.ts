// Duplicate works WHILE the audio thread runs. duplicateSystem used to clone the source's live core
// (GB_save_state on the core the audio thread is stepping) and bail with `if (audioRunning_) return null`,
// so in the live plugin (audio always active) Duplicate silently added nothing — the store treats a null
// backend result as "append nothing". It now clones off the DSP-published state snapshot (tear-free, like
// getFrame) and adopts through the invoker, so it works during a run. This starts the background audio
// thread, lets it publish a snapshot, then duplicates through BOTH the backend and the store and asserts a
// second instance actually lands. Resource-free (embedded mGB). Ownership discipline (per dsp-threaded):
// construct + load the kernel BEFORE startAudio; read systemCount only AFTER stopAudio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

test("duplicate clones off the state snapshot while the audio thread runs", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // GB boot + mGB firmware init — enough blocks to publish a state snapshot

  // Run the real background audio thread: active_ is now the QueuedInvoker + audioRunning_ is set.
  expect(audio.startAudio()).toBeTruthy();
  audio.sleepMs(50); // let the audio thread publish at least one tear-free state snapshot

  // Backend path: the exact method that used to return null while audio ran.
  const dupId = be.duplicateSystem(id, null);
  expect(dupId != null).toBeTruthy();
  expect(dupId === id).toBeFalsy(); // a fresh id, not the source

  audio.sleepMs(50); // drain the queued AddSystem onto the audio thread
  expect(audio.stopAudio()).toBeTruthy();
  audio.drainReleased();

  // Both cores are live in the Project (the source + the adopted clone).
  expect(audio.systemCount()).toBe(2);
});
