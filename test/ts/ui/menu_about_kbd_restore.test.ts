// Regression: visiting the AboutPanel and going back must leave the keypad
// group pointed at the underlying Menu — not the empty input-sink group.
//
// AboutPanel claims its own keyboard group on mount and restores the sink on
// unmount. On the StartScreen the Menu is re-mounted in the SAME commit that
// unmounts AboutPanel; if AboutPanel restores the group from a passive effect
// (useEffect) its cleanup runs AFTER the Menu's synchronous useLayoutEffect
// claim, clobbering it with the sink and killing menu keyboard nav. ui.focused()
// reads lv_group_get_focused(keypad group): non-null only when a real Menu group
// is claimed (the sink is empty), so it's a direct probe for the regression.
import { test, expect, ui, Key } from "ui-harness";

const ABOUT_BODY = "Game Boy / NES / GBA emulator plugin"; // ui/menu/AboutPanel.tsx

// Down-arrow until "About" is the focused row (deterministic via ui.focused()).
function focusAbout() {
  for (let i = 0; i < 12; i++) {
    const f = ui.focused();
    if (f && f.text.includes("About")) return true;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  return false;
}

test("keyboard nav survives an About open/close round-trip", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Sanity: the StartScreen menu owns the keypad — a row is focused.
  expect(ui.focused()).toBeTruthy();

  expect(focusAbout()).toBeTruthy();
  ui.tapKey(Key.Enter); // open About
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBeTruthy();

  ui.tapKey(Key.Enter); // AboutPanel body consumes Enter -> close, back to menu
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null);

  // The bug: keypad group is now the empty sink -> focused() is null and
  // arrow nav is dead. After the fix the Menu re-claims the group on remount.
  expect(ui.focused()).toBeTruthy();

  // And nav actually moves: Down changes the focused row.
  const before = ui.focused();
  ui.tapKey(Key.Down);
  ui.pump(10);
  const after = ui.focused();
  expect(after).toBeTruthy();
  expect(after!.text !== before!.text).toBeTruthy();
});
