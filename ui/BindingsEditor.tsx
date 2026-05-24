import { View, Text, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { tileWidth, tileHeight } from "./layout";
import {
    dpfKeyToName,
    KEY_BACKSPACE,
    KEY_ENTER,
    KEY_ESCAPE,
} from "../runtime/lvgljs/input";
import type { BindingMapJson } from "plugin-service";

// LVGL types don't expose refs; cast through `any` like the rest of the UI.
const TextAny = Text as any;

// GB buttons in canonical menu order — matches LSDJ / SameBoy joypad layout.
const GB_BUTTONS = [
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start",
] as const;

const COLOR = {
    bg:        "#0e0e1c",
    panel:     "#1a1a2e",
    text:      "#cccccc",
    textHi:    "#ffffff",
    textDim:   "#888888",
    accent:    "#4fc3f7",
    dirty:     "#ffb74d",
    danger:    "#ef5350",
    rowHover:  "#22325a",
    capture:   "#4fc3f7",
    promptBg:  "#161628",
};

const FONT_M    = 13;
const FONT_L    = 15;
const ROW_H     = 22;
const HEADER_H  = 26;
const FOOTER_H  = 26;
const SECTION_H = 22;

type Kind = "keyboard" | "gamepad";

type PromptKind = "saveAs" | "rename" | "deleteConfirm" | null;

interface BindingsEditorProps {
    kind:      Kind;
    zoom:      number;
    onClose:   () => void;
    sinkGroup: any;
}

function emptyBindingMap(name: string): BindingMapJson {
    return {
        schemaVersion: 1,
        name,
        keyboard: {},
        gamepad:  {},
    };
}

function bindingsForKind(b: BindingMapJson, kind: Kind): Record<string, string[]> {
    return kind === "keyboard" ? (b.keyboard ?? {}) : (b.gamepad ?? {});
}

function withChannel(
    b: BindingMapJson, kind: Kind, channel: Record<string, string[]>,
): BindingMapJson {
    return kind === "keyboard"
        ? { ...b, keyboard: channel }
        : { ...b, gamepad:  channel };
}

function channelsEqual(
    a: Record<string, string[]>, b: Record<string, string[]>,
): boolean {
    const ka = Object.keys(a), kb = Object.keys(b);
    if (ka.length !== kb.length) return false;
    for (const k of ka) {
        const av = a[k] ?? [], bv = b[k] ?? [];
        if (av.length !== bv.length) return false;
        for (let i = 0; i < av.length; i++) if (av[i] !== bv[i]) return false;
    }
    return true;
}

function formatBindingList(values: string[] | undefined): string {
    if (!values || values.length === 0) return "-";
    return values.join(", ");
}

const VALID_PROFILE_RE = /^[A-Za-z0-9_-]+$/;

function isValidName(s: string): boolean {
    return s.length > 0 && s !== "config" && VALID_PROFILE_RE.test(s);
}

export function BindingsEditor({ kind, zoom, onClose, sinkGroup }: BindingsEditorProps) {
    // ---- state ------------------------------------------------------------

    // Active profile name for this kind. Initially populated from a
    // getUserConfig() call; subsequent profile-switches inside the editor
    // update it without writing config.json (we only persist the switch
    // when the user explicitly picks a profile to make active).
    const [activeProfile, setActiveProfile]   = useState<string>("");
    const [availableProfiles, setAvailableProfiles] = useState<string[]>([]);
    // The file we're editing (which may not be the active profile if the
    // user is browsing other profiles inside the editor).
    const [profileName, setProfileName] = useState<string>("");
    // Working copy of the file's BindingMapJson and the unedited reference
    // copy (used for dirty tracking + Cancel restore).
    const [working,  setWorking]  = useState<BindingMapJson | null>(null);
    const [original, setOriginal] = useState<BindingMapJson | null>(null);
    // Capture mode: GB button name being rebound, or null.
    const [captureFor, setCaptureFor] = useState<string | null>(null);
    // Inline name-prompt modal state. value holds the in-progress string.
    const [promptKind, setPromptKind] = useState<PromptKind>(null);
    const [promptValue, setPromptValue] = useState<string>("");
    const [promptError, setPromptError] = useState<string>("");
    const [status, setStatus] = useState<string>("Pick a row, hit Enter, press a key.");

    const captureForRef = useRef<string | null>(null);
    useEffect(() => { captureForRef.current = captureFor; }, [captureFor]);
    const promptKindRef = useRef<PromptKind>(null);
    useEffect(() => { promptKindRef.current = promptKind; }, [promptKind]);

    // ---- server sync ------------------------------------------------------

    // Fetch the active profile name + the profile list once on mount.
    useEffect(() => {
        void (async () => {
            const cfg = await plugin.getUserConfig();
            const active = kind === "keyboard"
                ? cfg.activeKeyboardBindings
                : cfg.activeGamepadBindings;
            setActiveProfile(active);
            setAvailableProfiles(cfg.availableProfiles ?? []);
            setProfileName(active);
        })();
    }, [kind]);

    // Whenever the selected profile changes, fetch the full underlying file
    // so we can preserve the other channel when saving.
    useEffect(() => {
        if (!profileName) return;
        void (async () => {
            try {
                const raw = await plugin.getBindingProfile(profileName);
                if (raw) {
                    setOriginal(raw);
                    setWorking(raw);
                } else {
                    // File missing or unparseable. Treat as empty so the
                    // editor still works (Save As writes a fresh file).
                    const stub = emptyBindingMap(profileName);
                    setOriginal(stub);
                    setWorking(stub);
                }
                setCaptureFor(null);
            } catch (e) {
                console.warn("[bindings-editor] getBindingProfile failed", e);
            }
        })();
    }, [profileName]);

    // Refresh profile list + active reference when C++ pushes a change
    // (e.g. our own save round-tripping through efsw).
    useEffect(() => {
        const apply = async () => {
            try {
                const cfg = await plugin.getUserConfig();
                setAvailableProfiles(cfg.availableProfiles ?? []);
                setActiveProfile(kind === "keyboard"
                    ? cfg.activeKeyboardBindings
                    : cfg.activeGamepadBindings);
            } catch { /* ignore */ }
        };
        const handler = () => { void apply(); };
        on("user-config-changed", handler);
        return () => off("user-config-changed", handler);
    }, [kind]);

    // ---- window sizing ----------------------------------------------------

    // Fit the editor into a fixed window that matches the tile size at the
    // current zoom (consistent with AboutPanel). Tiled WMs ignore this.
    useEffect(() => {
        void (async () => {
            if (await plugin.isWindowSizeControlled()) return;
            await plugin.setWindowSize(tileWidth(zoom), tileHeight(zoom));
        })();
    }, [zoom]);

    // ---- focus group lifecycle -------------------------------------------

    // Item count: 8 GB-button rows + 1 profile bar (treated as one focusable
    // unit) + 4 footer action rows (Save / Cancel / Save As / Delete).
    // Rename/Duplicate live on the profile bar's modal flow rather than as
    // top-level focusables to keep the count stable.
    const itemRefs = useRef<any[]>([]);
    const groupRef = useRef<any>(null);
    const focusIdxRef = useRef(0);
    const [focusedIdx, setFocusedIdx] = useState(0);

    const totalItems = 1 /* profile row */ + GB_BUTTONS.length + 6 /* footer */;

    useEffect(() => {
        const group = createGroup();
        groupRef.current = group;
        for (const ref of itemRefs.current) if (ref) group.add(ref);
        const idx = Math.min(focusIdxRef.current, itemRefs.current.length - 1);
        if (idx >= 0 && itemRefs.current[idx]) group.focus(itemRefs.current[idx]);
        setKeyboardGroup(group);
        return () => {
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
            groupRef.current = null;
        };
    }, [sinkGroup, totalItems]);

    const onItemKey = useCallback((e: { key: number }) => {
        const refs  = itemRefs.current;
        const group = groupRef.current;
        if (!group || refs.length === 0) return;
        let next = focusIdxRef.current;
        if (e.key === ELvKey.LV_KEY_DOWN || e.key === ELvKey.LV_KEY_NEXT) {
            next = (focusIdxRef.current + 1) % refs.length;
        } else if (e.key === ELvKey.LV_KEY_UP || e.key === ELvKey.LV_KEY_PREV) {
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

    // ---- capture / prompt: subscribe to raw event channels ---------------
    //
    // The "key" / "gamepad-button" subscribers below are mounted once and
    // dispatch through stable wrapper refs that ALWAYS read the latest
    // closure. The original "define handler inside useEffect with [kind]
    // deps" approach captured the first render's `working` (== null), so
    // applyCapture's `if (!working) return` guard short-circuited every
    // single capture. The ref indirection keeps the subscription cheap
    // while letting helpers read live state.
    const keyHandlerRef = useRef<(key: number, press: boolean) => void>(() => {});
    const padHandlerRef = useRef<(pad: number, button: string, pressed: boolean) => void>(() => {});

    // Always overwrite on render so the indirection picks up the latest
    // closures. Cheap — just a property assignment.
    keyHandlerRef.current = (key, press) => {
        if (!press) return;
        if (promptKind) { handlePromptKey(key); return; }
        if (captureFor) {
            if (key === KEY_ESCAPE) {
                setCaptureFor(null);
                setStatus("Capture cancelled.");
                return;
            }
            if (kind !== "keyboard") {
                // Gamepad editor in capture: Esc only. Other keys ignored.
                return;
            }
            const name = dpfKeyToName(key);
            if (!name) {
                setStatus(`Unknown key 0x${key.toString(16)} - try another.`);
                return;
            }
            applyCapture(name);
            return;
        }
        // Outside capture / prompt: Esc closes the editor.
        if (key === KEY_ESCAPE) onClose();
    };

    padHandlerRef.current = (_pad, button, pressed) => {
        if (!pressed) return;
        if (kind !== "gamepad") return;
        if (!captureFor) return;
        applyCapture(button);
    };

    useEffect(() => {
        const keyWrap = (k: number, p: boolean) => keyHandlerRef.current(k, p);
        on("key", keyWrap);
        const padWrap = (p: number, b: string, pr: boolean) => padHandlerRef.current(p, b, pr);
        on("gamepad-button", padWrap);
        return () => {
            off("key", keyWrap);
            off("gamepad-button", padWrap);
        };
    }, []);

    // ---- capture: apply a captured input source -------------------------

    function applyCapture(source: string) {
        const btn = captureForRef.current;
        if (!btn || !working) return;
        const channel = { ...bindingsForKind(working, kind) };
        channel[btn] = [source];        // replace-all
        setWorking(withChannel(working, kind, channel));
        setCaptureFor(null);
        setStatus(`${btn} = ${source}`);
    }

    function clearBinding(btn: string) {
        if (!working) return;
        const channel = { ...bindingsForKind(working, kind) };
        delete channel[btn];
        setWorking(withChannel(working, kind, channel));
        setStatus(`${btn} cleared.`);
    }

    // ---- prompt handling -------------------------------------------------

    function openPrompt(p: PromptKind, defaultValue = "") {
        setPromptKind(p);
        setPromptValue(defaultValue);
        setPromptError("");
        setCaptureFor(null);
    }

    function closePrompt() {
        setPromptKind(null);
        setPromptValue("");
        setPromptError("");
    }

    function handlePromptKey(key: number) {
        const pk = promptKindRef.current;
        if (!pk) return;
        if (key === KEY_ESCAPE) { closePrompt(); return; }
        if (key === KEY_ENTER)  { void confirmPrompt(); return; }
        if (key === KEY_BACKSPACE) {
            setPromptValue(v => v.slice(0, -1));
            setPromptError("");
            return;
        }
        if (pk === "deleteConfirm") return;   // Y/N only
        // Printable ASCII letters / digits / dash / underscore.
        if (key >= 0x20 && key <= 0x7E) {
            const ch = String.fromCharCode(key);
            if (VALID_PROFILE_RE.test(ch)) {
                setPromptValue(v => (v + ch).slice(0, 48));
                setPromptError("");
            }
        }
    }

    async function confirmPrompt() {
        const pk = promptKindRef.current;
        if (!pk) return;
        if (pk === "deleteConfirm") {
            await doDelete();
            return;
        }
        const name = promptValue.trim();
        if (!isValidName(name)) {
            setPromptError("Invalid name (use A-Z, 0-9, _, -).");
            return;
        }
        if (pk === "saveAs") {
            if (availableProfiles.includes(name)) {
                setPromptError("Profile already exists.");
                return;
            }
            const toWrite: BindingMapJson = {
                ...(working ?? emptyBindingMap(name)),
                schemaVersion: 1,
                name,
            };
            const ok = await plugin.saveBindingProfile(name, toWrite);
            if (!ok) { setPromptError("Save failed (write error)."); return; }
            setStatus(`Saved as "${name}".`);
            closePrompt();
            setProfileName(name);
            return;
        }
        if (pk === "rename") {
            if (name === profileName) { closePrompt(); return; }
            if (availableProfiles.includes(name)) {
                setPromptError("Profile already exists.");
                return;
            }
            const ok = await plugin.renameBindingProfile(profileName, name);
            if (!ok) { setPromptError("Rename failed."); return; }
            setStatus(`Renamed "${profileName}" → "${name}".`);
            closePrompt();
            setProfileName(name);
            return;
        }
    }

    async function doDelete() {
        const name = profileName;
        if (name === activeProfile) {
            setPromptError("Cannot delete the active profile.");
            return;
        }
        const ok = await plugin.deleteBindingProfile(name);
        if (!ok) { setPromptError("Delete failed."); return; }
        setStatus(`Deleted "${name}".`);
        closePrompt();
        // Switch to the active profile after delete.
        setProfileName(activeProfile);
    }

    // ---- top-level actions ----------------------------------------------

    const onStartCapture = useCallback((btn: string) => {
        setCaptureFor(btn);
        setStatus(kind === "keyboard"
            ? `Press a key for ${btn} (Esc to cancel)...`
            : `Press a controller button for ${btn} (Esc to cancel)...`);
    }, [kind]);

    const onSave = useCallback(async () => {
        if (!working) return;
        const ok = await plugin.saveBindingProfile(profileName, working);
        if (!ok) { setStatus("Save failed."); return; }
        setStatus(`Saved "${profileName}".`);
        setOriginal(working);
    }, [working, profileName]);

    const onCancel = useCallback(() => {
        if (original) setWorking(original);
        setStatus("Reverted to last saved.");
    }, [original]);

    const onCycleProfile = useCallback((dir: 1 | -1) => {
        if (availableProfiles.length === 0) return;
        const idx = Math.max(0, availableProfiles.indexOf(profileName));
        const len = availableProfiles.length;
        const next = dir > 0 ? (idx + 1) % len : (idx - 1 + len) % len;
        setProfileName(availableProfiles[next]);
    }, [availableProfiles, profileName]);

    const onMakeActive = useCallback(async () => {
        if (profileName === activeProfile) return;
        const ok = kind === "keyboard"
            ? await plugin.setActiveKeyboardBindings(profileName)
            : await plugin.setActiveGamepadBindings(profileName);
        if (!ok) { setStatus("Could not switch active profile."); return; }
        setStatus(`Active profile = "${profileName}".`);
    }, [profileName, activeProfile, kind]);

    const onDuplicate = useCallback(async () => {
        if (!working) return;
        // Pick a unique name based on the current one.
        let candidate = profileName + "-copy";
        let i = 2;
        while (availableProfiles.includes(candidate)) {
            candidate = `${profileName}-copy-${i++}`;
            if (i > 100) return;
        }
        const toWrite: BindingMapJson = { ...working, schemaVersion: 1, name: candidate };
        const ok = await plugin.saveBindingProfile(candidate, toWrite);
        if (!ok) { setStatus("Duplicate failed."); return; }
        setStatus(`Duplicated to "${candidate}".`);
        setProfileName(candidate);
    }, [working, profileName, availableProfiles]);

    // ---- render ----------------------------------------------------------

    const width  = tileWidth(zoom);
    const height = tileHeight(zoom);

    itemRefs.current = [];
    let nextRefIdx = 0;
    const claimRefIdx = () => nextRefIdx++;
    const refCallback = (idx: number) => (r: any) => { itemRefs.current[idx] = r; };

    const channel = working ? bindingsForKind(working, kind) : {};
    const origChannel = original ? bindingsForKind(original, kind) : {};
    const dirty = working && original ? !channelsEqual(channel, origChannel) : false;

    return (
        <View style={{
            width, height,
            "background-color": COLOR.bg,
            "background-opacity": 255,
            "border-width": 1,
            "border-color": COLOR.accent,
            "border-opacity": 255,
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
            {/* title */}
            <Text style={{
                "text-color": COLOR.accent,
                "font-size": FONT_L,
                width: "100%",
                height: HEADER_H,
                padding: 4,
            }}>
                {kind === "keyboard" ? "Keyboard Bindings" : "Gamepad Bindings"}
            </Text>

            {/* profile row: name + cycler + indicator */}
            {(() => {
                const refIdx = claimRefIdx();
                const isActive = profileName === activeProfile;
                const label = `Profile: ${profileName || "-"}` +
                              (isActive ? "  [active]" : "") +
                              (dirty ? "  *" : "");
                return (
                    <TextAny
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: ROW_H,
                            "text-color": COLOR.textHi,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover : COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 3,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={(e: { key: number }) => {
                            if (e.key === ELvKey.LV_KEY_LEFT)  { onCycleProfile(-1); return; }
                            if (e.key === ELvKey.LV_KEY_RIGHT) { onCycleProfile( 1); return; }
                            if (e.key === ELvKey.LV_KEY_ENTER) { void onMakeActive(); return; }
                            onItemKey(e);
                        }}
                        onClick={() => onCycleProfile(1)}
                    >
                        {`< ${label} >`}
                    </TextAny>
                );
            })()}

            {/* 8 button rows */}
            {GB_BUTTONS.map((btn) => {
                const refIdx = claimRefIdx();
                const isCapturing = captureFor === btn;
                const value = isCapturing
                    ? (kind === "keyboard" ? "Press a key..." : "Press a button...")
                    : formatBindingList(channel[btn]);
                return (
                    <TextAny
                        key={`btn-${btn}`}
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: ROW_H,
                            "text-color": isCapturing ? COLOR.capture : COLOR.text,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover
                                : (isCapturing ? COLOR.panel : COLOR.bg),
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 3,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={(e: { key: number }) => {
                            if (e.key === ELvKey.LV_KEY_ENTER) {
                                onStartCapture(btn);
                                return;
                            }
                            if (e.key === ELvKey.LV_KEY_DEL ||
                                e.key === ELvKey.LV_KEY_BACKSPACE) {
                                clearBinding(btn);
                                return;
                            }
                            onItemKey(e);
                        }}
                        onClick={() => onStartCapture(btn)}
                    >
                        {`  ${btn.padEnd(7)}  ${value}`}
                    </TextAny>
                );
            })}

            {/* status line */}
            <Text style={{
                "text-color": COLOR.textDim,
                "font-size": FONT_M,
                width: "100%",
                height: SECTION_H,
                padding: 3,
            }}>
                {status}
            </Text>

            {/* footer actions: Save, Cancel, Save As, Rename, Duplicate, Delete */}
            {[
                { label: dirty ? "[ Save ]  *" : "[ Save ]",
                  onAct: () => { void onSave(); },
                  color: dirty ? COLOR.accent : COLOR.text },
                { label: "[ Revert ]",
                  onAct: onCancel,
                  color: COLOR.text },
                { label: "[ Save As... ]",
                  onAct: () => openPrompt("saveAs", ""),
                  color: COLOR.text },
                { label: "[ Rename... ]",
                  onAct: () => openPrompt("rename", profileName),
                  color: COLOR.text },
                { label: "[ Duplicate ]",
                  onAct: () => { void onDuplicate(); },
                  color: COLOR.text },
                { label: profileName === activeProfile
                    ? "[ Delete (switch first) ]"
                    : "[ Delete ]",
                  onAct: () => {
                      if (profileName === activeProfile) {
                          setStatus("Switch to another active profile first.");
                          return;
                      }
                      openPrompt("deleteConfirm", profileName);
                  },
                  color: profileName === activeProfile ? COLOR.textDim : COLOR.danger },
            ].map((it, i) => {
                const refIdx = claimRefIdx();
                return (
                    <TextAny
                        key={`act-${i}`}
                        ref={refCallback(refIdx)}
                        style={{
                            width: "100%",
                            height: FOOTER_H,
                            "text-color": it.color,
                            "background-color": focusedIdx === refIdx
                                ? COLOR.rowHover : COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            padding: 4,
                        }}
                        onFocus={() => onItemFocus(refIdx)}
                        onKey={(e: { key: number }) => {
                            if (e.key === ELvKey.LV_KEY_ENTER) { it.onAct(); return; }
                            onItemKey(e);
                        }}
                        onClick={() => it.onAct()}
                    >
                        {it.label}
                    </TextAny>
                );
            })}

            {/* Inline prompt overlay (rendered on top via absolute pos) */}
            {promptKind && (
                <View style={{
                    position: "absolute",
                    left: 16, top: 80,
                    width: width - 32,
                    height: 110,
                    "background-color": COLOR.promptBg,
                    "background-opacity": 255,
                    "border-width": 1,
                    "border-color": COLOR.accent,
                    "border-opacity": 255,
                    "padding-left": 8, "padding-right": 8,
                    "padding-top": 6,  "padding-bottom": 6,
                    display: "flex",
                    "flex-direction": "column",
                }}>
                    <Text style={{
                        "text-color": COLOR.accent,
                        "font-size": FONT_M,
                        width: "100%",
                        height: ROW_H,
                    }}>
                        {promptKind === "saveAs"        ? "Save as new profile:"
                       : promptKind === "rename"        ? `Rename "${profileName}" to:`
                       :                                  `Delete "${profileName}"? Press Enter to confirm, Esc to cancel.`}
                    </Text>
                    {promptKind !== "deleteConfirm" && (
                        <Text style={{
                            "text-color": COLOR.textHi,
                            "background-color": COLOR.panel,
                            "background-opacity": 255,
                            "font-size": FONT_M,
                            width: "100%",
                            height: ROW_H,
                            padding: 4,
                        }}>
                            {promptValue + "_"}
                        </Text>
                    )}
                    <Text style={{
                        "text-color": promptError ? COLOR.danger : COLOR.textDim,
                        "font-size": FONT_M,
                        width: "100%",
                        height: ROW_H,
                    }}>
                        {promptError || "Enter to confirm  |  Esc to cancel"}
                    </Text>
                </View>
            )}
        </View>
    );
}
