// The render's risa song-length auto-detection against a REAL song that ends: ecoli_soul HFFs its last
// track at ~59 s, driving seq_mode → STOPPED, and the render has to end there rather than at the cap.
//
// Its own file because this is a single ~35 s render over the real core — an order of magnitude past the
// rest of risa-render.test.ts. Sharing a file with the equally long region render (risa-render-region)
// made one process the native suite's longest pole; as separate files the runner pool overlaps them.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { runRenderJob, decodeWav } from "../src/render";
import { RISA_ROM, ECOLI_SRM, ECOLI_PAL_MS, rms, newCtx, baseOpts } from "./risa-render-lib";

test("render auto-detects a real risa song's HFF stop (hff true) and trims to it over the real core", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM) || !be.fileExists(ECOLI_SRM)) skip(`risa-render-hff: missing ${RISA_ROM} or ${ECOLI_SRM}`);
  const out = "/tmp/rp-risa-ecoli.wav";
  // ecoli_soul's working song HFFs its last track at the end → seq_mode STOPPED; auto-detect must end there,
  // not at the cap. The cap sits just past the song so a regression (never detecting the stop) is visible.
  const res = runRenderJob(newCtx(be), baseOpts({ sav: ECOLI_SRM, maxDurationMs: 65000, out }));
  console.log(`[risa-render-hff] ecoli_soul auto-detect: hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames}`);
  expect(res.hff).toBe(true); // the song's HFF end drove seq_mode → STOPPED, detected over the real core
  expect(res.lengthMs! > 55000 && res.lengthMs! < 63000).toBe(true); // ~59 s, well under the 65 s cap

  // Pin the exact figure too, not just the window: risa-render-region uses ECOLI_PAL_MS as the PAL leg of
  // its region ratio rather than re-rendering this same 59 s in its own process. This assertion is what
  // stops that reference going stale — if the song's PAL length ever moves, it fails HERE, next to the
  // measurement, instead of silently skewing the sibling's ratio.
  expect(res.lengthMs!, "the PAL reference risa-render-region compares against").toBeCloseTo(ECOLI_PAL_MS, 1000);

  const wav = decodeWav(be.readFile(out)!);
  expect(wav.pcm.length).toBe(res.frames); // the WAV is trimmed exactly to the detected stop (no silent tail)
  expect(rms(wav.pcm) > 0.001).toBe(true);
});
