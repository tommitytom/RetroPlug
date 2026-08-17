// A console-agnostic "song catalog" — the shared shape LSDj and risa both expose over their battery/sav
// bytes, so the recent list can surface each project's song name and the Songs menu's uniform rows
// (Load / Delete / reorder) can be built once. Every op is BYTE-LEVEL (both consoles deliberately avoid
// the lossy decoded-song model for song management): it takes the live SRAM image and returns a NEW image
// (or null), never mutating the input. The file-dialog actions (Export / Replace / Add) live in the UI
// menu layer (per-console, since they own file formats + browseThen). Concrete catalogs:
// ./lsdjSongCatalog, ./risaSongCatalog; resolution: ./index (resolveSongCatalog).

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
