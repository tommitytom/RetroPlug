// smsggdj's tracker integration: what an smsggdj system's Songs and Assets menus are built over. Unlike LSDj's and
// risa's, this cart's working song lives in WORK RAM, so a load is a poke into the running cart rather than a byte
// transform on the battery - which is what songWrites below builds, for a Load from a slot and for a reset's carry.
// The song catalog reads the SMDJ4 battery (its directory entries and blocks); the asset catalog is empty for now, the
// ROM-asset half being its own slice.
//
// The menus themselves are the shell's (ui/screens/menu); an integration is the data they are built from, resolved by
// a system's markerRole (the shell's trackerRegistry).
import type { TrackerIntegration, RamWrite } from "@retroplug/tracker/trackerIntegration";
import {
  PLAY_HOLD_MS,
  type RenderFacts,
  type RenderHost,
  type RenderPlaySink,
  type TrackerRender,
} from "@retroplug/tracker/trackerRender";
import type { SongInfo } from "@retroplug/tracker/songCatalog";
import { smsggdjSongCatalog } from "./smsggdjSongCatalog";
import { smsggdjAssetCatalog } from "./smsggdjAssetCatalog";
import { identifySmsggdjVersion } from "./romDetect";
import { resolveSmsggdjLayout } from "./runtime/layout";
import type { SmsggdjLayout } from "./runtime/types";
import {
  readSongBlock,
  readSongName,
  readSongEcho,
  sanitizeEcho,
  songLengthRows,
  isGrooveEmpty,
  isSmsggdjSav,
  GLIDE_GROOVE_SEL,
} from "./codec/sav";

/** The writes that put a song block (and the metadata SMDJ4 keeps beside it) into a running smsggdj -
 *  shared by a Load (from a slot) and a reset's carry (from an image). The block alone is not enough:
 *  SMDJ4 keeps the NAME and the ECHO settings in the directory entry (src/rle.asm:34 - "metadata, not in
 *  the block"), and echo is audible, so loading without it would play the new song through the old
 *  song's delay taps. `edited` is what `song_edited` should read afterwards. */
function songWrites(
  layout: SmsggdjLayout,
  block: Uint8Array,
  name: Uint8Array | null,
  echo: Uint8Array | null,
  edited: boolean,
  ram?: Uint8Array,
): RamWrite[] {
  const writes: RamWrite[] = [{ offset: layout.song, bytes: block }];
  if (name) writes.push({ offset: layout.name, bytes: name.subarray(0, layout.nameLen) });
  // Sanitized as the cart's PROJECT-screen load does (`song_load` runs echo_sanitize; its FILES-screen
  // load does not), so a corrupt or foreign directory entry cannot put an out-of-range echo mode or a
  // zero delay tap into the live engine. Slot matching compares echo sanitized, so this costs nothing.
  if (echo) writes.push({ offset: layout.echo, bytes: sanitizeEcho(echo.subarray(0, layout.echoLen)) });
  writes.push({ offset: layout.edited, bytes: Uint8Array.of(edited ? 1 : 0) });
  // `prj_slot` is deliberately NOT written. The cart's own load READS it (you move the PROJECT cursor
  // to a slot, then press LOAD) rather than writing it, and its legacy meaning is a 6-slot SMDJ3 index
  // that the PROJECT screen may still clamp - so pointing it at a directory index up to 31 risks a
  // confused UI for no real gain. It stays in the layout because it costs nothing to know.

  // --- the cart's `load_rebase`, for a load that lands while the transport is RUNNING --------------
  // load_rebase opens with `ret z` on play_state, so a load made while stopped needs none of this and
  // gets none of it. While playing, the engine is caching state derived from the OLD song, and leaving
  // it is not a passing glitch: eng_len is the wrap point, so the sequencer would loop at the previous
  // song's length forever. Three effects are reproduced; the fourth, load_carry_post's CONT beat-carry
  // (which replants the carried phrase in the reserved slots), is NOT - that is a musical feature of
  // the cart's own CONT load, and synthesizing it from out here would be re-implementing the tracker.
  if (ram && ram.length > layout.playState && ram[layout.playState] !== 0) {
    writes.push({ offset: layout.engLen, bytes: Uint8Array.of(songLengthRows(block)) });
    // Queued LIVE cells address the old song's grid, and a pending chain-end stop would fire against
    // the carried chain.
    writes.push({ offset: layout.liveQ, bytes: new Uint8Array(layout.liveQLen).fill(0xff) });
    // An empty groove gives the clock nothing to advance on, so the cart falls back to groove 0. The
    // cart asks through `groove_base`, which resolves the glide sentinel to `glide_scratch` in work
    // RAM - so mid-glide the question is about RAM the load leaves alone. Asked of the block instead,
    // groove 16 starts exactly at the block's end, read as "empty", and EVERY mid-glide load aborted it.
    const sel = ram[layout.grooveSel];
    // Any other value is a block groove, and `groove_base` indexes it as `sel * 16` in 8 bits - so a stray
    // sel above 16 wraps to groove `sel & 15` there, and here.
    const empty = sel === GLIDE_GROOVE_SEL ? ram[layout.glideScratch] === 0 : isGrooveEmpty(block, sel & 0x0f);
    if (empty) {
      writes.push({ offset: layout.grooveSel, bytes: Uint8Array.of(0) });
      writes.push({ offset: layout.groovePos, bytes: Uint8Array.of(0) });
    }
  }

  // --- and repaint, which unlike load_rebase is needed whether or not the transport is running -----
  // The cart only redraws rows flagged in `dirty_rows`, and every one of its OWN load paths ends in
  // `mark_all_dirty` (fpx_close, cont_load_fire). Writing the song without this loads it correctly and
  // leaves the PREVIOUS song on screen: the notes change, the display does not, until the user nudges
  // something. Measured before the fix - a load moved exactly 0 of 48,903 non-animating pixels.
  // `mark_all_dirty` is literally "1 into all 16 flags", so reproducing it is one write.
  writes.push({ offset: layout.dirtyRows, bytes: new Uint8Array(layout.dirtyRowsLen).fill(1) });
  writes.push({ offset: layout.labelDirty, bytes: Uint8Array.of(1) });
  return writes;
}

/** The writes that load saved song `index` into a running cart: null when this build's layout is unknown or the slot
 *  holds no song it can decode. The one write list both the Songs menu's Load and a render's song load use. */
export function smsggdjLiveLoad(
  rom: Uint8Array,
  sav: Uint8Array,
  index: number,
  ram?: Uint8Array,
): RamWrite[] | null {
  const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
  if (!layout) return null;
  const block = readSongBlock(sav, index); // null on a free slot / bad checksum / malformed stream
  if (!block) return null;
  // The cart clears `song_edited` on its own load ("loaded block matches the slot: clean"), and leaving it set would tell
  // the cart the freshly loaded song has unsaved edits.
  return songWrites(layout, block, readSongName(sav, index), readSongEcho(sav, index), false, ram);
}

// --- rendering a song (docs/modular-graph-phase-f4a-design.md sections 4.1 and 5.1) --------------------------------
//
// A render of a chosen song is a post-boot write here, not a seed: the working song is the live work-RAM block at $C000
// and the cart boots deliberately blank (`song_new`, main.asm:238), so there is nothing in the battery to promote. An
// earlier version seeded the superblock's `cur_slot` byte, which is the right durable record but only the unreleased
// cur-slot build reads it at boot, so on the shipped v0.45 it selected nothing and the render came out silent.

// The load has to land AFTER the cart's boot-time `song_new`, which blanks the song block on the way up (main.asm:224,
// right after the splash). The shared 1500 ms settle is tuned for the GB splash and is not enough here: measured on the
// shipped v0.45, a write at 1500 or 2000 ms is silently undone and only 2500 ms and later survive. A bigger fixed number
// would be guessing (the margin is ~500 ms on NTSC and PAL runs ~17% slower) and retrying until the block reads back is
// NOT enough either: measured, a write at 1750 ms verifies perfectly (the whole 6,912-byte block byte-identical) and is
// then wiped 400 ms later, so the loop stops on a match that does not last.
//
// So wait for the cart to tell us it is past that point. `song_new` fills wave_ram with the 8 preset waves, and work RAM
// powers on all zeros (MesenSmsSystem pins RamPowerOnState = AllZeros), so a non-zero wave_ram IS "song_new has run". The
// write then still has to verify, and hold across several checks: the readback is the guarantee, the boot signal is what
// stops it passing too early.
const SMSGGDJ_STEP_MS = 250;
const SMSGGDJ_BOOT_STEPS = 24; // x STEP = 6s of boot to wait through before writing
const SMSGGDJ_HOLD_STEPS = 4; // consecutive verified checks (1s) before the load is believed
const SMSGGDJ_WRITE_TRIES = 4;

/** Any non-zero byte in `ram[at .. at+len)`. */
function anyNonZero(ram: Uint8Array, at: number, len: number): boolean {
  for (let i = at; i < at + len && i < ram.length; i++) if (ram[i] !== 0) return true;
  return false;
}

/** `ram[at .. at+len) === want[from .. from+len)`. */
function ramMatches(ram: Uint8Array, at: number, want: Uint8Array, from: number, len: number): boolean {
  if (ram.length < at + len || want.length < from + len) return false;
  for (let i = 0; i < len; i++) if (ram[at + i] !== want[from + i]) return false;
  return true;
}

const isSmsggdjRom = (rom: Uint8Array | null): boolean =>
  rom !== null && identifySmsggdjVersion(rom) !== null;

/** The Pause press that starts the transport, and the check before it: the cart's play/stop control is the PAUSE button
 *  (SMS: the Z80 NMI; GG: its real Start), which sets nmi_event and the frame loop turns that into tp_start when stopped,
 *  tp_stop when playing (main.asm:928-936). From the boot SONG screen tp_start is engine_play in MODE_SONG, the whole song
 *  from song_cur, which is what a render wants. It is a TOGGLE, so pressing it blind is wrong whenever the cart is already
 *  running - which a render restored from a savestate very much can be. So ask first. */
function pressSmsggdjPlay(host: RenderHost, id: number, rom: Uint8Array, sink: RenderPlaySink): Float32Array[][] {
  // The play button IS the Pause NMI on this console, so an ordinary Master System game would be PAUSED by it: only
  // a cart this build recognises is pressed. The role's marker and this gate are not the same question - the marker is
  // the provider's, and it need not require the version string the layout does.
  if (!isSmsggdjRom(rom)) return [];
  const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
  const ram = layout ? host.readRam(id) : null;
  // A cart whose transport cannot be read is pressed as before.
  if (layout && ram && ram.length > layout.playState && ram[layout.playState] !== 0) return [];
  host.pressButton(id, SMS_START, true);
  const head = sink.renderChunk(PLAY_HOLD_MS); // the song starts here: capture it as the lead-in
  host.pressButton(id, SMS_START, false);
  return [head];
}

/** The saved slots as one line, in this cart's own spelling ("0:METRO", no space: the FILES screen's form), as a
 *  selection error prints them. A battery with no directory entries has none to name. */
const smsggdjListing = (songs: readonly SongInfo[]): string =>
  songs.map((s) => `${s.index}:${s.name}`).join(", ") || "(none)";

/** The warnings a render of this cart owes the user about its BOOT state: it boots to a blank song, which is silent
 *  without one. The other thing a render of it cannot do (find an end of song) is said where the probe answers null,
 *  which is where the render learns it. */
function warnAboutBlankBoot(facts: RenderFacts, warn: (msg: string) => void): void {
  if (!isSmsggdjRom(facts.rom)) return;
  // A selected song is loaded, so the boot state is not what is heard; a savestate carries work RAM, and the working song
  // with it (that is how the UI's System > Render reaches this console), so warning there would be false.
  if (!facts.song && !facts.state && facts.sav && isSmsggdjSav(facts.sav)) {
    const songs = smsggdjSongCatalog.list(facts.sav);
    if (songs.length) {
      warn(
        `render: smsggdj boots to a BLANK song (it keeps no working song in the battery), so this will render ` +
          `silence \u2014 pass --song <name> or --song-index <n> to play one of: ` +
          songs.map((sg) => `${sg.index}:${sg.name || "(unnamed)"}`).join(", "),
      );
    }
  }
}

/** The end-of-song probe this cart has not got, and the warning it owes the user for that: `H` is a phrase hop, not
 *  LSDj's HFF stop, nothing in the command set touches the transport, and when a chain ends the track loops back to the
 *  top of its own contiguous block (engine.asm at_loopblk) - columns loop independently and forever, by design. Say so
 *  instead of leaving the user to guess why the WAV is exactly the cap long, at the point in the render where a probe
 *  that can find the end would have answered. */
function noPlayingProbe(facts: RenderFacts, warn: (msg: string) => void): null {
  if (facts.durationMs === undefined && isSmsggdjRom(facts.rom)) {
    warn(
      `render: smsggdj songs loop forever (no HFF-style stop exists on this console), so song-length ` +
        `auto-detect is not possible - rendering the full ${facts.maxDurationMs}ms cap; ` +
        `pass --duration to pin a length`,
    );
  }
  return null;
}

const SMS_START = 7; // SmsButton::Start -> SmsController::Buttons::Pause. smsggdj toggles play/stop on it.

export const SMSGGDJ_RENDER: TrackerRender = {
  listSongs: (sav) => smsggdjSongCatalog.list(sav),
  songList: { listing: smsggdjListing, empty: "(no saved songs in the catalog)" },

  // Write the chosen song into the booted cart's work RAM and confirm it stuck (see above). Throws rather than warns, on
  // every failure path: the user named a song, and rendering the blank boot song instead would produce a silent WAV that
  // looks like a successful render - exactly the bug this path exists to fix.
  loadSong: (host, id, rom, sav, index) => {
    const writes = smsggdjLiveLoad(rom, sav, index, host.readRam(id) ?? undefined);
    const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
    if (!layout || !writes?.length) {
      throw new Error(
        `render: could not load smsggdj song ${index} - no work-RAM layout for this build ` +
          `(${identifySmsggdjVersion(rom) ?? "unrecognized version"}), or the song will not decode`,
      );
    }
    const want = writes.find((w) => w.offset === layout.song)?.bytes;
    if (!want) throw new Error(`render: smsggdj song ${index} produced no song block to write`);

    // wave_ram is the block's first field, so it spans [song, song + phrasePool) - no extra symbol needed.
    for (let i = 0; i < SMSGGDJ_BOOT_STEPS; i++) {
      const ram = host.readRam(id);
      if (ram && anyNonZero(ram, layout.song, layout.phrasePool)) break;
      host.renderAudio(SMSGGDJ_STEP_MS);
    }

    // The whole block is compared, not a witness field: nothing else writes it while the transport is stopped, and a
    // partial match is exactly the failure being guarded against.
    for (let tries = 0; tries < SMSGGDJ_WRITE_TRIES; tries++) {
      for (const w of writes) {
        // Refused only for a range past the work RAM (a wrong layout) or a full command ring; a render drives its blocks
        // on this thread, so the ring is drained inline and the second cannot happen here.
        if (!host.writeRam(id, w.offset, w.bytes)) {
          throw new Error(`render: work-RAM write refused at +$${w.offset.toString(16)} loading smsggdj song ${index}`);
        }
      }
      let held = 0;
      while (held < SMSGGDJ_HOLD_STEPS) {
        host.renderAudio(SMSGGDJ_STEP_MS); // let the queued writes land, and the boot move on
        const ram = host.readRam(id);
        if (!ram || !ramMatches(ram, layout.song, want, 0, want.length)) break;
        held++;
      }
      if (held === SMSGGDJ_HOLD_STEPS) return;
    }
    throw new Error(
      `render: smsggdj song ${index} would not stay in work RAM - the cart kept overwriting it (still booting?)`,
    );
  },

  pressPlay: pressSmsggdjPlay,
  playingProbe: (_host, _id, _rom, facts, warn) => noPlayingProbe(facts, warn),

  warnings: warnAboutBlankBoot,
};

export const smsggdjIntegration: TrackerIntegration = {
  id: "smsggdj",
  label: "SMSGGDJ",
  markerRole: "sms-sync", // overloaded as the menu gate, as LSDj overloads lsdj-sync
  songs: smsggdjSongCatalog,
  assets: smsggdjAssetCatalog, // empty for now; the ROM-asset half is its own slice
  romName: (rom) => {
    const v = identifySmsggdjVersion(rom); // scans for the version string near the SMSGGDJ marker
    return v ? `smsggdj v${v}` : null;
  },
  // Supported = we have this build's work-RAM layout, exactly as risa gates on a bundled symbol
  // snapshot. NOT whether the CART restores a song at boot (cur_slot), which is the ROM's own concern
  // and only one of several ways to load. Gating the whole submenu on that greyed out
  // Export / Replace / Delete / Move / Add / Import too, none of which need the cart's help at all.
  // An unknown build still greys out - the failure mode is unchanged, only the question is right now.
  isVersionSupported: (rom) => resolveSmsggdjLayout(identifySmsggdjVersion(rom)) !== null,
  render: SMSGGDJ_RENDER,

  // This cart's working song is work RAM, so a load is a poke rather than a byte transform on the sav -
  // and more than the block (see songWrites).
  liveLoad: smsggdjLiveLoad,

  restoreWorkingSong: (rom, song, ram) => {
    const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
    return layout ? songWrites(layout, song.block, song.name, song.echo, song.edited, ram) : null;
  },
};
