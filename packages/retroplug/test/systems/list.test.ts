// Pure systems-list logic: the immutable ordering transforms + the decision kernels
// (suffix ownership, the paired-sav override test, sibling-ROM pairing, focus
// fallback). No Backend — every rule is unit-testable in isolation, exactly as the
// store will compose them. Ports of the native list ordering (PluginDSP.cpp:406-458),
// the assignSavSuffix ownership predicate, buildSystemFromPath's weakly_canonical
// override test, and findSiblingRom's candidate loop.
import { test, expect } from "../../testing/harness";
import {
  type SystemEntry,
  findById,
  appendEntry,
  removeById,
  replaceById,
  isSuffixOwned,
  resolveSavOverride,
  pickSiblingRom,
  nextFocusAfterRemove,
} from "../../src/systemsList";

function entry(id: number, romPath: string, savSuffix = 0, savPath = ""): SystemEntry {
  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom: "" };
}

test("findById / appendEntry: append keeps order and returns a new list", () => {
  const a = entry(1, "/a.gb");
  const list = appendEntry([a], entry(2, "/b.gb"));
  expect(list.map((e) => e.id)).toEqual([1, 2]);
  expect(findById(list, 2)?.romPath).toBe("/b.gb");
  expect(findById(list, 9)).toBe(undefined);
});

test("removeById: drops by id, survivors keep ids + relative order", () => {
  const list = [entry(1, "/a.gb"), entry(2, "/b.gb"), entry(3, "/c.gb")];
  expect(removeById(list, 2).map((e) => e.id)).toEqual([1, 3]);
  expect(removeById(list, 9).map((e) => e.id)).toEqual([1, 2, 3]); // absent: unchanged
});

test("replaceById: swaps in place, preserving the slot index", () => {
  const list = [entry(1, "/a.gb"), entry(2, "/b.gb"), entry(3, "/c.gb")];
  const next = replaceById(list, 2, entry(9, "/new.gb"));
  expect(next.map((e) => e.id)).toEqual([1, 9, 3]); // same position, new id
  expect(findById(next, 9)?.romPath).toBe("/new.gb");
  expect(replaceById(list, 42, entry(9, "/x.gb"))).toBe(list); // absent: same ref
});

test("isSuffixOwned: true when a live system with this rom holds the suffix", () => {
  const list = [entry(1, "/game.gb", 0), entry(2, "/game.gb", 2), entry(3, "/other.gb", 0)];
  expect(isSuffixOwned(list, "/game.gb", 0)).toBeTruthy();
  expect(isSuffixOwned(list, "/game.gb", 2)).toBeTruthy();
  expect(isSuffixOwned(list, "/game.gb", 3)).toBeFalsy();
  expect(isSuffixOwned(list, "/other.gb", 2)).toBeFalsy();
});

test("resolveSavOverride: the natural sibling is NOT an override; a different file IS", () => {
  const canon = (p: string) => p; // inputs are already canonical here
  // picked == the suffix-0 sibling → no override
  expect(resolveSavOverride("/d/game.gb", 0, "/d/game.sav", canon)).toBe("");
  // picked == the suffix-N sibling → no override
  expect(resolveSavOverride("/d/game.gb", 2, "/d/game-2.sav", canon)).toBe("");
  // picked == the plain suffix-0 sibling even while suffix is 2 → still no override
  expect(resolveSavOverride("/d/game.gb", 2, "/d/game.sav", canon)).toBe("");
  // a genuinely different file → override kept (the raw path)
  expect(resolveSavOverride("/d/game.gb", 0, "/other/mine.sav", canon)).toBe("/other/mine.sav");
  // empty pick → no override
  expect(resolveSavOverride("/d/game.gb", 0, "", canon)).toBe("");
});

test("resolveSavOverride: applies canonicalization before comparing", () => {
  // A non-canonical spelling of the sibling still counts as the sibling (no override).
  const canon = (p: string) => p.replace(/\/[^/]+\/\.\.\//g, "/").replace(/\/\.\//g, "/");
  expect(resolveSavOverride("/d/game.gb", 0, "/d/sub/../game.sav", canon)).toBe("");
});

test("pickSiblingRom: first candidate that exists AND classifies as a real ROM", () => {
  // game.gb missing, game.gbc present but not a ROM, game.gba present + valid → picks it.
  const present = new Set(["/roms/game.gbc", "/roms/game.gba"]);
  const kinds: Record<string, string> = { "/roms/game.gbc": "unknown", "/roms/game.gba": "gba" };
  const got = pickSiblingRom(
    "/roms/game.sav",
    (p) => present.has(p),
    (p) => (kinds[p] ?? "unknown") as never,
  );
  expect(got).toBe("/roms/game.gba");
});

test("pickSiblingRom: null when no candidate both exists and validates", () => {
  expect(pickSiblingRom("/roms/game.sav", () => false, () => "unknown" as never)).toBe(null);
  // exists but every candidate is an unknown format
  expect(pickSiblingRom("/roms/game.sav", () => true, () => "unknown" as never)).toBe(null);
});

test("nextFocusAfterRemove: refocus the new front only when the focused one went away", () => {
  const remaining = [entry(2, "/b.gb"), entry(3, "/c.gb")];
  expect(nextFocusAfterRemove(remaining, 1, 1)).toBe(2); // removed focused → front of remainder
  expect(nextFocusAfterRemove(remaining, 1, 3)).toBe(3); // removed a non-focused → focus unchanged
  expect(nextFocusAfterRemove([], 1, 1)).toBe(0); // last one removed → no focus
});
