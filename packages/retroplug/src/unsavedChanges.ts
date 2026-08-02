// What the close-confirm asks about: the project itself is dirty (a systems or settings edit since the
// last save/load - projectStore's flag), and/or some system's live battery differs from its on-disk `.sav`
// (dirtySramTargets). Two shapes over the same two channels: `hasUnsavedChanges` for the yes/no gate, and
// `unsavedChanges` for the ITEMS the prompt lists, each naming the file a save would write. Kept as tiny
// pure aggregators over the stores so they're unit-testable without the UI, and so the close guard
// (ui/lvgl/useCloseGuard) stays a thin seam.

import type { ControlPlaneBackend } from "./backend";
import type { ProjectStore } from "./projectStore";
import { dirtySramTargets, sramDirtyCount } from "./sramAutoSave";

/** One unsaved thing: the project file, or one system's battery. `path` is "" for a project that has
 *  never been saved (a save would ask where); `isNew` marks a battery whose `.sav` isn't on disk yet. */
export type UnsavedItem =
  | { kind: "project"; path: string }
  | { kind: "sram"; id: number; savPath: string; isNew: boolean };

/** True when there are unsaved project changes or unsaved battery SRAM. Deliberately short-circuits on the
 *  project flag: this runs on EVERY menu rebuild (the Save Project row's " *"), so it must not always pay
 *  for the battery scan. Use `unsavedChanges` when the DETAIL is wanted - only a prompt needs that. */
export function hasUnsavedChanges(backend: ControlPlaneBackend, project: ProjectStore): boolean {
  return project.isDirty() || sramDirtyCount(backend, project.systems.systems()) > 0;
}

/** Everything currently unsaved, in the order a prompt should list it: the project first, then one entry
 *  per system with an unsaved battery (in systems order). Empty when there's nothing to save. */
export function unsavedChanges(backend: ControlPlaneBackend, project: ProjectStore): UnsavedItem[] {
  const items: UnsavedItem[] = project.isDirty() ? [{ kind: "project", path: project.currentPath() }] : [];
  for (const t of dirtySramTargets(backend, project.systems.systems())) {
    items.push({ kind: "sram", id: t.id, savPath: t.savPath, isNew: t.isNew });
  }
  return items;
}
