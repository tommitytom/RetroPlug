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

// The inverse of KEY_NAME_TO_DPF, canonical name per code (Enter, not its Return synonym). Used by the
// bindings editor to turn a captured DPF code back into the symbolic name it stores.
const DPF_TO_KEY_NAME: Record<number, string> = {
  [KEY_BACKSPACE]: "Backspace",
  [KEY_TAB]: "Tab",
  [KEY_ENTER]: "Enter",
  [KEY_ESCAPE]: "Escape",
  [KEY_LEFT]: "Left",
  [KEY_UP]: "Up",
  [KEY_RIGHT]: "Right",
  [KEY_DOWN]: "Down",
  [KEY_SHIFT_L]: "ShiftL",
  [KEY_SHIFT_R]: "ShiftR",
};

/** A captured DPF code → the symbolic name to store, or null when unbindable. Named keys resolve from the
 *  table; printable ASCII (0x20..0x7e) becomes its character. The round-trip inverse of resolveKeyName, so
 *  a captured key re-resolves to the same code in buildKeyToButton. */
export function dpfCodeToKeyName(code: number): string | null {
  if (code in DPF_TO_KEY_NAME) return DPF_TO_KEY_NAME[code];
  if (code >= 0x20 && code <= 0x7e) return String.fromCharCode(code);
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

/** Invert a resolved gamepad binding map (GB-button-name → gamepad-button-name[]) into a
 *  gamepad-button-name → button-value lookup. Unlike buildKeyToButton there is no code table: the
 *  gamepad names ("dpright"/"a"/"start") ARE the raw names the "gamepad-button" bus delivers (SDL
 *  canonical), so they map straight onto BUTTON_VALUE. Unknown button names are skipped; a later
 *  binding for the same gamepad name wins. */
export function buildGamepadToButton(gamepad: Record<string, string[]>): Map<string, number> {
  const map = new Map<string, number>();
  for (const [buttonName, padNames] of Object.entries(gamepad)) {
    const value = BUTTON_VALUE[buttonName];
    if (value === undefined) continue;
    for (const padName of padNames) map.set(padName, value);
  }
  return map;
}

/** Which half-axis token (or "" = centered) a stick axis is in, with hysteresis so it doesn't chatter at
 *  the boundary. A token is `<axisName><sign>` and lives in the gamepad map like a button name (SDL
 *  convention: X+ = right, Y+ = down → "leftx+"=Right, "leftx-"=Left, "lefty+"=Down, "lefty-"=Up). `current`
 *  is the token currently held for this axis: press when |value| ≥ 0.5, then hold that direction until value
 *  falls back under 0.4 on the SAME side (so a flip through centre releases before the opposite direction
 *  presses). The native poll already dead-zone-clips to 0 at centre, which reads here as release. */
export function axisToken(axisName: string, value: number, current: string): string {
  const PRESS = 0.5;
  const RELEASE = 0.4;
  if (value <= -PRESS) return `${axisName}-`;
  if (value >= PRESS) return `${axisName}+`;
  if (current === `${axisName}+` && value >= RELEASE) return current; // still pushed positive within hysteresis
  if (current === `${axisName}-` && value <= -RELEASE) return current; // still pushed negative within hysteresis
  return "";
}

/** A menu-navigation action: cursor moves (up/down), value cycles (left/right), and the confirm/back
 *  buttons — the semantic vocabulary the Menu maps onto its keyboard nav primitives. */
export type MenuNav = "up" | "down" | "left" | "right" | "select" | "back";

/** A controller button's fixed menu role, or null when it doesn't drive the menu. These are FIXED SDL
 *  names (independent of the user's gameplay bindings), mirroring how keyboard menu nav uses fixed
 *  arrows/Enter regardless of game keybinds: d-pad moves + cycles, A confirms, B backs out. */
export function menuNavForButton(name: string): MenuNav | null {
  switch (name) {
    case "dpup":
      return "up";
    case "dpdown":
      return "down";
    case "dpleft":
      return "left";
    case "dpright":
      return "right";
    case "a":
      return "select";
    case "b":
      return "back";
    default:
      return null;
  }
}

/** The menu direction a resolved half-axis token drives, or null. Only the LEFT stick navigates (the right
 *  stick + triggers are ignored); tokens follow the SDL convention (Y− = up, Y+ = down, X∓ = left/right). */
export function menuNavForAxisToken(token: string): MenuNav | null {
  switch (token) {
    case "lefty-":
      return "up";
    case "lefty+":
      return "down";
    case "leftx-":
      return "left";
    case "leftx+":
      return "right";
    default:
      return null;
  }
}
