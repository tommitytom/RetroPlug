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
// Plugins or extensions can rebind by editing this file; the JS bundle hot-
// reloads, no C++ rebuild needed.

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

// DPF key constants (deps/dpf/dgl/Base.hpp). Only the ones we currently use.
// Arrows live in the 0xE03x band (after PageUp/Down/End/Home); modifiers are
// up at 0xE05x — easy to confuse, double-check the header before adding more.
// ASCII keys (Z, X, etc.) just use their character code directly.
export const KEY_BACKSPACE = 0x00000008;
export const KEY_ENTER     = 0x0000000D;
export const KEY_ESCAPE    = 0x0000001B;
export const KEY_LEFT      = 0xE035;
export const KEY_UP        = 0xE036;
export const KEY_RIGHT     = 0xE037;
export const KEY_DOWN      = 0xE038;
export const KEY_SHIFT_L   = 0xE051;
export const KEY_SHIFT_R   = 0xE052;

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
 * Edit here to rebind — no C++ rebuild required.
 */
export function mapKeyToGameboyButton(key: number): GameboyButton | null {
    switch (key) {
        case KEY_LEFT:           return GameboyButton.Left;
        case KEY_RIGHT:          return GameboyButton.Right;
        case KEY_UP:             return GameboyButton.Up;
        case KEY_DOWN:           return GameboyButton.Down;
        case KEY_ENTER:          return GameboyButton.Start;
        case KEY_SHIFT_L:
        case KEY_SHIFT_R:
        case KEY_BACKSPACE:      return GameboyButton.Select;
        case 0x7A: // 'z'
        case 0x5A: // 'Z'
            return GameboyButton.A;
        case 0x78: // 'x'
        case 0x58: // 'X'
            return GameboyButton.B;
        default:                 return null;
    }
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
