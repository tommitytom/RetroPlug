// Resolve the in-process synchronous RPC entry the CLI test runner exposes on
// the embedded QuickJS global (Symbol.for("retroplug").__rpcSend, registered by
// cli/TestHarness.cpp). Pair with createSyncClient.

import type { RpcSend } from "./createSyncClient";

interface HarnessNamespace {
    __rpcSend?: RpcSend;
}

export function harnessRpcSend(): RpcSend {
    const ns = (globalThis as unknown as Record<symbol, HarnessNamespace | undefined>)[
        Symbol.for("retroplug")
    ];
    if (!ns?.__rpcSend) {
        throw new Error("retroplug.__rpcSend not registered (harness bridge missing?)");
    }
    return ns.__rpcSend;
}
