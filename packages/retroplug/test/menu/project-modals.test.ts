// Where the cursor OPENS in each project overlay. Menu puts it on the first enabled row, so that row is
// what a stray Enter - or the second half of the double-tap that raised the prompt - runs. It must never
// be the one that throws work away: making the song-discard prompt's message line read-only once moved
// the opening cursor onto "Discard & load", one keypress from losing the working song.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import type { AppStores } from "../../src/appStores";
import { buildProjectModal, type ProjectModalHandlers } from "../../ui/app/projectModals";
import type { MenuItem, MenuTree } from "../../ui/screens/menu/menuTree";
import { gbRomBattery } from "../systems/fixtures";

function setup() {
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be));
  const stores = { backend: be, project } as unknown as AppStores;
  const calls: string[] = [];
  const h: ProjectModalHandlers = {
    handleOutcome: () => calls.push("outcome"),
    onClose: () => calls.push("close"),
    proceedSong: () => calls.push("proceedSong"),
  };
  return { be, project, stores, calls, h };
}

/** The row Menu's focusTarget opens on: the first that is neither a separator nor disabled. */
const openingRow = (tree: MenuTree): MenuItem => tree.items.find((i) => i.kind !== "separator" && !i.disabled)!;

test("song discard opens on Keep current song; Enter there keeps the song", () => {
  const { stores, calls, h } = setup();
  const tree = buildProjectModal({ kind: "songDiscard", song: "NEW", working: "OLD" }, stores, h);
  const first = openingRow(tree);
  expect(first.id).toBe("song-discard-cancel");
  first.onSelect!();
  expect(calls).toEqual(["close"]);
  // The destructive choice is still offered, just not under the cursor.
  expect(tree.items.some((i) => i.id === "song-discard-go" && !i.disabled)).toBeTruthy();
});

test("the unsaved-changes confirm opens on Save, after the read-only list of what is unsaved", () => {
  const { be, project, stores, h } = setup();
  be.seed("/roms/a.gb", gbRomBattery());
  project.systems.addSystem("/roms/a.gb"); // dirty: an unsaved project + a new battery
  const tree = buildProjectModal({ kind: "confirm", proceed: () => {} }, stores, h);
  expect(tree.items[0].disabled).toBe(true); // the "Project: ..." readout leads
  expect(openingRow(tree).id).toBe("discard-save");
});

test("a notice opens on OK, not on its body", () => {
  const { stores, calls, h } = setup();
  const tree = buildProjectModal({ kind: "notice", title: "Load Failed", body: "The project file couldn't be read." }, stores, h);
  const first = openingRow(tree);
  expect(first.id).toBe("notice-ok");
  first.onSelect!();
  expect(calls).toEqual(["close"]);
});

test("relink opens on the first Locate, with Cancel last", () => {
  const { stores, h } = setup();
  const tree = buildProjectModal({ kind: "relink", missing: [{ systemIndex: 0, itemKind: "rom", path: "/gone/a.gb" }] }, stores, h);
  expect(openingRow(tree).id).toBe("relink-0");
  expect(tree.items[tree.items.length - 1].id).toBe("relink-cancel");
});
