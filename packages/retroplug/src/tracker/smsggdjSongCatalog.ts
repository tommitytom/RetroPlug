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
//
// The same fact has a sharper edge than it first appears: because the cold boot is what makes ANY edit
// take effect, and the working song is not in the image, EVERY battery op here destroys it - not just
// `load`. See `workingSongOutsideBattery` below, which is how the shared Songs menu learns to warn about
// Delete and Move Up as well.
import type { SongCatalog } from "./songCatalog";
import {
  listSongs,
  isSmsggdjSav,
  isSongSaved,
  curSlot,
  setCurSlot,
  deleteSong,
  reorderSongs,
  importSongs,
  SMDJ4_BLOCK_LEN,
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

  // Every battery edit on this console cold-boots the cart, and the working song is not in the battery,
  // so it does not come back. Delete, Move Up and Add destroy an hour's work exactly as thoroughly as
  // Load does. The other two consoles are immune - their working song rides along inside the image - so
  // the shared menu guarded Load alone, and that assumption had to become explicit rather than implied.
  workingSongOutsideBattery: true,

  // Answerable only WITH work RAM, which is why the interface grew the second parameter. Without it we
  // say clean: "I cannot tell" has to look like "nothing to lose", because a prompt that fires when
  // nothing would be lost trains people to dismiss the one that matters.
  //
  // Dirty means the live block matches NO saved song - the contract's own words, and the right test for
  // a console with no link byte to consult. Once the cart autoloads its currently-loaded slot at boot, a
  // freshly booted cart matches the slot it came from and stays silent; before that it has no way to
  // know a blank song is blank, which is one more reason the Songs menu is gated on that ROM.
  workingSongDirty: (sav, ram) => {
    if (!ram || ram.length < SMDJ4_BLOCK_LEN || !isSmsggdjSav(sav)) return false;
    return !isSongSaved(sav, ram.subarray(0, SMDJ4_BLOCK_LEN));
  },

  // Naming the slot is the whole of `load`; mutateLiveSav's cold boot is what makes the cart act on it.
  load: (sav, index) => setCurSlot(sav, index),

  delete: (sav, index) => deleteSong(sav, index),
  reorder: (sav, from, to) => reorderSongs(sav, from, to),
};
