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
  expect(cfg.settings).toEqual({ layout: 2, midiRouting: 0, audioRouting: 0, zoom: 0 }); // defaults filled
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
  // roles is a known field; a role's `config` is an open record, so its fields survive
  expect(sys.roles).toEqual([{ kind: "lsdj-sync", config: { mode: 2, tempoDivisor: 1 } }]);
});

test("coerces a malformed settings value + drops a garbage system entry", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    settings: { layout: 99, zoom: "bad" }, // out-of-range + wrong-type → clamped/defaulted
    systems: [{ platform: "gb", romPath: "/a.gb" }, 12345, null], // non-object entries dropped
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings.layout).toBe(3); // clamped to max
  expect(cfg.settings.zoom).toBe(0); // wrong type → default
  expect(cfg.systems.length).toBe(1); // garbage entries dropped
  expect(cfg.systems[0].romPath).toBe("/a.gb");
});
