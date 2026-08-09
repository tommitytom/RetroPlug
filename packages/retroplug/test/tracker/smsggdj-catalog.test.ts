// The smsggdj SongCatalog + its TrackerIntegration: resolution, the Load-by-naming-a-slot mechanism,
// and the version gate that stops a build which cannot honour it from showing a dead menu.
import { test, expect } from "../../testing/harness";
import { resolveSongCatalog, resolveTracker } from "../../src/tracker";
import { smsggdjSongCatalog } from "../../src/tracker/smsggdjSongCatalog";
import { smsggdjIntegration } from "../../src/tracker/trackerIntegration";
import { buildSav, curSlot, SMDJ4_BLOCK_LEN } from "../../src/smsggdj/codec/sav";
import { identifySmsggdjVersion, supportsCurSlot } from "../../src/smsggdj/romDetect";

const CART = 32 * 1024;
const song = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
const twoSongs = (): Uint8Array =>
  buildSav([{ block: song(1), name: "ALPHA" }, { block: song(2), name: "BETA" }], CART)!;

/** A ROM carrying the SMSGGDJ marker at $3640 and a version string after it, as real builds do. */
function rom(version: string): Uint8Array {
  const b = new Uint8Array(0x8000);
  for (let i = 0; i < "SMSGGDJ".length; i++) b[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  const v = `V${version}`;
  for (let i = 0; i < v.length; i++) b[0x367b + i] = v.charCodeAt(i);
  return b;
}

test("the catalog resolves off sms-sync, for both machines", () => {
  // The sync role is the marker, overloaded exactly as LSDj overloads lsdj-sync - and the provider
  // attaches it for .sms AND .gg, so one entry covers both machines.
  expect(resolveSongCatalog([{ kind: "sms-sync", config: { machine: "sms" } }])).toBe(smsggdjSongCatalog);
  expect(resolveSongCatalog([{ kind: "sms-sync", config: { machine: "gg" } }])).toBe(smsggdjSongCatalog);
  expect(resolveTracker([{ kind: "sms-sync", config: {} }])).toBe(smsggdjIntegration);
  // A plain Master System cart has no sync role, so no Songs menu.
  expect(resolveSongCatalog([{ kind: "mesen", config: {} }])).toBe(undefined);
});

test("list + isValidSav over a real SMDJ4 image", () => {
  const sav = twoSongs();
  expect(smsggdjSongCatalog.list(sav)).toEqual([
    { index: 0, name: "ALPHA" },
    { index: 1, name: "BETA" },
  ]);
  expect(smsggdjSongCatalog.isValidSav(sav)).toBe(true);
  expect(smsggdjSongCatalog.isValidSav(new Uint8Array(CART))).toBe(false); // a blank cart is not a save
});

test("load names the slot rather than moving the song, because the cart does the loading", () => {
  // The whole mechanism. This tracker's working song is work RAM, so there is nothing in the image to
  // move it into - `load` records WHICH song, and mutateLiveSav's cold boot is what makes the cart act.
  const sav = twoSongs();
  expect(smsggdjSongCatalog.workingName(sav)).toBe(null); // nothing loaded yet

  const loaded = smsggdjSongCatalog.load(sav, 1)!;
  expect(curSlot(loaded)).toBe(1);
  expect(smsggdjSongCatalog.workingName(loaded)).toBe("BETA");
  expect(smsggdjSongCatalog.workingSong!(loaded)).toEqual({ name: "BETA", linked: true });

  // Only the marker byte moved: no song was rewritten, so every blob is where it was.
  const a = sav.slice(), b = loaded.slice();
  b[7] = a[7];
  expect(b).toEqual(a);

  expect(smsggdjSongCatalog.load(sav, 5)).toBe(null); // no such song
});

test("workingSongDirty is absent, so the menu never prompts on a signal it cannot have", () => {
  // Deliberate: the predicate asks whether the working song's CONTENT differs from its slot, and that
  // content is in work RAM, which a (sav) => boolean cannot see. The interface documents omitting it,
  // and songLoadWouldDiscard returns false when it is missing - a confirm that fires when nothing would
  // be lost is worse than none.
  expect(smsggdjSongCatalog.workingSongDirty).toBe(undefined);
});

test("delete and reorder keep the loaded marker pointing at its own song", () => {
  const sav = smsggdjSongCatalog.load(twoSongs(), 1)!;
  expect(smsggdjSongCatalog.workingName(smsggdjSongCatalog.delete(sav, 0)!)).toBe("BETA"); // shifted to 0
  expect(smsggdjSongCatalog.workingName(smsggdjSongCatalog.reorder!(sav, 1, 0)!)).toBe("BETA");
  // Deleting the loaded song leaves nothing loaded, rather than a marker aimed at a stranger.
  expect(smsggdjSongCatalog.workingName(smsggdjSongCatalog.delete(sav, 1)!)).toBe(null);
});

test("romName reads the version, which is NOT adjacent to the build marker", () => {
  // v0.45 puts SMSGGDJ at $3640 and the version at $367B with the UI string table between them, so a
  // fixed-offset read would work today and break on the next build that adds a string.
  expect(smsggdjIntegration.romName(rom("0.46"))).toBe("smsggdj v0.46");
  expect(identifySmsggdjVersion(rom("0.45"))).toBe("0.45");
  expect(identifySmsggdjVersion(rom("0.46a"))).toBe("0.46a"); // point releases carry a letter
  expect(identifySmsggdjVersion(new Uint8Array(0x8000))).toBe(null); // no marker, no version
});

test("a build too old to honour the loaded-slot byte reports unsupported", () => {
  // It is not broken, it is not driveable: Load would write a byte the cart ignores and the user would
  // get a boot into a blank song with no error. Greying the submenu says so; risa gates the same way on
  // a version with no bundled RAM layout.
  expect(smsggdjIntegration.isVersionSupported!(rom("0.45"))).toBe(false);
  expect(smsggdjIntegration.isVersionSupported!(rom("0.46"))).toBe(true);
  expect(smsggdjIntegration.isVersionSupported!(rom("0.47"))).toBe(true);
  expect(smsggdjIntegration.isVersionSupported!(rom("1.0"))).toBe(true);
  expect(smsggdjIntegration.isVersionSupported!(new Uint8Array(0x8000))).toBe(false);
  expect(supportsCurSlot(null)).toBe(false);
});
