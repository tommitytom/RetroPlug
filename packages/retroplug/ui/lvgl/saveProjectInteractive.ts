// saveProjectInteractive — resolve a path (opening a Save-As dialog when the project has no path yet),
// save every unsaved working song into its cart's battery, flush dirty SRAM and save the project. Returns
// true if EVERYTHING saved; false if the Save-As was cancelled, or a working song could not be stored (a
// full cart, an older save format) - the rest is still saved, and the caller keeps its prompt up, which
// now lists only that song and why. Every route that saves a project goes through here - the close guard (Save & Quit), the
// New/Load discard guard, and the Project + instance menu Save rows — because saving a project and
// saving the batteries it contains are one user action, not two.

import type { AppStores } from "../../src/appStores";
import { flushDirtySram } from "../../src/sramAutoSave";
import { saveWorkingSong } from "../../src/tracker/liveSav";

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
  // Working songs FIRST: one that lives outside the battery (smsggdj) is saved INTO it, and the flush
  // below is what then carries it to the `.sav`. The other way round, the flush writes the old battery.
  const systems = stores.project.systems;
  let songsSaved = true;
  for (const sys of systems.systems()) {
    const r = await saveWorkingSong(stores.backend, systems, sys);
    if (r.kind === "refused") {
      console.warn(`[project] working song on system ${sys.id} not saved: ${r.reason}`);
      songsSaved = false;
    }
  }
  flushDirtySram(stores.backend, systems.systems()); // battery → sibling .sav
  stores.project.save(path);
  return songsSaved;
}
