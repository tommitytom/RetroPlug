// A keyboard + gamepad button map — the content of one bindings/<name>.json profile
// (native's BindingMapJson, config/UserConfigSerialization.hpp). Each channel maps a
// Game Boy button name ("Right"/"Left"/"Up"/"Down"/"A"/"B"/"Select"/"Start") to a list
// of symbolic key or gamepad-button names (multi-bind on purpose). This is the model +
// its zod validator + the hardcoded default; parse/serialize live in
// bindingSerialization.ts and the profile CRUD in bindingsStore.ts.

import { z } from "zod";
import { stringField } from "@retroplug/core/configFields";
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

/** A contributed action's defaults: the two channels' own names for what a UI contribution declares (src/ knows no
 *  contribution, so this is the shape it reads, and @retroplug/ui's UiAction is assignable to it). */
export interface ActionDefaults {
  readonly id: string;
  readonly keys?: readonly string[];
  readonly pad?: readonly string[];
}

/** Fill a stored profile's two app-action sections with the contributed actions' defaults
 *  (docs/modular-graph-phase-f4c-design.md section 5.6). A stored profile has no entry for a contributed id until the
 *  user edits one, and an id PRESENT in a section is the user's, `[]` included: a cleared binding is a choice, as
 *  CyclePrev's empty default is.
 *
 *  A pure function at the two readers (App builds its maps through it, the bindings menus read theirs through it), not
 *  a store change: AppStores is shared by the control plane and the UI, the contributions are UI-only, and the file
 *  on disk is not rewritten until the user edits a binding (the menus load raw and save only what was edited). */
export function withActionDefaults(map: BindingMap, actions: readonly ActionDefaults[]): BindingMap {
  const keyboardActions = { ...map.keyboardActions };
  const gamepadActions = { ...map.gamepadActions };
  for (const action of actions) {
    if (!(action.id in keyboardActions)) keyboardActions[action.id] = [...(action.keys ?? [])];
    if (!(action.id in gamepadActions)) gamepadActions[action.id] = [...(action.pad ?? [])];
  }
  return { ...map, keyboardActions, gamepadActions };
}

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
