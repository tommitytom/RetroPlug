// The risa runtime debug overlay, end to end on the headless display — the risa twin of lsdj-overlay.
// HIDDEN by default, toggled with backtick (App → toggleLsdjDebug, shared). We load a real risa ROM
// (auto-attaches the `risa` role, so useRisaRuntime engages + identifies the version from the PRG), then
// assert: absent by default; backtick reveals the readout (even idle risa decodes a `supported` state);
// backtick again hides it. SKIPs when no risa ROM was staged (see run-ui-tests.mjs).
import { test, expect, ui } from "ui-harness";

const RISA = () => ui.romDir() + "/risa.nes";
const BACKTICK = 0x60; // ` — the debug toggle

test("the risa overlay is hidden by default and toggles on/off with backtick", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  ui.fileDrop(RISA(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) {
    console.log("# SKIP risa-overlay: no risa ROM staged in romDir");
    return;
  }

  // Boot far enough that the core is running and readRam yields live internal RAM.
  ui.advance(1500);
  ui.pump(10);

  // Hidden by default.
  expect(ui.findByTestId("risa-overlay")).toBe(null);

  // Backtick reveals it: useRisaRuntime identifies v2.2.1 from the PRG + resolves the layout, so even the
  // idle (stopped) state renders as a supported readout.
  ui.tapKey(BACKTICK);
  ui.advance(300); // a frame tick drives useRisaRuntime to read + decode RAM
  ui.pump(10);
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();

  // Backtick again hides it.
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByTestId("risa-overlay")).toBe(null);
});
