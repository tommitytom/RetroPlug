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
import { identifyLsdj } from "../lsdj/runtime/identify";
import { identifyRisaVersion } from "../risa/runtime/identify";
import { resolveRisaLayout } from "../risa/runtime/layout";

export interface TrackerIntegration {
  /** Row-id prefix + instance-submenu id suffix, e.g. "lsdj" / "risa". */
  readonly id: string;
  /** Instance-submenu label, e.g. "LSDj". */
  readonly label: string;
  /** The role kind the ROM provider attaches that identifies this console (the menu gate). */
  readonly markerRole: string;
  readonly songs: SongCatalog;
  readonly assets: AssetCatalog;
  /** The cart's OWN name — its internal ROM title / embedded version marker, NOT the on-disk filename
   *  (e.g. "LSDj v9.4.2" from the GB cartridge title, "risa v2.2.1" from the PRG "RISA V" marker). null
   *  when the ROM bytes carry no recognisable version. */
  romName(rom: Uint8Array): string | null;
  /** Whether THIS build can drive the cart's embedded version - it has the RAM-offset layout the runtime
   *  overlay + Songs/Assets menus read. LSDj drives every version (predicate omitted -> always supported);
   *  risa only versions with a bundled layout. false -> the menu greys the tracker submenu as
   *  "(Unsupported Version)" rather than offering dead rows. */
  isVersionSupported?(rom: Uint8Array): boolean;
}

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
};

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
  // Only versions with a bundled RAM layout (2.2.0 / 2.2.1 / 2.3.0) are driveable; an unknown / unmarked
  // version resolves no layout. See supportedRisaVersions() in ../risa/runtime/layout.
  isVersionSupported: (rom) => resolveRisaLayout(identifyRisaVersion(rom)) !== null,
};

/** Every registered tracker integration. The one place a new tracker console is added. */
export const TRACKER_INTEGRATIONS: TrackerIntegration[] = [lsdjIntegration, risaIntegration];
