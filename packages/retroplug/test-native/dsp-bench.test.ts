// DSP-thread allocation benchmark (spec/08-profiling.md). Drives the real script nodes (the MIDI routing and an
// rp.mgb.midi a system) off the RT thread via the deterministic renderAudio pull path under a seeded mGB + heavy-MIDI
// workload, and reports what the script host's audio runtime (the bare QuickJS runtime they run in) allocates PER
// BLOCK: the refcount-churn signal (QuickJS frees acyclic garbage immediately, so "GC" is rare; malloc/free churn
// per block is the real cost). Then a window each on a real LSDj, playing, whose rp.lsdj.serial-out decodes its serial
// out to host MIDI, in master sync and in MI.OUT: those need the fixture tree's LSDj ROMs (RETROPLUG_RESOURCES_DIR),
// and without them the case SKIPs there, after the mGB window has been held to its zero.
//
// The allocation counters exist only in a RETROPLUG_PROFILE host (built by `pnpm profile` /
// tools/run-profile.sh). Under the default host the counters report enabled:false and this
// test SKIPS (so the normal `pnpm test:native` sweep stays fast, and says the case did not run
// rather than printing a green tick for it). One test per file (the
// native Engine is shared per host process).
import { test, expect, skip } from "../testing/harness";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec, SerialOutCapture } from "../src/backend";
import { directGraph } from "../src/graph/directGraph";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { createRealBackend } from "../src/realBackend";

declare const __RESOURCES_DIR__: string;

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

// The embedded mGB, built bare (no roles of its own): the pipeline set here is its one protocol node.
const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

// The LSDj windows' ROMs (the fixture tree's) and songs, as lsdj-mastersync.test.ts and lsdj-midiout.test.ts author
// them: a stock LSDj in SYNC=LSDj playing a one-note phrase, and an Arduinoboy build in SYNC=MI.OUT playing eight
// notes, each with an N command, which MI.OUT sends as a NoteOn.
const LSDJ_ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const LSDJ_ABOY_ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const LSDJ_START = 7; // GameboyButton::Start
const MASTER_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};
const MIDIOUT_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiOut" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [
      {
        notes: [1, 13, 25, 1, 13, 25, 1, 13],
        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
      },
    ],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};

const SR = 44100;
const BLK = 1024;
const DEFAULT_WARMUP = 200; // blocks discarded before the window; the zero-allocation assertion needs at least this
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
// chord bursts + arp (per-block bursts). C: synthetic worst case (~700 ev/s): floods the routing and the FIFO.
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

test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI workload", () => {
  const audio = createAudioDriver();

  const profile = envStr("RP_BENCH_PROFILE", "A");
  const cores = Math.max(1, envInt("RP_BENCH_CORES", 1));
  const blocks = Math.max(1, envInt("RP_BENCH_BLOCKS", 2000));
  const warmup = Math.max(0, envInt("RP_BENCH_WARMUP", DEFAULT_WARMUP));
  const seed = envInt("RP_BENCH_SEED", 0xc0ffee);

  // `cores` real mGB cores, each with an mgb pipeline: cores are the dominant JS-load lever (each one a
  // protocol node and a routing output). The graph places only real systems, so each is a real core.
  const ids: number[] = [];
  for (let i = 1; i <= cores; i++) {
    expect(graph.add(MGB, i)).toBeTruthy();
    expect(graph.setPipeline(i, [{ kind: "mgb", config: {} }])).toBeTruthy();
    ids.push(i);
  }
  // SendToAll → every system's mgb pipeline.
  expect(graph.setMidiRouting("sendToAll")).toBeTruthy();

  // Bail early (before the render loops) when the host has no profiling allocator, so the normal
  // test:native sweep stays fast + green.
  if (!audio.dspAllocStats().enabled)
    skip("dsp-bench: allocation instrumentation off — run via `pnpm profile` for real metrics");

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
  // One self-driven cycle pass. Auto-GC was pinned off for the window, and the bundle's load leaves no cycles (it left
  // about 144 KiB while zod and the DSP kernel were in it, which the first setGraph's reclaim collected before the
  // window opened): what this frees, the window's blocks left in cycles, so freedBytes ~0 says they accumulate none.
  const gc = audio.dspRunGc();

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

  expect(s.blockCount, "blocks measured").toBeGreaterThan(0);
  // A warm window of the protocol nodes allocates nothing, and the headline is held: a count of 0 here means the
  // window counted and found none only if the window is live, which the short window after the trace proves below (the
  // runtime's peak heap cannot: it is never reset, and the bundle's load alone puts ~300 KB in it before this window).
  // The zero holds at the default warmup only: a shorter one (RP_BENCH_WARMUP) leaves pool growth in the window, so
  // then the figure is printed and not asserted.
  if (warmup >= DEFAULT_WARMUP) {
    expect(s.allocCalls, "allocations in the measured window").toBe(0);
    expect(s.maxBlockAllocCalls, "allocations in the worst block").toBe(0);
  }

  // --- optional per-role runtime trace (spec/08-profiling.md Tier B) --------------------------------
  // A separate SHORT armed window AFTER the alloc measurement (so those numbers stay pristine). Records nested
  // wall-time spans (per block: apu-render, the plan's whole run, holding each script node's process, a span named by
  // its type, rp.midi.route then each rp.mgb.midi, which holds marshal and then js-call, which holds a marshal per
  // input it reads; then state-publish) and prints ONE Chrome trace-event JSON line the wrapper captures into
  // build-prof/trace.json (load in ui.perfetto.dev or speedscope). Gated on RP_BENCH_TRACE so the normal stats run
  // stays a single line.
  if (env("RP_BENCH_TRACE")) {
    const traceBlocks = Math.max(1, envInt("RP_BENCH_TRACE_BLOCKS", 64));
    const tSched = buildSchedule(profile, traceBlocks, seed ^ 0x9e3779b9);
    let ti = 0;
    const stageTrace = (blk: number): void => {
      while (ti < tSched.length && tSched[ti].block === blk) audio.stageMidiIn(tSched[ti++].msg);
    };
    audio.dspTraceReset(true); // arm: the script host and the engine record spans
    for (let blk = 0; blk < traceBlocks; blk++) {
      stageTrace(blk);
      audio.renderAudio(BLOCK_MS);
    }
    const spans = audio.dspTrace();
    const names = audio.dspTraceNames();
    audio.dspTraceReset(false); // disarm
    const traceEvents = spans.map((sp) => ({
      name: names[sp.label] ?? `#${sp.label}`,
      ph: "X",
      ts: sp.t0, // µs, relative to the window base
      dur: sp.t1 - sp.t0,
      pid: 1,
      tid: 1,
    }));
    console.log("DSP-TRACE " + JSON.stringify({ traceEvents, displayTimeUnit: "ms" }));
  }

  // The window is live: a block known to allocate on the audio runtime shows up in a fresh one. Adding a system and
  // its mgb role places a protocol node; GraphHost::publish adopts the plan at once in this pull path, so the node's
  // createJs runs inside graph.add / setPipeline, before any block (counted in allocCalls, not in maxBlockAllocCalls).
  // What lands inside the block is fillMessages making each fresh port's reused Message[] and its pool the first time
  // a node calls messages() (ScriptHost.cpp): for the new rp.mgb.midi, and for rp.midi.route, whose reshape freed its
  // port arrays when its outputs grew. Pre-making those arrays at create time would move that allocation out of the
  // block, and this assertion would then need another trigger. The zero above is only worth printing if this can fail.
  const extra = cores + 1;
  const expectWindowLive = (): void => {
    audio.dspResetAllocStats(true);
    expect(graph.add(MGB, extra)).toBeTruthy();
    expect(graph.setPipeline(extra, [{ kind: "mgb", config: {} }])).toBeTruthy();
    audio.renderAudio(BLOCK_MS);
    const live = audio.dspAllocStats();
    expect(live.blockCount, "blocks in the proof window").toBeGreaterThan(0);
    expect(live.allocCalls, "a block whose fresh ports fill their message arrays allocates").toBeGreaterThan(0);
    expect(live.maxBlockAllocCalls, "and the worst-block counter sees it").toBeGreaterThan(0);
    graph.remove(extra);
  };
  expectWindowLive();

  // --- an LSDj decoding its serial out to host MIDI, playing ------------------------------------------------------
  // The lsdj-sync role's two MIDI-out modes run their decoders, in rp.lsdj.serial-out after the Game Boy, only on what
  // a playing LSDj clocks out, so each has a window of its own on a real LSDj, the mGB systems gone: master sync on a
  // stock LSDj in SYNC=LSDj (a byte a 24-PPQN tick, which the core captures in `listen`, as lsdj-mastersync.test.ts
  // runs it), and MI.OUT on an Arduinoboy build in SYNC=MI.OUT (its clock and the N commands' NoteOns, captured in
  // `drive`, as lsdj-midiout.test.ts runs it). Each window is held to the mGB window's zero, the clocks its decoder
  // sent say LSDj played through it, and the same proof as above says the window counted.
  for (const id of ids) graph.remove(id);
  const be = createRealBackend();
  const lsdjBlocks = Math.min(blocks, 400);
  const lsdjWindow = (
    workload: string,
    romPath: string,
    song: SavInput,
    mode: string,
    capture: SerialOutCapture,
    minClocks: number,
  ): void => {
    if (!be.fileExists(romPath)) skip(`dsp-bench: the ${workload} window needs the LSDj ROM at ${romPath}`);
    const id = extra + 1;
    const spec: ConstructSpec = { romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null };
    expect(graph.add({ ...spec, sramBytes: savFrom(song) }, id)).toBeTruthy();
    expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode } }])).toBeTruthy();
    expect(be.setSerialOutCapture(id, capture)).toBeTruthy(); // what the store does for the mode
    audio.renderAudio(6000); // the song screen
    audio.pressButton(id, LSDJ_START, true); // play
    audio.renderAudio(80);
    audio.pressButton(id, LSDJ_START, false);
    for (let b = 0; b < warmup; b++) audio.renderAudio(BLOCK_MS);
    audio.drainMidiOut();
    audio.dspResetAllocStats(true);
    for (let b = 0; b < lsdjBlocks; b++) audio.renderAudio(BLOCK_MS);
    const w = audio.dspAllocStats();
    const status = audio.drainMidiOut().map((m) => m.data[0]);
    const clocks = status.filter((st) => st === 0xf8).length;
    const notes = status.filter((st) => (st & 0xf0) === 0x90).length;
    const wb = w.blockCount || 1;
    console.log(
      "DSP-BENCH " +
        JSON.stringify({
          workload,
          warmupBlocks: warmup,
          blocks: w.blockCount,
          allocsPerBlock: round(w.allocCalls / wb),
          bytesPerBlock: round(w.allocBytes / wb),
          maxBlockAllocs: w.maxBlockAllocCalls,
          maxBlockBytes: w.maxBlockAllocBytes,
          midiOut: status.length,
          clocks,
          noteOns: notes,
        }),
    );
    expect(w.blockCount, `${workload}: blocks measured`).toBe(lsdjBlocks);
    // LSDj played through the window: its decoder sent more clocks than its tempo sends in half of it (in master sync
    // about 51 a second at tempo 128, a byte a 24-PPQN tick, against the 40 asked; in MI.OUT about 4, against 2).
    expect(clocks, `${workload}: clocks decoded in the window`).toBeGreaterThan(minClocks);
    if (warmup >= DEFAULT_WARMUP) {
      expect(w.allocCalls, `${workload}: allocations in the measured window`).toBe(0);
      expect(w.maxBlockAllocCalls, `${workload}: allocations in the worst block`).toBe(0);
    }
    graph.remove(id);
    expectWindowLive();
  };
  const seconds = (lsdjBlocks * BLOCK_MS) / 1000;
  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", "listen", 40 * seconds);
  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", "drive", 2 * seconds);
});
