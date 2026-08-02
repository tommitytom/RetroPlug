// The informational block both unsaved-changes prompts lead with: one greyed row per unsaved item naming
// the FILE a save would write, fenced off from the buttons by a separator. Pure data (no LVGL), so the
// labels + the disabled flag - the thing that keeps Enter landing on Save / Discard / Cancel - are
// assertable here rather than only on the headless display.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { unsavedRows } from "../../ui/lvgl/unsavedRows";
import { gbRom } from "../systems/fixtures";

function newProject() {
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be));
  return { be, project };
}

test("unsavedRows: nothing at all when the project is clean", () => {
  const { be, project } = newProject();
  expect(unsavedRows(be, project)).toEqual([]);
});

test("unsavedRows: a row per unsaved item, greyed + nav-skipped, closed by a separator", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  project.systems.addSystem("/roms/a.gb"); // suffix 0 -> /roms/a.sav (not on disk)
  project.systems.addSystem("/roms/a.gb"); // suffix 2 -> /roms/a-2.sav (not on disk)

  const rows = unsavedRows(be, project);
  expect(rows.map((r) => r.label)).toEqual([
    "Project: (not saved yet)", // no project file to name yet
    "Battery: a.sav (new file)",
    "Battery: a-2.sav (new file)",
    "", // the separator
  ]);
  // Every readout row is disabled (Menu greys those and skips them), and only the separator isn't an action.
  expect(rows.slice(0, 3).every((r) => r.kind === "action" && r.disabled === true)).toBeTruthy();
  expect(rows[3].kind).toBe("separator");
});

test("unsavedRows: a saved project names its file; a mirrored battery drops off", () => {
  const { be, project } = newProject();
  be.seed("/roms/a.gb", gbRom());
  const id = project.systems.addSystem("/roms/a.gb")!;
  be.seed("/roms/a.sav", be.readSram(id)!); // battery mirrored to disk
  project.save("/proj/song.rplg");
  project.setLayout("grid"); // project dirty again, battery still clean

  expect(unsavedRows(be, project).map((r) => r.label)).toEqual(["Project: song.rplg", ""]);

  be.setSram(id, new Uint8Array([1, 2, 3])); // in-game battery write: the file exists, so no "(new file)"
  expect(unsavedRows(be, project).map((r) => r.label)).toEqual(["Project: song.rplg", "Battery: a.sav", ""]);
});
