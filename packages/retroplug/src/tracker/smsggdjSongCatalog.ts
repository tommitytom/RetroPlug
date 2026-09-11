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
// Delete and Move Up as well. It also means the reverse: after a project LOAD the cart has only just
// booted, so its working song is the cart's, not the user's, and `workingSongDirty` has to say so.
import type { SongCatalog } from "./songCatalog";
import { commonBootedOffset, commonSongEditedOffset, commonSongNameOffset } from "../smsggdj/runtime/layout";
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

/** Has the cart finished booting? `ints_on` is written exactly once, right before the main loop starts
 *  and after every boot-time overwrite of the working song (main.asm: init's zero-fill, the splash,
 *  song_new, editor_init, boot_autoload, init_paint - THEN `ld a,1 / ld (ints_on),a / ei`). Until it
 *  reads 1, work RAM is readable but not yet the cart's. Not ready when there is no RAM, when the RAM is
 *  too short to hold the latch, or when the supported builds disagree on where it lives - "cannot tell"
 *  has to mean "do not write", which is the opposite polarity from the dirty predicate below. */
function isBooted(ram?: Uint8Array): boolean {
  if (!ram) return false;
  const at = commonBootedOffset();
  return at !== null && ram.length > at && ram[at] === 1;
}

/** The cart's own `song_name`, read out of live work RAM. Null when there is no RAM, when the cart has
 *  not booted (see isBooted), when the supported builds disagree on where the field lives (see
 *  commonSongNameOffset), or when the bytes are blank - a freshly booted cart has never loaded anything,
 *  and "" is not a song name.
 *
 *  Also null when the bytes are not a name at all. The cart writes names in ASCII (the same font-indexed
 *  strings its own `print_at` shows), padded with spaces or zeros; anything outside printable ASCII is a
 *  snapshot taken mid-write, or bytes that were never a name. A caller that RECORDS this - the Recent
 *  list polls it every half second - must never be handed such a thing, because it was: rows of box
 *  glyphs appeared in Recent, recorded from a cart that was still booting. */
function workingNameFromRam(ram?: Uint8Array): string | null {
  if (!ram || !isBooted(ram)) return null;
  const at = commonSongNameOffset();
  if (!at || ram.length < at.offset + at.length) return null;
  let s = "";
  for (let i = 0; i < at.length; i++) {
    const c = ram[at.offset + i];
    if (c === 0) break;
    if (c < 0x20 || c > 0x7e) return null;
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

  // The live cart answers ALONE when there is one, because its own `song_name` is the truth: it is what
  // the FILES screen shows, it survives a load made from INSIDE the cart, and it is what a host-side
  // liveLoad writes. Blank, or not yet booted, is therefore "no song" and never the save's guess - the
  // superblock's cur_slot says which slot the cart will autoload, not what it holds now. That byte is
  // the fallback only for callers with NO live system (an offline .sav), and it is null on every build
  // before v0.46 - which is precisely why reading work RAM is what lights the working-song row, per-song
  // recents and the window title up on v0.45.
  workingName: (sav, ram) => {
    if (ram) return workingNameFromRam(ram);
    const slot = curSlot(sav);
    return slot < 0 ? null : (listSongs(sav).find((s) => s.index === slot)?.name ?? null);
  },

  // See isBooted. Every consumer of the working song - the Recent list, the window title, the Songs
  // menu, the load guards, and the live load itself - waits on this rather than on a timer, because
  // the boot takes as long as the splash does and a timer would be a guess.
  workingSongReady: (ram) => isBooted(ram),

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
  // Two signals, and BOTH have to agree before anything is called unsaved:
  //
  //   song_edited  the cart's own flag, "1 = song data changed since last save/load" (editor.asm:141) -
  //                the byte its PROJECT screen prints UNSAVED from. Set at the input dispatch for every
  //                song-data screen (do_place / do_cut / do_edit), and cleared by a load (rle.asm:484),
  //                a save (rle.asm:817) and `song_new` (engine.asm:4326, "fresh song = no unsaved
  //                changes"). It answers "has a human touched this", which content cannot.
  //   the content  the live block matching no saved song. Narrows the flag, which is set on the
  //                KEYPRESS rather than on a mutation, so an edit typed and undone by hand stays clean.
  //
  // The flag is what this console needs and the other two do not. Its working song is not in the image,
  // so a cart that has just booted holds whatever `song_new` gave it - a blank song, saved in no slot.
  // Content alone therefore read every fresh boot as an hour of unsaved work: loading a project from
  // Recent offered to discard a song nobody had written, and could only call it "the working song"
  // because there wasn't one to name.
  workingSongDirty: (sav, ram) => {
    if (!ram || !isBooted(ram) || ram.length < SMDJ4_BLOCK_LEN || !isSmsggdjSav(sav)) return false;
    const edited = commonSongEditedOffset();
    if (edited === null || ram.length <= edited || ram[edited] === 0) return false;
    return !isSongSaved(sav, ram.subarray(0, SMDJ4_BLOCK_LEN));
  },

  // Naming the slot is the whole of `load`; mutateLiveSav's cold boot is what makes the cart act on it.
  load: (sav, index) => setCurSlot(sav, index),

  delete: (sav, index) => deleteSong(sav, index),
  reorder: (sav, from, to) => reorderSongs(sav, from, to),
};
