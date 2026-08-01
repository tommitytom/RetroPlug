// The generic role registry: extensions register RoleTypes (config schema + clamp)
// + ROM providers (feature roles by ROM header). The core registers the built-in
// backend "system" roles via registerCoreRoles; a FAKE feature extension proves the
// mechanism with zero LSDj knowledge in the core. Role behavior (the doc-06
// translators) is a deferred RoleType seam, not exercised here.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { z, clampedInt } from "../../src/configSchema";
import { sameboyRoleConfig } from "../systems/fixtures";

// A ROM header (0x150 bytes) carrying an ASCII cartridge title at 0x134.
function headerWithTitle(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

// A fake third-party extension: a "demo-sync" feature role + a provider that attaches
// it to any ROM whose title starts with "DEMO". No core changes needed.
function registerFakeExtension(reg: RoleRegistry): void {
  reg.registerRole({
    kind: "demo-sync",
    category: "feature",
    schema: z.object({ level: clampedInt(0, 10, 1) }),
  });
  reg.registerRomProvider(({ header }) => {
    const title = String.fromCharCode(...header.slice(0x134, 0x138));
    return title.startsWith("DEMO") ? [{ kind: "demo-sync", config: { level: 1 } }] : [];
  });
}

test("systemRoleFor: the core registers a SameBoy and a Mesen system role", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  const sb = reg.systemRoleFor("sameboy");
  expect(sb?.kind).toBe("sameboy");
  expect(sb?.category).toBe("system");
  expect(sb?.schema.parse({})).toEqual(sameboyRoleConfig());

  const mesen = reg.systemRoleFor("mesen");
  expect(mesen?.kind).toBe("mesen");
  expect(mesen?.category).toBe("system");
  expect(mesen?.schema.parse({})).toEqual({ region: "auto", removeSpriteLimit: false, apuLatencyMs: 1.4, channelExportMode: "mix" });
});

test("mesen schema: fills defaults + defaults an unknown region", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  const mesen = reg.roleType("mesen")!;
  expect(mesen.schema.parse({ region: 99 })).toEqual({ region: "auto", removeSpriteLimit: false, apuLatencyMs: 1.4, channelExportMode: "mix" });
  expect(mesen.schema.parse({ removeSpriteLimit: true })).toEqual({ region: "auto", removeSpriteLimit: true, apuLatencyMs: 1.4, channelExportMode: "mix" });
});

test("schema: fills defaults + defaults unknown enums, clamps numeric fields", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  const sb = reg.roleType("sameboy")!;
  expect(sb.schema.parse({})).toEqual(sameboyRoleConfig());
  expect(sb.schema.parse({ model: 99, highpass: 5, linkGroupId: 999 })).toEqual(
    sameboyRoleConfig({ linkGroupId: 255 }),
  );
});

test("a fake feature extension registers a role + a ROM provider", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerFakeExtension(reg);
  expect(reg.roleType("demo-sync")?.category).toBe("feature");
  expect(reg.roleType("nope")).toBe(undefined);
});

test("defaultRoles: system role first, then provider-matched feature roles", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerFakeExtension(reg);

  const demo = reg.defaultRoles("sameboy", "gb", headerWithTitle("DEMO-GAME"));
  expect(demo).toEqual([
    { kind: "sameboy", config: sameboyRoleConfig() },
    { kind: "demo-sync", config: { level: 1 } },
  ]);

  // A non-matching ROM gets only the backend system role.
  const plain = reg.defaultRoles("sameboy", "gb", headerWithTitle("ZELDA"));
  expect(plain).toEqual([
    { kind: "sameboy", config: sameboyRoleConfig() },
  ]);

  // A NES system gets the Mesen system role (no feature provider registered in this reg → just it).
  expect(reg.defaultRoles("mesen", "nes", headerWithTitle("SMB3"))).toEqual([
    { kind: "mesen", config: { region: "auto", removeSpriteLimit: false, apuLatencyMs: 1.4, channelExportMode: "mix" } },
  ]);
});
