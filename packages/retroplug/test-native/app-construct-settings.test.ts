// Chunk B: a system adopted with a saved "sameboy" role config builds its node's core with that config.
// fastBoot is the sharp proof: applied live it's DEFERRED to the next restart (setFastBoot only
// changes the next boot), so an audible GB boot chime at THIS boot can only come from the role config being
// part of the build its node makes. The control is app-play-mgb's fastBoot=true start (idle ~0.00003
// — the boot screen skipped, silent). Here fastBoot=false runs the boot ROM, which chimes.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a system adopted with fastBoot=false plays the GB boot chime (construct-time role config)", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const audio = createAudioDriver();

  // Adopt the embedded mGB with an explicit sameboy role whose fastBoot is FALSE, so construct runs
  // the boot ROM instead of skipping it. (adopt is the load seam; it forwards the sameboy role config
  // to the system node's knobs, which is what its build reads.)
  const id = project.systems.adopt({
    embeddedRom: "mgb",
    roles: [
      { kind: "sameboy", config: { model: "cgbC", highpass: "accurate", linkGroupId: 0, fastBoot: false } },
      { kind: "mgb", config: {} },
    ],
  })!;
  expect(typeof id).toBe("number");

  // No MIDI staged: the ONLY thing that can make noise this early is the GB boot chime, which plays
  // only because fastBoot=false was honoured at construct (a live setFastBoot would be deferred).
  const boot = rms(audio.renderAudio(3000));
  console.log(`[app-construct-settings] boot(fastBoot=false)=${boot.toFixed(5)}`);
  expect(boot > 0.001).toBeTruthy(); // the boot chime sounded → the construct-time blob took effect
  project.systems.removeSystem(id);
});

test("a label native cannot read is left out of the build, not defaulted over the fields beside it", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const project = new ProjectStore(be, new RecentStore(be), registry);
  const audio = createAudioDriver();

  // A newer build's palette label (additive, so no version bump) beside a construct-time field the core does read.
  // The document writes only the fields the node takes, so the unknown label is left out and the readable fastBoot
  // still reaches the build: both configs below are the node's, not one defaulted whole. The true case is the control:
  // it must stay silent.
  const bootFor = (fastBoot: boolean): number => {
    const id = project.systems.adopt({
      embeddedRom: "mgb",
      roles: [
        { kind: "sameboy", config: { fastBoot, dmgPalette: "aPaletteFromANewerBuild" } },
        { kind: "mgb", config: {} },
      ],
    })!;
    expect(typeof id).toBe("number");
    const boot = rms(audio.renderAudio(3000));
    project.systems.removeSystem(id);
    return boot;
  };
  const silent = bootFor(true);
  const chime = bootFor(false);
  console.log(
    `[app-construct-settings] unknown palette: fastBoot true=${silent.toFixed(5)}, ` +
      `false=${chime.toFixed(5)}`,
  );
  expect(chime > silent * 2).toBeTruthy(); // the readable fastBoot still reached the core
});
