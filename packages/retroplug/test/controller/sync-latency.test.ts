// A role's host latency is its own (docs/modular-graph-phase-f4a-design.md section 4.5): the plugin's
// `__rp_syncLatencyMs` takes the maximum over each system's PLACED roles, so the shell names no cart and no mode. LSDj's
// sync role is the one that declares any - the two modes that lock its playback to the host's clock run a fixed distance
// behind it - and the placement rule (a role the pipeline leaves out adds nothing) is pinned end to end by
// test-native/plugin-controlplane.test.ts.
import { test, expect } from "@retroplug/testing";
import { buildAppRegistry } from "../../src/appHost";
import { LSDJ_MODE_VALUES } from "@retroplug/lsdj/enums";

const registry = buildAppRegistry();
const latencyOf = (kind: string, config: Record<string, unknown>): number | undefined =>
  registry.roleType(kind)?.hostLatencyMs?.(config);

test("the two host-clocked LSDj modes add the cart's 33 ms, and every other mode adds none", () => {
  for (const mode of LSDJ_MODE_VALUES) {
    const hostClocked = mode === "midiSync" || mode === "midiSyncArduinoboy";
    expect(latencyOf("lsdj-sync", { mode })).toBe(hostClocked ? 33 : 0);
  }
});

test("a role that adds no latency declares no hook", () => {
  // The consoles' own roles, mGB's, risa's and smsggdj's sync roles: no host-locked playback, so nothing to compensate.
  for (const kind of ["sameboy", "mesen", "mgb", "risa-sync", "sms-sync"]) {
    expect(latencyOf(kind, { mode: "midiSync" })).toBe(undefined);
  }
});
