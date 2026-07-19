// The LSDj runtime debug overlay, end to end on the headless display. The overlay is a developer aid,
// HIDDEN by default and toggled with the backtick key (App → toggleLsdjDebug). We load a real LSDj ROM
// (auto-attaches the lsdj-sync role, so useLsdjRuntime engages), then assert:
//   - it is absent by default (not shown to players);
//   - backtick reveals it (the WRAM reader resolves a supported layout → the readout renders);
//   - backtick again hides it.
// SKIPs when no LSDj ROM was staged (a large external asset — see run-ui-tests.mjs).

import { test, expect, ui } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60; // ` — the debug toggle (raw DPF code on the "key" bus)

test("the LSDj overlay is hidden by default and toggles on/off with backtick", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Load a real LSDj ROM by dropping it on the start screen. Absent (unstaged) → no tile → SKIP.
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) {
    console.log("# SKIP lsdj-overlay: no LSDj ROM staged in romDir");
    return;
  }

  // Boot far enough that the core is running and readRam yields live WRAM.
  ui.advance(1500);
  ui.pump(10);

  // Hidden by default — the overlay must not show unprompted.
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);

  // Backtick reveals it: the reader identifies v9.4.2 + resolves a layout, so the readout renders.
  ui.tapKey(BACKTICK);
  ui.advance(300); // a frame tick drives useLsdjRuntime to read + decode WRAM
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // Backtick again hides it.
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
});
