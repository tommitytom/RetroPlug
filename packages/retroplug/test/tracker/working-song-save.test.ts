// A working song that lives OUTSIDE the battery (smsggdj's work RAM) is in neither the project file nor
// the `.sav`, so until now three things threw it away without a word: closing the window, a reset, and the
// rows that replace what the cart holds. This file covers the host side of all three over the mock
// backend - the codec and catalog pieces, saving the song INTO the battery, the unsaved-changes list, and
// a reset carrying the song across the reboot. The real-core proofs are test-native/sms-working-song-save.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { SystemsStore } from "../../src/systemsStore";
import { buildAppRegistry } from "../../src/appHost";
import { hasUnsavedChanges, unsavedChanges } from "../../src/unsavedChanges";
import { unsavedLabel } from "../../ui/lvgl/unsavedRows";
import { saveWorkingSong } from "../../src/tracker/liveSav";
import { smsggdjSongCatalog } from "../../src/tracker/smsggdjSongCatalog";
import { resolveSmsggdjLayout } from "../../src/smsggdj/runtime/layout";
import {
  addSong,
  buildSav,
  ensureSongDirectory,
  isSmsggdjSav,
  listSongs,
  readSongBlock,
  readSongEcho,
  curSlot,
  SMDJ4_BLOCK_LEN,
} from "../../src/smsggdj/codec/sav";
import type { WorkingSongImage } from "../../src/tracker/songCatalog";
import { smsggdjRom, smsggdjRam } from "../systems/fixtures";
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
const image = (tag: number, name: string, edited = true, echo = new Uint8Array(8)): WorkingSongImage => ({
  block: block(tag),
  name: nameBytes(name),
  echo,
  edited,
});

/** A battery the cart has never saved into: no SMDJ4 directory at all. What every new user starts with. */
const freshBattery = (): Uint8Array => new Uint8Array(CART).fill(0xff);

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
  expect(raw[0]).toBe(0xff); // a NEW image: the input is never mutated
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

test("commitWorkingSong overwrites the ONE slot with the same name, and appends when that is ambiguous", () => {
  const two = buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!;
  const r = smsggdjSongCatalog.commitWorkingSong!(two, image(9, "BETA"));
  if (!r.ok) throw new Error(`refused: ${r.reason}`);
  expect(r.replaced).toBe(true);
  expect(r.index).toBe(1);
  expect(listSongs(r.sav).length).toBe(2); // replaced, not duplicated
  expect(readSongBlock(r.sav, 1)).toEqual(block(9));
  expect(readSongBlock(r.sav, 0)).toEqual(block(1)); // the other song untouched

  // Two slots called BETA: no evidence which one is meant, and overwriting has no undo - so append.
  const dup = buildSav([{ block: block(1), name: "BETA" }, { block: block(2), name: "BETA" }], CART)!;
  const d = smsggdjSongCatalog.commitWorkingSong!(dup, image(9, "BETA"));
  if (!d.ok) throw new Error(`refused: ${d.reason}`);
  expect(d.replaced).toBe(false);
  expect(listSongs(d.sav).length).toBe(3);

  // A blank name is no identity at all: it never claims a slot, even a blank-named one.
  const blank = buildSav([{ block: block(1), name: "" }], CART)!;
  const b = smsggdjSongCatalog.commitWorkingSong!(blank, image(9, ""));
  if (!b.ok) throw new Error(`refused: ${b.reason}`);
  expect(b.replaced).toBe(false);
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
  expect(items).toEqual([{ kind: "song", id, name: "MYSONG", savPath: "/roms/smsggdj.sav" }]);
  expect(unsavedLabel(items[0])).toBe("Song: MYSONG -> smsggdj.sav");
});

test("the prompt says up front when Save cannot store the song, and why", () => {
  const { be, project, id } = smsProject();
  const smdj3 = freshBattery();
  smdj3.set([0x53, 0x4d, 0x44, 0x4a, 0x33], 0);
  be.setSram(id, smdj3);
  be.seed("/roms/smsggdj.sav", smdj3);
  be.setRam(id, ramWith(image(4, "")));
  const items = unsavedChanges(be, project);
  expect(items).toEqual([{ kind: "song", id, name: "", savPath: "/roms/smsggdj.sav", blocked: "format" }]);
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
