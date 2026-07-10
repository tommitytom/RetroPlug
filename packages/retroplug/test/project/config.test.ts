// The project config model + codec: schema-version check, building a thin config
// from the live systems, and the serialize/parse round-trip with relative↔absolute
// path rebasing. Ports schemaVersions.ts + the ProjectConfig shape. TS owns the
// config (produces + consumes the JSON); a thin config drops ids + default fields so
// a fresh project round-trips faithfully (native restores the rest via DefaultIfMissing).
import { test, expect } from "../../testing/harness";
import {
  K_PROJECT,
  VersionCheck,
  checkVersion,
  parseProjectVersion,
  DEFAULT_SETTINGS,
  buildConfig,
  serializeConfig,
  parseConfig,
  toAbsolute,
} from "../../src/projectConfig";
import type { SystemEntry } from "../../src/systemsList";

const identity = (p: string) => p;

function sys(id: number, romPath: string, savPath = "", savSuffix = 0, embeddedRom = ""): SystemEntry {
  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom };
}

test("schema: checkVersion + parseProjectVersion", () => {
  expect(checkVersion(1, 1)).toBe(VersionCheck.Ok);
  expect(checkVersion(0, 1)).toBe(VersionCheck.Older);
  expect(checkVersion(2, 1)).toBe(VersionCheck.Newer);
  expect(parseProjectVersion("1.0")).toBe(1); // legacy string, leading int
  expect(parseProjectVersion("2")).toBe(2);
  expect(parseProjectVersion("")).toBe(K_PROJECT); // no digits → baseline floor
  expect(parseProjectVersion("garbage")).toBe(K_PROJECT);
});

test("buildConfig: systems → thin entries, ids dropped, defaults omitted, schema stamped", () => {
  const cfg = buildConfig(DEFAULT_SETTINGS, [
    sys(1, "/roms/a.gb"), // all defaults besides romPath
    sys(2, "/roms/a.gb", "/saves/x.sav", 2), // override + suffix
    sys(3, "", "", 0, "mgb"), // embedded, no path
  ]);
  expect(cfg.schemaVersion).toBe(String(K_PROJECT));
  expect(cfg.systems).toEqual([
    { platform: "gb", core: "sameboy", romPath: "/roms/a.gb" },
    { platform: "gb", core: "sameboy", romPath: "/roms/a.gb", savPath: "/saves/x.sav", savSuffix: 2 },
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
  ]);
});

test("name: an explicit name round-trips; a nameless config omits it (parses undefined)", () => {
  const named = buildConfig(DEFAULT_SETTINGS, [sys(1, "/roms/a.gb")], "My Song");
  expect(named.name).toBe("My Song");
  expect(parseConfig(serializeConfig(named, "", identity)).name).toBe("My Song");

  const nameless = buildConfig(DEFAULT_SETTINGS, [sys(1, "/roms/a.gb")]);
  expect("name" in nameless).toBeFalsy(); // omitted from the thin config
  expect(parseConfig(serializeConfig(nameless, "", identity)).name).toBe(undefined);
});

test("serializeConfig + parseConfig: round-trips, filling settings defaults", () => {
  const cfg = buildConfig({ layout: 3, midiRouting: 1, audioRouting: 2, zoom: 4 }, [sys(1, "/proj/a.gb")]);
  const json = serializeConfig(cfg, "", identity); // no baseDir → absolute
  const back = parseConfig(json);
  expect(back.settings).toEqual({ layout: 3, midiRouting: 1, audioRouting: 2, zoom: 4 });
  expect(back.systems).toEqual([{ platform: "gb", core: "sameboy", romPath: "/proj/a.gb" }]);
});

test("parseConfig: tolerant of a partial document (defaults + empty systems)", () => {
  const back = parseConfig(JSON.stringify({ schemaVersion: "1" }));
  expect(back.settings).toEqual(DEFAULT_SETTINGS);
  expect(back.systems).toEqual([]);
});

test("serializeConfig rebases under baseDir; toAbsolute restores it", () => {
  const cfg = buildConfig(DEFAULT_SETTINGS, [sys(1, "/proj/sub/a.gb", "/proj/sub/a.sav", 0)]);
  const json = serializeConfig(cfg, "/proj", identity); // rebase relative to /proj
  const parsed = JSON.parse(json);
  expect(parsed.systems[0].romPath).toBe("sub/a.gb"); // stored relative + forward-slash
  expect(parsed.systems[0].savPath).toBe("sub/a.sav");

  const back = parseConfig(json);
  toAbsolute(back, "/proj");
  expect(back.systems[0].romPath).toBe("/proj/sub/a.gb"); // restored absolute
  expect(back.systems[0].savPath).toBe("/proj/sub/a.sav");
});

test("serializeConfig keeps an out-of-base asset absolute (no ../ chains)", () => {
  const cfg = buildConfig(DEFAULT_SETTINGS, [sys(1, "/elsewhere/a.gb")]);
  const parsed = JSON.parse(serializeConfig(cfg, "/proj", identity));
  expect(parsed.systems[0].romPath).toBe("/elsewhere/a.gb"); // outside base → kept absolute
});
