// A tracker "cart" integration — the console-agnostic bundle of a music tracker's SONG catalog (the battery)
// and ASSET catalog (the ROM's replaceable kits/palettes/fonts). One integration per console (LSDj, risa, …),
// resolved from a system's roles by its markerRole. The menu builds the Songs + asset submenus generically
// over these; the file-dialog actions (which own file formats) stay per-console in the UI. New consoles add
// one entry to TRACKER_INTEGRATIONS — the single extension point.
import type { RoleInstance } from "../systemRoles";
import type { SongCatalog } from "./songCatalog";
import type { AssetCatalog } from "./assetCatalog";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { risaSongCatalog } from "./risaSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";
import { risaAssetCatalog } from "./risaAssetCatalog";
import { evermidiAssetCatalog } from "./evermidiAssetCatalog";
import { everMidiVersion } from "../evermidi/romDetect";
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
  /** The song catalog (the battery), if this console has one. Asset-only consoles (e.g. EverMIDI, a MIDI
   *  synth with no song battery) omit it — the shared Songs menu is then simply not built. */
  readonly songs?: SongCatalog;
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

// EverMIDI: an NES MIDI synth (not a tracker), so it has NO song battery — only ROM assets (a baked DMC kit
// + the CHR font). An asset-only integration: `songs` is omitted, so only the asset submenus are built.
export const everMidiIntegration: TrackerIntegration = {
  id: "evermidi",
  label: "EverMIDI",
  markerRole: "evermidi",
  assets: evermidiAssetCatalog,
  romName: (rom) => {
    const v = everMidiVersion(rom); // the byte after the "EVERMIDI" head marker (-1 when absent)
    return v >= 0 ? `EverMIDI v${v}` : null;
  },
};

/** Every registered tracker integration. The one place a new tracker console is added. */
export const TRACKER_INTEGRATIONS: TrackerIntegration[] = [lsdjIntegration, risaIntegration, everMidiIntegration];

/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
 *  integration's markerRole). undefined for a non-tracker system. */
export function resolveTracker(roles: RoleInstance[]): TrackerIntegration | undefined {
  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
}

/** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
export function resolveSongCatalog(roles: RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}