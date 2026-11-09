// LSDj's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.4): its sync rows, its Songs and
// Assets specs and the fragment it builds. The rows are data over the `lsdj-sync` role; the file-dialog actions are the
// cart's own because only LSDj knows its formats (.lsdsng/.lsdprj, .kit/.lsdpal/.png), and they reach the shell through
// the UI host's groups only - the dialog, the battery, the role edits and the one full-window screen it opens.
import type { FileBrowserOpts, Host, SystemInfo } from "@retroplug/core/host";
import type { AssetTypeInfo } from "@retroplug/tracker/assetCatalog";
import { roleConfig } from "@retroplug/core/systemRoles";
import { SAV_PATTERNS, isSavPath, sanitizeName, stem } from "@retroplug/core/path";
import { action, browseThen, separator, type MenuItem } from "@retroplug/ui/menu";
import {
  defineUi,
  type AssetMenuSpec,
  type RoleRow,
  type SongMenuSpec,
  type TrackerUi,
} from "@retroplug/ui/contribution";
import { applyOverridesToRom, readOverrides, type LsdjAssetOverride } from "../lsdjAssetsRole";
import { lsdjAssetCatalog } from "../lsdjAssetCatalog";
import { lsdjSongCatalog } from "../lsdjSongCatalog";
import {
  canSaveWorkingToCatalog,
  replaceSongInSav,
  saveWorkingToCatalog as lsdjSaveWorkingToCatalog,
} from "../lsdjSongOps";
import { planLsdprjImport } from "../lsdjLsdprjImport";
import { lsdjIntegration } from "../lsdjIntegration";
import { LSDJ_MODE_VALUES } from "../enums";
import { LsdjRom, decodeLsdpal, encodeLsdpal } from "../rom";
import { activeSlot as lsdjActiveSlot, decompressSlot, savSongName, savSongVersion } from "../codec/sav";
import { encodeLsdsngRaw } from "../codec/lsdsng";

/** The host groups this cart's rows reach: narrowed to what they use, so a third-party pack copied
 *  from this one takes exactly these and nothing more. */
type LsdjUiHost = Host<"files" | "png" | "systems" | "liveSongs" | "views" | "dialogs">;

/** LSDj sync modes (LsdjSyncMode 0..8), as the menu spells them. All shown; "Keyboard"(4) is not yet driven. "MIDI
 *  Out"(7, SYNC=MI.OUT) and "Master Sync"(8, SYNC=LSDJ) are the two LSDj-to-host MIDI-out modes. */
const LSDJ_MODE_NAMES = [
  "Off",
  "MIDI Sync",
  "MIDI Sync (Arduinoboy)",
  "MIDI Map",
  "Keyboard",
  "Keyboard MIDI",
  "MIDI Passthrough",
  "MIDI Out",
  "Master Sync",
];
/** Tempo divisor subdivides the 24-PPQN clock (24/divisor) for MIDI Sync + MIDI Sync (Arduinoboy). */
const LSDJ_DIVISORS = [1, 2, 4, 8];

const lsdjOverrides = (sys: SystemInfo): LsdjAssetOverride[] => readOverrides(roleConfig(sys.roles, "lsdj-assets"));

const readLsdjRom = (host: Host<"files">, romPath: string): LsdjRom | null => {
  const bytes = romPath ? host.files.readFile(romPath) : null;
  if (!bytes) return null;
  const rom = LsdjRom.fromBytes(bytes);
  return rom.isLsdj ? rom : null;
};

// --- LSDj asset file actions (Export/Replace own the .kit/.lsdpal/.png formats) -----------------------
// Export asset `type`/`slot` to a picked file: the override bytes if replaced (already the file format), else
// the base ROM's asset read straight out via the pure-TS module.
function exportAsset(host: LsdjUiHost, sys: SystemInfo, type: AssetTypeInfo, slot: number, label: string): void {
  const kind = type.kind;
  const ov = lsdjOverrides(sys).find((o) => o.type === kind && o.slot === slot);
  const defaultName = `${sanitizeName(label)}${type.ext}`;
  browseThen(host, { title: `Export ${kind} ${slot}`, patterns: type.patterns, saving: true, defaultName }, (path) => {
    // An overridden slot's current asset comes from the override (a palette re-encodes its inline colours;
    // a kit/font copies its linked file); otherwise read it from the base ROM.
    let bytes: Uint8Array | null = null;
    if (ov)
      bytes =
        ov.type === "palette" && ov.colorSets
          ? encodeLsdpal(ov.name ?? "", ov.colorSets)
          : ov.path
            ? host.files.readFile(ov.path)
            : null;
    if (!bytes) {
      const rom = readLsdjRom(host, sys.romPath);
      if (!rom) return;
      if (kind === "kit") bytes = rom.exportKitFile(slot);
      else if (kind === "palette") bytes = rom.exportPaletteFile(slot);
      else {
        const img = rom.exportFontImage(slot, true); // include the extended gfx tiles (64x120) - lossless
        bytes = host.png.encode(img.width, img.height, img.rgba);
      }
    }
    if (bytes && bytes.length) host.files.writeFileAtomic(path, bytes);
  });
}

// Replace asset `type`/`slot` from a picked file: validate by trial-applying to the base ROM (import* throws
// on a bad file), record the override (kit/font by PATH, palette INLINE) in role config, and reload so it
// takes effect. NON-DESTRUCTIVE - the base .gb is never written.
function replaceAsset(host: LsdjUiHost, sys: SystemInfo, type: AssetTypeInfo, slot: number): void {
  const kind = type.kind;
  browseThen(host, { title: `Replace ${kind} ${slot}`, patterns: type.patterns }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    const rom = readLsdjRom(host, sys.romPath);
    if (!rom) return;
    // The new override: PALETTES are stored inline as structured colours (never a file link); KITS/FONTS
    // link to the file on disk by path. Validate by trial-applying to the base ROM (import* throws on a
    // bad file / out-of-range slot) - leaving the real ROM untouched.
    let entry: LsdjAssetOverride;
    try {
      if (kind === "kit") {
        rom.importKitFile(slot, data);
        entry = { type: "kit", slot, name: rom.kit(slot).name() || stem(path), path };
      } else if (kind === "palette") {
        const decoded = decodeLsdpal(data);
        if (!decoded) return; // not a valid .lsdpal
        rom.importPaletteFile(slot, data); // validates the slot is in range on this ROM
        entry = { type: "palette", slot, name: decoded.name || stem(path), colorSets: decoded.colorSets };
      } else {
        const img = host.png.decode(data);
        if (!img) return; // not a decodable PNG
        rom.importFontImage(slot, img);
        entry = { type: "font", slot, name: stem(path), path };
      }
    } catch {
      return; // invalid asset file (wrong size / bad image) -> leave the ROM untouched
    }
    const overrides = [...lsdjOverrides(sys).filter((o) => !(o.type === kind && o.slot === slot)), entry];
    host.systems.setRoleConfig(sys.id, "lsdj-assets", { overrides });
    host.systems.reload(sys.id);
  });
}

const lsdjAssetSpec: AssetMenuSpec = { id: "lsdj", catalog: lsdjAssetCatalog, exportAsset, replaceAsset };

// --- LSDj Songs (the SAV's 32 saved-song slots: export / replace / delete / add) ----------------------
// Songs are the battery, NOT a ROM override: edits act directly on the live SRAM (like LSDj's own FILE
// screen) via the host's liveSongs.mutate - read the live sav, apply a BYTE-LEVEL transform (never the lossy
// Song model - see lsdjSongOps), write the resolved .sav, cold-boot the core from it. Durable on disk and reflected
// in the running LSDj.

// Export the saved song in `slot` to a picked `.lsdsng` file (byte-exact - decompress the slot, re-wrap).
function exportSong(host: LsdjUiHost, sys: SystemInfo, slot: number, name: string): void {
  const opts: FileBrowserOpts = {
    title: `Export song ${slot}`,
    patterns: ["*.lsdsng"],
    saving: true,
    defaultName: `${sanitizeName(name)}.lsdsng`,
  };
  browseThen(host, opts, (path) => {
    const bytes = host.systems.readSram(sys.id);
    if (!bytes) return;
    const raw = decompressSlot(bytes, slot);
    if (!raw) return;
    host.files.writeFileAtomic(
      path,
      encodeLsdsngRaw(savSongName(bytes, slot) || name, savSongVersion(bytes, slot), raw),
    );
  });
}

// Import a `.lsdprj` (song + its kit banks). The song goes into a slot (targetSlot for Replace, else the
// first free slot); its kits are deduped against the effective ROM and the missing ones are recorded as
// lsdj-assets kit overrides that LINK the `.lsdprj` by path (+ ordinal), with the song's kit references
// remapped to the assigned slots (all byte-level - the Song model can't hold 6-bit kit indices). One rebuild
// (loadSram) applies the kit overrides -> romBytes and boots the imported song from the .sav.
function importLsdprj(host: LsdjUiHost, sys: SystemInfo, path: string, targetSlot?: number): void {
  const file = host.files.readFile(path);
  const liveSram = host.systems.readSram(sys.id);
  const baseRom = sys.romPath ? host.files.readFile(sys.romPath) : null;
  if (!file || !liveSram || !baseRom) return;

  const overrides = lsdjOverrides(sys);
  const effectiveRom = applyOverridesToRom(baseRom, overrides, host);
  const plan = planLsdprjImport({ file, path, effectiveRom, overrides, liveSram, targetSlot });
  if (!plan) return; // malformed file / out of kit or song slots

  const target = host.systems.savPathOf(sys.id);
  if (!target || !host.files.writeFileAtomic(target, plan.savBytes)) return;
  if (plan.addedKits > 0) host.systems.setRoleConfig(sys.id, "lsdj-assets", { overrides: plan.overrides });
  host.systems.loadSram(sys.id, target); // one rebuild: kit overrides -> romBytes + boot the imported song
}

// Replace the song in `slot` from a picked `.lsdsng` or `.lsdprj` (byte-exact; a bad file leaves the sav
// alone). A `.lsdprj` also imports its kits (see importLsdprj).
function replaceSong(host: LsdjUiHost, sys: SystemInfo, slot: number): void {
  browseThen(host, { title: `Replace song ${slot}`, patterns: ["*.lsdsng", "*.lsdprj"] }, (path) => {
    if (path.toLowerCase().endsWith(".lsdprj")) {
      importLsdprj(host, sys, path, slot);
      return;
    }
    const data = host.files.readFile(path);
    if (!data) return;
    host.liveSongs.mutate(sys, (sav) => replaceSongInSav(sav, slot, data));
  });
}

// Add a song: a `.lsdsng`/`.lsdprj` into the first free slot (the same importer drag-and-drop uses), or a
// selection of songs from a `.sav`/`.srm` (the checkbox picker, validated against the cart's console).
function addSongFromDisk(host: LsdjUiHost, sys: SystemInfo): void {
  browseThen(host, { title: "Add Song", patterns: ["*.lsdsng", "*.lsdprj", ...SAV_PATTERNS] }, (path) => {
    if (isSavPath(path)) {
      const data = host.files.readFile(path);
      if (data) host.liveSongs.beginImport(sys, data);
    } else {
      lsdjIntegration.songImport?.import(host, sys, [path]);
    }
  });
}

// The LSDj working-song row's actions. The row shows only for a LINKED working song (see lsdjSongCatalog),
// so both of these inherit the active slot's name + version - LSDj keeps those on the stored project rather
// than in the song, so a detached working song has neither.
function exportWorking(host: LsdjUiHost, sys: SystemInfo, name: string): void {
  const opts: FileBrowserOpts = {
    title: "Export working song",
    patterns: ["*.lsdsng"],
    saving: true,
    defaultName: `${sanitizeName(name)}.lsdsng`,
  };
  browseThen(host, opts, (path) => {
    const bytes = host.systems.readSram(sys.id);
    if (!bytes) return;
    const slot = lsdjActiveSlot(bytes);
    if (slot < 0) return;
    // The LIVE working song (the first 0x8000), not the stored slot - exporting the edits is the point.
    host.files.writeFileAtomic(
      path,
      encodeLsdsngRaw(savSongName(bytes, slot) || name, savSongVersion(bytes, slot), bytes.slice(0, 0x8000)),
    );
  });
}

function saveWorking(host: LsdjUiHost, sys: SystemInfo): void {
  // No name argument: the row is linked-only, so saveWorkingToCatalog overwrites its slot and keeps its name.
  host.liveSongs.mutate(sys, (sav) => lsdjSaveWorkingToCatalog(sav));
}

const lsdjSongSpec: SongMenuSpec = {
  id: "lsdj",
  catalog: lsdjSongCatalog,
  exportSong,
  replaceSong,
  addSong: addSongFromDisk,
  exportWorking,
  saveWorkingToCatalog: saveWorking,
  commitWorking: lsdjSaveWorkingToCatalog,
  // Only an UNLINKED LSDj working song needs a name: a linked one inherits its slot's.
  workingNeedsName: (sav) => lsdjActiveSlot(sav) < 0,
  canCommitWorking: canSaveWorkingToCatalog,
};

// The tail of LSDj's tracker extras, after the sync rows: the HD Player action and the separator before the asset
// menus. The HD player is song + all four chains + all four phrases on one full-window screen, in the cart's own font
// and palette; the cart keeps playing and stays playable underneath, and Esc returns to the grid.
function extrasBuild(host: LsdjUiHost, sys: SystemInfo): MenuItem[] {
  return [action("lsdj-hd", "HD Player", () => host.views.open("lsdj-hd", sys.id)), separator("lsdj-assets-sep")];
}

/** The tracker's UI: its sync rows, its Songs and Assets specs and the HD Player fragment. */
export const LSDJ_TRACKER_UI: TrackerUi = {
  id: "lsdj",
  song: lsdjSongSpec,
  asset: lsdjAssetSpec,
  extras: {
    rows: [
      {
        id: "lsdj-mode",
        label: "Mode",
        role: "lsdj-sync",
        key: "mode",
        choices: LSDJ_MODE_VALUES.map((value, i) => ({ value, label: LSDJ_MODE_NAMES[i] })),
      },
      {
        id: "lsdj-divisor",
        label: "Tempo Divisor",
        role: "lsdj-sync",
        key: "tempoDivisor",
        choices: LSDJ_DIVISORS.map((value) => ({ value, label: String(value) })),
      },
      // Auto Start taps START on the host transport rise to auto-arm SYNC=MIDI carts (MidiSync / Arduinoboy): off by
      // default, so the modes keep their manual-arm behaviour.
      { id: "lsdj-autostart", label: "Auto Start", role: "lsdj-sync", key: "autoStart", toggle: true },
    ] satisfies RoleRow[],
    build: extrasBuild,
  },
};

export default defineUi({ trackers: [LSDJ_TRACKER_UI] });
