// Rendering an smsggdj cart, over the real core through the shared render library.
//
// Before this, `render smsggdj.sms --sav set.sav` produced a silent WAV, and did so quietly. Three things
// were missing and all three are checked here: --song/--song-index was rejected outright for SMS/GG, no
// play gesture was ever pressed (pressPlay knew gb and nes only), and nothing warned that the result
// could only be silence.
//
// The song load is what makes this console different from LSDj and risa. Their working song lives in the
// battery, so the render seeds SRAM before boot and the cart comes up playing it. smsggdj keeps its
// working song in work RAM and boots deliberately BLANK (song_new), so the render has to write the song
// into the running core afterwards - and, measured on the shipped v0.45, a write at 1500 or 2000 ms is
// silently undone by that same song_new. Worse, a write at 1750 ms verifies as a byte-perfect 6,912-byte
// block and is wiped 400 ms later, so "write and read it back" is not sufficient on its own. The loader
// therefore waits for song_new's own footprint (wave_ram's preset waves) before writing, then requires
// the block to hold. The regression this guards is silence, so every case asserts on audio.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import { runRenderJob, decodeWav, validateRenderOpts, type RenderContext, type RenderOpts } from "../src/render";
import { buildSav } from "../src/smsggdj/codec/sav";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const GG_ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg";
const SAV = "/tmp/rp-smsggdj-render.sav";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

// ONE context for the file, cleared between tests. A per-test ProjectStore would not do: the native
// backend outlives the store, so a fresh store cannot tear down the systems the previous one constructed
// and they keep playing into the next render. That is not hypothetical - it made the "no song selected
// renders silence" case come out at full level, sounding like a pass for the wrong reason.
let shared: RenderContext | null = null;

function ctxFor(be: ReturnType<typeof createRealBackend>): RenderContext {
  if (!shared) {
    const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
    const dsp = createDspRuntime();
    const audio = createAudioDriver();
    expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
    project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
    shared = { backend: be, project, dsp, audio };
  }
  shared.project.systems.clear(); // tear down the previous test's cart before booting the next
  return shared;
}

/** A two-song battery whose songs have populated arrangements, with the cart's own SYNC left OFF so
 *  playback free-runs rather than waiting on a host clock. */
function writeSav(be: ReturnType<typeof createRealBackend>): void {
  const low = buildMetronomeBlock();
  const high = buildMetronomeBlock();
  expect(
    be.writeFile(
      SAV,
      buildSav([{ block: low, name: "ALPHA" }, { block: high, name: "BETA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!,
    ),
  ).toBeTruthy();
}

const baseOpts = (over: Partial<RenderOpts>): RenderOpts => ({
  rom: ROM, sav: SAV, maxDurationMs: 5000, split: "mix", transport: false, start: true, listSongs: false, ...over,
});

test("--song-index loads the song into the booted cart and renders real audio", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) { console.log(`# SKIP sms-render: no ROM at ${ROM}`); return; }
  writeSav(be);
  const out = "/tmp/rp-sms-render.wav";
  runRenderJob(ctxFor(be), baseOpts({ songIndex: 0, durationMs: 2500, out }));

  const wav = decodeWav(be.readFile(out)!);
  const level = rms(wav.pcm);
  console.log(`[sms-render] --song-index 0 → ${wav.pcm.length} samples @${wav.sampleRate}Hz, RMS ${level.toFixed(4)}`);
  expect(level > 0.001).toBe(true);
});

test("--song selects by name, and the Game Gear build renders too", () => {
  const be = createRealBackend();
  if (!be.fileExists(GG_ROM)) { console.log(`# SKIP sms-render: no ROM at ${GG_ROM}`); return; }
  writeSav(be);
  const out = "/tmp/rp-gg-render.wav";
  // Same layout entry as the .sms build (the two v0.45 links have an identical RAM label set), same play
  // button — on GG it is a real Start rather than the SMS Pause NMI, but it is the same wire index.
  runRenderJob(ctxFor(be), baseOpts({ rom: GG_ROM, song: "beta", durationMs: 2500, out }));

  const wav = decodeWav(be.readFile(out)!);
  const level = rms(wav.pcm);
  console.log(`[sms-render] --song beta on .gg → RMS ${level.toFixed(4)}`);
  expect(level > 0.001).toBe(true);
});

test("with no song selected the render is silent, and says so before doing it", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) { console.log(`# SKIP sms-render: no ROM at ${ROM}`); return; }
  writeSav(be);
  const out = "/tmp/rp-sms-nosong.wav";
  const warnings: string[] = [];
  // This is the ONE case that legitimately renders silence: the cart boots blank on purpose and there is
  // no working song in the battery to fall back on. It is not an error (rendering the boot state is a
  // real thing to want), so the contract is that it warns and names the songs it could have played.
  runRenderJob(ctxFor(be), baseOpts({ durationMs: 800, out }), { warn: (m) => warnings.push(m), log: () => {} });

  const lvl = rms(decodeWav(be.readFile(out)!).pcm);
  const said = warnings.join("\n");
  console.log(`[sms-render] no-song rms=${lvl.toFixed(6)} warnings=${JSON.stringify(warnings)}`);
  expect(lvl < 0.0005).toBe(true); // still silent — the diagnosis, not a fix
  expect(said.includes("--song")).toBe(true);
  expect(said.includes("ALPHA")).toBe(true); // the warning lists what it could have played
});

test("a song that cannot be loaded fails loudly instead of rendering silence", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) { console.log(`# SKIP sms-render: no ROM at ${ROM}`); return; }
  writeSav(be);
  let threw = "";
  try {
    runRenderJob(ctxFor(be), baseOpts({ songIndex: 7, durationMs: 500, out: "/tmp/rp-sms-bad.wav" }));
  } catch (e) {
    threw = String(e);
  }
  // An empty slot is caught before the boot. The point is that it is an ERROR: a silent WAV that looks
  // like a successful render is the failure mode this whole path exists to remove.
  expect(threw.includes("slot 7 is empty")).toBe(true);
});

test("a render restored from a savestate plays what was loaded, without re-toggling the transport", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) { console.log(`# SKIP sms-render: no ROM at ${ROM}`); return; }
  writeSav(be);
  const ctx = ctxFor(be);

  // What the UI's System > Render does for this console: the battery does NOT describe the working song
  // (it lives in work RAM), so the job carries a SAVESTATE of the live cart. Build that state here - load a
  // song into a running cart and start it - then render from it with no --song at all.
  const id = ctx.project.systems.addSystem(ROM, { explicitSav: SAV })!;
  ctx.audio.renderAudio(3000);
  for (const w of smsggdjIntegration.liveLoad!(be.readFile(ROM)!, be.readFile(SAV)!, 1, be.readRam(id) ?? undefined)!) {
    expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
  }
  ctx.audio.pressButton(id, 7, true);
  ctx.audio.renderAudio(100);
  ctx.audio.pressButton(id, 7, false);
  ctx.audio.renderAudio(500);
  const state = be.readState(id);
  expect(state != null).toBeTruthy();
  const statePath = "/tmp/rp-smsggdj-render.ss0";
  expect(be.writeFile(statePath, state!)).toBeTruthy();

  const out = "/tmp/rp-sms-state.wav";
  runRenderJob(ctxFor(be), baseOpts({ state: statePath, durationMs: 2000, out }), { warn: () => {}, log: () => {} });
  const level = rms(decodeWav(be.readFile(out)!).pcm);
  console.log(`[sms-render] from a playing savestate → RMS ${level.toFixed(4)}`);
  // The press is a TOGGLE: a blind one would have STOPPED this cart and rendered silence.
  expect(level > 0.001).toBe(true);
});

test("validateRenderOpts accepts a song flag on sms/gg and still rejects it on gba", () => {
  // The flag was rejected for every platform but gb/nes, which is what made the smsggdj seeding path
  // below it unreachable. gba stays rejected — it has no song catalog at all.
  validateRenderOpts(baseOpts({ songIndex: 0 }), "sms");
  validateRenderOpts(baseOpts({ songIndex: 0 }), "gg");
  let threw = "";
  try {
    validateRenderOpts(baseOpts({ songIndex: 0 }), "gba");
  } catch (e) {
    threw = String(e);
  }
  expect(threw.includes("--song/--song-index")).toBe(true);
});
