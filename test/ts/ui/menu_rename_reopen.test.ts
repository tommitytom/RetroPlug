// Regression: confirming a menu prompt with Enter must NOT re-open it.
//
// A prompt overlay is driven by the global "key" channel (Enter -> confirm ->
// close), but the underlying menu row still holds LVGL focus, so the same Enter
// also fires LV_EVENT_CLICKED -> onClick -> activate() once the key is released.
// If the prompt has already closed by then, activate() re-opens it: the dialog
// "closes then immediately reappears". The Recent > <entry> > Rename... prompt
// is the clean repro — the row persists across the rename so the reopened
// overlay is observable.
import { test, expect, ui, Key } from "ui-harness";

const PRESENT = "/tmp/rp_rename_present.rplg";

function clickCenter(w: { x: number; y: number; width: number; height: number }) {
  ui.clickAt(w.x + (w.width >> 1), w.y + (w.height >> 1));
}

function ensureExpanded(headerSubstr: string) {
  const row = ui.findByTextContaining(headerSubstr);
  expect(row).toBeTruthy();
  if (row!.text.trim().endsWith(">")) {
    clickCenter(row!);
    ui.pump(20);
  }
}

// The open prompt's title is `Rename "<name>" to:` — unique vs the "Rename..."
// menu row, and present whether the prompt shows the hint or an error.
const PROMPT_OPEN = '" to:';

test("Enter to confirm a rename closes the prompt and does not reopen it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  ui.writeProjectJson(PRESENT, "/tmp/rp_rename_rom.gb");
  ui.seedRecent(PRESENT);
  ui.pump(30);

  ensureExpanded("Recent");
  ensureExpanded("rp_rename_present.rplg");

  const rename = ui.findByText("Rename...");
  expect(rename).toBeTruthy();
  clickCenter(rename!);
  ui.pump(20);
  expect(ui.findByTextContaining(PROMPT_OPEN)).toBeTruthy(); // prompt open

  ui.tapKey(Key.Enter); // confirm
  ui.pump(60);

  // Pre-fix: the Enter's trailing CLICKED re-activated the row and the prompt
  // is back (a fresh overlay, hint shown). Post-fix: it stays closed.
  expect(ui.findByTextContaining(PROMPT_OPEN)).toBe(null);
});
