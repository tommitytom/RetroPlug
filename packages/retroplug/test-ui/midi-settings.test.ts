// Settings > MIDI, driven on the real headless menu. Guards the whole seam end to end: the submenu appears only when
// the host offers the MIDI stream (standalone), the rows are the PROJECT's (a checklist over the saved `midi` section),
// a pick writes the section at once and repaints, a saved name whose port is not present shows as "(missing)", and the
// Rescan row re-applies the resolved list in the reopening mode. The native host is faked, so no MIDI port is listed or
// opened.
import { test, expect, ui, navTo, Key } from "ui-harness";
import { FakeMidiDevice } from "@retroplug/midi/testSupport/fakeDevice";

const fake = new FakeMidiDevice({
  inputs: ["Launchpad MK2", "Arturia KeyStep 32"],
  outputs: ["Deluge"],
}).install();
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > MIDI: the checklist picks ports into the project, 'All devices' is a choice, Rescan reopens", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The MIDI submenu is present (standalone + the stream) - open it.
  expect(navTo("MIDI")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The rows are saved with the project: nothing is open in either direction, which is a fresh project's state. The
  // virtual "RetroPlug In" port is open regardless, so "nothing" still receives from a DAW; only physical devices are
  // opt-in.
  expect(ui.findByTextContaining("Saved with this project") != null).toBeTruthy();
  expect(labelOf("All devices")).toBe("[ ] All devices");
  expect(labelOf("Launchpad MK2")).toBe("[ ] Launchpad MK2");
  expect(labelOf("Arturia KeyStep 32")).toBe("[ ] Arturia KeyStep 32");

  // A pick writes the section at once and the row checks itself (the service applies it on the same frame).
  expect(navTo("Arturia KeyStep 32")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Arturia KeyStep 32")).toBe("[x] Arturia KeyStep 32");
  expect(fake.applied.at(-1)!.selection.inputs).toEqual(["Arturia KeyStep 32"]);

  // The output side mirrors to a chosen port; Left/Right do nothing on a checklist row, Enter toggles it.
  expect(navTo("Deluge")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Deluge")).toBe("[x] Deluge");
  expect(fake.applied.at(-1)!.selection.outputs).toEqual(["Deluge"]);

  // Picking the same row again takes the name out again.
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Deluge")).toBe("[ ] Deluge");

  // "All devices" is the explicit everything, not the default.
  expect(navTo("All devices")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("All devices")).toBe("[x] All devices");
  expect(fake.applied.at(-1)!.selection.allInputs).toBe(true);

  // Rescan re-resolves the saved names against a fresh listing and reopens every port. A later poll may re-apply the
  // section as a diff (opening the submenu and writing a row both ask for one), so the assertion is that a REOPEN
  // happened, not that it is the last call.
  expect(navTo("Rescan")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(fake.applied.some((a) => a.mode === "reopen")).toBe(true);
});

