// The control-plane client for the native DSP-side JS runtime (a second, bare QuickJS context that
// runs the role KERNEL). It compiles the kernel bundle to bytecode, loads it, and pushes the system
// structure — all over the same globalThis[Symbol.for("plugin")].__rpcSend channel realBackend uses.
// This is a DISTINCT capability from `Backend` (it never joins that interface, so MockBackend doesn't
// grow an unrelated surface — see plans/02-dsp-data-model.md).
//
// The seam is bytes: the kernel crosses as QuickJS bytecode, the system structure as a JSON string.
// A JS object never crosses; the per-block drive happens inside the native render loop, not here.

import type { KernelStructure } from "./dspKernel";

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

export interface DspRuntimeClient {
  /** Compile the kernel bundle source to QuickJS bytecode, or null on a compile error. */
  compileScript(source: string): Uint8Array | null;
  /** Instantiate (or hot-reload) the kernel from its bytecode. */
  loadKernel(bytecode: Uint8Array): boolean;
  /** Push the system + pipeline structure; it crosses as a JSON string the kernel parses once. */
  setSystems(struct: KernelStructure): boolean;
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

  // Binary INPUT crosses as a plain number[] (reflect-cpp's byte reader rejects a typed array).
  const ints = (b: Uint8Array): number[] => Array.from(b);

  return {
    compileScript: (source) => {
      const r = call("compileScript", source);
      return r == null ? null : (r as Uint8Array);
    },
    loadKernel: (bytecode) => call("dspLoadKernel", ints(bytecode)) as boolean,
    setSystems: (struct) => call("dspSetSystems", JSON.stringify(struct)) as boolean,
  };
}
