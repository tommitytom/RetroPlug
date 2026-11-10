// reactView: the typed way to build a view body out of a React component.
//
// @retroplug/ui types a `component` body's component as `unknown`, because the view contract imports no React; a pack
// author who hands the shell a function with the wrong props would otherwise learn it in the shell. So a package that
// wants React for a view calls this and gets the contract's own body back typed.
import type { ComponentType } from "react";

import type { ComponentBody, ViewComponentProps } from "@retroplug/ui/contribution";

/** The contract's props type, published from here too so a pack author takes it from the kit beside `reactView`. */
export type { ViewComponentProps };

/** A component a package draws a view with. */
export function reactView(Component: ComponentType<ViewComponentProps>): ComponentBody {
  return { kind: "component", component: Component };
}
