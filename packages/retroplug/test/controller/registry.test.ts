// The controller-app registry, and the config contract it owns.
//
// It mirrors RoleRegistry deliberately, down to reusing RoleConfigSchema - so the interesting assertions
// are the ones about tolerance: a stale or garbage config must snap to something sane rather than fail the
// parse, because these values come off disk and a user should never be locked out of their own controller
// by a bad enum.
import { test, expect } from "../../testing/harness";
import { APP_CONFIGS } from "../../src/controller/registry";
import { ControllerRegistry, registerControllerApps, lsdjMidiMap, MAX_PAGE, CONTROLLER_APPS, controllerApp } from "../../src/controller";

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

test("the registry is the apps' runtime, each with its label and schema, in the runtime's order", () => {
  // The audio side reads the runtime (controller/apps/index.ts) alone, without the schemas, so the two must name the
  // same apps with the same functions.
  expect(registry().list().map((t) => t.id)).toEqual(CONTROLLER_APPS.map((a) => a.id));
  for (const { id, app } of CONTROLLER_APPS) {
    expect(registry().get(id)!.app, id).toBe(app);
    expect(controllerApp(id), id).toBe(app);
  }
  expect(controllerApp("lsdj-midimap")).toBe(lsdjMidiMap);
});

test("the runtime knows no app it does not list, an Object.prototype name included", () => {
  expect(controllerApp("nope")).toBe(undefined);
  expect(controllerApp("toString")).toBe(undefined);
  expect(controllerApp("")).toBe(undefined);
});

test("every config entry names a runtime, and every runtime has a config", () => {
  const runtimeIds = CONTROLLER_APPS.map((a) => a.id);
  // registerControllerApps walks the runtimes, so a config no runtime names would be dropped without a word.
  expect(Object.keys(APP_CONFIGS).sort()).toEqual(runtimeIds.slice().sort());
});

test("a runtime with no label and config is refused when the apps are registered, not half-registered", () => {
  const orphan = { id: "no-config", app: lsdjMidiMap };
  const r = new ControllerRegistry();
  expect(() => registerControllerApps(r, [...CONTROLLER_APPS, orphan])).toThrow(`controller app "no-config" has no label or config schema`);
  // The apps before it registered; none is reachable under the refused id.
  expect(r.get("lsdj-midimap") !== undefined).toBe(true);
  expect(r.get("no-config")).toBe(undefined);
});
