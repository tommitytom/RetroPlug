// The LSDj implementation of SongCatalog — thin wrappers over the existing pure ops (no logic here).
import type { SongCatalog } from "./songCatalog";
import { listProjects, workingSongName } from "../lsdj/codec/sav";
import { loadSongToWorking, deleteSongInSav, moveSongInSav } from "../lsdjSongOps";

export const lsdjSongCatalog: SongCatalog = {
  markerRole: "lsdj-sync", // LSDj overloads lsdj-sync as its menu-gate marker
  list: (sav) => listProjects(sav).map((p) => ({ index: p.slot, name: p.name })),
  workingName: (sav) => workingSongName(sav),
  load: (sav, index) => loadSongToWorking(sav, index),
  delete: (sav, index) => deleteSongInSav(sav, index),
  // reorder swaps two saved songs' slot contents (LSDj addresses songs by a fixed slot number, so a
  // reorder renumbers — `from`/`to` are list positions, not slots; see moveSongInSav)
  reorder: (sav, from, to) => moveSongInSav(sav, from, to),
};
