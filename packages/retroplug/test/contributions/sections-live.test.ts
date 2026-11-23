// Sections and services follow a commit (docs/downloadable-packs-m2a-plan.md, Task 4): a project section, a project
// service and a config section a pack contributes after the stores were built are parsed from the block the project or
// the file kept verbatim (or defaulted), the service is built and polled, and the store publishes when asked to adopt.
import { test, expect } from "@retroplug/testing";
import type { ProjectSection } from "@retroplug/core/definePackage";
import type { ProjectServiceFactory } from "@retroplug/tracker/projectService";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import { defaultSettings, K_PROJECT, projectSection } from "../../src/projectConfig";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { gbRom } from "../systems/fixtures";
import { UserConfigStore } from "../../src/userConfigStore";
import { USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG } from "../../src/userConfig";
import { stringifyConfig } from "../../src/configSchema";

const CONFIG = "/config/config.json";

/** A section whose parse marks what it read, so a test can tell a parsed block from the raw one the store kept. */
function markingSection(key: string): ProjectSection<{ value: number; parsed: true }> {
  return {
    key,
    schema: {
      parse: (raw: unknown) => ({ value: Number((raw as { value?: unknown } | undefined)?.value ?? 0), parsed: true }),
    },
    defaults: () => ({ value: 7, parsed: true }),
  };
}

test("a project section committed after a load parses the block the project kept verbatim", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const raw = JSON.stringify({ schemaVersion: K_PROJECT, settings: { layout: "column", packfixtureA: { value: 3 } },
    systems: [] });
  be.seed("/p/one.rplg", raw);
  stores.project.load("/p/one.rplg");
  // No contributor yet: the block is kept as the file carried it.
  expect(stores.project.section("packfixtureA")).toEqual({ value: 3 });
  commitContributions(appAdditions("packfixture", { projectSections: [markingSection("packfixtureA")] }));
  expect(stores.project.section("packfixtureA")).toEqual({ value: 3, parsed: true });
  // And it saves back through its contributor.
  expect(projectSection(stores.project.settings(), "packfixtureA")).toEqual({ value: 3, parsed: true });
});

test("a project section the project never carried reads its defaults, and a new project starts with them", () => {
  const stores = composeAppStores({ backend: new MockBackend("/cfg") });
  commitContributions(appAdditions("packfixture-b", { projectSections: [markingSection("packfixtureB")] }));
  expect(stores.project.section("packfixtureB")).toEqual({ value: 7, parsed: true });
  stores.project.newProject();
  expect(stores.project.section("packfixtureB")).toEqual({ value: 7, parsed: true });
  expect(projectSection(defaultSettings(), "packfixtureB")).toEqual({ value: 7, parsed: true });
});

test("a block a section parsed is not parsed again by a later commit", () => {
  let parses = 0;
  const counting: ProjectSection<{ n: number }> = {
    key: "packfixtureC",
    schema: { parse: (raw: unknown) => (parses++, { n: Number((raw as { n?: unknown } | undefined)?.n ?? 0) }) },
    defaults: () => ({ n: 0 }),
  };
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/p/three.rplg", JSON.stringify({ schemaVersion: K_PROJECT, settings: { packfixtureC: { n: 2 } }, systems: [] }));
  stores.project.load("/p/three.rplg");
  commitContributions(appAdditions("packfixture-c", { projectSections: [counting] }));
  expect(stores.project.section("packfixtureC")).toEqual({ n: 2 });
  expect(parses).toBe(1);
  commitContributions(appAdditions("packfixture-c2", { projectSections: [markingSection("packfixtureC2")] }));
  expect(stores.project.section("packfixtureC")).toEqual({ n: 2 });
  expect(parses).toBe(1);
});

test("a project service committed later is built once, polled, and its model published on adopt", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  let created = 0;
  let polls = 0;
  const factory: ProjectServiceFactory = {
    key: "packfixtureD",
    create: () => {
      created++;
      return {
        poll: () => (polls++, false),
        state: () => ({ alive: true }),
      };
    },
  } as unknown as ProjectServiceFactory;
  commitContributions(appAdditions("packfixture-d", {
    projectSections: [markingSection("packfixtureD")],
    projectServices: [factory],
  }));
  expect(stores.project.adoptContributions()).toBe(true);
  expect(stores.project.adoptContributions()).toBe(false);
  expect(created).toBe(1);
  stores.project.pollServices();
  expect(polls).toBe(1);
  expect(stores.project.serviceState("packfixtureD")).toEqual({ alive: true });
});

test("a config section committed later parses the block the file carried, and a missing one reads its defaults", () => {
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG,
    packfixtureE: { value: 5 } }));
  const store = new UserConfigStore(be, () => {});
  store.load();
  expect(store.section("packfixtureE")).toEqual({ value: 5 });
  commitContributions(appAdditions("packfixture-e", {
    configSections: [markingSection("packfixtureE"), markingSection("packfixtureF")],
  }));
  expect(store.section("packfixtureE")).toEqual({ value: 5, parsed: true });
  expect(store.section("packfixtureF")).toEqual({ value: 7, parsed: true });
  // The file is not stamped with a block nobody set: a core write leaves packfixtureF out of it.
  expect(store.setDefaultZoom(2)).toBeTruthy();
  const written = JSON.parse(be.readText(CONFIG)!) as Record<string, unknown>;
  expect("packfixtureF" in written).toBe(false);
  expect(written.packfixtureE).toEqual({ value: 5 });
});

test("a store built over fixed contributions adopts nothing", () => {
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, packfixtureG: { value: 1 } }));
  const store = new UserConfigStore(be, () => {}, []);
  store.load();
  commitContributions(appAdditions("packfixture-g", { configSections: [markingSection("packfixtureG")] }));
  expect(store.section("packfixtureG")).toEqual({ value: 1 });
});

test("a service whose create throws is isolated and tried once", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  let created = 0;
  const factory = {
    key: "packfixtureI",
    create: () => {
      // Throws on the first call only: the registry outlives this case, and a later store's constructor builds it too.
      if (created++ === 0) throw new Error("a broken service");
      return {};
    },
  } as unknown as ProjectServiceFactory;
  const warned: string[] = [];
  const realWarn = console.warn;
  console.warn = (...args: unknown[]) => warned.push(args.map(String).join(" "));
  try {
    commitContributions(appAdditions("packfixture-i", {
      projectSections: [markingSection("packfixtureI")],
      projectServices: [factory],
    }));
    expect(stores.project.settings()).toBeTruthy();
    expect(stores.project.section("packfixtureI")).toEqual({ value: 7, parsed: true });
    expect(stores.project.save("/p/i.rplg")).toBe(true);
    expect(stores.project.serviceState("packfixtureI") === undefined).toBe(true);
    expect(created).toBe(1);
    expect(warned.some((line) => line.includes("a broken service"))).toBe(true);
  } finally {
    console.warn = realWarn;
  }
});

test("a load held for relink adopts a section committed meanwhile from the block the parse kept", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  // Author the project through a save so its system entry has the shape a load reads, then take the ROM away.
  const author = new MockBackend("/cfg");
  const authored = composeAppStores({ backend: author });
  author.seed("/p/a.gb", gbRom());
  authored.project.systems.addSystem("/p/a.gb");
  authored.project.save("/p/h.rplg");
  const saved = JSON.parse(author.readText("/p/h.rplg")!) as { settings: Record<string, unknown> };
  saved.settings.packfixtureH = { value: 9 };
  be.seed("/p/h.rplg", JSON.stringify(saved));
  be.seed("/new/a.gb", gbRom());
  expect(stores.project.load("/p/h.rplg").kind).toBe("missing");
  commitContributions(appAdditions("packfixture-h", { projectSections: [markingSection("packfixtureH")] }));
  // The outgoing project adopts it as a default; the pending one is not in the store yet.
  expect(stores.project.section("packfixtureH")).toEqual({ value: 7, parsed: true });
  const done = stores.project.relink({ systemIndex: 0, itemKind: "rom", path: "/p/a.gb" }, "/new/a.gb");
  expect(done.kind).toBe("loaded");
  expect(stores.project.section("packfixtureH")).toEqual({ value: 9, parsed: true });
});

test("a project store over fixed contributions adopts nothing", () => {
  const be = new MockBackend("/cfg");
  const recent = new RecentStore(be, () => {});
  const project = new ProjectStore(be, recent, undefined, { sections: [], services: [] });
  be.seed("/p/j.rplg", JSON.stringify({ schemaVersion: K_PROJECT, settings: { packfixtureJ: { value: 4 } }, systems: [] }));
  project.load("/p/j.rplg");
  let created = 0;
  const factory = { key: "packfixtureJ", create: () => (created++, {}) } as unknown as ProjectServiceFactory;
  commitContributions(appAdditions("packfixture-j", {
    projectSections: [markingSection("packfixtureJ")],
    projectServices: [factory],
  }));
  expect(project.adoptContributions()).toBe(false);
  expect(project.section("packfixtureJ")).toEqual({ value: 4 });
  expect(created).toBe(0);
});
