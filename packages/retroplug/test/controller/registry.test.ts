// The controller-app registry, and the config contract it owns.
//
// It mirrors RoleRegistry deliberately, down to reusing RoleConfigSchema - so the interesting assertions
// are the ones about tolerance: a stale or garbage config must snap to something sane rather than fail the
// parse, because these values come off disk and a user should never be locked out of their own controller
// by a bad enum.
import { test, expect } from "../../testing/harness";
import { ControllerRegistry, registerControllerApps, lsdjMidiMap, MAX_PAGE } from "../../src/controller";

const registry = (): ControllerRegistry => {
  const r = new ControllerRegistry();
  registerControllerApps(r);
  return r;
};

test("the built-in app is registered under a stable id", () => {
  const r = registry();
  const app = r.get("lsdj-midimap")!;
  expect(app.label).toBe("LSDj MI.MAP");
  expect(app.app).toBe(lsdjMidiMap);
  expect(r.list().length).toBe(1);
});

test("an unknown id returns undefined rather than throwing", () => {
  const r = registry();
  expect(r.get("nope")).toBe(undefined);
  expect(r.defaultConfig("nope")).toBe(undefined);
});

test("defaultConfig fills the documented defaults", () => {
  expect(registry().defaultConfig("lsdj-midimap")).toEqual({ quantise: "bar", follow: true, page: 0 });
});

test("a garbage config snaps to sane values instead of failing to load", () => {
  const schema = registry().get("lsdj-midimap")!.schema;
  expect(schema.parse({ quantise: "sometime", follow: "yes", page: 99 }))
    .toEqual({ quantise: "bar", follow: true, page: MAX_PAGE });
  expect(schema.parse({ page: -5 })).toEqual({ quantise: "bar", follow: true, page: 0 });
});

test("valid values are preserved", () => {
  const schema = registry().get("lsdj-midimap")!.schema;
  expect(schema.parse({ quantise: "rowEnd", follow: false, page: 3 }))
    .toEqual({ quantise: "rowEnd", follow: false, page: 3 });
});

test("registering the same id twice replaces it, so an extension can override a built-in", () => {
  const r = registry();
  const replacement = { id: "lsdj-midimap", label: "Mine", schema: { parse: () => ({}) }, app: () => {} };
  r.register(replacement);
  expect(r.get("lsdj-midimap")!.label).toBe("Mine");
  expect(r.list().length).toBe(1);
});
