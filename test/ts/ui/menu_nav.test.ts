// Headless UI: keyboard navigation moves the menu selection. Asserted via a
// snapshot diff (the focused item's highlight colour changes) so it doesn't
// depend on the exact item order.
import { test, expect, ui, Key, pixelDiff } from "ui-harness";

test("keypad Down moves the StartScreen selection", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const before = ui.snapshot();
  ui.tapKey(Key.Down);
  ui.pump(10);
  const after = ui.snapshot();

  // The selection highlight moved -> some pixels changed.
  expect(pixelDiff(before, after)).toBeGreaterThan(0);

  // The UI is still alive (didn't crash / blank out).
  expect(ui.findByText("About")).toBeTruthy();
});
