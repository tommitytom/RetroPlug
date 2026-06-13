// @retroplug/retroplug — the TypeScript layer over the native core. Currently
// the synchronous in-process RPC client used by the CLI test harness; grows to
// the full client + ergonomics as the restructure proceeds.

export { createSyncClient, type Unpromisify, type RpcSend } from "./createSyncClient";
export { harnessRpcSend } from "./syncTransport";
