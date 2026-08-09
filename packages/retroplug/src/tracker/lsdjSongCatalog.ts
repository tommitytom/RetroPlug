// The LSDj implementation of SongCatalog — thin wrappers over the existing pure ops (no logic here).
import type { SongCatalog } from "./songCatalog";
import { listProjects, workingSongName, isLsdjSav, workingSongDirty, activeSlot, savSongName } from "../lsdj/codec/sav";
import { loadSongToWorking, deleteSongInSav, moveSongInSav, importSongsFromSav } from "../lsdjSongOps";

export const lsdjSongCatalog: SongCatalog = {
  markerRole: "lsdj-sync", // LSDj overloads lsdj-sync as its menu-gate marker
  list: (sav) => listProjects(sav).map((p) => ({ index: p.slot, name: p.name })),
  isValidSav: (bytes) => isLsdjSav(bytes),
  // indices are source SLOT numbers (LSDj's list index === slot); import copies them into free target slots.
  importSongs: (target, source, indices) => importSongsFromSav(target, source, indices),
  workingName: (sav) => workingSongName(sav),
  // A LINKED working song borrows its slot's name (LSDj stores names on the stored project, not in the song)
  // and reports linked, so saving overwrites that slot rather than claiming a new one.
  //
  // An UNLINKED one gets no row at all. That is not the risa rule with a different answer, it is the same
  // rule meeting a different fact: an unlinked LSDj working song has NO name anywhere to label a row with,
  // where risa's carries its own. It is still dirty, so the load guard still warns before destroying it -
  // and the guard, unlike a row, can ask for a name (see `workingNeedsName` in the menu).
  workingSong: (sav) => {
    const slot = activeSlot(sav);
    return slot < 0 ? null : { name: savSongName(sav, slot), linked: true };
  },
  workingSongDirty: (sav) => workingSongDirty(sav),
  load: (sav, index) => loadSongToWorking(sav, index),
  delete: (sav, index) => deleteSongInSav(sav, index),
  // reorder swaps two saved songs' slot contents (LSDj addresses songs by a fixed slot number, so a
  // reorder renumbers — `from`/`to` are list positions, not slots; see moveSongInSav)
  reorder: (sav, from, to) => moveSongInSav(sav, from, to),
};
