// The running UI follows a commit (docs/downloadable-packs-m2a-plan.md, Task 6): a menu builder committed after the UI
// booted shows in the start menu, and a tile view committed with a system on screen gets its slot in every tile. The
// commit goes through the UI bundle's own seam (__rp_commitUiContributions), since the UI bundle's registries are its
// own copy and this test's bundle cannot name them.
import { test, expect, ui, navTo, Key } from "ui-harness";

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

test("a tile view committed with a system on screen gets its slot", () => {
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("view-slot-packfixture-tile")).toBe(null);
  commit([{
    registry: "views",
    pkg: "packfixture",
    items: [{
      id: "packfixture-tile",
      label: "Pack Fixture Tile",
      placement: "tile",
      when: () => false,
      body: { kind: "readout", open: () => null },
    }],
  }]);
  ui.pump(10);
  expect(ui.findByTestId("view-slot-packfixture-tile") != null).toBeTruthy();
  // The tile is still the system's, drawn again rather than lost.
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
