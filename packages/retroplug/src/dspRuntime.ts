// The control-plane client for the native DSP-side JS runtime (a second, bare QuickJS context that
// runs the role KERNEL). It compiles the kernel bundle to bytecode, loads it, and pushes the system
// structure — all over the same globalThis[Symbol.for("plugin")].__rpcSend channel realBackend uses.
// This is a DISTINCT capability from `Backend` (it never joins that interface, so MockBackend doesn't
// grow an unrelated surface — see plans/02-dsp-data-model.md).
//
// The seam is bytes: the kernel crosses as QuickJS bytecode, the system structure as a JSON string.
// A JS object never crosses; the per-block drive happens inside the native render loop, not here.

import type { KernelStructure } from "./dspKernel";
import { makeCall } from "./rpcClient";


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
  const call = makeCall();

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
