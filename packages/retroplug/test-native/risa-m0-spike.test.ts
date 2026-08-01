// M0 de-risking spike for risa (NES/MMC5 tracker) support.
// Answers the three blocking unknowns from docs/risa-integration-plan.md against a REAL Mesen core:
//   M0b  Mesen accepts risa's iNES header (mapper 5 / MMC5) and boots it.
//   M0c  the NES per-block readRam seam returns the 2KB internal RAM, and readSram returns the
//        FULL 64KB MMC5 battery WRAM (not just the 8KB $6000 window) — this sizes M1/M2.
//   M0d  MMC5 audio actually renders: a loaded demo song is silent idle, audible after START.
//
// The risa ROM + demo saves are NOT in this repo; build risa (`make` in the risa source tree) then
// point the paths below at it. The test SKIPs cleanly when they're absent so CI stays green.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const DEMO_SRM = "/workspaces/risa-v2.2.1-source/website/play/demos/hevander.srm";
const BTN_START = 7; // GB button order (Right=0 … Start=7), reused for NES

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("M0: risa (NES/MMC5) boots in Mesen, exposes 64KB battery + 2KB RAM, and plays", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-m0: no ROM at ${RISA_ROM}`); return; }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  // ---- M0b: load + boot. If Mesen rejected MMC5, the construct fails and loadRom returns no system.
  const hasSav = be.fileExists(DEMO_SRM);
  const loaded = project.systems.loadRom(RISA_ROM, hasSav ? { explicitSav: DEMO_SRM } : undefined);
  const id = (loaded as { system: number }).system;
  expect(typeof id).toBe("number");
  expect(project.systems.view()[0].platform).toBe("nes");
  console.log(`[risa-m0] booted id=${id} sav=${hasSav ? DEMO_SRM : "(none)"}`);

  audio.renderAudio(1500); // boot + let the ROM's init settle

  // ---- M0c: the memory windows the whole integration rides on.
  const ram = be.readRam(id);
  const sram = be.readSram(id);
  console.log(`[risa-m0] readRam len=${ram?.length ?? "null"}  readSram len=${sram?.length ?? "null"}`);
  expect(ram != null).toBeTruthy();
  expect(ram!.length).toBe(2048); // NES internal RAM $0000-$07FF — where all risa playhead/tempo state lives

  expect(sram != null).toBeTruthy();
  // The single highest-leverage unknown: does GetMemory(NesSaveRam) return the full 64KB MMC5 battery?
  console.log(`[risa-m0] sram==64KB? ${sram!.length === 0x10000}  (0x${sram!.length.toString(16)})`);
  expect(sram!.length).toBe(0x10000);

  // The RSAV catalog lives in banks 4-7 (offset 0x8000). A demo battery should carry it.
  if (sram!.length >= 0x8004) {
    const magic = String.fromCharCode(sram![0x8000], sram![0x8001], sram![0x8002], sram![0x8003]);
    console.log(`[risa-m0] sram@0x8000 = ${JSON.stringify(magic)} (expect "RSAV")`);
  }
  // Is a working song present in live banks 0-3 (so START should produce sound)?
  let liveNonZero = 0;
  for (let i = 0; i < 0x8000; i++) if (sram![i] !== 0 && sram![i] !== 0xff) liveNonZero++;
  console.log(`[risa-m0] live banks 0-3 non-trivial bytes = ${liveNonZero}`);

  // ---- M0d: MMC5 audio. START toggles play (docs: "START — Play or stop from the current context").
  const idle = rms(audio.renderAudio(500));
  audio.pressButton(id, BTN_START, true);
  audio.renderAudio(60);
  audio.pressButton(id, BTN_START, false);
  const playing = rms(audio.renderAudio(2500));
  console.log(`[risa-m0] idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(playing > 0.001).toBeTruthy();
  expect(playing > idle).toBeTruthy();
});
