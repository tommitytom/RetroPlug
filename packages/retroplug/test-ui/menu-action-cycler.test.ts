// The inline action-cycler row ("actionCycler"), end to end on the headless display — the flat form the
// tracker KIT lists took so that patching a kit costs no extra level of menu.
//
// test/menu/*.test.ts already proves the MODEL side (that a kit row carries its verbs rather than children).
// What only this can prove is the renderer's half, which lives entirely in Menu.tsx and has no other cover:
// that a focused row lays out as `[0] <name>  <  verb  >` and an unfocused one is just its name, that
// Left/Right re-render it onto the next verb without moving focus, that the pick wraps, that the two arrows
// hold the SAME pixels whatever verb sits between them, and that Enter runs the verb the row is showing at
// that moment — the renderer owns which verb that is, so nothing downstream can be asked.
//
// Driven on resources/roms/bliptoaster.nes: a mapper-69 (FME-7) banking cart, so its kit type is addable and
// the row carries all three of Export / Replace / Delete. Delete is the one verb that neither opens a file
// dialog nor needs an existing override, which is what makes the Enter leg runnable headlessly.
import { test, expect, ui, navTo, Key } from "ui-harness";

const BLIPTOASTER = () => ui.romDir() + "/bliptoaster.nes";

/** Open the instance menu and expand BlipToaster > Kits. The menu closes on every Enter that runs a verb, so
 *  the whole descent is repeated rather than kept. */
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

/** Step the focused row's verb and hand back its new label. */
function step(key: number): string {
  ui.tapKey(key);
  ui.pump(6);
  return ui.focused()?.text ?? "(unfocused)";
}

test("a kit row carries its verbs inline: Left/Right pick one, Enter runs the picked one", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(BLIPTOASTER(), 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  openKits();

  // The kit rows are rows, not submenus: no "v"/">" expander, the verb rides on the line between the arrows.
  // Landing on one shows the FIRST verb, which is deliberately the read-only one — an unaimed Enter can't
  // destroy a kit. (The row is several labels laid out as columns, so focused().text is the harness composing
  // them in tree order — see RenderCore::widgetInfo.)
  expect(navTo("[0] ")).toBeTruthy();
  const row = () => ui.focused()?.text ?? "(unfocused)";
  const name = "[0] TR-909";
  expect(row()).toBe(`${name} < Export... >`);

  // The arrows are geometry, not text: they must land on the same pixels whatever the verb between them is,
  // which is the entire reason the row is laid out instead of space-padded (the font is proportional).
  const arrowBox = () => {
    const lhs = ui.findByText("<")!;
    const rhs = ui.findByText(">")!;
    return `${lhs.x}..${rhs.x + rhs.width}`;
  };
  const fixed = arrowBox();

  // Right walks the list; the name half never changes, focus stays put (a step is not a move), and the
  // arrows do not budge under the widest verb in the list.
  expect(step(Key.Right)).toBe(`${name} < Replace from Disk... >`);
  expect(arrowBox()).toBe(fixed);
  expect(step(Key.Right)).toBe(`${name} < Delete >`);
  expect(arrowBox()).toBe(fixed);
  // ...and wraps, like every other cycler in this menu.
  expect(step(Key.Right)).toBe(`${name} < Export... >`);
  // Left walks it back the other way, so it is a live two-way pick and not a one-shot advance.
  expect(step(Key.Left)).toBe(`${name} < Delete >`);

  // The element belongs to the cursor: step off the row and it is just its name again, so a list of kits
  // reads as a column of names. Stepping back on restores the verb the row was left on, not the first one.
  ui.tapKey(Key.Up);
  ui.pump(6);
  expect(ui.findByTextContaining("[0] ")?.text).toBe(name);
  expect(ui.findByText("<")).toBe(null); // no arrows anywhere once no action-cycler row is focused
  ui.tapKey(Key.Down);
  ui.pump(6);
  expect(row()).toBe(`${name} < Delete >`);
  expect(arrowBox()).toBe(fixed);

  // Enter runs the verb the row is SHOWING (Delete), not the one it opened on: the slot is erased through the
  // assets role and the menu closes, exactly as the equivalent submenu leaf did.
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  openKits();
  expect(ui.findByTextContaining("[0] ")).toBe(null); // slot 0 is gone from the effective kit list
});
