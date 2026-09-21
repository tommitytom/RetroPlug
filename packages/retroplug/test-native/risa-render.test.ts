// The `render` tool's song-selection flags extended to risa (NES) savs: --list-songs lists the RSAV catalog,
// --song-index promotes a saved song to the working banks before the render, and the risa play gesture
// (SELECT+START) makes it audible. Drives the shared render library over a real backend + DSP runtime
// (mirrors test-native/app-play-nes-channels.test.ts). Gated on the built risa ROM — SKIPs when absent.
//
// The two long-song legs live in sibling files (risa-render-hff / risa-render-region) so they render
// concurrently instead of serializing behind each other; shared fixtures are in risa-render-lib.ts.
// Everything here is a short render, so this file stays a few seconds.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { runRenderJob, readRisaSongs, decodeWav, type RenderOpts } from "../src/render";
import { dirname, joinPath } from "../src/pathUtil";
import { savBytes } from "../test/risa/fixtures";
import { RISA_ROM, rms, newCtx, baseOpts } from "./risa-render-lib";

// Scratch sav for the short blumarbl renders. Named for this file — the sibling risa-render-*
// files run as concurrent processes, and a shared scratch path would be a write race.
const SAV = "/tmp/rp-risa-render.srm";
const opts = (over: Partial<RenderOpts>): RenderOpts => baseOpts({ sav: SAV, ...over });

test("render --list-songs lists the risa catalog (readRisaSongs)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy(); // current v2 catalog, one song: BLUMARBL
  const { songs } = readRisaSongs(newCtx(be), opts({}));
  expect(songs.length).toBe(1);
  expect(songs[0].index).toBe(0);
  expect(songs[0].name).toBe("BLUMARBL");
});

test("render --song-index promotes a risa catalog song to working + renders non-silent audio", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const out = "/tmp/rp-risa-render.wav";
  runRenderJob(newCtx(be), opts({ songIndex: 0, durationMs: 2000, out }));

  const wav = decodeWav(be.readFile(out)!);
  const level = rms(wav.pcm);
  console.log(`[risa-render] --song-index 0 (BLUMARBL) → ${wav.pcm.length} samples @${wav.sampleRate}Hz, RMS ${level.toFixed(4)}`);
  expect(level > 0.001).toBe(true); // the promoted working song actually plays (SELECT+START gesture)
});

test("render without --out defaults the output filename to the song's name (not the ROM name)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  // No --out: outBase derives the name from the selected song (BLUMARBL), next to the ROM — not risa-pal.wav.
  const res = runRenderJob(newCtx(be), opts({ songIndex: 0, durationMs: 300 }));
  const expected = joinPath(dirname(RISA_ROM), "BLUMARBL.wav");
  expect(res.outputs).toEqual([expected]);
  expect(be.fileExists(expected)).toBe(true);
  be.deleteFile(expected); // don't leave the derived WAV next to the source ROM
});

test("render onExists 'rename' writes the next free name instead of clobbering", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const first = joinPath(dirname(RISA_ROM), "BLUMARBL.wav");
  const second = joinPath(dirname(RISA_ROM), "BLUMARBL_2.wav");
  be.deleteFile(first); be.deleteFile(second); // clean slate

  runRenderJob(newCtx(be), opts({ songIndex: 0, durationMs: 300 })); // 1st → BLUMARBL.wav (overwrite default)
  const r2 = runRenderJob(newCtx(be), opts({ songIndex: 0, durationMs: 300, onExists: "rename" }));
  expect(r2.outputs).toEqual([second]); // target exists → renamed, not clobbered
  expect(be.fileExists(first)).toBe(true); // the first render is left intact
  expect(be.fileExists(second)).toBe(true);
  be.deleteFile(first); be.deleteFile(second);
});

test("render auto-detects risa song length via seq_mode (HFF stop) over the real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const out = "/tmp/rp-risa-autodetect.wav";
  // No durationMs → the risa auto-detect path: render to the seq_mode STOPPED edge, capped at maxDurationMs.
  // (A looping song caps out with hff:false; either way the auto-detect path reports a length + real audio.)
  const res = runRenderJob(newCtx(be), opts({ songIndex: 0, maxDurationMs: 3000, out }));
  console.log(`[risa-render] auto-detect: hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames}`);
  expect(res.lengthMs !== undefined).toBe(true); // the auto-detect path engaged (a fixed render reports none)
  expect((res.frames ?? 0) > 0).toBe(true);
  const wav = decodeWav(be.readFile(out)!);
  expect(rms(wav.pcm) > 0.001).toBe(true); // real song audio, whether it HFF-stops or hits the cap
});

test("a carried gainDb + role knob land on the system the render builds", () => {
  // The randomized NES power-on RAM rules out a level comparison for gain, so this asserts WHERE the values
  // land rather than what they sound like: the render's core is built by adopt, and the store it is built in
  // still holds it when the job returns. What the knobs then DO to the audio is each knob's own test
  // (test-native/expansion-volume.test.ts for the expansion chip, risa-render-region for the clock).
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM)) skip(`risa-render: no ROM at ${RISA_ROM}`);
  expect(be.writeFile(SAV, savBytes("v2_blumarbl"))).toBeTruthy();
  const ctx = newCtx(be);
  runRenderJob(ctx, opts({
    songIndex: 0, durationMs: 300, out: "/tmp/rp-risa-carried.wav",
    gainDb: -6, roles: [{ kind: "mesen", config: { expansionVolume: 25 } }],
  }));

  const sys = ctx.project.systems.view()[0];
  expect(sys.settings.gainDb, "the instance's gain").toBe(-6);
  const mesen = sys.roles.find((r) => r.kind === "mesen")!.config as { expansionVolume?: number };
  expect(mesen.expansionVolume, "the instance's Expansion Volume").toBe(25);
});
