// Typed @rpcpp/client bound to the in-process transport. This is the
// single import the UI uses to reach the C++ PluginRpcService.
//
// The generated `PluginService` interface lives at build/ui/generated/
// PluginService.ts (aliased as "plugin-service") and is regenerated from
// PluginRpcService's OpenRPC schema on every build.

import { createClient, type ClientControl } from "@rpcpp/createClient";
import type { Codec } from "@rpcpp/codec";

import type {
    PluginService,
    PluginRpcServiceSystemEntry,
    PluginRpcServiceFrameResponse,
    PluginRpcServiceOpenRomOpts,
} from "plugin-service";

import { createInProcessTransport } from "./transport";

// The C++ bridge marshals JSON-RPC envelopes as live JS objects (rpcpp's QuickJS
// codec), so the client codec is a passthrough: encode/decode hand the object
// straight through __rpcSend, no serialization. The @rpcpp Codec contract is
// typed for Uint8Array frames, hence the casts. Binary fields (rfl::Bytestring)
// arrive as JS Uint8Arrays from the qjs codec.
const objectCodec: Codec = {
    isBinary: false,
    framing:  "line",
    encode:   (value) => value as unknown as Uint8Array,
    decode:   (frame) => frame,
};

// The frame buffer (rfl::Bytestring) comes back as a Uint8Array from the qjs
// codec, not the `number[]` the JSON-schema-driven codegen infers. Override the
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
    codec:     objectCodec,
});
