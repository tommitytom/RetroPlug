// The Recent submenu's per-row hotkeys, end to end on the headless display. Each recent entry is now a
// single action row (Enter loads); F2 renames it and Del removes it. This proves the Menu's key wiring
// reaches the row's onRename / onDelete (the piece the pure-TS menu tests can't exercise). We seed a recent
// by saving a freshly loaded mGB through the stubbed Save-As browser, then drive F2 / Del on the entry.

import { test, expect, ui, navTo, Key } from "ui-harness";

const KEY_F2 = 0xe001; // DPF kKeyF1 + 1
const KEY_DELETE = 0x7f; // DPF Delete

test("Recent rows: F2 opens the rename prompt, Del removes the entry", () => {
  const g = globalThis as {
    __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string, sd: string, dir: boolean) => void;
    __rp_onFileBrowserResult?: (path: string | null) => void;
  };
  // Stub the native browser: Save-As resolves to a fixed .rplg path (→ project.save → a recents entry).
  g.__rp_openFileBrowser = () => g.__rp_onFileBrowserResult?.("/tmp/rp-recent.rplg");

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Start menu → Load mGB → the grid. mGB has no path, so Save Project opens Save-As.
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  // Instance menu → Save Project → the stubbed Save-As seeds one recents entry.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Save Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  // Reopen the instance menu → expand Recent → focus its first (only) entry.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Recent")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand the submenu
  ui.pump(6);
  ui.tapKey(Key.Down); // move onto the first recent row
  ui.pump(6);
  const entryLabel = ui.focused()!.text; // the row's label (project-name / file stem), captured to assert removal
  expect(entryLabel.length > 0).toBeTruthy();

  // F2 → the rename prompt overlay opens (its title is `Rename "<name>" to:`).
  ui.tapKey(KEY_F2);
  ui.pump(6);
  expect(ui.findByTextContaining("Rename") != null).toBeTruthy();
  ui.tapKey(Key.Esc); // cancel the prompt; the menu stays open, focus still on the row
  ui.pump(6);
  expect(ui.findByTextContaining("Rename")).toBe(null);

  // Del → the entry is removed; the Recent submenu (still expanded) falls back to its empty placeholder.
  ui.tapKey(KEY_DELETE);
  ui.pump(10);
  expect(ui.findByText(entryLabel)).toBe(null); // the row is gone
  expect(ui.findByTextContaining("No Recent Files") != null).toBeTruthy();

  delete g.__rp_openFileBrowser;
});
