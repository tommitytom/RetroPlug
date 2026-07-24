// The LSDj instance submenu's ASSET section (Kits / Fonts / Palettes) — closes a long-standing coverage gap:
// the risa asset menu was structurally tested (menu/risa.test.ts) but the LSDj one wasn't. Both now render
// through the generic assetMenu (src/tracker AssetCatalog), so this also guards the unified builder for LSDj.
// Build the MenuContext the way App.tsx does, read the tree, and drive the no-dialog actions.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "../../src/lsdj/rom";
import { gbRomBattery, nesRom } from "../systems/fixtures";

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
  };
}
const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// A 1 MiB image LsdjRom accepts (mirrors systems/lsdj-assets.test): GB logo + battery header, a version title
// ("LSDJ…" → the provider attaches lsdj-sync), and a 2-palette block so palette rows appear.
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

// The sole system's inst-lsdj children, re-queried by index so it survives a reloadSystem id swap.
function lsdjKids(be: MockBackend, stores: AppStores, romPath: string): () => MenuItem[] {
  be.seed(romPath, lsdjRom1M());
  stores.project.systems.addSystem(romPath);
  return () => submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-lsdj");
}

test("the LSDj submenu shows Kits / Fonts / Palettes asset submenus for a full ROM (not for a plain NES)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A plain NES ROM → no lsdj-sync role → no LSDj submenu.
  be.seed("/roms/plain.nes", nesRom());
  const plainId = stores.project.systems.addSystem("/roms/plain.nes")!;
  const plain = stores.project.systems.view().find((s) => s.id === plainId)!;
  expect(findItem(buildInstanceMenu({ ...ctxOf(stores), system: plain }).items, "inst-lsdj")).toBe(undefined);
  stores.project.systems.removeSystem(plainId);

  const kids = lsdjKids(be, stores, "/roms/song.gb");
  // Asset submenus in LSDj order: Kits, Fonts, Palettes.
  expect(kids().filter((k) => k.kind === "submenu" && k.id.startsWith("lsdj-") && k.id.endsWith("s") && !k.id.endsWith("songs")).map((k) => k.id))
    .toEqual(["lsdj-kits", "lsdj-fonts", "lsdj-palettes"]);

  // The 2-palette block → two palette rows, each with Export + Replace and no Remove Override yet.
  const palettes = submenuChildren(kids(), "lsdj-palettes");
  expect(palettes.map((p) => p.id)).toEqual(["lsdj-palette-0", "lsdj-palette-1"]);
  const p0 = submenuChildren(palettes, "lsdj-palette-0");
  expect(findItem(p0, "lsdj-palette-0-export")?.kind).toBe("action");
  expect(findItem(p0, "lsdj-palette-0-replace")?.kind).toBe("action");
  expect(findItem(p0, "lsdj-palette-0-delete")).toBe(undefined); // palettes aren't kits — no Delete
  expect(findItem(p0, "lsdj-palette-0-remove")).toBe(undefined); // no override yet

  // The Kits submenu leads with Add... (+ separator) — the addable affordance.
  const kits = submenuChildren(kids(), "lsdj-kits");
  expect(findItem(kits, "lsdj-kit-add")?.kind).toBe("action");
  expect(findItem(kits, "lsdj-kit-add-sep")?.kind).toBe("separator");
});

test("a palette override shows a * marker + a Remove Override row", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/song.gb");
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", {
    overrides: [{ type: "palette", slot: 1, name: "NEON", colorSets: [{ colors: [{ r: 0, g: 0, b: 0 }] }] }],
  });

  const palettes = submenuChildren(kids(), "lsdj-palettes");
  expect(findItem(palettes, "lsdj-palette-1")?.label).toBe("[1] NEON *"); // override name + the * marker
  expect(findItem(submenuChildren(palettes, "lsdj-palette-1"), "lsdj-palette-1-remove")?.kind).toBe("action");
});

test("a linked kit override shows a * marker + Delete + Remove Override, added to the effective kit list", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/song.gb");
  be.seed("/kits/drums.kit", new Uint8Array(BANK_SIZE)); // a 16 KB bank the override links by path
  stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", {
    overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.kit" }],
  });

  const kits = submenuChildren(kids(), "lsdj-kits");
  expect(kits.find((k) => k.id === "lsdj-kit-0")?.label).toBe("[0] DRUMS *");
  const rows = submenuChildren(kits, "lsdj-kit-0");
  expect(findItem(rows, "lsdj-kit-0-export")?.kind).toBe("action");
  expect(findItem(rows, "lsdj-kit-0-replace")?.kind).toBe("action");
  expect(findItem(rows, "lsdj-kit-0-delete")?.kind).toBe("action"); // kits get Delete
  expect(findItem(rows, "lsdj-kit-0-remove")?.kind).toBe("action");
});

test("Remove Override drops the override end-to-end (reload reverts the effective ROM to base)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const kids = lsdjKids(be, stores, "/roms/song.gb");
  const id = stores.project.systems.view()[0].id;
  stores.project.systems.setRoleConfig(id, "lsdj-assets", {
    overrides: [{ type: "palette", slot: 1, name: "NEON", colorSets: [{ colors: [{ r: 0, g: 0, b: 0 }] }] }],
  });
  expect(findItem(submenuChildren(kids(), "lsdj-palettes"), "lsdj-palette-1")?.label).toBe("[1] NEON *");

  // Remove Override: writes the emptied list + reloads → the row reverts (no *, no -remove).
  findItem(submenuChildren(submenuChildren(kids(), "lsdj-palettes"), "lsdj-palette-1"), "lsdj-palette-1-remove")!.onSelect!();
  const palettes = submenuChildren(kids(), "lsdj-palettes");
  expect(findItem(palettes, "lsdj-palette-1")?.label).toBe("[1] ABCD"); // back to the base name (no *)
  expect(findItem(submenuChildren(palettes, "lsdj-palette-1"), "lsdj-palette-1-remove")).toBe(undefined);
  // The last construct carries the base ROM (no override-patched romBytes).
  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
});
