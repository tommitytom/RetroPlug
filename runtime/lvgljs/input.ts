// Keyboard input routing — TypeScript side.
//
// PluginUI::onKeyboard emits a "key" event for every DPF keyboard event with
// (key, press) arguments. The values match DPF's `kKey*` constants from
// deps/dpf/dgl/Base.hpp; ASCII keys come through as their unicode codepoints.
// This module is the single source of truth for:
//   - which DPF keys map to which Game Boy buttons
//   - the GameboyButton enum (mirrors src/system/InputTypes.hpp /
//     SameBoy's GB_key_t — values must stay in sync)
//   - the useKeyboard React hook for subscribing to the event channel
//
// Bindings are runtime-configurable. The startup defaults below mirror the
// previous hardcoded set so the bundle still works before installBindings()
// is called. Plugin code in ui/PluginUI.tsx calls plugin.getUserConfig() on
// mount and pipes the result through installBindings(); a subsequent
// "user-config-changed" event from C++ re-fetches and re-installs.

import { useEffect } from "react";
import { on, off } from "lvgljs";

// Values mirror src/system/InputTypes.hpp::GameboyButton (= GB_key_t in
// deps/sameboy/Core/joypad.h). The C++ side stores commands using the same
// uint8 values, so plugin.pressButton can pass the numeric value through.
export enum GameboyButton {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    Select = 6,
    Start  = 7,
}

// Stable name → enum table so JSON config can refer to buttons by name.
const BUTTON_BY_NAME: Record<string, GameboyButton> = {
    Right:  GameboyButton.Right,
    Left:   GameboyButton.Left,
    Up:     GameboyButton.Up,
    Down:   GameboyButton.Down,
    A:      GameboyButton.A,
    B:      GameboyButton.B,
    Select: GameboyButton.Select,
    Start:  GameboyButton.Start,
};

// DPF key constants (deps/dpf/dgl/Base.hpp). Only the ones we currently use.
// Arrows live in the 0xE03x band (after PageUp/Down/End/Home); modifiers are
// up at 0xE05x — easy to confuse, double-check the header before adding more.
// ASCII keys (Z, X, etc.) just use their character code directly.
export const KEY_BACKSPACE = 0x00000008;
export const KEY_TAB       = 0x00000009;
export const KEY_ENTER     = 0x0000000D;
export const KEY_ESCAPE    = 0x0000001B;
export const KEY_LEFT      = 0xE035;
export const KEY_UP        = 0xE036;
export const KEY_RIGHT     = 0xE037;
export const KEY_DOWN      = 0xE038;
export const KEY_SHIFT_L   = 0xE051;
export const KEY_SHIFT_R   = 0xE052;

// Symbolic key name (as used in JSON config files) → DPF key code.
// Single-char ASCII names ("Z", "x", "1") are resolved by charCodeAt below
// without needing an entry here — the table only carries the named keys.
const KEY_NAME_TO_DPF: Record<string, number> = {
    Backspace: KEY_BACKSPACE,
    Tab:       KEY_TAB,
    Enter:     KEY_ENTER,
    Return:    KEY_ENTER,
    Escape:    KEY_ESCAPE,
    Left:      KEY_LEFT,
    Up:        KEY_UP,
    Right:     KEY_RIGHT,
    Down:      KEY_DOWN,
    ShiftL:    KEY_SHIFT_L,
    ShiftR:    KEY_SHIFT_R,
};

function resolveKeyName(name: string): number | null {
    if (name in KEY_NAME_TO_DPF) return KEY_NAME_TO_DPF[name];
    if (name.length === 1) return name.charCodeAt(0);
    return null;
}

// Inverse of resolveKeyName: DPF key code → preferred symbolic name. Used
// by the in-app bindings editor so that the user pressing e.g. KEY_ENTER
// in capture mode lands "Enter" in the JSON (canonical) rather than
// "Return" (synonym). Returns null for keys we don't know how to name.
//
// Multiple entries in KEY_NAME_TO_DPF collide on the same code (Enter /
// Return both = 0x0D); the preferred-name table below picks one.
const DPF_TO_KEY_NAME: Record<number, string> = {
    [KEY_BACKSPACE]: "Backspace",
    [KEY_TAB]:       "Tab",
    [KEY_ENTER]:     "Enter",
    [KEY_ESCAPE]:    "Escape",
    [KEY_LEFT]:      "Left",
    [KEY_UP]:        "Up",
    [KEY_RIGHT]:     "Right",
    [KEY_DOWN]:      "Down",
    [KEY_SHIFT_L]:   "ShiftL",
    [KEY_SHIFT_R]:   "ShiftR",
};

export function dpfKeyToName(code: number): string | null {
    if (code in DPF_TO_KEY_NAME) return DPF_TO_KEY_NAME[code];
    // Printable ASCII: keep case (so "Z" and "z" remain distinguishable).
    if (code >= 0x20 && code <= 0x7E) return String.fromCharCode(code);
    return null;
}

// List of every symbolic key name the loader understands, plus a single
// representative of the printable-ASCII range. Useful for the editor's
// "unbind / clear" affordance when the underlying file is multi-bind.
export const KNOWN_KEY_NAMES: readonly string[] = [
    "Backspace", "Tab", "Enter", "Escape",
    "Left", "Up", "Right", "Down",
    "ShiftL", "ShiftR",
];

// Runtime maps populated either from the hardcoded defaults below (initial
// state) or from a user JSON profile (after installBindings runs).
let keyMap_: Map<number, GameboyButton> = new Map();
let padMap_: Map<string, GameboyButton> = new Map();

// The default profile — also written to bindings/default.json on first run
// by the C++ side (see src/config/UserConfigSerialization.hpp). Kept here
// too so JS works before any RPC hop completes.
const DEFAULT_KEYBOARD: Record<string, string[]> = {
    Right:  ["Right"],
    Left:   ["Left"],
    Up:     ["Up"],
    Down:   ["Down"],
    A:      ["Z", "z"],
    B:      ["X", "x"],
    Start:  ["Enter"],
    Select: ["ShiftL", "ShiftR", "Backspace"],
};

const DEFAULT_GAMEPAD: Record<string, string[]> = {
    Right:  ["dpright"],
    Left:   ["dpleft"],
    Up:     ["dpup"],
    Down:   ["dpdown"],
    A:      ["a"],
    B:      ["b"],
    Start:  ["start"],
    Select: ["back"],
};

function rebuildKeyboardMap(spec: Record<string, string[]>): Map<number, GameboyButton> {
    const out = new Map<number, GameboyButton>();
    for (const [buttonName, keyNames] of Object.entries(spec)) {
        const button = BUTTON_BY_NAME[buttonName];
        if (button === undefined) {
            console.warn(`[bindings] unknown Game Boy button "${buttonName}" — skipped`);
            continue;
        }
        for (const k of keyNames) {
            const code = resolveKeyName(k);
            if (code === null) {
                console.warn(`[bindings] unknown key name "${k}" for button ${buttonName} — skipped`);
                continue;
            }
            out.set(code, button);
        }
    }
    return out;
}

function rebuildGamepadMap(spec: Record<string, string[]>): Map<string, GameboyButton> {
    const out = new Map<string, GameboyButton>();
    for (const [buttonName, padNames] of Object.entries(spec)) {
        const button = BUTTON_BY_NAME[buttonName];
        if (button === undefined) {
            console.warn(`[bindings] unknown Game Boy button "${buttonName}" — skipped`);
            continue;
        }
        // SDL canonical button names are strings — no lookup table needed.
        for (const n of padNames) out.set(n, button);
    }
    return out;
}

// Initial population — overridden once the JS side fetches user config.
keyMap_ = rebuildKeyboardMap(DEFAULT_KEYBOARD);
padMap_ = rebuildGamepadMap(DEFAULT_GAMEPAD);

// Shape used by installBindings() — matches BindingMapJson on the C++ side
// (src/config/UserConfigSerialization.hpp). The schemaVersion / name fields
// are present in the wire DTO but irrelevant to the runtime maps.
export interface BindingsSpec {
    keyboard: Record<string, string[]>;
    gamepad:  Record<string, string[]>;
}

/**
 * Rebuild the keyboard + gamepad runtime maps from a bindings spec.
 * Called once on UI mount with the result of plugin.getUserConfig(), and
 * again whenever the C++ side emits "user-config-changed".
 *
 * Unknown key names or button names are logged and skipped — they don't
 * fail the whole install. Missing entries fall through to no mapping (so
 * a button left out of the JSON simply becomes unbound, not defaulted).
 */
export function installBindings(spec: BindingsSpec): void {
    keyMap_ = rebuildKeyboardMap(spec.keyboard ?? {});
    padMap_ = rebuildGamepadMap(spec.gamepad ?? {});
}

/**
 * Map a DPF key code to a Game Boy button. Returns null for unmapped keys.
 *
 * Default bindings:
 *   Arrow keys  → D-pad
 *   Z           → A
 *   X           → B
 *   Enter       → Start
 *   Shift / Backspace → Select
 *
 * Edit ~/.config/retroplug/bindings/default.json (or the platform-equivalent)
 * to rebind without a rebuild — see src/config/UserConfig.hpp.
 */
export function mapKeyToGameboyButton(key: number): GameboyButton | null {
    const b = keyMap_.get(key);
    return b === undefined ? null : b;
}

/**
 * React hook: subscribe to the C++ "key" event channel. The handler is
 * called on every keyboard press AND release with the raw DPF key code
 * (call mapKeyToGameboyButton inside if you want game-input semantics).
 */
export function useKeyboard(handler: (key: number, press: boolean) => void) {
    useEffect(() => {
        const wrapped = (key: number, press: boolean) => handler(key, press);
        on("key", wrapped);
        return () => off("key", wrapped);
    }, [handler]);
}

// DPF mouse button constants (deps/dpf/dgl/Base.hpp::MouseButton). Button
// indices start at 1.
export const MOUSE_BUTTON_LEFT   = 1;
export const MOUSE_BUTTON_RIGHT  = 2;
export const MOUSE_BUTTON_MIDDLE = 3;

/**
 * React hook: subscribe to the C++ "mouse" event channel. The handler
 * receives (button, press, x, y) where button is the DPF MouseButton index
 * (1 = left, 2 = right, 3 = middle), and (x, y) are widget-relative pixels.
 *
 * LVGL itself only sees a single binary "pressed" state for the pointer,
 * which is enough for the framework's onClick / focus routing. This channel
 * exists so TS can implement button-aware policy (e.g. right-click opens
 * the per-instance menu).
 */
export function useMouse(
    handler: (button: number, press: boolean, x: number, y: number) => void,
) {
    useEffect(() => {
        const wrapped = (button: number, press: boolean, x: number, y: number) =>
            handler(button, press, x, y);
        on("mouse", wrapped);
        return () => off("mouse", wrapped);
    }, [handler]);
}

// --- Gamepad / SDL game controller input -----------------------------------
//
// PluginUI::pumpGamepad polls SDL_GameController state every uiIdle and emits
// per-event channels:
//   "gamepad-connected"    (pad: number, name: string)
//   "gamepad-disconnected" (pad: number)
//   "gamepad-button"       (pad: number, button: string, pressed: boolean)
//   "gamepad-axis"         (pad: number, axis: string, value: number)
// `pad` is SDL_JoystickID (stable across hot-plug). Button / axis names are
// SDL's canonical strings ("a", "b", "dpup", "leftx", "righttrigger", …) —
// they're label-based, so "a" is whichever face button reads as A on the
// pad's own labels (the bottom face on Xbox layout, the right face on
// Nintendo). Values mean the same thing as the JS Gamepad API:
//   buttons: pressed = true on press, false on release (already de-bounced
//            by the C++ side — no auto-repeat).
//   axes:    -1.0 to 1.0, with a small dead-zone clipped to 0.

/**
 * Default mapping for SDL game-controller buttons → Game Boy buttons.
 * D-pad is the obvious choice; the rest mirror the keyboard defaults
 * (right face button = A, bottom face button = B, Start, Back = Select).
 *
 * Returns null for unmapped buttons (sticks click, shoulder buttons,
 * Guide, paddles, etc.) — let JS handle those for UI nav if it wants.
 */
export function mapGamepadButtonToGameboyButton(button: string): GameboyButton | null {
    const b = padMap_.get(button);
    return b === undefined ? null : b;
}

/** React hook: subscribe to the "gamepad-button" event channel. */
export function useGamepadButton(
    handler: (pad: number, button: string, pressed: boolean) => void,
) {
    useEffect(() => {
        const wrapped = (pad: number, button: string, pressed: boolean) =>
            handler(pad, button, pressed);
        on("gamepad-button", wrapped);
        return () => off("gamepad-button", wrapped);
    }, [handler]);
}

/** React hook: subscribe to the "gamepad-axis" event channel. */
export function useGamepadAxis(
    handler: (pad: number, axis: string, value: number) => void,
) {
    useEffect(() => {
        const wrapped = (pad: number, axis: string, value: number) =>
            handler(pad, axis, value);
        on("gamepad-axis", wrapped);
        return () => off("gamepad-axis", wrapped);
    }, [handler]);
}

/**
 * React hook: subscribe to gamepad hot-plug notifications. Either callback
 * may be omitted. Useful for showing a "Controller connected" banner.
 */
export function useGamepadConnections(opts: {
    onConnected?:    (pad: number, name: string) => void,
    onDisconnected?: (pad: number) => void,
}) {
    useEffect(() => {
        const c = opts.onConnected;
        const d = opts.onDisconnected;
        if (c) on("gamepad-connected", c);
        if (d) on("gamepad-disconnected", d);
        return () => {
            if (c) off("gamepad-connected", c);
            if (d) off("gamepad-disconnected", d);
        };
    }, [opts.onConnected, opts.onDisconnected]);
}
