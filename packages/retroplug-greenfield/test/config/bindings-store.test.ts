// BindingsStore: profile CRUD + enumeration + resolution over MockBackend and a real
// UserConfigStore. Covers first-run defaults, the name-forced save/load round-trip,
// rename (repointing the active ref, refusing clobber), delete (refusing the active
// profile), and the synthesized resolved bindings with default fallback.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { UserConfigStore } from "../../src/userConfigStore";
import { BindingsStore } from "../../src/bindingsStore";
import { parseBindingMap } from "../../src/bindingSerialization";
import { defaultBindingMap, type BindingMap } from "../../src/bindingMap";

const DEFAULT_JSON = "/config/bindings/default.json";

function setup() {
  const be = new MockBackend("/config");
  const uc = new UserConfigStore(be);
  const store = new BindingsStore(be, uc);
  return { be, uc, store };
}

// saveProfile forces the embedded name to the filename, so this `name` is intentionally
// wrong to prove that.
const wasd = (): BindingMap => ({
  name: "ignored",
  keyboard: { Left: ["A"], Right: ["D"], Up: ["W"], Down: ["S"] },
  gamepad: {},
  keyboardActions: {},
  gamepadActions: {},
});

test("ensureDefaults: writes bindings/default.json from defaultBindingMap", () => {
  const { be, store } = setup();
  store.ensureDefaults();
  expect(parseBindingMap(be.readText(DEFAULT_JSON)!)).toEqual(defaultBindingMap());
  expect(store.availableProfiles()).toEqual(["default"]);
});

test("save + load: round-trips a profile (name forced to filename); rejects invalid/missing", () => {
  const { store } = setup();
  expect(store.saveProfile("wasd", wasd())).toBeTruthy();
  const loaded = store.loadProfile("wasd")!;
  expect(loaded.name).toBe("wasd"); // forced to the filename, not "ignored"
  expect(loaded.keyboard.Left).toEqual(["A"]);
  expect(store.saveProfile("bad name", wasd())).toBeFalsy(); // invalid name
  expect(store.loadProfile("nope")).toBe(null); // missing file
  expect(store.loadProfile("config")).toBe(null); // reserved name
});

test("availableProfiles: every *.json stem, sorted", () => {
  const { store } = setup();
  store.ensureDefaults();
  store.saveProfile("wasd", wasd());
  store.saveProfile("arrows", wasd());
  expect(store.availableProfiles()).toEqual(["arrows", "default", "wasd"]);
});

test("rename: moves the file, refuses missing-src / existing-dst, repoints the active ref", () => {
  const { uc, store } = setup();
  store.saveProfile("wasd", wasd());
  uc.setActiveKeyboardBindings("wasd"); // make it active

  expect(store.renameProfile("wasd", "wasd2")).toBeTruthy();
  expect(store.loadProfile("wasd")).toBe(null);
  expect(store.loadProfile("wasd2")!.name).toBe("wasd2"); // embedded name rewritten
  expect(uc.config().activeKeyboardBindings).toBe("wasd2"); // active ref repointed

  expect(store.renameProfile("missing", "x")).toBeFalsy(); // no source
  store.saveProfile("a", wasd());
  store.saveProfile("b", wasd());
  expect(store.renameProfile("a", "b")).toBeFalsy(); // dst exists → no clobber
  expect(store.loadProfile("a") !== null).toBeTruthy(); // untouched
});

test("delete: removes a profile but refuses the active one / invalid / missing", () => {
  const { uc, store } = setup();
  store.saveProfile("wasd", wasd());
  expect(store.deleteProfile("wasd")).toBeTruthy();
  expect(store.loadProfile("wasd")).toBe(null);

  store.saveProfile("keep", wasd());
  uc.setActiveGamepadBindings("keep");
  expect(store.deleteProfile("keep")).toBeFalsy(); // active gamepad profile
  expect(store.loadProfile("keep") !== null).toBeTruthy(); // still there

  expect(store.deleteProfile("config")).toBeFalsy(); // reserved/invalid
  expect(store.deleteProfile("nope")).toBeFalsy(); // missing
});

test("resolvedBindings: merges active keyboard + gamepad; default fallback when absent", () => {
  const { uc, store } = setup();
  store.ensureDefaults();
  store.saveProfile("wasd", wasd());
  uc.setActiveKeyboardBindings("wasd"); // keyboard channel from wasd
  uc.setActiveGamepadBindings("default"); // gamepad channel from default

  const r = store.resolvedBindings();
  expect(r.name).toBe("wasd");
  expect(r.keyboard.Left).toEqual(["A"]); // from wasd
  expect(r.gamepad.Start).toEqual(["start"]); // from the default profile

  // A missing active keyboard profile falls back to the built-in default keyboard.
  uc.setActiveKeyboardBindings("ghost");
  expect(store.resolvedBindings().keyboard).toEqual(defaultBindingMap().keyboard);
});

test("resolvedBindings: forwards keyboardActions from the keyboard profile, gamepadActions from the gamepad profile", () => {
  const { uc, store } = setup();
  const mk = (name: string, ka: string[], ga: string[]): BindingMap => ({
    name,
    keyboard: {},
    gamepad: {},
    keyboardActions: { OpenMenu: ka },
    gamepadActions: { OpenMenu: ga },
  });
  store.saveProfile("kb", mk("kb", ["F1"], ["x"]));
  store.saveProfile("gp", mk("gp", ["F9"], ["y"]));
  uc.setActiveKeyboardBindings("kb");
  uc.setActiveGamepadBindings("gp");

  const r = store.resolvedBindings();
  expect(r.keyboardActions.OpenMenu).toEqual(["F1"]); // from the keyboard profile
  expect(r.gamepadActions.OpenMenu).toEqual(["y"]); // from the gamepad profile

  // A missing active keyboard profile falls back to the seeded default actions.
  uc.setActiveKeyboardBindings("ghost");
  expect(store.resolvedBindings().keyboardActions).toEqual(defaultBindingMap().keyboardActions);
});
