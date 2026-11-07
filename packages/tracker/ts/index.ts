// The tracker layer's front door: the contracts a music-tracker cart satisfies (its SONG catalog over the battery, its
// ASSET catalog over the base ROM, the TrackerIntegration that bundles the two, the playback position a controller
// surface reads) and the formats they name (the rit asset overrides, the NES kit bank / CHR font / theme record).
// Nothing here is a cart: the concrete catalogs are the carts' own, and a cart contributes its integration through its
// app module. The shell's barrel (packages/retroplug/src/tracker/index.ts) adds the carts' catalogs and the
// store-facing helpers (liveSav) to
// this surface; a package imports the module it names.
export * from "./songCatalog";
export * from "./assetCatalog";
export * from "./trackerIntegration";
export * from "./playbackModel";
export * from "./workingSongBlob";
export * from "./ritAssets";
