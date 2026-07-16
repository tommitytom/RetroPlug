// hasUnsavedChanges — the close-confirm's "is anything unsaved" aggregate over the real ProjectStore +
// mock backend: project-dirty (a systems/settings edit) OR a live battery that differs from its on-disk
// .sav. Proves each channel independently (a clean project can still be SRAM-dirty, and vice versa).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { hasUnsavedChanges } from "../../src/unsavedChanges";
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
