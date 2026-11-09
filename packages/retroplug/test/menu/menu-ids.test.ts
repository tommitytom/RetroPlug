// The menu's one-id rule (docs/modular-graph-phase-f4b-design.md section 4.4): the menu is keyed by id, so a
// contributed builder that emits one of the shell's own ids (a footer builder emitting `inst-exit`) would silently take
// that row's place. The composers assert it over the tree they built, which is the only place both halves are known.
import { test, expect } from "@retroplug/testing";
import { action, submenu } from "@retroplug/ui/menu";
import { assertUniqueMenuIds } from "../../ui/host/menuContributions";

test("two rows of one id in one menu are refused, naming the id and the menu", () => {
  expect(() => assertUniqueMenuIds([action("a", "A", () => {}), action("a", "B", () => {})], "a menu"))
    .toThrow("two menu rows share the id a in a menu");
});

test("a nested row of an id its menu already has is refused too", () => {
  const tree = [submenu("outer", "Outer", [action("inst-exit", "Exit", () => {})]), action("inst-exit", "Exit", () => {})];
  expect(() => assertUniqueMenuIds(tree, "the instance menu"))
    .toThrow("two menu rows share the id inst-exit in the instance menu");
});
