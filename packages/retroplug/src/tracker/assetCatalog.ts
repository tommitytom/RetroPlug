// A console-agnostic "asset catalog" — the shared shape LSDj and risa both expose over their base ROM's
// replaceable assets (kits, palettes/themes, fonts), so the asset menu's uniform rows (Export / Replace /
// Delete-for-kits / Remove-Override) can be built once and the effective-slot merge lives in one place. Assets
// ride a per-system NON-DESTRUCTIVE override list (the `*-assets` role config); the catalog knows only the
// base ROM's slots (baseSlots) + the asset types it exposes. The effective view (base overlaid with overrides)
// is effectiveAssets. The per-console FILE actions (Export/Replace — which own file formats + the file dialog)
// stay in the UI menu layer, exactly like SongCatalog's Export/Replace/Add. Concrete catalogs:
// ./lsdjAssetCatalog, ./risaAssetCatalog; resolution rides ./trackerIntegration (resolveAssetCatalog).

/** A base-ROM asset slot: its index + a display name (name-fallbacks already applied). */
export interface AssetSlot {
  slot: number;
  name: string;
}

/** An effective asset slot (base overlaid with the override list): `overridden` when a replace override rides it. */
export interface AssetSlotRow extends AssetSlot {
  overridden: boolean;
}

/** The loose common shape of an asset override entry — the subset the generic menu reads (type/slot/name/erase).
 *  The per-console file-actions build the concrete typed entries (LsdjAssetOverride / RisaAssetOverride), whose
 *  extra fields (path / colorSets / theme / lsdprjKit) ride through untouched (structurally assignable here). */
export interface AssetOverride {
  type: string;
  slot: number;
  name?: string;
  erase?: boolean;
}

/** One asset type a console exposes (a submenu). `patterns`/`ext` drive the file dialog; `addable`/`maxSlots`
 *  are the kit-only "Add.../Delete" affordance (non-kit types are a fixed base-slot list). */
export interface AssetTypeInfo {
  kind: string; // the override `type` value, e.g. "kit" / "palette" / "font" / "theme"
  title: string; // submenu title, e.g. "Kits"
  noun: string; // singular label for a nameless slot, e.g. "Kit" → "Kit 3"
  patterns: string[]; // file-dialog globs, e.g. ["*.kit"]
  ext: string; // default export extension, e.g. ".kit"
  addable: boolean; // Add... + Delete rows (kits only)
  maxSlots: number; // slot bound for Add... (meaningful only when addable)
}

/** A console's replaceable-ROM-asset view. `baseSlots` parses the base ROM for one asset type; the shared
 *  effectiveAssets (below) overlays the override list. The file-dialog actions live in the UI menu layer. */
export interface AssetCatalog {
  /** The feature-role kind that carries this console's override list (e.g. "lsdj-assets"). */
  readonly assetRole: string;
  /** The asset types this console exposes, in menu order. */
  readonly types: AssetTypeInfo[];
  /** The base ROM's slots for one asset type (name-fallbacks applied) — [] when the ROM can't be parsed. */
  baseSlots(romBytes: Uint8Array, kind: string): AssetSlot[];
}

/** The effective slots of one asset type: base slots overlaid with the override list. A `kind` override with
 *  `erase` empties its slot; otherwise it marks the slot overridden (and may add a slot the base lacks — kits).
 *  Sorted by slot. Names: an override prefers its own name, else the base name, else "`${noun} ${slot}`". */
export function effectiveAssets(base: AssetSlot[], overrides: AssetOverride[], kind: string, noun: string): AssetSlotRow[] {
  const rows = new Map<number, AssetSlotRow>();
  for (const s of base) rows.set(s.slot, { slot: s.slot, name: s.name, overridden: false });
  for (const ov of overrides) {
    if (ov.type !== kind) continue;
    if (ov.erase) {
      rows.delete(ov.slot);
      continue;
    }
    const existing = rows.get(ov.slot);
    rows.set(ov.slot, { slot: ov.slot, name: ov.name || existing?.name || `${noun} ${ov.slot}`, overridden: true });
  }
  return [...rows.values()].sort((a, b) => a.slot - b.slot);
}

/** Read the override list off a `*-assets` role config (empty when absent/invalid) — console-agnostic. */
export function readAssetOverrides(config: Record<string, unknown> | undefined): AssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as AssetOverride[]) : [];
}
