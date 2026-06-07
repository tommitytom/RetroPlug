// Headless UI input driving: click the StartScreen "About" item to open the
// AboutPanel (pointer -> onClick), then Esc to close it (keypad indev).
import { test, expect, ui, Key } from "ui-harness";

const ABOUT_BODY = "Game Boy / NES / GBA emulator plugin"; // ui/menu/AboutPanel.tsx

test("clickAt opens the About panel; Esc closes it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const about = ui.findByText("About");
  expect(about).toBeTruthy();
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null); // panel not open yet

  // Click the center of the "About" label -> its onClick -> openAbout().
  ui.clickAt(about!.x + (about!.width >> 1), about!.y + (about!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBeTruthy(); // AboutPanel rendered

  // Esc (keypad indev) closes the panel.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null);
});
