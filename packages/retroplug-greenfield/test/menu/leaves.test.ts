// Menu leaves that open the file browser. The Load / Add items on the start + instance menus drive
// FileSelection through the COMPOSED store graph (composeAppStores now carries `fileSelection`). This
// mirrors browse.test.ts but exercises the menu wiring: build the MenuContext the way App.tsx does,
// invoke a leaf's onSelect, and assert the store mutated once the (mocked) dialog settled.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
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
