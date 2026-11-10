// The bindings menus' contributed action rows (docs/modular-graph-phase-f4c-design.md section 5.7), built with no
// product action at all: the menu context takes the contributed list, so this file passes its own action and drives the
// rows the two menus build. What the shipped product shows for its two real actions is the same code path with the
// generated registry's list, which the recorded menu matrix holds once a product carries one.
import { test, expect } from "@retroplug/testing";

import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";
import { assertUniqueMenuIds } from "../../ui/host/menuContributions";

/** The one test action: a package-prefixed id (a dot, as the registry holds a contributed id), a backtick default and
 *  no pad default, which is the shapes the two real overlays' actions have. */
const TEST_ACTION: UiAction = {
  id: "test.debug-overlay",
  label: "Test Debug Overlay",
  keys: ["`"],
  run: () => {},
};

// The MenuContext App assembles from its hooks, minus React, with the test action injected in place of the registry's
// list.
function ctxOf(stores: AppStores, actions: readonly { pkg: string; action: UiAction }[]): MenuContext {
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
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
    actions,
  };
}

const kids = (items: MenuItem[], id: string): MenuItem[] => {
  const found = items.find((item) => item.id === id);
  return found && found.kind === "submenu" ? found.children ?? [] : [];
};

const bindingsMenu = (stores: AppStores, id: string): MenuItem[] => {
  const ctx = ctxOf(stores, [{ pkg: "test", action: TEST_ACTION }]);
  return kids(kids(buildStartMenu(ctx).items, "start-settings"), id);
};

const run = (stores: AppStores): MenuItem[] => bindingsMenu(stores, "set-keybindings");

const pads = (stores: AppStores): MenuItem[] => bindingsMenu(stores, "set-gamepad-bindings");

test("Keyboard Bindings: the test action's row follows the shell's three, with its default and its capture", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const rows = run(stores);
  // The shell's three are still there, and the contributed row is appended after them in registration order.
  const captureIds = rows.filter((row) => row.kind === "capture").map((row) => row.id);
  expect(captureIds.slice(8)).toEqual([
    "bind-act-OpenMenu",
    "bind-act-CycleNext",
    "bind-act-CyclePrev",
    "bind-act-test.debug-overlay",
  ]);
  const row = rows.find((item) => item.id === "bind-act-test.debug-overlay")!;
  expect(row.kind).toBe("capture");
  expect(row.label).toBe("Test Debug Overlay: `"); // the action's default, filled in (the profile has no entry)
  expect(row.capture!.source).toBe("keyboard");

  // Capturing writes the action's own id into the profile, and the rebuilt row reads what was captured.
  const active = stores.userConfig.config().activeKeyboardBindings;
  row.capture!.onCapture("Q");
  expect(stores.bindings.loadProfile(active)!.keyboardActions["test.debug-overlay"]).toEqual(["Q"]);
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: Q");

  // Clearing is the user's `[]`, which the fill leaves alone: the row reads "-" rather than the default again.
  run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.capture!.onClear();
  expect(stores.bindings.loadProfile(active)!.keyboardActions["test.debug-overlay"]).toEqual([]);
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: -");

  // Reset writes the shell's defaults for the channel; the contributed id it leaves out reads its default through the
  // fill again, which is the same as resetting it.
  run(stores).find((item) => item.id === "bind-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().keyboardActions).toEqual({
    OpenMenu: ["Escape"],
    CycleNext: ["Tab"],
    CyclePrev: [],
  });
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: `");
});

test("Gamepad Bindings: the row reads '-' for the action's missing pad default, and captures a pad button", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const row = pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!;
  expect(row.kind).toBe("capture");
  expect(row.label).toBe("Test Debug Overlay: -"); // no pad default, as Cycle Instances (Back) has none
  expect(row.capture!.source).toBe("gamepad");

  const active = stores.userConfig.config().activeGamepadBindings;
  row.capture!.onCapture("misc1");
  expect(stores.bindings.loadProfile(active)!.gamepadActions["test.debug-overlay"]).toEqual(["misc1"]);
  expect(pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.label).toBe(
    "Test Debug Overlay: misc1",
  );

  // A pad capture clears to the same "-", and the filled default (an empty list) is what the map then holds.
  pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.capture!.onClear();
  expect(pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.label).toBe(
    "Test Debug Overlay: -",
  );
  expect(stores.bindings.resolvedBindings().gamepadActions["test.debug-overlay"]).toEqual([]);
});

test("a row id with a dot in it is an id like any other: unique, and kept whole", () => {
  // A contributed id carries a package prefix, so every contributed row's id has a dot. The menu trees are keyed by id
  // and Menu only ever compares, stores and joins them (read: ui/screens/menu/Menu.tsx has no split on an id), and the
  // composer's uniqueness check is over the whole string.
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const tree = buildStartMenu(ctxOf(stores, [{ pkg: "test", action: TEST_ACTION }])).items;
  assertUniqueMenuIds(tree, "start"); // throws on a duplicate; a dotted id is not one
  const ids = run(stores).map((item) => item.id);
  expect(ids.filter((id) => id === "bind-act-test.debug-overlay").length).toBe(1);
  // No row renames or truncates the dotted id on the way out either: the menu keeps the string it was given.
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: `");
});
