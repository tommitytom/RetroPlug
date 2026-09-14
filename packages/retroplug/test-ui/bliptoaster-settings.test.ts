// The BlipToaster > Settings submenu, end to end on the headless display. The cart has no settings memory, so
// its rig defaults are bytes in the ROM; these rows edit them non-destructively (pinned on the
// bliptoaster-assets role, folded into the ROM in memory at construct). This is also where you CHOOSE which of
// the baked themes / fonts / kits the cart comes up in - the Themes and Fonts submenus replace an entry's
// contents, they don't pick the live one.
//
// test/menu/bliptoaster.test.ts already proves the menu MODEL. What only this can prove is the glue: that the
// rows become real LVGL widgets inside the instance menu, and that a keypress on one round-trips through the
// store and comes back as a new label. Base MIDI Channel and PPU Enabled are the rows driven here because they are
// two of the four fields the block shipped with, so every cart that has the block honours them; the staged
// resources/roms/bliptoaster.nes predates the theme + font fields, which is exactly why those two must show up
// greyed below.
//
// That staged ROM also predates the 2026-09-13 polarity flip of the screen byte (+9 was "Mode 1 at boot", 1 =
// dark; it is now "PPU enabled", 1 = draws). Its byte is 0, so the row below reads "Off" - which is what the
// byte says today, not what that old image does with it. Refreshing the ROM makes this "On" (a current build
// bakes 1) at the same time as it turns the two greyed rows into cyclers.
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
  expect(labelOf("Default Kit")).toBe("Default Kit: TR-909"); // named from the ROM, not a bare index
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: Off");
  expect(labelOf("Velocity Curve")).toBe("Velocity Curve: Linear");
  // Theme and Font are live rows like the rest. NOTE: the staged resources/roms/bliptoaster.nes is still an
  // OLDER cart, from before the block carried these two fields - its bytes are the reserved 0xFF, which decodes
  // to slot 0, and its code never reads them. The rows used to be greyed for exactly that reason; that gate is
  // gone (no released build predates the fields), so re-stage this ROM and the two rows become truthful as well
  // as live.
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
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: On");

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
  expect(labelOf("PPU Enabled")).toBe("PPU Enabled: Off");
  expect(ui.findByTextContaining("Apply (Reboot Cart)")).toBe(null);
});
