// A model's app config is parsed by the product's own placer, so the registry that placer's app table holds must be
// filled wherever a model can reach one (docs/modular-graph-phase-f5a-design.md section 6.5). The fill is a load-time
// side effect of the generated registry, and this module - the publisher that builds systems without stores - is a
// caller that never parses a project, so it imports the generated registry itself rather than trusting whichever of its
// own imports happens to reach a tracker or a kit format first. Without that, `normalize` meets an app the empty table
// does not know and answers `{}`: every knob silently back to the app's defaults.
import { test, expect } from "@retroplug/testing";
import { directGraph } from "../../src/graph/directGraph";
import { PLACERS } from "../../src/generated/placers";

test("the model publisher parses a model's app config, so the registry is filled by the time it loads", () => {
  // The publisher is imported for its side effect: it is not called (a DirectGraph needs a native host's RPC).
  expect(typeof directGraph).toBe("function");
  const placer = PLACERS.find((entry) => entry.placer.key === "controller")?.placer;
  expect(placer !== undefined).toBe(true);
  // A user's own value survives; the knobs it does not name fill from the app's schema, which is the table's.
  const normalized = placer!.normalize!({
    app: "lsdj-midimap",
    target: "system",
    systemId: 1,
    appConfig: { quantise: "immediate" },
  }) as { appConfig: Record<string, unknown> };
  expect(normalized.appConfig).toEqual({ quantise: "immediate", follow: true, page: 0 });
});
