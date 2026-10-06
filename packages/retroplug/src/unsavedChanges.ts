// What the close-confirm asks about: the project itself is dirty (a systems or settings edit since the
// last save/load - projectStore's flag), some system's live battery differs from its on-disk `.sav`
// (dirtySramTargets), and/or some cart holds an unsaved working song that is in NEITHER - smsggdj keeps
// its working song in work RAM, which no project file and no battery flush ever captures, so without
// the third channel a closed window took an hour's work with it and asked nothing. Two shapes over the same two channels: `hasUnsavedChanges` for the yes/no gate, and
// `unsavedChanges` for the ITEMS the prompt lists, each naming the file a save would write. Kept as tiny
// pure aggregators over the stores so they're unit-testable without the UI, and so the close guard
// (ui/lvgl/useCloseGuard) stays a thin seam.

import type { ControlPlaneBackend } from "./backend";
import type { ProjectStore } from "./projectStore";
import { dirtySramTargets, sramDirtyCount } from "./sramAutoSave";
import { resolveSavPath } from "./savPaths";
import { resolveSongCatalog } from "./tracker/trackerIntegration";
import { savEditWouldDiscard } from "./tracker/liveSav";

/** One unsaved thing: the project file, or one system's battery. `path` is "" for a project that has
 *  never been saved (a save would ask where); `isNew` marks a battery whose `.sav` isn't on disk yet. */
export type UnsavedItem =
  | { kind: "project"; path: string }
  | { kind: "sram"; id: number; savPath: string; isNew: boolean }
  // A working song outside the battery. Save writes it INTO the battery (then the `.sav`), so `savPath`
  // is where it ends up; `blocked` is why Save cannot store it, when it cannot.
  | { kind: "song"; id: number; name: string; savPath: string; blocked?: "full" | "format" | "corrupt" };

/** True when there are unsaved project changes or unsaved battery SRAM. Deliberately short-circuits on the
 *  project flag: this runs on EVERY menu rebuild (the Save Project row's " *"), so it must not always pay
 *  for the battery scan. Use `unsavedChanges` when the DETAIL is wanted - only a prompt needs that. */
export function hasUnsavedChanges(backend: ControlPlaneBackend, project: ProjectStore): boolean {
  return (
    project.isDirty() ||
    sramDirtyCount(backend, project.systems.systems()) > 0 ||
    project.systems.systems().some((sys) => savEditWouldDiscard(project.systems, sys))
  );
}

/** Everything currently unsaved, in the order a prompt should list it: the project first, then one entry
 *  per system with an unsaved battery (in systems order). Empty when there's nothing to save. */
export function unsavedChanges(backend: ControlPlaneBackend, project: ProjectStore): UnsavedItem[] {
  const items: UnsavedItem[] = project.isDirty() ? [{ kind: "project", path: project.currentPath() }] : [];
  for (const t of dirtySramTargets(backend, project.systems.systems())) {
    items.push({ kind: "sram", id: t.id, savPath: t.savPath, isNew: t.isNew });
  }
  for (const sys of project.systems.systems()) {
    if (!savEditWouldDiscard(project.systems, sys)) continue;
    const song = project.systems.workingSongImage(sys.id);
    const sram = project.systems.readSram(sys.id);
    const commit = song && sram ? resolveSongCatalog(sys.roles)?.commitWorkingSong?.(sram, song) : undefined;
    items.push({
      kind: "song",
      id: sys.id,
      name: song ? songName(song.name) : "",
      savPath: resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath),
      // A dry run of exactly what Save will do, so the prompt can say up front that it cannot, rather than
      // the user finding out by the song still being listed after they pressed it.
      ...(commit && !commit.ok ? { blocked: commit.reason } : {}),
    });
  }
  return items;
}

/** The name a cart shows for its working song: its raw name bytes up to the first NUL, padding dropped. */
function songName(bytes: Uint8Array): string {
  let s = "";
  for (const c of bytes) {
    if (c === 0) break;
    s += c >= 0x20 && c <= 0x7e ? String.fromCharCode(c) : "?";
  }
  return s.trim();
}
