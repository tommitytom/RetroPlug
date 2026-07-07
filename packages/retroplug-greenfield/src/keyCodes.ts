// DPF keyboard codes → Game Boy joypad buttons — the greenfield game-input resolver.
//
// The "key" event bus delivers raw DPF/DGL key codes (arrows in the 0xE03x band, ASCII as its
// codepoint). The bindings model (bindingMap.ts / BindingsStore.resolvedBindings) speaks SYMBOLIC key
// names ("Right"/"Z"/"ShiftL"), keyed by GB button name. This module bridges the two: a compact
// name→code table (ported from the legacy shell's input.ts) plus buildKeyToButton, which inverts a
// resolved keyboard map into a DPF-code → button-value lookup the UI hook indexes per keystroke.
//
// The button values mirror native InputTypes.hpp (Right=0 … Start=7) — the raw uint8 pressButton
// carries across the bridge, so they must not drift.

/** GB button name → the native GameboyButton value ([InputTypes.hpp] Right=0 … Start=7). */
export const BUTTON_VALUE: Record<string, number> = {
  Right: 0,
  Left: 1,
  Up: 2,
  Down: 3,
  A: 4,
  B: 5,
  Select: 6,
  Start: 7,
};

// DPF key codes for the named (non-ASCII) keys. ASCII keys ("Z"/"x"/…) resolve via charCodeAt.
export const KEY_BACKSPACE = 0x08;
export const KEY_TAB = 0x09;
export const KEY_ENTER = 0x0d;
export const KEY_ESCAPE = 0x1b;
export const KEY_LEFT = 0xe035;
export const KEY_UP = 0xe036;
export const KEY_RIGHT = 0xe037;
export const KEY_DOWN = 0xe038;
export const KEY_SHIFT_L = 0xe051;
export const KEY_SHIFT_R = 0xe052;

// Symbolic key name (as stored in bindings JSON) → DPF code. Single-char ASCII names are resolved by
// charCodeAt in resolveKeyName, so only the named keys need an entry (Return is a synonym of Enter).
const KEY_NAME_TO_DPF: Record<string, number> = {
  Backspace: KEY_BACKSPACE,
  Tab: KEY_TAB,
  Enter: KEY_ENTER,
  Return: KEY_ENTER,
  Escape: KEY_ESCAPE,
  Left: KEY_LEFT,
  Up: KEY_UP,
  Right: KEY_RIGHT,
  Down: KEY_DOWN,
  ShiftL: KEY_SHIFT_L,
  ShiftR: KEY_SHIFT_R,
};

/** A symbolic key name → its DPF code, or null when unknown. Single-char names use their codepoint. */
export function resolveKeyName(name: string): number | null {
  if (name in KEY_NAME_TO_DPF) return KEY_NAME_TO_DPF[name];
  if (name.length === 1) return name.charCodeAt(0);
  return null;
}

/** Invert a resolved keyboard binding map (GB-button-name → key-name[]) into a DPF-code → button-value
 *  lookup. Unknown key names and button names are skipped; a later binding for the same code wins. */
export function buildKeyToButton(keyboard: Record<string, string[]>): Map<number, number> {
  const map = new Map<number, number>();
  for (const [buttonName, keyNames] of Object.entries(keyboard)) {
    const value = BUTTON_VALUE[buttonName];
    if (value === undefined) continue;
    for (const keyName of keyNames) {
      const code = resolveKeyName(keyName);
      if (code !== null) map.set(code, value);
    }
  }
  return map;
}
