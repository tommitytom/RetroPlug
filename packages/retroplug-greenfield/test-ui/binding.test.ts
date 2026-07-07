// Greenfield UI binding-layer proof.
//
// Boots the store-backed greenfield UI (DevProbe under StoreProvider) on the headless LVGL display and
// proves the whole store→React seam through the REAL reconciler:
//   - useNativeEvent("frame", …) receives the bus events pump() emits (frame counter advances).
//   - Tapping "zoom+" calls a SILENT ProjectStore setter (setZoom); the new setOnChange observer +
//     the provider fan-out + the cached snapshot re-render the zoom + dirty views.
// If any link in that chain is missing, the zoom text wouldn't change and dirty wouldn't flip.

import { test, expect, ui, isFlat } from "ui-harness";

test("the binding layer re-renders store views on a mutation driven through the real reconciler", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // settle the React mount + effects

  const snap = ui.snapshot();
  expect(snap.width).toBe(480);
  expect(isFlat(snap)).toBeFalsy(); // something rendered

  // Native-event delivery: the "frame" bus events pump() emits reached useNativeEvent.
  const frames0 = ui.findByTextContaining("frames:");
  expect(frames0 != null).toBeTruthy();
  expect(frames0!.text !== "frames:0").toBeTruthy();

  // Initial store-backed views.
  const zoom0 = ui.findByTextContaining("zoom:");
  expect(zoom0 != null).toBeTruthy();
  const dirty0 = ui.findByTextContaining("dirty:");
  expect(dirty0?.text).toBe("dirty:no");

  // Drive a store mutation via a real click on the "zoom+" widget.
  const btn = ui.findByTextContaining("zoom+");
  expect(btn != null).toBeTruthy();
  ui.clickAt(btn!.x + Math.floor(btn!.width / 2), btn!.y + Math.floor(btn!.height / 2));
  ui.pump(20); // let the click resolve to onClick → setZoom → notify → re-render

  // Eyeball artifact (zoom text shows the post-click value).
  ui.snapshotPng("/tmp/greenfield-ui-binding.png");

  // The silent setter is now observed: the zoom view changed and dirty flipped.
  const zoom1 = ui.findByTextContaining("zoom:");
  expect(zoom1 != null).toBeTruthy();
  expect(zoom1!.text !== zoom0!.text).toBeTruthy();
  const dirty1 = ui.findByTextContaining("dirty:");
  expect(dirty1?.text).toBe("dirty:yes");
});
