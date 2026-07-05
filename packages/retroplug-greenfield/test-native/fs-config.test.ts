// The config stores driven over the REAL native Backend (fs) with real temp files —
// proving the fs/config seam end-to-end. `__CONFIG_DIR__` (injected by the runner) is the
// fresh temp dir the host was given as RETROPLUG_USER_CONFIG_DIR, so the stores read/write
// real files under it. Assertions are on observable outcomes (files on disk, reload
// round-trips), not mock introspection.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { UserConfigStore } from "../src/userConfigStore";
import { BindingsStore } from "../src/bindingsStore";

declare const __CONFIG_DIR__: string;

test("RecentStore: add persists to real recent.json; a fresh store reads it back", () => {
  const be = createRealBackend();
  const song = __CONFIG_DIR__ + "/song.rplg";

  const r1 = new RecentStore(be);
  r1.load();
  r1.add(song);

  // A fresh store over the same real disk sees the persisted entry.
  const r2 = new RecentStore(be);
  r2.load();
  const v = r2.view();
  expect(v.length).toBe(1);
  expect(v[0].path).toBe(be.canonicalize(song)); // stored under the canonical dedupe key
});

test("UserConfigStore: first load writes real defaults; a set persists across a reload", () => {
  const be = createRealBackend();
  const cfg = __CONFIG_DIR__ + "/config.json";

  const u1 = new UserConfigStore(be);
  u1.load(); // no config.json yet → readFile returns null → writes defaults to real disk
  expect(be.fileExists(cfg)).toBeTruthy();
  expect(u1.config().defaultZoom).toBe(3); // the default

  expect(u1.setDefaultZoom(5)).toBeTruthy();

  const u2 = new UserConfigStore(be);
  u2.load(); // reads the real config.json back
  expect(u2.config().defaultZoom).toBe(5);
});

test("BindingsStore: ensureDefaults + profile CRUD over real disk (listDir/write/read/delete)", () => {
  const be = createRealBackend();
  const uc = new UserConfigStore(be);
  uc.load();
  const b = new BindingsStore(be, uc);

  b.ensureDefaults(); // writes bindings/default.json (creating the bindings/ dir)
  expect(be.fileExists(__CONFIG_DIR__ + "/bindings/default.json")).toBeTruthy();
  expect(b.availableProfiles()).toEqual(["default"]);

  expect(b.saveProfile("wasd", { name: "ignored", keyboard: { Left: ["A"] }, gamepad: {} })).toBeTruthy();
  expect(b.availableProfiles()).toEqual(["default", "wasd"]); // listDir → *.json stems, sorted

  const loaded = b.loadProfile("wasd")!;
  expect(loaded.name).toBe("wasd"); // forced to the filename
  expect(loaded.keyboard.Left).toEqual(["A"]);

  expect(b.deleteProfile("wasd")).toBeTruthy();
  expect(b.availableProfiles()).toEqual(["default"]);
  expect(b.loadProfile("wasd")).toBe(null);
});
