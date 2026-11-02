// The background-render badge on a system tile, end to end on the headless display. The tile lists the store graph's
// renders (src/renderJobs.ts); a scripted graph (test-ui/scriptedRenders.ts) lets the test start a render for the loaded
// system and move it through each state, then asserts the badge renders for each. Proves the tile's per-frame poll, the
// badge JSX (Box/Text/align props) and the tap-to-cancel route actually work - not just typecheck.

import { test, expect, ui, navTo, Key } from "ui-harness";
import { installScriptedStores } from "./scriptedRenders";

test("the render badge shows the rendered duration / error on a tile, cancels on a tap, and clears when the job finishes", () => {
  const scripted = installScriptedStores();
  const { stores, renders } = scripted;

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  const sysId = stores.project.systems.view()[0].id;

  // Rendering: the per-frame poll surfaces the badge, counting the audio rendered so far (no percentage -
  // a tracker render's true length isn't known until its HFF stop lands).
  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
  renders[0].options.onProgress!(42_000);
  ui.pump(10);
  expect(ui.findByTextContaining("rendering") != null).toBeTruthy();
  expect(ui.findByTextContaining("0:42") != null).toBeTruthy();
  expect(ui.findByTextContaining("%")).toBe(null);

  // A tap on the badge cancels the job through its handle.
  const badge = ui.findByTextContaining("rendering")!;
  ui.clickAt(badge.x + 2, badge.y + 2);
  ui.pump(5);
  expect(renders[0].cancelled).toBe(1);

  // Cancelled: the tile dismisses the job and the badge clears.
  renders[0].settle({ status: "cancelled", message: "", outputs: [], renderedMs: 42_000 });
  ui.pump(10);
  expect(ui.findByTextContaining("rendering")).toBe(null);
  expect(stores.renders.list().length).toBe(0);

  // Error: the badge switches to the failure message, and a tap dismisses it.
  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
  renders[1].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
  ui.pump(10);
  expect(ui.findByTextContaining("render failed") != null).toBeTruthy();
  expect(ui.findByTextContaining("rendering")).toBe(null);
  const failed = ui.findByTextContaining("render failed")!;
  ui.clickAt(failed.x + 2, failed.y + 2);
  ui.pump(5);
  expect(ui.findByTextContaining("render failed")).toBe(null);

  // Done: the tile dismisses the job and the badge clears.
  stores.renders.start(sysId, { rom: "/roms/mGB.gb" });
  renders[2].settle({ status: "done", message: "", outputs: ["/o.wav"], renderedMs: 42_100 });
  ui.pump(10);
  expect(ui.findByTextContaining("rendering")).toBe(null);
  expect(stores.renders.list().length).toBe(0);

  scripted.restore();
});
