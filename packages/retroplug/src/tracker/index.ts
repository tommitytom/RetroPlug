// The tracker-integration layer: a console-agnostic view of a music-tracker cart (LSDj, risa, …). Each console
// is one TrackerIntegration bundling its SONG catalog (the battery — shared song menu + recent-song names) and
// ASSET catalog (the ROM's replaceable kits/palettes/fonts — shared asset menu). New consoles add their
// integration to TRACKER_INTEGRATIONS (./trackerIntegration) — the one extension point (like the CLI `tools` array).
// This module is the layer's front door: it only re-exports, so a leaf (./liveSav) can import the resolvers
// from ./trackerIntegration without an import cycle through here.

export type { SongCatalog, SongInfo } from "./songCatalog";
export type { AssetCatalog, AssetSlot, AssetSlotRow, AssetOverride, AssetTypeInfo } from "./assetCatalog";
export { effectiveAssets, readAssetOverrides } from "./assetCatalog";
export type { TrackerIntegration } from "./trackerIntegration";
export { TRACKER_INTEGRATIONS, resolveTracker, resolveSongCatalog, resolveAssetCatalog } from "./trackerIntegration";
export { lsdjSongCatalog } from "./lsdjSongCatalog";
export { risaSongCatalog } from "./risaSongCatalog";
export { lsdjAssetCatalog } from "./lsdjAssetCatalog";
export { risaAssetCatalog } from "./risaAssetCatalog";
export { mutateLiveSav, loadSongByName, loadSongInPrimary, type LiveSavTarget } from "./liveSav";
