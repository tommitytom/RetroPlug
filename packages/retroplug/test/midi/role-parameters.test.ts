// The DAW parameter tables are their carts' now (docs/modular-graph-phase-f4a-design.md section 4.5): the `mgb` role
// serves mGB's lanes, the `bliptoaster` role serves the lanes of the build its `chip` config names, and the shell's slot
// projection asks the registry. This holds the move to the bytes: every lane the shell expanded before the move, for
// mGB and for each of BlipToaster's six builds, comes back lane for lane (test/midi/recordedParameters.ts, recorded from
// the design commit's parameterMap.ts), so no host sees a different parameter list than it did.
import { test, expect } from "@retroplug/testing";
import { buildAppRegistry } from "../../src/appHost";
import { projectParameterMap } from "../../src/parameterMap";
import { MidiRouting } from "@retroplug/core/midiRouting";
import type { RomParameter } from "@retroplug/core/romParameters";
import { RECORDED_PARAMETER_LANES } from "./recordedParameters";

const registry = buildAppRegistry();

/** The lanes a role kind serves for `config`, as the projection asks for them. */
const lanesOf = (kind: string, config: Record<string, unknown> = {}): readonly RomParameter[] | undefined =>
  registry.roleType(kind)?.parameters?.(config);

test("mGB's role serves exactly the lanes the shell's own table expanded", () => {
  expect(lanesOf("mgb")).toEqual(RECORDED_PARAMETER_LANES.mgb);
});

for (const chip of ["2a03", "vrc6", "vrc7", "s5b", "n163", "mmc5"]) {
  test(`BlipToaster's ${chip} build serves exactly the lanes the shell's own table expanded`, () => {
    expect(lanesOf("bliptoaster", { chip })).toEqual(RECORDED_PARAMETER_LANES[chip]);
  });
}

test("an unknown chip, or none, serves the 2A03 core's lanes", () => {
  // An older project (before the field) or a build newer than this table: the core set is never wrong for a build,
  // only narrower.
  expect(lanesOf("bliptoaster", { chip: "fictional" })).toEqual(RECORDED_PARAMETER_LANES["2a03"]);
  expect(lanesOf("bliptoaster")).toEqual(RECORDED_PARAMETER_LANES["2a03"]);
});

test("the projection reaches the same lanes through the registry, slot for slot", () => {
  // The whole path a host sees: a system's roles in, the registry's tables, the slot pool out. The mGB system's first
  // lane is slot 0, and there are exactly as many claimed lanes as the table has under SendToAll.
  const map = projectParameterMap([{ roles: [{ kind: "mgb" }] }], MidiRouting.SendToAll, registry);
  expect(map.length).toEqual(RECORDED_PARAMETER_LANES.mgb.length);
  expect(map[0].slot).toEqual(0);
  const first = RECORDED_PARAMETER_LANES.mgb[0];
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
