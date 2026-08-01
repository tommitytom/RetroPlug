// Pure recent-list logic: prepend / dedupe / cap / name-preservation, plus
// remove / relink / label. No backend - these operate on plain arrays
// of already-canonicalized entries, so they're exhaustively testable in
// isolation.
import { test, expect } from "../../testing/harness";
import {
  addEntry,
  removeEntry,
  relinkEntry,
  label,
  type RecentEntry,
} from "../../src/recentList";

const e = (path: string, name = ""): RecentEntry => ({ path, name });
const paths = (list: RecentEntry[]): string[] => list.map((x) => x.path);

test("add: prepends and keeps most-recent-first", () => {
  let l: RecentEntry[] = [];
  l = addEntry(l, "/a", "");
  l = addEntry(l, "/b", "");
  l = addEntry(l, "/c", "");
  expect(paths(l)).toEqual(["/c", "/b", "/a"]);
});

test("add: re-adding an existing path moves it to the front (dedupe)", () => {
  let l = [e("/c"), e("/b"), e("/a")];
  l = addEntry(l, "/a", "");
  expect(paths(l)).toEqual(["/a", "/c", "/b"]);
  expect(l.length).toBe(3); // no duplicate
});

test("add: caps at max, dropping the oldest", () => {
  let l: RecentEntry[] = [];
  for (let i = 0; i < 12; i++) l = addEntry(l, `/p${i}`, "", undefined, 10);
  expect(l.length).toBe(10);
  expect(l[0].path).toBe("/p11"); // newest
  expect(l[9].path).toBe("/p2"); // /p0, /p1 dropped
});

test("add: preserves an existing name on re-add unless a new one is given", () => {
  let l = [e("/a", "My Song")];
  l = addEntry(l, "/a", ""); // no name -> keep what was recorded
  expect(l[0].name).toBe("My Song");
  l = addEntry(l, "/a", "Renamed"); // explicit name -> overrides
  expect(l[0].name).toBe("Renamed");
});

test("add: sets the working-song label; undefined on re-add preserves it", () => {
  let l = addEntry([], "/a", "", "INTRO");
  expect(l[0].song).toBe("INTRO");
  l = addEntry(l, "/a", "Named"); // no song arg → keep the existing song
  expect(l[0].song).toBe("INTRO");
  expect(l[0].name).toBe("Named");
  l = addEntry(l, "/a", "", "OUTRO"); // a fresh song value overrides
  expect(l[0].song).toBe("OUTRO");
});

test("remove: drops the entry, leaves the rest", () => {
  let l = [e("/a"), e("/b"), e("/c")];
  l = removeEntry(l, "/b");
  expect(paths(l)).toEqual(["/a", "/c"]);
});

test("relink: repoints in place, keeps position + name, absorbs a collision", () => {
  // /old sits in the middle with a name; /new already exists further down.
  let l = [e("/x"), e("/old", "Named"), e("/y"), e("/new")];
  l = relinkEntry(l, "/old", "/new");
  // /old -> /new in place (position 1, name kept); the later /new is dropped.
  expect(paths(l)).toEqual(["/x", "/new", "/y"]);
  expect(l[1].name).toBe("Named");
});

test("relink: a missing source leaves the list untouched", () => {
  const l = [e("/a"), e("/b")];
  expect(relinkEntry(l, "/nope", "/c")).toEqual(l);
});

test("label: the recorded name wins, else the basename with the project extension stripped", () => {
  expect(label(e("/music/song.rplg", "Nice Name"))).toBe("Nice Name");
  expect(label(e("/music/song.rplg", "  "))).toBe("song"); // blank name -> basename, .rplg stripped
  expect(label(e("/music/song.rplg"))).toBe("song");
  expect(label(e("/music/song.rplg.zip"))).toBe("song"); // exported project extension too
  expect(label(e("/music/mixtape.RPLG"))).toBe("mixtape"); // case-insensitive
  expect(label(e("/roms/game.gb"))).toBe("game.gb"); // non-project names are left intact
});
