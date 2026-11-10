// Binding map: the model + serialization tolerance + profile-name validation. The
// on-disk shape + field spellings + defaultBindingMap values match native's
// BindingMapJson, so a real bindings/*.json round-trips.
import { test, expect } from "@retroplug/testing";
import { defaultBindingMap, withActionDefaults } from "../../src/bindingMap";
import { parseBindingMap, serializeBindingMap, BINDINGS_SCHEMA } from "../../src/bindingSerialization";
import { isValidProfileName } from "../../src/bindingsStore";

test("defaultBindingMap: the 8 buttons per channel with native values, plus the seeded app actions", () => {
  const d = defaultBindingMap();
  expect(d.name).toBe("default");
  expect(Object.keys(d.keyboard).sort()).toEqual(["A", "B", "Down", "Left", "Right", "Select", "Start", "Up"]);
  expect(d.keyboard.A).toEqual(["Z", "z"]);
  expect(d.keyboard.Select).toEqual(["ShiftL", "ShiftR", "Backspace"]);
  expect(d.gamepad.Start).toEqual(["start"]);
  expect(d.gamepad.Select).toEqual(["back"]);
  // App actions: Open Menu = Esc / L1, Cycle (next) = Tab / R1, Cycle (Back) unbound.
  expect(d.keyboardActions.OpenMenu).toEqual(["Escape"]);
  expect(d.keyboardActions.CycleNext).toEqual(["Tab"]);
  expect(d.keyboardActions.CyclePrev).toEqual([]);
  expect(d.gamepadActions.OpenMenu).toEqual(["leftshoulder"]);
  expect(d.gamepadActions.CycleNext).toEqual(["rightshoulder"]);
});

test("parse: a partial doc fills defaults; unknowns stripped; a bad channel → {}", () => {
  const m = parseBindingMap(JSON.stringify({ keyboard: { A: ["Q"] }, gamepad: "nope", extra: 1 }))!;
  expect(m.name).toBe("default"); // missing name → default
  expect(m.keyboard).toEqual({ A: ["Q"] });
  expect(m.gamepad).toEqual({}); // bad channel coerced to empty
  expect((m as unknown as Record<string, unknown>).extra).toBe(undefined); // unknown stripped
  // A profile predating app actions seeds them, so Esc / leftshoulder keep opening the menu.
  expect(m.keyboardActions).toEqual(defaultBindingMap().keyboardActions);
  expect(m.gamepadActions).toEqual(defaultBindingMap().gamepadActions);
});

test("parse: an app-action section the user cleared stays cleared (not re-seeded)", () => {
  // Present-but-empty ≠ missing: `.default` only fires on undefined, so a deliberately emptied section persists.
  const m = parseBindingMap(JSON.stringify({ keyboardActions: {}, gamepadActions: { OpenMenu: ["guide"] } }))!;
  expect(m.keyboardActions).toEqual({}); // stays cleared — Esc no longer opens the menu for this profile
  expect(m.gamepadActions.OpenMenu).toEqual(["guide"]); // a customized value round-trips
});

test("parse: malformed / non-object / newer schema all yield null", () => {
  expect(parseBindingMap("not json")).toBe(null);
  expect(parseBindingMap("[]")).toBe(null);
  expect(parseBindingMap(JSON.stringify({ schemaVersion: BINDINGS_SCHEMA + 1 }))).toBe(null);
});

test("serialize: stamps the schema version and round-trips through parse", () => {
  const doc = JSON.parse(serializeBindingMap(defaultBindingMap()));
  expect(doc.schemaVersion).toBe(BINDINGS_SCHEMA);
  expect(parseBindingMap(serializeBindingMap(defaultBindingMap()))).toEqual(defaultBindingMap());
});

test("serialize: a NON-default app action round-trips (guards the serialize path)", () => {
  // The default round-trip above can't catch a dropped section — parse would re-seed it. A customized value
  // only survives if serialize actually emits keyboardActions/gamepadActions.
  const custom = { ...defaultBindingMap(), keyboardActions: { OpenMenu: ["F1"] }, gamepadActions: { CyclePrev: ["leftstick"] } };
  const back = parseBindingMap(serializeBindingMap(custom))!;
  expect(back.keyboardActions.OpenMenu).toEqual(["F1"]);
  expect(back.gamepadActions.CyclePrev).toEqual(["leftstick"]);
});

test("isValidProfileName: allows names, rejects empty / reserved / bad chars", () => {
  expect(isValidProfileName("default")).toBeTruthy();
  expect(isValidProfileName("wasd-2")).toBeTruthy();
  expect(isValidProfileName("A_b")).toBeTruthy();
  expect(isValidProfileName("")).toBeFalsy();
  expect(isValidProfileName("config")).toBeFalsy(); // would collide with config.json
  expect(isValidProfileName("has space")).toBeFalsy();
  expect(isValidProfileName("a.b")).toBeFalsy();
  expect(isValidProfileName("a/b")).toBeFalsy();
});

test("withActionDefaults: an absent id takes the action's default, a present one (a key or []) is the user's", () => {
  const map = defaultBindingMap();
  const filled = withActionDefaults(map, [
    { id: "lsdj.debug-overlay", keys: ["`"] },
    { id: "risa.debug-overlay", keys: ["`"], pad: ["misc1"] },
  ]);
  // Absent from both sections: the action's own defaults, and no pad default reads as an empty list.
  expect(filled.keyboardActions["lsdj.debug-overlay"]).toEqual(["`"]);
  expect(filled.keyboardActions["risa.debug-overlay"]).toEqual(["`"]);
  expect(filled.gamepadActions["lsdj.debug-overlay"]).toEqual([]);
  expect(filled.gamepadActions["risa.debug-overlay"]).toEqual(["misc1"]);
  // The shell's own three are untouched, and the profile is a new object: both channels are copied, not mutated.
  expect(filled.keyboardActions.OpenMenu).toEqual(["Escape"]);
  expect(map.keyboardActions["lsdj.debug-overlay"]).toBe(undefined);

  // A present entry is the user's, `[]` included (a cleared binding is a choice, as CyclePrev's is).
  const mine = { ...map, keyboardActions: { ...map.keyboardActions, "lsdj.debug-overlay": ["F"] } };
  expect(withActionDefaults(mine, [{ id: "lsdj.debug-overlay", keys: ["`"] }]).keyboardActions[
    "lsdj.debug-overlay"
  ]).toEqual(["F"]);
  const cleared = { ...map, keyboardActions: { ...map.keyboardActions, "lsdj.debug-overlay": [] } };
  expect(withActionDefaults(cleared, [{ id: "lsdj.debug-overlay", keys: ["`"] }]).keyboardActions[
    "lsdj.debug-overlay"
  ]).toEqual([]);

  // No contributed actions at all is the profile unchanged, and a profile with an id no action names keeps it.
  expect(withActionDefaults(map, []).keyboardActions).toEqual(map.keyboardActions);
  const stale = { ...map, gamepadActions: { ...map.gamepadActions, "gone.action": ["misc1"] } };
  expect(withActionDefaults(stale, [{ id: "lsdj.debug-overlay" }]).gamepadActions["gone.action"]).toEqual(["misc1"]);
});
