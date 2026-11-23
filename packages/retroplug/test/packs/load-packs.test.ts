// The pack loader (docs/downloadable-packs-m2c-plan.md): a pack's halves imported through the host, held to its
// manifest, and committed all or nothing per pack, in one commit when every pack passes.
import { test, expect } from "@retroplug/testing";
import { loadPacks, packStatus, APP_BUILDERS, type HalfBuilder } from "../../src/packs/loadPacks";
import type { HalfKind, PackHost, PackInfo } from "../../src/packs/packHost";
import { rolesRegistry } from "../../src/contributions/appRegistries";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { contributionsVersion } from "../../src/contributions/registry";

const role = (kind: string) => ({ kind, category: "feature", schema: { parse: () => ({}) } });

function pack(id: string, over: Partial<PackInfo> = {}): PackInfo {
  return {
    id,
    version: "1.0.0",
    halves: ["app"],
    provides: { platforms: [], nodeTypes: [], roles: [] },
    requires: { packs: [], platforms: [] },
    ...over,
  };
}

/** A host serving `modules[id][half]` as each half's default export (a function is called, so a half can throw). */
function fakeHost(packs: PackInfo[], modules: Record<string, Partial<Record<HalfKind, unknown>>>) {
  const refused: Record<string, string> = {};
  const imported: string[] = [];
  const host: PackHost = {
    list: () => packs,
    importHalf: async (id, half) => {
      imported.push(`${id}/${half}`);
      const m = modules[id]?.[half];
      if (typeof m === "function") return { default: (m as () => unknown)() };
      return { default: m };
    },
    refuse: (id, reason) => { refused[id] = reason; },
  };
  return { host, refused, imported };
}

test("a pack's app half commits its roles, in one commit with the other packs'", async () => {
  const before = contributionsVersion();
  const { host, refused } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } }),
     pack("lp-b", { provides: { platforms: [], nodeTypes: [], roles: ["lp-b-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } }, "lp-b": { app: { roles: [role("lp-b-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(rolesRegistry.items().some((r) => r.kind === "lp-a-role")).toBe(true);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-b-role")).toBe(true);
  expect(contributionsVersion()).toBe(before + 1);
  expect(packStatus().loaded["lp-a"]).toEqual(["app"]);
});

test("a half loaded in this context is not imported again", async () => {
  const { host, imported } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(imported).toEqual([]);
});

test("a pack that fails the commit is refused by name, and the others commit", async () => {
  const builtIn = rolesRegistry.items()[0]!.kind;
  const { host, refused } = fakeHost(
    [pack("lp-dupe", { provides: { platforms: [], nodeTypes: [], roles: [builtIn] } }),
     pack("lp-c", { provides: { platforms: [], nodeTypes: [], roles: ["lp-c-role"] } })],
    { "lp-dupe": { app: { roles: [role(builtIn)] } }, "lp-c": { app: { roles: [role("lp-c-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-dupe"] ?? "").includes(`declare the role kind ${builtIn} twice`)).toBe(true);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-c-role")).toBe(true);
  expect((packStatus().refused["lp-dupe"] ?? "").includes("twice")).toBe(true);
});

test("the manifest is held to the halves", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-undeclared"),
     pack("lp-overdeclared", { provides: { platforms: [], nodeTypes: [], roles: ["lp-ghost"] } }),
     pack("lp-embeds", { provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-kind"),
     pack("lp-throws"),
     pack("lp-shape"),
     pack("lp-dependent", { requires: { packs: ["lp-throws"], platforms: [] } }),
     pack("lp-needs-platform", { halves: ["app", "consoles"], requires: { packs: [], platforms: ["lp-nowhere"] } })],
    {
      "lp-undeclared": { app: { roles: [role("lp-undeclared-role")] } },
      "lp-overdeclared": { app: {} },
      "lp-embeds": { app: { embeds: [{ id: "lp-rom" }] } },
      "lp-kind": { app: { controllerApps: [{ id: "x" }] } },
      "lp-throws": { app: () => { throw new Error("the half threw"); } },
      "lp-shape": { app: 42 },
      "lp-dependent": { app: {} },
      "lp-needs-platform": { app: {}, consoles: {} },
    },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-undeclared"] ?? "").includes("contributes the role kind lp-undeclared-role, which its pack.json's " +
    "provides.roles does not declare")).toBe(true);
  expect((refused["lp-overdeclared"] ?? "").includes("provides.roles names lp-ghost, which no half of the pack contributes")).toBe(true);
  expect((refused["lp-embeds"] ?? "").includes("a pack cannot carry an embedded ROM")).toBe(true);
  expect((refused["lp-kind"] ?? "").includes("controllerApps")).toBe(true);
  expect((refused["lp-throws"] ?? "").includes("the half threw")).toBe(true);
  expect((refused["lp-shape"] ?? "").includes("its app half's default export is not an object")).toBe(true);
  expect((refused["lp-dependent"] ?? "").includes("requires the pack lp-throws, which is refused")).toBe(true);
  expect((refused["lp-needs-platform"] ?? "").includes("requires the platform lp-nowhere, which no loaded console provides")).toBe(true);
});

test("a console's platforms and node type are the pack's to declare", async () => {
  // A console needs its core's system role: borrow a built-in console's, on a platform of the pack's own.
  const borrowed = consolesRegistry.items()[0]!;
  const descriptor = { ...borrowed, id: "lp-console", nodeType: "rp.lp.console",
    platforms: [{ ...borrowed.platforms[0]!, id: "lp-plat", extensions: [".lpx"] }] };
  const { host, refused } = fakeHost(
    [pack("lp-console-bad", { halves: ["consoles"], provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-console", { halves: ["consoles"],
       provides: { platforms: ["lp-plat"], nodeTypes: ["rp.lp.console"], roles: [] } })],
    { "lp-console-bad": { consoles: { consoles: [descriptor] } }, "lp-console": { consoles: { consoles: [descriptor] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-console-bad"] ?? "").includes("the platform lp-plat, which its pack.json's provides.platforms does " +
    "not declare")).toBe(true);
  expect(refused["lp-console"]).toBe(undefined);
  expect(consolesRegistry.items().some((d) => d.id === "lp-console")).toBe(true);
});

test("a context's builders decide which halves it loads", async () => {
  const only: Partial<Record<HalfKind, HalfBuilder>> = { consoles: APP_BUILDERS.consoles };
  const { host, imported } = fakeHost([pack("lp-app-only", { halves: ["app"] })], { "lp-app-only": { app: {} } });
  await loadPacks(host, only);
  expect(imported).toEqual([]);
});
