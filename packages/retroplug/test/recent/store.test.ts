// RecentStore: the integration layer that ties the pure list + serialization to
// the Backend — canonicalizes inputs, computes missing flags from fileExists,
// persists to <configDir>/recent.json atomically, and fires onChange. Tested
// entirely against the in-memory MockBackend.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { serializeRecent } from "../../src/recentSerialization";

const RECENT = "/cfg/recent.json";

function newStore(seedFiles: Record<string, string> = {}) {
  const be = new MockBackend("/cfg");
  for (const [p, c] of Object.entries(seedFiles)) be.seed(p, c);
  let changes = 0;
  const store = new RecentStore(be, () => { changes++; });
  return { be, store, changes: () => changes };
}

test("load: reads recent.json into memory", () => {
  const json = serializeRecent([{ path: "/proj/a.rplg", name: "A" }]);
  const { be, store } = newStore({ [RECENT]: json });
  be.seed("/proj/a.rplg", "x"); // exists on disk -> not missing
  store.load();
  const view = store.view();
  expect(view.length).toBe(1);
  expect(view[0].path).toBe("/proj/a.rplg");
  expect(view[0].label).toBe("A");
  expect(view[0].missing).toBeFalsy();
});

test("load: a missing recent.json yields an empty list (no throw)", () => {
  const { store } = newStore();
  store.load();
  expect(store.view()).toEqual([]);
});

test("add: persists recent.json atomically and fires onChange", () => {
  const { be, store, changes } = newStore();
  store.load();
  expect(store.add("/proj/song.rplg", "")).toBeTruthy();
  // persisted to the config dir, via the atomic writer
  const onDisk = be.readText(RECENT);
  expect(onDisk).toBe(serializeRecent([{ path: "/proj/song.rplg", name: "" }]));
  expect(be.log.includes("writeFileAtomic")).toBeTruthy();
  expect(changes()).toBe(1);
});

test("add: canonicalizes so ./ and absolute forms dedupe to one entry", () => {
  const { store } = newStore();
  store.load();
  store.add("/proj/a.rplg", "");
  store.add("/proj/./sub/../a.rplg", ""); // same file, non-canonical
  const view = store.view();
  expect(view.length).toBe(1);
  expect(view[0].path).toBe("/proj/a.rplg");
});

test("view: computes the missing flag from the backend per call", () => {
  const { be, store } = newStore();
  store.load();
  be.seed("/proj/here.rplg", "x");
  store.add("/proj/here.rplg", "");
  store.add("/proj/gone.rplg", "");
  const byPath = Object.fromEntries(store.view().map((v) => [v.path, v.missing]));
  expect(byPath["/proj/here.rplg"]).toBeFalsy();
  expect(byPath["/proj/gone.rplg"]).toBeTruthy();
});

test("remove: found removes + persists + notifies; absent is a no-op", () => {
  const { be, store, changes } = newStore();
  store.load();
  store.add("/a.rplg", "");
  store.add("/b.rplg", "");
  const before = changes();
  expect(store.remove("/a.rplg")).toBeTruthy();
  expect(store.view().map((v) => v.path)).toEqual(["/b.rplg"]);
  expect(changes()).toBe(before + 1);
  // absent -> false, no write, no notify
  const writes = () => be.log.filter((m) => m === "writeFileAtomic").length;
  const writesBefore = writes();
  const afterChanges = changes();
  expect(store.remove("/nope.rplg")).toBeFalsy();
  expect(changes()).toBe(afterChanges); // no notify
  expect(writes()).toBe(writesBefore); // no write
});

test("add: one row per song, and re-adding the current song is a genuine no-op (no write, no notify)", () => {
  const { be, store, changes } = newStore();
  store.load();
  expect(store.add("/proj/a.rplg", "cart", "GRUB")).toBeTruthy();
  expect(store.add("/proj/a.rplg", "cart", "INTRO")).toBeTruthy(); // same project, second song
  expect(store.view().map((v) => v.song)).toEqual(["INTRO", "GRUB"]);

  // The song watcher calls add on a timer: while nothing changed it must not write or notify.
  const writes = () => be.log.filter((m) => m === "writeFileAtomic").length;
  const [writesBefore, changesBefore] = [writes(), changes()];
  expect(store.add("/proj/a.rplg", "cart", "INTRO")).toBeFalsy();
  expect(writes()).toBe(writesBefore);
  expect(changes()).toBe(changesBefore);
});

test("remove: drops one song row of a project, leaving its others", () => {
  const { store } = newStore();
  store.load();
  store.add("/proj/a.rplg", "cart", "GRUB");
  store.add("/proj/a.rplg", "cart", "INTRO");
  expect(store.remove("/proj/a.rplg", "GRUB")).toBeTruthy();
  expect(store.view().map((v) => v.song)).toEqual(["INTRO"]);
  expect(store.remove("/proj/a.rplg", "GRUB")).toBeFalsy(); // already gone
  expect(store.remove("/proj/a.rplg")).toBeFalsy(); // the songless row was never there
});

test("relink: repoints a moved project + persists; absent is a no-op", () => {
  const { be, store } = newStore();
  store.load();
  store.add("/old/song.rplg", "Keep");
  be.seed("/new/song.rplg", "x");
  expect(store.relink("/old/song.rplg", "/new/song.rplg")).toBeTruthy();
  const v = store.view();
  expect(v[0].path).toBe("/new/song.rplg");
  expect(v[0].label).toBe("Keep"); // the recorded name survives the relink
  expect(v[0].missing).toBeFalsy();
  expect(store.relink("/nope.rplg", "/x.rplg")).toBeFalsy();
});
