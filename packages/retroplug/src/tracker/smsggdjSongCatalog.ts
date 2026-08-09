// The smsggdj implementation of SongCatalog - thin wrappers over the pure SMDJ4 ops.
//
// One thing works differently here from LSDj and risa, and it is worth understanding before changing
// anything: THIS CART'S WORKING SONG IS NOT IN THE BATTERY. It is the live 6,912-byte work-RAM block at
// $C000 (SAVEFORMAT.md: "the contiguous live-RAM block"), and the cart boots blank rather than
// autoloading - `song_new` at main.asm:238, whose comment explains the choice: "a first power-on should
// make sound".
//
// The shared spine (mutateLiveSav) is read SRAM -> byte transform -> write .sav -> cold boot, which for
// the other two consoles restores the working song because it lives in the image. For this one it
// would boot a blank song no matter what we wrote. So `load` names the slot in the superblock's
// currently-loaded byte and the CART loads it on the way up - which is why this catalog needs a build
// that honours that byte (see supportsCurSlot), and why `smsggdjIntegration.isVersionSupported` gates
// on it rather than the menu silently doing nothing.
//
// That byte also supplies the "currently loaded slot" the format previously lacked, which is what makes
// `workingName` / `workingSong` answerable at all.
import type { SongCatalog } from "./songCatalog";
import {
  listSongs,
  isSmsggdjSav,
  curSlot,
  setCurSlot,
  deleteSong,
  reorderSongs,
  importSongs,
} from "../smsggdj/codec/sav";

export const smsggdjSongCatalog: SongCatalog = {
  // Overloads the sync role as the menu gate, exactly as LSDj overloads lsdj-sync. It is attached by
  // the ROM provider off the SMSGGDJ build marker and covers both .sms and .gg, so it identifies the
  // cart precisely - a generic Master System game has no role and no Songs menu.
  markerRole: "sms-sync",

  list: (sav) => listSongs(sav),
  isValidSav: (bytes) => isSmsggdjSav(bytes),
  importSongs: (target, source, indices) => importSongs(target, source, indices),

  workingName: (sav) => {
    const slot = curSlot(sav);
    return slot < 0 ? null : (listSongs(sav).find((s) => s.index === slot)?.name ?? null);
  },

  // Always `linked`: the working song got there by being named in the superblock, so it is by
  // construction the slot it came from. There is no unlinked state to report - unlike risa, where a
  // working song can be imported from elsewhere and belong to no slot.
  workingSong: (sav) => {
    const slot = curSlot(sav);
    if (slot < 0) return null;
    const name = listSongs(sav).find((s) => s.index === slot)?.name;
    return name === undefined ? null : { name, linked: true };
  },

  // `workingSongDirty` is DELIBERATELY absent. It asks whether the working song's CONTENT differs from
  // its saved slot, and that content is in work RAM, which a `(sav) => boolean` predicate cannot see.
  // The interface documents exactly this case ("a console that can't tell omits it, and the caller then
  // never prompts"), so omitting is the contract-correct answer rather than a shortcut - a confirm that
  // fired when nothing would be lost is worse than none, because users learn to dismiss it.
  //
  // The consequence is real and belongs in the release notes: on SMS/GG, Load does not warn before
  // discarding unsaved edits. Closing it needs a WRAM-aware extension to the interface; `readRam` is on
  // the control-plane facet, so the data is reachable - only the signature is in the way.

  // Naming the slot is the whole of `load`; mutateLiveSav's cold boot is what makes the cart act on it.
  load: (sav, index) => setCurSlot(sav, index),

  delete: (sav, index) => deleteSong(sav, index),
  reorder: (sav, from, to) => reorderSongs(sav, from, to),
};
