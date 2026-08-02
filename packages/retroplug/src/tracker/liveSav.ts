// Editing the LIVE battery of a running tracker cart. Songs (and the asset-free sav edits around them) are
// the BATTERY, not a ROM override, so every edit follows one shape: read the published SRAM snapshot, apply
// a BYTE-LEVEL transform (never the lossy decoded-song model), write the resolved `.sav`, and cold-boot the
// core from it - durable on disk AND reflected in the running cart, exactly like the cart's own FILE screen.
//
// Two callers share it: the Songs submenu (menuDefs) and the Recent list, whose per-song rows reopen a
// project with that song loaded.

import type { ControlPlaneBackend } from "../backend";
import type { SystemsStore } from "../systemsStore";
import type { RoleInstance } from "../systemRoles";
import { resolveSavPath } from "../savPaths";
import { resolveSongCatalog } from "./trackerIntegration"; // the leaf, not ./index - this module IS re-exported there

/** The per-system fields a live-sav edit needs - SystemView and SystemEntry both satisfy it. */
export interface LiveSavTarget {
  id: number;
  romPath: string;
  savPath: string; // the paired-save override ("" = the suffix sibling)
  savSuffix: number;
  roles: RoleInstance[];
}

type SavBackend = Pick<ControlPlaneBackend, "writeFileAtomic">;
type SavSystems = Pick<SystemsStore, "readSram" | "loadSram">;
type FocusedSystems = SavSystems & Pick<SystemsStore, "primary">;

/** Apply a byte-level transform to `sys`'s live battery and boot the cart from the result. A no-op (false)
 *  when there's no readable SRAM, the transform declines (null - malformed input / no space / bad index),
 *  or the `.sav` can't be written; the running system is left untouched in every one of those cases. */
export function mutateLiveSav(
  backend: SavBackend,
  systems: SavSystems,
  sys: LiveSavTarget,
  fn: (sav: Uint8Array) => Uint8Array | null,
): boolean {
  const bytes = systems.readSram(sys.id);
  if (!bytes) return false;
  const out = fn(bytes);
  if (!out) return false;
  const target = resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath);
  if (!backend.writeFileAtomic(target, out)) return false;
  return systems.loadSram(sys.id, target) !== null;
}

/** Load the saved song called `name` into `sys`'s working memory - the Songs menu's Load, addressed by NAME
 *  (what a recents row carries) instead of by slot. Already-loaded is a deliberate no-op: re-picking the song
 *  you are on shouldn't cold-boot the core or throw away working memory. False when the cart isn't a tracker,
 *  its battery can't be read, or it holds no song by that name (renamed / deleted since) - the caller carries
 *  on with whatever the cart already had loaded. Duplicate names resolve to the first match; they're
 *  indistinguishable in the list too. */
export function loadSongByName(backend: SavBackend, systems: SavSystems, sys: LiveSavTarget, name: string): boolean {
  const catalog = resolveSongCatalog(sys.roles);
  if (!catalog || !name) return false;
  const sram = systems.readSram(sys.id);
  if (!sram) return false;
  if (catalog.workingName(sram) === name) return false; // already the working song
  const match = catalog.list(sram).find((s) => s.name === name);
  if (!match) return false;
  return mutateLiveSav(backend, systems, sys, (sav) => catalog.load(sav, match.index));
}

/** `loadSongByName` against the PRIMARY system (focused, else first) - the instance the recents row's song
 *  was recorded from. What a recent song row runs once its project has loaded. */
export function loadSongInPrimary(backend: SavBackend, systems: FocusedSystems, name: string): boolean {
  const sys = systems.primary();
  return sys ? loadSongByName(backend, systems, sys, name) : false;
}
