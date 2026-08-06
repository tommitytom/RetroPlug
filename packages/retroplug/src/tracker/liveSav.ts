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

type SavBackend = Pick<ControlPlaneBackend, "writeFileAtomic" | "writeFile">;
type SavSystems = Pick<SystemsStore, "readSram" | "loadSram">;
type FocusedSystems = SavSystems & Pick<SystemsStore, "primary">;

/** The rolling backup a destructive battery edit leaves behind - `<sav>.bak`, one per cart, overwritten
 *  each time. Every op here (Load / Replace / Delete / Add / reorder) overwrites the `.sav` in place with
 *  no undo, and Load in particular destroys the working song in RAM AND on disk, so this is the last line
 *  of defence when a confirm is dismissed or a path grows that forgets to raise one. */
export function backupSavPath(savPath: string): string {
  return savPath + ".bak";
}

/** Apply a byte-level transform to `sys`'s live battery and boot the cart from the result. A no-op (false)
 *  when there's no readable SRAM, the transform declines (null - malformed input / no space / bad index),
 *  or the `.sav` can't be written; the running system is left untouched in every one of those cases.
 *
 *  Fully SYNCHRONOUS, and that is load-bearing: the Continuous SRAM auto-save pump (useSramAutoSave) runs
 *  on the frame tick, so single-threaded JS means it can never observe the half-applied state between the
 *  write and the cold boot. `loadSram` also allocates a NEW system id, so the pump's cached hash for the
 *  old id is pruned and the next tick re-seeds from the file just written rather than clobbering it. */
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
  // Back up the PRE-MUTATION LIVE battery, not the on-disk file: that is the state actually being
  // destroyed, and under the OnProjectSave preference the on-disk copy can be much older.
  //
  // Genuinely best-effort, hence the catch: the RPC layer THROWS on a backend error (makeCall turns an
  // error reply into an exception), so an unwritable directory - a ROM on read-only media, say - would
  // otherwise propagate out of here and break the edit the user actually asked for. A safety net is not
  // allowed to be the thing that breaks the feature.
  try {
    backend.writeFile(backupSavPath(target), bytes);
  } catch {
    // no backup this time; the edit still goes ahead
  }
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

/** Would loading a DIFFERENT song into `sys` discard work that exists in no saved slot? The one place the
 *  UI asks "should I warn about this", so the Songs menu and the Recent list can't drift apart - both
 *  destroy the working song through the same `catalog.load`.
 *
 *  False whenever we cannot be certain: no tracker cart, unreadable battery, or a console whose catalog
 *  doesn't implement the predicate. Warning only on a positive signal is the point - see the
 *  `workingSongDirty` contract in ./songCatalog. */
export function songLoadWouldDiscard(systems: SavSystems, sys: LiveSavTarget): boolean {
  const catalog = resolveSongCatalog(sys.roles);
  if (!catalog?.workingSongDirty) return false;
  const sram = systems.readSram(sys.id);
  return sram ? catalog.workingSongDirty(sram) : false;
}

/** `songLoadWouldDiscard`, but skipped when `name` is ALREADY the working song - loading the song you are
 *  on is `loadSongByName`'s documented no-op, so it destroys nothing and must never prompt. The recents
 *  path's gate (the menu addresses songs by index and checks the index instead). */
export function songLoadByNameWouldDiscard(systems: SavSystems, sys: LiveSavTarget, name: string): boolean {
  const catalog = resolveSongCatalog(sys.roles);
  if (!catalog) return false;
  const sram = systems.readSram(sys.id);
  if (!sram || catalog.workingName(sram) === name) return false; // no-op load: nothing to lose
  if (!catalog.list(sram).some((s) => s.name === name)) return false; // nothing to load either
  return songLoadWouldDiscard(systems, sys);
}
