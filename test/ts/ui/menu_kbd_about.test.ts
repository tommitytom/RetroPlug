// Headless UI: open the AboutPanel entirely by keyboard — navigate to "About"
// with the arrow keys and activate with Enter, then close with Enter. Isolated
// in its own file because opening the panel reassigns the keypad group.
import { test, expect, ui, Key } from "ui-harness";

const ABOUT_BODY = "Game Boy / NES / GBA emulator plugin"; // ui/menu/AboutPanel.tsx

test("keyboard: arrows to About + Enter opens it; Enter closes it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null);

  // Down until "About" is the focused item (deterministic via ui.focused()).
  let onAbout = false;
  for (let i = 0; i < 12 && !onAbout; i++) {
    const f = ui.focused();
    if (f && f.text.includes("About")) { onAbout = true; break; }
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  expect(onAbout).toBeTruthy();

  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBeTruthy(); // opened by keyboard

  ui.tapKey(Key.Enter); // the AboutPanel body consumes Enter -> close
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null);
});
