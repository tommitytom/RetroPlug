// renderTimeline drives a REAL booted session: a Timeline note scheduled at 300ms plays through the
// render (the persistent engine consumes the staged MIDI on the chunk after the event), so the returned
// PCM is audible. Proves the CLI event-scripting path end-to-end against a real Mesen core. (The pure
// build() ordering/bytes are covered in the mock test/cli/timeline.test.ts.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

function rms(pcm: Float32Array): number {
  let s = 0;
  for (let i = 0; i < pcm.length; i++) s += pcm[i] * pcm[i];
  return pcm.length ? Math.sqrt(s / pcm.length) : 0;
}

test("a Timeline note plays through renderTimeline → audible PCM", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // A ch1 note at 300ms held 400ms; render 900ms total, after a 1000ms warm-up (n8-midi ignores MIDI
  // until booted). Any signal here proves the scheduled note reached the core at its time.
  const tl = new Timeline().note(300, 60, { durationMs: 400, channel: 1, velocity: 100 });
  const pcm = renderTimeline(s, tl, { durationMs: 900, warmupMs: 1000 });

  expect(pcm.length > 0).toBeTruthy();
  expect(rms(pcm) > 0.001).toBeTruthy();
});
