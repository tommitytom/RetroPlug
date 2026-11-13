// Mega Drive (BlastEm) speed benchmark, the sibling of nes-bench.test.ts. One measured window per workload, each on a
// real system driven through the engine's deterministic renderAudio pull path: the synthetic images the parity group
// boots (so the number says what this host costs on a machine whose only work is the core's), and, from the fixture
// tree, a real genmddj cart playing a song clocked over port 2 (what the shipped sync path costs on top of the core).
// Reports xRT (audio produced / wall time) and the per-block allocation counters the profile host carries.
//
// The counters/spans exist only in a RETROPLUG_PROFILE host (built by tools/run-profile.sh); under the default host
// this SKIPS, so the normal test:native sweep stays fast and says the case did not run rather than printing a green
// tick for it. Run it with:
//   pnpm run profile md-bench   # per-window xRT + allocation counters, in a build-prof/ host
//   tools/run-profile.sh stats md-bench   # the same, without the wrapper (`pnpm profile`'s script name routes to npm's
//                                         # own `profile` command, so the bare `pnpm profile md-bench` is not this)
//
// Workload knobs (env): RP_BENCH_BLOCKS, RP_BENCH_WARMUP. One test per file (shared native Engine): the genmddj window
// is the file's fixture-gated half, so a box without the ROM skips here after every synthetic window printed its line,
// and the skip is counted (spec/06).
import { test, expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver, type DspAllocStats } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import {
  fmPsgRom, psgOnlyRom, fmOnlyRom, hvPsgRom, vintHintRom, z80TimingRom, vintDmaRom,
} from "@retroplug/blastem/testFixtures";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string; // the temp dir run-native-tests.mjs gives each file (this case's own ROM copies)

const BLK = 1024; // one renderAudio(BLOCK_MS) == exactly one processBlock, at the partition nes-bench measures at
const DEFAULT_WARMUP = 200; // blocks discarded before the window, so pools and the core's own caches settle first
const GENMDDJ_ROM = __RESOURCES_DIR__ + "/roms/genmddj_v0_27.bin";

// The cart's own work-RAM variables, by offset from $FF0000, as genmddj-sync.test.ts reads them: `playing` says the
// cart's own transport is running, and `live_bar` (one step per row advance, 4 bits wide) is what proves the clock
// stream reached it through the window. opt_sync 5 is IN24, proj_tempo the cart's BPM.
const LIVE_BAR = 0xd3cb;
const PLAYING = 0xe20a;
const OPT_SYNC = 0xe3e5;
const PROJ_TEMPO = 0xe3e7;
const SYNC_IN24 = 5;
const BUTTON_START = 7;
/** The DAW tempo the genmddj window runs the transport at, and the cart's IN24 rate: 24 PPQN (genmddjSyncNode.ts) and
 *  6 clocks per row (main.asm:9574), so 8 rows a second at this tempo. */
const BPM = 120;
const ROWS_PER_SECOND = (BPM / 60) * 24 / 6;

// The parity group's seven images (parity/md.test.ts), in its order: two PSG-only images, the one whose YM writes
// actually land, and the four that watch their own VDP counters, take interrupts, run a Z80 program and start a
// 68k-to-VDP DMA. They differ in what the core charges per step, which is what makes seven lines rather than one.
const IMAGES: [name: string, image: () => Uint8Array][] = [
  ["fm-psg", fmPsgRom],
  ["psg", psgOnlyRom],
  ["fm", fmOnlyRom],
  ["hv-psg", hvPsgRom],
  ["vint-hint", vintHintRom],
  ["z80-timing", z80TimingRom],
  ["vint-dma", vintDmaRom],
];

function env(name: string): string | undefined {
  const g = globalThis as { tjs?: { env?: Record<string, string> }; process?: { env?: Record<string, string> } };
  try {
    return g.tjs?.env?.[name] ?? g.process?.env?.[name];
  } catch {
    return undefined;
  }
}
const envInt = (name: string, def: number): number => {
  const v = env(name);
  const n = v == null ? NaN : parseInt(v, 10);
  return Number.isFinite(n) ? n : def;
};
const nowMs = (): number => (globalThis as { performance?: { now(): number } }).performance?.now() ?? 0;
const round = (x: number): number => Math.round(x * 100) / 100;

test("md-bench: a Mega Drive's xRT on the synthetic images and on a playing genmddj song", () => {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  // The guard comes before any system is built: the sweep this file joins on a normal run is the default host's, where
  // nothing here can be measured, so booting a console to find that out would only slow the sweep down.
  if (!audio.dspAllocStats().enabled)
    skip("md-bench: allocation instrumentation off - run `pnpm run profile md-bench` for the numbers");

  const blocks = Math.max(1, envInt("RP_BENCH_BLOCKS", 2000));
  const warmup = Math.max(0, envInt("RP_BENCH_WARMUP", DEFAULT_WARMUP));
  const rate = audio.sampleRate() || 44100;
  const blockMs = (1000 * BLK) / rate;

  // One system, one window, then it is gone: the figure is a console's cost, so no window pays for another's instance.
  // `check` runs once the window's numbers are read and before the system goes, so a workload can hold them and still
  // read its cart's own state; anything it renders is outside the timed window and outside the counted one. The window's
  // console is removed in a `finally`, so a failing assertion or check cannot leave it in the shared engine.
  const benchWindow = (name: string, id: number, check?: (s: DspAllocStats) => void): void => {
    for (let b = 0; b < warmup; b++) audio.renderAudio(blockMs);

    audio.dspResetAllocStats(true); // open the window + pin QuickJS auto-GC off (deterministic)
    const t0 = nowMs();
    for (let b = 0; b < blocks; b++) audio.renderAudio(blockMs);
    const wallMs = nowMs() - t0;

    const s = audio.dspAllocStats();
    const gc = audio.dspRunGc();
    const bc = s.blockCount || 1;
    const xRT = wallMs > 0 ? round((s.blockCount * blockMs) / wallMs) : 0;
    console.log(
      "DSP-BENCH " +
        JSON.stringify({
          core: "blastem.md",
          window: name,
          hostRate: rate,
          blocks: s.blockCount,
          allocsPerBlock: round(s.allocCalls / bc),
          bytesPerBlock: round(s.allocBytes / bc),
          freesPerBlock: round(s.freeCalls / bc),
          liveHeapDelta: s.liveBytesDelta,
          peakLive: s.peakBytes,
          maxBlockAllocs: s.maxBlockAllocCalls,
          maxBlockBytes: s.maxBlockAllocBytes,
          gcMs: round(gc.ms),
          gcFreedBytes: gc.freedBytes,
          xRT,
          usPerBlock: wallMs > 0 ? round((wallMs * 1000) / s.blockCount) : 0,
        }),
    );
    // The headline prints beside the line it summarises, inside the window: the genmddj window is fixture-gated, so a
    // box without the ROM leaves the test through `skip()` before any trailing loop would print the synthetic ones.
    console.log(`md: ${xRT}x realtime (${name})`);

    try {
      expect(s.blockCount, `${name}: blocks measured`).toBeGreaterThan(0);
      expect(xRT, `${name}: the window measured something`).toBeGreaterThan(0);
      check?.(s);
    } finally {
      project.systems.removeSystem(id);
    }
  };

  // The synthetic images: written into this file's own directory, since a console opens its ROM by path and classifies
  // it by the SEGA header at $100 rather than by the extension (.md is the Mega Drive's own, parity/scenes.ts).
  for (const [name, image] of IMAGES) {
    const path = `${__CONFIG_DIR__}/${name}.md`;
    expect(be.writeFile(path, image())).toBeTruthy();
    const id = project.systems.addSystem(path);
    expect(typeof id === "number", `${name}: the image built a Mega Drive`).toBeTruthy();
    const md = id as number;
    const view = project.systems.view().find((v) => v.id === md);
    expect(view !== undefined, `${name}: the image is in the view`).toBeTruthy();
    expect(view?.platform).toBe("md");
    audio.renderAudio(1500); // the image's own boot: its chip-register writes land in the first instructions after reset
    benchWindow(name, md);
  }

  // The real cart, fixture-gated: it is not vendored (tools/fetch-genmddj-fixture.sh builds it from a checkout), so a
  // box without the tree skips here, after every synthetic line printed, and the skip is counted.
  if (!be.fileExists(GENMDDJ_ROM)) skip(`md-bench: the genmddj window needs the ROM at ${GENMDDJ_ROM}`);

  // The ROM is copied into this file's own directory first, so its .sav and its battery slot are the bench's and the
  // resources tree stays read-only (genmddj-sync.test.ts does the same, and for the same reason).
  const local = __CONFIG_DIR__ + "/genmddj_v0_27.bin";
  expect(be.writeFile(local, be.readFile(GENMDDJ_ROM)!)).toBeTruthy();
  const genmddj = (project.systems.loadRom(local) as { system: number }).system;
  // The role the ROM provider attached is what makes the window the shipped sync path rather than a cart reading a
  // pad's idle lines, and the clock stream the window measures comes from it.
  const cartView = project.systems.view().find((v) => v.id === genmddj);
  expect(cartView !== undefined, "genmddj: the cart is in the view").toBeTruthy();
  expect(cartView?.platform).toBe("md");
  expect(cartView?.roles.map((r) => r.kind).includes("genmddj-sync")).toBeTruthy();

  audio.renderAudio(2000); // boot: the cart clears RAM, sets its VDP up and draws its UI
  const ram = () => project.systems.readRam(genmddj)!;
  // IN24, and a tempo whose 24-PPQN clocks are well inside the cart's 3-per-frame poll cap. The cart reads its OPTIONS
  // byte every tick, so a poke is what a user's menu pick ends up as (its save format is spec 2's, unbuilt here).
  expect(project.systems.writeRam(genmddj, OPT_SYNC, Uint8Array.of(SYNC_IN24))).toBe(true);
  expect(project.systems.writeRam(genmddj, PROJ_TEMPO, Uint8Array.of(BPM))).toBe(true);
  audio.pressButton(genmddj, BUTTON_START, true); // play, through the cart's own play-start (which arms the sync)
  audio.renderAudio(100);
  audio.pressButton(genmddj, BUTTON_START, false);
  audio.renderAudio(500);
  expect(ram()[PLAYING]).toBe(1);

  audio.setBpm(BPM);
  // Tempo before the transport: TickClock reads the block's tempo per block, so the block the transport starts in
  // would otherwise walk at whatever tempo the store held.
  audio.setTransport(true);
  benchWindow("genmddj", genmddj, (s) => {
    // This is the one window with a script node in its plan (the role's rp.genmddj.sync, marshalling the clock words it
    // hands the port device), so it is the one whose zero dsp-bench's reasoning holds for: a count of 0 can only be read
    // as "the node allocates nothing per block" if something JS ran, which the row check below says it did. The
    // synthetic windows hold no node, so theirs is printed, not held.
    if (warmup >= DEFAULT_WARMUP) {
      expect(s.allocCalls, "genmddj: allocations in the measured window").toBe(0);
      expect(s.maxBlockAllocCalls, "genmddj: allocations in the worst block").toBe(0);
    }
    // The window measured a SONG, not a paused cart: live_bar advances while the clock stream lands. Read after the
    // window because readRam copies into a fresh array and the counted runtime is the one this bundle runs in, so a
    // read inside the loop would land in the census. 32 blocks is about 0.7 s, and at 8 rows a second the count cannot
    // wrap the 4 bits the counter holds, so the difference is the row count itself; a clock stream even twice too fast
    // counts about 12 rows, still short of the wrap and past the upper bound below.
    expect(ram()[PLAYING], "genmddj: the cart still played at the window's end").toBe(1);
    const barBefore = ram()[LIVE_BAR];
    audio.renderAudio(32 * blockMs);
    const rows = (ram()[LIVE_BAR] - barBefore) & 0xf;
    const expectedRows = 32 * (blockMs / 1000) * ROWS_PER_SECOND;
    expect(rows, "genmddj: rows the song advanced during the proof render").toBeGreaterThanOrEqual(
      Math.floor(expectedRows) - 3,
    );
    expect(rows, "genmddj: and no faster than the clock stream allows").toBeLessThanOrEqual(
      Math.ceil(expectedRows) + 2,
    );
  });

  audio.setTransport(false);
});
