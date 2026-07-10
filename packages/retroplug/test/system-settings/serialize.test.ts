// Settings + roles are TS-owned config, so they round-trip through the project
// serialization — closing the project domain's "rich fields deferred at defaults"
// gap for these. A customized model role + gain serialize into the thin config and
// rebuild through the real store; a fresh all-default system emits neither.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { buildConfig, serializeConfig, parseConfig, DEFAULT_SETTINGS } from "../../src/projectConfig";
import { gbRom, nesRom } from "../systems/fixtures";

const identity = (p: string) => p;
function reg(): RoleRegistry {
  const r = new RoleRegistry();
  registerCoreRoles(r);
  return r;
}

test("customized settings + role round-trip through build → serialize → parse → adopt", () => {
  const be = new MockBackend("/cfg");
  be.seed("/proj/a.gb", gbRom());
  const s1 = new SystemsStore(be, () => {}, reg());
  const id = s1.addSystem("/proj/a.gb") as number;
  s1.setRoleConfig(id, "sameboy", { model: 3 });
  s1.setGain(id, -6);

  const cfg = buildConfig(DEFAULT_SETTINGS, s1.systems());
  expect(cfg.systems[0].settings).toEqual({ gainDb: -6 }); // reload default omitted
  expect(cfg.systems[0].roles).toEqual([
    { kind: "sameboy", config: { model: 3, highpass: 1, linkGroupId: 0, fastBoot: true } },
  ]);

  const back = parseConfig(serializeConfig(cfg, "", identity));

  const be2 = new MockBackend("/cfg");
  be2.seed("/proj/a.gb", gbRom());
  const s2 = new SystemsStore(be2, () => {}, reg());
  for (const sys of back.systems) s2.adopt(sys);

  const v = s2.view()[0];
  expect(v.settings.gainDb).toBe(-6); // restored
  expect(v.roles[0].config.model).toBe(3); // restored (stored role wins over defaults)
});

test("a customized NES (mesen) role round-trips through serialize → parse → adopt", () => {
  const be = new MockBackend("/cfg");
  be.seed("/proj/a.nes", nesRom());
  const s1 = new SystemsStore(be, () => {}, reg());
  const id = s1.addSystem("/proj/a.nes") as number;
  s1.setRoleConfig(id, "mesen", { region: 2 }); // PAL

  const cfg = buildConfig(DEFAULT_SETTINGS, s1.systems());
  expect(cfg.systems[0].roles).toEqual([{ kind: "mesen", config: { region: 2, removeSpriteLimit: false } }]);

  const back = parseConfig(serializeConfig(cfg, "", identity));
  const be2 = new MockBackend("/cfg");
  be2.seed("/proj/a.nes", nesRom());
  const s2 = new SystemsStore(be2, () => {}, reg());
  for (const sys of back.systems) s2.adopt(sys);

  expect(s2.view()[0].roles[0].config.region).toBe(2); // restored
});

test("a fresh all-default system emits no settings/roles", () => {
  const be = new MockBackend("/cfg");
  be.seed("/proj/a.gb", gbRom());
  const s = new SystemsStore(be, () => {}); // no registry → no roles either
  s.addSystem("/proj/a.gb");
  const cfg = buildConfig(DEFAULT_SETTINGS, s.systems());
  expect(cfg.systems[0].settings).toBe(undefined);
  expect(cfg.systems[0].roles).toBe(undefined);
});
