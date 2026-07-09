// The control-plane driver for the native audio-render path: send MIDI into a real core and
// capture its rendered audio. Distinct from `Backend` (in production the HOST drives audio per
// block, not the store), so it stays off that interface — over the same
// globalThis[Symbol.for("plugin")].__rpcSend channel realBackend/dspRuntime use. A dev/test
// facade for driving the emulator headlessly.

type RpcSend = (request: unknown) => unknown;
interface Reply {
  result?: unknown;
  error?: { code: number; message: string };
}

function resolveSend(): RpcSend {
  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { __rpcSend?: RpcSend } | undefined;
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}

/** DSP-runtime allocation counters (spec/08-profiling.md). Fields are DELTAS since the last
 *  dspResetAllocStats(); `enabled` is false unless the host was built with RETROPLUG_PROFILE.
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

export interface AudioDriver {
  /** Enqueue a button transition (button = GameboyButton value; down = press/release). A
   *  press then release around a short render is a tap. */
  pressButton(id: number, button: number, down: boolean): boolean;
  /** Debug: write a system's latest framebuffer to `path` as an RGB24 PNG. */
  screenshot(id: number, path: string): boolean;
  /** Advance the block runner `ms` and return the mixed stereo output, interleaved L,R,L,R…. */
  renderAudio(ms: number): Float32Array;
  setTransport(running: boolean): boolean;
  setBpm(bpm: number): boolean;
  /** Stage a global host-MIDI message for the kernel's next render (consumed on its first block).
   *  The kernel's midi-routing behaviour fans it to systems; with no routing role it reaches none. */
  stageMidiIn(bytes: Uint8Array | number[]): boolean;

  // --- DSP-runtime allocation/GC profiling (spec/08-profiling.md; real only in a RETROPLUG_PROFILE host) ---
  /** Snapshot the DSP JS runtime's allocation counters (deltas since the last reset). */
  dspAllocStats(): DspAllocStats;
  /** Open a fresh measurement window; `disableAutoGc` pins QuickJS auto-GC off for determinism. */
  dspResetAllocStats(disableAutoGc: boolean): boolean;
  /** Run + time a self-driven cycle-collection pass; freedBytes ~0 proves no reference cycles. */
  dspRunGc(): DspGcResult;

  // --- background audio thread (threaded mode) ---
  /** Spawn a real audio thread that free-runs the render loop; DSP-structure edits sent while it
   *  runs (setSystems/loadKernel/stageMidiIn) cross via a lock-free queue applied on that thread.
   *  Construct systems + load the kernel BEFORE this; read core state only AFTER stopAudio. */
  startAudio(): boolean;
  /** Stop and join the audio thread. */
  stopAudio(): boolean;
  /** Block the caller `ms` so the audio thread accumulates a real window (it keeps running). */
  sleepMs(ms: number): boolean;
  /** Monotonic capture snapshot; diff two of these for a windowed RMS = sqrt(Δenergy / Δframes). */
  audioCaptured(): { energy: number; frames: number };
  /** Live count of systems in the Project — safe to read while the audio thread runs (the audio
   *  thread publishes it after each add/remove applied from the command queue). */
  systemCount(): number;
  /** Delete every core the audio thread released (via removeSystem while running), returning the
   *  count freed — the control-thread end of the ownership handoff. */
  drainReleased(): number;
}

/** Build an audio driver backed by the native host. Throws if no RPC surface is bound. */
export function createAudioDriver(): AudioDriver {
  const send = resolveSend();
  let nextId = 1;

  const call = (method: string, ...params: unknown[]): unknown => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params }) as Reply | null | undefined;
    if (reply == null) return undefined;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };

  const ints = (b: Uint8Array | number[]): number[] => Array.from(b);

  return {
    pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
    screenshot: (id, path) => call("screenshot", id, path) as boolean,
    renderAudio: (ms) => {
      const bytes = call("renderAudio", ms) as Uint8Array;
      // Raw interleaved f32; slice() copies to a fresh 4-byte-aligned ArrayBuffer at offset 0.
      return new Float32Array(bytes.slice().buffer);
    },
    setTransport: (running) => call("setTransport", running) as boolean,
    setBpm: (bpm) => call("setBpm", bpm) as boolean,
    stageMidiIn: (bytes) => call("stageMidiIn", ints(bytes)) as boolean,
    dspAllocStats: () => call("dspAllocStats") as DspAllocStats,
    dspResetAllocStats: (disableAutoGc) => call("dspResetAllocStats", disableAutoGc) as boolean,
    dspRunGc: () => call("dspRunGc") as DspGcResult,
    startAudio: () => call("startAudio") as boolean,
    stopAudio: () => call("stopAudio") as boolean,
    sleepMs: (ms) => call("sleepMs", ms) as boolean,
    audioCaptured: () => call("audioCaptured") as { energy: number; frames: number },
    systemCount: () => call("systemCount") as number,
    drainReleased: () => call("drainReleased") as number,
  };
}
