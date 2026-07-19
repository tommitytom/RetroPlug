// The tracker-integration layer: a console-agnostic view of a music-tracker cart (LSDj, risa, …). Pass 1
// covers the SONG catalog (shared song menu + recent-song names); assets / sync / runtime stay per-console.
// New consoles add their SongCatalog to SONG_CATALOGS — the one extension point (like the CLI `tools` array).
import type { RoleInstance } from "../systemRoles";
import type { SongCatalog } from "./songCatalog";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { risaSongCatalog } from "./risaSongCatalog";

export type { SongCatalog, SongInfo } from "./songCatalog";

/** Every registered song catalog. The one place a new tracker console is added. */
export const SONG_CATALOGS: SongCatalog[] = [lsdjSongCatalog, risaSongCatalog];

/** The song catalog for a system, resolved from its attached roles (the first role whose kind is a
 *  catalog's markerRole). undefined for a non-tracker system. */
export function resolveSongCatalog(roles: RoleInstance[]): SongCatalog | undefined {
  return SONG_CATALOGS.find((cat) => roles.some((r) => r.kind === cat.markerRole));
}

export { lsdjSongCatalog } from "./lsdjSongCatalog";
export { risaSongCatalog } from "./risaSongCatalog";
