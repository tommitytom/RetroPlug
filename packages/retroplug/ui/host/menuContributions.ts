// The product's UI contributions (docs/modular-graph-phase-f4b-design.md section 4.2), for the shell's menu composers:
// the generated list, in product order, is the one place a package's rows, builders, tracker specs, services and app
// actions are read from. Only ui/ imports it, so the control plane, the render worker, the SDK and the planner carry no
// menu contribution; the generated module has already refused a duplicate row id, an unknown placement, a row over a
// role kind no registered type declares or over a key its schema does not read, and a tracker UI no integration
// matches, so what this module reads it can index. The views are viewContributions.ts's, over their own generated
// module: a component view imports React, which this module must not reach.
import type {
  ContributedAction, MenuBuildContext, MenuBuilder, RoleRow, TrackerUi, UiHost, UiService,
} from "@retroplug/ui/contribution";
import type { SystemInfo } from "@retroplug/core/host";
import type { MenuItem } from "@retroplug/ui/menu";
import { ACTIONS, UI_CONTRIBUTIONS } from "../generated/menus";

/** Every contributed role row, in product order: the System submenu's package-owned block. */
export function roleRows(): readonly RoleRow[] {
  return UI_CONTRIBUTIONS.flatMap((entry) => entry.ui.rows ?? []);
}

/** The tracker UI a cart contributes, by its integration id, or undefined for a cart with no contributed specs. */
export function contributedTrackerUi(id: string): TrackerUi | undefined {
  for (const entry of UI_CONTRIBUTIONS) {
    for (const tracker of entry.ui.trackers ?? []) {
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
  return UI_CONTRIBUTIONS.flatMap((entry) =>
    (entry.ui.menus ?? []).filter((menu) => menu.placement === placement).flatMap((menu) => menu.build(host, sys, ctx)),
  );
}

/** The open hook for the shell's container of one placement: `onOpen` on the container row runs every builder's, in
 *  product order, when the user expands it. Undefined when no builder at this placement declares one, so the container
 *  row is the same row it always was. */
export function menuPlacementOpen(
  placement: MenuBuilder["placement"],
  host: UiHost,
): (() => void) | undefined {
  const builders = UI_CONTRIBUTIONS.flatMap((entry) =>
    (entry.ui.menus ?? []).filter((menu) => menu.placement === placement && menu.onOpen),
  );
  if (builders.length === 0) return undefined;
  return () => {
    for (const menu of builders) menu.onOpen?.(host);
  };
}

/** Every app action a package contributes, in product order, with the package it came from: the bindings menus' rows,
 *  the key and gamepad maps' "known" ids, and the dispatch's lookups. The generated list itself, so the export the
 *  generator's checks run over is the one the product reads. A module constant, so a hook's dependency (and a memo
 *  over it) is stable. */
const ACTION_IDS: readonly string[] = ACTIONS.map((entry) => entry.action.id);

export function contributedActions(): readonly ContributedAction[] {
  return ACTIONS;
}

/** Their ids alone, for the maps' "known" set and the bindings rows. */
export function contributedActionIds(): readonly string[] {
  return ACTION_IDS;
}

/** Every service a package contributes, in product order: the list `useUiServices` runs, subscribes and polls. It is a
 *  module constant derived from the generated list, so the hook's dependency is stable. */
const SERVICES: readonly UiService[] = UI_CONTRIBUTIONS.flatMap((entry) => entry.ui.services ?? []);

export function uiServices(): readonly UiService[] {
  return SERVICES;
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
