// Headless UI: a system whose ROM AND paired save are both missing shows two
// distinct relink rows and completes the load once both are located. Guards the
// two-missing-item flow (the RelinkMenu row key must include itemKind, or the
// rom + sram rows collapse to one key).
import { test, expect, ui, Key } from "ui-harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb"; // present ROM, used to relink
const PROJ = "/tmp/rp_relink_both_proj.rplg";
const GONE_ROM = "/tmp/rp_relink_both_gone.gb";     // referenced but absent
const GONE_SAV = "/tmp/rp_relink_both_gone.sav";    // referenced but absent
const FOUND_SAV = "/tmp/rp_relink_both_found.sav";  // the real save we locate

test("relink menu handles a system missing both its ROM and paired save", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Thin project: ROM AND its explicit paired save both point at missing files.
  ui.writeProjectJson(PROJ, GONE_ROM, 0, GONE_SAV);
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(40);

  // Two distinct rows (this is where the duplicate-key bug bit).
  expect(ui.findByTextContaining("Locate ROM")).toBeTruthy();
  expect(ui.findByTextContaining("Locate save")).toBeTruthy();

  // Locate the ROM (focused first row) → the row drops, the save row remains.
  ui.tapKey(Key.Enter);
  ui.pump(10);
  ui.selectFile(LSDJ);
  ui.pump(40);
  expect(ui.findByTextContaining("Locate ROM")).toBeFalsy();
  expect(ui.findByTextContaining("Locate save")).toBeTruthy();

  // Locate the save → the load commits.
  ui.writeFile(FOUND_SAV, new Uint8Array([1, 2, 3, 4]));
  ui.tapKey(Key.Enter);
  ui.pump(10);
  ui.selectFile(FOUND_SAV);
  ui.pump(60);

  expect(ui.findByTextContaining("Locate")).toBeFalsy();
  expect(ui.findByTestId("slot-1")).toBeTruthy();
});
