// Menu leaves that open the file browser. The Load / Add items on the start + instance menus drive
// FileSelection through the COMPOSED store graph (composeAppStores now carries `fileSelection`). This
// mirrors browse.test.ts but exercises the menu wiring: build the MenuContext the way App.tsx does,
// invoke a leaf's onSelect, and assert the store mutated once the (mocked) dialog settled.
import { test, expect } from "../../testing/harness";
import { MockBackend, stateBytesFor, sramBytesFor } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, buildInstanceMenu, composeWindowTitle, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { buildKeyToButton, buildGamepadToButton, buildKeyToAction, buildGamepadToAction, BUTTON_VALUE } from "../../src/keyCodes";
import { defaultBindingMap } from "../../src/bindingMap";
import { gbRom, gbRomBattery, lsdjRom, nesRom, nesRomBattery } from "../systems/fixtures";
import { savFrom, type SavInput } from "../../src/lsdjSav";

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
    requestExit: () => {}, // App wires this to the native quit path; inert here.
    beginSongImport: () => {},
  };
}

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);

function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// The instance-menu Project submenu, with one system seeded so the save-side items appear.
function projectSubmenuWithSystem(be: MockBackend, stores: AppStores): MenuItem[] {
  be.seed("/roms/a.gb", gbRom());
  const id = stores.project.systems.addSystem("/roms/a.gb");
  const anchored = stores.project.systems.view().find((s) => s.id === id)!;
  return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-project");
}

// The instance-menu System submenu + the anchored system's id (for asserting per-id state/sram bytes).
// A battery cart (gbRomBattery) so the Save-SRAM rows are live — the save/state leaves exercise a real target.
function systemMenu(be: MockBackend, stores: AppStores): { id: number; items: MenuItem[] } {
  be.seed("/roms/a.gb", gbRomBattery());
  const id = stores.project.systems.addSystem("/roms/a.gb")!;
  const anchored = stores.project.systems.view().find((s) => s.id === id)!;
  return { id, items: submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-system") };
}

test("the System submenu shows NES core knobs for a NES system, and the SameBoy knobs for GB", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const systemMenuFor = (id: number) => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-system");
  };

  // GB: SameBoy knobs present, NES rows absent.
  be.seed("/roms/a.gb", gbRom());
  const gbSys = systemMenuFor(stores.project.systems.addSystem("/roms/a.gb")!);
  expect(findItem(gbSys, "sys-model")).toBeTruthy();
  expect(findItem(gbSys, "sys-nes-region")).toBe(undefined);
  expect(findItem(gbSys, "sys-nes-spritelimit")).toBe(undefined);
  expect(findItem(gbSys, "sys-nes-apu-latency")).toBe(undefined);

  // NES: Region + Remove Sprite Limit + APU Latency present (defaults read through), SameBoy knobs absent.
  be.seed("/roms/g.nes", nesRom());
  const nesSys = systemMenuFor(stores.project.systems.addSystem("/roms/g.nes")!);
  expect(findItem(nesSys, "sys-nes-region")?.label).toBe("Region: Auto");
  expect(findItem(nesSys, "sys-nes-spritelimit")?.label).toBe("Remove Sprite Limit: Off");
  expect(findItem(nesSys, "sys-nes-apu-latency")?.label).toBe("APU Latency: 1.4 ms");
  expect(findItem(nesSys, "sys-model")).toBe(undefined);
});

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

test("a recent tracker entry's label leads with the song, then the project (ASCII separator)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/music/proj.rplg", "PK"); // on disk → not missing (no " [!]" marker)
  be.seed("/music/plain.rplg", "PK");
  stores.recent.add("/music/proj.rplg", "MyProject", "GRUB"); // a tracker cart: project alias + working song
  stores.recent.add("/music/plain.rplg", "Plain"); // no song

  const rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent");
  // "SONG - project" order, ASCII " - " (the LVGL font has no emdash glyph).
  expect(findItem(rows, "recent-1")?.label).toBe("GRUB - MyProject");
  expect(findItem(rows, "recent-1")?.label.includes("—")).toBe(false); // never the emdash
  // A non-tracker entry (no song) is just the project name.
  expect(findItem(rows, "recent-0")?.label).toBe("Plain");
});

test("recent entries are flat action rows: present loads + can be deleted, missing warns + relinks", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/here/present.rplg", "PK"); // an on-disk project
  stores.recent.add("/here/present.rplg", "Present");
  stores.recent.add("/gone/away.rplg", "Away"); // never seeded → missing

  let rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent");
  const present = findItem(rows, "recent-1")!; // most-recent-first: [0]=Away (missing), [1]=Present
  const missing = findItem(rows, "recent-0")!;

  // A present entry is a plain action row (no submenu, no warn, no marker), carrying the hotkey callbacks.
  expect(present.kind).toBe("action");
  expect(present.warn).toBeFalsy();
  expect(present.label).toBe("Present");
  expect(typeof present.onDelete).toBe("function");
  expect(typeof present.onRename).toBe("object");

  // A missing entry warns (yellow) with a trailing " [!]".
  expect(missing.warn).toBe(true);
  expect(missing.label).toBe("Away [!]");

  // Del (onDelete) drops the entry from the list.
  present.onDelete!();
  rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent");
  expect(stores.recent.view().some((e) => e.path.endsWith("present.rplg"))).toBe(false);
  expect(findItem(rows, "recent-0")?.label).toBe("Away [!]"); // only the missing one remains
});

test("a recent entry's Rename prompt renames the project (edits the file + recents alias)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.loadRom("/roms/a.gb");
  stores.project.adoptRomProject("/roms/a.gb"); // /roms/a.rplg in recents, name "a", open project

  // The recent entry is a single action row; its Rename prompt rides F2 (the onRename field), not a child.
  const rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-recent");
  const rename = findItem(rows, "recent-0")!.onRename!;
  expect(rename.onConfirm("  ")).toBe("Name cannot be empty."); // blank → error keeps it open
  expect(rename.onConfirm("My Song")).toBe(null); // success closes it

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
  // A tracker cart label ("<song> - <ROM name>") supersedes the project name; a null cart falls back to it.
  expect(composeWindowTitle("0.6.2", "Proj", "MYSONG - LSDj v9.4.2")).toBe("RetroPlug v0.6.2 - MYSONG - LSDj v9.4.2");
  expect(composeWindowTitle("0.6.2", "Proj", null)).toBe("RetroPlug v0.6.2 - Proj");
});

test("instance title for a tracker cart shows the working song + the ROM's own name (not the filename)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  // A file named "cool.gb" whose cartridge title is "LSDJ-V9.4.2" — the title must reflect the internal name.
  be.seed("/roms/cool.gb", lsdjRom("LSDJ-V9.4.2"));
  const id = stores.project.systems.addSystem("/roms/cool.gb")!;
  const song = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  be.setSram(id, savFrom({ activeProjectIndex: 0, projects: [{ name: "MYSONG", version: 0, song }] } as SavInput));
  const sys = stores.project.systems.view()[0];
  expect(buildInstanceMenu({ ...ctxOf(stores), version: "0.6.2", system: sys }).title).toBe("RetroPlug v0.6.2 - MYSONG - LSDj v9.4.2");
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

test("instance menu Duplicate inherits + promotes the link group", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  const id = stores.project.systems.addSystem("/roms/a.gb")!; // lone instance, group 0
  const anchored = stores.project.systems.view().find((s) => s.id === id)!;
  const groupOf = (sid: number) =>
    (stores.project.systems.view().find((s) => s.id === sid)!.roles.find((r) => r.kind === "sameboy")!.config as { linkGroupId: number }).linkGroupId;

  findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-dup")!.onSelect!();

  const view = stores.project.systems.view();
  expect(view.length).toBe(2);
  const clone = view.find((s) => s.id !== id)!;
  expect(groupOf(id)).toBe(1); // the lone parent was promoted to group 1
  expect(groupOf(clone.id)).toBe(1); // the clone joined it
});

test("instance menu hides Replace / Remove / Link Group for a lone instance, shows them for a peer", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  const first = stores.project.systems.addSystem("/roms/a.gb")!;
  const anchoredOf = (id: number) => stores.project.systems.view().find((s) => s.id === id)!;

  // Lone instance: the peer-only rows are absent (Add / Duplicate stay).
  const solo = buildInstanceMenu({ ...ctxOf(stores), system: anchoredOf(first) }).items;
  expect(findItem(solo, "inst-add")).toBeTruthy();
  expect(findItem(solo, "inst-dup")).toBeTruthy();
  expect(findItem(solo, "inst-replace")).toBe(undefined);
  expect(findItem(solo, "inst-remove")).toBe(undefined);
  expect(findItem(solo, "inst-link")).toBe(undefined);

  // A second instance makes them appear.
  stores.project.systems.addSystem("/roms/a.gb");
  const multi = buildInstanceMenu({ ...ctxOf(stores), system: anchoredOf(first) }).items;
  expect(findItem(multi, "inst-replace")).toBeTruthy();
  expect(findItem(multi, "inst-remove")).toBeTruthy();
  expect(findItem(multi, "inst-link")).toBeTruthy();
});

test("Audio Routing offers ChannelSplit only for a single system", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  // The Audio Routing cycler row, rebuilt fresh each call (picks up the current setting + system count).
  const audioRow = () => findItem(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-project"), "proj-audio")!;

  // One system: cycling past OnePerInstance(2) reaches ChannelSplit(3) — the "Channels (1 GB)" option.
  stores.project.systems.addSystem("/roms/a.gb");
  stores.project.setAudioRouting("onePerInstance");
  expect(audioRow().label).toBe("Audio Routing: 1 Ch / Inst");
  audioRow().onSelect!();
  expect(stores.project.settings().audioRouting).toBe("channelSplit");
  expect(audioRow().label).toBe("Audio Routing: Channels (1 GB)");

  // A second system drops ChannelSplit from the cycle, so stepping past OnePerInstance wraps to Stereo(0)
  // (native gates it too, so this is UX only — the three per-instance modes stay).
  stores.project.systems.addSystem("/roms/a.gb");
  stores.project.setAudioRouting("onePerInstance");
  audioRow().onSelect!();
  expect(stores.project.settings().audioRouting).toBe("stereo");
});

test("Link Group stays hidden for a NES peer (SameBoy serial link only); Replace / Remove still show", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.nes", nesRom());
  be.seed("/roms/b.nes", nesRom());
  const first = stores.project.systems.addSystem("/roms/a.nes")!;
  stores.project.systems.addSystem("/roms/b.nes"); // a NES peer → multi-instance
  const anchored = stores.project.systems.view().find((s) => s.id === first)!;

  const items = buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items;
  expect(findItem(items, "inst-replace")).toBeTruthy();       // core-agnostic peer rows still show
  expect(findItem(items, "inst-remove")).toBeTruthy();
  expect(findItem(items, "inst-link")).toBe(undefined);       // NES has no link cable → row gated out
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
  const row = findItem(recent, "recent-0")!; // a single action row (no nested submenu)
  expect(row.warn).toBe(true); // missing → yellow
  expect(row.label.endsWith(" [!]")).toBeTruthy(); // missing marker
  row.onSelect!(); // a missing entry's select action is Locate on Disk
  await flush();

  const view = stores.recent.view();
  expect(view.some((e) => e.path.endsWith("new.rplg"))).toBeTruthy();
});

test("system Swap ROM... opens a ROM-only browser and swaps in place, keeping the SRAM", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores);
  be.seed("/roms/new.gb", gbRom());
  be.queueBrowse("/roms/new.gb");

  expect(findItem(items, "sys-swaprom")?.label).toBe("Swap ROM (Preserve SRAM)...");
  findItem(items, "sys-swaprom")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.patterns.includes("*.sav")).toBeFalsy(); // ROM-only browser (a .sav pick would fight "keep SRAM")
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id); // swapped in place
  expect(call.romPath).toBe("/roms/new.gb");
  expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(id)); // the live battery carried into the new ROM
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

test("Save SRAM + New SRAM hide/grey out for a battery-less cart; Load SRAM stays live", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const sysItems = (romPath: string) => {
    const id = stores.project.systems.addSystem(romPath)!;
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-system");
  };

  // Battery carts (GB MBC1+RAM+BATTERY, NES iNES battery flag): both Save-SRAM rows are live, and New SRAM…
  // (a save-file op) is offered.
  be.seed("/roms/batt.gb", gbRomBattery());
  const gbBatt = sysItems("/roms/batt.gb");
  expect(findItem(gbBatt, "sys-quicksavesram")?.disabled).toBeFalsy();
  expect(findItem(gbBatt, "sys-savesram")?.disabled).toBeFalsy();
  expect(findItem(gbBatt, "sys-newsram")).toBeTruthy();
  be.seed("/roms/batt.nes", nesRomBattery());
  const nesBatt = sysItems("/roms/batt.nes");
  expect(findItem(nesBatt, "sys-quicksavesram")?.disabled).toBeFalsy();
  expect(findItem(nesBatt, "sys-savesram")?.disabled).toBeFalsy();

  // Battery-less carts (GB ROM-only, plain NES): the two Save-SRAM rows grey out...
  be.seed("/roms/plain.gb", gbRom());
  const gbPlain = sysItems("/roms/plain.gb");
  expect(findItem(gbPlain, "sys-quicksavesram")?.disabled).toBe(true);
  expect(findItem(gbPlain, "sys-savesram")?.disabled).toBe(true);
  be.seed("/roms/plain.nes", nesRom());
  const nesPlain = sysItems("/roms/plain.nes");
  expect(findItem(nesPlain, "sys-quicksavesram")?.disabled).toBe(true);
  expect(findItem(nesPlain, "sys-savesram")?.disabled).toBe(true);
  // ...and New SRAM… is hidden for a battery-less cart (it creates a save file — nothing to save here), like
  // the Save-SRAM rows; Load SRAM stays live (it seeds the running core, not an on-disk artifact).
  expect(findItem(nesPlain, "sys-newsram")).toBe(undefined);
  expect(findItem(nesPlain, "sys-loadsram")?.disabled).toBeFalsy();
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
  expect(stores.project.systems.view()[0].savPath).toBe("/in/x.sav"); // auto-save target repointed to the load
});

test("system New SRAM... opens a save dialog, boots blank, and repoints to the chosen file (ROM's .sav untouched)", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = systemMenu(be, stores); // ROM /roms/a.gb (battery)
  be.queueBrowse("/saves/fresh.sav");

  findItem(items, "sys-newsram")!.onSelect!();
  await flush();

  const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(last.saving).toBe(true); // a save (create) dialog, not an open
  expect(last.patterns.includes("*.sav")).toBeTruthy();
  const call = be.constructCalls[be.constructCalls.length - 1];
  expect(call.replaceId).toBe(id); // in-place replace
  const seed = new Uint8Array(call.sramBytes!);
  expect(seed.length).toBe(0x20000); // native truncates/zero-pads to the cart's real battery size
  expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
  expect(be.fileExists("/saves/fresh.sav")).toBe(true); // the fresh save was materialised at the pick
  expect(be.fileExists("/roms/a.sav")).toBe(false); // the ROM's own <rom>.sav was NOT overwritten
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
    stores.project.systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "lsdj-sync")!.config as { mode: string; tempoDivisor: number; autoStart: boolean };

  const mode = findItem(lsdjItems(), "lsdj-mode")!;
  expect(mode.kind).toBe("cycler");
  expect(mode.label).toBe("Mode: MIDI Sync"); // default MidiSync
  expect(cfg().mode).toBe("midiSync");

  // Enter/onSelect steps the mode forward (MidiSync → MidiSyncArduinoboy), applied through setRoleConfig (the live re-push path).
  mode.onSelect!();
  expect(cfg().mode).toBe("midiSyncArduinoboy");

  // Tempo Divisor cycler steps 1 → 2 (index 0 → 1 in [1,2,4,8]).
  expect(findItem(lsdjItems(), "lsdj-divisor")!.label).toBe("Tempo Divisor: 1");
  findItem(lsdjItems(), "lsdj-divisor")!.onSelect!();
  expect(cfg().tempoDivisor).toBe(2);

  // Auto Start toggles off → on, applied live (and the earlier mode/divisor edits are preserved by the merge).
  expect(findItem(lsdjItems(), "lsdj-autostart")!.label).toBe("Auto Start: Off"); // default off
  expect(cfg().autoStart).toBe(false);
  findItem(lsdjItems(), "lsdj-autostart")!.onSelect!();
  expect(cfg().autoStart).toBe(true);
  expect(cfg().mode).toBe("midiSyncArduinoboy"); // untouched by the autoStart edit
  expect(cfg().tempoDivisor).toBe(2);
});

// The Settings → Keyboard Bindings submenu (reachable from the start menu, no system needed).
function keyboardBindings(stores: AppStores): MenuItem[] {
  const settings = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  return submenuChildren(settings, "set-keybindings");
}

test("Keyboard Bindings: 8 GB + 3 app-action rows; capture rebinds write-through, clear + reset", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  const rows = keyboardBindings(stores);
  expect(rows.filter((r) => r.kind === "capture").length).toBe(11); // 8 GB buttons + Open Menu / Cycle / Cycle (Back)
  expect(findItem(rows, "bind-A")!.label).toBe("A: Z, z"); // resolved default baked into the label
  expect(findItem(rows, "bind-act-OpenMenu")!.label).toBe("Open Menu: Escape"); // app action seeded default
  expect(findItem(rows, "bind-act-CyclePrev")!.label).toBe("Cycle Instances (Back): -"); // unbound by default
  expect(findItem(rows, "bind-reset")!.kind).toBe("action");

  // Capture A → Q: the active keyboard profile persists and the resolved map + key→button lookup follow.
  findItem(rows, "bind-A")!.capture!.onCapture("Q");
  const active = stores.userConfig.config().activeKeyboardBindings;
  expect(stores.bindings.loadProfile(active)!.keyboard.A).toEqual(["Q"]); // written through to disk
  expect(buildKeyToButton(stores.bindings.resolvedBindings().keyboard).get("Q".charCodeAt(0))).toBe(BUTTON_VALUE.A);
  expect(keyboardBindings(stores).find((r) => r.id === "bind-A")!.label).toBe("A: Q"); // relabels on rebuild

  // Rebind Open Menu → M: written into keyboardActions; the action lookup follows.
  keyboardBindings(stores).find((r) => r.id === "bind-act-OpenMenu")!.capture!.onCapture("M");
  expect(stores.bindings.loadProfile(active)!.keyboardActions.OpenMenu).toEqual(["M"]);
  expect(buildKeyToAction(stores.bindings.resolvedBindings().keyboardActions).get("M".charCodeAt(0))).toBe("OpenMenu");

  // Clear A → the button unbinds.
  keyboardBindings(stores).find((r) => r.id === "bind-A")!.capture!.onClear();
  expect(stores.bindings.resolvedBindings().keyboard.A).toEqual([]);
  expect(keyboardBindings(stores).find((r) => r.id === "bind-A")!.label).toBe("A: -");

  // Reset restores BOTH the default keyboard map and the app actions (and preserves the gamepad channel).
  keyboardBindings(stores).find((r) => r.id === "bind-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().keyboard.A).toEqual(defaultBindingMap().keyboard.A);
  expect(stores.bindings.resolvedBindings().keyboardActions.OpenMenu).toEqual(["Escape"]); // action reset too
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
  expect(rows.filter((r) => r.kind === "capture").length).toBe(11); // 8 GB buttons + 3 app actions
  expect(findItem(rows, "bind-gp-A")!.capture!.source).toBe("gamepad"); // arms the pad bus, not the key bus
  expect(findItem(rows, "bind-gp-Up")!.label).toBe("Up: dpup, lefty-"); // default d-pad hat + left stick
  expect(findItem(rows, "bind-gp-act-OpenMenu")!.label).toBe("Open Menu: leftshoulder"); // seeded gamepad action
  expect(findItem(rows, "bind-gp-act-CycleNext")!.capture!.source).toBe("gamepad");

  // Capture A → a controller button (raw SDL name): written through to the active GAMEPAD profile.
  findItem(rows, "bind-gp-A")!.capture!.onCapture("y");
  const active = stores.userConfig.config().activeGamepadBindings;
  expect(stores.bindings.loadProfile(active)!.gamepad.A).toEqual(["y"]); // written through to disk
  expect(buildGamepadToButton(stores.bindings.resolvedBindings().gamepad).get("y")).toBe(BUTTON_VALUE.A);

  // Capture Up → a stick direction (half-axis token) — the analog-as-dpad binding form.
  gamepadBindings(stores).find((r) => r.id === "bind-gp-Up")!.capture!.onCapture("lefty-");
  expect(stores.bindings.resolvedBindings().gamepad.Up).toEqual(["lefty-"]);
  expect(buildGamepadToButton(stores.bindings.resolvedBindings().gamepad).get("lefty-")).toBe(BUTTON_VALUE.Up);

  // Bind Cycle (Back) → a stick click: written into gamepadActions; the action lookup follows.
  gamepadBindings(stores).find((r) => r.id === "bind-gp-act-CyclePrev")!.capture!.onCapture("leftstick");
  expect(stores.bindings.loadProfile(active)!.gamepadActions.CyclePrev).toEqual(["leftstick"]);
  expect(buildGamepadToAction(stores.bindings.resolvedBindings().gamepadActions).get("leftstick")).toBe("CyclePrev");

  // Clear A → unbinds; reset restores BOTH the default gamepad map and the app actions (preserving keyboard).
  gamepadBindings(stores).find((r) => r.id === "bind-gp-A")!.capture!.onClear();
  expect(stores.bindings.resolvedBindings().gamepad.A).toEqual([]);
  gamepadBindings(stores).find((r) => r.id === "bind-gp-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().gamepad.Up).toEqual(defaultBindingMap().gamepad.Up);
  expect(stores.bindings.resolvedBindings().gamepadActions.CyclePrev).toEqual([]); // action reset (back to unbound)
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
  be.seed("/music/song.rplg", "PK"); // present on disk → its select action is Load (not Locate)
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

  // A recent entry's Load also routes through ctx.loadProject (was a fire-and-forget project.load). The
  // entry is a single action row now, so its onSelect IS the load.
  const recentRows = submenuChildren(buildStartMenu(ctx).items, "start-recent");
  findItem(recentRows, "recent-0")!.onSelect!();
  expect(calls).toEqual(["new", "load:/picked/proj.rplg", "load:/music/song.rplg"]);
});

test("instance menu Save Project saves to the known path without browsing", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const anchored = stores.project.systems.view()[0];

  stores.project.save("/proj/p.rplg"); // establish a path (clears dirty)
  stores.project.setLayout("grid"); // Grid — re-dirties the project so the save has an observable effect
  expect(stores.project.isDirty()).toBe(true);

  findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-save")!.onSelect!();
  await flush();

  expect(stores.project.isDirty()).toBe(false); // saved in place, no Save-As dialog
  expect(stores.project.currentPath()).toBe("/proj/p.rplg");
});

test("instance menu Save Project shows a * marker only while there are unsaved changes", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  stores.project.systems.loadMgb(); // embedded synth: no romPath, so its battery is never counted SRAM-dirty
  const anchored = () => stores.project.systems.view()[0];
  const saveLabel = () => findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored() }).items, "inst-save")!.label;

  // Save to a path for a clean baseline (loadMgb dirtied the project) → no star, star purely tracks the project.
  stores.project.save("/proj/p.rplg");
  expect(stores.project.isDirty()).toBe(false);
  expect(saveLabel()).toBe("Save Project");

  // A settings edit re-dirties the project → the row wears a star until the next save.
  stores.project.setLayout("grid");
  expect(stores.project.isDirty()).toBe(true);
  expect(saveLabel()).toBe("Save Project *");
});

test("instance menu Save Project opens Save-As for a never-saved project", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb"); // a manually-built project — no path yet
  const anchored = stores.project.systems.view()[0];
  expect(stores.project.currentPath()).toBe("");
  be.queueBrowse("/picked/new.rplg"); // the interactive Save-As pick

  findItem(buildInstanceMenu({ ...ctxOf(stores), system: anchored }).items, "inst-save")!.onSelect!();
  await flush();

  expect(stores.project.currentPath()).toBe("/picked/new.rplg"); // adopted the picked path
  expect(stores.project.isDirty()).toBe(false);
});

test("instance menu New Project routes through the guarded ctx.newProject", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const anchored = stores.project.systems.view()[0];

  const calls: string[] = [];
  const ctx: MenuContext = { ...ctxOf(stores), newProject: () => calls.push("new") };

  findItem(buildInstanceMenu({ ...ctx, system: anchored }).items, "inst-new")!.onSelect!();
  expect(calls).toEqual(["new"]); // guarded op, NOT a raw project.newProject()
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

test("Settings -> Audio (standalone): cyclers stage a draft; Apply commits; label repaints, no live change until Apply", async () => {
  // Fake the SDL host's audio seam: a mutable live cfg, __rp_setAudioConfig commits into it.
  const live = { sampleRate: 48000, blockSize: 2048 };
  const g = globalThis as {
    __rp_isStandalone?: boolean;
    __rp_getAudioConfig?: () => { sampleRate: number; blockSize: number };
    __rp_setAudioConfig?: (r: number, b: number) => void;
  };
  g.__rp_isStandalone = true;
  g.__rp_getAudioConfig = () => ({ ...live });
  g.__rp_setAudioConfig = (r, b) => {
    live.sampleRate = r;
    live.blockSize = b;
  };
  const { resetAudioDraft } = await import("../../ui/screens/menu/audioDraft");
  resetAudioDraft(); // drop any draft leaked from a prior test in this file

  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be, notify: () => {} });
  const audioItems = () => submenuChildren(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings"), "set-audio");

  // Fresh: labels mirror the live device; Apply is inert (no pending change).
  let items = audioItems();
  expect(findItem(items, "audio-rate")!.label).toBe("Sample Rate: 48000 Hz");
  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 2048");
  expect(findItem(items, "audio-apply")!.disabled).toBe(true);

  // Stage a block-size change (a Left step, 2048 -> 1024): the DRAFT label moves, the live device does NOT,
  // and Apply becomes live.
  findItem(items, "audio-block")!.onCycle!(-1);
  items = audioItems(); // App re-renders on the draft bump; the test rebuilds to observe it
  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 1024");
  expect(live.blockSize).toBe(2048); // not applied yet
  expect(findItem(items, "audio-apply")!.disabled).toBeFalsy();

  // Apply commits to the device; the draft re-seeds and Apply goes inert again.
  findItem(items, "audio-apply")!.onSelect!();
  expect(live.blockSize).toBe(1024);
  items = audioItems();
  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 1024");
  expect(findItem(items, "audio-apply")!.disabled).toBe(true);

  resetAudioDraft();
  delete g.__rp_isStandalone;
  delete g.__rp_getAudioConfig;
  delete g.__rp_setAudioConfig;
});

test("Settings Default Render Dir: unset by default, Set persists to config, Clear resets (disabled when unset)", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const settings = () => submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  const renderDir = () => submenuChildren(settings(), "set-render-dir");

  // Unset by default → the label reads "(unset)" and Clear is disabled.
  expect(findItem(settings(), "set-render-dir")?.label).toBe("Default Render Dir: (unset)");
  expect(findItem(renderDir(), "set-render-dir-clear")?.disabled).toBe(true);

  // Set... opens a FOLDER picker, persists to render.outputDir, and the label reflects it.
  be.queueBrowse("/music/out");
  findItem(renderDir(), "set-render-dir-set")!.onSelect!();
  await flush();
  expect(stores.userConfig.config().render.outputDir).toBe("/music/out");
  expect(findItem(settings(), "set-render-dir")?.label).toBe("Default Render Dir: /music/out");
  expect(findItem(renderDir(), "set-render-dir-clear")?.disabled).toBeFalsy();

  // Clear → back to unset.
  findItem(renderDir(), "set-render-dir-clear")!.onSelect!();
  expect(stores.userConfig.config().render.outputDir).toBe("");
});

test("render Output Dir: defaults to the .sav / ROM folder; Settings then a session pick override it (never config)", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const dirRow = (id: number) => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    const sysMenu = submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-system");
    return findItem(submenuChildren(sysMenu, "sys-render"), "sys-render-dir")!;
  };

  // A battery cart whose .sav lives in a DIFFERENT folder than the ROM → the default is the .sav's folder.
  be.seed("/roms/game.gb", gbRomBattery());
  be.seed("/saves/mysong.sav", "battery");
  const battId = stores.project.systems.addSystem("/roms/game.gb", { explicitSav: "/saves/mysong.sav" })!;
  expect(dirRow(battId).label).toBe("Output Dir: /saves"); // the .sav's folder, not /roms

  // A non-battery cart → the default is the ROM's own folder.
  be.seed("/carts/tune.nes", nesRom());
  const nesId = stores.project.systems.addSystem("/carts/tune.nes")!;
  expect(dirRow(nesId).label).toBe("Output Dir: /carts");

  // A Settings default overrides the derived folder for every cart.
  stores.userConfig.setRenderOutputDir("/music/out");
  expect(dirRow(battId).label).toBe("Output Dir: /music/out");
  expect(dirRow(nesId).label).toBe("Output Dir: /music/out");

  // Picking in the render submenu is a per-SESSION override (wins), per-system, and does NOT touch config.
  be.queueBrowse("/tmp/session");
  dirRow(battId).onSelect!();
  await flush();
  expect(dirRow(battId).label).toBe("Output Dir: /tmp/session"); // session override wins for this cart
  expect(dirRow(nesId).label).toBe("Output Dir: /music/out"); // other systems still see the Settings default
  expect(stores.userConfig.config().render.outputDir).toBe("/music/out"); // Settings default untouched
});

test("the SameBoy display rows cycle their role config, gated by model and GB-only", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  const id = stores.project.systems.addSystem("/roms/a.gb")!;

  const items = () => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-system");
  };
  const row = (itemId: string) => findItem(items(), itemId)!;
  const cfg = () =>
    stores.project.systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "sameboy")!.config as Record<string, unknown>;
  const setModel = (m: string) => stores.project.systems.setRoleConfig(id, "sameboy", { model: m });

  // Defaults reproduce what the core did before these were configurable, so adding the rows can't
  // change how an existing project looks.
  expect(cfg().colorCorrection).toBe("disabled");
  expect(cfg().dmgPalette).toBe("grey");
  expect(cfg().lightTemperature).toBe(0);

  // --- the model gate ---------------------------------------------------------------------------
  // Default model is cgbC, so the CGB rows show and the DMG palette does not. Each row is only ever
  // offered where the core will actually use it (GB_is_cgb one way, its negation the other).
  expect(findItem(items(), "sys-color-correction")).toBeTruthy();
  expect(findItem(items(), "sys-light-temp")).toBeTruthy();
  expect(findItem(items(), "sys-dmg-palette")).toBe(undefined);

  // `auto` is a CGB model here — RetroPlug resolves it to CGB-C — so it keeps the CGB rows. This is
  // the case a naive "auto might be DMG" gate would get wrong.
  setModel("auto");
  expect(findItem(items(), "sys-color-correction")).toBeTruthy();
  expect(findItem(items(), "sys-dmg-palette")).toBe(undefined);

  // Every DMG-rendering model gets the palette row and loses the CGB pair — MGB and the Super Game
  // Boys render in DMG mode too, so gating on dmgB alone would hide a control that works.
  for (const m of ["dmgB", "mgb", "sgb", "sgbPal", "sgb2"]) {
    setModel(m);
    expect(findItem(items(), "sys-dmg-palette")).toBeTruthy();
    expect(findItem(items(), "sys-color-correction")).toBe(undefined);
    expect(findItem(items(), "sys-light-temp")).toBe(undefined);
  }
  // ...and every CGB-family model the reverse.
  for (const m of ["cgb0", "cgbA", "cgbB", "cgbC", "cgbD", "cgbE", "agb", "gbp"]) {
    setModel(m);
    expect(findItem(items(), "sys-dmg-palette")).toBe(undefined);
    expect(findItem(items(), "sys-color-correction")).toBeTruthy();
    expect(findItem(items(), "sys-light-temp")).toBeTruthy();
  }

  // --- the CGB rows (model is back on a CGB one) -------------------------------------------------
  setModel("cgbC");
  for (const rid of ["sys-color-correction", "sys-light-temp"]) {
    expect(row(rid).kind).toBe("cycler");
    expect(row(rid).keepOpen).toBeTruthy();
  }

  // Colour correction steps through its 7 modes in settingsEnums order and wraps.
  expect(row("sys-color-correction").label).toBe("Color Correction: Off");
  row("sys-color-correction").onCycle!(1);
  expect(cfg().colorCorrection).toBe("correctCurves");
  expect(row("sys-color-correction").label).toBe("Color Correction: Correct Curves");
  row("sys-color-correction").onCycle!(-1);
  expect(cfg().colorCorrection).toBe("disabled");
  row("sys-color-correction").onCycle!(-1); // wraps backwards to the last mode
  expect(cfg().colorCorrection).toBe("modernAccurate");
  row("sys-color-correction").onCycle!(1);
  expect(cfg().colorCorrection).toBe("disabled");

  // Light temperature is a cycler over a continuous value: it stores the double, and the row reflects it.
  expect(row("sys-light-temp").label).toBe("Light Temp: Neutral");
  row("sys-light-temp").onCycle!(1);
  expect(cfg().lightTemperature).toBe(0.2);
  expect(row("sys-light-temp").label).toBe("Light Temp: Warm 20%");
  row("sys-light-temp").onCycle!(-1);
  row("sys-light-temp").onCycle!(-1);
  expect(cfg().lightTemperature).toBe(-0.2);
  expect(row("sys-light-temp").label).toBe("Light Temp: Cool 20%");

  // An off-grid value (hand-edited project, or one written by a build with different steps) still shows
  // the nearest row rather than falling back to index 0 — that's what nearestIndex is for.
  stores.project.systems.setRoleConfig(id, "sameboy", { lightTemperature: 0.73 });
  expect(row("sys-light-temp").label).toBe("Light Temp: Warm 80%");
  // ...and it is NOT silently rewritten just by being displayed.
  expect(cfg().lightTemperature).toBe(0.73);

  // Out of range is clamped by the schema, not passed to the core.
  stores.project.systems.setRoleConfig(id, "sameboy", { lightTemperature: 99 });
  expect(cfg().lightTemperature).toBe(1);
  stores.project.systems.setRoleConfig(id, "sameboy", { lightTemperature: -99 });
  expect(cfg().lightTemperature).toBe(-1);

  // --- the DMG row ------------------------------------------------------------------------------
  setModel("dmgB");
  expect(row("sys-dmg-palette").kind).toBe("cycler");
  expect(row("sys-dmg-palette").label).toBe("DMG Palette: Grey");
  row("sys-dmg-palette").onCycle!(1);
  expect(cfg().dmgPalette).toBe("dmg");
  expect(row("sys-dmg-palette").label).toBe("DMG Palette: DMG");
  row("sys-dmg-palette").onCycle!(-1); // wraps backwards to the last palette
  expect(cfg().dmgPalette).toBe("grey");

  // Hiding a row doesn't discard its value: switch to CGB and back, and the palette is as it was.
  row("sys-dmg-palette").onCycle!(1);
  row("sys-dmg-palette").onCycle!(1);
  expect(cfg().dmgPalette).toBe("mgb");
  setModel("cgbC");
  expect(findItem(items(), "sys-dmg-palette")).toBe(undefined);
  setModel("dmgB");
  expect(row("sys-dmg-palette").label).toBe("DMG Palette: MGB");

  // NES carries no display rows — they're SameBoy knobs, and the mesen role has no such fields.
  be.seed("/roms/a.nes", nesRom());
  const nesId = stores.project.systems.addSystem("/roms/a.nes")!;
  const nesSys = stores.project.systems.view().find((s) => s.id === nesId)!;
  const nesItems = submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: nesSys }).items, "inst-system");
  for (const rid of ["sys-color-correction", "sys-dmg-palette", "sys-light-temp"]) {
    expect(findItem(nesItems, rid)).toBe(undefined);
  }
});
