// smsggdj's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.4): its Songs spec, with the
// `.smdj4` file actions (a 16-byte header - magic, block checksum, the echo settings that live in the DIRECTORY entry
// rather than the song - followed by the verbatim 6,912-byte block, uncompressed on purpose). Its asset catalog is
// empty, so the shared asset menus render nothing; the spec is still contributed so the ROM-scoped bake rows keep their
// place, exactly as the hand-built table had it.
import type { FileBrowserOpts, Host, SystemInfo } from "@retroplug/core/host";
import { SAV_PATTERNS, isSavPath, sanitizeName } from "@retroplug/core/path";
import { browseThen } from "@retroplug/ui/menu";
import {
  defineUi,
  type AssetMenuSpec,
  type SongMenuSpec,
  type TrackerUi,
} from "@retroplug/ui/contribution";
import { smsggdjAssetCatalog } from "../smsggdjAssetCatalog";
import { smsggdjSongCatalog } from "../smsggdjSongCatalog";
import {
  addSong,
  readSongBlock,
  readSongEcho,
  replaceSong as replaceSmdjSong,
  unwrapSong,
  wrapSong,
} from "../codec/sav";

/** The host groups this cart's rows reach: narrowed to what they use, so a third-party pack copied
 *  from this one takes exactly these and nothing more. */
type SmsggdjUiHost = Host<"files" | "systems" | "liveSongs" | "dialogs">;

function exportSong(host: SmsggdjUiHost, sys: SystemInfo, index: number, name: string): void {
  const opts: FileBrowserOpts = {
    title: `Export song ${index}`,
    patterns: ["*.smdj4"],
    saving: true,
    defaultName: `${sanitizeName(name)}.smdj4`,
  };
  browseThen(host, opts, (path) => {
    const sav = host.systems.readSram(sys.id);
    if (!sav) return;
    const block = readSongBlock(sav, index);
    if (!block) return; // free slot / failed its checksum: nothing honest to write
    const file = wrapSong(block, readSongEcho(sav, index) ?? undefined);
    if (file) host.files.writeFileAtomic(path, file);
  });
}

function replaceSong(host: SmsggdjUiHost, sys: SystemInfo, index: number): void {
  browseThen(host, { title: `Replace song ${index}`, patterns: ["*.smdj4"] }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    const song = unwrapSong(data); // rejects a wrong magic / length / checksum rather than importing junk
    if (!song) return;
    host.liveSongs.mutate(sys, (sav) => replaceSmdjSong(sav, index, song.block, song.echo));
  });
}

function addSongFromDisk(host: SmsggdjUiHost, sys: SystemInfo): void {
  browseThen(host, { title: "Add Song", patterns: ["*.smdj4", ...SAV_PATTERNS] }, (path) => {
    const data = host.files.readFile(path);
    if (!data) return;
    if (isSavPath(path)) {
      host.liveSongs.beginImport(sys, data); // a whole .sav/.srm -> the checkbox picker
      return;
    }
    const song = unwrapSong(data);
    if (!song) return;
    const name = path.replace(/^.*[\\/]/, "").replace(/\.smdj4$/i, "").slice(0, 8).toUpperCase();
    host.liveSongs.mutate(sys, (sav) => addSong(sav, song.block, name));
  });
}

const smsggdjSongSpec: SongMenuSpec = {
  id: "smsggdj",
  catalog: smsggdjSongCatalog,
  exportSong,
  replaceSong,
  addSong: addSongFromDisk,
  // No working-song rows: this cart's working song is work RAM, so the catalog reports no unsaved
  // working song to export or promote. See smsggdjSongCatalog.
};

/** `types: []`, so the shared asset menus render nothing and neither action is reachable. */
const smsggdjAssetSpec: AssetMenuSpec = {
  id: "smsggdj",
  catalog: smsggdjAssetCatalog,
  exportAsset: () => {},
  replaceAsset: () => {},
};

/** The tracker's UI: its Songs spec, and the empty asset spec the bake rows are built over. */
export const SMSGGDJ_TRACKER_UI: TrackerUi = {
  id: "smsggdj",
  song: smsggdjSongSpec,
  asset: smsggdjAssetSpec,
};

export default defineUi({ trackers: [SMSGGDJ_TRACKER_UI] });
