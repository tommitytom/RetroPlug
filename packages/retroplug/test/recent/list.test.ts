// Pure recent-list logic: prepend / dedupe / cap / name-preservation, plus
// remove / relink / label. The dedupe key is path + SONG, so one project holds a row
// per song it has had loaded. No backend - these operate on plain arrays
// of already-canonicalized entries, so they're exhaustively testable in
// isolation.
import { test, expect } from "../../testing/harness";
import {
  addEntry,
  removeEntry,
  relinkEntry,
  entryKey,
  label,
  type RecentEntry,
} from "../../src/recentList";

const e = (path: string, name = "", song?: string): RecentEntry => (song === undefined ? { path, name } : { path, name, song });
const paths = (list: RecentEntry[]): string[] => list.map((x) => x.path);
const keys = (list: RecentEntry[]): string[] => list.map((x) => entryKey(x.path, x.song));

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

test("add: a project holds ONE row per song; the same song again just moves its row up", () => {
  let l = addEntry([], "/a.rplg", "cart", "GRUB");
  l = addEntry(l, "/a.rplg", "cart", "INTRO"); // a different song of the SAME project -> its own row
  expect(keys(l)).toEqual([entryKey("/a.rplg", "INTRO"), entryKey("/a.rplg", "GRUB")]);

  l = addEntry(l, "/a.rplg", "cart", "GRUB"); // back to GRUB -> moved up, NOT duplicated
  expect(keys(l)).toEqual([entryKey("/a.rplg", "GRUB"), entryKey("/a.rplg", "INTRO")]);
  expect(l.length).toBe(2);
});

test("add: a song row supersedes the project's songless row - the two never coexist", () => {
  // The songless row is the placeholder for a project that has had no song loaded (a cart that booted
  // blank). The moment the project has a song row, the placeholder has nothing to say that the song
  // rows do not, and showing both is what read as "it added smsggdj.sav without listing the song".
  let l = addEntry([], "/a.rplg", "cart"); // opened blank
  l = addEntry(l, "/other.rplg", "other"); // an unrelated project's songless row stays
  l = addEntry(l, "/a.rplg", "cart", "DEMO"); // ...then DEMO is loaded
  expect(keys(l)).toEqual([entryKey("/a.rplg", "DEMO"), entryKey("/other.rplg")]);

  // Once song rows exist, opening the project blank again adds nothing: it is listed, and "open blank" is
  // not a thing to reopen. Order and names are untouched (no write follows from an unchanged list).
  const before = l;
  l = addEntry(l, "/a.rplg", "cart");
  expect(l).toEqual(before);
  l = addEntry(l, "/a.rplg", "renamed"); // not even a rename reaches through the no-op
  expect(l).toEqual(before);

  // A second song row drops nothing but the placeholder - which is already gone - and the other project's
  // songless row is not this project's to drop.
  l = addEntry(l, "/a.rplg", "cart", "INTRO");
  expect(keys(l)).toEqual([entryKey("/a.rplg", "INTRO"), entryKey("/a.rplg", "DEMO"), entryKey("/other.rplg")]);
});

test("add: the supersede is per path - a songless row survives song rows of OTHER projects", () => {
  let l = addEntry([], "/blank.rplg", "blank");
  l = addEntry(l, "/a.rplg", "cart", "DEMO");
  l = addEntry(l, "/a.rplg", "cart", "INTRO");
  expect(keys(l)).toEqual([entryKey("/a.rplg", "INTRO"), entryKey("/a.rplg", "DEMO"), entryKey("/blank.rplg")]);
  // ...and a non-tracker project, which only ever records songless rows, keeps its one row as before.
  l = addEntry(l, "/blank.rplg", "blank");
  expect(keys(l)).toEqual([entryKey("/blank.rplg"), entryKey("/a.rplg", "INTRO"), entryKey("/a.rplg", "DEMO")]);
  expect(l.length).toBe(3);
});

test("add: the supersede counts toward the cap like any other drop", () => {
  // Three rows, cap 3: the placeholder goes because it is superseded, not because it is oldest - so the
  // oldest unrelated row survives where a plain prepend would have pushed it out.
  let l: RecentEntry[] = [];
  l = addEntry(l, "/old.rplg", "old", undefined, 3);
  l = addEntry(l, "/x.rplg", "x", undefined, 3);
  l = addEntry(l, "/a.rplg", "cart", undefined, 3); // the placeholder, newest
  l = addEntry(l, "/a.rplg", "cart", "DEMO", 3);
  expect(keys(l)).toEqual([entryKey("/a.rplg", "DEMO"), entryKey("/x.rplg"), entryKey("/old.rplg")]);
});

test("remove + relink leave the supersede rule's results alone", () => {
  // remove takes out exactly the row it names; a relink carries song rows AND a songless row alike, and
  // neither reintroduces a placeholder next to song rows.
  let l = addEntry([], "/a.rplg", "cart", "DEMO");
  l = addEntry(l, "/b.rplg", "b"); // newest first: [/b, /a DEMO]
  expect(keys(removeEntry(l, "/a.rplg", "DEMO"))).toEqual([entryKey("/b.rplg")]);
  expect(keys(removeEntry(l, "/a.rplg"))).toEqual(keys(l)); // no songless row of /a to remove
  const moved = relinkEntry(l, "/a.rplg", "/moved.rplg"); // in place: the order is kept
  expect(keys(moved)).toEqual([entryKey("/b.rplg"), entryKey("/moved.rplg", "DEMO")]);
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

test("add: the name is per-row - an empty one keeps what that row was recorded with", () => {
  let l = addEntry([], "/a", "cart", "INTRO");
  expect(l[0].song).toBe("INTRO");
  l = addEntry(l, "/a", "", "INTRO"); // same row, no name -> keeps "cart"
  expect(l[0].name).toBe("cart");
  l = addEntry(l, "/a", "Renamed", "INTRO"); // explicit name -> overrides
  expect(l[0].name).toBe("Renamed");
});

test("add: the cap counts song rows too - the oldest row goes, whatever project it belongs to", () => {
  let l: RecentEntry[] = [];
  l = addEntry(l, "/old.rplg", "old", undefined, 3);
  for (const song of ["A", "B", "C"]) l = addEntry(l, "/a.rplg", "cart", song, 3);
  expect(keys(l)).toEqual([entryKey("/a.rplg", "C"), entryKey("/a.rplg", "B"), entryKey("/a.rplg", "A")]);
});

test("remove: drops the entry, leaves the rest", () => {
  let l = [e("/a"), e("/b"), e("/c")];
  l = removeEntry(l, "/b");
  expect(paths(l)).toEqual(["/a", "/c"]);
});

test("remove: takes out ONE song row, leaving that project's others", () => {
  let l = [e("/a", "cart", "GRUB"), e("/a", "cart", "INTRO"), e("/b")];
  l = removeEntry(l, "/a", "GRUB");
  expect(keys(l)).toEqual([entryKey("/a", "INTRO"), entryKey("/b")]);
  expect(removeEntry(l, "/a", "NOPE")).toEqual(l); // an unknown song is a no-op
});

test("relink: repoints in place, keeps position + name, absorbs a collision", () => {
  // /old sits in the middle with a name; /new already exists further down.
  let l = [e("/x"), e("/old", "Named"), e("/y"), e("/new")];
  l = relinkEntry(l, "/old", "/new");
  // /old -> /new in place (position 1, name kept); the later /new is dropped.
  expect(paths(l)).toEqual(["/x", "/new", "/y"]);
  expect(l[1].name).toBe("Named");
});

test("relink: repoints EVERY row of the moved project (one Locate fixes all its songs)", () => {
  let l = [e("/old", "cart", "GRUB"), e("/x"), e("/old", "cart", "INTRO"), e("/new", "cart", "GRUB")];
  l = relinkEntry(l, "/old", "/new");
  // Both /old rows follow the file, in place; the pre-existing /new+GRUB row collides and is dropped.
  expect(keys(l)).toEqual([entryKey("/new", "GRUB"), entryKey("/x"), entryKey("/new", "INTRO")]);
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
