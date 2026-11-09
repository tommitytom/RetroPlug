// The song-import half of a tracker integration (docs/modular-graph-phase-f4a-design.md section 4.4): the file types a
// cart can import, and how it folds them into a running system. A cart contributes it to its integration - LSDj's is
// the .lsdsng/.lsdprj importer - and the UI asks the integration its roles name, so neither the drop routing nor the
// menu names a cart or imports one.
//
// The host and target types are the shell's capabilities, narrowed to what an import uses, so an import is testable
// without the stores. The shell ADAPTS its backend and systems store to them (src/songImportHost.ts): a backend matches
// `caps` alone, and none of the rest, so every write, reload and role edit an import makes is a call on the host, and
// the shell keeps the backend, the serialization of edits and the reboot.
import type { ConstructCaps, RoleInstance } from "@retroplug/core/systemRoles";

/** The file I/O, the battery read, the role edit and the reboot of a system an import needs, which the shell adapts
 *  its backend and systems store to. */
export interface SongImportHost {
  /** The host's file I/O, as a construct hook's caps (core's ConstructCaps: readFile, fileExists, pngDecode),
   *  because an import that carries ROM assets patches the ROM through the same reads. The shell's backend
   *  satisfies this half alone. */
  readonly caps: ConstructCaps;
  /** Write one file whole; false when it did not land. */
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  /** The system's live battery, or null when its core has none to read. */
  readSram(id: number): Uint8Array | null;
  /** Reboot the system from `path`, the `.sav` the import just wrote. The one rebuild: the edits before it are one
   *  cold boot, not one each. */
  loadSram(id: number, path: string): unknown;
  /** Replace a role's config on the system: what an import that also carried ROM assets records beside the songs. */
  setRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
  /** The `.sav` a save of system `id` writes: its paired-save override when it has one, else the file beside its ROM.
   *  The host pairs them, because which file a system's save is belongs to the shell. Null when the system is gone, so
   *  an import writes nothing rather than an empty path. */
  savPathOf(id: number): string | null;
}

/** The system an import edits: the id the host's methods name, the ROM its assets patch, and the roles it reads. */
export interface SongImportTarget {
  readonly id: number;
  readonly romPath: string;
  readonly roles: readonly RoleInstance[];
}

/** How a cart imports song files onto a running system. */
export interface TrackerSongImport {
  /** The file types this cart imports, lowercase and dot included (".lsdsng", ".lsdprj"), for a drop's routing. */
  readonly extensions: readonly string[];
  /** Apply `paths` to `sys` and reboot it once. Returns true if at least one file was imported (and the save written
   *  and the core reloaded), false otherwise: no readable battery, none applied, or the write failed. */
  import(host: SongImportHost, sys: SongImportTarget, paths: readonly string[]): boolean;
}
