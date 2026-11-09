// The shell's half of a tracker integration's song import (docs/modular-graph-phase-f4a-design.md section 4.4): the
// backend's files, the systems store's battery read and reload, and the `.sav` a save of the system writes, adapted to
// tracker's SongImportHost so a cart's import needs no shell import. Built where the import is called, because the
// shell owns the stores and the package must not.
import type { SongImportHost } from "@retroplug/tracker/trackerSongImport";
import type { HostBackend } from "./backend";
import type { SystemsStore } from "./systemsStore";
import { resolveSavPath } from "./savPaths";

export function songImportHost(backend: HostBackend, systems: SystemsStore): SongImportHost {
  return {
    caps: backend,
    writeFileAtomic: (path, bytes) => backend.writeFileAtomic(path, bytes),
    readSram: (id) => systems.readSram(id),
    loadSram: (id, path) => systems.loadSram(id, path),
    setRoleConfig: (id, kind, config) => systems.setRoleConfig(id, kind, config),
    savPathOf: (id) => {
      const sys = systems.view().find((s) => s.id === id);
      return sys ? resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath) : "";
    },
  };
}
