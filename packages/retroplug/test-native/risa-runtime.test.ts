// M4 native proof: the risa runtime reader against a REAL, playing Mesen core. Boot risa with a demo
// song, read the live internal-RAM snapshot through the per-block readRam seam, and decode it — asserting
// the reader tracks the stopped→playing transition and reports sensible live tempo/position/screen. This
// proves the generated symbol addresses match the real ROM (a synthetic-RAM unit test can't). SKIPs when
// the built ROM is absent, like risa-m0-spike.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { runtime } from "../src/risa";

declare const __DSP_KERNEL_BUNDLE__: string;

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const DEMO_SRM = "/workspaces/risa-v2.2.1-source/website/play/demos/hevander.srm"; // carries a working song
const BTN_SELECT = 6;
const BTN_START = 7; // GB button order, reused for NES (NesButton::Start = 7). SELECT+START = play song.

test("the risa runtime reader decodes live playback state from the real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM) || !be.fileExists(DEMO_SRM)) {
    console.log(`# SKIP risa-runtime: missing ${RISA_ROM} or ${DEMO_SRM}`);
    return;
  }

  const layout = runtime.resolveRisaLayout("2.2.1");
  expect(layout != null).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = (project.systems.loadRom(RISA_ROM, { explicitSav: DEMO_SRM }) as { system: number }).system;
  audio.renderAudio(1500); // boot + settle

  // Before START: stopped. The reader sees a valid screen + resolved layout even when idle.
  const idle = runtime.decodeRisaState(be.readRam(id)!, layout);
  console.log(`[risa-runtime] idle: supported=${idle.supported} playing=${idle.playing} mode=${idle.mode} screen=${idle.screen}`);
  expect(idle.supported).toBeTruthy();
  expect(idle.playing).toBeFalsy();
  expect(idle.mode).toBe("stopped");

  // Start SONG playback of the restored working song. From the boot phrase screen a plain START first
  // nudges risa off the empty phrase context; SELECT+START then starts song playback from the current
  // row (all 5 tracks). Sample the live state across a window and capture a frame where it's running.
  audio.pressButton(id, BTN_START, true);
  audio.renderAudio(50);
  audio.pressButton(id, BTN_START, false);
  audio.renderAudio(1000);
  audio.pressButton(id, BTN_SELECT, true);
  audio.pressButton(id, BTN_START, true);
  audio.renderAudio(50);
  audio.pressButton(id, BTN_START, false);
  audio.pressButton(id, BTN_SELECT, false);

  let playing = idle;
  for (let i = 0; i < 40 && !playing.playing; i++) {
    audio.renderAudio(50);
    playing = runtime.decodeRisaState(be.readRam(id)!, layout);
  }
  console.log(
    `[risa-runtime] playing: playing=${playing.playing} mode=${playing.mode} bpm=${playing.bpm} 4x=${playing.fourX} screen=${playing.screen} ` +
      `active=[${playing.tracks.map((t) => (t.active ? 1 : 0)).join(",")}] songRows=[${playing.tracks.map((t) => t.songRow).join(",")}] ` +
      `phraseRows=[${playing.tracks.map((t) => t.phraseRow).join(",")}]`,
  );

  expect(playing.playing).toBeTruthy(); // SELECT+START moved seq_mode off stopped → the reader tracks it
  expect(playing.mode !== "stopped" && playing.mode !== "unknown").toBeTruthy();
  expect(playing.tracks.some((t) => t.active)).toBeTruthy(); // a real song drives ≥1 APU channel
  expect(playing.bpm !== null || playing.fourX).toBeTruthy(); // a sensible tempo (40..295) or the 4x mode
  expect(playing.screen !== "unknown").toBeTruthy();

  // Advancing playback moves at least one live phrase row while it keeps running — proves the reader
  // tracks per-frame state, not a static snapshot. (Sample until it moves or the run ends.)
  const startRows = playing.tracks.map((t) => t.phraseRow);
  let moved = false;
  for (let i = 0; i < 20 && !moved; i++) {
    audio.renderAudio(40);
    const now = runtime.decodeRisaState(be.readRam(id)!, layout);
    if (!now.playing) break; // playback ended — the burst was captured above, that's enough
    moved = now.tracks.some((t, k) => t.phraseRow !== startRows[k]);
  }
  console.log(`[risa-runtime] phraseRows advanced from [${startRows.join(",")}]: moved=${moved}`);
  expect(moved).toBeTruthy();
});
