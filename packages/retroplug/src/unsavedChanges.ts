// The "is anything unsaved" question the close-confirm asks: the project itself is dirty (a systems or
// settings edit since the last save/load — projectStore's flag), OR some system's live battery differs
// from its on-disk `.sav` (sramDirtyCount). Kept as a tiny pure aggregator over the stores so it's
// unit-testable without the UI, and so the close guard (ui/lvgl/useCloseGuard) stays a thin seam.

import type { ControlPlaneBackend } from "./backend";
import type { ProjectStore } from "./projectStore";
import { sramDirtyCount } from "./sramAutoSave";

/** True when there are unsaved project changes or unsaved battery SRAM. */
export function hasUnsavedChanges(backend: ControlPlaneBackend, project: ProjectStore): boolean {
  return project.isDirty() || sramDirtyCount(backend, project.systems.systems()) > 0;
}
