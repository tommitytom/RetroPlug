// smsggdj exposes no ROM assets to RetroPlug YET, so this is an empty catalog rather than an absent one.
//
// The cart does have replaceable assets - 8 sample kits, 8 recolourable UI palettes, 8 FM presets, all
// patchable without recompiling - and they map onto AssetCatalog the way LSDj's and risa's do. That is
// its own slice: it needs the ROM-side parsing (a different problem from the save format) and an
// `sms-assets` override role to carry non-destructive replacements.
//
// Empty rather than making `TrackerIntegration.assets` optional: `types: []` already means "this
// console contributes no asset submenus", every consumer handles it, and widening the interface for one
// temporary case would push the same emptiness check into the menu instead.
import type { AssetCatalog, AssetSlot } from "./assetCatalog";

export const smsggdjAssetCatalog: AssetCatalog = {
  assetRole: "sms-assets", // reserved; no role registers under it yet, so no overrides can exist
  types: [],
  baseSlots: (): AssetSlot[] => [],
  applyOverrides: (romBytes: Uint8Array): Uint8Array => romBytes,
};
