// The control-plane client for the native DSP-side JS runtime (a second, bare QuickJS
// context). It compiles a translator to bytecode, loads it, configures it, and runs it per
// block — all over the same globalThis[Symbol.for("plugin")].__rpcSend channel realBackend
// uses. This is a DISTINCT capability from `Backend` (it never joins that interface, so
// MockBackend doesn't grow an unrelated surface — see plans/02-dsp-data-model.md).
//
// The seam is bytes: the script crosses as QuickJS bytecode, config as a byte blob (a JSON
// string in this first cut), per-block MIDI as structured bytes. A JS object never crosses.

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

/** One MIDI event in/out of a block: a sample offset + its raw bytes. */
export interface DspMidiIn {
  frame: number;
  data: Uint8Array;
}
export interface DspMidiOut {
  frame: number;
  data: Uint8Array;
}

/** The per-block context handed to the script (mirrors native AudioBlockInfo). */
export interface DspBlockInfo {
  frames: number;
  sampleRate: number;
  tempo: number;
  ppqPosBlockStart: number;
  transportPlaying: boolean;
}

export interface DspRuntimeClient {
  /** Compile an ES5 translator source to QuickJS bytecode, or null on a compile error. */
  compileScript(source: string): Uint8Array | null;
  /** Instantiate (or hot-reload) the script from its bytecode. */
  loadScript(bytecode: Uint8Array): boolean;
  /** Hand the script a config blob (a JSON string here); it parses once into its slots. */
  setConfig(bytes: Uint8Array): boolean;
  /** Run one block; returns whatever the script emitted via emitMidiOut. */
  runBlock(midi: DspMidiIn[], block: DspBlockInfo): DspMidiOut[];
}

/** Build a DSP-runtime client backed by the native host. Throws if no RPC surface is bound. */
export function createDspRuntime(): DspRuntimeClient {
  const send = resolveSend();
  let nextId = 1;

  const call = (method: string, ...params: unknown[]): unknown => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params }) as Reply | null | undefined;
    if (reply == null) return undefined;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };

  // Binary INPUT crosses as a plain number[] (reflect-cpp's byte reader rejects a typed array);
  // outputs come back as Uint8Array.
  const ints = (b: Uint8Array): number[] => Array.from(b);

  return {
    compileScript: (source) => {
      const r = call("compileScript", source);
      return r == null ? null : (r as Uint8Array);
    },
    loadScript: (bytecode) => call("dspLoadScript", ints(bytecode)) as boolean,
    setConfig: (bytes) => call("dspSetConfig", ints(bytes)) as boolean,
    runBlock: (midi, block) => {
      const midiParam = midi.map((m) => ({ frame: m.frame, data: ints(m.data) }));
      const out = call("dspRunBlock", midiParam, block) as DspMidiOut[];
      return out;
    },
  };
}
