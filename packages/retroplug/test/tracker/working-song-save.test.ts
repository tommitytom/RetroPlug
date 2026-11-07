// A working song that lives OUTSIDE the battery (smsggdj's work RAM) is in neither the project file nor
// the `.sav`, so until now three things threw it away without a word: closing the window, a reset, and the
// rows that replace what the cart holds. This file covers the host side of all three over the mock
// backend - the codec and catalog pieces, saving the song INTO the battery, the unsaved-changes list, and
// a reset carrying the song across the reboot. The real-core proofs are test-native/sms-working-song-save.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { SystemsStore } from "../../src/systemsStore";
import { buildAppRegistry } from "../../src/appHost";
import { hasUnsavedChanges, unsavedChanges } from "../../src/unsavedChanges";
import { unsavedLabel } from "../../ui/app/unsavedRows";
import { saveWorkingSong, loadSongLive, songLoadByNameWouldDiscard } from "../../src/tracker/liveSav";
import { smsggdjSongCatalog } from "@retroplug/smsggdj/smsggdjSongCatalog";
import { resolveSmsggdjLayout } from "@retroplug/smsggdj/runtime/layout";
import {
  addSong,
  buildSav,
  ensureSongDirectory,
  isSmsggdjSav,
  listSongs,
  readSongBlock,
  readSongEcho,
  curSlot,
  hasDirectoryHoles,
  packDirectory,
  SMDJ4_BLOCK_LEN,
} from "@retroplug/smsggdj/codec/sav";
import type { TrackedSlot, WorkingSongImage } from "@retroplug/tracker/songCatalog";
import { encodeWorkingSong, decodeWorkingSong } from "@retroplug/tracker/workingSongBlob";
import { setCurSlot as setCurSlotFor, sanitizeEcho } from "@retroplug/smsggdj/codec/sav";
import { smsggdjRom, smsggdjRam } from "../systems/fixtures";
import { commonConfigOffsets } from "@retroplug/smsggdj/runtime/layout";
import { composeAppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";

const CART = 32 * 1024;
const layout = resolveSmsggdjLayout("0.45")!;

const block = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
const nameBytes = (s: string): Uint8Array => Uint8Array.from({ length: 8 }, (_, i) => (i < s.length ? s.charCodeAt(i) : 0x20));
const image = (tag: number, name: string, edited = true, echo: Uint8Array = new Uint8Array(8)): WorkingSongImage => ({
  block: block(tag),
  name: nameBytes(name),
  echo,
  edited,
});

/** A battery the cart has never saved into: no SMDJ4 directory at all. What every new user starts with -
 *  all zeros, as Mesen powers a new cart's RAM on (RamPowerOnState AllZeros). */
const freshBattery = (): Uint8Array => new Uint8Array(CART);

/** Work RAM holding `img` the way the cart holds it, booted. */
function ramWith(img: WorkingSongImage, booted = true): Uint8Array {
  const ram = smsggdjRam({ block: img.block, edited: img.edited, booted });
  ram.set(img.name, layout.name);
  ram.set(img.echo, layout.echo);
  return ram;
}

// --- the codec ---------------------------------------------------------------------------------------

test("ensureSongDirectory gives a never-saved battery an empty directory, keeping its OPTIONS block", () => {
  const raw = freshBattery();
  raw.set([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 0x3f60); // the cart's machine config, which no song edit may eat
  const sav = ensureSongDirectory(raw)!;
  expect(isSmsggdjSav(sav)).toBe(true);
  expect(listSongs(sav)).toEqual([]);
  expect(sav.subarray(0x3f60, 0x3f6a)).toEqual(Uint8Array.of(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  expect(raw[0]).toBe(0); // a NEW image: the input is never mutated
});

test("ensureSongDirectory refuses an older SMDJ format rather than flattening its songs", () => {
  // The cart's own rle_dir_ensure initialises over anything that is not SMDJ4, SMDJ3 songs included. A
  // host save is one the user did not watch happen, so it is stricter: those belong in migrate.html.
  const smdj3 = freshBattery();
  smdj3.set([0x53, 0x4d, 0x44, 0x4a, 0x33], 0); // "SMDJ3"
  expect(ensureSongDirectory(smdj3)).toBe(null);
  const ok = buildSav([{ block: block(1), name: "ALPHA" }], CART)!;
  expect(ensureSongDirectory(ok)).toBe(ok); // already SMDJ4: untouched, same image
});

test("addSong carries the echo settings and the cart's raw name bytes", () => {
  // It used to take a name string and write echo OFF, so a working song saved through it would have lost
  // its delay taps - audible, not cosmetic.
  const echo = Uint8Array.of(2, 4, 8, 2, 4, 0, 0xf4, 0x0c);
  const sav = addSong(buildSav([], CART)!, block(3), nameBytes("GAMMA"), echo)!;
  expect(listSongs(sav)).toEqual([{ index: 0, name: "GAMMA" }]);
  expect(readSongEcho(sav, 0)).toEqual(echo);
});

// --- the catalog -------------------------------------------------------------------------------------

test("an edited song on a cart that has never saved is UNSAVED, not clean", () => {
  // The regression. The dirty check required an SMDJ4 directory, and a cart only writes one on its first
  // SAVE - so every new user's first song read as clean and nothing ever warned about losing it.
  const ram = ramWith(image(5, "FIRST"));
  expect(smsggdjSongCatalog.workingSongDirty!(freshBattery(), ram)).toBe(true);
  // ...and the cart's own flag still decides: a fresh boot on the same battery is clean.
  expect(smsggdjSongCatalog.workingSongDirty!(freshBattery(), ramWith(image(5, "FIRST", false)))).toBe(false);
});

test("readWorkingSong lifts the block, name, echo and flag - and nothing from a cart still booting", () => {
  const echo = Uint8Array.of(1, 3, 5, 0, 0, 0, 0, 0);
  const img = image(6, "SIXES", true, echo);
  const got = smsggdjSongCatalog.readWorkingSong!(ramWith(img))!;
  expect(got.block).toEqual(img.block);
  expect(got.name).toEqual(img.name);
  expect(got.echo).toEqual(echo);
  expect(got.edited).toBe(true);
  expect(smsggdjSongCatalog.readWorkingSong!(ramWith(img, false))).toBe(null);
});

test("commitWorkingSong appends to a fresh cart, and marks the song as the loaded one", () => {
  const r = smsggdjSongCatalog.commitWorkingSong!(freshBattery(), image(7, "NEWSONG"));
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(listSongs(r.sav)).toEqual([{ index: 0, name: "NEWSONG" }]);
  expect(readSongBlock(r.sav, 0)).toEqual(block(7));
  expect(r.replaced).toBe(false);
  expect(curSlot(r.sav)).toBe(0);
});

test("commitWorkingSong writes back over the slot the song came from - never over a mere namesake", () => {
  const two = buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!;

  // Loaded from slot 1 (tracked): Save writes back there.
  const slot1 = smsggdjSongCatalog.trackSlot!(two, 1)!;
  const back = smsggdjSongCatalog.commitWorkingSong!(two, { ...image(9, "BETA"), slot: slot1 });
  if (!back.ok) throw new Error(`refused: ${back.reason}`);
  expect(back.replaced).toBe(true);
  expect(back.index).toBe(1);
  expect(listSongs(back.sav).length).toBe(2);
  expect(readSongBlock(back.sav, 1)).toEqual(block(9));
  expect(readSongBlock(back.sav, 0)).toEqual(block(1));

  // A NEW song the user happened to call BETA (no tracked slot): appended. Name matching used to write it
  // straight over the unrelated saved BETA, silently.
  const namesake = smsggdjSongCatalog.commitWorkingSong!(two, image(9, "BETA"));
  if (!namesake.ok) throw new Error(`refused: ${namesake.reason}`);
  expect(namesake.replaced).toBe(false);
  expect(readSongBlock(namesake.sav, 1)).toEqual(block(2)); // the old BETA survives

  // A tracked slot that no longer carries the song's name (renamed in the cart = "save as"): appended.
  const renamed = smsggdjSongCatalog.commitWorkingSong!(two, { ...image(9, "GAMMA"), slot: slot1 });
  if (!renamed.ok) throw new Error(`refused: ${renamed.reason}`);
  expect(renamed.replaced).toBe(false);
  expect(listSongs(renamed.sav).length).toBe(3);
});

test("an untitled song saved over and over stays ONE slot", () => {
  // The reported regression: a blank name matched nothing, so every save appended another copy until the
  // cart was full. Saving tracks the slot, and the next save writes back over it.
  let sav = freshBattery();
  let slot: TrackedSlot | undefined;
  for (let i = 0; i < 5; i++) {
    const r = smsggdjSongCatalog.commitWorkingSong!(sav, { ...image(10 + i, ""), slot });
    if (!r.ok) throw new Error(`refused: ${r.reason}`);
    sav = r.sav;
    slot = smsggdjSongCatalog.trackSlot!(sav, r.index)!;
  }
  expect(listSongs(sav).length).toBe(1);
  expect(readSongBlock(sav, 0)).toEqual(block(14));
});

test("a directory with a hole is packed like the cart packs it, not rebuilt without the songs after it", () => {
  // Executed against the old code: entry 0 cleared, songs at 1 and 2, and a save returned an image holding
  // only the new song. listSongs stops at a hole, so the heap rebuild dropped everything after it.
  const holed = buildSav([{ block: block(1), name: "A" }, { block: block(2), name: "B" }, { block: block(3), name: "C" }], CART)!;
  holed[0x20] = 0; // entry 0's valid byte
  expect(hasDirectoryHoles(holed)).toBe(true);
  expect(listSongs(packDirectory(holed))).toEqual([{ index: 0, name: "B" }, { index: 1, name: "C" }]);

  const r = smsggdjSongCatalog.commitWorkingSong!(holed, image(9, "NEW"));
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(listSongs(r.sav).map((x) => x.name)).toEqual(["B", "C", "NEW"]);
  expect(readSongBlock(r.sav, 0)).toEqual(block(2));
  expect(readSongBlock(r.sav, 1)).toEqual(block(3));

  // ...and the heap ops that cannot pack first refuse rather than rebuild a short list.
  expect(addSong(holed, block(9), "X")).toBe(null);
  // A song saved after the hole still counts as saved, so the working song is not wrongly unsaved.
  expect(smsggdjSongCatalog.workingSongImageDirty!(holed, image(3, "C"))).toBe(false);
});

test("a save persists the cart's OPTIONS, as config_save does", () => {
  const settings = Uint8Array.of(3, 1, 0, 1, 2, 5, 6);
  const r = smsggdjSongCatalog.commitWorkingSong!(freshBattery(), { ...image(7, "OPTS"), settings });
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  // "CF", the seven settings, their 8-bit sum - at $3F60 on a 32 KB cart.
  expect(r.sav.subarray(0x3f60, 0x3f6a)).toEqual(Uint8Array.of(0x43, 0x46, 3, 1, 0, 1, 2, 5, 6, 18));
});

test("commitWorkingSong says WHY it cannot: an old format, a damaged save, a full cart", () => {
  const smdj3 = freshBattery();
  smdj3.set([0x53, 0x4d, 0x44, 0x4a, 0x33], 0);
  expect(smsggdjSongCatalog.commitWorkingSong!(smdj3, image(1, "X"))).toEqual({ ok: false, reason: "format" });

  const damaged = buildSav([{ block: block(1), name: "ALPHA" }], CART)!;
  damaged[0x20 + 6] ^= 0xff; // slot 0's stored checksum
  expect(smsggdjSongCatalog.commitWorkingSong!(damaged, image(1, "X"))).toEqual({ ok: false, reason: "corrupt" });

  // An 8 KB cart is one bank with the config at $1F60: the worst-case reservation leaves room for one song.
  const small = buildSav([{ block: block(1), name: "ONLY" }], 8 * 1024)!;
  expect(smsggdjSongCatalog.commitWorkingSong!(small, image(2, "MORE"))).toEqual({ ok: false, reason: "full" });
});

// --- saving it into the LIVE battery -----------------------------------------------------------------

function smsProject() {
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const id = project.systems.addSystem("/roms/smsggdj.sms")!;
  be.setSram(id, freshBattery());
  be.seed("/roms/smsggdj.sav", freshBattery()); // battery mirrored: only the working song is unsaved
  project.save("/proj/p.rplg");
  return { be, project, systems: project.systems, id };
}

test("saveWorkingSong writes the song into the LIVE battery and tells the cart it is saved", async () => {
  // Into the live battery, not just the file: the teardown flush writes readSram to disk, so a file-only
  // save would be overwritten by the stale battery on the very close it was made for.
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(4, "LIVE")));
  const sys = systems.systems()[0];

  const r = await saveWorkingSong(be, systems, sys);
  expect(r).toEqual({ kind: "saved", index: 0, replaced: false });
  expect(listSongs(be.readSram(id)!)).toEqual([{ index: 0, name: "LIVE" }]);
  expect(readSongBlock(be.readSram(id)!, 0)).toEqual(block(4));
  expect(be.readRam(id)![layout.edited]).toBe(0); // the cart's own flag, cleared as its SAVE clears it
  expect(be.readFile("/roms/smsggdj.sav.bak")).toEqual(freshBattery()); // the pre-save battery, backed up

  expect(await saveWorkingSong(be, systems, sys)).toEqual({ kind: "clean" }); // nothing left to save
});

test("saveWorkingSong refuses, and changes nothing, when the cart cannot take the song", async () => {
  const { be, systems, id } = smsProject();
  const smdj3 = freshBattery();
  smdj3.set([0x53, 0x4d, 0x44, 0x4a, 0x33], 0);
  be.setSram(id, smdj3);
  be.setRam(id, ramWith(image(4, "LIVE")));
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "refused", reason: "format" });
  expect(be.readSram(id)).toEqual(smdj3);
  expect(be.readRam(id)![layout.edited]).toBe(1);
});

// --- the unsaved-changes prompt ----------------------------------------------------------------------

test("an unsaved working song is an unsaved change, named, with where Save will put it", () => {
  const { be, project, id } = smsProject();
  expect(hasUnsavedChanges(be, project)).toBe(false);

  be.setRam(id, ramWith(image(4, "MYSONG")));
  expect(hasUnsavedChanges(be, project)).toBe(true); // the close guard's gate
  const items = unsavedChanges(be, project);
  expect(items).toEqual([{ kind: "song", id, name: "MYSONG", savPath: "/roms/smsggdj.sav", newSlot: true }]);
  expect(unsavedLabel(items[0])).toBe("Song: MYSONG -> smsggdj.sav (new slot)");
});

test("the prompt says up front when Save cannot store the song, and why", () => {
  const { be, project, id } = smsProject();
  const smdj3 = freshBattery();
  smdj3.set([0x53, 0x4d, 0x44, 0x4a, 0x33], 0);
  be.setSram(id, smdj3);
  be.seed("/roms/smsggdj.sav", smdj3);
  be.setRam(id, ramWith(image(4, "")));
  const items = unsavedChanges(be, project);
  expect(items).toEqual([{ kind: "song", id, name: "", savPath: "/roms/smsggdj.sav", newSlot: true, blocked: "format" }]);
  expect(unsavedLabel(items[0])).toBe("Song: (untitled) (old save format, can't save)");
});

// --- a reset carries the song across the reboot ------------------------------------------------------

test("a reset carries an unsaved working song across the reboot and puts it back once the cart is up", () => {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const oldId = systems.addSystem("/roms/smsggdj.sms")!;
  const echo = Uint8Array.of(2, 3, 4, 0, 0, 0, 0, 0);
  be.setRam(oldId, ramWith(image(8, "CARRIED", true, echo)));

  const newId = systems.reset(oldId)!;
  expect(newId === oldId).toBe(false);
  expect(systems.hasCarries()).toBe(true);
  // While the new cart boots it holds nothing yet, but the song is not lost: the image answers for it, so
  // a close in this window still sees an unsaved song.
  be.setRam(newId, smsggdjRam({ booted: false }));
  expect(systems.workingSongImage(newId)!.block).toEqual(block(8));
  systems.settleCarries();
  expect(systems.hasCarries()).toBe(true); // not booted: still waiting, nothing written into the boot

  be.setRam(newId, smsggdjRam()); // the cart is up, blank, as song_new leaves it
  systems.settleCarries();
  expect(systems.hasCarries()).toBe(false);
  const ram = be.readRam(newId)!;
  expect(ram.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(8));
  expect(ram.subarray(layout.name, layout.name + 8)).toEqual(nameBytes("CARRIED"));
  expect(ram.subarray(layout.echo, layout.echo + 3)).toEqual(echo.subarray(0, 3));
  expect(ram[layout.edited]).toBe(1); // still unsaved after the reset - a reset is not a save
  expect(ram.subarray(layout.dirtyRows, layout.dirtyRows + layout.dirtyRowsLen).every((b) => b === 1)).toBe(true);
});

test("a song saved while it is being carried lands clean", () => {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const oldId = systems.addSystem("/roms/smsggdj.sms")!;
  be.setRam(oldId, ramWith(image(8, "CARRIED")));
  const newId = systems.reset(oldId)!;
  expect(systems.workingSongImage(newId)!.edited).toBe(true);
  systems.markCarriedSongSaved(newId);
  be.setRam(newId, smsggdjRam({ edited: true })); // a flag the restore must actively clear, not inherit
  systems.settleCarries();
  expect(be.readRam(newId)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(8)); // it DID land
  expect(be.readRam(newId)![layout.edited]).toBe(0);
});

test("a second reset inside the boot window carries the ORIGINAL song, not the blank one booting", () => {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const a = systems.addSystem("/roms/smsggdj.sms")!;
  be.setRam(a, ramWith(image(8, "CARRIED")));
  const b = systems.reset(a)!;
  be.setRam(b, smsggdjRam({ booted: false }));
  const c = systems.reset(b)!;
  be.setRam(c, smsggdjRam());
  systems.settleCarries();
  expect(be.readRam(c)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(8));
});

// --- the rows that REPLACE what the cart holds confirm first ------------------------------------------

function smsMenu() {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  stores.project.systems.loadRom("/roms/smsggdj.sms");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, freshBattery());
  const ctx = (): MenuContext => ({
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
  });
  const rows = (): MenuItem[] => {
    const sys = stores.project.systems.view().find((x) => x.id === id)!;
    const inst = buildInstanceMenu({ ...ctx(), system: sys }).items;
    return inst.find((i) => i.id === "inst-system")!.children!;
  };
  return { be, id, rows };
}

test("Load SRAM / Load State / New SRAM confirm only when an unsaved working song would be lost", () => {
  const { be, id, rows } = smsMenu();
  const kinds = () => ["sys-loadsram", "sys-loadstate", "sys-newsram"].map((r) => rows().find((i) => i.id === r)?.kind);

  be.setRam(id, ramWith(image(4, "MYSONG", false))); // a song, but nothing unsaved in it
  expect(kinds()).toEqual(["action", "action", "action"]);

  be.setRam(id, ramWith(image(4, "MYSONG"))); // edited, and in no slot
  expect(kinds()).toEqual(["prompt", "prompt", "prompt"]);
  const load = rows().find((i) => i.id === "sys-loadsram")!;
  expect(load.prompt!.confirm).toBe(true);
  expect(load.prompt!.title).toBe("Load SRAM? The working song has unsaved changes - they will be lost.");
});

test("an in-place rebuild keeps the system findable: currentId follows reset, and is null once removed", () => {
  // What keeps the instance menu on its cart when a keep-open row (FM Audio) resets it under a new id.
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const a = systems.addSystem("/roms/smsggdj.sms")!;
  const b = systems.reset(a)!;
  const c = systems.reset(b)!;
  expect(systems.currentId(a)).toBe(c);
  expect(systems.currentId(c)).toBe(c);
  systems.removeSystem(c);
  expect(systems.currentId(a)).toBe(null);
  expect(systems.currentId(12345)).toBe(null);
});

// --- tracking which slot the working song is ----------------------------------------------------------

test("Save tracks its slot, a menu Load tracks its slot, and an in-cart load re-syncs it", async () => {
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!);
  const sys = () => systems.systems()[0];

  // The cart loaded BETA on its own FILES screen: unedited, byte-identical to slot 1. The watcher sees it.
  be.setRam(id, ramWith(image(2, "BETA", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);

  // Edited: Save writes back over slot 1 instead of appending a second BETA.
  be.setRam(id, ramWith(image(5, "BETA")));
  expect(await saveWorkingSong(be, systems, sys())).toEqual({ kind: "saved", index: 1, replaced: true });
  expect(listSongs(be.readSram(id)!).length).toBe(2);
  expect(readSongBlock(be.readSram(id)!, 1)).toEqual(block(5));

  // A host-side Load of ALPHA retargets it to slot 0 - on the watcher's first look once it has landed (not
  // before: a reset or export ahead of the queued writes would carry the old song tracked to the new slot).
  expect(loadSongLive(be, systems, sys(), 0)).toBe(true);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);
});

test("a save made against a battery the cart wrote meanwhile is rebuilt, not written blind over it", async () => {
  // The image is built from a snapshot up to ~0.5 s old and replaces the whole battery. A cart that saved
  // a song itself in that window would have had it reverted.
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(4, "LIVE")));
  const real = be.writeSram.bind(be);
  let raced = false;
  be.writeSram = (i, off, bytes, expect) => {
    if (!raced) {
      raced = true; // the cart saves its own song an instant before ours lands
      be.setSram(id, buildSav([{ block: block(8), name: "CARTS" }], CART)!);
    }
    return real(i, off, bytes, expect);
  };
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "saved", index: 1, replaced: false });
  expect(listSongs(be.readSram(id)!).map((x) => x.name)).toEqual(["CARTS", "LIVE"]);
});

// --- second review: the tracked slot must still hold the song it was tracked as ------------------------

test("an in-cart FILES CLEAR that renumbers slots cannot make Save overwrite the song that moved in", () => {
  // Executed against the index + name rule: [untitled A, untitled B], A tracked at slot 0, A edited, the
  // user CLEARs slot 0 in the cart so B moves to 0 - and Save wrote the edited A over B.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "" }, { block: block(2), name: "" }], CART)!);
  be.setRam(id, ramWith(image(1, "", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);

  be.setRam(id, ramWith(image(5, ""))); // edited
  be.setSram(id, buildSav([{ block: block(2), name: "" }], CART)!); // the cart's own CLEAR of slot 0
  const r = smsggdjSongCatalog.commitWorkingSong!(be.readSram(id)!, systems.workingSongImage(id)!);
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(false); // appended
  expect(readSongBlock(r.sav, 0)).toEqual(block(2)); // B survives
});

test("an in-cart NEW drops the tracked slot, so saving the new song cannot overwrite the loaded one", () => {
  // Executed against the half-second poll: load untitled A, NEW in the cart, compose, Save - and A was
  // overwritten, because the tracked slot only ever moved on a MATCH and a fresh song matches nothing.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "" }], CART)!);
  be.setRam(id, ramWith(image(1, "", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);

  be.setRam(id, ramWith({ block: new Uint8Array(SMDJ4_BLOCK_LEN), name: nameBytes(""), echo: new Uint8Array(8), edited: false }));
  systems.observeWorkingSongs(); // the next frame: a clean song in no slot
  expect(systems.workingSongImage(id)!.slot).toBe(undefined);
});

test("a write that lands, then a cart write before the poll looks, still finishes the save", async () => {
  // Read as a conflict; the retry then found the song already saved and returned "clean" - leaving the
  // cart's UNSAVED flag set forever and the slot untracked, so the next save appended a duplicate.
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(4, "LIVE")));
  const real = be.writeSram.bind(be);
  let once = false;
  be.writeSram = (i, off, bytes, expect) => {
    const ok = real(i, off, bytes, expect);
    if (!once) {
      once = true;
      const after = be.readSram(id)!.slice();
      after[0x3f00] ^= 0x5a; // the cart touches its battery (outside the song heap) right after ours lands
      be.setSram(id, after);
    }
    return ok;
  };
  const r = await saveWorkingSong(be, systems, systems.systems()[0]);
  expect(r).toEqual({ kind: "saved", index: 0, replaced: false });
  expect(be.readRam(id)![layout.edited]).toBe(0);
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);
});

test("the tracked slot is persisted with the project, so a reopened session saves back over it", () => {
  // A DAW session reopens from a savestate with the song already edited, so nothing re-derives the slot;
  // in memory only, every reopen-edit-save cycle appended another copy.
  const { be, project, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  be.setRam(id, ramWith(image(1, "ALPHA", false)));
  systems.observeWorkingSongs();
  const zip = project.exportBytes()!;
  expect(project.loadBytes(zip).kind).toBe("loaded");
  const reopened = systems.systems()[0];
  expect(reopened.workingSlot!.index).toBe(0);
  expect(reopened.workingSlot!.fingerprint).toBe(smsggdjSongCatalog.trackSlot!(buildSav([{ block: block(1), name: "ALPHA" }], CART)!, 0)!.fingerprint);
});

test("a working song still being carried when the project is exported lands after the export is loaded", () => {
  // The savestate of a cart inside a reset's boot window holds the BLANK song; a DAW saving its session then
  // (or with the editor closed, so nothing settles it) lost the song on reopen.
  const { be, project, systems, id } = smsProject();
  be.setRam(id, ramWith(image(8, "CARRIED")));
  const newId = systems.reset(id)!;
  expect(systems.pendingCarry(newId) != null).toBe(true);
  const zip = project.exportBytes()!;

  expect(project.loadBytes(zip).kind).toBe("loaded");
  const reopened = systems.systems()[0].id;
  expect(systems.pendingCarry(reopened)!.block).toEqual(block(8));
  be.setRam(reopened, smsggdjRam());
  systems.settleCarries();
  expect(be.readRam(reopened)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(8));
  expect(be.readRam(reopened)![layout.edited]).toBe(1);
});

test("the archived working song round-trips byte for byte, and anything else decodes to nothing", () => {
  const song: WorkingSongImage = { ...image(3, "RT"), settings: Uint8Array.of(1, 2, 3, 4, 5, 6, 7), slot: { index: 4, fingerprint: 0xdeadbeef } };
  const back = decodeWorkingSong(encodeWorkingSong(song))!;
  expect(back.block).toEqual(song.block);
  expect(back.name).toEqual(song.name);
  expect(back.settings).toEqual(song.settings);
  expect(back.slot).toEqual(song.slot);
  expect(back.edited).toBe(true);
  const bad = encodeWorkingSong(song);
  bad[4] = 4; // a newer version
  expect(decodeWorkingSong(bad)).toBe(null);
  expect(decodeWorkingSong(encodeWorkingSong(song).subarray(0, 100))).toBe(null);
});

// --- third review ---------------------------------------------------------------------------------------

/** Find a menu row by id anywhere under `items`. */
function findRow(items: MenuItem[], id: string): MenuItem | undefined {
  for (const i of items) {
    if (i.id === id) return i;
    const hit = i.children ? findRow(i.children, id) : undefined;
    if (hit) return hit;
  }
  return undefined;
}

/** The audio thread, for a test: writeRam queued and applied only when the "block" runs. */
function deferRamWrites(be: MockBackend): () => void {
  const queue: [number, number, Uint8Array][] = [];
  const real = be.writeRam.bind(be);
  be.writeRam = (id, off, bytes) => {
    queue.push([id, off, bytes.slice()]);
    return true;
  };
  return () => {
    for (const [id, off, bytes] of queue.splice(0)) real(id, off, bytes);
  };
}

test("Load State drops the tracked slot, so Save cannot overwrite the song it used to name", () => {
  // Executed against 42233ca9: [untitled A, KEEP], A loaded and tracked at 0, Load State of an edited
  // untitled B - the slot survived the rebuild, both checks passed, and Save wrote B over A.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "" }, { block: block(2), name: "KEEP" }], CART)!);
  be.setRam(id, ramWith(image(1, "", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);
  be.seed("/states/b.ss0", new Uint8Array(16));
  const newId = systems.loadState(id, "/states/b.ss0")!;
  be.setRam(newId, ramWith(image(7, ""))); // the restored, edited B
  be.setSram(newId, be.readSram(id) ?? buildSav([{ block: block(1), name: "" }, { block: block(2), name: "KEEP" }], CART)!);
  const r = smsggdjSongCatalog.commitWorkingSong!(be.readSram(newId)!, systems.workingSongImage(newId)!);
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(false);
  expect(readSongBlock(r.sav, 0)).toEqual(block(1)); // A survives
});

test("a reset keeps the tracked slot through the frame its carry is still landing", () => {
  // Executed against 42233ca9: settleCarries queued the restore and observe ran the same frame, saw the
  // blank booted song (clean, in no slot), and cleared the slot just restored - the next Save appended.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!);
  be.setRam(id, ramWith(image(2, "BETA", false)));
  systems.observeWorkingSongs();
  be.setRam(id, ramWith(image(5, "BETA"))); // edited
  const newId = systems.reset(id)!;
  be.setSram(newId, be.readSram(id) ?? buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!);
  const block_ = deferRamWrites(be);
  be.setRam(newId, smsggdjRam()); // booted blank
  systems.settleCarries();
  systems.observeWorkingSongs(); // same frame: the writes have not landed
  block_(); // the audio block
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(newId)!.slot!.index).toBe(1);
  const r = smsggdjSongCatalog.commitWorkingSong!(be.readSram(newId)!, systems.workingSongImage(newId)!);
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(true);
  expect(listSongs(r.sav).length).toBe(2);
});

test("an echo-only edit is an unsaved change, and Save stores the echo", async () => {
  // The block is unchanged, and isSongSaved compared only the block - so an edit on the cart's ECHO
  // screen (which sets song_edited) read as saved: no prompt, Save did nothing, the echo was lost.
  const { be, systems, project, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  be.seed("/roms/smsggdj.sav", be.readSram(id)!);
  be.setRam(id, ramWith(image(1, "ALPHA", false)));
  systems.observeWorkingSongs();
  const echo = Uint8Array.of(2, 4, 8, 0, 0, 0, 0, 0);
  be.setRam(id, ramWith(image(1, "ALPHA", true, echo)));
  expect(hasUnsavedChanges(be, project)).toBe(true);
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "saved", index: 0, replaced: true });
  expect(readSongEcho(be.readSram(id)!, 0)).toEqual(echo);
});

test("the Songs menu still guards its rows while a reset is carrying the song back", () => {
  // Inside the boot window the cart holds nothing, and a RAM-only check made Move Up a plain action: it
  // cold-booted the cart, the carry's id died with the old core, and the song was gone.
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  stores.project.systems.loadRom("/roms/smsggdj.sms");
  const id = stores.project.systems.view()[0].id;
  const two = buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!;
  be.setSram(id, two);
  be.setRam(id, ramWith(image(5, "MINE")));
  const newId = stores.project.systems.reset(id)!;
  be.setSram(newId, two);
  be.setRam(newId, smsggdjRam({ booted: false }));
  const ctx: MenuContext = {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
  };
  const sys = stores.project.systems.view().find((x) => x.id === newId)!;
  const up = findRow(buildInstanceMenu({ ...ctx, system: sys }).items, "smsggdj-song-1-up");
  expect(up?.kind).toBe("prompt");
});

test("a Recent request for the last SAVED song still asks before replacing an unrelated unsaved one", () => {
  // cur_slot names the last song saved, and the no-op check read it instead of the cart: a request for
  // that song looked like "already loaded" and replaced the unsaved working song with no prompt.
  const { be, systems, id } = smsProject();
  be.setSram(id, setCurSlotFor(buildSav([{ block: block(1), name: "ALPHA" }], CART)!, 0)!);
  be.setRam(id, ramWith(image(9, "MINE")));
  expect(songLoadByNameWouldDiscard(systems, systems.systems()[0], "ALPHA")).toBe(true);
});

test("a song request waits for a reset's carry to land instead of being overwritten by it", () => {
  const { be, project, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  be.setRam(id, ramWith(image(5, "MINE")));
  const newId = systems.reset(id)!;
  be.setSram(newId, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  be.setRam(newId, smsggdjRam());
  project.requestSong({ name: "ALPHA", confirmed: false });
  expect(project.settleSong()).toBe("waiting"); // the carry has not landed
  systems.settleCarries();
  systems.observeWorkingSongs();
  // Landed: the carried MINE is the working song, unsaved - so the request now asks first.
  const r = project.settleSong();
  expect(typeof r === "object" && r.kind === "discard").toBe(true);
});

test("an export just after a carry lands still ships the song, since the savestate lags behind", () => {
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(8, "CARRIED")));
  const newId = systems.reset(id)!;
  be.setRam(newId, smsggdjRam());
  systems.settleCarries();
  expect(systems.pendingCarry(newId)).toBe(null);
  expect(systems.exportableWorkingSong(newId)!.block).toEqual(block(8)); // read live: the restore landed
});

// --- fourth review ----------------------------------------------------------------------------------------

test("while a carry is landing, Save, the prompt and an export all see the carried song, not the blank", async () => {
  // settleCarries dropped the carry before its queued writes applied, and everything but settleSong read
  // live RAM: an export shipped the blank as the song, the prompt listed nothing, Save did nothing.
  const { be, project, systems, id } = smsProject();
  be.setRam(id, ramWith(image(8, "CARRIED")));
  const newId = systems.reset(id)!;
  be.setSram(newId, freshBattery());
  be.seed("/roms/smsggdj.sav", freshBattery());
  be.setRam(newId, smsggdjRam()); // booted blank
  const flush = deferRamWrites(be);
  systems.settleCarries();
  expect(systems.exportableWorkingSong(newId)!.block).toEqual(block(8));
  expect(unsavedChanges(be, project).some((i) => i.kind === "song" && i.name === "CARRIED")).toBe(true);
  expect((await saveWorkingSong(be, systems, systems.systems()[0])).kind).toBe("saved");
  flush();
  expect(readSongBlock(be.readSram(newId)!, 0)).toEqual(block(8));
});

test("two slots with one block but different echo are different songs to the slot tracker", async () => {
  // An echo A/B saved in the cart: host Load of X, then the cart's own LOAD of Y. Echo was neither in the
  // change signal nor in the tracked-slot shortcut, so X stayed tracked and Save wrote Y's edits over X.
  const { be, systems, id } = smsProject();
  const echoX = Uint8Array.of(1, 2, 3, 0, 0, 0, 0, 0);
  const echoY = Uint8Array.of(2, 4, 8, 0, 0, 0, 0, 0);
  const sav = buildSav([{ block: block(1), name: "SONG", echo: echoX }, { block: block(1), name: "SONG", echo: echoY }], CART)!;
  be.setSram(id, sav);
  be.setRam(id, ramWith(image(1, "SONG", false, echoX)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0);
  be.setRam(id, ramWith(image(1, "SONG", false, echoY))); // the cart loads Y itself
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);
  be.setRam(id, ramWith(image(6, "SONG", true, echoY)));
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "saved", index: 1, replaced: true });
  expect(readSongEcho(be.readSram(id)!, 0)).toEqual(echoX); // X untouched
});

test("a slot saved with out-of-range echo still matches once loaded, raw or sanitized", () => {
  // buildSav / addSong write echo 0 by default (taps of 0, which the cart sanitizes to 1). The cart's
  // PROJECT load sanitizes and its FILES load does not, so RAM can hold either - and a raw compare never
  // matched the sanitized copy, so the song was untracked and the next Save appended a duplicate.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  for (const echo of [new Uint8Array(8), sanitizeEcho(new Uint8Array(8))]) {
    be.setRam(id, ramWith(image(1, "ALPHA", false, echo)));
    systems.observeWorkingSongs();
    expect(systems.workingSongImage(id)!.slot?.index).toBe(0);
  }
});

test("a tracked song shifted down by an in-cart CLEAR above it is written back where it went", () => {
  // Its fingerprint did not change, so nothing re-tracked it; the stale index failed and Save appended,
  // leaving the older copy behind as a duplicate.
  const two = buildSav([{ block: block(1), name: "A" }, { block: block(2), name: "B" }], CART)!;
  const tracked = smsggdjSongCatalog.trackSlot!(two, 1)!;
  const shifted = buildSav([{ block: block(2), name: "B" }], CART)!; // the cart CLEARed slot 0
  const r = smsggdjSongCatalog.commitWorkingSong!(shifted, { ...image(7, "B"), slot: tracked });
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r).toEqual({ ok: true, sav: r.sav, index: 0, replaced: true });
  expect(listSongs(r.sav).length).toBe(1);
});

test("a just-saved song read against a battery a block behind is re-checked, not given up on", () => {
  // The cart clears its unsaved flag in the routine that writes its battery; read a block apart, the
  // song matched no slot, and the cached verdict meant it was never looked at again.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!); // the OLD battery
  be.setRam(id, ramWith(image(4, "MINE", false))); // just saved by the cart
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot).toBe(undefined);
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }, { block: block(4), name: "MINE" }], CART)!);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot?.index).toBe(1);
});

// --- fifth review ---------------------------------------------------------------------------------------

test("an edit made the moment a carry lands is what Save stores - not the carried song", async () => {
  // Executed against 0ab09ee7: the window stayed open until RAM matched the CARRIED song, so an edit that
  // landed with the restore kept it open; Save then stored the pre-edit song and marked the edit saved.
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(8, "CARRIED")));
  const newId = systems.reset(id)!;
  be.setSram(newId, freshBattery());
  be.setRam(newId, smsggdjRam()); // booted blank
  const flush = deferRamWrites(be);
  systems.settleCarries();
  flush(); // the restore lands...
  be.setRam(newId, ramWith(image(9, "CARRIED"))); // ...and the user's first edit with it, before any frame
  expect(systems.workingSongImage(newId)!.block).toEqual(block(9));
  expect((await saveWorkingSong(be, systems, systems.systems()[0])).kind).toBe("saved");
  expect(readSongBlock(be.readSram(newId)!, 0)).toEqual(block(9));
});

test("relocation never picks a backup copy the deletions could not have moved the song to", () => {
  // Executed against 0ab09ee7: [A, X, A-backup], A tracked at 0 and edited, the cart CLEARs slot 0 -
  // Save "found" A at index 1 (the backup) and wrote the edit over it. One song gone can only have moved
  // things DOWN one slot; slot 0 itself was the deleted one, so there is nowhere A can have gone.
  const sav = buildSav([{ block: block(1), name: "A" }, { block: block(2), name: "X" }, { block: block(1), name: "A" }], CART)!;
  const tracked = smsggdjSongCatalog.trackSlot!(sav, 0)!;
  const cleared = buildSav([{ block: block(2), name: "X" }, { block: block(1), name: "A" }], CART)!;
  const r = smsggdjSongCatalog.commitWorkingSong!(cleared, { ...image(7, "A"), slot: tracked });
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(false);
  expect(readSongBlock(r.sav, 1)).toEqual(block(1)); // the backup survives
});

test("an echo A/B pair shifted down together: Save writes back to the variant that was loaded", () => {
  // Executed against 0ab09ee7: the slot check compared block + name only, so after a CLEAR above both,
  // the stale index (now the OTHER variant) passed and its echo was overwritten.
  const echoX = Uint8Array.of(1, 2, 3, 0, 0, 0, 0, 0);
  const echoY = Uint8Array.of(2, 4, 8, 0, 0, 0, 0, 0);
  const three = buildSav(
    [{ block: block(5), name: "X" }, { block: block(1), name: "A", echo: echoX }, { block: block(1), name: "A", echo: echoY }],
    CART,
  )!;
  const tracked = smsggdjSongCatalog.trackSlot!(three, 1)!; // the echoX variant
  const shifted = buildSav([{ block: block(1), name: "A", echo: echoX }, { block: block(1), name: "A", echo: echoY }], CART)!;
  const r = smsggdjSongCatalog.commitWorkingSong!(shifted, { ...image(7, "A", true, echoX), slot: tracked });
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.index).toBe(0); // where echoX went
  expect(readSongEcho(r.sav, 1)).toEqual(echoY); // the other variant untouched
});

test("a reset in the blocks between the cart writing a loaded song and clearing its flag carries it clean", () => {
  // The cart writes the song it LOADS, then clears song_edited a few blocks later. A reset in between
  // carried it as edited, with the stale slot - and the new cart never clears that flag, so it read
  // UNSAVED forever and the next Save appended a duplicate.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "LOW" }, { block: block(2), name: "HIGH" }], CART)!);
  be.setRam(id, ramWith(image(2, "HIGH", false)));
  systems.observeWorkingSongs(); // tracks HIGH at 1
  be.setRam(id, ramWith(image(1, "LOW", true))); // the cart's FILES LOAD of LOW, flag not cleared yet
  const newId = systems.reset(id)!;
  const carried = systems.pendingCarry(newId)!;
  expect(carried.edited).toBe(false); // carried clean...
  // ...and once it lands, the watcher's first clean look retargets it to the slot it came from. (The carry
  // itself does not move the slot: an edit passing through an older copy also looks "identical".)
  be.setSram(newId, buildSav([{ block: block(1), name: "LOW" }, { block: block(2), name: "HIGH" }], CART)!);
  be.setRam(newId, smsggdjRam());
  systems.settleCarries();
  systems.observeWorkingSongs();
  expect(be.readRam(newId)![layout.edited]).toBe(0);
  expect(systems.workingSongImage(newId)!.slot!.index).toBe(0);
});

test("the Songs menu offers Save Working Song & Load for a work-RAM song, and does it live", async () => {
  // It used to read the battery's cur_slot: a "[working]" row named the last SAVED song with an empty
  // submenu, and the guard said "no free slot" on a cart with thirty.
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  stores.project.systems.loadRom("/roms/smsggdj.sms");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, setCurSlotFor(buildSav([{ block: block(1), name: "LOW" }, { block: block(2), name: "HIGH" }], CART)!, 0)!);
  be.setRam(id, ramWith(image(9, "HIGH")));
  const ctx: MenuContext = {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
  };
  const items = buildInstanceMenu({ ...ctx, system: stores.project.systems.view()[0] }).items;
  const songs = findRow(items, "smsggdj-songs")!.children!;
  expect(songs.some((r) => r.label.includes("[working]"))).toBe(false);
  const save = findRow(items, "smsggdj-song-0-load-save")!;
  expect(save.disabled ?? false).toBe(false);
  save.onSelect!();
  // The save waits for the working song to hold still (a few blocks), then writes and lands.
  await new Promise<void>((r) => (globalThis as unknown as { setTimeout: (f: () => void, ms: number) => void }).setTimeout(r, 300));
  expect(listSongs(be.readSram(id)!).map((x) => x.name)).toEqual(["LOW", "HIGH", "HIGH"]); // saved (untracked: appended)
  expect(be.readRam(id)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(1)); // and LOW loaded, live
});

// --- sixth review ---------------------------------------------------------------------------------------

test("a Songs-menu load settles into the instance whose menu it was, not whichever is focused by then", () => {
  // Real app, executed against f50e0ceb: the request parked while instance 2 rebooted, focus moved to
  // instance 1, and the confirmed load landed THERE - replacing instance 1's unsaved song, unprompted.
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const one = project.systems.addSystem("/roms/smsggdj.sms")!;
  const two = project.systems.addSystem("/roms/smsggdj.sms")!;
  const sav = buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!;
  be.setSram(one, sav);
  be.setSram(two, sav);
  be.setRam(one, ramWith(image(9, "ONE")));
  be.setRam(two, ramWith(image(8, "TWO", false)));
  project.systems.setFocus(two);
  project.requestSong({ index: 1, confirmed: true, systemId: two });
  project.systems.setFocus(one); // focus moves before it settles
  expect(project.settleSong()).toBe("loaded");
  expect(be.readRam(two)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(2)); // BETA, into instance 2
  expect(be.readRam(one)!.subarray(0, SMDJ4_BLOCK_LEN)).toEqual(block(9)); // ONE untouched
});

test("an edit passing through an older copy's bytes for one frame does not move the tracked slot", async () => {
  // Executed against f50e0ceb: [SONG v1, SONG v2], v2 loaded (tracked 1); one observed frame of the edit
  // equal to v1 counted as "identical, so clean", retargeted to 0, and Save then overwrote v1 with v3.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "SONG" }, { block: block(2), name: "SONG" }], CART)!);
  be.setRam(id, ramWith(image(2, "SONG", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);
  be.setRam(id, ramWith(image(1, "SONG", true))); // the edit passes through v1's value
  systems.observeWorkingSongs();
  be.setRam(id, ramWith(image(3, "SONG", true))); // and goes on to v3
  systems.observeWorkingSongs();
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "saved", index: 1, replaced: true });
  expect(readSongBlock(be.readSram(id)!, 0)).toEqual(block(1)); // v1 survives
});

test("playback rewriting the groove table does not drop the tracked slot", () => {
  // The cart's `T` command rewrites the active groove inside the block during play without setting the
  // unsaved flag; the clean song then matched no slot and its tracking was dropped, so the next Save
  // appended a copy instead of writing back.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART)!);
  be.setRam(id, ramWith(image(1, "ALPHA", false)));
  systems.observeWorkingSongs();
  const played = ramWith(image(1, "ALPHA", false));
  played[0x1a00 + 3] ^= 0x07; // a groove tick, rewritten by playback...
  played[layout.playState] = 1; // ...which only happens while the transport runs
  be.setRam(id, played);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot?.index).toBe(0);

  // ...and the same groove-only change on a STOPPED cart is not drift (an in-cart NEW over a groove
  // template looks exactly like it) - it drops the tracking.
  const stopped = ramWith(image(1, "ALPHA", false));
  stopped[0x1a00 + 4] ^= 0x07;
  be.setRam(id, stopped);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot).toBe(undefined);
  be.setRam(id, played);
  systems.observeWorkingSongs();

  // ...whereas an in-cart NEW (a different song, still clean) does drop it.
  be.setRam(id, ramWith(image(3, "", false)));
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot).toBe(undefined);
});

test("a cleared original never sends Save to a backup copy directly above it", () => {
  // Executed against f50e0ceb: [X, A-backup, A], A tracked at 2, the cart CLEARs slot 2 - the count bound
  // opened a window over slot 1 and the edit was written over the backup.
  const sav = buildSav([{ block: block(5), name: "X" }, { block: block(1), name: "A" }, { block: block(1), name: "A" }], CART)!;
  const tracked = smsggdjSongCatalog.trackSlot!(sav, 2)!;
  const cleared = buildSav([{ block: block(5), name: "X" }, { block: block(1), name: "A" }], CART)!;
  const r = smsggdjSongCatalog.commitWorkingSong!(cleared, { ...image(7, "A"), slot: tracked });
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(false);
  expect(readSongBlock(r.sav, 1)).toEqual(block(1)); // the backup survives
});

test("a CONT-only change is unsaved work, and Save writes just the options, not a second copy", async () => {
  // The cart saves CONT in its OPTIONS block and sets song_edited for it, but the block does not change -
  // so the host said "nothing to save" while the cart said UNSAVED, forever.
  const { be, project, systems, id } = smsProject();
  const opts = Uint8Array.of(0x43, 0x46, 0, 0, 0, 0, 0, 0x0e, 3, 0x11); // "CF", cont=0
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }], CART, opts)!);
  be.seed("/roms/smsggdj.sav", be.readSram(id)!);
  const song = { ...image(1, "ALPHA", true), settings: Uint8Array.of(0, 0, 0, 0, 2, 0x0e, 3) }; // cont=2
  const ram = ramWith(song);
  const offsets = commonConfigOffsets()!;
  offsets.forEach((o, i) => (ram[o] = song.settings[i]));
  be.setRam(id, ram);
  expect(hasUnsavedChanges(be, project)).toBe(true);
  expect(await saveWorkingSong(be, systems, systems.systems()[0])).toEqual({ kind: "saved", index: 0, replaced: true });
  expect(listSongs(be.readSram(id)!).length).toBe(1);
  expect(be.readSram(id)!.subarray(0x3f60 + 2, 0x3f60 + 9)).toEqual(song.settings);
  expect(be.readRam(id)![layout.edited]).toBe(0);
});

// --- the fuzzer -------------------------------------------------------------------------------------------

test("an unedited song the cart saves to a new row moves the tracking there, battery change alone", async () => {
  // Found by the randomized cart model: the watcher keyed only on the WORKING song, which a save of an
  // unedited song does not change - so tracking stayed on the old row and the next host Save wrote there.
  // With playback drift first, that destroyed the original row's groove table.
  const { be, systems, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "" }], CART)!);
  be.setRam(id, ramWith(image(1, "", false)));
  systems.observeWorkingSongs(); // tracked at 0
  const drifted = ramWith(image(1, "", false));
  drifted[0x1a00 + 5] ^= 0x03; // playback rewrote a groove tick
  drifted[layout.playState] = 1;
  be.setRam(id, drifted);
  systems.observeWorkingSongs(); // drift: tracking held at 0
  // The cart's own SAVE to a new row: the battery grows, the working song does not change.
  be.setSram(id, buildSav([{ block: block(1), name: "" }, { block: drifted.slice(0, SMDJ4_BLOCK_LEN), name: "" }], CART)!);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);
  const edited = drifted.slice();
  edited[layout.edited] = 1;
  edited[7] ^= 0x01;
  be.setRam(id, edited);
  expect((await saveWorkingSong(be, systems, systems.systems()[0])).kind).toBe("saved");
  expect(readSongBlock(be.readSram(id)!, 0)).toEqual(block(1)); // the original, groove table and all
});

test("a re-carried song the cart already holds opens no landing window", () => {
  // Found by the fuzzer: exported within 2 s of a carry landing, the project re-carries a song its
  // savestate already holds; the window then keyed on the song's own signal and swallowed the cart's next
  // same-content LOAD for up to 120 frames.
  const { be, systems, id } = smsProject();
  be.setRam(id, ramWith(image(8, "SAME")));
  const newId = systems.reset(id)!;
  be.setRam(newId, ramWith(image(8, "SAME"))); // already there when it settles
  systems.settleCarries();
  expect(systems.isRestoringSong(newId)).toBe(false);
});

// --- the extended fuzzer -----------------------------------------------------------------------------------

test("playback drift onto an older version's exact tempo keeps tracking the version being played", async () => {
  // v1 (slot 0) and v2 (slot 1) differ only in their groove table. Playing v2, a `T` command sets v1's
  // tempo: the song is now byte-identical to v1, which a plain match took for an in-cart LOAD of v1 - so
  // the next edit + Save overwrote v1. The directory did not move, so it was playback.
  const { be, systems, id } = smsProject();
  const v1 = block(1);
  const v2 = block(1);
  v2[0x1a00 + 2] = 0x33;
  be.setSram(id, buildSav([{ block: v1, name: "SONG" }, { block: v2, name: "SONG" }], CART)!);
  const ram = ramWith({ block: v2, name: nameBytes("SONG"), echo: new Uint8Array(8), edited: false });
  be.setRam(id, ram);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);
  const playing = ramWith({ block: v1, name: nameBytes("SONG"), echo: new Uint8Array(8), edited: false });
  playing[layout.playState] = 1;
  be.setRam(id, playing);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1);
});

test("a re-asked menu load still asks when the working song merely shares the target's name", () => {
  // A parked Songs-menu load lost its confirmation when its cart was rebuilt (a Load State restored
  // unsaved work), and was then re-checked with the by-NAME test: the working song "A" and the target "A"
  // were taken for "already loaded, nothing to lose" though they were different songs.
  const { be, project, id } = smsProject();
  be.setSram(id, buildSav([{ block: block(1), name: "A" }], CART)!);
  be.setRam(id, ramWith(image(5, "A"))); // unsaved, and named like slot 0
  project.requestSong({ index: 0, confirmed: false, systemId: id });
  const r = project.settleSong();
  expect(typeof r === "object" && r.kind === "discard").toBe(true);
});

test("an explicit Load of a row the cart already matches tracks that row, not a twin elsewhere", () => {
  // Found by the fuzzer: playing v2 (tracked 1), drift made the song byte-identical to row 0, the user then
  // Loaded row 0 from the Songs menu - and once the load landed nothing told the tracking, which drift kept
  // on row 1. The Load's own target now becomes the tracking when its writes are seen to land.
  const { be, systems, id } = smsProject();
  const v1 = block(1);
  const v2 = block(1);
  v2[0x1a00 + 2] = 0x33;
  be.setSram(id, buildSav([{ block: v1, name: "SONG" }, { block: v2, name: "SONG" }], CART)!);
  be.setRam(id, ramWith({ block: v2, name: nameBytes("SONG"), echo: sanitizeEcho(new Uint8Array(8)), edited: false }));
  systems.observeWorkingSongs();
  const playing = ramWith({ block: v1, name: nameBytes("SONG"), echo: sanitizeEcho(new Uint8Array(8)), edited: false });
  playing[layout.playState] = 1;
  be.setRam(id, playing);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(1); // drift: still v2
  expect(loadSongLive(be, systems, systems.systems()[0], 0)).toBe(true);
  systems.observeWorkingSongs();
  expect(systems.workingSongImage(id)!.slot!.index).toBe(0); // the user asked for row 0
});
