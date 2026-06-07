// Headless UI: a StartScreen submenu ("Recent") expands inline on activation and
// collapses again. With no recent files wired, it shows "(No Recent Files)".
import { test, expect, ui } from "ui-harness";

test("clicking the Recent submenu expands and collapses it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Collapsed: the child placeholder is not present.
  expect(ui.findByTextContaining("No Recent Files")).toBe(null);

  const recent = ui.findByTextContaining("Recent"); // the "Recent >" menu item
  expect(recent).toBeTruthy();

  // Expand: click the item -> its children render below.
  ui.clickAt(recent!.x + (recent!.width >> 1), recent!.y + (recent!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining("No Recent Files")).toBeTruthy();

  // Collapse: click it again.
  const recent2 = ui.findByTextContaining("Recent");
  ui.clickAt(recent2!.x + (recent2!.width >> 1), recent2!.y + (recent2!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining("No Recent Files")).toBe(null);
});
