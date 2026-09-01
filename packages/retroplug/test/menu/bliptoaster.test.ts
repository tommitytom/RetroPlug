// The BlipToaster instance submenu: gated on the `bliptoaster` marker role the ROM provider attaches to an BlipToaster
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

test("the BlipToaster submenu appears only for an BlipToaster ROM, is asset-only (no Songs), and lists Kits + Fonts", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });

  // A plain NES ROM (no BLIPTOASTER marker) → no `bliptoaster` role → no submenu.
  be.seed("/roms/plain.nes", nesRom());
  const plainId = stores.project.systems.addSystem("/roms/plain.nes")!;
  const plain = stores.project.systems.view().find((s) => s.id === plainId)!;
  expect(findItem(buildInstanceMenu({ ...ctxOf(stores), system: plain }).items, "inst-bliptoaster")).toBe(undefined);

  // An BlipToaster ROM → the provider attaches `bliptoaster` → the submenu shows, asset-only (no Songs submenu).
  const items = blipToasterItems(be, stores);
  expect(findItem(items(), "inst-bliptoaster")?.kind).toBe("submenu");
  const kids = submenuChildren(items(), "inst-bliptoaster");
  expect(findItem(kids, "bliptoaster-songs")).toBe(undefined); // no song battery → no Songs submenu
  expect(findItem(kids, "bliptoaster-themes")?.kind).toBe("submenu");
  expect(findItem(kids, "bliptoaster-kits")?.kind).toBe("submenu");
  expect(findItem(kids, "bliptoaster-fonts")?.kind).toBe("submenu");
});

test("the Themes submenu lists the baked theme with Export/Replace (no Remove until overridden)", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const items = blipToasterItems(be, stores);
  const themes = submenuChildren(submenuChildren(items(), "inst-bliptoaster"), "bliptoaster-themes");
  expect(themes.length).toBe(1);
  expect(themes[0].label).toBe("[0] DFLT"); // decoded, space-trimmed theme name
  const t0 = submenuChildren(themes, "bliptoaster-theme-0");
  expect(findItem(t0, "bliptoaster-theme-0-export")?.kind).toBe("action");
  expect(findItem(t0, "bliptoaster-theme-0-replace")?.kind).toBe("action");
  expect(findItem(t0, "bliptoaster-theme-0-remove")).toBe(undefined);
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
