// Headless UI input: open the AboutPanel from the StartScreen and close it,
// exercising both the pointer (clickAt) and keypad (tapKey) paths. Each case
// returns to the StartScreen so they're order-independent.
import { test, expect, ui, Key } from "ui-harness";

const ABOUT_BODY = "Game Boy / NES / GBA emulator plugin"; // ui/menu/AboutPanel.tsx

function openAbout() {
  const about = ui.findByText("About");
  expect(about).toBeTruthy();
  ui.clickAt(about!.x + (about!.width >> 1), about!.y + (about!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBeTruthy();
}

test("clickAt opens the About panel; Esc closes it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null); // not open yet

  openAbout();
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null); // closed
});

test("clicking the About panel body closes it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  openAbout();
  const body = ui.findByTextContaining(ABOUT_BODY);
  expect(body).toBeTruthy();
  ui.clickAt(body!.x + (body!.width >> 1), body!.y + (body!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null); // closed via onClick
});
