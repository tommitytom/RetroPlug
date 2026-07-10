// Load-time missing-file detection + relink over a parsed config. A referenced ROM
// or paired save that no longer exists on disk (and isn't an embedded blob) is
// flagged so the UI can locate it before applying the project. Thin subset (rom +
// sram); kit samples land with the kits domain. Port of missingFiles.ts.
import { test, expect } from "../../testing/harness";
import { scanMissingFiles, relinkInConfig, autoFindSiblings } from "../../src/projectMissing";
import type { ProjectConfig } from "../../src/projectConfig";

const NO_BLOBS = new Set<string>();

function cfg(systems: ProjectConfig["systems"]): ProjectConfig {
  return { schemaVersion: "1", settings: { layout: 0, midiRouting: 0, audioRouting: 0, zoom: 0 }, systems };
}

test("scan: a ROM is missing when its path is absent and no blob is embedded", () => {
  const c = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  const missing = scanMissingFiles(c, NO_BLOBS, () => false); // nothing on disk
  expect(missing).toEqual([{ systemIndex: 0, itemKind: "rom", path: "/roms/a.gb" }]);
});

test("scan: present ROM / embedded ROM / bundled rom-blob are all OK", () => {
  const present = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(present, NO_BLOBS, (p) => p === "/roms/a.gb")).toEqual([]);

  const embedded = cfg([{ platform: "gb", embeddedRom: "mgb" }]);
  expect(scanMissingFiles(embedded, NO_BLOBS, () => false)).toEqual([]); // baked in

  const bundled = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(bundled, new Set(["systems/0/rom"]), () => false)).toEqual([]); // blob present
});

test("scan: an explicit savPath with no file + no blob is missing; an empty savPath is allowed", () => {
  const withOverride = cfg([{ platform: "gb", romPath: "/roms/a.gb", savPath: "/saves/x.sav" }]);
  const missing = scanMissingFiles(withOverride, NO_BLOBS, (p) => p === "/roms/a.gb"); // rom ok, sav gone
  expect(missing).toEqual([{ systemIndex: 0, itemKind: "sram", path: "/saves/x.sav" }]);

  // empty savPath = the suffix sibling, allowed to be absent (fresh cart)
  const noOverride = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(noOverride, NO_BLOBS, (p) => p === "/roms/a.gb")).toEqual([]);
});

test("scan: multiple systems report by config index", () => {
  const c = cfg([
    { platform: "gb", romPath: "/roms/a.gb" }, // present
    { platform: "gb", romPath: "/roms/b.gb" }, // missing
  ]);
  const missing = scanMissingFiles(c, NO_BLOBS, (p) => p === "/roms/a.gb");
  expect(missing).toEqual([{ systemIndex: 1, itemKind: "rom", path: "/roms/b.gb" }]);
});

test("relinkInConfig: repoints rom / sram in place; false for a bad index", () => {
  const c = cfg([{ platform: "gb", romPath: "/old/a.gb", savPath: "/old/a.sav" }]);
  expect(relinkInConfig(c, { systemIndex: 0, itemKind: "rom", path: "/old/a.gb" }, "/new/a.gb")).toBeTruthy();
  expect(c.systems[0].romPath).toBe("/new/a.gb");
  expect(relinkInConfig(c, { systemIndex: 0, itemKind: "sram", path: "/old/a.sav" }, "/new/a.sav")).toBeTruthy();
  expect(c.systems[0].savPath).toBe("/new/a.sav");
  expect(relinkInConfig(c, { systemIndex: 9, itemKind: "rom", path: "x" }, "y")).toBeFalsy();
});

test("autoFindSiblings: one located folder fixes the rest by basename", () => {
  const c = cfg([
    { platform: "gb", romPath: "/old/a.gb" },
    { platform: "gb", romPath: "/old/b.gb" },
  ]);
  // both files moved to /new; point autoFind at that folder
  const onDisk = new Set(["/new/a.gb", "/new/b.gb"]);
  const fixed = autoFindSiblings(c, "/new", NO_BLOBS, (p) => onDisk.has(p));
  expect(fixed).toBe(2);
  expect(c.systems.map((s) => s.romPath)).toEqual(["/new/a.gb", "/new/b.gb"]);
});
