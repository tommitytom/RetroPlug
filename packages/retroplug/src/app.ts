// The shell's own app contribution, its package.json's retroplug.app: the lists the app hardcoded in appHost.ts,
// romProviders.ts, tracker/trackerIntegration.ts and kit/compile.ts until the generated registry
// (src/generated/packages.ts) concatenated every package's. The shell carries what no package owns yet, in the order
// the app registered it; a package that grows an app module contributes its own list from there.
import { definePackage } from "@retroplug/core/definePackage";
import { CORE_ROLES } from "./coreRoles";
import { DSP_ROLES } from "./dspRoles";
import { LSDJ_ASSETS_ROLE } from "./lsdjAssetsRole";
import { RISA_ROLE } from "./risaRole";
import { RISA_ASSETS_ROLE } from "./risaAssetsRole";
import { BLIPTOASTER_ROLE } from "./bliptoasterRole";
import { BLIPTOASTER_ASSETS_ROLE } from "./bliptoasterAssetsRole";
import {
  BLIPTOASTER_ROM_PROVIDER,
  LSDJ_ROM_PROVIDER,
  MGB_ROM_PROVIDER,
  NES_ROM_PROVIDER,
  RISA_ROM_PROVIDER,
  SMSGGDJ_ROM_PROVIDER,
} from "./romProviderList";
import { blipToasterIntegration, lsdjIntegration, risaIntegration, smsggdjIntegration } from "./tracker/integrations";
import { LSDJ_KIT_FORMAT, RISA_KIT_FORMAT } from "./kit/compile";
// The keys the owner packages add to the contract (their modules augment PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "./tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";

export default definePackage({
  // The backend "system" roles (coreRoles), the feature roles that have a protocol node (dspRoles), and those that
  // have none (asset overrides, a ROM's facts), in the order buildAppRegistry registered them.
  roles: [
    ...CORE_ROLES,
    ...DSP_ROLES,
    LSDJ_ASSETS_ROLE,
    RISA_ROLE,
    RISA_ASSETS_ROLE,
    BLIPTOASTER_ROLE,
    BLIPTOASTER_ASSETS_ROLE,
  ],
  // The providers attach a ROM's feature roles by its identity, in the order the registry offers them.
  romProviders: [
    MGB_ROM_PROVIDER,
    LSDJ_ROM_PROVIDER,
    NES_ROM_PROVIDER,
    SMSGGDJ_ROM_PROVIDER,
    RISA_ROM_PROVIDER,
    BLIPTOASTER_ROM_PROVIDER,
  ],
  // The Songs and Assets menus' consoles, in the order they are searched (a system's first match wins).
  trackerIntegrations: [lsdjIntegration, risaIntegration, smsggdjIntegration, blipToasterIntegration],
  // The kit bank compilers the audio driver dispatches to by id.
  kitFormats: [LSDJ_KIT_FORMAT, RISA_KIT_FORMAT],
});
