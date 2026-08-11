// Config validation: parseConfig validates STRICTLY (unknown keys stripped) and coerces
// malformed values. Forward-tolerance across config versions is field DEFAULTS (an
// old config missing a newer field gets its default) — not passthrough. Unknown fields
// from a *different* writer (native C++'s richer shape) are a translation concern for
// the real adapter, and a *newer* writer is refused by version detection, so
// an older reader never needs to preserve unknowns. Breaking format changes bump the
// version and migrate at the load seam (migrateProjectRaw; e.g. v1→v2 backfills `core`).
import { test, expect } from "../../testing/harness";
import { parseConfig } from "../../src/projectConfig";

test("additive tolerance: a config missing newer fields gets their defaults", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    settings: { layout: 2 }, // the other three settings are absent
    systems: [{ platform: "gb", romPath: "/a.gb" }],
  });
  const cfg = parseConfig(raw);
  // defaults filled; the v1 integer `layout: 2` migrates to its string value (v2→v3). `controller` is the
  // newest field and the clearest example of what this test is for: a project written before it existed
  // loads with it defaulted off, needing no migration step at all.
  expect(cfg.settings).toEqual({
    layout: "column", midiRouting: "sendToAll", audioRouting: "stereo", zoom: 0,
    controller: { enabled: false, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: {} },
  });
  expect(cfg.systems[0]).toEqual({ platform: "gb", core: "sameboy", romPath: "/a.gb" }); // core backfilled (v1→v2)
});

test("strict: unknown fields are stripped, known fields kept", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    futureTopLevel: { anything: true }, // unknown root field
    settings: { layout: 2, futureSetting: 42 }, // unknown settings field
    systems: [
      {
        platform: "gb",
        romPath: "/a.gb",
        model: 5, // a native flat field the TS side doesn't model
        highpass: 2,
        roles: [{ kind: "lsdj-sync", config: { mode: 2, tempoDivisor: 1 } }], // known field
      },
    ],
  });
  const cfg = parseConfig(raw);
  expect((cfg as unknown as Record<string, unknown>).futureTopLevel).toBe(undefined); // root unknown stripped
  expect((cfg.settings as unknown as Record<string, unknown>).futureSetting).toBe(undefined); // settings unknown stripped
  const sys = cfg.systems[0] as unknown as Record<string, unknown>;
  expect(sys.model).toBe(undefined); // per-system unknown stripped
  expect(sys.highpass).toBe(undefined);
  expect(sys.romPath).toBe("/a.gb"); // known field kept
  // roles is a known field; a role's `config` is an open record, so its fields survive (the v1 integer
  // `mode: 2` migrates to its string value, v2→v3)
  expect(sys.roles).toEqual([{ kind: "lsdj-sync", config: { mode: "midiSyncArduinoboy", tempoDivisor: 1 } }]);
});

test("coerces a malformed settings value + drops a garbage system entry", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    settings: { layout: 99, zoom: "bad" }, // unknown enum + wrong-type → defaulted
    systems: [{ platform: "gb", romPath: "/a.gb" }, 12345, null], // non-object entries dropped
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings.layout).toBe("auto"); // out-of-range int is unknown to the enum → default
  expect(cfg.settings.zoom).toBe(0); // wrong type → default
  expect(cfg.systems.length).toBe(1); // garbage entries dropped
  expect(cfg.systems[0].romPath).toBe("/a.gb");
});
