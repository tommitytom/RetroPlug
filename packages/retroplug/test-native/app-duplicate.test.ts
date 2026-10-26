// Duplicate works WHILE the audio thread runs — now as TS orchestration. The store pulls the source's
// savestate from the registry (readState — a tear-free control-thread read, safe while the audio thread
// steps the core) and builds an INDEPENDENT clone via buildCore-with-state, whose queued plan swap
// lands on the audio thread. (The old native duplicateSystem cloned the live core + bailed with
// `if (audioRunning_) return null`; there is no native duplicate method anymore.) This starts the
// background audio thread, lets it publish a snapshot, duplicates through the store, and asserts a second
// instance actually lands. Resource-free (embedded mGB). Ownership discipline (per dsp-threaded):
// construct BEFORE startAudio; read systemCount only AFTER stopAudio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

test("duplicate (TS orchestration) clones off the state snapshot while the audio thread runs", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // GB boot + mGB init — enough blocks to publish a state snapshot

  // Run the real background audio thread: mutations enqueue for it + audioRunning_ is set.
  expect(audio.startAudio()).toBeTruthy();
  audio.sleepMs(50); // let the audio thread publish at least one tear-free state snapshot

  // The store reads readState(id) (safe while running) + buildCore-with-state; the plan swap is queued.
  const dupId = project.systems.duplicateSystem(id);
  expect(dupId != null).toBeTruthy();
  expect(dupId === id).toBeFalsy(); // a fresh id, not the source

  audio.sleepMs(50); // drain the queued AddSystem onto the audio thread
  expect(audio.stopAudio()).toBeTruthy();
  audio.drainReleased();

  // Both cores are live in the Project (the source + the adopted clone).
  expect(audio.systemCount()).toBe(2);
});
