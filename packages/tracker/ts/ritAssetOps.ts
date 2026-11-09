// The shared `.rit` asset menu actions (docs/modular-graph-phase-f4b-design.md section 4.4): risa and BlipToaster carry
// the SAME asset surface - a theme table, 8 KB CHR font banks and 8 KB DMC kit banks - down to the file formats
// (.rit / .chr / .rkit), the validation constants and the inline-vs-by-path split. Their two Export/Replace pairs were
// four functions that differed only in which class parsed the ROM and which role kind the override was written to, so
// those are the two things a cart supplies through `RitAssetOps`.
//
// It lives here rather than in risa because BlipToaster does not depend on risa (risa's own rom/index.ts says so), and
// the code needs no menu type: it takes the host's file I/O, its dialog and its role edits. LSDj is deliberately NOT
// folded in: its formats differ, and it validates by trial-applying to a parsed ROM rather than by length, which is a
// genuinely different shape.
import type { FileBrowserOpts, Host, SystemInfo } from "@retroplug/core/host";
import { roleConfig } from "@retroplug/core/systemRoles";
import { sanitizeName, stem } from "@retroplug/core/path";
import type { AssetTypeInfo } from "./assetCatalog";
import { readRitOverrides, type RitAssetOverride } from "./ritAssets";
import { CHR_BANK_SIZE, KIT_BANK_SIZE } from "./nes/constants";
import { decodeThemeFromRom, parseRit, serializeRit } from "./nes/theme";
import { bankToModel, isBankPopulated } from "./nes/kitBank";

/** What one cart supplies to the shared actions: the role kind its override list lives on, and how to open its ROM. */
export interface RitAssetOps {
  readonly role: string;
  openRom(host: Host<"files">, romPath: string): RitAssetRomReader | null;
}

/** The slice of a parsed ROM the shared Export reads. RisaRom and BlipToasterRom both satisfy it as written. */
export interface RitAssetRomReader {
  getTheme(slot: number): { recordBytes: Uint8Array; nameBytes: Uint8Array } | null;
  getKitBank(slot: number): Uint8Array | null;
  getChrFontSlot(slot: number): Uint8Array | null;
}

/** An override's linked bank, or null when the link is broken. A `.rkit` / `.chr` override records a PATH, and the user
 *  is free to move or delete that file afterwards - at which point Export used to produce nothing at all, silently,
 *  because a null read fell through the `if (bytes && bytes.length)` guard with no message. Returning null here lets
 *  the caller fall back to the base ROM's own bank, which is both useful (that is the asset the cart will actually boot
 *  with once the dead override is removed) and what the LSDj copy of these actions has always done. */
const readLinked = (host: Host<"files">, path: string | undefined): Uint8Array | null =>
  path ? host.files.readFile(path) : null;

const ritOverrides = (sys: SystemInfo, role: string): RitAssetOverride[] =>
  readRitOverrides(roleConfig(sys.roles, role));

/** Open the host's file dialog and apply the pick; a cancel is ignored. A local copy of the menu layer's helper
 *  because this package is what `@retroplug/ui` itself depends on, so it may not import it. */
function pick(host: Host<"dialogs">, opts: FileBrowserOpts, apply: (path: string) => void): void {
  void host.dialogs.browse(opts).then((path) => {
    if (path) apply(path);
  });
}

/** Export theme/font/kit `slot` to a picked file: the override's data if replaced, else the base ROM's asset. */
export function exportRitAsset(
  ops: RitAssetOps,
  host: Host<"files" | "dialogs">,
  sys: SystemInfo,
  type: AssetTypeInfo,
  slot: number,
  label: string,
): void {
  const kind = type.kind;
  const ov = ritOverrides(sys, ops.role).find((o) => o.type === kind && o.slot === slot);
  const defaultName = `${sanitizeName(label)}${type.ext}`;
  pick(host, { title: `Export ${kind} ${slot}`, patterns: type.patterns, saving: true, defaultName }, (path) => {
    let bytes: Uint8Array | null = null;
    if (kind === "theme") {
      // The theme comes from the inline override, or the base ROM decoded; emit a .rit (readable JSON).
      let theme = ov?.theme ?? null;
      if (!theme) {
        const t = ops.openRom(host, sys.romPath)?.getTheme(slot);
        if (t) theme = decodeThemeFromRom(t.recordBytes, t.nameBytes);
      }
      if (theme) bytes = new TextEncoder().encode(JSON.stringify(serializeRit(theme), null, 2) + "\n");
    } else if (kind === "kit") {
      // The linked bank if overridden, else the base ROM's 8 KB DMC bank - either is a ready-to-link .rkit.
      bytes = readLinked(host, ov?.path) ?? ops.openRom(host, sys.romPath)?.getKitBank(slot) ?? null;
    } else {
      bytes = readLinked(host, ov?.path) ?? ops.openRom(host, sys.romPath)?.getChrFontSlot(slot) ?? null;
    }
    if (bytes && bytes.length) host.files.writeFileAtomic(path, bytes);
  });
}

/** Replace theme/font/kit `slot` from a picked file: validate it, record the override (theme INLINE / font + kit by
 *  PATH) in role config, and reload so it takes effect. NON-DESTRUCTIVE: the base .nes is never written. */
export function replaceRitAsset(
  ops: RitAssetOps,
  host: Host<"files" | "dialogs" | "systems">,
  sys: SystemInfo,
  type: AssetTypeInfo,
  slot: number,
): void {
  const kind = type.kind;
  pick(host, { title: `Replace ${kind} ${slot}`, patterns: type.patterns }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    let entry: RitAssetOverride;
    try {
      if (kind === "theme") {
        const { theme } = parseRit(JSON.parse(new TextDecoder().decode(data))); // throws on a bad .rit
        entry = { type: "theme", slot, name: theme.name.trim() || stem(path), theme };
      } else if (kind === "kit") {
        if (data.length !== KIT_BANK_SIZE || !isBankPopulated(data)) return; // exactly one populated 8 KB DMC bank
        entry = { type: "kit", slot, name: bankToModel(data).name.trim() || stem(path), path };
      } else {
        if (data.length !== CHR_BANK_SIZE) return; // a .chr is exactly one 8 KB CHR bank
        entry = { type: "font", slot, name: stem(path), path };
      }
    } catch {
      return; // malformed .rit / unreadable -> leave the ROM untouched
    }
    const overrides = [
      ...ritOverrides(sys, ops.role).filter((o) => !(o.type === kind && o.slot === slot)),
      entry,
    ];
    host.systems.setRoleConfig(sys.id, ops.role, { overrides });
    host.systems.reload(sys.id);
  });
}
