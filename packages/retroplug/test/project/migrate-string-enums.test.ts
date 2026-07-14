// The v2→v3 project migration: every enum setting (project layout/routing + the per-system role-config
// enums) became string-valued. A pre-v3 config's integer values are rewritten to their string spellings
// on load; a v3 config is untouched; an unknown/out-of-range integer falls to the schema default. `zoom`
// and the genuine-numeric role fields (linkGroupId, tempoDivisor) stay numeric.
import { test, expect } from "../../testing/harness";
import { parseConfig, serializeConfig, buildConfig, K_PROJECT } from "../../src/projectConfig";
import type { ProjectSettings } from "../../src/projectConfig";
import type { SystemEntry } from "../../src/systemsList";

const identity = (p: string) => p;

test("v2→v3: integer project settings migrate to their string values", () => {
  const raw = JSON.stringify({
    schemaVersion: "2",
    settings: { layout: 3, midiRouting: 1, audioRouting: 2, zoom: 4 },
    systems: [],
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings).toEqual({
    layout: "grid",
    midiRouting: "fourChannelsPerInstance",
    audioRouting: "onePerInstance",
    zoom: 4, // a magnitude, not an enum — untouched
  });
});

test("v2→v3: integer role-config enums migrate to strings; numeric fields stay numeric", () => {
  const raw = JSON.stringify({
    schemaVersion: "2",
    settings: {},
    systems: [
      {
        platform: "gb",
        core: "sameboy",
        romPath: "/a.gb",
        roles: [
          { kind: "sameboy", config: { model: 9, highpass: 2, linkGroupId: 3, fastBoot: true } },
          { kind: "lsdj-sync", config: { mode: 7, tempoDivisor: 4, autoStart: false } },
        ],
      },
      {
        platform: "nes",
        core: "mesen",
        romPath: "/b.nes",
        roles: [{ kind: "mesen", config: { region: 2, removeSpriteLimit: false, channelExportMode: 3 } }],
      },
    ],
  });
  const cfg = parseConfig(raw);
  expect(cfg.systems[0].roles).toEqual([
    { kind: "sameboy", config: { model: "cgbC", highpass: "removeDcOffset", linkGroupId: 3, fastBoot: true } },
    { kind: "lsdj-sync", config: { mode: "midiOut", tempoDivisor: 4, autoStart: false } },
  ]);
  expect(cfg.systems[1].roles).toEqual([
    { kind: "mesen", config: { region: "pal", removeSpriteLimit: false, channelExportMode: "individualMono" } },
  ]);
});

test("a v3 config is a no-op (strings preserved)", () => {
  const raw = JSON.stringify({
    schemaVersion: String(K_PROJECT),
    settings: { layout: "row", midiRouting: "sendToAll", audioRouting: "channelSplit", zoom: 2 },
    systems: [{ platform: "gb", core: "sameboy", romPath: "/a.gb", roles: [{ kind: "sameboy", config: { model: "dmgB" } }] }],
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings.layout).toBe("row");
  expect(cfg.settings.audioRouting).toBe("channelSplit");
  expect(cfg.systems[0].roles).toEqual([{ kind: "sameboy", config: { model: "dmgB" } }]);
});

test("an unknown/out-of-range setting integer falls to the schema default", () => {
  const raw = JSON.stringify({
    schemaVersion: "2",
    settings: { layout: 99, audioRouting: 7 }, // no such ordinal — migration can't map them
    systems: [],
  });
  const cfg = parseConfig(raw);
  expect(cfg.settings.layout).toBe("auto"); // enumField default
  expect(cfg.settings.audioRouting).toBe("stereo"); // enumField default
});

test("string settings + role config round-trip through save/load, stamped v3", () => {
  const settings: ProjectSettings = { layout: "column", midiRouting: "oneChannelPerInstance", audioRouting: "twoPerInstance", zoom: 5 };
  const entry = {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/g.gb",
    roles: [{ kind: "sameboy", config: { model: "sgb", highpass: "off", linkGroupId: 0, fastBoot: true } }],
  } as unknown as SystemEntry;
  const cfg = buildConfig(settings, [entry]);
  expect(cfg.schemaVersion).toBe(String(K_PROJECT)); // "3"
  const back = parseConfig(serializeConfig(cfg, "", identity));
  expect(back.settings).toEqual(settings);
  expect(back.systems[0].roles).toEqual([{ kind: "sameboy", config: { model: "sgb", highpass: "off", linkGroupId: 0, fastBoot: true } }]);
});
