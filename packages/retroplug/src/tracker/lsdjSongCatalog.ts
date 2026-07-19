// The LSDj implementation of SongCatalog — thin wrappers over the existing pure ops (no logic here).
import type { SongCatalog } from "./songCatalog";
import { listProjects, workingSongName } from "../lsdj/codec/sav";
import { loadSongToWorking, deleteSongInSav } from "../lsdjSongOps";

export const lsdjSongCatalog: SongCatalog = {
  markerRole: "lsdj-sync", // LSDj overloads lsdj-sync as its menu-gate marker
  list: (sav) => listProjects(sav).map((p) => ({ index: p.slot, name: p.name })),
  workingName: (sav) => workingSongName(sav),
  load: (sav, index) => loadSongToWorking(sav, index),
  delete: (sav, index) => deleteSongInSav(sav, index),
  // no reorder — LSDj's song slots are fixed-index (no positional catalog)
};
