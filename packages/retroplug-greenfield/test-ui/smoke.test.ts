// Greenfield UI render-scaffold smoke.
//
// Boots the throwaway greenfield smoke UI on the headless software LVGL display (RenderCore), driven by
// the BackendFacade RPC (GreenfieldUiHarness), and asserts it actually rendered: a non-flat snapshot,
// the static title, and the cfg text produced by a real BackendFacade round-trip (configDir via
// realBackend). This proves the render scaffold + backend bridge loop end to end — the foundation for
// the Phase-4 UI-over-stores port. NOT a test of any real UI.

import { test, expect, ui, isFlat, CompType } from "ui-harness";

test("the render scaffold boots the greenfield smoke UI and renders it through BackendFacade", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // settle the React mount + the configDir RPC effect

  const snap = ui.snapshot();
  expect(snap.width).toBe(480);
  expect(snap.height).toBe(432);
  expect(isFlat(snap)).toBeFalsy(); // something rendered — not a blank screen

  // Eyeball artifact (parity with `pnpm screenshot`).
  ui.snapshotPng("/tmp/greenfield-ui-smoke.png");

  // The static title is the stable assertion anchor.
  const title = ui.findByTextContaining("RetroPlug Greenfield UI");
  expect(title != null).toBeTruthy();

  // The cfg line only appears once the BackendFacade round-trip (createRealBackend().configDir())
  // completed inside the UI — so finding it proves the RPC bridge works, not just the render.
  const cfg = ui.findByTextContaining("cfg:");
  expect(cfg != null).toBeTruthy();

  // Two Text widgets (title + cfg) live in the tree.
  expect(ui.countByType(CompType.Text) >= 2).toBeTruthy();
});
