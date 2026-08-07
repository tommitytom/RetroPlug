// The close-confirm's two views of "is anything unsaved", over the real ProjectStore + mock backend:
// hasUnsavedChanges (the yes/no gate) and unsavedChanges (the ITEMS the prompt lists). Both read the same
// two channels - project-dirty (a systems/settings edit) and a live battery that differs from its on-disk
// .sav - and each is proven independently (a clean project can still be SRAM-dirty, and vice versa).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { hasUnsavedChanges, unsavedChanges } from "../../src/unsavedChanges";
import { gbRom } from "../systems/fixtures";

function newProject() {
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be));
  return { be, project };
}

test("a clean empty project has no unsaved changes", () => {
  const { be, project } = newProject();
  expect(hasUnsavedChanges(be, project)).toBeFalsy();
});

test("a project edit is unsaved (project-dirty channel)", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  project.systems.addSystem("/roms/a.gb"); // structural edit → project dirty
  expect(project.isDirty()).toBeTruthy();
  expect(hasUnsavedChanges(be, project)).toBeTruthy();
});

test("a clean project with a battery differing from its .sav is unsaved (SRAM channel)", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const id = project.systems.addSystem("/roms/a.gb")!;
  be.seed("/roms/a.sav", be.readSram(id)!); // battery already mirrored to disk
  project.save("/roms/a.rplg"); // project now clean
  expect(hasUnsavedChanges(be, project)).toBeFalsy(); // clean on both channels

  be.setSram(id, new Uint8Array([1, 2, 3])); // in-game battery write, no project edit
  expect(project.isDirty()).toBeFalsy(); // the project itself is unchanged
  expect(hasUnsavedChanges(be, project)).toBeTruthy(); // but the battery is unsaved
});

test("unsavedChanges: nothing to list when the project is clean", () => {
  const { be, project } = newProject();
  expect(unsavedChanges(be, project)).toEqual([]);
});

test("unsavedChanges: an unsaved project names its file, or reports that it has none yet", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const id = project.systems.addSystem("/roms/a.gb")!;
  be.seed("/roms/a.sav", be.readSram(id)!); // battery mirrored, so only the project channel is dirty

  expect(unsavedChanges(be, project)).toEqual([{ kind: "project", path: "" }]); // never saved

  project.save("/proj/song.rplg");
  project.setLayout("grid"); // dirty again, but now it HAS a file
  expect(unsavedChanges(be, project)).toEqual([{ kind: "project", path: "/proj/song.rplg" }]);
});

test("unsavedChanges: an unsaved battery names the .sav it would write, flagging one not on disk yet", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const id = project.systems.addSystem("/roms/a.gb")!;
  project.save("/roms/a.rplg"); // project clean; no /roms/a.sav on disk

  // A battery with no .sav yet: a save would CREATE the file.
  expect(unsavedChanges(be, project)).toEqual([{ kind: "sram", id, savPath: "/roms/a.sav", isNew: true }]);

  be.seed("/roms/a.sav", be.readSram(id)!); // mirrored → clean
  expect(unsavedChanges(be, project)).toEqual([]);

  be.setSram(id, new Uint8Array([1, 2, 3])); // in-game battery write → differs from the existing file
  expect(unsavedChanges(be, project)).toEqual([{ kind: "sram", id, savPath: "/roms/a.sav", isNew: false }]);
});

test("unsavedChanges: the project leads, then one row per unsaved battery in systems order", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const first = project.systems.addSystem("/roms/a.gb")!; // suffix 0 -> /roms/a.sav
  const second = project.systems.addSystem("/roms/a.gb")!; // suffix 2 -> /roms/a-2.sav
  be.seed("/roms/a.sav", be.readSram(first)!);
  project.save("/roms/a.rplg");
  project.setZoom(4); // project dirty again

  expect(unsavedChanges(be, project)).toEqual([
    { kind: "project", path: "/roms/a.rplg" },
    { kind: "sram", id: second, savPath: "/roms/a-2.sav", isNew: true }, // `first` is mirrored, so absent
  ]);
});
