// Add one or more `.lsdsng`/`.lsdprj` files to a live LSDj system in ONE readSram → edit → write → loadSram
// cycle (a single cold-boot for the whole batch). The one orchestrator behind both the Songs menu's Add and
// drag-and-drop of song files onto an instance. Byte-level throughout (never the lossy Song model — see
// lsdjSongOps): `.lsdsng` folds straight into a free slot; `.lsdprj` also imports its kits as `lsdj-assets`
// overrides (linking the file by path), accumulating across the batch. Both importers load their song into
// working memory, so the LAST file dropped is the one showing after the reboot.
import type { HostBackend } from "./backend";
import type { SystemsStore, SystemView } from "./systemsStore";
import { addLsdsngToSav } from "./lsdjSongOps";
import { planLsdprjImport } from "./lsdjLsdprjImport";
import { applyOverridesToRom, readOverrides, type LsdjAssetOverride } from "./lsdjAssetsRole";
import { resolveSavPath } from "./savPaths";

/** A `.lsdsng` / `.lsdprj` path (the file types this importer handles). */
export function isSongPath(path: string): boolean {
  return /\.(lsdsng|lsdprj)$/i.test(path);
}

/** Apply `paths` (each `.lsdsng`/`.lsdprj`) to LSDj system `sys` and reboot it once. Returns true if at
 *  least one song was imported (and the SAV written + core reloaded), false otherwise (no readable SRAM,
 *  none applied, or the write failed). */
export function importSongFiles(be: HostBackend, systems: SystemsStore, sys: SystemView, paths: string[]): boolean {
  let sav = systems.readSram(sys.id);
  if (!sav) return false;
  const baseRom = sys.romPath ? be.readFile(sys.romPath) : null;
  let overrides = readOverrides(sys.roles.find((r) => r.kind === "lsdj-assets")?.config);
  let overridesChanged = false;
  let applied = 0;

  for (const path of paths) {
    const data = isSongPath(path) ? be.readFile(path) : null;
    if (!data) continue;
    if (/\.lsdprj$/i.test(path)) {
      if (!baseRom) continue; // .lsdprj needs a base ROM to patch its kits into
      const plan = planLsdprjImport({ file: data, path, effectiveRom: applyOverridesToRom(baseRom, overrides, be), overrides, liveSram: sav });
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
  const target = resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath);
  if (!be.writeFileAtomic(target, sav)) return false;
  if (overridesChanged) systems.setRoleConfig(sys.id, "lsdj-assets", { overrides: overrides as LsdjAssetOverride[] });
  systems.loadSram(sys.id, target); // one rebuild: kit overrides → romBytes + boot the last imported song
  return true;
}
