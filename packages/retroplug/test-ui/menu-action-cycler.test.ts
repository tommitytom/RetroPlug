// The inline action-cycler row ("actionCycler"), end to end on the headless display — the flat form the
// tracker KIT lists took so that patching a kit costs no extra level of menu.
//
// test/menu/*.test.ts already proves the MODEL side (that a kit row carries its verbs rather than children).
// What only this can prove is the renderer's half, which lives entirely in Menu.tsx and has no other cover:
// that the row composes into ONE lv_label carrying `[0] <name>   <  verb  >`, that Left/Right re-render it
// onto the next verb without moving focus, that the pick wraps, and that Enter runs the verb the row is
// showing at that moment — the renderer owns which verb that is, so nothing downstream can be asked.
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

  // The kit rows are rows, not submenus: no "v"/">" expander, the verb rides on the line inside <>. Landing
  // on one shows the FIRST verb, which is deliberately the read-only one — an unaimed Enter can't destroy a kit.
  expect(navTo("[0] ")).toBeTruthy();
  const row = () => ui.focused()?.text ?? "(unfocused)";
  expect(/^\[0\] .*<\s+Export\.\.\.\s+>$/.test(row())).toBeTruthy();

  // Right walks the list; the name half never changes, and focus stays put (a step is not a move).
  const name = row().slice(0, row().indexOf("<")).trimEnd();
  expect(step(Key.Right)).toBe(`${name}   < Replace from Disk... >`);
  expect(/^\[0\] .*<\s+Delete\s+>$/.test(step(Key.Right))).toBeTruthy();
  // ...and wraps, like every other cycler in this menu.
  expect(/<\s+Export\.\.\.\s+>$/.test(step(Key.Right))).toBeTruthy();
  // Left walks it back the other way, so it is a live two-way pick and not a one-shot advance.
  expect(/<\s+Delete\s+>$/.test(step(Key.Left))).toBeTruthy();

  // Enter runs the verb the row is SHOWING (Delete), not the one it opened on: the slot is erased through the
  // assets role and the menu closes, exactly as the equivalent submenu leaf did.
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  openKits();
  expect(ui.findByTextContaining("[0] ")).toBe(null); // slot 0 is gone from the effective kit list
});
