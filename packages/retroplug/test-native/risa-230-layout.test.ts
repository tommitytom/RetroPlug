// Certifies the risa 2.3.0 internal-RAM layout against the SHIPPED 2.3.0 ROM. The symbol snapshot in
// runtime/symbols.generated.ts comes from a LOCAL `make all` of the risa source, and that build is not
// byte-identical to the developer's released binary (a different cc65 build reshuffles codegen), so the
// label file alone does not prove the addresses fit the ROM users actually run. This does: it boots the
// released ROM and asserts the decode is coherent across the stopped -> playing transition, with
// positions advancing. Wrong addresses decode as garbage and fail here.
//
// 2.3.0 shares nothing with 2.2.1 - cc65 moved every variable the reader tracks - so it needs its own
// snapshot rather than a VERSION_ALIASES entry (contrast risa-220-layout, which proves an alias).
// SKIPs when the 2.3.0 ROM / LETGO sav aren't present, like the other risa native tests.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { runtime } from "../src/risa";

declare const __DSP_KERNEL_BUNDLE__: string;

const ROM_230 = "/workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes";
const LETGO = "/workspaces/resources/roms/risa/let_go.srm";
const BTN_SELECT = 6;
const BTN_START = 7;

test("risa 2.3.0 has its own layout - the released ROM decodes coherently", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230) || !be.fileExists(LETGO)) {
    console.log(`# SKIP risa-230-layout: missing ${ROM_230} or ${LETGO}`);
    return;
  }

  // The ROM self-identifies as 2.3.0 and resolves its own snapshot (not an alias to an older one).
  const version = runtime.identifyRisaVersion(be.readFile(ROM_230)!);
  expect(version).toBe("2.3.0");
  const layout = runtime.resolveRisaLayout(version);
  expect(layout != null).toBe(true);
  expect(layout!.version).toBe("2.3.0");
  expect(layout!.seqMode !== runtime.resolveRisaLayout("2.2.1")!.seqMode).toBe(true);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  const id = (project.systems.loadRom(ROM_230, { explicitSav: LETGO }) as { system: number }).system;
  audio.renderAudio(1500);

  // Idle: the reader resolves + reports a stopped sequencer (seq_mode byte reads 0).
  const idle = runtime.decodeRisaState(be.readRam(id)!, layout!);
  expect(idle.supported && !idle.playing && idle.mode === "stopped").toBe(true);

  // Play LETGO (a plain START nudges off the empty phrase context, then SELECT+START starts the song).
  audio.pressButton(id, BTN_START, true); audio.renderAudio(50); audio.pressButton(id, BTN_START, false);
  audio.renderAudio(1000);
  audio.pressButton(id, BTN_SELECT, true); audio.pressButton(id, BTN_START, true); audio.renderAudio(50);
  audio.pressButton(id, BTN_START, false); audio.pressButton(id, BTN_SELECT, false);

  let play = idle;
  for (let i = 0; i < 40 && !play.playing; i++) { audio.renderAudio(50); play = runtime.decodeRisaState(be.readRam(id)!, layout!); }
  console.log(`[risa-230-layout] play: playing=${play.playing} mode=${play.mode} bpm=${play.bpm} screen=${play.screen} active=[${play.tracks.map(t=>t.active?1:0)}]`);

  // A coherent decode (not garbage) is what proves the generated addresses fit the released build.
  expect(play.playing && play.mode === "song").toBe(true);
  expect(play.bpm !== null && play.screen !== "unknown").toBe(true);
  expect(play.tracks.some(t => t.active)).toBe(true);

  // Positions advance while it plays → the position symbols align too, not just seq_mode.
  const startRows = play.tracks.map(t => t.phraseRow);
  let moved = false;
  for (let i = 0; i < 30 && !moved; i++) {
    audio.renderAudio(40);
    const now = runtime.decodeRisaState(be.readRam(id)!, layout!);
    if (!now.playing) break;
    moved = now.tracks.some((t, k) => t.phraseRow !== startRows[k]);
  }
  expect(moved).toBe(true);
});
