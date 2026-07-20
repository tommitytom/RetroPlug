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
