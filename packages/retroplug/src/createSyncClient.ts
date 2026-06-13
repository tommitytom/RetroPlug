// A synchronous JSON-RPC client over an in-process `__rpcSend(bytes) -> bytes`
// hook. The CLI test harness drives the emulator synchronously (it controls
// time via runMs), so — unlike the plugin UI's async @rpcpp/client — tests must
// stay await-free. The native side (TypedRpcServer::processMessage, reached via
// __rpcSend) is synchronous in-process, so this just encodes the request,
// calls through, and decodes the reply inline.
//
// Wire format matches the UI's @rpcpp/MsgpackCodec exactly: raw @msgpack/msgpack
// of the JSON-RPC envelope, no length framing (the in-process path is a single
// request -> reply, not a stream).

import { encode, decode } from "@msgpack/msgpack";

// Unwrap each `(...args) => Promise<R>` method of a generated async service
// interface into its synchronous `(...args) => R` form, reusing the generated
// DTO types verbatim.
export type Unpromisify<T> = {
    [K in keyof T]: T[K] extends (...args: infer A) => Promise<infer R>
        ? (...args: A) => R
        : T[K];
};

export type RpcSend = (bytes: Uint8Array) => Uint8Array | ArrayBuffer | null;

interface RpcReplyEnvelope {
    result?: unknown;
    error?: { code: number; message: string; data?: unknown };
}

export function createSyncClient<T extends object>(rpcSend: RpcSend): Unpromisify<T> {
    let nextId = 1;

    const call = (method: string, params: unknown[]): unknown => {
        const request = { jsonrpc: "2.0", id: nextId++, method, params };
        const reply = rpcSend(encode(request));
        if (reply == null) return undefined; // notification / no reply
        const bytes = reply instanceof Uint8Array ? reply : new Uint8Array(reply);
        const env = decode(bytes) as RpcReplyEnvelope;
        if (env && env.error) {
            const e = new Error(`rpc ${method}: [${env.error.code}] ${env.error.message}`);
            (e as { code?: number }).code = env.error.code;
            throw e;
        }
        return env ? env.result : undefined;
    };

    return new Proxy({}, {
        get(_target, prop) {
            if (typeof prop !== "string") return undefined;
            return (...args: unknown[]) => call(prop, args);
        },
    }) as Unpromisify<T>;
}
