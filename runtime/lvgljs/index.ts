// Typed front door to the native lvgljs bridge registered by LvglJsEngine.
// Plugin code should `import { ... } from "lvgljs"` rather than reach into
// globalThis[Symbol.for("lvgljs")] directly.
//
// The build pipeline aliases the bare specifier "lvgljs" to this file (see
// ui/build.js). The native object is symbol-keyed for collision avoidance,
// but that's an implementation detail kept out of plugin code.

import { useState, useEffect, useMemo, useCallback } from "react";

interface NativeGroup {
    add(component: unknown): void;
    remove(component: unknown): void;
    focus(component: unknown): void;
    destroy(): void;
}

interface NativeRender {
    Group: new () => NativeGroup;
}

interface NativeBridge {
    setParameter(index: number, value: number): void;
    getParameterIndex(name: string): number;
    getParameterCount(): number;
    setKeyboardGroup(group: NativeGroup | null): void;
    on(channel: string, handler: (...args: any[]) => void): void;
    off(channel: string, handler?: (...args: any[]) => void): void;
    NativeRender: NativeRender;
}

const native = (globalThis as any)[Symbol.for("lvgljs")] as NativeBridge;

function resolveIndex(id: string | number): number {
    if (typeof id === "number") {
        const count = native.getParameterCount();
        if (id < 0 || id >= count)
            throw new RangeError(`Parameter index ${id} out of range (count=${count})`);
        return id;
    }
    const idx = native.getParameterIndex(id);
    if (idx < 0) throw new Error(`Unknown parameter "${id}"`);
    return idx;
}

export function setParameter(id: string | number, value: number): void {
    native.setParameter(resolveIndex(id), value);
}

// LVGL focus group wrapper. Keyboard navigation (arrows, Tab, Enter) routes
// through the active indev's group. createGroup() makes a new empty group;
// add components by ref to scope navigation to a subset (e.g. a modal menu).
// setKeyboardGroup(null) restores the default group.
export interface Group {
    add(component: unknown): void;
    remove(component: unknown): void;
    focus(component: unknown): void;
    destroy(): void;
}

export function createGroup(): Group {
    return new native.NativeRender.Group();
}

export function setKeyboardGroup(group: Group | null): void {
    native.setKeyboardGroup(group as NativeGroup | null);
}

export function on(channel: string, handler: (...args: any[]) => void): void {
    native.on(channel, handler);
}

export function off(channel: string, handler?: (...args: any[]) => void): void {
    native.off(channel, handler);
}

// React hook: bind a parameter to component state. The setter writes to the
// host (which echoes back through the "parameter" event, but the optimistic
// local update keeps the UI responsive). Throws at mount if the name/index
// is unknown.
export function useParameter(
    id: string | number,
    initial = 0,
): [number, (value: number) => void] {
    const index = useMemo(() => resolveIndex(id), [id]);
    const [value, setValue] = useState(initial);

    useEffect(() => {
        const handler = (idx: number, val: number) => {
            if (idx === index) setValue(val);
        };
        native.on("parameter", handler);
        return () => native.off("parameter", handler);
    }, [index]);

    const set = useCallback((v: number) => {
        setValue(v);
        native.setParameter(index, v);
    }, [index]);

    return [value, set];
}
