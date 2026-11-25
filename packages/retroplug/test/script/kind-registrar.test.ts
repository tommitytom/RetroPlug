// The built-in script bundle's kind registrar (docs/downloadable-packs-m2f-plan.md): a pack's kinds half registers its
// items' runtimes through it, after the bundle has registered the product's own. It refuses a kind the product does not
// declare, items that are not the ids it was given, and an id the kind already holds, so a pack never shadows a
// built-in app.
import { test, expect } from "@retroplug/testing";
// The production bundle's entry point: importing it registers the product's apps and defines the registrar.
import "../../src/script/bundle";
import { controllerApp } from "@retroplug/launchpad/apps";

type Registrar = (kind: string, items: readonly { id: string }[], ids: readonly string[]) => void;
const register = (): Registrar => (globalThis as Record<string, unknown>).__rp_registerKindItems as Registrar;
const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};
const runtime = (id: string) => ({ id, factory: () => () => {}, playbackModel: () => ({}) as never, target: () => ({}) as never });

test("the registrar is defined, and a pack's app is found by the session's lookup after it", () => {
  expect(typeof register()).toBe("function");
  register()("controllerApps", [runtime("kr-app")], ["kr-app"]);
  expect(controllerApp("kr-app")?.id).toBe("kr-app");
});

test("it refuses an undeclared kind, ids that are not the items', and a taken id", () => {
  expect(refusal(() => register()("nope", [runtime("x")], ["x"]))).toBe(
    "a pack's kinds half registers items of nope, a contribution kind this product does not declare");
  expect(refusal(() => register()("controllerApps", [runtime("kr-b")], ["kr-c"]))).toBe(
    "a pack's kinds half registers the controllerApps items kr-b, and its manifest declares kr-c");
  expect(refusal(() => register()("controllerApps", [runtime("lsdj-midimap")], ["lsdj-midimap"]))).toBe(
    "a pack's kinds half registers the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds");
  expect(refusal(() => register()("controllerApps", [runtime("kr-app")], ["kr-app"]))).toBe(
    "a pack's kinds half registers the controllerApps item kr-app, which an earlier pack holds");
});
