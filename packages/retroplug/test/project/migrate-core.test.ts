// The v1→v2 project migration: `core` is backfilled from `platform` when loading a pre-v2
// config, and round-trips once stored. The tightened systemThin schema requires both
// structural fields (platform + core) but keeps the genuine either-ors optional.
import { test, expect } from "../../testing/harness";
import { parseConfig, serializeConfig, buildConfig, DEFAULT_SETTINGS, K_PROJECT } from "../../src/projectConfig";
import type { SystemEntry } from "../../src/systemsList";

const identity = (p: string) => p;

test("v1→v2: core is backfilled from platform on load", () => {
  const raw = JSON.stringify({
    schemaVersion: "1", // pre-core
    settings: {},
    systems: [
      { platform: "gb", romPath: "/a.gb" }, // → sameboy
      { platform: "nes", romPath: "/b.nes" }, // → mesen
      { platform: "gba", romPath: "/c.gba" }, // → mesen
    ],
  });
  const cfg = parseConfig(raw);
  expect(cfg.systems.map((s) => s.core)).toEqual(["sameboy", "mesen", "mesen"]);
  expect(cfg.systems.map((s) => s.platform)).toEqual(["gb", "nes", "gba"]);
});

test("a v2 config keeps its explicit core (migration is a no-op)", () => {
  const raw = JSON.stringify({
    schemaVersion: String(K_PROJECT), // already current
    settings: {},
    systems: [{ platform: "gb", core: "mesen", romPath: "/x.gb" }], // an explicit, non-default core
  });
  const cfg = parseConfig(raw);
  expect(cfg.systems[0].core).toBe("mesen"); // not re-derived to sameboy
});

test("core round-trips through save/load", () => {
  const entry = { id: 1, platform: "nes", core: "mesen", romPath: "/g.nes" } as unknown as SystemEntry;
  const cfg = buildConfig(DEFAULT_SETTINGS, [entry]);
  expect(cfg.systems[0].core).toBe("mesen"); // written into the thin entry
  const back = parseConfig(serializeConfig(cfg, "", identity));
  expect(back.systems[0].core).toBe("mesen");
});

test("strict: a v2 system with an invalid platform/core is dropped", () => {
  const raw = JSON.stringify({
    schemaVersion: String(K_PROJECT),
    settings: {},
    systems: [
      { platform: "gb", core: "sameboy", romPath: "/ok.gb" }, // valid
      { platform: "zx", core: "sameboy", romPath: "/bad.zx" }, // invalid platform → dropped
      { platform: "gb", romPath: "/nocore.gb" }, // v2 file missing core → dropped
    ],
  });
  const cfg = parseConfig(raw);
  expect(cfg.systems.length).toBe(1);
  expect(cfg.systems[0].romPath).toBe("/ok.gb");
});

test("either-or optionality survives: embedded (no romPath) parses", () => {
  const raw = JSON.stringify({
    schemaVersion: String(K_PROJECT),
    settings: {},
    systems: [{ platform: "gb", core: "sameboy", embeddedRom: "mgb" }], // no romPath — genuine either-or
  });
  const cfg = parseConfig(raw);
  expect(cfg.systems.length).toBe(1);
  expect(cfg.systems[0].embeddedRom).toBe("mgb");
  expect(cfg.systems[0].romPath).toBe(undefined);
});
