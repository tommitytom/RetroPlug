// risa's contribution to the app: its three roles (the menu-gating marker, the asset-override role and the sync role
// whose work is rp.risa.sync), the ROM provider that attaches them, its Songs/Assets tracker integration and its DMC
// kit bank format. A list's order within the package is the order it registers in where one list carries several;
// where it sits among the product's other lists is the product list's order, which is behaviour for the ROM
// providers alone (roles, kit formats and integrations are looked up by kind, id or an exclusive marker).
import { definePackage } from "@retroplug/core/definePackage";
import { RISA_ROLE, RISA_SYNC_ROLE } from "./risaRole";
import { RISA_ASSETS_ROLE } from "./risaAssetsRole";
import { RISA_ROM_PROVIDER } from "./romProvider";
import { risaIntegration } from "./risaIntegration";
import { RISA_KIT_FORMAT } from "./kitFormat";
// The keys the owner packages add to the contract (their modules augment PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";

export default definePackage({
  roles: [RISA_ROLE, RISA_ASSETS_ROLE, RISA_SYNC_ROLE],
  romProviders: [RISA_ROM_PROVIDER],
  trackerIntegrations: [risaIntegration],
  kitFormats: [RISA_KIT_FORMAT],
});
