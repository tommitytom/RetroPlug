// The song-import half of a tracker integration (docs/modular-graph-phase-f4a-design.md section 4.4): the file types a
// cart can import, and how it folds them into a running system. A cart contributes it to its integration - LSDj's is
// the .lsdsng/.lsdprj importer - and the UI asks the integration its roles name, so neither the drop routing nor the
// menu names a cart or imports one.
//
// The host is core's contract, narrowed to the groups an import uses, so an import is testable without the stores: the
// file I/O it reads and writes (files), the PNG codec the asset overrides it applies may need (png), and the system's
// battery, role edits, `.sav` and reboot (systems). Every write, reload and role edit an import makes is a call on that
// host, so the shell keeps the backend, the serialization of edits and the reboot.
import type { Host, SystemInfo } from "@retroplug/core/host";

/** How a cart imports song files onto a running system. */
export interface TrackerSongImport {
  /** The file types this cart imports, lowercase and dot included (".lsdsng", ".lsdprj"), for a drop's routing. */
  readonly extensions: readonly string[];
  /** Apply `paths` to `sys` and reboot it once. The shell hands it only files whose extension `extensions` declares
   *  (the drop routing filters a mixed drop), so an import need not re-check the type. Returns true if at least one
   *  file was imported (and the save written and the core reloaded), false otherwise: no readable battery, none
   *  applied, or the write failed. */
  import(host: Host<"files" | "png" | "systems">, sys: SystemInfo, paths: readonly string[]): boolean;
}
