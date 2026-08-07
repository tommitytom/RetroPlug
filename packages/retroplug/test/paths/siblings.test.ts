// Sibling .sav / .rplg derivation + the sav-suffix and ROM-pairing kernels.
// Ports of packages/native SramAutoSave.hpp (siblingPath/siblingSavPath/
// resolveSavPath), assignSavSuffix (PluginRpcService.cpp:133), and findSiblingRom's
// candidate enumeration (PluginRpcService.cpp:380). The suffix scan and candidate
// list are pure kernels: they take predicates / return an ordered list, and the
// systems domain wires them to the live systems + Backend later.
import { test, expect } from "../../testing/harness";
import {
  siblingPath,
  siblingSavPath,
  siblingSavCandidates,
  siblingRplgPath,
  resolveSavPath,
  nextFreeSavSuffix,
  siblingRomCandidates,
  isSavPath,
  SAV_PATTERNS,
} from "../../src/savPaths";

test("siblingSavPath: suffix 0/1 replace the extension, ≥2 disambiguate the filename", () => {
  expect(siblingSavPath("/d/game.gb")).toBe("/d/game.sav"); // default suffix 0
  expect(siblingSavPath("/d/game.gb", 0)).toBe("/d/game.sav");
  expect(siblingSavPath("/d/game.gb", 1)).toBe("/d/game.sav"); // 1 behaves like 0
  expect(siblingSavPath("/d/game.gb", 2)).toBe("/d/game-2.sav");
  expect(siblingSavPath("/d/game.gb", 3)).toBe("/d/game-3.sav");
  expect(siblingSavPath("")).toBe(""); // empty romPath → empty
});

test("siblingSavCandidates: .sav then .srm, honouring the suffix; isSavPath / SAV_PATTERNS accept both", () => {
  expect(siblingSavCandidates("/d/game.nes")).toEqual(["/d/game.sav", "/d/game.srm"]);
  expect(siblingSavCandidates("/d/game.nes", 2)).toEqual(["/d/game-2.sav", "/d/game-2.srm"]);
  expect(isSavPath("/d/game.SAV")).toBe(true); // case-insensitive
  expect(isSavPath("/d/game.srm")).toBe(true);
  expect(isSavPath("/d/game.nes")).toBe(false);
  expect(SAV_PATTERNS).toEqual(["*.sav", "*.srm"]);
});

test("siblingPath: the generic ext helper (also used for savestates)", () => {
  expect(siblingPath("/d/game.gb", 0, ".ss0")).toBe("/d/game.ss0");
  expect(siblingPath("/d/game.gb", 2, ".ss0")).toBe("/d/game-2.ss0");
  expect(siblingPath("", 2, ".ss0")).toBe("");
});

test("siblingRplgPath: replace the extension, suffix-independent", () => {
  expect(siblingRplgPath("/d/game.gb")).toBe("/d/game.rplg");
  expect(siblingRplgPath("/d/game.gbc")).toBe("/d/game.rplg");
  expect(siblingRplgPath("")).toBe("");
});

test("resolveSavPath: an explicit override wins, else the suffix sibling", () => {
  expect(resolveSavPath("/d/game.gb", 0, "")).toBe("/d/game.sav");
  expect(resolveSavPath("/d/game.gb", 2, "")).toBe("/d/game-2.sav");
  expect(resolveSavPath("/d/game.gb", 0, "/other/battery.sav")).toBe("/other/battery.sav");
});

test("nextFreeSavSuffix: reclaim slot 0 when unowned, ignoring any file on disk", () => {
  // Nobody owns 0 → return 0, even though a <rom>.sav already exists on disk.
  expect(nextFreeSavSuffix("/d/game.gb", () => false, () => true)).toBe(0);
  // Empty romPath → 0.
  expect(nextFreeSavSuffix("", () => true, () => true)).toBe(0);
});

test("nextFreeSavSuffix: skip to 2, then grow past owned + on-disk slots", () => {
  const owned = (set: number[]) => (n: number) => set.includes(n);
  const onDisk = (set: number[]) => (n: number) => set.includes(n);

  // 0 owned, 2 free → 2 (never 1).
  expect(nextFreeSavSuffix("/d/game.gb", owned([0]), () => false)).toBe(2);
  // 0 owned, 2 has an orphaned file on disk → 3.
  expect(nextFreeSavSuffix("/d/game.gb", owned([0]), onDisk([2]))).toBe(3);
  // 0 and 2 owned, 3 on disk → 4.
  expect(nextFreeSavSuffix("/d/game.gb", owned([0, 2]), onDisk([3]))).toBe(4);
});

test("siblingRomCandidates: exact stem first, every ROM extension in order", () => {
  expect(siblingRomCandidates("/roms/game.sav")).toEqual([
    "/roms/game.gb",
    "/roms/game.gbc",
    "/roms/game.gba",
    "/roms/game.nes",
    "/roms/game.sms",
    "/roms/game.gg",
  ]);
});

test("siblingRomCandidates: a -<digits> slot also probes the base stem, after the exact one", () => {
  expect(siblingRomCandidates("/roms/game-2.sav")).toEqual([
    "/roms/game-2.gb",
    "/roms/game-2.gbc",
    "/roms/game-2.gba",
    "/roms/game-2.nes",
    "/roms/game-2.sms",
    "/roms/game-2.gg",
    "/roms/game.gb",
    "/roms/game.gbc",
    "/roms/game.gba",
    "/roms/game.nes",
    "/roms/game.sms",
    "/roms/game.gg",
  ]);
});

test("siblingRomCandidates: only a purely-numeric suffix adds the base stem", () => {
  // trailing part isn't all digits → no base stem
  expect(siblingRomCandidates("/roms/game-2a.sav")).toEqual([
    "/roms/game-2a.gb",
    "/roms/game-2a.gbc",
    "/roms/game-2a.gba",
    "/roms/game-2a.nes",
    "/roms/game-2a.sms",
    "/roms/game-2a.gg",
  ]);
  // dangling dash (nothing after it) → no base stem
  expect(siblingRomCandidates("/roms/game-.sav")).toEqual([
    "/roms/game-.gb",
    "/roms/game-.gbc",
    "/roms/game-.gba",
    "/roms/game-.nes",
    "/roms/game-.sms",
    "/roms/game-.gg",
  ]);
});

test("siblingRomCandidates: no directory yields bare relative candidates", () => {
  expect(siblingRomCandidates("game.sav")).toEqual([
    "game.gb",
    "game.gbc",
    "game.gba",
    "game.nes",
    "game.sms",
    "game.gg",
  ]);
});
