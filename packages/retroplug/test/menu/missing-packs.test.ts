// What the UI shows for a missing pack (docs/downloadable-packs-m3-design.md section 8), as pure data: a placeholder's
// card text and its menu (Remove only), a live system's missing role as a `[!]` row, and the load's notice rows. Every
// string comes from a need, so the shell names no pack, platform or kind.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, validAudioRoutings, type MenuContext } from "../../ui/screens/menu/menuDefs";
import { placeholderLines } from "../../ui/screens/grid/placeholderText";
import { missingPackText } from "../../src/packs/needs";
import type { MenuItem } from "@retroplug/ui/menu";
import { gbRomBattery } from "../systems/fixtures";

function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.slotView(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    beginSongImport: () => {},
  };
}

function loaded() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  const stores = composeAppStores({ backend: be });
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [
      { platform: "gb", core: "sameboy", romPath: "/roms/a.gb",
        roles: [{ kind: "sameboy", config: {} }, { kind: "mpui-role", config: {} }],
        provenance: { roles: { "mpui-role": { id: "mpui-roles", name: "MPUI Roles" } } } },
      { platform: "mpui-platform", core: "mpui-core", romPath: "/roms/tune.mpui",
        provenance: { platform: { id: "mpui-console", name: "MPUI Console", version: "3.1.0" } } },
    ],
  };
  be.seed("/proj/mpui.rplg", JSON.stringify(doc));
  const outcome = stores.project.load("/proj/mpui.rplg");
  return { be, stores, outcome };
}

const labels = (items: MenuItem[]) => items.filter((i) => i.kind !== "separator").map((i) => i.label);

test("a placeholder's card names its platform, its ROM and the pack it needs", () => {
  const { stores } = loaded();
  const held = stores.project.systems.slotView()[1];
  expect(placeholderLines(held)).toEqual(["mpui-platform", "tune.mpui", "Needs the MPUI Console pack"]);
});

test("a placeholder's menu has Remove only, under what it needs", () => {
  const { stores } = loaded();
  const held = stores.project.systems.slotView()[1];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: held });
  expect(labels(tree.items)).toEqual(["Needs the MPUI Console pack", "Remove Instance"]);
  tree.items.find((i) => i.id === "inst-remove")!.onSelect!();
  expect(stores.project.systems.slotView().length).toBe(1);
  expect(stores.project.isDirty()).toBe(true);
});

test("a package's peer-only row does not count a placeholder as a peer", () => {
  // sameboy's Link Group needs two systems: here there is one live Game Boy and one placeholder.
  const { stores } = loaded();
  const live = stores.project.systems.slotView()[0];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: live });
  expect(tree.items.some((i) => i.id === "inst-link")).toBe(false);
  expect(tree.items.some((i) => i.id === "inst-remove")).toBe(true); // the shell's own Remove counts the slot
});

test("a live system's missing role is a warning row at the top of its menu", () => {
  const { stores } = loaded();
  const live = stores.project.systems.slotView()[0];
  const tree = buildInstanceMenu({ ...ctxOf(stores), system: live });
  expect(tree.items[0].label).toBe("MPUI Roles pack missing [!]");
  expect(tree.items[0].warn).toBe(true);
  expect(tree.items[0].disabled).toBe(true);
});

test("the load's outcome lists each pack it needs, and the notice row says what each covers", () => {
  const { outcome } = loaded();
  expect(outcome.kind === "loaded" ? outcome.missingPacks?.map(missingPackText) : undefined).toEqual([
    "MPUI Roles: 1 role",
    "MPUI Console: 1 system",
  ]);
});

test("one live system and a placeholder offer no split: the audio routing counts slots", () => {
  const { stores } = loaded();
  const slots = stores.project.systems.slotView();
  expect(slots.length).toBe(2);
  expect(validAudioRoutings(slots)).toEqual(["stereo", "twoPerInstance", "onePerInstance"]);
});

test("a ROM load with a placeholder focused appends a system and leaves both existing slots alone", () => {
  const { be, stores } = loaded();
  be.seed("/roms/b.gb", gbRomBattery());
  const systems = stores.project.systems;
  const live = systems.slotView()[0];
  const held = systems.slotView()[1];
  systems.setFocus(held.id);
  const result = systems.loadRom("/roms/b.gb");
  expect(result && "system" in result).toBe(true);
  const after = systems.slotView();
  expect(after.length).toBe(3);
  expect(after[0].id).toBe(live.id);
  expect(after[0].romPath).toBe("/roms/a.gb");
  expect(after[1].id).toBe(held.id);
  expect(after[1].placeholder !== undefined).toBe(true);
  expect(after[2].romPath).toBe("/roms/b.gb");
  expect(systems.focused()).toBe(after[2].id);
});

test("a placeholder's card carries a rom need's text", () => {
  const { be } = loaded();
  be.seed("/proj/bad.rplg", JSON.stringify({
    schemaVersion: "4",
    settings: {},
    systems: [{ platform: "gb", core: "sameboy", romPath: "/roms/bad.gb" }],
  }));
  be.seed("/roms/bad.gb", new Uint8Array(64));
  const stores = composeAppStores({ backend: be });
  stores.project.load("/proj/bad.rplg");
  const held = stores.project.systems.slotView().find((v) => v.placeholder);
  expect(held ? placeholderLines(held).some((l) => l.includes("The ROM bad.gb")) : false).toBe(true);
});
