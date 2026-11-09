// The DAW slot projection reaches a cart's parameter lanes through the registry (docs/modular-graph-phase-f4a-design.md
// section 4.5): the tables themselves are their packages' (mgb/ts/test/parameters.test.ts and
// bliptoaster/ts/test/parameters.test.ts hold them to the pre-move expansion), and what stays here is the shell's half -
// a system's roles in, the claimed pool slots out.
import { test, expect } from "@retroplug/testing";
import { buildAppRegistry } from "../../src/appHost";
import { projectParameterMap } from "../../src/parameterMap";
import { MidiRouting } from "@retroplug/core/midiRouting";

const registry = buildAppRegistry();

test("the projection reaches a cart's lanes through the registry, slot for slot", () => {
  // The whole path a host sees: a system's roles in, the registry's tables, the slot pool out. The mGB system's first
  // lane is slot 0, and there are exactly as many claimed lanes as the role answers with under SendToAll.
  const lanes = registry.roleType("mgb")?.parameters?.({}) ?? [];
  const map = projectParameterMap([{ roles: [{ kind: "mgb" }] }], MidiRouting.SendToAll, registry);
  expect(map.length).toEqual(lanes.length);
  expect(map[0].slot).toEqual(0);
  const first = lanes[0];
  expect([map[0].channel, map[0].cc, map[0].name, map[0].shortName]).toEqual([
    first.channel,
    first.cc,
    first.name,
    first.shortName,
  ]);
  // A role whose type serves no parameters claims nothing, and the registry is what says so.
  expect(projectParameterMap([{ roles: [{ kind: "sameboy", config: {} }] }], MidiRouting.SendToAll, registry)).toEqual(
    [],
  );
});
