// Menu leaves that open the file browser. The Load / Add items on the start + instance menus drive
// FileSelection through the COMPOSED store graph (composeAppStores now carries `fileSelection`). This
// mirrors browse.test.ts but exercises the menu wiring: build the MenuContext the way App.tsx does,
// invoke a leaf's onSelect, and assert the store mutated once the (mocked) dialog settled.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor, sramBytesFor } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, buildInstanceMenu, composeWindowTitle, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { buildKeyToButton, buildGamepadToButton, BUTTON_VALUE } from "../../src/keyCodes";
import { defaultBindingMap } from "../../src/bindingMap";
import { gbRom, lsdjRom } from "../systems/fixtures";

// A leaf's onSelect fires a FileSelection call fire-and-forget; flush the microtask chain it kicks off
// (openFileBrowser resolve → pairing → the store mutation / runLoad .then). A handful of turns settles it.
const flush = async () => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};

// The MenuContext App.tsx assembles from its hooks, minus React.
function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {}, // App wires these to useProjectModals; tests spy per-case (see routing test).
    loadProject: () => {},
    // App guards this through useProjectModals; here it applies unguarded so the load tests observe the
    // store mutation directly (a clean project has nothing to guard anyway).
    loadRomAsProject: (romPath: string, explicitSav?: string) =>
      stores.project.openRom(romPath, explicitSav ? { explicitSav } : undefined),
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

test("the Recent submenu appears on BOTH the start and instance menus", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  stores.recent.add("/music/song.rplg", "Song"); // one recent entry so the submenu is populated

  expect(findItem(buildStartMenu(ctxOf(stores)).items, "start-recent")?.kind).toBe("submenu");

  be.seed("/roms/a.gb", gbRom());
  const sysId = stores.project.systems.addSystem("/roms/a.gb")!;
  const anchored = stores.project.systems.view().find((s) => s.id === sysId)!;
  const inst = buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items;
  expect(findItem(inst, "inst-recent")?.kind).toBe("submenu");
});

test("a recent entry's Rename prompt renames the project (edits the file + recents alias)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.loadRom("/roms/a.gb");
  stores.project.adoptRomProject("/roms/a.gb"); // /roms/a.rplg in recents, name "a", open project

  const row = submenuChildren(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent"), "recent-0");
  const rename = findItem(row, "recent-0-rename")!;
  expect(rename.kind).toBe("prompt");
  expect(rename.prompt!.onConfirm("  ")).toBe("Name cannot be empty."); // blank → error keeps it open
  expect(rename.prompt!.onConfirm("My Song")).toBe(null); // success closes it

  expect(stores.project.name()).toBe("My Song");
  expect(JSON.parse(be.readText("/roms/a.rplg")!).name).toBe("My Song");
});

test("menu titles: start shows the version; instance adds project + ROM (deduped when equal)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  expect(buildStartMenu({ ...ctxOf(stores), version: "0.6.2" }).title).toBe("RetroPlug v0.6.2");
  expect(buildStartMenu({ ...ctxOf(stores), version: "" }).title).toBe("RetroPlug"); // no version → fallback

  // A ROM-adopted project: its name is the ROM stem, so the ROM isn't repeated.
  be.seed("/roms/zelda.gb", gbRom());
  stores.project.systems.loadRom("/roms/zelda.gb");
  stores.project.adoptRomProject("/roms/zelda.gb");
  const sys = stores.project.systems.view()[0];
  expect(buildInstanceMenu({ ...ctxOf(stores), version: "0.6.2", system: sys }).title).toBe("RetroPlug v0.6.2 - zelda");
});

test("composeWindowTitle: version + project (no ROM), dropping empty segments", () => {
  expect(composeWindowTitle("0.6.2", "Song")).toBe("RetroPlug v0.6.2 - Song");
  expect(composeWindowTitle("0.6.2", "")).toBe("RetroPlug v0.6.2"); // nameless project → version only
  expect(composeWindowTitle("", "Song")).toBe("RetroPlug - Song"); // no version → bare base
  expect(composeWindowTitle("", "")).toBe("RetroPlug");
});

test("instance title shows a distinct project + ROM, and 'mGB' for the embedded synth", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A paired sav names the project distinctly from the ROM → both segments show.
  be.seed("/roms/zelda.gb", gbRom());
  stores.project.systems.loadRom("/roms/zelda.gb", { explicitSav: "/saves/song.sav" });
  stores.project.adoptRomProject("/roms/zelda.gb");
  const sys = stores.project.systems.view()[0];
  expect(buildInstanceMenu({ ...ctxOf(stores), version: "0.6.2", system: sys }).title).toBe("RetroPlug v0.6.2 - song - zelda");

  // The embedded mGB synth (no ROM path, no project name) → just "mGB".
  stores.project.newProject();
  stores.project.systems.loadMgb();
  const mgb = stores.project.systems.view()[0];
  expect(buildInstanceMenu({ ...ctxOf(stores), version: "0.6.2", system: mgb }).title).toBe("RetroPlug v0.6.2 - mGB");
});

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

test("instance menu Load... opens a NEW project (does not swap the anchored tile)", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.gb", gbRom());
  const anchoredId = stores.project.systems.addSystem("/roms/a.gb")!;
  stores.project.systems.addSystem("/roms/b.gb"); // a 2nd instance — a real multi-instance project
  const anchored = stores.project.systems.view().find((s) => s.id === anchoredId)!;
  be.queueBrowse("/roms/c.gb");

  findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-load")!.onSelect!();
  await flush();

  // Load… is project-level: the whole project is replaced by a fresh single-system one — not a tile swap
  // (which would keep both instances).
  const view = stores.project.systems.view();
  expect(view.length).toBe(1);
  expect(view[0].romPath).toBe("/roms/c.gb");
});

test("instance menu Replace Instance swaps the anchored instance in place", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.gb", gbRom());
  const anchoredId = stores.project.systems.addSystem("/roms/a.gb")!; // slot 0
  const otherId = stores.project.systems.addSystem("/roms/b.gb")!; // slot 1 — must stay untouched
  const anchored = stores.project.systems.view().find((s) => s.id === anchoredId)!;
  be.queueBrowse("/roms/c.gb");

  findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-replace")!.onSelect!();
  await flush();

  const view = stores.project.systems.view();
  expect(view.length).toBe(2); // in place — no add/remove
  expect(view[0].romPath).toBe("/roms/c.gb"); // the anchored slot now holds the new ROM
  expect(view.find((s) => s.id === otherId)?.romPath).toBe("/roms/b.gb"); // the other instance is untouched
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(anchoredId); // swapped the anchored id, not the focused tile
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

test("project Export Zip... writes a .rplg.zip archive to the picked path", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const proj = projectSubmenuWithSystem(be, stores);
  be.queueBrowse("/out/proj.rplg.zip");

  findItem(proj, "proj-export")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.patterns.includes("*.rplg.zip")).toBeTruthy(); // zip projects are always .rplg.zip
  expect(be.fileExists("/out/proj.rplg.zip")).toBeTruthy();
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

test("system Save State (quick) writes the live savestate to the ROM's sibling .ss0, no dialog", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);

  findItem(items, "sys-quicksavestate")!.onSelect!();

  expect(be.fileBrowserCalls.length).toBe(0); // pathless — no browser opens
  expect(be.readFile("/roms/a.ss0")).toEqual(stateBytesFor(id)); // rom-sibling slot 0
});

test("system Save SRAM (quick) writes the battery to the resolved sibling .sav, no dialog", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);

  findItem(items, "sys-quicksavesram")!.onSelect!();

  expect(be.fileBrowserCalls.length).toBe(0);
  expect(be.readFile("/roms/a.sav")).toEqual(sramBytesFor(id)); // the auto-save target
});

test("the embedded mGB synth omits the quick-save items (no on-disk ROM target)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  stores.project.systems.loadMgb();
  const anchored = stores.project.systems.view()[0];
  expect(anchored.romPath).toBe(""); // embedded
  const items = submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-system");
  expect(findItem(items, "sys-quicksavestate")).toBe(undefined);
  expect(findItem(items, "sys-quicksavesram")).toBe(undefined);
  expect(findItem(items, "sys-savestate")?.label).toBe("Save State As..."); // the "As…" browse variant remains
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

test("system Reload on ROM Change + Fast Boot are cyclers — Left/Right arrows step them", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);

  // Re-derive the System submenu off the live view (as a re-render would) so a fresh row carries the
  // post-change `current`. Reading the same stale item twice would re-apply the old value.
  const row = (itemId: string): MenuItem => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    return findItem(submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-system"), itemId)!;
  };
  const reloadOn = () => stores.project.systems.view().find((s) => s.id === id)!.settings.reloadOnRomChange;
  const fastBootOn = () =>
    (stores.project.systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "sameboy")!.config as { fastBoot: boolean }).fastBoot;

  // Both are cyclers (arrow-steppable) with keepOpen, not plain actions.
  expect(findItem(items, "sys-reload")!.kind).toBe("cycler");
  expect(findItem(items, "sys-reload")!.keepOpen).toBeTruthy();
  expect(findItem(items, "sys-fastboot")!.kind).toBe("cycler");

  // Reload defaults Off; Right (onCycle +1) turns it On, Left (−1) back Off.
  expect(reloadOn()).toBeFalsy();
  row("sys-reload").onCycle!(1);
  expect(reloadOn()).toBeTruthy();
  row("sys-reload").onCycle!(-1);
  expect(reloadOn()).toBeFalsy();

  // Fast Boot defaults On; either arrow flips a 2-value cycler.
  expect(fastBootOn()).toBeTruthy();
  row("sys-fastboot").onCycle!(1);
  expect(fastBootOn()).toBeFalsy();
  row("sys-fastboot").onCycle!(1);
  expect(fastBootOn()).toBeTruthy();
});

test("the LSDj submenu appears only for a system carrying an lsdj-sync role; its cyclers apply live", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A plain GB ROM → no lsdj-sync role → no LSDj submenu.
  be.seed("/roms/plain.gb", gbRom());
  const plainId = stores.project.systems.addSystem("/roms/plain.gb")!;
  const plain = stores.project.systems.view().find((s) => s.id === plainId)!;
  expect(findItem(buildInstanceMenu({ ...ctxOf(stores), system: plain }).items, "inst-lsdj")).toBe(undefined);

  // An LSDj cart (cartridge title "LSDJ") → the ROM provider attaches lsdj-sync → the submenu shows.
  be.seed("/roms/song.gb", lsdjRom());
  const id = stores.project.systems.addSystem("/roms/song.gb")!;
  const lsdjItems = () =>
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-lsdj");
  const cfg = () =>
    stores.project.systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "lsdj-sync")!.config as { mode: number; tempoDivisor: number };

  const mode = findItem(lsdjItems(), "lsdj-mode")!;
  expect(mode.kind).toBe("cycler");
  expect(mode.label).toBe("Mode: MIDI Sync"); // default mode 1
  expect(cfg().mode).toBe(1);

  // Enter/onSelect steps the mode forward (1 → 2), applied through setRoleConfig (the live re-push path).
  mode.onSelect!();
  expect(cfg().mode).toBe(2);

  // Tempo Divisor cycler steps 1 → 2 (index 0 → 1 in [1,2,4,8]).
  expect(findItem(lsdjItems(), "lsdj-divisor")!.label).toBe("Tempo Divisor: 1");
  findItem(lsdjItems(), "lsdj-divisor")!.onSelect!();
  expect(cfg().tempoDivisor).toBe(2);
});

// The Settings → Keyboard Bindings submenu (reachable from the start menu, no system needed).
function keyboardBindings(stores: AppStores): MenuItem[] {
  const settings = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  return submenuChildren(settings, "set-keybindings");
}

test("Keyboard Bindings: 8 capture rows + reset; capture rebinds write-through, clear + reset", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  const rows = keyboardBindings(stores);
  expect(rows.filter((r) => r.kind === "capture").length).toBe(8);
  expect(findItem(rows, "bind-A")!.label).toBe("A: Z, z"); // resolved default baked into the label
  expect(findItem(rows, "bind-reset")!.kind).toBe("action");

  // Capture A → Q: the active keyboard profile persists and the resolved map + key→button lookup follow.
  findItem(rows, "bind-A")!.capture!.onCapture("Q");
  const active = stores.userConfig.config().activeKeyboardBindings;
  expect(stores.bindings.loadProfile(active)!.keyboard.A).toEqual(["Q"]); // written through to disk
  expect(buildKeyToButton(stores.bindings.resolvedBindings().keyboard).get("Q".charCodeAt(0))).toBe(BUTTON_VALUE.A);
  expect(keyboardBindings(stores).find((r) => r.id === "bind-A")!.label).toBe("A: Q"); // relabels on rebuild

  // Clear A → the button unbinds.
  keyboardBindings(stores).find((r) => r.id === "bind-A")!.capture!.onClear();
  expect(stores.bindings.resolvedBindings().keyboard.A).toEqual([]);
  expect(keyboardBindings(stores).find((r) => r.id === "bind-A")!.label).toBe("A: -");

  // Reset restores the default keyboard map (and preserves the gamepad channel).
  keyboardBindings(stores).find((r) => r.id === "bind-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().keyboard.A).toEqual(defaultBindingMap().keyboard.A);
  expect(stores.bindings.loadProfile(active)!.gamepad).toEqual(defaultBindingMap().gamepad);
});

// The Settings → Gamepad Bindings submenu — the gamepad twin of the keyboard editor.
function gamepadBindings(stores: AppStores): MenuItem[] {
  const settings = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  return submenuChildren(settings, "set-gamepad-bindings");
}

test("Gamepad Bindings: gamepad-source capture rows; button + axis-token rebinds write-through, clear + reset", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  const rows = gamepadBindings(stores);
  expect(rows.filter((r) => r.kind === "capture").length).toBe(8);
  expect(findItem(rows, "bind-gp-A")!.capture!.source).toBe("gamepad"); // arms the pad bus, not the key bus
  expect(findItem(rows, "bind-gp-Up")!.label).toBe("Up: dpup, lefty-"); // default d-pad hat + left stick

  // Capture A → a controller button (raw SDL name): written through to the active GAMEPAD profile.
  findItem(rows, "bind-gp-A")!.capture!.onCapture("y");
  const active = stores.userConfig.config().activeGamepadBindings;
  expect(stores.bindings.loadProfile(active)!.gamepad.A).toEqual(["y"]); // written through to disk
  expect(buildGamepadToButton(stores.bindings.resolvedBindings().gamepad).get("y")).toBe(BUTTON_VALUE.A);

  // Capture Up → a stick direction (half-axis token) — the analog-as-dpad binding form.
  gamepadBindings(stores).find((r) => r.id === "bind-gp-Up")!.capture!.onCapture("lefty-");
  expect(stores.bindings.resolvedBindings().gamepad.Up).toEqual(["lefty-"]);
  expect(buildGamepadToButton(stores.bindings.resolvedBindings().gamepad).get("lefty-")).toBe(BUTTON_VALUE.Up);

  // Clear A → unbinds; reset restores the default gamepad map (and preserves the keyboard channel).
  gamepadBindings(stores).find((r) => r.id === "bind-gp-A")!.capture!.onClear();
  expect(stores.bindings.resolvedBindings().gamepad.A).toEqual([]);
  gamepadBindings(stores).find((r) => r.id === "bind-gp-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().gamepad.Up).toEqual(defaultBindingMap().gamepad.Up);
  expect(stores.bindings.loadProfile(active)!.keyboard).toEqual(defaultBindingMap().keyboard);
});

const promptOf = (stores: AppStores, id: string) => findItem(keyboardBindings(stores), id)!.prompt!;

test("New Profile: creates a named copy of the active bindings and switches to it", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  expect(promptOf(stores, "bind-new").onConfirm("wasd")).toBe(null);
  expect(stores.bindings.availableProfiles()).toEqual(["default", "wasd"]);
  expect(stores.userConfig.config().activeKeyboardBindings).toBe("wasd"); // made active
  expect(stores.bindings.loadProfile("wasd")!.keyboard).toEqual(defaultBindingMap().keyboard); // copied current
});

test("New Profile: rejects invalid + duplicate names, changing nothing", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  expect(promptOf(stores, "bind-new").onConfirm("bad name")).toBe("Invalid name (A-Z, 0-9, _, -).");
  expect(promptOf(stores, "bind-new").onConfirm("default")).toBe("Profile already exists.");
  expect(stores.bindings.availableProfiles()).toEqual(["default"]);
  expect(stores.userConfig.config().activeKeyboardBindings).toBe("default");
});

test("Rename: renames the active profile, repoints the active ref, rejects a dup", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  promptOf(stores, "bind-new").onConfirm("wasd"); // active = wasd

  expect(promptOf(stores, "bind-rename").onConfirm("wasd2")).toBe(null);
  expect(stores.bindings.availableProfiles()).toEqual(["default", "wasd2"]);
  expect(stores.userConfig.config().activeKeyboardBindings).toBe("wasd2");
  expect(promptOf(stores, "bind-rename").onConfirm("default")).toBe("Profile already exists.");
});

test("Delete Profile: lists only non-active profiles and removes on confirm", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  promptOf(stores, "bind-new").onConfirm("wasd"); // active = wasd
  promptOf(stores, "bind-new").onConfirm("arrows"); // active = arrows (gamepad still default)

  const delRows = submenuChildren(keyboardBindings(stores), "bind-delete");
  expect(delRows.map((r) => r.id)).toEqual(["bind-del-wasd"]); // active keyboard (arrows) + gamepad (default) excluded

  expect(delRows[0].prompt!.confirm).toBeTruthy(); // yes/no dialog
  expect(delRows[0].prompt!.onConfirm("")).toBe(null);
  expect(stores.bindings.availableProfiles()).toEqual(["arrows", "default"]);
});

test("Delete Profile: '(no other profiles)' when every profile is active", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const delRows = submenuChildren(keyboardBindings(stores), "bind-delete");
  expect(delRows.length).toBe(1);
  expect(delRows[0].id).toBe("bind-del-none");
});

test("Profile cycler: switches the active profile; resolved bindings follow", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  promptOf(stores, "bind-new").onConfirm("wasd"); // active = wasd, at profiles index 1
  keyboardBindings(stores).find((r) => r.id === "bind-A")!.capture!.onCapture("Q"); // wasd.A = Q

  findItem(keyboardBindings(stores), "bind-profile")!.onCycle!(-1); // wasd -> default
  expect(stores.userConfig.config().activeKeyboardBindings).toBe("default");
  expect(stores.bindings.resolvedBindings().keyboard.A).toEqual(defaultBindingMap().keyboard.A); // re-resolved
});

test("New Project / Load Project / recent Load route through the guarded ctx ops", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb"); // a system → the Project submenu shows New Project
  stores.recent.add("/music/song.rplg", "Song");
  be.seed("/picked/proj.rplg", "PK");
  be.queueBrowse("/picked/proj.rplg");

  const calls: string[] = [];
  const ctx: MenuContext = { ...ctxOf(stores), newProject: () => calls.push("new"), loadProject: (p) => calls.push(`load:${p}`) };
  const anchored = stores.project.systems.view()[0];

  // New Project → ctx.newProject (guarded), NOT a raw project.newProject().
  const proj = submenuChildren(buildInstanceMenu({ ...ctx, system: anchored }).items, "inst-project");
  findItem(proj, "proj-new")!.onSelect!();
  expect(calls).toEqual(["new"]);

  // Load Project... browses, then hands the picked path to ctx.loadProject (guarded + outcome-aware).
  findItem(proj, "proj-load")!.onSelect!();
  await flush();
  expect(calls).toEqual(["new", "load:/picked/proj.rplg"]);

  // A recent entry's Load also routes through ctx.loadProject (was a fire-and-forget project.load).
  const recentRow = submenuChildren(submenuChildren(buildStartMenu(ctx).items, "start-recent"), "recent-0");
  findItem(recentRow, "recent-0-load")!.onSelect!();
  expect(calls).toEqual(["new", "load:/picked/proj.rplg", "load:/music/song.rplg"]);
});

test("Settings -> Open Settings Folder reveals the config dir via the native seam", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  let opened: string | null = null;
  (globalThis as { __rp_openPath?: (p: string) => void }).__rp_openPath = (p) => {
    opened = p;
  };

  const settings = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  const item = findItem(settings, "set-open-folder")!;
  expect(item.kind).toBe("action");
  item.onSelect!();
  expect(opened).toBe("/cfg"); // backend.configDir()

  delete (globalThis as { __rp_openPath?: unknown }).__rp_openPath;
});

test("appStores: a userConfig change also invalidates the bindings channel", () => {
  const be = new MockBackend("/cfg");
  const fired: string[] = [];
  const stores = composeAppStores({ backend: be, notify: (c) => fired.push(c) });
  stores.bindings.saveProfile("wasd", { ...defaultBindingMap(), name: "wasd" });
  fired.length = 0;
  stores.userConfig.setActiveKeyboardBindings("wasd");
  expect(fired.includes("userConfig")).toBeTruthy();
  expect(fired.includes("bindings")).toBeTruthy(); // resolved bindings depend on the active pointer
});
