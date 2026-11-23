// The menu contributions follow a commit (docs/downloadable-packs-m2a-plan.md, Task 5): a UI contribution committed
// after the built-ins adds its rows, its menu builders, its actions and its services to what the composers read, and a
// commit the menus' checks refuse changes nothing. The views are the UI suite's (test-ui/contributions-live.test.ts):
// a views module reaches React, which this tier has no path to.
import { test, expect } from "@retroplug/testing";
import type { UiContribution } from "@retroplug/ui/contribution";
import type { RoleType } from "@retroplug/core/systemRoles";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import { uiRegistry } from "../../ui/contributions/menus";
import {
  contributedActionIds,
  contributedActions,
  menuPlacement,
  roleRows,
  uiServices,
} from "../../ui/host/menuContributions";

const fixtureRole = {
  kind: "packfixture-ui-role",
  category: "feature",
  label: "Pack Fixture UI",
  schema: { parse: () => ({ level: 1, on: false }) },
} as unknown as RoleType;

const fixtureUi = {
  rows: [{ id: "packfixture-level", role: "packfixture-ui-role", key: "level", label: "Level", kind: "cycle",
    choices: [{ value: 1, label: "One" }] }],
  // A placement no built-in package builds at, so the composer runs this builder alone over the stand-in host.
  menus: [{ placement: "instance.tracker", build: () => [{ id: "packfixture-hello", label: "Hello", kind: "action" }] }],
  actions: [{ id: "packfixture.toggle", label: "Toggle the fixture", run: () => {} }],
  services: [{ id: "packfixture-service" }],
} as unknown as UiContribution;

test("the built-in UI contributions are seeded", () => {
  expect(uiRegistry.seeded).toBe(true);
  expect(uiRegistry.items().length).toBeGreaterThan(0);
});

test("a UI contribution committed later is read by every composer", () => {
  const actionIdsBefore = contributedActionIds();
  commitContributions([
    ...appAdditions("packfixture", { roles: [fixtureRole] }),
    { registry: uiRegistry, pkg: "packfixture", items: [fixtureUi] },
  ]);
  expect(roleRows().some((row) => row.id === "packfixture-level")).toBe(true);
  const built = menuPlacement("instance.tracker", {} as never, null, {} as never);
  expect(built.some((item) => item.id === "packfixture-hello")).toBe(true);
  expect(contributedActions().some((entry) => entry.pkg === "packfixture" && entry.action.id === "packfixture.toggle"))
    .toBe(true);
  // A new list, so a memo keyed on it (App's key maps) rebuilds.
  expect(contributedActionIds() === actionIdsBefore).toBe(false);
  expect(contributedActionIds().includes("packfixture.toggle")).toBe(true);
  expect(uiServices().some((service) => service.id === "packfixture-service")).toBe(true);
});

test("the lists keep their identity between commits", () => {
  expect(contributedActionIds()).toBe(contributedActionIds());
  expect(uiServices()).toBe(uiServices());
});

test("a row over a role no package registers is refused, and nothing moves", () => {
  const count = uiRegistry.items().length;
  const stray = { rows: [{ id: "stray", role: "no-such-role", key: "level", label: "Stray", kind: "cycle",
    choices: [] }] } as unknown as UiContribution;
  expect(() => commitContributions([{ registry: uiRegistry, pkg: "stray", items: [stray] }])).toThrow(
    "the package stray declares the row stray over the role no-such-role, which no registered role type has",
  );
  expect(uiRegistry.items().length).toBe(count);
});

test("an action not prefixed by its package's name is refused", () => {
  const bad = { actions: [{ id: "other.thing", label: "Thing", run: () => {} }] } as unknown as UiContribution;
  expect(() => commitContributions([{ registry: uiRegistry, pkg: "packfixture2", items: [bad] }])).toThrow(
    'the package packfixture2 declares the action other.thing, which is not prefixed by its own name ("packfixture2.")',
  );
});
