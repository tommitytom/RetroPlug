// The app's registries follow a commit (docs/downloadable-packs-m2a-plan.md, Task 3): a role, a ROM provider, a tracker
// integration, a kit format and an embed committed after the built-ins reach the role registry the stores drive, the
// tracker lookups, the kit dispatch and the embed lookups; and a commit the app's checks refuse changes nothing. Every
// id here is this file's own.
import { test, expect } from "@retroplug/testing";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import type { KitFormat } from "@retroplug/dsp/kitContract";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import {
  appAdditions,
  embedsRegistry,
  bakedEmbedsRegistry,
  kitFormatsRegistry,
  rolesRegistry,
} from "../../src/contributions/app";
import { buildAppRegistry } from "../../src/appHost";
import { resolveTracker, trackerIntegrations, trackerMarkers } from "../../src/trackerRegistry";
import { kitFormat, kitFormats } from "../../src/kitFormats";
import { embeddedRomOf, embeddedRoms } from "../../src/embeds";

const fixtureRole = {
  kind: "packfixture-core",
  category: "system",
  label: "Pack Fixture",
  schema: { parse: (raw: unknown) => ({ level: 3, ...(raw as object) }) },
} as unknown as RoleType;

const markerRole = {
  kind: "packfixture-marker",
  category: "feature",
  label: "Pack Fixture Tracker",
  schema: { parse: () => ({}) },
} as unknown as RoleType;

const fixtureConsole = {
  id: "packfixture",
  core: "packfixture-core",
  nodeType: "rp.packfixture.system",
  platforms: [{ id: "packfixture", label: "Pack Fixture", extensions: [".rpk"] }],
} as unknown as ConsoleDescriptor;

const fixtureTracker = {
  id: "packfixture-tracks",
  label: "Pack Fixture",
  markerRole: "packfixture-marker",
} as unknown as TrackerIntegration;

const fixtureKit = { id: "packfixture-kit" } as unknown as KitFormat<unknown>;

test("the built-in app contributions are seeded", () => {
  expect(rolesRegistry.seeded).toBe(true);
  expect(rolesRegistry.items().length).toBeGreaterThan(0);
  expect(trackerIntegrations().length).toBeGreaterThan(0);
});

test("a pack's app contribution reaches a role registry built before it, and the tracker and kit lookups", () => {
  // Built before the commit: the stores hold one for their whole life, so it must adopt what arrives later.
  const registry = buildAppRegistry();
  expect(registry.roleType("packfixture-core")).toBe(undefined);
  commitContributions([
    { registry: consolesRegistry, pkg: "packfixture", items: [fixtureConsole] },
    ...appAdditions("packfixture", {
      roles: [fixtureRole, markerRole],
      romProviders: [
        (rom) => (rom.platform === "packfixture" ? [{ kind: "packfixture-marker", config: {} }] : []),
      ],
      trackerIntegrations: [fixtureTracker],
      kitFormats: [fixtureKit],
    }),
  ]);
  expect(registry.roleType("packfixture-core")?.kind).toBe("packfixture-core");
  expect(registry.systemRoleFor("packfixture-core")?.kind).toBe("packfixture-core");
  const roles = registry.defaultRoles("packfixture-core", "packfixture", new Uint8Array(0));
  expect(roles.map((role) => role.kind)).toEqual(["packfixture-core", "packfixture-marker"]);
  expect(roles[0].config).toEqual({ level: 3 });
  expect(trackerMarkers().has("packfixture-marker")).toBe(true);
  expect(resolveTracker([{ kind: "packfixture-marker", config: {} }])?.id).toBe("packfixture-tracks");
  expect(kitFormats().some((format) => format.id === "packfixture-kit")).toBe(true);
  expect(kitFormat("packfixture-kit")).toBe(fixtureKit);
});

test("a console whose core no system role names is refused with the roles' message", () => {
  const orphan = { ...fixtureConsole, id: "orphan", core: "orphan-core", nodeType: "rp.orphan.system",
    platforms: [{ id: "orphan", label: "Orphan", extensions: [".orp"] }] } as unknown as ConsoleDescriptor;
  expect(() => commitContributions([{ registry: consolesRegistry, pkg: "orphan", items: [orphan] }])).toThrow(
    "the console orphan names the core orphan-core, whose system role no package contributes",
  );
});

test("a duplicate kit format id is refused and nothing moves", () => {
  const count = kitFormatsRegistry.items().length;
  expect(() => commitContributions(appAdditions("packfixture-two", { kitFormats: [fixtureKit] }))).toThrow(
    "the app's kit formats declare the id packfixture-kit twice",
  );
  expect(kitFormatsRegistry.items().length).toBe(count);
});

test("an embed needs its manifest half from the same package, and a console for its platform", () => {
  const embed = { id: "packfixture-synth", label: "Pack Synth", platform: "packfixture" };
  expect(() => commitContributions(appAdditions("packfixture", { embeds: [embed] }))).toThrow(
    "the package packfixture declares the embedded ROM packfixture-synth, which its manifest does not bake in " +
      "(its retroplug.embed)",
  );
  commitContributions([
    ...appAdditions("packfixture", { embeds: [embed] }),
    { registry: bakedEmbedsRegistry, pkg: "packfixture", items: [{ id: "packfixture-synth" }] },
  ]);
  expect(embeddedRomOf("packfixture-synth")?.label).toBe("Pack Synth");
  expect(embeddedRoms().some((e) => e.id === "packfixture-synth")).toBe(true);
  expect(embedsRegistry.entries().find((e) => e.item.id === "packfixture-synth")?.pkg).toBe("packfixture");
});
