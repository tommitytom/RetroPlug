// A tracker "cart" integration — the console-agnostic bundle of a music tracker's SONG catalog (the battery)
// and ASSET catalog (the ROM's replaceable kits/palettes/fonts). One integration per console (LSDj, risa, …),
// resolved from a system's roles by its markerRole. The menu builds the Songs + asset submenus generically
// over these; the file-dialog actions (which own file formats) stay per-console in the UI. New consoles add
// one entry to TRACKER_INTEGRATIONS — the single extension point.
import type { SongCatalog } from "./songCatalog";
import type { AssetCatalog } from "./assetCatalog";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { risaSongCatalog } from "./risaSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";
import { risaAssetCatalog } from "./risaAssetCatalog";

export interface TrackerIntegration {
  /** Row-id prefix + instance-submenu id suffix, e.g. "lsdj" / "risa". */
  readonly id: string;
  /** Instance-submenu label, e.g. "LSDj". */
  readonly label: string;
  /** The role kind the ROM provider attaches that identifies this console (the menu gate). */
  readonly markerRole: string;
  readonly songs: SongCatalog;
  readonly assets: AssetCatalog;
}

export const lsdjIntegration: TrackerIntegration = {
  id: "lsdj",
  label: "LSDj",
  markerRole: "lsdj-sync",
  songs: lsdjSongCatalog,
  assets: lsdjAssetCatalog,
};

export const risaIntegration: TrackerIntegration = {
  id: "risa",
  label: "risa",
  markerRole: "risa",
  songs: risaSongCatalog,
  assets: risaAssetCatalog,
};

/** Every registered tracker integration. The one place a new tracker console is added. */
export const TRACKER_INTEGRATIONS: TrackerIntegration[] = [lsdjIntegration, risaIntegration];
