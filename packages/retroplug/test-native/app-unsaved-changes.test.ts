// The unsaved-changes readout against a REAL SameBoy core. The mock tier covers the list logic and the
// row labels; what only a real core can show is where the battery half comes from: the cart publishes its
// live SRAM to the snapshot registry, and unsavedChanges compares THOSE bytes against the `.sav` on disk.
// A mock backend hands back whatever a test set, so it can't prove the comparison is reading a real
// battery at all.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { buildAppRegistry } from "../src/appHost";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { unsavedChanges } from "../src/unsavedChanges";
import { flushDirtySram } from "../src/sramAutoSave";
import { savFrom, type SavInput } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";

const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };

test("an unsaved battery is listed with the .sav it would write; flushing it clears the list", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP app-unsaved-changes: LSDj ROM not found at ${LSDJ}`);
    return; // resource-less environment - the devcontainer has it
  }

  // A private copy of the cart, with no `.sav` beside it yet.
  const rom = __CONFIG_DIR__ + "/unsaved-changes.gb";
  const sav = __CONFIG_DIR__ + "/unsaved-changes.sav";
  const rplg = __CONFIG_DIR__ + "/unsaved-changes.rplg";
  be.deleteFile(sav); // ensure absent (ignore result)
  expect(be.writeFile(rom, be.readFile(LSDJ)!)).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const id = project.systems.addSystem(rom)!;
  expect(typeof id).toBe("number");
  project.save(rplg); // project clean; the cart's seeded battery has never been written

  // The live battery (published by the real core) has no `.sav` on disk: one row, flagged as a new file.
  expect(unsavedChanges(be, project)).toEqual([{ kind: "sram", id, savPath: sav, isNew: true }]);

  expect(flushDirtySram(be, project.systems.systems())).toBe(1); // what "Save" does for a battery
  expect(unsavedChanges(be, project)).toEqual([]); // mirrored - nothing left to save

  // An external battery write (what the cart does as you work) puts the row back, now naming an EXISTING
  // file rather than a new one.
  const other = savFrom({ activeProjectIndex: 0, projects: [{ name: "OTHER", version: 0, song: SONG }] } as SavInput);
  expect(be.writeFile(sav, other)).toBeTruthy();
  expect(unsavedChanges(be, project)).toEqual([{ kind: "sram", id, savPath: sav, isNew: false }]);
});
