// The real native Backend: forwards to the native host's RPC surface over
// globalThis[Symbol.for("plugin")].__rpcSend — the same namespace the future plugin host
// binds, so this one adapter serves both. A self-contained synchronous JSON-RPC client
// (the native side marshals request/response as live JS objects through the QuickJS codec,
// so binary rides Uint8Arrays and nothing is serialized), keeping greenfield dependency-
// free.
//
// The fs / config / codec methods forward to std::filesystem + miniz; the emulator methods
// (constructSystem / read* / …) drive a StubSystem in a real native Project. Only
// openFileBrowser is unimplemented (async — deferred).

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
    throw new Error(`realBackend.${name} is not implemented (async — deferred)`);
  };

  // ConstructSpec → RPC params: omit null path fields (so native reads nullopt, not "") and
  // send seed bytes as number[] only when present.
  const specParams = (spec: ConstructSpec): Record<string, unknown> => {
    const p: Record<string, unknown> = { romPath: spec.romPath, platform: spec.platform, core: spec.core, embeddedRom: spec.embeddedRom };
    if (spec.savPath != null) p.savPath = spec.savPath;
    if (spec.statePath != null) p.statePath = spec.statePath;
    if (spec.replaceId !== undefined) p.replaceId = spec.replaceId;
    if (spec.sramBytes) p.sramBytes = ints(new Uint8Array(spec.sramBytes));
    if (spec.stateBytes) p.stateBytes = ints(new Uint8Array(spec.stateBytes));
    if (spec.settings != null) p.settings = spec.settings;
    return p;
  };
  const idOrNull = (v: unknown): number | null => (v == null ? null : (v as number));

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

    // --- emulator lifecycle / reads ---------------------------------------
    constructSystem: (spec: ConstructSpec) => idOrNull(call("constructSystem", specParams(spec))),
    duplicateSystem: (srcId, savPath) => idOrNull(call("duplicateSystem", srcId, savPath)),
    reloadSystem: (id) => idOrNull(call("reloadSystem", id)),
    removeSystem: (id) => call("removeSystem", id) as boolean,
    applySystemSetting: (id, key, value) =>
      call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),

    // --- async dialog (deferred; needs the emit path) ---------------------
    openFileBrowser: (_opts: FileBrowserOpts) => notImpl("openFileBrowser"),
  };
}
