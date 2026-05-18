// Live emulator-memory subscription + one-shot read helpers.
//
// Wire shape (matches src/system/MemoryType.hpp). The integer values are
// passed straight to plugin.subscribeMemory / plugin.getMemory so they MUST
// stay in sync with the C++ enum.

import { useEffect, useState } from "react";

import { plugin } from "./client";

export enum MemoryType {
    Ram          = 0,
    Rom          = 1,
    Sram         = 2,
    Vram         = 3,
    IORegisters  = 4,
    HRam         = 5,
    OAM          = 6,
    NametableRam = 7,
    ExtWorkRam   = 8,
}

export interface MemorySnapshot {
    bytes:   Uint8Array;
    version: number;
}

// Wire shape of the `"memory"` JSON-RPC notification pushed from the C++
// DSP. Mirrors `MemoryNotificationPayload` in src/PluginJsBridge.cpp.
// Fields arrive as a structured msgpack map; `bytes` rides msgpack BIN
// (decoded as Uint8Array by MsgpackCodec on this side).
interface MemoryNotification {
    systemId: number;
    type:     number;
    bytes:    Uint8Array;
    version:  number;
}

/**
 * Subscribe to live snapshots of one emulator memory region. The DSP
 * publishes a tear-free snapshot per audio block (end-of-block = internally
 * consistent state); the UI thread reads from a triple-buffer, hashes for
 * dedup, and re-renders this hook only when the bytes have actually
 * changed.
 *
 * `hz` caps the per-sub emit rate. 0 means uncapped (one emit per uiIdle).
 *
 * Returns null until the first snapshot arrives. Returns null permanently
 * if the type isn't supported on the target system or the region exceeds
 * the streamable size cap (currently 64 KiB — ROM / GBA EWRAM / large SRAM
 * are one-shot only; use {@link getMemoryOnce} for those).
 */
export function useMemory(
    systemId: number,
    type: MemoryType,
    hz = 60,
): MemorySnapshot | null {
    const [snapshot, setSnapshot] = useState<MemorySnapshot | null>(null);

    useEffect(() => {
        let cancelled = false;

        // Server-pushed notification via rpcpp. The C++ side calls
        // RpcServer::writeNotification("memory", payload); the JS rpcpp
        // client decodes it and dispatches here. Single-arg payload is
        // the params object the C++ struct serializes to.
        const handler = (params: unknown) => {
            if (cancelled) return;
            const p = params as MemoryNotification;
            if (p.systemId !== systemId || p.type !== type) return;
            setSnapshot({ bytes: p.bytes, version: p.version });
        };
        plugin.$on("memory", handler);

        plugin.subscribeMemory(systemId, type, hz).catch((err) => {
            console.error("[useMemory] subscribeMemory failed", err);
        });

        return () => {
            cancelled = true;
            plugin.$off("memory", handler);
            plugin.unsubscribeMemory(systemId, type).catch((err) => {
                console.error("[useMemory] unsubscribeMemory failed", err);
            });
        };
    }, [systemId, type, hz]);

    return snapshot;
}

/**
 * One-shot cold-path read of a region slice. `length === 0` means "until end
 * of region". Returns null for unknown system / unsupported type / offset
 * past end. Use for ROM dumps, SRAM snapshots, savestate-style captures —
 * anything where live subscription would burn bandwidth without value.
 *
 * The base64-on-wire bytes are decoded into a Uint8Array view.
 */
export async function getMemoryOnce(
    systemId: number,
    type: MemoryType,
    offset = 0,
    length = 0,
): Promise<{ bytes: Uint8Array; hash: number; regionSize: number } | null> {
    const r = await plugin.getMemory(systemId, type, offset, length);
    if (!r) return null;
    // PluginService codegen types `bytes` as a base64 string because the
    // OpenRPC schema sees msgpack BIN as opaque. At runtime the msgpack
    // codec hands us a Uint8Array directly — same trick as FrameResponse
    // in client.ts.
    const bytes = r.bytes as unknown as Uint8Array;
    return { bytes, hash: r.hash, regionSize: r.regionSize };
}
