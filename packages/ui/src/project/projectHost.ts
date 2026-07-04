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
import { saveRplg, saveProjectFile, type ProjectHost } from "@retroplug/retroplug/serialization";

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

// The 6-method ProjectHost. applyProjectConfig (the load rebuild) is deferred:
// the plugin load path is async via the CommandQueue and lands in a later
// increment; this increment only wires save/export, which never call it.
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
        throw new Error("applyProjectConfig: plugin load not yet routed through TS (deferred increment)");
    },
};

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
