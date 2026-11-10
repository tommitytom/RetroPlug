// @retroplug/ui-fixture's UI module: the component view a package's React half is proved by.
//
// The package is listed by no product, so nothing here reaches a bundle: it is the worked example spec/06's "a view"
// step copies, and the shape rule 3's refusals are copies of (a .tsx under ts/ui/, the React host modules its files
// import declared in the manifest, and a tsconfig that extends the generated host-module paths).
import { defineUi } from "@retroplug/ui/contribution";
import { reactView } from "@retroplug/ui-react";

import { Badge } from "./Badge";

export { Badge };

/** The tile view this package contributes, built the way the kit asks: a component body. It is the module's own export,
 *  so the contribution below stays the four lists defineUi takes today; a product that lists the package wires the view
 *  from here, and an unlisted package's module is read by nothing. */
export const badgeView = { id: "ui-fixture-badge", label: "Fixture Badge", placement: "tile" as const, body: reactView(Badge) };

/** The package's contribution: the rows, menus, trackers and services it adds, which is none of them. */
export default defineUi({});
