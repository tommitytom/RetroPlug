// Chunk B: the construct-time settings blob applies a system's saved "sameboy" role config AT build.
// fastBoot is the sharp proof: applied live it's DEFERRED to the next restart (setFastBoot only
// changes the next boot), so an audible GB boot chime at THIS boot can only come from the blob being
// decoded in SameBoyBackend::build. The control is app-play-mgb's fastBoot=true start (idle ~0.00003
// — the boot screen skipped, silent). Here fastBoot=false runs the boot ROM, which chimes.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a system adopted with fastBoot=false plays the GB boot chime (construct-time settings blob)", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  // Adopt the embedded mGB with an explicit sameboy role whose fastBoot is FALSE, so construct runs
  // the boot ROM instead of skipping it. (adopt is the load seam; it forwards the sameboy role config
  // as the construct-time settings blob.)
  const id = project.systems.adopt({
    embeddedRom: "mgb",
    roles: [
      { kind: "sameboy", config: { model: "cgbC", highpass: "accurate", linkGroupId: 0, fastBoot: false } },
      { kind: "mgb", config: {} },
    ],
  })!;
  expect(typeof id).toBe("number");
  syncDspFromStore(project, dsp); // adopt is quiet

  // No MIDI staged: the ONLY thing that can make noise this early is the GB boot chime, which plays
  // only because fastBoot=false was honoured at construct (a live setFastBoot would be deferred).
  const boot = rms(audio.renderAudio(3000));
  console.log(`[app-construct-settings] boot(fastBoot=false)=${boot.toFixed(5)}`);
  expect(boot > 0.001).toBeTruthy(); // the boot chime sounded → the construct-time blob took effect
});
