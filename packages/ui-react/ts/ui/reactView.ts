// reactView: the typed way to build a view body out of a React component.
//
// @retroplug/ui types a `component` body's component as `unknown`, because the view contract imports no React; a pack
// author who hands the shell a function with the wrong props would otherwise learn it in the shell. So a package that
// wants React for a view calls this and gets the body back typed.
//
// The props and the body shape are declared here until @retroplug/ui publishes them (`ViewComponentProps` and
// `ViewBody`, which a body of the "component" kind cannot use while the contract imports no React): they are that
// contract's, so the replacement is this file's two declarations gone and the signature named by the imported types,
// with no call site touched.
import type { ComponentType } from "react";

import type { UiHost } from "@retroplug/ui/contribution";

/** What the shell hands a component view: its host, the system it is drawn for, and the space it has. */
export interface ViewComponentProps {
  readonly host: UiHost;
  readonly systemId: number;
  readonly width: number;
  readonly height: number;
}

/** A body of the view contract's "component" kind, local until the contract publishes `ViewBody`: the component,
 *  wrapped so the shell can tell it from a pixels or a readout body. */
interface ReactViewBody {
  readonly kind: "component";
  readonly component: ComponentType<ViewComponentProps>;
}

/** A component a package draws a view with. */
export function reactView(Component: ComponentType<ViewComponentProps>): ReactViewBody {
  return { kind: "component", component: Component };
}
