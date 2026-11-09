// A role's host latency is its own (docs/modular-graph-phase-f4a-design.md section 4.5): the plugin's
// `__rp_syncLatencyMs` takes the maximum over each system's PLACED roles, so the shell names no cart and no mode. LSDj's
// sync role is the one that declares any - the two modes that lock its playback to the host's clock run a fixed distance
// behind it - and the placement rule (a role the pipeline leaves out adds nothing) is pinned end to end by
// test-native/plugin-controlplane.test.ts.
import { test, expect } from "@retroplug/testing";
import { buildAppRegistry } from "../../src/appHost";
import { syncLatencyMs } from "../../src/syncLatency";
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

test("a hook's non-finite or negative answer is ignored, so the host is never handed a bad PDC", () => {
  // The answer is a package's, so the shell does not trust it: a NaN would ride Math.max into every later comparison and
  // reach the host as "NaN" PDC, an infinity as an infinite one, and a negative number is no delay at all. The role is
  // placed by its node, so the pipeline is what asks it, as for the maximum itself.
  const registry = buildAppRegistry();
  const roles = [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: {} }];
  const type = registry.roleType("lsdj-sync")!;
  for (const bad of [Number.NaN, Number.POSITIVE_INFINITY, -12]) {
    registry.registerRole({ ...type, hostLatencyMs: () => bad });
    expect(syncLatencyMs([{ roles }], registry)).toBe(0);
  }
  registry.registerRole({ ...type, hostLatencyMs: () => 33 });
  expect(syncLatencyMs([{ roles }], registry)).toBe(33);
});

test("a role that adds no latency declares no hook", () => {
  // The consoles' own roles, mGB's, risa's and smsggdj's sync roles: no host-locked playback, so nothing to compensate.
  for (const kind of ["sameboy", "mesen", "mgb", "risa-sync", "sms-sync"]) {
    expect(latencyOf(kind, { mode: "midiSync" })).toBe(undefined);
  }
});
