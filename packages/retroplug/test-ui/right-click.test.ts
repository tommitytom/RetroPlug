// Right-clicking an instance opens its menu, anchored to the clicked tile. The native editor emits the
// "mouse" bus (button, press, x, y); App's handler acts on the right button only, hit-tests the tile, focuses
// it, and opens its instance menu (the Menu replaces that tile's slot in SystemGrid). Drives it through the
// harness's ui.rightClick, which emits the right-button bus event at (x,y).

import { test, expect, ui, navTo, type WidgetInfo, Key } from "ui-harness";

const center = (w: WidgetInfo): [number, number] => [w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2)];
const rightClick = (w: WidgetInfo): void => ui.rightClick(...center(w));
const leftClick = (w: WidgetInfo): void => ui.clickAt(...center(w));

test("right-clicking an instance opens its menu, anchored to the clicked tile", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Single instance, grid showing (no menu). A right-click opens the instance menu — whose title carries
  // "RetroPlug", which the bare grid shows nowhere.
  expect(ui.findByTextContaining("RetroPlug")).toBe(null);
  rightClick(ui.findByTestId("tile-0")!);
  ui.pump(10);
  expect(ui.findByTextContaining("RetroPlug") != null).toBeTruthy();

  // Duplicate from that menu → two tiles; the menu closes. Left-click tile-0 to focus it.
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  leftClick(ui.findByTestId("tile-0")!);
  ui.pump(10);
  expect(ui.findByTextContaining("RetroPlug")).toBe(null); // menu closed
  expect(ui.findByTestId("dim-0")).toBe(null); // tile 0 focused
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();

  // Right-click the OTHER (unfocused) tile → its menu opens THERE: the Menu replaces tile-1's slot (its dim
  // is gone), focus moves to tile-1 (so tile-0 now dims), and the menu title shows.
  rightClick(ui.findByTestId("tile-1")!);
  ui.pump(10);
  expect(ui.findByTextContaining("RetroPlug") != null).toBeTruthy(); // menu opened
  expect(ui.findByTestId("dim-1")).toBe(null); // tile-1 slot now holds the Menu (no EmulatorTile/dim)
  expect(ui.findByTestId("dim-0") != null).toBeTruthy(); // focus moved off tile-0 → it dims
});
