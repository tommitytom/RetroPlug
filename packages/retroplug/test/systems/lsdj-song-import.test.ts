// importSongFiles — the batch importer behind the Songs menu Add and drag-and-drop: fold multiple
// .lsdsng/.lsdprj into a live LSDj SAV in ONE readSram→write→loadSram cycle. Byte-level; .lsdprj kits
// accumulate as lsdj-assets overrides. Routing (which drops reach here) is in test/file-drop/route.test.ts.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole, readOverrides } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { importSongFiles } from "../../src/lsdjSongImport";
import { savFrom, encodeLsdsngRaw, encodeLsdprj, decompressSlot, listProjects } from "../../src/lsdjSav";
import { LsdjRom, buildKitBank, ROM_SIZE } from "../../src/lsdj/rom";
import { deepEqual } from "../lsdj/_assert";
import { gbRomBattery } from "./fixtures";

function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.1.C";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}
const song = (tempo: number, sentinel: number): Uint8Array => {
  const raw = savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();
  raw[0x1730] = sentinel;
  return raw;
};

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerLsdjAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("dropping a .lsdsng + a .lsdprj adds both songs in ONE reboot; kits ride overrides", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({})); // blank live battery

  const aSong = song(100, 0x11); // a plain .lsdsng (no kits) — stays byte-exact
  // a .lsdprj whose song references a kit (so the bundled bank imports as an override + the refs remap)
  const bSong = savFrom({ workingSong: { settings: { tempo: 140 }, instruments: [{ type: "kit", kit1: 5, kit2: 5 }] } }).subarray(0, 0x8000);
  be.seed("/s/a.lsdsng", encodeLsdsngRaw("AAA", 1, aSong));
  be.seed("/s/b.lsdprj", encodeLsdprj({ name: "BBB", version: 2, songBytes: bSong, kitBanks: [buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }])] }));

  const before = be.constructCalls.length;
  const sys = store.view().find((s) => s.id === id)!;
  expect(importSongFiles(be, store, sys, ["/s/a.lsdsng", "/s/b.lsdprj"])).toBeTruthy();

  // Exactly one rebuild for the whole batch.
  expect(be.constructCalls.length - before).toBe(1);
  const spec = be.constructCalls[be.constructCalls.length - 1];
  const sav = spec.sramBytes!;
  expect(listProjects(sav).map((p) => p.slot)).toEqual([0, 1]); // both songs present
  deepEqual([...decompressSlot(sav, 0)!], [...aSong], ".lsdsng byte-exact in slot 0");
  deepEqual([...sav.subarray(0, 0x8000)], [...decompressSlot(sav, 1)!], "working memory = the LAST dropped song (slot 1)");

  // The .lsdprj's kit was recorded as an lsdj-assets override (linking the .lsdprj by path), and the
  // rebuild's effective ROM carries it.
  const ov = readOverrides(store.view().find((s) => s.romPath === "/roms/song.gb")!.roles.find((r) => r.kind === "lsdj-assets")?.config);
  expect(ov.some((o) => o.type === "kit" && o.path === "/s/b.lsdprj" && o.lsdprjKit === 0)).toBeTruthy();
  expect(LsdjRom.fromBytes(spec.romBytes!).kit(0).name()).toBe("DRUMS");
});

test("importSongFiles is a no-op (no write) for an empty / all-bad list", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({}));
  const before = be.constructCalls.length;
  const sys = store.view().find((s) => s.id === id)!;
  expect(importSongFiles(be, store, sys, [])).toBe(false);
  expect(importSongFiles(be, store, sys, ["/nope.lsdsng"])).toBe(false); // unreadable
  expect(be.constructCalls.length).toBe(before); // never rebooted
});
