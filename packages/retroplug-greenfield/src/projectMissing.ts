// Detect + repair project files that reference on-disk assets (a system's ROM, a
// paired .sav) which no longer exist — a moved file makes a load incomplete, so the
// UI locates them before applying. Port of the thin subset of missingFiles.ts (rom +
// sram); kit samples arrive with the kits domain, so the blobKeys arg is already
// here for that reuse. Addressed by config INDEX (systems don't exist yet at load).

import type { ProjectConfig } from "./projectConfig";
import { basename } from "./pathUtil";

export interface MissingFile {
  systemIndex: number;
  itemKind: "rom" | "sram";
  path: string;
}

type FileExists = (path: string) => boolean;

const romKey = (i: number) => `systems/${i}/rom`;
const sramKey = (i: number) => `systems/${i}/sram`;
const stateKey = (i: number) => `systems/${i}/state`;

/** Every referenced-but-absent asset, in config order. A ROM is present if it's
 *  embedded (marker or `systems/{i}/rom` blob) or its path exists; a paired save is
 *  present unless it names an explicit `savPath` with no sram/state blob and no file
 *  (an empty savPath is the derived suffix sibling, allowed to be absent). */
export function scanMissingFiles(
  cfg: ProjectConfig,
  blobKeys: ReadonlySet<string>,
  exists: FileExists,
): MissingFile[] {
  const out: MissingFile[] = [];
  cfg.systems.forEach((sys, i) => {
    const romOk = !!sys.embeddedRom || blobKeys.has(romKey(i)) || (!!sys.romPath && exists(sys.romPath));
    if (!romOk) out.push({ systemIndex: i, itemKind: "rom", path: sys.romPath ?? "" });

    const savOk = !sys.savPath || blobKeys.has(sramKey(i)) || blobKeys.has(stateKey(i)) || exists(sys.savPath);
    if (!savOk) out.push({ systemIndex: i, itemKind: "sram", path: sys.savPath });
  });
  return out;
}

/** Point a missing item at `newPath` (mutates the config). Returns false when the
 *  index doesn't resolve. */
export function relinkInConfig(cfg: ProjectConfig, item: MissingFile, newPath: string): boolean {
  const sys = cfg.systems[item.systemIndex];
  if (!sys) return false;
  if (item.itemKind === "rom") sys.romPath = newPath;
  else sys.savPath = newPath;
  return true;
}

/** After locating one file, look in its folder for the other still-missing files by
 *  basename and relink any matches — one pick fixes a whole moved folder. Returns the
 *  number of additional items resolved. */
export function autoFindSiblings(
  cfg: ProjectConfig,
  newDir: string,
  blobKeys: ReadonlySet<string>,
  exists: FileExists,
): number {
  let resolved = 0;
  for (const item of scanMissingFiles(cfg, blobKeys, exists)) {
    const candidate = (newDir ? newDir + "/" : "") + basename(item.path);
    if (exists(candidate) && relinkInConfig(cfg, item, candidate)) resolved++;
  }
  return resolved;
}
