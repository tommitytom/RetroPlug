// The launchpad package's two tables (ts/registry.ts and ts/apps/index.ts): the control plane's app registry, and the
// runtime registry the generated script bundle fills. Each is looked up by id, so a re-registration replaces rather
// than duplicating, and an unknown id - an Object.prototype name among them - is nothing.
import { test, expect } from "@retroplug/testing";
import type { ConfigRow } from "@retroplug/ui/contribution";
import { ControllerRegistry, type ControllerAppContribution, type ControllerAppField } from "../registry";
import { controllerApp, controllerApps, registerControllerApps, type ControllerAppRuntime } from "../apps";

const entry = (id: string, label = id): ControllerAppContribution => ({
  id,
  label,
  config: { parse: (v: unknown) => (v ?? {}) as Record<string, unknown> },
});

const runtimeOf = (id: string): ControllerAppRuntime => ({
  id,
  factory: () => () => {},
  playbackModel: () => ({
    channelCount: 4,
    position: () => ({ playing: false, channels: [] }),
    grid: () => ({ rowCount: 1, channelCount: 4, hasContent: () => false }),
    setRowTicks: () => {},
    anchorTo: () => {},
    launch: () => {},
    advance: () => {},
    stop: () => {},
    reset: () => {},
  }),
  target: () => ({ maxRow: 1, launch: () => {}, release: () => {} }),
});

test("the control-plane registry registers by id, in order, and replaces in place", () => {
  const r = new ControllerRegistry();
  r.register(entry("a"));
  r.register(entry("b"));
  expect(r.list().map((app) => app.id)).toEqual(["a", "b"]);
  r.register(entry("a", "Mine"));
  expect(r.get("a")!.label).toBe("Mine");
  expect(r.list().map((app) => app.id)).toEqual(["a", "b"]); // its position holds
});

test("an unknown id returns undefined rather than throwing, an Object.prototype name included", () => {
  const r = new ControllerRegistry();
  for (const id of ["nope", "toString", "__proto__", ""]) {
    expect(r.get(id)).toBe(undefined);
    expect(r.defaultConfig(id)).toBe(undefined);
  }
});

test("defaultConfig parses the app's schema with nothing given", () => {
  const r = new ControllerRegistry();
  r.register({ ...entry("a"), config: { parse: () => ({ filled: true }) } });
  expect(r.defaultConfig("a")).toEqual({ filled: true });
});

test("the runtime registry registers by id, and the generated bundle fills it", () => {
  registerControllerApps([runtimeOf("one")]);
  expect(controllerApp("one")!.id).toBe("one");
  expect(controllerApps().map((app) => app.id).includes("one")).toBe(true);
  registerControllerApps([runtimeOf("two")]);
  registerControllerApps([runtimeOf("one")]); // a reload replaces, keeping the order
  const ids = controllerApps().map((app) => app.id);
  expect(ids.indexOf("one")).toBeLessThan(ids.indexOf("two"));
  expect(ids.filter((id) => id === "one").length).toBe(1);
});

test("the runtime registry knows no app it was not handed", () => {
  for (const id of ["nope", "toString", "__proto__", ""]) expect(controllerApp(id)).toBe(undefined);
});

// A controller app's knob IS a config row: the registry aliases the menu's row to the UI contract's (registry.ts),
// so the menu renders both with one cycler. Held both ways, so a hand-written copy that grew (or lost) a member
// would fail the assignment instead of rendering a row the kit cannot draw.
const asConfigRow = (field: ControllerAppField): ConfigRow => field;
const asAppField = (row: ConfigRow): ControllerAppField => row;

test("a controller app's field and a config row are the same shape", () => {
  expect(typeof asConfigRow).toBe("function");
  expect(typeof asAppField).toBe("function");
});
