// A saved system names the package that provided each thing it names (docs/downloadable-packs-m3-design.md section 4):
// its platform's console, its embedded ROM's and each role's, built in or a pack alike, from the registries' `pkg`,
// with a pack's name and version from the pack list and a built-in's name from its pack.json (or its id).
import { test, expect } from "@retroplug/testing";
import { buildConfig, DEFAULT_SETTINGS, parseConfig } from "../../src/projectConfig";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { gbRom } from "../systems/fixtures";
import type { SystemEntry } from "../../src/systemsList";
import type { RoleType } from "@retroplug/core/systemRoles";

function entry(over: Partial<SystemEntry>): SystemEntry {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/a.gb",
    savPath: "",
    savSuffix: 0,
    embeddedRom: "",
    battery: false,
    settings: { gainDb: 0, reloadOnRomChange: false },
    roles: [],
    romName: "a",
    resources: { rom: 1 },
    ...over,
  } as SystemEntry;
}

test("a built-in package is recorded by its id, and by its pack.json name where it has one", () => {
  const [gb] = buildConfig(DEFAULT_SETTINGS, [entry({ roles: [{ kind: "sameboy", config: {} }] })]).systems;
  expect(gb.provenance).toEqual({
    platform: { id: "sameboy", name: "sameboy" },
    roles: { sameboy: { id: "sameboy", name: "sameboy" } },
  });
  // The desktop links Mesen, whose pack.json names it: a project saved here names Mesen on a build where it is a pack.
  const [nes] = buildConfig(DEFAULT_SETTINGS, [
    entry({ platform: "nes", core: "mesen", romPath: "/roms/a.nes", roles: [{ kind: "mesen", config: {} }] }),
  ]).systems;
  expect(nes.provenance?.platform).toEqual({ id: "mesen", name: "Mesen" });
  expect(nes.provenance?.roles?.mesen).toEqual({ id: "mesen", name: "Mesen" });
});

test("a pack is recorded with the name and version the pack list gives it", () => {
  const g = globalThis as Record<string, unknown>;
  g.__rp_packList = () =>
    JSON.stringify([
      {
        id: "provfix",
        name: "Provenance Fixture",
        version: "4.5.6",
        halves: ["app"],
        provides: { platforms: [], nodeTypes: [], roles: ["provfix-role"] },
        requires: { packs: [], platforms: [] },
      },
    ]);
  const role = { kind: "provfix-role", category: "feature", schema: { parse: () => ({}) } } as unknown as RoleType;
  // A registry only grows, so the commit stays for the rest of this file's bundle; the pack list is put back.
  try {
    commitContributions(appAdditions("provfix", { roles: [role] }));
    const [sys] = buildConfig(DEFAULT_SETTINGS, [entry({ roles: [{ kind: "provfix-role", config: {} }] })]).systems;
    expect(sys.provenance?.roles?.["provfix-role"]).toEqual({
      id: "provfix",
      name: "Provenance Fixture",
      version: "4.5.6",
    });
  } finally {
    delete g.__rp_packList;
  }
});

test("a role kind nothing registers keeps the provenance the file had for it, and a reader keeps provenance", () => {
  const stored = {
    platform: { id: "sameboy", name: "sameboy" },
    roles: { "gone-kind": { id: "gone-pack", name: "Gone", version: "1.0.0" } },
  };
  const parsed = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [
      {
        platform: "gb",
        core: "sameboy",
        romPath: "/roms/a.gb",
        roles: [{ kind: "gone-kind", config: {} }],
        provenance: stored,
      },
    ],
  }));
  expect(parsed.systems[0].provenance).toEqual(stored);
  const [sys] = buildConfig(DEFAULT_SETTINGS, [
    entry({ roles: [{ kind: "gone-kind", config: {} }], provenance: parsed.systems[0].provenance }),
  ]).systems;
  expect(sys.provenance?.roles?.["gone-kind"]).toEqual({ id: "gone-pack", name: "Gone", version: "1.0.0" });
});

test("a malformed provenance is read as none, and the system still parses", () => {
  const parsed = parseConfig(JSON.stringify({
    schemaVersion: "4",
    systems: [{ platform: "gb", core: "sameboy", romPath: "/roms/a.gb", provenance: { platform: 5, roles: "x" } }],
  }));
  expect(parsed.systems.length).toBe(1);
  expect(parsed.systems[0].provenance).toEqual({});
});

test("a role kind nothing registers keeps its provenance through a load, a duplicate, a reset and a save", () => {
  const be = new MockBackend("/cfg");
  be.seed("/proj/a.gb", gbRom());
  const gone = { id: "gone-pack", name: "Gone", version: "1.0.0" };
  be.seed(
    "/proj/p.rplg",
    JSON.stringify({
      schemaVersion: "4",
      systems: [
        {
          platform: "gb",
          core: "sameboy",
          romPath: "/proj/a.gb",
          roles: [{ kind: "gone-kind", config: {} }],
          provenance: { platform: { id: "sameboy", name: "sameboy" }, roles: { "gone-kind": gone } },
        },
      ],
    }),
  );
  const project = new ProjectStore(be, new RecentStore(be));
  expect(project.load("/proj/p.rplg").kind).toBe("loaded");
  const [first] = project.systems.view();
  const copy = project.systems.duplicateSystem(first.id)!;
  expect(copy).toBeTruthy();
  expect(project.systems.reset(first.id) !== null).toBe(true);
  expect(project.save("/proj/q.rplg")).toBeTruthy();
  const saved = JSON.parse(be.readText("/proj/q.rplg")!);
  expect(saved.systems.length).toBe(2);
  for (const sys of saved.systems) expect(sys.provenance.roles["gone-kind"]).toEqual(gone);
  for (const v of project.systems.view()) project.systems.removeSystem(v.id);
});
