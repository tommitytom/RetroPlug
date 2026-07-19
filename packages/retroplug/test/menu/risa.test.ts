// The risa instance submenu (M2 UI): gated on the `risa` role the ROM provider attaches to a risa cart,
// listing the RSAV catalog's songs with Delete + reorder rows. Mirrors the LSDj submenu test in
// leaves.test.ts: build the MenuContext the way App.tsx does, drive a leaf, assert the store mutated.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { risaRom, risaRomFull, nesRom } from "../systems/fixtures";
import { RisaRom } from "../../src/risa/rom";
import { savBytes } from "../risa/fixtures";
import { normalizeSaveContainer, listSongs, expandRecordToWorking, recordBytesAt, CURRENT_LAYOUT } from "../../src/risaSav";

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

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
// The 5-song legacy catalog (HOU8, HOU, DBZ, DBZ2-F, FUNK0), normalized to the 64 KB image readSram returns.
const CATALOG = () => normalizeSaveContainer(savBytes("multi_legacy")).save;

function risaSystem(be: MockBackend, stores: AppStores): { id: number; items: () => MenuItem[] } {
  be.seed("/roms/song.nes", risaRom());
  const id = stores.project.systems.addSystem("/roms/song.nes")!;
  be.setSram(id, CATALOG()); // what readSram returns → what the Songs submenu lists
  const items = () => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    return buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
  };
  return { id, items };
}

test("the risa submenu appears only for a risa ROM and lists the catalog's songs", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A plain NES ROM → no `risa` role → no risa submenu.
  be.seed("/roms/plain.nes", nesRom());
  const plainId = stores.project.systems.addSystem("/roms/plain.nes")!;
  const plain = stores.project.systems.view().find((s) => s.id === plainId)!;
  expect(findItem(buildInstanceMenu({ ...ctxOf(stores), system: plain }).items, "inst-risa")).toBe(undefined);

  // A risa ROM → the provider attaches `risa` → the submenu shows, listing the 5 songs.
  const { items } = risaSystem(be, stores);
  expect(findItem(items(), "inst-risa")?.kind).toBe("submenu");
  const songs = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");
  expect(songs.map((s) => s.label)).toEqual(["[0] HOU8", "[1] HOU", "[2] DBZ", "[3] DBZ2-F", "[4] FUNK0"]);
});

test("each risa song row offers Delete + reorder, with Move disabled at the ends", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { items } = risaSystem(be, stores);
  const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");

  const first = submenuChildren(songRows, "risa-song-0");
  expect(findItem(first, "risa-song-0-load")?.kind).toBe("action");
  expect(findItem(first, "risa-song-0-delete")?.kind).toBe("prompt");
  expect(findItem(first, "risa-song-0-up")?.disabled).toBe(true); // first song can't move up
  expect(findItem(first, "risa-song-0-down")?.disabled).toBeFalsy();

  const last = submenuChildren(songRows, "risa-song-4");
  expect(findItem(last, "risa-song-4-down")?.disabled).toBe(true); // last song can't move down
  expect(findItem(last, "risa-song-4-up")?.disabled).toBeFalsy();
});

test("Delete confirms then removes the song end-to-end (readSram -> op -> loadSram)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { id, items } = risaSystem(be, stores);
  const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");

  const del = findItem(submenuChildren(songRows, "risa-song-2"), "risa-song-2-delete")!; // DBZ
  expect(del.prompt!.confirm).toBeTruthy();
  del.prompt!.onConfirm(""); // confirm

  // The op wrote the edited catalog back and cold-booted the core from it: the last construct carries a
  // 4-song catalog (DBZ gone).
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  expect(listSongs(new Uint8Array(spec.sramBytes!)).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ2-F", "FUNK0"]);
});

test("Move Down reorders the catalog end-to-end", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { items } = risaSystem(be, stores);
  const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");

  findItem(submenuChildren(songRows, "risa-song-0"), "risa-song-0-down")!.onSelect!(); // HOU8 down one
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(listSongs(new Uint8Array(spec.sramBytes!)).map((s) => s.name)).toEqual(["HOU", "HOU8", "DBZ", "DBZ2-F", "FUNK0"]);
});

test("Load expands the selected song into working memory end-to-end (current-layout catalog)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/song.nes", risaRom());
  const id = stores.project.systems.addSystem("/roms/song.nes")!;
  const battery = normalizeSaveContainer(savBytes("v2_blumarbl")).save; // current v2, one song: BLUMARBL
  be.setSram(id, battery);
  const sys = () => stores.project.systems.view().find((s) => s.id === id)!;
  const songRows = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys() }).items, "inst-risa"),
    "risa-songs",
  );

  findItem(submenuChildren(songRows, "risa-song-0"), "risa-song-0-load")!.onSelect!();

  // The cold-booted core carries a battery whose working song (banks 0-3) is the expanded BLUMARBL, and
  // whose catalog (banks 4-7) is unchanged.
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  const out = new Uint8Array(spec.sramBytes!);
  expect(sameBytes(out.slice(0, 0x8000), expandRecordToWorking(recordBytesAt(battery, CURRENT_LAYOUT, 0)!))).toBe(true);
  expect(sameBytes(out.slice(0x8000), battery.slice(0x8000))).toBe(true);
});

test("Load is a safe no-op on a legacy-layout catalog (no cold-boot)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const { items } = risaSystem(be, stores); // seeds the legacy multi_legacy catalog
  const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");
  const before = be.constructCalls.length;

  findItem(submenuChildren(songRows, "risa-song-0"), "risa-song-0-load")!.onSelect!();
  expect(be.constructCalls.length).toBe(before); // loadSongToWorkingInSav returned null -> mutateSavBytes no-op
});

// A distinct romPath from the header-prefix risaRom() the Songs tests use — the asset inventory is memoised
// by romPath, and a full ROM (theme table + CHR) is needed for RisaRom.isRisa.
function risaAssetSystem(be: MockBackend, stores: AppStores): () => MenuItem[] {
  be.seed("/roms/full.nes", risaRomFull());
  const id = stores.project.systems.addSystem("/roms/full.nes")!;
  return () => submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-risa");
}

test("the risa submenu shows Themes (16) + Fonts (4) asset submenus for a full ROM", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const risaKids = risaAssetSystem(be, stores);

  const themes = submenuChildren(risaKids(), "risa-themes");
  const fonts = submenuChildren(risaKids(), "risa-fonts");
  expect(themes.length).toBe(16);
  expect(fonts.length).toBe(4);
  expect(themes[0].label).toBe("[0] TH0"); // decoded, space-trimmed theme name
  expect(fonts[3].label).toBe("[3] Font 3");

  // Each theme/font row offers Export + Replace (no Remove Override until one exists).
  const t0 = submenuChildren(themes, "risa-theme-0");
  expect(findItem(t0, "risa-theme-0-export")?.kind).toBe("action");
  expect(findItem(t0, "risa-theme-0-replace")?.kind).toBe("action");
  expect(findItem(t0, "risa-theme-0-remove")).toBe(undefined);
});

test("a theme override shows a * marker + a Remove Override row", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/full2.nes", risaRomFull());
  const id = stores.project.systems.addSystem("/roms/full2.nes")!;
  stores.project.systems.setRoleConfig(id, "risa-assets", {
    overrides: [{ type: "theme", slot: 1, name: "NEON", theme: { name: "NEON", bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25" } }],
  });
  const themes = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-risa"),
    "risa-themes",
  );
  expect(themes[1].label).toBe("[1] NEON *"); // override name + the * marker
  const t1 = submenuChildren(themes, "risa-theme-1");
  expect(findItem(t1, "risa-theme-1-remove")?.kind).toBe("action");
});

test("the risa submenu shows a Kits submenu with Add... + the base kit rows", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const risaKids = risaAssetSystem(be, stores);

  const kits = submenuChildren(risaKids(), "risa-kits");
  expect(findItem(kits, "risa-kit-add")?.kind).toBe("action"); // leads with Add...
  const k0 = kits.find((k) => k.id === "risa-kit-0")!; // the fixture's base "TEST" kit
  expect(k0.label).toBe("[0] TEST");

  // A kit row offers Export / Replace / Delete, no Remove Override until one exists.
  const rows = submenuChildren(kits, "risa-kit-0");
  expect(findItem(rows, "risa-kit-0-export")?.kind).toBe("action");
  expect(findItem(rows, "risa-kit-0-replace")?.kind).toBe("action");
  expect(findItem(rows, "risa-kit-0-delete")?.kind).toBe("action"); // kits get Delete
  expect(findItem(rows, "risa-kit-0-remove")).toBe(undefined);
});

test("a linked kit override shows a * marker + Remove Override, listed alongside the base kit", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/kfull.nes", risaRomFull());
  const id = stores.project.systems.addSystem("/roms/kfull.nes")!;
  be.seed("/kits/drums.rkit", RisaRom.fromBytes(risaRomFull()).getKitBank(0)!); // a real populated bank
  stores.project.systems.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 5, name: "DRUMS", path: "/kits/drums.rkit" }] });

  const kits = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-risa"),
    "risa-kits",
  );
  expect(kits.find((k) => k.id === "risa-kit-5")!.label).toBe("[5] DRUMS *"); // override name + * marker
  expect(kits.some((k) => k.id === "risa-kit-0")).toBe(true); // base kit still listed
  expect(findItem(submenuChildren(kits, "risa-kit-5"), "risa-kit-5-remove")?.kind).toBe("action");
});

test("Delete on a base kit records an erase override and empties the effective slot end-to-end", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/kdel.nes", risaRomFull());
  const id = stores.project.systems.addSystem("/roms/kdel.nes")!;
  const kits = () => submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items, "inst-risa"),
    "risa-kits",
  );
  expect(kits().some((k) => k.id === "risa-kit-0")).toBe(true);

  findItem(submenuChildren(kits(), "risa-kit-0"), "risa-kit-0-delete")!.onSelect!(); // Delete the base "TEST" kit

  // The erase override drops the slot from the effective list AND the reload handed native a patched ROM
  // whose slot 0 is unpopulated. (reloadSystem swaps the id, so re-query the sole system by index.)
  expect(kits().some((k) => k.id === "risa-kit-0")).toBe(false);
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  expect(RisaRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false);
});
