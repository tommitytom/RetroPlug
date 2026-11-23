// The product's UI contributions (docs/modular-graph-phase-f4b-design.md section 4.2), for the shell's menu composers:
// the live registry (ui/contributions/uiRegistries.ts, seeded by the generated module in product order and appended
// to by packs) is the one place a package's rows, builders, tracker specs, services and app actions are read from.
// Only ui/ imports it, so the control plane, the render worker, the SDK and the planner carry no menu contribution;
// the registry's check has already refused a duplicate row id, an unknown placement, a row over a
// role kind no registered type declares or over a key its schema does not read, and a tracker UI no integration
// matches, so what this module reads it can index. The views are viewContributions.ts's, over their own generated
// module: a component view imports React, which this module must not reach.
import type {
  ContributedAction, MenuBuildContext, MenuBuilder, RoleRow, TrackerUi, UiHost, UiService,
} from "@retroplug/ui/contribution";
import type { SystemInfo } from "@retroplug/core/host";
import type { MenuItem } from "@retroplug/ui/menu";
import { uiRegistry } from "../contributions/menus";
import { derivedList } from "../../src/contributions/registry";

/** Every contributed role row, in product order: the System submenu's package-owned block. */
export function roleRows(): readonly RoleRow[] {
  return uiRegistry.items().flatMap((ui) => ui.rows ?? []);
}

/** The tracker UI a cart contributes, by its integration id, or undefined for a cart with no contributed specs. */
export function contributedTrackerUi(id: string): TrackerUi | undefined {
  for (const ui of uiRegistry.items()) {
    for (const tracker of ui.trackers ?? []) {
      if (tracker.id === id) return tracker;
    }
  }
  return undefined;
}

/** Every menu fragment at one placement, built in product order: the shell's one composer for a contributed placement
 *  (`instance.devices`, `instance.footer`, `start`, `settings.midi`). A builder that returns `[]` hides its entry. */
export function menuPlacement(
  placement: MenuBuilder["placement"],
  host: UiHost,
  sys: SystemInfo | null,
  ctx: MenuBuildContext,
): MenuItem[] {
  return uiRegistry.items().flatMap((ui) =>
    (ui.menus ?? []).filter((menu) => menu.placement === placement).flatMap((menu) => menu.build(host, sys, ctx)),
  );
}

/** The open hook for the shell's container of one placement: `onOpen` on the container row runs every builder's, in
 *  product order, when the user expands it. Undefined when no builder at this placement declares one, so the container
 *  row is the same row it always was. */
export function menuPlacementOpen(
  placement: MenuBuilder["placement"],
  host: UiHost,
): (() => void) | undefined {
  const builders = uiRegistry.items().flatMap((ui) =>
    (ui.menus ?? []).filter((menu) => menu.placement === placement && menu.onOpen),
  );
  if (builders.length === 0) return undefined;
  return () => {
    for (const menu of builders) menu.onOpen?.(host);
  };
}

const actionWrappers = new WeakMap<object, ContributedAction>();

/** Every app action a package contributes, in registration order, with the package it came from: the bindings menus'
 *  rows, the key and gamepad maps' "known" ids, and the dispatch's lookups. The same array until the next commit, so a
 *  hook's dependency (and a memo over it) changes only when a pack adds actions. */
const ACTIONS = derivedList((): readonly ContributedAction[] =>
  uiRegistry.entries().flatMap(({ pkg, item }) => (item.actions ?? []).map((action) => {
    // One wrapper per action object, so an unchanged action keeps its identity across commits.
    let wrapped = actionWrappers.get(action);
    if (!wrapped) {
      wrapped = { pkg, action };
      actionWrappers.set(action, wrapped);
    }
    return wrapped;
  })),
);

const ACTION_IDS = derivedList((): readonly string[] => ACTIONS().map((entry) => entry.action.id));

export function contributedActions(): readonly ContributedAction[] {
  return ACTIONS();
}

/** Their ids alone, for the maps' "known" set and the bindings rows. */
export function contributedActionIds(): readonly string[] {
  return ACTION_IDS();
}

/** Every service a package contributes, in registration order: the list `useUiServices` runs, subscribes and polls. The
 *  same array until the next commit, so the hook's dependency changes only when a pack adds services. */
const SERVICES = derivedList((): readonly UiService[] => uiRegistry.items().flatMap((ui) => ui.services ?? []));

export function uiServices(): readonly UiService[] {
  return SERVICES();
}

/** Drop the later of two rows of one id in one menu, reporting it. This is what the composers run over the tree they
 *  built: a duplicate id would silently replace the row a user can see (the menu is keyed by id), and a hand-edited
 *  asset override can produce one, but a menu that does not render is worse than one row missing, so the FIRST row is
 *  kept and the rest are reported. `assertUniqueMenuIds` is the same rule as an assertion, for the generator's checks
 *  and the tests. */
export function firstWinsMenuIds(
  items: readonly MenuItem[],
  where: string,
  seen: Set<string> = new Set(),
): MenuItem[] {
  const kept: MenuItem[] = [];
  for (const item of items) {
    if (seen.has(item.id)) {
      console.warn(`[menu] two rows in ${where} share the id ${item.id}: keeping the first and dropping this one`);
      continue;
    }
    seen.add(item.id);
    kept.push(item.children ? { ...item, children: firstWinsMenuIds(item.children, where, seen) } : item);
  }
  return kept;
}

/** Refuse two rows of one id in one menu. The menu is keyed by id, so a contributed builder emitting one of the
 *  shell's own ids (a footer builder emitting `inst-exit`) would silently take that row's place; the composer runs this
 *  over the tree it just built, which is the only place both halves are known. */
export function assertUniqueMenuIds(
  items: readonly MenuItem[],
  where: string,
  seen: Set<string> = new Set(),
): void {
  for (const item of items) {
    if (seen.has(item.id)) throw new Error(`two menu rows share the id ${item.id} in ${where}`);
    seen.add(item.id);
    if (item.children) assertUniqueMenuIds(item.children, where, seen);
  }
}
