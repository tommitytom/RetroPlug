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
  /** The currently-loaded (working) song's name, for recents / titles. null when none / unsaved. */
  workingName(sav: Uint8Array): string | null;
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
   *  Optional - a console that can't tell omits it, and the caller then never prompts (a prompt that fires
   *  when nothing would be lost is worse than none, since users learn to dismiss it). */
  workingSongDirty?(sav: Uint8Array): boolean;
  /** Load a saved song into working memory (+ mark it active). New bytes, or null on an empty slot. */
  load(sav: Uint8Array, index: number): Uint8Array | null;
  /** Delete a saved song. New bytes, or null on an invalid index. */
  delete(sav: Uint8Array, index: number): Uint8Array | null;
  /** Reorder the saved songs: move the one at list position `from` to position `to` (positions index into
   *  `list()`, NOT slot numbers). New bytes, or null on an out-of-range / no-op move. Optional — only consoles
   *  whose saved songs can be reordered implement it (risa's positional records; LSDj by swapping slots). */
  reorder?(sav: Uint8Array, from: number, to: number): Uint8Array | null;
}
