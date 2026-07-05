// Binding map: the model + serialization tolerance + profile-name validation. The
// on-disk shape + field spellings + defaultBindingMap values match native's
// BindingMapJson, so a real bindings/*.json round-trips.
import { test, expect } from "../../testing/harness";
import { defaultBindingMap } from "../../src/bindingMap";
import { parseBindingMap, serializeBindingMap, BINDINGS_SCHEMA } from "../../src/bindingSerialization";
import { isValidProfileName } from "../../src/bindingsStore";

test("defaultBindingMap: the 8 buttons per channel with native values", () => {
  const d = defaultBindingMap();
  expect(d.name).toBe("default");
  expect(Object.keys(d.keyboard).sort()).toEqual(["A", "B", "Down", "Left", "Right", "Select", "Start", "Up"]);
  expect(d.keyboard.A).toEqual(["Z", "z"]);
  expect(d.keyboard.Select).toEqual(["ShiftL", "ShiftR", "Backspace"]);
  expect(d.gamepad.Start).toEqual(["start"]);
  expect(d.gamepad.Select).toEqual(["back"]);
});

test("parse: a partial doc fills defaults; unknowns stripped; a bad channel → {}", () => {
  const m = parseBindingMap(JSON.stringify({ keyboard: { A: ["Q"] }, gamepad: "nope", extra: 1 }))!;
  expect(m.name).toBe("default"); // missing name → default
  expect(m.keyboard).toEqual({ A: ["Q"] });
  expect(m.gamepad).toEqual({}); // bad channel coerced to empty
  expect((m as Record<string, unknown>).extra).toBe(undefined); // unknown stripped
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
