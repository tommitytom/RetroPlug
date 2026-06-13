// Resolve the in-process synchronous RPC entry the CLI test runner exposes on
// the embedded QuickJS global (Symbol.for("retroplug").__rpcSend, registered by
// cli/TestHarness.cpp). Pair with createSyncClient.

import type { RpcSend } from "./createSyncClient";

interface HarnessNamespace {
    __rpcSend?: RpcSend;
    getArgv?: () => string[];
    exit?: (code: number) => void;
}

function ns(): HarnessNamespace {
    const n = (globalThis as unknown as Record<symbol, HarnessNamespace | undefined>)[
        Symbol.for("retroplug")
    ];
    if (!n) throw new Error("retroplug host namespace not registered (host bridge missing?)");
    return n;
}

export function harnessRpcSend(): RpcSend {
    const n = ns();
    if (!n.__rpcSend) {
        throw new Error("retroplug.__rpcSend not registered (harness bridge missing?)");
    }
    return n.__rpcSend;
}

// The end-user CLI's argument vector (without argv[0]), provided by the host.
export function hostArgv(): string[] {
    const g = ns().getArgv;
    return g ? g() : [];
}

// Report the CLI process exit code to the host.
export function hostExit(code: number): void {
    const e = ns().exit;
    if (e) e(code);
}
