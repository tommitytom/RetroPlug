// Headless UI: "Load ROM" beside an existing <rom>.rplg opens that PROJECT
// instead of building a bare single-system project — the sibling-.rplg deferral
// that used to live in native loadRomFromPath now runs in TS
// (project/romBuild.ts onRomPathSelected -> startLoad). Proof: the sibling .rplg
// references a MISSING ROM, so deferring to the project loader surfaces the
// relink menu; a bare build of the *selected* ROM would just make a tile.
import { test, expect, ui } from "ui-harness";

const REAL_ROM = "resources/roms/mGB.gb";
const ROM  = "/tmp/rp_sibling.gb";        // a valid ROM the browser picks
const RPLG = "/tmp/rp_sibling.rplg";      // its sibling project (references a gone ROM)

test("Load ROM beside a sibling .rplg defers to the project loader", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // A real ROM on disk so native content-detects it and emits "rom-path-selected",
  // plus a sibling project pointing at a ROM that isn't there.
  ui.writeFile(ROM, ui.readFile(REAL_ROM));
  ui.writeProjectJson(RPLG, "/tmp/rp_sibling_gone.gb");

  // Start menu "Load..." opens the ROM browser; pick the ROM next to the .rplg.
  const load = ui.findByText("Load...");
  expect(load).toBeTruthy();
  ui.clickAt(load!.x + (load!.width >> 1), load!.y + (load!.height >> 1));
  ui.pump(20);
  expect(ui.browserOpenCount()).toBe(1);

  ui.selectFile(ROM);
  ui.pump(40);

  // Deferral fired: startLoad(<rom>.rplg) found its missing ROM → relink menu.
  // (A bare constructSystem build of the selected ROM would show a tile, not this.)
  expect(ui.findByTextContaining("Locate")).toBeTruthy();
  expect(ui.findByTextContaining("missing")).toBeTruthy();
});
