// The v2 to v3 step rewrites integer enum values to their string spellings, and it must go on doing that from the lists
// the v2 writers had, not from today's packages: a rename in a cart's enum would otherwise migrate an old project to a
// DEFAULT. This asserts every frozen value is still one the owning package's schema accepts, and runs the step for
// every ordinal so the file's own integer still means the value its writer meant. An append or a reorder in a package
// passes (the frozen ordinals have not moved); a rename fails here, which is the case the freeze exists to catch.
import { test, expect } from "@retroplug/testing";
import { parseConfig, V3_ROLE_ENUMS } from "../../src/projectConfig";
import { MODEL_VALUES, HIGHPASS_VALUES } from "@retroplug/sameboy/enums";
import { REGION_VALUES, CHANNEL_EXPORT_VALUES } from "@retroplug/mesen/enums";
import { LSDJ_MODE_VALUES } from "@retroplug/lsdj/enums";

/** A v2 file carrying one role, so the step's role-config half runs for its ordinal fields. */
function v2(platform: string, core: string, kind: string, config: Record<string, unknown>): string {
  return JSON.stringify({
    schemaVersion: "2",
    settings: {},
    systems: [{ platform, core, romPath: "/rom", roles: [{ kind, config }] }],
  });
}

const CASES: { platform: string; core: string; kind: string; field: string; current: readonly string[] }[] = [
  { platform: "gb", core: "sameboy", kind: "sameboy", field: "model", current: MODEL_VALUES },
  { platform: "gb", core: "sameboy", kind: "sameboy", field: "highpass", current: HIGHPASS_VALUES },
  { platform: "nes", core: "mesen", kind: "mesen", field: "region", current: REGION_VALUES },
  { platform: "nes", core: "mesen", kind: "mesen", field: "channelExportMode", current: CHANNEL_EXPORT_VALUES },
  { platform: "gb", core: "sameboy", kind: "lsdj-sync", field: "mode", current: LSDJ_MODE_VALUES },
];

for (const { platform, core, kind, field, current } of CASES) {
  // The frozen values the package no longer accepts: a renamed member leaves its old spelling here, and the step would
  // write a value the schema does not know.
  test(`every frozen ${kind}.${field} value is still one the package accepts`, () => {
    const frozen = V3_ROLE_ENUMS[kind][field];
    expect(frozen.filter((value) => !current.includes(value))).toEqual([]);
  });

  test(`v2→v3 maps every ${kind}.${field} ordinal to the value the v2 writer had`, () => {
    for (const [index, value] of V3_ROLE_ENUMS[kind][field].entries()) {
      const cfg = parseConfig(v2(platform, core, kind, { [field]: index }));
      const role = cfg.systems[0]?.roles?.find((r) => r.kind === kind);
      expect(role?.config[field], `${kind}.${field} ordinal ${index}`).toBe(value);
    }
  });
}
