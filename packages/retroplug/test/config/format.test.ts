// How the on-disk JSON is FORMATTED (the shapes themselves are covered per-root elsewhere). Every root
// RetroPlug writes — config.json, bindings/<name>.json, recent.json, and the project config (thin `.rplg`
// + the project.json inside an export zip / the DPF state chunk) — goes through stringifyConfig, so files
// are pretty-printed with a trailing newline: readable, hand-editable, diffable. Reads stay indifferent to
// whitespace, so a compact file from an older build still loads.
import { test, expect } from "../../testing/harness";
import { stringifyConfig } from "../../src/configSchema";
import { DEFAULT_USER_CONFIG } from "../../src/userConfig";
import { parseUserConfig, serializeUserConfig } from "../../src/userConfigSerialization";
import { defaultBindingMap } from "../../src/bindingMap";
import { parseBindingMap, serializeBindingMap } from "../../src/bindingSerialization";
import { parseRecent, serializeRecent } from "../../src/recentSerialization";
import { buildConfig, parseConfig, serializeConfig, DEFAULT_SETTINGS } from "../../src/projectConfig";
import type { SystemEntry } from "../../src/systemsList";

const identity = (p: string) => p;
const sys: SystemEntry = { id: 1, platform: "gb", core: "sameboy", romPath: "/roms/a.gb", savPath: "", savSuffix: 0, embeddedRom: "", battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [] };
const projectJson = () => serializeConfig(buildConfig(DEFAULT_SETTINGS, [sys]), "", identity);

test("stringifyConfig: 2-space indent, one field per line, trailing newline", () => {
  expect(stringifyConfig({ a: 1, b: { c: 2 } })).toBe('{\n  "a": 1,\n  "b": {\n    "c": 2\n  }\n}\n');
  expect(stringifyConfig({ a: {}, b: [] })).toBe('{\n  "a": {},\n  "b": []\n}\n'); // empties stay closed up
  expect(stringifyConfig({ a: undefined, b: 1 })).toBe('{\n  "b": 1\n}\n'); // undefined dropped, as JSON.stringify does
});

test("stringifyConfig: a short primitive array stays inline; objects and long arrays expand", () => {
  expect(stringifyConfig({ A: ["Z", "z"] })).toBe('{\n  "A": ["Z", "z"]\n}\n'); // the bindings shape
  expect(stringifyConfig({ s: [{ a: 1 }] })).toBe('{\n  "s": [\n    {\n      "a": 1\n    }\n  ]\n}\n'); // objects always expand
  // Too wide to read on one line → one element per line.
  const wide = stringifyConfig({ k: Array.from({ length: 20 }, (_, i) => `name${i}`) });
  expect(wide.split("\n").length).toBe(25); // {, "k": [, 20 elements, ], }, and the trailing newline's ""
});

test("every written root is pretty-printed and newline-terminated", () => {
  const written = [
    serializeUserConfig(DEFAULT_USER_CONFIG),
    serializeBindingMap(defaultBindingMap()),
    serializeRecent([{ path: "/proj/a.rplg", name: "A" }]),
    projectJson(),
  ];
  for (const json of written) {
    expect(json.startsWith("{\n  ")).toBeTruthy(); // indented, not one long line
    expect(json.endsWith("}\n")).toBeTruthy();
    expect(json.includes('\n  "schemaVersion"')).toBeTruthy(); // the stamp reads as its own line
  }
});

test("only whitespace changed: each root re-parses to the same values, compact or pretty", () => {
  // A file written by an older compact build (the pretty text minus its whitespace) still loads.
  const compact = (json: string) => JSON.stringify(JSON.parse(json));

  const cfg = serializeUserConfig(DEFAULT_USER_CONFIG);
  expect(parseUserConfig(cfg)).toEqual(DEFAULT_USER_CONFIG);
  expect(parseUserConfig(compact(cfg))).toEqual(parseUserConfig(cfg));

  const map = serializeBindingMap(defaultBindingMap());
  expect(parseBindingMap(map)).toEqual(defaultBindingMap());
  expect(parseBindingMap(compact(map))).toEqual(parseBindingMap(map));

  const recent = serializeRecent([{ path: "/proj/a.rplg", name: "A" }]);
  expect(parseRecent(recent)).toEqual([{ path: "/proj/a.rplg", name: "A" }]);
  expect(parseRecent(compact(recent))).toEqual(parseRecent(recent));

  const proj = projectJson();
  expect(parseConfig(compact(proj))).toEqual(parseConfig(proj));
});
