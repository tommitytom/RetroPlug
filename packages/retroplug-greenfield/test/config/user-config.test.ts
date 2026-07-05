// User config (config.json): the model/serialization tolerance + the Backend-backed
// store (first-run defaults, validated setters, atomic persist, no-op guard). Mirrors
// the recent-store tests. The on-disk shape + field spellings match native's
// UserConfigJson, so a real config.json round-trips.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { UserConfigStore } from "../../src/userConfigStore";
import { parseUserConfig, serializeUserConfig, USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG, type SramMirror } from "../../src/userConfig";

const CONFIG = "/config/config.json";

// --- serialization ---

test("parse: an empty object yields the defaults", () => {
  expect(parseUserConfig("{}")).toEqual(DEFAULT_USER_CONFIG);
});

test("parse: a partial doc fills the rest with defaults (additive tolerance)", () => {
  const cfg = parseUserConfig(JSON.stringify({ activeKeyboardBindings: "wasd" }))!;
  expect(cfg.activeKeyboardBindings).toBe("wasd");
  expect(cfg.activeGamepadBindings).toBe("default");
  expect(cfg.defaultZoom).toBe(3);
  expect(cfg.sramMirror).toBe("OnProjectSave");
});

test("parse: out-of-range zoom clamps and a bogus sramMirror coerces to the default", () => {
  const cfg = parseUserConfig(JSON.stringify({ defaultZoom: 99, sramMirror: "bogus" }))!;
  expect(cfg.defaultZoom).toBe(6); // clamped to max
  expect(cfg.sramMirror).toBe("OnProjectSave");
});

test("parse: a newer schema stamp / malformed / non-object all yield null (keep previous)", () => {
  expect(parseUserConfig(JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 1 }))).toBe(null);
  expect(parseUserConfig("not json")).toBe(null);
  expect(parseUserConfig("[]")).toBe(null);
});

test("serialize: stamps the schema version and round-trips through parse", () => {
  const doc = JSON.parse(serializeUserConfig(DEFAULT_USER_CONFIG));
  expect(doc.schemaVersion).toBe(USER_CONFIG_SCHEMA);
  expect(parseUserConfig(serializeUserConfig(DEFAULT_USER_CONFIG))).toEqual(DEFAULT_USER_CONFIG);
});

// --- store ---

function newStore() {
  const be = new MockBackend("/config");
  let changes = 0;
  const store = new UserConfigStore(be, () => changes++);
  return { be, store, changed: () => changes };
}

test("load: reads an existing config.json into config()", () => {
  const { be, store } = newStore();
  be.seed(CONFIG, serializeUserConfig({ ...DEFAULT_USER_CONFIG, defaultZoom: 5, sramMirror: "Continuous" }));
  store.load();
  expect(store.config().defaultZoom).toBe(5);
  expect(store.sramMirror()).toBe("Continuous");
});

test("load: first run (no file) writes the defaults out", () => {
  const { be, store } = newStore();
  store.load();
  const onDisk = be.readText(CONFIG);
  expect(onDisk !== null).toBeTruthy();
  expect(parseUserConfig(onDisk!)).toEqual(DEFAULT_USER_CONFIG);
  expect(be.log.includes("writeFileAtomic")).toBeTruthy();
});

test("load: a malformed file keeps the in-memory defaults", () => {
  const { be, store } = newStore();
  be.seed(CONFIG, "garbage");
  store.load();
  expect(store.config()).toEqual(DEFAULT_USER_CONFIG);
});

test("setDefaultZoom: accepts 1..6 (persists + notifies); rejects out-of-range / non-integer", () => {
  const { be, store, changed } = newStore();
  expect(store.setDefaultZoom(5)).toBeTruthy();
  expect(store.defaultZoom()).toBe(5);
  expect(changed()).toBe(1);
  expect(parseUserConfig(be.readText(CONFIG)!)!.defaultZoom).toBe(5); // persisted
  expect(store.setDefaultZoom(0)).toBeFalsy();
  expect(store.setDefaultZoom(7)).toBeFalsy();
  expect(store.setDefaultZoom(3.5)).toBeFalsy();
  expect(store.defaultZoom()).toBe(5); // unchanged by the rejects
  expect(changed()).toBe(1); // no extra notifications
});

test("setSramMirror: accepts a known mode, rejects an unknown one", () => {
  const { store } = newStore();
  expect(store.setSramMirror("Off")).toBeTruthy();
  expect(store.sramMirror()).toBe("Off");
  expect(store.setSramMirror("bogus" as SramMirror)).toBeFalsy();
  expect(store.sramMirror()).toBe("Off");
});

test("setActive*: persist the active profile names", () => {
  const { be, store } = newStore();
  expect(store.setActiveKeyboardBindings("wasd")).toBeTruthy();
  expect(store.setActiveGamepadBindings("pad2")).toBeTruthy();
  const cfg = parseUserConfig(be.readText(CONFIG)!)!;
  expect(cfg.activeKeyboardBindings).toBe("wasd");
  expect(cfg.activeGamepadBindings).toBe("pad2");
});

test("no-op guard: setting the current value doesn't write or notify", () => {
  const { be, store, changed } = newStore();
  store.load(); // first-run write of the defaults (no onChange)
  const writesBefore = be.log.filter((m) => m === "writeFileAtomic").length;
  expect(store.setDefaultZoom(DEFAULT_USER_CONFIG.defaultZoom)).toBeFalsy();
  expect(store.setSramMirror(DEFAULT_USER_CONFIG.sramMirror)).toBeFalsy();
  expect(changed()).toBe(0);
  expect(be.log.filter((m) => m === "writeFileAtomic").length).toBe(writesBefore); // no extra writes
});
