// The Songs pipeline is BYTE-LEVEL: operations edit the raw SAV and never round-trip song data through the
// Song model (the model is only a lossless byte round-trip WITH a template, which stored projects don't get,
// so the old model-based path silently corrupted ~300 bytes/song → wrong instruments). These tests prove the
// edits AND the key regression: an op must leave every untouched song byte-identical.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { savFrom, injectSong, decompressSlot, listProjects, encodeLsdsngRaw, loadSongToWorking } from "../../src/lsdjSav";
import { deleteSongInSav, addLsdsngToSav, replaceSongInSav, importAllSongsFromSav } from "../../src/lsdjSongOps";
import { deepEqual } from "../lsdj/_assert";
import { gbRomBattery } from "./fixtures";

// A valid 0x8000 song with a sentinel byte poked into a region the Song MODEL drops (first no-template diff
// is 0x1730) — so a model round-trip without a template (the OLD encodeSav bug) would clobber it, but a
// byte-level op preserves it exactly.
function songWithSentinel(tempo: number, sentinel: number): Uint8Array {
  const raw = savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();
  raw[0x1730] = sentinel;
  return raw;
}

// A blank sav with two raw songs injected byte-level at slots 0 and 3.
function twoSongSav(): Uint8Array {
  let sav = savFrom({});
  sav = injectSong(sav, 0, "AAA", 1, songWithSentinel(100, 0x11))!;
  sav = injectSong(sav, 3, "BBB", 2, songWithSentinel(140, 0x22))!;
  return sav;
}

// --- pure byte-level ops ------------------------------------------------------------------------------
test("byte-level ops never corrupt an untouched song (the model-corruption regression)", () => {
  const sav = twoSongSav();
  const a0 = decompressSlot(sav, 0)!;
  expect(a0[0x1730]).toBe(0x11); // sentinel present

  const afterDel = deleteSongInSav(sav, 3);
  deepEqual([...decompressSlot(afterDel, 0)!], [...a0], "slot 0 byte-identical after delete");
  expect(decompressSlot(afterDel, 3)).toBe(null);

  const afterLoad = loadSongToWorking(sav, 3)!;
  deepEqual([...decompressSlot(afterLoad, 0)!], [...a0], "slot 0 byte-identical after load");
  deepEqual([...afterLoad.subarray(0, 0x8000)], [...decompressSlot(sav, 3)!], "working memory == slot 3, byte-exact");
  expect(afterLoad[0x8140]).toBe(3); // active project
});

test("addLsdsngToSav / replaceSongInSav are byte-exact; importAllSongsFromSav packs free slots", () => {
  const sav = twoSongSav();
  const song = songWithSentinel(120, 0x33);
  const file = encodeLsdsngRaw("NEW", 7, song);

  const added = addLsdsngToSav(sav, file)!;
  expect(listProjects(added).map((p) => p.slot)).toEqual([0, 1, 3]); // filled the first gap
  deepEqual([...decompressSlot(added, 1)!], [...song], "added song byte-exact");
  deepEqual([...decompressSlot(added, 0)!], [...decompressSlot(sav, 0)!], "existing slot 0 untouched");
  deepEqual([...added.subarray(0, 0x8000)], [...song], "added song loaded into working memory");
  expect(added[0x8140]).toBe(1); // + made the active project so the reboot shows it

  const replaced = replaceSongInSav(sav, 0, file)!;
  deepEqual([...decompressSlot(replaced, 0)!], [...song], "replaced song byte-exact");
  deepEqual([...decompressSlot(replaced, 3)!], [...decompressSlot(sav, 3)!], "other slot untouched");

  const merged = importAllSongsFromSav(savFrom({}), sav);
  expect(listProjects(merged).map((p) => p.name)).toEqual(["AAA", "BBB"]);
  deepEqual([...decompressSlot(merged, 0)!], [...decompressSlot(sav, 0)!], "imported song A byte-exact");
});

test("deleteSongInSav clears the active pointer when it matched the deleted slot", () => {
  const sav = twoSongSav();
  sav[0x8140] = 3; // active = slot 3
  expect(deleteSongInSav(sav, 3)[0x8140]).toBe(0xff);
  expect(deleteSongInSav(sav, 0)[0x8140]).toBe(3); // deleting a different slot leaves it
});

// --- store write-back path (what the menu does) -------------------------------------------------------
function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerLsdjAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

// Mirror menuDefs.mutateSavBytes: readSram → byte-level op → writeFileAtomic(resolved .sav) → loadSram.
function mutate(be: MockBackend, store: SystemsStore, id: number, romPath: string, fn: (sav: Uint8Array) => Uint8Array | null): number {
  const bytes = store.readSram(id)!;
  const out = fn(bytes)!;
  const target = `${romPath.replace(/\.gb$/, "")}.sav`;
  be.writeFileAtomic(target, out);
  return store.loadSram(id, target)!;
}

test("Delete via the store path hands native the edited SAV (song removed)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", gbRomBattery());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, twoSongSav());

  mutate(be, store, id, "/roms/song.gb", (sav) => deleteSongInSav(sav, 0));

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.sramBytes != null).toBeTruthy(); // rebuilt from the edited SRAM
  expect(decompressSlot(spec.sramBytes!, 0)).toBe(null); // AAA deleted
  expect(listProjects(spec.sramBytes!).map((p) => p.slot)).toEqual([3]); // BBB kept
  expect(listProjects(be.readFile("/roms/song.sav")!).map((p) => p.slot)).toEqual([3]); // + on disk
});

test("Load via the store path boots from the loaded song; other slot untouched", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", gbRomBattery());
  const id = store.addSystem("/roms/song.gb")!;
  const seed = twoSongSav();
  be.setSram(id, seed);

  mutate(be, store, id, "/roms/song.gb", (sav) => loadSongToWorking(sav, 3)); // load BBB

  const spec = be.constructCalls[be.constructCalls.length - 1];
  deepEqual([...spec.sramBytes!.subarray(0, 0x8000)], [...decompressSlot(seed, 3)!], "working memory = slot 3, byte-exact");
  expect(spec.sramBytes![0x8140]).toBe(3);
  deepEqual([...decompressSlot(spec.sramBytes!, 0)!], [...decompressSlot(seed, 0)!], "slot 0 untouched");
});

test("Add a .lsdsng via the store path lands it in the first free slot", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", gbRomBattery());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, twoSongSav());

  const file = encodeLsdsngRaw("ADD", 7, songWithSentinel(120, 0x44));
  mutate(be, store, id, "/roms/song.gb", (sav) => addLsdsngToSav(sav, file));

  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(listProjects(spec.sramBytes!).map((p) => p.slot)).toEqual([0, 1, 3]); // slot 1 filled
  deepEqual([...decompressSlot(spec.sramBytes!, 1)!], [...songWithSentinel(120, 0x44)], "added song byte-exact");
  // Boots into the newly-added song (working memory + active project), not the old one.
  deepEqual([...spec.sramBytes!.subarray(0, 0x8000)], [...songWithSentinel(120, 0x44)], "reboot shows the added song");
  expect(spec.sramBytes![0x8140]).toBe(1);
});
