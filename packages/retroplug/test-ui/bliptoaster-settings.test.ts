// The BlipToaster settings rows, end to end on the headless display. The cart has no settings memory, so its rig
// defaults are bytes in the ROM; these rows edit them non-destructively (pinned on the bliptoaster-assets role,
// folded into the ROM in memory at construct). This is also where you CHOOSE which of the baked themes / fonts
// the cart comes up in - the Themes and Fonts submenus replace an entry's contents, they don't pick the live one.
//
// test/menu/bliptoaster.test.ts already proves the menu MODEL. What only this can prove is the glue: that the
// rows become real LVGL widgets inside the instance menu, and that a keypress on one round-trips through the
// store and comes back as a new label. Base MIDI Channel and PPU Enabled are the rows driven here because they
// are two of the fields the block shipped with, so every cart carrying the block honours them.
//
// Every expectation below is the staged resources/roms/bliptoaster.nes's OWN baked block, i.e. a cart straight
// out of the build: every field at its power-on default, which is 0 for all of them except PPU enabled (1 - the
// screen draws). Rebuild + re-stage that ROM and these stay true unless the ROM's own defaults move.
import { test, expect, ui, navTo, Key } from "ui-harness";

const BLIPTOASTER = () => ui.romDir() + "/bliptoaster.nes";
const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("the BlipToaster Settings rows render in the instance menu and cycle the cart's baked defaults", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  ui.fileDrop(BLIPTOASTER(), 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Esc opens the instance menu; the cart's marker role puts a BlipToaster submenu on it.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("BlipToaster")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);

  // The settings rows sit at the top of the BlipToaster menu, above the asset submenus and behind no submenu
  // of their own: expanding BlipToaster is all it takes to reach them.
  // Every field of the block has a row, each showing the ROM's own baked value.
  expect(labelOf("Base MIDI Channel")).toBe("Base MIDI Channel: 01");
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: On"); // the one field whose power-on default is 1
  expect(labelOf("Velocity Curve")).toBe("Velocity Curve: Linear");
  // Theme and Font are live rows like the rest, reading the current build's own bytes.
  expect(labelOf("Theme")).toBe("Theme: DFLT");
  expect(labelOf("Font")).toBe("Font: Font 0");
  // Nothing pinned yet, so the two reboot rows are absent: the rows above are showing the ROM's own bytes, and
  // there is nothing to apply or reset.
  expect(ui.findByTextContaining("Apply (Reboot Cart)")).toBe(null);
  expect(ui.findByTextContaining("Reset to ROM Defaults")).toBe(null);

  // Right steps the value: role config written, row re-rendered off the new pin.
  expect(navTo("Base MIDI Channel")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(labelOf("Base MIDI Channel")).toBe("Base MIDI Channel: 02");

  // Left comes back, so it is a live two-way row and not a one-shot write. Still PINNED, though, at the value
  // the ROM happens to bake - so the reboot rows have appeared and stay.
  ui.tapKey(Key.Left);
  ui.pump(20);
  expect(labelOf("Base MIDI Channel")).toBe("Base MIDI Channel: 01");
  expect(ui.findByTextContaining("Apply (Reboot Cart)") != null).toBeTruthy();

  // A cycler does NOT reboot, which is what keeps the menu open across all of the above - a reload swaps the
  // system id and the instance menu is anchored to it, so rebooting per keypress would drop the menu each time.
  expect(navTo("PPU Enabled")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: Off"); // stepped off the ROM's default

  // Reset clears the pin back to the ROM's own bytes. It reboots, so the menu closes - the grid tile is back.
  expect(navTo("Reset to ROM Defaults")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Reopen and the row is back at the ROM's value, with the reboot rows gone again.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("BlipToaster")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: On"); // the one field whose power-on default is 1
  expect(ui.findByTextContaining("Apply (Reboot Cart)")).toBe(null);
});
