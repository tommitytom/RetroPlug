// User config (config.json): the model/serialization tolerance + the Backend-backed
// store (first-run defaults, validated setters, atomic persist, no-op guard). Mirrors
// the recent-store tests. The on-disk shape + field spellings mirror native's
// UserConfigJson (with the mirror preference renamed to sramAutoSave), so a real
// config.json round-trips its shared fields.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { UserConfigStore } from "../../src/userConfigStore";
import { parseUserConfig, parseUserConfigResult, serializeUserConfig, USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG, type SramAutoSave } from "../../src/userConfig";

const CONFIG = "/config/config.json";

/** The shell's own fields of a config: what these cases (the core model) are about. */
const coreOf = (cfg: Record<string, unknown>): Record<string, unknown> =>
  Object.fromEntries(Object.keys(DEFAULT_USER_CONFIG).map((k) => [k, cfg[k]]));

/** A config's bytes with no section blocks: what these cases (the shell's own fields) are about. */
const serialized = (cfg: typeof DEFAULT_USER_CONFIG) => serializeUserConfig({ core: cfg, sections: {} });

// --- serialization ---

test("parse: an empty object yields the defaults", () => {
  expect(parseUserConfig("{}")).toEqual(DEFAULT_USER_CONFIG);
});

test("parse: a partial doc fills the rest with defaults (additive tolerance)", () => {
  const cfg = parseUserConfig(JSON.stringify({ activeKeyboardBindings: "wasd" }))!;
  expect(cfg.activeKeyboardBindings).toBe("wasd");
  expect(cfg.activeGamepadBindings).toBe("default");
  expect(cfg.defaultZoom).toBe(3);
  expect(cfg.sramAutoSave).toBe("OnProjectSave");
});

test("parse: out-of-range zoom clamps and a bogus sramAutoSave coerces to the default", () => {
  const cfg = parseUserConfig(JSON.stringify({ defaultZoom: 99, sramAutoSave: "bogus" }))!;
  expect(cfg.defaultZoom).toBe(6); // clamped to max
  expect(cfg.sramAutoSave).toBe("OnProjectSave");
});

test("parse: a newer schema stamp / malformed / non-object all yield null (keep previous)", () => {
  expect(parseUserConfig(JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 1 }))).toBe(null);
  expect(parseUserConfig("not json")).toBe(null);
  expect(parseUserConfig("[]")).toBe(null);
});

test("parse result: 'from the future' is a DIFFERENT refusal from 'corrupt'", () => {
  // The store writes over a corrupt file (so it heals) but must never write over a newer one,
  // so collapsing these two into one null is what made the overwrite bug possible.
  const newer = parseUserConfigResult(JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 3 }));
  expect(newer.ok).toBeFalsy();
  expect(!newer.ok && newer.reason).toBe("newer");
  expect(!newer.ok && newer.stamped).toBe(USER_CONFIG_SCHEMA + 3); // reported so the warning can name it

  for (const bad of ["not json", "[]", "null"]) {
    const r = parseUserConfigResult(bad);
    expect(r.ok).toBeFalsy();
    expect(!r.ok && r.reason).toBe("malformed");
  }

  const good = parseUserConfigResult(serialized(DEFAULT_USER_CONFIG));
  expect(good.ok).toBeTruthy();
});

test("store: a config.json from a NEWER build is never overwritten by this one", () => {
  const { be, store } = newStore();
  const fromTheFuture = JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 1, defaultZoom: 4, somethingNew: true });
  be.seed(CONFIG, fromTheFuture);
  store.load();
  expect(coreOf(store.config())).toEqual(DEFAULT_USER_CONFIG); // refused, so we run on defaults

  // The setter used to persist those defaults straight over the newer file.
  expect(store.setDefaultZoom(5)).toBeFalsy();
  expect(store.setSramAutoSave("Continuous")).toBeFalsy();
  expect(be.readText(CONFIG)).toBe(fromTheFuture); // byte-identical: untouched
  expect(be.log.includes("writeFileAtomic")).toBeFalsy();
});

test("store: a MALFORMED config.json still heals on the next change", () => {
  const { be, store } = newStore();
  be.seed(CONFIG, "garbage"); // not from the future — just junk we may replace
  store.load();
  expect(store.setDefaultZoom(5)).toBeTruthy();
  expect(parseUserConfig(be.readText(CONFIG)!)!.defaultZoom).toBe(5);
});

test("serialize: stamps the schema version and round-trips through parse", () => {
  const doc = JSON.parse(serialized(DEFAULT_USER_CONFIG));
  expect(doc.schemaVersion).toBe(USER_CONFIG_SCHEMA);
  expect(parseUserConfig(serialized(DEFAULT_USER_CONFIG))).toEqual(DEFAULT_USER_CONFIG);
});

test("render: defaults + a v1 render.lastDir migrates to outputDir (v1→v2)", () => {
  const def = parseUserConfig("{}")!;
  expect(def.render.outputDir).toBe(""); // no chosen folder yet
  expect(def.render.onExists).toBe("overwrite"); // default policy

  // A v1-stamped config with the OLD render.lastDir carries forward to outputDir; the old key is stripped.
  const cfg = parseUserConfig(JSON.stringify({ schemaVersion: 1, render: { lastDir: "/music/out" } }))!;
  expect(cfg.render.outputDir).toBe("/music/out"); // migrated
  expect((cfg.render as unknown as Record<string, unknown>).lastDir).toBe(undefined); // unknown key stripped by zod
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
  be.seed(CONFIG, serialized({ ...DEFAULT_USER_CONFIG, defaultZoom: 5, sramAutoSave: "Continuous" }));
  store.load();
  expect(store.config().defaultZoom).toBe(5);
  expect(store.sramAutoSave()).toBe("Continuous");
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
  expect(coreOf(store.config())).toEqual(DEFAULT_USER_CONFIG);
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

test("setSramAutoSave: accepts a known mode, rejects an unknown one", () => {
  const { store } = newStore();
  expect(store.setSramAutoSave("Continuous")).toBeTruthy();
  expect(store.sramAutoSave()).toBe("Continuous");
  expect(store.setSramAutoSave("bogus" as SramAutoSave)).toBeFalsy();
  expect(store.sramAutoSave()).toBe("Continuous");
});

test("the retired Off mode migrates to OnProjectSave (v2 -> v3)", () => {
  // Off never suppressed the save-path write, so it was OnProjectSave under another name. A v2 config
  // carrying it must land on the mode it actually behaved as, not on zod's silent coercion.
  const v2 = JSON.stringify({ schemaVersion: 2, sramAutoSave: "Off", defaultZoom: 4 });
  const cfg = parseUserConfig(v2)!;
  expect(cfg.sramAutoSave).toBe("OnProjectSave");
  expect(cfg.defaultZoom).toBe(4); // the rest of the config rides through untouched
});

test("setUseNativeFileDialogs: toggles both ways, persists, and notifies (regression: was omitted from serialize)", () => {
  const { be, store, changed } = newStore();
  expect(store.config().useNativeFileDialogs).toBe(true); // default: the host's OS dialog
  // The bug: serialize dropped this field, so commit() saw the change as a no-op and it never flipped.
  expect(store.setUseNativeFileDialogs(false)).toBeTruthy(); // a real change, not a no-op
  expect(store.config().useNativeFileDialogs).toBe(false);
  expect(changed()).toBe(1);
  expect(parseUserConfig(be.readText(CONFIG)!)!.useNativeFileDialogs).toBe(false); // persisted + round-trips
  expect(store.setUseNativeFileDialogs(true)).toBeTruthy(); // and back
  expect(store.config().useNativeFileDialogs).toBe(true);
  expect(changed()).toBe(2);
});

test("serialize includes useNativeFileDialogs (a non-default value survives a round-trip)", () => {
  const cfg = { ...DEFAULT_USER_CONFIG, useNativeFileDialogs: false };
  expect(parseUserConfig(serialized(cfg))).toEqual(cfg);
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
  expect(store.setSramAutoSave(DEFAULT_USER_CONFIG.sramAutoSave)).toBeFalsy();
  expect(changed()).toBe(0);
  expect(be.log.filter((m) => m === "writeFileAtomic").length).toBe(writesBefore); // no extra writes
});

// --- the control surface, as the Launchpad pack's config section ---
//
// The shape was the shell's; it is the pack's now (packages/launchpad/ts/control/configSection.ts), read and written
// through the store's generic section API. These are the same cases, re-pointed: the tolerance, the persistence, the
// no-op guard and the two-writers rule are about the mechanism, so they hold the pad's block as their data.

const CONTROL_SURFACE = "controlSurface";

test("controlSurface: additive, tolerant, and the pair a fresh install carries", () => {
  // Additive, so no migration step: a config.json from before the field parses with the contributor's defaults filled.
  const fresh = new UserConfigStore(new MockBackend("/config"));
  fresh.load();
  expect(fresh.section(CONTROL_SURFACE)).toEqual({ input: "", output: "", enabled: false, device: "" });
  // Tolerant one level up like `render`: a malformed block becomes {} and takes the fields' own defaults, rather than
  // failing the whole file (which would reset the user's zoom, bindings and render picks too).
  const held = (block: unknown) => {
    const be = new MockBackend("/config");
    be.seed(CONFIG, JSON.stringify({ ...JSON.parse(serialized(DEFAULT_USER_CONFIG)), controlSurface: block }));
    const store = new UserConfigStore(be);
    store.load();
    return store.section<{ input: string; enabled: boolean; device: string }>(CONTROL_SURFACE);
  };
  expect(held(null).enabled).toBe(false);
  expect(held([1, 2]).input).toBe("");
  expect(held({ input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: "yes", device: 5 })).toEqual({
    input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: false, device: "",
  });
});

test("setSection: patches, persists, round-trips, and a repeat is a no-op", () => {
  const { be, store, changed } = newStore();
  store.load();
  expect(store.setSection(CONTROL_SURFACE, { input: "LP in", output: "LP out", device: "Launchpad Pro [MK3]" })).toBeTruthy();
  expect(store.setSection(CONTROL_SURFACE, { enabled: true })).toBeTruthy(); // a patch of one field keeps the rest
  expect(store.section(CONTROL_SURFACE)).toEqual({
    input: "LP in", output: "LP out", enabled: true, device: "Launchpad Pro [MK3]",
  });
  expect(JSON.parse(be.readText(CONFIG)!).controlSurface.input).toBe("LP in");
  expect(changed()).toBe(2);
  expect(store.setSection(CONTROL_SURFACE, { input: "LP in" })).toBeFalsy();
  expect(changed()).toBe(2);
});

test("two writers sharing config.json: a change here keeps what the other wrote since this one loaded", () => {
  // The standalone reads config.json once at launch and does not watch it; the DAW plugin writes it too. A setter
  // computed from the stale in-memory copy wrote that whole copy back, reverting the other process's changes.
  const be = new MockBackend("/config");
  const standalone = new UserConfigStore(be);
  standalone.load();
  const plugin = new UserConfigStore(be);
  plugin.load();
  expect(plugin.setDefaultZoom(5)).toBeTruthy();
  expect(plugin.setActiveKeyboardBindings("wasd")).toBeTruthy();
  // The standalone, still holding its launch-time copy, records a control-surface pick.
  expect(standalone.setSection(CONTROL_SURFACE, { input: "LP in", output: "LP out", enabled: true })).toBeTruthy();
  const onDisk = parseUserConfig(be.readText(CONFIG)!)!;
  expect(onDisk.defaultZoom).toBe(5);
  expect(onDisk.activeKeyboardBindings).toBe("wasd");
  expect(JSON.parse(be.readText(CONFIG)!).controlSurface.input).toBe("LP in");
  // ...and the other way round: the plugin's next write keeps the standalone's pick.
  expect(plugin.setDefaultZoom(4)).toBeTruthy();
  expect(JSON.parse(be.readText(CONFIG)!).controlSurface.input).toBe("LP in");
});

test("a write that fails is not reported as saved, and is not adopted", () => {
  const be = new MockBackend("/config");
  const store = new UserConfigStore(be);
  store.load();
  const realWrite = be.writeFileAtomic.bind(be);
  be.writeFileAtomic = () => false; // e.g. Windows: another process holds config.json open
  expect(store.setSection(CONTROL_SURFACE, { input: "LP in" })).toBeFalsy();
  expect(store.section<{ input: string }>(CONTROL_SURFACE).input).toBe(""); // not shown as saved, then lost at launch
  be.writeFileAtomic = realWrite;
  expect(store.setSection(CONTROL_SURFACE, { input: "LP in" })).toBeTruthy();
});
