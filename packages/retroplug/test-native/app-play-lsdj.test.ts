// The composition root, proven for LSDj sync: a real project assembled from the app-layer STORES
// boots LSDj from an authored SYNC=MIDI sav (the store's `adopt` load seam), and the store's
// `lsdj-sync` role — attached by the ROM provider, toggled via setRoleConfig — is the SOLE clock.
// This is dsp-lsdj-midisync.test.ts's proof, but driven THROUGH the stores: setRoleConfig(mode)
// on a feature role re-projects the structure (syncDspFromStore) and pushes it, so flipping the
// store's role config is what makes an armed LSDj sing. Whole-mix RMS on a single system = that
// system's audio. (One test per file: whole-mix RMS needs an isolated native Project.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { savFromJson } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// SYNC=MIDI + chain0 -> phrase0 -> a C note on a hard-panned pulse (the proven flagship cell set).
const SYNC_MIDI_SONG = JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Midi" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
});

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the store's lsdj-sync role is the sole clock that makes an armed LSDj sing", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP app-play-lsdj: LSDj ROM not found at ${LSDJ}`);
    return; // no resources on disk (e.g. resource-less CI) — the devcontainer has it
  }

  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();

  // Boot LSDj from the authored SYNC=MIDI sav via the store's load seam. `adopt` reads the real
  // header, so the ROM provider attaches lsdj-sync (mode 1 default). `adopt` is quiet, so push the
  // initial structure explicitly; then install the hook so later setRoleConfig edits re-push.
  const sav = savFromJson(SYNC_MIDI_SONG);
  const id = project.systems.adopt({ romPath: LSDJ }, { sramBytes: sav })!;
  expect(typeof id).toBe("number");
  expect(project.systems.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeTruthy();
  syncDspFromStore(project, dsp);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  audio.renderAudio(6000); // reach the SONG screen from the sav (transport off → no clock leaks)

  // Arm: a START tap parks SYNC=MIDI LSDj in "wait for MIDI clock".
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(300);

  audio.setBpm(120);
  audio.setTransport(true);

  // Negative: lsdj-sync Off (via the store) → no clock → armed LSDj frozen → silent. No render
  // between setTransport(true) and this push, or the mode:1-default pipeline would clock + poison neg.
  expect(project.systems.setRoleConfig(id, "lsdj-sync", { mode: 0 })).toBeTruthy();
  const neg = rms(audio.renderAudio(600));

  // Positive: lsdj-sync MidiSync (via the store) → the kernel's 24-PPQN 0xF8 clock is the only clock.
  expect(project.systems.setRoleConfig(id, "lsdj-sync", { mode: 1 })).toBeTruthy();
  const pos = rms(audio.renderAudio(3000));

  console.log(`[app-play-lsdj] neg=${neg.toFixed(5)} pos=${pos.toFixed(5)}`);
  expect(neg < 0.001).toBeTruthy(); // armed but unclocked → silent
  expect(pos > 0.001).toBeTruthy(); // the store's role clocks LSDj → audible
  expect(pos > neg).toBeTruthy();
});
