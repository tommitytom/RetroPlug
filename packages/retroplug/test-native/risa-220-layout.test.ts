// risa 2.2.0 shares 2.2.1's internal-RAM layout (VERSION_ALIASES in runtime/layout.ts): cc65 didn't move
// the BSS/ZP variables the reader tracks between those builds. This proves the alias against a REAL 2.2.0
// core: resolveRisaLayout("2.2.0") yields the 2.2.1 addresses, and decoding a live 2.2.0 core with them is
// coherent across the stopped->playing transition — seq_mode, tempo, screen, and per-track positions all
// decode and advance. If a future risa build DID move these, this fails and the alias must be revisited.
// (This is what makes song-length auto-detect work for 2.2.0 carts — see render.ts buildPlayingProbe.)
// SKIPs when the 2.2.0 ROM / LETGO sav aren't present, like the other risa native tests.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { runtime } from "../src/risa";

declare const __DSP_KERNEL_BUNDLE__: string;

const ROM_220 = "/workspaces/resources/roms/risa/risa-v2.2.0/risa-2.2.0-pal.nes";
const LETGO = "/workspaces/resources/roms/risa/let_go.srm";
const BTN_SELECT = 6;
const BTN_START = 7;

test("risa 2.2.0 aliases the 2.2.1 layout — a live 2.2.0 core decodes coherently", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_220) || !be.fileExists(LETGO)) {
    console.log(`# SKIP risa-220-layout: missing ${ROM_220} or ${LETGO}`);
    return;
  }

  // The alias resolves, keeps the ROM's real version label, and borrows 2.2.1's addresses.
  const layout = runtime.resolveRisaLayout("2.2.0");
  expect(layout != null).toBe(true);
  expect(layout!.version).toBe("2.2.0");
  expect(layout!.seqMode).toBe(runtime.resolveRisaLayout("2.2.1")!.seqMode);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  const id = (project.systems.loadRom(ROM_220, { explicitSav: LETGO }) as { system: number }).system;
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
  console.log(`[risa-220-layout] play: playing=${play.playing} mode=${play.mode} bpm=${play.bpm} screen=${play.screen} active=[${play.tracks.map(t=>t.active?1:0)}]`);

  // A coherent decode (not garbage) is what proves the borrowed addresses fit the 2.2.0 build.
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
