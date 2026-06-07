// Headless UI: loading a ROM mounts an emulator tile showing the live GB frame.
// (TS port of the C++ PoC's second case.)
import { test, expect, ui, CompType, isFlat } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

test("loading a ROM mounts an emulator tile", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  expect(ui.findByTestId("slot-0")).toBe(null); // no tiles before any ROM

  const id = ui.loadRom(MGB);
  ui.pump(60); // mount the SystemGrid + tile, poll a few frames

  // The per-system slot wrapper was tagged via the UI ref -> __rp_tagTestId.
  expect(ui.findByTestId(`slot-${id}`)).toBeTruthy();
  // The EmulatorTile renders its framebuffer through a <Canvas> (lv_image).
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(0);
  expect(isFlat(ui.snapshot())).toBeFalsy();

  expect(ui.snapshotPng("/tmp/ui-ts-tile.png")).toBeTruthy();
});
