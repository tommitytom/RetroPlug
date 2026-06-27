// Headless UI: a saved project containing the embedded mGB reloads correctly
// through the real project-load path. mGB is path-less and byte-less (the bytes
// live in the binary), so scanMissingFiles must treat the embeddedRom marker as
// present — otherwise the reload lands in the relink menu instead of the tile.
// Guards the embed-mGB "survives reload" requirement end-to-end.
import { test, expect, ui, Key, CompType } from "ui-harness";

const PROJ = "/tmp/rp_mgb_reload.rplg";

function focusRow(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(5);
  }
  return ui.focused();
}

test("an embedded-mGB project survives Save Project As then reload", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Load the embedded mGB from the start menu (no file, no path).
  const item = ui.findByText("Load mGB (Gameboy MIDI Synth)");
  expect(item).toBeTruthy();
  ui.clickAt(item!.x + (item!.width >> 1), item!.y + (item!.height >> 1));
  ui.pump(60);
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(0); // the mGB tile

  // Save Project As → drive the (stubbed) save browser to a temp .rplg. This
  // writes a thin project: a SameBoy system with embeddedRom="mgb", no path,
  // no bytes.
  ui.tapKey(Key.Esc);
  ui.pump(30);
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Project
  ui.pump(10);
  expect(focusRow("Save Project As")?.text.includes("Save Project As")).toBeTruthy();
  ui.tapKey(Key.Enter); // opens the save browser
  ui.pump(20);
  ui.selectFile(PROJ); // completes the save → writes the thin .rplg
  ui.pump(40);

  // Reload through the real project-load path (scanMissingFiles runs here).
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(80);

  // The mGB came back: no relink menu, not the empty start screen, a tile renders.
  expect(ui.findByTextContaining("Locate")).toBeFalsy();  // no relink menu
  expect(ui.findByTextContaining("missing")).toBeFalsy();  // no relink menu
  expect(ui.findByTextContaining("Load mGB")).toBeFalsy();  // not the start screen
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(0); // the tile is back
});
