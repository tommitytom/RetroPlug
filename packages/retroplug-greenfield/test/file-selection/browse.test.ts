// FileSelection.browse — the async dialog flow. browse() opens the OS ROM-or-sav
// browser and resolves to the FINAL outcome after every dialog settles, so an
// unpaired .sav simply awaits a 2nd (ROM-only) browser inside the same Promise — no
// pending latch. Ports openRomBrowser + the onFileBrowserSelected dispatch, with the
// mocked dialog responses queued FIFO.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { FileSelection } from "../../src/fileSelection";
import { gbRom } from "../systems/fixtures";

function newFs() {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be);
  const fs = new FileSelection(be, systems);
  return { be, systems, fs };
}

test("browse: opens the ROM-or-sav dialog and loads the picked ROM", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.queueBrowse("/roms/a.gb");
  const out = await fs.browse("load");
  expect(out).toEqual({ kind: "loaded", system: systems.focused() });
  expect(be.fileBrowserCalls.length).toBe(1);
  expect(be.fileBrowserCalls[0].patterns.includes("*.sav")).toBeTruthy(); // ROM-or-sav
});

test("browse: a cancelled dialog yields cancelled and builds nothing", async () => {
  const { be, fs, systems } = newFs();
  be.queueBrowse(null); // user closed the dialog
  const out = await fs.browse("load");
  expect(out).toEqual({ kind: "cancelled" });
  expect(systems.view().length).toBe(0);
});

test("browse: an unpaired .sav awaits a 2nd ROM-only browser and pairs the result", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery"); // no sibling ROM in its folder
  be.seed("/roms/thegame.gb", gbRom());
  be.queueBrowse("/saves/orphan.sav", "/roms/thegame.gb"); // 1st dialog, then the 2nd
  const out = await fs.browse("load");
  expect(out.kind).toBe("loaded");
  expect(be.fileBrowserCalls.length).toBe(2); // opened a 2nd browser
  expect(be.fileBrowserCalls[1].patterns.includes("*.sav")).toBeFalsy(); // ROM-only
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romPath).toBe("/roms/thegame.gb"); // paired with the chosen ROM
  expect(spec.savPath).toBe("/saves/orphan.sav"); // the picked save crossed as the override
});

test("browse: a 2nd-browser pick that isn't a ROM is an error", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery");
  be.seed("/roms/notes.txt", "hello");
  be.queueBrowse("/saves/orphan.sav", "/roms/notes.txt");
  const out = await fs.browse("load");
  expect(out).toEqual({ kind: "error", path: "/roms/notes.txt" });
});

test("browse: a cancelled 2nd browser yields cancelled", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery");
  be.queueBrowse("/saves/orphan.sav", null);
  const out = await fs.browse("load");
  expect(out).toEqual({ kind: "cancelled" });
});

test("browse: add mode appends the picked ROM", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  await fs.selectPath("/roms/a.gb", "load"); // seed one system first
  be.queueBrowse("/roms/b.gb");
  const out = await fs.browse("add");
  expect(out.kind).toBe("added");
  expect(systems.view().length).toBe(2);
});
