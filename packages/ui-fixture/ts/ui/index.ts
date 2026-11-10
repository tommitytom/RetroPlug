// @retroplug/ui-fixture's UI module: the component view a package's React half is proved by.
//
// The package is listed by no product, so nothing here reaches a bundle: it is the worked example spec/06's "a view"
// step copies, and the shape rule 3's refusals are copies of (a .tsx under ts/ui/, the React host modules its files
// import declared in the manifest, and a tsconfig that extends the generated host-module paths).
import { defineUi, type TileView } from "@retroplug/ui/contribution";
import { reactView } from "@retroplug/ui-react";

import { Badge } from "./Badge";

export { Badge };

/** The tile view this package contributes, built the way the kit asks: a component body. `satisfies` is what the
 *  contract checks here, since the view is declared as data rather than through a helper of its own. */
export const badgeView = {
  id: "ui-fixture-badge",
  label: "Fixture Badge",
  placement: "tile",
  body: reactView(Badge),
} satisfies TileView;

/** The package's contribution: the view it adds and none of the rows, menus, trackers or services. */
export default defineUi({ views: [badgeView] });
