// The action-cycler row's MOUSE semantics, on the headless display. Split from menu-action-cycler.test.ts
// because both tests finish by deleting the cart's only kit, and UI test files get a process each — sharing
// one would leave the second with no kit row to aim at.
//
// Driven on resources/roms/bliptoaster.nes (mapper 69, so its kit type is addable and the row carries
// Export / Replace / Delete). Delete is the only verb that neither opens a file dialog nor needs an existing
// override, which is what makes the "clicking the verb runs it" leg runnable headlessly.
import { test, expect, ui, navTo, Key, State, type WidgetInfo } from "ui-harness";

const BLIPTOASTER = () => ui.romDir() + "/bliptoaster.nes";

const mid = (w: WidgetInfo) => [w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2)] as const;

/** Open the instance menu and expand BlipToaster > Kits. */
function openKits(): void {
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("BlipToaster")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Kits")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
}

// The mouse aims at the CELLS, not the row. That distinction only matters because the element is invisible
// until the row is focused: without it, a click meant to select a kit would run whichever verb the row
// happened to be sitting on, with nothing on screen to say which.
//
// Every click here is preceded by a moveMouse to the same spot, the way a real pointer arrives. It is also
// load-bearing: LVGL only refreshes `indev->pointer.last_hovered` on a RELEASE that moved, and never sets it
// on press when it was NULL (lv_indev.c:1296-1346), so a teleporting click leaves its target stuck in
// LV_STATE_HOVERED forever. Reaching a cell by teleport is a harness-only motion, but it makes the hover
// assertions below lie, so don't.
test("the mouse aims at the cells: the verb runs it, the arrows step it, the row body only selects", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(BLIPTOASTER(), 0, 0);
  ui.pump(30);
  openKits();

  // The cursor is on "Kits"; the kit row below is unfocused, so it is a bare name with nothing to aim at.
  const nameCell = ui.findByText("[0] TR-909")!;
  expect(ui.findByText("<")).toBe(null);

  // An unfocused row is ONE hover target across its whole width — its name label fills the row's slack
  // rather than hugging the text. A clickable child steals LV_STATE_HOVERED from its ancestor, so while the
  // name was content-sized the row's bar showed in the empty space beside it and blinked out over the text
  // itself. Sweep the pointer across the row: it must stay lit the whole way.
  const cy = mid(nameCell)[1];
  for (const x of [nameCell.x + 4, nameCell.x + Math.floor(nameCell.width / 2), nameCell.x + nameCell.width - 6]) {
    ui.moveMouse(x, cy);
    ui.pump(8);
    expect(ui.findByText("[0] TR-909")!.state & State.Hovered).toBe(State.Hovered);
  }

  // Clicking the row BODY only moves the cursor there — the element appears and the menu stays open.
  ui.moveMouse(...mid(nameCell));
  ui.clickAt(...mid(nameCell));
  ui.pump(12);
  expect(ui.focused()?.text).toBe("[0] TR-909 < Export... >");
  expect(ui.findByTextContaining("Kits") != null).toBeTruthy(); // nothing ran; still in the menu

  // The verb's hit target is the WORDS, not the column it is centred in — comfortably narrower than the gap
  // between the arrows — and hovering lights it alone.
  const verb = ui.findByText("Export...")!;
  expect(verb.width).toBeLessThan(ui.findByText(">")!.x - (ui.findByText("<")!.x + ui.findByText("<")!.width));
  ui.moveMouse(...mid(verb));
  ui.pump(8);
  expect(ui.findByText("Export...")!.state & State.Hovered).toBe(State.Hovered);
  expect(ui.findByText("<")!.state & State.Hovered).toBe(0);
  expect(ui.findByText(">")!.state & State.Hovered).toBe(0);

  // Clicking an arrow steps the pick rather than running anything.
  const click = (label: string) => {
    const w = ui.findByText(label)!;
    ui.moveMouse(...mid(w));
    ui.pump(4);
    ui.clickAt(...mid(w));
    ui.pump(12);
  };
  click(">");
  expect(ui.focused()?.text).toBe("[0] TR-909 < Replace from Disk... >");
  click("<");
  expect(ui.focused()?.text).toBe("[0] TR-909 < Export... >");
  click(">");
  click(">");
  expect(ui.focused()?.text).toBe("[0] TR-909 < Delete >");
  expect(ui.findByTextContaining("Kits") != null).toBeTruthy(); // four arrow clicks, nothing run

  // Clicking the verb runs it: the slot is erased and the menu closes.
  click("Delete");
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  openKits();
  expect(ui.findByTextContaining("[0] ")).toBe(null);
});
