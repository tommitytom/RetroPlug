// The SD Card Files panel's state and listing (ts/ui/sdCard.ts): what it lists of a card folder and in what order, and
// the open list following a system through the rebuild a new card makes.
import { test, expect } from "@retroplug/testing";
import { sdCardEntries, sdFilesOpen, sdFilesShown, setSdFilesShown } from "../ui/sdCard";

/** A card as a map of folder -> names, listDir's way: a subfolder carries a trailing slash, in no set order. */
function card(tree: Record<string, string[]>) {
  return { listDir: (dir: string) => tree[dir] ?? [] };
}

test("folders first, then files, each by name ignoring case, a folder's entries indented under it", () => {
  const files = card({
    "/sd": ["zeta.bin", "nesvj/", "Alpha.txt", "beta/", ".Trashes/", ".DS_Store"],
    "/sd/nesvj": ["02-tone.raw", "01-rick.raw", "deeper/"],
    "/sd/beta": [],
    "/sd/nesvj/deeper": ["never.raw"],
  });
  const lines = sdCardEntries(files, "/sd").map((e) => `${e.depth}:${e.name}${e.dir ? "/" : ""}`);
  // Hidden names are left out, and the listing goes one folder deep (nesvj/deeper/ is named, not opened).
  expect(lines).toEqual([
    "0:beta/",
    "0:nesvj/",
    "1:deeper/",
    "1:01-rick.raw",
    "1:02-tone.raw",
    "0:Alpha.txt",
    "0:zeta.bin",
  ]);
});

test("a root given with a trailing slash reads its folders at the same paths", () => {
  const files = card({ "/sd": ["nesvj/"], "/sd/nesvj": ["a.raw"] });
  expect(sdCardEntries(files, "/sd/").map((e) => e.name)).toEqual(["nesvj", "a.raw"]);
});

test("a folder that cannot be read lists nothing", () => {
  expect(sdCardEntries(card({}), "/missing")).toEqual([]);
});

test("the open list follows a rebuilt system to its new id, and drops a system that is gone", () => {
  sdFilesOpen.set([]);
  // System 3 was rebuilt as 8 (a new card); system 5 was removed.
  const successors: Record<number, number | null> = { 3: 8, 5: null };
  const systems = { currentId: (id: number) => (id in successors ? successors[id] : id) };
  sdFilesOpen.set([3, 5]);
  expect(sdFilesShown(systems, 8)).toBe(true);
  expect(sdFilesShown(systems, 3)).toBe(false);
  setSdFilesShown(systems, 11, true);
  expect([...sdFilesOpen.get()]).toEqual([8, 11]);
  setSdFilesShown(systems, 8, false);
  expect([...sdFilesOpen.get()]).toEqual([11]);
  sdFilesOpen.set([]);
});
