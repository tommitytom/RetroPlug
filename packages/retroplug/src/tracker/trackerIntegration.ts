// A tracker "cart" integration — the console-agnostic bundle of a music tracker's SONG catalog (the battery)
// and ASSET catalog (the ROM's replaceable kits/palettes/fonts). One integration per console (LSDj, risa, …),
// resolved from a system's roles by its markerRole. The menu builds the Songs + asset submenus generically
// over these; the file-dialog actions (which own file formats) stay per-console in the UI. New consoles add
// one entry to TRACKER_INTEGRATIONS — the single extension point.
import type { RoleInstance } from "../systemRoles";
import type { SongCatalog } from "./songCatalog";
import type { AssetCatalog } from "./assetCatalog";
import { lsdjSongCatalog } from "./lsdjSongCatalog";
import { risaSongCatalog } from "./risaSongCatalog";
import { lsdjAssetCatalog } from "./lsdjAssetCatalog";
import { risaAssetCatalog } from "./risaAssetCatalog";
import { smsggdjSongCatalog } from "./smsggdjSongCatalog";
import { smsggdjAssetCatalog } from "./smsggdjAssetCatalog";
import { identifySmsggdjVersion } from "../smsggdj/romDetect";
import { resolveSmsggdjLayout } from "../smsggdj/runtime/layout";
import { readSongBlock, readSongName, readSongEcho, sanitizeEcho, songLengthRows, isGrooveEmpty } from "../smsggdj/codec/sav";
import { identifyLsdj } from "../lsdj/runtime/identify";
import { identifyRisaVersion } from "../risa/runtime/identify";
import { resolveRisaLayout } from "../risa/runtime/layout";

export interface TrackerIntegration {
  /** Row-id prefix + instance-submenu id suffix, e.g. "lsdj" / "risa". */
  readonly id: string;
  /** Instance-submenu label, e.g. "LSDj". */
  readonly label: string;
  /** The role kind the ROM provider attaches that identifies this console (the menu gate). */
  readonly markerRole: string;
  readonly songs: SongCatalog;
  readonly assets: AssetCatalog;
  /** The cart's OWN name — its internal ROM title / embedded version marker, NOT the on-disk filename
   *  (e.g. "LSDj v9.4.2" from the GB cartridge title, "risa v2.2.1" from the PRG "RISA V" marker). null
   *  when the ROM bytes carry no recognisable version. */
  romName(rom: Uint8Array): string | null;
  /** Whether THIS build can drive the cart's embedded version - it has the RAM-offset layout the runtime
   *  overlay + Songs/Assets menus read. LSDj drives every version (predicate omitted -> always supported);
   *  risa only versions with a bundled layout. false -> the menu greys the tracker submenu as
   *  "(Unsupported Version)" rather than offering dead rows. */
  isVersionSupported?(rom: Uint8Array): boolean;
  /** Load the saved song at `index` into the RUNNING cart's work RAM, expressed as the writes to make.
   *
   *  For a console whose working song lives in the battery (LSDj, risa), loading it is a byte transform
   *  on the sav and the shared `mutateLiveSav` cold boot puts it in memory - so those omit this. smsggdj
   *  keeps its working song in work RAM and boots blank, so there is nothing in the image to transform;
   *  the song has to be written into the core directly. `writeRam` makes that possible without stopping
   *  the audio thread, and doing it live means the Load never touches the `.sav` at all - strictly less
   *  destructive than the cold-boot path it replaces.
   *
   *  Takes the ROM because the write offsets come from that BUILD's symbol layout, and this is already
   *  where `romName` / `isVersionSupported` take one. Returns null when the version has no layout, the
   *  index names no song, or the song will not decode - the caller then leaves the cart alone.
   *
   *  Stays PURE: it returns writes, it does not perform them. `loadSongLive` (./liveSav) applies them,
   *  which keeps this testable without a core and keeps I/O out of the console-specific layer. */
  liveLoad?(rom: Uint8Array, sav: Uint8Array, index: number, ram?: Uint8Array): RamWrite[] | null;
}

/** One contiguous poke into a system's work RAM: `offset` indexes the same region `readRam` returns. */
export interface RamWrite {
  offset: number;
  bytes: Uint8Array;
}

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
  // snapshot. NOT `supportsCurSlot`: that asks whether the CART restores a song at boot, which is the
  // ROM's own concern and only one of several ways to load. Gating the whole submenu on it greyed out
  // Export / Replace / Delete / Move / Add / Import too, none of which need the cart's help at all.
  // An unknown build still greys out - the failure mode is unchanged, only the question is right now.
  isVersionSupported: (rom) => resolveSmsggdjLayout(identifySmsggdjVersion(rom)) !== null,

  // This cart's working song is work RAM, so a load is a poke rather than a byte transform on the sav.
  // The block alone is not enough: SMDJ4 keeps the NAME and the ECHO settings in the directory entry
  // (src/rle.asm:34 - "metadata, not in the block"), and echo is audible, so loading without it would
  // play the new song through the old song's delay taps.
  liveLoad: (rom, sav, index, ram) => {
    const layout = resolveSmsggdjLayout(identifySmsggdjVersion(rom));
    if (!layout) return null;
    const block = readSongBlock(sav, index); // null on a free slot / bad checksum / malformed stream
    if (!block) return null;

    const writes: RamWrite[] = [{ offset: layout.song, bytes: block }];
    const name = readSongName(sav, index);
    if (name) writes.push({ offset: layout.name, bytes: name.subarray(0, layout.nameLen) });
    // Sanitized exactly as the cart sanitizes after its own load, so a corrupt or foreign directory
    // entry cannot put an out-of-range echo mode or a zero delay tap into the live engine.
    const echo = readSongEcho(sav, index);
    if (echo) writes.push({ offset: layout.echo, bytes: sanitizeEcho(echo.subarray(0, layout.echoLen)) });
    // The cart clears this on its own load ("loaded block matches the slot: clean"), and leaving it set
    // would tell the cart the freshly loaded song has unsaved edits.
    writes.push({ offset: layout.edited, bytes: Uint8Array.of(0) });
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
      // An empty groove gives the clock nothing to advance on, so the cart falls back to groove 0.
      const sel = ram[layout.grooveSel];
      if (isGrooveEmpty(block, sel)) {
        writes.push({ offset: layout.grooveSel, bytes: Uint8Array.of(0) });
        writes.push({ offset: layout.groovePos, bytes: Uint8Array.of(0) });
      }
    }
    return writes;
  },
};

/** Every registered tracker integration. The one place a new tracker console is added. */
export const TRACKER_INTEGRATIONS: TrackerIntegration[] = [lsdjIntegration, risaIntegration, smsggdjIntegration];

/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
 *  integration's markerRole). undefined for a non-tracker system. */
export function resolveTracker(roles: RoleInstance[]): TrackerIntegration | undefined {
  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
}

/** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
export function resolveSongCatalog(roles: RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}
