// The control-plane driver for the native audio-render path: send MIDI into a real core and
// capture its rendered audio. Distinct from `Backend` (in production the HOST drives audio per
// block, not the store), so it stays off that interface — over the same
// globalThis[Symbol.for("plugin")].__rpcSend channel realBackend uses. A dev/test
// facade for driving the emulator headlessly.


import { makeCall } from "./rpcClient";
import type { EngineGraph } from "./engineGraph";
import type { HostShape } from "./graph/buildGraph";

/** The allocation counters of the script host's audio runtime, the JS the audio thread runs
 *  (spec/08-profiling.md). Fields are DELTAS since the last dspResetAllocStats() (`blockCount` and the
 *  max-block pair count engine blocks); `enabled` is false unless the host was built with RETROPLUG_PROFILE.
 *  Mirrors the native DspAllocStats struct field-for-field (reflect-cpp names). */
export interface DspAllocStats {
  enabled: boolean;
  allocCalls: number;
  reallocCalls: number;
  freeCalls: number;
  allocBytes: number;
  liveBytesDelta: number; // net live-heap change over the window (flat ~0 = churn, rising = leak)
  peakBytes: number;
  blockCount: number;
  maxBlockAllocCalls: number;
  maxBlockAllocBytes: number;
}

/** Result of a self-driven JS_RunGC pass. `freedBytes` ~0 proves the acyclic kernel holds no cycles. */
export interface DspGcResult {
  enabled: boolean;
  ms: number;
  freedBytes: number;
}

/** One recorded timing span (spec/08-profiling.md Tier B). `t0`/`t1` are microseconds relative to the
 *  window base; `label` indexes the parallel `dspTraceNames()` array. Empty off-profile. */
export interface DspTraceSpan {
  label: number;
  t0: number;
  t1: number;
}

// LSDJ kit-compile effects (mirror the native rp::lsdj tagged union; discriminated on `type`). Applied
// per sample: gain (normalize/scale) + filter (RBJ biquad) run before resampling, dither after — matching
// the native ordering. Passed verbatim to the compileKit RPC where reflect-cpp decodes the tagged union.
export type KitGainEffect = { type: "gain"; normalize?: boolean; gain?: number };
export type KitFilterEffect = {
  type: "filter";
  filterType?: "LowPass" | "HighPass" | "BandPass" | "BandStop" | "Peak" | "LowShelf" | "HighShelf" | "AllPass";
  frequency?: number;
  q?: number;
  gain?: number;
};
export type KitDitherEffect = {
  type: "dither";
  ditherType?: "HighPassTPDF" | "ShapedTPDF" | "ErrorDiffusion" | "JJN" | "SierraLite";
};
export type KitEffect = KitGainEffect | KitFilterEffect | KitDitherEffect;

/** One source sample for compileKit: a source audio file (WAV/MP3/FLAC, decoded natively), a 3-char slot
 *  name, an optional source-frame window, and an effect chain. */
export interface KitSampleSpec {
  path: string;
  name: string;
  offset?: number;
  length?: number;
  effects: KitEffect[];
}
/** A whole-kit compile: a 6-char kit name + up to 15 samples. `rotate` = the LSDj 9.2.0+ frame rotation,
 *  set from the target ROM's version (omitted → true / 9.2.0+). */
export interface KitCompileSpec {
  name: string;
  samples: KitSampleSpec[];
  rotate?: boolean;
}

/** One source sample for the risa DMC compiler: a kit sample plus the PAL DPCM playback-rate index (0..15,
 *  default 12), a loop flag, and whether to peak-normalize into the 7-bit domain (default true). */
export interface RisaDmcSampleSpec extends KitSampleSpec {
  rate?: number;
  loop?: boolean;
  normalize?: boolean;
}
/** A whole risa DMC kit compile: a 6-char kit name + up to 16 samples. */
export interface RisaKitCompileSpec {
  name: string;
  samples: RisaDmcSampleSpec[];
}

/** One host block for renderBlocks: what a DAW hands the plugin per process call. */
export interface RenderBlock {
  /** 1..8192. */
  frames: number;
  /** Host MIDI at frames inside the block, staged in list order; frames may tie but never go backwards. */
  midi?: { frame: number; bytes: number[] }[];
  /** Control-surface messages (a Launchpad pad press), staged as stageControllerIn stages them. */
  controller?: number[][];
  /** Transport, applied after the block's queued edits drain and before it renders. */
  playing?: boolean;
  bpm?: number;
  /** A locate (quarter notes). */
  ppq?: number;
}

export interface RenderBlocksResult {
  /** Planar f32, one per output, each the whole run long. */
  lanes: Float32Array[];
  /** The kernel's MIDI out, tagged with the index of the block that emitted it. */
  midiOut: { block: number; system: number; frame: number; data: Uint8Array }[];
}

export interface AudioDriver {
  /** Enqueue a button transition (button = GameboyButton value; down = press/release). A
   *  press then release around a short render is a tap. */
  pressButton(id: number, button: number, down: boolean): boolean;
  /** Debug: write a system's latest framebuffer to `path` as an RGB24 PNG. */
  screenshot(id: number, path: string): boolean;
  /** Render `ms` through the engine and return the mixed stereo output, interleaved L,R,L,R…. */
  renderAudio(ms: number): Float32Array;
  /** Render `ms` through the engine and return EACH live system's own interleaved-stereo output, in
   *  slot order: the per-system isolation that proves LSDj link-cable sync (a follower sounds
   *  only when it actually synced; a healthy two-system mix can't show that). Empty when no systems. */
  renderAudioPerSystem(ms: number): Float32Array[];
  /** Render `ms` through the engine and return system `id`'s per-channel output: one interleaved-stereo
   *  buffer per stream in its `channelLayout()` (Game Boy = 4: Pulse 1, Pulse 2, Wave, Noise). Empty
   *  unless the project holds exactly ONE system (the PerChannel shape fans a single core's channels). */
  renderAudioPerChannel(id: number, ms: number): Float32Array[];
  /** Compile an LSDJ sample kit from source audio files into a 16 KB kit bank (resample to the GB rate +
   *  effects + 4-bit pack, per-sample in parallel natively). A per-sample load failure leaves that slot
   *  empty rather than failing the whole kit — the caller validates by reading the bank back. */
  compileKit(spec: KitCompileSpec): Uint8Array;
  /** Compile a risa NES-DPCM sample kit into an 8 KB kit bank (resample to the PAL DPCM rate + effects +
   *  1-bit ±2 delta pack, per-sample in parallel natively). Same behaviour as compileKit — an unloadable
   *  slot is left empty; the caller validates by reading the bank back. */
  compileDmc(spec: RisaKitCompileSpec): Uint8Array;
  /** The engine's audio sample rate (Hz) — thread into encodeWav so WAV headers are labelled correctly. */
  sampleRate(): number;
  /** Set the host sample rate (Hz). Baked into cores at construct, so it only takes effect BEFORE any
   *  system is built; returns false once a system exists (no resample-on-change) or for a non-positive rate. */
  setSampleRate(sampleRate: number): boolean;
  setTransport(running: boolean): boolean;
  /** Move the host playhead (quarter notes). A DAW locates on every stop-and-rewind, loop wrap, or
   *  timeline click; roles see it as a ppqStart discontinuity. Without this a test can only ever let
   *  ppq accumulate, so "resume where we paused" is expressible but "rewind and play again" is not. */
  setPpq(ppq: number): boolean;
  setBpm(bpm: number): boolean;
  /** Stage global host-MIDI bytes for the kernel's next render (consumed on its first block), at `frame` inside
   *  that block (0 when omitted). Any non-empty length: a channel message, a whole SysEx, or several messages as
   *  one run - the kernel's midi-routing behaviour fans a channel message to its system(s) and broadcasts a longer
   *  run unchanged; with no routing role it reaches none. Messages may be staged out of frame order, as a plugin
   *  stages a block (its notes, then each moved automation slot's CC at frame 0): the engine's MIDI input sorts
   *  them by frame, keeping the staging order on a tie. False, nothing staged, for an empty array or a frame that is
   *  not an integer from 0 to 1023. The frame is where the message lands when that block renders at least 1024
   *  frames (renderAudio's first block is the lesser of 1024 frames and the whole render); a block of
   *  `frame` frames or fewer, a renderBlocks one included, is the caller's to avoid. */
  stageMidiIn(bytes: Uint8Array | number[], frame?: number): boolean;
  /** Stage one CONTROL-SURFACE message (a Launchpad pad press), delivered to the kernel as
   *  `controllerIn` rather than as musical MIDI - so a headless test can drive a controller app with
   *  no controller attached. Separate from stageMidiIn because a pad press is a NoteOn, and a
   *  translator like LSDj's MidiMap would read one on the musical stream as a row launch. */
  stageControllerIn(bytes: Uint8Array | number[]): boolean;
  /** Drain the MIDI-out the DSP kernel emitted (e.g. the LSDj MI.OUT decoder → `emitMidiOut`),
   *  accumulated across the renderAudio blocks since the last drain. Each entry is one MIDI message
   *  with its source system + block-frame. The plugin drains this to the DAW directly; this is the
   *  headless test path. */
  drainMidiOut(): { system: number; frame: number; data: Uint8Array }[];
  /** Render `blocks` as the plugin renders each host block (stage MIDI, drain edits, transport,
   *  processBlock at `outputs` lanes, take the MIDI out), for the parity suite. Throws with the reason
   *  when the request is refused (a frame outside its block, frames going backwards, a bad output count,
   *  or the background audio thread running); nothing renders then. */
  renderBlocks(outputs: 2 | 4 | 6 | 8, blocks: RenderBlock[]): RenderBlocksResult;
  /** The target the host was built for ("linux-x64-gnu13"): which parity baseline it can be held to. */
  buildTarget(): string;
  /** Messages the engine's graph has dropped so far, a full queue on one of its wires each (cumulative, never
   *  reset): what a test holds to zero to know nothing it staged was lost on the way to a core. */
  droppedMessages(): number;

  // --- audio-runtime allocation/GC profiling (spec/08-profiling.md; real only in a RETROPLUG_PROFILE host) ---
  /** Snapshot the audio JS runtime's allocation counters (deltas since the last reset). */
  dspAllocStats(): DspAllocStats;
  /** Open a fresh measurement window; `disableAutoGc` pins QuickJS auto-GC off for determinism. False, doing
   *  nothing, when refused (the background audio thread running) or on a host that is not a profile build. */
  dspResetAllocStats(disableAutoGc: boolean): boolean;
  /** Run + time a self-driven cycle-collection pass; freedBytes ~0 proves no reference cycles. `enabled`
   *  false when refused (the audio thread running) or off-profile. */
  dspRunGc(): DspGcResult;
  /** Open (arm=true) / close (arm=false) a runtime-trace window; while armed, every script node's process,
   *  the engine's stages and the kernel's roles record nested wall-time spans. False, doing nothing, when
   *  refused (the audio thread running: a window armed before startAudio records until stopAudio) or
   *  off-profile. */
  dspTraceReset(arm: boolean): boolean;
  /** The recorded spans (µs, relative to the window base), nested by containment for a flame; a read leaves
   *  them, dspTraceReset clears them. Empty while the audio thread runs, and off-profile. */
  dspTrace(): DspTraceSpan[];
  /** The span label table: `dspTraceNames()[span.label]` is the stage/role name. Empty while the audio
   *  thread runs, and off-profile. */
  dspTraceNames(): string[];

  // --- background audio thread (threaded mode) ---
  /** Spawn a real audio thread that free-runs the render loop; edits sent while it runs (a graph,
   *  stageMidiIn) cross via a lock-free queue applied on that thread. Construct systems BEFORE this;
   *  read core state only AFTER stopAudio. */
  startAudio(): boolean;
  /** Stop and join the audio thread. */
  stopAudio(): boolean;
  /** Block the caller `ms` so the audio thread accumulates a real window (it keeps running). */
  sleepMs(ms: number): boolean;
  /** Monotonic capture snapshot; diff two of these for a windowed RMS = sqrt(Δenergy / Δframes). */
  audioCaptured(): { energy: number; frames: number };
  /** Live count of systems in the running plan - safe to read while the audio thread runs (the audio
   *  thread publishes it after each block's drain has adopted the latest plan). */
  systemCount(): number;
  /** Free every plan the audio thread has handed back, and with each the cores only it held (a system
   *  removed or replaced while running), returning the count of PLANS freed, not cores: one plan can
   *  free several cores, or none. The control-thread end of the ownership handoff. */
  drainReleased(): number;
}

/** Build an audio driver backed by the native host. Throws if no RPC surface is bound.
 *
 *  `graph` is the engine's graph whoever builds the systems publishes (docs/modular-graph-phase-b2-design.md
 *  section 4): each render call renders into the buffers of one host shape, so it sets that shape for the
 *  call and puts the one before it back, and startAudio sets the stereo pair its loop renders. Without one
 *  the calls render whatever shape is planned, and native refuses buffers that do not match it. */
export function createAudioDriver(graph?: EngineGraph): AudioDriver {
  const call = makeCall();

  const ints = (b: Uint8Array | number[]): number[] => Array.from(b);
  const STEREO: HostShape = { kind: "multi", channels: 2 };
  const RENDER_BLOCKS_OUTPUTS: readonly number[] = [2, 4, 6, 8];
  // renderAudio's block: a staged message's frame is below it. Copies kBlockSize (1024), declared in
  // packages/native/src/host/rpc/EngineRpcService.hpp, with nothing tying the two.
  const STAGE_FRAMES = 1024;
  // While this driver's audio thread runs, the render calls refuse natively, so they leave the graph alone. The
  // flag is this driver's own (B1's native render calls checked the engine's audioThreadOwns instead): with a
  // second driver on the same graph, or a raw startAudio RPC, it reads false while a thread runs, and a render
  // call swaps the live plan's shape, for the blocks between its document and the restore, before native refuses
  // the render itself.
  let running = false;
  const shaped = <T>(shape: HostShape, render: () => T, refused: () => T): T => {
    if (!graph || running) return render();
    const out = graph.withShape(shape, render);
    return out === null ? refused() : out;
  };

  return {
    pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
    screenshot: (id, path) => call("screenshot", id, path) as boolean,
    renderAudio: (ms) =>
      shaped(
        STEREO,
        () => {
          const bytes = call("renderAudio", ms) as Uint8Array;
          // Raw interleaved f32; slice() copies to a fresh 4-byte-aligned ArrayBuffer at offset 0.
          return new Float32Array(bytes.slice().buffer);
        },
        () => new Float32Array(0),
      ),
    renderAudioPerSystem: (ms) =>
      shaped(
        { kind: "perSystem" },
        () => {
          const bufs = call("renderAudioPerSystem", ms) as Uint8Array[];
          // One interleaved-f32 buffer per system; slice() 4-byte-aligns each like the mixed path above.
          return bufs.map((b) => new Float32Array(b.slice().buffer));
        },
        () => [],
      ),
    renderAudioPerChannel: (id, ms) =>
      shaped(
        { kind: "perChannel" },
        () => {
          const bufs = (call("renderAudioPerChannel", id, ms) as Uint8Array[] | null) ?? [];
          // One interleaved-f32 buffer per channel stream; slice() 4-byte-aligns each.
          return bufs.map((b) => new Float32Array(b.slice().buffer));
        },
        () => [],
      ),
    compileKit: (spec) => call("compileKit", spec) as Uint8Array, // the 16 KB kit bank, straight through
    compileDmc: (spec) => call("compileDmc", spec) as Uint8Array, // the 8 KB risa DMC kit bank
    sampleRate: () => call("sampleRate") as number,
    setSampleRate: (sampleRate) => call("setSampleRate", sampleRate) as boolean,
    setTransport: (running) => call("setTransport", running) as boolean,
    setPpq: (ppq) => call("setPpq", ppq) as boolean,
    setBpm: (bpm) => call("setBpm", bpm) as boolean,
    // A bad frame is refused here, as native refuses one at 1024 or past: the RPC reads a frame as an integer,
    // truncated to its low 32 bits (1.5 is 1, NaN and -Infinity are 0, 2^32 + 5 is 5), so native cannot tell those
    // from a good one.
    stageMidiIn: (bytes, frame = 0) =>
      Number.isInteger(frame) && frame >= 0 && frame < STAGE_FRAMES
        ? (call("stageMidiIn", ints(bytes), frame) as boolean)
        : false,
    stageControllerIn: (bytes) => call("stageControllerIn", ints(bytes)) as boolean,
    drainMidiOut: () =>
      (call("drainMidiOut") as { system: number; frame: number; data: Uint8Array }[] | null) ?? [],
    renderBlocks: (outputs, blocks) => {
      // Refused here as native refuses it (renderBlocksRefusal), before the shape is touched: a count native
      // never renders would otherwise plan a throwaway document, or fail as a shape that did not plan.
      if (!RENDER_BLOCKS_OUTPUTS.includes(outputs)) throw new Error("renderBlocks: outputs must be 2, 4, 6 or 8");
      return shaped({ kind: "multi", channels: outputs }, () => renderBlocks(outputs, blocks), () => {
        throw new Error(`renderBlocks: the ${outputs}-output host shape did not plan`);
      });
    },
    buildTarget: () => call("buildTarget") as string,
    droppedMessages: () => call("droppedMessages") as number,
    dspAllocStats: () => call("dspAllocStats") as DspAllocStats,
    dspResetAllocStats: (disableAutoGc) => call("dspResetAllocStats", disableAutoGc) as boolean,
    dspRunGc: () => call("dspRunGc") as DspGcResult,
    dspTraceReset: (arm) => call("dspTraceReset", arm) as boolean,
    dspTrace: () => call("dspTrace") as DspTraceSpan[],
    dspTraceNames: () => call("dspTraceNames") as string[],
    startAudio: () => {
      if (graph && !running && !graph.setShape(STEREO)) return false;
      const ok = call("startAudio") as boolean;
      if (ok) running = true;
      return ok;
    },
    stopAudio: () => {
      const ok = call("stopAudio") as boolean;
      if (ok) running = false;
      return ok;
    },
    sleepMs: (ms) => call("sleepMs", ms) as boolean,
    audioCaptured: () => call("audioCaptured") as { energy: number; frames: number },
    systemCount: () => call("systemCount") as number,
    drainReleased: () => call("drainReleased") as number,
  };

  function renderBlocks(outputs: number, blocks: RenderBlock[]): RenderBlocksResult {
    // Every field is sent, absent ones as null/[]: the native struct has no field defaults.
    const req = {
      outputs,
      blocks: blocks.map((b) => ({
        frames: b.frames,
        midi: (b.midi ?? []).map((m) => ({ frame: m.frame, bytes: Array.from(m.bytes) })),
        controller: (b.controller ?? []).map((c) => Array.from(c)),
        playing: b.playing ?? null,
        bpm: b.bpm ?? null,
        ppq: b.ppq ?? null,
      })),
    };
    const r = call("renderBlocks", req) as {
      error: string;
      lanes: Uint8Array[];
      midiOut: { block: number; system: number; frame: number; data: Uint8Array }[];
    };
    if (r.error) throw new Error(`renderBlocks: ${r.error}`);
    // slice() copies each lane to a fresh 4-byte-aligned buffer, as renderAudio does.
    return { lanes: r.lanes.map((l) => new Float32Array(l.slice().buffer)), midiOut: r.midiOut };
  }
}
