// In-process Transport for @rpcpp/client. The C++ side (PluginJsBridge)
// exposes one synchronous entrypoint — `__rpcSend(Uint8Array) ->
// Uint8Array | null` — which calls TypedRpcServer::processMessage and
// returns the inline response (or null for notifications). Async / out-of-
// band frames arrive through the existing `engine.emit("rpc-message", ...)`
// channel; we forward them to onFrame the same way StdioTransport would.

import type { Transport, FrameHandler, CloseHandler } from "@rpcpp/transport";
import { on, off } from "lvgljs";

interface PluginNamespace {
    __rpcSend?: (bytes: Uint8Array) => Uint8Array | null;
}

const ns = (globalThis as any)[Symbol.for("plugin")] as PluginNamespace | undefined;

export function createInProcessTransport(): Transport {
    if (!ns?.__rpcSend)
        throw new Error("plugin.__rpcSend not registered — bridge missing?");
    const rpcSend = ns.__rpcSend;

    let frameHandler: FrameHandler | undefined;
    let closeHandler: CloseHandler | undefined;
    let closed = false;

    // C++ idle pump fans async/notification frames through this channel as
    // ArrayBuffer payloads. Forward them as Uint8Array to keep the
    // Transport contract consistent.
    const onAsync = (buf: ArrayBuffer | Uint8Array) => {
        if (!frameHandler) return;
        const view = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
        frameHandler(view);
    };
    on("rpc-message", onAsync);

    return {
        async send(bytes: Uint8Array): Promise<void> {
            if (closed) return;
            const reply = rpcSend(bytes);
            if (reply && frameHandler) frameHandler(reply);
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
