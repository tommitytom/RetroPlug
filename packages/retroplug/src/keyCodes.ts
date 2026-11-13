// DPF keyboard codes → Game Boy joypad buttons — the game-input resolver.
//
// The "key" event bus delivers raw DPF/DGL key codes (arrows in the 0xE03x band, ASCII as its
// codepoint). The bindings model (bindingMap.ts / BindingsStore.resolvedBindings) speaks SYMBOLIC key
// names ("Right"/"Z"/"ShiftL"), keyed by GB button name. This module bridges the two: a compact
// name→code table (ported from the legacy shell's input.ts) plus buildKeyToButton, which inverts a
// resolved keyboard map into a DPF-code → button-value lookup the UI hook indexes per keystroke.
//
// The button values mirror native InputTypes.hpp (Right=0 … Start=7) — the raw uint8 pressButton
// carries across the bridge, so they must not drift. The bytes past Start belong to whichever console
// has more than eight buttons on its pad (the GBA's L/R are 8/9 of ITS table, the Mega Drive's
// X/Y/Z/Mode are 8..11 of its own), so a press byte is only ever read by the console it was sent to.
// The four Mega Drive extras are named here so a user can bind them; the seeded profile leaves them
// unbound, so no default binding of any console changes (see bindingMap.ts defaultBindingMap()).

/** Button name → the wire byte native's InputTypes.hpp table of the running console assigns (the shared
 *  eight are Right=0 … Start=7 on every one; the extras are per console, so L/R name the GBA's 8/9 and
 *  X/Y/Z/Mode the Mega Drive's 8..11). The raw uint8 pressButton carries across the bridge. */
export const BUTTON_VALUE: Record<string, number> = {
  Right: 0,
  Left: 1,
  Up: 2,
  Down: 3,
  A: 4,
  B: 5,
  Select: 6,
  Start: 7,
  // The 6-button Mega Drive pad's extras. A console whose table has no such button drops the byte rather
  // than falling through onto one it does have, so naming them here binds nothing on its own.
  X: 8,
  Y: 9,
  Z: 10,
  Mode: 11,
};

// DPF key codes for the named (non-ASCII) keys. ASCII keys ("Z"/"x"/…) resolve via charCodeAt.
export const KEY_BACKSPACE = 0x08;
export const KEY_TAB = 0x09;
export const KEY_ENTER = 0x0d;
export const KEY_ESCAPE = 0x1b;
export const KEY_DELETE = 0x7f;
export const KEY_SPACE = 0x20;
export const KEY_PAGE_UP = 0xe031;
export const KEY_PAGE_DOWN = 0xe032;
export const KEY_LEFT = 0xe035;
export const KEY_UP = 0xe036;
export const KEY_RIGHT = 0xe037;
export const KEY_DOWN = 0xe038;
export const KEY_SHIFT_L = 0xe051;
export const KEY_SHIFT_R = 0xe052;

// Symbolic key name (as stored in bindings JSON) → DPF code. Single-char ASCII names are resolved by
// charCodeAt in resolveKeyName, so only the named keys need an entry (Return is a synonym of Enter).
// Space is the one printable key with no visible glyph, so it gets a word rather than the ASCII fallback
// (which would store + show a literal " " — an apparently unbound row).
const KEY_NAME_TO_DPF: Record<string, number> = {
  Backspace: KEY_BACKSPACE,
  Tab: KEY_TAB,
  Enter: KEY_ENTER,
  Return: KEY_ENTER,
  Escape: KEY_ESCAPE,
  Space: KEY_SPACE,
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
  [KEY_SPACE]: "Space",
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

/** A stored key name → the name to SHOW in the bindings editor. Canonicalises through the two tables, so a
 *  profile that stored the raw " " (written before Space had a name) still reads "Space" rather than looking
 *  unbound. Names that don't resolve pass through unchanged. */
export function keyDisplayName(name: string): string {
  const code = resolveKeyName(name);
  return code === null ? name : (dpfCodeToKeyName(code) ?? name);
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

/** An app-level (non-GB) action a key or controller button can be bound to: open/close the menu, and cycle
 *  which instance is focused (forward / back). These live in the separate keyboardActions/gamepadActions
 *  binding sections, so they never collide with the GB-button resolvers above. A package contributes more of
 *  them (docs/modular-graph-phase-f4c-design.md section 3.2); the shell's three keep their bare ids. */
export type AppAction = "OpenMenu" | "CycleNext" | "CyclePrev";
const APP_ACTIONS: readonly AppAction[] = ["OpenMenu", "CycleNext", "CyclePrev"];

/** Whether an id is one of the shell's three app actions (not a contribution's, which carries a package prefix). */
export function isAppAction(id: string): id is AppAction {
  return (APP_ACTIONS as readonly string[]).includes(id);
}

/** Every name the native GamepadManager hands the "gamepad-button" bus: SDL's canonical button names, from
 *  SDL_GameControllerGetStringForButton over SDL_CONTROLLER_BUTTON_* in order (GamepadManager.cpp is the one emitter,
 *  so this is SDL's set and not ours). A stick half-axis token ("leftx+") is NOT a button: the axis bus dispatches no
 *  app action, so a default on one could never fire, which is why the set excludes it. */
export const GAMEPAD_BUTTON_NAMES: readonly string[] = [
  "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
  "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
];

/** Add one action id to a key's list: a key keeps at most ONE of the shell's own ids, a later shell binding
 *  replacing the earlier in place (what the old Map.set did, so a key two shell actions share fires what it fires
 *  today), while contributed ids accumulate, and an id the list already holds is skipped (a hand-edited
 *  `OpenMenu: ["Enter", "Return"]` is one code once). */
function addActionId<K>(map: Map<K, string[]>, key: K, id: string): void {
  const ids = map.get(key);
  if (ids === undefined) {
    map.set(key, [id]);
    return;
  }
  if (ids.includes(id)) return;
  if (!isAppAction(id)) {
    ids.push(id);
    return;
  }
  const at = ids.findIndex((held) => isAppAction(held));
  if (at === -1) ids.push(id);
  else ids[at] = id;
}

/** Invert a keyboard app-action map (action id → key-name[]) into a DPF-code → action-id LIST, the app-action twin
 *  of buildKeyToButton. A key can carry several actions (the shell's and a package's, or two packages'), and the list
 *  keeps the order the profile gives them, since a press fires all of them. Null-tolerant (an older profile may lack
 *  the section); an id that is neither the shell's nor in `contributed` is skipped, as an unresolvable key name is. */
export function buildKeyToAction(
  actions: Record<string, string[]> | undefined,
  contributed: readonly string[] = [],
): Map<number, string[]> {
  const map = new Map<number, string[]>();
  if (!actions) return map;
  for (const [id, keyNames] of Object.entries(actions)) {
    if (!isAppAction(id) && !contributed.includes(id)) continue;
    for (const keyName of keyNames) {
      const code = resolveKeyName(keyName);
      if (code === null) continue;
      addActionId(map, code, id);
    }
  }
  return map;
}

/** The gamepad twin: SDL-button-name → action-id list (the names ARE the raw "gamepad-button" bus names, so no code
 *  table, like buildGamepadToButton). Null-tolerant; the same "known" rule as the keyboard map. */
export function buildGamepadToAction(
  actions: Record<string, string[]> | undefined,
  contributed: readonly string[] = [],
): Map<string, string[]> {
  const map = new Map<string, string[]>();
  if (!actions) return map;
  for (const [id, padNames] of Object.entries(actions)) {
    if (!isAppAction(id) && !contributed.includes(id)) continue;
    for (const padName of padNames) addActionId(map, padName, id);
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
