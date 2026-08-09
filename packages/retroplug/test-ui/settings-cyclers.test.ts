// Settings cyclers must actually toggle. Regression for a real bug: `useNativeFileDialogs` was in the config
// schema + had a setter, but was omitted from serializeUserConfig — so commit() (which diffs serializations to
// detect a real change) saw the "File Dialogs" toggle as a no-op, and the setting could never be changed or
// persisted. Drive the cycler on the real headless menu and assert its label flips.

import { test, expect, ui, Key } from "ui-harness";

function navTo(substr: string, maxSteps = 30): boolean {
  for (let i = 0; i < maxSteps; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return true;
    ui.tapKey(Key.Down);
    ui.pump(2);
  }
  const f = ui.focused();
  return !!f && f.text.includes(substr);
}

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > File Dialogs cycler actually toggles In-App <-> OS Native", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Starts at the default (OS Native).
  expect(labelOf("File Dialogs")).toBe("File Dialogs: OS Native");

  // Enter wraps it forward → In-App.
  expect(navTo("File Dialogs")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("File Dialogs")).toBe("File Dialogs: In-App");

  // Enter again steps back → OS Native (proves it's a live two-way toggle, not a one-shot write).
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("File Dialogs")).toBe("File Dialogs: OS Native");

  // Left/Right cycle it too.
  ui.tapKey(Key.Right);
  ui.pump(8);
  expect(labelOf("File Dialogs")).toBe("File Dialogs: In-App");
  ui.tapKey(Key.Left);
  ui.pump(8);
  expect(labelOf("File Dialogs")).toBe("File Dialogs: OS Native");
});
