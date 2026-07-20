// The tracker-integration layer: a console-agnostic view of a music-tracker cart (LSDj, risa, …). Each console
// is one TrackerIntegration bundling its SONG catalog (the battery — shared song menu + recent-song names) and
// ASSET catalog (the ROM's replaceable kits/palettes/fonts — shared asset menu). New consoles add their
// integration to TRACKER_INTEGRATIONS (./trackerIntegration) — the one extension point (like the CLI `tools` array).
import type { RoleInstance } from "../systemRoles";
import type { SongCatalog } from "./songCatalog";
import type { AssetCatalog } from "./assetCatalog";
import { TRACKER_INTEGRATIONS, type TrackerIntegration } from "./trackerIntegration";

export type { SongCatalog, SongInfo } from "./songCatalog";
export type { AssetCatalog, AssetSlot, AssetSlotRow, AssetOverride, AssetTypeInfo } from "./assetCatalog";
export { effectiveAssets, readAssetOverrides } from "./assetCatalog";
export type { TrackerIntegration } from "./trackerIntegration";
export { TRACKER_INTEGRATIONS } from "./trackerIntegration";
export { lsdjSongCatalog } from "./lsdjSongCatalog";
export { risaSongCatalog } from "./risaSongCatalog";
export { lsdjAssetCatalog } from "./lsdjAssetCatalog";
export { risaAssetCatalog } from "./risaAssetCatalog";
export { evermidiAssetCatalog } from "./evermidiAssetCatalog";

/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
 *  integration's markerRole). undefined for a non-tracker system. */
export function resolveTracker(roles: RoleInstance[]): TrackerIntegration | undefined {
  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
}

/** The song catalog for a system (convenience over resolveTracker) — used by the recent list. */
export function resolveSongCatalog(roles: RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}
