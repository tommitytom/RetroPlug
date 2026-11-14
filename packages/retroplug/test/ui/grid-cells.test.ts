// gridCells (ui/screens/grid/layout.ts): the grid's cells are each system's tile, then the side views open beside it,
// in the order given. Every size and hit test counts these, so a panel takes a grid place exactly as a system does.
import { test, expect } from "@retroplug/testing";
import { gridCells, hitTestTile, SystemLayout } from "../../ui/screens/grid/layout";

const cellsOf = (open: Record<number, string[]>) =>
  gridCells([{ id: 7 }, { id: 9 }, { id: 4 }], (sys) => open[sys.id] ?? []).map((cell) =>
    cell.kind === "system" ? `sys ${cell.system.id} @${cell.index}` : `${cell.view} beside ${cell.system.id} @${cell.index}`,
  );

test("no panel open: one cell a system, in slot order", () => {
  expect(cellsOf({})).toEqual(["sys 7 @0", "sys 9 @1", "sys 4 @2"]);
});

test("a panel follows its own system's tile, and keeps that system's index", () => {
  expect(cellsOf({ 9: ["files"] })).toEqual(["sys 7 @0", "sys 9 @1", "files beside 9 @1", "sys 4 @2"]);
  expect(cellsOf({ 7: ["a", "b"], 4: ["a"] })).toEqual([
    "sys 7 @0", "a beside 7 @0", "b beside 7 @0", "sys 9 @1", "sys 4 @2", "a beside 4 @2",
  ]);
});

test("one system and its panel make a two-column grid, and the panel is the right-hand cell", () => {
  const cells = gridCells([{ id: 1 }], () => ["files"]);
  const win = { width: 960, height: 432 };
  expect(hitTestTile(100, 100, cells.length, SystemLayout.Auto, 3, 3, win)).toBe(0);
  const right = hitTestTile(700, 100, cells.length, SystemLayout.Auto, 3, 3, win);
  expect(right).toBe(1);
  expect(cells[right!].kind).toBe("side");
});
