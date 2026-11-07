// The systems store carrying per-system settings + roles. A constructed system
// auto-gets its backend "system" role (SameBoy = model/highpass/…) + universal
// settings (gain/reload). Editing a SYSTEM role reaches the emulator as its system node's
// knobs (a setting publishes the document, a param is set live); editing a FEATURE role
// with no protocol node is pure TS. A fake extension proves the feature path with no LSDj
// in the core.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "@retroplug/core/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { z, clampedInt } from "../../src/configSchema";
import { gbRom, nesRom, sameboyRoleConfig } from "../systems/fixtures";
import { canonicalJson } from "@retroplug/core/canonicalJson";

// `v` as a document carries it: canonical JSON, its keys sorted.
const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));

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

test("a NES system auto-gets the mesen system role; editing region crosses to the emulator in the document", () => {
  const { be, store } = newStore();
  be.seed("/roms/g.nes", nesRom());
  const id = store.addSystem("/roms/g.nes") as number;
  const v = store.view()[0];
  expect(v.platform).toBe("nes");
  expect(v.roles).toEqual([{ kind: "mesen", config: { region: "auto", removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8", expansionVolume: 100, channelExportMode: "mix", sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, driveRealCart: true } }]);

  expect(store.setRoleConfig(id, "mesen", { region: "pal" })).toBeTruthy(); // PAL
  expect(store.view()[0].roles[0].config.region).toBe("pal");
  // Its node's settings, labels as the role spells them; its params, the gain and the expansion volume.
  expect(be.graphNode(`sys-${id}`)).toEqual(
    canon({
      id: `sys-${id}`,
      type: "rp.mesen.nes",
      settings: {
        system: id,
        rom: "res:1",
        romName: "g",
        region: "pal",
        removeSpriteLimit: false,
        apuLatencyMs: 1.4,
        s5bNoise: "n8",
        mmc5PhaseReset: "n8",
        // The construct-time fields its node builds the core with.
        sdRoot: "",
        fifo: "instant",
        fifoDepth: 0,
        fifoBytesPerSecond: 0,
        n8Link: "auto",
        n8LinkRttUs: 2200,
        channelExportMode: "mix",
      },
      params: { gain: 0, expansionVolume: 100 },
    }),
  );
});

test("setRoleConfig on a system role: updates, clamps, publishes its node's settings, dirties", () => {
  const { be, store, changes } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb") as number;
  const before = changes();
  const sent = be.graphRequests.length;
  expect(store.setRoleConfig(id, "sameboy", { model: "sgb" })).toBeTruthy();
  expect(store.view()[0].roles[0].config.model).toBe("sgb");
  // The labels as the role config spells them, never ordinals: the document and a saved project carry one word. The
  // link group is wiring, not a setting.
  expect(be.graphRequests.length).toBe(sent + 1);
  expect(be.graphNode(`sys-${id}`)?.settings).toEqual(
    canon({
      system: id,
      rom: "res:1",
      romName: "a",
      model: "sgb",
      highpass: "accurate",
      fastBoot: true,
      colorCorrection: "disabled",
      dmgPalette: "grey",
      lightTemperature: 0,
      serialOutCapture: "off",
    }),
  );
  expect(changes()).toBe(before + 1);
  // unknown enum → defaulted, not stored raw
  store.setRoleConfig(id, "sameboy", { model: 99 });
  expect(store.view()[0].roles[0].config.model).toBe("cgbC");
  // absent role → false
  expect(store.setRoleConfig(id, "nope", { x: 1 })).toBeFalsy();
});

test("setGain / setReloadOnRomChange: clamp, update; the gain set live on its node, the reload flag TS's alone", () => {
  const { be, store, changes } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb") as number;
  const before = changes();
  const sent = be.graphRequests.length;
  expect(store.setGain(id, -6)).toBeTruthy();
  expect(store.view()[0].settings.gainDb).toBe(-6);
  expect(store.setGain(id, -500)).toBeTruthy(); // below -90 → clamped
  expect(store.view()[0].settings.gainDb).toBe(-90);
  store.setReloadOnRomChange(id, true);
  expect(store.view()[0].settings.reloadOnRomChange).toBeTruthy();
  expect(be.paramCalls).toEqual([
    { nodeId: `sys-${id}`, paramId: "gain", value: -6 },
    { nodeId: `sys-${id}`, paramId: "gain", value: -90 },
  ]);
  expect(be.graphRequests.length).toBe(sent); // no publish
  expect(changes()).toBe(before + 3); // each dirties
  expect(store.setGain(9999, 0)).toBeFalsy(); // absent id
  expect(store.setReloadOnRomChange(9999, true)).toBeFalsy();
});

test("a feature role attaches via the provider; editing it is PURE TS (no emulator apply)", () => {
  const { be, store } = newStore();
  be.seed("/roms/demo.gb", gbRomWithTitle("DEMO-CART"));
  const id = store.addSystem("/roms/demo.gb") as number;
  const kinds = store.view()[0].roles.map((r) => r.kind);
  expect(kinds).toEqual(["sameboy", "demo-sync"]); // system role + provider feature role

  const sent = be.graphRequests.length;
  expect(store.setRoleConfig(id, "demo-sync", { level: 5 })).toBeTruthy();
  expect(store.view()[0].roles[1].config.level).toBe(5);
  // Feature-role config never crosses to native: no document, no param.
  expect(be.graphRequests.length).toBe(sent);
  expect(be.paramCalls.length).toBe(0);
});

test("no registry → no roles attached (back-compat)", () => {
  const { be, store } = newStore(false);
  be.seed("/roms/a.gb", gbRom());
  store.addSystem("/roms/a.gb");
  expect(store.view()[0].roles).toEqual([]);
});
