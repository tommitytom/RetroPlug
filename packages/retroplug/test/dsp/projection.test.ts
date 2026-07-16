// projectKernelStructure turns the store's live systems (SystemView[]) into the DSP kernel's
// structure: a synthesized project-scope midi-routing role + one pipeline per system mirroring
// its roles in order. Pure — no backend, no registry — so it's tested against plain views.
import { test, expect } from "../../testing/harness";
import { projectKernelStructure } from "../../src/kernelProjection";
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
