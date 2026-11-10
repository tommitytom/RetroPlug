// The component renderer (docs/modular-graph-phase-f4c-design.md section 5.3): mounts a package's own React component
// with the props the contract hands a view, inside the one error boundary the shell puts around package code. The body's
// component is `unknown` in @retroplug/ui (that contract imports no React), and this is the one cast the shell makes:
// reactView is what typed it.
import { Component, type ComponentType, type ErrorInfo, type ReactNode } from "react";

import type { UiHost, ViewComponentProps, ViewDef } from "@retroplug/ui/contribution";
import { componentFault } from "./viewSession";

/** A component view's boundary: a throw in the component's render or effect would otherwise reach the app root, and
 *  React 18 unmounts the whole UI for an uncaught one, so it is contained the way the plain-TypeScript guards contain a
 *  throwing `when`, `open`, `frame` or `sample` - reported once per body and drawn as nothing (viewSession.ts's
 *  `componentFault`). The boundary is keyed on the system, so another system gets a fresh attempt. */
class ComponentBoundary extends Component<{ body: object; id: string; children: ReactNode }, { failed: boolean }> {
  override state: { failed: boolean } = { failed: false };

  static getDerivedStateFromError(): { failed: boolean } {
    return { failed: true };
  }

  override componentDidCatch(error: unknown, _info: ErrorInfo): void {
    componentFault(this.props.body, this.props.id, error);
  }

  override render(): ReactNode {
    return this.state.failed ? null : this.props.children;
  }
}

export function ComponentView({
  view,
  host,
  systemId,
  width,
  height,
}: {
  view: ViewDef;
  host: UiHost;
  systemId: number;
  width: number;
  height: number;
}) {
  const body = view.body;
  if (body.kind !== "component") return null;
  const Body = body.component as ComponentType<ViewComponentProps>;
  return (
    <ComponentBoundary key={systemId} body={body} id={view.id}>
      <Body host={host} systemId={systemId} width={width} height={height} />
    </ComponentBoundary>
  );
}
