// A console-agnostic "song catalog" — the shared shape LSDj and risa both expose over their battery/sav
// bytes, so the recent list can surface each project's song name and the Songs menu's uniform rows
// (Load / Delete / reorder) can be built once. Every op is BYTE-LEVEL (both consoles deliberately avoid
// the lossy decoded-song model for song management): it takes the live SRAM image and returns a NEW image
// (or null), never mutating the input. The file-dialog actions (Export / Replace / Add) live in the UI
// menu layer (per-console, since they own file formats + browseThen). Concrete catalogs:
// the carts' own catalogs, still the shell's (packages/retroplug/src/tracker/lsdjSongCatalog and
// .../risaSongCatalog) while neither cart has a package of its own; resolution: the shell's
// packages/retroplug/src/trackerRegistry (resolveSongCatalog).

export interface SongInfo {
  index: number;
  name: string;
}

/** The saved songs an UNLINKED working song plausibly came from: those sharing its name. A working song with
 *  no link names no slot, so if the user has been editing one that was loaded before the link was recorded
 *  (or imported from someone else's sav), the only save on offer is "append a new song" - which grows a
 *  second entry under the same name, the exact duplicate the menu is trying to avoid. These are the slots
 *  worth offering to overwrite instead.
 *
 *  Advisory, never automatic. Names are short and not unique, and overwriting a saved song has no undo, so
 *  the menu shows the candidates and the user picks; nothing here decides on their behalf. Built from
 *  `workingName` + `list`, so it needs no per-console support beyond what a SongCatalog already has. */
export function workingSongTargets(cat: SongCatalog, sav: Uint8Array): SongInfo[] {
  const name = cat.workingName(sav)?.trim().toUpperCase();
  if (!name) return [];
  return cat.list(sav).filter((s) => s.name.trim().toUpperCase() === name);
}

/** A working song that lives OUTSIDE the battery (smsggdj's work RAM), lifted out as data: the block plus
 *  the per-song metadata the cart keeps beside it, and the cart's own unsaved flag. Version-independent,
 *  so it outlives the core it was read from - a reset carries one across the reboot, and saving it
 *  into the battery needs nothing else. */
export interface WorkingSongImage {
  block: Uint8Array;
  /** The cart's raw name bytes, verbatim (padding included). */
  name: Uint8Array;
  echo: Uint8Array;
  /** The cart's "changed since the last save or load" flag. */
  edited: boolean;
  /** The cart's live OPTIONS (palette, sync, ...), which its own SAVE persists alongside the song. */
  settings?: Uint8Array;
  /** Whether the cart's transport was running when this was read - playback rewrites part of the song by
   *  itself (smsggdj's `T` command), and only then can a change be that rather than an edit or a load. */
  playing?: boolean;
  /** The saved slot this song was loaded from or last saved into, when the host knows it - what Save
   *  writes back over. Absent means a new song as far as the host can tell, and Save appends. */
  slot?: TrackedSlot;
}

/** Which saved slot a working song belongs to: its index AND a fingerprint of the song that slot held
 *  when it was tracked. The index alone is not enough - the cart's own FILES screen deletes and
 *  renumbers - so a slot only counts while it still holds that exact song. */
export interface TrackedSlot {
  index: number;
  fingerprint: number;
  /** How many songs the catalog held when this was tracked - which bounds where an in-cart delete above
   *  the slot can have shifted it to (see the catalog's save target). Absent: never relocate. */
  count?: number;
  /** A fingerprint of the slots ABOVE `index` when tracked. Unchanged means nothing above was deleted, so
   *  the song cannot have shifted at all - even if its own slot was cleared. Absent: never relocate. */
  above?: number;
}

/** What committing a working song into the battery did. `index` is the slot it landed in; `replaced`
 *  says whether that slot already held a song (the same-named one) or was appended. The refusals:
 *  `full` - no room for one more song; `format` - the battery is an older SMDJ format a save would
 *  flatten; `corrupt` - a saved song will not decode, and rebuilding around it would drop the rest. */
export type WorkingSongCommit =
  | { ok: true; sav: Uint8Array; index: number; replaced: boolean }
  | { ok: false; reason: "full" | "format" | "corrupt" };

export interface SongCatalog {
  /** The role kind that identifies this console on a system (the menu-gate marker, e.g. "lsdj-sync"/"risa"). */
  readonly markerRole: string;
  /** The saved songs in the catalog (index + name) — a cheap header-only walk; [] when there's no catalog.
   *  Works on ANY sav image (not just the loaded system's), so it also enumerates an import source. */
  list(sav: Uint8Array): SongInfo[];
  /** True when `bytes` is a valid, readable save of THIS console — the gate for importing songs from it (a
   *  wrong-console / corrupt buffer is rejected). Format-version tolerant: any parseable save passes. */
  isValidSav(bytes: Uint8Array): boolean;
  /** Copy the songs at the given SOURCE `indices` (into `source`'s `list()`) into `target`, byte-exact.
   *  Returns the new image, or null when nothing could be imported. Used by the Songs menu's sav importer. */
  importSongs(target: Uint8Array, source: Uint8Array, indices: number[]): Uint8Array | null;
  /** True when the working song can be read and written RIGHT NOW - the live core has finished booting
   *  far enough that what is in memory is the cart's to keep, not a boot sequence in progress.
   *
   *  Absent (the default) means always ready: LSDj and risa keep the working song in the battery, which
   *  is complete from the first frame. smsggdj keeps it in work RAM, and work RAM is readable from the
   *  moment the core exists but is NOT the cart's for its first seconds - `init` zero-fills it, the splash
   *  runs, `song_new` seeds the blank song, the cur-slot build's `boot_autoload` (unreleased) reloads the last slot. A
   *  name read in that window is nothing; a song written in it is silently wiped. Everything that reads the working
   *  song (recents, titles, the Songs menu, the load guards) and everything that writes it (a live load)
   *  waits on this. `ram` is the live work RAM; no RAM at all is "not ready", the safe polarity for a
   *  write. */
  workingSongReady?(ram?: Uint8Array): boolean;
  /** The currently-loaded (working) song's name, for recents / titles. null when none / unsaved.
   *
   *  `ram` is the system's live WORK RAM, and it is what makes this answerable for a console that keeps
   *  the working song there rather than in the battery: smsggdj reads the cart's own `song_name`, which
   *  is the true source (it is what the cart displays), where the battery alone knows nothing. Absent
   *  when the caller has no live system - an import source, or an offline `.sav` render - and a catalog
   *  must then fall back to whatever the image can tell it, exactly as before this parameter existed.
   *  LSDj and risa ignore it entirely; their working song IS in the image. */
  workingName(sav: Uint8Array, ram?: Uint8Array): string | null;
  /** DESCRIBE the live working song, for the Songs menu's synthetic row: its display name, and whether it is
   *  LINKED to a saved slot (which decides whether saving updates that slot or creates a new song). Null when
   *  there is no readable working song.
   *
   *  It does NOT decide whether the row is shown - `workingSongDirty` below does, and the menu calls this
   *  only once that has said yes. Splitting it that way is deliberate: the two questions were previously
   *  answered from different facts, and the row asked the wrong one. Gating on the LINK byte alone put a
   *  "[working] BLUMARBL" row next to an identical "[0] BLUMARBL" after every host-side load (content the
   *  same, link cleared), while the case that actually matters - linked, and edited for an hour - got no row
   *  at all. Content decides both, so the row now appears exactly when there is work to lose.
   *
   *  Optional: only consoles with a separate, readable working-song region implement it (risa). */
  workingSong?(sav: Uint8Array): { name: string; linked: boolean } | null;
  /** True when the working song holds content that exists in NO saved slot - exactly what `load` (and a
   *  cart reboot into another song) destroys. The gate for BOTH the Songs menu's load confirm and its
   *  synthetic working-song row. Two cases are dirty: an UNLINKED working song that no catalog slot claims,
   *  and one linked to a slot whose CONTENT it no longer matches (the common case - load a song, edit for an
   *  hour, load another).
   *  `ram` is the system's live WORK RAM, for consoles that keep the working song there instead of in the
   *  battery (smsggdj). It is absent when the caller has no system to read - an import source, a sav on
   *  disk - and a catalog that needs it must then answer false, since "I cannot tell" and "nothing to
   *  lose" have to collapse to the same silence.
   *
   *  Optional - a console that can't tell omits it, and the caller then never prompts (a prompt that fires
   *  when nothing would be lost is worse than none, since users learn to dismiss it). */
  workingSongDirty?(sav: Uint8Array, ram?: Uint8Array): boolean;
  /** True when the working song lives OUTSIDE the battery image, so the cold boot that every battery edit
   *  ends in destroys it - Delete and Move Up as surely as Load.
   *
   *  Absent (the default) means the working song is part of the image: LSDj keeps it at offset 0, risa in
   *  its own region, so rewriting the `.sav` around a song edit carries it through the reboot untouched
   *  and only an explicit `load` overwrites it. That is why the shared menu historically guarded Load
   *  alone. smsggdj breaks the assumption - its working song is work RAM at $C000 and appears nowhere in
   *  the file - so for it EVERY battery edit is destructive, and the menu has to say so. */
  readonly workingSongOutsideBattery?: boolean;
  /** Lift the working song out of live work RAM, for a console where it lives there. Null when there is
   *  no RAM, the cart has not booted (see `workingSongReady`), or the build's layout is unknown. */
  readWorkingSong?(ram: Uint8Array): WorkingSongImage | null;
  /** True when `song` holds work that is in no saved slot: the cart flagged it edited AND its content is
   *  not already in the battery. `workingSongDirty`'s question, asked of an image rather than of live
   *  RAM, so it can also be asked of a song a reset is still carrying across the reboot. */
  workingSongImageDirty?(sav: Uint8Array, song: WorkingSongImage): boolean;
  /** Save `song` INTO the battery the way the cart's own SAVE would - what "Save" in the unsaved-changes
   *  prompt does for a working song that lives outside the battery. Writes over `song.slot` (where it was
   *  loaded from or last saved) only while that slot still holds the song it held then and carries the
   *  song's name; anything else appends, because overwriting has no undo. Persists the OPTIONS too, as
   *  the cart's SAVE does. Pure: returns the new image, the caller writes it. */
  commitWorkingSong?(sav: Uint8Array, song: WorkingSongImage): WorkingSongCommit;
  /** The tracked slot for an UNEDITED working song - the cart just loaded or saved it ITSELF (its FILES
   *  screen, its PROJECT SAVE), which the host otherwise never hears about: the slot holding exactly its
   *  block (the currently tracked one first, when two hold the same song), or null when none does - a
   *  NEW song, or one loaded from outside the battery. */
  slotOfCleanSong?(sav: Uint8Array, song: WorkingSongImage): TrackedSlot | null;
  /** The TrackedSlot for saved slot `index` as the battery holds it now; null when it holds no song. */
  trackSlot?(sav: Uint8Array, index: number): TrackedSlot | null;
  /** A cheap identity for the working song's CONTENT, so a per-frame watcher can skip work while it is
   *  unchanged. */
  fingerprintOf?(song: WorkingSongImage): number;
  /** True when an UNEDITED working song differs from its tracked slot only in what the cart's playback
   *  rewrites by itself (not a load, not a NEW), so the tracking should hold. */
  isPlaybackDrift?(sav: Uint8Array, slot: TrackedSlot, song: WorkingSongImage, anyTransport?: boolean): boolean;
  /** Saved slot `index` as the working song it becomes when loaded (unedited) - what a host Load expects
   *  the cart to hold once its queued writes land. */
  slotImage?(sav: Uint8Array, index: number, ram?: Uint8Array): WorkingSongImage | null;
  /** True while the cart is in the middle of one of its own battery routines, so a host save must wait
   *  rather than rebuild the battery under it. */
  isBatteryBusy?(sav: Uint8Array): boolean;
  /** A cheap identity for the battery's song DIRECTORY, so the watcher re-checks when the cart saves,
   *  clears or renames a song even though its working song did not change. */
  directorySignal?(sav: Uint8Array): number;
  /** The work-RAM writes that tell a running cart its working song has just been saved - what the cart's
   *  own SAVE does to its unsaved flag. Without them a host-side save would leave the cart (and every
   *  dirty check that reads its flag) still believing the song unsaved. */
  workingSongSavedWrites?(): { offset: number; bytes: Uint8Array }[];
  /** Load a saved song into working memory (+ mark it active). New bytes, or null on an empty slot. */
  load(sav: Uint8Array, index: number): Uint8Array | null;
  /** Delete a saved song. New bytes, or null on an invalid index. */
  delete(sav: Uint8Array, index: number): Uint8Array | null;
  /** Commit the live working song INTO the saved song at `index`, overwriting it, and link the working song
   *  there. New bytes, or null when the slot / working song won't take it. Only meaningful for a console
   *  whose working song can be detached from the catalog; see `workingSongTargets` for who calls it. */
  saveWorkingToSlot?(sav: Uint8Array, index: number): Uint8Array | null;
  /** Reorder the saved songs: move the one at list position `from` to position `to` (positions index into
   *  `list()`, NOT slot numbers). New bytes, or null on an out-of-range / no-op move. Optional — only consoles
   *  whose saved songs can be reordered implement it (risa's positional records; LSDj by swapping slots). */
  reorder?(sav: Uint8Array, from: number, to: number): Uint8Array | null;
}
