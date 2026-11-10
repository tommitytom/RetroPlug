// The tile slots (docs/modular-graph-phase-f4c-design.md section 5.2): every REGISTERED tile view, in product order,
// each in a StableSlot of its own, always. The slot is why a view that appears late (a flag flipping, a session opening
// on the next frame) does not land at the END of the tile's child list: lv_binding_js's insertChildBefore always
// appends (AGENTS.md "Framework gotchas"), so a fixed wrapper with one swappable child is the one way React can place
// it. The list is the registry's, which is a module constant, so the slot list never changes length; a `when` that
// flips under a live tile swaps the slot's child rather than appending a slot over the border and the badge, and a slot
// with nothing in it is an empty transparent Box (the same shape the dim layer proves passes a click to the tile).
import { useCallback, useSyncExternalStore } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { TileView, UiHost } from "@retroplug/ui/contribution";
import { StableSlot } from "../lvgl/StableSlot";
import { ComponentView } from "./ComponentView";
import { PixelView } from "./PixelView";
import { ReadoutView } from "./ReadoutView";
import { tileViews } from "../host/menuContributions";
import { useSystem } from "../stores/useStores";
import { subscribeViewShown, viewAppliesTo, viewShown } from "./viewSession";

export function TileViews({
  host,
  systemId,
  width,
  height,
}: {
  host: UiHost;
  systemId: number;
  width: number;
  height: number;
}) {
  const system = useSystem(systemId);
  // A system that went away between the tile's render and its own is the grid's to unmount: no slot, no work.
  if (system === undefined) return null;
  return (
    <>
      {tileViews().map(({ view }) => (
        <StableSlot key={view.id} testId={`view-slot-${view.id}`} width={width} height={height}>
          <TileViewBody view={view} host={host} system={system} width={width} height={height} />
        </StableSlot>
      ))}
    </>
  );
}

/** One tile view's body: the view's own visibility (subscribed here, for every body kind, so a hidden component view
 *  mounts nothing either) and its `when`, then the renderer its kind names. */
function TileViewBody({
  view,
  host,
  system,
  width,
  height,
}: {
  view: TileView;
  host: UiHost;
  system: SystemInfo;
  width: number;
  height: number;
}) {
  const shown = useSyncExternalStore(
    useCallback((onChange: () => void) => subscribeViewShown(view, onChange), [view]),
    useCallback(() => viewShown(view), [view]),
  );
  if (!shown || !viewAppliesTo(view, system)) return null;
  if (view.body.kind === "component") {
    return <ComponentView view={view} host={host} systemId={system.id} width={width} height={height} />;
  }
  if (view.body.kind === "pixels") {
    return <PixelView view={view} host={host} system={system} width={width} height={height} placement="tile" />;
  }
  return <ReadoutView view={view} host={host} system={system} width={width} />;
}
