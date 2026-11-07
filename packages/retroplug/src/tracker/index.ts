// The shell's front door to the tracker layer: @retroplug/tracker's contracts and formats, the consoles' concrete
// catalogs (each cart's own, in its package where it has one), and the store-facing helpers that
// read a running system (./liveSav, which needs the shell's backend and stores). The contracts live in the package and
// are re-exported here so the UI and the stores keep one door; a package imports @retroplug/tracker directly.
//
// A console is one TrackerIntegration bundling its SONG catalog (the battery - the shared song menu and recent-song
// names) and its ASSET catalog (the ROM's replaceable kits/palettes/fonts - the shared asset menu). A cart adds its
// integration to the product's list through its app module; the resolvers over that list are src/trackerRegistry.ts.

export type { SongCatalog, SongInfo } from "@retroplug/tracker/songCatalog";
export { workingSongTargets } from "@retroplug/tracker/songCatalog";
export type {
  AssetCatalog,
  AssetSlot,
  AssetSlotRow,
  AssetOverride,
  AssetTypeInfo,
} from "@retroplug/tracker/assetCatalog";
export { effectiveAssets, readAssetOverrides } from "@retroplug/tracker/assetCatalog";
export type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
export { lsdjSongCatalog } from "@retroplug/lsdj/lsdjSongCatalog";
export { risaSongCatalog } from "@retroplug/risa/risaSongCatalog";
export { lsdjAssetCatalog } from "@retroplug/lsdj/lsdjAssetCatalog";
export { risaAssetCatalog } from "@retroplug/risa/risaAssetCatalog";
export { smsggdjSongCatalog } from "@retroplug/smsggdj/smsggdjSongCatalog";
export { smsggdjAssetCatalog } from "@retroplug/smsggdj/smsggdjAssetCatalog";
export { bliptoasterAssetCatalog } from "@retroplug/bliptoaster/bliptoasterAssetCatalog";

export {
  mutateLiveSav,
  loadSongByName,
  loadSongInPrimary,
  songLoadWouldDiscard,
  savEditWouldDiscard,
  loadSongLive,
  songLoadByNameWouldDiscard,
  workingSongReady,
  backupSavPath,
  type LiveSavTarget,
} from "./liveSav";
