// The menu's one-id rule (docs/modular-graph-phase-f4b-design.md section 4.4): the menu is keyed by id, so a
// contributed builder that emits one of the shell's own ids (a footer builder emitting `inst-exit`) would silently take
// that row's place. The composers run `firstWinsMenuIds` over the tree they built, which is the only place both halves
// are known: the first row is kept and the later one is REPORTED, because a duplicate (a hand-edited asset override can
// make one) must not take App's render down. `assertUniqueMenuIds` is the same rule as an assertion, for the
// generator's checks and a test.
import { test, expect } from "@retroplug/testing";
import { action, submenu } from "@retroplug/ui/menu";
import { assertUniqueMenuIds, firstWinsMenuIds } from "../../ui/host/menuContributions";

test("two rows of one id in one menu are refused, naming the id and the menu", () => {
  expect(() => assertUniqueMenuIds([action("a", "A", () => {}), action("a", "B", () => {})], "a menu"))
    .toThrow("two menu rows share the id a in a menu");
});

test("a nested row of an id its menu already has is refused too", () => {
  const tree = [submenu("outer", "Outer", [action("inst-exit", "Exit", () => {})]), action("inst-exit", "Exit", () => {})];
  expect(() => assertUniqueMenuIds(tree, "the instance menu"))
    .toThrow("two menu rows share the id inst-exit in the instance menu");
});

test("at runtime the first row of one id is kept and the later one is reported, never thrown", () => {
  // A hand-edited asset override can make a contributed builder emit an id the tree already has. App's render must
  // survive that: the row the first half built stays, the second is dropped, and the fact is said on the console.
  const warnings: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => void warnings.push(args.map(String).join(" "));
  let tree: unknown;
  try {
    tree = firstWinsMenuIds(
      [action("a", "First", () => {}), action("a", "Second", () => {}), action("b", "Other", () => {})],
      "a menu",
    );
  } finally {
    console.warn = original;
  }
  expect((tree as { id: string; label: string }[]).map((row) => [row.id, row.label]))
    .toEqual([["a", "First"], ["b", "Other"]]);
  expect(warnings.length).toBe(1);
  expect(warnings[0].includes("a menu") && warnings[0].includes(" a "), warnings[0]).toBe(true);
});

test("a nested duplicate is dropped the same way, and the rest of its submenu survives", () => {
  const warnings: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => void warnings.push(args.map(String).join(" "));
  let tree: unknown;
  try {
    tree = firstWinsMenuIds(
      [submenu("outer", "Outer", [action("inst-exit", "Inner", () => {}), action("keep", "Keep", () => {})]),
        action("inst-exit", "Outer Exit", () => {})],
      "the instance menu",
    );
  } finally {
    console.warn = original;
  }
  expect(warnings.length).toBe(1);
  const children = (tree as { children: { id: string; label: string }[] }[])[0].children;
  expect(children.map((row) => [row.id, row.label])).toEqual([["inst-exit", "Inner"], ["keep", "Keep"]]);
});
