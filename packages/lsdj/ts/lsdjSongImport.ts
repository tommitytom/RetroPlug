// LSDj's song-file import (docs/modular-graph-phase-f4a-design.md section 4.4): adds one or more `.lsdsng`/`.lsdprj`
// files to a live LSDj system in ONE readSram → edit → write → loadSram cycle (a single cold-boot for the whole batch).
// The one orchestrator behind both the Songs menu's Add and drag-and-drop of song files onto an instance. Byte-level
// throughout (never the lossy Song model - see lsdjSongOps): `.lsdsng` folds straight into a free slot; `.lsdprj` also
// imports its kits as `lsdj-assets` overrides (linking the file by path), accumulating across the batch. Both importers
// load their song into working memory, so the LAST file dropped is the one showing after the reboot.
//
// The host is the shell's ADAPTER over its backend and systems store (packages/retroplug/src/songImportHost.ts), and the
// target is the system view's id, ROM path and roles: they are narrowed here rather than imported, because a package
// imports no shell.
import type { SongImportHost, SongImportTarget, TrackerSongImport } from "@retroplug/tracker/trackerSongImport";
import { addLsdsngToSav } from "./lsdjSongOps";
import { planLsdprjImport } from "./lsdjLsdprjImport";
import { applyOverridesToRom, readOverrides, type LsdjAssetOverride } from "./lsdjAssetsRole";

/** The file types this importer handles. */
const extensions = [".lsdsng", ".lsdprj"] as const;

const isSongPath = (path: string): boolean => extensions.some((ext) => path.toLowerCase().endsWith(ext));

/** Apply `paths` (each `.lsdsng`/`.lsdprj`) to LSDj system `sys` and reboot it once. Returns true if at least one
 *  song was imported (and the SAV written + core reloaded), false otherwise (no readable SRAM, none applied, or the
 *  write failed). */
function importSongs(host: SongImportHost, sys: SongImportTarget, paths: readonly string[]): boolean {
  let sav = host.readSram(sys.id);
  if (!sav) return false;
  const baseRom = sys.romPath ? host.caps.readFile(sys.romPath) : null;
  let overrides = readOverrides(sys.roles.find((r) => r.kind === "lsdj-assets")?.config);
  let overridesChanged = false;
  let applied = 0;

  for (const path of paths) {
    const data = isSongPath(path) ? host.caps.readFile(path) : null;
    if (!data) continue;
    if (/\.lsdprj$/i.test(path)) {
      if (!baseRom) continue; // .lsdprj needs a base ROM to patch its kits into
      const plan = planLsdprjImport({
        file: data,
        path,
        effectiveRom: applyOverridesToRom(baseRom, overrides, host.caps),
        overrides,
        liveSram: sav,
      });
      if (!plan) continue;
      sav = plan.savBytes;
      if (plan.addedKits > 0) {
        overrides = plan.overrides; // accumulate; the next .lsdprj sees these kits (dedupe + free-slot)
        overridesChanged = true;
      }
      applied++;
    } else {
      const next = addLsdsngToSav(sav, data);
      if (!next) continue;
      sav = next;
      applied++;
    }
  }

  if (applied === 0) return false;
  const target = host.savPathOf(sys.id);
  if (!target || !host.writeFileAtomic(target, sav)) return false;
  if (overridesChanged) host.setRoleConfig(sys.id, "lsdj-assets", { overrides: overrides as LsdjAssetOverride[] });
  host.loadSram(sys.id, target); // one rebuild: kit overrides → romBytes + boot the last imported song
  return true;
}

export const LSDJ_SONG_IMPORT: TrackerSongImport = { extensions, import: importSongs };
