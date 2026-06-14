// In-process Transport for @rpcpp/client. The C++ bridge (PluginJsBridge, via
// dpf.js's JsRpcBridge) exposes one synchronous entrypoint —
// `__rpcSend(request) -> response | null` — which marshals JSON-RPC envelopes as
// live JS objects through rpcpp's QuickJS codec (no serialization), returning the
// inline response object (or null for notifications). Async / out-of-band frames
// arrive as objects through the `engine.emit("rpc-message", obj)` channel; we
// forward them to onFrame the same way StdioTransport would forward bytes.
//
// The @rpcpp Transport contract is typed in terms of Uint8Array frames; here the
// "frames" are plain JS objects (the codec is a passthrough), so the few casts
// below bridge the nominal byte type to the object actually in flight.

import type { Transport, FrameHandler, CloseHandler } from "@rpcpp/transport";
import { on, off } from "lvgljs";

interface PluginNamespace {
    __rpcSend?: (request: unknown) => unknown;
}

const ns = (globalThis as any)[Symbol.for("plugin")] as PluginNamespace | undefined;

export function createInProcessTransport(): Transport {
    if (!ns?.__rpcSend)
        throw new Error("plugin.__rpcSend not registered — bridge missing?");
    const rpcSend = ns.__rpcSend;

    let frameHandler: FrameHandler | undefined;
    let closeHandler: CloseHandler | undefined;
    let closed = false;

    // C++ idle pump fans async/notification frames (response objects) through
    // this channel; forward them straight to the frame handler.
    const onAsync = (msg: unknown) => {
        if (frameHandler) frameHandler(msg as Uint8Array);
    };
    on("rpc-message", onAsync);

    return {
        async send(frame: Uint8Array): Promise<void> {
            if (closed) return;
            // `frame` is the request object (the codec is a passthrough).
            const reply = rpcSend(frame);
            if (reply != null && frameHandler) frameHandler(reply as Uint8Array);
        },
        onFrame(handler: FrameHandler): void { frameHandler = handler; },
        onClose(handler: CloseHandler): void { closeHandler = handler; },
        async close(): Promise<void> {
            if (closed) return;
            closed = true;
            off("rpc-message", onAsync);
            if (closeHandler) closeHandler();
        },
    };
}
