// The real native Backend: forwards to the native host's RPC surface over
// globalThis[Symbol.for("plugin")].__rpcSend — the same namespace the future plugin host
// binds, so this one adapter serves both. A self-contained synchronous JSON-RPC client
// (the native side marshals request/response as live JS objects through the QuickJS codec,
// so binary rides Uint8Arrays and nothing is serialized), keeping the TS side dependency-
// free.
//
// The client is split by CAPABILITY into three factories — host (fs/config/codec/sav + the
// file dialog), emulator (lifecycle / live config / input / reads), and debug (live-core
// inspection) — mirroring the native RPC facets a host binds. All three resolve the SAME
// __rpcSend channel; the split is about which subset a consumer depends on, so a store that
// only needs the filesystem can't reach emulator or debug methods. `createRealBackend`
// recomposes them into the full `Backend`. openFileBrowser is the one async method and rides
// a UI-direct native hook rather than the RPC bridge (see below).

import type { ApuState, Backend, BreakInfo, Breakpoint, CallFrame, ConstructSpec, ControlPlaneBackend, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "./backend";
import { savFromJson as savFromJsonTs } from "./lsdj";

type RpcSend = (request: unknown) => unknown;
interface Reply {
  result?: unknown;
  error?: { code: number; message: string };
}

// --- file dialog (async, UI-direct) ------------------------------------------------------------------
// openFileBrowser is the ONE async Backend method, and it does NOT ride the RPC bridge: it calls the
// __rp_openFileBrowser hook on the shared globalThis and, once settled, gets __rp_onFileBrowserResult back.
// This crosses the control-plane↔UI bundle boundary via globalThis (module singletons do NOT — each bundle
// gets its own). The hook is installed by the UI (the in-app React/LVGL browser — see useFileBrowser) which
// overrides any native host browser; a native OS dialog is an opt-in the UI routes to. Absent (headless
// harness) → every browse resolves null.
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
  if (typeof hook !== "function") return Promise.resolve(null); // no browser installed (e.g. the headless harness)
  if (pendingBrowse) return Promise.resolve(null); // one at a time
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

// A synchronous JSON-RPC caller over the bound channel. Each client keeps its own id counter; ids are
// only cosmetic here (the reply is returned inline), so independent counters across facets are fine.
type Call = (method: string, ...params: unknown[]) => unknown;
function makeCall(): Call {
  const send = resolveSend();
  let nextId = 1;
  return (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params }) as Reply | null | undefined;
    if (reply == null) return undefined; // notification / no reply
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
}

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
  if (spec.romBytes) p.romBytes = spec.romBytes;
  if (spec.sramBytes) p.sramBytes = spec.sramBytes;
  if (spec.stateBytes) p.stateBytes = spec.stateBytes;
  if (spec.settings != null) p.settings = spec.settings;
  return p;
};

/** The fs / config / codec / sav facet + the async file dialog. Throws if no native RPC surface is bound. */
export function createHostClient(): HostBackend {
  const call = makeCall();
  return {
    readFile: (path) => bytesOrNull(call("readFile", path)),
    writeFile: (path, bytes) => call("writeFile", path, bytes) as boolean,
    writeFileAtomic: (path, bytes) => call("writeFileAtomic", path, bytes) as boolean,
    appendFile: (path, bytes) => call("appendFile", path, bytes) as boolean,
    writeFileAt: (path, offset, bytes) => call("writeFileAt", path, offset, bytes) as boolean,
    fileExists: (path) => call("fileExists", path) as boolean,
    rename: (from, to) => call("rename", from, to) as boolean,
    listDir: (dir) => call("listDir", dir) as string[],
    deleteFile: (path) => call("deleteFile", path) as boolean,
    drainChangedPaths: () => call("drainChangedPaths") as string[],
    setWatchedRoms: (paths) => void call("setWatchedRoms", paths),
    canonicalize: (path) => call("canonicalize", path) as string,
    readFilePrefix: (path, length) => bytesOrNull(call("readFilePrefix", path, length)),
    configDir: () => call("configDir") as string,
    version: () => call("version") as string,
    zip: (entries: ZipEntry[]) => bytesOrNull(call("zip", entries)), // {name, bytes: Uint8Array} matches BackendZipInput
    unzip: (bytes) => (call("unzip", bytes) as ZipEntry[] | null) ?? null,
    pngEncode: (width, height, rgba) => bytesOrNull(call("pngEncode", { width, height, rgba })), // PngImage DTO
    pngDecode: (bytes) => (call("pngDecode", bytes) as PngImageData | null) ?? null,
    savFromJson: (json) => savFromJsonTs(json), // pure-TS codec (was a native RPC round-trip)
    openFileBrowser: (opts: FileBrowserOpts) => browseFile(opts), // async UI-direct hook, not RPC (see above)
  };
}

/** The emulator lifecycle / live config / input / snapshot-reads facet. Throws if no RPC surface is bound. */
export function createEmulatorClient(): EmulatorBackend {
  const call = makeCall();
  return {
    constructSystem: (spec: ConstructSpec, id: number) => call("constructSystem", specParams(spec, id)) as boolean,
    removeSystem: (id) => call("removeSystem", id) as boolean,
    applySystemSetting: (id, key, value) =>
      call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
    setSerialOutCapture: (id, on) => call("setSerialOutCapture", id, on) as boolean,
    setAudioRouting: (mode) => call("setAudioRouting", mode) as boolean,
    pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),
    readRam: (id) => bytesOrNull(call("readRam", id)),
    getFrame: (id): FrameData | null => {
      const r = call("getFrame", id) as { width: number; height: number; published: boolean; data?: Uint8Array } | null;
      if (r == null || r.width === 0) return null; // no such system / no framebuffer
      return { width: r.width, height: r.height, published: r.published, pixels: r.data ?? new Uint8Array(0) };
    },
  };
}

/** The live-core debug facet (spec/09-cli-debugging.md). Throws if no RPC surface is bound.
 *  Field-for-field mirrors of the native reflect-cpp structs → a direct cast (the DspAllocStats pattern). */
export function createDebugClient(): DebugBackend {
  const call = makeCall();
  return {
    getApuState: (id) => call("getApuState", id) as ApuState,
    getExpansionAudioState: (id) => call("getExpansionAudioState", id) as ExpansionAudioState,
    getPpuState: (id) => call("getPpuState", id) as PpuState,
    readCpu: (id, addr) => call("readCpu", id, addr) as number | null,
    writeCpu: (id, addr, value) => call("writeCpu", id, addr, value) as boolean,
    readMemory: (id, region) => bytesOrNull(call("readMemory", id, region)),
    getCpuRegisters: (id) => call("getCpuRegisters", id) as CpuRegister[],
    stepInstruction: (id) => Number(call("stepInstruction", id)),
    drainEvents: (id) => call("drainEvents", id) as DebugEvent[],
    loadLabels: (id, path) => call("loadLabels", id, path) as boolean,
    setCpuRegister: (id, name, value) => call("setCpuRegister", id, name, value) as boolean,
    runUntilPc: (id, target, maxCycles) => call("runUntilPc", id, target, maxCycles) as boolean,
    setBreakpoints: (id, breakpoints: Breakpoint[]) =>
      call("setBreakpoints", id, breakpoints.map((b) => ({ type: b.type, start: b.start, end: b.end ?? 0, condition: b.condition ?? "" }))) as boolean,
    runUntilBreak: (id, maxCycles) => call("runUntilBreak", id, maxCycles) as BreakInfo,
    setTrace: (id, on) => call("setTrace", id, on) as boolean,
    readTrace: (id, count) => call("readTrace", id, count) as TraceLine[],
    stepInto: (id) => call("stepInto", id) as BreakInfo,
    stepOver: (id) => call("stepOver", id) as BreakInfo,
    stepOut: (id) => call("stepOut", id) as BreakInfo,
    beginProfile: (id) => call("beginProfile", id) as boolean,
    readProfile: (id) => call("readProfile", id) as ProfiledFunction[],
    disassemble: (id, addr, count) => call("disassemble", id, addr, count) as DisasmLine[],
    getCallStack: (id) => call("getCallStack", id) as CallFrame[],
  };
}

/** Build the native Backend. By default all three facets (the CLI's full surface); pass `{debug:false}`
 *  for the control-plane surface (fs + emulator only) the plugin/UI store graph runs on. Throws if no
 *  native RPC surface is bound. */
export function createRealBackend(opts: { debug: false }): ControlPlaneBackend;
export function createRealBackend(opts?: { debug?: boolean }): Backend;
export function createRealBackend(opts: { debug?: boolean } = {}): Backend | ControlPlaneBackend {
  const base = { ...createHostClient(), ...createEmulatorClient() };
  return opts.debug === false ? base : { ...base, ...createDebugClient() };
}
