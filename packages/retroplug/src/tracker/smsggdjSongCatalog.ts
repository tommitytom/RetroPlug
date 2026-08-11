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
// would boot a blank song no matter what we wrote.
//
// So loading is done LIVE instead, by `smsggdjIntegration.liveLoad` - the song is written straight into
// work RAM through `writeRam`, with no `.sav` write and no reboot. `load` below (which names a slot in
// the superblock's cur_slot byte, for a v0.46+ cart to pick up at boot) survives as the fallback for a
// build with no layout, and as the durable record for real hardware and for savetool.html.
//
// `workingName` therefore reads the cart's own `song_name` out of work RAM when it has it, falling back
// to cur_slot; that is what makes it answerable on v0.45, which has no cur_slot at all.
//
// The same fact has a sharper edge than it first appears: because the cold boot is what makes ANY edit
// take effect, and the working song is not in the image, EVERY battery op here destroys it - not just
// `load`. See `workingSongOutsideBattery` below, which is how the shared Songs menu learns to warn about
// Delete and Move Up as well.
import type { SongCatalog } from "./songCatalog";
import { commonSongNameOffset } from "../smsggdj/runtime/layout";
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

/** The cart's own `song_name`, read out of live work RAM. Null when there is no RAM, when the supported
 *  builds disagree on where the field lives (see commonSongNameOffset), or when the bytes are blank -
 *  a freshly booted cart has never loaded anything, and "" is not a song name. */
function workingNameFromRam(ram?: Uint8Array): string | null {
  if (!ram) return null;
  const at = commonSongNameOffset();
  if (!at || ram.length < at.offset + at.length) return null;
  let s = "";
  for (let i = 0; i < at.length; i++) {
    const c = ram[at.offset + i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.trim() || null;
}

export const smsggdjSongCatalog: SongCatalog = {
  // Overloads the sync role as the menu gate, exactly as LSDj overloads lsdj-sync. It is attached by
  // the ROM provider off the SMSGGDJ build marker and covers both .sms and .gg, so it identifies the
  // cart precisely - a generic Master System game has no role and no Songs menu.
  markerRole: "sms-sync",

  list: (sav) => listSongs(sav),
  isValidSav: (bytes) => isSmsggdjSav(bytes),
  importSongs: (target, source, indices) => importSongs(target, source, indices),

  // Work RAM first, because the cart's own `song_name` is the truth: it is what the FILES screen shows,
  // it survives a load made from INSIDE the cart, and it is what a host-side liveLoad writes. The
  // superblock's cur_slot is the fallback for callers with no live system (an offline .sav), and it is
  // null on every build before v0.46 - which is precisely why reading work RAM is what lights the
  // working-song row, per-song recents and the window title up on v0.45.
  workingName: (sav, ram) => {
    const fromRam = workingNameFromRam(ram);
    if (fromRam !== null) return fromRam;
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
