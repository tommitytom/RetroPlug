// DSP-thread allocation benchmark (spec/08-profiling.md). Drives the real DSP kernel off the RT thread
// via the deterministic renderAudio pull path under a seeded mGB + heavy-MIDI workload, and reports what
// the bare QuickJS runtime allocates PER BLOCK — the refcount-churn signal (QuickJS frees acyclic garbage
// immediately, so "GC" is rare; malloc/free churn per block is the real cost).
//
// The allocation counters exist only in a RETROPLUG_PROFILE host (built by `pnpm profile:greenfield` /
// tools/run-greenfield-profile.sh). Under the default host the counters report enabled:false and this
// test no-ops (so the normal `pnpm test:greenfield-native` sweep stays green). One test per file (the
// native Project/Engine is shared per host process).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { createDspRuntime } from "../src/dspRuntime";
import { SystemsStore } from "../src/systemsStore";

declare const __DSP_KERNEL_BUNDLE__: string;

const SR = 44100;
const BLK = 1024;
const BLOCK_MS = (1000 * BLK) / SR; // 23.22 ms — one renderAudio(BLOCK_MS) == exactly one processBlock

// --- env params (optional; the wrapper script sets these to vary the run) --------------------------
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

// --- seeded MIDI generators (pure; no Math.random / Date) ------------------------------------------
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
const cc = (ch: number, num: number, val: number): number[] => [0xb0 | ch, num, val & 0x7f];
const pb = (ch: number, v14: number): number[] => [0xe0 | ch, v14 & 0x7f, (v14 >> 7) & 0x7f];

// Realistic mGB streams (docs/lsdj.md + spec/08 §5). A: musical 4-part @140 BPM (~105 ev/s). B: poly
// chord bursts + arp (per-block bursts). C: synthetic worst case (~700 ev/s) — floods the kernel/FIFO.
function buildSchedule(profile: string, blocks: number, seed: number): Ev[] {
  const rnd = mulberry32(seed);
  const pick = <T>(a: T[]): T => a[Math.floor(rnd() * a.length)];
  const totalMs = blocks * BLOCK_MS;
  const at = (ms: number): number => Math.max(0, Math.min(blocks - 1, Math.floor(ms / BLOCK_MS)));
  const scale = [60, 62, 64, 65, 67, 69, 71, 72];
  const evs: Ev[] = [];

  if (profile === "C") {
    for (let b = 0; b < blocks; b++) {
      for (let ch = 0; ch < 4; ch++) {
        evs.push({ block: b, msg: noteOff(ch, 60) });
        evs.push({ block: b, msg: noteOn(ch, pick(scale), 100) });
        evs.push({ block: b, msg: cc(ch, 1, Math.floor(rnd() * 128)) });
      }
      evs.push({ block: b, msg: pb(0, Math.floor(rnd() * 16384)) });
    }
    return evs;
  }

  const beat = 60000 / 140;
  const sixteenth = beat / 4;
  const eighth = beat / 2;
  const melody = (ch: number, step: number, hold: number, notes: number[]): void => {
    for (let t = 0; t < totalMs; t += step) {
      const n = pick(notes);
      evs.push({ block: at(t), msg: noteOn(ch, n, 90) });
      evs.push({ block: at(t + hold), msg: noteOff(ch, n) });
    }
  };

  if (profile === "B") {
    for (let t = 0; t < totalMs; t += eighth) {
      const root = pick(scale);
      [root, root + 4, root + 7].forEach((n) => evs.push({ block: at(t), msg: noteOff(4, n) }));
      [root, root + 4, root + 7].forEach((n) => evs.push({ block: at(t + eighth * 0.9), msg: noteOn(4, n, 90) }));
    }
    melody(0, sixteenth, sixteenth * 0.8, scale); // arp
    for (let b = 0; b < blocks; b++) {
      evs.push({ block: b, msg: cc(0, 1, Math.floor(rnd() * 128)) });
      evs.push({ block: b, msg: cc(2, 3, Math.floor(rnd() * 128)) });
    }
    return evs.sort((a, c) => a.block - c.block);
  }

  // Profile A (default): lead 16ths, harmony + bass 8ths, noise-drums 16ths, 2 CC LFOs (~per block).
  melody(0, sixteenth, sixteenth * 0.8, scale);
  melody(1, eighth, eighth * 0.8, scale.map((n) => n - 12));
  melody(2, eighth, eighth * 0.9, scale.map((n) => n - 24));
  melody(3, sixteenth, sixteenth * 0.5, [38, 42, 46]);
  for (let b = 0; b < blocks; b++) {
    const ph = (b * BLOCK_MS) / 1000;
    evs.push({ block: b, msg: cc(0, 10, Math.floor(64 + 63 * Math.sin(ph * 2 * Math.PI * 0.7))) }); // pan LFO
    evs.push({ block: b, msg: cc(1, 1, Math.floor(64 + 63 * Math.sin(ph * 2 * Math.PI * 1.3))) }); // pulse-width LFO
  }
  return evs.sort((a, c) => a.block - c.block);
}

test("dsp-bench: DSP kernel per-block allocation under an mGB + MIDI workload", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const dsp = createDspRuntime();

  const profile = envStr("RP_BENCH_PROFILE", "A");
  const cores = Math.max(1, envInt("RP_BENCH_CORES", 1));
  const blocks = Math.max(1, envInt("RP_BENCH_BLOCKS", 2000));
  const warmup = Math.max(0, envInt("RP_BENCH_WARMUP", 200));
  const seed = envInt("RP_BENCH_SEED", 0xc0ffee);

  // One real mGB core (audio + realism); extra cores are kernel-only synthetic ids that still exercise
  // the per-system JS allocation path (ctx/closures/filters) — cores are the dominant JS-load lever.
  const realId = new SystemsStore(be).loadMgb()!;
  expect(typeof realId).toBe("number");
  const ids = [realId];
  for (let i = 1; i < cores; i++) ids.push(realId + i);

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(
    dsp.setSystems({
      project: [{ kind: "midi-routing", config: { mode: 0 } }], // SendToAll → every system's mgb pipeline
      systems: ids.map((id) => ({ id, pipeline: [{ kind: "mgb", config: {} }] })),
    }),
  ).toBeTruthy();

  // Bail early (before the render loops) when the host has no profiling allocator, so the normal
  // test:greenfield-native sweep stays fast + green.
  if (!audio.dspAllocStats().enabled) {
    console.warn("[dsp-bench] instrumentation off — run via `pnpm profile:greenfield` for real metrics. Skipping.");
    expect(true).toBeTruthy();
    return;
  }

  audio.setBpm(140);
  audio.setTransport(true);
  audio.renderAudio(1500); // warm up mGB firmware (discarded)

  const schedule = buildSchedule(profile, warmup + blocks, seed);
  let si = 0;
  const stageForBlock = (b: number): void => {
    while (si < schedule.length && schedule[si].block === b) audio.stageMidiIn(schedule[si++].msg);
  };

  for (let b = 0; b < warmup; b++) {
    stageForBlock(b);
    audio.renderAudio(BLOCK_MS); // warmup blocks — bytecode/atoms/pools settle (discarded)
  }

  audio.dspResetAllocStats(true); // open the window + pin QuickJS auto-GC off (deterministic)
  const t0 = nowMs();
  for (let b = warmup; b < warmup + blocks; b++) {
    stageForBlock(b);
    audio.renderAudio(BLOCK_MS);
  }
  const wallMs = nowMs() - t0;

  const s = audio.dspAllocStats();
  const gc = audio.dspRunGc(); // one self-driven cycle pass — freedBytes ~0 proves no cycle accumulation

  const bc = s.blockCount || 1;
  const round = (x: number): number => Math.round(x * 100) / 100;
  const metrics = {
    profile,
    cores,
    warmupBlocks: warmup,
    blocks: s.blockCount,
    allocsPerBlock: round(s.allocCalls / bc),
    bytesPerBlock: round(s.allocBytes / bc),
    freesPerBlock: round(s.freeCalls / bc),
    liveHeapDelta: s.liveBytesDelta, // ~0 = churn (frees balance allocs); rising = leak
    peakLive: s.peakBytes,
    maxBlockAllocs: s.maxBlockAllocCalls, // the tail — worst single block
    maxBlockBytes: s.maxBlockAllocBytes,
    gcMs: round(gc.ms),
    gcFreedBytes: gc.freedBytes,
    xRT: wallMs > 0 ? round((s.blockCount * BLOCK_MS) / wallMs) : 0,
  };
  console.log("DSP-BENCH " + JSON.stringify(metrics));

  expect(s.blockCount > 0).toBeTruthy();
  expect(s.allocCalls >= 0).toBeTruthy();
});
