// The file-watch reaction: UserConfigStore.reload() (keep-previous re-read) and the
// FileWatcher that drains the native watcher's changed paths and routes them —
// config.json → reload, bindings/*.json → refresh signal, a system's ROM → reloadSystem.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { UserConfigStore } from "../../src/userConfigStore";
import { SystemsStore } from "../../src/systemsStore";
import { FileWatcher } from "../../src/fileWatcher";
import { serializeUserConfig } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG } from "../../src/userConfig";
import { gbRom } from "../systems/fixtures";

const CONFIG = "/config/config.json";
const withZoom = (z: number) => serializeUserConfig({ ...DEFAULT_USER_CONFIG, defaultZoom: z });

// --- UserConfigStore.reload() ---

test("reload: picks up an external config.json edit and fires onChange", () => {
  const be = new MockBackend("/config");
  let changes = 0;
  const uc = new UserConfigStore(be, () => changes++);
  uc.load(); // first-run writes defaults (no onChange)
  be.seed(CONFIG, withZoom(5)); // an external edit
  expect(uc.reload()).toBeTruthy();
  expect(uc.config().defaultZoom).toBe(5);
  expect(changes).toBe(1);
});

test("reload: a malformed / missing file keeps the current value (no onChange)", () => {
  const be = new MockBackend("/config");
  let changes = 0;
  const uc = new UserConfigStore(be, () => changes++);
  be.seed(CONFIG, withZoom(4));
  uc.load();
  be.seed(CONFIG, "garbage");
  expect(uc.reload()).toBeFalsy();
  expect(uc.config().defaultZoom).toBe(4); // kept
  be.deleteFile(CONFIG);
  expect(uc.reload()).toBeFalsy(); // deleted → keep
  expect(uc.config().defaultZoom).toBe(4);
  expect(changes).toBe(0);
});

test("reload: identical content is a no-op", () => {
  const be = new MockBackend("/config");
  let changes = 0;
  const uc = new UserConfigStore(be, () => changes++);
  uc.load();
  expect(uc.reload()).toBeFalsy();
  expect(changes).toBe(0);
});

// --- FileWatcher routing ---

function setup() {
  const be = new MockBackend("/config");
  let bindingsRefreshes = 0;
  const uc = new UserConfigStore(be);
  const systems = new SystemsStore(be);
  const fw = new FileWatcher(be, uc, systems, () => bindingsRefreshes++);
  uc.load(); // seed config.json with defaults
  return { be, uc, systems, fw, refreshes: () => bindingsRefreshes };
}

test("pump: an empty drain is an all-false no-op", () => {
  const { fw } = setup();
  expect(fw.pump()).toEqual({ configReloaded: false, bindingsChanged: false, romReloaded: [] });
});

test("pump: a config.json change reloads the user config", () => {
  const { be, uc, fw } = setup();
  be.seed(CONFIG, withZoom(6));
  be.emitFileChange(CONFIG);
  expect(fw.pump().configReloaded).toBeTruthy();
  expect(uc.config().defaultZoom).toBe(6);
});

test("pump: a bindings profile change fires the refresh signal", () => {
  const { be, fw, refreshes } = setup();
  be.emitFileChange("/config/bindings/wasd.json");
  expect(fw.pump().bindingsChanged).toBeTruthy();
  expect(refreshes()).toBe(1);
});

test("pump: a changed ROM reloads a system with reloadOnRomChange on", () => {
  const { be, systems, fw } = setup();
  be.seed("/proj/a.gb", gbRom());
  const id = systems.addSystem("/proj/a.gb")!;
  systems.setReloadOnRomChange(id, true);
  be.emitFileChange("/proj/a.gb");
  const r = fw.pump();
  expect(r.romReloaded.length).toBe(1);
  expect(r.romReloaded[0] !== id).toBeTruthy(); // reloadSystem swapped in a new id
  expect(be.log.includes("reloadSystem")).toBeTruthy();
});

test("pump: a changed ROM is ignored when reloadOnRomChange is off", () => {
  const { be, systems, fw } = setup();
  be.seed("/proj/a.gb", gbRom());
  systems.addSystem("/proj/a.gb"); // reloadOnRomChange defaults false
  be.emitFileChange("/proj/a.gb");
  expect(fw.pump().romReloaded).toEqual([]);
  expect(be.log.includes("reloadSystem")).toBeFalsy();
});

test("pump: an unrelated path (recent.json) is ignored", () => {
  const { be, fw } = setup();
  be.emitFileChange("/config/recent.json");
  expect(fw.pump()).toEqual({ configReloaded: false, bindingsChanged: false, romReloaded: [] });
});
