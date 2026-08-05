// Baking the asset overrides into the ROM: the tracker submenu's two whole-ROM rows (Patch ROM in Place /
// Export Patched ROM...), built once for both consoles off the AssetMenuSpec. The overrides are otherwise
// non-destructive (folded onto the base ROM in memory at construct), which leaves a project depending on asset
// files spread around the disk; these rows write the EFFECTIVE ROM out so the cart stands alone. Same harness
// shape as menu/lsdj-assets.test.ts: build the MenuContext the way App.tsx does, drive the rows, assert the
// fake disk + the role config.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { LsdjRom, ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { lsdjAssetCatalog } from "../../src/tracker";
import { gbRomBattery, risaRomFull } from "../systems/fixtures";

// A row that browses (Export) fires openFileBrowser fire-and-forget; flush the microtask chain it kicks off.
const flush = async () => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};
const sameBytes = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((v, i) => v === b[i]);

function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    beginSongImport: () => {},
    requestExit: () => {},
  };
}
const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// The 1 MiB LSDj image menu/lsdj-assets.test.ts uses: GB logo + battery header, a version title (the provider
// attaches lsdj-sync), and a 2-palette block whose names are all "ABCD".
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  const count = 2;
  b.set(PALETTE_CHECK, 1 * BANK_SIZE + 0x100 + count * PALETTE_SIZE);
  const nb = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3 + 2 * count; i++) for (let j = 0; j < 4; j++) b[nb + i * 5 + j] = 0x41 + j;
  b[nb + 15 + 2 * count * 5 + 4] = 0x01;
  return b;
}

// A palette override with INLINE colours, so it applies with no linked file on disk (the simplest thing to bake).
const NEON = { type: "palette", slot: 1, name: "NEON", colorSets: [{ colors: [{ r: 31, g: 0, b: 31 }] }] };

// The sole system's tracker-submenu children, re-queried by index so it survives an id swap.
function lsdjKids(be: MockBackend, stores: AppStores, romPath: string): () => MenuItem[] {
  be.seed(romPath, lsdjRom1M());
  stores.project.systems.addSystem(romPath);
  return () => submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj");
}
const overridesOf = (stores: AppStores): unknown[] =>
  (stores.project.systems.view()[0].roles.find((r) => r.kind === "lsdj-assets")?.config.overrides ?? []) as unknown[];
const paletteName = (rom: Uint8Array, slot: number): string => LsdjRom.fromBytes(rom).palettes()[slot].name;

test("both bake rows are present but greyed until there's an override to bake", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/none.gb");

  // They sit at the tracker submenu's ROOT (whole-ROM ops), below the asset submenus.
  expect(findItem(kids(), "lsdj-patch-rom")?.kind).toBe("prompt");
  expect(findItem(kids(), "lsdj-export-patched-rom")?.kind).toBe("action");
  expect(findItem(kids(), "lsdj-patch-rom")?.disabled).toBe(true);
  expect(findItem(kids(), "lsdj-export-patched-rom")?.disabled).toBe(true);

  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides: [NEON] });
  expect(findItem(kids(), "lsdj-patch-rom")?.disabled).toBeFalsy();
  expect(findItem(kids(), "lsdj-export-patched-rom")?.disabled).toBeFalsy();
});

test("Patch ROM in Place bakes the overrides into the ROM on disk and clears them from the project", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/bake.gb");
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides: [NEON] });
  const base = be.readFile("/roms/bake.gb")!;
  const constructs = be.constructCalls.length;

  const row = findItem(kids(), "lsdj-patch-rom")!;
  expect(row.prompt!.confirm).toBeTruthy(); // an irreversible write always confirms
  expect(row.prompt!.title).toBe("Overwrite bake.gb with the patched ROM?");
  expect(row.prompt!.onConfirm("")).toBe(null); // null = success, overlay closes

  // The ROM on disk now carries the override.
  const written = be.readFile("/roms/bake.gb")!;
  expect(paletteName(base, 1)).toBe("ABCD");
  expect(paletteName(written, 1)).toBe("NEON");
  // The override is gone from the project (it's in the ROM now), and the rows grey out again.
  expect(overridesOf(stores)).toEqual([]);
  expect(findItem(kids(), "lsdj-patch-rom")?.disabled).toBe(true);
  // The palette row keeps the name but loses its *, reading the REPATCHED base ROM (the menu's rom cache
  // was evicted; without that it would still be parsing the pre-patch bytes).
  expect(findItem(submenuChildren(kids(), "lsdj-palettes"), "lsdj-palette-1")?.label).toBe("[1] NEON");
  // No cold boot: the effective ROM is byte-identical to what the core is already running.
  expect(be.constructCalls.length).toBe(constructs);
});

test("the baked ROM is byte-identical to the image construct was already handing the core", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/same.gb");
  const id = stores.project.systems.view()[0].id;
  stores.project.systems.setRoleConfig(id, "lsdj-assets", { overrides: [NEON] });
  stores.project.systems.reloadSystem(id); // construct folds the override in -> the EFFECTIVE image
  const effective = new Uint8Array(be.constructCalls[be.constructCalls.length - 1].romBytes!);

  findItem(kids(), "lsdj-patch-rom")!.prompt!.onConfirm("");

  // What baking wrote IS that image, which is why no cold boot is needed after it.
  expect(sameBytes(be.readFile("/roms/same.gb")!, effective)).toBe(true);
  // And the cart now stands alone: the asset sits in the ROM's OWN base slots, so it needs no override (and
  // no asset file on disk) to come back.
  expect(lsdjAssetCatalog.baseSlots(be.readFile("/roms/same.gb")!, "palette")[1].name).toBe("NEON");
});

test("Patch ROM in Place refuses when the patcher doesn't recognise the ROM (it would clear the list for nothing)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  // A GB image titled bare "LSDJ" (no version): the provider still attaches lsdj-sync + lsdj-assets on the
  // title prefix, so the submenu is live, but LsdjRom.isLsdj is false. The patcher bails BEFORE its
  // per-override loop, so it reports no skips - "applied nothing" must not read as success. Reachable
  // whenever the file at romPath is no longer the one the overrides were recorded against.
  const rom = lsdjRom1M();
  for (let i = 0; i < 12; i++) rom[0x134 + i] = i < 4 ? "LSDJ".charCodeAt(i) : 0;
  be.seed("/roms/bare.gb", rom);
  const id = stores.project.systems.addSystem("/roms/bare.gb")!;
  const kids = () => submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj");
  stores.project.systems.setRoleConfig(id, "lsdj-assets", { overrides: [NEON] });

  const err = findItem(kids(), "lsdj-patch-rom")!.prompt!.onConfirm("");
  expect(err != null).toBe(true);
  expect(err!.includes("bare.gb")).toBe(true);
  expect(sameBytes(be.readFile("/roms/bare.gb")!, rom)).toBe(true); // nothing written
  expect(overridesOf(stores).length).toBe(1); // and the override kept, so the link survives
});

test("a failed write leaves the override list intact (the clear happens only after the ROM lands)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/ro.gb");
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides: [NEON] });
  be.writeFileAtomic = () => false; // a read-only ROM / full disk, as systems/lsdj-lsdprj-safety.test.ts does

  const err = findItem(kids(), "lsdj-patch-rom")!.prompt!.onConfirm("");
  expect(err).toBe("Could not write ro.gb");
  expect(overridesOf(stores).length).toBe(1);
});

test("Patch ROM in Place refuses (and writes nothing) when an override can't be applied", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/broken.gb");
  const base = be.readFile("/roms/broken.gb")!;
  // A kit override whose linked file has been moved/deleted. Construct would silently skip it, but baking it
  // out would discard a link the user could still repair.
  const overrides = [NEON, { type: "kit", slot: 3, name: "GONE", path: "/kits/missing.kit" }];
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides });

  const err = findItem(kids(), "lsdj-patch-rom")!.prompt!.onConfirm("");
  expect(err != null).toBe(true); // a message keeps the overlay open, shown red
  expect(err!.includes("kit 3")).toBe(true);
  expect(sameBytes(be.readFile("/roms/broken.gb")!, base)).toBe(true); // strict: nothing written
  expect(overridesOf(stores).length).toBe(2); // and nothing dropped
});

test("Export Patched ROM... writes the effective ROM elsewhere, leaving the ROM and the project alone", async () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/song.gb");
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides: [NEON] });
  const base = be.readFile("/roms/song.gb")!;
  be.queueBrowse("/out/song-patched.gb");

  findItem(kids(), "lsdj-export-patched-rom")!.onSelect!();
  await flush();

  const dialog = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
  expect(dialog.saving).toBe(true);
  expect(dialog.defaultName).toBe("song-patched.gb"); // <stem>-patched<ext>, the base ROM's own extension
  expect(paletteName(be.readFile("/out/song-patched.gb")!, 1)).toBe("NEON"); // the baked image
  expect(sameBytes(be.readFile("/roms/song.gb")!, base)).toBe(true); // base ROM untouched
  expect(overridesOf(stores).length).toBe(1); // and the project still carries the override
});

test("risa gets the same two rows, and patching bakes a theme into the .nes", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/bake.nes", risaRomFull());
  const id = stores.project.systems.addSystem("/roms/bake.nes")!;
  const kids = () => submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-risa");
  expect(findItem(kids(), "risa-patch-rom")?.disabled).toBe(true); // the shared builder, not an LSDj-only row

  stores.project.systems.setRoleConfig(id, "risa-assets", {
    overrides: [
      { type: "theme", slot: 1, name: "NEON", theme: { name: "NEON", bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25" } },
    ],
  });
  expect(findItem(submenuChildren(kids(), "risa-themes"), "risa-theme-1")?.label).toBe("[1] NEON *"); // override

  expect(findItem(kids(), "risa-patch-rom")!.prompt!.onConfirm("")).toBe(null);

  // Baked into the .nes: the theme survives with no override behind it (no *), read back off the new bytes.
  expect(findItem(submenuChildren(kids(), "risa-themes"), "risa-theme-1")?.label).toBe("[1] NEON");
  expect((stores.project.systems.view()[0].roles.find((r) => r.kind === "risa-assets")?.config.overrides as unknown[]).length).toBe(0);
});
