// A window view's frame (docs/modular-graph-phase-f4c-design.md sections 5.1 and 5.3): it takes the whole window in
// place of the grid, tags itself with the view's id (which is also the test id the HD player has today) and hands the
// body to the renderer its kind names.
import { Box, tagTestId } from "@retroplug/ui-react";
import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, WindowView } from "@retroplug/ui/contribution";
import { ComponentView } from "./ComponentView";
import { PixelView } from "./PixelView";
import { ReadoutView } from "./ReadoutView";

export function ViewWindow({
  view,
  host,
  system,
  systemId,
  width,
  height,
}: {
  view: WindowView;
  host: UiHost;
  system: SystemInfo | null;
  systemId: number;
  width: number;
  height: number;
}) {
  if (view.body.kind === "component") {
    return (
      <ComponentView
        component={view.body.component}
        host={host}
        systemId={systemId}
        width={width}
        height={height}
      />
    );
  }
  return (
    <Box innerRef={tagTestId(view.id)} style={{ width, height, "background-color": "#000000" }}>
      {view.body.kind === "pixels" ? (
        <PixelView view={view} host={host} system={system} width={width} height={height} placement="window" />
      ) : (
        <ReadoutView view={view} host={host} system={system} width={width} />
      )}
    </Box>
  );
}
