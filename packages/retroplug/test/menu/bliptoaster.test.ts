// The BlipToaster instance submenu: gated on the `bliptoaster` marker role the ROM provider attaches to a BlipToaster
// cart. It is ASSET-ONLY (no song battery), so it exercises the songs-optional path — there must be NO Songs
// submenu, only the Kits + Fonts asset submenus. Mirrors test/menu/risa.test.ts (asset half).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuAction, MenuItem } from "../../ui/screens/menu/menuTree";
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
/** An inline action-cycler row's verb list — the form every asset row takes (one row, its own actions). */
function actionsOf(items: MenuItem[], id: string): MenuAction[] {
  const row = items.find((i) => i.id === id);
  return row && row.kind === "actionCycler" ? row.actions ?? [] : [];
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
  // The settings rows live in THIS list, not behind a "Settings" submenu of their own.
  expect(findItem(kids, "bliptoaster-settings")).toBe(undefined);
  expect(findItem(kids, "bliptoaster-set-basech")?.kind).toBe("cycler");
});

// ── Settings: the cart's baked rig defaults ────────────────────────────────────────────────────────────
// These rows are how you CHOOSE among the baked themes / fonts / kits; the asset submenus below replace an
// entry's contents. Each row is a cycler over the ROM's own entries, and a change is pinned on the
// bliptoaster-assets role (folded into the ROM in memory at construct - the .nes on disk is untouched). They
// sit at the top of the BlipToaster menu itself, so this is just its child list.
const settingsRows = (items: MenuItem[]) => submenuChildren(items, "inst-bliptoaster");
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
  // A ROM straight out of the build: every field at its power-on default.
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base MIDI Channel: 01");
  // No row for anything the menu names ELSEWHERE: theme and font are picked in their own lists (a `*` on the
  // live slot, `Select` on the rest), and the DMC kit with ch5 CC 14.
  expect(findItem(settingsRows(items()), "bliptoaster-set-theme")).toBe(undefined);
  expect(findItem(settingsRows(items()), "bliptoaster-set-font")).toBe(undefined);
  expect(findItem(settingsRows(items()), "bliptoaster-set-kit")).toBe(undefined);
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

  cycle(items(), "bliptoaster-set-basech"); // 0 -> 1
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base MIDI Channel: 02");
  expect(pinned()).toEqual({ baseChannel: 1 });

  cycle(items(), "bliptoaster-set-basech", -1); // 1 -> 0: still PINNED, at the same value the ROM bakes
  expect(pinned()).toEqual({ baseChannel: 0 });

  cycle(items(), "bliptoaster-set-ppu");
  expect(rowLabel(items(), "bliptoaster-set-ppu")).toBe("PPU Enabled: Off");
  expect(Object.keys(pinned()).sort()).toEqual(["baseChannel", "ppu"]); // accumulated, not replaced
  expect(pinned().baseChannel).toBe(0);
  expect(pinned().ppu).toBe(false);

  // A cycler pins but does NOT reboot - it tells the RUNNING cart instead, with the rig SysEx, so there is no
  // "Apply" row to reboot into the change. (That also keeps the menu alive across a step: reloadSystem swaps
  // the id the instance menu is anchored to.) The send goes to THIS system, bypassing the musical routing.
  expect(findItem(settingsRows(items()), "bliptoaster-set-apply")).toBe(undefined);
  const sent = be.stageSystemMidiCalls;
  expect(sent.length).toBe(3); // one per cycle above, each declaring the whole block
  expect(sent[sent.length - 1].id).toBe(stores.project.systems.view()[0].id);
  // F0 7D 42 03 <baseCh> <ppu> <curve> <theme> <font> F7 - the pins just made (base 0, ppu off) inside it.
  expect(sent[sent.length - 1].bytes).toEqual([0xf0, 0x7d, 0x42, 0x03, 0, 0, 0, 0, 0, 0xf7]);

  // Reset clears the lot back to the ROM's bytes, and reboots so the cart actually comes up that way.
  findItem(settingsRows(items()), "bliptoaster-set-reset")!.onSelect!();
  expect(pinned()).toEqual({});
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base MIDI Channel: 01");
  expect(rowLabel(items(), "bliptoaster-set-ppu")).toBe("PPU Enabled: On");
});

test("what can be SELECTED is exactly what the cart carries - the list is the bound", () => {
  // This used to be two cases pinning the Theme and Font CYCLERS' wrap points against the ROM's table sizes.
  // Selecting from the list makes that structural instead of arithmetic: a slot the cart has no record for has
  // no row, so it has no Select either, and there is no index left to get wrong. The NROM fixture has ONE CHR
  // bank and sixteen themes, and the lists say so.
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const items = blipToasterItems(be, stores, "/roms/bounded.nes");
  const kids = () => submenuChildren(items(), "inst-bliptoaster");

  const fonts = submenuChildren(kids(), "bliptoaster-fonts");
  expect(fonts.map((f) => f.label)).toEqual(["[0] Font 0 *"]); // the one bank, and it is the live one
  expect(actionsOf(fonts, "bliptoaster-font-0").some((a) => a.id === "bliptoaster-font-0-select")).toBe(false);

  const themes = submenuChildren(kids(), "bliptoaster-themes");
  expect(themes.length).toBe(16);
  expect(themes.filter((t) => t.label.includes("*")).length).toBe(1); // exactly one live theme
});

test("a ROM whose block leaves the screen fields reserved still gets live rows, reading them as 0", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const rom = blipToasterRom();
  const at = 0x100 + 6 + 16 * 11; // the settings block
  rom.fill(0xff, at + 11, at + 16); // the reserved tail, as a build predating the theme + font fields left it
  be.seed("/roms/oldsett.nes", rom);
  stores.project.systems.addSystem("/roms/oldsett.nes");
  const items = () => buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view()[0] }).items;

  // The theme/font rows used to be greyed as "(ROM Too Old)" on exactly this ROM, from when carts in the wild
  // predated the fields; nothing unreleased does. The 0xFF decodes to slot 0, so slot 0 is simply the live one
  // and the list is fully usable.
  const themes = submenuChildren(submenuChildren(items(), "inst-bliptoaster"), "bliptoaster-themes");
  expect(themes[0].label).toBe("[0] DFLT *");
  expect(actionsOf(themes, "bliptoaster-theme-1")[0].id).toBe("bliptoaster-theme-1-select");

  expect(findItem(settingsRows(items()), "bliptoaster-set-basech")?.kind).toBe("cycler");
  cycle(items(), "bliptoaster-set-basech");
  expect(rowLabel(items(), "bliptoaster-set-basech")).toBe("Base MIDI Channel: 02");
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
  expect(findItem(kids, "bliptoaster-set-basech")).toBe(undefined); // no readable block → no settings rows
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
    "[0] DFLT *", "[1] DARK", "[2] NEON", "[3] LITE", "[4] CRT", "[5] ICE", "[6] FIRE", "[7] GB",
    "[8] AQUA", "[9] MONO", "[10] PLSM", "[11] MTRX", "[12] FOG", "[13] SUN", "[14] MOON", "[15] AMBR",
  ]); // decoded, space-trimmed theme names
  // The live slot offers no Select (there is nothing to select); every other one leads with it, so landing on
  // a row offers the thing the list is for.
  expect(actionsOf(themes, "bliptoaster-theme-0").map((a) => a.id))
    .toEqual(["bliptoaster-theme-0-export", "bliptoaster-theme-0-replace"]);
  expect(actionsOf(themes, "bliptoaster-theme-15").map((a) => a.id))
    .toEqual(["bliptoaster-theme-15-select", "bliptoaster-theme-15-export", "bliptoaster-theme-15-replace"]);
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
  // Slot 0 is BOTH the live theme (the cart's baked default) and the overridden one, so it wears both markers -
  // they are different facts about a slot and neither hides the other.
  expect(themes[0].label).toBe("[0] NEON *~");
  expect(actionsOf(themes, "bliptoaster-theme-0").some((a) => a.id === "bliptoaster-theme-0-remove")).toBe(true);
  // ...and being live, it offers no Select: there is nothing to select.
  expect(actionsOf(themes, "bliptoaster-theme-0").some((a) => a.id === "bliptoaster-theme-0-select")).toBe(false);
  expect(actionsOf(themes, "bliptoaster-theme-1")[0].id).toBe("bliptoaster-theme-1-select"); // leads on the rest
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
  // A kit is one inline row carrying its own verbs (Left/Right pick, Enter runs) — no submenu to descend into.
  expect(k0.kind).toBe("actionCycler");
  expect(actionsOf(kits, "bliptoaster-kit-0").map((a) => a.id)).toEqual([
    "bliptoaster-kit-0-export",
    "bliptoaster-kit-0-replace",
    // not addable → no Delete; no override yet → no Remove Override
  ]);

  const fonts = submenuChildren(kids(), "bliptoaster-fonts");
  const f0 = fonts.find((f) => f.id === "bliptoaster-font-0")!;
  expect(f0.label).toBe("[0] Font 0 *"); // the live font, and the only bank on this cart
  expect(actionsOf(fonts, "bliptoaster-font-0").map((a) => a.id)).toEqual(["bliptoaster-font-0-export", "bliptoaster-font-0-replace"]);
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
  expect(actionsOf(kitsOf(), "bliptoaster-kit-0").some((a) => a.id === "bliptoaster-kit-0-delete")).toBe(true);

  // A linked override into slot 5 adds a second row [5] HATS ~ alongside the base kit.
  be.seed("/kits/hats.rkit", BlipToasterRom.fromBytes(blipToasterMultiKitRom()).getKitBank(0)!); // a real populated bank
  stores.project.systems.setRoleConfig(id, "bliptoaster-assets", {
    overrides: [{ type: "kit", slot: 5, name: "HATS", path: "/kits/hats.rkit" }],
  });
  expect(kitsOf().find((k) => k.id === "bliptoaster-kit-0")).toBeTruthy(); // base kit still present
  expect(kitsOf().find((k) => k.id === "bliptoaster-kit-5")!.label).toBe("[5] HATS ~");
  expect(actionsOf(kitsOf(), "bliptoaster-kit-5").some((a) => a.id === "bliptoaster-kit-5-remove")).toBe(true);
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
  expect(kits.find((k) => k.id === "bliptoaster-kit-0")!.label).toBe("[0] DRUMS ~"); // override name + the ~ marker (* is the LIVE slot)
  expect(actionsOf(kits, "bliptoaster-kit-0").some((a) => a.id === "bliptoaster-kit-0-remove")).toBe(true);
});

test("Select on a theme row makes it the live one: the star moves, the pin lands, the cart is told", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/sel.nes", blipToasterRom());
  const id = stores.project.systems.addSystem("/roms/sel.nes")!;
  const themes = () =>
    submenuChildren(
      submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-bliptoaster"),
      "bliptoaster-themes",
    );
  const starred = () => themes().filter((t) => t.label.includes("*")).map((t) => t.id);

  expect(starred()).toEqual(["bliptoaster-theme-0"]); // the cart's baked default

  actionsOf(themes(), "bliptoaster-theme-2").find((a) => a.id === "bliptoaster-theme-2-select")!.onSelect();

  // The star moved, and slot 2 no longer offers Select while slot 0 now does - the list IS the picker.
  expect(starred()).toEqual(["bliptoaster-theme-2"]);
  expect(actionsOf(themes(), "bliptoaster-theme-2").some((a) => a.id === "bliptoaster-theme-2-select")).toBe(false);
  expect(actionsOf(themes(), "bliptoaster-theme-0")[0].id).toBe("bliptoaster-theme-0-select");

  // Pinned on the role (what a load replays, what a bake writes)...
  const pinned = stores.project.systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "bliptoaster-assets")!.config.settings;
  expect(pinned).toEqual({ theme: 2 });
  // ...and declared to the running cart, whole-block, to THIS system.
  const sent = be.stageSystemMidiCalls;
  expect(sent.length).toBe(1);
  expect(sent[0].id).toBe(id);
  expect(sent[0].bytes).toEqual([0xf0, 0x7d, 0x42, 0x03, 0, 1, 0, 2, 0, 0xf7]);
});

test("Select keeps the menu open, so a run of themes can be auditioned without reopening it", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/sel2.nes", blipToasterRom());
  const id = stores.project.systems.addSystem("/roms/sel2.nes")!;
  const themes = submenuChildren(
    submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: stores.project.systems.view().find((s) => s.id === id)! }).items, "inst-bliptoaster"),
    "bliptoaster-themes",
  );
  const select = actionsOf(themes, "bliptoaster-theme-2").find((a) => a.id === "bliptoaster-theme-2-select")!;
  expect(select.keepOpen).toBe(true);
  // Its neighbours do NOT: Export opens a dialog the menu should get out of the way of.
  expect(actionsOf(themes, "bliptoaster-theme-2").find((a) => a.id === "bliptoaster-theme-2-export")!.keepOpen).toBe(undefined);
});
