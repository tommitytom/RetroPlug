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
  /** The live WORKING song when it is UNSAVED — not represented by any listed catalog slot — so the Songs
   *  menu can surface it as a synthetic row (some carts ship the song only in working memory). null when the
   *  working song is already saved, absent, or the console has no separate working-song region. Optional —
   *  only consoles whose working song can exist outside the saved list implement it (risa; LSDj does not,
   *  since its working song is a copy of a saved slot addressed by activeProjectIndex). */
  workingSong?(sav: Uint8Array): { name: string } | null;
  /** Load a saved song into working memory (+ mark it active). New bytes, or null on an empty slot. */
  load(sav: Uint8Array, index: number): Uint8Array | null;
  /** Delete a saved song. New bytes, or null on an invalid index. */
  delete(sav: Uint8Array, index: number): Uint8Array | null;
  /** Reorder the saved songs: move the one at list position `from` to position `to` (positions index into
   *  `list()`, NOT slot numbers). New bytes, or null on an out-of-range / no-op move. Optional — only consoles
   *  whose saved songs can be reordered implement it (risa's positional records; LSDj by swapping slots). */
  reorder?(sav: Uint8Array, from: number, to: number): Uint8Array | null;
}
