// The smsggdj SongCatalog + its TrackerIntegration: resolution, the Load-by-naming-a-slot mechanism,
// and the version gate that stops a build which cannot honour it from showing a dead menu.
import { test, expect } from "../../testing/harness";
import { resolveSongCatalog, resolveTracker } from "../../src/tracker";
import { smsggdjSongCatalog } from "../../src/tracker/smsggdjSongCatalog";
import { lsdjSongCatalog } from "../../src/tracker/lsdjSongCatalog";
import { risaSongCatalog } from "../../src/tracker/risaSongCatalog";
import { smsggdjIntegration } from "../../src/tracker/trackerIntegration";
import { buildSav, curSlot, SMDJ4_BLOCK_LEN, songLengthRows, sanitizeEcho } from "../../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout, supportedSmsggdjVersions } from "../../src/smsggdj/runtime/layout";
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

test("workingSongDirty needs work RAM, and says CLEAN when it has none", () => {
  // The predicate asks whether the working song's content exists in no saved slot, and on this console
  // that content is work RAM - so the interface passes it in. Without it the honest answer is false:
  // "I cannot tell" has to look like "nothing to lose", or the menu prompts on every edit forever.
  const sav = twoSongs();
  expect(smsggdjSongCatalog.workingSongDirty!(sav)).toBe(false); // no ram argument at all
  expect(smsggdjSongCatalog.workingSongDirty!(sav, new Uint8Array(16))).toBe(false); // too short to hold a song
});

test("a working song that matches a saved slot is clean; one that matches none is dirty", () => {
  const sav = twoSongs();
  const ram = new Uint8Array(8192); // SMS work RAM; the song block is at its base

  ram.set(song(2), 0); // exactly BETA
  expect(smsggdjSongCatalog.workingSongDirty!(sav, ram)).toBe(false);

  ram[5] ^= 0xff; // ...now edited, and saved nowhere
  expect(smsggdjSongCatalog.workingSongDirty!(sav, ram)).toBe(true);

  ram.set(song(1), 0); // exactly ALPHA - a different slot, still saved
  expect(smsggdjSongCatalog.workingSongDirty!(sav, ram)).toBe(false);
});

test("the working song is declared OUTSIDE the battery, which is what guards the other five ops", () => {
  // The flag the shared Songs menu reads. LSDj and risa leave it unset because their working song is
  // part of the image a battery edit rewrites, so their cold boot restores it and only Load is
  // destructive. Here every edit is, and the menu has to know that without special-casing the console.
  expect(smsggdjSongCatalog.workingSongOutsideBattery).toBe(true);
  expect(lsdjSongCatalog.workingSongOutsideBattery).toBe(undefined);
  expect(risaSongCatalog.workingSongOutsideBattery).toBe(undefined);
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

test("supported means we have that build's RAM layout, not that the cart autoloads", () => {
  // The gate was originally keyed on supportsCurSlot - whether the CART restores a song at boot - which
  // greyed out the whole submenu for v0.45, including Export / Delete / Move / Add / Import, none of
  // which need the cart's help. The right question is risa's: do we have this build's symbol snapshot,
  // and therefore know where to write. An unknown build still greys out; only the question changed.
  expect(smsggdjIntegration.isVersionSupported!(rom("0.45"))).toBe(true); // committed snapshot
  expect(smsggdjIntegration.isVersionSupported!(rom("0.46"))).toBe(true); // alias to 0.45's
  expect(smsggdjIntegration.isVersionSupported!(rom("0.47"))).toBe(false); // no snapshot, no alias
  expect(smsggdjIntegration.isVersionSupported!(rom("1.0"))).toBe(false);
  expect(smsggdjIntegration.isVersionSupported!(new Uint8Array(0x8000))).toBe(false); // not an smsggdj ROM

  // supportsCurSlot survives, because it still answers a real and different question - whether the CART
  // restores a song at boot - which is what the ROM branch and the docs are about.
  expect(supportsCurSlot("0.45")).toBe(false);
  expect(supportsCurSlot("0.46")).toBe(true);
  expect(supportsCurSlot(null)).toBe(false);
});

test("the layout resolves for the snapshot version and for its alias, and for nothing else", () => {
  const l45 = resolveSmsggdjLayout("0.45")!;
  expect(l45 != null).toBe(true);
  expect(l45.song).toBe(0); // the block leads work RAM - the assumption every write rests on
  expect(l45.songLen).toBe(SMDJ4_BLOCK_LEN);
  expect(l45.echoLen).toBe(8); // eight contiguous db's, asserted at generation time
  expect(l45.nameLen).toBe(8);

  // An alias borrows the addresses but keeps its OWN label, so a reader can still tell them apart.
  const l46 = resolveSmsggdjLayout("0.46")!;
  expect(l46.version).toBe("0.46");
  expect(l46.name).toBe(l45.name);
  expect(l46.echo).toBe(l45.echo);

  expect(resolveSmsggdjLayout("0.47")).toBe(null);
  expect(resolveSmsggdjLayout(null)).toBe(null);
  expect(supportedSmsggdjVersions().includes("0.45")).toBe(true);
});

test("liveLoad returns the block AND the metadata SMDJ4 keeps outside it", () => {
  // The reason a layout is needed at all: poking only the 6,912-byte block loads the right notes with
  // the previous song's ECHO settings, which is audible, and the previous song's name on screen.
  const sav = buildSav([{ block: song(1), name: "ALPHA" }, { block: song(2), name: "BETA" }], CART)!;
  const layout = resolveSmsggdjLayout("0.45")!;
  const writes = smsggdjIntegration.liveLoad!(rom("0.45"), sav, 1)!;
  expect(writes != null).toBe(true);

  const at = (offset: number) => writes.find((w) => w.offset === offset);
  expect(at(layout.song)!.bytes).toEqual(song(2)); // the decoded block, at work-RAM offset 0
  expect(at(layout.name)!.bytes.length).toBe(8);
  expect(String.fromCharCode(...at(layout.name)!.bytes).trimEnd().replace(/\0+$/, "")).toBe("BETA");
  expect(at(layout.echo)!.bytes.length).toBe(8);
  expect(at(layout.edited)!.bytes).toEqual(Uint8Array.of(0)); // the cart's own load clears this

  // prj_slot is deliberately left alone - the cart READS it to decide what to load, it doesn't write it.
  expect(at(layout.slot)).toBe(undefined);

  // Every write must land inside the 8 KB region, or writeRam refuses it and the load half-applies.
  for (const w of writes) expect(w.offset + w.bytes.length <= 0x2000).toBe(true);
});

test("liveLoad reproduces load_rebase only when the transport is RUNNING", () => {
  // The cart's own load_rebase opens with `ret z` on play_state, so a load made while stopped needs none
  // of it. While playing it matters and is not a passing glitch: eng_len is the wrap point, so a song
  // loaded under a running transport without it loops at the PREVIOUS song's length indefinitely.
  const sav = buildSav([{ block: song(1), name: "ALPHA" }], CART)!;
  const layout = resolveSmsggdjLayout("0.45")!;
  const offsets = (ram?: Uint8Array) => new Set(smsggdjIntegration.liveLoad!(rom("0.45"), sav, 0, ram)!.map((w) => w.offset));

  const stopped = new Uint8Array(8192); // play_state = 0
  expect(offsets(stopped).has(layout.engLen)).toBe(false);
  expect(offsets(stopped).has(layout.liveQ)).toBe(false);
  expect(offsets(undefined).has(layout.engLen)).toBe(false); // no RAM at all: assume stopped, write less

  const playing = new Uint8Array(8192);
  playing[layout.playState] = 1;
  const w = smsggdjIntegration.liveLoad!(rom("0.45"), sav, 0, playing)!;
  const at = (off: number) => w.find((x) => x.offset === off);
  expect(at(layout.engLen) != null).toBe(true);
  expect(at(layout.liveQ)!.bytes).toEqual(new Uint8Array(layout.liveQLen).fill(0xff)); // stale cells cleared
});

test("songLengthRows follows the cart's own scan, including its minimum of 1", () => {
  // engine.asm:2282-2308: scan the 128x4 grid backwards for the last byte that is not $FF, then
  // ceil(bytes / 4), floored at 1. A zero would stall the sequencer, so an empty song is one row long.
  const empty = new Uint8Array(SMDJ4_BLOCK_LEN).fill(0xff);
  expect(songLengthRows(empty)).toBe(1);

  const one = new Uint8Array(SMDJ4_BLOCK_LEN).fill(0xff);
  one[0x1300] = 0; // a chain in row 0, column 0
  expect(songLengthRows(one)).toBe(1);

  const four = new Uint8Array(SMDJ4_BLOCK_LEN).fill(0xff);
  four[0x1300 + 3 * 4] = 0; // row 3
  expect(songLengthRows(four)).toBe(4);

  const full = new Uint8Array(SMDJ4_BLOCK_LEN).fill(0xff);
  full[0x1300 + 511] = 0; // the very last byte of the grid
  expect(songLengthRows(full)).toBe(128);
});

test("echo is sanitized the way the cart sanitizes it, so a bad entry cannot reach the engine", () => {
  // echo_sanitize (engine.asm:982-1011) runs after the cart's OWN load. Skipping it would let a corrupt
  // or foreign directory entry put an out-of-range mode or a zero delay tap into the live engine.
  expect(sanitizeEcho(Uint8Array.of(9, 0, 99, 0xff, 0xff, 7, 0, 0))).toEqual(
    Uint8Array.of(0, 1, 15, 0x0f, 0x0f, 1, 0, 0),
  );
  // A legal set passes through untouched, transposes included - every signed byte is valid there.
  const ok = Uint8Array.of(2, 4, 8, 2, 4, 0, 0xf4, 0x0c);
  expect(sanitizeEcho(ok)).toEqual(ok);
  expect(sanitizeEcho(ok) === ok).toBe(false); // a NEW array: the caller's directory bytes are never mutated
});

test("liveLoad declines rather than guessing", () => {
  const sav = buildSav([{ block: song(1), name: "ALPHA" }], CART)!;
  expect(smsggdjIntegration.liveLoad!(rom("0.47"), sav, 0)).toBe(null); // no layout for that build
  expect(smsggdjIntegration.liveLoad!(new Uint8Array(0x8000), sav, 0)).toBe(null); // not an smsggdj ROM
  expect(smsggdjIntegration.liveLoad!(rom("0.45"), sav, 3)).toBe(null); // no song in that slot

  // A song whose stored checksum no longer matches is refused, not poked in half-decoded.
  const corrupt = sav.slice();
  corrupt[32 + 6] ^= 0xff;
  expect(smsggdjIntegration.liveLoad!(rom("0.45"), corrupt, 0)).toBe(null);
});

test("workingName prefers the cart's own song_name in work RAM", () => {
  // What makes the working-song row, per-song recents and the window title work on v0.45, which has no
  // cur_slot byte at all.
  const sav = twoSongs();
  const layout = resolveSmsggdjLayout("0.45")!;
  const ram = new Uint8Array(8192);
  ram.set(new TextEncoder().encode("MYSONG\0\0"), layout.name);
  expect(smsggdjSongCatalog.workingName(sav, ram)).toBe("MYSONG");

  // Blank work RAM means the cart has loaded nothing, which is not a song called "".
  expect(smsggdjSongCatalog.workingName(sav, new Uint8Array(8192))).toBe(null);
  // No RAM at all (an offline .sav): fall back to the save's own record, which v0.45 does not carry.
  expect(smsggdjSongCatalog.workingName(sav)).toBe(null);
  expect(smsggdjSongCatalog.workingName(smsggdjSongCatalog.load(sav, 1)!)).toBe("BETA"); // ...but v0.46 does
});
