// risa's tracker integration: what a risa system's Songs and Assets menus are built over. The song catalog reads the
// RSav battery (its records, the working song and the catalog layout), the asset catalog reads the ROM's replaceable
// kits, themes and fonts, and the two ROM questions - which build this is ("RISA V<major>.<minor>.<patch>" in the PRG)
// and whether it can be driven (only the versions with a bundled RAM layout can) - come from the runtime reader.
//
// The menus themselves are the shell's (ui/screens/menu); an integration is the data they are built from, resolved by
// a system's markerRole (the shell's trackerRegistry).
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { risaSongCatalog } from "./risaSongCatalog";
import { risaAssetCatalog } from "./risaAssetCatalog";
import { identifyRisaVersion } from "./runtime/identify";
import { resolveRisaLayout } from "./runtime/layout";

export const risaIntegration: TrackerIntegration = {
  id: "risa",
  label: "Risa",
  markerRole: "risa",
  songs: risaSongCatalog,
  assets: risaAssetCatalog,
  romName: (rom) => {
    const v = identifyRisaVersion(rom); // scans the PRG for the ASCII "RISA V<major>.<minor>.<patch>" marker
    return v ? `risa v${v}` : null;
  },
  // Only versions with a bundled RAM layout (2.2.0 / 2.2.1 / 2.3.0) are driveable; an unknown / unmarked version
  // resolves no layout. See supportedRisaVersions() in ./runtime/layout.
  isVersionSupported: (rom) => resolveRisaLayout(identifyRisaVersion(rom)) !== null,
};
