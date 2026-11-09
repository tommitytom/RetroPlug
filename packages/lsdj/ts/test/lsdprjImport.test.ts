// The .lsdprj import PLANNER (planLsdprjImport): song injected byte-level, a missing kit assigned to a free ROM slot
// and recorded as an override linking the .lsdprj by path+ordinal, and every failure path returning null with its
// inputs untouched - the caller then writes nothing. The store-driven role path (a lsdprjKit override patched into
// the effective ROM at construct) is the shell's, test/systems/lsdj-lsdprj.test.ts: it needs the systems store.
import { test, expect } from "@retroplug/testing";
import { sameBytes } from "@retroplug/testing/bytes";
import { savFrom, decodeSav, encodeLsdprj, injectSong, listProjects, decompressSlot } from "..";
import { planLsdprjImport } from "../lsdjLsdprjImport";
import { LsdjRom, buildKitBank, ROM_SIZE, KIT_COUNT } from "../rom";
import type { LsdjAssetOverride } from "../lsdjAssetsRole";

/** A minimal 1 MiB image LsdjRom accepts (GB logo + a version title); all kit slots start free. */
function lsdjRom1M(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

/** A GB cartridge header with the logo at 0x104 (what classifies it) and a battery (MBC1+RAM+BATTERY, 8 KB SRAM). */
function gbRomBattery(): Uint8Array {
  const b = new Uint8Array(0x8000);
  b.set(GB_LOGO, 0x104);
  b[0x147] = 0x03;
  b[0x149] = 0x02;
  return b;
}

// The Game Boy's Nintendo logo, which is what classifies a buffer as a Game Boy ROM.
const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];

// A .lsdprj whose song uses kit index 5, carrying one custom kit bank ("DRUMS").
function lsdprjWithKit(): { file: Uint8Array; kit: Uint8Array } {
  const song = savFrom({ workingSong: { instruments: [{ type: "kit", kit1: 5, kit2: 5 }] } }).subarray(0, 0x8000);
  const kit = buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }]);
  return { file: encodeLsdprj({ name: "MYPRJ", version: 0x40, songBytes: song, kitBanks: [kit] }), kit };
}
const rawSong = (tempo: number) => savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();

test("planLsdprjImport assigns a missing kit to a free slot + injects the song", () => {
  const { file } = lsdprjWithKit();
  const plan = planLsdprjImport({ file, path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: savFrom({}) })!;
  expect(plan != null).toBeTruthy();
  expect(plan.addedKits).toBe(1);
  expect(plan.songSlot).toBe(0);
  const ov = plan.overrides[0];
  expect(ov.type).toBe("kit");
  expect(ov.path).toBe("/p.lsdprj");
  expect(ov.lsdprjKit).toBe(0);
  expect(ov.name).toBe("DRUMS");
  expect(decodeSav(plan.savBytes).projects[0]!.name).toBe("MYPRJ"); // song injected
});

test("planLsdprjImport dedupes a kit already present in the ROM (no new override)", () => {
  const { file, kit } = lsdprjWithKit();
  const rom = LsdjRom.fromBytes(lsdjRom1M());
  rom.importKitFile(3, kit); // the same kit already lives at slot 3
  const plan = planLsdprjImport({ file, path: "/p.lsdprj", effectiveRom: rom.bytes(), overrides: [], liveSram: savFrom({}) })!;
  expect(plan.addedKits).toBe(0); // reused slot 3, no new override
  expect(plan.overrides.length).toBe(0);
});

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
  expect(planLsdprjImport({ file: lsdprjWithKit().file, path: "/p.lsdprj", effectiveRom: notLsdj, overrides: [], liveSram: savFrom({}) })).toBe(null);
});

test("planLsdprjImport returns null when the ROM is out of free kit slots", () => {
  const rom = LsdjRom.fromBytes(lsdjRom1M());
  for (let k = 0; k < KIT_COUNT; k++) rom.importKitFile(k, buildKitBank(`K${k}`, [{ name: "X", bytes: Uint8Array.of(k + 1) }])); // fill all 51
  expect(planLsdprjImport({ file: lsdprjWithKit().file, path: "/p.lsdprj", effectiveRom: rom.bytes(), overrides: [], liveSram: savFrom({}) })).toBe(null);
});

test("planLsdprjImport returns null when the SAV is out of song slots (no targetSlot)", () => {
  let full = savFrom({});
  for (let i = 0; i < 32; i++) full = injectSong(full, i, `S${i}`, 1, rawSong(60))!; // fill all 32 slots
  const before = full.slice();
  expect(planLsdprjImport({ file: lsdprjWithKit().file, path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: full })).toBe(null);
  expect(sameBytes(full, before)).toBe(true); // the full SAV is untouched
});

test("planLsdprjImport Replace (targetSlot) frees the target + injects there, leaving other songs intact", () => {
  let sav = savFrom({});
  sav = injectSong(sav, 0, "KEEP", 1, rawSong(111))!;
  sav = injectSong(sav, 5, "OLD", 2, rawSong(222))!;
  const keep = decompressSlot(sav, 0)!;
  const plan = planLsdprjImport({ file: lsdprjWithKit().file, path: "/p.lsdprj", effectiveRom: lsdjRom1M(), overrides: [], liveSram: sav, targetSlot: 5 })!;
  expect(plan.songSlot).toBe(5);
  expect(listProjects(plan.savBytes).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:KEEP", "5:MYPRJ"]); // OLD replaced, KEEP intact
  expect(sameBytes(decompressSlot(plan.savBytes, 0)!, keep)).toBe(true); // KEEP byte-identical
});
