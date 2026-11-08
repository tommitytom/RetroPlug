// The MI.MAP app's control-plane half (ts/app.ts): its id, its menu label and the schema its config is parsed with - plus
// the sync mode it needs the cart its launches drive to be in, which is DATA in the package's manifest
// (protocol.apps[].syncOverride) because the planner applies it and must carry no app and no zod. It reuses
// RoleConfigSchema for the knobs, so the interesting assertions are the ones about tolerance: a stale or garbage config
// must snap to something sane rather than fail the parse, because these values come off disk and a user should never be
// locked out of their own controller by a bad enum.
import { test, expect } from "@retroplug/testing";
import contribution from "../app";
import { MAX_PAGE, QUANTISE_VALUES } from "../lsdjMidiMap";
import runtime from "../appRuntime";

const apps = contribution.controllerApps ?? [];
const miMap = apps.find((app) => app.id === "lsdj-midimap")!;

test("the app is contributed under the id a project stores, with the menu's label", () => {
  expect(apps.map((app) => app.id)).toEqual(["lsdj-midimap"]);
  expect(miMap.label).toBe("LSDj MI.MAP");
  expect(typeof miMap.config.parse).toBe("function");
});

test("the schema fills the documented defaults", () => {
  expect(miMap.config.parse({})).toEqual({ quantise: "bar", follow: true, page: 0 });
});

test("a garbage config snaps to sane values instead of failing to load", () => {
  expect(miMap.config.parse({ quantise: "sometime", follow: "yes", page: 99 }))
    .toEqual({ quantise: "bar", follow: true, page: MAX_PAGE });
  expect(miMap.config.parse({ page: -5 })).toEqual({ quantise: "bar", follow: true, page: 0 });
});

test("valid values are preserved", () => {
  expect(miMap.config.parse({ quantise: "rowEnd", follow: false, page: 3 }))
    .toEqual({ quantise: "rowEnd", follow: false, page: 3 });
});

test("every quantise mode the menu offers is one the schema accepts", () => {
  for (const quantise of QUANTISE_VALUES) expect(miMap.config.parse({ quantise }).quantise).toBe(quantise);
});

test("the two halves are joined by id", () => {
  // The generated script bundle registers the runtime and the app's registry holds this half: a document names one id,
  // so the two must name the same app.
  expect(runtime.id).toBe(miMap.id);
});
