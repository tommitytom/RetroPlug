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
import { resolveSongCatalog, resolveTracker } from "./trackerIntegration"; // the leaf, not ./index - this module IS re-exported there

/** The per-system fields a live-sav edit needs - SystemView and SystemEntry both satisfy it. */
export interface LiveSavTarget {
  id: number;
  romPath: string;
  savPath: string; // the paired-save override ("" = the suffix sibling)
  savSuffix: number;
  roles: RoleInstance[];
}

// `readFile` is here for the LIVE load only: the write offsets come from the ROM build's symbol layout,
// so that path needs the ROM bytes. The cold-boot path never reads it.
type SavBackend = Pick<ControlPlaneBackend, "writeFileAtomic" | "writeFile"> & { readFile?(path: string): Uint8Array | null };
// `readRam` / `writeRam` are OPTIONAL on the seam: only a console whose working song lives outside the
// battery needs them, and keeping them optional means the mock stores and the recents path don't have to
// grow a region they never look at.
type SavSystems = Pick<SystemsStore, "readSram" | "loadSram"> & {
  readRam?(id: number): Uint8Array | null;
  writeRam?(id: number, offset: number, bytes: Uint8Array): boolean;
};
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

/** Load the saved song at `index` into the RUNNING cart's memory, without touching the `.sav` and without
 *  rebooting - the `liveLoad` path, for a console whose working song lives outside the battery.
 *
 *  Strictly less destructive than `mutateLiveSav`: nothing is written to disk, so there is no `.bak` to
 *  take and nothing to lose if it fails. It is also the only load that does NOT throw away the working
 *  song of every OTHER kind (the cold boot does), which is why the menu prefers it when it exists.
 *
 *  False when the cart has no `liveLoad`, its ROM or battery can't be read, the version has no layout, or
 *  the song won't decode. Applies writes in order and stops at the first refusal, reporting false - a
 *  half-written song is a wedged cart, but the alternative (pressing on past a rejected write) is worse,
 *  and the only way a write is refused is an out-of-bounds offset, which means the layout is wrong and
 *  the remaining writes would be too. */
export function loadSongLive(backend: SavBackend, systems: SavSystems, sys: LiveSavTarget, index: number): boolean {
  const tracker = resolveTracker(sys.roles);
  if (!tracker?.liveLoad || !sys.romPath || !backend.readFile || !systems.writeRam) return false;
  const rom = backend.readFile(sys.romPath);
  const sram = systems.readSram(sys.id);
  if (!rom || !sram) return false;
  // The CURRENT work RAM, so liveLoad can see whether the transport is running - the cart's own load
  // does different work in that case, and reproducing it needs to know.
  const writes = tracker.liveLoad(rom, sram, index, systems.readRam?.(sys.id) ?? undefined);
  if (!writes?.length) return false;
  for (const w of writes) {
    if (!systems.writeRam(sys.id, w.offset, w.bytes)) return false;
  }
  return true;
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
  if (catalog.workingName(sram, systems.readRam?.(sys.id) ?? undefined) === name) return false; // already working
  const match = catalog.list(sram).find((s) => s.name === name);
  if (!match) return false;
  // A cart that can be loaded LIVE is, in preference to the cold boot: it is faster, it leaves the `.sav`
  // untouched, and for the one console that has it the reboot is what destroys the working song.
  if (resolveTracker(sys.roles)?.liveLoad && systems.writeRam) return loadSongLive(backend, systems, sys, match.index);
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
  // Work RAM only matters to a console that keeps its working song there, and reading it is a snapshot
  // copy, so don't pay for it otherwise.
  const ram = catalog.workingSongOutsideBattery ? (systems.readRam?.(sys.id) ?? undefined) : undefined;
  return sram ? catalog.workingSongDirty(sram, ram) : false;
}

/** Would ANY battery edit - Delete, Replace, Move, Add, Import, not just Load - discard unsaved work?
 *
 *  `mutateLiveSav` always ends in a cold boot, and for a console whose working song lives outside the
 *  image (see `workingSongOutsideBattery`) that boot destroys it no matter which op caused it. The shared
 *  Songs menu was built when only LSDj and risa existed, and for both of those the reboot is harmless
 *  because the working song is part of the bytes being rewritten - so Load was the only guarded row and
 *  Delete/Move/Add got no confirm at all. On smsggdj that silence is a straight path to losing an hour's
 *  work by reordering a list.
 *
 *  False for the other two consoles by construction, so this adds no friction where the reboot is free. */
export function savEditWouldDiscard(systems: SavSystems, sys: LiveSavTarget): boolean {
  const catalog = resolveSongCatalog(sys.roles);
  return catalog?.workingSongOutsideBattery ? songLoadWouldDiscard(systems, sys) : false;
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
