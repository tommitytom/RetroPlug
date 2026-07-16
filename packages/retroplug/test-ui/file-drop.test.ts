// Drag-and-drop routing, end to end on the headless display. The native editor delivers an OS file drop
// on the "file-drop" bus (PluginUI::uiFileDropped); App's handler routes it by instance count. We drive
// that bus with ui.fileDrop (a real, staged resources ROM by absolute path) and assert the grid mutated
// the right way:
//   - dropped on the start screen → loads as a NEW project (one tile appears);
//   - dropped onto a specific tile in a 2-up project → cold-boot REPLACES that tile (still two tiles).
//
// One harness process per file, so state persists across the tests below — they run as one narrative.

import { test, expect, ui, navTo, Key } from "ui-harness";

const MGB = () => ui.romDir() + "/mGB.gb";

test("dropping a ROM on the start screen loads it as a project", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.romDir().length > 0).toBeTruthy(); // the runner staged a real ROM
  expect(ui.findByTestId("tile-0")).toBe(null); // start screen: no tiles yet

  ui.fileDrop(MGB(), 0, 0); // no tile to hit on the start screen; coords are ignored
  ui.pump(30);

  expect(ui.findByTestId("tile-0") != null).toBeTruthy(); // a single instance now shows
  expect(ui.findByTestId("tile-1")).toBe(null);
});

test("dropping a ROM onto a tile in a multi-instance project replaces that tile (no add)", () => {
  // Grow to two instances via the instance menu (Duplicate) on the project loaded above.
  ui.tapKey(Key.Esc); // open the instance menu
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  const t0 = ui.findByTestId("tile-0");
  const t1 = ui.findByTestId("tile-1");
  expect(t0 != null && t1 != null).toBeTruthy();
  expect(ui.findByTestId("tile-2")).toBe(null); // exactly two before the drop

  // Drop the ROM onto the CENTER of tile-1 → cold-boot replace that tile.
  ui.fileDrop(MGB(), t1.x + Math.floor(t1.width / 2), t1.y + Math.floor(t1.height / 2));
  ui.pump(30);

  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();
  expect(ui.findByTestId("tile-2")).toBe(null); // replaced in place, not appended
});
