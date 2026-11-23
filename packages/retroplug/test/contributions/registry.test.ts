// The live registries' primitive (docs/downloadable-packs-m2a-plan.md, Task 1): append-only entries, a commit staged
// across registries and checked whole, one counter and one notification per commit, and a refused commit leaving
// every registry as it was. The registries here are this file's own: each test file runs in a process of its own, so
// the names cannot meet the app's.
import { test, expect } from "@retroplug/testing";
import {
  Registry,
  commitByName,
  commitContributions,
  contributionsVersion,
  derived,
  subscribeContributions,
} from "../../src/contributions/registry";

const fruit = new Registry<string>("test.fruit", (entries) => {
  const seen = new Set<string>();
  for (const { pkg, item } of entries) {
    if (seen.has(item)) throw new Error(`the package ${pkg} adds the fruit ${item}, which is already registered`);
    seen.add(item);
  }
});
const baskets = new Registry<{ fruit: string }>("test.baskets", (entries, view) => {
  const known = new Set(view(fruit).map((entry) => entry.item));
  for (const { pkg, item } of entries) {
    if (!known.has(item.fruit)) throw new Error(`the package ${pkg} fills a basket with ${item.fruit}, which no package registers`);
  }
});

test("a registry starts empty and unseeded", () => {
  const empty = new Registry<number>("test.empty");
  expect(empty.entries()).toEqual([]);
  expect(empty.items()).toEqual([]);
  expect(empty.seeded).toBe(false);
});

test("a commit appends in order, with each item's package, and seeds what it touches", () => {
  commitContributions([
    { registry: fruit, pkg: "alpha", items: ["apple", "pear"] },
    { registry: fruit, pkg: "beta", items: ["plum"] },
  ]);
  expect(fruit.items()).toEqual(["apple", "pear", "plum"]);
  expect(fruit.entries().map((entry) => entry.pkg)).toEqual(["alpha", "alpha", "beta"]);
  expect(fruit.seeded).toBe(true);
  expect(baskets.seeded).toBe(false);
});

test("an addition with no items still seeds its registry", () => {
  commitContributions([{ registry: baskets, pkg: "alpha", items: [] }]);
  expect(baskets.seeded).toBe(true);
  expect(baskets.items()).toEqual([]);
});

test("the arrays a registry hands out are frozen and keep their identity until the next commit", () => {
  const before = fruit.items();
  expect(Object.isFrozen(before)).toBe(true);
  expect(Object.isFrozen(fruit.entries())).toBe(true);
  expect(fruit.items()).toBe(before);
  commitContributions([{ registry: fruit, pkg: "gamma", items: ["fig"] }]);
  expect(fruit.items() === before).toBe(false);
  expect(before).toEqual(["apple", "pear", "plum"]);
});

test("a check reads the other registries as the commit would leave them", () => {
  // The basket names a fruit added in the same commit: the staged view, not the committed one, is what is checked.
  commitContributions([
    { registry: fruit, pkg: "delta", items: ["quince"] },
    { registry: baskets, pkg: "delta", items: [{ fruit: "quince" }] },
  ]);
  expect(baskets.items().map((b) => b.fruit)).toEqual(["quince"]);
});

test("a refused commit changes no registry, bumps no counter and notifies no one", () => {
  const fruitBefore = fruit.items();
  const basketsBefore = baskets.items();
  const version = contributionsVersion();
  let calls = 0;
  const off = subscribeContributions(() => calls++);
  expect(() =>
    commitContributions([
      { registry: fruit, pkg: "eps", items: ["kiwi"] },
      { registry: baskets, pkg: "eps", items: [{ fruit: "durian" }] },
    ]),
  ).toThrow("the package eps fills a basket with durian, which no package registers");
  expect(fruit.items()).toBe(fruitBefore);
  expect(baskets.items()).toBe(basketsBefore);
  expect(contributionsVersion()).toBe(version);
  expect(calls).toBe(0);
  off();
});

test("a check on a registry the commit did not touch still runs", () => {
  const noDurian = new Registry<string>("test.noDurian", (_entries, view) => {
    if (view(fruit).some((entry) => entry.item === "durian")) throw new Error("a fruit list may not carry durian");
  });
  commitContributions([{ registry: noDurian, pkg: "zeta", items: [] }]);
  expect(() => commitContributions([{ registry: fruit, pkg: "zeta", items: ["durian"] }])).toThrow(
    "a fruit list may not carry durian",
  );
});

test("a check sees its own registry's committed and staged entries whole", () => {
  // The duplicate is caught across the registry's own committed entries (apple) and the entries this commit stages.
  expect(() =>
    commitContributions([
      { registry: fruit, pkg: "zeta", items: ["apple"] },
      { registry: baskets, pkg: "zeta", items: [{ fruit: "apple" }] },
    ]),
  ).toThrow("the package zeta adds the fruit apple, which is already registered");
});

test("an unseeded registry reads as empty and its check does not run", () => {
  const strict = new Registry<string>("test.strict", () => {
    throw new Error("an unseeded registry's check ran");
  });
  const reader = new Registry<string>("test.reader", (_entries, view) => {
    if (view(strict).length !== 0) throw new Error("an unseeded registry read as non-empty");
  });
  commitContributions([{ registry: reader, pkg: "alpha", items: ["x"] }]);
  expect(reader.items()).toEqual(["x"]);
  expect(strict.seeded).toBe(false);
});

test("a commit bumps the counter once and notifies every listener once", () => {
  const version = contributionsVersion();
  const seen: number[] = [];
  const off = subscribeContributions(() => seen.push(contributionsVersion()));
  commitContributions([
    { registry: fruit, pkg: "eta", items: ["lime"] },
    { registry: fruit, pkg: "theta", items: ["date"] },
  ]);
  expect(contributionsVersion()).toBe(version + 1);
  expect(seen).toEqual([version + 1]);
  off();
  commitContributions([{ registry: fruit, pkg: "iota", items: ["sloe"] }]);
  expect(seen).toEqual([version + 1]);
});

test("a listener that throws does not stop the others or undo the commit", () => {
  const warned: string[] = [];
  const realWarn = console.warn;
  console.warn = (...args: unknown[]) => warned.push(args.map(String).join(" "));
  let second = 0;
  const offA = subscribeContributions(() => {
    throw new Error("a broken listener");
  });
  const offB = subscribeContributions(() => second++);
  try {
    commitContributions([{ registry: fruit, pkg: "kappa", items: ["yuzu"] }]);
  } finally {
    console.warn = realWarn;
    offA();
    offB();
  }
  expect(second).toBe(1);
  expect(fruit.items().includes("yuzu")).toBe(true);
  expect(warned.some((line) => line.includes("a broken listener"))).toBe(true);
});

test("derived rebuilds only after a commit", () => {
  let builds = 0;
  const count = derived(() => {
    builds++;
    return fruit.items().length;
  });
  const first = count();
  expect(count()).toBe(first);
  expect(builds).toBe(1);
  commitContributions([{ registry: fruit, pkg: "lambda", items: ["sorb"] }]);
  expect(count()).toBe(first + 1);
  expect(builds).toBe(2);
});

test("commitByName resolves registries by their names, and refuses an unknown one naming the known", () => {
  commitByName([{ registry: "test.fruit", pkg: "mu", items: ["medlar"] }]);
  expect(fruit.items().includes("medlar")).toBe(true);
  expect(() => commitByName([{ registry: "test.nuts", pkg: "mu", items: ["pecan"] }])).toThrow(
    "no registry is named test.nuts in this bundle",
  );
});

test("two registries may not share a name", () => {
  expect(() => new Registry<string>("test.fruit")).toThrow("two registries are named test.fruit");
});

test("an addition's items are typed by their registry", () => {
  const numbers = new Registry<number>("test.numbers");
  // @ts-expect-error a string is not a number: the seed's items are checked against the registry's element type
  const bad = () => commitContributions([{ registry: numbers, pkg: "nu", items: ["bad"] }]);
  void bad;
  commitContributions([{ registry: numbers, pkg: "nu", items: [1, 2] }]);
  expect(numbers.items()).toEqual([1, 2]);
});
