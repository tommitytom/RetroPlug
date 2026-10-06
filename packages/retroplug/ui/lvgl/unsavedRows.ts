// The "what exactly is unsaved" block both unsaved-changes prompts lead with - the close guard (App) and
// the New / Load discard confirm (useProjectModals). One informational row per unsaved item, naming the
// FILE a save would write, then a separator fencing them off from the Save / Discard / Cancel buttons.
//
// Every row is `disabled`: Menu greys those and skips them in nav, so Enter still lands on the first
// button exactly as it did before the block existed. Pure data (no LVGL), so the labels are unit-testable.

import type { ControlPlaneBackend } from "../../src/backend";
import type { ProjectStore } from "../../src/projectStore";
import { unsavedChanges, type UnsavedItem } from "../../src/unsavedChanges";
import { basename } from "../../src/pathUtil";
import type { MenuItem } from "../screens/menu/menuTree";

/** Why Save cannot store a working song, as the row says it. */
const SONG_BLOCKED: Record<"full" | "format" | "corrupt", string> = {
  full: "cart full, can't save",
  format: "old save format, can't save",
  corrupt: "damaged save, can't save",
};

/** The row text for one unsaved item: "Project: song.rplg" / "Project: (not saved yet)" /
 *  "Battery: lsdj.sav" / "Battery: lsdj-2.sav (new file)" / "Song: MYSONG -> smsggdj.sav" /
 *  "Song: MYSONG (cart full, can't save)". */
export function unsavedLabel(item: UnsavedItem): string {
  if (item.kind === "project") return `Project: ${basename(item.path) || "(not saved yet)"}`;
  if (item.kind === "song") {
    const name = item.name || "(untitled)";
    return item.blocked ? `Song: ${name} (${SONG_BLOCKED[item.blocked]})` : `Song: ${name} -> ${basename(item.savPath)}`;
  }
  return `Battery: ${basename(item.savPath)}${item.isNew ? " (new file)" : ""}`;
}

/** The informational rows + their trailing separator, to prepend to a prompt's buttons. Empty when nothing
 *  is unsaved, so the prompt degrades to exactly its buttons. */
export function unsavedRows(backend: ControlPlaneBackend, project: ProjectStore): MenuItem[] {
  const items = unsavedChanges(backend, project);
  if (items.length === 0) return [];
  const rows: MenuItem[] = items.map((item, i) => ({
    id: `unsaved-${i}`,
    label: unsavedLabel(item),
    kind: "action",
    disabled: true, // greyed + skipped by nav: a readout, not a choice
    onSelect: () => {},
  }));
  rows.push({ id: "unsaved-sep", label: "", kind: "separator" });
  return rows;
}
