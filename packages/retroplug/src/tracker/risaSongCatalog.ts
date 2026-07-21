// The risa implementation of SongCatalog — thin wrappers over the existing pure ops. risa's delete/move
// throw on a bad index (unlike LSDj's), so they're wrapped to the catalog's null-on-failure contract.
import type { SongCatalog } from "./songCatalog";
import { listSongs, workingSongName, workingSongInfo } from "../risa/codec/sav";
import { loadSongToWorkingInSav, deleteSongInSav, moveSongInSav } from "../risaSongOps";

const tryOp = (fn: () => Uint8Array): Uint8Array | null => {
  try {
    return fn();
  } catch {
    return null; // out of range / malformed → leave the sav untouched
  }
};

export const risaSongCatalog: SongCatalog = {
  markerRole: "risa",
  list: (sav) => listSongs(sav).map((s) => ({ index: s.index, name: s.name })),
  workingName: (sav) => workingSongName(sav),
  // Surface the working song ONLY when it's unsaved (not linked to a catalog slot); a saved working song is
  // already listed as its slot, so it would just duplicate a row.
  workingSong: (sav) => {
    const info = workingSongInfo(sav);
    return info && info.unsaved ? { name: info.name } : null;
  },
  load: (sav, index) => loadSongToWorkingInSav(sav, index),
  delete: (sav, index) => tryOp(() => deleteSongInSav(sav, index)),
  reorder: (sav, from, to) => tryOp(() => moveSongInSav(sav, from, to)),
};
