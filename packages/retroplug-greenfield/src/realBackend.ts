// The real native Backend: forwards to the native host's RPC surface over
// globalThis[Symbol.for("plugin")].__rpcSend — the same namespace the future plugin host
// binds, so this one adapter serves both. A self-contained synchronous JSON-RPC client
// (the native side marshals request/response as live JS objects through the QuickJS codec,
// so binary rides Uint8Arrays and nothing is serialized), keeping greenfield dependency-
// free.
//
// Increment 1 wires the emulator-free fs / config / codec methods; the emulator methods
// throw until the stub core lands (increment 2).

import type { Backend, ConstructSpec, FileBrowserOpts, ZipEntry } from "./backend";

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

/** Build a Backend backed by the native host. Throws if no native RPC surface is bound. */
export function createRealBackend(): Backend {
  const send = resolveSend();
  let nextId = 1;

  const call = (method: string, ...params: unknown[]): unknown => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params }) as Reply | null | undefined;
    if (reply == null) return undefined; // notification / no reply
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };

  // A null RPC result (an absent std::optional) maps to null for the nullable byte reads.
  const bytesOrNull = (v: unknown): Uint8Array | null => (v == null ? null : (v as Uint8Array));
  // Binary INPUT crosses as a plain number[] — reflect-cpp's reader rejects a typed array
  // (its byte fields are std::vector<std::uint8_t>). Outputs come back as Uint8Array.
  const ints = (b: Uint8Array): number[] => Array.from(b);

  const notImpl = (name: string): never => {
    throw new Error(`realBackend.${name} is not implemented yet (increment 2 — stub core)`);
  };

  return {
    // --- fs / config / codec (increment 1) --------------------------------
    readFile: (path) => bytesOrNull(call("readFile", path)),
    writeFile: (path, bytes) => call("writeFile", path, ints(bytes)) as boolean,
    writeFileAtomic: (path, bytes) => call("writeFileAtomic", path, ints(bytes)) as boolean,
    fileExists: (path) => call("fileExists", path) as boolean,
    rename: (from, to) => call("rename", from, to) as boolean,
    listDir: (dir) => call("listDir", dir) as string[],
    deleteFile: (path) => call("deleteFile", path) as boolean,
    drainChangedPaths: () => call("drainChangedPaths") as string[],
    canonicalize: (path) => call("canonicalize", path) as string,
    readFilePrefix: (path, length) => bytesOrNull(call("readFilePrefix", path, length)),
    configDir: () => call("configDir") as string,
    zip: (entries: ZipEntry[]) => bytesOrNull(call("zip", entries.map((e) => ({ name: e.name, bytes: ints(e.bytes) })))),
    unzip: (bytes) => (call("unzip", ints(bytes)) as ZipEntry[] | null) ?? null,

    // --- emulator lifecycle / reads (increment 2) -------------------------
    constructSystem: (_spec: ConstructSpec) => notImpl("constructSystem"),
    duplicateSystem: (_srcId, _savPath) => notImpl("duplicateSystem"),
    reloadSystem: (_id) => notImpl("reloadSystem"),
    removeSystem: (_id) => notImpl("removeSystem"),
    applySystemSetting: (_id, _key, _value) => notImpl("applySystemSetting"),
    applyRoleConfig: (_id, _kind, _config) => notImpl("applyRoleConfig"),
    readState: (_id) => notImpl("readState"),
    readSram: (_id) => notImpl("readSram"),

    // --- async dialog (deferred; needs the emit path) ---------------------
    openFileBrowser: (_opts: FileBrowserOpts) => notImpl("openFileBrowser"),
  };
}
