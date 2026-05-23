// Typed @rpcpp/client bound to the in-process transport. This is the
// single import the UI uses to reach the C++ PluginRpcService.
//
// The generated `PluginService` interface lives at build/ui/generated/
// PluginService.ts (aliased as "plugin-service") and is regenerated from
// PluginRpcService's OpenRPC schema on every build.

import { createClient, type ClientControl } from "@rpcpp/createClient";
import { MsgpackCodec } from "@rpcpp/MsgpackCodec";

import type {
    PluginService,
    PluginRpcServiceSystemEntry,
    PluginRpcServiceFrameResponse,
    PluginRpcServiceOpenRomOpts,
} from "plugin-service";

import { createInProcessTransport } from "./transport";

// msgpack-decoded frame buffer: BIN type comes back as Uint8Array, not
// the `number[]` the JSON-schema-driven codegen infers. Override the
// `buffer` field type so consumers get the actual runtime shape.
export interface FrameResponse extends Omit<PluginRpcServiceFrameResponse, "buffer"> {
    buffer: Uint8Array;
}

// Re-export the generated entry interfaces under shorter aliases.
export type SystemEntry  = PluginRpcServiceSystemEntry;
export type OpenRomOpts  = PluginRpcServiceOpenRomOpts;

export interface RecentFileDto { path: string; kind: string; }

// Tighten the generated service interface where the codegen can't infer
// the actual runtime shape (msgpack BIN → Uint8Array). `getRecentFiles`
// is declared here so the IDE has the symbol before the build regenerates
// PluginService.ts; the generated signature matches this shape.
export type PluginClient = Omit<PluginService, "getFrame"> & {
    getFrame(systemId: number): Promise<FrameResponse | null>;
    getRecentFiles(): Promise<RecentFileDto[]>;
} & ClientControl;

export const plugin: PluginClient = createClient<PluginClient>({
    transport: createInProcessTransport(),
    codec:     new MsgpackCodec(),
});
