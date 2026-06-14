// A synchronous JSON-RPC client over an in-process `__rpcSend(request) ->
// response` hook. The CLI test harness drives the emulator synchronously (it
// controls time via runMs), so — unlike the plugin UI's async @rpcpp/client —
// tests stay await-free. The native side (TypedRpcServer::processMessage, reached
// via __rpcSend) marshals JSON-RPC envelopes as live JS objects through rpcpp's
// QuickJS codec (no serialization), so this just passes the request object
// through and reads the reply object inline. Binary fields (rfl::Bytestring)
// arrive as Uint8Arrays.

// Unwrap each `(...args) => Promise<R>` method of a generated async service
// interface into its synchronous `(...args) => R` form, reusing the generated
// DTO types verbatim.
export type Unpromisify<T> = {
    [K in keyof T]: T[K] extends (...args: infer A) => Promise<infer R>
        ? (...args: A) => R
        : T[K];
};

// In-process dispatch: a JSON-RPC request object in, the response object out
// (null/undefined for a notification / no reply).
export type RpcSend = (request: unknown) => unknown;

interface RpcReplyEnvelope {
    result?: unknown;
    error?: { code: number; message: string; data?: unknown };
}

export function createSyncClient<T extends object>(rpcSend: RpcSend): Unpromisify<T> {
    let nextId = 1;

    const call = (method: string, params: unknown[]): unknown => {
        const request = { jsonrpc: "2.0", id: nextId++, method, params };
        const reply = rpcSend(request) as RpcReplyEnvelope | null | undefined;
        if (reply == null) return undefined; // notification / no reply
        if (reply.error) {
            const e = new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
            (e as { code?: number }).code = reply.error.code;
            throw e;
        }
        return reply.result;
    };

    return new Proxy({}, {
        get(_target, prop) {
            if (typeof prop !== "string") return undefined;
            return (...args: unknown[]) => call(prop, args);
        },
    }) as Unpromisify<T>;
}
