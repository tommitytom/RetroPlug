// Forward-tolerance: parseConfig validates the config SHAPE (defaults filled, values
// coerced) via zod, but must PRESERVE unknown fields — a native-written .rplg carries
// richer per-system fields (model, native-shaped roles, …) that the greenfield thin
// model doesn't track. If those were stripped, a greenfield load→save round-trip
// would silently lose them. This locks the z.looseObject passthrough that a naive
// zod migration (strip-by-default) would break.
import { test, expect } from "../../testing/harness";
import { parseConfig, serializeConfig } from "../../src/projectConfig";

const identity = (p: string) => p;

test("parseConfig preserves unknown top-level + per-system fields", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    settings: { layout: 2, futureSetting: 42 }, // unknown settings field
    futureTopLevel: { anything: true }, // unknown root field
    systems: [
      {
        kind: "sameboy",
        romPath: "/roms/a.gb",
        model: 5, // a native flat field greenfield doesn't model
        roles: [{ kind: "lsdj-sync", config: { mode: 2, tempoDivisor: 1 } }], // native-shaped role
        highpass: 2, // another unknown rich field
      },
    ],
  });

  const cfg = parseConfig(raw);
  // known fields validated/typed
  expect(cfg.settings.layout).toBe(2);
  expect(cfg.systems[0].romPath).toBe("/roms/a.gb");
  // unknown fields survive on the parsed object
  const sys = cfg.systems[0] as Record<string, unknown>;
  expect(sys.model).toBe(5);
  expect(sys.highpass).toBe(2);
  expect(sys.roles).toEqual([{ kind: "lsdj-sync", config: { mode: 2, tempoDivisor: 1 } }]);
  expect((cfg.settings as Record<string, unknown>).futureSetting).toBe(42);
  expect((cfg as Record<string, unknown>).futureTopLevel).toEqual({ anything: true });

  // and they survive a re-serialize (no data loss on round-trip)
  const round = JSON.parse(serializeConfig(cfg, "", identity));
  expect(round.systems[0].model).toBe(5);
  expect(round.systems[0].roles[0].config.mode).toBe(2);
  expect(round.futureTopLevel).toEqual({ anything: true });
});

test("parseConfig coerces a malformed settings value + drops a garbage system entry", () => {
  const raw = JSON.stringify({
    schemaVersion: "1",
    settings: { layout: 99, zoom: "bad" }, // out-of-range + wrong-type → clamped/defaulted
    systems: [{ kind: "sameboy", romPath: "/a.gb" }, 12345, null], // non-object entries dropped
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings.layout).toBe(3); // clamped to max
  expect(cfg.settings.zoom).toBe(0); // wrong type → default
  expect(cfg.systems.length).toBe(1); // garbage entries dropped
  expect(cfg.systems[0].romPath).toBe("/a.gb");
});
