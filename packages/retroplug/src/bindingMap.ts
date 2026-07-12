// A keyboard + gamepad button map — the content of one bindings/<name>.json profile
// (native's BindingMapJson, config/UserConfigSerialization.hpp). Each channel maps a
// Game Boy button name ("Right"/"Left"/"Up"/"Down"/"A"/"B"/"Select"/"Start") to a list
// of symbolic key or gamepad-button names (multi-bind on purpose). This is the model +
// its zod validator + the hardcoded default; parse/serialize live in
// bindingSerialization.ts and the profile CRUD in bindingsStore.ts.

import { z, stringField } from "./configSchema";

/** One binding profile: a display name, the two GB-button channel maps, and the two app-action channel maps
 *  (open the menu / cycle instances — keyed by AppAction id rather than GB button). `schemaVersion` is a
 *  serialization concern (see bindingSerialization.ts), not part of the model. */
export interface BindingMap {
  name: string;
  keyboard: Record<string, string[]>;
  gamepad: Record<string, string[]>;
  keyboardActions: Record<string, string[]>;
  gamepadActions: Record<string, string[]>;
}

// A channel map: button-name → list of key names. Tolerant — a bad/missing channel
// becomes an empty map rather than failing the whole profile.
const channel = () => z.record(z.string(), z.array(z.string())).catch(() => ({}));

/** The seeded app-action defaults, per channel (AppAction id → key/pad names). Open Menu keeps the historical
 *  Esc / leftshoulder; Cycle (next) defaults to Tab / rightshoulder; Cycle (Back) ships unbound (no clean
 *  single key for "previous", and L1/R1 are taken) but is bindable in the editor. */
export const DEFAULT_KEYBOARD_ACTIONS: Record<string, string[]> = { OpenMenu: ["Escape"], CycleNext: ["Tab"], CyclePrev: [] };
export const DEFAULT_GAMEPAD_ACTIONS: Record<string, string[]> = { OpenMenu: ["leftshoulder"], CycleNext: ["rightshoulder"], CyclePrev: [] };

// An app-action channel: like `channel()` but SEEDED — a missing section (an older profile that predates app
// actions) defaults to the seed so Esc / leftshoulder keep opening the menu. `.default` fires only on
// `undefined`, so a section the user deliberately cleared ({}) is preserved, not re-seeded.
const actionChannel = (seed: Record<string, string[]>) =>
  z.record(z.string(), z.array(z.string())).catch(() => ({})).default(() => ({ ...seed }));

/** Validates + defaults a (possibly partial/stale) profile object. Strict: unknown keys
 *  are stripped; a missing name defaults to "default"; a missing/bad GB channel → {}; a missing app-action
 *  channel → its seed (see actionChannel). The seeded full defaults are defaultBindingMap(). */
export const bindingMapSchema = z.object({
  name: stringField("default"),
  keyboard: channel(),
  gamepad: channel(),
  keyboardActions: actionChannel(DEFAULT_KEYBOARD_ACTIONS),
  gamepadActions: actionChannel(DEFAULT_GAMEPAD_ACTIONS),
});

/** The built-in "default" profile — the seed for bindings/default.json and the fallback when an active
 *  profile is absent. Keyboard mirrors native's defaultBindingMap(); the gamepad channel additionally binds
 *  the LEFT STICK alongside the d-pad hat (the `<axis><sign>` tokens — TS-only, native binds only the
 *  hat) so analog-as-dpad works out of the box. See keyCodes.ts `axisToken` for the token convention. */
export function defaultBindingMap(): BindingMap {
  return {
    name: "default",
    keyboard: {
      Right: ["Right"],
      Left: ["Left"],
      Up: ["Up"],
      Down: ["Down"],
      A: ["Z", "z"],
      B: ["X", "x"],
      Start: ["Enter"],
      Select: ["ShiftL", "ShiftR", "Backspace"],
    },
    gamepad: {
      Right: ["dpright", "leftx+"],
      Left: ["dpleft", "leftx-"],
      Up: ["dpup", "lefty-"],
      Down: ["dpdown", "lefty+"],
      A: ["a"],
      B: ["b"],
      Start: ["start"],
      Select: ["back"],
    },
    keyboardActions: { ...DEFAULT_KEYBOARD_ACTIONS },
    gamepadActions: { ...DEFAULT_GAMEPAD_ACTIONS },
  };
}
