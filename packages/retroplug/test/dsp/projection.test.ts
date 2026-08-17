// projectKernelStructure turns the store's live systems (SystemView[]) into the DSP kernel's
// structure: a synthesized project-scope midi-routing role + one pipeline per system mirroring
// its roles in order. Pure — no backend, no registry — so it's tested against plain views.
import { test, expect } from "../../testing/harness";
import { projectKernelStructure, type ControllerProjection } from "../../src/kernelProjection";
import { buildConfig, serializeConfig, parseConfig, DEFAULT_SETTINGS, K_PROJECT } from "../../src/projectConfig";
import type { SystemView } from "../../src/systemsStore";
import type { RoleInstance } from "../../src/systemRoles";

// A minimal SystemView carrying just the fields the projection reads (id + roles); the rest are
// filled with inert defaults so the type is satisfied.
function view(id: number, roles: RoleInstance[]): SystemView {
  return {
    id,
    platform: "gb",
    core: "sameboy",
    romPath: "",
    savPath: "",
    savSuffix: 0,
    embedded: false,
    battery: false,
    focused: false,
    missing: false,
    settings: { gainDb: 0, reloadOnRomChange: false },
    roles,
  };
}

test("projectKernelStructure: synthesizes the project midi-routing role from the routing mode", () => {
  const s = projectKernelStructure([], "oneChannelPerInstance");
  expect(s.project).toEqual([{ kind: "midi-routing", config: { mode: "oneChannelPerInstance" } }]);
  expect(s.systems).toEqual([]);
});

test("projectKernelStructure: each system's pipeline mirrors its roles in order", () => {
  const a: RoleInstance[] = [
    { kind: "sameboy", config: { model: "cgbC" } },
    { kind: "lsdj-sync", config: { mode: "midiSync" } },
  ];
  const b: RoleInstance[] = [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }];
  const s = projectKernelStructure([view(1, a), view(2, b)], "sendToAll");

  expect(s.project).toEqual([{ kind: "midi-routing", config: { mode: "sendToAll" } }]);
  expect(s.systems).toEqual([
    { id: 1, pipeline: a }, // order preserved: system role first, then the feature role
    { id: 2, pipeline: b },
  ]);
});

// --- the controller role -------------------------------------------------------------------------
// Synthesized like midi-routing, and synthesized for a reason: its config carries a ~1000-number derived
// timing table, and a synthesized project role never reaches the saved `.rplg`.

const controller = (over: Partial<ControllerProjection> = {}): ControllerProjection => ({
  enabled: true, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: {}, songRowTicks: [], anchor: null, ...over,
});

test("projectKernelStructure: no controller role unless one is enabled", () => {
  expect(projectKernelStructure([], "sendToAll").project!.length).toBe(1);
  expect(projectKernelStructure([], "sendToAll", controller({ enabled: false })).project!.length).toBe(1);
});

test("projectKernelStructure: an enabled controller becomes a launchpad role carrying its song table", () => {
  const table = [[96, null], [96, null], [96, null], [96, null]];
  const s = projectKernelStructure([view(1, [])], "sendToAll", controller({
    target: "midiOut", systemId: 4, appConfig: { quantise: "beat" }, songRowTicks: table,
  }));

  expect(s.project!.length).toBe(2);
  expect(s.project![0].kind).toBe("midi-routing"); // routing still runs first
  expect(s.project![1]).toEqual({
    kind: "launchpad",
    config: {
      app: "lsdj-midimap", target: "midiOut", systemId: 4, appConfig: { quantise: "beat" }, songRowTicks: table,
      // No anchor: this cart has not been seen starting on its own, and on the hardware path it never will.
      anchorRows: [], anchorSeq: 0,
    },
  });
});

test("a start-edge anchor rides along with its sequence number", () => {
  const s = projectKernelStructure([view(1, [])], "sendToAll", controller({
    anchor: { rows: [5, 5, null, 5], seq: 3 },
  }));
  const cfg = s.project![1].config as Record<string, unknown>;
  expect(cfg.anchorRows).toEqual([5, 5, null, 5]);
  expect(cfg.anchorSeq).toBe(3);
});

test("the derived song table never reaches a saved project", () => {
  // The whole reason the role is synthesized rather than stored. A `.rplg` holds the user's CHOICES;
  // a thousand numbers re-derivable from the cart's own battery would be derived data in a config file,
  // and stale the moment the song changed.
  const settings = { ...DEFAULT_SETTINGS, controller: { enabled: true, app: "lsdj-midimap", target: "system" as const, systemId: 0, appConfig: { quantise: "bar" } } };
  const json = serializeConfig(buildConfig(settings, []), "", (p) => p);

  expect(json.includes("songRowTicks")).toBe(false);
  expect(json.includes("launchpad")).toBe(false);
  expect(parseConfig(json).settings.controller.enabled).toBe(true); // but the choice itself persists
  expect(parseConfig(json).settings.controller.appConfig).toEqual({ quantise: "bar" });
});

test("an older project with no controller key loads with it defaulted off, needing no migration", () => {
  const raw = JSON.stringify({ schemaVersion: K_PROJECT, settings: { layout: "auto", midiRouting: "sendToAll", audioRouting: "stereo", zoom: 0 }, systems: [] });
  expect(parseConfig(raw).settings.controller).toEqual({ enabled: false, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: {} });
});
