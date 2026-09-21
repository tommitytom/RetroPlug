// The synchronous JSON-RPC channel every TS client reaches the native host through, which realBackend,
// dspRuntime and audioDriver each carried their own copy of - resolveSend character-for-character in all
// three, and the caller itself character-for-character in two.
//
// Synchronous is the whole point, and it is why this is not a promise-based client: the DSP kernel and the
// audio driver are driven from code that cannot await, and the native side answers inline
// (QuickJSTransport returns the reply value from the same call). See spec/02-native-host.md.
export type RpcSend = (request: unknown) => unknown;

export interface Reply {
  result?: unknown;
  error?: { code: number; message: string };
}

/** The bound channel, or a throw naming what is missing. Absent in a plain `tjs` process - which is the
 *  mock-backend suite, and is meant to fail loudly rather than silently no-op. */
export function resolveSend(): RpcSend {
  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { __rpcSend?: RpcSend } | undefined;
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}

export type Call = (method: string, ...params: unknown[]) => unknown;

/** A caller over the bound channel. Each client keeps its own id counter; ids are only cosmetic here (the
 *  reply is returned inline), so independent counters across facets are fine. */
export function makeCall(): Call {
  const send = resolveSend();
  let nextId = 1;
  return (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params }) as Reply | null | undefined;
    if (reply == null) return undefined; // notification / no reply
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
}

/** Binary INPUT crosses as a plain number[] — reflect-cpp's byte reader rejects a typed array. */
export const rpcBytes = (b: Uint8Array | number[]): number[] => Array.from(b);
