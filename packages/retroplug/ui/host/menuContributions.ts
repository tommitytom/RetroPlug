// The product's UI contributions (docs/modular-graph-phase-f4b-design.md section 4.2), for the shell's menu composers:
// the generated list, in product order, is the one place a package's rows, builders, tracker specs and services are
// read from. Only ui/ imports it, so the control plane, the render worker, the SDK and the planner carry no menu
// contribution; the generated module has already refused a duplicate row id, an unknown placement, a row over a role
// kind no registered type declares or over a key its schema does not read, and a tracker UI no integration matches, so
// what this module reads it can index.
import type { RoleRow, TrackerUi } from "@retroplug/ui/contribution";
import { UI_CONTRIBUTIONS } from "../generated/menus";

/** Every contributed role row, in product order: the System submenu's package-owned block. */
export function roleRows(): readonly RoleRow[] {
  return UI_CONTRIBUTIONS.flatMap((entry) => entry.ui.rows ?? []);
}

/** The tracker UI a cart contributes, by its integration id, or undefined for a cart with no contributed specs. */
export function contributedTrackerUi(id: string): TrackerUi | undefined {
  for (const entry of UI_CONTRIBUTIONS) {
    for (const tracker of entry.ui.trackers ?? []) {
      if (tracker.id === id) return tracker;
    }
  }
  return undefined;
}
