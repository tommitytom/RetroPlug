// The UI's live registries (docs/downloadable-packs-m2a-plan.md): each package's `retroplug.ui` contribution (its
// defineUi: system-settings rows, menu builders, tracker UIs, services, app actions) and each view of its
// `retroplug.views` module. ui/generated/menus.ts and ui/generated/views.ts seed them with the product's packages, a pack
// appends, and the checks below (the ones the generator used to emit into those two modules, word for word) run at every
// commit. This module imports neither seed and no React: ui/contributions/menus.ts and views.ts are the facades the
// shell reads through, kept apart so a menu module never reaches a component view.
import { PLACEMENTS, type RoleRow, type UiAction, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";
import { Registry } from "../../src/contributions/registry";
import { rolesRegistry, trackerIntegrationsRegistry } from "../../src/contributions/appRegistries";
import { GAMEPAD_BUTTON_NAMES, resolveKeyName } from "../../src/keyCodes";
import { DEFAULT_GAMEPAD_ACTIONS, DEFAULT_KEYBOARD_ACTIONS, defaultBindingMap } from "../../src/bindingMap";

/** The binding vocabulary an action's default is checked against: the key codes and pad names the shell and the Game Boy
 *  buttons already take at a default (a key on both would fire the action AND the button it is on). */
function takenBindings(): { keyCodes: Set<number>; padNames: Set<string> } {
  const keyCodes = new Set<number>();
  const padNames = new Set<string>();
  const defaultMap = defaultBindingMap();
  for (const names of Object.values(DEFAULT_KEYBOARD_ACTIONS)) for (const name of names) keyCodes.add(resolveKeyName(name)!);
  for (const names of Object.values(defaultMap.keyboard)) for (const name of names) keyCodes.add(resolveKeyName(name)!);
  for (const names of Object.values(DEFAULT_GAMEPAD_ACTIONS)) for (const name of names) padNames.add(name);
  for (const names of Object.values(defaultMap.gamepad)) for (const name of names) padNames.add(name);
  return { keyCodes, padNames };
}

/** Each package's UI contribution, one item per package. A row's id is unique inside its placement (the menu is keyed by
 *  id); a row's role must be registered and its key (and its gate's) one its role's schema reads; a tracker UI names a
 *  registered integration, once; a service id and an action id are unique; an action's id is prefixed by its package's
 *  name and its defaults are key and pad names the shell resolves and does not already bind (two contributions on one
 *  key are allowed: the overlays' backtick); a menu builder's placement
 *  is one the shell has. */
export const uiRegistry = new Registry<UiContribution>("ui", (entries, view) => {
  const roles = view(rolesRegistry).map((entry) => entry.item);
  const roleKinds = new Set(roles.map((role) => role.kind));
  const roleKeys = new Map(roles.map((role) => [role.kind, new Set(Object.keys(role.schema.parse({})))]));
  const trackerIds = new Set(view(trackerIntegrationsRegistry).map((entry) => entry.item.id));
  const placements = new Set<string>(PLACEMENTS);
  const padButtonNames = new Set<string>(GAMEPAD_BUTTON_NAMES);
  const { keyCodes: takenKeyCodes, padNames: takenPadNames } = takenBindings();
  const rowOwners = new Map<string, string>();
  const trackerOwners = new Map<string, string>();
  const serviceOwners = new Map<string, string>();
  const actionOwners = new Map<string, string>();

  const claimRow = (placement: string, id: string, pkg: string): void => {
    const owner = rowOwners.get(`${placement}:${id}`);
    if (owner === undefined) {
      rowOwners.set(`${placement}:${id}`, pkg);
      return;
    }
    throw new Error(owner === pkg
      ? `the package ${pkg} declares the row ${id} twice in the placement ${placement}`
      : `two packages declare the row ${id} in the placement ${placement}: ${owner} and ${pkg}`);
  };

  const checkRow = (placement: string, pkg: string, row: RoleRow): void => {
    if (!roleKinds.has(row.role)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} over the role ${row.role}, which no ` +
        "registered role type has");
    }
    claimRow(placement, row.id, pkg);
    const keys = roleKeys.get(row.role)!;
    if (!keys.has(row.key)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} over the key ${row.key}, which the role ` +
        `${row.role} does not read`);
    }
    if (row.when && !keys.has(row.when.key)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} gated on the key ${row.when.key}, which ` +
        `the role ${row.role} does not read`);
    }
  };

  const checkAction = (pkg: string, action: UiAction): void => {
    const owner = actionOwners.get(action.id);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the action ${action.id} twice`
        : `two packages declare the action ${action.id}: ${owner} and ${pkg}`);
    }
    actionOwners.set(action.id, pkg);
    if (!action.id.startsWith(`${pkg}.`)) {
      throw new Error(`the package ${pkg} declares the action ${action.id}, which is not prefixed ` +
        `by its own name ("${pkg}.")`);
    }
    for (const name of action.keys ?? []) {
      const code = resolveKeyName(name);
      if (code === null) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
          `${name}, which is no key name`);
      }
      if (takenKeyCodes.has(code)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
          `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
      }
      if (/^[A-Z]$/.test(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ${name} by ` +
          `default, which no host sends (the hosts send letters unshifted: bind "${name.toLowerCase()}")`);
      }
    }
    for (const name of action.pad ?? []) {
      if (!padButtonNames.has(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
          `${name}, which is no SDL button name (the names are GAMEPAD_BUTTON_NAMES)`);
      }
      if (takenPadNames.has(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
          `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
      }
    }
  };

  for (const { pkg, item: ui } of entries) {
    for (const row of ui.rows ?? []) checkRow("system.settings", pkg, row);
    for (const action of ui.actions ?? []) checkAction(pkg, action);
    for (const tracker of ui.trackers ?? []) {
      if (!trackerIds.has(tracker.id)) {
        throw new Error(`the package ${pkg} declares a tracker UI for ${tracker.id}, which no registered ` +
          "tracker integration has");
      }
      const owner = trackerOwners.get(tracker.id);
      if (owner !== undefined) {
        throw new Error(owner === pkg
          ? `the package ${pkg} declares the tracker UI ${tracker.id} twice`
          : `two packages declare a tracker UI for ${tracker.id}: ${owner} and ${pkg}`);
      }
      trackerOwners.set(tracker.id, pkg);
      for (const row of tracker.extras?.rows ?? []) checkRow(`instance.tracker:${tracker.id}`, pkg, row);
    }
    for (const service of ui.services ?? []) {
      const owner = serviceOwners.get(service.id);
      if (owner !== undefined) {
        throw new Error(owner === pkg
          ? `the package ${pkg} declares the UI service ${service.id} twice`
          : `two packages declare a UI service ${service.id}: ${owner} and ${pkg}`);
      }
      serviceOwners.set(service.id, pkg);
    }
    for (const menu of ui.menus ?? []) {
      if (!placements.has(menu.placement)) {
        throw new Error(`the package ${pkg} declares a menu builder at the placement ${menu.placement}, ` +
          `which is no placement (the placements are ${PLACEMENTS.join(", ")})`);
      }
    }
  }
});

/** Every view, one item per view. A view's id is its test id and the id host.views.open names, so it is unique across
 *  the product; its placement and its body's kind are the two data fields the type cannot hold a package to at load. */
export const viewsRegistry = new Registry<ViewDef>("views", (entries) => {
  const viewOwners = new Map<string, string>();
  const viewPlacements = new Set<string>(["window", "tile"]);
  const bodyKinds = new Set<string>(["pixels", "readout", "component"]);
  for (const { pkg, item: view } of entries) {
    const owner = viewOwners.get(view.id);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the view ${view.id} twice`
        : `two packages declare the view ${view.id}: ${owner} and ${pkg}`);
    }
    viewOwners.set(view.id, pkg);
    if (!viewPlacements.has(view.placement)) {
      throw new Error(`the package ${pkg} declares the view ${view.id} at the placement ${view.placement}, ` +
        `which is no placement (the placements are window, tile)`);
    }
    const kind = view.body?.kind;
    if (!bodyKinds.has(kind)) {
      throw new Error(`the package ${pkg} declares the view ${view.id} with the body kind ${kind}, ` +
        `which is no body kind (the kinds are pixels, readout, component)`);
    }
    const body = view.body as { readonly open?: unknown; readonly component?: unknown };
    if (kind === "component") {
      if (typeof body.component !== "function") {
        throw new Error(`the package ${pkg} declares the view ${view.id} with a component body and no ` +
          "component (reactView builds the one a component body carries)");
      }
    } else if (typeof body.open !== "function") {
      throw new Error(`the package ${pkg} declares the view ${view.id} with a ${kind} body and no open ` +
        `(a ${kind} body's open is what the shell calls)`);
    }
    if (view.placement !== "tile" && (view as { readonly visible?: unknown }).visible !== undefined) {
      throw new Error(`the package ${pkg} declares the window view ${view.id} with a visible flag, which only ` +
        `a tile view has`);
    }
  }
});
