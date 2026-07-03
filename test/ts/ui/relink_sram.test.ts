// Headless UI: loading a thin project whose explicit paired save (savPath) has
// moved shows the relink menu with a "Locate save" row; locating the .sav commits
// the load and a tile appears. The ROM is present, so only the save is missing —
// exercises the sram branch of the missing-files flow end to end (parse →
// "missing-files" event → relink → commit) plus the RelinkMenu row + .sav browser.
import { test, expect, ui, Key } from "ui-harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb"; // present ROM (found location)
const PROJ = "/tmp/rp_relink_sram_proj.rplg";
const GONE_SAV  = "/tmp/rp_relink_sram_gone.sav";   // referenced but absent
const FOUND_SAV = "/tmp/rp_relink_sram_found.sav";  // the real save we locate

test("relink menu locates a missing paired save and completes the load", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Thin project: ROM present, but its paired savPath points at a missing file.
  ui.writeProjectJson(PROJ, LSDJ, 0, GONE_SAV);

  // Only the save is missing → the load is held and the relink menu shows.
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(40);

  expect(ui.findByTextContaining("missing")).toBeTruthy();       // menu title
  expect(ui.findByTextContaining("Locate save")).toBeTruthy();   // the sram row

  // Author a real save on disk, then locate it via the (sram) browser.
  ui.writeFile(FOUND_SAV, new Uint8Array([0x11, 0x22, 0x33, 0x44]));
  ui.tapKey(Key.Enter);
  ui.pump(10);
  ui.selectFile(FOUND_SAV);
  ui.pump(60);

  // Load committed: relink menu gone, a system tile present.
  expect(ui.findByTextContaining("Locate")).toBeFalsy();
  expect(ui.findByTestId("slot-1")).toBeTruthy();
});
