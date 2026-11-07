// BlipToaster's contribution to the app: the two roles its ROM provider attaches, the provider itself and its
// asset-only tracker integration. BlipToaster is an NES MIDI synth rather than a tracker, so it has no song battery:
// the roles are a marker (which build of the cart, for the parameter map) and the cart's ROM overrides, and the
// integration carries assets alone.
import { definePackage } from "@retroplug/core/definePackage";
import { BLIPTOASTER_ROLE } from "./bliptoasterRole";
import { BLIPTOASTER_ASSETS_ROLE } from "./bliptoasterAssetsRole";
import { BLIPTOASTER_ROM_PROVIDER } from "./romProvider";
import { blipToasterIntegration } from "./bliptoasterIntegration";
// The keys the owner packages add to the contract (their modules augment PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "@retroplug/tracker/packageContributions";

export default definePackage({
  // The marker role first, then the asset role: the provider attaches them in that order, and the app's role list
  // registers every kind where buildAppRegistry always did.
  roles: [BLIPTOASTER_ROLE, BLIPTOASTER_ASSETS_ROLE],
  // The provider attaches both roles by the ROM's baked marker, in the order the registry offers it.
  romProviders: [BLIPTOASTER_ROM_PROVIDER],
  // The Songs and Assets menus' consoles, in the order they are searched (a system's first match wins).
  trackerIntegrations: [blipToasterIntegration],
});
