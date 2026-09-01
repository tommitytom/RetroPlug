// The v3→v4 project migration: the EverMIDI project renamed to BlipToaster, and the two role kinds it owns
// are persisted verbatim as `systems[].roles[].kind`, so every project saved before the rename carries the
// old spelling. On load those kinds are rewritten; everything else (the role CONFIG, other roles, other
// systems) is left exactly as it was.
import { test, expect } from "../../testing/harness";
import { parseConfig, serializeConfig, buildConfig, DEFAULT_SETTINGS, K_PROJECT } from "../../src/projectConfig";
import type { SystemEntry } from "../../src/systemsList";

const identity = (p: string) => p;

// A v3 project with an EverMIDI system: the marker role + the assets role carrying one kit override.
const v3Project = (version = "3") =>
  JSON.stringify({
    schemaVersion: version,
    settings: {},
    systems: [
      {
        platform: "nes",
        core: "mesen",
        romPath: "/bliptoaster.nes",
        roles: [
          { kind: "mesen", config: { region: "ntsc" } },
          { kind: "evermidi", config: {} },
          {
            kind: "evermidi-assets",
            config: { overrides: [{ type: "kit", slot: 1, name: "HATS", path: "/kits/hats.rkit" }] },
          },
        ],
      },
    ],
  });

test("v3→v4: the evermidi role kinds are rewritten to bliptoaster", () => {
  const roles = parseConfig(v3Project()).systems[0].roles!;
  expect(roles.map((r) => r.kind)).toEqual(["mesen", "bliptoaster", "bliptoaster-assets"]);
});

test("v3→v4: the renamed roles keep their config byte-for-byte", () => {
  const roles = parseConfig(v3Project()).systems[0].roles!;
  // The assets role's override list is the persisted source of truth for a user's kit/font/theme
  // replacements — a rename that dropped it would silently lose their work.
  expect(roles.find((r) => r.kind === "bliptoaster-assets")!.config).toEqual({
    overrides: [{ type: "kit", slot: 1, name: "HATS", path: "/kits/hats.rkit" }],
  });
  expect(roles.find((r) => r.kind === "bliptoaster")!.config).toEqual({});
  expect(roles.find((r) => r.kind === "mesen")!.config).toEqual({ region: "ntsc" });
});

test("the rename also runs for a project stamped older than v3 (the whole chain applies)", () => {
  // A v1 file walks v1→v2→v3→v4; the rename must still land.
  const roles = parseConfig(v3Project("1")).systems[0].roles!;
  expect(roles.map((r) => r.kind)).toEqual(["mesen", "bliptoaster", "bliptoaster-assets"]);
});

test("the step is idempotent-safe: an already-renamed v4 config is untouched", () => {
  const raw = JSON.stringify({
    schemaVersion: String(K_PROJECT),
    settings: {},
    systems: [
      {
        platform: "nes",
        core: "mesen",
        romPath: "/bliptoaster.nes",
        roles: [
          { kind: "bliptoaster", config: {} },
          { kind: "bliptoaster-assets", config: { overrides: [] } },
        ],
      },
    ],
  });
  const roles = parseConfig(raw).systems[0].roles!;
  expect(roles.map((r) => r.kind)).toEqual(["bliptoaster", "bliptoaster-assets"]);
});

test("unrelated role kinds that merely contain the old name are NOT rewritten", () => {
  // The rename is an exact-match map, not a substring rewrite: the NES MIDI-in DSP role is named for the
  // Everdrive N8 cartridge, not for the ROM, and must survive untouched.
  const raw = JSON.stringify({
    schemaVersion: "3",
    settings: {},
    systems: [{ platform: "nes", core: "mesen", romPath: "/a.nes", roles: [{ kind: "nes-n8-midi", config: {} }] }],
  });
  expect(parseConfig(raw).systems[0].roles!.map((r) => r.kind)).toEqual(["nes-n8-midi"]);
});

test("a renamed project round-trips through save/load, stamped v4", () => {
  const entry = {
    id: 1,
    platform: "nes",
    core: "mesen",
    romPath: "/bliptoaster.nes",
    roles: [
      { kind: "bliptoaster", config: {} },
      { kind: "bliptoaster-assets", config: { overrides: [{ type: "font", slot: 0, name: "f", path: "/f.chr" }] } },
    ],
  } as unknown as SystemEntry;
  const cfg = buildConfig(DEFAULT_SETTINGS, [entry]);
  expect(cfg.schemaVersion).toBe(String(K_PROJECT));
  const back = parseConfig(serializeConfig(cfg, "", identity));
  expect(back.systems[0].roles).toEqual(entry.roles);
});
