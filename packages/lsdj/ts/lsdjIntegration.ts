// LSDj's tracker integration: its Songs catalog (the battery's projects, the shared song menu and recent-song names) and
// its Assets catalog (the ROM's replaceable kits/palettes/fonts), with the ROM name read from the cartridge title. The
// contract is @retroplug/tracker's; the catalog objects are this package's, beside the codecs they read.
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { LSDJ_RENDER } from "./lsdjRender";
import { identifyLsdj } from "./runtime/identify";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";

export const lsdjIntegration: TrackerIntegration = {
  id: "lsdj",
  label: "LSDj",
  markerRole: "lsdj-sync",
  songs: lsdjSongCatalog,
  assets: lsdjAssetCatalog,
  romName: (rom) => {
    const v = identifyLsdj(rom); // parses the GB cartridge title at 0x134 ("LSDJ-Vx.y.z")
    return v ? `LSDj v${v.major}.${v.minor}.${v.patchLabel}${v.build ? ` ${v.build}` : ""}` : null;
  },
  render: LSDJ_RENDER,
};
