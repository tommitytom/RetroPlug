// risa's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.4): its Songs and Assets specs,
// with the file-dialog actions that own its formats - a `.risong` is the raw self-describing song record (the `.lsdsng`
// analog), and its assets are the shared `.rit` / `.chr` / `.rkit` formats, whose actions are the tracker package's
// because BlipToaster carries the very same surface. The callbacks reach the shell through the UI host's groups only.
import type { FileBrowserOpts, Host, SystemInfo } from "@retroplug/core/host";
import { SAV_PATTERNS, isSavPath, sanitizeName } from "@retroplug/core/path";
import { browseThen } from "@retroplug/ui/menu";
import {
  defineUi,
  type AssetMenuSpec,
  type SongMenuSpec,
  type TrackerUi,
} from "@retroplug/ui/contribution";
import { exportRitAsset, replaceRitAsset, type RitAssetOps } from "@retroplug/tracker/ritAssetOps";
import { overlayView } from "./overlayView";
import { risaDebugOverlay } from "./debugOverlay";
import { risaAssetCatalog } from "../risaAssetCatalog";
import { risaSongCatalog } from "../risaSongCatalog";
import { RisaRom } from "../rom";
import {
  addSongRecordToSav,
  canSaveWorkingToCatalog,
  replaceSongRecordInSav,
  saveWorkingToCatalog,
  songRecordBytes,
  workingSongRecord,
} from "../risaSongOps";

/** The host groups this cart's rows reach: narrowed to what they use, so a third-party pack copied
 *  from this one takes exactly these and nothing more. */
type RisaUiHost = Host<"files" | "systems" | "liveSongs" | "dialogs">;

/** The facet risa supplies to the shared `.rit` actions: the role kind its override list lives on, and its ROM view. */
const risaOps: RitAssetOps = {
  role: "risa-assets",
  openRom: (host: Host<"files">, romPath: string) => {
    const bytes = romPath ? host.files.readFile(romPath) : null;
    if (!bytes) return null;
    const rom = RisaRom.fromBytes(bytes);
    return rom.isRisa ? rom : null;
  },
};

const risaAssetSpec: AssetMenuSpec = {
  id: "risa",
  catalog: risaAssetCatalog,
  exportAsset: (host, sys, type, slot, label) => exportRitAsset(risaOps, host, sys, type, slot, label),
  replaceAsset: (host, sys, type, slot) => replaceRitAsset(risaOps, host, sys, type, slot),
};

// The throw->null wrapper for risa's catalog ops used by its file actions (LSDj's ops return null directly; risa's
// byte-ops throw on a bad index / no space).
function tryOp(fn: () => Uint8Array): Uint8Array | null {
  try {
    return fn();
  } catch {
    return null; // out of range / malformed -> leave the sav untouched
  }
}

// risa song file I/O: a `.risong` is the raw self-describing song record - the `.lsdsng` analog (song only, no kits).
// Export writes songRecordBytes; Replace/Add inject the record via the byte-level catalog ops.
function exportSong(host: RisaUiHost, sys: SystemInfo, index: number, name: string): void {
  const opts: FileBrowserOpts = {
    title: `Export song ${index}`,
    patterns: ["*.risong"],
    saving: true,
    defaultName: `${sanitizeName(name)}.risong`,
  };
  browseThen(host, opts, (path) => {
    const bytes = host.systems.readSram(sys.id);
    if (!bytes) return;
    const record = songRecordBytes(bytes, index);
    if (record) host.files.writeFileAtomic(path, record);
  });
}

function replaceSong(host: RisaUiHost, sys: SystemInfo, index: number): void {
  browseThen(host, { title: `Replace song ${index}`, patterns: ["*.risong"] }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    host.liveSongs.mutate(sys, (sav) => tryOp(() => replaceSongRecordInSav(sav, index, data)));
  });
}

function addSong(host: RisaUiHost, sys: SystemInfo): void {
  browseThen(host, { title: "Add Song", patterns: ["*.risong", ...SAV_PATTERNS] }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    if (isSavPath(path)) host.liveSongs.beginImport(sys, data); // a whole .sav/.srm -> the checkbox picker
    else host.liveSongs.mutate(sys, (sav) => tryOp(() => addSongRecordToSav(sav, data))); // a single .risong record
  });
}

// The synthetic working-song row's actions. Export writes the LIVE working song (banks 0-3, encoded as a
// record) to a `.risong`; Save to Catalog promotes it into a new saved slot + links it, so it stops being
// unsaved and moves into the numbered list.
function exportWorking(host: RisaUiHost, sys: SystemInfo, name: string): void {
  const opts: FileBrowserOpts = {
    title: "Export working song",
    patterns: ["*.risong"],
    saving: true,
    defaultName: `${sanitizeName(name)}.risong`,
  };
  browseThen(host, opts, (path) => {
    const bytes = host.systems.readSram(sys.id);
    if (!bytes) return;
    const record = workingSongRecord(bytes);
    if (record) host.files.writeFileAtomic(path, record);
  });
}

function saveWorking(host: RisaUiHost, sys: SystemInfo): void {
  host.liveSongs.mutate(sys, (sav) => tryOp(() => saveWorkingToCatalog(sav)));
}

// risa's working song carries its own name, so it never needs one asked for - `workingNeedsName` is simply
// absent (false) and the menu offers a plain "Save Working Song & Load".
const risaCommitWorking = (sav: Uint8Array): Uint8Array | null => tryOp(() => saveWorkingToCatalog(sav));

const risaSongSpec: SongMenuSpec = {
  id: "risa",
  catalog: risaSongCatalog,
  exportSong,
  replaceSong,
  addSong,
  exportWorking,
  saveWorkingToCatalog: saveWorking,
  commitWorking: risaCommitWorking,
  canCommitWorking: canSaveWorkingToCatalog,
};

/** The tracker's UI: its Songs and Assets specs. It builds no fragment of its own. */
export const RISA_TRACKER_UI: TrackerUi = {
  id: "risa",
  song: risaSongSpec,
  asset: risaAssetSpec,
};

export default defineUi({
  trackers: [RISA_TRACKER_UI],
  views: [overlayView],
  actions: [risaDebugOverlay],
});
