// Menu leaves that open the file browser. The Load / Add items on the start + instance menus drive
// FileSelection through the COMPOSED store graph (composeAppStores now carries `fileSelection`). This
// mirrors browse.test.ts but exercises the menu wiring: build the MenuContext the way App.tsx does,
// invoke a leaf's onSelect, and assert the store mutated once the (mocked) dialog settled.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor, sramBytesFor } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { gbRom } from "../systems/fixtures";

// A leaf's onSelect fires browse() fire-and-forget; flush the microtask chain it kicks off (openFileBrowser
// resolve → route → applyRom → the runSelection .then). A handful of turns settles the plain-ROM path.
const flush = async () => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};

// The MenuContext App.tsx assembles from its hooks, minus React.
function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
  };
}

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);

function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children : [];
}

// The instance-menu Project submenu, with one system seeded so the save-side items appear.
function projectSubmenuWithSystem(be: MockBackend, stores: AppStores): MenuItem[] {
  be.seed("/roms/a.gb", gbRom());
  const id = stores.project.systems.addSystem("/roms/a.gb");
  const anchored = stores.project.systems.view().find((s) => s.id === id)!;
  return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-project");
}

// The instance-menu System submenu + the anchored system's id (for asserting per-id state/sram bytes).
function systemMenu(be: MockBackend, stores: AppStores): { id: number; items: MenuItem[] } {
  be.seed("/roms/a.gb", gbRom());
  const id = stores.project.systems.addSystem("/roms/a.gb")!;
  const anchored = stores.project.systems.view().find((s) => s.id === id)!;
  return { id, items: submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-system") };
}

test("start menu Load... browses the ROM-or-sav dialog and loads the picked ROM", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  be.queueBrowse("/roms/a.gb");

  const load = findItem(buildStartMenu(ctxOf(stores)).items, "start-load");
  expect(load?.kind).toBe("action");
  load!.onSelect!();
  await flush();

  expect(stores.project.systems.view().length).toBe(1);
  expect(be.fileBrowserCalls.length).toBe(1);
  expect(be.fileBrowserCalls[0].patterns.includes("*.sav")).toBeTruthy(); // ROM-or-sav filter
});

test("instance menu Add Instance appends the picked ROM", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const anchoredId = stores.project.systems.addSystem("/roms/a.gb"); // one system → an instance menu exists
  const anchored = stores.project.systems.view().find((s) => s.id === anchoredId)!;
  be.queueBrowse("/roms/b.gb");

  const add = findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-add");
  expect(add?.kind).toBe("action");
  add!.onSelect!();
  await flush();

  expect(stores.project.systems.view().length).toBe(2);
});

test("a cancelled browse mutates nothing", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.queueBrowse(null); // user closed the dialog

  findItem(buildStartMenu(ctxOf(stores)).items, "start-load")!.onSelect!();
  await flush();

  expect(stores.project.systems.view().length).toBe(0);
});

test("project Save Project As... writes the project to the picked path via a save dialog", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const proj = projectSubmenuWithSystem(be, stores);
  be.queueBrowse("/out/proj.rplg");

  findItem(proj, "proj-saveas")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.saving).toBe(true); // a save dialog
  expect(be.fileExists("/out/proj.rplg")).toBeTruthy();
});

test("project Export Zip... writes the archive to the picked path", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const proj = projectSubmenuWithSystem(be, stores);
  be.queueBrowse("/out/proj.zip");

  findItem(proj, "proj-export")!.onSelect!();
  await flush();

  expect(be.fileExists("/out/proj.zip")).toBeTruthy();
});

test("project Load Project... opens a read (non-saving) .rplg dialog", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.queueBrowse(null); // cancel — we only assert the dialog it opened

  findItem(buildStartMenu(ctxOf(stores)).items, "start-project"); // ensure the submenu builds
  const proj = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-project");
  findItem(proj, "proj-load")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(!!last.saving).toBe(false); // read dialog
  expect(last.patterns.includes("*.rplg")).toBeTruthy();
});

test("recent Locate on Disk relinks the entry to the picked path", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  stores.recent.add("/gone/old.rplg"); // a missing entry
  be.seed("/found/new.rplg", "PK"); // the file the user points at
  be.queueBrowse("/found/new.rplg");

  const recent = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent");
  findItem(recent, "recent-0")!; // the entry submenu exists
  const entrySub = submenuChildren(recent, "recent-0");
  findItem(entrySub, "recent-0-locate")!.onSelect!();
  await flush();

  const view = stores.recent.view();
  expect(view.some((e) => e.path.endsWith("new.rplg"))).toBeTruthy();
});

test("system Save State... writes the live savestate to the picked path via a save dialog", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);
  be.queueBrowse("/out/a.ss0");

  findItem(items, "sys-savestate")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.saving).toBe(true);
  expect(last.patterns.includes("*.ss?")).toBeTruthy();
  expect(be.readFile("/out/a.ss0")).toEqual(stateBytesFor(id));
});

test("system Load State... reconstructs the system from the picked savestate", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);
  be.seed("/in/x.ss0", stateBytesFor(999));
  be.queueBrowse("/in/x.ss0");

  findItem(items, "sys-loadstate")!.onSelect!();
  await flush();

  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id); // in-place replace
  expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(999)); // the file's bytes seeded the core
});

test("system Save SRAM... writes the battery SRAM to the picked path", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);
  be.queueBrowse("/out/a.sav");

  findItem(items, "sys-savesram")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.patterns.includes("*.sav")).toBeTruthy();
  expect(be.readFile("/out/a.sav")).toEqual(sramBytesFor(id));
});

test("system Load SRAM... cold-boots the system with the picked file's SRAM", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);
  be.seed("/in/x.sav", sramBytesFor(999));
  be.queueBrowse("/in/x.sav");

  findItem(items, "sys-loadsram")!.onSelect!();
  await flush();

  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id);
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(999));
});

test("system New SRAM cold-boots with a blank battery, no dialog", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);

  findItem(items, "sys-newsram")!.onSelect!();

  expect(be.fileBrowserCalls.length).toBe(0); // pathless — no browser opens
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id); // in-place replace
  const seed = new Uint8Array(call.sramBytes!);
  expect(seed.length).toBe(0x20000); // native truncates/zero-pads to the cart's real battery size
  expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
});

test("system Reset reboots carrying the live battery, no dialog", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);

  findItem(items, "sys-reset")!.onSelect!();

  expect(be.fileBrowserCalls.length).toBe(0); // pathless — no browser opens
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id); // in-place replace
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(id)); // the live battery carried forward
});
