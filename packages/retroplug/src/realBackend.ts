// The real native Backend: forwards to the native host's RPC surface over
// globalThis[Symbol.for("plugin")].__rpcSend — the same namespace the future plugin host
// binds, so this one adapter serves both. A self-contained synchronous JSON-RPC client
// (the native side marshals request/response as live JS objects through the QuickJS codec,
// so binary rides Uint8Arrays and nothing is serialized), keeping the TS side dependency-
// free.
//
// The client is split by CAPABILITY into three factories — host (fs/config/codec/sav + the
// file dialog), emulator (lifecycle / live params / input / reads), and debug (live-core
// inspection) — mirroring the native RPC facets a host binds. All three resolve the SAME
// __rpcSend channel; the split is about which subset a consumer depends on, so a store that
// only needs the filesystem can't reach emulator or debug methods. `createRealBackend`
// recomposes them into the full `Backend`. openFileBrowser is the one async method and rides
// a UI-direct native hook rather than the RPC bridge (see below).

import type { MidiClient } from "@retroplug/core/host";
import type { ApuState, Backend, BreakHitBatch, SetGraphRequest, SetGraphResult, LoadScriptPackRequest, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, ControlPlaneBackend, CoreTransportStats, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, PutResourceResult, ResourceSpec, TraceLine, ZipEntry } from "./backend";
import { makeCall } from "./rpcClient";
import { CapabilityJobs, type JobCompletion, type JobRequest, type JobSubmitResult } from "./capability";
import {
  OfflineJobs,
  type EngineWireRequest,
  type OfflineSubmitResult,
  type OfflineWireDrain,
  type OfflineWireRequest,
} from "./offline";


// --- file dialog (async, UI-direct) ------------------------------------------------------------------
// openFileBrowser is the ONE async Backend method, and it does NOT ride the RPC bridge: it calls the
// __rp_openFileBrowser hook on the shared globalThis and, once settled, gets __rp_onFileBrowserResult back.
// This crosses the control-plane↔UI bundle boundary via globalThis (module singletons do NOT — each bundle
// gets its own). The hook is installed by the UI (the in-app React/LVGL browser — see useFileBrowser) which
// overrides any native host browser; a native OS dialog is an opt-in the UI routes to. Only one browse is
// ever in flight, so one module-level pending slot suffices. Absent (headless harness) → every browse
// resolves null. startDir opens the browser at that directory; directory picks a folder (the render Output Dir).
type OpenBrowserHook = (title: string, patterns: string, saving: boolean, defaultName: string, startDir: string, directory: boolean) => void;

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
    hook(opts.title, opts.patterns.join(" "), !!opts.saving, opts.defaultName ?? "", opts.startDir ?? "", !!opts.directory);
  });
}



// A null RPC result (an absent std::optional) maps to null for the nullable byte reads. Binary
// crosses as a Uint8Array in both directions (the qjs codec decodes a typed byte param straight
// into rfl::Bytestring), so no number[] marshaling is needed either way.
const bytesOrNull = (v: unknown): Uint8Array | null => (v == null ? null : (v as Uint8Array));

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
    syncFile: (path) => call("syncFile", path) as boolean,
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
    openFileBrowser: (opts: FileBrowserOpts) => browseFile(opts), // async UI-direct hook, not RPC (see above)
  };
}

// ResourceSpec → RPC params: exactly one source is present, so the fields the spec does not carry are absent
// (native reads nullopt), not "".
const resourceParams = (spec: ResourceSpec): Record<string, unknown> => {
  const p: Record<string, unknown> = { type: spec.type };
  if ("bytes" in spec) p.bytes = spec.bytes;
  else if ("path" in spec) p.path = spec.path;
  else p.embedded = spec.embedded;
  return p;
};

// Native's completion queue is one per engine, and a runtime binds one engine (globalThis[Symbol.for("plugin")]), so
// every emulator client of a runtime shares one CapabilityJobs: a second client's drain settles the first client's
// promises, where a per-client queue would drop the completions it does not hold. The sharing is per module registry,
// not per QuickJS context, so its precondition is one bundle per engine creating emulator clients: the UI bundle
// borrows the control plane's stores rather than composing its own, and bootSession replans the one engine instead of
// replacing it. A host that rebound the RPC to another engine would hold the old promises' ids, which are the old
// engine's, against the new channel's completions; nothing rebinds today.
let realJobs: CapabilityJobs | null = null;

function sharedJobs(): CapabilityJobs {
  realJobs ??= new CapabilityJobs({
    submitJob: (request: JobRequest) => makeCall()("submitJob", request) as JobSubmitResult,
    drainJobs: () => makeCall()("drainJobs") as JobCompletion[],
  });
  return realJobs;
}

// The offline jobs' queue, graph and engine jobs alike, shared by every emulator client of a runtime for the capability
// jobs' reason: native's completion queue is one per engine, and one drain hands back both kinds.
let realOffline: OfflineJobs | null = null;

function sharedOffline(): OfflineJobs {
  realOffline ??= new OfflineJobs({
    submitOfflineJob: (request: OfflineWireRequest) => makeCall()("submitOfflineJob", request) as OfflineSubmitResult,
    submitEngineJob: (request: EngineWireRequest) => makeCall()("submitEngineJob", request) as OfflineSubmitResult,
    drainOfflineJobs: (waitMs: number) => makeCall()("drainOfflineJobs", waitMs) as OfflineWireDrain,
    cancelOfflineJob: (id: number) => makeCall()("cancelOfflineJob", id) as boolean,
  });
  return realOffline;
}

/** This runtime's offline queue as the `offlineRun` runner the kit compiles take. It is the shared queue
 *  `createEmulatorClient` below uses, not a second one over the same engine: native's completion queue is one per
 *  engine, so two queues would each drain the other's completions. Nothing throws here with no RPC surface bound: the
 *  channel is resolved inside the queue's port lambdas, so the first run rejects with the channel's error rather than
 *  this call throwing. */
export function offlineRunner(): Pick<Backend, "offlineRun"> {
  const offline = sharedOffline();
  return { offlineRun: (request) => offline.run(request) };
}

/** The emulator lifecycle / live params / input / snapshot-reads facet. Throws if no RPC surface is bound. */
export function createEmulatorClient(): EmulatorBackend {
  const call = makeCall();
  const jobs = sharedJobs();
  const offline = sharedOffline();
  return {
    // Native answers { id } or { error }: one optional each way, so a refused put has no id to name.
    putResource: (spec: ResourceSpec): PutResourceResult => {
      const r = call("putResource", resourceParams(spec)) as { id?: number | null; error?: string | null } | null;
      if (r == null || r.id == null) return { error: r?.error ?? "the engine created no resource" };
      return { id: r.id };
    },
    releaseResource: (id: number) => call("releaseResource", id) as boolean,
    setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
    loadScriptPack: (request: LoadScriptPackRequest) => call("loadScriptPack", request) as LoadScriptPackResult,
    setParam: (nodeId, paramId, value) => call("setParam", nodeId, paramId, value) as boolean,
    pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
    stageNodeMidi: (nodeId, bytes) => call("stageNodeMidi", nodeId, bytes) as boolean,
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),
    readRam: (id) => bytesOrNull(call("readRam", id)),
    writeRam: (id, offset, bytes) => call("writeRam", id, offset, bytes) as boolean,
    writeSram: (id, offset, bytes, expect) => call("writeSram", id, offset, bytes, expect ?? new Uint8Array(0)) as boolean,
    capability: (nodeId, op, args, onProgress) => jobs.run(nodeId, op, args, onProgress),
    // One pump for both kinds: the UI tick's, and the one after each submission.
    pumpJobs: () => jobs.pump() + offline.pump(),
    offlineRun: (request) => offline.run(request),
    engineRun: (kind, spec, options) => offline.engine(kind, spec, options),
    rejectPendingJobs: (reason) => jobs.rejectAll(reason),
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
    // Always a Uint8Array, never null: "the ROM has sent nothing" and "this core has no back-channel"
    // are both an empty drain, so a caller can loop over the result without a guard.
    drainCoreBytes: (id) => bytesOrNull(call("drainCoreBytes", id)) ?? new Uint8Array(0),
    // Always an object, never null: a gone id and a core with no transport both read as all-zero, so
    // a caller can subtract two samples without guarding either.
    getCoreTransportStats: (id) =>
      (call("getCoreTransportStats", id) as CoreTransportStats | null | undefined) ??
      { rxDepth: 0, rxCapacity: 0, droppedBytes: 0, wirePending: 0, txDepth: 0, linkFrames: 0, linkResent: 0 },
    loadLabels: (id, path) => call("loadLabels", id, path) as boolean,
    symbolAddress: (id, name) => (call("symbolAddress", id, name) as number | null | undefined) ?? null,
    setCpuRegister: (id, name, value) => call("setCpuRegister", id, name, value) as boolean,
    runUntilPc: (id, target, maxCycles) => call("runUntilPc", id, target, maxCycles) as boolean,
    setBreakpoints: (id, breakpoints: Breakpoint[]) =>
      call("setBreakpoints", id, breakpoints.map((b) => ({ type: b.type, start: b.start, end: b.end ?? 0, condition: b.condition ?? "" }))) as boolean,
    runUntilBreak: (id, maxCycles) => call("runUntilBreak", id, maxCycles) as BreakInfo,
    // Always a batch, never null: a core with no debug target reports "nothing tripped" rather than
    // an absence, so a caller can read .hits without guarding.
    drainBreakHits: (id) =>
      (call("drainBreakHits", id) as BreakHitBatch | null | undefined) ?? { hits: [], overflow: 0 },
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

// The live-MIDI client's shape is core's (@retroplug/core/host), because the host contract's devices group names it;
// re-exported here so every bridge and probe that opens one keeps one import.
export type { MidiClient } from "@retroplug/core/host";

/** @deprecated the input-only name this had before the output half existed. */
export type MidiInputClient = MidiClient;

export function createMidiClient(): MidiClient {
  const call = makeCall();
  return {
    listInputs: () => (call("midiListInputs") as string[] | undefined) ?? [],
    listOutputs: () => (call("midiListOutputs") as string[] | undefined) ?? [],
    open: (clientName, input) => call("midiOpen", clientName, input) as boolean,
    selectOutput: (output) => void call("midiSelectOutput", output),
    poll: () => ((call("midiPoll") as { bytes: Uint8Array }[] | undefined) ?? []).map((m) => m.bytes),
    send: (bytes) => void call("midiSend", bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)),
    close: () => void call("midiClose"),
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
