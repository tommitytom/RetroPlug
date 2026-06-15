import { View, Text, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import type {
    PluginRpcServiceKitEntry,
    PluginRpcServiceKitSampleEntry,
    PluginRpcServiceKitSampleSpec,
} from "plugin-service";

// LVGL types don't expose refs; cast through `any` like the rest of the UI.
const TextAny = Text as any;

const KIT_SLOT_COUNT    = 16;
const SAMPLE_SLOT_COUNT = 15;
const SAMPLE_NAME_MAX   = 3;

const COLOR = {
    bg:       "#0e0e1c",
    panel:    "#1a1a2e",
    text:     "#cccccc",
    textHi:   "#ffffff",
    textDim:  "#888888",
    accent:   "#4fc3f7",
    dirty:    "#ffb74d",
    danger:   "#ef5350",
    rowHover: "#22325a",
};

// Single source of truth for row heights — match these to font-size so
// LVGL doesn't add silent padding that pushes content past the visible area.
// Tuned for a 480x640 window: 24 + 16*20 + 20 + 20 + 20 + 3*24 = 476.
const ROW_H        = 20;
const HEADER_H     = 24;
const FOOTER_H     = 24;
const SECTION_H    = 20;
const FONT_M       = 13;
const FONT_L       = 15;

// --------- helpers ---------

type EditableSample = PluginRpcServiceKitSampleEntry;

type EditableKit = {
    slot:       number;
    name:       string;
    samples:    EditableSample[];
    serverHash: number;
    populated:  boolean;
};

function emptySlot(idx: number): EditableKit {
    return { slot: idx, name: "", samples: [], serverHash: 0, populated: false };
}

function structureHash(samples: EditableSample[]): string {
    return samples.map((s) => `${s.name} ${s.path}`).join("");
}

function isDirty(local: EditableKit, lastApplied: Map<number, string>): boolean {
    const cur  = structureHash(local.samples);
    const seen = lastApplied.get(local.slot);
    if (seen === undefined) return local.samples.length > 0;
    return cur !== seen;
}

// Strip a filesystem path into a 3-char LSDJ sample name. "kick.wav" → "KCK".
function deriveSampleName(path: string): string {
    const file = path.split("/").pop() ?? path;
    const stem = file.replace(/\.[^.]+$/, "");
    const upper = stem.toUpperCase().replace(/[^A-Z0-9]/g, "");
    if (upper.length <= SAMPLE_NAME_MAX) {
        return upper.padEnd(SAMPLE_NAME_MAX, "-");
    }
    const head = upper[0];
    const tail = upper.slice(1).replace(/[AEIOU]/g, "");
    const compact = (head + tail).slice(0, SAMPLE_NAME_MAX);
    return compact.length >= SAMPLE_NAME_MAX
        ? compact
        : upper.slice(0, SAMPLE_NAME_MAX);
}

// openSampleBrowser is fire-and-forget on the server; the chosen path
// arrives via the "sample-path-selected" event. Wrap that in a Promise
// so callers can `await` a file pick. Times out after 5 minutes to keep
// stray listeners from accumulating across re-mounts.
function pickSampleFile(): Promise<string | null> {
    return new Promise((resolve) => {
        let timer: ReturnType<typeof setTimeout> | null = null;
        const handler = (path: string) => {
            off("sample-path-selected", handler);
            if (timer) clearTimeout(timer);
            resolve(path && path.length > 0 ? path : null);
        };
        on("sample-path-selected", handler);
        void plugin.$notify("openSampleBrowser");
        timer = setTimeout(() => {
            off("sample-path-selected", handler);
            resolve(null);
        }, 5 * 60 * 1000);
    });
}

// --------- component ---------

interface KitEditorProps {
    systemId: number;
    // The sink group owned by PluginUI; we hand keyboard control back to
    // this group when the editor unmounts so the menu/grid receives keys.
    sinkGroup: any;
    onClose:   () => void;
}

export function KitEditor({ systemId, sinkGroup, onClose }: KitEditorProps) {
    const [kits, setKits] = useState<EditableKit[]>(() =>
        Array.from({ length: KIT_SLOT_COUNT }, (_, i) => emptySlot(i)));
    const [selectedSlot, setSelectedSlot] = useState(0);
    const [busy, setBusy] = useState(false);
    const [status, setStatus] = useState<string>("Pick a slot, add samples, hit Patch.");
    const lastAppliedRef = useRef<Map<number, string>>(new Map());

    // All interactive elements register here on render via the ref
    // callback. Order matters for arrow-key navigation: slot rows first
    // (top-down), then sample rows, then the trailing action items.
    const itemRefs   = useRef<any[]>([]);
    const groupRef   = useRef<any>(null);
    const focusIdxRef = useRef(0);
    const [focusedIdx, setFocusedIdx] = useState(0);

    // ---- server <-> local sync ----

    const pullFromServer = useCallback(async () => {
        const res = await plugin.getKitsConfig(systemId);
        const next: EditableKit[] = Array.from({ length: KIT_SLOT_COUNT },
            (_, i) => emptySlot(i));
        const applied = new Map<number, string>();
        for (const k of res.kits as PluginRpcServiceKitEntry[]) {
            if (k.slot >= KIT_SLOT_COUNT) continue;
            next[k.slot] = {
                slot:       k.slot,
                name:       k.name,
                samples:    k.samples,
                serverHash: k.compiledHash,
                populated:  k.compiledSize > 0,
            };
            applied.set(k.slot, structureHash(k.samples));
        }
        lastAppliedRef.current = applied;
        setKits(next);
    }, [systemId]);

    useEffect(() => {
        void pullFromServer();
        const handler = () => { void pullFromServer(); };
        on("config-changed", handler);
        return () => off("config-changed", handler);
    }, [pullFromServer]);

    // Request a window tall enough to fit 16 slots + the sample list + the
    // 3-row action footer. 480x700 matches the menu's typical width so the
    // host doesn't snap us awkwardly. Tiling WMs (Hyprland) silently ignore
    // the request, which is fine — the editor degrades to a clipped view
    // but still works keyboard-side.
    useEffect(() => {
        void (async () => {
            if (await plugin.isWindowSizeControlled()) return;
            await plugin.setWindowSize(480, 700);
        })();
    }, []);

    // ---- focus group lifecycle ----
    //
    // Pattern lifted from MenuOverlay (PluginUI.tsx). We rebuild the
    // group whenever the item count could change (samples added/removed
    // alter the count). itemRefs is populated by the render below; we
    // claim the keyboard *after* refs settle by sequencing through
    // useEffect with the slot count + sample count in the deps array.
    const totalItems = KIT_SLOT_COUNT + kits[selectedSlot].samples.length + 4;
    //                                                                 ^^^
    //          ↑ trailing items: [+ Add sample], [Patch], [Erase], [Close]

    useEffect(() => {
        const group = createGroup();
        groupRef.current = group;
        for (const ref of itemRefs.current) {
            if (ref) group.add(ref);
        }
        // Re-focus whatever was focused before the rebuild, clamped.
        const idx = Math.min(focusIdxRef.current, itemRefs.current.length - 1);
        if (idx >= 0 && itemRefs.current[idx]) {
            group.focus(itemRefs.current[idx]);
        }
        setKeyboardGroup(group);
        return () => {
            // Hand keyboard back to the parent's sink so the menu/grid keys
            // start working again. Passing `null` would fall back to
            // lv_group_get_default — which contains every clickable View
            // and would route arrows to random tiles.
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
            groupRef.current = null;
        };
    }, [sinkGroup, totalItems]);

    // ---- arrow / enter navigation ----

    const onItemKey = useCallback((e: { key: number }) => {
        const refs  = itemRefs.current;
        const group = groupRef.current;
        if (!group || refs.length === 0) return;
        let next = focusIdxRef.current;
        if (e.key === ELvKey.LV_KEY_DOWN || e.key === ELvKey.LV_KEY_RIGHT) {
            next = (focusIdxRef.current + 1) % refs.length;
        } else if (e.key === ELvKey.LV_KEY_UP || e.key === ELvKey.LV_KEY_LEFT) {
            next = (focusIdxRef.current - 1 + refs.length) % refs.length;
        } else {
            return;
        }
        if (refs[next]) group.focus(refs[next]);
    }, []);

    const onItemFocus = useCallback((idx: number) => {
        focusIdxRef.current = idx;
        setFocusedIdx(idx);
    }, []);

    // ---- actions ----

    const onAddSample = useCallback(async () => {
        const local = kits[selectedSlot];
        if (local.samples.length >= SAMPLE_SLOT_COUNT) {
            setStatus(`slot ${selectedSlot} full (${SAMPLE_SLOT_COUNT} samples max)`);
            return;
        }
        const path = await pickSampleFile();
        if (!path) return;
        setKits((prev) => {
            const cur = prev[selectedSlot];
            const updated = {
                ...cur,
                samples: [...cur.samples, {
                    path,
                    name:       deriveSampleName(path),
                    pitch:      0x7F,
                    volume:     0xFF,
                    sourceHash: 0,
                    offset:     0,
                    length:     0,
                    effects:    [],
                }],
                populated: true,
            };
            const next = prev.slice();
            next[selectedSlot] = updated;
            return next;
        });
    }, [kits, selectedSlot]);

    const onRemoveSample = useCallback((sampleIdx: number) => {
        setKits((prev) => {
            const cur = prev[selectedSlot];
            const samples = cur.samples.slice();
            samples.splice(sampleIdx, 1);
            const next = prev.slice();
            next[selectedSlot] = { ...cur, samples };
            return next;
        });
    }, [selectedSlot]);

    const onPatchKit = useCallback(async () => {
        if (busy) return;
        const slot = selectedSlot;
        const local = kits[slot];
        const kitName = local.name || `KIT${String(slot).padStart(2, "0")}`;
        const specs: PluginRpcServiceKitSampleSpec[] = local.samples.map((s) => ({
            path:    s.path,
            name:    s.name,
            offset:  s.offset,
            length:  s.length,
            effects: s.effects,
            pitch:   s.pitch,
            volume:  s.volume,
        }));
        setBusy(true);
        setStatus(`compiling kit ${slot}...`);
        try {
            const res = await plugin.compileAndPatchKit(systemId, slot, kitName, specs);
            if (!res.ok) {
                setStatus(`compile failed: ${res.error}`);
                return;
            }
            lastAppliedRef.current.set(slot, structureHash(local.samples));
            setStatus(`patched slot ${slot} (${specs.length} samples)`);
            await pullFromServer();
        } finally {
            setBusy(false);
        }
    }, [busy, kits, selectedSlot, systemId, pullFromServer]);

    const onEraseKit = useCallback(async () => {
        if (busy) return;
        const slot = selectedSlot;
        setBusy(true);
        setStatus(`erasing kit ${slot}...`);
        try {
            const ok = await plugin.eraseKit(systemId, slot);
            setStatus(ok ? `erased slot ${slot}` : "erase failed");
            lastAppliedRef.current.delete(slot);
            await pullFromServer();
        } finally {
            setBusy(false);
        }
    }, [busy, selectedSlot, systemId, pullFromServer]);

    // ---- render ----

    const selected = kits[selectedSlot];
    const selectedDirty = isDirty(selected, lastAppliedRef.current);

    // Reset itemRefs every render — exact length depends on the current
    // kit's sample count. Indices: 0..15 = slot rows, then samples, then
    // [+ Add sample], [Patch], [Erase], [Close].
    //
    // Each clickable element claims its index by calling `claimRefIdx()`
    // synchronously during JSX creation — NOT inside the ref callback,
    // because ref callbacks fire bottom-up after the whole tree mounts,
    // which would index everything in reverse and clobber the position-
    // based focus/highlight logic. The ref callback then just stores the
    // pre-assigned ref at that fixed index.
    itemRefs.current = [];
    let nextRefIdx = 0;
    const claimRefIdx = () => nextRefIdx++;
    const refCallback = (idx: number) => (r: any) => {
        itemRefs.current[idx] = r;
    };

    return (
        <View style={{
            width: "100%",
            height: "100%",
            "background-color": COLOR.bg,
            "background-opacity": 255,
            "border-width": 0,
            "border-opacity": 0,
            "padding-left":  6,
            "padding-right": 6,
            "padding-top":   4,
            "padding-bottom":4,
            display: "flex",
            "flex-direction": "column",
            "align-items": "flex-start",
            "justify-content": "flex-start",
            overflow: "hidden",
        }}>
            {/* header */}
            <Text style={{
                "text-color": COLOR.accent,
                "font-size": FONT_L,
                width: "100%",
                height: HEADER_H,
                padding: 4,
            }}>
                {`Kit Editor (system ${systemId})`}
            </Text>

            {/* 16 kit slot rows */}
            {kits.map((k, idx) => {
                const refIdx = claimRefIdx();
                const isSelected = idx === selectedSlot;
                const dirty = isDirty(k, lastAppliedRef.current);
                const label = k.populated
                    ? `${String(k.slot).padStart(2, "0")}  ${k.name || "(unnamed)"}`
                    : `${String(k.slot).padStart(2, "0")}  (empty)`;
                return (
                    <TextAny
                        key={`slot-${idx}`}
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: ROW_H,
                            "text-color": isSelected ? COLOR.textHi : COLOR.text,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : (isSelected ? COLOR.panel : COLOR.bg),
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 3,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={() => setSelectedSlot(idx)}
                    >
                        {dirty ? `${label}  *` : label}
                    </TextAny>
                );
            })}

            {/* Selected-kit section header */}
            <Text style={{
                "text-color": COLOR.accent,
                "font-size": FONT_M,
                width: "100%",
                height: SECTION_H,
                padding: 3,
            }}>
                {`Slot ${selectedSlot}: ${selected.name || "(empty)"}` +
                 (selectedDirty ? "  *" : "")}
            </Text>

            {/* sample rows */}
            {selected.samples.map((s, idx) => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        key={`sample-${idx}`}
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: ROW_H,
                            "text-color": COLOR.textHi,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 3,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={() => onRemoveSample(idx)}
                    >
                        {`  ${s.name}  ${s.path.split("/").pop() ?? s.path}   [x]`}
                    </TextAny>
                );
            })}

            {/* + Add sample */}
            {(() => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: ROW_H,
                            "text-color": COLOR.accent,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : COLOR.bg,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 3,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={() => { void onAddSample(); }}
                    >
                        {"+ Add sample"}
                    </TextAny>
                );
            })()}

            {/* status + actions */}
            <Text style={{
                "text-color": COLOR.textDim,
                "font-size": FONT_M,
                width: "100%",
                height: ROW_H,
                padding: 3,
            }}>
                {status}
            </Text>

            {/* footer action row: [Patch] [Erase] [Close] */}
            {(() => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: FOOTER_H,
                            "text-color": selectedDirty && !busy ? COLOR.accent : COLOR.text,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 4,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={() => { void onPatchKit(); }}
                    >
                        {"[ Patch this kit ]"}
                    </TextAny>
                );
            })()}
            {(() => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: FOOTER_H,
                            "text-color": busy ? COLOR.textDim : COLOR.danger,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 4,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={() => { void onEraseKit(); }}
                    >
                        {"[ Erase this kit ]"}
                    </TextAny>
                );
            })()}
            {(() => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: FOOTER_H,
                            "text-color": COLOR.text,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : COLOR.bg,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 4,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={onItemKey}
                        onClick={onClose}
                    >
                        {"[ Close ]"}
                    </TextAny>
                );
            })()}
        </View>
    );
}
