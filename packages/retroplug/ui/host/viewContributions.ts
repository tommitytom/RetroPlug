// The product's views (docs/modular-graph-phase-f5a-design.md section 5.3), for the shell's view slots: the generated
// views module, in product order, is the one place a package's views are read from. Apart from menuContributions' menus
// module on purpose: a component view imports React, and only the UI bundle resolves it, so a menu module that reached
// this one would put React in every pure-tier test that builds a menu.
import type { SideView, TileView, ViewDef, WindowView } from "@retroplug/ui/contribution";
import { VIEWS } from "../generated/views";

/** The view a package registers under `id`, with the package it came from, or undefined. The generated module has
 *  already refused a duplicate id, so the first match is the only one. */
export function viewById(id: string): { pkg: string; view: ViewDef } | undefined {
  return VIEWS.find((entry) => entry.view.id === id);
}

/** The window view a package registers under `id`, or undefined for an unknown id or a tile view. What
 *  `host.views.open` looks an id up in, so a tile view's id opens nothing. */
export function windowViewOf(id: string): WindowView | undefined {
  const entry = viewById(id);
  return entry?.view.placement === "window" ? entry.view : undefined;
}

/** Resolve a window open against the generated registry: the view, or the report saying why it cannot open. App's
 *  `views.open` runs this and then the two checks only a project can answer (a system that is gone, a view whose `when`
 *  rejects it), so the id's own refusals are worded in one place. */
export function windowViewOrReport(id: string): { view: WindowView } | { report: string } {
  const entry = viewById(id);
  if (!entry) return { report: `[views] no view is registered under the id ${id}` };
  if (entry.view.placement !== "window") return { report: `[views] ${id} is a tile view, which opens no window` };
  return { view: entry.view };
}

/** Every tile view, in product order, with the package it came from: the layers EmulatorTile mounts over each tile
 *  whose system the view's `when` accepts. */
export function tileViews(): readonly { pkg: string; view: TileView }[] {
  return VIEWS.filter((entry): entry is { pkg: string; view: TileView } => entry.view.placement === "tile");
}

/** Every side view, in product order: the panels SystemGrid gives a cell of their own beside each system a view's
 *  `openFor` names. */
export function sideViews(): readonly { pkg: string; view: SideView }[] {
  return VIEWS.filter((entry): entry is { pkg: string; view: SideView } => entry.view.placement === "side");
}
