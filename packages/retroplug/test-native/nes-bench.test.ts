// NES (Mesen) DSP-thread benchmark — the sibling of dsp-bench.test.ts for the Game Boy. Drives a REAL
// Mesen core off the RT thread via the deterministic renderAudio pull path under a MIDI workload, and
// reports the same per-block allocation + per-stage timing metrics. The point of comparison: on the
// handheld the NES core "struggles" far more than SameBoy, and this pins down WHERE — the Mesen core
// step (CPU+PPU+APU, the DSP_SPAN_APU "apu-render" span wraps runBlock for every core, Mesen included) vs
// the JS kernel (marshal + js-call). One real NES system driven by host MIDI (the nes-n8-midi role).
//
// The counters/spans exist only in a RETROPLUG_PROFILE host (built by tools/run-profile.sh); under the
// default host this no-ops (so the normal test:native sweep stays green). Run it with:
//   tools/run-profile.sh stats nes-bench      # per-block allocation counters + xRT
//   tools/run-profile.sh trace nes-bench      # per-stage Chrome trace-event JSON → build-prof/trace.json
// On-device: bundle via tools/bundle-native-test.mjs nes-bench, set RP_BENCH_ROM to the on-device ROM.
//
// Workload knobs (env): RP_BENCH_PROFILE (A|B|C), RP_BENCH_BLOCKS, RP_BENCH_WARMUP, RP_BENCH_SEED,
// RP_BENCH_ROM (path to a .nes; default the in-tree bliptoaster.nes). One test per file (shared native Engine).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const SR = 44100;
const BLK = 1024;
const BLOCK_MS = (1000 * BLK) / SR; // 23.22 ms — one renderAudio(BLOCK_MS) == exactly one processBlock

function env(name: string): string | undefined {
  const g = globalThis as { tjs?: { env?: Record<string, string> }; process?: { env?: Record<string, string> } };
  try {
    return g.tjs?.env?.[name] ?? g.process?.env?.[name];
  } catch {
    return undefined;
  }
}
const envStr = (name: string, def: string): string => env(name) ?? def;
const envInt = (name: string, def: number): number => {
  const v = env(name);
  const n = v == null ? NaN : parseInt(v, 10);
  return Number.isFinite(n) ? n : def;
};
const nowMs = (): number => (globalThis as { performance?: { now(): number } }).performance?.now() ?? 0;

// --- seeded MIDI generator (pure; no Math.random / Date) — same shapes as dsp-bench --------------------
function mulberry32(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

type Ev = { block: number; msg: number[] };
const noteOn = (ch: number, note: number, vel: number): number[] => [0x90 | ch, note, vel];
const noteOff = (ch: number, note: number): number[] => [0x80 | ch, note, 0];

// bliptoaster.nes voice map (reference_bliptoaster): MIDI ch1(0) → Pulse1, ch3(2) → Triangle, ch4(3) → Noise
// (ch2 is a known-broken ROM voice — driving it is harmless, it just doubles onto Pulse1). The Mesen core
// steps its whole CPU/PPU/APU each block regardless of which voices ring, so this drives audible voices for
// realism; the per-block core cost is what we're measuring.
function buildSchedule(profile: string, blocks: number, seed: number): Ev[] {
  const rnd = mulberry32(seed);
  const pick = <T>(a: T[]): T => a[Math.floor(rnd() * a.length)];
  const totalMs = blocks * BLOCK_MS;
  const at = (ms: number): number => Math.max(0, Math.min(blocks - 1, Math.floor(ms / BLOCK_MS)));
  const scale = [60, 62, 64, 65, 67, 69, 71, 72];
  const evs: Ev[] = [];

  if (profile === "C") {
    // Synthetic worst case: flood every voice with a note churn per block.
    for (let b = 0; b < blocks; b++) {
      for (const ch of [0, 2, 3]) {
        evs.push({ block: b, msg: noteOff(ch, 60) });
        evs.push({ block: b, msg: noteOn(ch, pick(scale), 100) });
      }
    }
    return evs;
  }

  const beat = 60000 / 140;
  const sixteenth = beat / 4;
  const eighth = beat / 2;
  const melody = (ch: number, step: number, hold: number, notes: number[]): void => {
    for (let t = 0; t < totalMs; t += step) {
      const n = pick(notes);
      evs.push({ block: at(t), msg: noteOn(ch, n, 100) });
      evs.push({ block: at(t + hold), msg: noteOff(ch, n) });
    }
  };

  // Profile A (default): Pulse1 lead 16ths, Triangle bass 8ths, Noise drums 16ths. Profile B: sparser.
  const leadStep = profile === "B" ? eighth : sixteenth;
  melody(0, leadStep, leadStep * 0.8, scale); // Pulse1
  melody(2, eighth, eighth * 0.9, scale.map((n) => n - 24)); // Triangle bass
  melody(3, sixteenth, sixteenth * 0.5, [38, 42, 46]); // Noise drums
  return evs.sort((a, c) => a.block - c.block);
}

test("nes-bench: Mesen core per-block cost + JS allocation under a NES + MIDI workload", () => {
  const be = createRealBackend();
  const rom = envStr("RP_BENCH_ROM", __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes");
  if (!be.fileExists(rom)) {
    console.warn(`[nes-bench] no ROM at ${rom} — set RP_BENCH_ROM. Skipping.`);
    expect(true).toBeTruthy();
    return;
  }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const profile = envStr("RP_BENCH_PROFILE", "A");
  const blocks = Math.max(1, envInt("RP_BENCH_BLOCKS", 2000));
  const warmup = Math.max(0, envInt("RP_BENCH_WARMUP", 200));
  const seed = envInt("RP_BENCH_SEED", 0xc0ffee);

  // Ownership discipline (per dsp-threaded / app-play-nes): kernel loaded + store→DSP hook installed
  // before audio, then loadRom triggers syncDspFromStore to build the NES pipeline (nes-n8-midi role).
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
  const id = (project.systems.loadRom(rom) as { system: number }).system;
  expect(typeof id).toBe("number");
  expect(project.systems.view()[0].platform).toBe("nes");

  // Bail before the render loops when the host has no profiling allocator (normal test:native stays fast).
  if (!audio.dspAllocStats().enabled) {
    console.warn("[nes-bench] instrumentation off — run via tools/run-profile.sh for real metrics. Skipping.");
    expect(true).toBeTruthy();
    return;
  }

  audio.setBpm(140);
  audio.setTransport(true);
  audio.renderAudio(1500); // let the ROM init settle (discarded)

  const schedule = buildSchedule(profile, warmup + blocks, seed);
  let si = 0;
  const stageForBlock = (b: number): void => {
    while (si < schedule.length && schedule[si].block === b) audio.stageMidiIn(schedule[si++].msg);
  };

  for (let b = 0; b < warmup; b++) {
    stageForBlock(b);
    audio.renderAudio(BLOCK_MS); // warmup — pools/atoms settle (discarded)
  }

  audio.dspResetAllocStats(true); // open the window + pin QuickJS auto-GC off (deterministic)
  const t0 = nowMs();
  for (let b = warmup; b < warmup + blocks; b++) {
    stageForBlock(b);
    audio.renderAudio(BLOCK_MS);
  }
  const wallMs = nowMs() - t0;

  const s = audio.dspAllocStats();
  const gc = audio.dspRunGc();

  const bc = s.blockCount || 1;
  const round = (x: number): number => Math.round(x * 100) / 100;
  const metrics = {
    core: "mesen",
    rom: rom.split("/").pop(),
    profile,
    blocks: s.blockCount,
    allocsPerBlock: round(s.allocCalls / bc),
    bytesPerBlock: round(s.allocBytes / bc),
    freesPerBlock: round(s.freeCalls / bc),
    liveHeapDelta: s.liveBytesDelta,
    peakLive: s.peakBytes,
    maxBlockAllocs: s.maxBlockAllocCalls,
    gcMs: round(gc.ms),
    gcFreedBytes: gc.freedBytes,
    // xRT = realtime headroom: (audio produced) / (wall time). >1 = keeps up; the handheld GB run was ~1.09.
    xRT: wallMs > 0 ? round((s.blockCount * BLOCK_MS) / wallMs) : 0,
    usPerBlock: wallMs > 0 ? round((wallMs * 1000) / s.blockCount) : 0,
  };
  console.log("DSP-BENCH " + JSON.stringify(metrics));

  expect(s.blockCount > 0).toBeTruthy();

  // --- optional per-stage runtime trace (spec/08 Tier B): dsp-kernel (⊃ marshal, js-call) + apu-render
  // (the Mesen core step). One Chrome trace-event JSON line the wrapper captures → build-prof/trace.json. ---
  if (env("RP_BENCH_TRACE")) {
    const traceBlocks = Math.max(1, envInt("RP_BENCH_TRACE_BLOCKS", 64));
    const tSched = buildSchedule(profile, traceBlocks, seed ^ 0x9e3779b9);
    let ti = 0;
    const stageTrace = (blk: number): void => {
      while (ti < tSched.length && tSched[ti].block === blk) audio.stageMidiIn(tSched[ti++].msg);
    };
    audio.dspTraceReset(true);
    for (let blk = 0; blk < traceBlocks; blk++) {
      stageTrace(blk);
      audio.renderAudio(BLOCK_MS);
    }
    const spans = audio.dspTrace();
    const names = audio.dspTraceNames();
    audio.dspTraceReset(false);
    const traceEvents = spans.map((sp) => ({
      name: names[sp.label] ?? `#${sp.label}`,
      ph: "X",
      ts: sp.t0,
      dur: sp.t1 - sp.t0,
      pid: 1,
      tid: 1,
    }));
    console.log("DSP-TRACE " + JSON.stringify({ traceEvents, displayTimeUnit: "ms" }));
  }
});
