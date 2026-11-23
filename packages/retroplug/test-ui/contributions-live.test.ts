// The running UI follows a commit (docs/downloadable-packs-m2a-plan.md, Task 6): a menu builder committed after the UI
// booted shows in the start menu, and a tile view committed with a system on screen gets its slot in every tile. The
// commit goes through the UI's seam (__rp_commitUiContributions), which reaches the context's one set of registries
// (the control plane's stores read the same copy); this test's bundle has its own module copies and cannot name them.
import { test, expect, ui, navTo, navToPad, Key } from "ui-harness";

type NamedAddition = { registry: string; pkg: string; items: unknown[] };
const commit = (additions: NamedAddition[]): void =>
  ((globalThis as Record<string, unknown>).__rp_commitUiContributions as (a: NamedAddition[]) => void)(additions);

test("a menu builder committed after boot shows in the start menu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.findByText("Pack Fixture Row")).toBe(null);
  commit([{
    registry: "ui",
    pkg: "packfixture",
    items: [{
      menus: [{
        placement: "start",
        build: () => [{ id: "packfixture-row", label: "Pack Fixture Row", kind: "action", onSelect: () => {} }],
      }],
    }],
  }]);
  ui.pump(10);
  expect(ui.findByText("Pack Fixture Row") != null).toBeTruthy();
});

test("a tile view committed with a system on screen gets its slot under the focus border", () => {
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  // Duplicate the instance so tile 0 is focused and not the only tile: it draws the focus border, which sits after the
  // slots in the child list, so a slot appended after it would paint over the border.
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  expect(navToPad("Duplicate Instance")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  const tile = ui.findByTestId("tile-0")!;
  expect(tile != null).toBeTruthy();
  expect(ui.findByTestId("view-slot-packfixture-tile")).toBe(null);
  const { width, height } = tile;
  const solid = new Uint32Array(width * height).fill(0xffff0000);
  commit([{
    registry: "views",
    pkg: "packfixture",
    items: [{
      id: "packfixture-tile",
      label: "Pack Fixture Tile",
      placement: "tile",
      when: () => true,
      body: { kind: "pixels", open: () => ({ frame: () => ({ pixels: solid, width, height }) }) },
    }],
  }]);
  ui.pump(20);
  expect(ui.findByTestId("view-slot-packfixture-tile") != null).toBeTruthy();
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  // The tile's outer ring is the focus border (#4a86e8), not the fixture's solid red: the slot sits before the border.
  const snap = ui.snapshot();
  const at = (x: number, y: number) => {
    const i = (y * snap.width + x) * 4;
    return { b: snap.pixels[i], g: snap.pixels[i + 1], r: snap.pixels[i + 2] };
  };
  const ring = at(tile.x + Math.floor(width / 2), tile.y);
  expect(ring.r).toBe(0x4a);
  expect(ring.g).toBe(0x86);
  expect(ring.b).toBe(0xe8);
  // And the fixture does draw: well inside the tile the solid red shows.
  const inside = at(tile.x + Math.floor(width / 2), tile.y + Math.floor(height / 2));
  expect(inside.r).toBe(0xff);
});
