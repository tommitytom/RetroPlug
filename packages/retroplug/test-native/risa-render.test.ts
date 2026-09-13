// The `render` tool's song-selection flags extended to risa (NES) savs: --list-songs lists the RSAV catalog,
// --song-index promotes a saved song to the working banks before the render, and the risa play gesture
// (SELECT+START) makes it audible. Drives the shared render library over a real backend + DSP runtime
// (mirrors test-native/app-play-nes-channels.test.ts). Gated on the built risa ROM — SKIPs when absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { runRenderJob, readRisaSongs, decodeWav, type RenderContext, type RenderOpts } from "../src/render";
import { dirname, joinPath } from "../src/pathUtil";
import { savBytes } from "../test/risa/fixtures";

declare const __DSP_KERNEL_BUNDLE__: string;

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const SAV = "/tmp/rp-risa-render.srm";
// A real risa song whose last track HFFs at the end (~59 s) — the only demo sav here that stops rather than
// loops, so it's the fixture that proves seq_mode → STOPPED end-detection over the real core.
const ECOLI_SRM = "/workspaces/resources/roms/risa/ecoli_soul.srm";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

function newCtx(be: ReturnType<typeof createRealBackend>): RenderContext {
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  return { backend: be, project, dsp, audio };
}

const baseOpts = (over: Partial<RenderOpts>): RenderOpts => ({
  rom: RISA_ROM, sav: SAV, maxDurationMs: 5000, split: "mix", transport: false, start: true, listSongs: false, ...over,
});

test("render --list-songs lists the risa catalog (readRisaSongs)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy(); // current v2 catalog, one song: BLUMARBL
  const { songs } = readRisaSongs(newCtx(be), baseOpts({}));
  expect(songs.length).toBe(1);
  expect(songs[0].index).toBe(0);
  expect(songs[0].name).toBe("BLUMARBL");
});

test("render --song-index promotes a risa catalog song to working + renders non-silent audio", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const out = "/tmp/rp-risa-render.wav";
  runRenderJob(newCtx(be), baseOpts({ songIndex: 0, durationMs: 2000, out }));

  const wav = decodeWav(be.readFile(out)!);
  const level = rms(wav.pcm);
  console.log(`[risa-render] --song-index 0 (BLUMARBL) → ${wav.pcm.length} samples @${wav.sampleRate}Hz, RMS ${level.toFixed(4)}`);
  expect(level > 0.001).toBe(true); // the promoted working song actually plays (SELECT+START gesture)
});

test("render without --out defaults the output filename to the song's name (not the ROM name)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  // No --out: outBase derives the name from the selected song (BLUMARBL), next to the ROM — not risa-pal.wav.
  const res = runRenderJob(newCtx(be), baseOpts({ songIndex: 0, durationMs: 300 }));
  const expected = joinPath(dirname(RISA_ROM), "BLUMARBL.wav");
  expect(res.outputs).toEqual([expected]);
  expect(be.fileExists(expected)).toBe(true);
  be.deleteFile(expected); // don't leave the derived WAV next to the source ROM
});

test("render onExists 'rename' writes the next free name instead of clobbering", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const first = joinPath(dirname(RISA_ROM), "BLUMARBL.wav");
  const second = joinPath(dirname(RISA_ROM), "BLUMARBL_2.wav");
  be.deleteFile(first); be.deleteFile(second); // clean slate

  runRenderJob(newCtx(be), baseOpts({ songIndex: 0, durationMs: 300 })); // 1st → BLUMARBL.wav (overwrite default)
  const r2 = runRenderJob(newCtx(be), baseOpts({ songIndex: 0, durationMs: 300, onExists: "rename" }));
  expect(r2.outputs).toEqual([second]); // target exists → renamed, not clobbered
  expect(be.fileExists(first)).toBe(true); // the first render is left intact
  expect(be.fileExists(second)).toBe(true);
  be.deleteFile(first); be.deleteFile(second);
});

test("render auto-detects risa song length via seq_mode (HFF stop) over the real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const out = "/tmp/rp-risa-autodetect.wav";
  // No durationMs → the risa auto-detect path: render to the seq_mode STOPPED edge, capped at maxDurationMs.
  // (A looping song caps out with hff:false; either way the auto-detect path reports a length + real audio.)
  const res = runRenderJob(newCtx(be), baseOpts({ songIndex: 0, maxDurationMs: 3000, out }));
  console.log(`[risa-render] auto-detect: hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames}`);
  expect(res.lengthMs !== undefined).toBe(true); // the auto-detect path engaged (a fixed render reports none)
  expect((res.frames ?? 0) > 0).toBe(true);
  const wav = decodeWav(be.readFile(out)!);
  expect(rms(wav.pcm) > 0.001).toBe(true); // real song audio, whether it HFF-stops or hits the cap
});

test("render auto-detects a real risa song's HFF stop (hff true) and trims to it over the real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM) || !be.fileExists(ECOLI_SRM)) {
    console.log(`# SKIP risa-render: missing ${RISA_ROM} or ${ECOLI_SRM}`);
    return;
  }
  const out = "/tmp/rp-risa-ecoli.wav";
  // ecoli_soul's working song HFFs its last track at the end → seq_mode STOPPED; auto-detect must end there,
  // not at the cap. The cap sits just past the song so a regression (never detecting the stop) is visible.
  const res = runRenderJob(newCtx(be), baseOpts({ sav: ECOLI_SRM, maxDurationMs: 65000, out }));
  console.log(`[risa-render] ecoli_soul auto-detect: hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames}`);
  expect(res.hff).toBe(true); // the song's HFF end drove seq_mode → STOPPED, detected over the real core
  expect(res.lengthMs! > 55000 && res.lengthMs! < 63000).toBe(true); // ~59 s, well under the 65 s cap
  const wav = decodeWav(be.readFile(out)!);
  expect(wav.pcm.length).toBe(res.frames); // the WAV is trimmed exactly to the detected stop (no silent tail)
  expect(rms(wav.pcm) > 0.001).toBe(true);
});

// --- the instance's configuration reaching the render -----------------------------------------------------
// A render boots a FRESH core from the ROM on disk, so whatever the live instance is set to has to be carried
// into the job (RenderOpts.roles / gainDb, filled by the UI's System > Render) or the offline core comes up at
// the role schema's DEFAULTS - rendering a PAL project at NTSC, a cart's expansion chip at unity whatever its
// Expansion Volume says, an instance's gain at 0 dB.
//
// Region is what proves it, and it has to be: this cart boots with RANDOMIZED NES RAM, so two renders of the
// same song differ by ~3 dB and NO level comparison here means anything. The console clock is structural
// instead - PAL's 50 Hz against NTSC's 60 makes the same song play ~16% faster, which the HFF end-detection
// reads off the cart's own sequencer rather than off the audio.
test("a carried role config configures the render's core (region moves the detected song length)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM) || !be.fileExists(ECOLI_SRM)) {
    console.log(`# SKIP risa-render: missing ${RISA_ROM} or ${ECOLI_SRM}`);
    return;
  }
  const lengthAt = (region: string): number => {
    const res = runRenderJob(newCtx(be), baseOpts({
      sav: ECOLI_SRM, maxDurationMs: 80000, out: `/tmp/rp-risa-region-${region}.wav`,
      roles: [{ kind: "mesen", config: { region } }],
    }));
    expect(res.hff, `${region}: the song's HFF end was detected`).toBe(true);
    return res.lengthMs ?? 0;
  };
  const pal = lengthAt("pal");
  const ntsc = lengthAt("ntsc");
  console.log(`[risa-render] carried region: pal ${pal} ms -> ntsc ${ntsc} ms (ratio ${(ntsc / pal).toFixed(3)})`);

  // 50/60 = 0.833 at the frame rate the cart sequences on; the detection granularity is a render chunk, so
  // allow a little either side. A role that never reached the core would put this at exactly 1.000.
  expect(ntsc / pal, "NTSC runs the same song ~16% faster").toBeCloseTo(0.84, 0.03);
});

test("a carried gainDb + role knob land on the system the render builds", () => {
  // The same randomized power-on RAM rules out a level comparison for gain, so this asserts WHERE the values
  // land rather than what they sound like: the render's core is built by adopt, and the store it is built in
  // still holds it when the job returns. What the knobs then DO to the audio is each knob's own test
  // (test-native/expansion-volume.test.ts for the expansion chip, the region case above for the clock).
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-render: no ROM at ${RISA_ROM}`); return; }
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const ctx = newCtx(be);
  runRenderJob(ctx, baseOpts({
    songIndex: 0, durationMs: 300, out: "/tmp/rp-risa-carried.wav",
    gainDb: -6, roles: [{ kind: "mesen", config: { expansionVolume: 25 } }],
  }));

  const sys = ctx.project.systems.view()[0];
  expect(sys.settings.gainDb, "the instance's gain").toBe(-6);
  const mesen = sys.roles.find((r) => r.kind === "mesen")!.config as { expansionVolume?: number };
  expect(mesen.expansionVolume, "the instance's Expansion Volume").toBe(25);
});
