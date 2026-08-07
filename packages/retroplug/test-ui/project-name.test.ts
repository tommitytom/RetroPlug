// Project > Name, end to end on the headless display. A project starts nameless: the row shows the name
// DERIVED from the instance (the mGB ROM's stem), flagged "(auto)". Arming the prompt opens an EMPTY field
// (the derived name is a fallback, not a seed), typing a name adopts it, and the window title - which reads
// the same display name - follows. This proves the Menu prompt wiring reaches ProjectStore.setName, which
// the pure-TS menu tests drive directly.

import { test, expect, ui, navTo, Key } from "ui-harness";

const Z = "z".charCodeAt(0); // an unshifted letter, as DPF delivers it on the key bus

test("Project > Name shows the derived name until the user sets one, then adopts what they typed", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  const titles: string[] = [];
  (globalThis as { __rp_setWindowTitle?: (t: string) => void }).__rp_setWindowTitle = (t) => {
    titles.push(t);
  };

  // Drop a real ROM → a disk-backed system named "mGB" by derivation (its rom stem).
  ui.fileDrop(ui.romDir() + "/mGB.gb", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Instance menu → Project → the Name row reads the derived name, marked automatic. "Project" is a
  // substring of the Save / New rows above it, so step past those before nav'ing to the submenu itself.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(navTo("System")).toBeTruthy(); // the row directly above the Project submenu
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text).toBe("Project >"); // the submenu row (" >" is the collapsed marker)
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Name: mGB (auto)")).toBeTruthy();

  // Enter arms the prompt. The field (rendered "<value>_") starts EMPTY - the derived name is a fallback,
  // never a seed, so what the user types isn't appended to "mGB".
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.findByTextContaining("Project name:") != null).toBeTruthy();
  expect(ui.findByText("_") != null).toBeTruthy();

  // Type "zz" and confirm → the project takes that name.
  ui.tapKey(Z);
  ui.tapKey(Z);
  ui.pump(4);
  expect(ui.findByText("zz_") != null).toBeTruthy(); // just what was typed - no "mGB" prefix
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTextContaining("Name: zz") != null).toBeTruthy(); // no "(auto)" - it's the project's own name

  // The window title follows the same display name (mGB carries no tracker song to supersede it).
  const last = titles[titles.length - 1];
  expect(!!last && last.endsWith(" - zz")).toBeTruthy();

  delete (globalThis as { __rp_setWindowTitle?: unknown }).__rp_setWindowTitle;
});
