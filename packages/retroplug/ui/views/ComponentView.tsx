// The component renderer (docs/modular-graph-phase-f4c-design.md section 5.3): mounts a package's own React component
// with the props the contract hands a view, and nothing around it. The body's component is `unknown` in
// @retroplug/ui (that contract imports no React), and this is the one cast the shell makes: reactView is what typed it.
import type { ComponentType } from "react";

import type { UiHost, ViewComponentProps } from "@retroplug/ui/contribution";

export function ComponentView({
  component,
  host,
  systemId,
  width,
  height,
}: {
  component: unknown;
  host: UiHost;
  systemId: number;
  width: number;
  height: number;
}) {
  const Component = component as ComponentType<ViewComponentProps>;
  return <Component host={host} systemId={systemId} width={width} height={height} />;
}
