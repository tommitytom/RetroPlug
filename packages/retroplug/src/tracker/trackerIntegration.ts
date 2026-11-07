// A tracker "cart" integration - the console-agnostic bundle of a music tracker's SONG catalog (the battery)
// and ASSET catalog (the ROM's replaceable kits/palettes/fonts). One integration per console (LSDj, risa, ...),
// resolved from a system's roles by its markerRole. The menu builds the Songs + asset submenus generically
// over these; the file-dialog actions (which own file formats) stay per-console in the UI. A console's
// integration is its package's app contribution, so the product's list of them and the resolvers over it are the
// shell's (src/trackerRegistry.ts); this module is the contract one satisfies, and reads nothing.
import type { SongCatalog, WorkingSongImage } from "./songCatalog";
import type { AssetCatalog } from "./assetCatalog";

export interface TrackerIntegration {
  /** Row-id prefix + instance-submenu id suffix, e.g. "lsdj" / "risa". */
  readonly id: string;
  /** Instance-submenu label, e.g. "LSDj". */
  readonly label: string;
  /** The role kind the ROM provider attaches that identifies this console (the menu gate). */
  readonly markerRole: string;
  /** The song catalog (the battery), if this console has one. Asset-only consoles (e.g. BlipToaster, a MIDI
   *  synth with no song battery) omit it — the shared Songs menu is then simply not built. */
  readonly songs?: SongCatalog;
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
  /** Put a working song lifted out of a PREVIOUS core (see SongCatalog.readWorkingSong) back into this
   *  one - what carries it across a reset, for a console whose reboot would otherwise erase it. The
   *  same writes as `liveLoad`, from an image instead of a slot, and keeping the image's unsaved flag:
   *  a song that was unsaved before the reset is still unsaved after it. Null when the version has no
   *  layout. Pure, like liveLoad; the caller applies it once the cart is ready. */
  restoreWorkingSong?(rom: Uint8Array, song: WorkingSongImage, ram?: Uint8Array): RamWrite[] | null;
}

/** One contiguous poke into a system's work RAM: `offset` indexes the same region `readRam` returns. */
export interface RamWrite {
  offset: number;
  bytes: Uint8Array;
}
