// The start menu can load the embedded mGB MIDI synth directly — no file
// browser — and it leaves no recent-files entry (it's pathless, see
// PluginRpcService::loadMgb). browserOpenCount() proves no dialog was opened
// (distinguishing it from the "Load…" item).
import { test, expect, ui, Key } from "ui-harness";

const LABEL = "Load mGB (Gameboy MIDI Synth)";

function focusRowContaining(substr: string, max = 20) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

test("start menu loads embedded mGB — no browser, no recent entry", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const item = ui.findByText(LABEL);
  expect(item).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);

  // Activate it — loads the embedded ROM (no file dialog).
  ui.clickAt(item!.x + (item!.width >> 1), item!.y + (item!.height >> 1));
  ui.pump(60);

  // A system loaded: the start screen is gone and the menu auto-closed.
  expect(ui.findByText("Load...")).toBe(null);
  expect(ui.findByText(LABEL)).toBe(null);
  // Crucially: no file browser was opened.
  expect(ui.browserOpenCount()).toBe(0);

  // Reopen the (instance) menu and confirm Recent stays empty — mGB is not
  // recorded in the recent list.
  ui.tapKey(Key.Esc);
  ui.pump(30);
  expect(focusRowContaining("Recent")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Recent
  ui.pump(20);
  expect(ui.findByTextContaining("No Recent Files")).toBeTruthy();
});
