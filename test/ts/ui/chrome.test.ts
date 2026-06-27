// Headless UI: the empty project renders the StartScreen chrome.
import { test, expect, ui, CompType, isFlat } from "ui-harness";

test("StartScreen renders at the window size with content", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // let the initial RPC round-trips + React render settle

  const snap = ui.snapshot();
  expect(snap.width).toBe(480);
  expect(snap.height).toBe(432);
  expect(isFlat(snap)).toBeFalsy();             // something actually rendered
  expect(ui.widgetCount()).toBeGreaterThan(0);
  expect(ui.countByType(CompType.Text)).toBeGreaterThan(0);

  expect(ui.snapshotPng("/tmp/ui-ts-start.png")).toBeTruthy();
});

test("StartScreen shows the expected menu items", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Title (now carries the version, e.g. "RetroPlug v0.6.2") + the top-level
  // menu (submenus render with a trailing " >").
  expect(ui.findByTextContaining("RetroPlug")).toBeTruthy();
  expect(ui.findByText("Load...")).toBeTruthy();
  expect(ui.findByTextContaining("Recent")).toBeTruthy();
  expect(ui.findByTextContaining("Project")).toBeTruthy();
  expect(ui.findByTextContaining("Settings")).toBeTruthy();
  expect(ui.findByText("About")).toBeTruthy();
});
