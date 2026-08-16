// The systems store carrying per-system settings + roles. A constructed system
// auto-gets its backend "system" role (SameBoy = model/highpass/…) + universal
// settings (gain/reload). Editing a SYSTEM role applies to the emulator; editing a
// FEATURE role is pure TS (behaviour deferred). A fake extension proves the feature
// path with no LSDj in the core.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { z, clampedInt } from "../../src/configSchema";
import { gbRom, nesRom, sameboyRoleConfig } from "../systems/fixtures";

// A GB ROM (valid logo for classification) carrying a cartridge title at 0x134.
function gbRomWithTitle(title: string): Uint8Array {
  const b = gbRom();
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

// The core backend roles + a fake "demo-sync" feature extension (attached to any ROM
// whose title starts with "DEMO").
function registryWithFake(): RoleRegistry {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  reg.registerRole({
    kind: "demo-sync",
    category: "feature",
    schema: z.object({ level: clampedInt(0, 10, 1) }),
  });
  reg.registerRomProvider(({ header }) =>
    String.fromCharCode(...header.slice(0x134, 0x138)).startsWith("DEMO")
      ? [{ kind: "demo-sync", config: { level: 1 } }]
      : [],
  );
  return reg;
}

function newStore(withRegistry = true) {
  const be = new MockBackend("/cfg");
  let changes = 0;
  const store = new SystemsStore(be, () => changes++, withRegistry ? registryWithFake() : undefined);
  return { be, store, changes: () => changes };
}

test("a SameBoy system auto-gets the sameboy system role + universal settings", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  store.addSystem("/roms/a.gb");
  const v = store.view()[0];
  expect(v.settings).toEqual({ gainDb: 0, reloadOnRomChange: false });
  expect(v.roles).toEqual([{ kind: "sameboy", config: sameboyRoleConfig() }]);
});

test("a NES system auto-gets the mesen system role; editing region crosses to the emulator", () => {
  const { be, store } = newStore();
  be.seed("/roms/g.nes", nesRom());
  const id = store.addSystem("/roms/g.nes") as number;
  const v = store.view()[0];
  expect(v.platform).toBe("nes");
  expect(v.roles).toEqual([{ kind: "mesen", config: { region: "auto", removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "chip", mmc5PhaseReset: "chip", channelExportMode: "mix" } }]);

  expect(store.setRoleConfig(id, "mesen", { region: "pal" })).toBeTruthy(); // PAL
  expect(store.view()[0].roles[0].config.region).toBe("pal");
  expect(be.applyRoleCalls[be.applyRoleCalls.length - 1]).toEqual({
    id,
    kind: "mesen",
    config: { region: 2, removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: 0, mmc5PhaseReset: 0, channelExportMode: 0 }, // whole role config crosses (system-category, native-encoded)
  });
});

test("setRoleConfig on a system role: updates, clamps, applies to the emulator, dirties", () => {
  const { be, store, changes } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb") as number;
  const before = changes();
  expect(store.setRoleConfig(id, "sameboy", { model: "sgb" })).toBeTruthy();
  expect(store.view()[0].roles[0].config.model).toBe("sgb");
  // Native-encoded: every string enum in the role has become its ordinal (model "sgb" → 3,
  // highpass "accurate" → 1, colorCorrection "disabled" → 0, dmgPalette "grey" → 0), and the
  // non-enum fields pass through as-is. That mapping is roleConfigForNative's whole job.
  expect(be.applyRoleCalls[be.applyRoleCalls.length - 1]).toEqual({
    id,
    kind: "sameboy",
    config: {
      model: 3,
      highpass: 1,
      linkGroupId: 0,
      fastBoot: true,
      colorCorrection: 0,
      dmgPalette: 0,
      lightTemperature: 0,
    },
  });
  expect(changes()).toBe(before + 1);
  // unknown enum → defaulted, not stored raw
  store.setRoleConfig(id, "sameboy", { model: 99 });
  expect(store.view()[0].roles[0].config.model).toBe("cgbC");
  // absent role → false
  expect(store.setRoleConfig(id, "nope", { x: 1 })).toBeFalsy();
});

test("setGain / setReloadOnRomChange: clamp, update, apply to the emulator", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb") as number;
  expect(store.setGain(id, -6)).toBeTruthy();
  expect(store.view()[0].settings.gainDb).toBe(-6);
  expect(store.setGain(id, -500)).toBeTruthy(); // below -90 → clamped
  expect(store.view()[0].settings.gainDb).toBe(-90);
  store.setReloadOnRomChange(id, true);
  expect(store.view()[0].settings.reloadOnRomChange).toBeTruthy();
  expect(be.applySettingCalls.map((c) => c.key)).toEqual(["gainDb", "gainDb", "reloadOnRomChange"]);
  expect(store.setGain(9999, 0)).toBeFalsy(); // absent id
});

test("a feature role attaches via the provider; editing it is PURE TS (no emulator apply)", () => {
  const { be, store } = newStore();
  be.seed("/roms/demo.gb", gbRomWithTitle("DEMO-CART"));
  const id = store.addSystem("/roms/demo.gb") as number;
  const kinds = store.view()[0].roles.map((r) => r.kind);
  expect(kinds).toEqual(["sameboy", "demo-sync"]); // system role + provider feature role

  expect(store.setRoleConfig(id, "demo-sync", { level: 5 })).toBeTruthy();
  expect(store.view()[0].roles[1].config.level).toBe(5);
  expect(be.applyRoleCalls.length).toBe(0); // feature-role config never crosses to native
});

test("no registry → no roles attached (back-compat)", () => {
  const { be, store } = newStore(false);
  be.seed("/roms/a.gb", gbRom());
  store.addSystem("/roms/a.gb");
  expect(store.view()[0].roles).toEqual([]);
});
