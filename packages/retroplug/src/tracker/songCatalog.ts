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
  /** The saved songs in the catalog (index + name) — a cheap header-only walk; [] when there's no catalog. */
  list(sav: Uint8Array): SongInfo[];
  /** The currently-loaded (working) song's name, for recents / titles. null when none / unsaved. */
  workingName(sav: Uint8Array): string | null;
  /** Load a saved song into working memory (+ mark it active). New bytes, or null on an empty slot. */
  load(sav: Uint8Array, index: number): Uint8Array | null;
  /** Delete a saved song. New bytes, or null on an invalid index. */
  delete(sav: Uint8Array, index: number): Uint8Array | null;
  /** Reorder the catalog (optional — only consoles with a positional catalog, e.g. risa). */
  reorder?(sav: Uint8Array, from: number, to: number): Uint8Array | null;
}
