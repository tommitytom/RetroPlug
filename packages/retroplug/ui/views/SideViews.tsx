// The side views: a package's panel in a grid cell of its own, right after the tile of each system its `openFor` names
// (the NES SD card's file list beside the game). The grid's cells are the systems' and these together, so the layout,
// the window fit and the hit tests read the one list `useGridCells` builds.
import { useMemo, useSyncExternalStore } from "react";

import type { SideView, UiHost } from "@retroplug/ui/contribution";
import { Box } from "@retroplug/ui-react";
import type { SystemView } from "../../src/systemsStore";
import { useStores } from "../stores/useStores";
import { sideViews } from "../host/viewContributions";
import { gridCells, type GridCell } from "../screens/grid/layout";
import { ComponentView } from "./ComponentView";
import { PixelView } from "./PixelView";
import { ReadoutView } from "./ReadoutView";
import { sideOpenFor, subscribeSideOpenFor, viewAppliesTo } from "./viewSession";

export type Cell = GridCell<SystemView, SideView>;

function subscribeAll(onChange: () => void): () => void {
  const offs = sideViews().map(({ view }) => subscribeSideOpenFor(view, onChange));
  return () => {
    for (const off of offs) off();
  };
}

/** What every side view is open beside, as one string: a snapshot useSyncExternalStore compares by value, so a write
 *  that changes nothing costs no render. */
function openSignature(): string {
  return sideViews()
    .map(({ view }) => `${view.id}:${sideOpenFor(view).join(",")}`)
    .join(";");
}

/** The grid's cells for `systems`: each tile, then the side views open beside it, in product order. */
export function useGridCells(systems: readonly SystemView[]): Cell[] {
  const stores = useStores();
  const signature = useSyncExternalStore(subscribeAll, openSignature);
  return useMemo(() => {
    // Each view's ids followed to the systems they name now, so a rebuild (a new SD card) keeps its panel open.
    const open = sideViews().map(({ view }) => ({
      view,
      ids: sideOpenFor(view).map((id) => stores.project.systems.currentId(id)),
    }));
    return gridCells(systems, (sys) =>
      open.filter(({ view, ids }) => ids.includes(sys.id) && viewAppliesTo(view, sys)).map(({ view }) => view),
    );
    // The signature is what moves when a panel opens or closes, and `systems` when one is rebuilt; the views themselves
    // are module constants.
  }, [stores, systems, signature]);
}

/** One side view's cell: the panel on black, a click on it focusing the system it belongs to, as its tile's does. */
export function SideCell({
  view,
  host,
  system,
  width,
  height,
  onFocus,
}: {
  view: SideView;
  host: UiHost;
  system: SystemView;
  width: number;
  height: number;
  onFocus?: () => void;
}) {
  const kind = view.body.kind;
  return (
    <Box onClick={onFocus} style={{ width, height, "background-color": "#000000" }}>
      {kind === "component" ? (
        <ComponentView view={view} host={host} systemId={system.id} width={width} height={height} />
      ) : kind === "pixels" ? (
        <PixelView view={view} host={host} system={system} width={width} height={height} placement="tile" shown />
      ) : (
        <ReadoutView view={view} host={host} system={system} width={width} shown />
      )}
    </Box>
  );
}
