// Plugin-specific JSON-RPC bridge to the C++ side. The native bindings
// (rpcSend, rpcPoll) live on globalThis[Symbol.for("plugin")], populated
// by PluginJsBridge — kept off the lvgljs framework namespace.
//
// Sync handlers return their response inline through rpcSend(); async
// handlers return null synchronously and resolve via rpcPoll() once the
// C++ Resolver fires. drainRpc runs on a libuv timer so the polling is
// driven by the same event loop that ticks the JS engine.

interface PluginNative {
    rpcSend(message: string): string | null;
    rpcPoll(): string | null;
}

const plugin = (globalThis as any)[Symbol.for("plugin")] as PluginNative | undefined;

interface PendingCall {
    resolve: (value: any) => void;
    reject: (reason: any) => void;
}

const pending = new Map<string, PendingCall>();
let nextId = 1;

interface RpcEnvelope {
    id?: string | number;
    result?: unknown;
    error?: { code: number; message: string };
}

function settle<T>(env: RpcEnvelope): T {
    if (env.error) throw env.error;
    return env.result as T;
}

export function rpcCall<T = unknown>(method: string, params: unknown[] = []): Promise<T> {
    if (!plugin) {
        return Promise.reject(new Error("plugin RPC bridge is not available"));
    }
    const id = String(nextId++);
    const request = JSON.stringify({ jsonrpc: "2.0", id, method, params });
    const sync = plugin.rpcSend(request);
    if (sync !== null) {
        try {
            return Promise.resolve(settle<T>(JSON.parse(sync) as RpcEnvelope));
        } catch (e) {
            return Promise.reject(e);
        }
    }
    return new Promise<T>((resolve, reject) => {
        pending.set(id, { resolve, reject });
    });
}

function drainRpc() {
    if (!plugin) return;
    while (true) {
        const msg = plugin.rpcPoll();
        if (msg === null) return;
        let env: RpcEnvelope;
        try {
            env = JSON.parse(msg) as RpcEnvelope;
        } catch {
            continue;
        }
        const id = env.id !== undefined ? String(env.id) : undefined;
        if (!id) continue;
        const call = pending.get(id);
        if (!call) continue;
        pending.delete(id);
        try {
            call.resolve(settle(env));
        } catch (e) {
            call.reject(e);
        }
    }
}

if (plugin) {
    setInterval(drainRpc, 16);
}
