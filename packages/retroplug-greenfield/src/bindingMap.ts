// A keyboard + gamepad button map — the content of one bindings/<name>.json profile
// (native's BindingMapJson, config/UserConfigSerialization.hpp). Each channel maps a
// Game Boy button name ("Right"/"Left"/"Up"/"Down"/"A"/"B"/"Select"/"Start") to a list
// of symbolic key or gamepad-button names (multi-bind on purpose). This is the model +
// its zod validator + the hardcoded default; parse/serialize live in
// bindingSerialization.ts and the profile CRUD in bindingsStore.ts.

import { z, stringField } from "./configSchema";

/** One binding profile: a display name + the two channel maps. `schemaVersion` is a
 *  serialization concern (see bindingSerialization.ts), not part of the model. */
export interface BindingMap {
  name: string;
  keyboard: Record<string, string[]>;
  gamepad: Record<string, string[]>;
}

// A channel map: button-name → list of key names. Tolerant — a bad/missing channel
// becomes an empty map rather than failing the whole profile.
const channel = () => z.record(z.string(), z.array(z.string())).catch(() => ({}));

/** Validates + defaults a (possibly partial/stale) profile object. Strict: unknown keys
 *  are stripped; a missing name defaults to "default"; a missing/bad channel → {}. Note
 *  the EMPTY-map default is the struct default — the seeded defaults are defaultBindingMap(). */
export const bindingMapSchema = z.object({
  name: stringField("default"),
  keyboard: channel(),
  gamepad: channel(),
});

/** The built-in "default" profile — the exact native defaultBindingMap() values, the
 *  seed for bindings/default.json and the fallback when an active profile is absent. */
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
      Right: ["dpright"],
      Left: ["dpleft"],
      Up: ["dpup"],
      Down: ["dpdown"],
      A: ["a"],
      B: ["b"],
      Start: ["start"],
      Select: ["back"],
    },
  };
}
