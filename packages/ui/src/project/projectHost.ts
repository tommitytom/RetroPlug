// Plugin-side ProjectHost adapter: drives the shared @retroplug/retroplug
// save/export orchestration (projectSerialization.ts — the same module the CLI
// test harness runs) over the plugin's native byte-mover primitives.
//
// The plugin UI's normal client is async (@rpcpp/client), but the shared
// ProjectHost interface is synchronous. The underlying bridge entry
// (Symbol.for("plugin").__rpcSend) IS synchronous in-process, so we bind the
// harness's createSyncClient to it for JUST these primitives; the rest of the UI
// keeps its async client untouched. Deep-imports from the transport-free package
// subpaths so this doesn't drag in the harness-only Symbol.for("retroplug")
// transport.

import { createSyncClient, type RpcSend } from "@retroplug/retroplug/sync-client";
import { saveRplg, saveProjectFile, type ProjectHost, type Blob } from "@retroplug/retroplug/serialization";

// The in-process bridge marshals JSON-RPC envelopes as live JS objects (no
// serialization). Binary OUTPUT (rfl::Bytestring) arrives as a Uint8Array;
// binary INPUT travels as number[] (reflect-cpp's reader is int-array-only).
// These two coercions mirror emu.ts's copyU8/toNums.
function copyU8(v: unknown): Uint8Array {
    if (v instanceof Uint8Array) return new Uint8Array(v);          // fresh zero-offset copy
    if (v instanceof ArrayBuffer) return new Uint8Array(v.slice(0));
    if (Array.isArray(v)) return Uint8Array.from(v as number[]);
    throw new Error("projectHost: expected binary value");
}
function toNums(bytes: Uint8Array): number[] {
    return Array.from(bytes);
}

// The primitives we call, in their on-the-wire shape (binary as number[]; at
// runtime the outputs are Uint8Arrays — see copyU8). createSyncClient unwraps
// these into synchronous form.
interface ProjectPrimitives {
    readFile(path: string): Promise<number[]>;
    writeFile(path: string, bytes: number[]): Promise<boolean>;
    zipEntries(entries: { name: string; bytes: number[] }[]): Promise<number[]>;
    unzipEntries(bytes: number[]): Promise<{ name: string; bytes: number[] }[]>;
    snapshotProjectConfig(baseDir: string): Promise<{ config: string; blobs: { name: string; bytes: number[] }[] }>;
    notifyProjectSaved(path: string, exported: boolean): Promise<boolean>;
    fileExists(path: string): Promise<boolean>;
    commitProject(config: string, blobs: { name: string; bytes: number[] }[], path: string): Promise<boolean>;
    // ROM add/load: the UI (romBuild.ts) decides load-vs-add + the sibling-.rplg
    // deferral, then drives this. `romPath` empty + `embeddedRom` set (e.g.
    // "mgb") builds the binary-baked ROM; otherwise the file is slurped +
    // auto-detected natively. No bytes cross — just the path.
    constructSystem(romPath: string, embeddedRom: string, mode: string): Promise<boolean>;
    openRomBrowser(opts: { mode: string }): Promise<boolean>;
}

// Resolve the plugin bridge's synchronous send at call time (the bridge is
// registered before the bundle runs, but stay lazy so a test host can attach
// it late).
const send: RpcSend = (request) => {
    const ns = (globalThis as Record<symbol, { __rpcSend?: RpcSend } | undefined>)[Symbol.for("plugin")];
    if (!ns?.__rpcSend) throw new Error("plugin.__rpcSend not registered — bridge missing?");
    return ns.__rpcSend(request);
};

const rpc = createSyncClient<ProjectPrimitives>(send);

// The ProjectHost (save side). applyProjectConfig — the harness's synchronous
// load rebuild — isn't used by the plugin: the plugin load is async (the DSP
// applies the makeLoadProject command off-thread), so it goes through
// commitProject below instead, and the plugin never calls the shared loadRplg.
export const projectHost: ProjectHost = {
    readFile: (path) => copyU8(rpc.readFile(path)),
    writeFile: (path, bytes) => { rpc.writeFile(path, toNums(bytes)); },
    zipEntries: (entries) =>
        copyU8(rpc.zipEntries(entries.map((e) => ({ name: e.name, bytes: toNums(e.bytes) })))),
    unzipEntries: (bytes) =>
        rpc.unzipEntries(toNums(bytes)).map((e) => ({ name: e.name, bytes: copyU8(e.bytes) })),
    snapshotProjectConfig: (baseDir) => {
        const s = rpc.snapshotProjectConfig(baseDir ?? "");
        return { config: s.config, blobs: s.blobs.map((b) => ({ name: b.name, bytes: copyU8(b.bytes) })) };
    },
    applyProjectConfig: () => {
        throw new Error("applyProjectConfig: the plugin load is async (commitProject) — not used here");
    },
};

// Load-side primitives (used by loadProject.ts). fileExists backs the TS
// missing-files scan; commitProject hands a resolved project to the DSP
// (Command::makeLoadProject) + does the native post-load bookkeeping. Both
// speak the shared Blob shape (Uint8Array) at the boundary.
export function fileExists(path: string): boolean {
    return rpc.fileExists(path);
}
export function commitProject(config: string, blobs: Blob[], path: string): void {
    rpc.commitProject(config, blobs.map((b) => ({ name: b.name, bytes: toNums(b.bytes) })), path);
}

// ROM-build primitives (used by romBuild.ts). constructSystem builds + queues a
// system (LoadRom for "load", AddSystem for "add") on the DSP thread and does
// the native sibling-.rplg + recent bookkeeping for a "load"; openRomBrowser
// opens the native ROM file dialog (its selection returns via the
// "rom-path-selected" event). mode is "load" | "add".
export function constructSystem(romPath: string, embeddedRom: string, mode: string): boolean {
    return rpc.constructSystem(romPath, embeddedRom, mode);
}
export function openRomBrowser(mode: string): void {
    rpc.openRomBrowser({ mode });
}

// Post-write bookkeeping (recent-files + currentProjectPath + the
// project-saved/exported event) that the old C++ saveProjectToPath/exportZipToPath
// did inline. Call after saveRplg/saveProjectFile resolves.
export function notifyProjectSaved(path: string, exported: boolean): void {
    rpc.notifyProjectSaved(path, exported);
}

// Save the live project to `path`, then run the native bookkeeping.
// exported=true → self-contained zip bundle (Export Zip); false → thin,
// path-only `.rplg` (the default Save Project). Synchronous — the whole
// orchestration runs over the in-process bridge.
export function runSave(path: string, exported: boolean): void {
    if (exported) saveRplg(projectHost, path);
    else saveProjectFile(projectHost, path);
    notifyProjectSaved(path, exported);
}
