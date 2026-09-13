// The BlipToaster instance submenu: gated on the `bliptoaster` marker role the ROM provider attaches to a BlipToaster
// cart. It is ASSET-ONLY (no song battery), so it exercises the songs-optional path — there must be NO Songs
// submenu, only the Kits + Fonts asset submenus. Mirrors test/menu/risa.test.ts (asset half).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { blipToasterRom, blipToasterMultiKitRom, nesRom } from "../systems/fixtures";
import { BlipToasterRom } from "../../src/bliptoaster/rom";

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
    openLsdjHd: () => {},
  };
}
const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

function blipToasterItems(be: MockBackend, stores: AppStores, path = "/roms/synth.nes"): () => MenuItem[] {
  be.seed(path, blipToasterRom());
  const id = stores.project.systems.addSystem(path)!;
  return () => buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items;
}

test("the BlipToaster submenu appears only for a BlipToaster ROM, is asset-only (no Songs), and lists Kits + Fonts", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A plain NES ROM (no "bliptoaster" marker) → no `bliptoaster` role → no submenu.
  be.seed("/roms/plain.nes", nesRom());
  const plainId = stores.project.systems.addSystem("/roms/plain.nes")!;
  const plain = stores.project.systems.view().find((s) => s.id === plainId)!;
  expect(findItem(buildInstanceMenu({ ...ctxOf(stores), system: plain }).items, "inst-bliptoaster")).toBe(undefined);

  // A BlipToaster ROM → the provider attaches `bliptoaster` → the submenu shows, asset-only (no Songs submenu).
  const items = blipToasterItems(be, stores);
  expect(findItem(items(), "inst-bliptoaster")?.kind).toBe("submenu");
  const kids = submenuChildren(items(), "inst-bliptoaster");
  expect(findItem(kids, "bliptoaster-songs")).toBe(undefined); // no song battery → no Songs submenu
  expect(findItem(kids, "bliptoaster-themes")?.kind).toBe("submenu");
  expect(findItem(kids, "bliptoaster-kits")?.kind).toBe("submenu");
  expect(findItem(kids, "bliptoaster-fonts")?.kind).toBe("submenu");
  expect(findItem(kids, "bliptoaster-settings")?.kind).toBe("submenu");
});

// ── Settings: the cart's baked rig defaults ────────────────────────────────────────────────────────────
// This submenu is how you CHOOSE among the baked themes / fonts / kits; the asset submenus above replace an
// entry's contents. Each row is a cycler over the ROM's own entries, and a change is pinned on the
// bliptoaster-assets role (folded into the ROM in memory at construct - the .nes on disk is untouched).
const settingsRows = (items: MenuItem[]) => submenuChildren(submenuChildren(items, "inst-bliptoaster"), "bliptoaster-settings");
const rowLabel = (items: MenuItem[], id: string) => findItem(settingsRows(items), id)?.label;
function cycle(items: MenuItem[], id: string, dir: 1 | -1 = 1): void {
  const row = findItem(settingsRows(items), id)!;
  expect(row.kind).toBe("cycler");
  row.onCycle!(dir);
}

test("the Settings rows show the ROM's own baked values, naming the entry each field selects", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const items = blipToasterItems(be, stores);
  // A ROM straight out of the build: every field at its power-on default. The Theme and Default Kit rows name
  // the ENTRY, read from the ROM, so a row says "DFLT" / "TEST" rather than a bare index.
  expect(rowLabel(items(), "bliptoaster-set-theme")).toBe("Theme: DFLT");
  expect(rowLabel(items(), "bliptoaster-set-font")).toBe("Font: Font 0");
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base Channel: BASE01");
  expect(rowLabel(items(), "bliptoaster-set-kit")).toBe("Default Kit: TEST");
  expect(rowLabel(items(), "bliptoaster-set-ppu")).toBe("PPU Enabled: On"); // the one field whose default is on
  expect(rowLabel(items(), "bliptoaster-set-curve")).toBe("Velocity Curve: Linear");
  // Nothing pinned yet, so there is nothing to apply and nothing to reset.
  expect(findItem(settingsRows(items()), "bliptoaster-set-apply")).toBe(undefined);
  expect(findItem(settingsRows(items()), "bliptoaster-set-reset")).toBe(undefined);
});

test("cycling a Settings row pins that field on the role and leaves the others unpinned", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/synth-set.nes", blipToasterRom());
  stores.project.systems.addSystem("/roms/synth-set.nes");
  // reloadSystem swaps the id, so re-read the system each time rather than caching the view.
  const items = () => {
    const sys = stores.project.systems.view()[0];
    return buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
  };
  const pinned = (): Record<string, unknown> =>
    stores.project.systems.view()[0].roles.find((r) => r.kind === "bliptoaster-assets")!.config.settings as Record<string, unknown>;

  cycle(items(), "bliptoaster-set-theme"); // 0 -> 1
  expect(rowLabel(items(), "bliptoaster-set-theme")).toBe("Theme: DARK");
  expect(pinned()).toEqual({ theme: 1 });

  cycle(items(), "bliptoaster-set-theme", -1); // 1 -> 0: still PINNED, at the same value the ROM bakes
  expect(pinned()).toEqual({ theme: 0 });
  cycle(items(), "bliptoaster-set-theme", -1); // 0 -> 15, wrapping the 16 the cart bakes
  expect(rowLabel(items(), "bliptoaster-set-theme")).toBe("Theme: AMBR");

  cycle(items(), "bliptoaster-set-ppu");
  expect(rowLabel(items(), "bliptoaster-set-ppu")).toBe("PPU Enabled: Off");
  expect(Object.keys(pinned()).sort()).toEqual(["ppu", "theme"]); // accumulated, not replaced
  expect(pinned().theme).toBe(15);
  expect(pinned().ppu).toBe(false);

  // A cycler pins but does NOT reboot - the ROM is read at startup only, so the reboot is its own row (which
  // keeps the menu alive across a step: reloadSystem swaps the id the instance menu is anchored to).
  expect(findItem(settingsRows(items()), "bliptoaster-set-apply")?.kind).toBe("action");

  // Reset clears the lot back to the ROM's bytes, and reboots so the cart actually comes up that way.
  findItem(settingsRows(items()), "bliptoaster-set-reset")!.onSelect!();
  expect(pinned()).toEqual({});
  expect(rowLabel(items(), "bliptoaster-set-theme")).toBe("Theme: DFLT");
  expect(rowLabel(items(), "bliptoaster-set-ppu")).toBe("PPU Enabled: On");
});

test("each Settings list is bounded by what THIS cart carries, not by the format's maximum", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  // The NROM fixture has one kit bank and one CHR bank, so those two rows have a single entry each and cannot
  // cycle - offering a slot the cart has no record for would bake an index it cannot use. Themes still wrap 16
  // (the table is there whatever the cart's banking is).
  const items = blipToasterItems(be, stores, "/roms/bounded.nes");
  cycle(items(), "bliptoaster-set-kit");
  expect(rowLabel(items(), "bliptoaster-set-kit")).toBe("Default Kit: TEST");
  cycle(items(), "bliptoaster-set-font");
  expect(rowLabel(items(), "bliptoaster-set-font")).toBe("Font: Font 0");
  cycle(items(), "bliptoaster-set-theme");
  expect(rowLabel(items(), "bliptoaster-set-theme")).toBe("Theme: DARK");
});

test("the Font row offers the 4 banks the banking ROM declares, and the Kit row its 16", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/synthbank-set.nes", blipToasterMultiKitRom());
  stores.project.systems.addSystem("/roms/synthbank-set.nes");
  const items = () => buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items;
  // Font wraps at 4 (the cart's FONT_COUNT), so stepping back from 0 lands on 3 rather than running past the
  // banks the ROM has.
  cycle(items(), "bliptoaster-set-font", -1);
  expect(rowLabel(items(), "bliptoaster-set-font")).toBe("Font: Font 3");
  // Kit 1 is a reserved (unpopulated) bank on this fixture, so it has no name to show - it is still selectable,
  // since the setting names a BANK, not a populated kit.
  cycle(items(), "bliptoaster-set-kit");
  expect(rowLabel(items(), "bliptoaster-set-kit")).toBe("Default Kit: Kit 1");
});

test("on a ROM predating the screen fields, Theme and Font are GREYED - not silently inert rows", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const rom = blipToasterRom();
  const at = 0x100 + 6 + 16 * 11; // the settings block
  rom.fill(0xff, at + 11, at + 16); // as a build predating the theme + font fields leaves it
  be.seed("/roms/oldsett.nes", rom);
  stores.project.systems.addSystem("/roms/oldsett.nes");
  const items = () => buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items;

  // This is the case behind the bug report: the cart ignores the byte, so a live cycler here would look exactly
  // like the feature being broken. It says why instead, and cannot be cycled.
  const theme = findItem(settingsRows(items()), "bliptoaster-set-theme")!;
  expect(theme.kind).toBe("action");
  expect(theme.disabled).toBe(true);
  expect(theme.label).toBe("Theme: DFLT (ROM Too Old)");
  expect(findItem(settingsRows(items()), "bliptoaster-set-font")?.disabled).toBe(true);

  // The four fields the block shipped with are unaffected - an old cart keeps the rest of the submenu.
  expect(findItem(settingsRows(items()), "bliptoaster-set-basech")?.kind).toBe("cycler");
  cycle(items(), "bliptoaster-set-basech");
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base Channel: BASE02");
});

test("a ROM with no readable settings block gets no Settings submenu (the asset submenus still show)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const rom = blipToasterRom();
  rom.fill(0, 0x100 + 6 + 16 * 11, 0x100 + 6 + 16 * 11 + 6); // wipe the SETT magic
  be.seed("/roms/nosett.nes", rom);
  const id = stores.project.systems.addSystem("/roms/nosett.nes")!;
  const kids = submenuChildren(
    buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items,
    "inst-bliptoaster",
  );
  expect(findItem(kids, "bliptoaster-settings")).toBe(undefined);
  expect(findItem(kids, "bliptoaster-themes")?.kind).toBe("submenu");
});

test("the Themes submenu lists ALL 16 baked themes with Export/Replace (no Remove until overridden)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const items = blipToasterItems(be, stores);
  const themes = submenuChildren(submenuChildren(items(), "inst-bliptoaster"), "bliptoaster-themes");
  // The cart bakes 16 and CC 16 switches between them live, so all 16 must be reachable here. A single row
  // was the bug this file pins: the count was hard-coded to 1 and the reader used risa's split layout.
  expect(themes.length).toBe(16);
  expect(themes.map((t) => t.label)).toEqual([
    "[0] DFLT", "[1] DARK", "[2] NEON", "[3] LITE", "[4] CRT", "[5] ICE", "[6] FIRE", "[7] GB",
    "[8] AQUA", "[9] MONO", "[10] PLSM", "[11] MTRX", "[12] FOG", "[13] SUN", "[14] MOON", "[15] AMBR",
  ]); // decoded, space-trimmed theme names
  for (const slot of [0, 15]) {
    const rows = submenuChildren(themes, `bliptoaster-theme-${slot}`);
    expect(findItem(rows, `bliptoaster-theme-${slot}-export`)?.kind).toBe("action");
    expect(findItem(rows, `bliptoaster-theme-${slot}-replace`)?.kind).toBe("action");
    expect(findItem(rows, `bliptoaster-theme-${slot}-remove`)).toBe(undefined);
  }
});

test("a theme override shows a * marker + a Remove Override row", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/synth3.nes", blipToasterRom());
  const id = stores.project.systems.addSystem("/roms/synth3.nes")!;
  stores.project.systems.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [
      {
        type: "theme",
        slot: 0,
        name: "NEON",
        theme: { name: "NEON", bg: "0x0D", normal: "0x30", shaded: "0x10", alternate: "0x20", status: "0x05", cursor: "0x15", selection: "0x25" },
      },
    ],
  });
  const themes = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-bliptoaster"),
    "bliptoaster-themes",
  );
  expect(themes[0].label).toBe("[0] NEON *");
  expect(findItem(submenuChildren(themes, "bliptoaster-theme-0"), "bliptoaster-theme-0-remove")?.kind).toBe("action");
});

test("the Kits + Fonts submenus list the base ROM's assets with Export/Replace (no Add/Delete — Replace-only)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const items = blipToasterItems(be, stores);
  const kids = () => submenuChildren(items(), "inst-bliptoaster");

  const kits = submenuChildren(kids(), "bliptoaster-kits");
  expect(findItem(kits, "bliptoaster-kit-add")).toBe(undefined); // single kit → not addable
  const k0 = kits.find((k) => k.id === "bliptoaster-kit-0")!; // the fixture's base "TEST" kit
  expect(k0.label).toBe("[0] TEST");
  const krows = submenuChildren(kits, "bliptoaster-kit-0");
  expect(findItem(krows, "bliptoaster-kit-0-export")?.kind).toBe("action");
  expect(findItem(krows, "bliptoaster-kit-0-replace")?.kind).toBe("action");
  expect(findItem(krows, "bliptoaster-kit-0-delete")).toBe(undefined); // not addable → no Delete
  expect(findItem(krows, "bliptoaster-kit-0-remove")).toBe(undefined); // no override yet

  const fonts = submenuChildren(kids(), "bliptoaster-fonts");
  const f0 = fonts.find((f) => f.id === "bliptoaster-font-0")!;
  expect(f0.label).toBe("[0] Font 0");
  const frows = submenuChildren(fonts, "bliptoaster-font-0");
  expect(findItem(frows, "bliptoaster-font-0-export")?.kind).toBe("action");
  expect(findItem(frows, "bliptoaster-font-0-replace")?.kind).toBe("action");
});

test("a banking ROM makes Kits addable (Add... + per-kit Delete) and shows a high-slot override row", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/synthbank.nes", blipToasterMultiKitRom()); // mapper 69 (FME-7) → 16 switchable kit banks
  const id = stores.project.systems.addSystem("/roms/synthbank.nes")!;
  const kitsOf = () =>
    submenuChildren(
      submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-bliptoaster"),
      "bliptoaster-kits",
    );

  // Kits is now addable (16 banks): leads with Add..., and the base kit row gains a Delete.
  expect(findItem(kitsOf(), "bliptoaster-kit-add")?.kind).toBe("action");
  expect(kitsOf().find((k) => k.id === "bliptoaster-kit-0")!.label).toBe("[0] TEST");
  expect(findItem(submenuChildren(kitsOf(), "bliptoaster-kit-0"), "bliptoaster-kit-0-delete")?.kind).toBe("action");

  // A linked override into slot 5 adds a second row [5] HATS * alongside the base kit.
  be.seed("/kits/hats.rkit", BlipToasterRom.fromBytes(blipToasterMultiKitRom()).getKitBank(0)!); // a real populated bank
  stores.project.systems.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [{ type: "kit", slot: 5, name: "HATS", path: "/kits/hats.rkit" }],
  });
  expect(kitsOf().find((k) => k.id === "bliptoaster-kit-0")).toBeTruthy(); // base kit still present
  expect(kitsOf().find((k) => k.id === "bliptoaster-kit-5")!.label).toBe("[5] HATS *");
  expect(findItem(submenuChildren(kitsOf(), "bliptoaster-kit-5"), "bliptoaster-kit-5-remove")?.kind).toBe("action");
});

test("a linked kit override shows a * marker + a Remove Override row", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/synth2.nes", blipToasterRom());
  const id = stores.project.systems.addSystem("/roms/synth2.nes")!;
  be.seed("/kits/drums.rkit", BlipToasterRom.fromBytes(blipToasterRom()).getKitBank(0)!); // a real populated bank
  stores.project.systems.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }],
  });

  const kits = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-bliptoaster"),
    "bliptoaster-kits",
  );
  expect(kits.find((k) => k.id === "bliptoaster-kit-0")!.label).toBe("[0] DRUMS *"); // override name + * marker
  expect(findItem(submenuChildren(kits, "bliptoaster-kit-0"), "bliptoaster-kit-0-remove")?.kind).toBe("action");
});
