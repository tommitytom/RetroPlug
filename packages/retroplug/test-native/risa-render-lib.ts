// Shared fixtures for the three risa render tests. They were one file until its two ecoli
// renders (~35 s each, over the real core) made it the native suite's longest pole by a
// factor of three — the runner pool would finish everything else and then sit on this one
// file, one core busy, for half the suite's wall clock. Split across files they run
// concurrently; this module is what they share. Not a `.test.ts`, so the runner's walk
// skips it.
import { expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import type { RenderContext, RenderOpts } from "../src/render";

declare const __DSP_KERNEL_BUNDLE__: string;

declare const __RISA_SRC__: string;
export const RISA_ROM = __RISA_SRC__ + "/build/risa-pal.nes";
// A real risa song whose last track HFFs at the end (~59 s) — the only demo sav here that
// stops rather than loops, so it's the fixture that proves seq_mode → STOPPED end-detection
// over the real core.
declare const __RESOURCES_DIR__: string;
export const ECOLI_SRM = __RESOURCES_DIR__ + "/roms/risa/ecoli_soul.srm";

// ecoli_soul's detected PAL length over the real core. Pinned from both sides so it cannot
// rot silently into a stale reference: risa-render-hff MEASURES it (and asserts it lands
// here, within a render chunk), and risa-render-region uses it as the PAL leg of its
// region-ratio check instead of re-rendering the same 59 s a second time in its own process.
export const ECOLI_PAL_MS = 59000;

export const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

export function newCtx(be: ReturnType<typeof createRealBackend>): RenderContext {
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  return { backend: be, project, dsp, audio };
}

// `sav` is deliberately required rather than defaulted: these files now run as concurrent
// processes, so a shared scratch .srm path would be a write race between them.
export const baseOpts = (over: Partial<RenderOpts> & { sav: string }): RenderOpts => ({
  rom: RISA_ROM, maxDurationMs: 5000, split: "mix", transport: false, start: true, listSongs: false, ...over,
});
