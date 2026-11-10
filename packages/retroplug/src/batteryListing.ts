// A battery image as the lines a listing prints (docs/modular-graph-phase-f4b-design.md section 5.2): a header and one
// row per saved song, or the "unrecognized battery image" line. Each cart describes its own records through its tracker
// integration's `batteryListing`, so what a cart's battery means stays the cart's and no cart is named here: the shell
// composes them in rank order (`BatteryListing.rank`), so two carts that read one image are tried deterministically.
import type { BatteryListing } from "@retroplug/tracker/trackerIntegration";
import { TRACKER_INTEGRATIONS } from "./trackerRegistry";

/** Every integration that lists a battery, in the order they are tried: a cart's rank first, and product order between
 *  equal ranks (a stable sort, so the list is deterministic). */
function listings(): BatteryListing[] {
  return TRACKER_INTEGRATIONS.map((integration) => integration.batteryListing)
    .filter((listing): listing is BatteryListing => listing !== undefined)
    .sort((a, b) => a.rank - b.rank);
}

/** The listing for `sram`: the first cart that reads it heads its own rows, and an image no cart reads gets the one
 *  refusal line naming them all. */
export function batteryListing(sram: Uint8Array): string[] {
  const carts = listings();
  for (const cart of carts) {
    const rows = cart.describe(sram);
    if (rows) return [`${cart.name} battery - ${rows.length} ${cart.noun}(s):`, ...rows];
  }
  const names = carts.map((cart) => cart.name).join(" or ");
  return [`unrecognized battery image (${sram.length} bytes) - not a ${names} save`];
}
