// saveProjectInteractive — resolve a path (opening a Save-As dialog when the project has no path yet),
// then flush dirty SRAM and save the project. Returns true if it saved, false if the Save-As was
// cancelled. Every route that saves a project goes through here — the close guard (Save & Quit), the
// New/Load discard guard, and the Project + instance menu Save rows — because saving a project and
// saving the batteries it contains are one user action, not two.

import type { AppStores } from "../../src/appStores";
import { flushDirtySram } from "../../src/sramAutoSave";

const PROJECT_SAVE_PATTERNS = ["*.rplg"];

export async function saveProjectInteractive(stores: AppStores): Promise<boolean> {
  let path = stores.project.currentPath();
  if (!path) {
    path =
      (await stores.backend.openFileBrowser({
        title: "Save Project",
        patterns: PROJECT_SAVE_PATTERNS,
        saving: true,
        defaultName: "project.rplg",
      })) ?? "";
    if (!path) return false; // Save-As cancelled
  }
  // AFTER the cancel check: a dismissed Save-As should leave the disk exactly as it found it, and this
  // used to write every dirty battery before the user had agreed to save anything.
  flushDirtySram(stores.backend, stores.project.systems.systems()); // battery → sibling .sav
  stores.project.save(path);
  return true;
}
