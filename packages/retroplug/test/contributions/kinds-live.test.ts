// The live kind tables (docs/downloadable-packs-m2f-plan.md): the generated modules seed the declared kinds, the taken
// item ids and the items' data; a commit adds a pack's; the readers see it after the commit; and a commit the checks
// refuse changes nothing.
import { test, expect } from "@retroplug/testing";
import "../../src/generated/packages";
import { commitContributions } from "../../src/contributions/registry";
import { contributionKindsRegistry, declaredKind, kindData, kindDataRegistry, kindItemsRegistry } from "../../src/contributions/kindRegistries";

const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};

test("the seed is the product's kinds, items and data", () => {
  const k = declaredKind("controllerApps");
  expect(k?.pkg).toBe("launchpad");
  expect(k?.data).toEqual(["syncOverride"]);
  expect(typeof k?.contribution.accept).toBe("function");
  expect(kindItemsRegistry.items().some((i) => i.kind === "controllerApps" && i.id === "lsdj-midimap")).toBe(true);
  expect(Object.keys(kindData("controllerApps"))).toEqual(["lsdj-midimap"]);
  expect(kindData("nothing")).toEqual({});
});

test("a pack's item id and data join in one commit, and the readers see them", () => {
  commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-a", items: [{ kind: "controllerApps", id: "kl-a-app" }] },
    { registry: kindDataRegistry, pkg: "kl-a", items: [{ kind: "controllerApps", id: "kl-a-app", data: { syncOverride: { node: "x" } } }] },
  ]);
  expect(kindData("controllerApps")["kl-a-app"]).toEqual({ syncOverride: { node: "x" } });
});

test("a taken id, an item of an undeclared kind, or data with no item is refused, and nothing changes", () => {
  const items = kindItemsRegistry.items();
  const data = kindDataRegistry.items();
  expect(refusal(() => commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-b", items: [{ kind: "controllerApps", id: "lsdj-midimap" }] },
  ]))).toBe("two packages contribute the controllerApps item lsdj-midimap: lsdj-launchpad and kl-b");
  expect(refusal(() => commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-c", items: [{ kind: "nope", id: "x" }] },
  ]))).toBe("kl-c contributes the nope item x, and no package declares the contribution kind nope");
  expect(refusal(() => commitContributions([
    { registry: kindDataRegistry, pkg: "kl-d", items: [{ kind: "controllerApps", id: "kl-d-app", data: {} }] },
  ]))).toBe("kl-d declares data for the controllerApps item kl-d-app, which no package contributes");
  expect(kindItemsRegistry.items()).toBe(items);
  expect(kindDataRegistry.items()).toBe(data);
  expect(contributionKindsRegistry.items().length).toBe(1);
});
