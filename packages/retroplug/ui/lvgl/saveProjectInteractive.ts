// saveProjectInteractive — flush dirty SRAM, then save the project, opening a Save-As dialog when the
// project has no path yet. Returns true if it saved, false if the Save-As was cancelled. Shared by the
// close guard (Save & Quit) and the New/Load discard guard (Save then proceed).

import type { AppStores } from "../../src/appStores";
import { flushDirtySram } from "../../src/sramAutoSave";

const PROJECT_SAVE_PATTERNS = ["*.rplg"];

export async function saveProjectInteractive(stores: AppStores): Promise<boolean> {
  flushDirtySram(stores.backend, stores.project.systems.systems()); // battery → sibling .sav (ungated)
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
  stores.project.save(path);
  return true;
}
