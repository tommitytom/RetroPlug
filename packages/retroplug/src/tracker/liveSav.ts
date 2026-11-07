// Editing the LIVE battery of a running tracker cart. Songs (and the asset-free sav edits around them) are
// the BATTERY, not a ROM override, so every edit follows one shape: read the published SRAM snapshot, apply
// a BYTE-LEVEL transform (never the lossy decoded-song model), write the resolved `.sav`, and cold-boot the
// core from it - durable on disk AND reflected in the running cart, exactly like the cart's own FILE screen.
//
// Two callers share it: the Songs submenu (menuDefs) and the Recent list, whose per-song rows reopen a
// project with that song loaded.
//
// It is the shell's, not @retroplug/tracker's, and it is not a split waiting to happen: every entry point here is
// orchestration over a RUNNING system - it reads the published SRAM through SystemsStore, resolves the `.sav` the edit
// writes (savPaths), cold-boots or writes work RAM, and polls for a conditional battery write landing - and the byte
// transforms it schedules are already the tracker contract's (SongCatalog.load, commitWorkingSong, slotImage), which
// are what moved. What it takes as arguments (readSram/loadSram/writeSram/readRam/writeRam/workingSongImage/
// expectSong, ControlPlaneBackend.writeFile/writeFileAtomic, resolveTracker/resolveSongCatalog over the generated
// registry) are the shell's types and the shell's registry.

import type { ControlPlaneBackend } from "../backend";
import type { SystemsStore } from "../systemsStore";
import type { RoleInstance } from "@retroplug/core/systemRoles";
import { resolveSavPath } from "../savPaths";
import { resolveSongCatalog, resolveTracker } from "../trackerRegistry"; // the shell registry: the list is generated
import type { WorkingSongImage } from "@retroplug/tracker/songCatalog";

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
  // The working song as an image, including one a reset is still carrying across the reboot (see
  // SystemsStore.workingSongImage). Optional for the same reason as readRam.
  workingSongImage?(id: number): WorkingSongImage | null;
  // The song host writes are about to put into work RAM - see SystemsStore.expectSong.
  expectSong?(id: number, song: WorkingSongImage, ramNow?: Uint8Array): void;
  // Where the working song now lives in the catalog, once a load or save put it there.
  noteSongSlot?(id: number, slot: number): void;
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
 *  half-written song is a wedged cart, but the alternative (pressing on past a rejected write) is worse.
 *  A write is refused for an out-of-bounds offset, which means the layout is wrong and the remaining writes
 *  would be too, or for a full command ring (the audio thread not draining), which the remaining writes
 *  would meet as well. */
export function loadSongLive(backend: SavBackend, systems: SavSystems, sys: LiveSavTarget, index: number): boolean {
  const tracker = resolveTracker(sys.roles);
  if (!tracker?.liveLoad || !sys.romPath || !backend.readFile || !systems.writeRam) return false;
  // A write into a cart that is still booting is a write the boot then erases - `song_new` re-seeds the
  // block a moment later - and the caller would believe it loaded a song. Refusing here is what makes
  // "false" mean "nothing happened" rather than "something happened and was undone"; the caller that
  // wants the song once the cart is up parks the request and asks again (ProjectStore.settleSong).
  if (!workingSongReady(systems, sys)) return false;
  const rom = backend.readFile(sys.romPath);
  const sram = systems.readSram(sys.id);
  if (!rom || !sram) return false;
  // The CURRENT work RAM, so liveLoad can see whether the transport is running - the cart's own load
  // does different work in that case, and reproducing it needs to know.
  const ramNow = systems.readRam?.(sys.id) ?? undefined;
  const writes = tracker.liveLoad(rom, sram, index, ramNow);
  if (!writes?.length) return false;
  // Until the queued writes land, the loaded song is the working song (see SystemsStore.expectSong).
  const catalog = resolveSongCatalog(sys.roles);
  const loaded = catalog?.slotImage?.(sram, index, ramNow);
  if (loaded) systems.expectSong?.(sys.id, { ...loaded, slot: catalog?.trackSlot?.(sram, index) ?? undefined }, ramNow);
  for (const w of writes) {
    if (!systems.writeRam(sys.id, w.offset, w.bytes)) return false;
  }
  // The slot is NOT tracked here: the writes are queued, and a reset or an export before they land would
  // carry the OLD song tracked to the NEW slot - which Save then overwrote. The watcher tracks the loaded
  // song from the cart itself once it is there (it lands unedited, matching this slot).
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

/** Can `sys`'s working song be read and written right now? True for every console but the one whose
 *  working song lives in work RAM (smsggdj), where the core's first seconds belong to the boot sequence
 *  and not to the cart - see `SongCatalog.workingSongReady`. Everything that would read the working
 *  song to make a decision, or write it, asks this first; a caller that gets false waits for the next
 *  frame rather than guessing at a timer. A cart with no catalog is trivially ready (nothing to wait for). */
export function workingSongReady(systems: SavSystems, sys: LiveSavTarget): boolean {
  const catalog = resolveSongCatalog(sys.roles);
  if (!catalog?.workingSongReady) return true;
  return catalog.workingSongReady(systems.readRam?.(sys.id) ?? undefined);
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
  // Asked of the IMAGE when the console has one, so a song a reset is still carrying - which the booting
  // cart does not hold yet - counts exactly as it will once it lands.
  if (catalog?.workingSongImageDirty && systems.workingSongImage) {
    const sram = systems.readSram(sys.id);
    const song = systems.workingSongImage(sys.id);
    return sram && song ? catalog.workingSongImageDirty(sram, song) : false;
  }
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
  // What the cart HOLDS, from live RAM where the console keeps it there - not the battery's own record:
  // smsggdj's cur_slot names the last song SAVED, which every host save sets, so a request for that song
  // read as a no-op over an unrelated unsaved song and loaded over it with no prompt.
  const ram = catalog.workingSongOutsideBattery ? (systems.readRam?.(sys.id) ?? undefined) : undefined;
  if (!sram || catalog.workingName(sram, ram) === name) return false; // no-op load: nothing to lose
  if (!catalog.list(sram).some((s) => s.name === name)) return false; // nothing to load either
  return songLoadWouldDiscard(systems, sys);
}

/** What saving one system's working song into its battery did. "clean" - nothing to save (the console
 *  keeps its song in the battery, or the song is already saved); "saved" - it is in the live battery,
 *  which the caller's battery flush then writes to the `.sav`; otherwise why it could not be. */
export type WorkingSongSave =
  | { kind: "clean" }
  | { kind: "saved"; index: number; replaced: boolean }
  | { kind: "refused"; reason: "full" | "format" | "corrupt" | "unwritable" | "busy" };

/** How long to wait for a battery write to show up in `readSram`. It lands on the next audio block
 *  (milliseconds) or inline when the audio thread is stopped; this bounds a wedged one. */
const SRAM_LAND_MS = 2000;
/** How many times to rebuild against a battery the cart wrote mid-save before giving up. Each conflict
 *  needs the cart to write its battery inside one save's few milliseconds; three in a row is a cart doing
 *  something other than sitting still, and the save says so rather than racing it. */
const SAVE_ATTEMPTS = 3;

/** Save `sys`'s working song INTO its battery - for a console whose working song lives outside it
 *  (smsggdj), the host-side twin of the cart's own SAVE, and what "Save" in the unsaved-changes prompt
 *  does before it flushes batteries to disk.
 *
 *  The write goes into the LIVE battery (`writeSram`), not just the file, and that is the whole design:
 *  the auto-save mirror and the teardown flush both write `readSram` to disk, so a file-only save would be
 *  overwritten by the stale live battery the moment the core was torn down - i.e. by the very close this
 *  save was for.
 *
 *  The image is rebuilt from a battery SNAPSHOT (republished every block on Mesen, but still a moment old
 *  by the time the write is applied) and replaces the whole battery - so a write made blind would revert
 *  anything the cart wrote in between (its own SAVE, LOAD, a FILES delete). So it is conditional on the battery still being that snapshot; if the cart moved it,
 *  native refuses and republishes, and this rebuilds from the fresh one. Resolves once `readSram` shows
 *  the new image, so the caller's flush writes it. The live battery is backed up to `<sav>.bak` first. */
export async function saveWorkingSong(
  backend: SavBackend,
  systems: SavSystems & {
    writeSram(id: number, offset: number, bytes: Uint8Array, expect?: Uint8Array): boolean;
    markCarriedSongSaved?(id: number): void;
  },
  sys: LiveSavTarget,
): Promise<WorkingSongSave> {
  const catalog = resolveSongCatalog(sys.roles);
  if (!catalog?.commitWorkingSong || !catalog.workingSongImageDirty || !systems.workingSongImage) return { kind: "clean" };
  // After the write lands: what the cart's own SAVE leaves behind. Its unsaved flag clears, or the cart
  // keeps saying UNSAVED and every dirty check that reads that flag keeps prompting; a song still being
  // carried across a reboot has no cart to tell yet, so the carry is told. And the slot is tracked.
  const finish = (index: number): void => {
    for (const w of catalog.workingSongSavedWrites?.() ?? []) systems.writeRam?.(sys.id, w.offset, w.bytes);
    systems.markCarriedSongSaved?.(sys.id);
    systems.noteSongSlot?.(sys.id, index);
  };
  let landedAt = -1; // the slot an attempt's write went to, in case a later look finds it already there
  for (let attempt = 0; attempt < SAVE_ATTEMPTS; attempt++) {
    // The cart mid-way through its own battery routine: wait it out (it takes a few blocks) rather than
    // rebuild the battery under it.
    if (catalog.isBatteryBusy && !(await settledWhen(() => {
      const now = systems.readSram(sys.id);
      return !!now && !catalog.isBatteryBusy!(now);
    }))) return { kind: "refused", reason: "busy" };
    // ...and for the working song to hold still: a cart mid-way through its own LOAD unpacks the new song
    // into work RAM over several blocks, and a save read in the middle stored half a song over the slot.
    // Between a user's keypresses it is stable, so this costs one short look.
    if (!(await steady(() => systems.workingSongImage?.(sys.id) ?? null))) return { kind: "refused", reason: "busy" };
    const sram = systems.readSram(sys.id);
    const song = systems.workingSongImage(sys.id);
    if (!sram || !song) return { kind: "clean" };
    if (!catalog.workingSongImageDirty(sram, song)) {
      // Clean on a RETRY is our own write: it landed, and then the cart wrote its battery too before the
      // poll looked, which read as a conflict. The song is saved; do what a landed save does.
      const at = attempt > 0 ? catalog.slotOfCleanSong?.(sram, { ...song, edited: false }) : null;
      if (at) {
        finish(at.index);
        return { kind: "saved", index: at.index, replaced: landedAt === at.index };
      }
      return { kind: "clean" };
    }
    const r = catalog.commitWorkingSong(sram, song);
    if (!r.ok) return { kind: "refused", reason: r.reason };

    if (attempt === 0) {
      try {
        backend.writeFile(backupSavPath(resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath)), sram);
      } catch {
        // best-effort, as in mutateLiveSav: a safety net may not break the save it guards
      }
    }
    if (!systems.writeSram(sys.id, 0, r.sav, sram)) return { kind: "refused", reason: "unwritable" };
    const outcome = await settled(() => systems.readSram(sys.id), r.sav, sram);
    if (outcome === "conflict") {
      landedAt = r.replaced ? r.index : -1;
      continue; // the cart wrote its battery: rebuild from what it holds now
    }
    if (outcome === "timeout") return { kind: "refused", reason: "unwritable" };
    finish(r.index);
    return { kind: "saved", index: r.index, replaced: r.replaced };
  }
  return { kind: "refused", reason: "busy" };
}

/** How long the working song must stay unchanged before a save reads it - a few audio blocks. */
const STEADY_MS = 40;

/** Wait until `read()` returns the same song (block, name, echo, flag) across STEADY_MS, within the
 *  SRAM_LAND_MS budget. With no pumped timer it cannot wait, and takes the first look as steady - the
 *  headless test host, where no audio runs between looks anyway. */
async function steady(read: () => WorkingSongImage | null): Promise<boolean> {
  const timer = (globalThis as { setTimeout?: (fn: () => void, ms: number) => unknown }).setTimeout;
  const pumped = (globalThis as { __rp_timersPumped?: boolean }).__rp_timersPumped !== false;
  if (!timer || !pumped) return true;
  const same = (a: WorkingSongImage | null, b: WorkingSongImage | null): boolean =>
    a === b ||
    (!!a && !!b && a.edited === b.edited && [a.block, a.name, a.echo].every((x, i) => {
      const y = [b.block, b.name, b.echo][i];
      return x.length === y.length && x.every((v, j) => v === y[j]);
    }));
  const deadline = Date.now() + SRAM_LAND_MS;
  let prev = read();
  for (;;) {
    await new Promise<void>((resolve) => timer(resolve, STEADY_MS));
    const now = read();
    if (same(prev, now)) return true;
    if (Date.now() > deadline) return false;
    prev = now;
  }
}

/** Wait until `ready()` holds, on the same timer and budget as `settled`. False if it never does. */
async function settledWhen(ready: () => boolean): Promise<boolean> {
  const timer = (globalThis as { setTimeout?: (fn: () => void, ms: number) => unknown }).setTimeout;
  const pumped = (globalThis as { __rp_timersPumped?: boolean }).__rp_timersPumped !== false;
  const deadline = Date.now() + SRAM_LAND_MS;
  while (!ready()) {
    if (!timer || !pumped || Date.now() > deadline) return false;
    await new Promise<void>((resolve) => timer(resolve, 10));
  }
  return true;
}

/** Wait for a conditional battery write to resolve: "landed" once `read()` shows `want`, "conflict" once it
 *  shows anything else but `base` (native refused the write and republished the battery the cart has
 *  since written), "timeout" after SRAM_LAND_MS. Polls on the host's timer, which the app's UI runtime
 *  services every frame (LvglJsEngine::tick pumps the loop). The first look is synchronous and is usually
 *  enough: with the audio thread stopped the write is applied inline, and native republishes the battery
 *  as it applies. The headless native test host does NOT pump timers while a test awaits, so a test that
 *  reaches the poll would hang - it is skipped there and reported as a timeout instead. */
async function settled(read: () => Uint8Array | null, want: Uint8Array, base: Uint8Array): Promise<"landed" | "conflict" | "timeout"> {
  const same = (a: Uint8Array | null, b: Uint8Array): boolean => !!a && a.length === b.length && a.every((x, i) => x === b[i]);
  const timer = (globalThis as { setTimeout?: (fn: () => void, ms: number) => unknown }).setTimeout;
  const pumped = (globalThis as { __rp_timersPumped?: boolean }).__rp_timersPumped !== false;
  const deadline = Date.now() + SRAM_LAND_MS;
  for (;;) {
    const now = read();
    if (same(now, want)) return "landed";
    if (now && !same(now, base)) return "conflict";
    if (!timer || !pumped || Date.now() > deadline) return "timeout";
    await new Promise<void>((resolve) => timer(resolve, 10));
  }
}
