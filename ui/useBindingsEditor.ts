import { useCallback, useEffect, useRef, useState } from "react";

import { plugin } from "./plugin/client";
import type { BindingMapJson } from "plugin-service";

export type BindingsKind = "keyboard" | "gamepad";

export const GB_BUTTONS = [
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start",
] as const;

const VALID_PROFILE_RE = /^[A-Za-z0-9_-]+$/;

export function isValidProfileName(s: string): boolean {
    return s.length > 0 && s !== "config" && VALID_PROFILE_RE.test(s);
}

export function isValidProfileChar(ch: string): boolean {
    return VALID_PROFILE_RE.test(ch);
}

function emptyBindingMap(name: string): BindingMapJson {
    return { schemaVersion: 1, name, keyboard: {}, gamepad: {} };
}

function bindingsForKind(b: BindingMapJson, kind: BindingsKind): Record<string, string[]> {
    return kind === "keyboard" ? (b.keyboard ?? {}) : (b.gamepad ?? {});
}

function withChannel(
    b: BindingMapJson, kind: BindingsKind, channel: Record<string, string[]>,
): BindingMapJson {
    return kind === "keyboard" ? { ...b, keyboard: channel } : { ...b, gamepad: channel };
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

export function formatBindingList(values: string[] | undefined): string {
    if (!values || values.length === 0) return "-";
    return values.join(", ");
}

export interface BindingsEditorState {
    kind:          BindingsKind;
    profiles:      string[];
    activeProfile: string;
    profileName:   string;
    working:       BindingMapJson | null;
    channel:       Record<string, string[]>;
    dirty:         boolean;
}

export interface BindingsEditorActions {
    cycleProfile:  (dir: 1 | -1) => void;
    makeActive:    () => Promise<void>;
    applyCapture:  (button: string, source: string) => void;
    clearBinding:  (button: string) => void;
    save:          () => Promise<string | null>;
    revert:        () => void;
    saveAs:        (name: string) => Promise<string | null>;
    rename:        (name: string) => Promise<string | null>;
    duplicate:     () => Promise<string | null>;
    deleteProfile: () => Promise<string | null>;
    canDelete:     boolean;
}

export type BindingsEditor = BindingsEditorState & BindingsEditorActions;

/**
 * Owns the editing state for one bindings channel (keyboard or gamepad).
 *
 * `profiles` and `activeProfile` come from the upstream user-config fetch in
 * PluginUI — passing them in avoids a second round-trip per channel. The
 * hook fetches the full profile file (preserving the other channel for
 * round-trip writes) whenever `profileName` changes.
 *
 * Reset to the new active profile when the upstream activeProfile changes
 * AND the user hasn't touched the editor (i.e. !dirty). This way, a
 * makeActive() round-trip refreshes the view to the now-live profile, but
 * an in-progress edit isn't clobbered by an unrelated user-config-changed
 * tick (e.g. another agent rewriting a file on disk).
 */
export function useBindingsEditor(
    kind: BindingsKind,
    profiles: string[],
    activeProfile: string,
): BindingsEditor {
    const [profileName, setProfileName] = useState<string>("");
    const [working,  setWorking]  = useState<BindingMapJson | null>(null);
    const [original, setOriginal] = useState<BindingMapJson | null>(null);

    // Sync profileName to activeProfile on first activeProfile load, or
    // when the active profile changes externally and we're not mid-edit.
    const dirty = working && original
        ? !channelsEqual(bindingsForKind(working, kind), bindingsForKind(original, kind))
        : false;
    const dirtyRef = useRef(dirty);
    dirtyRef.current = dirty;

    useEffect(() => {
        if (!activeProfile) return;
        if (!profileName) {
            setProfileName(activeProfile);
            return;
        }
        // Active changed externally and we're not editing — follow it.
        if (!dirtyRef.current && profileName !== activeProfile
            && profiles.includes(activeProfile)) {
            setProfileName(activeProfile);
        }
    }, [activeProfile, profileName, profiles]);

    // If the currently-edited profile vanishes (e.g. another agent deleted
    // it), fall back to the active profile so we don't sit on a dead name.
    useEffect(() => {
        if (profileName && profiles.length > 0 && !profiles.includes(profileName)) {
            setProfileName(activeProfile || profiles[0]);
        }
    }, [profiles, profileName, activeProfile]);

    useEffect(() => {
        if (!profileName) return;
        let cancelled = false;
        void (async () => {
            try {
                const raw = await plugin.getBindingProfile(profileName);
                if (cancelled) return;
                const m = raw ?? emptyBindingMap(profileName);
                setOriginal(m);
                setWorking(m);
            } catch (e) {
                console.warn(`[bindings:${kind}] getBindingProfile failed`, e);
            }
        })();
        return () => { cancelled = true; };
    }, [profileName, kind]);

    const cycleProfile = useCallback((dir: 1 | -1) => {
        if (profiles.length === 0) return;
        const idx = Math.max(0, profiles.indexOf(profileName));
        const len = profiles.length;
        const next = dir > 0 ? (idx + 1) % len : (idx - 1 + len) % len;
        setProfileName(profiles[next]);
    }, [profiles, profileName]);

    const makeActive = useCallback(async () => {
        if (!profileName || profileName === activeProfile) return;
        const ok = kind === "keyboard"
            ? await plugin.setActiveKeyboardBindings(profileName)
            : await plugin.setActiveGamepadBindings(profileName);
        if (!ok) console.warn(`[bindings:${kind}] setActive failed`);
    }, [profileName, activeProfile, kind]);

    const applyCapture = useCallback((button: string, source: string) => {
        setWorking(prev => {
            if (!prev) return prev;
            const channel = { ...bindingsForKind(prev, kind) };
            channel[button] = [source];
            return withChannel(prev, kind, channel);
        });
    }, [kind]);

    const clearBinding = useCallback((button: string) => {
        setWorking(prev => {
            if (!prev) return prev;
            const channel = { ...bindingsForKind(prev, kind) };
            delete channel[button];
            return withChannel(prev, kind, channel);
        });
    }, [kind]);

    const save = useCallback(async (): Promise<string | null> => {
        if (!working) return "Nothing to save.";
        const ok = await plugin.saveBindingProfile(profileName, working);
        if (!ok) return "Save failed.";
        setOriginal(working);
        return null;
    }, [working, profileName]);

    const revert = useCallback(() => {
        if (original) setWorking(original);
    }, [original]);

    const saveAs = useCallback(async (name: string): Promise<string | null> => {
        if (!isValidProfileName(name))  return "Invalid name (A-Z, 0-9, _, -).";
        if (profiles.includes(name))    return "Profile already exists.";
        const base = working ?? emptyBindingMap(name);
        const toWrite: BindingMapJson = { ...base, schemaVersion: 1, name };
        const ok = await plugin.saveBindingProfile(name, toWrite);
        if (!ok) return "Save failed.";
        setProfileName(name);
        return null;
    }, [working, profiles]);

    const rename = useCallback(async (name: string): Promise<string | null> => {
        if (!profileName)              return "No profile selected.";
        if (name === profileName)      return null;
        if (!isValidProfileName(name)) return "Invalid name (A-Z, 0-9, _, -).";
        if (profiles.includes(name))   return "Profile already exists.";
        const ok = await plugin.renameBindingProfile(profileName, name);
        if (!ok) return "Rename failed.";
        setProfileName(name);
        return null;
    }, [profileName, profiles]);

    const duplicate = useCallback(async (): Promise<string | null> => {
        if (!working) return "Nothing to duplicate.";
        let candidate = `${profileName}-copy`;
        let i = 2;
        while (profiles.includes(candidate)) {
            candidate = `${profileName}-copy-${i++}`;
            if (i > 100) return "Could not find a free name.";
        }
        const toWrite: BindingMapJson = { ...working, schemaVersion: 1, name: candidate };
        const ok = await plugin.saveBindingProfile(candidate, toWrite);
        if (!ok) return "Duplicate failed.";
        setProfileName(candidate);
        return null;
    }, [working, profileName, profiles]);

    const deleteProfile = useCallback(async (): Promise<string | null> => {
        if (!profileName)                return "No profile selected.";
        if (profileName === activeProfile) return "Cannot delete the active profile.";
        const ok = await plugin.deleteBindingProfile(profileName);
        if (!ok) return "Delete failed.";
        setProfileName(activeProfile);
        return null;
    }, [profileName, activeProfile]);

    const channel = working ? bindingsForKind(working, kind) : {};

    return {
        kind, profiles, activeProfile, profileName, working, channel, dirty,
        cycleProfile, makeActive, applyCapture, clearBinding,
        save, revert, saveAs, rename, duplicate, deleteProfile,
        canDelete: profileName !== activeProfile && !!profileName,
    };
}
