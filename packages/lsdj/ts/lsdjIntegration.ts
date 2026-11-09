// LSDj's tracker integration: its Songs catalog (the battery's projects, the shared song menu and recent-song names) and
// its Assets catalog (the ROM's replaceable kits/palettes/fonts), with the ROM name read from the cartridge title, its
// playback (the controller's poll and the anchor), its battery signature (the auto-save) and its song-file import. The
// contract is @retroplug/tracker's; what it is made of is this package's, beside the codecs they read.
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { LSDJ_RENDER } from "./lsdjRender";
import { LSDJ_PLAYBACK } from "./lsdjPlayback";
import { lsdjSramSignature } from "./lsdjSramSignature";
import { LSDJ_SONG_IMPORT } from "./lsdjSongImport";
import { identifyLsdj } from "./runtime/identify";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";
import { isLsdjSav, listProjects } from "./codec/sav";

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
  playback: LSDJ_PLAYBACK,
  sramSignature: lsdjSramSignature,
  songImport: LSDJ_SONG_IMPORT,
  // LSDj's battery rows, printed for a CLI listing: the project's slot and its name. Tried after risa (rank 1).
  batteryListing: {
    name: "LSDj",
    noun: "project",
    rank: 1,
    describe: (sav) =>
      isLsdjSav(sav)
        ? listProjects(sav).map((p) => `  [${String(p.slot).padStart(2)}] ${p.name.trim() || "(unnamed)"}`)
        : null,
  },
};
