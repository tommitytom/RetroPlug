// Headless UI: the Recent menu lists projects, each an expandable submenu with
// Load / Rename / Remove (plus Locate when the project file is missing). Seeded
// via ui.seedRecent (the stand-in for the real load path recording a recent),
// which writes the per-harness RecentFiles and refreshes the menu.
//
// Submenu open/close state persists across test cases in a file, so every
// expansion here is idempotent: it clicks only when the row shows the collapsed
// ">" glyph (Menu.tsx appends " v" when open, " >" when closed).
import { test, expect, ui, Key } from "ui-harness";

const PRESENT = "/tmp/rp_recent_present.rplg";
const MISSING = "/tmp/rp_recent_missing.rplg";  // deliberately never written to disk
const SECOND  = "/tmp/rp_recent_second.rplg";

function clickCenter(w: { x: number; y: number; width: number; height: number }) {
  ui.clickAt(w.x + (w.width >> 1), w.y + (w.height >> 1));
}

// Expand a submenu row (idempotent): click only when collapsed.
function ensureExpanded(headerSubstr: string) {
  const row = ui.findByTextContaining(headerSubstr);
  expect(row).toBeTruthy();
  if (row!.text.trim().endsWith(">")) {
    clickCenter(row!);
    ui.pump(20);
  }
}

test("recent entries are submenus; missing projects are marked and offer Locate", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // A present project (its .rplg exists) and a missing one (never written).
  ui.writeProjectJson(PRESENT, "/tmp/rp_recent_rom.gb");
  ui.seedRecent(MISSING);   // missing -> front
  ui.seedRecent(PRESENT);   // newest -> front
  ui.pump(30);

  ensureExpanded("Recent");

  // Both entries render; the missing one carries the "(missing)" marker.
  expect(ui.findByTextContaining("rp_recent_present.rplg")).toBeTruthy();
  expect(ui.findByTextContaining("(missing)")).toBeTruthy();

  // Expand the present entry: Load / Rename / Remove, but no Locate (it exists).
  ensureExpanded("rp_recent_present.rplg");
  expect(ui.findByText("Load")).toBeTruthy();
  expect(ui.findByTextContaining("Rename")).toBeTruthy();
  expect(ui.findByTextContaining("Remove from List")).toBeTruthy();
  expect(ui.findByTextContaining("Locate on Disk")).toBeFalsy();

  // Expand the missing entry: it offers Locate on Disk.
  ensureExpanded("(missing)");
  expect(ui.findByTextContaining("Locate on Disk")).toBeTruthy();
});

test("removing a recent entry drops it and leaves the others", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Two present projects; we remove one and the other must remain.
  ui.writeProjectJson(PRESENT, "/tmp/rp_recent_rom.gb");
  ui.writeProjectJson(SECOND,  "/tmp/rp_recent_rom2.gb");
  ui.seedRecent(SECOND);
  ui.seedRecent(PRESENT);   // newest -> front
  ui.pump(30);

  ensureExpanded("Recent");
  expect(ui.findByTextContaining("rp_recent_present.rplg")).toBeTruthy();
  expect(ui.findByTextContaining("rp_recent_second.rplg")).toBeTruthy();

  ensureExpanded("rp_recent_present.rplg");

  // Activate "Remove from List" -> confirm prompt opens.
  const remove = ui.findByTextContaining("Remove from List");
  expect(remove).toBeTruthy();
  clickCenter(remove!);
  ui.pump(20);
  expect(ui.findByTextContaining("from recent files")).toBeTruthy(); // confirm overlay

  // Enter confirms -> removeRecentFile -> recent-files-changed -> refetch.
  ui.tapKey(Key.Enter);
  ui.pump(60);

  // The removed entry is gone; the other remains in the list.
  expect(ui.findByTextContaining("rp_recent_present.rplg")).toBeFalsy();
  expect(ui.findByTextContaining("rp_recent_second.rplg")).toBeTruthy();
});
