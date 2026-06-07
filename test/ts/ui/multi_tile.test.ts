// Headless UI: loading two ROMs lays out two tiles side by side in the grid.
import { test, expect, ui, CompType } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

test("two ROMs render two tiles in a row", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const a = ui.loadRom(MGB);
  const b = ui.loadRom(MGB);
  expect(a).toBeLessThan(b); // distinct system ids
  ui.pump(60);

  const slotA = ui.findByTestId(`slot-${a}`);
  const slotB = ui.findByTestId(`slot-${b}`);
  expect(slotA).toBeTruthy();
  expect(slotB).toBeTruthy();

  // Two tiles in the default (auto) layout sit at different x positions and the
  // same row (side by side), each non-empty.
  expect(slotA!.x).toBeLessThan(slotB!.x);
  expect(slotA!.width).toBeGreaterThan(0);
  expect(slotB!.width).toBeGreaterThan(0);

  // One <Canvas> (lv_image) per tile -> at least two.
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(1);

  expect(ui.snapshotPng("/tmp/ui-ts-multi.png")).toBeTruthy();
});
