// The risa implementation of SongCatalog — thin wrappers over the existing pure ops. risa's delete/move
// throw on a bad index (unlike LSDj's), so they're wrapped to the catalog's null-on-failure contract.
import type { SongCatalog } from "./songCatalog";
import { listSongs, workingSongName, workingSongInfo, isRisaSav } from "../risa/codec/sav";
import {
  loadSongToWorkingInSav,
  deleteSongInSav,
  moveSongInSav,
  importSongsFromSav,
  workingSongDirty,
  workingSongSlot,
  saveWorkingToSlot,
} from "../risaSongOps";

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
  isValidSav: (bytes) => isRisaSav(bytes),
  importSongs: (target, source, indices) => tryOp(() => importSongsFromSav(target, source, indices)),
  workingName: (sav) => workingSongName(sav),
  // Name + link state only; workingSongDirty below is what decides whether the row is shown at all (see the
  // SongCatalog contract). `unsaved` deliberately isn't consulted here: it reports the LINK byte, and a
  // song can be linked and still hold an hour of unsaved edits.
  workingSong: (sav) => {
    const info = workingSongInfo(sav);
    return info ? { name: info.name, linked: workingSongSlot(sav) >= 0 } : null;
  },
  workingSongDirty: (sav) => workingSongDirty(sav),
  load: (sav, index) => loadSongToWorkingInSav(sav, index),
  saveWorkingToSlot: (sav, index) => tryOp(() => saveWorkingToSlot(sav, index)),
  delete: (sav, index) => tryOp(() => deleteSongInSav(sav, index)),
  reorder: (sav, from, to) => tryOp(() => moveSongInSav(sav, from, to)),
};
