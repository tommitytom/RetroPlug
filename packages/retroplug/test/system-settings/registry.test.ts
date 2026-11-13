// The generic role registry: extensions register RoleTypes (config schema + clamp)
// + ROM providers (feature roles by ROM header). The core registers the built-in
// backend "system" roles via registerCoreRoles; a FAKE feature extension proves the
// mechanism with zero LSDj knowledge in the core. A role's behaviour is its
// protocol node (script/nodes/), not a RoleType field, so none is exercised here.
import { test, expect } from "@retroplug/testing";
import { RoleRegistry, type RoleType } from "@retroplug/core/systemRoles";
import { buildAppRegistry } from "../../src/appHost";
import { ROLES } from "../../src/generated/packages";
import { protocolNodeOf } from "../../src/graph/protocolNodes";
import { genmddjHeader } from "@retroplug/genmddj/testFixtures";
import { registerCoreRoles } from "../_coreRoles";
import { registerFeatureRoles } from "../_featureRoles";
import { z } from "zod";
import { clampedInt } from "@retroplug/core/configFields";
import { systemKnobs } from "../../src/graph/buildGraph";
import type { SystemNodeType } from "../../src/platform";
import {
  blipToasterRom,
  gbRom,
  gbaRom,
  lsdjRom,
  nesRom,
  risaRom,
  sameboyRoleConfig,
  smsggdjRom,
} from "../systems/fixtures";

// risa's host-sync capability tag ("RISA230") in the header prefix, which a real 2.3.0+ build carries.
function risaRomWithSyncMarker(): Uint8Array {
  const rom = risaRom();
  const marker = "RISA230";
  for (let i = 0; i < marker.length; i++) rom[0x10 + i] = marker.charCodeAt(i);
  return rom;
}

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
  expect(mesen?.schema.parse({})).toEqual({ region: "auto", removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8", expansionVolume: 100, channelExportMode: "mix", sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, driveRealCart: true });
});

test("the app's roles are the product's kinds, and a duplicate is what the generated module refuses", () => {
  // The registry holds roles in a Map keyed by kind and never iterates it, so the order the app registers them in is
  // unobservable: the generated module checks that each kind appears once, and that is the property this pins. A kind
  // lost by a package's app contribution would still show here, whatever order the list is in.
  expect(ROLES.map((role) => role.kind).sort()).toEqual([
    "blastem", "bliptoaster", "bliptoaster-assets", "genmddj-sync", "lsdj-assets", "lsdj-sync", "mesen", "mgb",
    "nes-n8-midi", "risa", "risa-assets", "risa-sync", "sameboy", "sms-sync",
  ]);
});

test("the product's ROM providers fix a system's role-list order", () => {
  // A ROM provider's place in the product list IS behaviour: defaultRoles walks the providers in that order, and the
  // first to match a ROM fixes its feature roles' order. The n8 package's always-on NES provider stands before the
  // carts' (risa's and BlipToaster's), so a NES gets the MIDI passthrough ahead of whatever the cart contributes.
  const reg = buildAppRegistry();
  const kinds = (core: string, platform: string, rom: Uint8Array): string[] =>
    reg.defaultRoles(core as never, platform as never, rom).map((role) => role.kind);

  expect(kinds("mesen", "nes", nesRom())).toEqual(["mesen", "nes-n8-midi"]);
  expect(kinds("mesen", "nes", blipToasterRom())).toEqual([
    "mesen", "nes-n8-midi", "bliptoaster", "bliptoaster-assets",
  ]);
  expect(kinds("mesen", "nes", risaRom())).toEqual(["mesen", "nes-n8-midi", "risa", "risa-assets"]);
  // A sync-capable build adds risa-sync after the two roles risa's provider attaches first.
  expect(kinds("mesen", "nes", risaRomWithSyncMarker())).toEqual([
    "mesen", "nes-n8-midi", "risa", "risa-assets", "risa-sync",
  ]);
  expect(kinds("sameboy", "gb", lsdjRom())).toEqual(["sameboy", "lsdj-sync", "lsdj-assets"]);
  expect(kinds("sameboy", "gb", gbRom())).toEqual(["sameboy"]);
  expect(kinds("mesen", "sms", smsggdjRom())).toEqual(["mesen", "sms-sync"]);
  // The Mega Drive's cart role, behind the console's: the provider matches the cart's identity, so a genmddj image is
  // the only Mega Drive ROM that gets the one thing that opens controller port 2.
  expect(kinds("blastem", "md", genmddjHeader())).toEqual(["blastem", "genmddj-sync"]);
  expect(kinds("mesen", "gba", gbaRom())).toEqual(["mesen"]);
});

test("every field a system role's schema fills reaches some system node", () => {
  // A core config is what its system node's settings are built from (graph/buildGraph.ts systemKnobs): a setting the
  // node applies live, a construct-time field it builds its core with, or a param. A field in neither table is dead -
  // a knob added to a role and forgotten there would never reach a core, and only a document would look right. The
  // registered kinds are read, not listed, so a new system role is covered too.
  const types: SystemNodeType[] = ["rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"];
  const system = kindsRegisteredBy(() => registerCoreRoles(new RoleRegistry())).filter((t) => t.category === "system");
  expect(system.map((t) => t.kind)).toEqual(["sameboy", "mesen"]);
  const has = (o: object, k: string): boolean => Object.prototype.hasOwnProperty.call(o, k);
  for (const t of system) {
    const config = t.schema.parse({}) as Record<string, unknown>;
    for (const field of Object.keys(config)) {
      if (field === "linkGroupId") continue; // wiring (the link cable), not a node setting
      // Placement, not a knob: whether the system drives a real cart decides whether buildGraph places its rp.n8.nes at
      // all, and the node carries nothing from it (the cart's own knob is the system's expansionVolume).
      if (field === "driveRealCart") continue;
      const taken = types.some((type) => {
        const knobs = systemKnobs({ type, coreConfig: config });
        return has(knobs.settings, field) || has(knobs.construct, field) || has(knobs.params, field);
      });
      expect(taken, `${t.kind}.${field}`).toBe(true);
    }
    // A construct-time field a stored config does not take is written at the fallback, which must be the same value
    // the role schema fills for a fresh config: a later schema default not mirrored there would make an old project's
    // sparse config name a different value than a fresh one, and its first edit would rebuild for no reason.
    for (const type of types)
      expect(systemKnobs({ type, coreConfig: config }).construct, `${t.kind} -> ${type}`).toEqual(
        systemKnobs({ type }).construct,
      );
  }
});

test("mesen schema: fills defaults + defaults an unknown region", () => {
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  const mesen = reg.roleType("mesen")!;
  expect(mesen.schema.parse({ region: 99 })).toEqual({ region: "auto", removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8", expansionVolume: 100, channelExportMode: "mix", sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, driveRealCart: true });
  expect(mesen.schema.parse({ removeSpriteLimit: true })).toEqual({ region: "auto", removeSpriteLimit: true, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8", expansionVolume: 100, channelExportMode: "mix", sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, driveRealCart: true });
});

test("mesen schema: expansion volume is a whole percent, as its node takes it", () => {
  // The node's param is an int: the planner rounds a document's value and the core takes whole percent, so the schema
  // a store holds rounds too, to the whole percent the node would give the core.
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  const mesen = reg.roleType("mesen")!;
  const volume = (v: unknown): number => (mesen.schema.parse({ expansionVolume: v }) as { expansionVolume: number }).expansionVolume;
  expect(volume(25.7)).toBe(26);
  expect(volume(25.2)).toBe(25);
  expect(volume(-5)).toBe(0);
  expect(volume(250)).toBe(200);
  expect(volume(Number.NaN)).toBe(100); // the helper's default, as a non-number gets
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
    { kind: "mesen", config: { region: "auto", removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8", expansionVolume: 100, channelExportMode: "mix", sdRoot: "", fifo: "instant", fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: "auto", n8LinkRttUs: 2200, driveRealCart: true } },
  ]);
});

// Every role kind `register` registers, in order.
function kindsRegisteredBy(register: () => void): RoleType[] {
  const types: RoleType[] = [];
  const registerRole = RoleRegistry.prototype.registerRole;
  RoleRegistry.prototype.registerRole = function (t) {
    types.push(t);
    registerRole.call(this, t);
  };
  try {
    register();
  } finally {
    RoleRegistry.prototype.registerRole = registerRole;
  }
  return types;
}

test("the protocol-node table names a node for exactly the feature roles that have one", () => {
  // The stores place what graph/protocolNodes.ts says, not what a registry says, so the two must agree for every role
  // the app registers: each kind registerFeatureRoles registers (the roles whose work is the audio thread's) has a
  // node, and no other kind the app registers does: not a core's config role, not a feature with none (an asset
  // override, a ROM's facts). A role registered there with no node, a node for a role registered elsewhere, or a kind
  // renamed on either side fails here.
  const dsp = kindsRegisteredBy(() => registerFeatureRoles(new RoleRegistry())).map((t) => t.kind);
  expect(dsp).toEqual(["mgb", "nes-n8-midi", "risa-sync", "sms-sync", "genmddj-sync", "lsdj-sync"]);
  const app = kindsRegisteredBy(() => buildAppRegistry());
  expect(app.length).toBeGreaterThan(10);
  for (const t of app) expect(protocolNodeOf(t.kind) !== undefined, t.kind).toBe(dsp.includes(t.kind));
  for (const kind of dsp) expect(app.some((t) => t.kind === kind && t.category === "feature"), kind).toBe(true);
});
