// Headless UI: the empty project renders the StartScreen chrome.
// (TS port of the C++ PoC's first case.)
import { test, expect, ui, CompType, isFlat } from "ui-harness";

test("headless UI boots and renders start-screen chrome", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // let the initial RPC round-trips + React render settle

  const snap = ui.snapshot();
  expect(snap.width).toBe(480);
  expect(snap.height).toBe(432);
  expect(isFlat(snap)).toBeFalsy();          // something actually rendered
  expect(ui.widgetCount()).toBeGreaterThan(0);
  expect(ui.countByType(CompType.Text)).toBeGreaterThan(0); // StartScreen menu labels

  expect(ui.snapshotPng("/tmp/ui-ts-start.png")).toBeTruthy();
});
