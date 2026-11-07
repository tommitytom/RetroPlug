// The tracker integrations themselves, one per console: the catalogs, detectors and layouts a cart's Songs and Assets
// menus are built over, as the pure list the app's contributions carry (app.ts). trackerIntegration.ts holds the
// contract and resolves a system's roles through this list; the objects are here rather than there so the generated
// registry can import them without the module that reads it (src/generated/packages.ts).
import type { TrackerIntegration, RamWrite } from "./trackerIntegration";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { risaSongCatalog } from "./risaSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";
import { risaAssetCatalog } from "./risaAssetCatalog";
import { smsggdjSongCatalog } from "./smsggdjSongCatalog";
import { smsggdjAssetCatalog } from "./smsggdjAssetCatalog";
import { identifySmsggdjVersion } from "../smsggdj/romDetect";
import { resolveSmsggdjLayout } from "../smsggdj/runtime/layout";
import type { SmsggdjLayout } from "../smsggdj/runtime/types";
import {
  readSongBlock,
  readSongName,
  readSongEcho,
  sanitizeEcho,
  songLengthRows,
  isGrooveEmpty,
  GLIDE_GROOVE_SEL,
} from "../smsggdj/codec/sav";
import { bliptoasterAssetCatalog } from "./bliptoasterAssetCatalog";
import { blipToasterInfo, BLIPTOASTER_MARKER } from "../bliptoaster/romDetect";
import { identifyLsdj } from "../lsdj/runtime/identify";
import { identifyRisaVersion } from "../risa/runtime/identify";
import { resolveRisaLayout } from "../risa/runtime/layout";

export const lsdjIntegration: TrackerIntegration = {
  id: "lsdj",
  label: "LSDj",
  markerRole: "lsdj-sync",
  songs: lsdjSongCatalog,
  assets: lsdjAssetCatalog,
  romName: (rom) => {
    const v = identifyLsdj(rom); // parses the GB cartridge title at 0x134 ("LSDJ-Vx.y.z")
    return v ? `LSDj v${v.major}.${v.minor}.${v.patchLabel}${v.build ? ` ${v.build}` : ""}` : null;
  },
};

export const risaIntegration: TrackerIntegration = {
  id: "risa",
  label: "Risa",
  markerRole: "risa",
  songs: risaSongCatalog,
  assets: risaAssetCatalog,
  romName: (rom) => {
    const v = identifyRisaVersion(rom); // scans the PRG for the ASCII "RISA V<major>.<minor>.<patch>" marker
    return v ? `risa v${v}` : null;
  },
  // Only versions with a bundled RAM layout (2.2.0 / 2.2.1 / 2.3.0) are driveable; an unknown / unmarked
  // version resolves no layout. See supportedRisaVersions() in ../risa/runtime/layout.
  isVersionSupported: (rom) => resolveRisaLayout(identifyRisaVersion(rom)) !== null,
};

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

  // This cart's working song is work RAM, so a load is a poke rather than a byte transform on the sav -
  // and more than the block (see songWrites).
  liveLoad: (rom, sav, index, ram) => {
    const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
    if (!layout) return null;
    const block = readSongBlock(sav, index); // null on a free slot / bad checksum / malformed stream
    if (!block) return null;
    // The cart clears `song_edited` on its own load ("loaded block matches the slot: clean"), and leaving
    // it set would tell the cart the freshly loaded song has unsaved edits.
    return songWrites(layout, block, readSongName(sav, index), readSongEcho(sav, index), false, ram);
  },

  restoreWorkingSong: (rom, song, ram) => {
    const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
    return layout ? songWrites(layout, song.block, song.name, song.echo, song.edited, ram) : null;
  },
};

// BlipToaster: an NES MIDI synth (not a tracker), so it has NO song battery: only ROM assets (a baked DMC kit
// + the CHR font). An asset-only integration: `songs` is omitted, so only the asset submenus are built.
export const blipToasterIntegration: TrackerIntegration = {
  id: "bliptoaster",
  label: "BlipToaster",
  markerRole: "bliptoaster",
  assets: bliptoasterAssetCatalog,
  romName: (rom) => {
    const info = blipToasterInfo(rom); // decoded bliptoaster SIG block (null when the marker is absent)
    if (!info) return null;
    return `${BLIPTOASTER_MARKER} v${info.semver.join(".")}`; // marker doubles as the display name
  },
};
