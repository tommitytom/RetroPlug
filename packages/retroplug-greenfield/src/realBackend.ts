// The real native Backend: forwards to the native host's RPC surface over
// globalThis[Symbol.for("plugin")].__rpcSend — the same namespace the future plugin host
// binds, so this one adapter serves both. A self-contained synchronous JSON-RPC client
// (the native side marshals request/response as live JS objects through the QuickJS codec,
// so binary rides Uint8Arrays and nothing is serialized), keeping greenfield dependency-
// free.
//
// The fs / config / codec methods forward to std::filesystem + miniz; the emulator methods
// (constructSystem / read* / …) drive a StubSystem in a real native Project. openFileBrowser is the
// one async method and rides a UI-direct native hook rather than the RPC bridge (see below).

import type { Backend, ConstructSpec, FileBrowserOpts, FrameData, ZipEntry } from "./backend";

type RpcSend = (request: unknown) => unknown;
interface Reply {
  result?: unknown;
  error?: { code: number; message: string };
}

// --- file dialog (async, UI-direct) ------------------------------------------------------------------
// openFileBrowser is the ONE async Backend method, and it does NOT ride the RPC bridge: the editor
// (PluginGreenfieldUI) hangs __rp_openFileBrowser on the shared context — like __rp_setWindowSize — and,
// once the OS dialog settles, calls __rp_onFileBrowserResult back, both on the single UI thread. Only one
// native dialog is ever in flight, so one module-level pending slot suffices (shared across every
// createRealBackend on this context). When the hook is absent (the headless UI harness) the browser is
// inert and every browse resolves null, exactly as the window-size hooks no-op there.
type OpenBrowserHook = (title: string, patterns: string, saving: boolean, defaultName: string) => void;

let pendingBrowse: ((path: string | null) => void) | null = null;
let browseResolverInstalled = false;

function installBrowseResolver(): void {
  if (browseResolverInstalled) return;
  browseResolverInstalled = true;
  (globalThis as Record<string, unknown>).__rp_onFileBrowserResult = (path: string | null): void => {
    const resolve = pendingBrowse;
    pendingBrowse = null;
    resolve?.(path ?? null);
  };
}

function browseFile(opts: FileBrowserOpts): Promise<string | null> {
  const hook = (globalThis as Record<string, unknown>).__rp_openFileBrowser as OpenBrowserHook | undefined;
  if (typeof hook !== "function") return Promise.resolve(null); // no editor window (e.g. the headless harness)
  if (pendingBrowse) return Promise.resolve(null); // one dialog at a time
  installBrowseResolver();
  return new Promise<string | null>((resolve) => {
    pendingBrowse = resolve;
    hook(opts.title, opts.patterns.join(" "), !!opts.saving, opts.defaultName ?? "");
  });
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

  // A null RPC result (an absent std::optional) maps to null for the nullable byte reads. Binary
  // crosses as a Uint8Array in both directions (the qjs codec decodes a typed byte param straight
  // into rfl::Bytestring), so no number[] marshaling is needed either way.
  const bytesOrNull = (v: unknown): Uint8Array | null => (v == null ? null : (v as Uint8Array));

  // ConstructSpec → RPC params: omit null path fields (so native reads nullopt, not "") and pass the
  // seed bytes as a Uint8Array only when present.
  const specParams = (spec: ConstructSpec, id: number): Record<string, unknown> => {
    const p: Record<string, unknown> = { id, romPath: spec.romPath, platform: spec.platform, core: spec.core, embeddedRom: spec.embeddedRom };
    if (spec.savPath != null) p.savPath = spec.savPath;
    if (spec.statePath != null) p.statePath = spec.statePath;
    if (spec.replaceId !== undefined) p.replaceId = spec.replaceId;
    if (spec.sramBytes) p.sramBytes = spec.sramBytes;
    if (spec.stateBytes) p.stateBytes = spec.stateBytes;
    if (spec.settings != null) p.settings = spec.settings;
    return p;
  };

  return {
    // --- fs / config / codec (increment 1) --------------------------------
    readFile: (path) => bytesOrNull(call("readFile", path)),
    writeFile: (path, bytes) => call("writeFile", path, bytes) as boolean,
    writeFileAtomic: (path, bytes) => call("writeFileAtomic", path, bytes) as boolean,
    fileExists: (path) => call("fileExists", path) as boolean,
    rename: (from, to) => call("rename", from, to) as boolean,
    listDir: (dir) => call("listDir", dir) as string[],
    deleteFile: (path) => call("deleteFile", path) as boolean,
    drainChangedPaths: () => call("drainChangedPaths") as string[],
    canonicalize: (path) => call("canonicalize", path) as string,
    readFilePrefix: (path, length) => bytesOrNull(call("readFilePrefix", path, length)),
    configDir: () => call("configDir") as string,
    version: () => call("version") as string,
    zip: (entries: ZipEntry[]) => bytesOrNull(call("zip", entries)), // {name, bytes: Uint8Array} matches BackendZipInput
    unzip: (bytes) => (call("unzip", bytes) as ZipEntry[] | null) ?? null,
    savFromJson: (json) => call("savFromJson", json) as Uint8Array, // Bytestring result → Uint8Array

    // --- emulator lifecycle / reads ---------------------------------------
    constructSystem: (spec: ConstructSpec, id: number) => call("constructSystem", specParams(spec, id)) as boolean,
    removeSystem: (id) => call("removeSystem", id) as boolean,
    applySystemSetting: (id, key, value) =>
      call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
    setAudioRouting: (mode) => call("setAudioRouting", mode) as boolean,
    pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),
    getFrame: (id): FrameData | null => {
      const r = call("getFrame", id) as { width: number; height: number; published: boolean; data?: Uint8Array } | null;
      if (r == null || r.width === 0) return null; // no such system / no framebuffer
      return { width: r.width, height: r.height, published: r.published, pixels: r.data ?? new Uint8Array(0) };
    },

    // --- async dialog (UI-direct native hook; see browseFile above) -------
    openFileBrowser: (opts: FileBrowserOpts) => browseFile(opts),
  };
}
