// Headless UI: loading a thin project whose ROM has moved shows the relink menu
// (built from the same <Menu> as the rest of the app); locating the ROM commits
// the load and a tile appears. Exercises the real PluginRpcService missing-files
// flow end to end (parse → "missing-files" event → relink → commit).
import { test, expect, ui, Key } from "ui-harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb"; // the ROM's real, found location
const PROJ = "/tmp/rp_relink_proj.rplg";

test("relink menu appears for a missing ROM and load completes after Locate", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Thin project whose SameBoy system points at a ROM that isn't there.
  ui.writeProjectJson(PROJ, "/tmp/rp_relink_gone.gb");

  // The referenced ROM is missing → the load is held and the relink menu shows.
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(40);

  expect(ui.findByTextContaining("missing")).toBeTruthy();   // menu title
  const locate = ui.findByTextContaining("Locate");
  expect(locate).toBeTruthy();

  // Activate the (focused) Locate entry, then inject the real ROM path as the
  // browser selection.
  ui.tapKey(Key.Enter);
  ui.pump(10);
  ui.selectFile(LSDJ);
  ui.pump(60);

  // Load committed: relink menu gone, a system tile present.
  expect(ui.findByTextContaining("Locate")).toBeFalsy();
  expect(ui.findByTestId("slot-1")).toBeTruthy();
});
