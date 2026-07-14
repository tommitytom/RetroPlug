// hitTestTile — map a window-pixel drop coordinate to the grid tile index under it. Must agree with
// SystemGrid's layout: the fitZoom-capped displayed zoom + the grid CENTERED in the window. A tile is
// 160·zoom × 144·zoom (GB native × zoom).
import { test, expect } from "../../testing/harness";
import { hitTestTile, SystemLayout } from "../../ui/screens/grid/layout";

const AUTO = SystemLayout.Auto;

test("single instance, window sized to the grid: center hits the only tile", () => {
  // count 1 → 1×1; zoom 3 → 480×432, window exactly that, no centering offset.
  const win = { width: 480, height: 432 };
  expect(hitTestTile(240, 216, 1, AUTO, 3, 3, win)).toBe(0);
  expect(hitTestTile(0, 0, 1, AUTO, 3, 3, win)).toBe(0);
  expect(hitTestTile(479, 431, 1, AUTO, 3, 3, win)).toBe(0);
});

test("single instance: a coordinate outside the grid misses", () => {
  const win = { width: 480, height: 432 };
  expect(hitTestTile(480, 216, 1, AUTO, 3, 3, win)).toBe(null); // x at the right edge (exclusive)
  expect(hitTestTile(-1, 10, 1, AUTO, 3, 3, win)).toBe(null);
  expect(hitTestTile(10, 432, 1, AUTO, 3, 3, win)).toBe(null);
});

test("2×2 grid: each quadrant maps to its tile index", () => {
  // count 4 → 2×2; zoom 2 → tile 320×288, grid 640×576, window exact.
  const win = { width: 640, height: 576 };
  expect(hitTestTile(10, 10, 4, AUTO, 2, 2, win)).toBe(0); // top-left
  expect(hitTestTile(330, 10, 4, AUTO, 2, 2, win)).toBe(1); // top-right
  expect(hitTestTile(10, 300, 4, AUTO, 2, 2, win)).toBe(2); // bottom-left
  expect(hitTestTile(330, 300, 4, AUTO, 2, 2, win)).toBe(3); // bottom-right
});

test("partially-filled last row: the empty cell misses", () => {
  // count 3 still lays out on a 2×2 grid; the bottom-right cell is empty.
  const win = { width: 640, height: 576 };
  expect(hitTestTile(10, 300, 3, AUTO, 2, 2, win)).toBe(2); // filled cell
  expect(hitTestTile(330, 300, 3, AUTO, 2, 2, win)).toBe(null); // empty cell (index 3 ≥ count)
});

test("grid centered in a larger window: offset is accounted for", () => {
  // count 1, zoom 3 → 480×432 grid centered in 600×500 → offset (60, 34).
  const win = { width: 600, height: 500 };
  expect(hitTestTile(60, 34, 1, AUTO, 3, 3, win)).toBe(0); // top-left of the grid
  expect(hitTestTile(300, 250, 1, AUTO, 3, 3, win)).toBe(0); // center
  expect(hitTestTile(10, 10, 1, AUTO, 3, 3, win)).toBe(null); // in the letterbox margin
  expect(hitTestTile(540, 250, 1, AUTO, 3, 3, win)).toBe(null); // x at the grid's right edge (60+480)
});

test("an out-of-range project zoom falls back to the default zoom", () => {
  const win = { width: 320, height: 288 }; // fits exactly at zoom 2
  expect(hitTestTile(160, 144, 1, AUTO, 0, 2, win)).toBe(0); // projectZoom 0 invalid → defaultZoom 2
  expect(hitTestTile(160, 144, 1, AUTO, 7, 2, win)).toBe(0); // projectZoom 7 invalid → defaultZoom 2
});

test("no instances: every coordinate misses", () => {
  expect(hitTestTile(100, 100, 0, AUTO, 3, 3, { width: 480, height: 432 })).toBe(null);
});

test("row layout: a 3-wide strip maps by column", () => {
  // Row → 3×1; zoom 2 → tile 320×288, grid 960×288.
  const win = { width: 960, height: 288 };
  expect(hitTestTile(10, 10, 3, SystemLayout.Row, 2, 2, win)).toBe(0);
  expect(hitTestTile(330, 10, 3, SystemLayout.Row, 2, 2, win)).toBe(1);
  expect(hitTestTile(650, 10, 3, SystemLayout.Row, 2, 2, win)).toBe(2);
  expect(hitTestTile(960, 10, 3, SystemLayout.Row, 2, 2, win)).toBe(null); // past the right edge
});
