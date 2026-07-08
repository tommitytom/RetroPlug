// Confirm-on-close, end to end on the headless display. The editor's native onClose() calls
// __rp_onCloseRequested; this test drives that seam directly (same JS context as the UI bundle, like
// resize.test.ts spies __rp_setWindowSize). A clean project lets the close through; once a tile is added
// (project dirty) the request is vetoed and the Save/Discard/Cancel overlay appears. Esc cancels; Discard
// & Quit calls __rp_quitWindow (spied here — natively it closes the window).

import { test, expect, ui, Key } from "ui-harness";

const onCloseRequested = (): boolean =>
  (globalThis as unknown as { __rp_onCloseRequested?: () => boolean }).__rp_onCloseRequested?.() ?? false;

test("close is vetoed with unsaved changes; Esc cancels, Discard quits", () => {
  let quitCalls = 0;
  (globalThis as unknown as { __rp_quitWindow?: () => void }).__rp_quitWindow = () => {
    quitCalls++;
  };

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Clean start (empty project): the seam is installed and a close request is ALLOWED (no veto, no overlay).
  expect(onCloseRequested()).toBe(false);
  expect(ui.findByTextContaining("Unsaved changes")).toBe(null);

  // Load mGB (focused start-menu row) → adds a tile and marks the project dirty.
  expect(ui.findByTextContaining("Load mGB") != null).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // A close request now VETOES (returns true) and raises the prompt with all three choices.
  expect(onCloseRequested()).toBe(true);
  ui.pump(10);
  expect(ui.findByTextContaining("Unsaved changes") != null).toBeTruthy();
  expect(ui.findByTextContaining("Save & Quit") != null).toBeTruthy();
  expect(ui.findByTextContaining("Discard & Quit") != null).toBeTruthy();
  expect(ui.findByTextContaining("Cancel") != null).toBeTruthy();
  expect(quitCalls).toBe(0); // nothing closed yet

  // Esc cancels the prompt (window stays open, project untouched).
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Unsaved changes")).toBe(null);
  expect(quitCalls).toBe(0);

  // Re-request, then Discard & Quit (Down from the focused Save & Quit, Enter) → __rp_quitWindow fires.
  expect(onCloseRequested()).toBe(true);
  ui.pump(10);
  ui.tapKey(Key.Down);
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(quitCalls).toBe(1);
  expect(ui.findByTextContaining("Unsaved changes")).toBe(null); // dismissed on discard
});
