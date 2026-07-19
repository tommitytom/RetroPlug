// Data-safety for the .lsdprj import path: planLsdprjImport (pure) must return null on ANY failure without
// mutating its inputs (the caller then writes nothing), and importSongFiles (the store orchestrator) must
// never clobber the live SAV / role config on a skip, a partial batch, or a failed disk write.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { planLsdprjImport } from "../../src/lsdjLsdprjImport";
import { importSongFiles } from "../../src/lsdjSongImport";
import { savFrom, encodeLsdprj, encodeLsdsngRaw, injectSong, listProjects, decompressSlot } from "../../src/lsdjSav";
import { LsdjRom, buildKitBank, ROM_SIZE, KIT_COUNT } from "../../src/lsdj/rom";
import { readOverrides, type LsdjAssetOverride } from "../../src/lsdjAssetsRole";
import { gbRomBattery } from "./fixtures";
import { sameBytes } from "../_bytes";

// A 1 MiB LSDj ROM (all kit slots free).
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}
// A .lsdprj whose song references kit index 5, bundling one custom "DRUMS" kit bank.
function lsdprjWithKit(): Uint8Array {
  const song = savFrom({ workingSong: { instruments: [{ type: "kit", kit1: 5, kit2: 5 }] } }).subarray(0, 0x8000);
  const kit = buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }]);
  return encodeLsdprj({ name: "MYPRJ", version: 0x40, songBytes: song, kitBanks: [kit] });
}
const rawSong = (tempo: number) => savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerLsdjAssetsRole(reg);
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

// --- planLsdprjImport failure paths (pure — null + inputs untouched = the caller writes nothing) --------

test("planLsdprjImport returns null on a malformed .lsdprj and never mutates its inputs", () => {
  const liveSram = savFrom({});
  const before = liveSram.slice();
  const overrides: LsdjAssetOverride[] = [];
  expect(planLsdprjImport({ file: new Uint8Array([1, 2, 3]), path: "/bad.lsdprj", effectiveRom: lsdjRom1M(), overrides, liveSram })).toBe(null);
  expect(sameBytes(liveSram, before)).toBe(true);
  expect(overrides.length).toBe(0);
});

test("planLsdprjImport returns null when the effective ROM isn't LSDj", () => {
  const notLsdj = new Uint8Array(ROM_SIZE); // no logo / version title
  expect(planLsdprjImport({ file: lsdprjWithKit(), path: "/p.lsdprj", effectiveRom: notLsdj, overrides: [], liveSram: savFrom({}) })).toBe(null);
});

test("planLsdprjImport returns null when the ROM is out of free kit slots", () => {
  const rom = LsdjRom.fromBytes(lsdjRom1M());
  for (let k = 0; k < KIT_COUNT; k++) rom.importKitFile(k, buildKitBank(`K${k}`, [{ name: "X", bytes: Uint8Array.of(k + 1) }])); // fill all 51
  expect(planLsdprjImport({ file: lsdprjWithKit(), path: "/p.lsdprj", effectiveRom: rom.bytes(), overrides: [], liveSram: savFrom({}) })).toBe(null);
});

test("planLsdprjImport returns null when the SAV is out of song slots (no targetSlot)", () => {
  let full = savFrom({});
  for (let i = 0; i < 32; i++) full = injectSong(full, i, `S${i}`, 1, rawSong(60))!; // fill all 32 slots
  const before = full.slice();
  expect(planLsdprjImport({ file: lsdprjWithKit(), path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: full })).toBe(null);
  expect(sameBytes(full, before)).toBe(true); // the full SAV is untouched
});

test("planLsdprjImport Replace (targetSlot) frees the target + injects there, leaving other songs intact", () => {
  let sav = savFrom({});
  sav = injectSong(sav, 0, "KEEP", 1, rawSong(111))!;
  sav = injectSong(sav, 5, "OLD", 2, rawSong(222))!;
  const keep = decompressSlot(sav, 0)!;
  const plan = planLsdprjImport({ file: lsdprjWithKit(), path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: sav, targetSlot: 5 })!;
  expect(plan.songSlot).toBe(5);
  expect(listProjects(plan.savBytes).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:KEEP", "5:MYPRJ"]); // OLD replaced, KEEP intact
  expect(sameBytes(decompressSlot(plan.savBytes, 0)!, keep)).toBe(true); // KEEP byte-identical
});

// --- importSongFiles failure paths (store orchestration — no clobber on skip / partial / write-fail) -----

test("importSongFiles skips a .lsdprj when the base ROM can't be read (no reboot, no clobber)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({}));
  be.seed("/s/b.lsdprj", lsdprjWithKit());
  const sys = store.view().find((s) => s.id === id)!;
  const before = be.constructCalls.length;
  const orig = be.readFile.bind(be);
  be.readFile = (p: string) => (p === "/roms/song.gb" ? null : orig(p)); // the ROM becomes unreadable
  expect(importSongFiles(be, store, sys, ["/s/b.lsdprj"])).toBe(false); // .lsdprj needs the ROM → skipped → nothing applied
  expect(be.constructCalls.length).toBe(before); // no cold-boot
});

test("importSongFiles applies the valid song and skips a malformed one in the same batch (one reboot)", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({}));
  const good = rawSong(90);
  be.seed("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, good));
  be.seed("/s/bad.lsdprj", new Uint8Array([9, 9, 9])); // malformed .lsdprj
  const sys = store.view().find((s) => s.id === id)!;
  expect(importSongFiles(be, store, sys, ["/s/good.lsdsng", "/s/bad.lsdprj"])).toBe(true); // the good one still lands
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(listProjects(spec.sramBytes!).map((p) => p.name)).toEqual(["GOOD"]); // only the good song; the bad file corrupted nothing
  expect(sameBytes(decompressSlot(spec.sramBytes!, 0)!, good)).toBe(true);
});

test("importSongFiles: a failed atomic write aborts cleanly — false, no reboot, nothing on disk", () => {
  const { be, store } = newStore();
  be.seed("/roms/song.gb", lsdjRom1M());
  const id = store.addSystem("/roms/song.gb")!;
  be.setSram(id, savFrom({}));
  be.seed("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, rawSong(90)));
  const sys = store.view().find((s) => s.id === id)!;
  const before = be.constructCalls.length;
  be.writeFileAtomic = () => false; // the disk write fails
  expect(importSongFiles(be, store, sys, ["/s/good.lsdsng"])).toBe(false);
  expect(be.constructCalls.length).toBe(before); // no cold-boot from a failed write
  expect(be.readFile("/roms/song.sav")).toBe(null); // nothing was written to disk
  // the lsdj-assets role config was never touched (no override written)
  expect(readOverrides(store.view().find((s) => s.id === id)?.roles.find((r) => r.kind === "lsdj-assets")?.config).length).toBe(0);
});
